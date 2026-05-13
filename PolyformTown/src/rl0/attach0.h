#ifndef RL0_ATTACH0_H
#define RL0_ATTACH0_H

#include "core/cycle.h"
#include "core/tile.h"
#include "core/attach.h"
#include "rl0/forget_map.h"

#define ATTACH0_MAX_TILES 128

typedef struct {
    int variant_order_checked;
    int variant_order_failures;
    int attach_one_attempts;
    int attach_one_successes;
    int attach_arc_attempts;
    int attach_arc_successes;
} Attach0Stats;

typedef struct {
    int vertices_checked;
    int zero_choice_failures;
    int forced_vertices;
    int forced_successes;
    int forced_failures;
    int unresolved_vertices;
    int closure_steps;
} Attach0ClosureStats;

void attach0_stats_init(Attach0Stats *stats);
void attach0_closure_stats_init(Attach0ClosureStats *stats);
int attach0_verify_variant_order(const Tile *tile, Attach0Stats *stats);

int attach0_try_attach_one(const Poly *base,
                           const Tile *tile,
                           Coord target,
                           RL0FMItem item,
                           Poly *out,
                           Cycle *aligned_out,
                           Attach0Stats *stats);

int attach0_try_attach_arc(const Poly *base,
                           const Tile *tile,
                           const Cycle *tiles,
                           int tile_count,
                           Coord target,
                           const RL0FMArc *arc,
                           Poly *out,
                           Cycle *out_tiles,
                           int *out_tile_count,
                           Attach0Stats *stats);

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
                                  AttachStatus *status_out);

int attach0_force_live_closure(Poly *poly,
                               const Tile *tile,
                               Cycle *tiles,
                               int *tile_count,
                               const RL0ForgetMap *map,
                               int max_steps,
                               Attach0Stats *attach_stats,
                               Attach0ClosureStats *closure_stats);

int attach0_force_live_closure_status(Poly *poly,
                                      const Tile *tile,
                                      Cycle *tiles,
                                      int *tile_count,
                                      const RL0ForgetMap *map,
                                      int max_steps,
                                      Attach0Stats *attach_stats,
                                      Attach0ClosureStats *closure_stats,
                                      AttachStatus *status_out);

#endif
