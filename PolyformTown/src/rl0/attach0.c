#include "rl0/attach0.h"

#include <string.h>

#include "core/attach.h"
#include "core/boundary.h"
#include "core/lattice.h"
#include "core/tetrille.h"
#include "rl0/boundary0.h"

#define A0_MAX_EDGES (MAX_VERTS * MAX_CYCLES)

void attach0_stats_init(Attach0Stats *stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
}

void attach0_closure_stats_init(Attach0ClosureStats *stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
}

static int cycle_same_cyclic0(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return 0;
    int n = a->n;
    for (int shift = 0; shift < n; shift++) {
        int ok = 1;
        for (int k = 0; k < n; k++) {
            if (!coord_eq(a->v[k], b->v[(shift + k) % n])) {
                ok = 0;
                break;
            }
        }
        if (ok) return 1;
    }
    return 0;
}

static int cycle_same_reversed_cyclic0(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return 0;
    int n = a->n;
    for (int shift = 0; shift < n; shift++) {
        int ok = 1;
        for (int k = 0; k < n; k++) {
            int j = (shift - k) % n;
            if (j < 0) j += n;
            if (!coord_eq(a->v[k], b->v[j])) {
                ok = 0;
                break;
            }
        }
        if (ok) return 1;
    }
    return 0;
}

static int cycle_equal0(const Cycle *a, const Cycle *b) {
    return !cycle_less(a, b) && !cycle_less(b, a);
}

static void normalize_like_tile_build0(const Tile *tile, Cycle *c) {
    if (cycle_signed_area2(c, tile->lattice) < 0) cycle_reverse(c);
    cycle_normalize_position(c, tile->lattice);
    cycle_canonicalize_shift(c);
}

int attach0_verify_variant_order(const Tile *tile, Attach0Stats *stats) {
    int tcount = lattice_transform_count(tile->lattice);
    long long base_area;
    if (!tile || tile->base.n <= 0) return 0;
    base_area = cycle_signed_area2(&tile->base, tile->lattice);
    if (base_area < 0) base_area = -base_area;

    for (int v = 0; v < tile->variant_count; v++) {
        int matched = 0;
        if (stats) stats->variant_order_checked++;
        for (int t = 0; t < tcount; t++) {
            Cycle raw = {0};
            Cycle oriented = {0};
            Cycle norm = {0};
            long long area;
            cycle_transform_lattice(&tile->base, &raw, tile->lattice, t);
            area = cycle_signed_area2(&raw, tile->lattice);
            oriented = raw;
            cycle_normalize_position(&oriented, tile->lattice);
            norm = raw;
            normalize_like_tile_build0(tile, &norm);
            if (!cycle_equal0(&norm, &tile->variants[v])) continue;

            if (area >= 0) {
                if (!cycle_same_cyclic0(&tile->variants[v], &oriented)) continue;
            } else {
                if (!cycle_same_reversed_cyclic0(&tile->variants[v], &oriented)) continue;
            }
            matched = 1;
            break;
        }
        if (!matched) {
            if (stats) stats->variant_order_failures++;
            return 0;
        }
    }
    return 1;
}

static int item_equal0(RL0FMItem a, RL0FMItem b) {
    return a.p == b.p && a.i == b.i;
}

int attach0_try_attach_one(const Poly *base,
                           const Tile *tile,
                           Coord target,
                           RL0FMItem item,
                           Poly *out,
                           Cycle *aligned_out,
                           Attach0Stats *stats) {
    Edge frontier[A0_MAX_EDGES];
    int fc;
    if (!base || !tile || !out) return 0;
    fc = build_boundary_edges(base, frontier);
    if (fc < 0 || fc > A0_MAX_EDGES) return 0;

    for (int be = 0; be < fc; be++) {
        if (!coord_eq(frontier[be].b, target)) continue;
        for (int v = 0; v < tile->variant_count; v++) {
            const Cycle *tv = &tile->variants[v];
            for (int te = 0; te < tv->n; te++) {
                Poly grown;
                Cycle aligned;
                RL0FMItem got;
                if (stats) stats->attach_one_attempts++;
                if (!try_attach_tile_poly_ex(base,
                                             tv,
                                             tile->lattice,
                                             be,
                                             te,
                                             &grown,
                                             &aligned)) {
                    continue;
                }
                if (!boundary0_tile_item_at_vertex(tile, &aligned, target, &got)) {
                    continue;
                }
                if (!item_equal0(got, item)) continue;
                *out = grown;
                if (aligned_out) *aligned_out = aligned;
                if (stats) stats->attach_one_successes++;
                return 1;
            }
        }
    }
    return 0;
}

int attach0_try_attach_arc(const Poly *base,
                           const Tile *tile,
                           const Cycle *tiles,
                           int tile_count,
                           Coord target,
                           const RL0FMArc *arc,
                           Poly *out,
                           Cycle *out_tiles,
                           int *out_tile_count,
                           Attach0Stats *stats) {
    Poly cur;
    Cycle carried[ATTACH0_MAX_TILES];
    int carried_count;
    if (!base || !tile || !arc || !out || !out_tiles || !out_tile_count) return 0;
    if (tile_count < 0 || tile_count > ATTACH0_MAX_TILES) return 0;
    if (arc->n < 0 || arc->n > RL0_FM_MAX_ITEMS) return 0;
    if (tile_count + arc->n > ATTACH0_MAX_TILES) return 0;

    if (stats) stats->attach_arc_attempts++;
    cur = *base;
    carried_count = tile_count;
    for (int i = 0; i < tile_count; i++) carried[i] = tiles[i];

    for (int k = 0; k < arc->n; k++) {
        Poly grown;
        Cycle aligned;
        if (!attach0_try_attach_one(&cur,
                                    tile,
                                    target,
                                    arc->item[k],
                                    &grown,
                                    &aligned,
                                    stats)) {
            return 0;
        }
        cur = grown;
        carried[carried_count++] = aligned;
    }

    *out = cur;
    for (int i = 0; i < carried_count; i++) out_tiles[i] = carried[i];
    *out_tile_count = carried_count;
    if (stats) stats->attach_arc_successes++;
    return 1;
}
int attach0_force_live_closure(Poly *poly,
                               const Tile *tile,
                               Cycle *tiles,
                               int *tile_count,
                               const RL0ForgetMap *map,
                               int max_steps,
                               Attach0Stats *attach_stats,
                               Attach0ClosureStats *closure_stats) {
    int steps = 0;
    if (!poly || !tile || !tiles || !tile_count || !map) return 0;
    if (*tile_count < 0 || *tile_count > ATTACH0_MAX_TILES) return 0;
    if (max_steps <= 0) max_steps = 1024;

    while (steps < max_steps) {
        Coord verts[A0_MAX_EDGES];
        int vc = build_boundary_vertices(poly, verts);
        int forced_this_pass = 0;
        if (vc < 0 || vc > A0_MAX_EDGES) return 0;

        for (int v = 0; v < vc; v++) {
            RL0FMArc key;
            const RL0FMArc *values = NULL;
            int value_count = 0;
            int nonempty_count = 0;
            int nonempty_index = -1;

            if (closure_stats) closure_stats->vertices_checked++;
            if (!boundary0_build_vertex_arc(tile, tiles, *tile_count, verts[v], &key) ||
                !rl0_fm_lookup_any_rotation(map, &key, &values, &value_count, NULL) ||
                value_count <= 0) {
                if (closure_stats) closure_stats->zero_choice_failures++;
                return 0;
            }

            for (int k = 0; k < value_count; k++) {
                if (values[k].n > 0) {
                    nonempty_count++;
                    nonempty_index = k;
                }
            }

            if (nonempty_count == 0) {
                continue;
            }
            if (nonempty_count > 1) {
                if (closure_stats) closure_stats->unresolved_vertices++;
                continue;
            }

            {
                Poly grown;
                Cycle out_tiles[ATTACH0_MAX_TILES];
                int out_tile_count = 0;
                if (closure_stats) closure_stats->forced_vertices++;
                if (!attach0_try_attach_arc(poly,
                                            tile,
                                            tiles,
                                            *tile_count,
                                            verts[v],
                                            &values[nonempty_index],
                                            &grown,
                                            out_tiles,
                                            &out_tile_count,
                                            attach_stats)) {
                    if (closure_stats) closure_stats->forced_failures++;
                    return 0;
                }
                *poly = grown;
                if (out_tile_count < 0 || out_tile_count > ATTACH0_MAX_TILES) return 0;
                for (int i = 0; i < out_tile_count; i++) tiles[i] = out_tiles[i];
                *tile_count = out_tile_count;
                if (closure_stats) {
                    closure_stats->forced_successes++;
                    closure_stats->closure_steps++;
                }
                steps++;
                forced_this_pass = 1;
                break;
            }
        }

        if (!forced_this_pass) return 1;
    }
    return 0;
}
