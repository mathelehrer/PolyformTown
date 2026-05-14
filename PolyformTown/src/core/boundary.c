#include "core/boundary.h"
#include "core/cycle.h"
#include "core/attach.h"

#include <stdlib.h>

#define MAX_BOUNDARY_VERTS (MAX_VERTS * MAX_CYCLES)

static int dfs_has_completion(const Poly *p,
                              const Tile *tile,
                              Coord target);

/* membership functions  */
static int coord_in_list(const Coord *verts, int count, Coord v) {
    for (int i = 0; i < count; i++) if (coord_eq(verts[i], v)) return 1;
    return 0;
}

static int point_on_poly_boundary(const Poly *p, Coord q, int lattice) {
    (void)lattice;  // no longer needed

    for (int i = 0; i < p->cycle_count; i++) {
        const Cycle *c = &p->cycles[i];
        for (int j = 0; j < c->n; j++) {
            if (coord_eq(c->v[j], q)) return 1;
        }
    }
    return 0;
}


/* boundary checking top level */

int build_boundary_vertices(const Poly *p, Coord *verts) {
    int count = 0;
    for (int i = 0; i < p->cycle_count; i++) {
        const Cycle *c = &p->cycles[i];
        for (int j = 0; j < c->n; j++) {
            Coord v = c->v[j];
            if (!coord_in_list(verts, count, v)) {
                if (count >= MAX_BOUNDARY_VERTS) return -1;
                verts[count++] = v;
            }
        }
    }
    return count;
}

int build_boundary_edges(const Poly *p, Edge *edges) {
    int n = 0;
    for (int i = 0; i < p->cycle_count; i++)
        for (int j = 0; j < p->cycles[i].n; j++)
            edges[n++] = cycle_edge(&p->cycles[i], j);
    return n;
}

int poly_has_live_boundary(const Poly *p, const Tile *tile) {
    Coord *verts = malloc(sizeof(*verts) * (size_t)(2 * MAX_BOUNDARY_VERTS));
    int vc;
    int ok = 1;

    if (!verts) return 0;
    vc = build_boundary_vertices(p, verts);
    if (vc < 0 || vc > 2 * MAX_BOUNDARY_VERTS) {
        free(verts);
        return 0;
    }
    for (int i = 0; i < vc; i++) {
        if (!dfs_has_completion(p, tile, verts[i])) {
            ok = 0;
            break;
        }
    }
    free(verts);
    return ok;
}

/* boundary checking top level */

static int dfs_has_completion(const Poly *p,
                              const Tile *tile,
                              Coord target)
{
    Edge *edges = malloc(sizeof(*edges) * (size_t)(2 * MAX_BOUNDARY_VERTS));
    Poly *grown = malloc(sizeof(*grown));
    Cycle *aligned = malloc(sizeof(*aligned));
    int ec;
    int result = 0;

    if (!edges || !grown || !aligned) goto done;
    ec = build_boundary_edges(p, edges);

    for (int be = 0; be < ec && !result; be++) {

        if (!coord_eq(edges[be].a, target))
            continue;

        for (int v = 0; v < tile->variant_count && !result; v++) {
            const Cycle *tv = &tile->variants[v];

            for (int te = 0; te < tv->n; te++) {
                if (!try_attach_tile_poly_ex(p, tv, tile->lattice,
                                             be, te, grown, aligned))
                    continue;

                /* success: target no longer on boundary */
                if (!point_on_poly_boundary(grown, target, tile->lattice)) {
                    result = 1;
                    break;
                }

                if (dfs_has_completion(grown, tile, target)) {
                    result = 1;
                    break;
                }
            }
        }
    }

done:
    free(aligned);
    free(grown);
    free(edges);
    return result;
}
