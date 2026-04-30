#include "core/dfs.h"

#include <stdlib.h>

static int should_stop(const DfsConfig *cfg, const DfsStats *stats) {
    if (cfg->stop_on_first && stats->solutions_kept > 0) {
        return 1;
    }
    if (cfg->max_results > 0 && stats->solutions_kept >= cfg->max_results) {
        return 1;
    }
    return 0;
}

static int dfs_walk(const DfsConfig *cfg,
                    const void *state,
                    size_t depth,
                    DfsStats *stats) {
    enum { DFS_CONTINUE = 0, DFS_STOP = 1, DFS_ERROR = -1 };
    int cursor = 0;

    if (cfg->veracity && cfg->veracity(state, depth, cfg->ctx)) {
        if (cfg->trace) {
            cfg->trace(depth, state, -1, DFS_TRACE_SOLUTION, cfg->ctx);
        }
        stats->solutions_found++;
        if (!should_stop(cfg, stats)) {
            stats->solutions_kept++;
            if (cfg->on_solution) {
                cfg->on_solution(state, depth, cfg->ctx);
            }
        }
        if (should_stop(cfg, stats)) {
            return DFS_STOP;
        }
    }

    if (depth >= cfg->max_depth) {
        return 0;
    }

    for (;;) {
        int candidate_id = -1;
        void *child_state = malloc(cfg->state_size);
        int has_child;

        if (!child_state) {
            return DFS_ERROR;
        }

        has_child = cfg->next(state,
                              depth,
                              &cursor,
                              child_state,
                              &candidate_id,
                              cfg->ctx);
        if (!has_child) {
            free(child_state);
            break;
        }
        stats->nodes_visited++;
        if (cfg->validity && !cfg->validity(child_state, depth + 1, cfg->ctx)) {
            stats->validity_prunes++;
            if (cfg->trace) {
                cfg->trace(depth + 1,
                           child_state,
                           candidate_id,
                           DFS_TRACE_PRUNE,
                           cfg->ctx);
            }
            free(child_state);
            continue;
        }
        if (cfg->trace) {
            cfg->trace(depth + 1,
                       child_state,
                       candidate_id,
                       DFS_TRACE_ENTER,
                       cfg->ctx);
        }
        {
            int rc = dfs_walk(cfg, child_state, depth + 1, stats);
            if (rc != DFS_CONTINUE) {
                free(child_state);
                return rc;
            }
        }
        if (cfg->trace) {
            cfg->trace(depth, state, candidate_id, DFS_TRACE_BACKTRACK, cfg->ctx);
        }
        free(child_state);
    }

    return DFS_CONTINUE;
}

int dfs_run(const DfsConfig *cfg,
            const void *init_state,
            DfsStats *stats_out) {
    DfsStats stats = {0, 0, 0, 0};

    if (!cfg || !init_state || !cfg->next || cfg->state_size == 0) {
        return 0;
    }

    if (cfg->validity && !cfg->validity(init_state, 0, cfg->ctx)) {
        if (stats_out) {
            *stats_out = stats;
        }
        return 1;
    }

    if (dfs_walk(cfg, init_state, 0, &stats) < 0) {
        if (stats_out) {
            *stats_out = stats;
        }
        return 0;
    }

    if (stats_out) {
        *stats_out = stats;
    }
    return 1;
}
