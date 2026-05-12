#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "rl1/bcomp1.h"

#define RA_MAX_RECORDS 8192
#define RA_DEPTH_COUNT 2

typedef enum {
    RA_LIVING = 0,
    RA_ESCAPED,
    RA_DEAD,
    RA_UNKNOWN
} RAStatus;

typedef struct {
    int dead[RA_MAX_RECORDS];
    int dead_count;
    int escaped[RA_MAX_RECORDS];
    int escaped_count;
    int unknown[RA_MAX_RECORDS];
    int unknown_count;
    int living_count;
    int total;
} DepthSummary;

typedef struct {
    const char *kind;
    const char *source;
    DepthSummary depth[RA_DEPTH_COUNT];
} DatasetSummary;

typedef struct {
    const char *tile_path;
    const char *remembrance_path;
    const char *deletions_path;
    const char *output_path;
    int max_tiles;
    size_t max_dfs;
    size_t progress_interval;
    int progress_depth;
} Options;

static const char *DEFAULT_SOURCES[3] = {
    "data/rl1/completions.dat",
    "data/rl1/completions_d1.dat",
    "data/rl1/completions_d2.dat"
};

static const char *DEFAULT_KINDS[3] = {
    "hexagon_base",
    "supertile_d1",
    "supertile_d2"
};

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [options]\n"
            "defaults:\n"
            "  output: data/rl1/deletions.dat\n"
            "  tile:   preferences/focus.tile\n"
            "inputs checked, not generated:\n"
            "  data/rl1/completions.dat\n"
            "  data/rl1/completions_d1.dat\n"
            "  data/rl1/completions_d2.dat\n"
            "options:\n"
            "  --output PATH       override deletions output path\n"
            "  --tile PATH         override tile file\n"
            "  --max-tiles N       hard tile cap for each refine search\n"
            "  --max-dfs N         hard DFS cap; cap hits are escaped\n"
            "  --progress N        update stderr every N DFS calls\n"
            "  --progress-depth N  branch path depth to display, default 8\n"
            "  --remembrance PATH  default data/rl0/remembrance.dat\n"
            "  --deletions PATH    default data/rl0/deletions.dat\n",
            prog);
}

static void options_default(Options *opt) {
    opt->tile_path = "preferences/focus.tile";
    opt->remembrance_path = "data/rl0/remembrance.dat";
    opt->deletions_path = "data/rl0/deletions.dat";
    opt->output_path = "data/rl1/deletions.dat";
    opt->max_tiles = 0;
    opt->max_dfs = 0;
    opt->progress_interval = 0;
    opt->progress_depth = 8;
}

static int parse_args(int argc, char **argv, Options *opt) {
    options_default(opt);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) opt->output_path = argv[++i];
        else if (strcmp(argv[i], "--tile") == 0 && i + 1 < argc) opt->tile_path = argv[++i];
        else if (strcmp(argv[i], "--max-tiles") == 0 && i + 1 < argc) opt->max_tiles = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-dfs") == 0 && i + 1 < argc) opt->max_dfs = (size_t)atoll(argv[++i]);
        else if (strcmp(argv[i], "--progress") == 0 && i + 1 < argc) opt->progress_interval = (size_t)atoll(argv[++i]);
        else if (strcmp(argv[i], "--progress-depth") == 0 && i + 1 < argc) opt->progress_depth = atoi(argv[++i]);
        else if (strcmp(argv[i], "--remembrance") == 0 && i + 1 < argc) opt->remembrance_path = argv[++i];
        else if (strcmp(argv[i], "--deletions") == 0 && i + 1 < argc) opt->deletions_path = argv[++i];
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(argv[0]); exit(0); }
        else return 0;
    }
    return 1;
}

static int file_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int has_escape(const BComp1Stats *s) {
    return s->truncated ||
           s->max_tiles_stop ||
           s->attach_tile_capacity_hit ||
           s->attach_tile_limit_hit ||
           s->hidden_rebuild_fail ||
           s->cycle_capacity_seen;
}

static RAStatus classify_stats(const BComp1Stats *s) {
    if (s->outputs > 0) return RA_LIVING;
    if (has_escape(s)) return RA_ESCAPED;
    return RA_DEAD;
}

static const char *status_name(RAStatus st) {
    switch (st) {
        case RA_LIVING: return "living";
        case RA_ESCAPED: return "escaped";
        case RA_DEAD: return "dead";
        case RA_UNKNOWN: return "unknown";
    }
    return "unknown";
}

static void push_index(int *items, int *count, int idx) {
    if (*count < RA_MAX_RECORDS) items[(*count)++] = idx;
}

static void write_index_list(FILE *fp, const char *label, const int *items, int count) {
    fprintf(fp, "%s:", label);
    for (int i = 0; i < count; i++) fprintf(fp, " %d", items[i]);
    fprintf(fp, "\n");
}

static int refine_dataset_depth(const Options *opt,
                                BComp1Context *ctx,
                                const char *kind,
                                const char *source,
                                int depth,
                                DepthSummary *summary) {
    BComp1RecordVec records = {0};
    BComp1Options opts;
    memset(summary, 0, sizeof(*summary));

    if (!bcomp1_load_records(source, &records)) {
        fprintf(stderr, "failed to load records: %s\n", source);
        return 0;
    }
    if (records.count > RA_MAX_RECORDS) {
        fprintf(stderr, "too many records in %s: %zu > %d\n", source, records.count, RA_MAX_RECORDS);
        bcomp1_free_records(&records);
        return 0;
    }

    bcomp1_options_default(&opts);
    opts.depth = depth;
    opts.collect_records = 0;
    opts.stop_after_output = 1;
    if (opt->max_tiles > 0) opts.max_tiles = opt->max_tiles;
    if (opt->max_dfs > 0) opts.max_dfs = opt->max_dfs;
    opts.progress_interval = opt->progress_interval;
    opts.progress_depth = opt->progress_depth;

    fprintf(stderr, "refine_all kind=%s source=%s depth=%d records=%zu\n",
            kind, source, depth, records.count);

    for (size_t i = 0; i < records.count; i++) {
        BComp1State seed;
        BComp1Result result;
        RAStatus st;
        char label[128];
        int idx = (int)i + 1;
        snprintf(label, sizeof(label), "%s:d%d:record=%d", kind, depth, idx);
        opts.progress_label = label;

        if (!bcomp1_state_from_record(&records.items[i], &seed)) {
            st = RA_UNKNOWN;
            push_index(summary->unknown, &summary->unknown_count, idx);
            fprintf(stderr, "  record=%d status=%s malformed\n", idx, status_name(st));
            continue;
        }
        if (!bcomp1_complete_state(ctx, &seed, &records.items[i].center, &opts, &result)) {
            st = RA_UNKNOWN;
            push_index(summary->unknown, &summary->unknown_count, idx);
            fprintf(stderr, "  record=%d status=%s search_failed\n", idx, status_name(st));
            continue;
        }
        st = classify_stats(&result.stats);
        if (st == RA_DEAD) push_index(summary->dead, &summary->dead_count, idx);
        else if (st == RA_ESCAPED) push_index(summary->escaped, &summary->escaped_count, idx);
        else if (st == RA_UNKNOWN) push_index(summary->unknown, &summary->unknown_count, idx);
        else summary->living_count++;

        fprintf(stderr,
                "  record=%d status=%s outputs=%zu escapes=%zu dfs=%zu\n",
                idx,
                status_name(st),
                result.stats.outputs,
                (size_t)has_escape(&result.stats),
                result.stats.dfs_calls);
        bcomp1_result_clear(&result);
    }
    summary->total = (int)records.count;
    bcomp1_free_records(&records);
    return 1;
}

static int write_deletions_file(const char *path, const DatasetSummary *sets, int set_count) {
    FILE *fp = fopen(path, "w");
    if (!fp) return 0;
    fprintf(fp, "# RL1 refine-all deletion candidates.\n");
    fprintf(fp, "# Indices are 1-based and relative to each section source file.\n");
    fprintf(fp, "# Escaped/unknown records are not deletions; inspect before promoting.\n");
    for (int s = 0; s < set_count; s++) {
        fprintf(fp, "---[%d]---\n", s);
        fprintf(fp, "source: %s\n", sets[s].source);
        fprintf(fp, "kind: %s\n", sets[s].kind);
        for (int d = 0; d < RA_DEPTH_COUNT; d++) {
            const DepthSummary *sum = &sets[s].depth[d];
            int depth = d + 1;
            fprintf(fp,
                    "depth_%d_summary: total=%d living=%d dead=%d escaped=%d unknown=%d\n",
                    depth,
                    sum->total,
                    sum->living_count,
                    sum->dead_count,
                    sum->escaped_count,
                    sum->unknown_count);
            {
                char label[64];
                snprintf(label, sizeof(label), "depth_%d_dead", depth);
                write_index_list(fp, label, sum->dead, sum->dead_count);
                snprintf(label, sizeof(label), "depth_%d_escaped", depth);
                write_index_list(fp, label, sum->escaped, sum->escaped_count);
                snprintf(label, sizeof(label), "depth_%d_unknown", depth);
                write_index_list(fp, label, sum->unknown, sum->unknown_count);
            }
        }
    }
    fclose(fp);
    return 1;
}

int main(int argc, char **argv) {
    Options opt;
    BComp1Context ctx;
    DatasetSummary sets[3];
    int ok = 1;

    if (!parse_args(argc, argv, &opt)) {
        usage(argv[0]);
        return 1;
    }

    for (int i = 0; i < 3; i++) {
        if (!file_exists(DEFAULT_SOURCES[i])) {
            fprintf(stderr,
                    "missing required input: %s\n"
                    "Run ./bin/rl1_supertiles first to generate supertile data.\n",
                    DEFAULT_SOURCES[i]);
            return 1;
        }
    }

    if (!bcomp1_context_init(&ctx,
                             opt.tile_path,
                             opt.remembrance_path,
                             opt.deletions_path)) {
        fprintf(stderr, "failed to initialize RL1 context\n");
        return 1;
    }

    memset(sets, 0, sizeof(sets));
    for (int s = 0; s < 3; s++) {
        sets[s].kind = DEFAULT_KINDS[s];
        sets[s].source = DEFAULT_SOURCES[s];
        for (int d = 0; d < RA_DEPTH_COUNT; d++) {
            if (!refine_dataset_depth(&opt, &ctx, sets[s].kind, sets[s].source, d + 1, &sets[s].depth[d])) {
                ok = 0;
                break;
            }
        }
        if (!ok) break;
    }

    bcomp1_context_clear(&ctx);
    if (!ok) return 1;

    if (!write_deletions_file(opt.output_path, sets, 3)) {
        fprintf(stderr, "failed to write %s\n", opt.output_path);
        return 1;
    }

    fprintf(stderr, "wrote %s\n", opt.output_path);
    return 0;
}
