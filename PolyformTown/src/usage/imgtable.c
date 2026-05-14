#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#define MAX_LINE 65536
#define MAX_SHAPES 512
#define MAX_CONSTS 32
#define MAX_NAME 32
#define MAX_EXPR 128
#define MAX_BLOCK 4096
#define MAX_BASES 16
#define MAX_CYCLES 32
#define MAX_VERTS_PER_CYCLE 4096
#define CELL_MARGIN 14.0
#define STROKE_W 1.5
#define UNIT 20.0

typedef struct { char name[MAX_NAME]; char expr[MAX_EXPR]; double value; int have_value; } Constant;
typedef struct { int valence; char e11[MAX_EXPR], e12[MAX_EXPR], e21[MAX_EXPR], e22[MAX_EXPR]; double a11, a12, a21, a22; } Basis;
typedef struct { int v, x, y; } VPoint;
typedef struct { int n; VPoint v[MAX_VERTS_PER_CYCLE]; } Path;
typedef struct {
    int hole_flag;
    int constant_count;
    Constant constants[MAX_CONSTS];
    int basis_count;
    Basis bases[MAX_BASES];
    int cycle_count;
    Path cycles[MAX_CYCLES];
} Shape;
typedef struct { double x, y; } DPoint;
typedef struct {
    Shape *aggregate;
    Shape *tiles;
    int tile_count;
    int tile_cap;
    VPoint *hidden;
    int hidden_count;
    int *tile_ch;
    Shape *highlights;
    int highlight_count;
    int highlight_cap;
    int hidden_cap;
    int has_center;
    VPoint center;
} GroupShape;

typedef enum {
    CHIRALITY_UNKNOWN = 0,
    CHIRALITY_NORMAL,
    CHIRALITY_REFLECTED
} Chirality;

typedef struct {
    const char *s;
    const Shape *shape;
} ExprParser;

typedef struct {
    double a11, a12, a21, a22;
    double tx, ty;
} Affine2;

static void skip_ws(const char **pp) {
    while (isspace((unsigned char)**pp)) (*pp)++;
}

static int parse_int(const char **pp, int *out) {
    char *end;
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

static void copy_trim(char *dst, size_t dstsz, const char *src, size_t len) {
    while (len > 0 && isspace((unsigned char)*src)) {
        src++;
        len--;
    }
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    if (len >= dstsz) len = dstsz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static int split_top_level(const char *src, char delim, const char **out_left, size_t *out_left_len,
                           const char **out_right, size_t *out_right_len) {
    int depth = 0;
    for (const char *p = src; *p; p++) {
        if (*p == '(') depth++;
        else if (*p == ')' && depth > 0) depth--;
        else if (*p == delim && depth == 0) {
            *out_left = src;
            *out_left_len = (size_t)(p - src);
            *out_right = p + 1;
            *out_right_len = strlen(p + 1);
            return 1;
        }
    }
    return 0;
}

static int extract_paren_block(const char **pp, char *dst, size_t dstsz) {
    const char *start;
    const char *p;
    int depth = 0;
    skip_ws(pp);
    if (**pp != '(') return 0;
    (*pp)++;
    start = *pp;
    p = *pp;
    while (*p) {
        if (*p == '(') depth++;
        else if (*p == ')') {
            if (depth == 0) {
                copy_trim(dst, dstsz, start, (size_t)(p - start));
                *pp = p + 1;
                return 1;
            }
            depth--;
        }
        p++;
    }
    return 0;
}

static int is_factor_start(char c) {
    return c == '(' || c == '.' || c == '+' || c == '-' || isdigit((unsigned char)c) || isalpha((unsigned char)c) || c == '_';
}

static Constant *find_constant(const Shape *shape, const char *name) {
    for (int i = 0; i < shape->constant_count; i++) {
        if (strcmp(shape->constants[i].name, name) == 0) return (Constant *)&shape->constants[i];
    }
    return NULL;
}

static double parse_expr_sum(ExprParser *p);

static double eval_constant(Constant *c, const Shape *shape) {
    if (c->have_value) return c->value;
    ExprParser p = { c->expr, shape };
    c->value = parse_expr_sum(&p);
    c->have_value = 1;
    return c->value;
}

static double parse_expr_atom(ExprParser *p) {
    double v;
    char name[MAX_NAME];
    int n = 0;

    while (isspace((unsigned char)*p->s)) p->s++;

    if (*p->s == '(') {
        p->s++;
        v = parse_expr_sum(p);
        while (isspace((unsigned char)*p->s)) p->s++;
        if (*p->s == ')') p->s++;
        return v;
    }

    if (isdigit((unsigned char)*p->s) || *p->s == '.') {
        char *end;
        v = strtod(p->s, &end);
        p->s = end;
        return v;
    }

    if (isalpha((unsigned char)*p->s) || *p->s == '_') {
        while ((isalpha((unsigned char)p->s[n]) || isdigit((unsigned char)p->s[n]) || p->s[n] == '_') && n < MAX_NAME - 1) {
            name[n] = p->s[n];
            n++;
        }
        name[n] = '\0';
        p->s += n;
        if (strcmp(name, "sqrt") == 0) {
            while (isspace((unsigned char)*p->s)) p->s++;
            if (*p->s == '(') {
                p->s++;
                v = parse_expr_sum(p);
                while (isspace((unsigned char)*p->s)) p->s++;
                if (*p->s == ')') p->s++;
                return sqrt(v);
            }
            return 0.0;
        }
        Constant *c = find_constant(p->shape, name);
        if (c) return eval_constant(c, p->shape);
        return 0.0;
    }

    return 0.0;
}

static double parse_expr_unary(ExprParser *p) {
    while (isspace((unsigned char)*p->s)) p->s++;
    if (*p->s == '+') {
        p->s++;
        return parse_expr_unary(p);
    }
    if (*p->s == '-') {
        p->s++;
        return -parse_expr_unary(p);
    }
    return parse_expr_atom(p);
}

static double parse_expr_product(ExprParser *p) {
    double v = parse_expr_unary(p);
    for (;;) {
        while (isspace((unsigned char)*p->s)) p->s++;
        if (*p->s == '*') {
            p->s++;
            v *= parse_expr_unary(p);
            continue;
        }
        if (*p->s == '/') {
            p->s++;
            v /= parse_expr_unary(p);
            continue;
        }
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
        while (isspace((unsigned char)*p->s)) p->s++;
        if (*p->s == '+') {
            p->s++;
            v += parse_expr_product(p);
            continue;
        }
        if (*p->s == '-') {
            p->s++;
            v -= parse_expr_product(p);
            continue;
        }
        break;
    }
    return v;
}

static double eval_expr_text(const Shape *shape, const char *expr) {
    ExprParser p = { expr, shape };
    return parse_expr_sum(&p);
}

static Basis *find_basis(const Shape *shape, int valence) {
    for (int i = 0; i < shape->basis_count; i++) {
        if (shape->bases[i].valence == valence) return (Basis *)&shape->bases[i];
    }
    return NULL;
}

static DPoint vertex_to_xy(const Shape *shape, VPoint p) {
    Basis *b = find_basis(shape, p.v);
    if (!b) return (DPoint){ (double)p.x, (double)p.y };
    return (DPoint){ b->a11 * p.x + b->a21 * p.y, b->a12 * p.x + b->a22 * p.y };
}

static int parse_constants_block(const char **pp, Shape *s) {
    char block[MAX_BLOCK];
    if (!extract_paren_block(pp, block, sizeof(block))) return 0;
    if (!block[0]) return 1;

    const char *cur = block;
    while (*cur && s->constant_count < MAX_CONSTS) {
        const char *lhs, *rhs;
        size_t lhs_len, rhs_len;
        char item[MAX_BLOCK];
        const char *comma;
        int depth = 0;
        for (comma = cur; *comma; comma++) {
            if (*comma == '(') depth++;
            else if (*comma == ')' && depth > 0) depth--;
            else if (*comma == ',' && depth == 0) break;
        }
        copy_trim(item, sizeof(item), cur, (size_t)(comma - cur));
        if (!split_top_level(item, '=', &lhs, &lhs_len, &rhs, &rhs_len)) return 0;
        copy_trim(s->constants[s->constant_count].name, sizeof(s->constants[s->constant_count].name), lhs, lhs_len);
        copy_trim(s->constants[s->constant_count].expr, sizeof(s->constants[s->constant_count].expr), rhs, rhs_len);
        s->constants[s->constant_count].have_value = 0;
        s->constant_count++;
        cur = *comma ? comma + 1 : comma;
    }
    return 1;
}

static int read_basis_expr(const char **pp, char *dst, size_t dstsz, char stop) {
    const char *start;
    int depth = 0;
    skip_ws(pp);
    start = *pp;
    while (**pp) {
        char ch = **pp;
        if (ch == '(') depth++;
        else if (ch == ')' && depth > 0) depth--;
        else if (ch == stop && depth == 0) break;
        (*pp)++;
    }
    copy_trim(dst, dstsz, start, (size_t)(*pp - start));
    return dst[0] != '\0';
}

static int read_basis_expr_last(const char **pp, char *dst, size_t dstsz) {
    const char *start;
    int depth = 0;
    skip_ws(pp);
    start = *pp;
    while (**pp) {
        char ch = **pp;
        if (ch == '(') depth++;
        else if (ch == ')' && depth > 0) depth--;
        else if (ch == ',' && depth == 0) {
            const char *q = *pp + 1;
            skip_ws(&q);
            if (isdigit((unsigned char)*q) || *q == '-') break;
        }
        (*pp)++;
    }
    copy_trim(dst, dstsz, start, (size_t)(*pp - start));
    return dst[0] != '\0';
}

static int parse_basis_block(const char **pp, Shape *s) {
    char block[MAX_BLOCK];
    const char *p;
    if (!extract_paren_block(pp, block, sizeof(block))) return 0;
    if (!block[0]) return 1;

    p = block;
    while (*p && s->basis_count < MAX_BASES) {
        Basis *b = &s->bases[s->basis_count];
        if (!parse_int(&p, &b->valence)) return 0;
        if (!expect_char(&p, ':')) return 0;
        if (!read_basis_expr(&p, b->e11, sizeof(b->e11), ',')) return 0;
        if (!expect_char(&p, ',')) return 0;
        if (!read_basis_expr(&p, b->e12, sizeof(b->e12), ';')) return 0;
        if (!expect_char(&p, ';')) return 0;
        if (!read_basis_expr(&p, b->e21, sizeof(b->e21), ',')) return 0;
        if (!expect_char(&p, ',')) return 0;
        if (!read_basis_expr_last(&p, b->e22, sizeof(b->e22))) return 0;
        s->basis_count++;
        skip_ws(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '\0') return 1;
        return 0;
    }
    return 1;
}

static int parse_cycle_block(const char **pp, Path *path) {
    char block[MAX_BLOCK];
    if (!extract_paren_block(pp, block, sizeof(block))) return 0;
    if (!block[0]) return 1;
    const char *p = block;
    while (path->n < MAX_VERTS_PER_CYCLE) {
        VPoint *v = &path->v[path->n];
        if (!parse_int(&p, &v->v) || !parse_int(&p, &v->x) || !parse_int(&p, &v->y)) return 0;
        path->n++;
        skip_ws(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '\0') return 1;
        return 0;
    }
    return 0;
}

static int parse_shape_line(const char *line, Shape *s) {
    const char *p = line;
    memset(s, 0, sizeof(*s));
    if (!expect_char(&p, '[')) return 0;
    if (!parse_int(&p, &s->hole_flag)) return 0;
    if (!expect_char(&p, '|')) return 0;
    if (!parse_constants_block(&p, s)) return 0;
    if (!expect_char(&p, '|')) return 0;
    if (!parse_basis_block(&p, s)) return 0;

    while (s->cycle_count < MAX_CYCLES) {
        if (!expect_char(&p, '|')) return 0;
        if (!parse_cycle_block(&p, &s->cycles[s->cycle_count])) return 0;
        s->cycle_count++;
        skip_ws(&p);
        if (*p == ']') {
            p++;
            break;
        }
    }
    if (s->hole_flag == 0 && s->cycle_count != 1) return 0;
    if (s->hole_flag == 1 && s->cycle_count < 2) return 0;

    for (int i = 0; i < s->basis_count; i++) {
        s->bases[i].a11 = eval_expr_text(s, s->bases[i].e11);
        s->bases[i].a12 = eval_expr_text(s, s->bases[i].e12);
        s->bases[i].a21 = eval_expr_text(s, s->bases[i].e21);
        s->bases[i].a22 = eval_expr_text(s, s->bases[i].e22);
    }
    return 1;
}

static int heading_index(const char *s) {
    if (s[0] != '[') return -1;
    if (!isdigit((unsigned char)s[1])) return -1;
    int i = 1;
    while (isdigit((unsigned char)s[i])) i++;
    if (s[i] != ']') return -1;
    return atoi(s + 1);
}

static void free_groups(GroupShape *groups, int count) {
    for (int i = 0; i < count; i++) {
        free(groups[i].aggregate);
        free(groups[i].tiles);
        free(groups[i].highlights);
        free(groups[i].hidden);
        free(groups[i].tile_ch);
    }
}

static int group_add_tile(GroupShape *g, const Shape *s, Chirality ch) {
    if (g->tile_count >= g->tile_cap) {
        int next_cap = g->tile_cap == 0 ? 8 : g->tile_cap * 2;
        Shape *next_tiles = (Shape *)realloc(g->tiles, (size_t)next_cap * sizeof(Shape));
        if (!next_tiles) return 0;
        g->tiles = next_tiles;
        int *next_ch = (int *)realloc(g->tile_ch, (size_t)next_cap * sizeof(int));
        if (!next_ch) return 0;
        g->tile_ch = next_ch;
        g->tile_cap = next_cap;
    }
    g->tiles[g->tile_count] = *s;
    g->tile_ch[g->tile_count++] = ch;
    return 1;
}

static int group_add_highlight(GroupShape *g, const Shape *s) {
    if (g->highlight_count >= g->highlight_cap) {
        int next_cap = g->highlight_cap == 0 ? 4 : g->highlight_cap * 2;
        Shape *next = (Shape *)realloc(g->highlights, (size_t)next_cap * sizeof(Shape));
        if (!next) return 0;
        g->highlights = next;
        g->highlight_cap = next_cap;
    }
    g->highlights[g->highlight_count++] = *s;
    return 1;
}

static int group_add_hidden(GroupShape *g, VPoint v) {
    if (g->hidden_count >= g->hidden_cap) {
        int next_cap = g->hidden_cap == 0 ? 16 : g->hidden_cap * 2;
        VPoint *next_hidden =
            (VPoint *)realloc(g->hidden, (size_t)next_cap * sizeof(VPoint));
        if (!next_hidden) return 0;
        g->hidden = next_hidden;
        g->hidden_cap = next_cap;
    }
    g->hidden[g->hidden_count++] = v;
    return 1;
}

static int ensure_group_capacity(GroupShape **groups_io, int *cap_io, int need) {
    if (need <= *cap_io) return 1;
    int next = (*cap_io == 0) ? 64 : (*cap_io * 2);
    while (next < need) next *= 2;
    GroupShape *next_groups =
        (GroupShape *)realloc(*groups_io, (size_t)next * sizeof(GroupShape));
    if (!next_groups) return 0;
    memset(next_groups + *cap_io, 0,
           (size_t)(next - *cap_io) * sizeof(GroupShape));
    *groups_io = next_groups;
    *cap_io = next;
    return 1;
}

static int read_group_shapes(GroupShape **groups_io, int *group_cap_io,
                             const char *first_line) {
    char line[MAX_LINE];
    int count = 0;
    int section = 0; /* 1 aggregate, 2 tiles, 3 center, 4 constellation, 5 light, 6 dark, 7 highlights */
    int cur = -1;
    GroupShape *groups = *groups_io;
    const char *pending = first_line;
    for (;;) {
        const char *p;
        if (pending) {
            p = pending;
            pending = NULL;
        } else {
            if (!fgets(line, sizeof(line), stdin)) break;
            p = line;
        }
        while (isspace((unsigned char)*p)) p++;
        if (!*p) continue;
        if (heading_index(p) >= 0) {
            if (!ensure_group_capacity(groups_io, group_cap_io, count + 1)) {
                return -1;
            }
            groups = *groups_io;
            cur = count++;
            memset(&groups[cur], 0, sizeof(groups[cur]));
            section = 0;
            continue;
        }
        if (strncmp(p, "Aggregate", 9) == 0) {
            section = 1;
            continue;
        }
        if (strncmp(p, "TilesLight", 10) == 0) { section = 5; continue; }
        if (strncmp(p, "TilesDark", 9) == 0) { section = 6; continue; }
        if (strncmp(p, "Highlights", 10) == 0) { section = 7; continue; }
        if (strncmp(p, "Tiles", 5) == 0) { section = 2; continue; }
        if (strncmp(p, "Center", 6) == 0) {
            section = 3;
            continue;
        }
        if (strncmp(p, "Constellation", 13) == 0 || strncmp(p, "Hidden", 6) == 0) { section = 4; continue; }
        if (cur < 0) continue;
        if (section == 3) {
            const char *q = p;
            int v, x, y;
            skip_ws(&q);
            if (*q == '(') {
                q++;
                if (parse_int(&q, &v) &&
                    expect_char(&q, ',') &&
                    parse_int(&q, &x) &&
                    expect_char(&q, ',') &&
                    parse_int(&q, &y)) {
                    groups[cur].has_center = 1;
                    groups[cur].center.v = v;
                    groups[cur].center.x = x;
                    groups[cur].center.y = y;
                }
            } else if (parse_int(&q, &v) &&
                       parse_int(&q, &x) &&
                       parse_int(&q, &y)) {
                groups[cur].has_center = 1;
                groups[cur].center.v = v;
                groups[cur].center.x = x;
                groups[cur].center.y = y;
            }
            continue;
        }
        if (section == 4) {
            const char *q = p;
            VPoint hv;
            skip_ws(&q);
            if (*q == '(') {
                q++;
                if (parse_int(&q, &hv.v) &&
                    expect_char(&q, ',') &&
                    parse_int(&q, &hv.x) &&
                    expect_char(&q, ',') &&
                    parse_int(&q, &hv.y)) {
                    if (!group_add_hidden(&groups[cur], hv)) return -1;
                }
            } else if (parse_int(&q, &hv.v) &&
                       parse_int(&q, &hv.x) &&
                       parse_int(&q, &hv.y)) {
                if (!group_add_hidden(&groups[cur], hv)) return -1;
            }
            continue;
        }
        Shape s;
        if (!parse_shape_line(p, &s)) continue;
        if (section == 1 && !groups[cur].aggregate) {
            groups[cur].aggregate = (Shape *)malloc(sizeof(Shape));
            if (!groups[cur].aggregate) return -1;
            *groups[cur].aggregate = s;
        } else if (section == 2) {
            if (!group_add_tile(&groups[cur], &s, CHIRALITY_UNKNOWN)) return -1;
        } else if (section == 5) {
            if (!group_add_tile(&groups[cur], &s, CHIRALITY_NORMAL)) return -1;
        } else if (section == 6) {
            if (!group_add_tile(&groups[cur], &s, CHIRALITY_REFLECTED)) return -1;
        } else if (section == 7) {
            if (!group_add_highlight(&groups[cur], &s)) return -1;
        }
    }
    int kept = 0;
    for (int i = 0; i < count; i++) {
        if (!groups[i].aggregate) {
            free(groups[i].tiles);
            continue;
        }
        if (kept != i) groups[kept] = groups[i];
        kept++;
    }
    return kept;
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

static int affine_maps_point(const Affine2 *a, const DPoint *r, const DPoint *c) {
    double x = a->a11 * r->x + a->a12 * r->y + a->tx;
    double y = a->a21 * r->x + a->a22 * r->y + a->ty;
    return fabs(x - c->x) < 1e-6 && fabs(y - c->y) < 1e-6;
}

static int cycle_relation_det_sign(const Shape *ref, const Shape *cand) {
    if (!ref || !cand || ref->cycle_count <= 0 || cand->cycle_count <= 0) {
        return 0;
    }
    const Path *rp = &ref->cycles[0];
    const Path *cp = &cand->cycles[0];
    if (rp->n != cp->n || rp->n < 3) return 0;

    int n = rp->n;
    DPoint r0 = vertex_to_xy(ref, rp->v[0]);
    DPoint r1 = vertex_to_xy(ref, rp->v[1]);
    int rk = 2;
    DPoint r2 = vertex_to_xy(ref, rp->v[rk]);
    while (rk < n) {
        double ux1 = r1.x - r0.x, uy1 = r1.y - r0.y;
        double ux2 = r2.x - r0.x, uy2 = r2.y - r0.y;
        if (fabs(ux1 * uy2 - uy1 * ux2) >= 1e-12) break;
        rk++;
        if (rk < n) r2 = vertex_to_xy(ref, rp->v[rk]);
    }
    if (rk >= n) return 0;

    int found_pos = 0;
    int found_neg = 0;
    for (int j = 0; j < n; j++) {
        for (int dir = -1; dir <= 1; dir += 2) {
            int j0 = j;
            int j1 = wrap_index(n, j + dir);
            int j2 = wrap_index(n, j + dir * rk);
            DPoint c0 = vertex_to_xy(cand, cp->v[j0]);
            DPoint c1 = vertex_to_xy(cand, cp->v[j1]);
            DPoint c2 = vertex_to_xy(cand, cp->v[j2]);
            Affine2 a;
            if (!affine_build(&r0, &r1, &r2, &c0, &c1, &c2, &a)) continue;
            int ok = 1;
            for (int t = 0; t < n; t++) {
                DPoint rt = vertex_to_xy(ref, rp->v[t]);
                DPoint ct = vertex_to_xy(cand, cp->v[wrap_index(n, j + dir * t)]);
                if (!affine_maps_point(&a, &rt, &ct)) {
                    ok = 0;
                    break;
                }
            }
            if (ok) {
                double det = a.a11 * a.a22 - a.a12 * a.a21;
                if (det > 1e-9) found_pos = 1;
                if (det < -1e-9) found_neg = 1;
                if (found_pos && found_neg) return 0;
            }
        }
    }
    if (found_pos) return +1;
    if (found_neg) return -1;
    return 0;
}

static void shape_bbox(const Shape *s, double *minx, double *miny, double *maxx, double *maxy) {
    int first = 1;
    *minx = *miny = *maxx = *maxy = 0.0;
    for (int c = 0; c < s->cycle_count; c++) {
        const Path *p = &s->cycles[c];
        for (int i = 0; i < p->n; i++) {
            DPoint q = vertex_to_xy(s, p->v[i]);
            if (first) {
                *minx = *maxx = q.x;
                *miny = *maxy = q.y;
                first = 0;
            } else {
                if (q.x < *minx) *minx = q.x;
                if (q.x > *maxx) *maxx = q.x;
                if (q.y < *miny) *miny = q.y;
                if (q.y > *maxy) *maxy = q.y;
            }
        }
    }
}

static void global_bbox(const Shape *shapes, int count, double *minx, double *miny, double *maxx, double *maxy) {
    int first = 1;
    *minx = *miny = *maxx = *maxy = 0.0;
    for (int s = 0; s < count; s++) {
        double sx0, sy0, sx1, sy1;
        shape_bbox(&shapes[s], &sx0, &sy0, &sx1, &sy1);
        if (first) {
            *minx = sx0; *miny = sy0; *maxx = sx1; *maxy = sy1;
            first = 0;
        } else {
            if (sx0 < *minx) *minx = sx0;
            if (sy0 < *miny) *miny = sy0;
            if (sx1 > *maxx) *maxx = sx1;
            if (sy1 > *maxy) *maxy = sy1;
        }
    }
}

static void choose_grid(int count, double cell_w, double cell_h, int *out_rows, int *out_cols) {
    double target = 16.0 / 9.0;
    double best_score = 1e100;
    int best_r = 1, best_c = count > 0 ? count : 1;
    for (int rows = 1; rows <= count; rows++) {
        int cols = (count + rows - 1) / rows;
        double aspect = (cols * cell_w) / (rows * cell_h);
        double score = fabs(aspect - target);
        if (score < best_score) {
            best_score = score;
            best_r = rows;
            best_c = cols;
        }
    }
    *out_rows = best_r;
    *out_cols = best_c;
}

static void emit_shape_svg(FILE *fp, const Shape *s,
                           double cell_x, double cell_y,
                           double cell_w, double cell_h,
                           double scale) {
    double minx, miny, maxx, maxy;
    shape_bbox(s, &minx, &miny, &maxx, &maxy);
    double bw = maxx - minx;
    double bh = maxy - miny;

    double tx = cell_x + (cell_w - scale * bw) * 0.5 - scale * minx;
    double ty = cell_y + (cell_h - scale * bh) * 0.5 + scale * maxy;

    fprintf(fp, "<path d=\"");
    for (int c = 0; c < s->cycle_count; c++) {
        const Path *p = &s->cycles[c];
        if (p->n <= 0) continue;
        DPoint q0 = vertex_to_xy(s, p->v[0]);
        double x0 = tx + scale * q0.x;
        double y0 = ty - scale * q0.y;
        fprintf(fp, "M %.3f %.3f ", x0, y0);
        for (int i = 1; i < p->n; i++) {
            DPoint q = vertex_to_xy(s, p->v[i]);
            double x = tx + scale * q.x;
            double y = ty - scale * q.y;
            fprintf(fp, "L %.3f %.3f ", x, y);
        }
        fprintf(fp, "Z ");
    }
    fprintf(fp, "\" fill=\"#dddddd\" stroke=\"black\" stroke-width=\"%.2f\" fill-rule=\"evenodd\"/>\n", STROKE_W);
}

static void group_bbox(const GroupShape *g,
                       double *minx, double *miny,
                       double *maxx, double *maxy) {
    shape_bbox(g->aggregate, minx, miny, maxx, maxy);
    for (int i = 0; i < g->tile_count; i++) {
        double x0, y0, x1, y1;
        shape_bbox(&g->tiles[i], &x0, &y0, &x1, &y1);
        if (x0 < *minx) *minx = x0;
        if (y0 < *miny) *miny = y0;
        if (x1 > *maxx) *maxx = x1;
        if (y1 > *maxy) *maxy = y1;
    }
    for (int i = 0; i < g->highlight_count; i++) {
        double x0, y0, x1, y1;
        shape_bbox(&g->highlights[i], &x0, &y0, &x1, &y1);
        if (x0 < *minx) *minx = x0;
        if (y0 < *miny) *miny = y0;
        if (x1 > *maxx) *maxx = x1;
        if (y1 > *maxy) *maxy = y1;
    }
}

static void emit_shape_path(FILE *fp, const Shape *s,
                            double tx, double ty, double scale,
                            const char *fill, const char *stroke,
                            double stroke_w) {
    fprintf(fp, "<path d=\"");
    for (int c = 0; c < s->cycle_count; c++) {
        const Path *p = &s->cycles[c];
        if (p->n <= 0) continue;
        DPoint q0 = vertex_to_xy(s, p->v[0]);
        fprintf(fp, "M %.3f %.3f ", tx + scale * q0.x, ty - scale * q0.y);
        for (int i = 1; i < p->n; i++) {
            DPoint q = vertex_to_xy(s, p->v[i]);
            fprintf(fp, "L %.3f %.3f ", tx + scale * q.x, ty - scale * q.y);
        }
        fprintf(fp, "Z ");
    }
    fprintf(fp, "\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%.2f\" fill-rule=\"evenodd\"/>\n",
            fill, stroke, stroke_w);
}

static void emit_group_svg(FILE *fp, const GroupShape *g,
                           double cell_x, double cell_y,
                           double cell_w, double cell_h,
                           double scale) {
    double minx, miny, maxx, maxy;
    group_bbox(g, &minx, &miny, &maxx, &maxy);
    double bw = maxx - minx;
    double bh = maxy - miny;
    double tx = cell_x + (cell_w - scale * bw) * 0.5 - scale * minx;
    double ty = cell_y + (cell_h - scale * bh) * 0.5 + scale * maxy;
    emit_shape_path(fp, g->aggregate, tx, ty, scale, "#dddddd", "black", STROKE_W);
    for (int i = 0; i < g->tile_count; i++) {
        const char *fill = "#d9e9f7";
        Chirality c = g->tile_ch ? g->tile_ch[i] : CHIRALITY_UNKNOWN;
        if (c == CHIRALITY_UNKNOWN && g->tile_count > 0) {
            int sign = cycle_relation_det_sign(&g->tiles[0], &g->tiles[i]);
            if (sign > 0) c = CHIRALITY_NORMAL;
            else if (sign < 0) c = CHIRALITY_REFLECTED;
        }
        if (c == CHIRALITY_REFLECTED) fill = "#bcd0e2";
        emit_shape_path(fp, &g->tiles[i], tx, ty, scale, fill, "#666666", 0.8);
    }
    for (int i = 0; i < g->highlight_count; i++) {
        emit_shape_path(fp, &g->highlights[i], tx, ty, scale, "none", "#d60000", 2.4);
    }
    for (int i = 0; i < g->hidden_count; i++) {
        DPoint h = vertex_to_xy(g->aggregate, g->hidden[i]);
        double hx = tx + scale * h.x;
        double hy = ty - scale * h.y;
        fprintf(fp,
                "<circle cx=\"%.3f\" cy=\"%.3f\" r=\"2.1\" "
                "fill=\"#000000\" stroke=\"none\"/>\n",
                hx, hy);
    }
    if (g->has_center) {
        DPoint c = vertex_to_xy(g->aggregate, g->center);
        double cx = tx + scale * c.x;
        double cy = ty - scale * c.y;
        fprintf(fp,
                "<circle cx=\"%.3f\" cy=\"%.3f\" r=\"2.8\" "
                "fill=\"#d60000\" stroke=\"none\"/>\n",
                cx, cy);
    }
}

int main(void) {
    char first_line[MAX_LINE];
    const char *first = NULL;
    while (fgets(first_line, sizeof(first_line), stdin)) {
        const char *p = first_line;
        while (isspace((unsigned char)*p)) p++;
        if (*p) {
            first = p;
            break;
        }
    }
    if (!first) {
        fprintf(stderr, "imgtable: no shapes on stdin\n");
        return 1;
    }

    GroupShape *groups = NULL;
    int group_cap = 0;
    int group_count = 0;
    if (heading_index(first) >= 0) {
        group_count = read_group_shapes(&groups, &group_cap, first);
    }
    if (group_count < 0) {
        fprintf(stderr, "imgtable: failed to parse grouped input\n");
        free_groups(groups, group_cap);
        free(groups);
        return 1;
    }
    if (group_count > 0) {
        double gx0, gy0, gx1, gy1;
        group_bbox(&groups[0], &gx0, &gy0, &gx1, &gy1);
        double gminx = gx0, gminy = gy0, gmaxx = gx1, gmaxy = gy1;
        for (int i = 1; i < group_count; i++) {
            group_bbox(&groups[i], &gx0, &gy0, &gx1, &gy1);
            if (gx0 < gminx) gminx = gx0;
            if (gy0 < gminy) gminy = gy0;
            if (gx1 > gmaxx) gmaxx = gx1;
            if (gy1 > gmaxy) gmaxy = gy1;
        }
        double gw = gmaxx - gminx;
        double gh = gmaxy - gminy;
        if (gw < 1e-9) gw = 1.0;
        if (gh < 1e-9) gh = 1.0;
        double cell_w = 2.0 * CELL_MARGIN + UNIT * gw;
        double cell_h = 2.0 * CELL_MARGIN + UNIT * gh;
        int rows, cols;
        choose_grid(group_count, cell_w, cell_h, &rows, &cols);
        double width = cols * cell_w;
        double height = rows * cell_h;
        printf("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" viewBox=\"0 0 %.0f %.0f\">\n",
               width, height, width, height);
        printf("<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>\n");
        for (int i = 0; i < group_count; i++) {
            int row = i / cols;
            int col = i % cols;
            emit_group_svg(stdout, &groups[i],
                           col * cell_w, row * cell_h,
                           cell_w, cell_h, UNIT);
        }
        printf("</svg>\n");
        free_groups(groups, group_count);
        free(groups);
        return 0;
    }
    free(groups);

    Shape *shapes = (Shape *)calloc(MAX_SHAPES, sizeof(Shape));
    if (!shapes) {
        fprintf(stderr, "imgtable: out of memory\n");
        return 1;
    }
    int count = 0;
    if (!parse_shape_line(first, &shapes[count])) {
        fprintf(stderr, "imgtable: failed to parse input\n");
        free(shapes);
        return 1;
    }
    count++;
    while (count < MAX_SHAPES && fgets(first_line, sizeof(first_line), stdin)) {
        const char *p = first_line;
        while (isspace((unsigned char)*p)) p++;
        if (!*p) continue;
        if (!parse_shape_line(p, &shapes[count])) {
            fprintf(stderr, "imgtable: failed to parse input\n");
            free(shapes);
            return 1;
        }
        count++;
    }

    double global_minx, global_miny, global_maxx, global_maxy;
    global_bbox(shapes, count, &global_minx, &global_miny, &global_maxx, &global_maxy);

    double global_bw = global_maxx - global_minx;
    double global_bh = global_maxy - global_miny;
    if (global_bw < 1e-9) global_bw = 1.0;
    if (global_bh < 1e-9) global_bh = 1.0;

    double cell_w = 2.0 * CELL_MARGIN + UNIT * global_bw;
    double cell_h = 2.0 * CELL_MARGIN + UNIT * global_bh;

    int rows, cols;
    choose_grid(count, cell_w, cell_h, &rows, &cols);
    double width = cols * cell_w;
    double height = rows * cell_h;

    printf("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" viewBox=\"0 0 %.0f %.0f\">\n", width, height, width, height);
    printf("<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>\n");

    for (int i = 0; i < count; i++) {
        int row = i / cols;
        int col = i % cols;
        emit_shape_svg(stdout, &shapes[i],
                       col * cell_w, row * cell_h,
                       cell_w, cell_h, UNIT);
    }

    printf("</svg>\n");
    free(shapes);
    return 0;
}
