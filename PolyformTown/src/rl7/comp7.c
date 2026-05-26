#include "comp7.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim(char *s){
    char *p = s;
    while(isspace((unsigned char)*p)) p++;
    if(p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while(n && isspace((unsigned char)s[n-1])) s[--n] = '\0';
}


static int split_csv_tokens(const char *line, Tok *out, int max){
    char buf[MAX_LINE];
    int n = 0;
    snprintf(buf, sizeof(buf), "%s", line ? line : "");
    char *p = buf;
    while(p && *p && n < max){
        char *comma = strchr(p, ',');
        if(comma) *comma = '\0';
        trim(p);
        if(*p) tok_set(&out[n++], p);
        p = comma ? comma + 1 : NULL;
    }
    return n;
}


int load_model(const char *path, Model *m, char *err, size_t errsz){
    FILE *fp = fopen(path, "r");
    char line[MAX_LINE];
    enum {SEC_NONE, SEC_TILES, SEC_RULES, SEC_VWORDS} sec = SEC_NONE;
    memset(m, 0, sizeof(*m));
    if(!fp){
        snprintf(err, errsz, "could not open %s", path);
        return 0;
    }
    while(fgets(line, sizeof(line), fp)){
        trim(line);
        if(!line[0] || line[0] == '#') continue;
        if(strcmp(line, "---[tiles]---") == 0){ sec = SEC_TILES; continue; }
        if(strcmp(line, "---[edge rules directed lex]---") == 0){ sec = SEC_RULES; continue; }
        if(strcmp(line, "---[valid vertex triples]---") == 0){ sec = SEC_VWORDS; continue; }
        if(strncmp(line, "---[", 4) == 0){ sec = SEC_NONE; continue; }

        if(sec == SEC_TILES){
            if(m->ntile >= MAX_TILES){ snprintf(err, errsz, "too many tiles"); fclose(fp); return 0; }
            Tok toks[8];
            int n = split_csv_tokens(line, toks, 8);
            if(n == 6){
                for(int i=0;i<6;i++) m->tiles[m->ntile].v[i] = toks[i];
                m->ntile++;
            }
        } else if(sec == SEC_RULES){
            if(m->nrule >= MAX_RULES){ snprintf(err, errsz, "too many edge rules"); fclose(fp); return 0; }
            char *eq = strchr(line, '=');
            if(!eq) continue;
            *eq = '\0';
            char left[MAX_LINE], right[MAX_LINE];
            snprintf(left, sizeof(left), "%s", line);
            snprintf(right, sizeof(right), "%s", eq + 1);
            Tok l[4], r[4];
            int nl = split_csv_tokens(left, l, 4);
            int nr = split_csv_tokens(right, r, 4);
            if(nl == 2 && nr == 2){
                m->rules[m->nrule].a = l[0];
                m->rules[m->nrule].b = l[1];
                m->rules[m->nrule].c = r[0];
                m->rules[m->nrule].d = r[1];
                m->nrule++;
            }
        } else if(sec == SEC_VWORDS){
            if(m->nvword >= MAX_VWORDS){ snprintf(err, errsz, "too many vertex words"); fclose(fp); return 0; }
            Tok toks[4];
            int n = split_csv_tokens(line, toks, 4);
            if(n == 3){
                for(int i=0;i<3;i++) m->vwords[m->nvword].v[i] = toks[i];
                m->nvword++;
            }
        }
    }
    fclose(fp);
    if(m->ntile <= 0 || m->nrule <= 0 || m->nvword <= 0){
        snprintf(err, errsz, "incomplete RL6 model in %s (tiles=%d rules=%d vwords=%d)",
                 path, m->ntile, m->nrule, m->nvword);
        return 0;
    }
    return 1;
}


Tok tile_sym(const Model *m, int tile, int rot, int slot){
    int idx = (slot + rot) % 6;
    if(idx < 0) idx += 6;
    return m->tiles[tile].v[idx];
}


int tile_matches(const Model *m, const Cell *c, int t, int r){
    for(int k=0;k<6;k++) if(!tok_empty(&c->slot[k])){
        Tok x = tile_sym(m,t,r,k);
        if(!tok_eq(&c->slot[k], &x)) return 0;
    }
    return 1;
}


int vword_matches(const VWord *w, const Tok *known, int nk){
    int used[3]={0,0,0};
    for(int i=0;i<nk;i++){
        int ok=0;
        for(int j=0;j<3;j++) if(!used[j] && tok_eq(&w->v[j],&known[i])){ used[j]=1; ok=1; break; }
        if(!ok) return 0;
    }
    return 1;
}

int vword_is_aaa(const VWord *w){ return strcmp(w->v[0].s,"A")==0 && strcmp(w->v[1].s,"A")==0 && strcmp(w->v[2].s,"A")==0; }


static int edge_rule_exact_exists(const Model *m, const Tok *A, const Tok *B,
                                  const Tok *C, const Tok *D){
    for(int ri=0;ri<m->nrule;ri++){
        const EdgeRule *er=&m->rules[ri];
        if(tok_eq(&er->a,A) && tok_eq(&er->b,B) &&
           tok_eq(&er->c,C) && tok_eq(&er->d,D)) return 1;
    }
    return 0;
}


static int vword_exact_allowed(const Model *m, Tok vals[3]){
    for(int wi=0;wi<m->nvword;wi++){
        int used[3]={0,0,0};
        int ok=1;
        for(int i=0;i<3;i++){
            int hit=0;
            for(int j=0;j<3;j++){
                if(!used[j] && tok_eq(&m->vwords[wi].v[j], &vals[i])){
                    used[j]=1; hit=1; break;
                }
            }
            if(!hit){ ok=0; break; }
        }
        if(ok) return 1;
    }
    return 0;
}


int validate_state_local(const Model *m, const State *s,
                                char *err, size_t errsz){
    /* Every partially labeled cell must still match at least one RL6 tile rotation. */
    for(int ci=0; ci<s->ncell; ci++){
        const Cell *c=&s->cells[ci];
        int known=0;
        for(int k=0;k<6;k++) if(!tok_empty(&c->slot[k])) known++;
        if(!known) continue;
        int candidates=0;
        for(int t=0;t<m->ntile;t++) for(int r=0;r<6;r++) if(tile_matches(m,c,t,r)) candidates++;
        if(candidates <= 0){
            snprintf(err, errsz,
                     "tile check failed: (%d,%d) matches no RL6 tile rotation",
                     c->q,c->r);
            return 0;
        }
    }

    /* Every completed adjacent edge must be backed by the RL6 edge table. */
    for(int ci=0;ci<s->ncell;ci++){
        const Cell *c=&s->cells[ci];
        for(int side=0;side<6;side++){
            int nq, nr; side_neighbor_coord(c->q, c->r, side, &nq, &nr);
            int ni=find_cell(s, nq, nr);
            if(ni < 0) continue;
            int as,bs,cs,ds;
            edge_slots(side,&as,&bs);
            edge_slots((side+3)%6,&cs,&ds);
            const Cell *nc=&s->cells[ni];
            if(tok_empty(&c->slot[as]) || tok_empty(&c->slot[bs]) ||
               tok_empty(&nc->slot[cs]) || tok_empty(&nc->slot[ds])) continue;
            if(!edge_rule_exact_exists(m, &c->slot[as], &c->slot[bs],
                                       &nc->slot[cs], &nc->slot[ds])){
                snprintf(err, errsz,
                         "edge check failed: (%d,%d) side %d has %s,%s = %s,%s",
                         c->q,c->r,side,c->slot[as].s,c->slot[bs].s,
                         nc->slot[cs].s,nc->slot[ds].s);
                return 0;
            }
        }
    }

    /* Every completed 3-valent displayed vertex must be an RL6 vertex word. */
    VGroup groups[256];
    int ng=collect_vgroups(s,groups,256);
    for(int gi=0;gi<ng;gi++){
        VGroup *g=&groups[gi];
        if(g->n != 3) continue;
        Tok vals[3];
        int full=1;
        for(int i=0;i<3;i++){
            vals[i]=s->cells[g->ci[i]].slot[g->sl[i]];
            if(tok_empty(&vals[i])){ full=0; break; }
        }
        if(full && !vword_exact_allowed(m, vals)){
            snprintf(err, errsz,
                     "vertex check failed: %s,%s,%s is not an RL6 vertex word",
                     vals[0].s, vals[1].s, vals[2].s);
            return 0;
        }
    }
    return 1;
}


