#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ROWS 128
#define MAX_TOK 32
#define MAX_LINE 4096
#define MAX_EDGES 512
#define MAX_RULES 4096
#define NTILES 8
#define SIDES 6

typedef struct { char t[SIDES][MAX_TOK]; } Row;
typedef struct { char label[8]; Row raw; } LRow;
typedef struct { char label[8]; Row raw; } BRow;
typedef struct { char a[MAX_TOK], b[MAX_TOK]; } Edge;
typedef struct { Edge little; Edge big; char id[8]; } DoubleEdge;
typedef struct { int a, b; } RuleIdx;
typedef struct {
    char tid[8];
    char llabels[32];
    char blabel[8];
    Row little;
    Row big;
    Row little_reduced;
    Row big_reduced;
    int edge_ids[SIDES];
} Tile;

static void copy_tok(char dst[MAX_TOK], const char *src){
    const char *p = src ? src : "";
    int i = 0;
    while(i < MAX_TOK - 1 && p[i]){
        dst[i] = p[i];
        i++;
    }
    dst[i] = '\0';
}

static void trim(char *s){
    char *p = s;
    size_t n;
    while(*p && isspace((unsigned char)*p)) p++;
    if(p != s) memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while(n && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

static int read_file(const char *path, char **out){
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;
    if(!f) return 0;
    if(fseek(f, 0, SEEK_END) != 0){ fclose(f); return 0; }
    n = ftell(f);
    if(n < 0){ fclose(f); return 0; }
    if(fseek(f, 0, SEEK_SET) != 0){ fclose(f); return 0; }
    buf = malloc((size_t)n + 1);
    if(!buf){ fclose(f); return 0; }
    if(fread(buf, 1, (size_t)n, f) != (size_t)n){ free(buf); fclose(f); return 0; }
    buf[n] = '\0';
    fclose(f);
    *out = buf;
    return 1;
}

static int tokenize(char *s, char tok[][MAX_TOK], int cap){
    int n = 0;
    char *p = strtok(s, " \t\r\n");
    while(p && n < cap){
        copy_tok(tok[n++], p);
        p = strtok(NULL, " \t\r\n");
    }
    return n;
}

static void parse_little_hex_line(const char *s, Row *r){
    char tmp[MAX_LINE];
    char tok[16][MAX_TOK];
    int n;
    snprintf(tmp, sizeof(tmp), "%s", s);
    char *hash = strchr(tmp, '#');
    if(hash) *hash = '\0';
    trim(tmp);
    n = tokenize(tmp, tok, 16);
    for(int i=0;i<SIDES;i++) copy_tok(r->t[i], i < n ? tok[i] : ".");
}

static void parse_big_hex_line(const char *s, Row *r){
    int n = 0;
    const char *p = s;
    while(*p && n < SIDES){
        if(*p == 'v' && isdigit((unsigned char)p[1])){
            int j = 0;
            char buf[MAX_TOK];
            buf[j++] = 'v';
            p++;
            while(isdigit((unsigned char)*p) && j < MAX_TOK - 1) buf[j++] = *p++;
            buf[j] = '\0';
            copy_tok(r->t[n++], buf);
        } else {
            p++;
        }
    }
    for(int i=n;i<SIDES;i++) copy_tok(r->t[i], ".");
}

static int has_six_little_tokens(const char *s){
    char tmp[MAX_LINE];
    char tok[16][MAX_TOK];
    snprintf(tmp, sizeof(tmp), "%s", s);
    char *hash = strchr(tmp, '#');
    if(hash) *hash = '\0';
    trim(tmp);
    if(!*tmp) return 0;
    return tokenize(tmp, tok, 16) >= SIDES;
}

static int parse_little_rows(const char *text, LRow rows[], int cap){
    char *copy = strdup(text);
    char *save = NULL;
    int in = 0, n = 0;
    if(!copy) return 0;
    for(char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)){
        char tmp[MAX_LINE];
        snprintf(tmp, sizeof(tmp), "%s", line);
        trim(tmp);
        if(strcmp(tmp, "# Unique hexagons") == 0){ in = 1; continue; }
        if(in && strcmp(tmp, "# Edge Matches") == 0) break;
        if(!in || tmp[0] == '#' || !has_six_little_tokens(tmp)) continue;
        if(n >= cap){ free(copy); return 0; }
        snprintf(rows[n].label, sizeof(rows[n].label), "L%02d", n + 1);
        parse_little_hex_line(tmp, &rows[n].raw);
        n++;
    }
    free(copy);
    return n;
}

static int parse_big_rows(const char *text, BRow rows[], int cap){
    char *copy = strdup(text);
    char *save = NULL;
    int in = 0, n = 0;
    if(!copy) return 0;
    for(char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)){
        char tmp[MAX_LINE];
        snprintf(tmp, sizeof(tmp), "%s", line);
        trim(tmp);
        if(strcmp(tmp, "Unique Supertile Hexagons") == 0){ in = 1; continue; }
        if(in && strncmp(tmp, "Vertex QA", 9) == 0) break;
        if(!in || !isdigit((unsigned char)tmp[0])) continue;
        if(n >= cap){ free(copy); return 0; }
        int id = atoi(tmp);
        snprintf(rows[n].label, sizeof(rows[n].label), "B%02d", id);
        parse_big_hex_line(tmp, &rows[n].raw);
        n++;
    }
    free(copy);
    return n;
}

static const char *little_sym(const char *s){
    if(strcmp(s, "3") == 0 || strcmp(s, "5") == 0) return "z";
    return s;
}

static const char *big_sym(const char *s){
    if(strcmp(s, "v3") == 0 || strcmp(s, "v5") == 0) return "w";
    if(strcmp(s, "v9") == 0 || strcmp(s, "v21") == 0) return "x";
    if(strcmp(s, "v31") == 0 || strcmp(s, "v41") == 0) return "y";
    return s;
}

static void reduce_little_row(const Row *in, Row *out){
    for(int i=0;i<SIDES;i++) copy_tok(out->t[i], little_sym(in->t[i]));
}

static void reduce_big_row(const Row *in, Row *out){
    for(int i=0;i<SIDES;i++) copy_tok(out->t[i], big_sym(in->t[i]));
}

static int edge_eq(const Edge *a, const Edge *b){
    return strcmp(a->a, b->a) == 0 && strcmp(a->b, b->b) == 0;
}

static int add_double_edge(DoubleEdge edges[], int *n, Edge little, Edge big){
    for(int i=0;i<*n;i++){
        if(edge_eq(&edges[i].little, &little)) return i;
    }
    if(*n >= MAX_EDGES) return -1;
    edges[*n].little = little;
    edges[*n].big = big;
    snprintf(edges[*n].id, sizeof(edges[*n].id), "E%02d", *n + 1);
    (*n)++;
    return *n - 1;
}

static int find_edge_by_little(DoubleEdge edges[], int n, Edge e){
    for(int i=0;i<n;i++) if(edge_eq(&edges[i].little, &e)) return i;
    return -1;
}

static int find_edge_by_big(DoubleEdge edges[], int n, Edge e){
    for(int i=0;i<n;i++) if(edge_eq(&edges[i].big, &e)) return i;
    return -1;
}

static int find_lrow(LRow rows[], int n, const char *label){
    for(int i=0;i<n;i++) if(strcmp(rows[i].label, label) == 0) return i;
    return -1;
}

static int find_brow(BRow rows[], int n, const char *label){
    for(int i=0;i<n;i++) if(strcmp(rows[i].label, label) == 0) return i;
    return -1;
}

static void print_row(FILE *f, const Row *r){
    for(int i=0;i<SIDES;i++) fprintf(f, "%s%s", i ? " " : "", r->t[i]);
}

static int tok_rank(const char *s){
    if(strcmp(s, "z") == 0) return 1000;
    if(strcmp(s, "w") == 0) return 1001;
    if(strcmp(s, "x") == 0) return 1002;
    if(strcmp(s, "y") == 0) return 1003;
    if(s[0] == 'v' && isdigit((unsigned char)s[1])) return atoi(s + 1);
    if(isdigit((unsigned char)s[0])) return atoi(s);
    if(strcmp(s, "a") == 0) return 1004;
    if(strcmp(s, "b") == 0) return 1005;
    return 2000;
}

static int edge_order(const void *pa, const void *pb){
    const DoubleEdge *a = (const DoubleEdge*)pa;
    const DoubleEdge *b = (const DoubleEdge*)pb;
    int c = tok_rank(a->little.a) - tok_rank(b->little.a);
    if(c) return c;
    c = tok_rank(a->little.b) - tok_rank(b->little.b);
    if(c) return c;
    c = strcmp(a->big.a, b->big.a);
    if(c) return c;
    return strcmp(a->big.b, b->big.b);
}

static int parse_edge_pair_line(const char *s, Edge *a, Edge *b, int is_big){
    const char *p = s;
    char vals[4][MAX_TOK];
    int n = 0;
    while(*p && n < 4){
        if(*p == '('){
            char left[MAX_TOK], right[MAX_TOK];
            int li = 0, ri = 0;
            p++;
            while(*p && *p != ',' && *p != ')' && li < MAX_TOK - 1) left[li++] = *p++;
            left[li] = '\0';
            if(*p == ',') p++;
            while(*p && *p != ')' && ri < MAX_TOK - 1) right[ri++] = *p++;
            right[ri] = '\0';
            if(*p == ')') p++;
            trim(left); trim(right);
            copy_tok(vals[n++], is_big ? big_sym(left) : little_sym(left));
            copy_tok(vals[n++], is_big ? big_sym(right) : little_sym(right));
        } else {
            p++;
        }
    }
    if(n != 4) return 0;
    copy_tok(a->a, vals[0]); copy_tok(a->b, vals[1]);
    copy_tok(b->a, vals[2]); copy_tok(b->b, vals[3]);
    return 1;
}

static int load_rules(const char *text, int is_big, DoubleEdge edges[], int ne, RuleIdx rules[], int cap){
    char *copy = strdup(text);
    char *save = NULL;
    int in = 0, n = 0;
    if(!copy) return 0;
    for(char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)){
        char tmp[MAX_LINE];
        Edge e1, e2;
        int a, b;
        snprintf(tmp, sizeof(tmp), "%s", line);
        trim(tmp);
        if(!is_big && strcmp(tmp, "# Edge Matches") == 0){ in = 1; continue; }
        if(is_big && strcmp(tmp, "Downmapped Super Hexagon Edge Matches") == 0){ in = 1; continue; }
        if(in && (strncmp(tmp, "# observations", 14) == 0 || strncmp(tmp, "Summary", 7) == 0)) break;
        if(!in || strstr(tmp, "+e(") == NULL || strstr(tmp, "-e(") == NULL) continue;
        if(!parse_edge_pair_line(tmp, &e1, &e2, is_big)) continue;
        a = is_big ? find_edge_by_big(edges, ne, e1) : find_edge_by_little(edges, ne, e1);
        b = is_big ? find_edge_by_big(edges, ne, e2) : find_edge_by_little(edges, ne, e2);
        if(a < 0 || b < 0) continue;
        if(n < cap){ rules[n].a = a; rules[n].b = b; n++; }
    }
    free(copy);
    return n;
}

static int rule_seen(const RuleIdx rules[], int n, RuleIdx r){
    for(int i=0;i<n;i++){
        if((rules[i].a == r.a && rules[i].b == r.b) ||
           (rules[i].a == r.b && rules[i].b == r.a)) return 1;
    }
    return 0;
}

static int unique_rules(RuleIdx in[], int n, RuleIdx out[], int cap){
    int m = 0;
    for(int i=0;i<n;i++){
        if(!rule_seen(out, m, in[i]) && m < cap) out[m++] = in[i];
    }
    return m;
}

static void emit_save(FILE *out, const Tile tiles[], int nt, DoubleEdge edges[], int ne,
                      const RuleIdx little_rules[], int nr_little,
                      const RuleIdx big_rules[], int nr_big){
    int common = 0, little_only = 0, big_only = 0;
    fprintf(out, "# RL5 save-state: reduced 8-hex system\n");
    fprintf(out, "# Generated by rl5_refine from hexagons.dat and supertile_hexagons.dat.\n\n");

    fprintf(out, "NOTATION\n");
    fprintf(out, "  little: 3,5 -> z\n");
    fprintf(out, "  big:    v3,v5 -> w\n");
    fprintf(out, "  big:    v9,v21 -> x\n");
    fprintf(out, "  big:    v31,v41 -> y\n\n");

    fprintf(out, "INDEX MAP\n");
    for(int i=0;i<nt;i++){
        fprintf(out, "  %-3s  little %-8s  -> big %s\n", tiles[i].tid, tiles[i].llabels, tiles[i].blabel);
    }
    fprintf(out, "\n");

    fprintf(out, "REDUCED HEXAGONS\n");
    for(int i=0;i<nt;i++){
        fprintf(out, "  %s little ", tiles[i].tid); print_row(out, &tiles[i].little_reduced);
        fprintf(out, "   big "); print_row(out, &tiles[i].big_reduced);
        fprintf(out, "   edges");
        for(int k=0;k<SIDES;k++) fprintf(out, " %s", edges[tiles[i].edge_ids[k]].id);
        fprintf(out, "\n");
    }
    fprintf(out, "\n");

    (void)edge_order;
    fprintf(out, "EDGE MAP\n");
    for(int i=0;i<ne;i++){
        fprintf(out, "  %-3s  little e(%s,%s)  ->  big e(%s,%s)\n",
                edges[i].id, edges[i].little.a, edges[i].little.b, edges[i].big.a, edges[i].big.b);
    }
    fprintf(out, "\n");

    fprintf(out, "REDUCED EDGE RULES\n");
    fprintf(out, "  little rules: %d\n", nr_little);
    fprintf(out, "  big rules:    %d\n", nr_big);
    for(int i=0;i<nr_little;i++){
        if(rule_seen(big_rules, nr_big, little_rules[i])) common++;
        else little_only++;
    }
    for(int i=0;i<nr_big;i++) if(!rule_seen(little_rules, nr_little, big_rules[i])) big_only++;
    fprintf(out, "  common:       %d\n", common);
    fprintf(out, "  little_only:  %d\n", little_only);
    fprintf(out, "  big_only:     %d\n\n", big_only);

    fprintf(out, "LITTLE RULE IDS\n");
    for(int i=0;i<nr_little;i++) fprintf(out, "  E%02d = E%02d\n", little_rules[i].a + 1, little_rules[i].b + 1);
    fprintf(out, "\nBIG RULE IDS\n");
    for(int i=0;i<nr_big;i++) fprintf(out, "  E%02d = E%02d\n", big_rules[i].a + 1, big_rules[i].b + 1);
}

int main(int argc, char **argv){
    const char *little_path = "data/rl5/hexagons.dat";
    const char *big_path = "data/rl5/supertile_hexagons.dat";
    const char *save_path = "data/rl5/reduced_8_hexes.dat";
    char *little_text = NULL, *big_text = NULL;
    LRow lrows[MAX_ROWS]; BRow brows[MAX_ROWS];
    Tile tiles[NTILES];
    DoubleEdge edges[MAX_EDGES]; int ne = 0;
    RuleIdx lr0[MAX_RULES], br0[MAX_RULES], lr[MAX_RULES], br[MAX_RULES];
    int nr_l0, nr_b0, nr_l, nr_b;
    const char *tid[NTILES]     = {"T01","T02","T03","T04","T05","T06","T07","T08"};
    const char *llabels[NTILES] = {"L01","L02","L03","L04","L06","L07,L09","L08,L10","L05"};
    const char *lbase[NTILES]   = {"L01","L02","L03","L04","L06","L07","L08","L05"};
    const char *blabels[NTILES] = {"B04","B02","B07","B01","B05","B03","B08","B09"};

    if(argc > 4){
        fprintf(stderr, "usage: %s [hexagons.dat supertile_hexagons.dat [reduced_8_hexes.dat]]\n", argv[0]);
        return 1;
    }
    if(argc > 1) little_path = argv[1];
    if(argc > 2) big_path = argv[2];
    if(argc > 3) save_path = argv[3];

    if(!read_file(little_path, &little_text) || !read_file(big_path, &big_text)){
        fprintf(stderr, "rl5_refine: failed to read input files\n");
        free(little_text); free(big_text);
        return 1;
    }

    int nl = parse_little_rows(little_text, lrows, MAX_ROWS);
    int nb = parse_big_rows(big_text, brows, MAX_ROWS);
    if(nl < 10 || nb < 9){
        fprintf(stderr, "rl5_refine: expected at least 10 little rows and 9 big rows, got %d and %d\n", nl, nb);
        free(little_text); free(big_text);
        return 1;
    }

    for(int i=0;i<NTILES;i++){
        int li = find_lrow(lrows, nl, lbase[i]);
        int bi = find_brow(brows, nb, blabels[i]);
        if(li < 0 || bi < 0){
            fprintf(stderr, "rl5_refine: missing row for %s (%s -> %s)\n", tid[i], lbase[i], blabels[i]);
            free(little_text); free(big_text);
            return 1;
        }
        memset(&tiles[i], 0, sizeof(tiles[i]));
        snprintf(tiles[i].tid, sizeof(tiles[i].tid), "%s", tid[i]);
        snprintf(tiles[i].llabels, sizeof(tiles[i].llabels), "%s", llabels[i]);
        snprintf(tiles[i].blabel, sizeof(tiles[i].blabel), "%s", blabels[i]);
        tiles[i].little = lrows[li].raw;
        tiles[i].big = brows[bi].raw;
        reduce_little_row(&tiles[i].little, &tiles[i].little_reduced);
        reduce_big_row(&tiles[i].big, &tiles[i].big_reduced);
        for(int k=0;k<SIDES;k++){
            Edge le, be;
            copy_tok(le.a, tiles[i].little_reduced.t[k]);
            copy_tok(le.b, tiles[i].little_reduced.t[(k + 1) % SIDES]);
            copy_tok(be.a, tiles[i].big_reduced.t[k]);
            copy_tok(be.b, tiles[i].big_reduced.t[(k + 1) % SIDES]);
            tiles[i].edge_ids[k] = add_double_edge(edges, &ne, le, be);
            if(tiles[i].edge_ids[k] < 0){
                fprintf(stderr, "rl5_refine: too many edges\n");
                free(little_text); free(big_text);
                return 1;
            }
        }
    }

    nr_l0 = load_rules(little_text, 0, edges, ne, lr0, MAX_RULES);
    nr_b0 = load_rules(big_text, 1, edges, ne, br0, MAX_RULES);
    nr_l = unique_rules(lr0, nr_l0, lr, MAX_RULES);
    nr_b = unique_rules(br0, nr_b0, br, MAX_RULES);

    FILE *out = fopen(save_path, "w");
    if(!out){
        fprintf(stderr, "rl5_refine: failed to write %s\n", save_path);
        free(little_text); free(big_text);
        return 1;
    }
    emit_save(out, tiles, NTILES, edges, ne, lr, nr_l, br, nr_b);
    fclose(out);

    printf("rl5 refine reduced hexes\n");
    printf("  input_little=%s rows=%d\n", little_path, nl);
    printf("  input_big=%s rows=%d\n", big_path, nb);
    printf("  output=%s\n", save_path);
    printf("  tiles=%d edge_map=%d little_rules=%d big_rules=%d\n", NTILES, ne, nr_l, nr_b);

    free(little_text); free(big_text);
    return 0;
}
