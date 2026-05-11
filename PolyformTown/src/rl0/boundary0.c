#include "rl0/boundary0.h"

#include <string.h>
#include <stdlib.h>

#include "core/boundary.h"
#include "core/lattice.h"
#include "core/tetrille.h"

#define B0_MAX_BOUNDARY_VERTS (MAX_VERTS * MAX_CYCLES)

void boundary0_stats_init(Boundary0Stats *stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
}

static int cycle_matches_cyclic_local(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return 0;
    int n = a->n;
    for (int shift = 0; shift < n; shift++) {
        int ok = 1;
        for (int i = 0; i < n; i++) {
            if (!coord_eq(a->v[i], b->v[(shift + i) % n])) { ok = 0; break; }
        }
        if (ok) return 1;
        ok = 1;
        for (int i = 0; i < n; i++) {
            int j = (shift - i) % n;
            if (j < 0) j += n;
            if (!coord_eq(a->v[i], b->v[j])) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

static int cycle_vertex_index_local(const Cycle *c, Coord q) {
    for (int i = 0; i < c->n; i++) if (coord_eq(c->v[i], q)) return i;
    return -1;
}

int boundary0_tile_item_at_vertex(const Tile *tile,
                               const Cycle *placed_tile,
                               Coord center,
                               RL0FMItem *out) {
    int n = tile->base.n;
    long long src_area = cycle_signed_area2(&tile->base, tile->lattice);
    int tcount = lattice_transform_count(tile->lattice);
    if (placed_tile->n != n) return 0;

    for (int t = 0; t < tcount; t++) {
        Cycle tr = {0};
        cycle_transform_lattice(&tile->base, &tr, tile->lattice, t);
        for (int j = 0; j < n; j++) {
            if (placed_tile->v[j].v != tr.v[0].v) continue;
            int dx = placed_tile->v[j].x - tr.v[0].x;
            int dy = placed_tile->v[j].y - tr.v[0].y;
            Cycle candidate = tr;
            if (tile->lattice == TILE_LATTICE_TETRILLE) {
                int m6 = 0, n6 = 0;
                if (!tetrille_delta_to_6(tr.v[0].v, dx, dy, &m6, &n6)) continue;
                tetrille_translate_cycle(&candidate, m6, n6);
            } else {
                cycle_translate(&candidate, dx, dy);
            }
            if (!cycle_matches_cyclic_local(&candidate, placed_tile)) continue;
            int p = (cycle_signed_area2(&tr, tile->lattice) * src_area >= 0) ? 1 : -1;
            Cycle canonical;
            canonical.n = n;
            if (p == 1) {
                for (int k = 0; k < n; k++) canonical.v[k] = candidate.v[k];
            } else {
                for (int k = 0; k < n; k++) canonical.v[k] = candidate.v[n - 1 - k];
            }
            int idx = cycle_vertex_index_local(&canonical, center);
            if (idx < 0) return 0;
            if (p == -1) idx = n - 1 - idx;
            out->p = p;
            out->i = idx;
            return 1;
        }
    }
    return 0;
}

static int coord_in_cycle(const Cycle *c, Coord q) {
    for (int i = 0; i < c->n; i++) if (coord_eq(c->v[i], q)) return 1;
    return 0;
}

typedef struct {
    RL0FMItem item;
    Coord prev;
    Coord next;
    int tile_index;
    int next_idx;
    int prev_idx;
    int used;
} B0Incident;

static int item_less0(RL0FMItem a, RL0FMItem b) {
    if (a.p != b.p) return a.p < b.p;
    return a.i < b.i;
}

static int incident_less0(const B0Incident *a, const B0Incident *b) {
    if (item_less0(a->item, b->item)) return 1;
    if (item_less0(b->item, a->item)) return 0;
    return a->tile_index < b->tile_index;
}

static int tile_vertex_pos0(const Cycle *c, Coord q) {
    for (int i = 0; i < c->n; i++) {
        if (coord_eq(c->v[i], q)) return i;
    }
    return -1;
}

static int edge_walk_order_incidents0(B0Incident *inc, int count, RL0FMArc *arc) {
    int start = -1;
    int visited = 0;
    int cur;

    if (count <= 0 || count > RL0_FM_MAX_ITEMS) return 0;
    arc->n = 0;

    for (int i = 0; i < count; i++) {
        inc[i].next_idx = -1;
        inc[i].prev_idx = -1;
        inc[i].used = 0;
    }

    for (int i = 0; i < count; i++) {
        for (int j = 0; j < count; j++) {
            if (i == j) continue;

            /* Walk through tile i at q, then cross its previous edge.
               Shared edges around a valid tiling appear with opposite
               cycle orientation, so i.prev should be j.next. */
            if (coord_eq(inc[i].prev, inc[j].next)) {
                if (inc[i].next_idx >= 0) return 0;
                inc[i].next_idx = j;
            }

            /* Reverse relation: cross tile i's next edge. */
            if (coord_eq(inc[i].next, inc[j].prev)) {
                if (inc[i].prev_idx >= 0) return 0;
                inc[i].prev_idx = j;
            }
        }
    }

    /* Boundary arcs have an endpoint whose next edge is exposed.  Walking
       from that endpoint by crossing previous edges keeps all gaps at the
       list boundaries.  Closed cycles have no endpoint; choose a stable
       representative and let dictionary rotation canonicalization handle it. */
    for (int i = 0; i < count; i++) {
        if (inc[i].prev_idx < 0) {
            if (start < 0 || incident_less0(&inc[i], &inc[start])) start = i;
        }
    }
    if (start < 0) {
        start = 0;
        for (int i = 1; i < count; i++) {
            if (incident_less0(&inc[i], &inc[start])) start = i;
        }
    }

    cur = start;
    while (cur >= 0) {
        if (inc[cur].used) break;
        if (arc->n >= RL0_FM_MAX_ITEMS) return 0;
        inc[cur].used = 1;
        arc->item[arc->n++] = inc[cur].item;
        visited++;
        cur = inc[cur].next_idx;
    }

    if (visited != count) return 0;
    return arc->n == count;
}

static int lookup_maximal_arc0(const RL0ForgetMap *map,
                               const RL0FMArc *arc,
                               const RL0FMArc **values,
                               int *value_count);

static int collect_incidents0(const Tile *tile,
                              const Cycle *tiles,
                              int tile_count,
                              Coord q,
                              B0Incident *inc,
                              int *inc_count) {
    *inc_count = 0;
    for (int t = 0; t < tile_count; t++) {
        RL0FMItem item;
        int pos;
        if (!coord_in_cycle(&tiles[t], q)) continue;
        if (!boundary0_tile_item_at_vertex(tile, &tiles[t], q, &item)) return 0;
        if (*inc_count >= RL0_FM_MAX_ITEMS) return 0;
        pos = tile_vertex_pos0(&tiles[t], q);
        if (pos < 0) return 0;
        inc[*inc_count].item = item;
        inc[*inc_count].prev = tiles[t].v[(pos + tiles[t].n - 1) % tiles[t].n];
        inc[*inc_count].next = tiles[t].v[(pos + 1) % tiles[t].n];
        inc[*inc_count].tile_index = t;
        (*inc_count)++;
    }
    return *inc_count > 0;
}

static int incidents_adjacent0(const B0Incident *a, const B0Incident *b) {
    return coord_eq(a->prev, b->next) ||
           coord_eq(a->next, b->prev) ||
           coord_eq(a->prev, b->prev) ||
           coord_eq(a->next, b->next);
}

static int lookup_component_arc0(const RL0ForgetMap *map,
                                 B0Incident *inc,
                                 int inc_count,
                                 const int *members,
                                 int member_count,
                                 const RL0FMArc **values,
                                 int *value_count) {
    B0Incident local[RL0_FM_MAX_ITEMS];
    (void)inc_count;
    RL0FMArc arc;
    for (int i = 0; i < member_count; i++) local[i] = inc[members[i]];
    if (!edge_walk_order_incidents0(local, member_count, &arc)) return 0;
    return lookup_maximal_arc0(map, &arc, values, value_count);
}

int boundary0_build_vertex_arc(const Tile *tile,
                               const Cycle *tiles,
                               int tile_count,
                               Coord q,
                               RL0FMArc *arc) {
    B0Incident inc[RL0_FM_MAX_ITEMS];
    int inc_count = 0;
    if (!collect_incidents0(tile, tiles, tile_count, q, inc, &inc_count)) return 0;
    return edge_walk_order_incidents0(inc, inc_count, arc);
}

static int boundary0_vertex_has_dictionary_completion0(const Tile *tile,
                                                       const Cycle *tiles,
                                                       int tile_count,
                                                       Coord q,
                                                       const RL0ForgetMap *map) {
    B0Incident inc[RL0_FM_MAX_ITEMS];
    int inc_count = 0;
    int assigned[RL0_FM_MAX_ITEMS] = {0};
    int remaining;

    if (!collect_incidents0(tile, tiles, tile_count, q, inc, &inc_count)) return 0;
    remaining = inc_count;

    while (remaining > 0) {
        int queue[RL0_FM_MAX_ITEMS];
        int members[RL0_FM_MAX_ITEMS];
        int qh = 0, qt = 0, mc = 0;
        const RL0FMArc *values = NULL;
        int value_count = 0;
        int seed = -1;

        for (int i = 0; i < inc_count; i++) {
            if (!assigned[i]) { seed = i; break; }
        }
        if (seed < 0) return 0;
        assigned[seed] = 1;
        queue[qt++] = seed;
        members[mc++] = seed;
        remaining--;

        while (qh < qt) {
            int cur = queue[qh++];
            for (int j = 0; j < inc_count; j++) {
                if (assigned[j]) continue;
                if (!incidents_adjacent0(&inc[cur], &inc[j])) continue;
                assigned[j] = 1;
                queue[qt++] = j;
                members[mc++] = j;
                remaining--;
            }
        }

        if (!lookup_component_arc0(map,
                                   inc,
                                   inc_count,
                                   members,
                                   mc,
                                   &values,
                                   &value_count) ||
            value_count <= 0) {
            return 0;
        }
    }
    return 1;
}

static int lookup_maximal_arc0(const RL0ForgetMap *map,
                               const RL0FMArc *arc,
                               const RL0FMArc **values,
                               int *value_count) {
    return rl0_fm_lookup_any_rotation(map, arc, values, value_count, NULL) &&
           *value_count > 0;
}

int boundary0_vertex_has_dictionary_completion(const Tile *tile,
                                               const Cycle *tiles,
                                               int tile_count,
                                               Coord q,
                                               const RL0ForgetMap *map) {
    return boundary0_vertex_has_dictionary_completion0(tile, tiles, tile_count, q, map);
}

int boundary0_poly_has_live_boundary(const Poly *p,
                                     const Tile *tile,
                                     const Cycle *tiles,
                                     int tile_count,
                                     const RL0ForgetMap *map,
                                     Boundary0Stats *stats) {
    Coord verts[B0_MAX_BOUNDARY_VERTS];
    int vc = build_boundary_vertices(p, verts);
    if (vc < 0) return 0;

    for (int v = 0; v < vc; v++) {
        RL0FMArc arc;
        if (!boundary0_build_vertex_arc(tile, tiles, tile_count, verts[v], &arc)) {
            /* Multiple disconnected maximal arcs are handled below; this
               failure is not fatal by itself. */
            arc.n = 0;
        }
        if (stats) {
            stats->vertices_checked++;
            if (arc.n > stats->max_incident) stats->max_incident = arc.n;
        }
        if (!boundary0_vertex_has_dictionary_completion0(tile,
                                                         tiles,
                                                         tile_count,
                                                         verts[v],
                                                         map)) {
            if (stats) stats->dictionary_misses++;
            return 0;
        }
    }
    return 1;
}
