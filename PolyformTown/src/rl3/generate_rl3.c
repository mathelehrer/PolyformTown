#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "core/cycle.h"
#include "core/tile.h"
#include "rl1/bcomp1.h"

#define RL3_MAX_DELETIONS 65536

static int ensure_dir(const char *path) {
    if (mkdir(path, 0777) == 0) return 1;
    return errno == EEXIST;
}

static int ensure_output_dirs(void) {
    return ensure_dir("data") && ensure_dir("data/rl3");
}

static int parse_deletions(const char *path, unsigned char *deleted, size_t cap, size_t *dead_count) {
    FILE *fp = fopen(path, "r");
    char line[262144];
    *dead_count = 0;
    memset(deleted, 0, cap);
    if (!fp) return 1;
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        if (strncmp(p, "dead:", 5) != 0) continue;
        p += 5;
        while (*p) {
            char *end = NULL;
            long v;
            while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
            if (!*p) break;
            v = strtol(p, &end, 10);
            if (end == p) break;
            if (v > 0 && (size_t)v < cap && !deleted[v]) {
                deleted[v] = 1;
                (*dead_count)++;
            }
            p = end;
        }
    }
    fclose(fp);
    return 1;
}

static int poly_eq_exact(const Poly *a, const Poly *b) {
    if (a->cycle_count != b->cycle_count) return 0;
    for (int c = 0; c < a->cycle_count; c++) {
        if (a->cycles[c].n != b->cycles[c].n) return 0;
        for (int i = 0; i < a->cycles[c].n; i++) {
            if (!coord_eq(a->cycles[c].v[i], b->cycles[c].v[i])) return 0;
        }
    }
    return 1;
}

static int record_vec_push(BComp1RecordVec *v, const BComp1Record *r) {
    if (v->count == v->cap) {
        size_t next = v->cap ? v->cap * 2 : 64;
        BComp1Record *p = realloc(v->items, next * sizeof(*p));
        if (!p) return 0;
        v->items = p;
        v->cap = next;
    }
    v->items[v->count++] = *r;
    return 1;
}

static int record_vec_contains_boundary(const BComp1RecordVec *v, const BComp1Record *r) {
    for (size_t i = 0; i < v->count; i++) {
        if (poly_eq_exact(&v->items[i].boundary, &r->boundary)) return 1;
    }
    return 0;
}

static int append_unique_records(BComp1RecordVec *dst, const BComp1RecordVec *src, size_t *duplicates) {
    for (size_t i = 0; i < src->count; i++) {
        if (record_vec_contains_boundary(dst, &src->items[i])) {
            (*duplicates)++;
            continue;
        }
        if (!record_vec_push(dst, &src->items[i])) return 0;
    }
    return 1;
}

static int write_records_file(const char *path, BComp1RecordVec *records) {
    FILE *fp = fopen(path, "w");
    if (!fp) return 0;
    bcomp1_sort_records(records);
    for (size_t i = 0; i < records->count; i++) bcomp1_print_record(fp, i + 1, &records->items[i]);
    return fclose(fp) == 0;
}

int main(void) {
    const char *tile_path = "preferences/focus.tile";
    const char *input_path = "data/rl2/completions.dat";
    const char *deleted_path = "data/rl2/deletions.dat";
    const char *output_path = "data/rl3/completions.dat";
    const char *remembrance_path = "data/rl0/remembrance.dat";
    const char *rl0_deletions_path = "data/rl0/deletions.dat";
    BComp1Context ctx;
    BComp1RecordVec input = {0};
    BComp1RecordVec output = {0};
    BComp1Options opts;
    unsigned char deleted[RL3_MAX_DELETIONS];
    size_t deleted_count = 0;
    size_t candidates = 0, skipped_deleted = 0, children_total = 0, duplicates = 0;
    size_t dfs_total = 0, attempts_total = 0, success_total = 0;

    if (!ensure_output_dirs()) {
        fprintf(stderr, "ERROR: failed to create data/rl3\n");
        return 1;
    }
    if (!parse_deletions(deleted_path, deleted, sizeof(deleted), &deleted_count)) {
        fprintf(stderr, "ERROR: failed to parse RL2 deletions: %s\n", deleted_path);
        return 1;
    }
    if (!bcomp1_context_init(&ctx, tile_path, remembrance_path, rl0_deletions_path)) {
        fprintf(stderr, "ERROR: failed to initialize RL3 context\n");
        return 1;
    }
    if (!bcomp1_load_records(input_path, &input)) {
        fprintf(stderr, "ERROR: failed to load RL2 completions: %s\n", input_path);
        bcomp1_context_clear(&ctx);
        return 1;
    }

    bcomp1_options_default(&opts);
    opts.depth = 1;
    opts.collect_records = 1;
    opts.live_only = 1;

    for (size_t i = 0; i < input.count; i++) {
        BComp1State seed;
        BComp1Result result;
        if (i + 1 < sizeof(deleted) && deleted[i + 1]) {
            skipped_deleted++;
            continue;
        }
        candidates++;
        if (!bcomp1_state_from_record(&input.items[i], &seed)) {
            fprintf(stderr, "ERROR: malformed RL2 record %zu\n", i + 1);
            bcomp1_free_records(&output);
            bcomp1_free_records(&input);
            bcomp1_context_clear(&ctx);
            return 1;
        }
        if (!bcomp1_complete_state(&ctx, &seed, &input.items[i].center, &opts, &result)) {
            fprintf(stderr, "ERROR: RL3 depth-1 search failed for RL2 record %zu\n", i + 1);
            bcomp1_free_records(&output);
            bcomp1_free_records(&input);
            bcomp1_context_clear(&ctx);
            return 1;
        }
        children_total += result.records.count;
        dfs_total += result.stats.dfs_calls;
        attempts_total += result.stats.attach_attempts;
        success_total += result.stats.attach_successes;
        if (!append_unique_records(&output, &result.records, &duplicates)) {
            fprintf(stderr, "ERROR: failed to append RL3 records\n");
            bcomp1_result_clear(&result);
            bcomp1_free_records(&output);
            bcomp1_free_records(&input);
            bcomp1_context_clear(&ctx);
            return 1;
        }
        bcomp1_result_clear(&result);
    }

    if (!write_records_file(output_path, &output)) {
        fprintf(stderr, "ERROR: failed to write %s\n", output_path);
        bcomp1_free_records(&output);
        bcomp1_free_records(&input);
        bcomp1_context_clear(&ctx);
        return 1;
    }

    (void)input;
    (void)deleted_count;
    (void)candidates;
    (void)skipped_deleted;
    (void)children_total;
    (void)duplicates;
    (void)dfs_total;
    (void)attempts_total;
    (void)success_total;
    fprintf(stderr, "%s\n", output_path);

    bcomp1_free_records(&output);
    bcomp1_free_records(&input);
    bcomp1_context_clear(&ctx);
    return 0;
}
