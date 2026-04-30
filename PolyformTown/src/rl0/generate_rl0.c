#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "core/boundary.h"
#include "core/cycle.h"
#include "core/lattice.h"
#include "core/tile.h"
#include "throughput/vcomp.h"

#define RL0_MAX_TRACE MAX_VERTS

typedef struct {
    int valence;
    int tile_count;
    Poly poly;
} RL0SeenKey;

typedef struct {
    int valence;
    int tile_count;
    Coord center;
    Poly poly;
    char *boundary_str;
    Cycle tiles[RL0_MAX_TRACE];
    int indices[RL0_MAX_TRACE];
    Coord hidden[VCOMP_MAX_HIDDEN];
    int hidden_count;
} RL0Record;

typedef struct {
    FILE *fp;
    int lattice;
    const Tile *tile;
    Coord target;
    int record_no;
    RL0SeenKey *seen;
    size_t seen_count;
    size_t seen_cap;
    RL0Record *records;
    size_t record_count;
    size_t record_cap;
} RL0Ctx;

static int coord_less_local(Coord a, Coord b) {
    if (a.v != b.v) return a.v < b.v;
    if (a.x != b.x) return a.x < b.x;
    return a.y < b.y;
}

static int coord_cmp_local(const void *A, const void *B) {
    const Coord *a = A;
    const Coord *b = B;
    if (coord_less_local(*a, *b)) return -1;
    if (coord_less_local(*b, *a)) return 1;
    return 0;
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

static void print_indices(FILE *fp, const int *indices, int count) {
    fprintf(fp, "[");
    for (int i = 0; i < count; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "%d", indices[i]);
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

static int cycle_vertex_index(const Cycle *c, Coord q) {
    for (int i = 0; i < c->n; i++) {
        if (coord_eq(c->v[i], q)) return i;
    }
    return -1;
}

static int poly_equal_local(const Poly *a, const Poly *b) {
    if (a->cycle_count != b->cycle_count) return 0;
    for (int i = 0; i < a->cycle_count; i++) {
        if (a->cycles[i].n != b->cycles[i].n) return 0;
        for (int j = 0; j < a->cycles[i].n; j++) {
            if (!coord_eq(a->cycles[i].v[j], b->cycles[i].v[j])) return 0;
        }
    }
    return 1;
}

static int seen_has(const RL0Ctx *ctx, int valence, int tile_count, const Poly *poly) {
    for (size_t i = 0; i < ctx->seen_count; i++) {
        if (ctx->seen[i].valence != valence) continue;
        if (ctx->seen[i].tile_count != tile_count) continue;
        if (poly_equal_local(&ctx->seen[i].poly, poly)) return 1;
    }
    return 0;
}

static int seen_add(RL0Ctx *ctx, int valence, int tile_count, const Poly *poly) {
    if (ctx->seen_count == ctx->seen_cap) {
        size_t nc = ctx->seen_cap ? (ctx->seen_cap * 2) : 128;
        RL0SeenKey *n = realloc(ctx->seen, nc * sizeof(*ctx->seen));
        if (!n) return 0;
        ctx->seen = n;
        ctx->seen_cap = nc;
    }
    ctx->seen[ctx->seen_count].valence = valence;
    ctx->seen[ctx->seen_count].tile_count = tile_count;
    ctx->seen[ctx->seen_count].poly = *poly;
    ctx->seen_count++;
    return 1;
}

static char *poly_to_string_local(const Poly *p) {
    size_t cap = 4096;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t off = 0;

    off += (size_t)snprintf(buf + off, cap - off, "[");
    for (int i = 0; i < p->cycle_count; i++) {
        if (i) off += (size_t)snprintf(buf + off, cap - off, "|");
        off += (size_t)snprintf(buf + off, cap - off, "[");
        for (int j = 0; j < p->cycles[i].n; j++) {
            if (j) off += (size_t)snprintf(buf + off, cap - off, ",");
            off += (size_t)snprintf(buf + off,
                                    cap - off,
                                    "(%d,%d,%d)",
                                    p->cycles[i].v[j].v,
                                    p->cycles[i].v[j].x,
                                    p->cycles[i].v[j].y);
        }
        off += (size_t)snprintf(buf + off, cap - off, "]");
    }
    off += (size_t)snprintf(buf + off, cap - off, "]");
    return buf;
}

static int records_add(RL0Ctx *ctx,
                       int valence,
                       int tile_count,
                       Coord center,
                       const VCompRawState *raw,
                       const int *indices) {
    if (ctx->record_count == ctx->record_cap) {
        size_t nc = ctx->record_cap ? (ctx->record_cap * 2) : 128;
        RL0Record *n = realloc(ctx->records, nc * sizeof(*ctx->records));
        if (!n) return 0;
        ctx->records = n;
        ctx->record_cap = nc;
    }

    RL0Record *rec = &ctx->records[ctx->record_count];
    rec->valence = valence;
    rec->tile_count = tile_count;
    rec->center = center;
    rec->poly = raw->poly;
    rec->boundary_str = poly_to_string_local(&raw->poly);
    if (!rec->boundary_str) return 0;
    rec->hidden_count = raw->hidden_count;

    for (int i = 0; i < tile_count; i++) {
        rec->tiles[i] = raw->tiles[i];
        rec->indices[i] = indices[i];
    }
    for (int i = 0; i < raw->hidden_count; i++) {
        rec->hidden[i] = raw->hidden[i];
    }

    ctx->record_count++;
    return 1;
}

static int record_cmp_local(const void *A, const void *B) {
    const RL0Record *a = A;
    const RL0Record *b = B;

    if (a->tile_count != b->tile_count) {
        return (a->tile_count < b->tile_count) ? -1 : 1;
    }

    int pcmp = strcmp(a->boundary_str, b->boundary_str);
    if (pcmp != 0) return pcmp;

    if (a->hidden_count != b->hidden_count) {
        return (a->hidden_count < b->hidden_count) ? -1 : 1;
    }

    if (a->valence != b->valence) {
        return (a->valence < b->valence) ? -1 : 1;
    }
    return 0;
}

static void write_records(RL0Ctx *ctx) {
    qsort(ctx->records,
          ctx->record_count,
          sizeof(*ctx->records),
          record_cmp_local);

    for (size_t i = 0; i < ctx->record_count; i++) {
        const RL0Record *rec = &ctx->records[i];
        fprintf(ctx->fp, "---[%zu]---\n", i + 1);
        fprintf(ctx->fp, "valence:%d\n", rec->valence);
        fprintf(ctx->fp, "tile_count:%d\n", rec->tile_count);
        fprintf(ctx->fp,
                "center:(%d,%d,%d)\n",
                rec->center.v,
                rec->center.x,
                rec->center.y);
        fprintf(ctx->fp, "canonical_boundary:");
        fputs(rec->boundary_str, ctx->fp);
        fprintf(ctx->fp, "\n");
        fprintf(ctx->fp, "tiles:");
        print_tile_list(ctx->fp, rec->tiles, rec->tile_count);
        fprintf(ctx->fp, "\n");
        fprintf(ctx->fp, "hidden:");
        print_coord_list(ctx->fp, rec->hidden, rec->hidden_count);
        fprintf(ctx->fp, "\n");
        fprintf(ctx->fp, "indices:");
        print_indices(ctx->fp, rec->indices, rec->tile_count);
        fprintf(ctx->fp, "\n");
    }
}

static int emit_raw_completion(const VCompRawState *raw, RL0Ctx *ctx) {
    int indices[RL0_MAX_TRACE];
    int total_tile_count = raw->tile_count;
    Coord center = raw->target;

    if (!poly_has_live_boundary(&raw->poly, ctx->tile)) return 1;

    for (int i = 0; i < total_tile_count; i++) {
        indices[i] = cycle_vertex_index(&raw->tiles[i], center);
    }

    int valence = lattice_direction_count(ctx->lattice);
    if (ctx->lattice == TILE_LATTICE_TETRILLE &&
        (center.v == 3 || center.v == 4 || center.v == 6)) {
        valence = center.v;
    }
    if (seen_has(ctx, valence, total_tile_count, &raw->poly)) return 1;
    if (!seen_add(ctx, valence, total_tile_count, &raw->poly)) return 0;

    return records_add(ctx, valence, total_tile_count, center, raw, indices);
}

static int ensure_dir(const char *path) {
    if (mkdir(path, 0777) == 0) return 1;
    if (errno == EEXIST) return 1;
    return 0;
}

int main(int argc, char **argv) {
    const char *tile_path = "tiles/hat.tile";
    const char *output_path = "levelData/rl0/completions.dat";

    if (argc > 1) tile_path = argv[1];
    if (argc > 2) output_path = argv[2];

    Tile tile;
    if (!tile_load(tile_path, &tile)) {
        fprintf(stderr, "failed to load tile: %s\n", tile_path);
        return 1;
    }

    if (!ensure_dir("levelData") || !ensure_dir("levelData/rl0")) {
        fprintf(stderr, "failed to create levelData/rl0\n");
        return 1;
    }

    FILE *fp = fopen(output_path, "w");
    if (!fp) {
        fprintf(stderr, "failed to open output: %s\n", output_path);
        return 1;
    }

    Poly seed;
    Coord verts[MAX_VERTS * MAX_CYCLES];
    int vc;

    memset(&seed, 0, sizeof(seed));
    seed.cycle_count = 1;
    seed.cycles[0] = tile.base;
    vc = build_boundary_vertices(&seed, verts);
    if (vc < 0) {
        fprintf(stderr, "failed to build boundary vertices\n");
        fclose(fp);
        return 1;
    }

    qsort(verts, (size_t)vc, sizeof(Coord), coord_cmp_local);

    RL0Ctx ctx;
    ctx.fp = fp;
    ctx.lattice = tile.lattice;
    ctx.tile = &tile;
    ctx.record_no = 0;
    ctx.seen = NULL;
    ctx.seen_count = 0;
    ctx.seen_cap = 0;
    ctx.records = NULL;
    ctx.record_count = 0;
    ctx.record_cap = 0;

    int ok = 1;
    for (int i = 0; i < vc && ok; i++) {
        VCompLevels raw;
        ctx.target = verts[i];
        vcomp_levels_init(&raw, VCOMP_MAX_LEVELS - 1);
        vcomp_enumerate_levels(&seed,
                               &tile,
                               verts[i],
                               NULL,
                               0,
                               NULL,
                               0,
                               NULL,
                               0,
                               VCOMP_MAX_LEVELS - 1,
                               1,
                               &raw);
        for (int level = 1; level <= raw.max_level; level++) {
            for (size_t r = 0; r < raw.levels[level].count; r++) {
                if (!emit_raw_completion(&raw.levels[level].data[r], &ctx)) {
                    ok = 0;
                    break;
                }
            }
            if (!ok) break;
        }
        vcomp_levels_destroy(&raw);
    }

    if (!ok) {
        fprintf(stderr, "allocation failure while generating RL0 records\n");
        fclose(fp);
        free(ctx.seen);
        for (size_t i = 0; i < ctx.record_count; i++) {
            free(ctx.records[i].boundary_str);
        }
        free(ctx.records);
        return 1;
    }

    write_records(&ctx);

    fclose(fp);
    free(ctx.seen);
    for (size_t i = 0; i < ctx.record_count; i++) {
        free(ctx.records[i].boundary_str);
    }
    free(ctx.records);
    return 0;
}
