#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "rl1/bcomp1.h"

#define RL4_MAX_DELETIONS 65536

typedef struct {
    const char *level_name;
    const char *input_path;
    const char *deletions_path;
    const char *output_path;
} FilterSpec;

static int ensure_dir(const char *path) {
    if (mkdir(path, 0777) == 0) return 1;
    return errno == EEXIST;
}

static int parse_deletions(const char *path,
                           unsigned char *deleted,
                           size_t cap,
                           size_t *dead_count) {
    FILE *fp = fopen(path, "r");
    char line[262144];
    *dead_count = 0;
    memset(deleted, 0, cap);
    if (!fp) return 0;

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

static int write_filtered_records(const FilterSpec *spec,
                                  const BComp1RecordVec *records,
                                  const unsigned char *deleted,
                                  size_t deleted_cap,
                                  size_t *written,
                                  size_t *skipped_deleted) {
    FILE *fp = fopen(spec->output_path, "w");
    if (!fp) return 0;

    *written = 0;
    *skipped_deleted = 0;
    for (size_t i = 0; i < records->count; i++) {
        size_t record_index = i + 1;
        if (record_index < deleted_cap && deleted[record_index]) {
            (*skipped_deleted)++;
            continue;
        }
        (*written)++;
        bcomp1_print_record(fp, *written, &records->items[i]);
    }

    return fclose(fp) == 0;
}

static int filter_one_level(const FilterSpec *spec) {
    BComp1RecordVec records = {0};
    unsigned char deleted[RL4_MAX_DELETIONS];
    size_t dead_count = 0;
    size_t written = 0;
    size_t skipped_deleted = 0;

    if (!parse_deletions(spec->deletions_path, deleted, sizeof(deleted), &dead_count)) {
        fprintf(stderr, "ERROR: failed to read %s deletions: %s\n",
                spec->level_name,
                spec->deletions_path);
        return 0;
    }
    if (!bcomp1_load_records(spec->input_path, &records)) {
        fprintf(stderr, "ERROR: failed to read %s completions: %s\n",
                spec->level_name,
                spec->input_path);
        return 0;
    }
    if (!write_filtered_records(spec,
                                &records,
                                deleted,
                                sizeof(deleted),
                                &written,
                                &skipped_deleted)) {
        fprintf(stderr, "ERROR: failed to write %s\n", spec->output_path);
        bcomp1_free_records(&records);
        return 0;
    }

    fprintf(stderr,
            "rl4 filter %s input=%zu deleted=%zu skipped=%zu written=%zu output=%s\n",
            spec->level_name,
            records.count,
            dead_count,
            skipped_deleted,
            written,
            spec->output_path);

    bcomp1_free_records(&records);
    return 1;
}

int main(void) {
    static const FilterSpec specs[] = {
        { "rl1", "data/rl1/completions.dat", "data/rl1/deletions.dat", "data/rl4/rl1_filtered.dat" },
        { "rl2", "data/rl2/completions.dat", "data/rl2/deletions.dat", "data/rl4/rl2_filtered.dat" },
        { "rl3", "data/rl3/completions.dat", "data/rl3/deletions.dat", "data/rl4/rl3_filtered.dat" },
    };

    if (!ensure_dir("data") || !ensure_dir("data/rl1") ||
        !ensure_dir("data/rl2") || !ensure_dir("data/rl3") ||
        !ensure_dir("data/rl4")) {
        fprintf(stderr, "ERROR: failed to create runlevel data directories\n");
        return 1;
    }

    for (size_t i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
        if (!filter_one_level(&specs[i])) return 1;
    }

    return 0;
}
