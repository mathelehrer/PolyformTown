#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "core/cycle.h"
#include "core/tile.h"
#include "throughput/vcomp_pipeline.h"

#define RL1_MAX_RECORD_TEXT 262144

typedef struct {
    int level;
    int tile_count;
    Cycle center_tile;
    Poly boundary;
    char *boundary_str;
    Cycle tiles[VCOMP_MAX_TILES];
    int hidden_count;
    Coord hidden[VCOMP_MAX_HIDDEN];
} RL1Record;

typedef struct {
    FILE *fp;
    const Tile *tile;
    const char *output_path;
    RL1Record *records;
    size_t record_count;
    size_t record_cap;
    size_t append_count;
} RL1Ctx;

static int ensure_dir(const char *path) {
    if (mkdir(path, 0777) == 0) return 1;
    if (errno == EEXIST) return 1;
    return 0;
}

static void ensure_parent_dirs(void) {
    (void)ensure_dir("data");
    (void)ensure_dir("data/rl1");
}

static int coord_less_local(Coord a, Coord b) {
    if (a.v != b.v) return a.v < b.v;
    if (a.x != b.x) return a.x < b.x;
    return a.y < b.y;
}

static int coord_eq_local(Coord a, Coord b) {
    return a.v == b.v && a.x == b.x && a.y == b.y;
}

static int coord_in_list(Coord q, const Coord *items, int count) {
    for (int i = 0; i < count; i++) {
        if (coord_eq_local(q, items[i])) return 1;
    }
    return 0;
}

static int coord_on_boundary(Coord q, const Poly *p) {
    for (int c = 0; c < p->cycle_count; c++) {
        for (int i = 0; i < p->cycles[c].n; i++) {
            if (coord_eq_local(q, p->cycles[c].v[i])) return 1;
        }
    }
    return 0;
}

static int cycles_share_vertex(const Cycle *a, const Cycle *b) {
    for (int i = 0; i < a->n; i++) {
        for (int j = 0; j < b->n; j++) {
            if (coord_eq_local(a->v[i], b->v[j])) return 1;
        }
    }
    return 0;
}

static int tile_is_completely_surrounded(const Cycle *tile,
                                         const Poly *boundary,
                                         const Coord *hidden,
                                         int hidden_count) {
    for (int i = 0; i < tile->n; i++) {
        Coord q = tile->v[i];
        if (coord_on_boundary(q, boundary)) return 0;
        if (!coord_in_list(q, hidden, hidden_count)) return 0;
    }
    return 1;
}

static int select_unique_center_tile(const VCompState *state, Cycle *center_tile) {
    int center_index = -1;

    for (int i = 0; i < state->tile_count; i++) {
        if (!tile_is_completely_surrounded(&state->tiles[i],
                                           &state->poly,
                                           state->hidden,
                                           state->hidden_count)) {
            continue;
        }
        if (center_index >= 0) return 0;
        center_index = i;
    }

    if (center_index < 0) return 0;

    for (int i = 0; i < state->tile_count; i++) {
        if (i == center_index) continue;
        if (!cycles_share_vertex(&state->tiles[i], &state->tiles[center_index])) {
            return 0;
        }
    }

    *center_tile = state->tiles[center_index];
    return 1;
}

static int append_text(char **buf, size_t *cap, size_t *off, const char *text) {
    size_t n = strlen(text);
    if (*off + n + 1 > *cap) {
        size_t next = *cap ? *cap : 4096;
        while (*off + n + 1 > next) next *= 2;
        char *p = realloc(*buf, next);
        if (!p) return 0;
        *buf = p;
        *cap = next;
    }
    memcpy(*buf + *off, text, n);
    *off += n;
    (*buf)[*off] = '\0';
    return 1;
}

static int append_coord_text(char **buf, size_t *cap, size_t *off, Coord q) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "(%d,%d,%d)", q.v, q.x, q.y);
    return append_text(buf, cap, off, tmp);
}

static char *poly_to_string_local(const Poly *p) {
    char *buf = NULL;
    size_t cap = 0, off = 0;

    if (!append_text(&buf, &cap, &off, "[")) return NULL;
    for (int c = 0; c < p->cycle_count; c++) {
        if (c && !append_text(&buf, &cap, &off, "|")) goto fail;
        if (!append_text(&buf, &cap, &off, "[")) goto fail;
        for (int i = 0; i < p->cycles[c].n; i++) {
            if (i && !append_text(&buf, &cap, &off, ",")) goto fail;
            if (!append_coord_text(&buf, &cap, &off, p->cycles[c].v[i])) goto fail;
        }
        if (!append_text(&buf, &cap, &off, "]")) goto fail;
    }
    if (!append_text(&buf, &cap, &off, "]")) goto fail;
    return buf;

fail:
    free(buf);
    return NULL;
}

static void print_cycle(FILE *fp, const Cycle *c) {
    fprintf(fp, "[");
    for (int i = 0; i < c->n; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "(%d,%d,%d)", c->v[i].v, c->v[i].x, c->v[i].y);
    }
    fprintf(fp, "]");
}

static void print_tile_list(FILE *fp, const Cycle *tiles, int tile_count) {
    fprintf(fp, "[");
    for (int i = 0; i < tile_count; i++) {
        if (i) fprintf(fp, ",");
        print_cycle(fp, &tiles[i]);
    }
    fprintf(fp, "]");
}

static void print_coord_list(FILE *fp, const Coord *coords, int count) {
    fprintf(fp, "[");
    for (int i = 0; i < count; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "(%d,%d,%d)", coords[i].v, coords[i].x, coords[i].y);
    }
    fprintf(fp, "]");
}

static void write_one_record(FILE *fp, size_t index, const RL1Record *rec) {
    fprintf(fp, "---[%zu]---\n", index);
    fprintf(fp, "level:%d\n", rec->level);
    fprintf(fp, "tile_count:%d\n", rec->tile_count);
    fprintf(fp, "center_tile:");
    print_cycle(fp, &rec->center_tile);
    fprintf(fp, "\n");
    fprintf(fp, "boundary:");
    fputs(rec->boundary_str, fp);
    fprintf(fp, "\n");
    fprintf(fp, "constellation:");
    print_coord_list(fp, rec->hidden, rec->hidden_count);
    fprintf(fp, "\n");
    fprintf(fp, "tiles:");
    print_tile_list(fp, rec->tiles, rec->tile_count);
    fprintf(fp, "\n");
}

static int records_add(RL1Ctx *ctx,
                       int level,
                       const VCompState *state,
                       const Cycle *center_tile) {
    if (ctx->record_count == ctx->record_cap) {
        size_t next = ctx->record_cap ? ctx->record_cap * 2 : 128;
        RL1Record *p = realloc(ctx->records, next * sizeof(*ctx->records));
        if (!p) return 0;
        ctx->records = p;
        ctx->record_cap = next;
    }

    RL1Record *rec = &ctx->records[ctx->record_count];
    rec->level = level;
    rec->tile_count = state->tile_count;
    rec->center_tile = *center_tile;
    rec->boundary = state->poly;
    rec->boundary_str = poly_to_string_local(&state->poly);
    if (!rec->boundary_str) return 0;
    rec->hidden_count = state->hidden_count;

    for (int i = 0; i < state->tile_count; i++) rec->tiles[i] = state->tiles[i];
    for (int i = 0; i < state->hidden_count; i++) rec->hidden[i] = state->hidden[i];

    ctx->record_count++;
    return 1;
}

static int cycle_cmp_local(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return (a->n < b->n) ? -1 : 1;
    for (int i = 0; i < a->n; i++) {
        if (coord_less_local(a->v[i], b->v[i])) return -1;
        if (coord_less_local(b->v[i], a->v[i])) return 1;
    }
    return 0;
}

static int record_cmp_local(const void *A, const void *B) {
    const RL1Record *a = A;
    const RL1Record *b = B;

    if (a->level != b->level) return (a->level < b->level) ? -1 : 1;
    if (a->tile_count != b->tile_count) return (a->tile_count < b->tile_count) ? -1 : 1;
    int ccmp = cycle_cmp_local(&a->center_tile, &b->center_tile);
    if (ccmp) return ccmp;
    return strcmp(a->boundary_str, b->boundary_str);
}

static int on_level(int level,
                    const VCompStateVec *states,
                    size_t unique_poly_count,
                    const Tile *tile,
                    void *userdata) {
    RL1Ctx *ctx = userdata;
    (void)unique_poly_count;
    (void)tile;

    for (size_t i = 0; i < states->count; i++) {
        Cycle center_tile;
        if (!select_unique_center_tile(&states->data[i], &center_tile)) continue;
        if (!records_add(ctx, level, &states->data[i], &center_tile)) return 0;

        ctx->append_count++;
        write_one_record(ctx->fp, ctx->append_count, &ctx->records[ctx->record_count - 1]);
        fflush(ctx->fp);
    }

    fprintf(stderr,
            "level %d: states=%zu hits=%zu\n",
            level,
            states->count,
            ctx->append_count);
    fflush(stderr);
    return 1;
}

static void rewrite_sorted_records(RL1Ctx *ctx) {
    qsort(ctx->records,
          ctx->record_count,
          sizeof(*ctx->records),
          record_cmp_local);

    FILE *fp = fopen(ctx->output_path, "w");
    if (!fp) {
        fprintf(stderr, "failed to rewrite output: %s\n", ctx->output_path);
        return;
    }

    for (size_t i = 0; i < ctx->record_count; i++) {
        write_one_record(fp, i + 1, &ctx->records[i]);
    }
    fclose(fp);
}

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [tile_path] [output_path] [max_n] [--live-only]\n"
            "default tile_path: tiles/hat.tile\n"
            "default output_path: data/rl1/naive_hat.dat\n"
            "default max_n: 20\n",
            prog);
}

int main(int argc, char **argv) {
    const char *tile_path = "tiles/hat.tile";
    const char *output_path = "data/rl1/naive_hat.dat";
    int max_n = 20;
    int live_only = 0;
    int positional = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--live-only") == 0) {
            live_only = 1;
            continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (positional == 0) tile_path = argv[i];
        else if (positional == 1) output_path = argv[i];
        else if (positional == 2) max_n = atoi(argv[i]);
        else {
            usage(argv[0]);
            return 1;
        }
        positional++;
    }

    if (max_n < 0) max_n = 0;
    if (max_n >= VCOMP_MAX_LEVELS) max_n = VCOMP_MAX_LEVELS - 1;

    Tile tile;
    if (!tile_load(tile_path, &tile)) {
        fprintf(stderr, "failed to load tile: %s\n", tile_path);
        return 1;
    }

    ensure_parent_dirs();

    FILE *fp = fopen(output_path, "w");
    if (!fp) {
        fprintf(stderr, "failed to open output: %s\n", output_path);
        return 1;
    }

    RL1Ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fp = fp;
    ctx.tile = &tile;
    ctx.output_path = output_path;

    run_vcomp_levels(&tile, max_n, 1, live_only, on_level, &ctx);
    fclose(fp);
    ctx.fp = NULL;

    rewrite_sorted_records(&ctx);

    fprintf(stderr,
            "wrote %zu records to %s\n",
            ctx.record_count,
            output_path);

    for (size_t i = 0; i < ctx.record_count; i++) free(ctx.records[i].boundary_str);
    free(ctx.records);
    return 0;
}
