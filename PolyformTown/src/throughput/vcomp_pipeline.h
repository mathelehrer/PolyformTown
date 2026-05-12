#ifndef VCOMP_PIPELINE_H
#define VCOMP_PIPELINE_H

#include "core/tile.h"

#define VCOMP_MAX_LEVELS 256
#define VCOMP_MAX_HIDDEN (MAX_VERTS * MAX_CYCLES)
#define VCOMP_MAX_TILES 128

typedef struct {
    Poly poly;
    Coord hidden[VCOMP_MAX_HIDDEN];
    int hidden_count;
    Coord ports[VCOMP_MAX_HIDDEN];
    int port_count;
    int use_ports_identity;
    Cycle tiles[VCOMP_MAX_TILES];
    int tile_count;
} VCompState;

typedef struct {
    VCompState *data;
    size_t count, cap;
} VCompStateVec;

typedef int (*VCompLevelFn)(int level,
                            const VCompStateVec *states,
                            size_t unique_poly_count,
                            const Tile *tile,
                            void *userdata);

void run_vcomp_levels(const Tile *tile,
                      int max_n,
                      int track_tiles,
                      int live_only,
                      VCompLevelFn on_level,
                      void *userdata);

#endif
