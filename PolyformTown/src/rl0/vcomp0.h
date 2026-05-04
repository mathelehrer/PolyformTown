#ifndef RL0_VCOMP0_H
#define RL0_VCOMP0_H

#include "throughput/vcomp_pipeline.h"
#include "rl0/forget_map.h"
#include "rl0/boundary0.h"

typedef struct {
    Boundary0Stats boundary_stats;
    int states_rejected_by_dictionary;
    int states_checked_by_dictionary;
    int prescribed_vertices;
    int prescribed_arcs;
    int prescribed_successes;
} VComp0Stats;

void vcomp0_stats_init(VComp0Stats *stats);
void run_vcomp0_levels(const Tile *tile,
                       const RL0ForgetMap *map,
                       int max_n,
                       int live_only,
                       VCompLevelFn on_level,
                       void *userdata,
                       VComp0Stats *stats);

#endif
