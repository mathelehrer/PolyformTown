#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/cycle.h"
#include "core/attach.h"
#include "core/boundary.h"
#include "core/tile.h"

typedef struct {
    int have_valence;
    int valence;
    int have_tile_count;
    int tile_count;
    int have_boundary;
    Poly boundary;
} RL0Record;

typedef struct {
    long total;
    long live;
    long dead;
    long valence_total[8];
    long valence_live[8];
    long tile_total[32];
    long tile_live[32];
} Stats;

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

static int parse_int_line(const char *line, const char *prefix, int *out) {
    size_t n = strlen(prefix);
    if (strncmp(line, prefix, n) != 0) return 0;
    const char *p = line + n;
    return parse_int(&p, out);
}

static void reset_record(RL0Record *r) {
    memset(r, 0, sizeof(*r));
}

static void apply_record(Stats *s, const RL0Record *r, const Tile *tile) {
    int live;
    if (!r->have_boundary) return;

    live = poly_has_live_boundary(&r->boundary, tile);

    s->total++;
    if (live) s->live++; else s->dead++;

    if (r->have_valence && r->valence >= 0 && r->valence < 8) {
        s->valence_total[r->valence]++;
        if (live) s->valence_live[r->valence]++;
    }

    if (r->have_tile_count && r->tile_count >= 0 && r->tile_count < 32) {
        s->tile_total[r->tile_count]++;
        if (live) s->tile_live[r->tile_count]++;
    }
}

int main(int argc, char **argv) {
    const char *data_path = "data/rl0/completions.dat";
    const char *tile_path = "tiles/hat.tile";

    if (argc > 1) data_path = argv[1];
    if (argc > 2) tile_path = argv[2];

    Tile tile;
    if (!tile_load(tile_path, &tile)) {
        fprintf(stderr, "failed to load tile: %s\n", tile_path);
        return 1;
    }

    FILE *fp = fopen(data_path, "r");
    if (!fp) {
        fprintf(stderr, "failed to open data: %s\n", data_path);
        return 1;
    }

    Stats stats;
    RL0Record rec;
    char line[262144];

    memset(&stats, 0, sizeof(stats));
    reset_record(&rec);

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "---[", 4) == 0) {
            apply_record(&stats, &rec, &tile);
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
        if (strncmp(line, "canonical_boundary:", 19) == 0) {
            rec.have_boundary = parse_poly(line + 19, &rec.boundary);
            continue;
        }
    }

    apply_record(&stats, &rec, &tile);
    fclose(fp);

    printf("total_records=%ld\n", stats.total);
    printf("live_records=%ld\n", stats.live);
    printf("dead_records=%ld\n", stats.dead);

    for (int v = 0; v < 8; v++) {
        if (stats.valence_total[v] == 0) continue;
        printf("valence_%d_total=%ld live=%ld dead=%ld\n",
               v,
               stats.valence_total[v],
               stats.valence_live[v],
               stats.valence_total[v] - stats.valence_live[v]);
    }

    for (int n = 0; n < 32; n++) {
        if (stats.tile_total[n] == 0) continue;
        printf("tile_count_%d_total=%ld live=%ld dead=%ld\n",
               n,
               stats.tile_total[n],
               stats.tile_live[n],
               stats.tile_total[n] - stats.tile_live[n]);
    }

    return 0;
}
