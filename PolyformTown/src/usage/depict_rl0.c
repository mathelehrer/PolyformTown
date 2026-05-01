#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/cycle.h"
#include "core/attach.h"
#include "core/boundary.h"
#include "core/tile.h"

#define MAX_TILES_PER_RECORD MAX_VERTS

typedef struct {
    int have_valence;
    int valence;
    int have_tile_count;
    int tile_count;
    int have_center;
    Coord center;
    int have_boundary;
    Poly boundary;
    int have_tiles;
    int tile_count_list;
    Cycle tiles[MAX_TILES_PER_RECORD];
    int have_indices;
    int index_count;
    int indices[MAX_TILES_PER_RECORD];
    int have_parities;
    int parity_count;
    int parities[MAX_TILES_PER_RECORD];
    int have_hidden;
    int hidden_count;
    Coord hidden[MAX_TILES_PER_RECORD];
} RL0Record;

typedef struct {
    const char *data_path;
    int limit;
    int valences[16];
    int valence_count;
    int tile_counts[32];
    int tile_count_count;
    int grouped;
    int live_only;
} Options;

typedef struct {
    int valence;
    int tile_count;
    Poly boundary;
} SeenKey;

typedef struct {
    SeenKey *items;
    int count;
    int cap;
} SeenSet;

static void skip_ws(const char **pp) {
    while (isspace((unsigned char)**pp)) (*pp)++;
}

static int parse_int(const char **pp, int *out) {
    char *end = NULL;
    long v;
    skip_ws(pp);
    v = strtol(*pp, &end, 10);
    if (end == *pp) return 0;
    *out = (int)v;
    *pp = end;
    return 1;
}

static int expect_char(const char **pp, char ch) {
    skip_ws(pp);
    if (**pp != ch) return 0;
    (*pp)++;
    return 1;
}

static int parse_cycle(const char **pp, Cycle *cycle) {
    cycle->n = 0;
    if (!expect_char(pp, '[')) return 0;

    skip_ws(pp);
    if (**pp == ']') {
        (*pp)++;
        return 1;
    }

    while (cycle->n < MAX_VERTS) {
        Coord q;
        if (!expect_char(pp, '(')) return 0;
        if (!parse_int(pp, &q.v)) return 0;
        if (!expect_char(pp, ',')) return 0;
        if (!parse_int(pp, &q.x)) return 0;
        if (!expect_char(pp, ',')) return 0;
        if (!parse_int(pp, &q.y)) return 0;
        if (!expect_char(pp, ')')) return 0;
        cycle->v[cycle->n++] = q;

        skip_ws(pp);
        if (**pp == ',') {
            (*pp)++;
            continue;
        }
        if (**pp == ']') {
            (*pp)++;
            return cycle->n > 0;
        }
        return 0;
    }

    return 0;
}

static int parse_poly(const char *text, Poly *poly) {
    const char *p = text;
    poly->cycle_count = 0;

    if (!expect_char(&p, '[')) return 0;

    while (poly->cycle_count < MAX_CYCLES) {
        if (!parse_cycle(&p, &poly->cycles[poly->cycle_count])) return 0;
        poly->cycle_count++;

        skip_ws(&p);
        if (*p == '|') {
            p++;
            continue;
        }
        if (*p == ']') {
            p++;
            skip_ws(&p);
            return *p == '\0' || *p == '\n' || *p == '\r';
        }
        return 0;
    }

    return 0;
}

static int parse_tile_list(const char *text, Cycle *out, int *out_count) {
    const char *p = text;
    int count = 0;

    if (!expect_char(&p, '[')) return 0;
    skip_ws(&p);
    if (*p == ']') {
        p++;
        *out_count = 0;
        return 1;
    }

    while (count < MAX_TILES_PER_RECORD) {
        if (!parse_cycle(&p, &out[count])) return 0;
        count++;

        skip_ws(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == ']') {
            p++;
            skip_ws(&p);
            if (*p != '\0' && *p != '\n' && *p != '\r') return 0;
            *out_count = count;
            return 1;
        }
        return 0;
    }

    return 0;
}

static int parse_center(const char *text, Coord *center) {
    const char *p = text;
    if (!expect_char(&p, '(')) return 0;
    if (!parse_int(&p, &center->v)) return 0;
    if (!expect_char(&p, ',')) return 0;
    if (!parse_int(&p, &center->x)) return 0;
    if (!expect_char(&p, ',')) return 0;
    if (!parse_int(&p, &center->y)) return 0;
    if (!expect_char(&p, ')')) return 0;
    skip_ws(&p);
    return *p == '\0' || *p == '\n' || *p == '\r';
}

static int parse_int_list(const char *text, int *out, int *out_count) {
    const char *p = text;
    int count = 0;

    if (!expect_char(&p, '[')) return 0;
    skip_ws(&p);
    if (*p == ']') {
        p++;
        *out_count = 0;
        return 1;
    }

    while (count < MAX_TILES_PER_RECORD) {
        if (!parse_int(&p, &out[count])) return 0;
        count++;

        skip_ws(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == ']') {
            p++;
            skip_ws(&p);
            if (*p != '\0' && *p != '\n' && *p != '\r') return 0;
            *out_count = count;
            return 1;
        }
        return 0;
    }

    return 0;
}

static int parse_coord_list(const char *text, Coord *out, int *out_count) {
    const char *p = text;
    int count = 0;

    if (!expect_char(&p, '[')) return 0;
    skip_ws(&p);
    if (*p == ']') {
        p++;
        *out_count = 0;
        return 1;
    }

    while (count < MAX_TILES_PER_RECORD) {
        if (!expect_char(&p, '(')) return 0;
        if (!parse_int(&p, &out[count].v)) return 0;
        if (!expect_char(&p, ',')) return 0;
        if (!parse_int(&p, &out[count].x)) return 0;
        if (!expect_char(&p, ',')) return 0;
        if (!parse_int(&p, &out[count].y)) return 0;
        if (!expect_char(&p, ')')) return 0;
        count++;

        skip_ws(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == ']') {
            p++;
            skip_ws(&p);
            if (*p != '\0' && *p != '\n' && *p != '\r') return 0;
            *out_count = count;
            return 1;
        }
        return 0;
    }
    return 0;
}

static void reset_record(RL0Record *r) {
    memset(r, 0, sizeof(*r));
}

static int poly_equal_local(const Poly *a, const Poly *b) {
    return !poly_less(a, b) && !poly_less(b, a);
}

static int seen_insert(SeenSet *set,
                       int valence,
                       int tile_count,
                       const Poly *boundary) {
    for (int i = 0; i < set->count; i++) {
        const SeenKey *k = &set->items[i];
        if (k->valence != valence) continue;
        if (k->tile_count != tile_count) continue;
        if (poly_equal_local(&k->boundary, boundary)) return 0;
    }

    if (set->count >= set->cap) {
        int next_cap = (set->cap == 0) ? 64 : (2 * set->cap);
        SeenKey *next = realloc(set->items, (size_t)next_cap * sizeof(SeenKey));
        if (!next) {
            fprintf(stderr, "out of memory while deduplicating records\n");
            exit(1);
        }
        set->items = next;
        set->cap = next_cap;
    }

    set->items[set->count].valence = valence;
    set->items[set->count].tile_count = tile_count;
    set->items[set->count].boundary = *boundary;
    set->count++;
    return 1;
}

static int parse_int_line(const char *line, const char *prefix, int *out) {
    size_t n = strlen(prefix);
    if (strncmp(line, prefix, n) != 0) return 0;
    const char *p = line + n;
    return parse_int(&p, out);
}

static int record_matches(const RL0Record *r,
                          const Options *opt,
                          const Tile *tile) {
    int ok = 0;
    if (!r->have_boundary) return 0;
    if (opt->valence_count > 0) {
        if (!r->have_valence) return 0;
        ok = 0;
        for (int i = 0; i < opt->valence_count; i++) {
            if (r->valence == opt->valences[i]) {
                ok = 1;
                break;
            }
        }
        if (!ok) return 0;
    }
    if (opt->tile_count_count > 0) {
        if (!r->have_tile_count) return 0;
        ok = 0;
        for (int i = 0; i < opt->tile_count_count; i++) {
            if (r->tile_count == opt->tile_counts[i]) {
                ok = 1;
                break;
            }
        }
        if (!ok) return 0;
    }
    if (opt->live_only && !poly_has_live_boundary(&r->boundary, tile)) {
        return 0;
    }
    return 1;
}

static void emit_record(const RL0Record *r,
                        const Tile *tile,
                        int grouped,
                        int index) {
    if (!grouped) {
        tile_print_imgtable_shape(tile, &r->boundary);
        return;
    }

    printf("[%d]\n", index);
    printf("Aggregate\n");
    tile_print_imgtable_shape(tile, &r->boundary);
    printf("TilesLight\n");
    for (int i = 0; i < r->tile_count_list; i++) {
        if (r->have_parities && r->parities[i] == 1) {
            Poly p; p.cycle_count = 1; p.cycles[0] = r->tiles[i];
            tile_print_imgtable_shape(tile, &p);
        }
    }
    printf("TilesDark\n");
    for (int i = 0; i < r->tile_count_list; i++) {
        if (r->have_parities && r->parities[i] == -1) {
            Poly p; p.cycle_count = 1; p.cycles[0] = r->tiles[i];
            tile_print_imgtable_shape(tile, &p);
        }
    }
    if (r->have_center) {
        printf("Center\n");
        printf("(%d,%d,%d)\n", r->center.v, r->center.x, r->center.y);
    }
    if (r->have_hidden) {
        printf("Hidden\n");
        for (int i = 0; i < r->hidden_count; i++) {
            printf("(%d,%d,%d)\n",
                   r->hidden[i].v,
                   r->hidden[i].x,
                   r->hidden[i].y);
        }
    }
}

static int parse_limit(const char *s) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0) return -1;
    return (int)v;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [--data FILE] [--limit N] "
            "[--valence V [V...]] [--tile-count N [N...]] "
            "[--grouped] [--live-only]\n",
            prog);
}

static int parse_args(int argc, char **argv, Options *opt) {
    opt->data_path = "data/rl0/completions.dat";
    opt->limit = 0;
    opt->valence_count = 0;
    opt->tile_count_count = 0;
    opt->grouped = 0;
    opt->live_only = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            opt->data_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            opt->limit = parse_limit(argv[++i]);
            if (opt->limit < 0) return 0;
            continue;
        }
        if (strcmp(argv[i], "--valence") == 0 && i + 1 < argc) {
            int added = 0;
            while (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
                int v = parse_limit(argv[++i]);
                if (v < 0 || opt->valence_count >= 16) return 0;
                opt->valences[opt->valence_count++] = v;
                added = 1;
            }
            if (!added) return 0;
            continue;
        }
        if (strcmp(argv[i], "--tile-count") == 0 && i + 1 < argc) {
            int added = 0;
            while (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
                int n = parse_limit(argv[++i]);
                if (n < 0 || opt->tile_count_count >= 32) return 0;
                opt->tile_counts[opt->tile_count_count++] = n;
                added = 1;
            }
            if (!added) return 0;
            continue;
        }
        if (strcmp(argv[i], "--grouped") == 0) {
            opt->grouped = 1;
            continue;
        }
        if (strcmp(argv[i], "--live-only") == 0) {
            opt->live_only = 1;
            continue;
        }
        return 0;
    }

    return 1;
}

int main(int argc, char **argv) {
    Options opt;
    if (!parse_args(argc, argv, &opt)) {
        usage(argv[0]);
        return 1;
    }

    Tile tile;
    if (!tile_load("tiles/hat.tile", &tile)) {
        fprintf(stderr, "failed to load tile: %s\n", "tiles/hat.tile");
        return 1;
    }

    FILE *fp = fopen(opt.data_path, "r");
    if (!fp) {
        fprintf(stderr, "failed to open data: %s\n", opt.data_path);
        return 1;
    }

    RL0Record rec;
    SeenSet seen = {0};
    reset_record(&rec);

    char line[262144];
    int emitted = 0;
    int record_index = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "---[", 4) == 0) {
            if (rec.have_tiles && (!rec.have_parities ||
                 rec.parity_count != rec.tile_count_list ||
                 rec.parity_count == 0)) {
                fprintf(stderr, "malformed record: missing/invalid parities\n");
                fclose(fp);
                free(seen.items);
                return 1;
            }
            if (record_matches(&rec, &opt, &tile) &&
                seen_insert(&seen,
                            rec.valence,
                            rec.tile_count,
                            &rec.boundary)) {
                record_index++;
                emit_record(&rec, &tile, opt.grouped, record_index);
                emitted++;
                if (opt.limit > 0 && emitted >= opt.limit) break;
            }
            reset_record(&rec);
            continue;
        }

        if (parse_int_line(line, "valence:", &rec.valence)) {
            rec.have_valence = 1;
            continue;
        }
        if (parse_int_line(line, "tile_count:", &rec.tile_count)) {
            rec.have_tile_count = 1;
            continue;
        }
        if (strncmp(line, "center:", 7) == 0) {
            rec.have_center = parse_center(line + 7, &rec.center);
            continue;
        }
        if (strncmp(line, "canonical_boundary:", 19) == 0) {
            rec.have_boundary = parse_poly(line + 19, &rec.boundary);
            continue;
        }
        if (strncmp(line, "tiles:", 6) == 0) {
            rec.have_tiles = parse_tile_list(line + 6,
                                             rec.tiles,
                                             &rec.tile_count_list);
            continue;
        }
        if (strncmp(line, "parities:", 9) == 0) {
            rec.have_parities = parse_int_list(line + 9,
                                               rec.parities,
                                               &rec.parity_count);
            continue;
        }
        if (strncmp(line, "constellation:", 14) == 0) {
            rec.have_hidden = parse_coord_list(line + 14,
                                               rec.hidden,
                                               &rec.hidden_count);
            continue;
        }
        if (strncmp(line, "hidden:", 7) == 0) {
            rec.have_hidden = parse_coord_list(line + 7,
                                               rec.hidden,
                                               &rec.hidden_count);
            continue;
        }
        if (strncmp(line, "indices:", 8) == 0) {
            rec.have_indices = parse_int_list(line + 8,
                                              rec.indices,
                                              &rec.index_count);
            if (rec.have_indices && rec.have_tiles &&
                rec.index_count == rec.tile_count_list &&
                rec.index_count > 0 &&
                rec.indices[0] >= 0 &&
                rec.indices[0] < rec.tiles[0].n) {
                rec.center = rec.tiles[0].v[rec.indices[0]];
                rec.have_center = 1;
            }
            continue;
        }
    }

    if ((!rec.have_parities || !rec.have_tiles ||
         rec.parity_count != rec.tile_count_list ||
         rec.parity_count == 0) && rec.have_tiles) {
        fprintf(stderr, "malformed record: missing/invalid parities\n");
        free(seen.items);
        fclose(fp);
        return 1;
    }

    if ((opt.limit == 0 || emitted < opt.limit) &&
        record_matches(&rec, &opt, &tile) &&
        seen_insert(&seen,
                    rec.valence,
                    rec.tile_count,
                    &rec.boundary)) {
        record_index++;
        emit_record(&rec, &tile, opt.grouped, record_index);
    }

    free(seen.items);
    fclose(fp);
    return 0;
}
