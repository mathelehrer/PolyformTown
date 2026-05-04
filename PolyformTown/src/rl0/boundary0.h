#ifndef RL0_BOUNDARY0_H
#define RL0_BOUNDARY0_H

#include "core/cycle.h"
#include "core/tile.h"
#include "rl0/forget_map.h"

typedef struct {
    int vertices_checked;
    int dictionary_misses;
    int max_incident;
} Boundary0Stats;

void boundary0_stats_init(Boundary0Stats *stats);
int boundary0_build_vertex_arc(const Tile *tile,
                               const Cycle *tiles,
                               int tile_count,
                               Coord q,
                               RL0FMArc *arc);
int boundary0_poly_has_live_boundary(const Poly *p,
                                     const Tile *tile,
                                     const Cycle *tiles,
                                     int tile_count,
                                     const RL0ForgetMap *map,
                                     Boundary0Stats *stats);

#endif
