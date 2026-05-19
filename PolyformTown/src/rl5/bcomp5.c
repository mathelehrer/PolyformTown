#include "rl5/hex5.h"
#include <stdio.h>
#include <string.h>

static int find_oriented(const Hex5Model *m, const Hex5 *h){
    for(int i=0;i<m->noriented;i++) if(hex5_equal(&m->oriented[i], h)) return i;
    return -1;
}

static int rot_left_idx(const Hex5Model *m, int idx, int k){
    Hex5 r;
    hex5_rot_left(&m->oriented[idx], k, &r);
    return find_oriented(m, &r);
}

static int rot_right_idx(const Hex5Model *m, int idx, int k){
    Hex5 r;
    hex5_rot_right(&m->oriented[idx], k, &r);
    return find_oriented(m, &r);
}

static int ring_cmp(const Ring5 *a, const Ring5 *b){
    if(a->center != b->center) return a->center - b->center;
    for(int i=0;i<HEX5_SIDES;i++) if(a->r[i] != b->r[i]) return a->r[i] - b->r[i];
    return 0;
}

static void ring_canon_model(const Hex5Model *m, const Ring5 *in, Ring5 *out){
    Ring5 best, c;
    best = *in;
    for(int k=1;k<HEX5_SIDES;k++){
        c.center = rot_left_idx(m, in->center, k);
        if(c.center < 0) continue;
        for(int i=0;i<HEX5_SIDES;i++) c.r[i] = in->r[(i+k)%HEX5_SIDES];
        if(ring_cmp(&c, &best) < 0) best = c;
    }
    *out = best;
}

static int add_ring(const Hex5Model *m, BComp5Result *b, const Ring5 *r){
    Ring5 c;
    ring_canon_model(m, r, &c);
    for(int i=0;i<b->nrings;i++) if(ring_cmp(&b->rings[i], &c) == 0) return 1;
    if(b->nrings >= HEX5_MAX_BCOMP) return 0;
    b->rings[b->nrings++] = c;
    return 1;
}

static int dfs_ring(const Hex5Model *m, const VComp5Dict *v, BComp5Result *b,
                    int h0, int h1, Ring5 *r, int x){
    int outs[HEX5_MAX_ORIENTED];
    if(x == 5){
        int center = rot_left_idx(m, h0, 5);
        int no;
        if(center < 0) return 1;
        no = vcomp5_lookup_pair(v, center, r->r[5], outs, HEX5_MAX_ORIENTED);
        b->closure_tests += no;
        for(int i=0;i<no;i++){
            int closed = rot_right_idx(m, outs[i], 1);
            if(closed == h1){
                b->closure_hits++;
                if(!add_ring(m, b, r)) return 0;
            }
        }
        return 1;
    }

    int center = rot_left_idx(m, h0, x);
    int no;
    if(center < 0) return 1;
    no = vcomp5_lookup_pair(v, center, r->r[x], outs, HEX5_MAX_ORIENTED);
    for(int i=0;i<no;i++){
        int rr = rot_right_idx(m, outs[i], 1);
        if(rr < 0) continue;
        r->r[x+1] = rr;
        if(!dfs_ring(m, v, b, h0, h1, r, x + 1)) return 0;
    }
    return 1;
}

int bcomp5_build(const Hex5Model *m, const VComp5Dict *v, BComp5Result *b){
    int pairs[HEX5_MAX_ORIENTED][2];
    b->nrings = 0;
    b->starts = b->closure_tests = b->closure_hits = 0;
    for(int base=0;base<m->ntiles;base++){
        int h0 = find_oriented(m, &m->tiles[base]);
        if(h0 < 0) continue;
        int np = vcomp5_lookup_one(v, h0, pairs, HEX5_MAX_ORIENTED);
        for(int p=0;p<np;p++){
            Ring5 r;
            int h1 = pairs[p][0];
            int h2 = pairs[p][1];
            int rr = rot_right_idx(m, h2, 1);
            if(rr < 0) continue;
            b->starts++;
            r.center = h0;
            for(int i=0;i<HEX5_SIDES;i++) r.r[i] = -1;
            r.r[0] = h1;
            r.r[1] = rr;
            if(!dfs_ring(m, v, b, h0, h1, &r, 1)) return 0;
        }
    }
    return 1;
}

static int inner_cmp_raw(const Inner5 *x, const Inner5 *y){
    for(int i=0;i<HEX5_SIDES;i++){
        int c = strcmp(x->a[i], y->a[i]); if(c) return c;
        c = strcmp(x->b[i], y->b[i]); if(c) return c;
    }
    return 0;
}

static void inner_canon(const Inner5 *in, Inner5 *out){
    Inner5 best, c;
    best = *in;
    for(int k=1;k<HEX5_SIDES;k++){
        for(int i=0;i<HEX5_SIDES;i++){
            snprintf(c.a[i], HEX5_TOK, "%s", in->a[(i+k)%HEX5_SIDES]);
            snprintf(c.b[i], HEX5_TOK, "%s", in->b[(i+k)%HEX5_SIDES]);
        }
        if(inner_cmp_raw(&c, &best) < 0) best = c;
    }
    *out = best;
}

static int add_inner(Inner5Set *s, const Inner5 *in){
    Inner5 c;
    inner_canon(in, &c);
    for(int i=0;i<s->nrows;i++) if(inner_cmp_raw(&s->rows[i], &c) == 0) return 1;
    if(s->nrows >= HEX5_MAX_INNER) return 0;
    s->rows[s->nrows++] = c;
    return 1;
}

int bcomp5_inner_generated(const Hex5Model *m, const BComp5Result *b, Inner5Set *s){
    s->nrows = 0;
    for(int i=0;i<b->nrings;i++){
        Inner5 in;
        for(int k=0;k<HEX5_SIDES;k++){
            int center = rot_left_idx(m, b->rings[i].center, k);
            snprintf(in.a[k], HEX5_TOK, "%s", center >= 0 ? m->oriented[center].e[0] : "?");
            snprintf(in.b[k], HEX5_TOK, "%s", b->rings[i].r[k] >= 0 ? m->oriented[b->rings[i].r[k]].e[1] : "?");
        }
        if(!add_inner(s, &in)) return 0;
    }
    return 1;
}

int hex5_inner_extracted(const Hex5Model *m, Inner5Set *s){
    s->nrows = 0;
    for(int i=0;i<m->nrules;i+=2){
        Inner5 in;
        for(int k=0;k<HEX5_SIDES;k++){
            snprintf(in.a[k], HEX5_TOK, "%s", m->rule_a[i]);
            snprintf(in.b[k], HEX5_TOK, "%s", m->rule_b[i]);
        }
        if(!add_inner(s, &in)) return 0;
    }
    return 1;
}
