#include "rl0/vcomp0.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/boundary.h"
#include "core/attach.h"
#include "core/lattice.h"
#include "rl0/boundary0.h"
#include "rl0/attach0.h"
#include "core/hash.h"
#include "throughput/vcomp.h"

static void sv_init(VCompStateVec *v) {
    v->data = NULL;
    v->count = 0;
    v->cap = 0;
}

static void sv_destroy(VCompStateVec *v) { free(v->data); }

static void sv_push(VCompStateVec *v, const VCompState *s) {
    if (v->count == v->cap) {
        size_t nc = v->cap ? 2 * v->cap : 64;
        v->data = realloc(v->data, nc * sizeof(*v->data));
        if (!v->data) {
            fprintf(stderr, "oom\n");
            exit(1);
        }
        v->cap = nc;
    }
    v->data[v->count++] = *s;
}

static int coord_cmp(const void *A, const void *B) {
    const Coord *a = A;
    const Coord *b = B;
    if (a->v != b->v) return a->v - b->v;
    if (a->x != b->x) return a->x - b->x;
    return a->y - b->y;
}

static int poly_equal_local(const Poly *a, const Poly *b) {
    if (a->cycle_count != b->cycle_count) return 0;
    for (int k = 0; k < a->cycle_count; k++) {
        const Cycle *ca = &a->cycles[k];
        const Cycle *cb = &b->cycles[k];
        if (ca->n != cb->n) return 0;
        for (int i = 0; i < ca->n; i++) {
            if (ca->v[i].v != cb->v[i].v ||
                ca->v[i].x != cb->v[i].x ||
                ca->v[i].y != cb->v[i].y) {
                return 0;
            }
        }
    }
    return 1;
}

static int state_equal(const VCompState *a, const VCompState *b) {
    if (!poly_equal_local(&a->poly, &b->poly)) return 0;
    if (!a->use_ports_identity && !b->use_ports_identity) return 1;
    if (a->use_ports_identity != b->use_ports_identity) return 0;
    if (a->port_count != b->port_count) return 0;
    for (int i = 0; i < a->port_count; i++) {
        if (!coord_eq(a->ports[i], b->ports[i])) return 0;
    }
    return 1;
}

typedef struct {
    uint64_t hash;
    size_t index;
    int used;
} StateSetEntry;

typedef struct {
    StateSetEntry *data;
    size_t cap;
    size_t count;
} StateSet;

static size_t pow2_ge(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

static void stateset_init(StateSet *s, size_t cap_hint) {
    s->cap = pow2_ge(cap_hint < 128 ? 128 : cap_hint);
    s->count = 0;
    s->data = calloc(s->cap, sizeof(*s->data));
    if (!s->data) {
        fprintf(stderr, "oom\n");
        exit(1);
    }
}

static void stateset_destroy(StateSet *s) { free(s->data); }

static uint64_t mix_u64(uint64_t h, uint64_t x) {
    h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

static uint64_t poly_hash64(const Poly *p) {
    uint64_t h = 1469598103934665603ULL;
    h = mix_u64(h, (uint64_t)p->cycle_count);
    for (int c = 0; c < p->cycle_count; c++) {
        h = mix_u64(h, (uint64_t)p->cycles[c].n);
        for (int i = 0; i < p->cycles[c].n; i++) {
            const Coord *q = &p->cycles[c].v[i];
            h = mix_u64(h, (uint64_t)(uint32_t)q->v);
            h = mix_u64(h, (uint64_t)(uint32_t)q->x);
            h = mix_u64(h, (uint64_t)(uint32_t)q->y);
        }
    }
    return h;
}

static uint64_t state_hash64(const VCompState *s, const Poly *poly_key) {
    uint64_t h = poly_hash64(poly_key);
    if (!s->use_ports_identity) return h;
    h = mix_u64(h, (uint64_t)s->port_count);
    for (int i = 0; i < s->port_count; i++) {
        h = mix_u64(h, (uint64_t)(uint32_t)s->ports[i].v);
        h = mix_u64(h, (uint64_t)(uint32_t)s->ports[i].x);
        h = mix_u64(h, (uint64_t)(uint32_t)s->ports[i].y);
    }
    return h;
}

static void stateset_rehash(StateSet *s, size_t new_cap) {
    StateSetEntry *old = s->data;
    size_t old_cap = s->cap;
    s->data = calloc(new_cap, sizeof(*s->data));
    if (!s->data) {
        fprintf(stderr, "oom\n");
        exit(1);
    }
    s->cap = new_cap;
    s->count = 0;

    for (size_t i = 0; i < old_cap; i++) {
        if (!old[i].used) continue;
        size_t idx = (size_t)(old[i].hash & (s->cap - 1));
        while (s->data[idx].used) idx = (idx + 1) & (s->cap - 1);
        s->data[idx] = old[i];
        s->count++;
    }
    free(old);
}

static int stateset_insert(StateSet *s,
                           const VCompStateVec *v,
                           const VCompState *state,
                           uint64_t hash,
                           size_t index) {
    if ((s->count + 1) * 10 >= s->cap * 7) {
        stateset_rehash(s, s->cap * 2);
    }

    size_t slot = (size_t)(hash & (s->cap - 1));
    while (s->data[slot].used) {
        if (s->data[slot].hash == hash &&
            state_equal(&v->data[s->data[slot].index], state)) {
            return 0;
        }
        slot = (slot + 1) & (s->cap - 1);
    }

    s->data[slot].used = 1;
    s->data[slot].hash = hash;
    s->data[slot].index = index;
    s->count++;
    return 1;
}


static int coord_on_boundary0(const Poly *p, Coord q) {
    Coord verts[MAX_VERTS * MAX_CYCLES];
    int vc = build_boundary_vertices(p, verts);
    if (vc < 0) return 1;
    for (int i = 0; i < vc; i++) if (coord_eq(verts[i], q)) return 1;
    return 0;
}

static int try_prescribed_arc0(const VCompState *state,
                               const Tile *tile,
                               Coord target,
                               const RL0FMArc *forgotten) {
    Poly grown;
    Cycle out_tiles[ATTACH0_MAX_TILES];
    int out_tile_count = 0;
    if (forgotten->n <= 0) return 0;
    if (!attach0_try_attach_arc(&state->poly,
                                tile,
                                state->tiles,
                                state->tile_count,
                                target,
                                forgotten,
                                &grown,
                                out_tiles,
                                &out_tile_count,
                                NULL)) {
        return 0;
    }
    return !coord_on_boundary0(&grown, target);
}

static void probe_prescribed_growth0(const VCompState *state,
                                     const Tile *tile,
                                     const RL0ForgetMap *map,
                                     VComp0Stats *stats) {
    Coord verts[MAX_VERTS * MAX_CYCLES];
    int vc;
    if (!map || !stats || state->tile_count <= 0) return;
    vc = build_boundary_vertices(&state->poly, verts);
    if (vc < 0) return;

    for (int j = 0; j < vc; j++) {
        RL0FMArc arc;
        const RL0FMArc *values = NULL;
        int value_count = 0;
        RL0FMArc matched;
        if (!boundary0_build_vertex_arc(tile,
                                        state->tiles,
                                        state->tile_count,
                                        verts[j],
                                        &arc)) {
            continue;
        }
        if (arc.n < 2) continue;
        stats->prescribed_vertices++;
        if (!rl0_fm_lookup_any_rotation(map, &arc, &values, &value_count, &matched)) continue;
        for (int v = 0; v < value_count; v++) {
            if (values[v].n <= 0) continue;
            stats->prescribed_arcs++;
            if (try_prescribed_arc0(state, tile, verts[j], &values[v])) {
                stats->prescribed_successes++;
            }
        }
    }
}

static void ingest_raw0(const VCompRawState *raw,
                        const Tile *tile,
                        const RL0ForgetMap *map,
                        int max_level,
                        int live_only,
                        VCompStateVec *levels,
                        StateSet *level_sets,
                        HashTable *poly_seen,
                        VComp0Stats *stats) {
    if (raw->hidden_count < 0 || raw->hidden_count > max_level) return;

    VCompState s;
    Poly key;
    uint64_t h;

    memset(&s, 0, sizeof(s));
    s.poly = raw->poly;
    s.hidden_count = raw->hidden_count;
    for (int i = 0; i < s.hidden_count; i++) s.hidden[i] = raw->hidden[i];
    qsort(s.hidden, s.hidden_count, sizeof(Coord), coord_cmp);
    s.use_ports_identity = (tile->lattice == TILE_LATTICE_TETRILLE);
    s.port_count = raw->port_count;
    for (int i = 0; i < s.port_count; i++) s.ports[i] = raw->ports[i];
    qsort(s.ports, s.port_count, sizeof(Coord), coord_cmp);
    s.tile_count = raw->tile_count;
    for (int i = 0; i < s.tile_count; i++) s.tiles[i] = raw->tiles[i];

    poly_hash_key_lattice(&s.poly, tile->lattice, &key);
    if (live_only) {
        int ok = 1;
        if (map && s.tile_count > 0) {
            if (stats) stats->states_checked_by_dictionary++;
            ok = boundary0_poly_has_live_boundary(&s.poly,
                                                  tile,
                                                  s.tiles,
                                                  s.tile_count,
                                                  map,
                                                  stats ? &stats->boundary_stats : NULL);
            if (!ok && stats) stats->states_rejected_by_dictionary++;
        }
        if (!ok) return;
        if (!poly_has_live_boundary(&s.poly, tile)) return;
    }

    h = state_hash64(&s, &key);
    if (stateset_insert(&level_sets[s.hidden_count],
                        &levels[s.hidden_count],
                        &s,
                        h,
                        levels[s.hidden_count].count)) {
        sv_push(&levels[s.hidden_count], &s);
        hash_insert(&poly_seen[s.hidden_count], &key);
    }
}

void vcomp0_stats_init(VComp0Stats *stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
}

void run_vcomp0_levels(const Tile *tile,
                       const RL0ForgetMap *map,
                       int max_n,
                       int live_only,
                       VCompLevelFn on_level,
                       void *userdata,
                       VComp0Stats *stats) {
    int track_tiles = 1;
    VCompStateVec levels[VCOMP_MAX_LEVELS];
    StateSet level_sets[VCOMP_MAX_LEVELS];
    HashTable poly_seen[VCOMP_MAX_LEVELS];

    if (max_n >= VCOMP_MAX_LEVELS) max_n = VCOMP_MAX_LEVELS - 1;

    for (int i = 0; i <= max_n; i++) {
        sv_init(&levels[i]);
        stateset_init(&level_sets[i], 128);
        hash_init(&poly_seen[i], 1024);
    }

    VCompState seed_state;
    Poly seed_key;
    memset(&seed_state, 0, sizeof(seed_state));
    seed_state.poly.cycle_count = 1;
    seed_state.poly.cycles[0] = tile->base;
    seed_state.tile_count = track_tiles ? 1 : 0;
    if (track_tiles) seed_state.tiles[0] = tile->base;
    seed_state.port_count = 0;
    sv_push(&levels[0], &seed_state);
    poly_hash_key_lattice(&seed_state.poly, tile->lattice, &seed_key);
    stateset_insert(&level_sets[0],
                    &levels[0],
                    &seed_state,
                    state_hash64(&seed_state, &seed_key),
                    0);
    hash_insert(&poly_seen[0], &seed_key);

    for (int level = 0; level <= max_n; level++) {
        if (on_level &&
            !on_level(level,
                      &levels[level],
                      poly_seen[level].count,
                      tile,
                      userdata)) {
            break;
        }
        if (level == max_n) break;

        for (size_t i = 0; i < levels[level].count; i++) {
            Coord verts[MAX_VERTS * MAX_CYCLES];
            int vc = build_boundary_vertices(&levels[level].data[i].poly, verts);
            if (vc < 0) continue;

            for (int j = 0; j < vc; j++) {
                if (level > 0) {
                    int allowed = 0;
                    for (int p = 0; p < levels[level].data[i].port_count; p++) {
                        if (coord_eq(verts[j], levels[level].data[i].ports[p])) {
                            allowed = 1;
                            break;
                        }
                    }
                    if (!allowed) continue;
                }
                probe_prescribed_growth0(&levels[level].data[i], tile, map, stats);
                VCompLevels raw;
                vcomp_levels_init(&raw, max_n);
                vcomp_enumerate_levels(&levels[level].data[i].poly,
                                       tile,
                                       verts[j],
                                       levels[level].data[i].hidden,
                                       levels[level].data[i].hidden_count,
                                       levels[level].data[i].ports,
                                       levels[level].data[i].port_count,
                                       track_tiles
                                           ? levels[level].data[i].tiles
                                           : NULL,
                                       track_tiles
                                           ? levels[level].data[i].tile_count
                                           : 0,
                                       max_n,
                                       track_tiles,
                                       &raw);

                for (int lv = level + 1; lv <= max_n; lv++) {
                    for (size_t r = 0; r < raw.levels[lv].count; r++) {
                        ingest_raw0(&raw.levels[lv].data[r],
                                    tile,
                                    map,
                                    max_n,
                                    live_only,
                                    levels,
                                    level_sets,
                                    poly_seen,
                                    stats);
                    }
                }
                vcomp_levels_destroy(&raw);
            }
        }
    }

    for (int i = 0; i <= max_n; i++) {
        hash_destroy(&poly_seen[i]);
        stateset_destroy(&level_sets[i]);
        sv_destroy(&levels[i]);
    }
}
