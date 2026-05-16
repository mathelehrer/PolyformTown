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
    int limit;
} Options;

typedef struct {
    const char *s;
    const Tile *tile;
} ExprParser;

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [tile_path] [data_path] [supertile_path] [--limit N]\n"
            "default tile_path: preferences/focus.tile\n"
            "default data_path: data/rl4/rl3_filtered.dat\n"
            "default supertile_path: preferences/focus.supertile\n",
            prog);
}

static int parse_args(int argc, char **argv, Options *opt) {
    int positional = 0;
    opt->tile_path = "preferences/focus.tile";
    opt->data_path = "data/rl4/rl3_filtered.dat";
    opt->supertile_path = "preferences/focus.supertile";
    opt->limit = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) { opt->limit = atoi(argv[++i]); continue; }
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

static void emit_record(const Tile *tile, const BComp1Record *r,
                        const BComp1Record *supertile, int out_index,
                        int *highlight_total, int *fit_fail_total) {
    int printed_light = 0, printed_dark = 0;
    int printed_highlight = 0;
    if (!r->have_boundary || !r->have_tiles) return;
    printf("[%d]\n", out_index);
    printf("Aggregate\n");
    tile_print_imgtable_shape(tile, &r->boundary);
    for (int pass = 0; pass < 2; pass++) {
        int want_p = pass == 0 ? 1 : -1;
        int printed_header = 0;
        for (int i = 0; i < r->tiles_count; i++) {
            int p = tile_parity(tile, &r->tiles[i]);
            if (p != want_p) continue;
            if (!printed_header) { printf("%s\n", want_p > 0 ? "TilesLight" : "TilesDark"); printed_header = 1; }
            print_cycle_shape(tile, &r->tiles[i]);
            if (want_p > 0) printed_light++; else printed_dark++;
        }
    }
    if (!printed_light && !printed_dark) {
        printf("Tiles\n");
        for (int i = 0; i < r->tiles_count; i++) print_cycle_shape(tile, &r->tiles[i]);
    }
    for (int i = 0; i < r->tiles_count; i++) {
        if (tile_parity(tile, &r->tiles[i]) >= 0) continue;
        Affine2 a;
        Poly placed;
        if (!fit_cycle_affine(tile, &supertile->center, &r->tiles[i], &a) ||
            !transform_poly(tile, &supertile->boundary, &a, &placed)) {
            (*fit_fail_total)++;
            continue;
        }
        if (!printed_highlight) { printf("Highlights\n"); printed_highlight = 1; }
        print_poly_shape(tile, &placed);
        (*highlight_total)++;
    }
    if (r->have_hidden && r->hidden_count > 0) {
        printf("Hidden\n");
        for (int i = 0; i < r->hidden_count; i++) printf("(%d,%d,%d)\n", r->hidden[i].v, r->hidden[i].x, r->hidden[i].y);
    }
}

int main(int argc, char **argv) {
    Options opt;
    Tile tile;
    BComp1RecordVec records;
    BComp1RecordVec super_records;
    int emitted = 0;
    int highlights = 0;
    int fit_fails = 0;

    if (!parse_args(argc, argv, &opt)) { usage(argv[0]); return 1; }
    if (!tile_load(opt.tile_path, &tile)) { fprintf(stderr, "failed to load tile: %s\n", opt.tile_path); return 1; }
    if (!bcomp1_load_records(opt.data_path, &records)) { fprintf(stderr, "failed to load records: %s\n", opt.data_path); return 1; }
    if (!bcomp1_load_records(opt.supertile_path, &super_records) || super_records.count < 1 ||
        !super_records.items[0].have_center || !super_records.items[0].have_boundary) {
        fprintf(stderr, "failed to load supertile: %s\n", opt.supertile_path);
        bcomp1_free_records(&records);
        return 1;
    }

    for (size_t i = 0; i < records.count; i++) {
        if (!records.items[i].have_boundary || !records.items[i].have_tiles) continue;
        emitted++;
        emit_record(&tile, &records.items[i], &super_records.items[0], emitted, &highlights, &fit_fails);
        if (opt.limit > 0 && emitted >= opt.limit) break;
    }
    fprintf(stderr, "depict_supertile_highlights: records=%d highlights=%d fit_failures=%d\n",
            emitted, highlights, fit_fails);
    bcomp1_free_records(&records);
    bcomp1_free_records(&super_records);
    return fit_fails ? 2 : 0;
}
