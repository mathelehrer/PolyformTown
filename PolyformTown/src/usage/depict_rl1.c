#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/cycle.h"
#include "core/tile.h"
#include "rl0/boundary0.h"

#define VCOMP_MAX_TILES_FALLBACK 128
#define MAX_COORDS_PER_RECORD (MAX_VERTS * MAX_CYCLES)

typedef struct {
    int have_level;
    int level;
    int have_tile_count;
    int tile_count;
    int have_center_tile;
    Cycle center_tile;
    int have_boundary;
    Poly boundary;
    int have_hidden;
    Coord hidden[MAX_COORDS_PER_RECORD];
    int hidden_count;
    int have_tiles;
    Cycle tiles[VCOMP_MAX_TILES_FALLBACK];
    int tile_list_count;
} RL1Record;

typedef struct {
    const char *tile_path;
    const char *data_path;
    int grouped;
    int limit;
} Options;

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

static int parse_coord(const char **pp, Coord *q) {
    if (!expect_char(pp, '(')) return 0;
    if (!parse_int(pp, &q->v)) return 0;
    if (!expect_char(pp, ',')) return 0;
    if (!parse_int(pp, &q->x)) return 0;
    if (!expect_char(pp, ',')) return 0;
    if (!parse_int(pp, &q->y)) return 0;
    return expect_char(pp, ')');
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
        if (!parse_coord(pp, &cycle->v[cycle->n])) return 0;
        cycle->n++;
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

    while (count < VCOMP_MAX_TILES_FALLBACK) {
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

    while (count < MAX_COORDS_PER_RECORD) {
        if (!parse_coord(&p, &out[count])) return 0;
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


static int append_value_text(char *dst, size_t cap, size_t *off, const char *text) {
    size_t n = strlen(text);
    if (*off + n + 1 > cap) return 0;
    memcpy(dst + *off, text, n);
    *off += n;
    dst[*off] = '\0';
    return 1;
}

static int bracket_delta(const char *text, int *seen_bracket) {
    int d = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '[') { d++; *seen_bracket = 1; }
        else if (*p == ']') d--;
    }
    return d;
}

static int read_section_value(FILE *fp, const char *initial, char *dst, size_t cap) {
    char line[262144];
    size_t off = 0;
    int seen = 0;
    int balance = 0;
    dst[0] = '\0';
    if (!append_value_text(dst, cap, &off, initial)) return 0;
    balance += bracket_delta(initial, &seen);
    while ((!seen || balance > 0) && fgets(line, sizeof(line), fp)) {
        if (!append_value_text(dst, cap, &off, line)) return 0;
        balance += bracket_delta(line, &seen);
    }
    return seen && balance == 0;
}

static int parse_int_line(const char *line, const char *prefix, int *out) {
    size_t n = strlen(prefix);
    const char *p;
    if (strncmp(line, prefix, n) != 0) return 0;
    p = line + n;
    return parse_int(&p, out);
}

static void reset_record(RL1Record *r) {
    memset(r, 0, sizeof(*r));
}

static void emit_record(const RL1Record *r, const Tile *tile, int grouped, int index) {
    if (!r->have_boundary) return;

    if (!grouped) {
        tile_print_imgtable_shape(tile, &r->boundary);
        return;
    }

    printf("[%d]\n", index);
    printf("Aggregate\n");
    tile_print_imgtable_shape(tile, &r->boundary);

    if (r->have_center_tile) {
        Poly p;
        p.cycle_count = 1;
        p.cycles[0] = r->center_tile;
        printf("CenterTile\n");
        tile_print_imgtable_shape(tile, &p);
    }

    if (r->have_tiles) {
        int printed_light = 0;
        int printed_dark = 0;
        for (int pass = 0; pass < 2; pass++) {
            int want_p = pass == 0 ? 1 : -1;
            int printed_header = 0;
            for (int i = 0; i < r->tile_list_count; i++) {
                RL0FMItem item;
                Poly p;
                if (!boundary0_tile_item_at_vertex(tile, &r->tiles[i], r->tiles[i].v[0], &item)) {
                    item.p = 1;
                }
                if (item.p != want_p) continue;
                if (!printed_header) {
                    printf("%s\n", want_p > 0 ? "TilesLight" : "TilesDark");
                    printed_header = 1;
                }
                p.cycle_count = 1;
                p.cycles[0] = r->tiles[i];
                tile_print_imgtable_shape(tile, &p);
                if (want_p > 0) printed_light++;
                else printed_dark++;
            }
        }
        if (!printed_light && !printed_dark) {
            printf("Tiles\n");
            for (int i = 0; i < r->tile_list_count; i++) {
                Poly p;
                p.cycle_count = 1;
                p.cycles[0] = r->tiles[i];
                tile_print_imgtable_shape(tile, &p);
            }
        }
    }

    if (r->have_hidden) {
        printf("Hidden\n");
        for (int i = 0; i < r->hidden_count; i++) {
            printf("(%d,%d,%d)\n", r->hidden[i].v, r->hidden[i].x, r->hidden[i].y);
        }
    }
}

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [tile_path] [data_path] [--grouped] [--limit N]\n"
            "default tile_path: tiles/hat.tile\n"
            "default data_path: data/rl1/completions.dat\n",
            prog);
}

static int parse_args(int argc, char **argv, Options *opt) {
    int positional = 0;
    opt->tile_path = "tiles/hat.tile";
    opt->data_path = "data/rl1/completions.dat";
    opt->grouped = 0;
    opt->limit = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--grouped") == 0) {
            opt->grouped = 1;
            continue;
        }
        if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            opt->limit = atoi(argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            exit(0);
        }
        if (positional == 0) opt->tile_path = argv[i];
        else if (positional == 1) opt->data_path = argv[i];
        else return 0;
        positional++;
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
    if (!tile_load(opt.tile_path, &tile)) {
        fprintf(stderr, "failed to load tile: %s\n", opt.tile_path);
        return 1;
    }

    FILE *fp = fopen(opt.data_path, "r");
    if (!fp) {
        fprintf(stderr, "failed to open data: %s\n", opt.data_path);
        return 1;
    }

    RL1Record rec;
    reset_record(&rec);

    char line[262144];
    int emitted = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "---[", 4) == 0) {
            if (rec.have_boundary) {
                emitted++;
                emit_record(&rec, &tile, opt.grouped, emitted);
                if (opt.limit > 0 && emitted >= opt.limit) break;
            }
            reset_record(&rec);
            continue;
        }
        if (parse_int_line(line, "level:", &rec.level)) {
            rec.have_level = 1;
            continue;
        }
        if (parse_int_line(line, "tile_count:", &rec.tile_count)) {
            rec.have_tile_count = 1;
            continue;
        }
        if (strncmp(line, "center_tile:", 12) == 0) {
            char value[1048576];
            const char *p;
            if (!read_section_value(fp, line + 12, value, sizeof(value))) {
                fprintf(stderr, "failed to read center_tile section\n");
                fclose(fp);
                return 1;
            }
            p = value;
            rec.have_center_tile = parse_cycle(&p, &rec.center_tile);
            continue;
        }
        if (strncmp(line, "boundary:", 9) == 0) {
            char value[1048576];
            if (!read_section_value(fp, line + 9, value, sizeof(value))) {
                fprintf(stderr, "failed to read boundary section\n");
                fclose(fp);
                return 1;
            }
            rec.have_boundary = parse_poly(value, &rec.boundary);
            continue;
        }
        if (strncmp(line, "constellation:", 14) == 0) {
            char value[1048576];
            if (!read_section_value(fp, line + 14, value, sizeof(value))) {
                fprintf(stderr, "failed to read constellation section\n");
                fclose(fp);
                return 1;
            }
            rec.have_hidden = parse_coord_list(value,
                                               rec.hidden,
                                               &rec.hidden_count);
            continue;
        }
        if (strncmp(line, "tiles:", 6) == 0) {
            char value[1048576];
            if (!read_section_value(fp, line + 6, value, sizeof(value))) {
                fprintf(stderr, "failed to read tiles section\n");
                fclose(fp);
                return 1;
            }
            rec.have_tiles = parse_tile_list(value,
                                             rec.tiles,
                                             &rec.tile_list_count);
            continue;
        }
    }

    if (!(opt.limit > 0 && emitted >= opt.limit) && rec.have_boundary) {
        emitted++;
        emit_record(&rec, &tile, opt.grouped, emitted);
    }

    fclose(fp);
    return 0;
}
