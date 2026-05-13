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

static void attach0_status_set(AttachStatus *status, AttachStatus value) {
    if (status) *status = value;
}

static int cycle_equal0(const Cycle *a, const Cycle *b) {
    return !cycle_less(a, b) && !cycle_less(b, a);
}

static int source_index_to_variant_slot0(int n, int parity, int source_index) {
    if (source_index < 0 || source_index >= n) return -1;
    if (parity == 1) return source_index;
    if (parity == -1) return n - 1 - source_index;
    return -1;
}

static int variant_matches_transform0(const Tile *tile,
                                      const Cycle *variant,
                                      int transform,
                                      int *parity_out) {
    Cycle cur = {0};
    int p;
    cycle_transform_lattice(&tile->base, &cur, tile->lattice, transform);
    p = cycle_signed_area2(&cur, tile->lattice) >= 0 ? 1 : -1;
    if (p < 0) {
        Cycle rev = {0};
        rev.n = cur.n;
        for (int i = 0; i < cur.n; i++) rev.v[i] = cur.v[cur.n - 1 - i];
        cur = rev;
    }
    cycle_normalize_position(&cur, tile->lattice);
    if (!cycle_equal0(&cur, variant)) return 0;
    if (parity_out) *parity_out = p;
    return 1;
}

static int variant_has_parity0(const Tile *tile, const Cycle *variant, int parity) {
    int tcount = lattice_transform_count(tile->lattice);
    for (int t = 0; t < tcount; t++) {
        int p = 0;
        if (variant_matches_transform0(tile, variant, t, &p) && p == parity) {
            return 1;
        }
    }
    return 0;
}

static int edge_equal0(Edge a, Edge b) {
    return coord_eq(a.a, b.a) && coord_eq(a.b, b.b);
}

static int find_boundary_edge0(const Poly *p, Edge wanted) {
    Edge edges[A0_MAX_EDGES];
    int ec = build_boundary_edges(p, edges);
    if (ec < 0 || ec > A0_MAX_EDGES) return -1;
    for (int i = 0; i < ec; i++) {
        if (edge_equal0(edges[i], wanted)) return i;
    }
    return -1;
}

static Edge previous_edge_at_vertex0(const Cycle *c, Coord target) {
    Edge e;
    e.a = target;
    e.b = target;
    for (int i = 0; i < c->n; i++) {
        if (!coord_eq(c->v[i], target)) continue;
        e.a = c->v[(i + c->n - 1) % c->n];
        e.b = c->v[i];
        return e;
    }
    return e;
}

static int attach0_try_attach_one_on_edge_status(const Poly *base,
                                                 const Tile *tile,
                                                 Coord target,
                                                 Edge current_edge,
                                                 RL0FMItem item,
                                                 Poly *out,
                                                 Cycle *aligned_out,
                                                 Edge *next_edge,
                                                 Attach0Stats *stats,
                                                 AttachStatus *status_out) {
    int be = find_boundary_edge0(base, current_edge);
    attach0_status_set(status_out, ATTACH_STATUS_GEOMETRY);
    if (be < 0) return 0;

    for (int v = 0; v < tile->variant_count; v++) {
        const Cycle *tv = &tile->variants[v];
        int te;
        Poly grown;
        Cycle aligned;
        if (!variant_has_parity0(tile, tv, item.p)) continue;
        te = source_index_to_variant_slot0(tv->n, item.p, item.i);
        if (te < 0 || te >= tv->n) continue;
        if (stats) stats->attach_one_attempts++;
        {
            AttachStatus one_status = ATTACH_STATUS_GEOMETRY;
            if (!try_attach_tile_poly_ex_status(base,
                                                tv,
                                                tile->lattice,
                                                be,
                                                te,
                                                &grown,
                                                &aligned,
                                                &one_status)) {
                if (one_status == ATTACH_STATUS_BOUNDARY_BOUND ||
                    one_status == ATTACH_STATUS_CYCLE_BOUND ||
                    one_status == ATTACH_STATUS_INTERNAL_BOUND) {
                    attach0_status_set(status_out, one_status);
                    return 0;
                }
                continue;
            }
        }
        *out = grown;
        if (aligned_out) *aligned_out = aligned;
        if (next_edge) *next_edge = previous_edge_at_vertex0(&aligned, target);
        if (stats) stats->attach_one_successes++;
        attach0_status_set(status_out, ATTACH_STATUS_OK);
        return 1;
    }
    return 0;
}

int attach0_verify_variant_order(const Tile *tile, Attach0Stats *stats) {
    if (!tile || tile->base.n <= 0) return 0;

    for (int v = 0; v < tile->variant_count; v++) {
        int matched = 0;
        if (stats) stats->variant_order_checked++;
        for (int t = 0; t < lattice_transform_count(tile->lattice); t++) {
            if (variant_matches_transform0(tile, &tile->variants[v], t, NULL)) {
                matched = 1;
                break;
            }
        }
        if (!matched) {
            if (stats) stats->variant_order_failures++;
            return 0;
        }
    }
    return 1;
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
        if (attach0_try_attach_one_on_edge_status(base,
                                                  tile,
                                                  target,
                                                  frontier[be],
                                                  item,
                                                  out,
                                                  aligned_out,
                                                  NULL,
                                                  stats,
                                                  NULL)) {
            return 1;
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
    return attach0_try_attach_arc_status(base,
                                         tile,
                                         tiles,
                                         tile_count,
                                         target,
                                         arc,
                                         out,
                                         out_tiles,
                                         out_tile_count,
                                         stats,
                                         NULL);
}

int attach0_try_attach_arc_status(const Poly *base,
                                  const Tile *tile,
                                  const Cycle *tiles,
                                  int tile_count,
                                  Coord target,
                                  const RL0FMArc *arc,
                                  Poly *out,
                                  Cycle *out_tiles,
                                  int *out_tile_count,
                                  Attach0Stats *stats,
                                  AttachStatus *status_out) {
    Edge frontier[A0_MAX_EDGES];
    int fc;
    attach0_status_set(status_out, ATTACH_STATUS_GEOMETRY);
    if (!base || !tile || !arc || !out || !out_tiles || !out_tile_count) {
        attach0_status_set(status_out, ATTACH_STATUS_INTERNAL_BOUND);
        return 0;
    }
    if (tile_count < 0 || tile_count > ATTACH0_MAX_TILES) {
        attach0_status_set(status_out, ATTACH_STATUS_INTERNAL_BOUND);
        return 0;
    }
    if (arc->n < 0 || arc->n > RL0_FM_MAX_ITEMS) {
        attach0_status_set(status_out, ATTACH_STATUS_INTERNAL_BOUND);
        return 0;
    }
    if (tile_count + arc->n > ATTACH0_MAX_TILES) {
        attach0_status_set(status_out, ATTACH_STATUS_BOUNDARY_BOUND);
        return 0;
    }
    fc = build_boundary_edges(base, frontier);
    if (fc < 0 || fc > A0_MAX_EDGES) {
        attach0_status_set(status_out, ATTACH_STATUS_BOUNDARY_BOUND);
        return 0;
    }

    if (stats) stats->attach_arc_attempts++;

    if (arc->n == 0) {
        *out = *base;
        for (int i = 0; i < tile_count; i++) out_tiles[i] = tiles[i];
        *out_tile_count = tile_count;
        if (stats) stats->attach_arc_successes++;
        attach0_status_set(status_out, ATTACH_STATUS_OK);
        return 1;
    }

    for (int start = 0; start < fc; start++) {
        Poly cur;
        Cycle carried[ATTACH0_MAX_TILES];
        int carried_count;
        Edge current;
        int ok = 1;

        if (!coord_eq(frontier[start].b, target)) continue;

        cur = *base;
        carried_count = tile_count;
        for (int i = 0; i < tile_count; i++) carried[i] = tiles[i];
        current = frontier[start];

        for (int k = 0; k < arc->n; k++) {
            Poly grown;
            Cycle aligned;
            Edge next;
            {
                AttachStatus one_status = ATTACH_STATUS_GEOMETRY;
                if (!attach0_try_attach_one_on_edge_status(&cur,
                                                           tile,
                                                           target,
                                                           current,
                                                           arc->item[k],
                                                           &grown,
                                                           &aligned,
                                                           &next,
                                                           stats,
                                                           &one_status)) {
                    if (one_status == ATTACH_STATUS_BOUNDARY_BOUND ||
                        one_status == ATTACH_STATUS_CYCLE_BOUND ||
                        one_status == ATTACH_STATUS_INTERNAL_BOUND) {
                        attach0_status_set(status_out, one_status);
                        return 0;
                    }
                    ok = 0;
                    break;
                }
            }
            cur = grown;
            if (carried_count >= ATTACH0_MAX_TILES) {
                attach0_status_set(status_out, ATTACH_STATUS_BOUNDARY_BOUND);
                return 0;
            }
            carried[carried_count++] = aligned;
            current = next;
        }

        if (ok) {
            *out = cur;
            for (int i = 0; i < carried_count; i++) out_tiles[i] = carried[i];
            *out_tile_count = carried_count;
            if (stats) stats->attach_arc_successes++;
            attach0_status_set(status_out, ATTACH_STATUS_OK);
            return 1;
        }
    }
    return 0;
}

int attach0_force_live_closure(Poly *poly,
                               const Tile *tile,
                               Cycle *tiles,
                               int *tile_count,
                               const RL0ForgetMap *map,
                               int max_steps,
                               Attach0Stats *attach_stats,
                               Attach0ClosureStats *closure_stats) {
    return attach0_force_live_closure_status(poly,
                                             tile,
                                             tiles,
                                             tile_count,
                                             map,
                                             max_steps,
                                             attach_stats,
                                             closure_stats,
                                             NULL);
}

int attach0_force_live_closure_status(Poly *poly,
                                      const Tile *tile,
                                      Cycle *tiles,
                                      int *tile_count,
                                      const RL0ForgetMap *map,
                                      int max_steps,
                                      Attach0Stats *attach_stats,
                                      Attach0ClosureStats *closure_stats,
                                      AttachStatus *status_out) {
    int steps = 0;
    attach0_status_set(status_out, ATTACH_STATUS_GEOMETRY);
    if (!poly || !tile || !tiles || !tile_count || !map) {
        attach0_status_set(status_out, ATTACH_STATUS_INTERNAL_BOUND);
        return 0;
    }
    if (*tile_count < 0 || *tile_count > ATTACH0_MAX_TILES) {
        attach0_status_set(status_out, ATTACH_STATUS_INTERNAL_BOUND);
        return 0;
    }
    if (max_steps <= 0) max_steps = 1024;

    while (steps < max_steps) {
        Coord verts[A0_MAX_EDGES];
        int vc = build_boundary_vertices(poly, verts);
        int forced_this_pass = 0;
        if (vc < 0 || vc > A0_MAX_EDGES) {
            attach0_status_set(status_out, ATTACH_STATUS_BOUNDARY_BOUND);
            return 0;
        }

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
                attach0_status_set(status_out, ATTACH_STATUS_GEOMETRY);
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
                AttachStatus forced_status = ATTACH_STATUS_GEOMETRY;
                if (closure_stats) closure_stats->forced_vertices++;
                if (!attach0_try_attach_arc_status(poly,
                                                   tile,
                                                   tiles,
                                                   *tile_count,
                                                   verts[v],
                                                   &values[nonempty_index],
                                                   &grown,
                                                   out_tiles,
                                                   &out_tile_count,
                                                   attach_stats,
                                                   &forced_status)) {
                    if (closure_stats) closure_stats->forced_failures++;
                    attach0_status_set(status_out, forced_status);
                    return 0;
                }
                *poly = grown;
                if (out_tile_count < 0 || out_tile_count > ATTACH0_MAX_TILES) {
                    attach0_status_set(status_out, ATTACH_STATUS_BOUNDARY_BOUND);
                    return 0;
                }
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

        if (!forced_this_pass) {
            attach0_status_set(status_out, ATTACH_STATUS_OK);
            return 1;
        }
    }
    attach0_status_set(status_out, ATTACH_STATUS_INTERNAL_BOUND);
    return 0;
}
