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
#define BOX_H 165.0
#define BOX_GAP_X 36.0
#define BOX_GAP_Y 70.0
#define PAGE_MARGIN 28.0
#define SHAPE_PAD 8.0

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
    int highlighted;
    int discover_step;
    int division;
    int tile_count;
    int hidden_count;
    int prov_count;
    int prov[64];
    int tile_path_count;
    int tile_path_cap;
    int focus_count;
    int focus_cap;
    char kind[32];
    char key[MAX_BLOCK];
    VPoint *focus_ports;
    VPoint *hidden;
    Path *tiles;
    unsigned char *tile_reflected;
    Shape aggregate;
} GraphNode;

typedef struct {
    int src;
    int dst;
    int step;
    int sublevel;
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
static int find_node_index_by_id(const NodeVec *nodes, int id);
static int same_point0(VPoint a, VPoint b);

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

static int graphnode_add_focus0(GraphNode *n, VPoint port) {
    for (int i = 0; i < n->focus_count; i++) {
        if (same_point0(n->focus_ports[i], port)) return 1;
    }
    if (n->focus_count == n->focus_cap) {
        int nc = n->focus_cap ? 2 * n->focus_cap : 4;
        VPoint *next = (VPoint *)realloc(n->focus_ports, (size_t)nc * sizeof(VPoint));
        if (!next) return 0;
        n->focus_ports = next;
        n->focus_cap = nc;
    }
    n->focus_ports[n->focus_count++] = port;
    return 1;
}

static void parse_prov_numbers0(const char *s, GraphNode *n) {
    const char *p = s;
    n->prov_count = 0;
    while (*p && n->prov_count < 64) {
        if ((*p == '-') || isdigit((unsigned char)*p)) {
            char *end = NULL;
            long v = strtol(p, &end, 10);
            if (end != p) {
                n->prov[n->prov_count++] = (int)v;
                p = end;
                continue;
            }
        }
        p++;
    }
}

static void free_nodes(NodeVec *v) {
    for (int i = 0; i < v->count; i++) {
        free(v->data[i].focus_ports);
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
            n.division = -1;
            sscanf(line, "---node %d---", &n.id);
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "---end-node---", 14) == 0) break;
                if (strncmp(line, "kind:", 5) == 0) {
                    copy_trim(n.kind, sizeof(n.kind), line + 5, strlen(line + 5));
                } else if (strncmp(line, "discover_step:", 14) == 0) {
                    n.discover_step = atoi(line + 14);
                } else if (strncmp(line, "division:", 9) == 0) {
                    n.division = atoi(line + 9);
                } else if (strncmp(line, "distance:", 9) == 0) {
                    if (n.division < 0) n.division = atoi(line + 9);
                } else if (strncmp(line, "tile_count:", 11) == 0) {
                    n.tile_count = atoi(line + 11);
                } else if (strncmp(line, "hidden_count:", 13) == 0) {
                    n.hidden_count = atoi(line + 13);
                } else if (strncmp(line, "prov:", 5) == 0) {
                    parse_prov_numbers0(line + 5, &n);
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
        } else if (strncmp(line, "---node-update---", 17) == 0) {
            int id = -1;
            int discover_step = -1;
            int division = -1;
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "---end-node-update---", 21) == 0) break;
                if (strncmp(line, "id:", 3) == 0) {
                    id = atoi(line + 3);
                } else if (strncmp(line, "discover_step:", 14) == 0) {
                    discover_step = atoi(line + 14);
                } else if (strncmp(line, "division:", 9) == 0) {
                    division = atoi(line + 9);
                }
            }
            if (id >= 0) {
                for (int ni = 0; ni < nodes->count; ni++) {
                    if (nodes->data[ni].id == id) {
                        if (discover_step >= 0) nodes->data[ni].discover_step = discover_step;
                        if (division >= -1) nodes->data[ni].division = division;
                        break;
                    }
                }
            }
        } else if (strncmp(line, "---node-mark---", 15) == 0) {
            int id = -1;
            int highlight = 0;
            char mark[64] = {0};
            char status[128] = {0};
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "---end-node-mark---", 19) == 0) break;
                if (strncmp(line, "id:", 3) == 0) {
                    id = atoi(line + 3);
                } else if (strncmp(line, "mark:", 5) == 0) {
                    copy_trim(mark, sizeof(mark), line + 5, strlen(line + 5));
                } else if (strncmp(line, "status:", 7) == 0) {
                    copy_trim(status, sizeof(status), line + 7, strlen(line + 7));
                }
            }
            (void)status;
            if (strcmp(mark, "highlight") == 0 ||
                strcmp(mark, "escape-arrow") == 0) {
                highlight = 1;
            }
            if (id >= 0 && highlight) {
                int ni = find_node_index_by_id(nodes, id);
                if (ni >= 0) nodes->data[ni].highlighted = 1;
            }
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
                } else if (strncmp(line, "sublevel:", 9) == 0) {
                    e.sublevel = atoi(line + 9);
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
            int node_id = -1;
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "---end-focus---", 15) == 0) break;
                if (strncmp(line, "node:", 5) == 0) {
                    node_id = atoi(line + 5);
                } else if (strncmp(line, "key:", 4) == 0) {
                    copy_trim(key, sizeof(key), line + 4, strlen(line + 4));
                } else if (strncmp(line, "port:", 5) == 0) {
                    if (!parse_coord_triplet(line + 5, &port)) return 0;
                }
            }
            if (node_id >= 0) {
                int ni = find_node_index_by_id(nodes, node_id);
                if (ni >= 0 && !graphnode_add_focus0(&nodes->data[ni], port)) return 0;
            } else if (key[0]) {
                int ni = find_node_index_by_key(nodes, key);
                if (ni >= 0 && !graphnode_add_focus0(&nodes->data[ni], port)) return 0;
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
        const char *fill = "#ff4444";
        const char *stroke = "#7a0000";
        int suspicious = 0;
        if (p->n <= 0) continue;
        if (p->v[0].v == 6) {
            fill = "#eeeeee";
            stroke = "#666666";
        } else if (p->v[0].v == 4) {
            fill = "#9a9a9a";
            stroke = "#777777";
        }
        for (int i = 0; i < p->n; i++) {
            if (!(p->v[i].v == 3 || p->v[i].v == 4 || p->v[i].v == 6)) {
                suspicious = 1;
                break;
            }
        }
        if (suspicious) {
            fill = "#ff4444";
            stroke = "#7a0000";
        }
        fprintf(fp, "<path d=\"");
        for (int i = 0; i < p->n; i++) {
            DPoint q = vertex_to_xy(&node->aggregate, p->v[i]);
            fprintf(fp, "%c %.3f %.3f ", i ? 'L' : 'M', tx + scale * q.x, ty - scale * q.y);
        }
        fprintf(fp, "Z\" fill=\"%s\" fill-opacity=\"0.85\" stroke=\"%s\" stroke-width=\"0.7\"/>\n", fill, stroke);
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


static void build_division_row_layout0(const int *division_levels,
                                       const int *local_step,
                                       int n,
                                       int div_rows,
                                       int **row_nodes_out,
                                       int **row_offsets_out,
                                       int **row_counts_out) {
    int rows = div_rows + 1;
    int root_row = 0;
    int *row_counts = (int *)calloc((size_t)rows, sizeof(int));
    int *row_offsets = (int *)calloc((size_t)(rows + 1), sizeof(int));
    int *fill = (int *)calloc((size_t)rows, sizeof(int));
    int *row_nodes = NULL;
    if (!row_counts || !row_offsets || !fill) exit(1);

    for (int i = 0; i < n; i++) {
        int row = division_levels[i] < 0 ? root_row : division_levels[i] + 1;
        if (row < 0) row = 0;
        if (row >= rows) row = rows - 1;
        row_counts[row]++;
    }
    for (int r = 0; r < rows; r++) row_offsets[r + 1] = row_offsets[r] + row_counts[r];

    row_nodes = (int *)calloc((size_t)n, sizeof(int));
    if (!row_nodes) exit(1);
    for (int i = 0; i < n; i++) {
        int row = division_levels[i] < 0 ? root_row : division_levels[i] + 1;
        int pos;
        if (row < 0) row = 0;
        if (row >= rows) row = rows - 1;
        pos = row_offsets[row] + fill[row]++;
        row_nodes[pos] = i;
    }

    for (int r = 0; r < rows; r++) {
        int start = row_offsets[r];
        int count = row_counts[r];
        for (int a = 0; a < count; a++) {
            for (int b = a + 1; b < count; b++) {
                int ia = row_nodes[start + a];
                int ib = row_nodes[start + b];
                if (local_step[ib] < local_step[ia] ||
                    (local_step[ib] == local_step[ia] && ia > ib)) {
                    int tmp = row_nodes[start + a];
                    row_nodes[start + a] = row_nodes[start + b];
                    row_nodes[start + b] = tmp;
                }
            }
        }
    }

    free(fill);
    *row_nodes_out = row_nodes;
    *row_offsets_out = row_offsets;
    *row_counts_out = row_counts;
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

    for (int i = 0; i < node->focus_count; i++) {
        if (port_count < (int)(sizeof(ports) / sizeof(ports[0]))) {
            ports[port_count++] = node->focus_ports[i];
        }
    }

    if (port_count == 0) {
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

static void __attribute__((unused)) choose_primary_parents0(const NodeVec *nodes,
                                   const EdgeVec *edges,
                                   int *parent) {
    int n = nodes->count;
    for (int i = 0; i < n; i++) parent[i] = -1;
    for (int di = 0; di < n; di++) {
        int best_si = -1;
        if (nodes->data[di].discover_step <= 0) continue;
        for (int e = 0; e < edges->count; e++) {
            int si = find_node_index_by_id(nodes, edges->data[e].src);
            int dsti = find_node_index_by_id(nodes, edges->data[e].dst);
            if (si < 0 || dsti != di || si == di) continue;
            if (best_si < 0 ||
                nodes->data[si].discover_step > nodes->data[best_si].discover_step ||
                (nodes->data[si].discover_step == nodes->data[best_si].discover_step && nodes->data[si].id < nodes->data[best_si].id)) {
                best_si = si;
            }
        }
        parent[di] = best_si;
    }
}


static void build_primary_tree0(int n,
                                const int *parent,
                                int **child_offsets_out,
                                int **child_counts_out,
                                int **children_out) {
    int *child_counts = (int *)calloc((size_t)n, sizeof(int));
    int *child_offsets = (int *)calloc((size_t)(n + 1), sizeof(int));
    int *fill = (int *)calloc((size_t)n, sizeof(int));
    int *children = (int *)calloc((size_t)n, sizeof(int));
    if (!child_counts || !child_offsets || !fill || !children) exit(1);
    for (int i = 0; i < n; i++) if (parent[i] >= 0) child_counts[parent[i]]++;
    for (int i = 0; i < n; i++) child_offsets[i + 1] = child_offsets[i] + child_counts[i];
    for (int i = 0; i < n; i++) {
        if (parent[i] >= 0) {
            int p = parent[i];
            int pos = child_offsets[p] + fill[p]++;
            children[pos] = i;
        }
    }
    free(fill);
    *child_offsets_out = child_offsets;
    *child_counts_out = child_counts;
    *children_out = children;
}

static double compute_subtree_width0(int idx,
                                     const int *child_offsets,
                                     const int *child_counts,
                                     const int *children,
                                     double *subtree_width) {
    const double child_gap = BOX_GAP_X * 1.55;
    int count = child_counts[idx];
    if (count <= 0) {
        subtree_width[idx] = BOX_W;
        return subtree_width[idx];
    }
    double total = 0.0;
    for (int k = 0; k < count; k++) {
        int child = children[child_offsets[idx] + k];
        total += compute_subtree_width0(child, child_offsets, child_counts, children, subtree_width);
    }
    total += child_gap * (count - 1);
    if (total < BOX_W) total = BOX_W;
    subtree_width[idx] = total;
    return total;
}

static void assign_subtree_centers0(int idx,
                                    double left,
                                    const int *child_offsets,
                                    const int *child_counts,
                                    const int *children,
                                    const double *subtree_width,
                                    double *center_x) {
    const double child_gap = BOX_GAP_X * 1.55;
    int count = child_counts[idx];
    double width = subtree_width[idx];
    if (count <= 0) {
        center_x[idx] = left + width * 0.5;
        return;
    }
    double child_total = 0.0;
    for (int k = 0; k < count; k++) {
        int child = children[child_offsets[idx] + k];
        child_total += subtree_width[child];
    }
    child_total += child_gap * (count - 1);
    double cur = left + (width - child_total) * 0.5;
    for (int k = 0; k < count; k++) {
        int child = children[child_offsets[idx] + k];
        assign_subtree_centers0(child, cur, child_offsets, child_counts, children, subtree_width, center_x);
        cur += subtree_width[child] + child_gap;
    }
    {
        int first_child = children[child_offsets[idx]];
        int last_child = children[child_offsets[idx] + count - 1];
        center_x[idx] = 0.5 * (center_x[first_child] + center_x[last_child]);
    }
}



static int compute_subtree_count0(int idx,
                                  const int *child_offsets,
                                  const int *child_counts,
                                  const int *children,
                                  int *count_cache) {
    int total = 1;
    if (count_cache[idx] >= 0) return count_cache[idx];
    for (int k = 0; k < child_counts[idx]; k++) {
        int child = children[child_offsets[idx] + k];
        total += compute_subtree_count0(child, child_offsets, child_counts, children, count_cache);
    }
    count_cache[idx] = total;
    return total;
}

static int compute_subtree_maxdiv0(int idx,
                                   const int *child_offsets,
                                   const int *child_counts,
                                   const int *children,
                                   const int *division_levels,
                                   int *cache) {
    int best = division_levels[idx];
    if (cache[idx] >= 0) return cache[idx];
    for (int k = 0; k < child_counts[idx]; k++) {
        int child = children[child_offsets[idx] + k];
        int d = compute_subtree_maxdiv0(child, child_offsets, child_counts, children, division_levels, cache);
        if (d > best) best = d;
    }
    cache[idx] = best;
    return best;
}

static void sort_children_by_metric0(int *children,
                                     int start,
                                     int count,
                                     const int *metric_cache,
                                     const int *count_cache,
                                     const int *order_in_row) {
    for (int i = start; i < start + count; i++) {
        for (int j = i + 1; j < start + count; j++) {
            int ci = children[i], cj = children[j];
            if (metric_cache[cj] > metric_cache[ci] ||
                (metric_cache[cj] == metric_cache[ci] && count_cache[cj] > count_cache[ci]) ||
                (metric_cache[cj] == metric_cache[ci] && count_cache[cj] == count_cache[ci] && order_in_row[cj] < order_in_row[ci])) {
                int tmp = children[i];
                children[i] = children[j];
                children[j] = tmp;
            }
        }
    }
}

static void sort_tree_children_by_metric0(int n,
                                          const int *child_offsets,
                                          const int *child_counts,
                                          int *children,
                                          const int *metric_cache,
                                          const int *count_cache,
                                          const int *order_in_row) {
    for (int i = 0; i < n; i++) {
        if (child_counts[i] > 1) {
            sort_children_by_metric0(children, child_offsets[i], child_counts[i], metric_cache, count_cache, order_in_row);
        }
    }
}


static void filter_leaf_duplicate_boundary_nodes0(NodeVec *nodes, EdgeVec *edges) {
    int n = nodes->count;
    int m = edges->count;
    int *outdeg = (int *)calloc((size_t)n, sizeof(int));
    int *suppress = (int *)calloc((size_t)n, sizeof(int));
    if (!outdeg || !suppress) exit(1);

    for (int e = 0; e < m; e++) {
        int si = find_node_index_by_id(nodes, edges->data[e].src);
        int di = find_node_index_by_id(nodes, edges->data[e].dst);
        if (si >= 0 && di >= 0 && si != di) outdeg[si]++;
    }

    for (int i = 0; i < n; i++) {
        if (outdeg[i] != 0) continue;
        if (!nodes->data[i].key[0]) continue;
        for (int j = 0; j < n; j++) {
            if (i == j) continue;
            if (strcmp(nodes->data[i].key, nodes->data[j].key) != 0) continue;
            if (outdeg[j] > 0 || j < i) {
                suppress[i] = 1;
                break;
            }
        }
    }

    {
        GraphNode *new_nodes = (GraphNode *)calloc((size_t)n, sizeof(GraphNode));
        GraphEdge *new_edges = (GraphEdge *)calloc((size_t)m, sizeof(GraphEdge));
        int *keep_by_old = (int *)calloc((size_t)n, sizeof(int));
        int new_n = 0, new_m = 0;
        if (!new_nodes || !new_edges || !keep_by_old) exit(1);

        for (int i = 0; i < n; i++) {
            if (!suppress[i]) {
                keep_by_old[i] = 1;
                new_nodes[new_n++] = nodes->data[i];
            }
        }
        for (int e = 0; e < m; e++) {
            int si = find_node_index_by_id(nodes, edges->data[e].src);
            int di = find_node_index_by_id(nodes, edges->data[e].dst);
            if (si < 0 || di < 0) continue;
            if (!keep_by_old[si] || !keep_by_old[di]) continue;
            new_edges[new_m++] = edges->data[e];
        }

        free(nodes->data);
        nodes->data = new_nodes;
        nodes->count = new_n;
        nodes->cap = new_n;
        free(edges->data);
        edges->data = new_edges;
        edges->count = new_m;
        edges->cap = new_m;
        free(keep_by_old);
    }

    free(outdeg);
    free(suppress);
}

static const char *edge_color0(int sublevel, int use_color) {
    if (!use_color) return "#777777";
    switch (sublevel & 3) {
        case 1: return "#cc3333";
        case 2: return "#2f8f46";
        case 3: return "#3366cc";
        default: return "#777777";
    }
}

static const char *edge_marker0(int sublevel, int use_color) {
    if (!use_color) return "arrow-gray";
    switch (sublevel & 3) {
        case 1: return "arrow-red";
        case 2: return "arrow-green";
        case 3: return "arrow-blue";
        default: return "arrow-gray";
    }
}

static void __attribute__((unused)) compute_node_sublevels0(const NodeVec *nodes,
                                    const EdgeVec *edges,
                                    int *node_sublevels) {
    for (int i = 0; i < nodes->count; i++) node_sublevels[i] = 0;
    for (int e = 0; e < edges->count; e++) {
        int di = find_node_index_by_id(nodes, edges->data[e].dst);
        if (di < 0) continue;
        if (edges->data[e].sublevel > node_sublevels[di]) {
            node_sublevels[di] = edges->data[e].sublevel;
        }
    }
}


static void emit_svg(const NodeVec *nodes, const EdgeVec *edges, int use_color) {
    int n = nodes->count;
    int topo_rows;
    int div_rows = 0;
    int *levels = (int *)calloc((size_t)n, sizeof(int));
    int *division_levels = (int *)calloc((size_t)n, sizeof(int));
    int *local_step = (int *)calloc((size_t)n, sizeof(int));
    int *row_nodes = NULL;
    int *row_offsets = NULL;
    int *row_counts = NULL;
    int *draw_nodes = NULL;
    int *draw_offsets = NULL;
    int *draw_counts = NULL;
    int draw_rows = 0;
    int *order_in_row = NULL;
    int *primary_parent = (int *)calloc((size_t)n, sizeof(int));
    int *parent_edge = (int *)calloc((size_t)n, sizeof(int));
    int *root_of = (int *)calloc((size_t)n, sizeof(int));
    int *child_offsets = NULL;
    int *child_counts = NULL;
    int *children = NULL;
    double *center_x = (double *)calloc((size_t)n, sizeof(double));
    double *subtree_width = (double *)calloc((size_t)n, sizeof(double));
    double *cx = (double *)calloc((size_t)n, sizeof(double));
    double *box_y = (double *)calloc((size_t)n, sizeof(double));
    double *box_x = (double *)calloc((size_t)n, sizeof(double));
    int *rows_per_div = NULL;
    int *metric_cache = NULL;
    int *count_cache = NULL;
    double *divider_y = NULL;
    double *division_start = NULL;
    int *seed_for_prov = (int *)calloc(64, sizeof(int));
    int *process_order = (int *)calloc((size_t)n, sizeof(int));
    int *parent_ready = (int *)calloc((size_t)n, sizeof(int));
    double min_box_x = 0.0, max_box_x = 0.0;
    double width, height, dx;
    int first_box = 1;
    const double ROW_DY = BOX_H + 50.0;
    const double ROW_GAP = ROW_DY - BOX_H;
    const double DIV_GAP = 30.0;
    const double ROOT_GAP = ROW_GAP;
    const double DIV_TOP_GAP = ROW_GAP;
    const double ROOT_GAP_X = BOX_GAP_X * 1.05;
    const double LABEL_FONT = 72.0;

    if (!levels || !division_levels || !local_step || !primary_parent || !parent_edge || !root_of ||
        !center_x || !subtree_width || !cx || !box_y || !box_x || !seed_for_prov || !process_order || !parent_ready) exit(1);

    if (!compute_vertex_strata(nodes, edges, levels, &topo_rows)) exit(1);
    build_row_layout0(nodes, edges, levels, topo_rows, &row_nodes, &row_offsets, &row_counts, &order_in_row);

    for (int i = 0; i < 64; i++) seed_for_prov[i] = -1;
    for (int i = 0; i < n; i++) {
        primary_parent[i] = -1;
        parent_edge[i] = -1;
        root_of[i] = i;
        local_step[i] = 0;
        if (nodes->data[i].discover_step < 0 || strcmp(nodes->data[i].kind, "seed") == 0) {
            division_levels[i] = -1;
            if (nodes->data[i].prov_count == 1) {
                int p0 = nodes->data[i].prov[0];
                if (p0 >= 0 && p0 < 64 && seed_for_prov[p0] < 0) seed_for_prov[p0] = i;
            }
        } else {
            int d = nodes->data[i].division;
            if (d < 0) d = nodes->data[i].discover_step > 0 ? nodes->data[i].discover_step - 1 : 0;
            if (d < 0) d = 0;
            division_levels[i] = d;
            if (d + 1 > div_rows) div_rows = d + 1;
        }
        process_order[i] = i;
    }
    if (div_rows <= 0) div_rows = 1;

    for (int a = 0; a < n; a++) {
        for (int b = a + 1; b < n; b++) {
            int ia = process_order[a], ib = process_order[b];
            int da = nodes->data[ia].discover_step;
            int db = nodes->data[ib].discover_step;
            if (db < da || (db == da && nodes->data[ib].id < nodes->data[ia].id)) {
                int tmp = process_order[a];
                process_order[a] = process_order[b];
                process_order[b] = tmp;
            }
        }
    }

    for (int oi = 0; oi < n; oi++) {
        int di = process_order[oi];
        int best_si = -1;
        int best_edge = -1;
        int best_same = -1;
        int target_root = -1;
        if (division_levels[di] < 0) {
            primary_parent[di] = -1;
            root_of[di] = di;
            parent_ready[di] = 1;
            continue;
        }
        if (nodes->data[di].prov_count == 1) {
            int p0 = nodes->data[di].prov[0];
            if (p0 >= 0 && p0 < 64) target_root = seed_for_prov[p0];
        }
        for (int e = 0; e < edges->count; e++) {
            int si = find_node_index_by_id(nodes, edges->data[e].src);
            int dsti = find_node_index_by_id(nodes, edges->data[e].dst);
            int same = 0;
            if (si < 0 || dsti != di || si == di) continue;
            /* Parentage must come from raw graph edges.  Node discover_step is
               mutable via node-update records, so an edge emitted from an
               earlier state can appear to connect equal-step nodes after later
               updates.  Use process-order readiness to keep the primary tree
               acyclic instead of rejecting valid raw incoming edges. */
            if (!parent_ready[si]) continue;
            if (target_root >= 0 && root_of[si] == target_root) same = 1;
            if (best_si < 0 ||
                same > best_same ||
                (same == best_same && nodes->data[si].discover_step > nodes->data[best_si].discover_step) ||
                (same == best_same && nodes->data[si].discover_step == nodes->data[best_si].discover_step && nodes->data[si].id < nodes->data[best_si].id)) {
                best_si = si;
                best_edge = e;
                best_same = same;
            }
        }
        primary_parent[di] = best_si;
        parent_edge[di] = best_edge;
        root_of[di] = (best_si >= 0) ? root_of[best_si] : di;
        parent_ready[di] = 1;
    }

    for (int oi = 0; oi < n; oi++) {
        int i = process_order[oi];
        int p = primary_parent[i];
        if (division_levels[i] < 0) continue;
        if (p >= 0 && division_levels[p] > division_levels[i]) {
            division_levels[i] = division_levels[p];
        }
        if (division_levels[i] + 1 > div_rows) div_rows = division_levels[i] + 1;
    }

    build_primary_tree0(n, primary_parent, &child_offsets, &child_counts, &children);

    rows_per_div = (int *)calloc((size_t)div_rows, sizeof(int));
    metric_cache = (int *)calloc((size_t)n, sizeof(int));
    count_cache = (int *)calloc((size_t)n, sizeof(int));
    divider_y = (double *)calloc((size_t)(div_rows + 1), sizeof(double));
    division_start = (double *)calloc((size_t)div_rows, sizeof(double));
    if (!rows_per_div || !metric_cache || !count_cache || !divider_y || !division_start) exit(1);
    for (int i = 0; i < n; i++) { metric_cache[i] = -1; count_cache[i] = -1; }

    for (int i = 0; i < n; i++) {
        if (primary_parent[i] < 0) {
            compute_subtree_maxdiv0(i, child_offsets, child_counts, children, division_levels, metric_cache);
            compute_subtree_count0(i, child_offsets, child_counts, children, count_cache);
        }
    }
    sort_tree_children_by_metric0(n, child_offsets, child_counts, children, metric_cache, count_cache, order_in_row);

    {
        int *roots = (int *)calloc((size_t)n, sizeof(int));
        int root_count = 0;
        double cur_left = 0.0;
        if (!roots) exit(1);
        for (int i = 0; i < n; i++) if (primary_parent[i] < 0) roots[root_count++] = i;
        for (int a = 0; a < root_count; a++) {
            for (int b = a + 1; b < root_count; b++) {
                int ia = roots[a], ib = roots[b];
                if (metric_cache[ib] < metric_cache[ia] ||
                    (metric_cache[ib] == metric_cache[ia] && count_cache[ib] < count_cache[ia]) ||
                    (metric_cache[ib] == metric_cache[ia] && count_cache[ib] == count_cache[ia] && order_in_row[ib] < order_in_row[ia])) {
                    int tmp = roots[a];
                    roots[a] = roots[b];
                    roots[b] = tmp;
                }
            }
        }
        /*
           Optimized multi-seed layout needs one ordering exception:
           after normal metric sorting, place the first root component last.
           This is a renderer-only translation/order hack; it does not alter
           graph nodes, edges, or divisions.
        */
        if (root_count == 5) {
            int first_root = roots[0];
            for (int r = 1; r < root_count; r++) roots[r - 1] = roots[r];
            roots[root_count - 1] = first_root;
        }
        for (int r = 0; r < root_count; r++) {
            int idx = roots[r];
            compute_subtree_width0(idx, child_offsets, child_counts, children, subtree_width);
            if (r > 0) cur_left += ROOT_GAP_X;
            assign_subtree_centers0(idx, cur_left, child_offsets, child_counts, children, subtree_width, center_x);
            cur_left += subtree_width[idx];
        }
        free(roots);
    }

    for (int oi = 0; oi < n; oi++) {
        int i = process_order[oi];
        int p = primary_parent[i];
        if (division_levels[i] < 0) continue;
        if (p < 0 || root_of[p] != root_of[i] || division_levels[p] != division_levels[i]) local_step[i] = 0;
        else local_step[i] = local_step[p] + 1;
        if (local_step[i] + 1 > rows_per_div[division_levels[i]]) rows_per_div[division_levels[i]] = local_step[i] + 1;
    }
    for (int d = 0; d < div_rows; d++) if (rows_per_div[d] <= 0) rows_per_div[d] = 1;

    divider_y[0] = PAGE_MARGIN + BOX_H + ROOT_GAP;
    for (int d = 0; d < div_rows; d++) {
        double div_height = BOX_H + (rows_per_div[d] - 1) * ROW_DY;
        division_start[d] = divider_y[d] + DIV_TOP_GAP;
        divider_y[d + 1] = division_start[d] + div_height + DIV_GAP;
    }

    for (int i = 0; i < n; i++) {
        double y0;
        if (division_levels[i] < 0) y0 = divider_y[0] - BOX_H - ROOT_GAP;
        else y0 = division_start[division_levels[i]] + local_step[i] * ROW_DY;
        box_x[i] = center_x[i] - BOX_W * 0.5;
        box_y[i] = y0;
        cx[i] = center_x[i];
        if (first_box) {
            min_box_x = box_x[i];
            max_box_x = box_x[i] + BOX_W;
            first_box = 0;
        } else {
            if (box_x[i] < min_box_x) min_box_x = box_x[i];
            if (box_x[i] + BOX_W > max_box_x) max_box_x = box_x[i] + BOX_W;
        }
    }

    draw_rows = div_rows + 1;
    build_division_row_layout0(division_levels, local_step, n, div_rows, &draw_nodes, &draw_offsets, &draw_counts);

    {
        double content_width = max_box_x - min_box_x;
        double pad_x = content_width / 18.0;
        double pad_y = divider_y[div_rows] / 18.0;
        double extra_y;
        if (pad_x < PAGE_MARGIN) pad_x = PAGE_MARGIN;
        if (pad_y < PAGE_MARGIN) pad_y = PAGE_MARGIN;

        dx = pad_x - min_box_x;
        for (int i = 0; i < n; i++) {
            box_x[i] += dx;
            cx[i] += dx;
        }

        extra_y = pad_y - PAGE_MARGIN;
        if (extra_y != 0.0) {
            for (int i = 0; i < n; i++) box_y[i] += extra_y;
            for (int d = 0; d <= div_rows; d++) divider_y[d] += extra_y;
        }

        width = content_width + 2.0 * pad_x;
        if (width < 2.0 * pad_x + BOX_W) width = 2.0 * pad_x + BOX_W;
        height = divider_y[div_rows] + pad_y;
    }

    printf("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" viewBox=\"0 0 %.0f %.0f\">\n", width, height, width, height);
    printf("<defs>\n");
    printf("<marker id=\"arrow-gray\" markerWidth=\"36\" markerHeight=\"36\" refX=\"30\" refY=\"18\" orient=\"auto\" markerUnits=\"userSpaceOnUse\"><path d=\"M 0 0 L 36 18 L 0 36 z\" fill=\"#777777\"/></marker>\n");
    printf("<marker id=\"arrow-red\" markerWidth=\"36\" markerHeight=\"36\" refX=\"30\" refY=\"18\" orient=\"auto\" markerUnits=\"userSpaceOnUse\"><path d=\"M 0 0 L 36 18 L 0 36 z\" fill=\"#cc3333\"/></marker>\n");
    printf("<marker id=\"arrow-green\" markerWidth=\"36\" markerHeight=\"36\" refX=\"30\" refY=\"18\" orient=\"auto\" markerUnits=\"userSpaceOnUse\"><path d=\"M 0 0 L 36 18 L 0 36 z\" fill=\"#2f8f46\"/></marker>\n");
    printf("<marker id=\"arrow-blue\" markerWidth=\"36\" markerHeight=\"36\" refX=\"30\" refY=\"18\" orient=\"auto\" markerUnits=\"userSpaceOnUse\"><path d=\"M 0 0 L 36 18 L 0 36 z\" fill=\"#3366cc\"/></marker>\n");
    printf("</defs>\n");
    printf("<rect x=\"0\" y=\"0\" width=\"%.0f\" height=\"%.0f\" fill=\"white\"/>\n", width, height);

    for (int d = 0; d <= div_rows; d++) {
        const char *div_color = "#b8b8b8";
        const char *text_color = "#666666";
        double yline = divider_y[d];
        if (use_color && d == 4) { div_color = "#cc3333"; text_color = "#cc3333"; }
        else if (use_color && d == 5) { div_color = "#2f8f46"; text_color = "#2f8f46"; }
        else if (use_color && d == 6) { div_color = "#3366cc"; text_color = "#3366cc"; }
        else if (use_color && d == 10) { div_color = "#7a3db8"; text_color = "#7a3db8"; }
        printf("<line x1=\"0\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"%s\" stroke-width=\"2.2\"/>\n", yline, width, yline, div_color);
        printf("<text x=\"18\" y=\"%.1f\" font-size=\"%.0f\" font-family=\"monospace\" font-weight=\"bold\" fill=\"%s\">%d</text>\n", yline - 8.0, LABEL_FONT, text_color, d);
    }

    for (int i = 0; i < n; i++) {
        int p = primary_parent[i];
        int e = parent_edge[i];
        double x1, y1, x2, y2;
        if (p < 0 || e < 0) continue;
        x1 = cx[p];
        y1 = box_y[p] + BOX_H + 2.0;
        x2 = cx[i];
        y2 = box_y[i] - 2.0;
        if (y2 <= y1) continue;
        printf("<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\" stroke=\"%s\" stroke-opacity=\"0.88\" stroke-width=\"7.0\" marker-end=\"url(#%s)\"/>\n",
               x1, y1, x2, y2, edge_color0(edges->data[e].sublevel, use_color), edge_marker0(edges->data[e].sublevel, use_color));
    }

    for (int row = 0; row < draw_rows; row++) {
        for (int slot = 0; slot < draw_counts[row]; slot++) {
            int idx = draw_nodes[draw_offsets[row] + slot];
            const GraphNode *node = &nodes->data[idx];
            double x0 = box_x[idx];
            double y0 = box_y[idx];
            double minx, miny, maxx, maxy, bw, bh, scale, tx, ty;
            double shape_h = BOX_H - 18.0;
            const char *frame_fill = node->highlighted ? "#ffd7d7" : "#fafafa";
            const char *frame_stroke = node->highlighted ? "#cc0000" : "black";
            double frame_stroke_width = node->highlighted ? 4.0 : 1.2;
            printf("<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" rx=\"8\" ry=\"8\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%.1f\"/>\n", x0, y0, BOX_W, BOX_H, frame_fill, frame_stroke, frame_stroke_width);
            shape_bbox(&node->aggregate, &minx, &miny, &maxx, &maxy);
            bw = maxx - minx; bh = maxy - miny;
            if (bw < 1e-9) bw = 1.0;
            if (bh < 1e-9) bh = 1.0;
            scale = (BOX_W - 2 * SHAPE_PAD < shape_h - 2 * SHAPE_PAD ? BOX_W - 2 * SHAPE_PAD : shape_h - 2 * SHAPE_PAD) / (bw > bh ? bw : bh);
            tx = x0 + (BOX_W - scale * bw) * 0.5 - scale * minx;
            ty = y0 + 9.0 + (shape_h - scale * bh) * 0.5 + scale * maxy;
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

    free(levels); free(division_levels); free(local_step); free(row_nodes); free(row_offsets); free(row_counts); free(draw_nodes); free(draw_offsets); free(draw_counts); free(order_in_row);
    free(primary_parent); free(parent_edge); free(root_of); free(child_offsets); free(child_counts); free(children);
    free(center_x); free(subtree_width); free(cx); free(box_y); free(box_x);
    free(rows_per_div); free(metric_cache); free(count_cache); free(divider_y); free(division_start);
    free(parent_ready);
    free(seed_for_prov); free(process_order);
}


static int parse_highlight_nodes0(const char *arg, int **ids_out, int *count_out) {
    size_t arg_len = strlen(arg);
    char *copy = (char *)malloc(arg_len + 1);
    char *tok;
    if (copy) memcpy(copy, arg, arg_len + 1);
    int cap = 16;
    int count = 0;
    int *ids = (int *)calloc((size_t)cap, sizeof(int));
    if (!copy || !ids) {
        free(copy);
        free(ids);
        return 0;
    }
    for (tok = strtok(copy, ","); tok; tok = strtok(NULL, ",")) {
        char *end = NULL;
        long v = strtol(tok, &end, 10);
        while (end && isspace((unsigned char)*end)) end++;
        if (!end || *end != '\0') {
            free(copy);
            free(ids);
            return 0;
        }
        if (count == cap) {
            int nc = 2 * cap;
            int *next = (int *)realloc(ids, (size_t)nc * sizeof(int));
            if (!next) {
                free(copy);
                free(ids);
                return 0;
            }
            ids = next;
            cap = nc;
        }
        ids[count++] = (int)v;
    }
    free(copy);
    *ids_out = ids;
    *count_out = count;
    return 1;
}

static void mark_highlight_nodes0(NodeVec *nodes, const int *ids, int count) {
    for (int j = 0; j < count; j++) {
        for (int i = 0; i < nodes->count; i++) {
            if (nodes->data[i].id == ids[j]) nodes->data[i].highlighted = 1;
        }
    }
}

int main(int argc, char **argv) {
    const char *path = NULL;
    FILE *fp = stdin;
    NodeVec nodes = {0};
    EdgeVec edges = {0};
    int use_color = 0;
    int *highlight_ids = NULL;
    int highlight_count = 0;
    for (int ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "--color") == 0 || strcmp(argv[ai], "--colors") == 0) {
            use_color = 1;
        } else if ((strcmp(argv[ai], "--highlight") == 0 || strcmp(argv[ai], "--red-nodes") == 0) && ai + 1 < argc) {
            free(highlight_ids);
            highlight_ids = NULL;
            highlight_count = 0;
            if (!parse_highlight_nodes0(argv[++ai], &highlight_ids, &highlight_count)) {
                fprintf(stderr, "bad highlight node list: %s\n", argv[ai]);
                return 1;
            }
        } else if (!path) {
            path = argv[ai];
        } else {
            fprintf(stderr, "usage: %s [--color] [--highlight ids] [graph_dump.txt]\n", argv[0]);
            free(highlight_ids);
            return 1;
        }
    }
    if (path) {
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
    filter_leaf_duplicate_boundary_nodes0(&nodes, &edges);
    mark_highlight_nodes0(&nodes, highlight_ids, highlight_count);
    emit_svg(&nodes, &edges, use_color);
    free_nodes(&nodes);
    free(edges.data);
    free(highlight_ids);
    return 0;
}
