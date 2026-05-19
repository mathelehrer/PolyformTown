#define _POSIX_C_SOURCE 200809L
#include "rl5/hex5.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_tok(char dst[HEX5_TOK], const char *src){
    snprintf(dst, HEX5_TOK, "%s", src ? src : "");
}

static void trim(char *s){
    char *p = s;
    size_t n;
    while(*p && isspace((unsigned char)*p)) p++;
    if(p != s) memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while(n && isspace((unsigned char)s[n-1])) s[--n] = '\0';
}

int hex5_cmp(const Hex5 *a, const Hex5 *b){
    for(int i=0;i<HEX5_SIDES;i++){
        int c = strcmp(a->e[i], b->e[i]);
        if(c) return c;
    }
    return 0;
}

int hex5_equal(const Hex5 *a, const Hex5 *b){ return hex5_cmp(a,b) == 0; }

void hex5_rot_left(const Hex5 *in, int k, Hex5 *out){
    k %= HEX5_SIDES; if(k < 0) k += HEX5_SIDES;
    for(int i=0;i<HEX5_SIDES;i++) copy_tok(out->e[i], in->e[(i+k)%HEX5_SIDES]);
}

void hex5_rot_right(const Hex5 *in, int k, Hex5 *out){ hex5_rot_left(in, HEX5_SIDES - (k % HEX5_SIDES), out); }

void hex5_canonical(const Hex5 *in, Hex5 *out){
    Hex5 best, r;
    hex5_rot_left(in, 0, &best);
    for(int k=1;k<HEX5_SIDES;k++){
        hex5_rot_left(in, k, &r);
        if(hex5_cmp(&r, &best) < 0) best = r;
    }
    *out = best;
}

static int add_tile(Hex5Model *m, const Hex5 *h){
    Hex5 c;
    hex5_canonical(h, &c);
    for(int i=0;i<m->ntiles;i++) if(hex5_equal(&m->tiles[i], &c)) return 1;
    if(m->ntiles >= HEX5_MAX_TILES) return 0;
    m->tiles[m->ntiles++] = c;
    return 1;
}

static int add_rule(Hex5Model *m, const char *a, const char *b){
    for(int i=0;i<m->nrules;i++){
        if(strcmp(m->rule_a[i], a)==0 && strcmp(m->rule_b[i], b)==0) return 1;
    }
    if(m->nrules >= HEX5_MAX_RULES) return 0;
    copy_tok(m->rule_a[m->nrules], a);
    copy_tok(m->rule_b[m->nrules], b);
    m->nrules++;
    return 1;
}

static void edge_tok(char dst[HEX5_TOK], const char *a, const char *b){
    snprintf(dst, HEX5_TOK, "%s,%s", a, b);
}

static int parse_e_after(const char *p, char out[HEX5_TOK]){
    const char *q = strstr(p, "e(");
    const char *r;
    int n;
    if(!q) return 0;
    q += 2;
    r = strchr(q, ')');
    if(!r) return 0;
    n = (int)(r - q);
    if(n <= 0 || n >= HEX5_TOK) return 0;
    memcpy(out, q, (size_t)n);
    out[n] = '\0';
    return 1;
}

static int parse_rule_line(Hex5Model *m, const char *line){
    char l[HEX5_TOK], r[HEX5_TOK];
    const char *eq = strchr(line, '=');
    if(!eq) return 1;
    if(!parse_e_after(line, l)) return 1;
    if(!parse_e_after(eq, r)) return 1;
    m->nsource_rules++;
    return add_rule(m, l, r) && add_rule(m, r, l);
}

static int parse_little_hex_line(Hex5Model *m, const char *line){
    char tmp[4096], *hash, *tok[32];
    int n = 0;
    char *save = NULL;
    Hex5 h;
    snprintf(tmp, sizeof(tmp), "%s", line);
    hash = strchr(tmp, '#'); if(hash) *hash = '\0';
    trim(tmp);
    if(!*tmp) return 1;
    for(char *p = strtok_r(tmp, " \t\r\n", &save); p && n < 32; p = strtok_r(NULL, " \t\r\n", &save)) tok[n++] = p;
    if(n < HEX5_SIDES) return 1;
    for(int i=0;i<HEX5_SIDES;i++) edge_tok(h.e[i], tok[i], tok[(i+1)%HEX5_SIDES]);
    return add_tile(m, &h);
}

static int parse_big_hex_line(Hex5Model *m, const char *line){
    char first[HEX5_SIDES][HEX5_TOK];
    char last[HEX5_SIDES][HEX5_TOK];
    int n = 0;
    Hex5 h;
    const char *p = strstr(line, "[[");
    if(!p) return 1;

    while((p = strchr(p, '[')) && n < HEX5_SIDES){
        const char *q;
        int seen = 0;
        if(p[1] == '['){ p++; continue; }
        if(p[1] != 'v') { p++; continue; }
        q = strchr(p, ']');
        if(!q) break;
        for(const char *r = p; r < q; ){
            if(*r == 'v' && isdigit((unsigned char)r[1])){
                int j = 0;
                char buf[HEX5_TOK];
                buf[j++] = *r++;
                while(r < q && isdigit((unsigned char)*r) && j < HEX5_TOK-1) buf[j++] = *r++;
                buf[j] = '\0';
                if(!seen) copy_tok(first[n], buf);
                copy_tok(last[n], buf);
                seen = 1;
            } else r++;
        }
        if(seen) n++;
        p = q + 1;
    }
    if(n != HEX5_SIDES) return 1;
    for(int i=0;i<HEX5_SIDES;i++) edge_tok(h.e[i], first[i], last[(i+1)%HEX5_SIDES]);
    return add_tile(m, &h);
}

static int is_section(const char *line, const char *s){ return strcmp(line, s) == 0; }

int hex5_parse_file(const char *path, Hex5Model *m){
    FILE *f = fopen(path, "r");
    char line[4096];
    int in_little = 0, in_big = 0, in_rules = 0;
    memset(m, 0, sizeof(*m));
    if(!f) return 0;
    while(fgets(line, sizeof(line), f)){
        char tmp[4096];
        snprintf(tmp, sizeof(tmp), "%s", line);
        trim(tmp);
        if(!*tmp) continue;
        if(is_section(tmp, "# Unique hexagons")){ in_little = 1; in_big = in_rules = 0; continue; }
        if(is_section(tmp, "Unique Supertile Hexagons")){ in_big = 1; in_little = in_rules = 0; continue; }
        if(is_section(tmp, "# Edge Matches") || is_section(tmp, "Downmapped Super Hexagon Edge Matches")){
            in_rules = 1; in_little = in_big = 0; continue;
        }
        if(in_rules && strstr(tmp, "# observations=")) { in_rules = 0; continue; }
        if(in_little){
            if(tmp[0] == '#') continue;
            if(!parse_little_hex_line(m, tmp)){ fclose(f); return 0; }
        } else if(in_big){
            if(!isdigit((unsigned char)tmp[0])) continue;
            if(!parse_big_hex_line(m, tmp)){ fclose(f); return 0; }
        } else if(in_rules){
            if(tmp[0] == '#') continue;
            if(!parse_rule_line(m, tmp)){ fclose(f); return 0; }
        }
    }
    fclose(f);
    return hex5_model_finish(m);
}

int hex5_model_finish(Hex5Model *m){
    m->noriented = 0;
    for(int i=0;i<m->ntiles;i++){
        for(int k=0;k<HEX5_SIDES;k++){
            Hex5 r;
            hex5_rot_left(&m->tiles[i], k, &r);
            int seen = 0;
            for(int j=0;j<m->noriented;j++) if(hex5_equal(&m->oriented[j], &r)){ seen = 1; break; }
            if(!seen){
                if(m->noriented >= HEX5_MAX_ORIENTED) return 0;
                m->oriented[m->noriented++] = r;
            }
        }
    }
    return 1;
}
