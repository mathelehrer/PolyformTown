#ifndef RL1_BCOMP1_H
#define RL1_BCOMP1_H

#include <stddef.h>
#include <stdio.h>

#include "core/cycle.h"
#include "core/tile.h"
#include "rl0/attach0.h"
#include "rl0/forget_map.h"

#define BCOMP1_MAX_COORDS (MAX_VERTS * ATTACH0_MAX_TILES)

typedef struct {
    Poly poly;
    Cycle tiles[ATTACH0_MAX_TILES];
    int tile_count;
    Coord hidden[BCOMP1_MAX_COORDS];
    int hidden_count;
} BComp1State;

typedef struct {
    int level;
    int tile_count;
    int start_index;
    int dir;
    int have_center;
    int have_boundary;
    int have_hidden;
    int have_tiles;
    Cycle center;
    Poly boundary;
    Coord hidden[BCOMP1_MAX_COORDS];
    int hidden_count;
    Cycle tiles[ATTACH0_MAX_TILES];
    int tiles_count;
} BComp1Record;

typedef struct {
    BComp1Record *items;
    size_t count;
    size_t cap;
} BComp1RecordVec;

typedef struct {
    size_t dfs_calls;
    size_t attach_attempts;
    size_t attach_successes;
    size_t outputs;
    size_t duplicates;
    size_t filtered_dead;
    size_t filtered_illegal;
    size_t no_dictionary;
    size_t walker_zero_not_done;
    size_t walker_multi_port;
    size_t walker_completed;
    size_t exceptions;
    size_t max_depth_seen;
    size_t truncated;

    /* Capacity/cutoff tracing.  These must stay visible because hitting
       one of these can make a finite search incomplete. */
    size_t max_tile_count_seen;
    size_t max_hidden_count_seen;
    size_t max_boundary_vertices_seen;
    size_t max_cycle_vertices_seen;
    size_t max_tiles_stop;
    size_t attach_tile_capacity_hit;
    size_t attach_tile_limit_hit;
    size_t hidden_rebuild_fail;
    size_t cycle_capacity_seen;
} BComp1Stats;

typedef struct {
    int depth;
    int max_tiles;
    int live_only;
    int collect_records;
    size_t max_dfs;
    const char *debug_exceptions_path;
    int debug_exceptions_limit;
    size_t progress_interval;
    int progress_depth;
    int progress_tty;
    const char *progress_label;
    int stop_after_output;
} BComp1Options;

typedef struct {
    Tile tile;
    RL0ForgetMap map;
} BComp1Context;

typedef struct {
    BComp1Stats stats;
    BComp1RecordVec records;
} BComp1Result;

void bcomp1_options_default(BComp1Options *opts);
int bcomp1_context_init(BComp1Context *ctx,
                        const char *tile_path,
                        const char *remembrance_path,
                        const char *deletions_path);
void bcomp1_context_clear(BComp1Context *ctx);

int bcomp1_load_records(const char *path, BComp1RecordVec *out);
void bcomp1_free_records(BComp1RecordVec *vec);
int bcomp1_make_seed_state(const BComp1Context *ctx, BComp1State *state);
int bcomp1_state_from_record(const BComp1Record *record, BComp1State *state);
int bcomp1_complete_state(const BComp1Context *ctx,
                          const BComp1State *seed,
                          const Cycle *center,
                          const BComp1Options *opts,
                          BComp1Result *result);
void bcomp1_result_clear(BComp1Result *result);
void bcomp1_sort_records(BComp1RecordVec *records);
void bcomp1_print_record(FILE *fp, size_t index, const BComp1Record *record);

#endif
