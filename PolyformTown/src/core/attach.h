#ifndef ATTACH_H
#define ATTACH_H

#include "core/cycle.h"
#include "core/tile.h"

int poly_has_boundary_vertex(const Poly *p, Coord v);
int align_tile_to_frontier_edge(const Poly *base, const Cycle *tile_variant,
                                int base_edge_index, int tile_edge_index,
                                Cycle *aligned);
int try_attach_tile_poly(const Poly *base, const Cycle *tile_variant,
                         int lattice,
                         int base_edge_index, int tile_edge_index,
                         Poly *out);
typedef enum {
    ATTACH_STATUS_GEOMETRY = 0,
    ATTACH_STATUS_OK = 1,
    ATTACH_STATUS_BOUNDARY_BOUND = -1,
    ATTACH_STATUS_CYCLE_BOUND = -2,
    ATTACH_STATUS_INTERNAL_BOUND = -3
} AttachStatus;

int try_attach_tile_poly_ex(const Poly *base, const Cycle *tile_variant,
                            int lattice,
                            int base_edge_index, int tile_edge_index,
                            Poly *out,
                            Cycle *aligned_out);

int try_attach_tile_poly_ex_status(const Poly *base,
                                   const Cycle *tile_variant,
                                   int lattice,
                                   int base_edge_index,
                                   int tile_edge_index,
                                   Poly *out,
                                   Cycle *aligned_out,
                                   AttachStatus *status_out);

#endif
