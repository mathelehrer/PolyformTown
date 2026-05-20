#include "rl5/hex5.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MAX_FIGS HEX5_MAX_VFIGS
#define MAX_MERGES 128
#define MAX_LABELS 1024

typedef struct { char from[HEX5_TOK]; char to[HEX5_TOK]; } Merge6;
typedef struct { Merge6 m[MAX_MERGES]; int n; } MergeSet6;
typedef struct { char s[MAX_LABELS][HEX5_TOK]; int n; } LabelSet6;
typedef struct { char raw[HEX5_TOK]; char cluster[HEX5_TOK]; } EdgeMap6;
typedef struct { EdgeMap6 m[4096]; int n; } EdgeMapSet6;
typedef struct { char name[HEX5_TOK]; int parent; } DsuItem6;
typedef struct { DsuItem6 item[MAX_LABELS]; int n; } Dsu6;

static int is_refine_invocation6(const char *argv0){
    return argv0 && strstr(argv0, "rl6_refine") != NULL;
}

static void usage(const char *argv0){
    if(is_refine_invocation6(argv0)){
        fprintf(stderr,
                "usage: %s [--cancel-depth N] [--output PATH] [--certify-path 'A=B C=D'] [--emit-state N] [--print-inner N]\n"
                "\n"
                "Search exhaustive reversible reductions of the full RL6 letter model only.\n"
                "DFS runs until the accepted frontier is exhausted; cancellation-depth is\n"
                "the practical knob. A binary merge is accepted only when the 25\n"
                "projected cycles remain injective/present, all extra rebuilt cycles\n"
                "are killed by the cancellation-depth probe, and the slot matrix is\n"
                "preserved through the recovery map. The model selector is intentionally\n"
                "not public here.\n",
                argv0);
    } else {
        fprintf(stderr,
                "usage: %s [--model basic|super|overlap|unified] [--cluster-symbols] [--merge A,B[=X]] [--scan-pairs] [--dfs N] [--dfs-slots [N]] [--emit-state N] [--certify-path 'A=B C=D'] [--alignment] [--offset-map] [--print-missing N] [--print-inner N] [--probe-extra N] [--probe-dimer EDGE_A EDGE_B] [--probe-cycle-pair A B] [--probe-depth N] [--probe-states N]\n"
                "\n"
                "Build the ordinary RL5/RL6 vertex atlas, then build the closed-cycle\n"
                "atlas by enumerating six-neighbor rings around each central tile.\n"
                "Also canonicalize the six inner-meets-outer edge-pair cycles.\n"
                "With --merge/--scan-pairs, collapse vertex labels and rebuild stats.\n",
                argv0);
    }
}

static int vfig_cmp_raw(const int a[3], const int b[3]){
    for(int i=0;i<3;i++) if(a[i] != b[i]) return a[i] - b[i];
    return 0;
}

static void canon3(int h0, int h1, int h2, int out[3]){
    int cand[3][3] = {{h0,h1,h2},{h1,h2,h0},{h2,h0,h1}};
    int best = 0;
    for(int i=1;i<3;i++) if(vfig_cmp_raw(cand[i], cand[best]) < 0) best = i;
    out[0] = cand[best][0];
    out[1] = cand[best][1];
    out[2] = cand[best][2];
}

static int vfig_cmp_struct(const void *va, const void *vb){
    const VFig5 *a = (const VFig5 *)va;
    const VFig5 *b = (const VFig5 *)vb;
    return vfig_cmp_raw(a->h, b->h);
}

static int fig_equal(const VFig5 *a, const VFig5 *b){
    return a->h[0] == b->h[0] && a->h[1] == b->h[1] && a->h[2] == b->h[2];
}

static int add_fig(VFig5 *figs, int *nfigs, int h0, int h1, int h2){
    VFig5 f;
    canon3(h0, h1, h2, f.h);
    for(int i=0;i<*nfigs;i++) if(fig_equal(&figs[i], &f)) return 1;
    if(*nfigs >= MAX_FIGS) return 0;
    figs[(*nfigs)++] = f;
    return 1;
}

static int find_oriented(const Hex5Model *m, const Hex5 *h){
    for(int i=0;i<m->noriented;i++) if(hex5_equal(&m->oriented[i], h)) return i;
    return -1;
}

static int rot_left_idx(const Hex5Model *m, int idx, int k){
    Hex5 r;
    if(idx < 0 || idx >= m->noriented) return -1;
    hex5_rot_left(&m->oriented[idx], k, &r);
    return find_oriented(m, &r);
}

static void copy_tok(char dst[HEX5_TOK], const char *src){
    size_t i = 0;
    if(!src) src = "";
    while(src[i] && i + 1 < HEX5_TOK){
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void edge_tok(char dst[HEX5_TOK], const char *a, const char *b){
    size_t p = 0;
    if(!a) a = "";
    if(!b) b = "";
    for(size_t i=0; a[i] && p + 1 < HEX5_TOK; i++) dst[p++] = a[i];
    if(p + 1 < HEX5_TOK) dst[p++] = ',';
    for(size_t i=0; b[i] && p + 1 < HEX5_TOK; i++) dst[p++] = b[i];
    dst[p] = '\0';
}


static int tok_cmp(const void *va, const void *vb){
    const char *a = (const char *)va;
    const char *b = (const char *)vb;
    return strcmp(a, b);
}

static int add_label(LabelSet6 *ls, const char *x){
    if(!x || !*x) return 1;
    for(int i=0;i<ls->n;i++) if(strcmp(ls->s[i], x) == 0) return 1;
    if(ls->n >= MAX_LABELS) return 0;
    copy_tok(ls->s[ls->n++], x);
    return 1;
}

static int split_edge(const char *edge, char a[HEX5_TOK], char b[HEX5_TOK]){
    const char *comma = strchr(edge, ',');
    size_t n;
    if(!comma) return 0;
    n = (size_t)(comma - edge);
    if(n >= HEX5_TOK) n = HEX5_TOK - 1;
    memcpy(a, edge, n);
    a[n] = '\0';
    copy_tok(b, comma + 1);
    return 1;
}

static int add_edge_labels(LabelSet6 *ls, const char *edge){
    char a[HEX5_TOK], b[HEX5_TOK];
    if(!split_edge(edge, a, b)) return 1;
    return add_label(ls, a) && add_label(ls, b);
}

static int collect_labels(const Hex5Model *m, LabelSet6 *ls){
    ls->n = 0;
    for(int i=0;i<m->ntiles;i++){
        for(int k=0;k<HEX5_SIDES;k++) if(!add_edge_labels(ls, m->tiles[i].e[k])) return 0;
    }
    for(int i=0;i<m->nrules;i++){
        if(!add_edge_labels(ls, m->rule_a[i])) return 0;
        if(!add_edge_labels(ls, m->rule_b[i])) return 0;
    }
    qsort(ls->s, (size_t)ls->n, sizeof(ls->s[0]), tok_cmp);
    return 1;
}

static void map_label(const MergeSet6 *ms, const char *in, char out[HEX5_TOK]){
    for(int i=0;i<ms->n;i++){
        if(strcmp(ms->m[i].from, in) == 0){ copy_tok(out, ms->m[i].to); return; }
    }
    copy_tok(out, in);
}

static void map_edge(const MergeSet6 *ms, const char *in, char out[HEX5_TOK]){
    char a[HEX5_TOK], b[HEX5_TOK], aa[HEX5_TOK], bb[HEX5_TOK];
    if(!split_edge(in, a, b)){ map_label(ms, in, out); return; }
    map_label(ms, a, aa);
    map_label(ms, b, bb);
    edge_tok(out, aa, bb);
}

static int add_rule_unique(Hex5Model *m, const char *a, const char *b){
    for(int i=0;i<m->nrules;i++){
        if(strcmp(m->rule_a[i], a) == 0 && strcmp(m->rule_b[i], b) == 0) return 1;
    }
    if(m->nrules >= HEX5_MAX_RULES) return 0;
    copy_tok(m->rule_a[m->nrules], a);
    copy_tok(m->rule_b[m->nrules], b);
    m->nrules++;
    return 1;
}

static int add_tile_unique(Hex5Model *m, const Hex5 *h){
    Hex5 c;
    hex5_canonical(h, &c);
    for(int i=0;i<m->ntiles;i++) if(hex5_equal(&m->tiles[i], &c)) return 1;
    if(m->ntiles >= HEX5_MAX_TILES) return 0;
    m->tiles[m->ntiles++] = c;
    return 1;
}

static int parse_merge(const char *arg, MergeSet6 *ms){
    char buf[3 * HEX5_TOK];
    char *comma, *eq;
    char a[HEX5_TOK], b[HEX5_TOK], to[HEX5_TOK];
    ms->n = 0;
    snprintf(buf, sizeof(buf), "%s", arg ? arg : "");
    comma = strchr(buf, ',');
    if(!comma) return 0;
    *comma = '\0';
    eq = strchr(comma + 1, '=');
    if(eq) *eq = '\0';
    copy_tok(a, buf);
    copy_tok(b, comma + 1);
    if(!*a || !*b) return 0;
    if(eq && eq[1]) copy_tok(to, eq + 1);
    else copy_tok(to, strcmp(a, b) <= 0 ? a : b);
    if(strcmp(a, to) != 0){ copy_tok(ms->m[ms->n].from, a); copy_tok(ms->m[ms->n].to, to); ms->n++; }
    if(strcmp(b, to) != 0){ copy_tok(ms->m[ms->n].from, b); copy_tok(ms->m[ms->n].to, to); ms->n++; }
    return 1;
}

static void pair_merge(const char *a, const char *b, MergeSet6 *ms){
    char arg[3 * HEX5_TOK];
    snprintf(arg, sizeof(arg), "%s,%s", a, b);
    parse_merge(arg, ms);
}

static int transform_model(const Hex5Model *base, const MergeSet6 *ms, Hex5Model *out){
    memset(out, 0, sizeof(*out));
    for(int i=0;i<base->ntiles;i++){
        Hex5 h;
        for(int k=0;k<HEX5_SIDES;k++) map_edge(ms, base->tiles[i].e[k], h.e[k]);
        if(!add_tile_unique(out, &h)) return 0;
    }
    for(int i=0;i<base->nrules;i++){
        char a[HEX5_TOK], b[HEX5_TOK];
        map_edge(ms, base->rule_a[i], a);
        map_edge(ms, base->rule_b[i], b);
        if(!add_rule_unique(out, a, b)) return 0;
    }
    out->nsource_rules = base->nsource_rules;
    return hex5_model_finish(out);
}

static int remove_tile_exact(Hex5Model *m, const char *v0, const char *v1,
                             const char *v2, const char *v3,
                             const char *v4, const char *v5){
    const char *v[HEX5_SIDES] = {v0,v1,v2,v3,v4,v5};
    Hex5 h, c;
    int removed = 0;
    for(int i=0;i<HEX5_SIDES;i++) edge_tok(h.e[i], v[i], v[(i+1)%HEX5_SIDES]);
    hex5_canonical(&h, &c);
    for(int i=0;i<m->ntiles;){
        if(hex5_equal(&m->tiles[i], &c)){
            for(int j=i+1;j<m->ntiles;j++) m->tiles[j-1] = m->tiles[j];
            m->ntiles--;
            removed++;
        } else i++;
    }
    return removed;
}


static int remove_tile_edges_exact(Hex5Model *m, const char *e0, const char *e1,
                                   const char *e2, const char *e3,
                                   const char *e4, const char *e5){
    const char *e[HEX5_SIDES] = {e0,e1,e2,e3,e4,e5};
    Hex5 h, c;
    int removed = 0;
    for(int i=0;i<HEX5_SIDES;i++) copy_tok(h.e[i], e[i]);
    hex5_canonical(&h, &c);
    for(int i=0;i<m->ntiles;){
        if(hex5_equal(&m->tiles[i], &c)){
            for(int j=i+1;j<m->ntiles;j++) m->tiles[j-1] = m->tiles[j];
            m->ntiles--;
            removed++;
        } else i++;
    }
    return removed;
}

static int remove_rule_exact(Hex5Model *m, const char *a, const char *b){
    int removed = 0;
    for(int i=0;i<m->nrules;){
        if(strcmp(m->rule_a[i], a) == 0 && strcmp(m->rule_b[i], b) == 0){
            for(int j=i+1;j<m->nrules;j++){
                copy_tok(m->rule_a[j-1], m->rule_a[j]);
                copy_tok(m->rule_b[j-1], m->rule_b[j]);
            }
            m->nrules--;
            removed++;
        } else i++;
    }
    return removed;
}


static void trim6(char *s){
    char *p = s;
    size_t n;
    while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if(p != s) memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while(n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = '\0';
}

static int cluster_dead_overlap_row6(int row){
    return row == 6 || row == 7 || row == 12 || row == 13;
}

static int edge_map_add6(EdgeMapSet6 *em, const char *raw, const char *cluster){
    if(!raw || !cluster || !*raw || !*cluster) return 1;
    for(int i=0;i<em->n;i++){
        if(strcmp(em->m[i].raw, raw) == 0 && strcmp(em->m[i].cluster, cluster) == 0) return 1;
    }
    if(em->n >= (int)(sizeof(em->m) / sizeof(em->m[0]))) return 0;
    copy_tok(em->m[em->n].raw, raw);
    copy_tok(em->m[em->n].cluster, cluster);
    em->n++;
    return 1;
}

static const char *cluster_first_vertex6(const char *cluster, char out[HEX5_TOK]){
    size_t i = 0;
    while(cluster[i] && cluster[i] != '.' && i + 1 < HEX5_TOK){ out[i] = cluster[i]; i++; }
    out[i] = '\0';
    return out;
}

static const char *cluster_last_vertex6(const char *cluster, char out[HEX5_TOK]){
    const char *p = strrchr(cluster, '.');
    copy_tok(out, p ? p + 1 : cluster);
    return out;
}

static int parse_cluster_hex_line6(const char *line, int *row_out, char side[HEX5_SIDES][HEX5_TOK]){
    const char *p = line;
    int row = 0, n = 0;
    while(*p == ' ' || *p == '\t') p++;
    if(*p < '0' || *p > '9') return 0;
    while(*p >= '0' && *p <= '9'){ row = row * 10 + (*p - '0'); p++; }
    while((p = strchr(p, '[')) && n < HEX5_SIDES){
        const char *q;
        size_t len;
        if(p[1] == '['){ p++; continue; }
        q = strchr(p, ']');
        if(!q) return 0;
        len = (size_t)(q - (p + 1));
        if(len == 0 || len >= HEX5_TOK) return 0;
        memcpy(side[n], p + 1, len);
        side[n][len] = '\0';
        trim6(side[n]);
        n++;
        p = q + 1;
    }
    if(n != HEX5_SIDES) return 0;
    *row_out = row;
    return 1;
}

static int parse_raw_rule_edges6(const char *line, char a[HEX5_TOK], char b[HEX5_TOK]){
    const char *p = strstr(line, "e(");
    const char *q;
    int n;
    if(!p) return 0;
    p += 2;
    q = strchr(p, ')');
    if(!q) return 0;
    n = (int)(q - p);
    if(n <= 0 || n >= HEX5_TOK) return 0;
    memcpy(a, p, (size_t)n); a[n] = '\0';
    p = strstr(q, "e(");
    if(!p) return 0;
    p += 2;
    q = strchr(p, ')');
    if(!q) return 0;
    n = (int)(q - p);
    if(n <= 0 || n >= HEX5_TOK) return 0;
    memcpy(b, p, (size_t)n); b[n] = '\0';
    return 1;
}

static int add_mapped_rule_pairs6(Hex5Model *m, const EdgeMapSet6 *em,
                                  const char *raw_a, const char *raw_b){
    int na = 0, nb = 0;
    for(int i=0;i<em->n;i++) if(strcmp(em->m[i].raw, raw_a) == 0) na++;
    for(int i=0;i<em->n;i++) if(strcmp(em->m[i].raw, raw_b) == 0) nb++;
    if(na == 0 || nb == 0) return 1;
    for(int i=0;i<em->n;i++){
        if(strcmp(em->m[i].raw, raw_a) != 0) continue;
        for(int j=0;j<em->n;j++){
            if(strcmp(em->m[j].raw, raw_b) != 0) continue;
            if(!add_rule_unique(m, em->m[i].cluster, em->m[j].cluster)) return 0;
            if(!add_rule_unique(m, em->m[j].cluster, em->m[i].cluster)) return 0;
        }
    }
    return 1;
}

static int load_overlap_cluster_model6(const char *path, Hex5Model *m){
    FILE *f = fopen(path, "r");
    char line[4096];
    EdgeMapSet6 em;
    int in_hex = 0, in_rules = 0;
    if(!f) return 0;
    memset(m, 0, sizeof(*m));
    memset(&em, 0, sizeof(em));
    while(fgets(line, sizeof(line), f)){
        char tmp[4096];
        snprintf(tmp, sizeof(tmp), "%s", line);
        trim6(tmp);
        if(!*tmp) continue;
        if(strcmp(tmp, "Unique Supertile Hexagons") == 0){ in_hex = 1; in_rules = 0; continue; }
        if(strcmp(tmp, "Downmapped Super Hexagon Edge Matches") == 0){ in_rules = 1; in_hex = 0; continue; }
        if(strstr(tmp, "# observations=")){ in_rules = 0; continue; }
        if(in_hex){
            int row = 0;
            char side[HEX5_SIDES][HEX5_TOK];
            if(!parse_cluster_hex_line6(tmp, &row, side)) continue;
            if(cluster_dead_overlap_row6(row)) continue;
            Hex5 h;
            for(int i=0;i<HEX5_SIDES;i++){
                char first[HEX5_TOK], last[HEX5_TOK], raw[HEX5_TOK], cluster[HEX5_TOK];
                int j = (i + 1) % HEX5_SIDES;
                cluster_first_vertex6(side[i], first);
                cluster_last_vertex6(side[j], last);
                edge_tok(raw, first, last);
                edge_tok(cluster, side[i], side[j]);
                copy_tok(h.e[i], cluster);
                if(!edge_map_add6(&em, raw, cluster)){ fclose(f); return 0; }
            }
            if(!add_tile_unique(m, &h)){ fclose(f); return 0; }
        } else if(in_rules){
            char a[HEX5_TOK], b[HEX5_TOK];
            if(tmp[0] == '#') continue;
            if(!parse_raw_rule_edges6(tmp, a, b)) continue;
            m->nsource_rules++;
            if(!add_mapped_rule_pairs6(m, &em, a, b)){ fclose(f); return 0; }
        }
    }
    fclose(f);
    return hex5_model_finish(m);
}

static int apply_errata(Hex5Model *m, const char *model){
    if(strcmp(model, "super") == 0){
        /* data/rl5/errata.dat: dead: super 6 overlap 6 7 12 13 */
        remove_tile_exact(m, "v2", "v9", "v21", "v26", "v31", "v38");

        /* This self-match is only sourced by the dead row-6 family. */
        remove_rule_exact(m, "v9,v21", "v9,v21");
        return hex5_model_finish(m);
    }

    if(strcmp(model, "overlap") == 0){
        /* data/rl5/errata.dat: dead: super 6 overlap 6 7 12 13 */
        remove_tile_edges_exact(m, "v5,v13", "v13,v21", "v21,v31", "v29,v38", "v37,v45", "v45,v5");
        remove_tile_edges_exact(m, "v5,v13", "v13,v21", "v21,v29", "v27,v38", "v37,v45", "v45,v5");
        remove_tile_edges_exact(m, "v5,v13", "v12,v24", "v23,v31", "v29,v38", "v37,v45", "v45,v5");
        remove_tile_edges_exact(m, "v5,v13", "v12,v24", "v23,v29", "v29,v38", "v37,v45", "v45,v5");
        return hex5_model_finish(m);
    }

    return 1;
}


static int parse_unified_tile_line6(Hex5Model *m, const char *line){
    char tmp[4096], *tok[HEX5_SIDES];
    int n = 0;
    Hex5 h;
    snprintf(tmp, sizeof(tmp), "%s", line ? line : "");
    trim6(tmp);
    if(!*tmp || tmp[0] == '#') return 1;
    for(char *p = strtok(tmp, ", \t\r\n"); p && n < HEX5_SIDES; p = strtok(NULL, ", \t\r\n")){
        tok[n++] = p;
    }
    if(n != HEX5_SIDES) return 1;
    for(int i=0;i<HEX5_SIDES;i++) edge_tok(h.e[i], tok[i], tok[(i+1)%HEX5_SIDES]);
    return add_tile_unique(m, &h);
}

static int parse_unified_rule_line6(Hex5Model *m, const char *line){
    char tmp[4096], left[HEX5_TOK], right[HEX5_TOK];
    char la[HEX5_TOK], lb[HEX5_TOK], ra[HEX5_TOK], rb[HEX5_TOK];
    char *eq;
    snprintf(tmp, sizeof(tmp), "%s", line ? line : "");
    trim6(tmp);
    if(!*tmp || tmp[0] == '#') return 1;
    eq = strchr(tmp, '=');
    if(!eq) return 1;
    *eq = '\0';
    copy_tok(left, tmp);
    copy_tok(right, eq + 1);
    trim6(left);
    trim6(right);
    if(!split_edge(left, la, lb) || !split_edge(right, ra, rb)) return 1;
    edge_tok(left, la, lb);
    edge_tok(right, ra, rb);
    m->nsource_rules++;
    return add_rule_unique(m, left, right) && add_rule_unique(m, right, left);
}

static int load_unified_model6(const char *path, Hex5Model *m){
    FILE *f = fopen(path, "r");
    char line[4096];
    int in_tiles = 0, in_rules = 0;
    if(!f) return 0;
    memset(m, 0, sizeof(*m));
    while(fgets(line, sizeof(line), f)){
        char tmp[4096];
        snprintf(tmp, sizeof(tmp), "%s", line);
        trim6(tmp);
        if(strcmp(tmp, "---[tiles]---") == 0){ in_tiles = 1; in_rules = 0; continue; }
        if(strcmp(tmp, "---[edge rules directed lex]---") == 0){ in_tiles = 0; in_rules = 1; continue; }
        if(strncmp(tmp, "---[", 5) == 0){ in_tiles = 0; in_rules = 0; continue; }
        if(in_tiles){ if(!parse_unified_tile_line6(m, tmp)){ fclose(f); return 0; } }
        else if(in_rules){ if(!parse_unified_rule_line6(m, tmp)){ fclose(f); return 0; } }
    }
    fclose(f);
    return hex5_model_finish(m);
}


static int load_unified_basic_merge6(const char *path, MergeSet6 *ms){
    FILE *f = fopen(path, "r");
    char line[4096];
    int in_basic = 0;
    if(!f) return 0;
    ms->n = 0;
    while(fgets(line, sizeof(line), f)){
        char tmp[4096], lhs[HEX5_TOK];
        char *colon, *rhs;
        snprintf(tmp, sizeof(tmp), "%s", line);
        trim6(tmp);
        if(strcmp(tmp, "---[basic]---") == 0){ in_basic = 1; continue; }
        if(strncmp(tmp, "---[", 5) == 0){ if(in_basic) break; else continue; }
        if(!in_basic || !*tmp || tmp[0] == '#') continue;
        colon = strchr(tmp, ':');
        if(!colon) continue;
        *colon = '\0';
        copy_tok(lhs, tmp);
        trim6(lhs);
        rhs = colon + 1;
        for(char *tok = strtok(rhs, ", \t\r\n"); tok; tok = strtok(NULL, ", \t\r\n")){
            if(!*tok || strcmp(tok, lhs) == 0) continue;
            if(ms->n >= MAX_MERGES){ fclose(f); return 0; }
            copy_tok(ms->m[ms->n].from, tok);
            copy_tok(ms->m[ms->n].to, lhs);
            ms->n++;
        }
    }
    fclose(f);
    return 1;
}

static const char *model_path(const char *model){
    if(strcmp(model, "basic") == 0) return "data/rl5/hexagons.dat";
    if(strcmp(model, "super") == 0) return "data/rl5/supertile_hexagons.dat";
    if(strcmp(model, "overlap") == 0) return "data/rl5/overlap_supertile_hexagons.dat";
    if(strcmp(model, "unified") == 0 || strcmp(model, "full") == 0 || strcmp(model, "letter") == 0) return "data/rl6/unified_model.dat";
    return NULL;
}

static int build_standard(const Hex5Model *m, Attach5Dict *a, VComp5Dict *v,
                          BComp5Result *b){
    if(!attach5_build(m, a)) return 0;
    if(!vcomp5_build(a, v)) return 0;
    if(!bcomp5_build(m, v, b)) return 0;
    return 1;
}

static int build_cycle_atlas(const Hex5Model *m, const BComp5Result *b,
                             VFig5 *figs, int *nfigs, long *raw){
    *nfigs = 0;
    *raw = 0;
    for(int i=0;i<b->nrings;i++){
        for(int k=0;k<HEX5_SIDES;k++){
            int center_k = rot_left_idx(m, b->rings[i].center, k);
            int next = b->rings[i].r[(k+1)%HEX5_SIDES];
            int third = rot_left_idx(m, next, 1);
            if(center_k < 0 || third < 0) return 0;
            (*raw)++;
            if(!add_fig(figs, nfigs, center_k, b->rings[i].r[k], third)) return 0;
        }
    }
    qsort(figs, (size_t)*nfigs, sizeof(figs[0]), vfig_cmp_struct);
    return 1;
}


static int inner_cmp_struct(const void *va, const void *vb){
    const Inner5 *a = (const Inner5 *)va;
    const Inner5 *b = (const Inner5 *)vb;
    for(int i=0;i<HEX5_SIDES;i++){
        int c = strcmp(a->a[i], b->a[i]);
        if(c) return c;
        c = strcmp(a->b[i], b->b[i]);
        if(c) return c;
    }
    return 0;
}

static void print_inner_cycle(const Inner5 *in){
    for(int i=0;i<HEX5_SIDES;i++){
        if(i) printf(" ");
        printf("[%s|%s]", in->a[i], in->b[i]);
    }
    printf("\n");
}

static int contains_fig(const VFig5 *figs, int nfigs, const VFig5 *f){
    int lo = 0, hi = nfigs;
    while(lo < hi){
        int mid = lo + (hi - lo)/2;
        int c = vfig_cmp_raw(figs[mid].h, f->h);
        if(c == 0) return 1;
        if(c < 0) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}

static void print_hex(const Hex5Model *m, int idx){
    if(idx < 0 || idx >= m->noriented){ printf("?"); return; }
    printf("[");
    for(int i=0;i<HEX5_SIDES;i++){
        if(i) printf(" ");
        printf("%s", m->oriented[idx].e[i]);
    }
    printf("]");
}


typedef struct {
    int ok;
    int tiles;
    int oriented;
    int rules;
    int attach;
    int rings;
    int inner;
    int cycle_vfigs;
    int standard_vfigs;
    int false_pos;
    int extra;
} ReduceStats6;

static int compute_stats(const Hex5Model *m, ReduceStats6 *st){
    Attach5Dict *a = calloc(1, sizeof(*a));
    VComp5Dict *v = calloc(1, sizeof(*v));
    BComp5Result *b = calloc(1, sizeof(*b));
    VFig5 *cycle = calloc(MAX_FIGS, sizeof(*cycle));
    Inner5Set *inner = calloc(1, sizeof(*inner));
    long raw_cycle = 0;
    if(!a || !v || !b || !cycle || !inner){
        free(a); free(v); free(b); free(cycle); free(inner);
        return 0;
    }
    memset(st, 0, sizeof(*st));
    if(!build_standard(m, a, v, b)) goto fail;
    qsort(v->figs, (size_t)v->nfigs, sizeof(v->figs[0]), vfig_cmp_struct);
    if(!build_cycle_atlas(m, b, cycle, &st->cycle_vfigs, &raw_cycle)) goto fail;
    if(!bcomp5_inner_generated(m, b, inner)) goto fail;
    qsort(inner->rows, (size_t)inner->nrows, sizeof(inner->rows[0]), inner_cmp_struct);
    st->ok = 1;
    st->tiles = m->ntiles;
    st->oriented = m->noriented;
    st->rules = m->nrules;
    st->attach = a->nedges;
    st->rings = b->nrings;
    st->inner = inner->nrows;
    st->standard_vfigs = v->nfigs;
    for(int i=0;i<v->nfigs;i++) if(!contains_fig(cycle, st->cycle_vfigs, &v->figs[i])) st->false_pos++;
    for(int i=0;i<st->cycle_vfigs;i++) if(!contains_fig(v->figs, v->nfigs, &cycle[i])) st->extra++;
    free(a); free(v); free(b); free(cycle); free(inner);
    return 1;
fail:
    free(a); free(v); free(b); free(cycle); free(inner);
    return 0;
}



static int inner_equal6(const Inner5 *a, const Inner5 *b){
    return inner_cmp_struct(a, b) == 0;
}

static void inner_canon6(const Inner5 *in, Inner5 *out){
    Inner5 best = *in, c;
    for(int k=1;k<HEX5_SIDES;k++){
        for(int i=0;i<HEX5_SIDES;i++){
            copy_tok(c.a[i], in->a[(i+k)%HEX5_SIDES]);
            copy_tok(c.b[i], in->b[(i+k)%HEX5_SIDES]);
        }
        if(inner_cmp_struct(&c, &best) < 0) best = c;
    }
    *out = best;
}

static int add_inner6(Inner5Set *s, const Inner5 *in){
    Inner5 c;
    inner_canon6(in, &c);
    for(int i=0;i<s->nrows;i++) if(inner_equal6(&s->rows[i], &c)) return 1;
    if(s->nrows >= HEX5_MAX_INNER) return 0;
    s->rows[s->nrows++] = c;
    return 1;
}

static int map_one_inner6(const Inner5 *src, const MergeSet6 *ms, Inner5 *dst){
    Inner5 tmp;
    for(int k=0;k<HEX5_SIDES;k++){
        map_edge(ms, src->a[k], tmp.a[k]);
        map_edge(ms, src->b[k], tmp.b[k]);
    }
    inner_canon6(&tmp, dst);
    return 1;
}

static int map_one_inner_offset6(const Inner5 *src, const MergeSet6 *ms,
                                 Inner5 *dst, int *off_out){
    Inner5 tmp, r;
    for(int k=0;k<HEX5_SIDES;k++){
        map_edge(ms, src->a[k], tmp.a[k]);
        map_edge(ms, src->b[k], tmp.b[k]);
    }
    inner_canon6(&tmp, dst);
    for(int off=0; off<HEX5_SIDES; off++){
        for(int i=0;i<HEX5_SIDES;i++){
            copy_tok(r.a[i], tmp.a[(i+off)%HEX5_SIDES]);
            copy_tok(r.b[i], tmp.b[(i+off)%HEX5_SIDES]);
        }
        if(inner_equal6(&r, dst)){
            if(off_out) *off_out = off;
            return 1;
        }
    }
    return 0;
}

static int map_inner_set(const Inner5Set *src, const MergeSet6 *ms, Inner5Set *dst){
    dst->nrows = 0;
    for(int r=0;r<src->nrows;r++){
        Inner5 in;
        if(!map_one_inner6(&src->rows[r], ms, &in)) return 0;
        if(!add_inner6(dst, &in)) return 0;
    }
    qsort(dst->rows, (size_t)dst->nrows, sizeof(dst->rows[0]), inner_cmp_struct);
    return 1;
}

static int inner_index6(const Inner5Set *s, const Inner5 *x){
    int lo = 0, hi = s->nrows;
    while(lo < hi){
        int mid = lo + (hi - lo)/2;
        int c = inner_cmp_struct(&s->rows[mid], x);
        if(c == 0) return mid;
        if(c < 0) lo = mid + 1;
        else hi = mid;
    }
    return -1;
}

static int contains_inner6(const Inner5Set *s, const Inner5 *x){
    return inner_index6(s, x) >= 0;
}

static int build_inner_set_for_model(const Hex5Model *m, Inner5Set *out){
    Attach5Dict *a = calloc(1, sizeof(*a));
    VComp5Dict *v = calloc(1, sizeof(*v));
    BComp5Result *b = calloc(1, sizeof(*b));
    int ok = 0;
    if(!a || !v || !b) goto done;
    if(!build_standard(m, a, v, b)) goto done;
    if(!bcomp5_inner_generated(m, b, out)) goto done;
    qsort(out->rows, (size_t)out->nrows, sizeof(out->rows[0]), inner_cmp_struct);
    ok = 1;
done:
    free(a); free(v); free(b);
    return ok;
}



static void remove_tile_index6(Hex5Model *m, int idx){
    if(idx < 0 || idx >= m->ntiles) return;
    for(int j=idx+1;j<m->ntiles;j++) m->tiles[j-1] = m->tiles[j];
    m->ntiles--;
    hex5_model_finish(m);
}

static int inner_image_counts(const Hex5Model *base, const Hex5Model *red,
                              const MergeSet6 *ms, int *image_n, int *red_n,
                              int *missing, int *extra){
    Inner5Set *base_inner = calloc(1, sizeof(*base_inner));
    Inner5Set *image = calloc(1, sizeof(*image));
    Inner5Set *red_inner = calloc(1, sizeof(*red_inner));
    int ok = 0;
    *image_n = *red_n = *missing = *extra = 0;
    if(!base_inner || !image || !red_inner) goto done;
    if(!build_inner_set_for_model(base, base_inner)) goto done;
    if(!map_inner_set(base_inner, ms, image)) goto done;
    if(!build_inner_set_for_model(red, red_inner)) goto done;
    *image_n = image->nrows;
    *red_n = red_inner->nrows;
    for(int i=0;i<red_inner->nrows;i++) if(!contains_inner6(image, &red_inner->rows[i])) (*extra)++;
    for(int i=0;i<image->nrows;i++) if(!contains_inner6(red_inner, &image->rows[i])) (*missing)++;
    ok = 1;
done:
    free(base_inner); free(image); free(red_inner);
    return ok;
}

static void print_base_tile6(const Hex5Model *m, int idx){
    if(idx < 0 || idx >= m->ntiles) return;
    printf("tile %d", idx);
    for(int k=0;k<HEX5_SIDES;k++) printf(" %s", m->tiles[idx].e[k]);
    printf("\n");
}



static void scan_delete_one_rulepair6(const Hex5Model *base, const Hex5Model *red,
                                      const MergeSet6 *ms){
    printf("delete-one-rulepair candidates preserving inner image:\n");
    int printed = 0;
    for(int r=0;r<red->nrules;r++){
        Hex5Model x = *red;
        int image_n, red_n, missing, extra;
        char a0[HEX5_TOK], b0[HEX5_TOK];
        copy_tok(a0, red->rule_a[r]);
        copy_tok(b0, red->rule_b[r]);
        remove_rule_exact(&x, a0, b0);
        remove_rule_exact(&x, b0, a0);
        hex5_model_finish(&x);
        if(!inner_image_counts(base, &x, ms, &image_n, &red_n, &missing, &extra)) continue;
        if(missing == 0 && extra == 0){
            printf("  drop %s = %s   tiles=%d inner=%d image=%d\n", a0, b0, x.ntiles, red_n, image_n);
            printed++;
        }
    }
    if(!printed) printf("  none\n");
}

static void scan_delete_one_tile6(const Hex5Model *base, const Hex5Model *red,
                                  const MergeSet6 *ms){
    printf("delete-one-tile candidates preserving inner image:\n");
    int printed = 0;
    for(int t=0;t<red->ntiles;t++){
        Hex5Model x = *red;
        int image_n, red_n, missing, extra;
        remove_tile_index6(&x, t);
        if(!inner_image_counts(base, &x, ms, &image_n, &red_n, &missing, &extra)) continue;
        if(missing == 0 && extra == 0){
            printf("  drop "); print_base_tile6(red, t);
            printf("    tiles=%d inner=%d image=%d\n", x.ntiles, red_n, image_n);
            printed++;
        }
    }
    if(!printed) printf("  none\n");
}


static int boundary_cycle_ok6(const Boundary5State *s){
    if(s->ncycle < 3) return 0;
    for(int i=0;i<s->ncycle;i++){
        int a = s->cycle[i];
        int b = s->cycle[(i+1)%s->ncycle];
        if(a < 0 || a >= s->patch.n || b < 0 || b >= s->patch.n) return 0;
        if(boundary5_dir_between(s->patch.cell[a], s->patch.cell[b]) < 0) return 0;
    }
    return 1;
}

static int boundary_state_complete6(const Boundary5State *s){
    for(int k=0;k<s->ncycle;k++){
        int item = s->cycle[k];
        if(item >= 0 && item < s->patch.n && (s->patch.flags[item] & BOUNDARY5_VANISH)) return 0;
    }
    return 1;
}

static int boundary_state_same_patch6(const Boundary5State *a, const Boundary5State *b){
    if(a->patch.n != b->patch.n) return 0;
    for(int i=0;i<a->patch.n;i++){
        int j = boundary5_patch_find(&b->patch, a->patch.cell[i]);
        if(j < 0) return 0;
        if(a->patch.hex[i] != b->patch.hex[j]) return 0;
        if((a->patch.flags[i] & BOUNDARY5_VANISH) != (b->patch.flags[j] & BOUNDARY5_VANISH)) return 0;
    }
    return 1;
}

static int boundary_state_seen6(const Boundary5State *states, int n, const Boundary5State *s){
    for(int i=0;i<n;i++) if(boundary_state_same_patch6(&states[i], s)) return 1;
    return 0;
}

static int boundary_vec_add_unique6(Boundary5State **states, int *n, int *cap,
                                    const Boundary5State *s, int max_states){
    Boundary5State *tmp;
    int ncap;
    if(boundary_state_seen6(*states, *n, s)) return 1;
    if(max_states > 0 && *n >= max_states) return 0;
    if(*n + 1 > *cap){
        ncap = (*cap > 0) ? *cap * 2 : 64;
        if(max_states > 0 && ncap > max_states) ncap = max_states;
        if(ncap < *n + 1) ncap = *n + 1;
        tmp = realloc(*states, (size_t)ncap * sizeof(**states));
        if(!tmp) return 0;
        *states = tmp;
        *cap = ncap;
    }
    (*states)[(*n)++] = *s;
    return 1;
}

static int common_outside6(Boundary5Cell prev, Boundary5Cell next, Boundary5Cell *out){
    int u = boundary5_dir_between(prev, next);
    if(u < 0) return 0;
    boundary5_neighbor(prev, u - 1, out);
    return 1;
}

static int edge_candidates6(const Hex5Model *m, const VComp5Dict *v,
                            const Boundary5State *s, int edge_pos,
                            Boundary5Cell *outside_pos,
                            int out[BOUNDARY5_MAX_CANDIDATES]){
    int prev = s->cycle[edge_pos];
    int next = s->cycle[(edge_pos + 1) % s->ncycle];
    if(!common_outside6(s->patch.cell[prev], s->patch.cell[next], outside_pos)) return 0;
    return boundary5_lookup_edge_bruteforce(m, v, &s->patch, prev, next,
                                            *outside_pos, out,
                                            BOUNDARY5_MAX_CANDIDATES);
}

static int choose_edge6(const Hex5Model *m, const VComp5Dict *v,
                        const Boundary5State *s, Boundary5Cell *outside_pos,
                        int out[BOUNDARY5_MAX_CANDIDATES], int *nout){
    int best_i = -1;
    int best_n = 0;
    Boundary5Cell best_pos = {0,0};
    int best[BOUNDARY5_MAX_CANDIDATES];
    for(int i=0;i<s->ncycle;i++){
        int prev = s->cycle[i];
        int next = s->cycle[(i+1)%s->ncycle];
        int touches = 0;
        Boundary5Cell pos;
        int cand[BOUNDARY5_MAX_CANDIDATES];
        int n;
        if(prev >= 0 && prev < s->patch.n && (s->patch.flags[prev] & BOUNDARY5_VANISH)) touches = 1;
        if(next >= 0 && next < s->patch.n && (s->patch.flags[next] & BOUNDARY5_VANISH)) touches = 1;
        if(!touches) continue;
        n = edge_candidates6(m, v, s, i, &pos, cand);
        if(n <= 0) continue;
        if(best_i < 0 || n < best_n){
            best_i = i;
            best_n = n;
            best_pos = pos;
            for(int k=0;k<n && k<BOUNDARY5_MAX_CANDIDATES;k++) best[k] = cand[k];
        }
    }
    if(best_i < 0){ *nout = 0; return -1; }
    *outside_pos = best_pos;
    *nout = best_n;
    for(int k=0;k<best_n && k<BOUNDARY5_MAX_CANDIDATES;k++) out[k] = best[k];
    return best_i;
}

static int apply_candidate6(const Hex5Model *m, const Boundary5State *in,
                            int edge_pos, Boundary5Cell outside_pos,
                            int cand, Boundary5State *out){
    char err[256];
    int newcycle[BOUNDARY5_MAX_CYCLE];
    int nn = 0;
    *out = *in;
    int added = boundary5_patch_add(&out->patch, outside_pos, cand, 0u);
    if(added < 0) return 0;
    if(!boundary5_patch_check_edges(m, &out->patch, err, (int)sizeof(err))) return 0;
    for(int k=0;k<in->ncycle;k++){
        int item = in->cycle[k];
        if(item >= 0 && item < out->patch.n && boundary5_patch_is_boundary_cell(&out->patch, item)){
            if(nn >= BOUNDARY5_MAX_CYCLE) return 0;
            newcycle[nn++] = item;
        }
        if(k == edge_pos && boundary5_patch_is_boundary_cell(&out->patch, added)){
            if(nn >= BOUNDARY5_MAX_CYCLE) return 0;
            newcycle[nn++] = added;
        }
    }
    out->ncycle = nn;
    for(int k=0;k<nn;k++) out->cycle[k] = newcycle[k];
    return boundary_cycle_ok6(out);
}

static void boundary_begin_next_layer6(Boundary5State *s){
    for(int i=0;i<s->patch.n;i++) s->patch.flags[i] &= ~BOUNDARY5_VANISH;
    for(int k=0;k<s->ncycle;k++){
        int item = s->cycle[k];
        if(item >= 0 && item < s->patch.n) s->patch.flags[item] |= BOUNDARY5_VANISH;
    }
}

static int complete_one_layer_collect6(const Hex5Model *m, const VComp5Dict *v,
                                       const Boundary5State *start, int max_states,
                                       Boundary5State **done_states, int *done_out,
                                       int *dead_out, int *escape_out){
    Boundary5State *cur = NULL, *nexts = NULL, *done = NULL;
    int ncur = 0, cur_cap = 0, nnext = 0, next_cap = 0, ndone = 0, done_cap = 0;
    int dead = 0, escape = 0;
    if(done_states) *done_states = NULL;
    if(max_states <= 0 || max_states > BOUNDARY5_MAX_STATES) max_states = BOUNDARY5_MAX_STATES;
    if(!boundary_vec_add_unique6(&cur, &ncur, &cur_cap, start, max_states)){ escape = 1; goto out; }
    for(int step=0; step<BOUNDARY5_MAX_LAYER_STEPS && ncur>0; step++){
        nnext = 0;
        for(int i=0;i<ncur;i++){
            Boundary5State *s = &cur[i];
            Boundary5Cell pos;
            int cand[BOUNDARY5_MAX_CANDIDATES];
            int ncand = 0;
            int edge;
            if(boundary_state_complete6(s)){
                if(!boundary_vec_add_unique6(&done, &ndone, &done_cap, s, max_states)) escape++;
                continue;
            }
            edge = choose_edge6(m, v, s, &pos, cand, &ncand);
            if(edge < 0 || ncand <= 0){ dead++; continue; }
            for(int c=0;c<ncand;c++){
                Boundary5State ns;
                if(!apply_candidate6(m, s, edge, pos, cand[c], &ns)) continue;
                if(boundary_state_complete6(&ns)){
                    if(!boundary_vec_add_unique6(&done, &ndone, &done_cap, &ns, max_states)) escape++;
                } else {
                    if(!boundary_vec_add_unique6(&nexts, &nnext, &next_cap, &ns, max_states)) escape++;
                }
            }
        }
        free(cur);
        cur = nexts;
        cur_cap = next_cap;
        nexts = NULL;
        next_cap = 0;
        ncur = nnext;
        if(escape) break;
    }
    if(ncur > 0) escape += ncur;
out:
    free(cur); free(nexts);
    if(done_states) *done_states = done;
    else free(done);
    if(done_out) *done_out = ndone;
    if(dead_out) *dead_out = dead;
    if(escape_out) *escape_out = escape;
    return ndone;
}

static int complete_one_layer6(const Hex5Model *m, const VComp5Dict *v,
                               const Boundary5State *start, int max_states,
                               int *done_out, int *dead_out, int *escape_out){
    Boundary5State *cur = NULL, *nexts = NULL, *done = NULL;
    int ncur = 0, cur_cap = 0, nnext = 0, next_cap = 0, ndone = 0, done_cap = 0;
    int dead = 0, escape = 0;
    if(max_states <= 0 || max_states > BOUNDARY5_MAX_STATES) max_states = BOUNDARY5_MAX_STATES;
    if(!boundary_vec_add_unique6(&cur, &ncur, &cur_cap, start, max_states)){ escape = 1; goto out; }
    for(int step=0; step<BOUNDARY5_MAX_LAYER_STEPS && ncur>0; step++){
        nnext = 0;
        for(int i=0;i<ncur;i++){
            Boundary5State *s = &cur[i];
            Boundary5Cell pos;
            int cand[BOUNDARY5_MAX_CANDIDATES];
            int ncand = 0;
            int edge;
            if(boundary_state_complete6(s)){
                if(!boundary_vec_add_unique6(&done, &ndone, &done_cap, s, max_states)) escape++;
                continue;
            }
            edge = choose_edge6(m, v, s, &pos, cand, &ncand);
            if(edge < 0 || ncand <= 0){ dead++; continue; }
            for(int c=0;c<ncand;c++){
                Boundary5State ns;
                if(!apply_candidate6(m, s, edge, pos, cand[c], &ns)) continue;
                if(boundary_state_complete6(&ns)){
                    if(!boundary_vec_add_unique6(&done, &ndone, &done_cap, &ns, max_states)) escape++;
                } else {
                    if(!boundary_vec_add_unique6(&nexts, &nnext, &next_cap, &ns, max_states)) escape++;
                }
            }
        }
        free(cur);
        cur = nexts;
        cur_cap = next_cap;
        nexts = NULL;
        next_cap = 0;
        ncur = nnext;
        if(escape) break;
    }
    if(ncur > 0) escape += ncur;
out:
    free(cur); free(nexts); free(done);
    if(done_out) *done_out = ndone;
    if(dead_out) *dead_out = dead;
    if(escape_out) *escape_out = escape;
    return ndone;
}

static int inner_from_ring6(const Hex5Model *m, const Ring5 *r, Inner5 *in){
    for(int k=0;k<HEX5_SIDES;k++){
        int center = rot_left_idx(m, r->center, k);
        if(center < 0 || r->r[k] < 0 || r->r[k] >= m->noriented) return 0;
        copy_tok(in->a[k], m->oriented[center].e[0]);
        copy_tok(in->b[k], m->oriented[r->r[k]].e[1]);
    }
    inner_canon6(in, in);
    return 1;
}

static int state_from_ring6(const Hex5Model *m, const Ring5 *r, Boundary5State *s){
    Boundary5Cell c0 = {0,0};
    char err[256];
    memset(s, 0, sizeof(*s));
    if(boundary5_patch_add(&s->patch, c0, r->center, 0u) < 0) return 0;
    for(int d=0; d<HEX5_SIDES; d++){
        Boundary5Cell c;
        int world = rot_left_idx(m, r->r[d], -(d + 2));
        int idx;
        if(world < 0) return 0;
        boundary5_neighbor(c0, d, &c);
        idx = boundary5_patch_add(&s->patch, c, world, BOUNDARY5_VANISH);
        if(idx < 0) return 0;
        s->cycle[d] = idx;
    }
    s->ncycle = HEX5_SIDES;
    if(!boundary5_patch_check_edges(m, &s->patch, err, (int)sizeof(err))) return 0;
    return boundary_cycle_ok6(s);
}

static void probe_extra_inner6(const Hex5Model *base, const Hex5Model *red,
                               const MergeSet6 *ms, int limit){
    Inner5Set *base_inner = calloc(1, sizeof(*base_inner));
    Inner5Set *image = calloc(1, sizeof(*image));
    BComp5Result *b = calloc(1, sizeof(*b));
    Attach5Dict *a = calloc(1, sizeof(*a));
    VComp5Dict *v = calloc(1, sizeof(*v));
    Inner5Set *seen = calloc(1, sizeof(*seen));
    int printed = 0, extra_cycles = 0, rings = 0, extend_live = 0, extend_dead = 0, extend_escape = 0;
    if(!base_inner || !image || !b || !a || !v || !seen) goto done;
    if(!build_inner_set_for_model(base, base_inner)) goto done;
    if(!map_inner_set(base_inner, ms, image)) goto done;
    if(!build_standard(red, a, v, b)) goto done;
    for(int i=0;i<b->nrings;i++){
        Inner5 in;
        Boundary5State s;
        int done = 0, dead = 0, escape = 0;
        if(!inner_from_ring6(red, &b->rings[i], &in)) continue;
        if(contains_inner6(image, &in)) continue;
        rings++;
        if(!contains_inner6(seen, &in)){
            add_inner6(seen, &in);
            extra_cycles++;
        }
        if(!state_from_ring6(red, &b->rings[i], &s)){
            dead++;
        } else {
            complete_one_layer6(red, v, &s, BOUNDARY5_MAX_STATES, &done, &dead, &escape);
        }
        if(done > 0) extend_live++;
        else if(escape > 0) extend_escape++;
        else extend_dead++;
        if(printed < limit){
            printf("extra_inner_probe ");
            print_inner_cycle(&in);
            printf("  extend done=%d dead=%d escape=%d\n", done, dead, escape);
            printed++;
        }
    }
done:
    printf("extra_probe_summary extra_hexes=%d witnesses=%d extend_live=%d extend_dead=%d extend_escape=%d\n",
           extra_cycles, rings, extend_live, extend_dead, extend_escape);
    free(base_inner); free(image); free(b); free(a); free(v); free(seen);
}



static int state_from_dimer_edges6(const Hex5Model *m, int h0, int h1, Boundary5State *s);

static void rotate_inner_edges_to6(const Inner5 *in, int src_slot, int dst_slot, Hex5 *h){
    for(int k=0;k<HEX5_SIDES;k++){
        int d = (k - src_slot + dst_slot) % HEX5_SIDES;
        if(d < 0) d += HEX5_SIDES;
        copy_tok(h->e[d], in->a[k]);
    }
}

static void probe_cycle_pair6(const Hex5Model *m, int ca, int cb, int print_limit, int probe_depth, int probe_states){
    Inner5Set *inner = calloc(1, sizeof(*inner));
    Attach5Dict *ad = calloc(1, sizeof(*ad));
    VComp5Dict *v = calloc(1, sizeof(*v));
    int matches = 0, seeds = 0, live = 0, dead = 0, escape = 0, printed = 0;
    int live2 = 0, dead2 = 0, escape2 = 0, first_done_total = 0;
    if(probe_depth < 1) probe_depth = 1;
    if(probe_states <= 0 || probe_states > BOUNDARY5_MAX_STATES) probe_states = BOUNDARY5_MAX_STATES;
    if(!inner || !ad || !v){ fprintf(stderr, "out of memory\n"); goto done; }
    if(!build_inner_set_for_model(m, inner) || ca < 0 || cb < 0 || ca >= inner->nrows || cb >= inner->nrows){
        fprintf(stderr, "bad cycle pair or failed inner set\n");
        goto done;
    }
    if(!attach5_build(m, ad) || !vcomp5_build(ad, v)){
        fprintf(stderr, "failed to build cycle-pair probe dictionaries\n");
        goto done;
    }
    for(int ia=0; ia<HEX5_SIDES; ia++){
        for(int ib=0; ib<HEX5_SIDES; ib++){
            Hex5 ha, hb;
            int hia, hib;
            Boundary5State st;
            int done_n = 0, dead_n = 0, esc_n = 0;
            if(strcmp(inner->rows[ca].a[ia], inner->rows[cb].b[ib]) != 0) continue;
            if(strcmp(inner->rows[ca].b[ia], inner->rows[cb].a[ib]) != 0) continue;
            matches++;
            rotate_inner_edges_to6(&inner->rows[ca], ia, 0, &ha);
            rotate_inner_edges_to6(&inner->rows[cb], ib, 3, &hb);
            hia = find_oriented(m, &ha);
            hib = find_oriented(m, &hb);
            if(hia < 0 || hib < 0){
                if(printed < print_limit){
                    printf("cycle_pair_match ca=%d cb=%d slots=%d,%d missing_oriented hia=%d hib=%d edge=%s|%s\n",
                           ca, cb, ia, ib, hia, hib, inner->rows[ca].a[ia], inner->rows[ca].b[ia]);
                    printed++;
                }
                continue;
            }
            if(!state_from_dimer_edges6(m, hia, hib, &st)){
                if(printed < print_limit){
                    printf("cycle_pair_match ca=%d cb=%d slots=%d,%d bad_dimer hia=%d hib=%d edge=%s|%s\n",
                           ca, cb, ia, ib, hia, hib, inner->rows[ca].a[ia], inner->rows[ca].b[ia]);
                    printed++;
                }
                continue;
            }
            seeds++;
            if(probe_depth <= 1){
                complete_one_layer6(m, v, &st, probe_states, &done_n, &dead_n, &esc_n);
            } else {
                Boundary5State *done_states = NULL;
                complete_one_layer_collect6(m, v, &st, probe_states,
                                            &done_states, &done_n, &dead_n, &esc_n);
                first_done_total += done_n;
                for(int di=0; di<done_n; di++){
                    Boundary5State layer2 = done_states[di];
                    int d2 = 0, dead_tmp = 0, esc_tmp = 0;
                    boundary_begin_next_layer6(&layer2);
                    complete_one_layer6(m, v, &layer2, probe_states,
                                        &d2, &dead_tmp, &esc_tmp);
                    if(d2 > 0) live2++;
                    else if(esc_tmp > 0) escape2++;
                    else dead2++;
                }
                free(done_states);
            }
            if(done_n > 0) live++;
            else if(esc_n > 0) escape++;
            else dead++;
            if(printed < print_limit){
                if(probe_depth <= 1){
                    printf("cycle_pair_seed ca=%d cb=%d slots=%d,%d hia=%d hib=%d edge=%s|%s done=%d dead=%d escape=%d\n",
                           ca, cb, ia, ib, hia, hib, inner->rows[ca].a[ia], inner->rows[ca].b[ia], done_n, dead_n, esc_n);
                } else {
                    printf("cycle_pair_seed ca=%d cb=%d slots=%d,%d hia=%d hib=%d edge=%s|%s depth1_done=%d depth1_dead=%d depth1_escape=%d\n",
                           ca, cb, ia, ib, hia, hib, inner->rows[ca].a[ia], inner->rows[ca].b[ia], done_n, dead_n, esc_n);
                }
                printed++;
            }
        }
    }
    if(probe_depth <= 1){
        printf("cycle_pair_probe ca=%d cb=%d depth=1 probe_states=%d matches=%d seeds=%d live=%d dead=%d escape=%d\n",
               ca, cb, probe_states, matches, seeds, live, dead, escape);
    } else {
        printf("cycle_pair_probe ca=%d cb=%d depth=2 probe_states=%d matches=%d seeds=%d depth1_live=%d depth1_dead=%d depth1_escape=%d depth1_done_states=%d depth2_live=%d depth2_dead=%d depth2_escape=%d\n",
               ca, cb, probe_states, matches, seeds, live, dead, escape, first_done_total, live2, dead2, escape2);
    }
done:
    free(inner); free(ad); free(v);
}

static int state_from_dimer_edges6(const Hex5Model *m, int h0, int h1, Boundary5State *s){
    Boundary5Cell c0 = {0,0};
    Boundary5Cell c1;
    char err[256];
    int i0, i1;
    memset(s, 0, sizeof(*s));
    boundary5_neighbor(c0, 0, &c1);
    i0 = boundary5_patch_add(&s->patch, c0, h0, BOUNDARY5_VANISH);
    i1 = boundary5_patch_add(&s->patch, c1, h1, BOUNDARY5_VANISH);
    if(i0 < 0 || i1 < 0) return 0;
    s->cycle[0] = i0;
    s->cycle[1] = i1;
    s->ncycle = 2;
    if(!boundary5_patch_check_edges(m, &s->patch, err, (int)sizeof(err))) return 0;
    return 1;
}

static void probe_dimer_edges6(const Hex5Model *m, const char *edge_a, const char *edge_b, int print_limit){
    Attach5Dict *a = calloc(1, sizeof(*a));
    VComp5Dict *v = calloc(1, sizeof(*v));
    int seeds = 0, live = 0, dead = 0, escape = 0, printed = 0;
    if(!a || !v){ fprintf(stderr, "out of memory\n"); goto done; }
    if(!attach5_build(m, a) || !vcomp5_build(a, v)){
        fprintf(stderr, "failed to build dimer probe dictionaries\n");
        goto done;
    }
    for(int h0=0; h0<m->noriented; h0++){
        if(strcmp(m->oriented[h0].e[0], edge_a) != 0) continue;
        for(int h1=0; h1<m->noriented; h1++){
            Boundary5State s;
            int done_n = 0, dead_n = 0, esc_n = 0;
            if(strcmp(m->oriented[h1].e[3], edge_b) != 0) continue;
            if(!boundary5_edge_matches(m, edge_a, edge_b)) continue;
            if(!state_from_dimer_edges6(m, h0, h1, &s)) continue;
            seeds++;
            complete_one_layer6(m, v, &s, BOUNDARY5_MAX_STATES, &done_n, &dead_n, &esc_n);
            if(done_n > 0) live++;
            else if(esc_n > 0) escape++;
            else dead++;
            if(printed < print_limit){
                printf("dimer_seed h0=%d h1=%d done=%d dead=%d escape=%d\n", h0, h1, done_n, dead_n, esc_n);
                printed++;
            }
        }
    }
    printf("dimer_probe edge_a=%s edge_b=%s seeds=%d live=%d dead=%d escape=%d\n",
           edge_a, edge_b, seeds, live, dead, escape);
done:
    free(a); free(v);
}

static void compare_inner_image(const Hex5Model *base, const Hex5Model *red,
                                const MergeSet6 *ms, int limit){
    Inner5Set *base_inner = calloc(1, sizeof(*base_inner));
    Inner5Set *image = calloc(1, sizeof(*image));
    Inner5Set *red_inner = calloc(1, sizeof(*red_inner));
    int *fiber = NULL;
    int missing = 0, extra = 0, merged_fibers = 0, max_fiber = 0;
    if(!base_inner || !image || !red_inner) goto done;
    if(!build_inner_set_for_model(base, base_inner)) goto done;
    if(!map_inner_set(base_inner, ms, image)) goto done;
    if(!build_inner_set_for_model(red, red_inner)) goto done;

    fiber = calloc((size_t)(image->nrows > 0 ? image->nrows : 1), sizeof(*fiber));
    if(!fiber) goto done;
    for(int i=0;i<base_inner->nrows;i++){
        Inner5 mapped;
        int idx;
        if(!map_one_inner6(&base_inner->rows[i], ms, &mapped)) goto done;
        idx = inner_index6(image, &mapped);
        if(idx < 0) goto done;
        fiber[idx]++;
    }
    for(int i=0;i<image->nrows;i++){
        if(fiber[i] > max_fiber) max_fiber = fiber[i];
        if(fiber[i] > 1) merged_fibers++;
    }

    for(int i=0;i<red_inner->nrows;i++){
        if(!contains_inner6(image, &red_inner->rows[i])){
            if(extra < limit){ printf("extra_inner "); print_inner_cycle(&red_inner->rows[i]); }
            extra++;
        }
    }
    for(int i=0;i<image->nrows;i++){
        if(!contains_inner6(red_inner, &image->rows[i])){
            if(missing < limit){ printf("missing_inner "); print_inner_cycle(&image->rows[i]); }
            missing++;
        }
    }
    printf("inner_base=%d inner_image=%d reduced_inner=%d missing_image=%d extra_reduced=%d merged_fibers=%d max_fiber=%d\n",
           base_inner->nrows, image->nrows, red_inner->nrows, missing, extra,
           merged_fibers, max_fiber);
done:
    free(fiber);
    free(base_inner); free(image); free(red_inner);
}



#define DFS6_MAX_INNER 128
#define DFS6_MAX_STATES 256
#define DFS6_PATH 1024

static int g_dfs_exact_inner = 0;
static int g_dfs_probe_states = BOUNDARY5_MAX_STATES;
static int g_dfs_cancel_depth = 1;
static int g_dfs_emit_target = 0;
static int g_dfs_emit_count = 0;
static const char *g_refine_output_path = "data/rl6/refined_model.dat";
static int g_refine_write_output = 0;

typedef struct {
    char a[HEX5_TOK];
    char b[HEX5_TOK];
} LabelPair6;

typedef struct {
    Inner5 rows[DFS6_MAX_INNER];
    int nrows;
} SmallInner6;

typedef struct {
    Hex5Model model;
    SmallInner6 inner;
    char path[DFS6_PATH];
    int depth;
    int tiles;
    int dead_extra_hexes;
    int parent;
} DfsState6;

static SmallInner6 g_root_inner_override6;
static int g_have_root_inner_override6 = 0;

typedef uint64_t SmallMatrix6[DFS6_MAX_INNER][DFS6_MAX_INNER];

static void small_slot_matrix6(const SmallInner6 *s, SmallMatrix6 m){
    memset(m, 0, sizeof(SmallMatrix6));
    for(int a=0; a<s->nrows; a++){
        for(int i=0; i<HEX5_SIDES; i++){
            for(int b=0; b<s->nrows; b++){
                for(int j=0; j<HEX5_SIDES; j++){
                    if(strcmp(s->rows[a].a[i], s->rows[b].b[j]) == 0 &&
                       strcmp(s->rows[a].b[i], s->rows[b].a[j]) == 0){
                        m[a][b] |= ((uint64_t)1) << (HEX5_SIDES * i + j);
                    }
                }
            }
        }
    }
}

static uint64_t small_shift_mask6(uint64_t mask, int ro, int co){
    uint64_t out = 0;
    ro %= HEX5_SIDES; if(ro < 0) ro += HEX5_SIDES;
    co %= HEX5_SIDES; if(co < 0) co += HEX5_SIDES;
    for(int i=0;i<HEX5_SIDES;i++){
        for(int j=0;j<HEX5_SIDES;j++){
            int ti = (i + ro) % HEX5_SIDES;
            int tj = (j + co) % HEX5_SIDES;
            if(mask & (((uint64_t)1) << (HEX5_SIDES * ti + tj))){
                out |= ((uint64_t)1) << (HEX5_SIDES * i + j);
            }
        }
    }
    return out;
}

static int matrix_equal_by_remember6(const SmallInner6 *root,
                                     const SmallInner6 *cur,
                                     const int root_of[DFS6_MAX_INNER],
                                     const int off_to_root[DFS6_MAX_INNER],
                                     int *first_a, int *first_b){
    SmallMatrix6 rm, cm;
    small_slot_matrix6(root, rm);
    small_slot_matrix6(cur, cm);
    for(int a=0; a<cur->nrows; a++){
        for(int b=0; b<cur->nrows; b++){
            int ra = root_of[a];
            int rb = root_of[b];
            uint64_t want;
            if(ra < 0 || rb < 0 || ra >= root->nrows || rb >= root->nrows) return 0;
            want = small_shift_mask6(rm[ra][rb], off_to_root[a], off_to_root[b]);
            if(cm[a][b] != want){
                if(first_a) *first_a = a;
                if(first_b) *first_b = b;
                return 0;
            }
        }
    }
    return 1;
}

static void inner_rotate6(const Inner5 *in, int off, Inner5 *out){
    for(int i=0;i<HEX5_SIDES;i++){
        copy_tok(out->a[i], in->a[(i+off)%HEX5_SIDES]);
        copy_tok(out->b[i], in->b[(i+off)%HEX5_SIDES]);
    }
}

static int small_contains_inner6(const SmallInner6 *s, const Inner5 *x){
    for(int i=0;i<s->nrows;i++) if(inner_equal6(&s->rows[i], x)) return 1;
    return 0;
}

static int small_index_inner6(const SmallInner6 *s, const Inner5 *x){
    for(int i=0;i<s->nrows;i++) if(inner_equal6(&s->rows[i], x)) return i;
    return -1;
}

static int small_add_inner6(SmallInner6 *s, const Inner5 *x){
    Inner5 c;
    inner_canon6(x, &c);
    if(small_contains_inner6(s, &c)) return 1;
    if(s->nrows >= DFS6_MAX_INNER) return 0;
    s->rows[s->nrows++] = c;
    return 1;
}

static int small_from_full_inner6(const Inner5Set *full, SmallInner6 *small){
    small->nrows = 0;
    for(int i=0;i<full->nrows;i++) if(!small_add_inner6(small, &full->rows[i])) return 0;
    return 1;
}

static int build_small_inner_for_model6(const Hex5Model *m, SmallInner6 *out){
    Inner5Set *full = calloc(1, sizeof(*full));
    int ok = 0;
    if(!full) return 0;
    if(!build_inner_set_for_model(m, full)) goto done;
    ok = small_from_full_inner6(full, out);
done:
    free(full);
    return ok;
}

static int map_small_inner6(const SmallInner6 *src, const MergeSet6 *ms, SmallInner6 *dst){
    dst->nrows = 0;
    for(int i=0;i<src->nrows;i++){
        Inner5 mapped;
        if(!map_one_inner6(&src->rows[i], ms, &mapped)) return 0;
        if(!small_add_inner6(dst, &mapped)) return 0;
    }
    return 1;
}


static int build_unified_survivor_inner6(SmallInner6 *out){
    Hex5Model *basic = calloc(1, sizeof(*basic));
    Inner5Set *base_full = calloc(1, sizeof(*base_full));
    Inner5Set *mapped_full = calloc(1, sizeof(*mapped_full));
    MergeSet6 ms;
    int ok = 0;
    if(!basic || !base_full || !mapped_full) goto done;
    if(!hex5_parse_file("data/rl5/hexagons.dat", basic)) goto done;
    if(!build_inner_set_for_model(basic, base_full)) goto done;
    if(!load_unified_basic_merge6("data/rl6/unified_model.dat", &ms)) goto done;
    if(!map_inner_set(base_full, &ms, mapped_full)) goto done;
    if(!small_from_full_inner6(mapped_full, out)) goto done;
    ok = 1;
done:
    free(basic);
    free(base_full);
    free(mapped_full);
    return ok;
}

static int full_contains_small_image6(const Inner5Set *full, const SmallInner6 *image,
                                      int *missing){
    int miss = 0;
    for(int i=0;i<image->nrows;i++) if(!contains_inner6(full, &image->rows[i])) miss++;
    if(missing) *missing = miss;
    return miss == 0;
}

static int count_extra_extension_depth1_6(const Hex5Model *red, const SmallInner6 *image,
                                         int *extra_cycles, int *extra_rings,
                                         int *extend_live, int *extend_dead,
                                         int *extend_escape){
    BComp5Result *b = calloc(1, sizeof(*b));
    Attach5Dict *a = calloc(1, sizeof(*a));
    VComp5Dict *v = calloc(1, sizeof(*v));
    SmallInner6 seen;
    int ok = 0;
    int ec = 0, er = 0, live = 0, dead = 0, esc = 0;
    seen.nrows = 0;
    if(!b || !a || !v) goto done;
    if(!build_standard(red, a, v, b)) goto done;
    for(int i=0;i<b->nrings;i++){
        Inner5 in;
        Boundary5State s;
        int done_n = 0, dead_n = 0, esc_n = 0;
        if(!inner_from_ring6(red, &b->rings[i], &in)) continue;
        if(small_contains_inner6(image, &in)) continue;
        er++;
        if(er > 96){ esc = 1; ok = 1; goto done; }
        if(small_contains_inner6(&seen, &in)) continue;
        if(!small_add_inner6(&seen, &in)) goto done;
        ec++;
        if(!state_from_ring6(red, &b->rings[i], &s)){
            dead++;
            continue;
        }
        complete_one_layer6(red, v, &s, g_dfs_probe_states, &done_n, &dead_n, &esc_n);
        if(done_n > 0){ live++; ok = 1; goto done; }
        else if(esc_n > 0){ esc++; ok = 1; goto done; }
        else dead++;
    }
    ok = 1;
done:
    if(extra_cycles) *extra_cycles = ec;
    if(extra_rings) *extra_rings = er;
    if(extend_live) *extend_live = live;
    if(extend_dead) *extend_dead = dead;
    if(extend_escape) *extend_escape = esc;
    free(b); free(a); free(v);
    return ok;
}



static int cancellation_probe_state6(const Hex5Model *m, const VComp5Dict *v,
                                     const Boundary5State *seed,
                                     int depth, int max_states,
                                     int *live_out, int *dead_out, int *escape_out){
    Boundary5State *cur = NULL;
    int ncur = 0, cap = 0;
    int live = 0, dead = 0, esc = 0;
    if(depth <= 1){
        int done = 0;
        complete_one_layer6(m, v, seed, max_states, &done, &dead, &esc);
        live = done > 0;
        if(live_out) *live_out = live;
        if(dead_out) *dead_out = live || esc ? 0 : 1;
        if(escape_out) *escape_out = esc > 0;
        return 1;
    }
    if(!boundary_vec_add_unique6(&cur, &ncur, &cap, seed, max_states)){
        if(live_out) *live_out = 0;
        if(dead_out) *dead_out = 0;
        if(escape_out) *escape_out = 1;
        return 1;
    }
    for(int d=1; d<=depth && ncur>0; d++){
        Boundary5State *next = NULL;
        int nnext = 0, next_cap = 0;
        for(int i=0;i<ncur;i++){
            Boundary5State work = cur[i];
            Boundary5State *done_states = NULL;
            int done_n = 0, dead_n = 0, esc_n = 0;
            if(d > 1) boundary_begin_next_layer6(&work);
            complete_one_layer_collect6(m, v, &work, max_states, &done_states, &done_n, &dead_n, &esc_n);
            dead += dead_n;
            esc += esc_n;
            if(d == depth){
                live += done_n;
            } else {
                for(int j=0;j<done_n;j++){
                    if(!boundary_vec_add_unique6(&next, &nnext, &next_cap, &done_states[j], max_states)) esc++;
                }
            }
            free(done_states);
        }
        free(cur);
        cur = next;
        ncur = nnext;
        cap = next_cap;
        if(esc) break;
    }
    free(cur);
    if(live_out) *live_out = live > 0;
    if(dead_out) *dead_out = (live == 0 && esc == 0) ? 1 : 0;
    if(escape_out) *escape_out = esc > 0;
    return 1;
}

static int count_extra_extension6(const Hex5Model *red, const SmallInner6 *image,
                                  int *extra_cycles, int *extra_rings,
                                  int *extend_live, int *extend_dead,
                                  int *extend_escape){
    BComp5Result *b = calloc(1, sizeof(*b));
    Attach5Dict *a = calloc(1, sizeof(*a));
    VComp5Dict *v = calloc(1, sizeof(*v));
    SmallInner6 seen;
    int ok = 0;
    int ec = 0, er = 0, live = 0, dead = 0, esc = 0;
    seen.nrows = 0;
    if(g_dfs_cancel_depth <= 1){
        ok = count_extra_extension_depth1_6(red, image, extra_cycles, extra_rings,
                                            extend_live, extend_dead, extend_escape);
        free(b); free(a); free(v);
        return ok;
    }
    if(!b || !a || !v) goto done;
    if(!build_standard(red, a, v, b)) goto done;
    for(int i=0;i<b->nrings;i++){
        Inner5 in;
        Boundary5State s;
        int l = 0, d = 0, e = 0;
        if(!inner_from_ring6(red, &b->rings[i], &in)) continue;
        if(small_contains_inner6(image, &in)) continue;
        er++;
        if(er > 96){ esc = 1; ok = 1; goto done; }
        if(small_contains_inner6(&seen, &in)) continue;
        if(!small_add_inner6(&seen, &in)) goto done;
        ec++;
        if(!state_from_ring6(red, &b->rings[i], &s)){
            dead++;
            continue;
        }
        if(!cancellation_probe_state6(red, v, &s, g_dfs_cancel_depth,
                                      g_dfs_probe_states, &l, &d, &e)) goto done;
        if(l){ live++; ok = 1; goto done; }
        else if(e){ esc++; ok = 1; goto done; }
        else dead++;
    }
    ok = 1;
done:
    if(extra_cycles) *extra_cycles = ec;
    if(extra_rings) *extra_rings = er;
    if(extend_live) *extend_live = live;
    if(extend_dead) *extend_dead = dead;
    if(extend_escape) *extend_escape = esc;
    free(b); free(a); free(v);
    return ok;
}

static int add_label_pair6(LabelPair6 *pairs, int *npairs, const char *a, const char *b){
    char aa[HEX5_TOK], bb[HEX5_TOK];
    if(strcmp(a, b) == 0) return 1;
    if(strcmp(a, b) < 0){ copy_tok(aa, a); copy_tok(bb, b); }
    else { copy_tok(aa, b); copy_tok(bb, a); }
    for(int i=0;i<*npairs;i++){
        if(strcmp(pairs[i].a, aa) == 0 && strcmp(pairs[i].b, bb) == 0) return 1;
    }
    if(*npairs >= 4096) return 0;
    copy_tok(pairs[*npairs].a, aa);
    copy_tok(pairs[*npairs].b, bb);
    (*npairs)++;
    return 1;
}

static int add_slot_label6(char slot[HEX5_SIDES][MAX_LABELS][HEX5_TOK],
                           int nslot[HEX5_SIDES], int k, const char *x){
    if(!x || !*x) return 1;
    for(int i=0;i<nslot[k];i++) if(strcmp(slot[k][i], x) == 0) return 1;
    if(nslot[k] >= MAX_LABELS) return 0;
    copy_tok(slot[k][nslot[k]++], x);
    return 1;
}

static int add_slot_edge_labels6(char slot[HEX5_SIDES][MAX_LABELS][HEX5_TOK],
                                 int nslot[HEX5_SIDES], int k, const char *edge){
    char a[HEX5_TOK], b[HEX5_TOK];
    if(!split_edge(edge, a, b)) return 1;
    return add_slot_label6(slot, nslot, k, a) && add_slot_label6(slot, nslot, k, b);
}

static int collect_model_slot_pairs6(const Hex5Model *m, LabelPair6 *pairs, int *npairs){
    char slot[HEX5_SIDES][MAX_LABELS][HEX5_TOK];
    int nslot[HEX5_SIDES];
    memset(nslot, 0, sizeof(nslot));
    *npairs = 0;
    for(int t=0;t<m->ntiles;t++){
        for(int k=0;k<HEX5_SIDES;k++){
            if(!add_slot_edge_labels6(slot, nslot, k, m->tiles[t].e[k])) return 0;
        }
    }
    for(int k=0;k<HEX5_SIDES;k++){
        qsort(slot[k], (size_t)nslot[k], sizeof(slot[k][0]), tok_cmp);
        for(int i=0;i<nslot[k];i++){
            for(int j=i+1;j<nslot[k];j++){
                if(!add_label_pair6(pairs, npairs, slot[k][i], slot[k][j])) return 0;
            }
        }
    }
    return 1;
}

static void print_alignment6(const Hex5Model *m, const char *model){
    (void)m;
    SmallInner6 inner;
    char slot[HEX5_SIDES][MAX_LABELS][HEX5_TOK];
    int nslot[HEX5_SIDES];
    LabelSet6 all;
    if(!build_small_inner_for_model6(m, &inner)){
        fprintf(stderr, "failed to build inner cycles for alignment report\n");
        return;
    }
    memset(nslot, 0, sizeof(nslot));
    all.n = 0;
    for(int r=0;r<inner.nrows;r++){
        for(int k=0;k<HEX5_SIDES;k++){
            char a[HEX5_TOK], b[HEX5_TOK];
            if(split_edge(inner.rows[r].a[k], a, b)){
                add_slot_label6(slot, nslot, k, a); add_slot_label6(slot, nslot, k, b);
                add_label(&all, a); add_label(&all, b);
            }
            if(split_edge(inner.rows[r].b[k], a, b)){
                add_slot_label6(slot, nslot, k, a); add_slot_label6(slot, nslot, k, b);
                add_label(&all, a); add_label(&all, b);
            }
        }
    }
    qsort(all.s, (size_t)all.n, sizeof(all.s[0]), tok_cmp);
    printf("RL6 canonical alignment (%s)\n", model);
    printf("source=25 canonical inner_outer_cycles; columns are cycle slots after canonical rotation\n");
    printf("%-12s %-8s %s\n", "label", "nslots", "slots");
    for(int i=0;i<all.n;i++){
        int ns = 0;
        char buf[64];
        buf[0] = '\0';
        for(int k=0;k<HEX5_SIDES;k++){
            int hit = 0;
            for(int j=0;j<nslot[k];j++) if(strcmp(slot[k][j], all.s[i]) == 0) hit = 1;
            if(hit){
                char tmp[16];
                snprintf(tmp, sizeof(tmp), "%s%d", ns ? "," : "", k);
                strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
                ns++;
            }
        }
        printf("%-12s %-8d %s\n", all.s[i], ns, buf);
    }
}

static int try_merge_state6(const DfsState6 *src, const char *la, const char *lb,
                            DfsState6 *dst, int *extra_cycles, int *extra_rings,
                            int *extend_dead){
    MergeSet6 ms;
    Hex5Model *red = calloc(1, sizeof(*red));
    Inner5Set *red_full = calloc(1, sizeof(*red_full));
    SmallInner6 image;
    int missing = 0, ec = 0, er = 0, live = 0, dead = 0, esc = 0;
    int cheap_extra = 0;
    int ok = 0;
    if(extra_cycles) *extra_cycles = 0;
    if(extra_rings) *extra_rings = 0;
    if(extend_dead) *extend_dead = 0;
    pair_merge(la, lb, &ms);
    if(!red || !red_full) goto done;
    if(!transform_model(&src->model, &ms, red)) goto done;
    if(red->ntiles >= src->model.ntiles) goto done;
    if(!map_small_inner6(&src->inner, &ms, &image)) goto done;
    if(image.nrows != src->inner.nrows) goto done; /* fiber merge */
    {
        int root_of[DFS6_MAX_INNER];
        int off_to_root[DFS6_MAX_INNER];
        int matrix_a = -1, matrix_b = -1;
        for(int i=0;i<DFS6_MAX_INNER;i++){ root_of[i] = -1; off_to_root[i] = 0; }
        for(int i=0;i<src->inner.nrows;i++){
            Inner5 mapped;
            int off = 0, ci;
            if(!map_one_inner_offset6(&src->inner.rows[i], &ms, &mapped, &off)) goto done;
            ci = small_index_inner6(&image, &mapped);
            if(ci < 0 || root_of[ci] >= 0) goto done;
            root_of[ci] = i;
            off_to_root[ci] = off;
        }
        if(!matrix_equal_by_remember6(&src->inner, &image, root_of, off_to_root, &matrix_a, &matrix_b)) goto done;
    }
    if(!build_inner_set_for_model(red, red_full)) goto done;
    if(!full_contains_small_image6(red_full, &image, &missing)) goto done;
    for(int ri=0; ri<red_full->nrows; ri++){
        if(!small_contains_inner6(&image, &red_full->rows[ri])) cheap_extra++;
    }
    if(g_dfs_exact_inner && cheap_extra > 0) goto done;
    if(cheap_extra > 0){
        int saved_probe_states = g_dfs_probe_states;
        if(!count_extra_extension6(red, &image, &ec, &er, &live, &dead, &esc)) goto done;
        /*
           A capped extension probe is only a speed filter, not a proof.
           If a small-extra candidate only failed by escape under the cap,
           rerun the same candidate with the full boundary state budget before
           rejecting it.  This keeps branches such as basic 3=5, then 0=1,
           from being lost to an artificial diagnostic cap.
        */
        if(live == 0 && esc != 0 && cheap_extra <= 2 && saved_probe_states < BOUNDARY5_MAX_STATES){
            g_dfs_probe_states = BOUNDARY5_MAX_STATES;
            ec = er = live = dead = esc = 0;
            if(!count_extra_extension6(red, &image, &ec, &er, &live, &dead, &esc)){
                g_dfs_probe_states = saved_probe_states;
                goto done;
            }
            g_dfs_probe_states = saved_probe_states;
        }
        if(live != 0 || esc != 0) goto done;
    } else {
        ec = er = dead = 0;
    }
    memset(dst, 0, sizeof(*dst));
    dst->model = *red;
    dst->inner = image;
    dst->depth = src->depth + 1;
    dst->tiles = red->ntiles;
    dst->dead_extra_hexes = src->dead_extra_hexes + ec;
    if(src->path[0]){
        snprintf(dst->path, sizeof(dst->path), "%s", src->path);
        strncat(dst->path, " ", sizeof(dst->path) - strlen(dst->path) - 1);
        strncat(dst->path, la, sizeof(dst->path) - strlen(dst->path) - 1);
        strncat(dst->path, "=", sizeof(dst->path) - strlen(dst->path) - 1);
        strncat(dst->path, lb, sizeof(dst->path) - strlen(dst->path) - 1);
    } else {
        snprintf(dst->path, sizeof(dst->path), "%s", la);
        strncat(dst->path, "=", sizeof(dst->path) - strlen(dst->path) - 1);
        strncat(dst->path, lb, sizeof(dst->path) - strlen(dst->path) - 1);
    }
    if(extra_cycles) *extra_cycles = ec;
    if(extra_rings) *extra_rings = er;
    if(extend_dead) *extend_dead = dead;
    ok = 1;
done:
    free(red);
    free(red_full);
    return ok;
}

static int same_model_tiles6(const Hex5Model *a, const Hex5Model *b){
    if(a->ntiles != b->ntiles) return 0;
    for(int i=0;i<a->ntiles;i++){
        int seen = 0;
        for(int j=0;j<b->ntiles;j++) if(hex5_equal(&a->tiles[i], &b->tiles[j])) seen = 1;
        if(!seen) return 0;
    }
    return 1;
}

static int parse_path_step6(const char *tok, char a[HEX5_TOK], char b[HEX5_TOK]){
    const char *eq = strchr(tok, '=');
    size_t n;
    if(!eq || eq == tok || !eq[1]) return 0;
    n = (size_t)(eq - tok);
    if(n >= HEX5_TOK) n = HEX5_TOK - 1;
    memcpy(a, tok, n);
    a[n] = '\0';
    copy_tok(b, eq + 1);
    return 1;
}

static void print_model_tiles6(const Hex5Model *m){
    for(int t=0;t<m->ntiles;t++){
        printf("  tile[%d]", t);
        for(int k=0;k<HEX5_SIDES;k++) printf(" %s", m->tiles[t].e[k]);
        printf("\n");
    }
}


typedef struct {
    char v[3][HEX5_TOK];
} VTripleTok6;

static int vtriple_cmp6(const void *va, const void *vb){
    const VTripleTok6 *a = (const VTripleTok6 *)va;
    const VTripleTok6 *b = (const VTripleTok6 *)vb;
    for(int i=0;i<3;i++){
        int c = strcmp(a->v[i], b->v[i]);
        if(c) return c;
    }
    return 0;
}

static int vtriple_equal6(const VTripleTok6 *a, const VTripleTok6 *b){
    return strcmp(a->v[0], b->v[0]) == 0 &&
           strcmp(a->v[1], b->v[1]) == 0 &&
           strcmp(a->v[2], b->v[2]) == 0;
}

static void vtriple_canon6(const char a[HEX5_TOK],
                           const char b[HEX5_TOK],
                           const char c[HEX5_TOK],
                           VTripleTok6 *out){
    VTripleTok6 cand[3];
    copy_tok(cand[0].v[0], a); copy_tok(cand[0].v[1], b); copy_tok(cand[0].v[2], c);
    copy_tok(cand[1].v[0], b); copy_tok(cand[1].v[1], c); copy_tok(cand[1].v[2], a);
    copy_tok(cand[2].v[0], c); copy_tok(cand[2].v[1], a); copy_tok(cand[2].v[2], b);
    int best = 0;
    for(int i=1;i<3;i++) if(vtriple_cmp6(&cand[i], &cand[best]) < 0) best = i;
    *out = cand[best];
}

static int oriented_vertex_label6(const Hex5Model *m, int idx, char out[HEX5_TOK]){
    char a[HEX5_TOK], b[HEX5_TOK];
    if(idx < 0 || idx >= m->noriented) return 0;
    if(!split_edge(m->oriented[idx].e[0], a, b)) return 0;
    /* attach5/vcomp5 represent the vertex between e[0] and e[1]; this is
       the second endpoint of e[0], equivalently the first endpoint of e[1]. */
    copy_tok(out, b);
    return 1;
}

static int add_vtriple_unique6(VTripleTok6 *triples, int *ntriples, const VTripleTok6 *x){
    for(int i=0;i<*ntriples;i++) if(vtriple_equal6(&triples[i], x)) return 1;
    if(*ntriples >= MAX_FIGS) return 0;
    triples[(*ntriples)++] = *x;
    return 1;
}

static int collect_valid_vertex_triples6(const Hex5Model *m,
                                         VTripleTok6 *triples,
                                         int *ntriples){
    Attach5Dict *a = calloc(1, sizeof(*a));
    VComp5Dict *v = calloc(1, sizeof(*v));
    int ok = 0;
    *ntriples = 0;
    if(!a || !v) goto done;
    if(!attach5_build(m, a) || !vcomp5_build(a, v)) goto done;
    for(int i=0;i<v->nfigs;i++){
        char x[3][HEX5_TOK];
        VTripleTok6 t;
        if(!oriented_vertex_label6(m, v->figs[i].h[0], x[0]) ||
           !oriented_vertex_label6(m, v->figs[i].h[1], x[1]) ||
           !oriented_vertex_label6(m, v->figs[i].h[2], x[2])) goto done;
        vtriple_canon6(x[0], x[1], x[2], &t);
        if(!add_vtriple_unique6(triples, ntriples, &t)) goto done;
    }
    qsort(triples, (size_t)*ntriples, sizeof(triples[0]), vtriple_cmp6);
    ok = 1;
done:
    free(a);
    free(v);
    return ok;
}

static void fprint_valid_vertex_triples6(FILE *out, const Hex5Model *m){
    VTripleTok6 *triples = calloc(MAX_FIGS, sizeof(*triples));
    int ntriples = 0;
    fprintf(out, "---[valid vertex triples]---\n");
    if(!triples || !collect_valid_vertex_triples6(m, triples, &ntriples)){
        fprintf(out, "# failed to build valid vertex triples\n");
        free(triples);
        return;
    }
    for(int i=0;i<ntriples;i++){
        fprintf(out, "%s, %s, %s\n", triples[i].v[0], triples[i].v[1], triples[i].v[2]);
    }
    free(triples);
}

static void print_valid_vertex_triples6(const Hex5Model *m){
    fprint_valid_vertex_triples6(stdout, m);
}

static void print_model_tile_words6(const Hex5Model *m){
    printf("---[tiles]---\n");
    for(int t=0;t<m->ntiles;t++){
        for(int k=0;k<HEX5_SIDES;k++){
            char a[HEX5_TOK], b[HEX5_TOK];
            (void)b;
            if(k) printf(", ");
            if(split_edge(m->tiles[t].e[k], a, b)) printf("%s", a);
            else printf("%s", m->tiles[t].e[k]);
        }
        printf("\n");
    }
}

static void print_model_rule_words6(const Hex5Model *m){
    printf("---[edge rules directed lex]---\n");
    for(int r=0;r<m->nrules;r++) printf("%s = %s\n", m->rule_a[r], m->rule_b[r]);
}

static void print_model_letter_file6(const Hex5Model *m){
    print_model_tile_words6(m);
    printf("\n");
    print_model_rule_words6(m);
    printf("\n");
    print_valid_vertex_triples6(m);
}

static void fprint_model_tile_words6(FILE *out, const Hex5Model *m){
    fprintf(out, "---[tiles]---\n");
    for(int t=0;t<m->ntiles;t++){
        for(int k=0;k<HEX5_SIDES;k++){
            char a[HEX5_TOK], b[HEX5_TOK];
            (void)b;
            if(k) fprintf(out, ", ");
            if(split_edge(m->tiles[t].e[k], a, b)) fprintf(out, "%s", a);
            else fprintf(out, "%s", m->tiles[t].e[k]);
        }
        fprintf(out, "\n");
    }
}

static void fprint_model_rule_words6(FILE *out, const Hex5Model *m){
    fprintf(out, "---[edge rules directed lex]---\n");
    for(int r=0;r<m->nrules;r++) fprintf(out, "%s = %s\n", m->rule_a[r], m->rule_b[r]);
}

static void fprint_model_letter_file6(FILE *out, const Hex5Model *m){
    fprint_model_tile_words6(out, m);
    fprintf(out, "\n");
    fprint_model_rule_words6(out, m);
    fprintf(out, "\n");
    fprint_valid_vertex_triples6(out, m);
}

static void print_dsu_classes_from_path6(const char *path){
    Dsu6 d;
    char buf[DFS6_PATH];
    int printed = 0;
    d.n = 0;
    snprintf(buf, sizeof(buf), "%s", path ? path : "");
    for(char *tok=strtok(buf, " \t,"); tok; tok=strtok(NULL, " \t,")){
        char a[HEX5_TOK], b[HEX5_TOK];
        int ia=-1, ib=-1, ra, rb;
        if(!parse_path_step6(tok, a, b)) continue;
        for(int i=0;i<d.n;i++){
            if(strcmp(d.item[i].name,a)==0) ia=i;
            if(strcmp(d.item[i].name,b)==0) ib=i;
        }
        if(ia<0 && d.n<MAX_LABELS){ ia=d.n; copy_tok(d.item[d.n].name,a); d.item[d.n].parent=d.n; d.n++; }
        if(ib<0 && d.n<MAX_LABELS){ ib=d.n; copy_tok(d.item[d.n].name,b); d.item[d.n].parent=d.n; d.n++; }
        ra=ia; while(d.item[ra].parent!=ra) ra=d.item[ra].parent;
        rb=ib; while(d.item[rb].parent!=rb) rb=d.item[rb].parent;
        if(ra!=rb){ if(strcmp(d.item[rb].name,d.item[ra].name)<0){ int t=ra; ra=rb; rb=t; } d.item[rb].parent=ra; }
    }
    printf("vertex_equalities\n");
    for(int i=0;i<d.n;i++){
        int r = i;
        while(d.item[r].parent!=r) r=d.item[r].parent;
        if(r != i) continue;
        int n = 0;
        for(int j=0;j<d.n;j++){ int x=j; while(d.item[x].parent!=x) x=d.item[x].parent; if(x == r) n++; }
        if(n <= 1) continue;
        printf("  ");
        for(int j=0;j<d.n;j++){ int x=j; while(d.item[x].parent!=x) x=d.item[x].parent; if(x == r){
            printf("%s%s", printed && 0 ? "" : "", n ? d.item[j].name : d.item[j].name);
            n--;
            if(n) printf(" = ");
        }}
        printf("\n");
        printed++;
    }
    if(!printed) printf("  none\n");
}


static void fprint_dsu_classes_from_path6(FILE *out, const char *path){
    Dsu6 d;
    char buf[DFS6_PATH];
    int printed = 0;
    d.n = 0;
    snprintf(buf, sizeof(buf), "%s", path ? path : "");
    for(char *tok=strtok(buf, " \t,"); tok; tok=strtok(NULL, " \t,")){
        char a[HEX5_TOK], b[HEX5_TOK];
        int ia=-1, ib=-1, ra, rb;
        if(!parse_path_step6(tok, a, b)) continue;
        for(int i=0;i<d.n;i++){
            if(strcmp(d.item[i].name,a)==0) ia=i;
            if(strcmp(d.item[i].name,b)==0) ib=i;
        }
        if(ia<0 && d.n<MAX_LABELS){ ia=d.n; copy_tok(d.item[d.n].name,a); d.item[d.n].parent=d.n; d.n++; }
        if(ib<0 && d.n<MAX_LABELS){ ib=d.n; copy_tok(d.item[d.n].name,b); d.item[d.n].parent=d.n; d.n++; }
        ra=ia; while(d.item[ra].parent!=ra) ra=d.item[ra].parent;
        rb=ib; while(d.item[rb].parent!=rb) rb=d.item[rb].parent;
        if(ra!=rb){ if(strcmp(d.item[rb].name,d.item[ra].name)<0){ int t=ra; ra=rb; rb=t; } d.item[rb].parent=ra; }
    }
    fprintf(out, "---[vertex equalities]---\n");
    for(int i=0;i<d.n;i++){
        int r = i;
        while(d.item[r].parent!=r) r=d.item[r].parent;
        if(r != i) continue;
        int n = 0;
        for(int j=0;j<d.n;j++){ int x=j; while(d.item[x].parent!=x) x=d.item[x].parent; if(x == r) n++; }
        if(n <= 1) continue;
        fprintf(out, "  ");
        for(int j=0;j<d.n;j++){ int x=j; while(d.item[x].parent!=x) x=d.item[x].parent; if(x == r){
            fprintf(out, "%s", d.item[j].name);
            n--;
            if(n) fprintf(out, " = ");
        }}
        fprintf(out, "\n");
        printed++;
    }
    if(!printed) fprintf(out, "  none\n");
}

static int rebuild_path_state6(const Hex5Model *base, const char *path,
                               DfsState6 *out,
                               int root_of[DFS6_MAX_INNER],
                               int off_to_root[DFS6_MAX_INNER]){
    DfsState6 cur;
    char buf[DFS6_PATH];
    memset(&cur, 0, sizeof(cur));
    cur.model = *base;
    cur.tiles = base->ntiles;
    cur.parent = -1;
    cur.path[0] = '\0';
    if(g_have_root_inner_override6) cur.inner = g_root_inner_override6;
    else if(!build_small_inner_for_model6(base, &cur.inner)) return 0;
    for(int i=0;i<DFS6_MAX_INNER;i++){ root_of[i] = -1; off_to_root[i] = 0; }
    for(int i=0;i<cur.inner.nrows;i++){ root_of[i] = i; off_to_root[i] = 0; }

    snprintf(buf, sizeof(buf), "%s", path ? path : "");
    for(char *tok=strtok(buf, " \t"); tok; tok=strtok(NULL, " \t")){
        char la[HEX5_TOK], lb[HEX5_TOK];
        MergeSet6 ms;
        Hex5Model red;
        SmallInner6 image;
        int new_root[DFS6_MAX_INNER], new_off[DFS6_MAX_INNER];
        if(!parse_path_step6(tok, la, lb)) return 0;
        pair_merge(la, lb, &ms);
        memset(&red, 0, sizeof(red));
        if(!transform_model(&cur.model, &ms, &red)) return 0;
        if(!map_small_inner6(&cur.inner, &ms, &image)) return 0;
        if(image.nrows != cur.inner.nrows) return 0;
        for(int i=0;i<DFS6_MAX_INNER;i++){ new_root[i] = -1; new_off[i] = 0; }
        for(int i=0;i<cur.inner.nrows;i++){
            Inner5 mapped;
            int off = 0, ci;
            if(!map_one_inner_offset6(&cur.inner.rows[i], &ms, &mapped, &off)) return 0;
            ci = small_index_inner6(&image, &mapped);
            if(ci < 0 || new_root[ci] >= 0) return 0;
            new_root[ci] = root_of[i];
            new_off[ci] = (off_to_root[i] + off) % HEX5_SIDES;
        }
        cur.model = red;
        cur.inner = image;
        cur.tiles = red.ntiles;
        cur.depth++;
        if(cur.path[0]){
            char next_path[DFS6_PATH];
            snprintf(next_path, sizeof(next_path), "%.900s %.100s", cur.path, tok);
            snprintf(cur.path, sizeof(cur.path), "%s", next_path);
        } else {
            snprintf(cur.path, sizeof(cur.path), "%.100s", tok);
        }
        for(int i=0;i<DFS6_MAX_INNER;i++){ root_of[i] = new_root[i]; off_to_root[i] = new_off[i]; }
    }
    *out = cur;
    return 1;
}

static int write_refined_output6(const char *path, const Hex5Model *base,
                                 const DfsState6 *best,
                                 int nbest, int cancel_depth){
    FILE *out;
    DfsState6 rebuilt;
    int root_of[DFS6_MAX_INNER], off_to_root[DFS6_MAX_INNER];
    int matrix_a = -1, matrix_b = -1;
    SmallInner6 root;
    int root_ok;
    if(!path || !*path) return 1;
    out = fopen(path, "w");
    if(!out){
        fprintf(stderr, "rl6_refine: failed to open output %s\n", path);
        return 0;
    }
    if(!rebuild_path_state6(base, best->path, &rebuilt, root_of, off_to_root)){
        fprintf(stderr, "rl6_refine: failed to rebuild best path for output\n");
        fclose(out);
        return 0;
    }
    root_ok = g_have_root_inner_override6 ? (root = g_root_inner_override6, 1)
                                          : build_small_inner_for_model6(base, &root);
    fprintf(out, "# RL6 refined full-letter model\n");
    fprintf(out, "# source: data/rl6/unified_model.dat\n");
    fprintf(out, "# recovery: apply the binary projection path below to the source model.\n");
    fprintf(out, "# cancel_depth=%d\n", cancel_depth);
    fprintf(out, "# best_tiles=%d\n", best->tiles);
    fprintf(out, "# best_ties=%d\n", nbest);
    fprintf(out, "# dead_extra_hexes=%d\n", best->dead_extra_hexes);
    fprintf(out, "# path=%s\n\n", best->path[0] ? best->path : "root");
    fprint_dsu_classes_from_path6(out, best->path);
    fprintf(out, "\n---[projection path]---\n%s\n\n", best->path[0] ? best->path : "root");
    fprint_model_letter_file6(out, &best->model);
    (void)root_ok;
    (void)root;
    (void)matrix_a;
    (void)matrix_b;
    fclose(out);
    return 1;
}

static int certify_path6(const Hex5Model *base, const char *model, const char *path){
    DfsState6 cur, next;
    int root_of[DFS6_MAX_INNER], off_to_root[DFS6_MAX_INNER];
    int step = 0;
    char buf[DFS6_PATH];
    int ok = 1;

    memset(&cur, 0, sizeof(cur));
    cur.model = *base;
    cur.tiles = base->ntiles;
    cur.parent = -1;
    if(g_have_root_inner_override6){
        cur.inner = g_root_inner_override6;
    } else if(!build_small_inner_for_model6(base, &cur.inner)){
        fprintf(stderr, "rl6_refine: failed to build root inner set\n");
        return 0;
    }
    for(int i=0;i<cur.inner.nrows;i++){ root_of[i] = i; off_to_root[i] = 0; }

    printf("RL6 refine certificate (%s)\n", model);
    printf("root tiles=%d inner=%d\n", cur.tiles, cur.inner.nrows);
    printf("projection path: %s\n", path && *path ? path : "root");
    printf("criterion: image of 25 cycles must be injective and present; extras must die in one-layer extension; final slot matrix must equal root through recovery map.\n");

    snprintf(buf, sizeof(buf), "%s", path ? path : "");
    for(char *tok=strtok(buf, " \t"); tok; tok=strtok(NULL, " \t")){
        char la[HEX5_TOK], lb[HEX5_TOK];
        MergeSet6 ms;
        Hex5Model red;
        Inner5Set *red_full = calloc(1, sizeof(*red_full));
        SmallInner6 image;
        int new_root[DFS6_MAX_INNER], new_off[DFS6_MAX_INNER];
        int missing = 0, ec = 0, er = 0, live = 0, dead = 0, esc = 0;
        int fibers[DFS6_MAX_INNER];
        int max_fiber = 0, merged_fibers = 0;
        int matrix_a = -1, matrix_b = -1;

        if(!parse_path_step6(tok, la, lb)){
            fprintf(stderr, "rl6_refine: bad path token [%s]\n", tok);
            ok = 0;
            break;
        }
        pair_merge(la, lb, &ms);
        memset(&red, 0, sizeof(red));
        if(!transform_model(&cur.model, &ms, &red) || !red_full){
            fprintf(stderr, "rl6_refine: failed transform at %s\n", tok);
            free(red_full);
            ok = 0;
            break;
        }
        image.nrows = 0;
        memset(fibers, 0, sizeof(fibers));
        for(int i=0;i<cur.inner.nrows;i++){
            Inner5 mapped;
            int off = 0, ci;
            if(!map_one_inner_offset6(&cur.inner.rows[i], &ms, &mapped, &off)){
                ok = 0;
                break;
            }
            if(!small_add_inner6(&image, &mapped)){
                ok = 0;
                break;
            }
            ci = small_index_inner6(&image, &mapped);
            if(ci < 0){ ok = 0; break; }
            fibers[ci]++;
        }
        if(!ok){ free(red_full); break; }
        for(int i=0;i<image.nrows;i++){
            if(fibers[i] > max_fiber) max_fiber = fibers[i];
            if(fibers[i] > 1) merged_fibers++;
        }
        if(image.nrows != cur.inner.nrows || merged_fibers != 0){
            printf("step %d merge=%s REJECT fiber_merge image=%d parent_inner=%d merged_fibers=%d max_fiber=%d\n",
                   step + 1, tok, image.nrows, cur.inner.nrows, merged_fibers, max_fiber);
            free(red_full);
            ok = 0;
            break;
        }
        for(int i=0;i<DFS6_MAX_INNER;i++){ new_root[i] = -1; new_off[i] = 0; }
        for(int i=0;i<cur.inner.nrows;i++){
            Inner5 mapped;
            int off = 0, ci;
            map_one_inner_offset6(&cur.inner.rows[i], &ms, &mapped, &off);
            ci = small_index_inner6(&image, &mapped);
            if(ci < 0){ ok = 0; break; }
            new_root[ci] = root_of[i];
            new_off[ci] = (off_to_root[i] + off) % HEX5_SIDES;
        }
        if(!ok){ free(red_full); break; }
        if(!build_inner_set_for_model(&red, red_full)){
            fprintf(stderr, "rl6_refine: failed to rebuild inner set at %s\n", tok);
            free(red_full);
            ok = 0;
            break;
        }
        if(!full_contains_small_image6(red_full, &image, &missing) || missing){
            printf("step %d merge=%s REJECT missing_image=%d\n", step + 1, tok, missing);
            free(red_full);
            ok = 0;
            break;
        }
        if(!count_extra_extension6(&red, &image, &ec, &er, &live, &dead, &esc)){
            fprintf(stderr, "rl6_refine: failed extra probe at %s\n", tok);
            free(red_full);
            ok = 0;
            break;
        }
        if(live || esc){
            printf("step %d merge=%s REJECT extras_not_dead extra_hexes=%d witnesses=%d live=%d dead=%d escape=%d\n",
                   step + 1, tok, ec, er, live, dead, esc);
            free(red_full);
            ok = 0;
            break;
        }
        next.model = red;
        next.inner = image;
        next.tiles = red.ntiles;
        next.depth = cur.depth + 1;
        next.dead_extra_hexes = cur.dead_extra_hexes + ec;
        next.parent = -1;
        if(cur.path[0]){
            snprintf(next.path, sizeof(next.path), "%.900s %.100s", cur.path, tok);
        } else {
            snprintf(next.path, sizeof(next.path), "%.100s", tok);
        }

        printf("step %d merge=%s ACCEPT tiles %d->%d inner=%d raw_inner=%d extra_hexes=%d witnesses=%d dead_extensions=%d remember=unique\n",
               step + 1, tok, cur.tiles, next.tiles, next.inner.nrows, red_full->nrows, ec, er, dead);
        if(!matrix_equal_by_remember6(&cur.inner, &next.inner, new_root, new_off, &matrix_a, &matrix_b)){
            printf("step %d merge=%s REJECT matrix_mismatch block=%d,%d\n", step + 1, tok, matrix_a, matrix_b);
            free(red_full);
            ok = 0;
            break;
        }
        printf("  matrix_check=preserved_through_recovery_map\n");

        cur = next;
        for(int i=0;i<cur.inner.nrows;i++){ root_of[i] = new_root[i]; off_to_root[i] = new_off[i]; }
        free(red_full);
        step++;
    }

    if(ok){
        int matrix_a = -1, matrix_b = -1;
        printf("final tiles=%d inner=%d dead_extra_hexes=%d steps=%d\n", cur.tiles, cur.inner.nrows, cur.dead_extra_hexes, step);
        /* Rebuild the root matrix from the saved root rows in a separate root state. */
        {
            SmallInner6 root;
            int root_ok = 1;
            if(g_have_root_inner_override6) root = g_root_inner_override6;
            else root_ok = build_small_inner_for_model6(base, &root);
            if(!root_ok ||
               !matrix_equal_by_remember6(&root, &cur.inner, root_of, off_to_root, &matrix_a, &matrix_b)){
                printf("final_matrix_check=FAIL block=%d,%d\n", matrix_a, matrix_b);
                ok = 0;
            } else {
                printf("final_matrix_check=PASS unique_remember_rows=25\n");
            }
        }
        print_dsu_classes_from_path6(path);
        printf("final_tiles_edges\n");
        print_model_tiles6(&cur.model);
        printf("final_model_comparable_to_unified_input\n");
        print_model_letter_file6(&cur.model);
    }
    return ok;
}


static void emit_state6(const char *model, int idx, const DfsState6 *s){
    printf("RL6_STATE model=%s idx=%d parent=%d depth=%d tiles=%d inner=%d dead_extra_hexes=%d map=%s\n",
           model, idx, s->parent, s->depth, s->tiles, s->inner.nrows, s->dead_extra_hexes,
           s->path[0] ? s->path : "root");
    for(int t=0;t<s->model.ntiles;t++){
        printf("TILE");
        for(int k=0;k<HEX5_SIDES;k++) printf(" %s", s->model.tiles[t].e[k]);
        printf("\n");
    }
    print_model_tile_words6(&s->model);
    for(int r=0;r<s->model.nrules;r++){
        printf("RULE %s %s\n", s->model.rule_a[r], s->model.rule_b[r]);
    }
    for(int c=0;c<s->inner.nrows;c++){
        printf("CYCLE");
        for(int k=0;k<HEX5_SIDES;k++) printf(" %s|%s", s->inner.rows[c].a[k], s->inner.rows[c].b[k]);
        printf("\n");
    }
    printf("END_STATE\n");
}

static void emit_ancestry6(const char *model, const DfsState6 *states, int nstates, int idx){
    int chain[DFS6_MAX_STATES];
    int n = 0;
    int cur = idx;
    while(cur >= 0 && cur < nstates && n < DFS6_MAX_STATES){
        chain[n++] = cur;
        cur = states[cur].parent;
    }
    printf("ANCESTRY model=%s idx=%d length=%d\n", model, idx, n);
    for(int i=n-1;i>=0;i--){
        const DfsState6 *s = &states[chain[i]];
        printf("  idx=%d parent=%d depth=%d tiles=%d dead_extra_hexes=%d map=%s\n",
               chain[i], s->parent, s->depth, s->tiles, s->dead_extra_hexes,
               s->path[0] ? s->path : "root");
    }
}

static void run_dfs6(const Hex5Model *base, const char *model, int max_depth, int slot_only){
    int exhaustive = max_depth < 0;
    if(exhaustive) max_depth = MAX_MERGES;
    DfsState6 *states = calloc(DFS6_MAX_STATES, sizeof(*states));
    int nstates = 1;
    int best_tiles;
    if(!states){ fprintf(stderr, "out of memory\n"); return; }
    states[0].model = *base;
    if(g_have_root_inner_override6){
        states[0].inner = g_root_inner_override6;
    } else if(!build_small_inner_for_model6(base, &states[0].inner)){
        fprintf(stderr, "failed to build root inner set\n");
        free(states);
        return;
    }
    states[0].tiles = base->ntiles;
    states[0].depth = 0;
    states[0].path[0] = '\0';
    states[0].parent = -1;
    best_tiles = base->ntiles;

    printf("RL6 DFS reductions (%s)\n", model);
    g_dfs_exact_inner = 0;
    printf("pairs=%s search=%s cancel_depth=%d\n",
           slot_only ? "tile-slot" : "all",
           exhaustive ? "frontier" : "depth-limited",
           g_dfs_cancel_depth);
    printf("policy: extras must die; recovery checked each step\n");
    if(!exhaustive) printf("search_depth=%d\n", max_depth);
    printf("root tiles=%d inner=%d\n", states[0].tiles, states[0].inner.nrows);
    printf("%-4s %-3s %-5s %-5s %-4s %s\n",
           "idx", "dep", "tiles", "extra", "wit", "map");
    printf("%-4d %-3d %-5d %-5d %-4d %s\n", 0, 0, states[0].tiles, 0, 0, "root");
    if(g_dfs_emit_target > 0 && states[0].tiles == g_dfs_emit_target){
        emit_state6(model, 0, &states[0]);
        emit_ancestry6(model, states, nstates, 0);
        g_dfs_emit_count++;
    }

    for(int idx=0; idx<nstates; idx++){
        DfsState6 *s = &states[idx];
        LabelSet6 *labels = calloc(1, sizeof(*labels));
        LabelPair6 *pairs = calloc(4096, sizeof(*pairs));
        DfsState6 *cand = calloc(1, sizeof(*cand));
        int npairs = 0;
        long attempts = 0, accepted = 0;
        if(!labels || !pairs || !cand){
            free(labels); free(pairs); free(cand);
            fprintf(stderr, "out of memory\n");
            break;
        }
        if(s->depth >= max_depth){ free(labels); free(pairs); free(cand); continue; }
        if(slot_only){
            if(!collect_model_slot_pairs6(&s->model, pairs, &npairs)){ free(labels); free(pairs); free(cand); continue; }
        } else {
            if(!collect_labels(&s->model, labels)){ free(labels); free(pairs); free(cand); continue; }
            for(int i=0;i<labels->n;i++){
                for(int j=i+1;j<labels->n;j++){
                    if(!add_label_pair6(pairs, &npairs, labels->s[i], labels->s[j])) continue;
                }
            }
        }
        for(int pi=0; pi<npairs; pi++){
            int ec = 0, er = 0, ed = 0;
            int dup = 0;
            attempts++;
            memset(cand, 0, sizeof(*cand));
            g_dfs_probe_states = BOUNDARY5_MAX_STATES;
            if(!try_merge_state6(s, pairs[pi].a, pairs[pi].b, cand, &ec, &er, &ed)) continue;
            cand->parent = idx;
            accepted++;
            for(int k=0;k<nstates;k++){
                if(cand->tiles == states[k].tiles && same_model_tiles6(&cand->model, &states[k].model)){
                    dup = 1;
                    break;
                }
            }
            if(dup) continue;
            if(nstates >= DFS6_MAX_STATES){
                printf("dfs_truncated max_states=%d\n", DFS6_MAX_STATES);
                idx = nstates;
                break;
            }
            states[nstates] = *cand;
            if(cand->tiles < best_tiles) best_tiles = cand->tiles;
            printf("%-4d %-3d %-5d %-5d %-4d %s\n",
                   nstates, cand->depth, cand->tiles, ec, er, cand->path);
            if(g_dfs_emit_target > 0 && cand->tiles == g_dfs_emit_target){
                emit_state6(model, nstates, cand);
                emit_ancestry6(model, states, nstates + 1, nstates);
                g_dfs_emit_count++;
            }
            fflush(stdout);
            nstates++;
        }
        free(labels); free(pairs); free(cand);
        if(attempts > 0){
            printf("done idx=%d dep=%d tiles=%d tries=%ld ok=%ld states=%d best=%d\n",
                   idx, s->depth, s->tiles, attempts, accepted, nstates, best_tiles);
        }
    }
    int max_seen_depth = 0;
    for(int i=0;i<nstates;i++) if(states[i].depth > max_seen_depth) max_seen_depth = states[i].depth;
    printf("dfs_summary states=%d best_tiles=%d cap=%d\n",
           nstates, best_tiles, DFS6_MAX_STATES);
    printf("  pairs=%s search=%s cancel_depth=%d emit=%d/%d\n",
           slot_only ? "tile-slot" : "all",
           exhaustive ? "frontier" : "depth-limited",
           g_dfs_cancel_depth, g_dfs_emit_count, g_dfs_emit_target);
    printf("best_by_depth\n");
    for(int d=0; d<=max_seen_depth; d++){
        int best_d = 999999, count_d = 0;
        for(int i=0;i<nstates;i++){
            if(states[i].depth != d) continue;
            count_d++;
            if(states[i].tiles < best_d) best_d = states[i].tiles;
        }
        if(count_d) printf("  depth=%d states=%d best_tiles=%d\n", d, count_d, best_d);
    }
    printf("best_states\n");
    int first_best = -1, nbest = 0;
    for(int i=0;i<nstates;i++){
        if(states[i].tiles == best_tiles){
            if(first_best < 0) first_best = i;
            nbest++;
            printf("  idx=%d parent=%d dep=%d tiles=%d inner=%d dead_extra=%d\n",
                   i, states[i].parent, states[i].depth, states[i].tiles,
                   states[i].inner.nrows, states[i].dead_extra_hexes);
            printf("  map=%s\n", states[i].path[0] ? states[i].path : "root");
            printf("  comparable_tiles idx=%d\n", i);
            print_model_tile_words6(&states[i].model);
        }
    }
    if(g_refine_write_output && first_best >= 0){
        if(write_refined_output6(g_refine_output_path, base, &states[first_best], nbest, g_dfs_cancel_depth)){
            printf("wrote_best_output=%s\n", g_refine_output_path);
            printf("  idx=%d tiles=%d ties=%d\n",
                   first_best, states[first_best].tiles, nbest);
        } else {
            printf("output_escape path=%s reason=write_failed\n", g_refine_output_path ? g_refine_output_path : "(null)");
        }
    }
    free(states);
}

static int load_model6(const char *model, Hex5Model *m){
    const char *path = model_path(model);
    if(!path) return 0;
    if(strcmp(model, "unified") == 0 || strcmp(model, "full") == 0 || strcmp(model, "letter") == 0){
        return load_unified_model6(path, m);
    }
    if(!hex5_parse_file(path, m)) return 0;
    return apply_errata(m, model);
}

static void run_offset_map6(void){
    const char *names[3] = {"basic", "super", "overlap"};
    Inner5Set *sets[3] = {0,0,0};
    Hex5Model *models[3] = {0,0,0};
    for(int i=0;i<3;i++){
        models[i] = calloc(1, sizeof(*models[i]));
        sets[i] = calloc(1, sizeof(*sets[i]));
        if(!models[i] || !sets[i] || !load_model6(names[i], models[i]) || !build_inner_set_for_model(models[i], sets[i])){
            fprintf(stderr, "failed offset input %s\n", names[i]);
            goto done;
        }
    }
    printf("RL6 cyclic offset map probe\n");
    printf("note=canonical cycle comparison is offset-free; this reports raw token overlap by offset before any relabeling.\n");
    printf("%-15s %-6s %-6s %-6s\n", "pair", "offset", "common", "target");
    for(int aidx=0;aidx<3;aidx++){
        for(int bidx=aidx+1;bidx<3;bidx++){
            int best_off = 0, best = -1;
            for(int off=0;off<HEX5_SIDES;off++){
                int common = 0;
                for(int i=0;i<sets[aidx]->nrows;i++){
                    Inner5 r, c;
                    inner_rotate6(&sets[aidx]->rows[i], off, &r);
                    inner_canon6(&r, &c);
                    if(contains_inner6(sets[bidx], &c)) common++;
                }
                if(common > best){ best = common; best_off = off; }
            }
            printf("%s->%s %6d %6d %6d\n", names[aidx], names[bidx], best_off, best, sets[aidx]->nrows);
        }
    }
done:
    for(int i=0;i<3;i++){ free(models[i]); free(sets[i]); }
}

static void print_scan_row(const char *name, const ReduceStats6 *st, int target_inner){
    printf("%-15s %5d %6d %6d %6d %7d %6d %6d %s\n",
           name, st->tiles, st->rings, st->inner, st->cycle_vfigs,
           st->standard_vfigs, st->false_pos, st->extra,
           st->inner == target_inner ? "keep" : "change");
}

static void print_fig(const Hex5Model *m, const VFig5 *f){
    printf("(%d,%d,%d) ", f->h[0], f->h[1], f->h[2]);
    print_hex(m, f->h[0]);
    printf(" | ");
    print_hex(m, f->h[1]);
    printf(" | ");
    print_hex(m, f->h[2]);
    printf("\n");
}

int main(int argc, char **argv){
    int public_refine = is_refine_invocation6(argv[0]);
    const char *model = public_refine ? "unified" : "super";
    const char *path;
    int print_missing = 0;
    int print_inner = 0;
    int probe_extra = 0;
    int probe_dimer = 0;
    int probe_cycle_pair = 0;
    int probe_cycle_depth = 1;
    int probe_cycle_states = 1024;
    int probe_cycle_a = -1, probe_cycle_b = -1;
    char probe_edge_a[HEX5_TOK], probe_edge_b[HEX5_TOK];
    int scan_pairs = 0;
    int dfs_depth = 0;
    int dfs_slots = 0;
    int offset_map = 0;
    int alignment = 0;
    int emit_target = 0;
    const char *certify_path = NULL;
    int cluster_symbols = 0;
    int have_merge = 0;
    MergeSet6 merge;
    memset(&merge, 0, sizeof(merge));
    Hex5Model *m = NULL;
    Attach5Dict *a = NULL;
    VComp5Dict *v = NULL;
    BComp5Result *b = NULL;
    VFig5 *cycle = NULL;
    Inner5Set *inner = NULL;
    int ncycle = 0;
    long raw_cycle = 0;
    int missing = 0;
    int extra = 0;

    if(public_refine){
        dfs_slots = 1;
        dfs_depth = -1;
        g_dfs_cancel_depth = 1;
        g_refine_write_output = 1;
    }

    for(int i=1;i<argc;i++){
        if(strcmp(argv[i], "--model") == 0 && i + 1 < argc){
            if(public_refine){
                fprintf(stderr, "rl6_refine always uses the full RL6 letter model; --model is not a public option.\n");
                return 2;
            }
            model = argv[++i];
        } else if(strcmp(argv[i], "--search-depth") == 0 && i + 1 < argc){
            dfs_depth = atoi(argv[++i]);
            if(dfs_depth < 0) dfs_depth = 0;
            dfs_slots = 1;
        } else if(strcmp(argv[i], "--cancel-depth") == 0 && i + 1 < argc){
            g_dfs_cancel_depth = atoi(argv[++i]);
            if(g_dfs_cancel_depth < 1) g_dfs_cancel_depth = 1;
            if(g_dfs_cancel_depth > 4) g_dfs_cancel_depth = 4;
        } else if(strcmp(argv[i], "--output") == 0 && i + 1 < argc){
            g_refine_output_path = argv[++i];
            g_refine_write_output = 1;
        } else if(strcmp(argv[i], "--cluster-symbols") == 0){
            cluster_symbols = 1;
        } else if(strcmp(argv[i], "--merge") == 0 && i + 1 < argc){
            if(!parse_merge(argv[++i], &merge)){ usage(argv[0]); return 2; }
            have_merge = 1;
        } else if(strcmp(argv[i], "--scan-pairs") == 0){
            scan_pairs = 1;
        } else if(strcmp(argv[i], "--dfs") == 0 && i + 1 < argc){
            dfs_depth = atoi(argv[++i]);
            if(dfs_depth < 0) dfs_depth = 0;
        } else if(strcmp(argv[i], "--dfs-slots") == 0){
            dfs_slots = 1;
            dfs_depth = 64;
            if(i + 1 < argc && argv[i+1][0] >= '0' && argv[i+1][0] <= '9'){
                dfs_depth = atoi(argv[++i]);
                if(dfs_depth < 0) dfs_depth = 0;
            }
        } else if(strcmp(argv[i], "--emit-state") == 0 && i + 1 < argc){
            emit_target = atoi(argv[++i]);
            if(emit_target < 0) emit_target = 0;
        } else if(strcmp(argv[i], "--certify-path") == 0 && i + 1 < argc){
            certify_path = argv[++i];
        } else if(strcmp(argv[i], "--alignment") == 0){
            alignment = 1;
        } else if(strcmp(argv[i], "--offset-map") == 0){
            offset_map = 1;
        } else if(strcmp(argv[i], "--print-missing") == 0 && i + 1 < argc){
            print_missing = atoi(argv[++i]);
            if(print_missing < 0) print_missing = 0;
        } else if(strcmp(argv[i], "--print-inner") == 0 && i + 1 < argc){
            print_inner = atoi(argv[++i]);
            if(print_inner < 0) print_inner = 0;
        } else if(strcmp(argv[i], "--probe-extra") == 0 && i + 1 < argc){
            probe_extra = atoi(argv[++i]);
            if(probe_extra < 0) probe_extra = 0;
        } else if(strcmp(argv[i], "--probe-dimer") == 0 && i + 2 < argc){
            copy_tok(probe_edge_a, argv[++i]);
            copy_tok(probe_edge_b, argv[++i]);
            probe_dimer = 1;
        } else if(strcmp(argv[i], "--probe-cycle-pair") == 0 && i + 2 < argc){
            probe_cycle_a = atoi(argv[++i]);
            probe_cycle_b = atoi(argv[++i]);
            probe_cycle_pair = 1;
        } else if(strcmp(argv[i], "--probe-depth") == 0 && i + 1 < argc){
            probe_cycle_depth = atoi(argv[++i]);
            if(probe_cycle_depth < 1) probe_cycle_depth = 1;
            if(probe_cycle_depth > 2) probe_cycle_depth = 2;
        } else if(strcmp(argv[i], "--probe-states") == 0 && i + 1 < argc){
            probe_cycle_states = atoi(argv[++i]);
            if(probe_cycle_states < 1) probe_cycle_states = 1;
            if(probe_cycle_states > BOUNDARY5_MAX_STATES) probe_cycle_states = BOUNDARY5_MAX_STATES;
        } else if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0){
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    path = model_path(model);
    if(!path){ fprintf(stderr, "unknown model: %s\n", model); return 2; }
    m = calloc(1, sizeof(*m));
    a = calloc(1, sizeof(*a));
    v = calloc(1, sizeof(*v));
    b = calloc(1, sizeof(*b));
    cycle = calloc(MAX_FIGS, sizeof(*cycle));
    inner = calloc(1, sizeof(*inner));
    if(!m || !a || !v || !b || !cycle || !inner){ fprintf(stderr, "out of memory\n"); return 1; }

    if(strcmp(model, "unified") == 0 || strcmp(model, "full") == 0 || strcmp(model, "letter") == 0){
        if(!load_unified_model6(path, m)){ fprintf(stderr, "failed to parse unified letter model %s\n", path); return 1; }
        if(public_refine){
            if(!build_unified_survivor_inner6(&g_root_inner_override6)){
                fprintf(stderr, "failed to build full-letter survivor cycle set from RL6 unified map\n");
                return 1;
            }
            g_have_root_inner_override6 = 1;
        }
    } else if(cluster_symbols && strcmp(model, "overlap") == 0){
        if(!load_overlap_cluster_model6(path, m)){ fprintf(stderr, "failed to parse cluster model %s\n", path); return 1; }
    } else {
        if(!hex5_parse_file(path, m)){ fprintf(stderr, "failed to parse %s\n", path); return 1; }
        if(!apply_errata(m, model)){ fprintf(stderr, "failed to apply errata\n"); return 1; }
    }

    if(offset_map){
        run_offset_map6();
        free(cycle); free(inner); free(b); free(v); free(a); free(m);
        return 0;
    }

    if(alignment){
        print_alignment6(m, model);
        free(cycle); free(inner); free(b); free(v); free(a); free(m);
        return 0;
    }

    if(probe_dimer){
        probe_dimer_edges6(m, probe_edge_a, probe_edge_b, print_inner);
        free(cycle); free(inner); free(b); free(v); free(a); free(m);
        return 0;
    }

    if(probe_cycle_pair){
        probe_cycle_pair6(m, probe_cycle_a, probe_cycle_b, print_inner, probe_cycle_depth, probe_cycle_states);
        free(cycle); free(inner); free(b); free(v); free(a); free(m);
        return 0;
    }

    if(certify_path){
        int ok = certify_path6(m, model, certify_path);
        free(cycle); free(inner); free(b); free(v); free(a); free(m);
        return ok ? 0 : 1;
    }

    if(emit_target > 0 && dfs_depth <= 0){
        DfsState6 *root = calloc(1, sizeof(*root));
        if(!root){
            fprintf(stderr, "out of memory building root state\n");
            free(cycle); free(inner); free(b); free(v); free(a); free(m);
            return 1;
        }
        root->model = *m;
        root->tiles = m->ntiles;
        root->depth = 0;
        root->parent = -1;
        root->path[0] = '\0';
        if(g_have_root_inner_override6){
            root->inner = g_root_inner_override6;
        } else if(!build_small_inner_for_model6(m, &root->inner)){
            fprintf(stderr, "failed to build root inner set\n");
            free(root);
            free(cycle); free(inner); free(b); free(v); free(a); free(m);
            return 1;
        }
        if(root->tiles != emit_target){
            fprintf(stderr, "rl6_reduce: root state for %s has %d tiles, not requested %d\n",
                    model, root->tiles, emit_target);
            free(root);
            free(cycle); free(inner); free(b); free(v); free(a); free(m);
            return 1;
        }
        emit_state6(model, 0, root);
        free(root);
        free(cycle); free(inner); free(b); free(v); free(a); free(m);
        return 0;
    }

    if(dfs_depth != 0){
        g_dfs_emit_target = emit_target;
        g_dfs_emit_count = 0;
        run_dfs6(m, model, dfs_depth, dfs_slots);
        free(cycle); free(inner); free(b); free(v); free(a); free(m);
        return 0;
    }

    if(scan_pairs || have_merge){
        Hex5Model *base = calloc(1, sizeof(*base));
        Hex5Model *red = calloc(1, sizeof(*red));
        LabelSet6 *labels = calloc(1, sizeof(*labels));
        ReduceStats6 base_st;
        int rc = 0;
        if(!base || !red || !labels){
            fprintf(stderr, "out of memory\n");
            free(base); free(red); free(labels);
            free(cycle); free(inner); free(b); free(v); free(a); free(m);
            return 1;
        }
        *base = *m;
        if(!compute_stats(base, &base_st)){ fprintf(stderr, "failed to compute base stats\n"); rc = 1; goto scan_done; }
        printf("RL6 inner/outer reduction scan (%s)\n", model);
        if(strcmp(model, "super") == 0 || strcmp(model, "overlap") == 0) printf("errata=dead super 6 overlap 6 7 12 13\n");
        printf("target_inner=%d base_tiles=%d base_rings=%d base_cycle_vfigs=%d base_standard_vfigs=%d\n",
               base_st.inner, base_st.tiles, base_st.rings, base_st.cycle_vfigs, base_st.standard_vfigs);
        printf("%-15s %5s %6s %6s %6s %7s %6s %6s %s\n",
               "merge", "tiles", "rings", "inner", "cycvf", "stdvf", "false", "extra", "inner");
        if(have_merge){
            ReduceStats6 st;
            if(!transform_model(base, &merge, red)){ fprintf(stderr, "failed to transform model\n"); rc = 1; goto scan_done; }
            if(!compute_stats(red, &st)){ fprintf(stderr, "failed to compute reduced stats\n"); rc = 1; goto scan_done; }
            print_scan_row("requested", &st, base_st.inner);
            compare_inner_image(base, red, &merge, print_inner);
            if(probe_extra) probe_extra_inner6(base, red, &merge, probe_extra);
            scan_delete_one_tile6(base, red, &merge);
            scan_delete_one_rulepair6(base, red, &merge);
            goto scan_done;
        }
        if(scan_pairs){
            int printed = 0;
            if(!collect_labels(base, labels)){ fprintf(stderr, "failed to collect labels\n"); rc = 1; goto scan_done; }
            for(int i=0;i<labels->n;i++){
                for(int j=i+1;j<labels->n;j++){
                    MergeSet6 ms;
                    ReduceStats6 st;
                    char name[2 * HEX5_TOK + 4];
                    pair_merge(labels->s[i], labels->s[j], &ms);
                    memset(red, 0, sizeof(*red));
                    if(!transform_model(base, &ms, red)) continue;
                    if(red->ntiles >= base_st.tiles) continue;
                    if(!compute_stats(red, &st)) continue;
                    if(st.tiles < base_st.tiles){
                        snprintf(name, sizeof(name), "%s,%s", labels->s[i], labels->s[j]);
                        print_scan_row(name, &st, base_st.inner);
                        printed++;
                    }
                }
            }
            if(!printed) printf("no pair merger reduced tile count\n");
            goto scan_done;
        }
scan_done:
        free(base); free(red); free(labels);
        free(cycle); free(inner); free(b); free(v); free(a); free(m);
        return rc;
    }

    if(!build_standard(m, a, v, b)){ fprintf(stderr, "failed to build standard data\n"); return 1; }

    qsort(v->figs, (size_t)v->nfigs, sizeof(v->figs[0]), vfig_cmp_struct);
    if(!build_cycle_atlas(m, b, cycle, &ncycle, &raw_cycle)){
        fprintf(stderr, "failed to build cycle atlas\n");
        free(cycle);
        free(inner);
        return 1;
    }
    if(!bcomp5_inner_generated(m, b, inner)){
        fprintf(stderr, "failed to build inner/outer cycles\n");
        free(cycle);
        free(inner);
        return 1;
    }
    qsort(inner->rows, (size_t)inner->nrows, sizeof(inner->rows[0]), inner_cmp_struct);

    printf("RL6 closed-cycle vertex atlas (%s)\n", model);
    if(strcmp(model, "super") == 0 || strcmp(model, "overlap") == 0) printf("errata=dead super 6 overlap 6 7 12 13\n");
    printf("tiles=%d oriented=%d rules=%d attach=%d\n",
           m->ntiles, m->noriented, m->nrules, a->nedges);
    printf("rings=%d inner_outer_cycles=%d\n", b->nrings, inner->nrows);
    printf("ring_figs_raw=%ld cycle_vfigs=%d standard_vfigs=%d\n",
           raw_cycle, ncycle, v->nfigs);
    for(int i=0;i<inner->nrows && i<print_inner;i++) print_inner_cycle(&inner->rows[i]);

    for(int i=0;i<v->nfigs;i++){
        if(!contains_fig(cycle, ncycle, &v->figs[i])){
            if(missing < print_missing) print_fig(m, &v->figs[i]);
            missing++;
        }
    }
    for(int i=0;i<ncycle;i++){
        if(!contains_fig(v->figs, v->nfigs, &cycle[i])) extra++;
    }

    printf("standard_minus_cycle=%d\n", missing);
    printf("cycle_minus_standard=%d\n", extra);
    printf("status=%s\n", (missing == 0 && extra == 0) ? "exact" : "standard_has_false_positives");

    free(cycle);
    free(inner);
    free(b);
    free(v);
    free(a);
    free(m);
    return 0;
}
