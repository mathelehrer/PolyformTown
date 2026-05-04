#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 65536
#define MAX_CONSTS 32
#define MAX_NAME 32
#define MAX_EXPR 128
#define MAX_BLOCK 4096
#define MAX_BASES 16
#define MAX_CYCLES 32
#define MAX_VERTS_PER_CYCLE 4096
#define BOX_W 210.0
#define BOX_H 180.0
#define BOX_GAP_X 36.0
#define BOX_GAP_Y 70.0
#define PAGE_MARGIN 28.0
#define SHAPE_PAD 12.0

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
typedef struct { const char *s; const Shape *shape; } ExprParser;

typedef struct {
    int id;
    int discover_step;
    int tile_count;
    int hidden_count;
    int tile_path_count;
    int tile_path_cap;
    int have_focus;
    char kind[32];
    char key[MAX_BLOCK];
    VPoint focus_port;
    VPoint *hidden;
    Path *tiles;
    unsigned char *tile_reflected;
    Shape aggregate;
} GraphNode;

typedef struct {
    int src;
    int dst;
    int step;
    int distance;
    char kind[32];
    VPoint port;
} GraphEdge;

typedef struct {
    GraphNode *data;
    int count;
    int cap;
} NodeVec;

typedef struct {
    GraphEdge *data;
    int count;
    int cap;
} EdgeVec;

static int find_node_index_by_key(const NodeVec *nodes, const char *key);

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

static int parse_coord_triplet(const char *text, VPoint *out) {
    const char *p = text;
    if (!expect_char(&p, '(')) return 0;
    if (!parse_int(&p, &out->v)) return 0;
    if (!expect_char(&p, ',')) return 0;
    if (!parse_int(&p, &out->x)) return 0;
    if (!expect_char(&p, ',')) return 0;
    if (!parse_int(&p, &out->y)) return 0;
    if (!expect_char(&p, ')')) return 0;
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

static int split_top_level(const char *src, char delim,
                           const char **out_left, size_t *out_left_len,
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
        {
            Constant *c = find_constant(p->shape, name);
            if (c) return eval_constant(c, p->shape);
        }
        return 0.0;
    }

    return 0.0;
}

static double parse_expr_unary(ExprParser *p) {
    while (isspace((unsigned char)*p->s)) p->s++;
    if (*p->s == '+') { p->s++; return parse_expr_unary(p); }
    if (*p->s == '-') { p->s++; return -parse_expr_unary(p); }
    return parse_expr_atom(p);
}

static double parse_expr_product(ExprParser *p) {
    double v = parse_expr_unary(p);
    for (;;) {
        while (isspace((unsigned char)*p->s)) p->s++;
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
        while (isspace((unsigned char)*p->s)) p->s++;
        if (*p->s == '+') { p->s++; v += parse_expr_product(p); continue; }
        if (*p->s == '-') { p->s++; v -= parse_expr_product(p); continue; }
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
        if (*p == ',') { p++; continue; }
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
        if (*p == ',') { p++; continue; }
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
        if (*p == ']') { p++; break; }
    }
    for (int i = 0; i < s->basis_count; i++) {
        s->bases[i].a11 = eval_expr_text(s, s->bases[i].e11);
        s->bases[i].a12 = eval_expr_text(s, s->bases[i].e12);
        s->bases[i].a21 = eval_expr_text(s, s->bases[i].e21);
        s->bases[i].a22 = eval_expr_text(s, s->bases[i].e22);
    }
    return 1;
}

static int parse_hidden_list(const char *text, VPoint **out, int *out_count) {
    const char *p = text;
    int cap = 16;
    int count = 0;
    VPoint *arr = NULL;
    if (!expect_char(&p, '[')) return 0;
    arr = (VPoint *)malloc((size_t)cap * sizeof(VPoint));
    if (!arr) return 0;
    skip_ws(&p);
    if (*p == ']') {
        p++;
        *out = arr;
        *out_count = 0;
        return 1;
    }
    for (;;) {
        VPoint q;
        if (!parse_coord_triplet(p, &q)) break;
        while (*p && *p != ')') p++;
        if (*p == ')') p++;
        if (count >= cap) {
            int nc = cap * 2;
            VPoint *next = (VPoint *)realloc(arr, (size_t)nc * sizeof(VPoint));
            if (!next) break;
            arr = next;
            cap = nc;
        }
        arr[count++] = q;
        skip_ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == ']') {
            p++;
            *out = arr;
            *out_count = count;
            return 1;
        }
        break;
    }
    free(arr);
    return 0;
}

static int nodevec_push(NodeVec *v, const GraphNode *n) {
    if (v->count >= v->cap) {
        int nc = v->cap ? 2 * v->cap : 64;
        GraphNode *next = (GraphNode *)realloc(v->data, (size_t)nc * sizeof(GraphNode));
        if (!next) return 0;
        v->data = next;
        v->cap = nc;
    }
    v->data[v->count++] = *n;
    return 1;
}

static int edgevec_push(EdgeVec *v, const GraphEdge *e) {
    if (v->count >= v->cap) {
        int nc = v->cap ? 2 * v->cap : 128;
        GraphEdge *next = (GraphEdge *)realloc(v->data, (size_t)nc * sizeof(GraphEdge));
        if (!next) return 0;
        v->data = next;
        v->cap = nc;
    }
    v->data[v->count++] = *e;
    return 1;
}

static void free_nodes(NodeVec *v) {
    for (int i = 0; i < v->count; i++) {
        free(v->data[i].hidden);
        free(v->data[i].tiles);
        free(v->data[i].tile_reflected);
    }
    free(v->data);
    v->data = NULL;
    v->count = v->cap = 0;
}

static int parse_graph_file(FILE *fp, NodeVec *nodes, EdgeVec *edges) {
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "---node ", 8) == 0) {
            GraphNode n;
            memset(&n, 0, sizeof(n));
            sscanf(line, "---node %d---", &n.id);
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "---end-node---", 14) == 0) break;
                if (strncmp(line, "kind:", 5) == 0) {
                    copy_trim(n.kind, sizeof(n.kind), line + 5, strlen(line + 5));
                } else if (strncmp(line, "discover_step:", 14) == 0) {
                    n.discover_step = atoi(line + 14);
                } else if (strncmp(line, "tile_count:", 11) == 0) {
                    n.tile_count = atoi(line + 11);
                } else if (strncmp(line, "hidden_count:", 13) == 0) {
                    n.hidden_count = atoi(line + 13);
                } else if (strncmp(line, "hidden:", 7) == 0) {
                    if (!parse_hidden_list(line + 7, &n.hidden, &n.hidden_count)) return 0;
                } else if (strncmp(line, "tile:", 5) == 0) {
                    const char *pp = line + 5;
                    int reflected = 0;
                    skip_ws(&pp);
                    if (strncmp(pp, "reflected", 9) == 0) {
                        reflected = 1;
                        pp += 9;
                    } else if (strncmp(pp, "normal", 6) == 0) {
                        reflected = 0;
                        pp += 6;
                    }
                    skip_ws(&pp);
                    if (n.tile_path_count >= n.tile_path_cap) {
                        int nc = n.tile_path_cap ? 2 * n.tile_path_cap : 16;
                        Path *next_tiles = (Path *)realloc(n.tiles, (size_t)nc * sizeof(Path));
                        unsigned char *next_refl;
                        if (!next_tiles) return 0;
                        n.tiles = next_tiles;
                        next_refl = (unsigned char *)realloc(n.tile_reflected, (size_t)nc * sizeof(unsigned char));
                        if (!next_refl) return 0;
                        n.tile_reflected = next_refl;
                        n.tile_path_cap = nc;
                    }
                    memset(&n.tiles[n.tile_path_count], 0, sizeof(Path));
                    if (!parse_cycle_block(&pp, &n.tiles[n.tile_path_count])) return 0;
                    n.tile_reflected[n.tile_path_count] = (unsigned char)reflected;
                    n.tile_path_count++;
                } else if (strncmp(line, "key:", 4) == 0) {
                    copy_trim(n.key, sizeof(n.key), line + 4, strlen(line + 4));
                } else if (strncmp(line, "aggregate:", 10) == 0) {
                    if (!fgets(line, sizeof(line), fp)) return 0;
                    if (!parse_shape_line(line, &n.aggregate)) return 0;
                }
            }
            if (!nodevec_push(nodes, &n)) return 0;
        } else if (strncmp(line, "---edge---", 10) == 0) {
            GraphEdge e;
            memset(&e, 0, sizeof(e));
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "---end-edge---", 14) == 0) break;
                if (strncmp(line, "kind:", 5) == 0) {
                    copy_trim(e.kind, sizeof(e.kind), line + 5, strlen(line + 5));
                } else if (strncmp(line, "src:", 4) == 0) {
                    e.src = atoi(line + 4);
                } else if (strncmp(line, "dst:", 4) == 0) {
                    e.dst = atoi(line + 4);
                } else if (strncmp(line, "step:", 5) == 0) {
                    e.step = atoi(line + 5);
                } else if (strncmp(line, "distance:", 9) == 0) {
                    e.distance = atoi(line + 9);
                } else if (strncmp(line, "port:", 5) == 0) {
                    if (!parse_coord_triplet(line + 5, &e.port)) return 0;
                }
            }
            if (!edgevec_push(edges, &e)) return 0;
        } else if (strncmp(line, "---focus---", 11) == 0) {
            char key[MAX_BLOCK] = {0};
            VPoint port = {0,0,0};
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "---end-focus---", 15) == 0) break;
                if (strncmp(line, "key:", 4) == 0) {
                    copy_trim(key, sizeof(key), line + 4, strlen(line + 4));
                } else if (strncmp(line, "port:", 5) == 0) {
                    if (!parse_coord_triplet(line + 5, &port)) return 0;
                }
            }
            if (key[0]) {
                int ni = find_node_index_by_key(nodes, key);
                if (ni >= 0) {
                    nodes->data[ni].have_focus = 1;
                    nodes->data[ni].focus_port = port;
                }
            }
        }
    }
    return 1;
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

static void emit_shape_path(FILE *fp, const Shape *s, double tx, double ty, double scale) {
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
    fprintf(fp, "\" fill=\"#dddddd\" stroke=\"black\" stroke-width=\"1.1\" fill-rule=\"evenodd\"/>\n");
}

static void emit_tile_paths(FILE *fp, const GraphNode *node, double tx, double ty, double scale) {
    for (int t = 0; t < node->tile_path_count; t++) {
        const Path *p = &node->tiles[t];
        if (p->n <= 0) continue;
        fprintf(fp, "<path d=\"");
        for (int i = 0; i < p->n; i++) {
            DPoint q = vertex_to_xy(&node->aggregate, p->v[i]);
            fprintf(fp, "%c %.3f %.3f ", i ? 'L' : 'M', tx + scale * q.x, ty - scale * q.y);
        }
        fprintf(fp, "Z\" fill=\"none\" stroke=\"#666666\" stroke-width=\"0.6\"/>\n");
    }
}

static double hidden_radius0(const GraphNode *node) {
    double r = 2.4 / sqrt(1.0 + 0.10 * (double)(node->hidden_count > 0 ? node->hidden_count : 0));
    if (r < 0.45) r = 0.45;
    if (r > 1.8) r = 1.8;
    return r;
}

static int cmp_node_step_id(const void *A, const void *B) {
    const GraphNode *a = (const GraphNode *)A;
    const GraphNode *b = (const GraphNode *)B;
    int sa = a->discover_step < -1 ? -1 : a->discover_step;
    int sb = b->discover_step < -1 ? -1 : b->discover_step;
    if (sa != sb) return sa - sb;
    return a->id - b->id;
}

static int find_node_index_by_id(const NodeVec *nodes, int id) {
    for (int i = 0; i < nodes->count; i++) if (nodes->data[i].id == id) return i;
    return -1;
}

static int find_node_index_by_key(const NodeVec *nodes, const char *key) {
    for (int i = 0; i < nodes->count; i++) {
        if (strcmp(nodes->data[i].key, key) == 0) return i;
    }
    return -1;
}

typedef struct {
    int idx;
    double key;
    int tie;
} OrderItem;

static int cmp_order_item0(const void *A, const void *B) {
    const OrderItem *a = (const OrderItem *)A;
    const OrderItem *b = (const OrderItem *)B;
    if (a->key < b->key) return -1;
    if (a->key > b->key) return 1;
    return a->tie - b->tie;
}

static int compute_vertex_strata(const NodeVec *nodes,
                                 const EdgeVec *edges,
                                 int *levels,
                                 int *rows_out) {
    int n = nodes->count;
    int m = edges->count;
    int *indeg = (int *)calloc((size_t)n, sizeof(int));
    int *queue = (int *)calloc((size_t)n, sizeof(int));
    int *src_index = (int *)malloc((size_t)m * sizeof(int));
    int *dst_index = (int *)malloc((size_t)m * sizeof(int));
    int head = 0;
    int tail = 0;
    int processed = 0;
    int rows = 0;
    if (!indeg || !queue || !src_index || !dst_index) {
        free(indeg); free(queue); free(src_index); free(dst_index);
        return 0;
    }
    for (int i = 0; i < n; i++) levels[i] = 0;
    for (int e = 0; e < m; e++) {
        src_index[e] = find_node_index_by_id(nodes, edges->data[e].src);
        dst_index[e] = find_node_index_by_id(nodes, edges->data[e].dst);
        if (src_index[e] >= 0 && dst_index[e] >= 0 && src_index[e] != dst_index[e]) indeg[dst_index[e]]++;
    }
    for (int i = 0; i < n; i++) if (indeg[i] == 0) queue[tail++] = i;
    while (head < tail) {
        int u = queue[head++];
        processed++;
        if (levels[u] + 1 > rows) rows = levels[u] + 1;
        for (int e = 0; e < m; e++) {
            int v;
            if (src_index[e] != u) continue;
            v = dst_index[e];
            if (v < 0 || v == u) continue;
            if (levels[v] < levels[u] + 1) levels[v] = levels[u] + 1;
            indeg[v]--;
            if (indeg[v] == 0) queue[tail++] = v;
        }
    }
    if (processed < n) {
        for (int i = 0; i < n; i++) if (indeg[i] > 0) levels[i] = rows;
        rows++;
    }
    if (rows < 1) rows = 1;
    *rows_out = rows;
    free(indeg); free(queue); free(src_index); free(dst_index);
    return 1;
}

static void build_row_layout0(const NodeVec *nodes,
                              const EdgeVec *edges,
                              const int *levels,
                              int rows,
                              int **row_nodes_out,
                              int **row_offsets_out,
                              int **row_counts_out,
                              int **order_in_row_out) {
    int n = nodes->count;
    int *row_counts = (int *)calloc((size_t)rows, sizeof(int));
    int *row_offsets = (int *)calloc((size_t)(rows + 1), sizeof(int));
    int *fill = (int *)calloc((size_t)rows, sizeof(int));
    int *row_nodes;
    int *order_in_row = (int *)calloc((size_t)n, sizeof(int));
    if (!row_counts || !row_offsets || !fill || !order_in_row) exit(1);
    for (int i = 0; i < n; i++) row_counts[levels[i]]++;
    for (int r = 0; r < rows; r++) row_offsets[r + 1] = row_offsets[r] + row_counts[r];
    row_nodes = (int *)calloc((size_t)n, sizeof(int));
    if (!row_nodes) exit(1);
    for (int i = 0; i < n; i++) {
        int row = levels[i];
        int pos = row_offsets[row] + fill[row]++;
        row_nodes[pos] = i;
    }
    for (int r = 0; r < rows; r++) {
        for (int k = 0; k < row_counts[r]; k++) order_in_row[row_nodes[row_offsets[r] + k]] = k;
    }
    for (int pass = 0; pass < 6; pass++) {
        for (int dir = 0; dir < 2; dir++) {
            int rstart = dir ? rows - 2 : 1;
            int rend = dir ? -1 : rows;
            int rstep = dir ? -1 : 1;
            for (int r = rstart; r != rend; r += rstep) {
                int count = row_counts[r];
                int start = row_offsets[r];
                OrderItem *items = (OrderItem *)calloc((size_t)count, sizeof(OrderItem));
                if (!items) exit(1);
                for (int k = 0; k < count; k++) {
                    int idx = row_nodes[start + k];
                    double sum = 0.0;
                    int deg = 0;
                    items[k].idx = idx;
                    items[k].tie = k;
                    for (int e = 0; e < edges->count; e++) {
                        int si = find_node_index_by_id(nodes, edges->data[e].src);
                        int di = find_node_index_by_id(nodes, edges->data[e].dst);
                        if (si < 0 || di < 0 || si == di) continue;
                        if (!dir) {
                            if (di == idx && levels[si] < r) { sum += order_in_row[si]; deg++; }
                        } else {
                            if (si == idx && levels[di] > r) { sum += order_in_row[di]; deg++; }
                        }
                    }
                    items[k].key = deg ? (sum / (double)deg) : (double)k;
                }
                qsort(items, (size_t)count, sizeof(OrderItem), cmp_order_item0);
                for (int k = 0; k < count; k++) row_nodes[start + k] = items[k].idx;
                for (int k = 0; k < count; k++) order_in_row[row_nodes[start + k]] = k;
                free(items);
            }
        }
    }
    free(fill);
    *row_nodes_out = row_nodes;
    *row_offsets_out = row_offsets;
    *row_counts_out = row_counts;
    *order_in_row_out = order_in_row;
}

static int same_point0(VPoint a, VPoint b) {
    return a.v == b.v && a.x == b.x && a.y == b.y;
}

static void draw_node_red_marks0(FILE *fp,
                                 const NodeVec *nodes,
                                 const EdgeVec *edges,
                                 int node_index,
                                 double tx,
                                 double ty,
                                 double scale) {
    const GraphNode *node = &nodes->data[node_index];
    VPoint ports[256];
    int port_count = 0;
    for (int i = 0; i < edges->count; i++) {
        if (edges->data[i].src != node->id) continue;
        if (edges->data[i].port.v <= 0) continue;
        {
            int seen = 0;
            for (int j = 0; j < port_count; j++) {
                if (same_point0(ports[j], edges->data[i].port)) {
                    seen = 1;
                    break;
                }
            }
            if (!seen && port_count < (int)(sizeof(ports) / sizeof(ports[0]))) {
                ports[port_count++] = edges->data[i].port;
            }
        }
    }
    if (port_count == 0 && node->have_focus) {
        ports[port_count++] = node->focus_port;
    }
    for (int i = 0; i < port_count; i++) {
        DPoint q = vertex_to_xy(&node->aggregate, ports[i]);
        double px = tx + scale * q.x;
        double py = ty - scale * q.y;
        fprintf(fp,
                "<circle cx=\"%.3f\" cy=\"%.3f\" r=\"2.0\" fill=\"red\" stroke=\"black\" stroke-width=\"0.35\"/>\n",
                px,
                py);
    }
}

static void emit_svg(const NodeVec *nodes, const EdgeVec *edges) {
    int n = nodes->count;
    int rows;
    int max_cols = 1;
    int *levels = (int *)calloc((size_t)n, sizeof(int));
    int *row_nodes = NULL;
    int *row_offsets = NULL;
    int *row_counts = NULL;
    int *order_in_row = NULL;
    double *cx = (double *)calloc((size_t)n, sizeof(double));
    double *box_y = (double *)calloc((size_t)n, sizeof(double));
    double *txs = (double *)calloc((size_t)n, sizeof(double));
    double *tys = (double *)calloc((size_t)n, sizeof(double));
    double *scales = (double *)calloc((size_t)n, sizeof(double));
    double width, height;
    if (!levels || !cx || !box_y || !txs || !tys || !scales) exit(1);
    if (!compute_vertex_strata(nodes, edges, levels, &rows)) exit(1);
    build_row_layout0(nodes, edges, levels, rows, &row_nodes, &row_offsets, &row_counts, &order_in_row);
    for (int r = 0; r < rows; r++) if (row_counts[r] > max_cols) max_cols = row_counts[r];
    width = PAGE_MARGIN * 2 + max_cols * BOX_W + (max_cols - 1) * BOX_GAP_X;
    height = PAGE_MARGIN * 2 + rows * BOX_H + (rows - 1) * BOX_GAP_Y;
    printf("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" viewBox=\"0 0 %.0f %.0f\">\n", width, height, width, height);
    printf("<defs><marker id=\"arrowhead\" markerWidth=\"8\" markerHeight=\"6\" refX=\"6.2\" refY=\"3\" orient=\"auto\" markerUnits=\"strokeWidth\"><path d=\"M 0 0 L 6 3 L 0 6 z\" fill=\"#555555\"/></marker></defs>\n");
    printf("<rect x=\"0\" y=\"0\" width=\"%.0f\" height=\"%.0f\" fill=\"white\"/>\n", width, height);
    for (int row = 0; row < rows; row++) {
        double y_line = PAGE_MARGIN + row * (BOX_H + BOX_GAP_Y) - 10.0;
        if (row == 0) y_line = PAGE_MARGIN - 10.0;
        printf("<line x1=\"12\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#d0d0d0\" stroke-width=\"1.0\"/>\n", y_line, width - 12.0, y_line);
        printf("<text x=\"16\" y=\"%.1f\" font-size=\"12\" font-family=\"monospace\" fill=\"#666666\">stratum %d</text>\n", y_line - 4.0, row);
    }
    for (int row = 0; row < rows; row++) {
        double row_width = row_counts[row] * BOX_W + (row_counts[row] - 1) * BOX_GAP_X;
        for (int slot = 0; slot < row_counts[row]; slot++) {
            int idx = row_nodes[row_offsets[row] + slot];
            double x0 = PAGE_MARGIN + (width - 2 * PAGE_MARGIN - row_width) * 0.5 + slot * (BOX_W + BOX_GAP_X);
            double y0 = PAGE_MARGIN + row * (BOX_H + BOX_GAP_Y);
            cx[idx] = x0 + BOX_W * 0.5;
            box_y[idx] = y0;
        }
    }
    /* Edges first, behind node frames. */
    for (int i = 0; i < edges->count; i++) {
        int si = find_node_index_by_id(nodes, edges->data[i].src);
        int di = find_node_index_by_id(nodes, edges->data[i].dst);
        double x1, y1, x2, y2;
        if (si < 0 || di < 0 || si == di) continue;
        x1 = cx[si];
        y1 = box_y[si] + BOX_H + 2.0;
        x2 = cx[di];
        y2 = box_y[di] - 2.0;
        if (y2 <= y1) continue;
        printf("<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" stroke=\"#555555\" stroke-opacity=\"0.82\" stroke-width=\"1.8\" marker-end=\"url(#arrowhead)\"/>\n", x1, y1, x2, y2);
    }
    for (int row = 0; row < rows; row++) {
        double row_width = row_counts[row] * BOX_W + (row_counts[row] - 1) * BOX_GAP_X;
        for (int slot = 0; slot < row_counts[row]; slot++) {
            int idx = row_nodes[row_offsets[row] + slot];
            const GraphNode *node = &nodes->data[idx];
            double x0 = PAGE_MARGIN + (width - 2 * PAGE_MARGIN - row_width) * 0.5 + slot * (BOX_W + BOX_GAP_X);
            double y0 = PAGE_MARGIN + row * (BOX_H + BOX_GAP_Y);
            double minx, miny, maxx, maxy, bw, bh, scale, tx, ty;
            double shape_h = BOX_H - 70.0;
            printf("<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"8\" ry=\"8\" fill=\"#fafafa\" stroke=\"black\" stroke-width=\"1.2\"/>\n", x0, y0, BOX_W, BOX_H);
            shape_bbox(&node->aggregate, &minx, &miny, &maxx, &maxy);
            bw = maxx - minx; bh = maxy - miny;
            if (bw < 1e-9) bw = 1.0;
            if (bh < 1e-9) bh = 1.0;
            scale = (BOX_W - 2 * SHAPE_PAD < shape_h - 2 * SHAPE_PAD ? BOX_W - 2 * SHAPE_PAD : shape_h - 2 * SHAPE_PAD) / (bw > bh ? bw : bh);
            tx = x0 + (BOX_W - scale * bw) * 0.5 - scale * minx;
            ty = y0 + 46.0 + (shape_h - scale * bh) * 0.5 + scale * maxy;
            txs[idx] = tx; tys[idx] = ty; scales[idx] = scale;
            emit_shape_path(stdout, &node->aggregate, tx, ty, scale);
            emit_tile_paths(stdout, node, tx, ty, scale);
            {
                double hr = hidden_radius0(node);
                for (int h = 0; h < node->hidden_count; h++) {
                    DPoint q = vertex_to_xy(&node->aggregate, node->hidden[h]);
                    fprintf(stdout, "<circle cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\" fill=\"black\"/>\n", tx + scale * q.x, ty - scale * q.y, hr);
                }
            }
            draw_node_red_marks0(stdout, nodes, edges, idx, tx, ty, scale);
        }
    }
    printf("</svg>\n");
    free(levels); free(row_nodes); free(row_offsets); free(row_counts); free(order_in_row);
    free(cx); free(box_y); free(txs); free(tys); free(scales);
}

int main(int argc, char **argv) {
    const char *path = NULL;
    FILE *fp = stdin;
    NodeVec nodes = {0};
    EdgeVec edges = {0};
    if (argc > 2) {
        fprintf(stderr, "usage: %s [graph_dump.txt]\n", argv[0]);
        return 1;
    }
    if (argc == 2) {
        path = argv[1];
        fp = fopen(path, "r");
        if (!fp) {
            fprintf(stderr, "failed to open graph dump: %s\n", path);
            return 1;
        }
    }
    if (!parse_graph_file(fp, &nodes, &edges)) {
        fprintf(stderr, "failed to parse graph dump\n");
        if (fp != stdin) fclose(fp);
        free_nodes(&nodes);
        free(edges.data);
        return 1;
    }
    if (fp != stdin) fclose(fp);
    qsort(nodes.data, (size_t)nodes.count, sizeof(GraphNode), cmp_node_step_id);
    emit_svg(&nodes, &edges);
    free_nodes(&nodes);
    free(edges.data);
    return 0;
}
