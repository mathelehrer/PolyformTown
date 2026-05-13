#include <string.h>
#include <math.h>
#include "core/attach.h"
#include "core/lattice.h"
#include "core/tetrille.h"
#include "core/boundary.h"
#include "core/numerics.h"

#define MAX_EDGES (MAX_VERTS)
#define MAX_LOCAL (4 * MAX_VERTS)

typedef struct {
    Edge e;
    int canceled;
} LEdge;

enum {
    WALK_FAIL = 0,
    WALK_OK = 1,
    WALK_DUP = 2,
    WALK_BOUND = 3
};

static void attach_status_set(AttachStatus *status, AttachStatus value) {
    if (status) *status = value;
}

static int edge_same(Edge a, Edge b) { return coord_eq(a.a,b.a) && coord_eq(a.b,b.b); }
static int edge_opp(Edge a, Edge b) { return coord_eq(a.a,b.b) && coord_eq(a.b,b.a); }


/* ---------- geometry core ---------- */

static int align_tile(const Cycle *tile, int tile_edge_index, Edge target, int lattice, Cycle *out) {
    Edge te = cycle_edge(tile, tile_edge_index);

    if (lattice == TILE_LATTICE_TETRILLE) {
        TetrilleTaggedVec ta, tb;
        int m6, n6, dx, dy;
        if (!tetrille_edge_tag(te, &ta) || !tetrille_edge_tag(target, &tb)) return 0;
        if (ta.system != tb.system) return 0;
        if (ta.dx != -tb.dx || ta.dy != -tb.dy) return 0;
        if (te.a.v != target.b.v || te.b.v != target.a.v) return 0;

        dx = target.b.x - te.a.x;
        dy = target.b.y - te.a.y;
        if (!tetrille_delta_to_6(te.a.v, dx, dy, &m6, &n6)) return 0;

        *out = *tile;
        tetrille_translate_cycle(out, m6, n6);

        {
            Edge ne = cycle_edge(out, tile_edge_index);
            if (!coord_eq(ne.a, target.b)) return 0;
            if (!coord_eq(ne.b, target.a)) return 0;
        }
        return 1;
    }

    {
        int tdx = te.b.x - te.a.x;
        int tdy = te.b.y - te.a.y;
        int bdx = target.a.x - target.b.x;
        int bdy = target.a.y - target.b.y;

        if (tdx != bdx || tdy != bdy) return 0;

        *out = *tile;

        {
            Edge ne = cycle_edge(out, tile_edge_index);
            int tx = target.b.x - ne.a.x;
            int ty = target.b.y - ne.a.y;
            cycle_translate(out, tx, ty);
        }
        return 1;
    }
}

int align_tile_to_frontier_edge(const Poly *base, const Cycle *tile_variant,
                                int base_edge_index, int tile_edge_index,
                                Cycle *aligned) {
    Edge frontier[MAX_VERTS * MAX_CYCLES];
    int frontier_n = build_boundary_edges(base, frontier);
    if (base_edge_index < 0 || base_edge_index >= frontier_n) return 0;
    return align_tile(tile_variant, tile_edge_index, frontier[base_edge_index], TILE_LATTICE_SQUARE, aligned);
}

static int build_union_edges(const Poly *a, const Cycle *b, LEdge *out, int *out_n,
                             AttachStatus *status) {
    int n = 0;

    for (int i = 0; i < a->cycle_count; i++) {
        for (int j = 0; j < a->cycles[i].n; j++) {
            if (n >= MAX_LOCAL) {
                attach_status_set(status, ATTACH_STATUS_BOUNDARY_BOUND);
                return 0;
            }
            out[n++] = (LEdge){ cycle_edge(&a->cycles[i], j), 0 };
        }
    }

    for (int i = 0; i < b->n; i++) {
        if (n >= MAX_LOCAL) {
            attach_status_set(status, ATTACH_STATUS_BOUNDARY_BOUND);
            return 0;
        }
        out[n++] = (LEdge){ cycle_edge(b, i), 0 };
    }

    for (int i = 0; i < n; i++) {
        if (out[i].canceled) continue;
        for (int j = i + 1; j < n; j++) {
            if (out[j].canceled) continue;
            if (edge_same(out[i].e, out[j].e)) {
                attach_status_set(status, ATTACH_STATUS_GEOMETRY);
                return 0;
            }
            if (edge_opp(out[i].e, out[j].e)) {
                out[i].canceled = 1;
                out[j].canceled = 1;
                break;
            }
        }
    }

    *out_n = n;
    return 1;
}

static int coord_seen_before(const Coord *seen, int seen_n, Coord c) {
    for (int i = 0; i < seen_n; i++)
        if (coord_eq(seen[i], c)) return 1;
    return 0;
}

static int pick_next_edge(const Edge *edges, int m, const int *used, Coord v,
                          int prev_dir, int prefer_left, int lattice) {
    int dir_count = lattice_direction_count(lattice);
    int rev = (prev_dir + dir_count / 2) % dir_count;
    int best = -1;
    int best_score = prefer_left ? -1 : dir_count + 1;

    for (int i = 0; i < m; i++) {
        if (used[i]) continue;
        if (!coord_eq(edges[i].a, v)) continue;

        int d = lattice_direction_index(lattice, edges[i]);
        if (d < 0) continue;
        int score = (d - rev + dir_count) % dir_count;
        if (score == 0) continue;

        if (prefer_left) {
            if (score > best_score) {
                best_score = score;
                best = i;
            }
        } else {
            if (score < best_score) {
                best_score = score;
                best = i;
            }
        }
    }

    return best;
}

static int walk_one_cycle(const Edge *edges, int m, int *used, int start,
                          int prefer_left, int lattice, Cycle *out) {
    Coord seen[MAX_VERTS];
    int seen_n = 0;
    int cur = start;

    out->n = 0;

    while (1) {
        if (used[cur]) return WALK_FAIL;
        if (out->n >= MAX_VERTS) return WALK_BOUND;
        if (coord_seen_before(seen, seen_n, edges[cur].a)) return WALK_DUP;

        seen[seen_n++] = edges[cur].a;
        used[cur] = 1;
        out->v[out->n++] = edges[cur].a;

        Coord v = edges[cur].b;
        if (coord_eq(v, edges[start].a)) break;

        int prev_dir = lattice_direction_index(lattice, edges[cur]);
        if (prev_dir < 0) return WALK_FAIL;
        int next = pick_next_edge(edges, m, used, v, prev_dir, prefer_left, lattice);
        if (next < 0) return WALK_FAIL;

        cur = next;
    }

    return WALK_OK;
}

static int find_start_edge(const Edge *edges, int m, const int *used, int lattice) {
    int start = -1;

    for (int i = 0; i < m; i++) {
        double ix, iy;
        double sx, sy;
        int idir, sdir;

        if (used[i]) continue;
        lattice_embed_point(lattice, edges[i].a, &ix, &iy);
        idir = lattice_direction_index(lattice, edges[i]);

        if (start < 0) {
            start = i;
            continue;
        }

        lattice_embed_point(lattice, edges[start].a, &sx, &sy);
        sdir = lattice_direction_index(lattice, edges[start]);
        if (ix < sx ||
            (ix == sx && iy < sy) ||
            (ix == sx && iy == sy && idir < sdir)) {
            start = i;
        }
    }

    return start;
}

static int extract_cycles(const LEdge *in, int in_n, int prefer_left, int lattice, Poly *out,
                          AttachStatus *status) {
    Edge edges[MAX_LOCAL];
    int m = 0;
    int used[MAX_LOCAL] = {0};

    out->cycle_count = 0;

    for (int i = 0; i < in_n; i++)
        if (!in[i].canceled)
            edges[m++] = in[i].e;

    if (m == 0) {
        attach_status_set(status, ATTACH_STATUS_GEOMETRY);
        return 0;
    }

    while (1) {
        int start = find_start_edge(edges, m, used, lattice);
        if (start < 0) break;

        if (out->cycle_count >= MAX_CYCLES) {
            attach_status_set(status, ATTACH_STATUS_CYCLE_BOUND);
            return 0;
        }

        int used_snapshot[MAX_LOCAL];
        memcpy(used_snapshot, used, sizeof(used));

        Cycle c;
        int r = walk_one_cycle(edges, m, used, start, prefer_left, lattice, &c);

        if (r == WALK_DUP) {
            memcpy(used, used_snapshot, sizeof(used));
            r = walk_one_cycle(edges, m, used, start, !prefer_left, lattice, &c);
        }

        if (r == WALK_BOUND) {
            attach_status_set(status, ATTACH_STATUS_CYCLE_BOUND);
            return 0;
        }
        if (r != WALK_OK) {
            attach_status_set(status, ATTACH_STATUS_GEOMETRY);
            return 0;
        }

        out->cycles[out->cycle_count++] = c;
    }

    return out->cycle_count;
}


/* ---------- overlap validation ---------- */

static int find_outer_cycle(const Poly *p, int lattice) {
    int outer = 0;
    long long best = cycle_signed_area2(&p->cycles[0], lattice);
    if (best < 0) best = -best;
    for (int i = 1; i < p->cycle_count; i++) {
        long long area = cycle_signed_area2(&p->cycles[i], lattice);
        if (area < 0) area = -area;
        if (area > best) {
            best = area;
            outer = i;
        }
    }
    return outer;
}

static int has_overlap_via_tile_test(const Poly *p, const Cycle *tile, int lattice) {
    int outer = find_outer_cycle(p, lattice);
    for (int i = 0; i < p->cycle_count; i++) {
        const Cycle *c;
        double px, py;
        if (i == outer) continue;
        c = &p->cycles[i];
        if (c->n < 3) continue;
        if (!cycle_interior_point(c, lattice, &px, &py)) continue;
        if (point_in_cycle(px, py, tile, lattice)) return 1;
    }
    return 0;
}


/* ---------- overall controllers ---------- */

int try_attach_tile_poly(const Poly *base, const Cycle *tile_variant,
                         int lattice,
                         int base_edge_index, int tile_edge_index,
                         Poly *out) {
    return try_attach_tile_poly_ex(base, tile_variant, lattice,
                                   base_edge_index, tile_edge_index,
                                   out, NULL);
}

int try_attach_tile_poly_ex(const Poly *base, const Cycle *tile_variant,
                            int lattice,
                            int base_edge_index, int tile_edge_index,
                            Poly *out,
                            Cycle *aligned_out) {
    return try_attach_tile_poly_ex_status(base,
                                          tile_variant,
                                          lattice,
                                          base_edge_index,
                                          tile_edge_index,
                                          out,
                                          aligned_out,
                                          NULL);
}

int try_attach_tile_poly_ex_status(const Poly *base,
                                   const Cycle *tile_variant,
                                   int lattice,
                                   int base_edge_index,
                                   int tile_edge_index,
                                   Poly *out,
                                   Cycle *aligned_out,
                                   AttachStatus *status_out) {
    Edge frontier[MAX_VERTS * MAX_CYCLES];
    Cycle aligned;
    LEdge merged[MAX_LOCAL];
    int frontier_n;
    int merged_n;

    attach_status_set(status_out, ATTACH_STATUS_GEOMETRY);
    if (!base || !tile_variant || !out) {
        attach_status_set(status_out, ATTACH_STATUS_INTERNAL_BOUND);
        return 0;
    }

    frontier_n = build_boundary_edges(base, frontier);
    if (frontier_n < 0) {
        attach_status_set(status_out, ATTACH_STATUS_INTERNAL_BOUND);
        return 0;
    }
    if (frontier_n + tile_variant->n > MAX_LOCAL) {
        attach_status_set(status_out, ATTACH_STATUS_BOUNDARY_BOUND);
        return 0;
    }
    if (base_edge_index < 0 || base_edge_index >= frontier_n) return 0;

    if (!align_tile(tile_variant, tile_edge_index, frontier[base_edge_index], lattice, &aligned)) return 0;

    if (!build_union_edges(base, &aligned, merged, &merged_n, status_out)) return 0;
    if (!extract_cycles(merged, merged_n, 1, lattice, out, status_out)) return 0;

    if (has_overlap_via_tile_test(out, &aligned, lattice)) return 0;
    if (aligned_out) *aligned_out = aligned;

    attach_status_set(status_out, ATTACH_STATUS_OK);
    return 1;
}
