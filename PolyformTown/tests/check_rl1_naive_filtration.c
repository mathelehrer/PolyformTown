#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/boundary.h"
#include "core/cycle.h"
#include "core/tile.h"
#include "core/lattice.h"
#include "core/tetrille.h"
#include "rl0/boundary0.h"
#include "rl0/forget_map.h"
#include "rl0/attach0.h"

#define DEFAULT_TILE "preferences/focus.tile"
#define DEFAULT_NAIVE "data/rl1/naive_completions.dat"
#define DEFAULT_REMEMBRANCE "data/rl0/remembrance.dat"
#define DEFAULT_DELETIONS "data/rl0/deletions.dat"
#define EXPECT_SURVIVORS 26
#define MAX_LINE 262144
#define MAX_RECORDS 1024
#define MAX_HIDDEN (MAX_VERTS * ATTACH0_MAX_TILES)

typedef struct {
    int id;
    int level;
    int tile_count;
    int have_boundary;
    Poly boundary;
    int hidden_count;
    Coord hidden[MAX_HIDDEN];
    int parsed_tile_count;
    Cycle tiles[ATTACH0_MAX_TILES];
} NaiveRecord;

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

static int expect_ch(const char **p, char ch) {
    skip_ws(p);
    if (**p != ch) return 0;
    (*p)++;
    return 1;
}

static int parse_coord(const char **p, Coord *q) {
    return expect_ch(p, '(') &&
           parse_int(p, &q->v) &&
           expect_ch(p, ',') &&
           parse_int(p, &q->x) &&
           expect_ch(p, ',') &&
           parse_int(p, &q->y) &&
           expect_ch(p, ')');
}

static int parse_cycle_bracket(const char **p, Cycle *c) {
    c->n = 0;
    if (!expect_ch(p, '[')) return 0;
    skip_ws(p);
    if (**p == ']') {
        (*p)++;
        return 1;
    }
    while (**p) {
        Coord q;
        if (c->n >= MAX_VERTS) return 0;
        if (!parse_coord(p, &q)) return 0;
        c->v[c->n++] = q;
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (**p == ']') {
            (*p)++;
            return c->n > 0;
        }
        return 0;
    }
    return 0;
}

static int parse_poly_bracket(const char *text, Poly *poly) {
    const char *p = text;
    poly->cycle_count = 0;
    if (!expect_ch(&p, '[')) return 0;
    skip_ws(&p);
    if (*p == ']') return 0;
    while (*p) {
        if (poly->cycle_count >= MAX_CYCLES) return 0;
        if (!parse_cycle_bracket(&p, &poly->cycles[poly->cycle_count])) return 0;
        poly->cycle_count++;
        skip_ws(&p);
        if (*p == '|') {
            p++;
            continue;
        }
        if (*p == ']') {
            p++;
            skip_ws(&p);
            return *p == '\0' || *p == '\r' || *p == '\n';
        }
        return 0;
    }
    return 0;
}

static int parse_coord_list(const char *text, Coord *coords, int *count) {
    const char *p = text;
    int n = 0;
    if (!expect_ch(&p, '[')) return 0;
    skip_ws(&p);
    if (*p == ']') {
        *count = 0;
        return 1;
    }
    while (*p) {
        Coord q;
        if (n >= MAX_HIDDEN) return 0;
        if (!parse_coord(&p, &q)) return 0;
        coords[n++] = q;
        skip_ws(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == ']') {
            p++;
            *count = n;
            return 1;
        }
        return 0;
    }
    return 0;
}

static int parse_tile_list(const char *text, Cycle *tiles, int *tile_count) {
    const char *p = text;
    int n = 0;
    if (!expect_ch(&p, '[')) return 0;
    skip_ws(&p);
    if (*p == ']') {
        *tile_count = 0;
        return 1;
    }
    while (*p) {
        if (n >= ATTACH0_MAX_TILES) return 0;
        if (!parse_cycle_bracket(&p, &tiles[n])) return 0;
        n++;
        skip_ws(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == ']') {
            p++;
            *tile_count = n;
            return 1;
        }
        return 0;
    }
    return 0;
}


static int cycle_matches_cyclic_or_reversed(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return 0;
    int n = a->n;
    for (int shift = 0; shift < n; shift++) {
        int ok = 1;
        for (int i = 0; i < n; i++) {
            if (!coord_eq(a->v[i], b->v[(shift + i) % n])) { ok = 0; break; }
        }
        if (ok) return 1;
        ok = 1;
        for (int i = 0; i < n; i++) {
            int j = shift - i;
            while (j < 0) j += n;
            j %= n;
            if (!coord_eq(a->v[i], b->v[j])) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

static int canonicalize_placed_tile(const Tile *tile, const Cycle *placed, Cycle *out) {
    int n = tile->base.n;
    int tcount = lattice_transform_count(tile->lattice);
    long long src_area = cycle_signed_area2(&tile->base, tile->lattice);

    if (placed->n != n) return 0;
    for (int t = 0; t < tcount; t++) {
        Cycle tr = {0};
        long long tr_area;
        int parity;
        cycle_transform_lattice(&tile->base, &tr, tile->lattice, t);
        tr_area = cycle_signed_area2(&tr, tile->lattice);
        parity = (tr_area * src_area >= 0) ? 1 : -1;
        for (int j = 0; j < n; j++) {
            int dx;
            int dy;
            Cycle candidate = tr;
            if (placed->v[j].v != tr.v[0].v) continue;
            dx = placed->v[j].x - tr.v[0].x;
            dy = placed->v[j].y - tr.v[0].y;
            if (tile->lattice == TILE_LATTICE_TETRILLE) {
                int m6 = 0, n6 = 0;
                if (!tetrille_delta_to_6(tr.v[0].v, dx, dy, &m6, &n6)) continue;
                tetrille_translate_cycle(&candidate, m6, n6);
            } else {
                cycle_translate(&candidate, dx, dy);
            }
            if (!cycle_matches_cyclic_or_reversed(&candidate, placed)) continue;
            out->n = n;
            if (parity == 1) {
                for (int k = 0; k < n; k++) out->v[k] = candidate.v[k];
            } else {
                for (int k = 0; k < n; k++) out->v[k] = candidate.v[n - 1 - k];
            }
            return 1;
        }
    }
    return 0;
}

static int normalize_record_tiles(const Tile *tile, NaiveRecord *rec) {
    Cycle normalized[ATTACH0_MAX_TILES];
    for (int i = 0; i < rec->parsed_tile_count; i++) {
        if (!canonicalize_placed_tile(tile, &rec->tiles[i], &normalized[i])) return 0;
    }
    for (int i = 0; i < rec->parsed_tile_count; i++) rec->tiles[i] = normalized[i];
    return 1;
}

static int coord_on_poly_vertices(Coord q, const Poly *p) {
    for (int c = 0; c < p->cycle_count; c++) {
        for (int i = 0; i < p->cycles[c].n; i++) {
            if (coord_eq(q, p->cycles[c].v[i])) return 1;
        }
    }
    return 0;
}

static int coord_in_local_list(Coord q, const Coord *items, int count) {
    for (int i = 0; i < count; i++) {
        if (coord_eq(q, items[i])) return 1;
    }
    return 0;
}

static int collect_complete_vertices(const NaiveRecord *rec, Coord *out, int *out_count) {
    int n = 0;
    for (int t = 0; t < rec->parsed_tile_count; t++) {
        for (int i = 0; i < rec->tiles[t].n; i++) {
            Coord q = rec->tiles[t].v[i];
            if (coord_on_poly_vertices(q, &rec->boundary)) continue;
            if (coord_in_local_list(q, out, n)) continue;
            if (n >= MAX_HIDDEN) return 0;
            out[n++] = q;
        }
    }
    *out_count = n;
    return 1;
}

static void trim_newline(char *s) {
    s[strcspn(s, "\r\n")] = '\0';
}

static int read_naive_records(const char *path, NaiveRecord *records, int *record_count) {
    FILE *fp = fopen(path, "r");
    char line[MAX_LINE];
    NaiveRecord *cur = NULL;
    int count = 0;
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        trim_newline(line);
        if (strncmp(line, "---[", 4) == 0) {
            int id = 0;
            if (count >= MAX_RECORDS) { fclose(fp); return 0; }
            if (sscanf(line, "---[%d]---", &id) != 1) { fclose(fp); return 0; }
            cur = &records[count++];
            memset(cur, 0, sizeof(*cur));
            cur->id = id;
            continue;
        }
        if (!cur) continue;
        if (strncmp(line, "level:", 6) == 0) {
            cur->level = atoi(line + 6);
        } else if (strncmp(line, "tile_count:", 11) == 0) {
            cur->tile_count = atoi(line + 11);
        } else if (strncmp(line, "boundary:", 9) == 0) {
            if (!parse_poly_bracket(line + 9, &cur->boundary)) { fclose(fp); return 0; }
            cur->have_boundary = 1;
        } else if (strncmp(line, "constellation:", 14) == 0) {
            if (!parse_coord_list(line + 14, cur->hidden, &cur->hidden_count)) { fclose(fp); return 0; }
        } else if (strncmp(line, "tiles:", 6) == 0) {
            if (!parse_tile_list(line + 6, cur->tiles, &cur->parsed_tile_count)) { fclose(fp); return 0; }
        }
    }
    fclose(fp);
    *record_count = count;
    return 1;
}

static int record_dictionary_justified(const Tile *tile,
                                       const RL0ForgetMap *map,
                                       const NaiveRecord *rec,
                                       int *checked,
                                       int *misses) {
    Coord complete[MAX_HIDDEN];
    int complete_count = 0;

    *checked = 0;
    *misses = 0;
    if (!collect_complete_vertices(rec, complete, &complete_count)) {
        (*misses)++;
        return 0;
    }

    for (int i = 0; i < complete_count; i++) {
        Coord q = complete[i];
        (*checked)++;
        if (!boundary0_vertex_has_dictionary_completion(tile,
                                                        rec->tiles,
                                                        rec->parsed_tile_count,
                                                        q,
                                                        map)) {
            (*misses)++;
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    int force_canonical = 0;
    Tile tile;
    RL0FMDeletionSet deletions;
    RL0ForgetMap map;
    NaiveRecord *records = NULL;
    int record_count = 0;
    int malformed = 0;
    int dead_boundary = 0;
    int dict_missing = 0;
    int survivors = 0;
    int checked_vertices = 0;
    int dictionary_misses = 0;

    for (int ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "--force-canonical") == 0) {
            force_canonical = 1;
        } else {
            fprintf(stderr, "usage: %s [--force-canonical]\n", argv[0]);
            return 2;
        }
    }

    if (!tile_load(DEFAULT_TILE, &tile)) {
        fprintf(stderr, "failed to load tile: %s\n", DEFAULT_TILE);
        return 2;
    }

    rl0_fm_deletions_init(&deletions);
    rl0_fm_init(&map);
    if (!rl0_fm_load_deletions(&deletions, DEFAULT_DELETIONS)) {
        fprintf(stderr, "failed to load deletions: %s\n", DEFAULT_DELETIONS);
        return 2;
    }
    if (!rl0_fm_load_remembrance_filtered(&map, DEFAULT_REMEMBRANCE, &deletions, 1000000000)) {
        fprintf(stderr, "failed to load filtered remembrance: %s\n", DEFAULT_REMEMBRANCE);
        return 2;
    }

    records = calloc(MAX_RECORDS, sizeof(*records));
    if (!records) return 2;
    if (!read_naive_records(DEFAULT_NAIVE, records, &record_count)) {
        fprintf(stderr, "failed to parse naive completions: %s\n", DEFAULT_NAIVE);
        free(records);
        return 2;
    }

    for (int i = 0; i < record_count; i++) {
        Boundary0Stats bstats;
        int checked = 0;
        int misses = 0;
        if (!records[i].have_boundary || records[i].parsed_tile_count != records[i].tile_count ||
            (force_canonical && !normalize_record_tiles(&tile, &records[i]))) {
            malformed++;
            continue;
        }
        boundary0_stats_init(&bstats);
        if (!boundary0_poly_has_live_boundary(&records[i].boundary,
                                              &tile,
                                              records[i].tiles,
                                              records[i].parsed_tile_count,
                                              &map,
                                              &bstats)) {
            dead_boundary++;
            continue;
        }
        if (!record_dictionary_justified(&tile, &map, &records[i], &checked, &misses)) {
            dict_missing++;
            checked_vertices += checked;
            dictionary_misses += misses;
            continue;
        }
        checked_vertices += checked;
        survivors++;
    }

    printf("rl1_naive_filtration: records=%d malformed=%d dead_boundary=%d dict_missing=%d survivors=%d checked_vertices=%d dictionary_misses=%d\n",
           record_count,
           malformed,
           dead_boundary,
           dict_missing,
           survivors,
           checked_vertices,
           dictionary_misses);

    free(records);
    rl0_fm_clear(&map);
    rl0_fm_deletions_clear(&deletions);

    if (malformed != 0) return 1;
    if (survivors != EXPECT_SURVIVORS) {
        fprintf(stderr, "expected %d survivors, got %d\n", EXPECT_SURVIVORS, survivors);
        return 1;
    }
    return 0;
}
