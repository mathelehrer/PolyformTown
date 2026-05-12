#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rl1/bcomp1.h"

#define REFINE1_MAX_SELECT 4096

typedef enum {
    R1_LIVING = 0,
    R1_ESCAPED,
    R1_DEAD,
    R1_UNKNOWN
} R1Status;

typedef struct {
    int selected[REFINE1_MAX_SELECT];
    int count;
    int all;
} Selection;

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [all | N[,N...] ...] [options]\n"
            "defaults:\n"
            "  input: data/rl1/completions.dat\n"
            "  tile:  preferences/focus.tile\n"
            "  depth: 2\n"
            "options:\n"
            "  --input PATH        override candidate data\n"
            "  --tile PATH         override tile file\n"
            "  --depth N           rings to search, default 2\n"
            "  --max-tiles N       hard tile cap; cap hits are escaped\n"
            "  --max-dfs N         hard dfs cap; cap hits are escaped\n"
            "  --progress N        update stderr progress every N dfs calls\n"
            "  --progress-depth N  branch path depth to display, default 8\n"
            "  --print             accepted for compatibility; table is printed by default\n"
            "  --quiet             suppress per-record table; print summary only\n"
            "  --live-only         prune dead boundary after every attach, default\n"
            "  --all-final         disable live-boundary pruning\n"
            "  --remembrance PATH  default data/rl0/remembrance.dat\n"
            "  --deletions PATH    default data/rl0/deletions.dat\n",
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

static R1Status classify_stats(const BComp1Stats *s) {
    if (s->outputs > 0) return R1_LIVING;
    if (has_escape(s)) return R1_ESCAPED;
    return R1_DEAD;
}

static const char *status_name(R1Status st) {
    switch (st) {
        case R1_LIVING: return "living";
        case R1_ESCAPED: return "escaped";
        case R1_DEAD: return "dead";
        case R1_UNKNOWN: return "unknown";
    }
    return "unknown";
}

static int add_selected(Selection *sel, int idx) {
    if (idx <= 0) return 0;
    if (sel->count >= REFINE1_MAX_SELECT) return 0;
    sel->selected[sel->count++] = idx;
    return 1;
}

static int parse_selection_token(Selection *sel, const char *arg) {
    const char *p = arg;
    if (strcmp(arg, "all") == 0 || strcmp(arg, "--all") == 0) {
        sel->all = 1;
        return 1;
    }
    while (*p) {
        char *end = NULL;
        long v;
        while (*p == ',' || isspace((unsigned char)*p)) p++;
        if (!*p) break;
        v = strtol(p, &end, 10);
        if (end == p || v <= 0 || v > INT_MAX) return 0;
        if (!add_selected(sel, (int)v)) return 0;
        p = end;
        while (*p == ',' || isspace((unsigned char)*p)) p++;
        if (*p && !isdigit((unsigned char)*p)) return 0;
    }
    return 1;
}

static void print_header(void) {
    printf("%6s  %-7s %8s %9s %9s %10s %8s %8s %8s %10s %7s %8s %9s %10s %10s %10s\n",
           "record", "status", "outputs", "attempts", "success", "duplicates",
           "dead", "illegal", "no_dict", "dfs", "trunc", "escapes",
           "max_tiles", "max_hidden", "max_bverts", "max_cverts");
}

static void print_row(size_t idx, R1Status st, const BComp1Stats *s) {
    printf("%6zu  %-7s %8zu %9zu %9zu %10zu %8zu %8zu %8zu %10zu %7zu %8zu %9zu %10zu %10zu %10zu\n",
           idx,
           status_name(st),
           s->outputs,
           s->attach_attempts,
           s->attach_successes,
           s->duplicates,
           s->filtered_dead,
           s->filtered_illegal,
           s->no_dictionary,
           s->dfs_calls,
           s->truncated,
           (size_t)has_escape(s),
           s->max_tile_count_seen,
           s->max_hidden_count_seen,
           s->max_boundary_vertices_seen,
           s->max_cycle_vertices_seen);
}

int main(int argc, char **argv) {
    const char *tile_path = "preferences/focus.tile";
    const char *input_path = "data/rl1/completions.dat";
    const char *remembrance_path = "data/rl0/remembrance.dat";
    const char *deletions_path = "data/rl0/deletions.dat";
    int print_table = 1;
    Selection sel;
    BComp1Options opts;
    BComp1Context ctx;
    BComp1RecordVec records = {0};
    size_t living = 0, escaped = 0, dead = 0, unknown = 0;
    size_t total = 0;

    memset(&sel, 0, sizeof(sel));
    bcomp1_options_default(&opts);
    opts.depth = 2;
    opts.collect_records = 0;
    opts.stop_after_output = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) input_path = argv[++i];
        else if (strcmp(argv[i], "--tile") == 0 && i + 1 < argc) tile_path = argv[++i];
        else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) opts.depth = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-tiles") == 0 && i + 1 < argc) opts.max_tiles = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-dfs") == 0 && i + 1 < argc) opts.max_dfs = (size_t)atoll(argv[++i]);
        else if (strcmp(argv[i], "--progress") == 0 && i + 1 < argc) opts.progress_interval = (size_t)atoll(argv[++i]);
        else if (strcmp(argv[i], "--progress-depth") == 0 && i + 1 < argc) opts.progress_depth = atoi(argv[++i]);
        else if (strcmp(argv[i], "--print") == 0) print_table = 1;
        else if (strcmp(argv[i], "--quiet") == 0) print_table = 0;
        else if (strcmp(argv[i], "--live-only") == 0) opts.live_only = 1;
        else if (strcmp(argv[i], "--all-final") == 0) opts.live_only = 0;
        else if (strcmp(argv[i], "--remembrance") == 0 && i + 1 < argc) remembrance_path = argv[++i];
        else if (strcmp(argv[i], "--deletions") == 0 && i + 1 < argc) deletions_path = argv[++i];
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(argv[0]); return 0; }
        else if (!parse_selection_token(&sel, argv[i])) { usage(argv[0]); return 1; }
    }

    if (!sel.all && sel.count == 0) sel.all = 1;
    if (opts.depth < 1) opts.depth = 1;

    if (!bcomp1_load_records(input_path, &records)) {
        fprintf(stderr, "failed to load records: %s\n", input_path);
        return 1;
    }
    if (!bcomp1_context_init(&ctx, tile_path, remembrance_path, deletions_path)) {
        fprintf(stderr, "failed to initialize bcomp1 context\n");
        bcomp1_free_records(&records);
        return 1;
    }

    if (print_table) print_header();

    size_t begin = sel.all ? 1 : 0;
    size_t end = sel.all ? records.count + 1 : (size_t)sel.count;
    for (size_t pos = begin; pos < end; pos++) {
        size_t idx = sel.all ? pos : (size_t)sel.selected[pos];
        BComp1State seed;
        BComp1Result result;
        R1Status st;
        char label[64];
        if (idx < 1 || idx > records.count) {
            fprintf(stderr, "record=%zu out_of_range count=%zu\n", idx, records.count);
            unknown++;
            total++;
            continue;
        }
        snprintf(label, sizeof(label), "record=%zu", idx);
        opts.progress_label = label;
        if (!bcomp1_state_from_record(&records.items[idx - 1], &seed)) {
            fprintf(stderr, "record=%zu malformed\n", idx);
            unknown++;
            total++;
            continue;
        }
        if (!bcomp1_complete_state(&ctx, &seed, &records.items[idx - 1].center, &opts, &result)) {
            fprintf(stderr, "record=%zu search_failed\n", idx);
            unknown++;
            total++;
            continue;
        }
        st = classify_stats(&result.stats);
        if (st == R1_LIVING) living++;
        else if (st == R1_ESCAPED) escaped++;
        else if (st == R1_DEAD) dead++;
        else unknown++;
        total++;
        if (print_table) print_row(idx, st, &result.stats);
        else printf("record=%zu status=%s outputs=%zu escapes=%zu dfs=%zu\n",
                    idx, status_name(st), result.stats.outputs, (size_t)has_escape(&result.stats), result.stats.dfs_calls);
        fflush(stdout);
        bcomp1_result_clear(&result);
    }

    printf("summary depth=%d total=%zu living=%zu escaped=%zu dead=%zu unknown=%zu\n",
           opts.depth, total, living, escaped, dead, unknown);

    bcomp1_context_clear(&ctx);
    bcomp1_free_records(&records);
    return unknown ? 1 : 0;
}
