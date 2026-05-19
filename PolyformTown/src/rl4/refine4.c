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
    char lhs[512];
    char rhs[512];
    char hash[128];
    int record;
} EdgeRulePair;


typedef struct {
    int n;
    DPoint p[MAX_VERTS + 1];
} EdgePath;

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
            "  --supertile PATH      default preferences/focus.supertile\n"
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
        if (strcmp(argv[i], "--supertile") == 0 && i + 1 < argc) {
            opt->supertile_path = argv[++i];
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



static int parse_edge_token0(const char *tok, int *sign, char *x, size_t xcap, char *y, size_t ycap) {
    const char *p = tok;
    const char *comma;
    const char *close;
    size_t xn, yn;
    if (!p || (*p != '+' && *p != '-')) return 0;
    *sign = (*p == '+') ? 1 : -1;
    p++;
    if (p[0] != 'e' || p[1] != '(') return 0;
    p += 2;
    comma = strchr(p, ',');
    if (!comma) return 0;
    close = strchr(comma + 1, ')');
    if (!close || close[1] != '\0') return 0;
    xn = (size_t)(comma - p);
    yn = (size_t)(close - (comma + 1));
    if (xn + 1 > xcap || yn + 1 > ycap) return 0;
    memcpy(x, p, xn);
    x[xn] = '\0';
    memcpy(y, comma + 1, yn);
    y[yn] = '\0';
    return 1;
}

static void edge_token_with_sign0(const char *tok, int flip, char *out, size_t cap) {
    int sign = 1;
    char x[64];
    char y[64];
    if (!parse_edge_token0(tok, &sign, x, sizeof(x), y, sizeof(y))) {
        const char *src = tok ? tok : ".";
        if (cap > 0) {
            size_t n = 0;
            while (n + 1 < cap && src[n]) n++;
            memcpy(out, src, n);
            out[n] = '\0';
        }
        return;
    }
    if (flip) sign = -sign;
    snprintf(out, cap, "%ce(%s,%s)", sign >= 0 ? '+' : '-', x, y);
}

static void join_rule_buf0(char *out, size_t cap, const char *lhs, const char *rhs) {
    if (cap == 0) return;
    out[0] = '\0';
    strncat(out, lhs, cap - 1);
    size_t n = strlen(out);
    if (n < cap - 1) strncat(out, " = ", cap - 1 - n);
    n = strlen(out);
    if (n < cap - 1) strncat(out, rhs, cap - 1 - n);
}

static void edge_rule_pair_variant_to_buf0(const EdgeRulePair *r, int flip, int swap, char *out, size_t cap) {
    char lhs[256];
    char rhs[256];
    edge_token_with_sign0(r->lhs, flip, lhs, sizeof(lhs));
    edge_token_with_sign0(r->rhs, flip, rhs, sizeof(rhs));
    if (swap) join_rule_buf0(out, cap, rhs, lhs);
    else join_rule_buf0(out, cap, lhs, rhs);
}

static void canonical_edge_rule_pair_to_buf(const EdgeRulePair *r, char *out, size_t cap) {
    char best[1024];
    char cur[1024];
    int have = 0;
    for (int flip = 0; flip < 2; flip++) {
        for (int swap = 0; swap < 2; swap++) {
            edge_rule_pair_variant_to_buf0(r, flip, swap, cur, sizeof(cur));
            if (!have || strcmp(cur, best) < 0) {
                snprintf(best, sizeof(best), "%s", cur);
                have = 1;
            }
        }
    }
    snprintf(out, cap, "%s", have ? best : "");
}


static int add_edge_rule_pair(EdgeRulePair **rules_io,
                              int *count_io,
                              int *cap_io,
                              const char *lhs,
                              const char *rhs,
                              const char *hash,
                              int record) {
    if (*count_io >= *cap_io) {
        int next_cap = *cap_io ? *cap_io * 2 : 128;
        EdgeRulePair *next = realloc(*rules_io, (size_t)next_cap * sizeof(*next));
        if (!next) return 0;
        *rules_io = next;
        *cap_io = next_cap;
    }
    snprintf((*rules_io)[*count_io].lhs, sizeof((*rules_io)[*count_io].lhs), "%s", lhs);
    snprintf((*rules_io)[*count_io].rhs, sizeof((*rules_io)[*count_io].rhs), "%s", rhs);
    snprintf((*rules_io)[*count_io].hash, sizeof((*rules_io)[*count_io].hash), "%s", hash ? hash : ".");
    (*rules_io)[*count_io].record = record;
    (*count_io)++;
    return 1;
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


static int build_owned_edge_path_with_tile(const Tile *tile,
                                           const Projection *projection,
                                           const OwnedSuperCycle *owned,
                                           int edge_index,
                                           EdgePath *path) {
    if (!projection || !owned || !path) return 0;
    if (projection->poly.cycle_count != 1) return 0;
    if (edge_index < 0 || edge_index >= owned->count) return 0;
    const Cycle *cy = &projection->poly.cycles[0];
    int start = owned->v[edge_index].pos;
    int end = owned->v[(edge_index + 1) % owned->count].pos;
    int i = start;
    int guard = 0;
    path->n = 0;
    while (guard++ <= cy->n) {
        if (path->n >= MAX_VERTS + 1) return 0;
        path->p[path->n++] = coord_xy(tile, cy->v[i]);
        if (i == end) return path->n >= 2;
        i = (i + 1) % cy->n;
    }
    return 0;
}

static void collect_projection_edge_rules(const Tile *tile,
                                          const Projection *proj,
                                          int proj_count,
                                          const HiddenCluster *clusters,
                                          int cluster_count,
                                          int source_record,
                                          int parent_id,
                                          int unique_id,
                                          EdgeRulePair **rules_io,
                                          int *rule_count,
                                          int *rule_cap,
                                          FILE *detail_fp) {
    if (proj_count <= 1 || cluster_count <= 0) return;

    OwnedSuperCycle central_owned;
    if (!build_owned_super_cycle(&proj[0], clusters, cluster_count, &central_owned)) return;
    if (detail_fp) fprintf(detail_fp, "  geometric_edge_observations:");

    if (central_owned.count <= 0 || central_owned.count > RL4_MAX_CLUSTERS) return;

    EdgePath *central_paths = calloc((size_t)central_owned.count, sizeof(*central_paths));
    int *have_central = calloc((size_t)central_owned.count, sizeof(*have_central));
    if (!central_paths || !have_central) {
        free(central_paths);
        free(have_central);
        return;
    }

    for (int cedge = 0; cedge < central_owned.count; cedge++) {
        have_central[cedge] = build_owned_edge_path_with_tile(tile, &proj[0], &central_owned, cedge,
                                                              &central_paths[cedge]);
    }

    for (int p = 1; p < proj_count; p++) {
        OwnedSuperCycle adjacent_owned;
        if (!build_owned_super_cycle(&proj[p], clusters, cluster_count, &adjacent_owned)) continue;
        if (adjacent_owned.count <= 0 || adjacent_owned.count > RL4_MAX_CLUSTERS) continue;

        EdgePath *adjacent_paths = calloc((size_t)adjacent_owned.count, sizeof(*adjacent_paths));
        int *have_adjacent = calloc((size_t)adjacent_owned.count, sizeof(*have_adjacent));
        if (!adjacent_paths || !have_adjacent) {
            free(adjacent_paths);
            free(have_adjacent);
            continue;
        }

        for (int aedge = 0; aedge < adjacent_owned.count; aedge++) {
            have_adjacent[aedge] = build_owned_edge_path_with_tile(tile, &proj[p], &adjacent_owned, aedge,
                                                                   &adjacent_paths[aedge]);
        }

        for (int cedge = 0; cedge < central_owned.count; cedge++) {
            if (!have_central[cedge]) continue;
            for (int aedge = 0; aedge < adjacent_owned.count; aedge++) {
                if (!have_adjacent[aedge]) continue;
                const OwnedSuperVertex *ca = &central_owned.v[cedge];
                const OwnedSuperVertex *cb = &central_owned.v[(cedge + 1) % central_owned.count];
                const OwnedSuperVertex *aa = &adjacent_owned.v[aedge];
                const OwnedSuperVertex *ab = &adjacent_owned.v[(aedge + 1) % adjacent_owned.count];
                if (!(ca->cluster == ab->cluster && cb->cluster == aa->cluster)) continue;

                char lhs[256];
                char rhs[256];
                make_owned_edge_token(&central_owned, +1, cedge, lhs, sizeof(lhs));
                make_owned_edge_token(&adjacent_owned, -1, aedge, rhs, sizeof(rhs));
                char hash[128];
                snprintf(hash, sizeof(hash), "rl5:%d:p%d:u%d:e%d:t%d:a%d",
                         source_record, parent_id, unique_id, cedge, proj[p].tile_index, aedge);
                add_edge_rule_pair(rules_io, rule_count, rule_cap, lhs, rhs, hash, source_record);
                if (detail_fp) {
                    fprintf(detail_fp, " %s:%s=%s", hash, lhs, rhs);
                }
            }
        }

        free(adjacent_paths);
        free(have_adjacent);
    }
    free(central_paths);
    free(have_central);
    if (detail_fp) fputc('\n', detail_fp);
}

static int int_cmp0(const void *A, const void *B) {
    int a = *(const int *)A;
    int b = *(const int *)B;
    return (a > b) - (a < b);
}

static int record_seen0(const int *records, int count, int record) {
    for (int i = 0; i < count; i++) if (records[i] == record) return 1;
    return 0;
}

typedef struct {
    char row[1024];
    int *records;
    int record_count;
    int obs;
} CanonEdgeRulePairRow;

static int canon_edge_rule_pair_row_cmp(const void *A, const void *B) {
    const CanonEdgeRulePairRow *a = (const CanonEdgeRulePairRow *)A;
    const CanonEdgeRulePairRow *b = (const CanonEdgeRulePairRow *)B;
    return strcmp(a->row, b->row);
}

static void print_edge_matches(FILE *fp, EdgeRulePair *rules, int rule_count, int surround_count) {
    CanonEdgeRulePairRow *rows = calloc((size_t)(rule_count > 0 ? rule_count : 1), sizeof(*rows));
    int row_count = 0;
    int width = 0;
    if (!rows) return;
    for (int i = 0; i < rule_count; i++) {
        char row[1024];
        int found = -1;
        canonical_edge_rule_pair_to_buf(&rules[i], row, sizeof(row));
        for (int j = 0; j < row_count; j++) {
            if (strcmp(rows[j].row, row) == 0) { found = j; break; }
        }
        if (found < 0) {
            found = row_count++;
            snprintf(rows[found].row, sizeof(rows[found].row), "%s", row);
            rows[found].records = calloc((size_t)(rule_count > 0 ? rule_count : 1), sizeof(*rows[found].records));
            rows[found].record_count = 0;
            rows[found].obs = 0;
            if (!rows[found].records) {
                for (int k = 0; k < row_count; k++) free(rows[k].records);
                            return;
            }
        }
        rows[found].obs++;
        if (!record_seen0(rows[found].records, rows[found].record_count, rules[i].record))
            rows[found].records[rows[found].record_count++] = rules[i].record;
    }
    qsort(rows, (size_t)row_count, sizeof(rows[0]), canon_edge_rule_pair_row_cmp);
    for (int i = 0; i < row_count; i++) {
        int len = (int)strlen(rows[i].row);
        if (len > width) width = len;
        qsort(rows[i].records, (size_t)rows[i].record_count, sizeof(rows[i].records[0]), int_cmp0);
    }
    for (int i = 0; i < row_count; i++) {
        fprintf(fp, "  %-*s  # records", width, rows[i].row);
        for (int k = 0; k < rows[i].record_count; k++) fprintf(fp, " %d", rows[i].records[k]);
        if (rows[i].obs != rows[i].record_count) fprintf(fp, " ; observations %d", rows[i].obs);
        fputc('\n', fp);
    }
    fprintf(fp, "  # observations=%d expected=%d unique_rows=%d status=%s\n",
            rule_count, 6 * surround_count, row_count,
            rule_count == 6 * surround_count ? "ok" : "count_mismatch");
    for (int i = 0; i < row_count; i++) free(rows[i].records);
}




typedef struct {
    char text[1024];
    int obs;
} CanonVertexFigureRow;

static void bare_edge_token0(const char *tok, char *out, size_t cap) {
    if (!tok) {
        snprintf(out, cap, ".");
        return;
    }
    if (tok[0] == '+' || tok[0] == '-') snprintf(out, cap, "%s", tok + 1);
    else snprintf(out, cap, "%s", tok);
}

static void positive_edge_token0(const char *tok, char *out, size_t cap) {
    char bare[128];
    bare_edge_token0(tok, bare, sizeof(bare));
    if (cap > 0) {
        out[0] = '+';
        if (cap > 1) {
            size_t n = 0;
            while (n + 2 < cap && bare[n]) n++;
            memcpy(out + 1, bare, n);
            out[1 + n] = '\0';
        }
    }
}

static int edge_token_match0(const char *a, const char *b, const EdgeRulePair *rules, int rule_count) {
    char pa[128];
    char pb[128];
    positive_edge_token0(a, pa, sizeof(pa));
    positive_edge_token0(b, pb, sizeof(pb));
    for (int i = 0; i < rule_count; i++) {
        char lhs[128];
        char rhs[128];
        positive_edge_token0(rules[i].lhs, lhs, sizeof(lhs));
        positive_edge_token0(rules[i].rhs, rhs, sizeof(rhs));
        if ((strcmp(pa, lhs) == 0 && strcmp(pb, rhs) == 0) ||
            (strcmp(pa, rhs) == 0 && strcmp(pb, lhs) == 0)) {
            return 1;
        }
    }
    return 0;
}

static void vertex_figure_text0(const char e[3][2][128], int shift, char *out, size_t cap) {
    char a[3][128];
    char b[3][128];
    size_t off = 0;
    out[0] = '\0';
    for (int r = 0; r < 3; r++) {
        int k = (shift + r) % 3;
        bare_edge_token0(e[k][0], a[r], sizeof(a[r]));
        bare_edge_token0(e[k][1], b[r], sizeof(b[r]));
    }
    for (int r = 0; r < 3; r++) {
        int n = snprintf(out + off, cap > off ? cap - off : 0,
                         "[%s, %s]%s", a[r], b[r], r == 2 ? "" : "\n");
        if (n < 0) return;
        off += (size_t)n;
        if (off >= cap) { out[cap ? cap - 1 : 0] = '\0'; return; }
    }
}

static void canonical_vertex_figure_text0(const char e[3][2][128], char *out, size_t cap) {
    char best[1024];
    char cur[1024];
    int have = 0;
    for (int shift = 0; shift < 3; shift++) {
        vertex_figure_text0(e, shift, cur, sizeof(cur));
        if (!have || strcmp(cur, best) < 0) {
            snprintf(best, sizeof(best), "%s", cur);
            have = 1;
        }
    }
    snprintf(out, cap, "%s", have ? best : "");
}

static int vertex_row_cmp0(const void *A, const void *B) {
    const CanonVertexFigureRow *a = (const CanonVertexFigureRow *)A;
    const CanonVertexFigureRow *b = (const CanonVertexFigureRow *)B;
    return strcmp(a->text, b->text);
}

static int add_canon_vertex_row(CanonVertexFigureRow **rows_io,
                                int *count_io,
                                int *cap_io,
                                const char *text) {
    for (int i = 0; i < *count_io; i++) {
        if (strcmp((*rows_io)[i].text, text) == 0) {
            (*rows_io)[i].obs++;
            return 1;
        }
    }
    if (*count_io >= *cap_io) {
        int next_cap = *cap_io ? *cap_io * 2 : 64;
        CanonVertexFigureRow *next = realloc(*rows_io, (size_t)next_cap * sizeof(*next));
        if (!next) return 0;
        *rows_io = next;
        *cap_io = next_cap;
    }
    snprintf((*rows_io)[*count_io].text, sizeof((*rows_io)[*count_io].text), "%s", text);
    (*rows_io)[*count_io].obs = 1;
    (*count_io)++;
    return 1;
}

static int canonical_row_seen0(char rows[][1024], int count, const char *row) {
    for (int i = 0; i < count; i++) if (strcmp(rows[i], row) == 0) return 1;
    return 0;
}

static int collect_unique_edge_rows0(const EdgeRulePair *rules, int rule_count, char rows[][1024], int max_rows) {
    int n = 0;
    for (int i = 0; i < rule_count; i++) {
        char row[1024];
        canonical_edge_rule_pair_to_buf(&rules[i], row, sizeof(row));
        if (canonical_row_seen0(rows, n, row)) continue;
        if (n >= max_rows) break;
        snprintf(rows[n++], 1024, "%s", row);
    }
    qsort(rows, (size_t)n, 1024, (int (*)(const void *, const void *))strcmp);
    return n;
}

static int collect_induced_edge_rows0(const CanonVertexFigureRow *vrows,
                                      int vrow_count,
                                      char rows[][1024],
                                      int max_rows) {
    int n = 0;
    for (int i = 0; i < vrow_count; i++) {
        char edges[6][128];
        int ec = 0;
        const char *p = vrows[i].text;
        while (*p && ec < 6) {
            const char *e = strstr(p, "e(");
            if (!e) break;
            const char *close = strchr(e, ')');
            if (!close) break;
            size_t len = (size_t)(close - e + 1);
            if (len >= sizeof(edges[ec])) len = sizeof(edges[ec]) - 1;
            memcpy(edges[ec], e, len);
            edges[ec][len] = '\0';
            ec++;
            p = close + 1;
        }
        if (ec != 6) continue;
        const int pairs[3][2] = {{0,3},{2,5},{4,1}};
        for (int k = 0; k < 3; k++) {
            EdgeRulePair r;
            snprintf(r.lhs, sizeof(r.lhs), "+%s", edges[pairs[k][0]]);
            snprintf(r.rhs, sizeof(r.rhs), "-%s", edges[pairs[k][1]]);
            r.hash[0] = '\0';
            r.record = 0;
            char row[1024];
            canonical_edge_rule_pair_to_buf(&r, row, sizeof(row));
            if (canonical_row_seen0(rows, n, row)) continue;
            if (n >= max_rows) break;
            snprintf(rows[n++], 1024, "%s", row);
        }
    }
    qsort(rows, (size_t)n, 1024, (int (*)(const void *, const void *))strcmp);
    return n;
}

static int row_set_missing_count0(char a[][1024], int an, char b[][1024], int bn) {
    int missing = 0;
    for (int i = 0; i < an; i++) if (!canonical_row_seen0(b, bn, a[i])) missing++;
    return missing;
}

static void print_row_set_missing0(FILE *fp, const char *title, char a[][1024], int an, char b[][1024], int bn) {
    int printed = 0;
    for (int i = 0; i < an; i++) {
        if (canonical_row_seen0(b, bn, a[i])) continue;
        if (!printed) {
            fprintf(fp, "# %s:\n", title);
            printed = 1;
        }
        fprintf(fp, "#   %s\n", a[i][0] == '+' ? a[i] + 1 : a[i]);
    }
}


typedef struct {
    char a[128];
    char b[128];
    double angle;
} VertexIncidentRow;


static void copy_vertex_token0(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    if (!src) src = "";
    size_t n = 0;
    while (n + 1 < cap && src[n]) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
}

static int vertex_incident_row_cmp_angle(const void *A, const void *B) {
    const VertexIncidentRow *a = (const VertexIncidentRow *)A;
    const VertexIncidentRow *b = (const VertexIncidentRow *)B;
    return (a->angle > b->angle) - (a->angle < b->angle);
}

static int projection_center_xy(const Tile *tile, const Projection *projection, DPoint *out) {
    if (!tile || !projection || !out || projection->poly.cycle_count < 1) return 0;
    const Cycle *cy = &projection->poly.cycles[0];
    if (cy->n <= 0) return 0;
    out->x = 0.0;
    out->y = 0.0;
    for (int i = 0; i < cy->n; i++) {
        DPoint p = coord_xy(tile, cy->v[i]);
        out->x += p.x;
        out->y += p.y;
    }
    out->x /= (double)cy->n;
    out->y /= (double)cy->n;
    return 1;
}

static void collect_projection_vertex_figures(const Tile *tile,
                                              const Projection *proj,
                                              int proj_count,
                                              const HiddenCluster *clusters,
                                              int cluster_count,
                                              const EdgeRulePair *rules,
                                              int rule_count,
                                              CanonVertexFigureRow **rows_io,
                                              int *count_io,
                                              int *cap_io) {
    if (!tile || !proj || proj_count <= 0 || !clusters || cluster_count <= 0) return;

    OwnedSuperCycle central_owned;
    if (!build_owned_super_cycle(&proj[0], clusters, cluster_count, &central_owned)) return;
    if (central_owned.count != 6) return;

    for (int ci = 0; ci < central_owned.count; ci++) {
        int cluster = central_owned.v[ci].cluster;
        VertexIncidentRow incident[8];
        int incident_count = 0;

        for (int p = 0; p < proj_count; p++) {
            OwnedSuperCycle owned;
            DPoint center;
            if (!build_owned_super_cycle(&proj[p], clusters, cluster_count, &owned)) continue;
            if (owned.count < 2) continue;
            if (!projection_center_xy(tile, &proj[p], &center)) continue;

            for (int vi = 0; vi < owned.count; vi++) {
                if (owned.v[vi].cluster != cluster) continue;
                if (incident_count >= (int)(sizeof(incident) / sizeof(incident[0]))) break;
                int prev_edge = (vi + owned.count - 1) % owned.count;
                int next_edge = vi;
                make_owned_edge_token(&owned, +1, prev_edge,
                                      incident[incident_count].a,
                                      sizeof(incident[incident_count].a));
                make_owned_edge_token(&owned, +1, next_edge,
                                      incident[incident_count].b,
                                      sizeof(incident[incident_count].b));
                incident[incident_count].angle = atan2(center.y - clusters[cluster].y,
                                                       center.x - clusters[cluster].x);
                incident_count++;
                break;
            }
        }

        if (incident_count != 3) continue;
        qsort(incident, (size_t)incident_count, sizeof(incident[0]), vertex_incident_row_cmp_angle);

        int best_perm[3] = {0, 1, 2};
        int found_perm = 0;
        int perms[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
        for (int pi = 0; pi < 6 && !found_perm; pi++) {
            VertexIncidentRow *r0 = &incident[perms[pi][0]];
            VertexIncidentRow *r1 = &incident[perms[pi][1]];
            VertexIncidentRow *r2 = &incident[perms[pi][2]];
            if (edge_token_match0(r0->a, r1->b, rules, rule_count) &&
                edge_token_match0(r1->a, r2->b, rules, rule_count) &&
                edge_token_match0(r2->a, r0->b, rules, rule_count)) {
                best_perm[0] = perms[pi][0];
                best_perm[1] = perms[pi][1];
                best_perm[2] = perms[pi][2];
                found_perm = 1;
            }
        }

        char e[3][2][128];
        for (int r = 0; r < 3; r++) {
            copy_vertex_token0(e[r][0], sizeof(e[r][0]), incident[best_perm[r]].a);
            copy_vertex_token0(e[r][1], sizeof(e[r][1]), incident[best_perm[r]].b);
        }
        char text[1024];
        canonical_vertex_figure_text0(e, text, sizeof(text));
        add_canon_vertex_row(rows_io, count_io, cap_io, text);
    }
}

static int print_vertex_figures(FILE *fp,
                                CanonVertexFigureRow *vrows,
                                int vrow_count,
                                const EdgeRulePair *edge_rules,
                                int edge_rule_count,
                                int strict) {
    char edge_set[512][1024];
    char induced_set[512][1024];
    int edge_n = collect_unique_edge_rows0(edge_rules, edge_rule_count, edge_set, 512);
    int induced_n = collect_induced_edge_rows0(vrows, vrow_count, induced_set, 512);
    int missing = row_set_missing_count0(edge_set, edge_n, induced_set, induced_n);
    int extra = row_set_missing_count0(induced_set, induced_n, edge_set, edge_n);

    qsort(vrows, (size_t)vrow_count, sizeof(vrows[0]), vertex_row_cmp0);
    fprintf(fp, "\n#\n");
    fprintf(fp, "# Vertex Figures\n");
    fprintf(fp, "#\n");
    if (missing || extra) {
        fprintf(fp, "# status=%s edge_rows=%d induced_edge_rows=%d missing=%d extra=%d\n",
                strict ? "ALERT" : "diagnostic", edge_n, induced_n, missing, extra);
        print_row_set_missing0(fp, "missing_from_vertex_figures", edge_set, edge_n, induced_set, induced_n);
        print_row_set_missing0(fp, "extra_from_vertex_figures", induced_set, induced_n, edge_set, edge_n);
        fprintf(fp, "#\n");
    }
    fprintf(fp, "\n");
    for (int i = 0; i < vrow_count; i++) {
        fprintf(fp, "---[vf:%d]---\n", i + 1);
        fprintf(fp, "%s\n\n", vrows[i].text);
    }
    return missing || extra;
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
    int emitted = 0;
    int projected_total = 0;
    int hidden_total = 0;
    UniqueHex *unique_hexes = NULL;
    int unique_count = 0;
    int unique_cap = 0;
    EdgeRulePair *edge_rules = NULL;
    int edge_rule_count = 0;
    int edge_rule_cap = 0;
    CanonVertexFigureRow *vertex_rows = NULL;
    int vertex_row_count = 0;
    int vertex_row_cap = 0;

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
        printf("# Supertile hexagons use cyclic CCW lists of clustered triple-intersection vertices.\n");
        printf("# Edge tokens use down(e(A,B)) = e(first(A),last(B)) on cyclic cluster edges.\n");
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
        emitted++;
        projected_total += proj_count;
        hidden_total += hidden_count;
        char hex_key[2048];
        build_hex_key(clusters, cluster_count, hex_key, sizeof(hex_key));
        int unique_id = unique_hex_add(&unique_hexes, &unique_count, &unique_cap, hex_key, emitted) + 1;
        int parent_id = 0;
        if (opt.verbose && !opt.data_mode) {
            fprintf(stderr, "[%d] source=%zu parent=%d unique=%d reflected=%d hidden=%d clusters=%d\n",
                    emitted, ri + 1, parent_id, unique_id, proj_count, hidden_count, cluster_count);
        }
        if (opt.data_mode) {
            collect_projection_edge_rules(&ctx.tile, proj, proj_count, clusters, cluster_count,
                                          emitted, parent_id, unique_id,
                                          &edge_rules, &edge_rule_count,
                                          &edge_rule_cap, NULL);
            collect_projection_vertex_figures(&ctx.tile, proj, proj_count,
                                              clusters, cluster_count,
                                              edge_rules, edge_rule_count,
                                              &vertex_rows,
                                              &vertex_row_count,
                                              &vertex_row_cap);
        } else {
            emit_record(&ctx.tile, emitted, r, proj, proj_count,
                        hidden, hidden_count);
            if (opt.verbose) {
                print_hex_model(stderr, emitted, ri + 1, clusters, cluster_count);
            }
            collect_projection_edge_rules(&ctx.tile, proj, proj_count, clusters, cluster_count,
                                          emitted, parent_id, unique_id,
                                          &edge_rules, &edge_rule_count,
                                          &edge_rule_cap, opt.verbose ? stderr : NULL);
        }
        if (opt.limit > 0 && emitted >= opt.limit) break;
    }

    if (opt.data_mode) {
        printf("\nUnique Supertile Hexagons\n");
        for (int i = 0; i < unique_count; i++) {
            printf("%2d  %-78s  # records %s\n",
                   i + 1,
                   unique_hexes[i].key,
                   unique_hexes[i].records);
        }

        printf("\nDownmapped Super Hexagon Edge Matches\n");
        printf("# canonical rows; provenance lists source surround records\n");
        print_edge_matches(stdout, edge_rules, edge_rule_count, emitted);
        print_vertex_figures(stdout, vertex_rows, vertex_row_count,
                             edge_rules, edge_rule_count, 1);
        printf("\nSummary emitted=%d projected=%d hidden=%d unique=%d edge_matches=%d vertex_figures=%d\n",
               emitted, projected_total, hidden_total, unique_count, edge_rule_count, vertex_row_count);
    }

    if (opt.verbose) {
        fprintf(stderr, "rl4_refine: emitted=%d projected=%d hidden=%d unique=%d\n",
                emitted, projected_total, hidden_total, unique_count);
    }

    free(unique_hexes);
    free(edge_rules);
    free(vertex_rows);
    free(proj);
    free(hidden);
    free(clusters);
    bcomp1_free_records(&records);
    bcomp1_free_records(&super_records);
    bcomp1_context_clear(&ctx);
    return 0;
}
