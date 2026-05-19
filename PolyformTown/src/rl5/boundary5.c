#include "rl5/hex5.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mod6(int x){
    x %= HEX5_SIDES;
    if(x < 0) x += HEX5_SIDES;
    return x;
}

static int find_oriented5(const Hex5Model *m, const Hex5 *h){
    for(int i=0;i<m->noriented;i++) if(hex5_equal(&m->oriented[i], h)) return i;
    return -1;
}

int boundary5_rot_left_idx(const Hex5Model *m, int idx, int k){
    Hex5 r;
    if(idx < 0 || idx >= m->noriented) return -1;
    hex5_rot_left(&m->oriented[idx], k, &r);
    return find_oriented5(m, &r);
}

int boundary5_rot_edge_to_slot0(const Hex5Model *m, int idx, int edge_slot){
    return boundary5_rot_left_idx(m, idx, mod6(edge_slot));
}

int boundary5_edge_matches(const Hex5Model *m, const char *a, const char *b){
    if(strcmp(a, b) == 0){
        for(int i=0;i<m->nrules;i++){
            if(strcmp(m->rule_a[i], a)==0 && strcmp(m->rule_b[i], b)==0) return 1;
        }
    }
    for(int i=0;i<m->nrules;i++){
        if(strcmp(m->rule_a[i], a)==0 && strcmp(m->rule_b[i], b)==0) return 1;
    }
    return 0;
}

void boundary5_neighbor(Boundary5Cell c, int dir, Boundary5Cell *out){
    static const int dq[HEX5_SIDES] = { 1, 0,-1,-1, 0, 1};
    static const int dr[HEX5_SIDES] = { 0, 1, 1, 0,-1,-1};
    dir = mod6(dir);
    out->q = c.q + dq[dir];
    out->r = c.r + dr[dir];
}

int boundary5_cell_equal(Boundary5Cell a, Boundary5Cell b){
    return a.q == b.q && a.r == b.r;
}

int boundary5_dir_between(Boundary5Cell a, Boundary5Cell b){
    for(int d=0; d<HEX5_SIDES; d++){
        Boundary5Cell n;
        boundary5_neighbor(a, d, &n);
        if(boundary5_cell_equal(n, b)) return d;
    }
    return -1;
}

int boundary5_patch_find(const Boundary5Patch *p, Boundary5Cell c){
    for(int i=0;i<p->n;i++) if(boundary5_cell_equal(p->cell[i], c)) return i;
    return -1;
}

int boundary5_patch_add(Boundary5Patch *p, Boundary5Cell c, int hex, unsigned flags){
    int old = boundary5_patch_find(p, c);
    if(old >= 0) return old;
    if(p->n >= BOUNDARY5_MAX_CELLS) return -1;
    p->cell[p->n] = c;
    p->hex[p->n] = hex;
    p->flags[p->n] = flags;
    return p->n++;
}

int boundary5_patch_is_boundary_cell(const Boundary5Patch *p, int item){
    if(item < 0 || item >= p->n) return 0;
    for(int d=0; d<HEX5_SIDES; d++){
        Boundary5Cell n;
        boundary5_neighbor(p->cell[item], d, &n);
        if(boundary5_patch_find(p, n) < 0) return 1;
    }
    return 0;
}

int boundary5_patch_vanish_complete(const Boundary5Patch *p){
    for(int i=0;i<p->n;i++){
        if((p->flags[i] & BOUNDARY5_VANISH) && boundary5_patch_is_boundary_cell(p, i)) return 0;
    }
    return 1;
}

int boundary5_patch_check_edges(const Hex5Model *m, const Boundary5Patch *p,
                                char *err, int err_cap){
    for(int i=0;i<p->n;i++){
        int hi = p->hex[i];
        if(hi < 0 || hi >= m->noriented){
            if(err && err_cap > 0) snprintf(err, (size_t)err_cap, "bad hex index at cell %d", i);
            return 0;
        }
        for(int d=0; d<HEX5_SIDES; d++){
            Boundary5Cell nc;
            int j;
            boundary5_neighbor(p->cell[i], d, &nc);
            j = boundary5_patch_find(p, nc);
            if(j < 0 || j < i) continue;
            int hj = p->hex[j];
            int od = mod6(d + 3);
            if(!boundary5_edge_matches(m, m->oriented[hi].e[d], m->oriented[hj].e[od])){
                if(err && err_cap > 0){
                    snprintf(err, (size_t)err_cap,
                             "edge mismatch (%d,%d)[%d]=%s vs (%d,%d)[%d]=%s",
                             p->cell[i].q, p->cell[i].r, d, m->oriented[hi].e[d],
                             p->cell[j].q, p->cell[j].r, od, m->oriented[hj].e[od]);
                }
                return 0;
            }
        }
    }
    if(err && err_cap > 0) err[0] = '\0';
    return 1;
}

static int count_patch_neighbors(const Boundary5Patch *p, Boundary5Cell c,
                                 int idx_out[HEX5_SIDES]){
    int n = 0;
    for(int d=0; d<HEX5_SIDES; d++){
        Boundary5Cell nc;
        int j;
        boundary5_neighbor(c, d, &nc);
        j = boundary5_patch_find(p, nc);
        idx_out[d] = j;
        if(j >= 0) n++;
    }
    return n;
}

int boundary5_candidate_world_orients(const Hex5Model *m, const Boundary5Patch *p,
                                      Boundary5Cell pos, int cand_dict,
                                      int out[HEX5_SIDES], int cap){
    int n = 0;
    int neighbors[HEX5_SIDES];
    if(boundary5_patch_find(p, pos) >= 0) return 0;
    count_patch_neighbors(p, pos, neighbors);
    for(int r=0; r<HEX5_SIDES; r++){
        int cand = boundary5_rot_left_idx(m, cand_dict, r);
        int ok = 1;
        if(cand < 0) continue;
        for(int d=0; d<HEX5_SIDES; d++){
            int j = neighbors[d];
            if(j < 0) continue;
            int od = mod6(d + 3);
            if(!boundary5_edge_matches(m, m->oriented[cand].e[d], m->oriented[p->hex[j]].e[od])){
                ok = 0;
                break;
            }
        }
        if(ok){
            int seen = 0;
            for(int k=0;k<n && k<cap;k++) if(out[k] == cand) seen = 1;
            if(!seen){
                if(n < cap) out[n] = cand;
                n++;
            }
        }
    }
    return n;
}

int boundary5_lookup_edge_bruteforce(const Hex5Model *m, const VComp5Dict *v,
                                     const Boundary5Patch *p,
                                     int prev_item, int next_item,
                                     Boundary5Cell outside_pos,
                                     int out[BOUNDARY5_MAX_CANDIDATES],
                                     int cap){
    int n = 0;
    int seen[BOUNDARY5_MAX_CANDIDATES];
    int nseen = 0;
    int outs[HEX5_MAX_ORIENTED];
    if(prev_item < 0 || prev_item >= p->n || next_item < 0 || next_item >= p->n) return 0;
    if(boundary5_patch_find(p, outside_pos) >= 0) return 0;

    for(int rn=0; rn<HEX5_SIDES; rn++){
        int qnext = boundary5_rot_left_idx(m, p->hex[next_item], rn);
        if(qnext < 0) continue;
        for(int rp=0; rp<HEX5_SIDES; rp++){
            int qprev = boundary5_rot_left_idx(m, p->hex[prev_item], rp);
            if(qprev < 0) continue;
            int no = vcomp5_lookup_pair(v, qnext, qprev, outs, HEX5_MAX_ORIENTED);
            for(int i=0;i<no;i++){
                int world[HEX5_SIDES];
                int nw = boundary5_candidate_world_orients(m, p, outside_pos, outs[i], world, HEX5_SIDES);
                for(int w=0; w<nw; w++){
                    int cand = world[w];
                    if(0){
                        int udbg = boundary5_dir_between(p->cell[prev_item], p->cell[next_item]);
                        fprintf(stderr, " brute_frame u=%d rn=%d rp=%d outdict=%d world=%d\n", udbg, rn, rp, outs[i], cand);
                    }
                    int dup = 0;
                    for(int s=0;s<nseen;s++) if(seen[s] == cand) dup = 1;
                    if(dup) continue;
                    if(nseen < BOUNDARY5_MAX_CANDIDATES) seen[nseen++] = cand;
                    if(n < cap) out[n] = cand;
                    n++;
                }
            }
        }
    }
    return n;
}

void boundary5_frame_report(FILE *f){
    if(!f) return;
    fprintf(f, "  boundary5 frame: fixed-North axial cells, side d checks slot d against neighbor slot d+3\n");
    fprintf(f, "  boundary5 frame: pair lookup uses direct fixed-North shifts; debug candidates are validated by grid edge checks\n");
}

static int boundary5_find_oriented_model(const Hex5Model *m, const Hex5 *h){
    for(int i=0;i<m->noriented;i++) if(hex5_equal(&m->oriented[i], h)) return i;
    return -1;
}

static int boundary5_state_cycle_ok(const Boundary5State *s){
    if(s->ncycle < 3) return 0;
    for(int i=0;i<s->ncycle;i++){
        int a = s->cycle[i];
        int b = s->cycle[(i+1)%s->ncycle];
        if(a < 0 || a >= s->patch.n || b < 0 || b >= s->patch.n) return 0;
        if(boundary5_dir_between(s->patch.cell[a], s->patch.cell[b]) < 0) return 0;
    }
    return 1;
}

static int boundary5_state_same_patch(const Boundary5State *a, const Boundary5State *b){
    if(a->patch.n != b->patch.n) return 0;
    for(int i=0;i<a->patch.n;i++){
        int j = boundary5_patch_find(&b->patch, a->patch.cell[i]);
        if(j < 0) return 0;
        if(a->patch.hex[i] != b->patch.hex[j]) return 0;
        if((a->patch.flags[i] & BOUNDARY5_VANISH) != (b->patch.flags[j] & BOUNDARY5_VANISH)) return 0;
    }
    return 1;
}


static int boundary5_ring_cmp(const Ring5 *a, const Ring5 *b){
    if(a->center != b->center) return a->center - b->center;
    for(int i=0;i<HEX5_SIDES;i++) if(a->r[i] != b->r[i]) return a->r[i] - b->r[i];
    return 0;
}

static void boundary5_ring_canon(const Hex5Model *m, const Ring5 *in, Ring5 *out){
    Ring5 best, c;
    best = *in;
    for(int k=1;k<HEX5_SIDES;k++){
        c.center = boundary5_rot_left_idx(m, in->center, k);
        if(c.center < 0) continue;
        for(int i=0;i<HEX5_SIDES;i++) c.r[i] = in->r[(i+k)%HEX5_SIDES];
        if(boundary5_ring_cmp(&c, &best) < 0) best = c;
    }
    *out = best;
}

static int boundary5_extract_one_vanish_ring(const Hex5Model *m, const Boundary5State *s, Ring5 *out){
    int center_item = -1;
    Boundary5Cell c0;
    for(int i=0;i<s->patch.n;i++){
        if(s->patch.flags[i] & BOUNDARY5_VANISH){
            if(center_item >= 0) return 0;
            center_item = i;
        }
    }
    if(center_item < 0) return 0;
    c0 = s->patch.cell[center_item];
    out->center = s->patch.hex[center_item];
    for(int d=0; d<HEX5_SIDES; d++){
        Boundary5Cell nc;
        int j, r;
        boundary5_neighbor(c0, d, &nc);
        j = boundary5_patch_find(&s->patch, nc);
        if(j < 0) return 0;
        /*
           Convert fixed-North neighbor back to the legacy ring convention.
           In fixed-North, center slot d is checked against neighbor slot d+3.
           Legacy ring slot d is oriented for the next pair lookup, so its
           slot 1 is the edge matching center slot d.  Therefore fixed slot
           d+3 must move to legacy slot 1: rotate left by (d+2).
        */
        r = boundary5_rot_left_idx(m, s->patch.hex[j], d + 2);
        if(r < 0) return 0;
        out->r[d] = r;
    }
    return 1;
}

static int boundary5_unique_completed_rings(const Hex5Model *m, const Boundary5State *states, int nstates){
    Ring5 *rings = NULL;
    int nr = 0;
    rings = calloc((size_t)(nstates > 0 ? nstates : 1), sizeof(*rings));
    if(!rings) return 0;
    for(int i=0;i<nstates;i++){
        Ring5 r, c;
        int seen = 0;
        if(!boundary5_extract_one_vanish_ring(m, &states[i], &r)) continue;
        boundary5_ring_canon(m, &r, &c);
        for(int k=0;k<nr;k++) if(boundary5_ring_cmp(&rings[k], &c) == 0) seen = 1;
        if(!seen) rings[nr++] = c;
    }
    free(rings);
    return nr;
}

static int boundary5_state_seen(const Boundary5State *states, int n, const Boundary5State *s){
    for(int i=0;i<n;i++) if(boundary5_state_same_patch(&states[i], s)) return 1;
    return 0;
}


static void boundary5_state_begin_next_layer(Boundary5State *s){
    for(int i=0;i<s->patch.n;i++) s->patch.flags[i] &= ~BOUNDARY5_VANISH;
    for(int k=0;k<s->ncycle;k++){
        int item = s->cycle[k];
        if(item >= 0 && item < s->patch.n) s->patch.flags[item] |= BOUNDARY5_VANISH;
    }
}

static int boundary5_state_complete(const Boundary5State *s){
    for(int k=0;k<s->ncycle;k++){
        int item = s->cycle[k];
        if(item >= 0 && item < s->patch.n && (s->patch.flags[item] & BOUNDARY5_VANISH)) return 0;
    }
    return 1;
}


static int boundary5_state_vec_reserve(Boundary5State **states, int *cap, int need){
    Boundary5State *tmp;
    int ncap;
    if(need <= *cap) return 1;
    ncap = (*cap > 0) ? *cap : 64;
    while(ncap < need){
        if(ncap > 1<<20) return 0;
        ncap *= 2;
    }
    tmp = realloc(*states, (size_t)ncap * sizeof(**states));
    if(!tmp) return 0;
    *states = tmp;
    *cap = ncap;
    return 1;
}

static int boundary5_state_vec_add_unique(Boundary5State **states, int *n, int *cap,
                                          const Boundary5State *s){
    if(boundary5_state_seen(*states, *n, s)) return 1;
    if(!boundary5_state_vec_reserve(states, cap, *n + 1)) return 0;
    (*states)[(*n)++] = *s;
    return 1;
}

static int boundary5_common_outside(Boundary5Cell prev, Boundary5Cell next, Boundary5Cell *out){
    int u = boundary5_dir_between(prev, next);
    if(u < 0) return 0;
    boundary5_neighbor(prev, u - 1, out);
    return 1;
}

static void boundary5_stats_touch_state(Boundary5DepthCounts *st, const Boundary5State *s){
    if(!st || !s) return;
    if(s->patch.n > st->max_cells) st->max_cells = s->patch.n;
    if(s->ncycle > st->max_cycle) st->max_cycle = s->ncycle;
}

static void boundary5_stats_fanout(Boundary5DepthCounts *st, int n){
    if(!st || n <= 0) return;
    if(st->min_fanout == 0 || n < st->min_fanout) st->min_fanout = n;
    if(n > st->max_fanout) st->max_fanout = n;
}

static int boundary5_apply_candidate(const Hex5Model *m, const Boundary5State *in,
                                     int edge_pos, Boundary5Cell outside_pos,
                                     int cand, Boundary5State *out){
    char err[256];
    *out = *in;
    int added = boundary5_patch_add(&out->patch, outside_pos, cand, 0u);
    if(added < 0) return 0;
    if(!boundary5_patch_check_edges(m, &out->patch, err, (int)sizeof(err))) return 0;

    int newcycle[BOUNDARY5_MAX_CYCLE];
    int nn = 0;
    for(int k=0;k<in->ncycle;k++){
        int item = in->cycle[k];
        if(item >= 0 && item < out->patch.n && boundary5_patch_is_boundary_cell(&out->patch, item)){
            if(nn >= BOUNDARY5_MAX_CYCLE) return 0;
            newcycle[nn++] = item;
        }
        if(k == edge_pos){
            if(boundary5_patch_is_boundary_cell(&out->patch, added)){
                if(nn >= BOUNDARY5_MAX_CYCLE) return 0;
                newcycle[nn++] = added;
            }
        }
    }
    out->ncycle = nn;
    for(int k=0;k<nn;k++) out->cycle[k] = newcycle[k];
    return boundary5_state_cycle_ok(out);
}


static int boundary5_lookup_edge_direct(const Hex5Model *m, const VComp5Dict *v,
                                        const Boundary5Patch *p,
                                        int prev_item, int next_item,
                                        Boundary5Cell outside_pos,
                                        int out[BOUNDARY5_MAX_CANDIDATES],
                                        int cap){
    int n = 0;
    int outs[HEX5_MAX_ORIENTED];
    int u;
    int qnext, qprev;
    if(prev_item < 0 || prev_item >= p->n || next_item < 0 || next_item >= p->n) return 0;
    if(boundary5_patch_find(p, outside_pos) >= 0) return 0;
    u = boundary5_dir_between(p->cell[prev_item], p->cell[next_item]);
    if(u < 0) return 0;

    /*
       Boundary edge prev -> next is a CCW baseline.  Exterior growth uses
       lookup(next, prev).

       Let U be the fixed-North direction prev -> next.  The shared edge is
       prev slot U against next slot U+3.  The dictionary input convention is:

           next = [m X ....]
           prev = [Y m'....]

       with m matching m'.  Therefore next is shifted by U+3 and prev by U-1
       so that next[0] and prev[1] are the shared matched edge labels.
       The returned outside tile is shifted back so dict slot 0 lands on the
       outside cell side U+1, which is the edge back to next.
    */
    qnext = boundary5_rot_edge_to_slot0(m, p->hex[next_item], u + 3);
    qprev = boundary5_rot_edge_to_slot0(m, p->hex[prev_item], u + 5);
    if(qnext < 0 || qprev < 0) return 0;
    int no = vcomp5_lookup_pair(v, qnext, qprev, outs, HEX5_MAX_ORIENTED);
    for(int i=0;i<no;i++){
        int cand = boundary5_rot_left_idx(m, outs[i], -(u + 1));
        int seen = 0;
        Boundary5Patch trial;
        char err[256];
        if(cand < 0) continue;

        /*
           Direct frame rule learned from the fixed-North vertex probe:
             next key = shift by U+3
             prev key = shift by U-1
             returned outside key is unshifted by U+1, in the reverse
             direction, before insertion into the fixed-North grid.
           The patch validator is intentionally retained here while the
           surround code is young: it checks all occupied neighbor edges in
           fixed-North slots, including prev/next and any extra touches.
        */
        trial = *p;
        if(boundary5_patch_add(&trial, outside_pos, cand, 0u) < 0) continue;
        if(!boundary5_patch_check_edges(m, &trial, err, (int)sizeof(err))) continue;

        for(int k=0;k<n && k<cap;k++) if(out[k] == cand) seen = 1;
        if(seen) continue;
        if(n < cap) out[n] = cand;
        n++;
    }
        return n;
}


static void boundary5_print_hex_row(FILE *f, const Hex5Model *m, int idx){
    if(!f) return;
    if(idx < 0 || idx >= m->noriented){ fprintf(f, "<bad:%d>", idx); return; }
    fprintf(f, "[%s %s %s %s %s %s]",
            m->oriented[idx].e[0], m->oriented[idx].e[1],
            m->oriented[idx].e[2], m->oriented[idx].e[3],
            m->oriented[idx].e[4], m->oriented[idx].e[5]);
}

static void boundary5_trace_edge_frames(const Hex5Model *m, const VComp5Dict *v,
                                        const Boundary5Patch *p,
                                        int prev_item, int next_item,
                                        Boundary5Cell outside_pos){
    int u = boundary5_dir_between(p->cell[prev_item], p->cell[next_item]);
    int qprev = -1, qnext = -1;
    int outs[HEX5_MAX_ORIENTED];
    int direct_count = 0;
    int brute_count = 0;
    int shown = 0;
    fprintf(stderr, "trace frames: edge item %d->%d cell (%d,%d)->(%d,%d) out=(%d,%d) u=%d\n",
            prev_item, next_item,
            p->cell[prev_item].q, p->cell[prev_item].r,
            p->cell[next_item].q, p->cell[next_item].r,
            outside_pos.q, outside_pos.r, u);
    fprintf(stderr, "  prev world hex=%d ", p->hex[prev_item]);
    boundary5_print_hex_row(stderr, m, p->hex[prev_item]);
    fprintf(stderr, "\n  next world hex=%d ", p->hex[next_item]);
    boundary5_print_hex_row(stderr, m, p->hex[next_item]);
    fprintf(stderr, "\n");
    if(u >= 0){
        qprev = boundary5_rot_edge_to_slot0(m, p->hex[prev_item], u + 5);
        qnext = boundary5_rot_edge_to_slot0(m, p->hex[next_item], u + 3);
        fprintf(stderr, "  direct shifts: prev=%d next=%d out_unshift=%d\n",
                mod6(u + 5), mod6(u + 3), mod6(u + 1));
        fprintf(stderr, "  direct prev key=%d ", qprev);
        boundary5_print_hex_row(stderr, m, qprev);
        fprintf(stderr, "\n  direct next key=%d ", qnext);
        boundary5_print_hex_row(stderr, m, qnext);
        fprintf(stderr, "\n");
        if(qprev >= 0 && qnext >= 0){
            int no = vcomp5_lookup_pair(v, qnext, qprev, outs, HEX5_MAX_ORIENTED);
            fprintf(stderr, "  direct lookup count=%d\n", no);
            for(int i=0;i<no && i<8;i++){
                int cand = boundary5_rot_left_idx(m, outs[i], -(u + 1));
                Boundary5Patch trial = *p;
                char err[256];
                int ok = 0;
                if(cand >= 0 && boundary5_patch_add(&trial, outside_pos, cand, 0u) >= 0 &&
                   boundary5_patch_check_edges(m, &trial, err, (int)sizeof(err))) ok = 1;
                fprintf(stderr, "    direct out[%d] dict=%d world=%d ok=%d ", i, outs[i], cand, ok);
                boundary5_print_hex_row(stderr, m, cand);
                if(!ok && cand >= 0){
                    Boundary5Patch trial2 = *p;
                    if(boundary5_patch_add(&trial2, outside_pos, cand, 0u) >= 0 &&
                       !boundary5_patch_check_edges(m, &trial2, err, (int)sizeof(err)))
                        fprintf(stderr, " err=%s", err);
                }
                fprintf(stderr, "\n");
            }
            direct_count = no;
        }
    }
    fprintf(stderr, "  brute winning frames (first 16):\n");
    for(int rn=0; rn<HEX5_SIDES; rn++){
        int bnext = boundary5_rot_left_idx(m, p->hex[next_item], rn);
        if(bnext < 0) continue;
        for(int rp=0; rp<HEX5_SIDES; rp++){
            int bprev = boundary5_rot_left_idx(m, p->hex[prev_item], rp);
            int no;
            if(bprev < 0) continue;
            no = vcomp5_lookup_pair(v, bnext, bprev, outs, HEX5_MAX_ORIENTED);
            if(no <= 0) continue;
            for(int i=0;i<no;i++){
                int worlds[HEX5_SIDES];
                int nw = boundary5_candidate_world_orients(m, p, outside_pos, outs[i], worlds, HEX5_SIDES);
                for(int w=0; w<nw; w++){
                    brute_count++;
                    if(shown++ < 16){
                        fprintf(stderr, "    rn=%d rp=%d outdict=%d world=%d ", rn, rp, outs[i], worlds[w]);
                        boundary5_print_hex_row(stderr, m, worlds[w]);
                        fprintf(stderr, "\n");
                    }
                }
            }
        }
    }
    fprintf(stderr, "  frame summary: direct_lookup=%d brute_world_hits=%d shown=%d\n",
            direct_count, brute_count, shown < 16 ? shown : 16);
}

static int boundary5_edge_candidate_count(const Hex5Model *m, const VComp5Dict *v,
                                          const Boundary5State *s, int edge_pos,
                                          Boundary5Cell *outside_pos,
                                          int out[BOUNDARY5_MAX_CANDIDATES]){
    int prev = s->cycle[edge_pos];
    int next = s->cycle[(edge_pos+1)%s->ncycle];
    if(!boundary5_common_outside(s->patch.cell[prev], s->patch.cell[next], outside_pos)) return 0;
    return boundary5_lookup_edge_direct(m, v, &s->patch, prev, next, *outside_pos,
                                        out, BOUNDARY5_MAX_CANDIDATES);
}

static int boundary5_choose_edge(const Hex5Model *m, const VComp5Dict *v,
                                 const Boundary5State *s,
                                 Boundary5Cell *outside_pos,
                                 int out[BOUNDARY5_MAX_CANDIDATES], int *nout){
    int best_i = -1;
    int best_n = 0;
    Boundary5Cell best_pos = {0,0};
    int best[BOUNDARY5_MAX_CANDIDATES];
    for(int i=0;i<s->ncycle;i++){
        int prev = s->cycle[i];
        int next = s->cycle[(i+1)%s->ncycle];
        int touches_vanish = 0;
        Boundary5Cell pos;
        int cand[BOUNDARY5_MAX_CANDIDATES];
        int n;
        if(prev >= 0 && prev < s->patch.n && (s->patch.flags[prev] & BOUNDARY5_VANISH)) touches_vanish = 1;
        if(next >= 0 && next < s->patch.n && (s->patch.flags[next] & BOUNDARY5_VANISH)) touches_vanish = 1;
        if(!touches_vanish) continue;
        n = boundary5_edge_candidate_count(m, v, s, i, &pos, cand);
        if(n <= 0) continue;
        if(best_i < 0 || n < best_n){
            best_i = i;
            best_n = n;
            best_pos = pos;
            for(int k=0;k<n && k<BOUNDARY5_MAX_CANDIDATES;k++) best[k] = cand[k];
        }
    }
    if(best_i < 0){
        if(0){
            fprintf(stderr, "trace choose: no edge ncycle=%d\n", s->ncycle);
            for(int ti=0; ti<s->ncycle; ti++){
                Boundary5Cell pos;
                int cand[BOUNDARY5_MAX_CANDIDATES];
                int tn = boundary5_edge_candidate_count(m, v, s, ti, &pos, cand);
                int tb = 0;
                { int a0=s->cycle[ti], b0=s->cycle[(ti+1)%s->ncycle];
                  tb = boundary5_lookup_edge_bruteforce(m, v, &s->patch, a0, b0, pos, cand, BOUNDARY5_MAX_CANDIDATES); }
                int a = s->cycle[ti];
                int b = s->cycle[(ti+1)%s->ncycle];
                fprintf(stderr, " edge %d item %d->%d cell (%d,%d)->(%d,%d) out=(%d,%d) n=%d brute=%d flags=%u,%u\n",
                        ti, a, b, s->patch.cell[a].q, s->patch.cell[a].r, s->patch.cell[b].q, s->patch.cell[b].r,
                        pos.q, pos.r, tn, tb, s->patch.flags[a], s->patch.flags[b]);
                if(tn == 0 && tb > 0 && 0){
                    boundary5_trace_edge_frames(m, v, &s->patch, a, b, pos);
                }
            }
        }
        *nout = 0; return -1;
    }
    *outside_pos = best_pos;
    *nout = best_n;
    for(int k=0;k<best_n && k<BOUNDARY5_MAX_CANDIDATES;k++) out[k] = best[k];
    return best_i;
}

static int boundary5_complete_one_layer(const Hex5Model *m, const VComp5Dict *v,
                                        const Boundary5State *start,
                                        Boundary5State **done_out, int *ndone_out,
                                        int *dead, int *escape,
                                        Boundary5DepthCounts *stats,
                                        int max_states){
    Boundary5State *cur = NULL, *nexts = NULL, *done = NULL;
    int cur_cap = 0, next_cap = 0, done_cap = 0;
    int ncur = 1, ndone = 0;
    int local_dead = 0;
    int local_escape = 0;

    if(stats){
        stats->processed = 0;
        stats->branches = 0;
        stats->accepted = 0;
        stats->completed = 0;
        stats->min_fanout = 0;
        stats->max_fanout = 0;
        stats->max_cells = 0;
        stats->max_cycle = 0;
        stats->max_frontier = 1;
    }
    if(!boundary5_state_vec_reserve(&cur, &cur_cap, 1)){
        if(escape) *escape += 1;
        if(stats) stats->escape += 1;
        return 0;
    }
    cur[0] = *start;
    boundary5_stats_touch_state(stats, start);

    for(int step=0; step<BOUNDARY5_MAX_LAYER_STEPS && ncur>0; step++){
        int nnext = 0;
        if(stats && ncur > stats->max_frontier) stats->max_frontier = ncur;
        for(int i=0;i<ncur;i++){
            if(stats){ stats->processed++; boundary5_stats_touch_state(stats, &cur[i]); }
            if(boundary5_state_complete(&cur[i])){
                if(stats) stats->completed++;
                if(max_states > 0 && ndone >= max_states){
                    local_escape++;
                    if(stats) stats->escape++;
                    continue;
                }
                if(!boundary5_state_vec_add_unique(&done, &ndone, &done_cap, &cur[i])){
                    local_escape++;
                    if(stats) stats->escape++;
                }
                continue;
            }
            Boundary5Cell pos;
            int cand[BOUNDARY5_MAX_CANDIDATES], ncand = 0;
            int edge = boundary5_choose_edge(m, v, &cur[i], &pos, cand, &ncand);
            if(edge < 0 || ncand <= 0){ local_dead++; if(stats) stats->dead++; continue; }
            if(stats){ stats->branches += ncand; boundary5_stats_fanout(stats, ncand); }
            for(int c=0;c<ncand;c++){
                Boundary5State ns;
                if(!boundary5_apply_candidate(m, &cur[i], edge, pos, cand[c], &ns)) continue;
                if(stats){ stats->accepted++; boundary5_stats_touch_state(stats, &ns); }
                if(boundary5_state_complete(&ns)){
                    if(stats) stats->completed++;
                    if(max_states > 0 && ndone >= max_states){
                        local_escape++;
                        if(stats) stats->escape++;
                        continue;
                    }
                    if(!boundary5_state_vec_add_unique(&done, &ndone, &done_cap, &ns)){
                        local_escape++;
                        if(stats) stats->escape++;
                    }
                } else {
                    if(max_states > 0 && nnext >= max_states){
                        local_escape++;
                        if(stats) stats->escape++;
                        continue;
                    }
                    if(!boundary5_state_vec_add_unique(&nexts, &nnext, &next_cap, &ns)){
                        local_escape++;
                        if(stats) stats->escape++;
                    }
                }
            }
        }
        free(cur);
        cur = nexts;
        cur_cap = next_cap;
        nexts = NULL;
        next_cap = 0;
        ncur = nnext;
    }
    if(ncur > 0){ local_escape += ncur; if(stats) stats->escape += ncur; }
    free(cur);
    free(nexts);
    if(dead) *dead += local_dead;
    if(escape) *escape += local_escape;
    if(stats){
        stats->live = ndone;
        stats->dead = local_dead;
        stats->escape = local_escape;
    }
    *done_out = done;
    *ndone_out = ndone;
    return ndone;
}

static int boundary5_init_single(const Hex5Model *m, const VComp5Dict *v, int seed_base,
                                 Boundary5State *out, int cap){
    int nout = 0;
    int pairs[HEX5_MAX_ORIENTED][2];
    int fanout[HEX5_SIDES];
    int best = 0;
    int h0_world = boundary5_find_oriented_model(m, &m->tiles[seed_base]);
    if(h0_world < 0) return 0;

    /*
       The one-hex lower-bound case is a formal self-loop.  To normalize it
       into a geometric three-cell boundary, any of the six vertices may be
       used as the first completion site.  For comprehensive enumeration we
       do not need to start from all six sites: every completed surround must
       complete every seed vertex, so start from a minimum-fanout vertex and
       let the boundary grower enumerate from there.
    */
    for(int d=0; d<HEX5_SIDES; d++){
        int h0_key = boundary5_rot_left_idx(m, h0_world, d);
        fanout[d] = 0;
        if(h0_key >= 0) fanout[d] = vcomp5_lookup_one(v, h0_key, pairs, HEX5_MAX_ORIENTED);
        if(fanout[d] > 0 && (best == 0 || fanout[d] < best)) best = fanout[d];
    }

    for(int d=0; d<HEX5_SIDES; d++){
        Boundary5Cell c0 = {0,0}, c1, c2;
        int h0_key = boundary5_rot_left_idx(m, h0_world, d);
        int np;
        if(h0_key < 0 || fanout[d] <= 0 || fanout[d] != best) continue;
        np = vcomp5_lookup_one(v, h0_key, pairs, HEX5_MAX_ORIENTED);
        boundary5_neighbor(c0, d, &c1);
        boundary5_neighbor(c0, d+1, &c2);

        for(int p=0;p<np;p++){
            /*
               Normalize the formal one-tile seed into a geometric three-cell
               vertex figure in fixed-North orientation.

               lookup_one(h0_key) returns [h1_key,h2_key] in dictionary order:
                   h0_key[0] matches h1_key[1]
                   h1_key[0] matches h2_key[1]
                   h2_key[0] matches h0_key[1]

               In world coordinates the seed tile is fixed-North, and this
               vertex uses seed sides d and d+1.  Therefore:
                   h1_key[1] must land on world side d+3
                   h2_key[0] must land on world side d+4
               This keeps all placed cells in fixed-North slots; only the
               temporary dictionary keys are rotated.
            */
            int h1 = boundary5_rot_left_idx(m, pairs[p][0], 1 - (d + 3));
            int h2 = boundary5_rot_left_idx(m, pairs[p][1], 0 - (d + 4));
            Boundary5State s;
            char err[256];
            if(h1 < 0 || h2 < 0) continue;
            memset(&s, 0, sizeof(s));
            int i0 = boundary5_patch_add(&s.patch, c0, h0_world, BOUNDARY5_VANISH);
            int i1 = boundary5_patch_add(&s.patch, c1, h1, 0u);
            int i2 = boundary5_patch_add(&s.patch, c2, h2, 0u);
            if(i0 < 0 || i1 < 0 || i2 < 0) continue;
            s.cycle[0] = i0; s.cycle[1] = i1; s.cycle[2] = i2; s.ncycle = 3;
            if(!boundary5_patch_check_edges(m, &s.patch, err, (int)sizeof(err))) continue;
            if(!boundary5_state_cycle_ok(&s)) continue;
            if(nout < cap && !boundary5_state_seen(out, nout, &s)) out[nout++] = s;
            else if(nout >= cap) return nout;
        }
    }
    return nout;
}

static int boundary5_add_ring_result(const Hex5Model *m, BComp5Result *b, const Ring5 *r){
    Ring5 c;
    boundary5_ring_canon(m, r, &c);
    for(int i=0;i<b->nrings;i++) if(boundary5_ring_cmp(&b->rings[i], &c) == 0) return 1;
    if(b->nrings >= HEX5_MAX_BCOMP) return 0;
    b->rings[b->nrings++] = c;
    return 1;
}

static int boundary5_add_completed_rings(const Hex5Model *m, BComp5Result *b,
                                         const Boundary5State *states, int nstates){
    for(int i=0;i<nstates;i++){
        Ring5 r;
        if(!boundary5_extract_one_vanish_ring(m, &states[i], &r)) continue;
        if(!boundary5_add_ring_result(m, b, &r)) return 0;
    }
    return 1;
}

int boundary5_collect_depth1_rings(const Hex5Model *m, const VComp5Dict *v,
                                   int max_states, BComp5Result *out,
                                   int *dead, int *escape){
    Boundary5State *states = NULL;
    int local_dead = 0;
    int local_escape = 0;
    memset(out, 0, sizeof(*out));
    if(max_states <= 0 || max_states > BOUNDARY5_MAX_STATES) max_states = BOUNDARY5_MAX_STATES;
    states = calloc((size_t)max_states, sizeof(*states));
    if(!states){
        free(states);
        if(escape) *escape = 1;
        if(dead) *dead = 0;
        return 0;
    }
    for(int seed=0; seed<m->ntiles; seed++){
        int nstates = boundary5_init_single(m, v, seed, states, max_states);
        int ndone = 0;
        out->starts += nstates;
        for(int i=0;i<nstates;i++){
            Boundary5State *done_i = NULL;
            int done_i_n = 0;
            int d0 = 0, e0 = 0;
            boundary5_complete_one_layer(m, v, &states[i], &done_i, &done_i_n, &d0, &e0, NULL, 0);
            ndone += done_i_n;
            local_dead += d0;
            local_escape += e0;
            if(!boundary5_add_completed_rings(m, out, done_i, done_i_n)){
                free(done_i);
                free(states);
                if(dead) *dead = local_dead;
                if(escape) *escape = local_escape + 1;
                return 0;
            }
            free(done_i);
        }
        out->closure_hits += ndone;
    }
    free(states);
    if(dead) *dead = local_dead;
    if(escape) *escape = local_escape;
    return 1;
}

int boundary5_probe_seed(const Hex5Model *m, const VComp5Dict *v,
                         int seed_base, int max_depth, int max_states,
                         Boundary5SeedReport *report){
    Boundary5State *states = NULL;
    int nstates, escaped = 0;
    if(max_depth > 15) max_depth = 15;
    if(max_states <= 0 || max_states > BOUNDARY5_MAX_STATES) max_states = BOUNDARY5_MAX_STATES;
    memset(report, 0, sizeof(*report));
    report->max_depth = max_depth;
    report->max_states = max_states;
    states = calloc((size_t)max_states, sizeof(*states));
    if(!states){ free(states); return 0; }
    nstates = boundary5_init_single(m, v, seed_base, states, max_states);
    if(nstates >= max_states) escaped++;
    report->depth[0].live = nstates;
    report->depth[0].escape = escaped;
    for(int d=1; d<=max_depth; d++){
        Boundary5State *all_done = NULL;
        int all_done_n = 0, all_done_cap = 0;
        int dead = 0, escape = 0;
        Boundary5DepthCounts agg;
        memset(&agg, 0, sizeof(agg));
        agg.max_frontier = nstates;
        for(int i=0;i<nstates;i++){
            Boundary5State *done_i = NULL;
            Boundary5State work;
            Boundary5DepthCounts st;
            int done_i_n = 0;
            work = states[i];
            if(d > 1) boundary5_state_begin_next_layer(&work);
            memset(&st, 0, sizeof(st));
            boundary5_complete_one_layer(m, v, &work, &done_i, &done_i_n, &dead, &escape, &st, max_states);
            agg.processed += st.processed;
            agg.branches += st.branches;
            agg.accepted += st.accepted;
            agg.completed += st.completed;
            if(st.min_fanout && (agg.min_fanout == 0 || st.min_fanout < agg.min_fanout)) agg.min_fanout = st.min_fanout;
            if(st.max_fanout > agg.max_fanout) agg.max_fanout = st.max_fanout;
            if(st.max_cells > agg.max_cells) agg.max_cells = st.max_cells;
            if(st.max_cycle > agg.max_cycle) agg.max_cycle = st.max_cycle;
            if(st.max_frontier > agg.max_frontier) agg.max_frontier = st.max_frontier;
            for(int j=0;j<done_i_n;j++){
                if(max_states > 0 && all_done_n >= max_states){
                    escape++;
                    agg.escape++;
                    continue;
                }
                if(!boundary5_state_vec_add_unique(&all_done, &all_done_n, &all_done_cap, &done_i[j])){
                    escape++;
                    agg.escape++;
                }
            }
            free(done_i);
        }
        report->depth[d] = agg;
        report->depth[d].live = all_done_n;
        report->depth[d].dead = dead;
        report->depth[d].escape = escape;
        report->depth[d].unique_rings = boundary5_unique_completed_rings(m, all_done, all_done_n);
        free(states);
        states = all_done;
        nstates = all_done_n;
        if(nstates == 0) break;
    }
    free(states);
    return 1;
}
