#include "boundary7.h"

#include <stdio.h>
#include <string.h>

void side_neighbor_coord(int q, int r, int side, int *nq, int *nr){
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


int tok_empty(const Tok *t){ return t->s[0] == '\0'; }

int tok_eq(const Tok *a, const Tok *b){ return strcmp(a->s, b->s) == 0; }

void tok_set(Tok *t, const char *s){
    if(!s) s = "";
    size_t i = 0;
    while(s[i] && i + 1 < sizeof(t->s)){
        t->s[i] = s[i];
        i++;
    }
    t->s[i] = '\0';
}


int find_cell(const State *s, int q, int r){
    for(int i=0;i<s->ncell;i++) if(s->cells[i].q == q && s->cells[i].r == r) return i;
    return -1;
}

int add_cell(State *s, int q, int r){
    int i = find_cell(s,q,r);
    if(i >= 0) return i;
    if(s->ncell >= MAX_CELLS) return -1;
    i = s->ncell++;
    s->cells[i].q = q; s->cells[i].r = r;
    for(int k=0;k<6;k++) tok_set(&s->cells[i].slot[k], "");
    s->cells[i].tile = -1; s->cells[i].rot = -1;
    return i;
}


void init_state(State *s){
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


int set_cell_slot(Cell *c, int slot, const Tok *sym, char *err, size_t errsz){
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


void edge_slots(int side, int *a, int *b){ *a = side; *b = (side + 1) % 6; }


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


int collect_vgroups(const State *s, VGroup *g, int maxg){
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

