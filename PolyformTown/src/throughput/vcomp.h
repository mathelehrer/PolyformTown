#ifndef VCOMP_H
#define VCOMP_H

#include "core/cycle.h"
#include "core/tile.h"

#define VCOMP_MAX_HIDDEN (MAX_VERTS * MAX_CYCLES)
#define VCOMP_MAX_TRACE MAX_VERTS
#define VCOMP_MAX_LEVELS 256

typedef struct {
    Poly poly;
    Coord target;
    Coord hidden[VCOMP_MAX_HIDDEN];
    int hidden_count;
    Coord ports[VCOMP_MAX_HIDDEN];
    int port_count;
    Cycle tiles[VCOMP_MAX_TRACE];
    int tile_count;
} VCompRawState;

typedef struct {
    VCompRawState *data;
    size_t count;
    size_t cap;
} VCompRawVec;

typedef struct {
    VCompRawVec levels[VCOMP_MAX_LEVELS];
    int max_level;
} VCompLevels;

void vcomp_levels_init(VCompLevels *out, int max_level);
void vcomp_levels_destroy(VCompLevels *out);

void vcomp_enumerate_levels(const Poly *base,
                            const Tile *tile,
                            Coord target,
                            const Coord *initial_hidden,
                            int initial_hidden_count,
                            const Coord *initial_ports,
                            int initial_port_count,
                            const Cycle *initial_tiles,
                            int initial_tile_count,
                            int max_hidden,
                            int track_tiles,
                            VCompLevels *out);

#endif
