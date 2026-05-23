/*
 * hce.c -- RL7 HCE local collar explorer REEPL.
 *
 * Reads the RL6 refined model by default from data/rl6/refined_model.dat,
 * opens a terminal read/erase/eval/print loop, and lets the user apply one
 * displayed local completion rule at a time.
 *
 * This is deliberately a small, inspectable prototype.  The board renderer is
 * generated from state; options are generated from the loaded RL6 model.
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define MAX_CELLS       81
#define MAX_TILES       64
#define MAX_RULES       512
#define MAX_VWORDS      256
#define MAX_ACTIONS     512
#define MAX_ADDS        16
#define MAX_HIST        256
#define MAX_LINE        512
#define TOKEN_MAX       32
#define DEFAULT_MODEL   "data/rl6/refined_model.dat"

/* Display convention: top = slots 3 2 1, bottom = slots 4 5 0. */
static const int display_top[3] = {3, 2, 1};
static const int display_bottom[3] = {4, 5, 0};
static int g_last_raw_actions = 0;

/* Edge side s connects slot s to slot s+1 mod 6.
 *
 * Display top=321 bottom=450 uses staggered rows.  The horizontal
 * sides are fixed:
 *   side 0 = right vertical edge, side 3 = left vertical edge.
 *
 * The four diagonal sides depend on row parity.  A fixed dq/dr table is
 * wrong here: it can make cells such as (0,-1) and (-1,0) look adjacent
 * even though they are two steps apart in the drawn layout. */
static void side_neighbor_coord(int q, int r, int side, int *nq, int *nr){
    int odd = (r & 1) != 0;
    switch(side){
    case 0: *nq = q + 1;          *nr = r;     break; /* right */
    case 3: *nq = q - 1;          *nr = r;     break; /* left */
    case 1: *nq = q + (odd?1:0);  *nr = r + 1; break; /* upper-right */
    case 2: *nq = q + (odd?0:-1); *nr = r + 1; break; /* upper-left */
    case 4: *nq = q + (odd?0:-1); *nr = r - 1; break; /* lower-left */
    case 5: *nq = q + (odd?1:0);  *nr = r - 1; break; /* lower-right */
    default:*nq = q;              *nr = r;     break;
    }
}

typedef struct { char s[TOKEN_MAX]; } Tok;
typedef struct { Tok v[6]; } TileWord;
typedef struct { Tok a, b, c, d; } EdgeRule;
typedef struct { Tok v[3]; } VWord;

typedef struct {
    TileWord tiles[MAX_TILES];
    int ntile;
    EdgeRule rules[MAX_RULES];
    int nrule;
    VWord vwords[MAX_VWORDS];
    int nvword;
} Model;

typedef struct {
    int q, r;
    Tok slot[6];          /* empty token means unknown */
    int tile;             /* -1 unknown, else index into model */
    int rot;
} Cell;

typedef struct {
    Cell cells[MAX_CELLS];
    int ncell;
} State;

typedef struct { int ci, slot; Tok sym; } Add;

typedef struct {
    char title[240];
    char status[240];
    Add add[MAX_ADDS];
    int nadd;
} Action;

typedef struct {
    State s[MAX_HIST];
    int n;
} History;

static int tok_empty(const Tok *t){ return t->s[0] == '\0'; }
static int tok_eq(const Tok *a, const Tok *b){ return strcmp(a->s, b->s) == 0; }
static void tok_set(Tok *t, const char *s){
    if(!s) s = "";
    size_t i = 0;
    while(s[i] && i + 1 < sizeof(t->s)){
        t->s[i] = s[i];
        i++;
    }
    t->s[i] = '\0';
}

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

static int load_model(const char *path, Model *m, char *err, size_t errsz){
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

static int find_cell(const State *s, int q, int r){
    for(int i=0;i<s->ncell;i++) if(s->cells[i].q == q && s->cells[i].r == r) return i;
    return -1;
}
static int add_cell(State *s, int q, int r){
    int i = find_cell(s,q,r);
    if(i >= 0) return i;
    if(s->ncell >= MAX_CELLS) return -1;
    i = s->ncell++;
    s->cells[i].q = q; s->cells[i].r = r;
    for(int k=0;k<6;k++) tok_set(&s->cells[i].slot[k], "");
    s->cells[i].tile = -1; s->cells[i].rot = -1;
    return i;
}

static void init_state(State *s){
    memset(s, 0, sizeof(*s));
    /* Keep the first explorer to the requested 3x3 postcard window:
     *
     *   X X X
     *   X 2 3
     *   X X X
     *
     * where the known edge is between the two central seed cells. */
    for(int r=-1;r<=1;r++) for(int q=-1;q<=1;q++) add_cell(s,q,r);
    /* Keep the right-hand forced hex visible; the first HCE collar needs
     * the extra box at (2,0) for the AAC / outlaw-AAA step. */
    add_cell(s, 2, 0);
    int left = find_cell(s,0,0), right = find_cell(s,1,0);
    if(left >= 0){ tok_set(&s->cells[left].slot[1], "K"); tok_set(&s->cells[left].slot[0], "A"); }
    if(right >= 0){ tok_set(&s->cells[right].slot[3], "H"); tok_set(&s->cells[right].slot[4], "J"); }
}

static Tok tile_sym(const Model *m, int tile, int rot, int slot){
    int idx = (slot + rot) % 6;
    if(idx < 0) idx += 6;
    return m->tiles[tile].v[idx];
}

static int set_cell_slot(Cell *c, int slot, const Tok *sym, char *err, size_t errsz){
    if(slot < 0 || slot >= 6){ snprintf(err, errsz, "invalid slot %d", slot); return 0; }
    if(tok_empty(sym)){ snprintf(err, errsz, "attempted to set empty symbol"); return 0; }
    if(!tok_empty(&c->slot[slot]) && !tok_eq(&c->slot[slot], sym)){
        snprintf(err, errsz, "conflict at (%d,%d).slot%d: has %s, tried %s",
                 c->q, c->r, slot, c->slot[slot].s, sym->s);
        return 0;
    }
    c->slot[slot] = *sym;
    return 1;
}

static int action_has_add(const Action *a, int ci, int slot, const Tok *sym){
    for(int i=0;i<a->nadd;i++) if(a->add[i].ci == ci && a->add[i].slot == slot && tok_eq(&a->add[i].sym, sym)) return 1;
    return 0;
}
static int action_add(Action *a, int ci, int slot, const Tok *sym){
    if(action_has_add(a,ci,slot,sym)) return 1;
    if(a->nadd >= MAX_ADDS) return 0;
    a->add[a->nadd].ci = ci; a->add[a->nadd].slot = slot; a->add[a->nadd].sym = *sym; a->nadd++;
    return 1;
}
static void add_action(Action *acts, int *n, const Action *a){ if(a->nadd > 0 && *n < MAX_ACTIONS) acts[(*n)++] = *a; }

static int tile_matches(const Model *m, const Cell *c, int t, int r){
    for(int k=0;k<6;k++) if(!tok_empty(&c->slot[k])){
        Tok x = tile_sym(m,t,r,k);
        if(!tok_eq(&c->slot[k], &x)) return 0;
    }
    return 1;
}

static void edge_slots(int side, int *a, int *b){ *a = side; *b = (side + 1) % 6; }

static int cyclic_known_run_before_adds(const Cell *c, const Action *a, char *run, size_t runsz);
static int cyclic_known_run_anywhere(const Cell *c, char *run, size_t runsz);
static void describe_remember_action(const Cell *c, const Action *a,
                                     char *out, size_t outsz);

static void enum_tile_complete(const Model *m, const State *s, Action *acts, int *na){
    for(int ci=0;ci<s->ncell;ci++){
        const Cell *c = &s->cells[ci];
        int known = 0;
        for(int k=0;k<6;k++) if(!tok_empty(&c->slot[k])) known++;
        if(!known) continue;
        int count = 0, mt = -1, mr = -1;
        for(int t=0;t<m->ntile;t++) for(int r=0;r<6;r++) if(tile_matches(m,c,t,r)){ count++; mt=t; mr=r; }
        if(count == 1){
            Action a; memset(&a, 0, sizeof(a));
            for(int k=0;k<6;k++) if(tok_empty(&c->slot[k])){ Tok x = tile_sym(m,mt,mr,k); action_add(&a,ci,k,&x); }
            if(a.nadd > 0){
                char anchor[128];
                if(!cyclic_known_run_before_adds(c, &a, anchor, sizeof(anchor)) &&
                   !cyclic_known_run_anywhere(c, anchor, sizeof(anchor))) anchor[0]='\0';
                (void)anchor;
                describe_remember_action(c, &a, a.title, sizeof(a.title));
                snprintf(a.status, sizeof(a.status), "wrote %d letter%s",
                         a.nadd, a.nadd == 1 ? "" : "s");
                add_action(acts, na, &a);
            }
        }
    }
}

static void enum_across_edge(const Model *m, const State *s, Action *acts, int *na){
    for(int ci=0;ci<s->ncell;ci++){
        const Cell *c = &s->cells[ci];
        for(int side=0;side<6;side++){
            int as, bs; edge_slots(side,&as,&bs);
            if(tok_empty(&c->slot[as]) || tok_empty(&c->slot[bs])) continue;
            int nq, nr; side_neighbor_coord(c->q, c->r, side, &nq, &nr);
            int ni = find_cell(s, nq, nr);
            if(ni < 0) continue;
            int cs, ds; edge_slots((side+3)%6, &cs, &ds);
            const Cell *ncell = &s->cells[ni];
            int count = 0; Tok C, D; tok_set(&C,""); tok_set(&D,"");
            for(int ri=0;ri<m->nrule;ri++){
                const EdgeRule *er = &m->rules[ri];
                if(!tok_eq(&er->a,&c->slot[as]) || !tok_eq(&er->b,&c->slot[bs])) continue;
                if(!tok_empty(&ncell->slot[cs]) && !tok_eq(&ncell->slot[cs],&er->c)) continue;
                if(!tok_empty(&ncell->slot[ds]) && !tok_eq(&ncell->slot[ds],&er->d)) continue;
                count++; C=er->c; D=er->d;
            }
            if(count == 1){
                Action a; memset(&a,0,sizeof(a));
                snprintf(a.title,sizeof(a.title),"Reflect edge %s,%s across (%d,%d)->(%d,%d); add %s,%s.",
                         c->slot[as].s,c->slot[bs].s,c->q,c->r,ncell->q,ncell->r,C.s,D.s);
                snprintf(a.status,sizeof(a.status),"wrote %d letters", a.nadd);
                if(tok_empty(&ncell->slot[cs])) action_add(&a,ni,cs,&C);
                if(tok_empty(&ncell->slot[ds])) action_add(&a,ni,ds,&D);
                add_action(acts,na,&a);
            }
        }
    }
}

static void format_down_adds(char *out, size_t outsz,
                             const Cell *c, const Tok *csym,
                             const Cell *nc, const Tok *nsym,
                             int add_c, int add_n){
    char syms[64] = "";
    char coords[96] = "";
    if(add_c){
        snprintf(syms + strlen(syms), sizeof(syms) - strlen(syms),
                 "%s", csym->s);
        snprintf(coords + strlen(coords), sizeof(coords) - strlen(coords),
                 "(%d,%d)", c->q, c->r);
    }
    if(add_n){
        snprintf(syms + strlen(syms), sizeof(syms) - strlen(syms),
                 "%s%s", syms[0] ? "," : "", nsym->s);
        snprintf(coords + strlen(coords), sizeof(coords) - strlen(coords),
                 "%s(%d,%d)", coords[0] ? "," : "", nc->q, nc->r);
    }
    if(!syms[0]) snprintf(out, outsz, "nothing");
    else snprintf(out, outsz, "%s at %s", syms, coords);
}

static void enum_down_edge(const Model *m, const State *s, Action *acts, int *na){
    for(int ci=0;ci<s->ncell;ci++){
        const Cell *c = &s->cells[ci];
        for(int side=0;side<6;side++){
            int nq, nr; side_neighbor_coord(c->q, c->r, side, &nq, &nr);
            int ni = find_cell(s, nq, nr);
            if(ni < 0) continue;
            const Cell *nc = &s->cells[ni];
            int as,bs,cs,ds; edge_slots(side,&as,&bs); edge_slots((side+3)%6,&cs,&ds);
            const Tok *A=&c->slot[as], *B=&c->slot[bs], *C=&nc->slot[cs], *D=&nc->slot[ds];
            if(!tok_empty(B) && !tok_empty(C) && (tok_empty(A) || tok_empty(D))){
                int count=0; Tok A0,D0; tok_set(&A0,""); tok_set(&D0,"");
                for(int ri=0;ri<m->nrule;ri++){
                    const EdgeRule *er=&m->rules[ri];
                    if(!tok_eq(&er->b,B) || !tok_eq(&er->c,C)) continue;
                    if(!tok_empty(A) && !tok_eq(A,&er->a)) continue;
                    if(!tok_empty(D) && !tok_eq(D,&er->d)) continue;
                    count++; A0=er->a; D0=er->d;
                }
                if(count == 1){
                    Action a; memset(&a,0,sizeof(a));
                    char adds[128];
                    int add_a = tok_empty(A);
                    int add_d = tok_empty(D);
                    format_down_adds(adds, sizeof(adds), c, &A0, nc, &D0, add_a, add_d);
                    snprintf(a.title,sizeof(a.title),
                             "Down edge _%s=%s_; add %s.",
                             B->s,C->s,adds);
                    snprintf(a.status,sizeof(a.status),"wrote %d letters", a.nadd);
                    if(add_a) action_add(&a,ci,as,&A0);
                    if(add_d) action_add(&a,ni,ds,&D0);
                    add_action(acts,na,&a);
                }
            }
            if(!tok_empty(A) && !tok_empty(D) && (tok_empty(B) || tok_empty(C))){
                int count=0; Tok Bsym,Csym; tok_set(&Bsym,""); tok_set(&Csym,"");
                for(int ri=0;ri<m->nrule;ri++){
                    const EdgeRule *er=&m->rules[ri];
                    if(!tok_eq(&er->a,A) || !tok_eq(&er->d,D)) continue;
                    if(!tok_empty(B) && !tok_eq(B,&er->b)) continue;
                    if(!tok_empty(C) && !tok_eq(C,&er->c)) continue;
                    count++; Bsym=er->b; Csym=er->c;
                }
                if(count == 1){
                    Action a; memset(&a,0,sizeof(a));
                    char adds[128];
                    int add_b = tok_empty(B);
                    int add_c = tok_empty(C);
                    format_down_adds(adds, sizeof(adds), c, &Bsym, nc, &Csym, add_b, add_c);
                    snprintf(a.title,sizeof(a.title),
                             "Down edge %s_ = _%s; add %s.",
                             A->s,D->s,adds);
                    snprintf(a.status,sizeof(a.status),"wrote %d letters", a.nadd);
                    if(add_b) action_add(&a,ci,bs,&Bsym);
                    if(add_c) action_add(&a,ni,cs,&Csym);
                    add_action(acts,na,&a);
                }
            }
        }
    }
}

typedef struct { int ci[3], sl[3], n; } VGroup;

static void maybe_add_vgroup(VGroup *g, int maxg, int *ng,
                             int ci0, int sl0, int ci1, int sl1,
                             int ci2, int sl2){
    if(*ng >= maxg) return;
    VGroup v;
    memset(&v, 0, sizeof(v));
    if(ci0 >= 0){ v.ci[v.n]=ci0; v.sl[v.n]=sl0; v.n++; }
    if(ci1 >= 0){ v.ci[v.n]=ci1; v.sl[v.n]=sl1; v.n++; }
    if(ci2 >= 0){ v.ci[v.n]=ci2; v.sl[v.n]=sl2; v.n++; }
    if(v.n >= 2) g[(*ng)++] = v;
}

static int collect_vgroups(const State *s, VGroup *g, int maxg){
    /*
     * Explicit vertex grouping for the 321/450 postcard layout.  Avoid
     * pointy-hex coordinates here; they sent the AFJ completion to (1,-1).
     *
     *   upper: (q,r).1, (q+1,r).3, (q+parity,r+1).5
     *   lower: (q,r).0, (q+1,r).4, (q+parity,r-1).2
     * where parity = 1 on odd rows and 0 on even rows.
     *
     * Seed edge:
     *   K,H -> F at (0,1).5
     *   A,J -> F at (0,-1).2
     */
    int ng=0;
    for(int ci=0; ci<s->ncell; ci++){
        const Cell *c=&s->cells[ci];
        int right = find_cell(s, c->q + 1, c->r);
        int qshift = (c->r & 1) ? 1 : 0;
        int up    = find_cell(s, c->q + qshift, c->r + 1);
        int down  = find_cell(s, c->q + qshift, c->r - 1);
        maybe_add_vgroup(g,maxg,&ng, ci,1, right,3, up,5);
        maybe_add_vgroup(g,maxg,&ng, ci,0, right,4, down,2);
    }
    return ng;
}
static int vword_matches(const VWord *w, const Tok *known, int nk){
    int used[3]={0,0,0};
    for(int i=0;i<nk;i++){
        int ok=0;
        for(int j=0;j<3;j++) if(!used[j] && tok_eq(&w->v[j],&known[i])){ used[j]=1; ok=1; break; }
        if(!ok) return 0;
    }
    return 1;
}
static int vword_is_aaa(const VWord *w){ return strcmp(w->v[0].s,"A")==0 && strcmp(w->v[1].s,"A")==0 && strcmp(w->v[2].s,"A")==0; }

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

static int validate_state_local(const Model *m, const State *s,
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

static int action_is_locally_valid(const Model *m, const State *s,
                                   const Action *a, char *err, size_t errsz){
    State tmp=*s;
    int changed=0;
    if(a->nadd <= 0){ snprintf(err, errsz, "action has no additions"); return 0; }
    for(int i=0;i<a->nadd;i++){
        if(a->add[i].ci < 0 || a->add[i].ci >= tmp.ncell){
            snprintf(err, errsz, "action has invalid cell index");
            return 0;
        }
        if(tok_empty(&a->add[i].sym)){
            snprintf(err, errsz, "action tries to add an empty symbol");
            return 0;
        }
        if(tok_empty(&tmp.cells[a->add[i].ci].slot[a->add[i].slot])) changed=1;
        if(!set_cell_slot(&tmp.cells[a->add[i].ci], a->add[i].slot,
                          &a->add[i].sym, err, errsz)) return 0;
    }
    if(!changed){ snprintf(err, errsz, "action adds only already-known symbols"); return 0; }
    return validate_state_local(m, &tmp, err, errsz);
}

static int actions_same_adds(const Action *a, const Action *b){
    if(a->nadd != b->nadd) return 0;
    for(int i=0;i<a->nadd;i++){
        int hit=0;
        for(int j=0;j<b->nadd;j++){
            if(a->add[i].ci == b->add[j].ci &&
               a->add[i].slot == b->add[j].slot &&
               tok_eq(&a->add[i].sym, &b->add[j].sym)){
                hit=1;
                break;
            }
        }
        if(!hit) return 0;
    }
    return 1;
}

static int action_priority(const Action *a){
    /* Canonical action-1 path: remembers first, then edge propagation,
     * then vertex completions/prunes.  Within a class, larger additions
     * sort first, then the stable title/cell tie-breakers below apply. */
    int cls = 50;
    if(strncmp(a->title, "Remember", 8) == 0) cls = 0;
    else if(strncmp(a->title, "Complete tile", 13) == 0) cls = 0;
    else if(strncmp(a->title, "Reflect edge", 12) == 0) cls = 1;
    else if(strncmp(a->title, "Down edge", 9) == 0) cls = 1;
    else if(strncmp(a->title, "Complete vertex", 15) == 0) cls = 2;
    else if(strncmp(a->title, "Outlaw", 6) == 0) cls = 2;
    return cls * 100 - a->nadd;
}

static int action_first_ci(const Action *a){
    int best = 1000000;
    for(int i=0;i<a->nadd;i++) if(a->add[i].ci < best) best = a->add[i].ci;
    return best;
}

static void sort_actions(Action *acts, int n){
    for(int i=1;i<n;i++){
        Action x = acts[i];
        int px = action_priority(&x);
        int cx = action_first_ci(&x);
        int j=i-1;
        while(j>=0){
            int pj = action_priority(&acts[j]);
            int cj = action_first_ci(&acts[j]);
            if(pj < px) break;
            if(pj == px && cj < cx) break;
            if(pj == px && cj == cx && strcmp(acts[j].title, x.title) <= 0) break;
            acts[j+1] = acts[j];
            j--;
        }
        acts[j+1] = x;
    }
}

static int dedupe_actions(Action *acts, int n){
    int out=0;
    for(int i=0;i<n;i++){
        int dup=0;
        for(int j=0;j<out;j++){
            if(actions_same_adds(&acts[i], &acts[j])){
                dup=1;
                break;
            }
        }
        if(!dup) acts[out++] = acts[i];
    }
    return out;
}

static int filter_verified_actions(const Model *m, const State *s,
                                   Action *acts, int n){
    int out=0;
    for(int i=0;i<n;i++){
        char err[240];
        if(action_is_locally_valid(m, s, &acts[i], err, sizeof(err))){
            acts[out++] = acts[i];
        }
    }
    out = dedupe_actions(acts, out);
    sort_actions(acts, out);
    return out;
}
static void enum_vertex(const Model *m, const State *s, Action *acts, int *na){
    VGroup groups[256]; int ng=collect_vgroups(s,groups,256);
    for(int gi=0;gi<ng;gi++){
        VGroup *g=&groups[gi]; Tok known[3]; int nk=0;
        for(int i=0;i<g->n;i++){ const Tok *x=&s->cells[g->ci[i]].slot[g->sl[i]]; if(!tok_empty(x)) known[nk++]=*x; }
        if(nk==0 || nk>=3) continue;
        int matches[256], nm=0;
        for(int wi=0;wi<m->nvword;wi++) if(vword_matches(&m->vwords[wi],known,nk)) matches[nm++]=wi;
        int chosen=-1; char title[240], status[240];
        if(nm==1){ chosen=matches[0]; snprintf(title,sizeof(title),"Complete vertex word %s,%s,%s",m->vwords[chosen].v[0].s,m->vwords[chosen].v[1].s,m->vwords[chosen].v[2].s); snprintf(status,sizeof(status),"wrote 1 letter"); }
        else if(nm==2){ int aaa=-1, other=-1; for(int i=0;i<nm;i++){ if(vword_is_aaa(&m->vwords[matches[i]])) aaa=matches[i]; else other=matches[i]; } if(aaa>=0 && other>=0){ chosen=other; snprintf(title,sizeof(title),"Outlaw AAA; force vertex %s,%s,%s",m->vwords[chosen].v[0].s,m->vwords[chosen].v[1].s,m->vwords[chosen].v[2].s); snprintf(status,sizeof(status),"wrote 1 letter"); } }
        if(chosen<0) continue;
        int used[3]={0,0,0};
        for(int i=0;i<nk;i++) for(int j=0;j<3;j++) if(!used[j] && tok_eq(&m->vwords[chosen].v[j],&known[i])){ used[j]=1; break; }
        Tok miss[3]; int nmiss=0; for(int j=0;j<3;j++) if(!used[j]) miss[nmiss++]=m->vwords[chosen].v[j];
        Action a; memset(&a,0,sizeof(a)); snprintf(a.title,sizeof(a.title),"%s",title); snprintf(a.status,sizeof(a.status),"%s",status);
        int mi=0; for(int i=0;i<g->n && mi<nmiss;i++){ int ci=g->ci[i], sl=g->sl[i]; if(tok_empty(&s->cells[ci].slot[sl])) action_add(&a,ci,sl,&miss[mi++]); }
        if(a.nadd > 0){
            const Cell *tc = &s->cells[a.add[0].ci];
            if(strncmp(title,"Complete vertex",15)==0){
                snprintf(a.title,sizeof(a.title),"%.200s at (%d,%d).",title,tc->q,tc->r);
            } else if(strncmp(title,"Outlaw",6)==0){
                snprintf(a.title,sizeof(a.title),"%.200s at (%d,%d).",title,tc->q,tc->r);
            }
        }
        add_action(acts,na,&a);
    }
}

static void append_tok(char *buf, size_t bufsz, const Tok *x){
    if(tok_empty(x)) return;
    if(buf[0]) snprintf(buf + strlen(buf), bufsz - strlen(buf), ",%s", x->s);
    else snprintf(buf, bufsz, "%s", x->s);
}

static int cyclic_known_run_before_adds(const Cell *c, const Action *a,
                                        char *run, size_t runsz){
    /* Find a known CCW run that ends immediately before one of the added
     * slots.  This makes messages such as "Remember H after A,F,G" instead
     * of anchoring to the shorter edge pair A,F. */
    int best_start = -1, best_len = 0;
    for(int addi=0; addi<a->nadd; addi++){
        int add_slot = a->add[addi].slot;
        int len = 0;
        for(int step=1; step<=6; step++){
            int s = (add_slot - step + 6) % 6;
            if(tok_empty(&c->slot[s])) break;
            len++;
        }
        if(len > best_len){
            best_len = len;
            best_start = (add_slot - len + 6) % 6;
        }
    }
    if(best_len <= 0) return 0;
    run[0] = '\0';
    for(int i=0;i<best_len;i++){
        int s = (best_start + i) % 6;
        append_tok(run, runsz, &c->slot[s]);
    }
    return run[0] != '\0';
}

static int cyclic_known_run_anywhere(const Cell *c, char *run, size_t runsz){
    int best_start=-1, best_len=0;
    for(int start=0; start<6; start++){
        if(tok_empty(&c->slot[start])) continue;
        int len=0;
        for(int step=0; step<6; step++){
            int s=(start+step)%6;
            if(tok_empty(&c->slot[s])) break;
            len++;
        }
        if(len > best_len){ best_len=len; best_start=start; }
    }
    if(best_len <= 0) return 0;
    run[0]='\0';
    for(int i=0;i<best_len;i++) append_tok(run, runsz, &c->slot[(best_start+i)%6]);
    return run[0] != '\0';
}

static void describe_remember_action(const Cell *c, const Action *a,
                                     char *out, size_t outsz){
    char remembered[128];
    char anchor[128];
    remembered[0]='\0';
    anchor[0]='\0';

    for(int i=0;i<a->nadd;i++) append_tok(remembered, sizeof(remembered), &a->add[i].sym);

    if(cyclic_known_run_before_adds(c, a, anchor, sizeof(anchor)) ||
       cyclic_known_run_anywhere(c, anchor, sizeof(anchor))){
        snprintf(out, outsz, "Remember %s after %s at (%d,%d).",
                 remembered[0] ? remembered : "letters", anchor, c->q, c->r);
    } else {
        snprintf(out, outsz, "Remember %s at (%d,%d).",
                 remembered[0] ? remembered : "letters", c->q, c->r);
    }
}

static void enum_hex_forget(const Model *m, const State *s, Action *acts, int *na){
    /* Conservative forgetful tile/hex rule: all compatible tile rotations for
     * a partially labeled tile agree on a blank slot.  This is data-driven from
     * RL6 tiles and is useful early; full six-around-hex can be added later. */
    for(int ci=0;ci<s->ncell;ci++){
        const Cell *c=&s->cells[ci]; int known=0; for(int k=0;k<6;k++) if(!tok_empty(&c->slot[k])) known++;
        if(!known) continue;
        Tok agree[6]; int ok[6]; for(int k=0;k<6;k++){ tok_set(&agree[k],""); ok[k]=1; }
        int ncand=0;
        for(int t=0;t<m->ntile;t++) for(int r=0;r<6;r++) if(tile_matches(m,c,t,r)){
            ncand++;
            for(int k=0;k<6;k++){ Tok x=tile_sym(m,t,r,k); if(ncand==1) agree[k]=x; else if(!tok_eq(&agree[k],&x)) ok[k]=0; }
        }
        if(ncand <= 1) continue;
        Action a; memset(&a,0,sizeof(a));
        int adds=0; for(int k=0;k<6;k++) if(tok_empty(&c->slot[k]) && ok[k] && !tok_empty(&agree[k])){ action_add(&a,ci,k,&agree[k]); if(++adds>=2) break; }
        if(a.nadd){
            describe_remember_action(c, &a, a.title, sizeof(a.title));
            snprintf(a.status,sizeof(a.status),"wrote %d letter%s",
                     a.nadd, a.nadd == 1 ? "" : "s");
            add_action(acts,na,&a);
        }
    }
}

static int enumerate_actions(const Model *m, const State *s, Action *acts){
    int n=0;
    enum_tile_complete(m,s,acts,&n);
    enum_across_edge(m,s,acts,&n);
    enum_down_edge(m,s,acts,&n);
    enum_vertex(m,s,acts,&n);
    enum_hex_forget(m,s,acts,&n);
    g_last_raw_actions = n;
    return filter_verified_actions(m, s, acts, n);
}

static int apply_action(State *s, const Action *a, char *status, size_t sz){
    State tmp=*s; char err[240];
    for(int i=0;i<a->nadd;i++){
        if(a->add[i].ci < 0 || a->add[i].ci >= tmp.ncell){ snprintf(status,sz,"ERR: invalid action cell index"); return 0; }
        if(!set_cell_slot(&tmp.cells[a->add[i].ci],a->add[i].slot,&a->add[i].sym,err,sizeof(err))){ snprintf(status,sz,"ERR: %s",err); return 0; }
    }
    *s=tmp;
    snprintf(status, sz, "wrote %d letter%s",
             a->nadd, a->nadd == 1 ? "" : "s");
    return 1;
}
static void push_hist(History *h,const State *s){ if(h->n>=MAX_HIST){ memmove(&h->s[0],&h->s[1],sizeof(State)*(MAX_HIST-1)); h->n=MAX_HIST-1; } h->s[h->n++]=*s; }
static int pop_hist(History *h,State *s){ if(h->n<=0) return 0; *s=h->s[--h->n]; return 1; }

static void centered(char out[16], const char *in){
    size_t w = 11;
    size_t n = strlen(in);
    if(n > w) n = w;
    size_t left = (w - n) / 2;
    size_t pos = 0;
    for(size_t i=0;i<left;i++) out[pos++] = ' ';
    for(size_t i=0;i<n;i++) out[pos++] = in[i];
    while(pos < w) out[pos++] = ' ';
    out[pos] = '\0';
}

static void cell_lines(const Cell *c, char lines[5][128]){
    const char *t0 = tok_empty(&c->slot[display_top[0]]) ? " " : c->slot[display_top[0]].s;
    const char *t1 = tok_empty(&c->slot[display_top[1]]) ? " " : c->slot[display_top[1]].s;
    const char *t2 = tok_empty(&c->slot[display_top[2]]) ? " " : c->slot[display_top[2]].s;
    const char *b0 = tok_empty(&c->slot[display_bottom[0]]) ? " " : c->slot[display_bottom[0]].s;
    const char *b1 = tok_empty(&c->slot[display_bottom[1]]) ? " " : c->slot[display_bottom[1]].s;
    const char *b2 = tok_empty(&c->slot[display_bottom[2]]) ? " " : c->slot[display_bottom[2]].s;
    char coord[32], mid[16];
    snprintf(coord, sizeof(coord), "(%d,%d)", c->q, c->r);
    centered(mid, coord);
    snprintf(lines[0],128,"┌───┬───┬───┐");
    snprintf(lines[1],128,"│ %-1.1s   %-1.1s   %-1.1s │",t0,t1,t2);
    snprintf(lines[2],128,"│%s│", mid);
    snprintf(lines[3],128,"│ %-1.1s   %-1.1s   %-1.1s │",b0,b1,b2);
    snprintf(lines[4],128,"└───┴───┴───┘");
}

static void ellipsize(char *buf, size_t sz){
    size_t n = strlen(buf);
    if(sz >= 4 && n >= sz){
        buf[sz-4] = '.';
        buf[sz-3] = '.';
        buf[sz-2] = '.';
        buf[sz-1] = '\0';
    }
}

static void print_short_option(int idx, const char *s){
    char buf[77];
    const char *msg = s ? s : "";
    int n = snprintf(buf, sizeof(buf), "%d. %.72s", idx, msg);
    if(n < 0){
        snprintf(buf, sizeof(buf), "%d.", idx);
    }
    ellipsize(buf, sizeof(buf));
    printf("%-76s\n", buf);
}

static void print_status_line(const char *status){
    char buf[77];
    const char *msg = (status && status[0]) ? status : "ready";
    int n = snprintf(buf, sizeof(buf), "STATUS: %.68s", msg);
    if(n < 0){
        snprintf(buf, sizeof(buf), "STATUS: error formatting status");
    }
    ellipsize(buf, sizeof(buf));
    printf("%-76s\n", buf);
}

static void print_blank_line(void){ printf("%-76s\n", ""); }

static void render_help_body(void){
    static const char *help[] = {
        "Commands:",
        "  1..N   apply action",
        "  a      apply action 1 repeatedly until stuck/conflict",
        "  ↑ / ↓  scroll completion list",
        "  u      undo",
        "  r      reset to central edge",
        "  q      quit",
        "  ?      help",
        "",
        "Press return to return to the diagram."
    };
    enum { BODY_ROWS = 21 };
    int nhelp = (int)(sizeof(help) / sizeof(help[0]));
    int top = (BODY_ROWS - nhelp) / 2;
    if(top < 0) top = 0;
    for(int i=0;i<BODY_ROWS;i++){
        if(i >= top && i < top + nhelp) printf("%-76s\n", help[i - top]);
        else print_blank_line();
    }
}

static void render(const Model *m, const State *s, Action *acts, int na,
                   int action_scroll, const char *status, int help_mode){
    (void)m;
    printf("RL7 HCE | q: quit, ?: help, u: undo, a: force-all, r: reset | 80x24\n");
    if(help_mode){
        render_help_body();
        print_blank_line();
        printf("$: ");
        fflush(stdout);
        return;
    }

    int rr[3]={1,0,-1};
    for(int ri=0;ri<3;ri++){
        int r=rr[ri], idx[16], cnt=0;
        for(int i=0;i<s->ncell;i++) if(s->cells[i].r==r) idx[cnt++]=i;
        for(int i=0;i<cnt;i++) for(int j=i+1;j<cnt;j++) if(s->cells[idx[j]].q < s->cells[idx[i]].q){ int t=idx[i]; idx[i]=idx[j]; idx[j]=t; }
        char blocks[16][5][128]; for(int i=0;i<cnt;i++) cell_lines(&s->cells[idx[i]],blocks[i]);
        const char *indent=(r&1)?"       ":"";
        for(int line=0;line<5;line++){ printf("%s",indent); for(int i=0;i<cnt;i++){ printf("%s",blocks[i][line]); if(i+1<cnt) putchar(' '); } putchar('\n'); }
    }

    int visible = na < 4 ? na : 4;
    int max_scroll = na > visible ? na - visible : 0;
    if(action_scroll < 0) action_scroll = 0;
    if(action_scroll > max_scroll) action_scroll = max_scroll;
    if(na > visible){
        printf("Complete: actions %d-%d of %d  (↑/↓ scroll)\n",
               action_scroll + 1, action_scroll + visible, na);
    } else {
        printf("Complete: showing %d of %d actions\n", visible, na);
    }
    for(int i=0;i<4;i++){
        int ai = action_scroll + i;
        if(i<visible && ai<na) print_short_option(ai+1, acts[ai].title);
        else print_blank_line();
    }
    print_status_line(status);
    print_blank_line();
    printf("$: "); fflush(stdout);
}

static int parse_int_strict(const char *s,int *out){
    while(isspace((unsigned char)*s)) s++;
    if(!*s) return 0;
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if(errno || end == s) return 0;
    while(isspace((unsigned char)*end)) end++;
    if(*end || v < 1 || v > 1000000) return 0;
    *out = (int)v;
    return 1;
}

typedef enum { INPUT_SUBMIT, INPUT_SCROLL_UP, INPUT_SCROLL_DOWN, INPUT_EOF } InputKind;

typedef struct {
    int tty;
    struct termios oldt;
    int active;
} TermMode;

static void term_restore(TermMode *tm){
    if(tm->active){
        tcsetattr(STDIN_FILENO, TCSANOW, &tm->oldt);
        tm->active = 0;
    }
}

static int term_setup(TermMode *tm){
    memset(tm, 0, sizeof(*tm));
    tm->tty = isatty(STDIN_FILENO);
    if(!tm->tty) return 1;
    if(tcgetattr(STDIN_FILENO, &tm->oldt) != 0) return 0;
    struct termios raw = tm->oldt;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if(tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return 0;
    tm->active = 1;
    return 1;
}

static InputKind read_input(TermMode *tm, char *line, size_t linesz){
    line[0] = '\0';
    if(!tm->tty){
        if(!fgets(line, linesz, stdin)) return INPUT_EOF;
        line[strcspn(line, "\r\n")] = '\0';
        if(strcmp(line, "\033[A") == 0) return INPUT_SCROLL_UP;
        if(strcmp(line, "\033[B") == 0) return INPUT_SCROLL_DOWN;
        return INPUT_SUBMIT;
    }

    size_t n = 0;
    for(;;){
        unsigned char ch;
        ssize_t got = read(STDIN_FILENO, &ch, 1);
        if(got == 0) return INPUT_EOF;
        if(got < 0){
            if(errno == EINTR) continue;
            return INPUT_EOF;
        }
        if(ch == '\r' || ch == '\n'){
            line[n] = '\0';
            putchar('\n');
            fflush(stdout);
            return INPUT_SUBMIT;
        }
        if(ch == 0x7f || ch == '\b'){
            if(n > 0){
                n--;
                line[n] = '\0';
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }
        if(ch == 0x04){
            if(n == 0) return INPUT_EOF;
            continue;
        }
        if(ch == 0x1b){
            unsigned char seq[2];
            ssize_t g1 = read(STDIN_FILENO, &seq[0], 1);
            if(g1 == 1 && seq[0] == '['){
                ssize_t g2 = read(STDIN_FILENO, &seq[1], 1);
                if(g2 == 1){
                    if(seq[1] == 'A') return INPUT_SCROLL_UP;
                    if(seq[1] == 'B') return INPUT_SCROLL_DOWN;
                }
            }
            continue;
        }
        if(isprint(ch)){
            if(n + 1 < linesz){
                line[n++] = (char)ch;
                line[n] = '\0';
                putchar((int)ch);
                fflush(stdout);
            }
        }
    }
}

static void erase_frame(int first, int after_enter){
    if(first) return;
    printf("\r\033[K");
    int rows = after_enter ? 24 : 23;
    for(int i=0;i<rows;i++) printf("\033[1A\r\033[K");
}

static int max_action_scroll(int na){ return na > 4 ? na - 4 : 0; }

int main(int argc, char **argv){
    const char *model_path = DEFAULT_MODEL;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--model")==0 && i+1<argc) model_path=argv[++i];
        else if(strcmp(argv[i],"--help")==0){ printf("usage: %s [--model data/rl6/refined_model.dat]\n", argv[0]); return 0; }
        else { fprintf(stderr,"ERR: unknown argument '%s'\n",argv[i]); return 2; }
    }
    Model m; char status[256], err[256];
    if(!load_model(model_path,&m,err,sizeof(err))){ fprintf(stderr,"ERR: %s\n",err); return 1; }

    TermMode tm;
    if(!term_setup(&tm)){
        fprintf(stderr, "ERR: could not configure terminal input\n");
        return 1;
    }

    State s; History h; Action acts[MAX_ACTIONS]; char line[256];
    int first_frame=1, action_scroll=0, help_mode=0, after_enter=0;
    h.n=0;
    init_state(&s);
    snprintf(status,sizeof(status),"loaded %s: %d tiles, %d edge rules, %d vertex words",model_path,m.ntile,m.nrule,m.nvword);

    for(;;){
        int na=enumerate_actions(&m,&s,acts);
        int max_scroll = max_action_scroll(na);
        if(action_scroll > max_scroll) action_scroll = max_scroll;
        if(action_scroll < 0) action_scroll = 0;
        erase_frame(first_frame, after_enter);
        first_frame=0;
        after_enter=0;
        render(&m,&s,acts,na,action_scroll,status,help_mode);

        InputKind input = read_input(&tm, line, sizeof(line));
        if(input == INPUT_EOF){ printf("\nEOF\n"); break; }
        if(input == INPUT_SCROLL_UP || input == INPUT_SCROLL_DOWN){
            if(!help_mode){
                if(input == INPUT_SCROLL_UP && action_scroll > 0) action_scroll--;
                if(input == INPUT_SCROLL_DOWN && action_scroll < max_action_scroll(na)) action_scroll++;
            }
            after_enter = 0;
            continue;
        }

        after_enter = 1;
        char *p=line;
        while(isspace((unsigned char)*p)) p++;

        if(help_mode){
            help_mode = 0;
            continue;
        }
        if(!*p){ snprintf(status,sizeof(status),"ready"); continue; }
        if(strcmp(p,"r")==0){ init_state(&s); h.n=0; action_scroll=0; snprintf(status,sizeof(status),"reset to central edge"); continue; }
        if(strcmp(p,"q")==0||strcmp(p,"quit")==0){ break; }
        if(strcmp(p,"?")==0||strcmp(p,"help")==0){ help_mode=1; continue; }
        if(strcmp(p,"u")==0){ if(pop_hist(&h,&s)) snprintf(status,sizeof(status),"undo"); else snprintf(status,sizeof(status),"ERR: nothing to undo"); action_scroll=0; continue; }
        if(strcmp(p,"a")==0){
            State before=s;
            int applied=0;
            int wrote=0;
            while(applied < 256){
                Action now[MAX_ACTIONS];
                int nc=enumerate_actions(&m,&s,now);
                if(nc <= 0) break;
                char verr[240];
                if(!action_is_locally_valid(&m, &s, &now[0], verr, sizeof(verr))){
                    snprintf(status,sizeof(status),"ERR: action 1 failed verification: %.180s",verr);
                    break;
                }
                int step_wrote = now[0].nadd;
                if(!apply_action(&s, &now[0], status, sizeof(status))) break;
                wrote += step_wrote;
                applied++;
            }
            if(applied>0) push_hist(&h,&before);
            snprintf(status,sizeof(status),"wrote %d letter%s in %d action%s%s",
                     wrote, wrote == 1 ? "" : "s",
                     applied, applied == 1 ? "" : "s",
                     applied>=256?"; stopped at cap":"");
            action_scroll=0;
            continue;
        }
        int choice=0;
        if(!parse_int_strict(p,&choice)){
            snprintf(status,sizeof(status),"ERR: parse failed; type number, q, r, u, a, or ?");
            continue;
        }
        if(choice<1 || choice>na){ snprintf(status,sizeof(status),"ERR: action %d out of range 1..%d",choice,na); continue; }
        push_hist(&h,&s);
        if(!apply_action(&s,&acts[choice-1],status,sizeof(status))) pop_hist(&h,&s);
        action_scroll=0;
    }
    term_restore(&tm);
    printf("bye\n");
    return 0;
}
