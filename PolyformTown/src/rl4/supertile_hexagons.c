#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/tile.h"
#include "rl0/boundary0.h"
#include "rl1/bcomp1.h"

typedef struct { double x, y; } DPoint;
typedef struct { double a11, a12, a21, a22, tx, ty; } Affine2;

typedef struct {
    const char *tile_path;
    const char *data_path;
    const char *supertile_path;
    const char *remembrance_path;
    const char *deletions_path;
    int limit;
    int depth;
} Options;

typedef struct {
    const char *s;
    const Tile *tile;
} ExprParser;

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [tile_path] [data_path] [supertile_path] [options]\n"
            "default tile_path: preferences/focus.tile\n"
            "default data_path: data/rl4/rl3_filtered.dat\n"
            "default supertile_path: preferences/focus.supertile\n"
            "options:\n"
            "  --limit N             emit at most N records\n"
            "  --depth N             completion rings, default 1\n"
            "  --remembrance PATH    default data/rl0/remembrance.dat\n"
            "  --deletions PATH      default data/rl0/deletions.dat\n",
            prog);
}

static int parse_args(int argc, char **argv, Options *opt) {
    int positional = 0;
    opt->tile_path = "preferences/focus.tile";
    opt->data_path = "data/rl4/rl3_filtered.dat";
    opt->supertile_path = "preferences/focus.supertile";
    opt->remembrance_path = "data/rl0/remembrance.dat";
    opt->deletions_path = "data/rl0/deletions.dat";
    opt->limit = 0;
    opt->depth = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) { opt->limit = atoi(argv[++i]); continue; }
        if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc) { opt->depth = atoi(argv[++i]); continue; }
        if (strcmp(argv[i], "--remembrance") == 0 && i + 1 < argc) { opt->remembrance_path = argv[++i]; continue; }
        if (strcmp(argv[i], "--deletions") == 0 && i + 1 < argc) { opt->deletions_path = argv[++i]; continue; }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(argv[0]); exit(0); }
        if (positional == 0) opt->tile_path = argv[i];
        else if (positional == 1) opt->data_path = argv[i];
        else if (positional == 2) opt->supertile_path = argv[i];
        else return 0;
        positional++;
    }
    return 1;
}

static void skip_ws_e(ExprParser *p) {
    while (isspace((unsigned char)*p->s)) p->s++;
}

static double parse_expr_sum(ExprParser *p);

static double eval_constant(const Tile *tile, const char *name) {
    for (int i = 0; i < tile->constant_count; i++) {
        if (strcmp(tile->constants[i].name, name) == 0) {
            ExprParser ep = { tile->constants[i].expr, tile };
            return parse_expr_sum(&ep);
        }
    }
    return 0.0;
}

static double parse_expr_atom(ExprParser *p) {
    skip_ws_e(p);
    if (*p->s == '(') {
        p->s++;
        double v = parse_expr_sum(p);
        skip_ws_e(p);
        if (*p->s == ')') p->s++;
        return v;
    }
    if (isdigit((unsigned char)*p->s) || *p->s == '.') {
        char *end = NULL;
        double v = strtod(p->s, &end);
        p->s = end;
        return v;
    }
    if (isalpha((unsigned char)*p->s) || *p->s == '_') {
        char name[MAX_TILE_EXPR];
        int n = 0;
        while ((isalnum((unsigned char)p->s[n]) || p->s[n] == '_') && n < MAX_TILE_EXPR - 1) {
            name[n] = p->s[n];
            n++;
        }
        name[n] = '\0';
        p->s += n;
        if (strcmp(name, "sqrt") == 0) {
            skip_ws_e(p);
            if (*p->s == '(') {
                p->s++;
                double v = parse_expr_sum(p);
                skip_ws_e(p);
                if (*p->s == ')') p->s++;
                return sqrt(v);
            }
            return 0.0;
        }
        return eval_constant(p->tile, name);
    }
    return 0.0;
}

static double parse_expr_unary(ExprParser *p) {
    skip_ws_e(p);
    if (*p->s == '+') { p->s++; return parse_expr_unary(p); }
    if (*p->s == '-') { p->s++; return -parse_expr_unary(p); }
    return parse_expr_atom(p);
}

static int is_factor_start(char c) {
    return c == '(' || c == '.' || c == '+' || c == '-' ||
           isdigit((unsigned char)c) || isalpha((unsigned char)c) || c == '_';
}

static double parse_expr_product(ExprParser *p) {
    double v = parse_expr_unary(p);
    for (;;) {
        skip_ws_e(p);
        if (*p->s == '*') { p->s++; v *= parse_expr_unary(p); continue; }
        if (*p->s == '/') { p->s++; v /= parse_expr_unary(p); continue; }
        if (is_factor_start(*p->s) && *p->s != ')' && *p->s != ',' && *p->s != ';' && *p->s != '|') {
            v *= parse_expr_unary(p);
            continue;
        }
        break;
    }
    return v;
}

static double parse_expr_sum(ExprParser *p) {
    double v = parse_expr_product(p);
    for (;;) {
        skip_ws_e(p);
        if (*p->s == '+') { p->s++; v += parse_expr_product(p); continue; }
        if (*p->s == '-') { p->s++; v -= parse_expr_product(p); continue; }
        break;
    }
    return v;
}

static double eval_expr(const Tile *tile, const char *s) {
    ExprParser p = { s, tile };
    return parse_expr_sum(&p);
}

static const TileBasis *find_basis(const Tile *tile, int valence) {
    for (int i = 0; i < tile->basis_count; i++) {
        if (tile->bases[i].valence == valence) return &tile->bases[i];
    }
    return NULL;
}

static int basis_values(const Tile *tile, int valence, double *a11, double *a12, double *a21, double *a22) {
    const TileBasis *b = find_basis(tile, valence);
    if (!b) return 0;
    *a11 = eval_expr(tile, b->a11);
    *a12 = eval_expr(tile, b->a12);
    *a21 = eval_expr(tile, b->a21);
    *a22 = eval_expr(tile, b->a22);
    return 1;
}

static DPoint coord_xy(const Tile *tile, Coord p) {
    double a11, a12, a21, a22;
    if (!basis_values(tile, p.v, &a11, &a12, &a21, &a22)) return (DPoint){ (double)p.x, (double)p.y };
    return (DPoint){ a11 * p.x + a21 * p.y, a12 * p.x + a22 * p.y };
}

static int xy_coord(const Tile *tile, int valence, DPoint q, Coord *out) {
    double a11, a12, a21, a22;
    if (!basis_values(tile, valence, &a11, &a12, &a21, &a22)) return 0;
    double det = a11 * a22 - a21 * a12;
    if (fabs(det) < 1e-12) return 0;
    double x = ( a22 * q.x - a21 * q.y) / det;
    double y = (-a12 * q.x + a11 * q.y) / det;
    int xi = (int)llround(x);
    int yi = (int)llround(y);
    if (fabs(x - xi) > 1e-5 || fabs(y - yi) > 1e-5) return 0;
    *out = (Coord){ valence, xi, yi };
    return 1;
}

static int wrap_index(int n, int i) {
    int r = i % n;
    if (r < 0) r += n;
    return r;
}

static int affine_build(const DPoint *r0, const DPoint *r1, const DPoint *r2,
                        const DPoint *c0, const DPoint *c1, const DPoint *c2,
                        Affine2 *out) {
    double ux1 = r1->x - r0->x, uy1 = r1->y - r0->y;
    double ux2 = r2->x - r0->x, uy2 = r2->y - r0->y;
    double vx1 = c1->x - c0->x, vy1 = c1->y - c0->y;
    double vx2 = c2->x - c0->x, vy2 = c2->y - c0->y;
    double det = ux1 * uy2 - uy1 * ux2;
    if (fabs(det) < 1e-12) return 0;
    double inv11 =  uy2 / det, inv12 = -ux2 / det;
    double inv21 = -uy1 / det, inv22 =  ux1 / det;
    out->a11 = vx1 * inv11 + vx2 * inv21;
    out->a12 = vx1 * inv12 + vx2 * inv22;
    out->a21 = vy1 * inv11 + vy2 * inv21;
    out->a22 = vy1 * inv12 + vy2 * inv22;
    out->tx = c0->x - out->a11 * r0->x - out->a12 * r0->y;
    out->ty = c0->y - out->a21 * r0->x - out->a22 * r0->y;
    return 1;
}

static DPoint affine_apply(const Affine2 *a, DPoint p) {
    return (DPoint){ a->a11 * p.x + a->a12 * p.y + a->tx,
                     a->a21 * p.x + a->a22 * p.y + a->ty };
}

static int affine_maps_point(const Affine2 *a, DPoint r, DPoint c) {
    DPoint q = affine_apply(a, r);
    return fabs(q.x - c.x) < 1e-6 && fabs(q.y - c.y) < 1e-6;
}

static int fit_cycle_affine(const Tile *tile, const Cycle *ref, const Cycle *cand, Affine2 *out) {
    if (!ref || !cand || ref->n != cand->n || ref->n < 3) return 0;
    int n = ref->n;
    DPoint r0 = coord_xy(tile, ref->v[0]);
    DPoint r1 = coord_xy(tile, ref->v[1]);
    int rk = 2;
    DPoint r2 = coord_xy(tile, ref->v[rk]);
    while (rk < n) {
        double ux1 = r1.x - r0.x, uy1 = r1.y - r0.y;
        double ux2 = r2.x - r0.x, uy2 = r2.y - r0.y;
        if (fabs(ux1 * uy2 - uy1 * ux2) >= 1e-12) break;
        rk++;
        if (rk < n) r2 = coord_xy(tile, ref->v[rk]);
    }
    if (rk >= n) return 0;
    for (int j = 0; j < n; j++) {
        for (int dir = -1; dir <= 1; dir += 2) {
            DPoint c0 = coord_xy(tile, cand->v[j]);
            DPoint c1 = coord_xy(tile, cand->v[wrap_index(n, j + dir)]);
            DPoint c2 = coord_xy(tile, cand->v[wrap_index(n, j + dir * rk)]);
            Affine2 a;
            if (!affine_build(&r0, &r1, &r2, &c0, &c1, &c2, &a)) continue;
            int ok = 1;
            for (int t = 0; t < n; t++) {
                DPoint rt = coord_xy(tile, ref->v[t]);
                DPoint ct = coord_xy(tile, cand->v[wrap_index(n, j + dir * t)]);
                if (!affine_maps_point(&a, rt, ct)) { ok = 0; break; }
            }
            if (ok) { *out = a; return 1; }
        }
    }
    return 0;
}

static int transform_poly(const Tile *tile, const Poly *src, const Affine2 *a, Poly *dst) {
    dst->cycle_count = src->cycle_count;
    if (dst->cycle_count > MAX_CYCLES) return 0;
    for (int c = 0; c < src->cycle_count; c++) {
        dst->cycles[c].n = src->cycles[c].n;
        if (dst->cycles[c].n > MAX_VERTS) return 0;
        for (int i = 0; i < src->cycles[c].n; i++) {
            Coord p = src->cycles[c].v[i];
            DPoint q = affine_apply(a, coord_xy(tile, p));
            if (!xy_coord(tile, p.v, q, &dst->cycles[c].v[i])) return 0;
        }
    }
    return 1;
}

static int tile_parity(const Tile *tile, const Cycle *cycle) {
    RL0FMItem item;
    if (cycle->n <= 0) return 1;
    if (!boundary0_tile_item_at_vertex(tile, cycle, cycle->v[0], &item)) return 1;
    return item.p;
}

static void print_cycle_shape(const Tile *tile, const Cycle *cycle) {
    Poly p;
    p.cycle_count = 1;
    p.cycles[0] = *cycle;
    tile_print_imgtable_shape(tile, &p);
}

static void print_poly_shape(const Tile *tile, const Poly *poly) {
    tile_print_imgtable_shape(tile, poly);
}

/*
 * Emit a record with projected supertile boundaries and intersection points.
 *
 * This function now constructs all reflected candidate placements of the
 * supertile boundary around each reflected tile in the record.  It then
 * chooses the first successful placement as the aggregate shape.  All
 * projected boundaries are emitted as highlights.  Intersection points
 * where three or more projected boundaries meet are emitted in the
 * Hidden section.  These hidden points are referenced relative to the
 * aggregate shape so that they render as small dots atop the boundary.
 */
static int emit_record(const Tile *tile, const BComp1Record *r,
                       const BComp1Record *supertile,
                       int out_index, int *reflected_total,
                       int *highlight_total, int *fit_fail_total) {
    if (!r->have_boundary || !r->have_tiles) return 0;
    int reflected_count = 0;
    int placed_cap = 0;
    int placed_count = 0;
    Poly *placements = NULL;
    int *tile_indices = NULL;
    /* First, project the supertile boundary around every reflected tile */
    for (int i = 0; i < r->tiles_count; i++) {
        if (tile_parity(tile, &r->tiles[i]) >= 0) continue;
        Affine2 a;
        Poly placed;
        reflected_count++;
        if (!fit_cycle_affine(tile, &supertile->center, &r->tiles[i], &a) ||
            !transform_poly(tile, &supertile->boundary, &a, &placed)) {
            (*fit_fail_total)++;
            continue;
        }
        if (placed_count >= placed_cap) {
            int next_cap = placed_cap == 0 ? 4 : placed_cap * 2;
            Poly *new_pl = (Poly *)realloc(placements, (size_t)next_cap * sizeof(Poly));
            int *new_idx = (int *)realloc(tile_indices, (size_t)next_cap * sizeof(int));
            if (!new_pl || !new_idx) {
                free(new_pl);
                free(new_idx);
                free(placements);
                free(tile_indices);
                fprintf(stderr, "emit_record: out of memory allocating placements\n");
                return 0;
            }
            placements = new_pl;
            tile_indices = new_idx;
            placed_cap = next_cap;
        }
        placements[placed_count] = placed;
        tile_indices[placed_count] = i;
        placed_count++;
    }
    /* Nothing to emit if no placements */
    if (placed_count == 0) {
        free(placements);
        free(tile_indices);
        return 0;
    }
    /* Choose the first placement as the aggregate shape */
    Poly *agg = &placements[0];
    int agg_tile_index = tile_indices[0];
    /* Determine valence for converting world coords to local coords */
    int agg_valence = r->tiles[agg_tile_index].v;
    printf("[%d]\n", out_index);
    printf("Aggregate\n");
    /* Print the aggregate shape, which is a placement of the supertile boundary */
    print_poly_shape(tile, agg);
    /* Print the record's tiles, grouped by parity (light vs dark) */
    int printed_light = 0, printed_dark = 0;
    for (int pass = 0; pass < 2; pass++) {
        int want_p = pass == 0 ? 1 : -1;
        int printed_header = 0;
        for (int i = 0; i < r->tiles_count; i++) {
            int p = tile_parity(tile, &r->tiles[i]);
            if (p != want_p) continue;
            if (!printed_header) {
                printf("%s\n", want_p > 0 ? "TilesLight" : "TilesDark");
                printed_header = 1;
            }
            print_cycle_shape(tile, &r->tiles[i]);
            if (want_p > 0) printed_light++;
            else printed_dark++;
        }
    }
    if (!printed_light && !printed_dark) {
        printf("Tiles\n");
        for (int i = 0; i < r->tiles_count; i++) print_cycle_shape(tile, &r->tiles[i]);
    }
    /* Print all placements as highlights */
    printf("Highlights\n");
    for (int j = 0; j < placed_count; j++) {
        print_poly_shape(tile, &placements[j]);
        (*highlight_total)++;
    }
    /* Build a map of local coordinates to counts */
    /* We'll store unique local coords and their multiplicities in a simple dynamic array */
    typedef struct { Coord loc; int count; } LocCount;
    LocCount *counts = NULL;
    int count_cap = 0;
    int count_len = 0;
    for (int j = 0; j < placed_count; j++) {
        Poly *pl = &placements[j];
        for (int c = 0; c < pl->cycle_count; c++) {
            for (int k = 0; k < pl->cycles[c].n; k++) {
                Coord p = pl->cycles[c].v[k];
                /* Compute world coordinate of this vertex */
                DPoint w = coord_xy(tile, p);
                /* Convert world coordinate to local coordinate relative to aggregate tile */
                Coord local;
                if (!xy_coord(tile, agg_valence, w, &local)) continue;
                /* Find or insert local coordinate in counts array */
                int found = 0;
                for (int t = 0; t < count_len; t++) {
                    if (counts[t].loc.v == local.v && counts[t].loc.x == local.x && counts[t].loc.y == local.y) {
                        counts[t].count++;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    if (count_len >= count_cap) {
                        int next_cap = count_cap == 0 ? 64 : count_cap * 2;
                        LocCount *next_counts = (LocCount *)realloc(counts, (size_t)next_cap * sizeof(LocCount));
                        if (!next_counts) {
                            free(counts);
                            free(placements);
                            free(tile_indices);
                            fprintf(stderr, "emit_record: out of memory allocating intersection counts\n");
                            return 0;
                        }
                        counts = next_counts;
                        count_cap = next_cap;
                    }
                    counts[count_len].loc = local;
                    counts[count_len].count = 1;
                    count_len++;
                }
            }
        }
    }
    /* Emit hidden points where three or more placements meet */
    int hidden_emitted = 0;
    for (int t = 0; t < count_len; t++) {
        if (counts[t].count >= 3) {
            if (!hidden_emitted) {
                printf("Hidden\n");
                hidden_emitted = 1;
            }
            printf("(%d,%d,%d)\n", counts[t].loc.v, counts[t].loc.x, counts[t].loc.y);
        }
    }
    /* Clean up */
    free(counts);
    free(placements);
    free(tile_indices);
    /* Record global counts */
    *reflected_total += reflected_count;
    return 1;
}

static int complete_record(const BComp1Context *ctx,
                           const BComp1Record *src,
                           int depth,
                           BComp1Result *result) {
    BComp1State seed;
    BComp1Options opts;
    if (!src->have_center || !bcomp1_state_from_record(src, &seed)) return 0;
    bcomp1_options_default(&opts);
    opts.depth = depth < 1 ? 1 : depth;
    opts.live_only = 1;
    opts.collect_records = 1;
    opts.stop_after_output = 0;
    opts.progress_interval = 0;
    opts.progress_tty = 0;
    memset(result, 0, sizeof(*result));
    return bcomp1_complete_state(ctx, &seed, &src->center, &opts, result);
}

int main(int argc, char **argv) {
    Options opt;
    BComp1Context ctx;
    BComp1RecordVec records;
    BComp1RecordVec super_records;
    int emitted = 0;
    int highlights = 0;
    int fit_fails = 0;
    int input_records = 0;
    int reflected_candidates = 0;
    int reflected_after_completion = 0;
    int completed = 0;
    int completion_fails = 0;
    int zero_outputs = 0;
    int nonunique_outputs = 0;
    int fallback_original = 0;

    if (!parse_args(argc, argv, &opt)) { usage(argv[0]); return 1; }
    if (!bcomp1_context_init(&ctx, opt.tile_path, opt.remembrance_path, opt.deletions_path)) {
        fprintf(stderr, "failed to initialize bcomp1 context\n");
        return 1;
    }
    if (!bcomp1_load_records(opt.data_path, &records)) {
        fprintf(stderr, "failed to load records: %s\n", opt.data_path);
        bcomp1_context_clear(&ctx);
        return 1;
    }
    if (!bcomp1_load_records(opt.supertile_path, &super_records) || super_records.count < 1 ||
        !super_records.items[0].have_center || !super_records.items[0].have_boundary) {
        fprintf(stderr, "failed to load supertile: %s\n", opt.supertile_path);
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 1;
    }

    for (size_t i = 0; i < records.count; i++) {
        BComp1Result result;
        const BComp1Record *draw = &records.items[i];
        if (!records.items[i].have_boundary || !records.items[i].have_tiles) continue;
        input_records++;
        for (int t = 0; t < records.items[i].tiles_count; t++) {
            if (tile_parity(&ctx.tile, &records.items[i].tiles[t]) < 0) reflected_candidates++;
        }

        memset(&result, 0, sizeof(result));
        if (!complete_record(&ctx, &records.items[i], opt.depth, &result)) {
            completion_fails++;
            fallback_original++;
        } else if (result.records.count == 0) {
            zero_outputs++;
            fallback_original++;
        } else {
            if (result.records.count != 1) nonunique_outputs++;
            draw = &result.records.items[0];
            completed++;
        }

        emitted++;
        emit_record(&ctx.tile, draw, &super_records.items[0], emitted,
                    &reflected_after_completion, &highlights, &fit_fails);
        bcomp1_result_clear(&result);
        if (opt.limit > 0 && emitted >= opt.limit) break;
    }
    fprintf(stderr,
            "rl4_supertile_hexagons: input_records=%d emitted=%d completed_records=%d zero_outputs=%d nonunique_outputs=%d completion_failures=%d fallback_original=%d reflected_input=%d reflected_completed=%d highlights=%d fit_failures=%d\n",
            input_records, emitted, completed, zero_outputs, nonunique_outputs,
            completion_fails, fallback_original, reflected_candidates,
            reflected_after_completion, highlights, fit_fails);
    bcomp1_free_records(&records);
    bcomp1_free_records(&super_records);
    bcomp1_context_clear(&ctx);
    return fit_fails ? 2 : 0;
}
