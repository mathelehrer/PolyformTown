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
    const char *parents_path;
    int limit;
    int data_mode;
    int verbose;
} Options;

typedef struct {
    const char *s;
    const Tile *tile;
} ExprParser;

typedef struct {
    Poly poly;
    int tile_index;
} Projection;

typedef struct {
    int parent;
    int size;
} DSUNode;

typedef struct {
    int root;
    int count;
    double x;
    double y;
    int member_count;
    Coord members[16];
    char label[512];
} HiddenCluster;

typedef struct {
    char lhs[256];
    char rhs[256];
} EdgeRulePair;

#define RL4_MAX_HIDDEN (MAX_VERTS * MAX_CYCLES)
#define RL4_MAX_CLUSTERS RL4_MAX_HIDDEN

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [tile_path] [data_path] [supertile_path] [options]\n"
            "default tile_path: preferences/focus.tile\n"
            "default data_path: data/rl4/rl3_filtered.dat\n"
            "default supertile_path: preferences/focus.supertile\n"
            "options:\n"
            "  --limit N             emit at most N records\n"
            "  --data                print supertile hexagon data instead of imgtable\n"
            "  --parents PATH        default data/rl4/rl2_filtered.dat\n"
            "  --remembrance PATH    default data/rl0/remembrance.dat\n"
            "  --deletions PATH      default data/rl0/deletions.dat\n"
            "  --verbose             print per-record diagnostics to stderr\n",
            prog);
}

static int parse_args(int argc, char **argv, Options *opt) {
    int positional = 0;
    opt->tile_path = "preferences/focus.tile";
    opt->data_path = "data/rl4/rl3_filtered.dat";
    opt->supertile_path = "preferences/focus.supertile";
    opt->remembrance_path = "data/rl0/remembrance.dat";
    opt->deletions_path = "data/rl0/deletions.dat";
    opt->parents_path = "data/rl4/rl2_filtered.dat";
    opt->limit = 0;
    opt->data_mode = 0;
    opt->verbose = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            opt->limit = atoi(argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--data") == 0) {
            opt->data_mode = 1;
            continue;
        }
        if (strcmp(argv[i], "--verbose") == 0) {
            opt->verbose = 1;
            continue;
        }
        if (strcmp(argv[i], "--parents") == 0 && i + 1 < argc) {
            opt->parents_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--remembrance") == 0 && i + 1 < argc) {
            opt->remembrance_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--deletions") == 0 && i + 1 < argc) {
            opt->deletions_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            exit(0);
        }
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
        while ((isalnum((unsigned char)p->s[n]) || p->s[n] == '_') &&
               n < MAX_TILE_EXPR - 1) {
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
        if (is_factor_start(*p->s) && *p->s != ')' && *p->s != ',' &&
            *p->s != ';' && *p->s != '|') {
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

static int basis_values(const Tile *tile, int valence,
                        double *a11, double *a12,
                        double *a21, double *a22) {
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
    if (!basis_values(tile, p.v, &a11, &a12, &a21, &a22)) {
        return (DPoint){ (double)p.x, (double)p.y };
    }
    return (DPoint){ a11 * p.x + a21 * p.y,
                     a12 * p.x + a22 * p.y };
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

static int fit_cycle_affine(const Tile *tile, const Cycle *ref,
                            const Cycle *cand, Affine2 *out) {
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

static int transform_poly(const Tile *tile, const Poly *src,
                          const Affine2 *a, Poly *dst) {
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

static int coord_same(Coord a, Coord b) {
    return a.v == b.v && a.x == b.x && a.y == b.y;
}

static int poly_has_coord(const Poly *poly, Coord q) {
    for (int c = 0; c < poly->cycle_count; c++) {
        for (int i = 0; i < poly->cycles[c].n; i++) {
            if (coord_same(poly->cycles[c].v[i], q)) return 1;
        }
    }
    return 0;
}

static int coord_seen(const Coord *items, int count, Coord q) {
    for (int i = 0; i < count; i++) {
        if (coord_same(items[i], q)) return 1;
    }
    return 0;
}

static int collect_hidden_vertices(const Projection *proj, int proj_count,
                                   Coord *hidden, int hidden_cap) {
    int hidden_count = 0;
    for (int p = 0; p < proj_count; p++) {
        for (int c = 0; c < proj[p].poly.cycle_count; c++) {
            const Cycle *cy = &proj[p].poly.cycles[c];
            for (int i = 0; i < cy->n; i++) {
                Coord q = cy->v[i];
                int degree = 0;
                for (int k = 0; k < proj_count; k++) {
                    if (poly_has_coord(&proj[k].poly, q)) degree++;
                }
                if (degree >= 3 && !coord_seen(hidden, hidden_count, q) &&
                    hidden_count < hidden_cap) {
                    hidden[hidden_count++] = q;
                }
            }
        }
    }
    return hidden_count;
}


static int hidden_index_of(const Coord *hidden, int hidden_count, Coord q) {
    for (int i = 0; i < hidden_count; i++) {
        if (coord_same(hidden[i], q)) return i;
    }
    return -1;
}

static int dsu_find(DSUNode *dsu, int i) {
    while (dsu[i].parent != i) {
        dsu[i].parent = dsu[dsu[i].parent].parent;
        i = dsu[i].parent;
    }
    return i;
}

static void dsu_union(DSUNode *dsu, int a, int b) {
    int ra = dsu_find(dsu, a);
    int rb = dsu_find(dsu, b);
    if (ra == rb) return;
    if (dsu[ra].size < dsu[rb].size) {
        int tmp = ra;
        ra = rb;
        rb = tmp;
    }
    dsu[rb].parent = ra;
    dsu[ra].size += dsu[rb].size;
}

static int boundary_linear_index(const Poly *boundary, int cycle_index, int vertex_index) {
    int offset = 0;
    for (int c = 0; c < cycle_index && c < boundary->cycle_count; c++) {
        offset += boundary->cycles[c].n;
    }
    return offset + vertex_index;
}

static int coord_template_index(const Projection *proj,
                                const Coord q,
                                int *out_index) {
    for (int c = 0; c < proj->poly.cycle_count; c++) {
        const Cycle *cy = &proj->poly.cycles[c];
        for (int i = 0; i < cy->n; i++) {
            if (coord_same(cy->v[i], q)) {
                *out_index = boundary_linear_index(&proj->poly, c, i);
                return 1;
            }
        }
    }
    return 0;
}

static void append_label(char *dst, size_t cap, const char *src) {
    size_t used = strlen(dst);
    size_t n = strlen(src);
    if (used + n + 2 >= cap) return;
    if (used > 0) dst[used++] = '.';
    memcpy(dst + used, src, n + 1);
}

static int cluster_cmp_ccw(const void *A, const void *B) {
    const HiddenCluster *a = (const HiddenCluster *)A;
    const HiddenCluster *b = (const HiddenCluster *)B;
    /* x/y were recentered into angle coordinates by caller: x=angle, y=radius. */
    if (a->x < b->x) return -1;
    if (a->x > b->x) return 1;
    if (a->y < b->y) return -1;
    if (a->y > b->y) return 1;
    return 0;
}

static int build_hidden_clusters(const Tile *tile,
                                 const Projection *proj,
                                 int proj_count,
                                 const Coord *hidden,
                                 int hidden_count,
                                 HiddenCluster *clusters,
                                 int cluster_cap) {
    DSUNode *dsu = calloc((size_t)hidden_count, sizeof(*dsu));
    int cluster_count = 0;
    if (!dsu) return 0;
    for (int i = 0; i < hidden_count; i++) {
        dsu[i].parent = i;
        dsu[i].size = 1;
    }

    /* Join edge-step nearest neighbors: two special hidden vertices are
       neighbors exactly when they are adjacent along at least one projected
       supertile boundary edge. */
    for (int p = 0; p < proj_count; p++) {
        for (int c = 0; c < proj[p].poly.cycle_count; c++) {
            const Cycle *cy = &proj[p].poly.cycles[c];
            for (int i = 0; i < cy->n; i++) {
                Coord a = cy->v[i];
                Coord b = cy->v[(i + 1) % cy->n];
                int ai = hidden_index_of(hidden, hidden_count, a);
                int bi = hidden_index_of(hidden, hidden_count, b);
                if (ai >= 0 && bi >= 0) dsu_union(dsu, ai, bi);
            }
        }
    }

    for (int i = 0; i < hidden_count; i++) {
        int root = dsu_find(dsu, i);
        int ci = -1;
        for (int j = 0; j < cluster_count; j++) {
            if (clusters[j].root == root) { ci = j; break; }
        }
        if (ci < 0) {
            if (cluster_count >= cluster_cap) break;
            ci = cluster_count++;
            clusters[ci].root = root;
            clusters[ci].count = 0;
            clusters[ci].x = 0.0;
            clusters[ci].y = 0.0;
            clusters[ci].label[0] = '\0';
            clusters[ci].member_count = 0;
        }
        DPoint pt = coord_xy(tile, hidden[i]);
        clusters[ci].count++;
        clusters[ci].x += pt.x;
        clusters[ci].y += pt.y;
        if (clusters[ci].member_count < (int)(sizeof(clusters[ci].members) / sizeof(clusters[ci].members[0]))) {
            clusters[ci].members[clusters[ci].member_count++] = hidden[i];
        }

        int template_index = -1;
        char one[32];
        for (int p = 0; p < proj_count; p++) {
            if (coord_template_index(&proj[p], hidden[i], &template_index)) break;
        }
        if (template_index >= 0) snprintf(one, sizeof(one), "v%d", template_index);
        else snprintf(one, sizeof(one), "(%d,%d,%d)", hidden[i].v, hidden[i].x, hidden[i].y);
        append_label(clusters[ci].label, sizeof(clusters[ci].label), one);
    }

    double cx = 0.0, cy = 0.0;
    int total = 0;
    for (int i = 0; i < cluster_count; i++) {
        cx += clusters[i].x;
        cy += clusters[i].y;
        total += clusters[i].count;
    }
    if (total > 0) {
        cx /= (double)total;
        cy /= (double)total;
    }
    for (int i = 0; i < cluster_count; i++) {
        double px = clusters[i].x / (double)clusters[i].count;
        double py = clusters[i].y / (double)clusters[i].count;
        double dx = px - cx;
        double dy = py - cy;
        clusters[i].x = atan2(dy, dx);
        clusters[i].y = sqrt(dx * dx + dy * dy);
    }
    qsort(clusters, (size_t)cluster_count, sizeof(clusters[0]), cluster_cmp_ccw);
    free(dsu);
    return cluster_count;
}


typedef struct {
    char key[2048];
    int count;
    int first_record;
    char records[1024];
} UniqueHex;

typedef struct {
    int record;
    int source;
    int parent;
    int unique_id;
    int projected;
    int hidden;
    int clusters;
    char key[2048];
} ModelRow;

static void build_hex_key(const HiddenCluster *clusters,
                          int cluster_count,
                          char *out,
                          size_t cap) {
    size_t off = 0;
    out[0] = '\0';
    off += (size_t)snprintf(out + off, off < cap ? cap - off : 0, "[");
    for (int i = 0; i < cluster_count && off < cap; i++) {
        off += (size_t)snprintf(out + off, cap - off, "%s[%s]", i ? "," : "", clusters[i].label);
    }
    if (off < cap) snprintf(out + off, cap - off, "]");
}

static int unique_hex_add(UniqueHex **items_io,
                          int *count_io,
                          int *cap_io,
                          const char *key,
                          int record_index) {
    for (int i = 0; i < *count_io; i++) {
        if (strcmp((*items_io)[i].key, key) == 0) {
            char tmp[32];
            (*items_io)[i].count++;
            snprintf(tmp, sizeof(tmp), " %d", record_index);
            if (strlen((*items_io)[i].records) + strlen(tmp) + 1 < sizeof((*items_io)[i].records)) {
                strcat((*items_io)[i].records, tmp);
            }
            return i;
        }
    }
    if (*count_io >= *cap_io) {
        int next_cap = *cap_io == 0 ? 16 : *cap_io * 2;
        UniqueHex *next = realloc(*items_io, (size_t)next_cap * sizeof(*next));
        if (!next) return -1;
        *items_io = next;
        *cap_io = next_cap;
    }
    snprintf((*items_io)[*count_io].key, sizeof((*items_io)[*count_io].key), "%s", key);
    (*items_io)[*count_io].count = 1;
    (*items_io)[*count_io].first_record = record_index;
    snprintf((*items_io)[*count_io].records, sizeof((*items_io)[*count_io].records), "%d", record_index);
    (*count_io)++;
    return *count_io - 1;
}


static int model_row_cmp_parent(const void *A, const void *B) {
    const ModelRow *a = (const ModelRow *)A;
    const ModelRow *b = (const ModelRow *)B;
    if (a->parent != b->parent) return a->parent - b->parent;
    return a->record - b->record;
}

static int cycle_exact_same(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) {
        if (!coord_same(a->v[i], b->v[i])) return 0;
    }
    return 1;
}

static int record_has_tile(const BComp1Record *r, const Cycle *tile_cycle) {
    if (!r->have_tiles) return 0;
    for (int i = 0; i < r->tiles_count; i++) {
        if (cycle_exact_same(&r->tiles[i], tile_cycle)) return 1;
    }
    return 0;
}

static int record_contains_parent_tiles(const BComp1Record *child,
                                        const BComp1Record *parent) {
    if (!child->have_tiles || !parent->have_tiles) return 0;
    for (int i = 0; i < parent->tiles_count; i++) {
        if (!record_has_tile(child, &parent->tiles[i])) return 0;
    }
    return 1;
}

static int find_parent_record(const BComp1RecordVec *parents,
                              const BComp1Record *child) {
    int best = 0;
    int best_tiles = -1;
    if (!parents || !parents->items) return 0;
    for (size_t i = 0; i < parents->count; i++) {
        const BComp1Record *p = &parents->items[i];
        if (!record_contains_parent_tiles(child, p)) continue;
        if (p->tiles_count > best_tiles) {
            best = (int)i + 1;
            best_tiles = p->tiles_count;
        }
    }
    return best;
}

static int model_row_add(ModelRow **rows_io,
                         int *count_io,
                         int *cap_io,
                         const ModelRow *row) {
    if (*count_io >= *cap_io) {
        int next_cap = *cap_io == 0 ? 64 : *cap_io * 2;
        ModelRow *next = realloc(*rows_io, (size_t)next_cap * sizeof(*next));
        if (!next) return 0;
        *rows_io = next;
        *cap_io = next_cap;
    }
    (*rows_io)[(*count_io)++] = *row;
    return 1;
}

static int parent_key_seen(const ModelRow *rows, int start, int end, const char *key) {
    for (int i = start; i < end; i++) {
        if (strcmp(rows[i].key, key) == 0) return 1;
    }
    return 0;
}

static void print_grouped_rows(FILE *fp, ModelRow *rows, int count, int parent_count) {
    if (count <= 0) return;
    qsort(rows, (size_t)count, sizeof(rows[0]), model_row_cmp_parent);

    int max_parent = parent_count;
    for (int i = 0; i < count; i++) {
        if (rows[i].parent > max_parent) max_parent = rows[i].parent;
    }
    if (max_parent <= 0) max_parent = 20;

    for (int parent = 1; parent <= max_parent; parent++) {
        fprintf(fp, "\n---[rl2:%d]---\n", parent);
        int parent_start = -1;
        int parent_end = -1;
        for (int i = 0; i < count; i++) {
            if (rows[i].parent != parent) continue;
            if (parent_start < 0) parent_start = i;
            parent_end = i + 1;
        }
        if (parent_start < 0) continue;
        for (int i = parent_start; i < parent_end; i++) {
            if (parent_key_seen(rows, parent_start, i, rows[i].key)) continue;
            fprintf(fp, "  %s\n", rows[i].key);
        }
    }

    int printed_unknown = 0;
    for (int i = 0; i < count; i++) {
        if (rows[i].parent > 0) continue;
        if (!printed_unknown) {
            fprintf(fp, "\n---[rl2:unknown]---\n");
            printed_unknown = 1;
        }
        if (parent_key_seen(rows, 0, i, rows[i].key)) continue;
        fprintf(fp, "  %s\n", rows[i].key);
    }
}

static void print_hex_model(FILE *fp,
                            int out_index,
                            size_t source_index,
                            const HiddenCluster *clusters,
                            int cluster_count) {
    fprintf(fp, "hex_model [%d] source=%zu clusters=%d", out_index, source_index, cluster_count);
    if (cluster_count != 6) fprintf(fp, " status=needs_merge");
    else fprintf(fp, " status=hex");
    fprintf(fp, "\n  vertices_ccw: [");
    for (int i = 0; i < cluster_count; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "[%s]", clusters[i].label);
    }
    fprintf(fp, "]\n");
    fprintf(fp, "  edge_cycle:");
    for (int i = 0; i < cluster_count; i++) {
        int j = (i + 1) % cluster_count;
        fprintf(fp, " e(%s,%s)", clusters[i].label, clusters[j].label);
    }
    fprintf(fp, "\n");
}



static int find_matching_central_edge(const Poly *central,
                                      Coord a,
                                      Coord b,
                                      int *out_i,
                                      int *out_j) {
    for (int c = 0; c < central->cycle_count; c++) {
        const Cycle *cy = &central->cycles[c];
        for (int i = 0; i < cy->n; i++) {
            int j = (i + 1) % cy->n;
            Coord u = cy->v[i];
            Coord v = cy->v[j];
            if ((coord_same(u, b) && coord_same(v, a)) ||
                (coord_same(u, a) && coord_same(v, b))) {
                *out_i = boundary_linear_index(central, c, i);
                *out_j = boundary_linear_index(central, c, j);
                return 1;
            }
        }
    }
    return 0;
}

static int edge_rule_pair_seen(const EdgeRulePair *rules, int count, const char *lhs, const char *rhs) {
    for (int i = 0; i < count; i++) {
        if (strcmp(rules[i].lhs, lhs) == 0 && strcmp(rules[i].rhs, rhs) == 0) return 1;
    }
    return 0;
}

static int add_edge_rule_pair(EdgeRulePair **rules_io,
                              int *count_io,
                              int *cap_io,
                              const char *lhs,
                              const char *rhs) {
    if (edge_rule_pair_seen(*rules_io, *count_io, lhs, rhs)) return 1;
    if (*count_io >= *cap_io) {
        int next_cap = *cap_io == 0 ? 64 : *cap_io * 2;
        EdgeRulePair *next = realloc(*rules_io, (size_t)next_cap * sizeof(*next));
        if (!next) return 0;
        *rules_io = next;
        *cap_io = next_cap;
    }
    snprintf((*rules_io)[*count_io].lhs, sizeof((*rules_io)[*count_io].lhs), "%s", lhs);
    snprintf((*rules_io)[*count_io].rhs, sizeof((*rules_io)[*count_io].rhs), "%s", rhs);
    (*count_io)++;
    return 1;
}

static int edge_rule_pair_cmp(const void *A, const void *B) {
    const EdgeRulePair *a = (const EdgeRulePair *)A;
    const EdgeRulePair *b = (const EdgeRulePair *)B;
    int k = strcmp(a->lhs, b->lhs);
    if (k) return k;
    return strcmp(a->rhs, b->rhs);
}

typedef struct {
    int cluster;
    int first;
    int last;
    int pos;
} OwnedSuperVertex;

typedef struct {
    int count;
    OwnedSuperVertex v[RL4_MAX_CLUSTERS];
    int edge_for_segment[MAX_CYCLES][MAX_VERTS];
} OwnedSuperCycle;

static int cluster_contains_coord(const HiddenCluster *cluster, Coord q) {
    for (int i = 0; i < cluster->member_count; i++) {
        if (coord_same(cluster->members[i], q)) return 1;
    }
    return 0;
}

static int cluster_of_coord(const HiddenCluster *clusters, int cluster_count, Coord q) {
    for (int i = 0; i < cluster_count; i++) {
        if (cluster_contains_coord(&clusters[i], q)) return i;
    }
    return -1;
}

static int append_owned_vertex(OwnedSuperCycle *owned,
                               int cluster,
                               int template_index,
                               int pos) {
    if (cluster < 0 || template_index < 0) return 1;
    if (owned->count > 0 && owned->v[owned->count - 1].cluster == cluster) {
        owned->v[owned->count - 1].last = template_index;
        return 1;
    }
    if (owned->count >= RL4_MAX_CLUSTERS) return 0;
    owned->v[owned->count].cluster = cluster;
    owned->v[owned->count].first = template_index;
    owned->v[owned->count].last = template_index;
    owned->v[owned->count].pos = pos;
    owned->count++;
    return 1;
}

static int build_owned_super_cycle(const Projection *projection,
                                   const HiddenCluster *clusters,
                                   int cluster_count,
                                   OwnedSuperCycle *owned) {
    memset(owned, 0, sizeof(*owned));
    for (int c = 0; c < MAX_CYCLES; c++) {
        for (int i = 0; i < MAX_VERTS; i++) owned->edge_for_segment[c][i] = -1;
    }
    if (projection->poly.cycle_count != 1) return 0;
    const Cycle *cy = &projection->poly.cycles[0];
    for (int i = 0; i < cy->n; i++) {
        int cluster = cluster_of_coord(clusters, cluster_count, cy->v[i]);
        if (cluster < 0) continue;
        int template_index = boundary_linear_index(&projection->poly, 0, i);
        if (!append_owned_vertex(owned, cluster, template_index, i)) return 0;
    }
    if (owned->count > 1 && owned->v[0].cluster == owned->v[owned->count - 1].cluster) {
        owned->v[0].first = owned->v[owned->count - 1].first;
        owned->count--;
    }
    if (owned->count < 2) return 0;

    for (int e = 0; e < owned->count; e++) {
        int start = owned->v[e].pos;
        int end = owned->v[(e + 1) % owned->count].pos;
        int i = start;
        int guard = 0;
        while (i != end && guard++ < cy->n) {
            owned->edge_for_segment[0][i] = e;
            i = (i + 1) % cy->n;
        }
    }
    return 1;
}

static int central_projection_has_trusted_vertices(const Projection *proj,
                                                   int proj_count,
                                                   const HiddenCluster *clusters,
                                                   int cluster_count) {
    if (proj_count <= 0 || cluster_count != 6) return 0;
    OwnedSuperCycle owned;
    if (!build_owned_super_cycle(&proj[0], clusters, cluster_count, &owned)) return 0;
    return owned.count == 6;
}

static int find_owned_segment_edge(const OwnedSuperCycle *owned,
                                   const Projection *projection,
                                   Coord a,
                                   Coord b) {
    for (int c = 0; c < projection->poly.cycle_count; c++) {
        const Cycle *cy = &projection->poly.cycles[c];
        for (int i = 0; i < cy->n; i++) {
            Coord u = cy->v[i];
            Coord v = cy->v[(i + 1) % cy->n];
            if ((coord_same(u, a) && coord_same(v, b)) ||
                (coord_same(u, b) && coord_same(v, a))) {
                if (c < MAX_CYCLES && i < MAX_VERTS) return owned->edge_for_segment[c][i];
                return -1;
            }
        }
    }
    return -1;
}

static void make_owned_edge_token(const OwnedSuperCycle *owned,
                                  int sign,
                                  int edge_index,
                                  char *out,
                                  size_t cap) {
    if (edge_index < 0 || edge_index >= owned->count) {
        snprintf(out, cap, "%ce(v-1,v-1)", sign >= 0 ? '+' : '-');
        return;
    }
    const OwnedSuperVertex *a = &owned->v[edge_index];
    const OwnedSuperVertex *b = &owned->v[(edge_index + 1) % owned->count];
    snprintf(out, cap, "%ce(v%d,v%d)", sign >= 0 ? '+' : '-', a->first, b->last);
}

static void collect_projection_edge_rules(const Projection *proj,
                                          int proj_count,
                                          const HiddenCluster *clusters,
                                          int cluster_count,
                                          EdgeRulePair **rules_io,
                                          int *rule_count,
                                          int *rule_cap,
                                          FILE *detail_fp) {
    if (proj_count <= 1 || cluster_count <= 0) return;

    OwnedSuperCycle central_owned;
    if (!build_owned_super_cycle(&proj[0], clusters, cluster_count, &central_owned)) return;
    const Poly *central = &proj[0].poly;
    if (detail_fp) fprintf(detail_fp, "  edge_observations:");

    for (int p = 1; p < proj_count; p++) {
        OwnedSuperCycle adjacent_owned;
        if (!build_owned_super_cycle(&proj[p], clusters, cluster_count, &adjacent_owned)) continue;
        const Poly *adjacent = &proj[p].poly;
        int seen_c[ATTACH0_MAX_TILES];
        int seen_a[ATTACH0_MAX_TILES];
        int seen_count = 0;

        for (int c = 0; c < adjacent->cycle_count; c++) {
            const Cycle *cy = &adjacent->cycles[c];
            for (int i = 0; i < cy->n; i++) {
                int j = (i + 1) % cy->n;
                Coord a = cy->v[i];
                Coord b = cy->v[j];
                int ci = -1;
                int cj = -1;
                if (!find_matching_central_edge(central, a, b, &ci, &cj)) continue;

                int central_edge = find_owned_segment_edge(&central_owned, &proj[0], a, b);
                int adjacent_edge = find_owned_segment_edge(&adjacent_owned, &proj[p], a, b);
                if (central_edge < 0 || adjacent_edge < 0) continue;
                int already_seen = 0;
                for (int sp = 0; sp < seen_count; sp++) {
                    if (seen_c[sp] == central_edge && seen_a[sp] == adjacent_edge) {
                        already_seen = 1;
                        break;
                    }
                }
                if (already_seen) continue;
                if (seen_count < ATTACH0_MAX_TILES) {
                    seen_c[seen_count] = central_edge;
                    seen_a[seen_count] = adjacent_edge;
                    seen_count++;
                }

                char lhs[256];
                char rhs[256];
                make_owned_edge_token(&central_owned, +1, central_edge, lhs, sizeof(lhs));
                make_owned_edge_token(&adjacent_owned, -1, adjacent_edge, rhs, sizeof(rhs));
                add_edge_rule_pair(rules_io, rule_count, rule_cap, lhs, rhs);
                if (detail_fp) {
                    fprintf(detail_fp, " t%d:c%d/a%d:%s=%s", proj[p].tile_index,
                            central_edge, adjacent_edge, lhs, rhs);
                }
            }
        }
    }
    if (detail_fp) fputc('\n', detail_fp);
}

static void print_edge_rules(FILE *fp, EdgeRulePair *rules, int rule_count) {
    qsort(rules, (size_t)rule_count, sizeof(rules[0]), edge_rule_pair_cmp);
    for (int i = 0; i < rule_count; i++) {
        fprintf(fp, "  %s = %s\n", rules[i].lhs, rules[i].rhs);
    }
}


static int emit_record(const Tile *tile,
                       int out_index,
                       const BComp1Record *record,
                       const Projection *proj,
                       int proj_count,
                       const Coord *hidden,
                       int hidden_count) {
    if (proj_count <= 0) return 0;
    printf("[%d]\n", out_index);
    printf("Aggregate\n");
    print_poly_shape(tile, &proj[0].poly);
    printf("TilesDark\n");
    for (int i = 0; i < proj_count; i++) {
        print_cycle_shape(tile, &record->tiles[proj[i].tile_index]);
    }
    printf("Highlights\n");
    for (int i = 0; i < proj_count; i++) {
        print_poly_shape(tile, &proj[i].poly);
    }
    if (hidden_count > 0) {
        printf("Hidden\n");
        for (int i = 0; i < hidden_count; i++) {
            printf("(%d,%d,%d)\n", hidden[i].v, hidden[i].x, hidden[i].y);
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    Options opt;
    BComp1Context ctx;
    BComp1RecordVec records = {0};
    BComp1RecordVec super_records = {0};
    BComp1RecordVec parents = {0};
    int emitted = 0;
    int projected_total = 0;
    int hidden_total = 0;
    int six_cluster_records = 0;
    int trusted_vertex_records = 0;
    UniqueHex *unique_hexes = NULL;
    int unique_count = 0;
    int unique_cap = 0;
    EdgeRulePair *edge_rules = NULL;
    int edge_rule_count = 0;
    int edge_rule_cap = 0;
    ModelRow *rows = NULL;
    int row_count = 0;
    int row_cap = 0;

    if (!parse_args(argc, argv, &opt)) { usage(argv[0]); return 1; }
    if (!bcomp1_context_init(&ctx, opt.tile_path,
                             opt.remembrance_path,
                             opt.deletions_path)) {
        fprintf(stderr, "failed to initialize bcomp1 context\n");
        return 1;
    }
    if (!bcomp1_load_records(opt.data_path, &records)) {
        fprintf(stderr, "failed to load records: %s\n", opt.data_path);
        bcomp1_context_clear(&ctx);
        return 1;
    }
    if (!bcomp1_load_records(opt.supertile_path, &super_records) ||
        super_records.count < 1 || !super_records.items[0].have_center ||
        !super_records.items[0].have_boundary) {
        fprintf(stderr, "failed to load supertile: %s\n", opt.supertile_path);
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 1;
    }

    if (opt.data_mode) {
        if (!bcomp1_load_records(opt.parents_path, &parents)) {
            fprintf(stderr, "warning: failed to load RL2 parent records: %s\n", opt.parents_path);
        }
    }

    Projection *proj = calloc(ATTACH0_MAX_TILES, sizeof(*proj));
    Coord *hidden = calloc(RL4_MAX_HIDDEN, sizeof(*hidden));
    HiddenCluster *clusters = calloc(RL4_MAX_CLUSTERS, sizeof(*clusters));
    if (!proj || !hidden || !clusters) {
        fprintf(stderr, "rl4_refine: allocation failure\n");
        free(proj);
        free(hidden);
        free(clusters);
        bcomp1_free_records(&records);
        bcomp1_free_records(&super_records);
        bcomp1_context_clear(&ctx);
        return 1;
    }

    const BComp1Record *supertile = &super_records.items[0];
    if (opt.data_mode) {
        printf("# RL5 supertile hexagon extraction\n");
        printf("# input: %s\n", opt.data_path);
        printf("# supertile: %s\n", opt.supertile_path);
        printf("# rl2_parents: %s\n\n", opt.parents_path);
        printf("# Supertile hexagons use cyclic CCW lists of clustered triple-intersection vertices.\n");
        printf("# Edge tokens use down(e(A,B)) = e(first(A),last(B)) on cyclic cluster edges.\n");
        printf("# Each row records the RL3 item, inferred RL2 parent, and unique reduction.\n\n");
    }
    for (size_t ri = 0; ri < records.count; ri++) {
        const BComp1Record *r = &records.items[ri];
        int proj_count = 0;
        int hidden_count = 0;

        if (!r->have_tiles) continue;
        for (int t = 0; t < r->tiles_count && proj_count < ATTACH0_MAX_TILES; t++) {
            Affine2 a;
            Poly placed;
            if (tile_parity(&ctx.tile, &r->tiles[t]) >= 0) continue;
            if (!fit_cycle_affine(&ctx.tile, &supertile->center,
                                  &r->tiles[t], &a)) continue;
            if (!transform_poly(&ctx.tile, &supertile->boundary,
                                &a, &placed)) continue;
            proj[proj_count].poly = placed;
            proj[proj_count].tile_index = t;
            proj_count++;
        }
        if (proj_count == 0) continue;

        hidden_count = collect_hidden_vertices(proj, proj_count,
                                               hidden, RL4_MAX_HIDDEN);
        int cluster_count = build_hidden_clusters(&ctx.tile, proj, proj_count,
                                                  hidden, hidden_count,
                                                  clusters, RL4_MAX_CLUSTERS);
        int vertices_trusted = central_projection_has_trusted_vertices(proj, proj_count, clusters, cluster_count);
        if (cluster_count == 6) six_cluster_records++;
        if (vertices_trusted) trusted_vertex_records++;
        emitted++;
        projected_total += proj_count;
        hidden_total += hidden_count;
        char hex_key[2048];
        build_hex_key(clusters, cluster_count, hex_key, sizeof(hex_key));
        int unique_id = unique_hex_add(&unique_hexes, &unique_count, &unique_cap, hex_key, emitted) + 1;
        int parent_id = opt.data_mode ? find_parent_record(&parents, r) : 0;
        if (opt.verbose && !opt.data_mode) {
            fprintf(stderr, "[%d] source=%zu parent=%d unique=%d reflected=%d hidden=%d clusters=%d\n",
                    emitted, ri + 1, parent_id, unique_id, proj_count, hidden_count, cluster_count);
        }
        if (opt.data_mode) {
            ModelRow row;
            memset(&row, 0, sizeof(row));
            row.record = emitted;
            row.source = (int)ri + 1;
            row.parent = parent_id;
            row.unique_id = unique_id;
            row.projected = proj_count;
            row.hidden = hidden_count;
            row.clusters = cluster_count;
            snprintf(row.key, sizeof(row.key), "%s", hex_key);
            model_row_add(&rows, &row_count, &row_cap, &row);
            collect_projection_edge_rules(proj, proj_count, clusters, cluster_count,
                                          &edge_rules, &edge_rule_count,
                                          &edge_rule_cap, NULL);
        } else {
            emit_record(&ctx.tile, emitted, r, proj, proj_count,
                        hidden, hidden_count);
            if (opt.verbose) {
                print_hex_model(stderr, emitted, ri + 1, clusters, cluster_count);
            }
            collect_projection_edge_rules(proj, proj_count, clusters, cluster_count,
                                          &edge_rules, &edge_rule_count,
                                          &edge_rule_cap, opt.verbose ? stderr : NULL);
        }
        if (opt.limit > 0 && emitted >= opt.limit) break;
    }

    if (opt.data_mode) {
        printf("\nSupertile Hexagons grouped by RL2 parent\n");
        print_grouped_rows(stdout, rows, row_count, (int)parents.count);
        printf("\nUnique Supertile Hexagons\n");
        for (int i = 0; i < unique_count; i++) {
            printf("%2d  %-78s  # records %s\n",
                   i + 1,
                   unique_hexes[i].key,
                   unique_hexes[i].records);
        }
        printf("\nVertex QA\n");
        printf("  records=%d\n", emitted);
        printf("  six_cluster_records=%d\n", six_cluster_records);
        printf("  trusted_central_vertex_records=%d\n", trusted_vertex_records);
        printf("  status=%s\n", emitted > 0 && trusted_vertex_records == emitted ? "trusted_central_cycles" : "needs_review");

        printf("\nDownmapped Super Hexagon Edge Rules\n");
        print_edge_rules(stdout, edge_rules, edge_rule_count);
        printf("\nSummary emitted=%d projected=%d hidden=%d unique=%d edge_rules=%d\n",
               emitted, projected_total, hidden_total, unique_count, edge_rule_count);
    }

    if (opt.verbose) {
        fprintf(stderr, "rl4_refine: emitted=%d projected=%d hidden=%d unique=%d\n",
                emitted, projected_total, hidden_total, unique_count);
    }

    free(unique_hexes);
    free(edge_rules);
    free(rows);
    free(proj);
    free(hidden);
    free(clusters);
    bcomp1_free_records(&records);
    bcomp1_free_records(&super_records);
    bcomp1_free_records(&parents);
    bcomp1_context_clear(&ctx);
    return 0;
}
