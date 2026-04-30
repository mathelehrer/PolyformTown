#ifndef DFS_H
#define DFS_H

#include <stddef.h>

typedef struct {
    size_t nodes_visited;
    size_t validity_prunes;
    size_t solutions_found;
    size_t solutions_kept;
} DfsStats;

typedef int (*DfsNextFn)(const void *state,
                         size_t depth,
                         int *cursor,
                         void *child_state,
                         int *candidate_id,
                         void *ctx);
typedef int (*DfsValidityFn)(const void *state,
                             size_t depth,
                             void *ctx);
typedef int (*DfsVeracityFn)(const void *state,
                             size_t depth,
                             void *ctx);
typedef void (*DfsSolutionFn)(const void *state,
                              size_t depth,
                              void *ctx);
typedef void (*DfsTraceFn)(size_t depth,
                           const void *state,
                           int candidate_id,
                           int event,
                           void *ctx);
enum {
    DFS_TRACE_ENTER = 1,
    DFS_TRACE_PRUNE = 2,
    DFS_TRACE_BACKTRACK = 3,
    DFS_TRACE_SOLUTION = 4,
};

typedef struct {
    size_t state_size;
    size_t max_depth;
    size_t max_results;
    int stop_on_first;
    DfsNextFn next;
    DfsValidityFn validity;
    DfsVeracityFn veracity;
    DfsSolutionFn on_solution;
    DfsTraceFn trace;
    void *ctx;
} DfsConfig;

int dfs_run(const DfsConfig *cfg,
            const void *init_state,
            DfsStats *stats_out);

#endif
