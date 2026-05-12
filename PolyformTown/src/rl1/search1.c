#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rl1/bcomp1.h"

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [--seed-tile | --input PATH --record N] [options]\n"
            "defaults:\n"
            "  input:  data/rl1/completions.dat when --record is used\n"
            "  tile:   preferences/focus.tile\n"
            "  output: data/rl1/search.dat\n"
            "  depth:  1\n"
            "options:\n"
            "  --record N         complete children of input record N\n"
            "  --seed-tile        start from the base tile instead of an input record\n"
            "  --input PATH       default data/rl1/completions.dat\n"
            "  --output PATH      default data/rl1/search.dat\n"
            "  --stdout           print records to stdout after successful search\n"
            "  --print            compatibility alias for --stdout\n"
            "  --tile PATH        default preferences/focus.tile\n"
            "  --depth N          default 1\n"
            "  --max-tiles N      default attach limit; cap hit makes search fail\n"
            "  --max-dfs N        default unlimited; cap hit makes search fail\n"
            "  --live-only        prune dead boundary after every attach, default\n"
            "  --all-final        disable live-boundary pruning\n"
            "  --remembrance PATH default data/rl0/remembrance.dat\n"
            "  --deletions PATH   default data/rl0/deletions.dat\n",
            prog);
}

static int has_escape(const BComp1Stats *s) {
    return s->truncated ||
           s->max_tiles_stop ||
           s->attach_tile_capacity_hit ||
           s->attach_tile_limit_hit ||
           s->hidden_rebuild_fail ||
           s->cycle_capacity_seen;
}

static size_t escape_count(const BComp1Stats *s) {
    return s->truncated +
           s->max_tiles_stop +
           s->attach_tile_capacity_hit +
           s->attach_tile_limit_hit +
           s->hidden_rebuild_fail +
           s->cycle_capacity_seen;
}

static int write_records(const char *path, int to_stdout, BComp1RecordVec *records) {
    FILE *fp = stdout;
    if (!to_stdout) {
        fp = fopen(path, "w");
        if (!fp) {
            fprintf(stderr, "ERROR: failed to write output: %s\n", path);
            return 0;
        }
    }
    bcomp1_sort_records(records);
    for (size_t i = 0; i < records->count; i++) {
        bcomp1_print_record(fp, i + 1, &records->items[i]);
    }
    if (!to_stdout && fclose(fp) != 0) {
        fprintf(stderr, "ERROR: failed while closing output: %s\n", path);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *tile_path = "preferences/focus.tile";
    const char *input_path = "data/rl1/completions.dat";
    const char *output_path = "data/rl1/search.dat";
    const char *remembrance_path = "data/rl0/remembrance.dat";
    const char *deletions_path = "data/rl0/deletions.dat";
    int seed_tile = 0;
    int record_index = 0;
    int to_stdout = 0;
    BComp1Options opts;
    BComp1Context ctx;
    BComp1RecordVec records = {0};
    BComp1State seed;
    BComp1Result result;
    Cycle center;

    bcomp1_options_default(&opts);
    opts.collect_records = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seed-tile") == 0) seed_tile = 1;
        else if (strcmp(argv[i], "--stdout") == 0 || strcmp(argv[i], "--print") == 0) to_stdout = 1;
        else if (strcmp(argv[i], "--live-only") == 0) opts.live_only = 1;
        else if (strcmp(argv[i], "--all-final") == 0) opts.live_only = 0;
        else if (strcmp(argv[i], "--tile") == 0 && i + 1 < argc) tile_path = argv[++i];
        else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) input_path = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) output_path = argv[++i];
        else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) record_index = atoi(argv[++i]);
        else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) opts.depth = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-tiles") == 0 && i + 1 < argc) opts.max_tiles = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-dfs") == 0 && i + 1 < argc) opts.max_dfs = (size_t)atoll(argv[++i]);
        else if (strcmp(argv[i], "--remembrance") == 0 && i + 1 < argc) remembrance_path = argv[++i];
        else if (strcmp(argv[i], "--deletions") == 0 && i + 1 < argc) deletions_path = argv[++i];
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(argv[0]); return 0; }
        else { usage(argv[0]); return 1; }
    }

    if (!seed_tile && record_index <= 0) { usage(argv[0]); return 1; }
    if (seed_tile && record_index > 0) { usage(argv[0]); return 1; }

    if (!bcomp1_context_init(&ctx, tile_path, remembrance_path, deletions_path)) {
        fprintf(stderr, "ERROR: failed to initialize bcomp1 context\n");
        return 1;
    }

    if (seed_tile) {
        if (!bcomp1_make_seed_state(&ctx, &seed)) {
            fprintf(stderr, "ERROR: failed to make seed tile state\n");
            bcomp1_context_clear(&ctx);
            return 1;
        }
        center = ctx.tile.base;
    } else {
        if (!bcomp1_load_records(input_path, &records)) {
            fprintf(stderr, "ERROR: failed to load records: %s\n", input_path);
            bcomp1_context_clear(&ctx);
            return 1;
        }
        if ((size_t)record_index > records.count) {
            fprintf(stderr, "ERROR: record index %d out of range; count=%zu\n", record_index, records.count);
            bcomp1_free_records(&records);
            bcomp1_context_clear(&ctx);
            return 1;
        }
        if (!bcomp1_state_from_record(&records.items[record_index - 1], &seed)) {
            fprintf(stderr, "ERROR: record %d malformed\n", record_index);
            bcomp1_free_records(&records);
            bcomp1_context_clear(&ctx);
            return 1;
        }
        center = records.items[record_index - 1].center;
    }

    if (!bcomp1_complete_state(&ctx, &seed, &center, &opts, &result)) {
        fprintf(stderr, "ERROR: search failed\n");
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 1;
    }

    if (has_escape(&result.stats)) {
        fprintf(stderr,
                "ERROR: search incomplete; escapes=%zu trunc=%zu max_tiles_stop=%zu attach_cap=%zu attach_limit=%zu hidden=%zu cycle=%zu max(tile=%zu hidden=%zu bverts=%zu cverts=%zu)\n",
                escape_count(&result.stats),
                result.stats.truncated,
                result.stats.max_tiles_stop,
                result.stats.attach_tile_capacity_hit,
                result.stats.attach_tile_limit_hit,
                result.stats.hidden_rebuild_fail,
                result.stats.cycle_capacity_seen,
                result.stats.max_tile_count_seen,
                result.stats.max_hidden_count_seen,
                result.stats.max_boundary_vertices_seen,
                result.stats.max_cycle_vertices_seen);
        bcomp1_result_clear(&result);
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 2;
    }

    if (!write_records(output_path, to_stdout, &result.records)) {
        bcomp1_result_clear(&result);
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 1;
    }

    fprintf(stderr,
            "rl1_search depth=%d outputs=%zu file=%s attempts=%zu successes=%zu duplicates=%zu dead=%zu illegal=%zu no_dict=%zu dfs=%zu max(tile=%zu hidden=%zu bverts=%zu cverts=%zu)\n",
            opts.depth,
            result.stats.outputs,
            to_stdout ? "stdout" : output_path,
            result.stats.attach_attempts,
            result.stats.attach_successes,
            result.stats.duplicates,
            result.stats.filtered_dead,
            result.stats.filtered_illegal,
            result.stats.no_dictionary,
            result.stats.dfs_calls,
            result.stats.max_tile_count_seen,
            result.stats.max_hidden_count_seen,
            result.stats.max_boundary_vertices_seen,
            result.stats.max_cycle_vertices_seen);

    bcomp1_result_clear(&result);
    bcomp1_free_records(&records);
    bcomp1_context_clear(&ctx);
    return 0;
}
