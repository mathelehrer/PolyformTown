#include "rl0/forget_map.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int item_cmp(RL0FMItem a, RL0FMItem b) {
    if (a.p != b.p) return (a.p < b.p) ? -1 : 1;
    if (a.i != b.i) return (a.i < b.i) ? -1 : 1;
    return 0;
}

static int cycle_cmp(const RL0FMCycle *a, const RL0FMCycle *b) {
    if (a->n != b->n) return (a->n < b->n) ? -1 : 1;
    for (int k = 0; k < a->n; k++) {
        int c = item_cmp(a->item[k], b->item[k]);
        if (c) return c;
    }
    return 0;
}

int rl0_fm_arc_equal(const RL0FMArc *a, const RL0FMArc *b) {
    if (a->n != b->n) return 0;
    for (int k = 0; k < a->n; k++) {
        if (a->item[k].p != b->item[k].p || a->item[k].i != b->item[k].i) {
            return 0;
        }
    }
    return 1;
}

int rl0_fm_cycle_equal(const RL0FMCycle *a, const RL0FMCycle *b) {
    return cycle_cmp(a, b) == 0;
}

void rl0_fm_canonicalize_cycle(const RL0FMCycle *in, RL0FMCycle *out) {
    if (in->n <= 0) {
        memset(out, 0, sizeof(*out));
        return;
    }
    RL0FMCycle best;
    int first = 1;
    memset(&best, 0, sizeof(best));
    for (int shift = 0; shift < in->n; shift++) {
        RL0FMCycle cur;
        cur.n = in->n;
        for (int k = 0; k < in->n; k++) {
            cur.item[k] = in->item[(shift + k) % in->n];
        }
        if (first || cycle_cmp(&cur, &best) < 0) {
            best = cur;
            first = 0;
        }
    }
    *out = best;
}

void rl0_fm_reflect_cycle(const RL0FMCycle *in, RL0FMCycle *out) {
    RL0FMCycle tmp;
    tmp.n = in->n;
    for (int k = 0; k < in->n; k++) {
        tmp.item[k] = in->item[in->n - 1 - k];
        tmp.item[k].p = -tmp.item[k].p;
    }
    rl0_fm_canonicalize_cycle(&tmp, out);
}

void rl0_fm_init(RL0ForgetMap *map) {
    memset(map, 0, sizeof(*map));
}

static void skip_ws(const char **p);
static int parse_int(const char **p, int *out);

void rl0_fm_deletions_init(RL0FMDeletionSet *set) {
    memset(set, 0, sizeof(*set));
}

int rl0_fm_deletions_contains_cycle(const RL0FMDeletionSet *set,
                                    const RL0FMCycle *cycle,
                                    int delete_through_level) {
    RL0FMCycle canon;
    if (!set || delete_through_level < 0) return 0;
    rl0_fm_canonicalize_cycle(cycle, &canon);
    for (int i = 0; i < set->count; i++) {
        if (set->level[i] > delete_through_level) continue;
        if (rl0_fm_cycle_equal(&set->cycle[i], &canon)) return 1;
    }
    return 0;
}

int rl0_fm_deletions_add_cycle(RL0FMDeletionSet *set,
                               int level,
                               const RL0FMCycle *cycle) {
    RL0FMCycle canon;
    RL0FMCycle reflected;
    if (!set || !cycle || cycle->n <= 0) return 1;
    rl0_fm_canonicalize_cycle(cycle, &canon);
    if (!rl0_fm_deletions_contains_cycle(set, &canon, level)) {
        if (set->count >= RL0_FM_MAX_DELETIONS) return 0;
        set->level[set->count] = level;
        set->cycle[set->count++] = canon;
    }

    /* Deleting a full indexed vertex figure also deletes its mirror, because
       source loading inserts both normal and reflected orientations. */
    rl0_fm_reflect_cycle(&canon, &reflected);
    if (!rl0_fm_deletions_contains_cycle(set, &reflected, level)) {
        if (set->count >= RL0_FM_MAX_DELETIONS) return 0;
        set->level[set->count] = level;
        set->cycle[set->count++] = reflected;
    }
    return 1;
}

static int parse_index_cycle_line(const char *line, RL0FMCycle *out) {
    const char *p = line;
    out->n = 0;
    skip_ws(&p);
    if (*p != '[') return 0;
    p++;
    skip_ws(&p);
    if (*p == ']') return 0;
    while (out->n < RL0_FM_MAX_ITEMS) {
        RL0FMItem it;
        if (*p != '[') return 0;
        p++;
        if (!parse_int(&p, &it.p)) return 0;
        skip_ws(&p);
        if (*p != ',') return 0;
        p++;
        if (!parse_int(&p, &it.i)) return 0;
        skip_ws(&p);
        if (*p != ']') return 0;
        p++;
        out->item[out->n++] = it;
        skip_ws(&p);
        if (*p == ',') {
            p++;
            skip_ws(&p);
            continue;
        }
        if (*p == ']') {
            p++;
            skip_ws(&p);
            return *p == '\0' || *p == '\n' || *p == '\r' || *p == '#';
        }
        return 0;
    }
    return 0;
}

static int parse_level_header(const char *line, int *level) {
    const char *p = line;
    if (strncmp(p, "---[", 4) != 0) return 0;
    p += 4;
    if (!parse_int(&p, level)) return 0;
    skip_ws(&p);
    return *p == ']' && p[1] == '-' && p[2] == '-' && p[3] == '-';
}

int rl0_fm_load_deletions(RL0FMDeletionSet *set, const char *path) {
    FILE *fp = fopen(path, "r");
    char line[4096];
    int level = -1;
    if (!fp) return 0;
    rl0_fm_deletions_init(set);
    while (fgets(line, sizeof(line), fp)) {
        const char *q = line;
        RL0FMCycle c;
        skip_ws(&q);
        if (*q == '\0' || *q == '\n' || *q == '\r' || *q == '#') continue;
        if (parse_level_header(q, &level)) continue;
        if (level < 0) continue;
        if (!parse_index_cycle_line(q, &c)) {
            fclose(fp);
            return 0;
        }
        if (!rl0_fm_deletions_add_cycle(set, level, &c)) {
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return 1;
}

int rl0_fm_cycle_from_parity_indices(const int *parities,
                                     const int *indices,
                                     int count,
                                     RL0FMCycle *out) {
    RL0FMCycle raw;
    if (!parities || !indices || !out || count <= 0 || count > RL0_FM_MAX_ITEMS) {
        return 0;
    }
    raw.n = count;
    for (int i = 0; i < count; i++) {
        raw.item[i].p = parities[i];
        raw.item[i].i = indices[i];
    }
    rl0_fm_canonicalize_cycle(&raw, out);
    return 1;
}

static int add_cycle(RL0ForgetMap *map, const RL0FMCycle *cycle) {
    RL0FMCycle c;
    rl0_fm_canonicalize_cycle(cycle, &c);
    for (int k = 0; k < map->cycle_count; k++) {
        if (rl0_fm_cycle_equal(&map->cycles[k], &c)) return 1;
    }
    if (map->cycle_count >= RL0_FM_MAX_CYCLES) return 0;
    map->cycles[map->cycle_count++] = c;
    return 1;
}

static void skip_ws(const char **p) {
    while (isspace((unsigned char)**p)) (*p)++;
}

static int parse_int(const char **p, int *out) {
    char *end = NULL;
    long v;
    skip_ws(p);
    v = strtol(*p, &end, 10);
    if (end == *p) return 0;
    *out = (int)v;
    *p = end;
    return 1;
}

static int parse_int_list(const char *line, int *out, int *count) {
    const char *p = line;
    *count = 0;
    skip_ws(&p);
    if (*p != '[') return 0;
    p++;
    skip_ws(&p);
    if (*p == ']') return 1;
    while (*count < RL0_FM_MAX_ITEMS) {
        if (!parse_int(&p, &out[*count])) return 0;
        (*count)++;
        skip_ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == ']') return 1;
        return 0;
    }
    return 0;
}

static int maybe_add_current_record(RL0ForgetMap *map,
                                    const int *parities,
                                    const int *indices,
                                    int pc,
                                    int ic) {
    if (pc != ic || pc <= 0) return 1;
    RL0FMCycle c;
    c.n = pc;
    for (int k = 0; k < pc; k++) {
        c.item[k].p = parities[k];
        c.item[k].i = indices[k];
    }
    if (!add_cycle(map, &c)) return 0;
    RL0FMCycle r;
    rl0_fm_reflect_cycle(&c, &r);
    if (!add_cycle(map, &r)) return 0;
    return 1;
}

int rl0_fm_load_completions_filtered(RL0ForgetMap *map,
                                      const char *path,
                                      const RL0FMDeletionSet *deletions,
                                      int delete_through_level) {
    FILE *fp = fopen(path, "r");
    char line[262144];
    int parities[RL0_FM_MAX_ITEMS];
    int indices[RL0_FM_MAX_ITEMS];
    int pc = 0, ic = 0;
    int have_p = 0, have_i = 0;
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "---[", 4) == 0) {
            if (have_p && have_i) {
                RL0FMCycle c;
                if (!rl0_fm_cycle_from_parity_indices(parities, indices, pc, &c)) {
                    fclose(fp);
                    return 0;
                }
                if (!rl0_fm_deletions_contains_cycle(deletions, &c, delete_through_level) &&
                    !maybe_add_current_record(map, parities, indices, pc, ic)) {
                    fclose(fp);
                    return 0;
                }
            }
            have_p = have_i = 0;
            pc = ic = 0;
            continue;
        }
        if (strncmp(line, "parities:", 9) == 0) {
            have_p = parse_int_list(line + 9, parities, &pc);
            continue;
        }
        if (strncmp(line, "indices:", 8) == 0) {
            have_i = parse_int_list(line + 8, indices, &ic);
            continue;
        }
    }
    if (have_p && have_i) {
        RL0FMCycle c;
        if (!rl0_fm_cycle_from_parity_indices(parities, indices, pc, &c)) {
            fclose(fp);
            return 0;
        }
        if (!rl0_fm_deletions_contains_cycle(deletions, &c, delete_through_level) &&
            !maybe_add_current_record(map, parities, indices, pc, ic)) {
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return 1;
}

int rl0_fm_load_completions(RL0ForgetMap *map, const char *path) {
    return rl0_fm_load_completions_filtered(map, path, NULL, -1);
}

int rl0_fm_load_completions_with_deletions(RL0ForgetMap *map,
                                           const char *completions_path,
                                           const char *deletions_path,
                                           int delete_through_level) {
    RL0FMDeletionSet deletions;
    if (delete_through_level < 0) {
        return rl0_fm_load_completions_filtered(map, completions_path, NULL, -1);
    }
    if (!rl0_fm_load_deletions(&deletions, deletions_path)) return 0;
    return rl0_fm_load_completions_filtered(map,
                                            completions_path,
                                            &deletions,
                                            delete_through_level);
}

static RL0FMRow *find_or_add_row(RL0ForgetMap *map, const RL0FMArc *key) {
    for (int r = 0; r < map->row_count; r++) {
        if (rl0_fm_arc_equal(&map->rows[r].key, key)) return &map->rows[r];
    }
    if (map->row_count >= RL0_FM_MAX_ROWS) return NULL;
    RL0FMRow *row = &map->rows[map->row_count++];
    memset(row, 0, sizeof(*row));
    row->key = *key;
    return row;
}

static int row_add_value(RL0FMRow *row, const RL0FMArc *value) {
    for (int k = 0; k < row->value_count; k++) {
        if (rl0_fm_arc_equal(&row->values[k], value)) return 1;
    }
    if (row->value_count >= RL0_FM_MAX_VALUES) return 0;
    row->values[row->value_count++] = *value;
    return 1;
}

static void make_arc_raw(const RL0FMCycle *full, int start, int len, RL0FMArc *out) {
    out->n = len;
    for (int k = 0; k < len; k++) out->item[k] = full->item[(start + k) % full->n];
}

int rl0_fm_build_from_cycles(RL0ForgetMap *map) {
    map->row_count = 0;
    for (int c = 0; c < map->cycle_count; c++) {
        RL0FMCycle canon_full;
        rl0_fm_canonicalize_cycle(&map->cycles[c], &canon_full);
        map->cycles[c] = canon_full;
        const RL0FMCycle *full = &map->cycles[c];
        int N = full->n;
        for (int forget_len = 0; forget_len <= N; forget_len++) {
            int starts = (forget_len == 0 || forget_len == N) ? 1 : N;
            for (int start = 0; start < starts; start++) {
                RL0FMArc forgotten, kept;
                make_arc_raw(full, start, forget_len, &forgotten);
                make_arc_raw(full, (start + forget_len) % N, N - forget_len, &kept);
                RL0FMRow *row = find_or_add_row(map, &kept);
                if (!row) return 0;
                if (!row_add_value(row, &forgotten)) return 0;
            }
        }
    }
    return 1;
}

int rl0_fm_lookup(const RL0ForgetMap *map,
                  const RL0FMArc *key,
                  const RL0FMArc **values,
                  int *value_count) {
    RL0FMArc k = *key;
    if (k.n >= RL0_FM_MAX_ITEMS) {
        RL0FMCycle canon;
        rl0_fm_canonicalize_cycle(&k, &canon);
        k = canon;
    }
    for (int r = 0; r < map->row_count; r++) {
        if (rl0_fm_arc_equal(&map->rows[r].key, &k)) {
            *values = map->rows[r].values;
            *value_count = map->rows[r].value_count;
            return 1;
        }
    }
    *values = NULL;
    *value_count = 0;
    return 0;
}

int rl0_fm_lookup_any_rotation(const RL0ForgetMap *map,
                               const RL0FMArc *key,
                               const RL0FMArc **values,
                               int *value_count,
                               RL0FMArc *matched_key) {
    if (key->n <= 1) return rl0_fm_lookup(map, key, values, value_count);
    for (int shift = 0; shift < key->n; shift++) {
        RL0FMArc cur;
        cur.n = key->n;
        for (int k = 0; k < key->n; k++) cur.item[k] = key->item[(shift + k) % key->n];
        if (rl0_fm_lookup(map, &cur, values, value_count)) {
            if (matched_key) *matched_key = cur;
            return 1;
        }
    }
    *values = NULL;
    *value_count = 0;
    return 0;
}

int rl0_fm_contains_complete(const RL0ForgetMap *map,
                             const RL0FMCycle *complete) {
    RL0FMCycle c;
    rl0_fm_canonicalize_cycle(complete, &c);
    for (int r = 0; r < map->row_count; r++) {
        if (!rl0_fm_arc_equal(&map->rows[r].key, &c)) continue;
        for (int k = 0; k < map->rows[r].value_count; k++) {
            if (map->rows[r].values[k].n == 0) return 1;
        }
    }
    return 0;
}
