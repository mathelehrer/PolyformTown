#include "evo7.h"

#include <stdio.h>
#include <string.h>

static int g_last_raw_actions = 0;

static int cyclic_known_run_before_adds(const Cell *c, const Action *a, char *run, size_t runsz);
static int cyclic_known_run_anywhere(const Cell *c, char *run, size_t runsz);
static void describe_remember_action(const Cell *c, const Action *a, char *out, size_t outsz);

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


int enumerate_actions(const Model *m, const State *s, Action *acts){
    int n=0;
    enum_tile_complete(m,s,acts,&n);
    enum_across_edge(m,s,acts,&n);
    enum_down_edge(m,s,acts,&n);
    enum_vertex(m,s,acts,&n);
    enum_hex_forget(m,s,acts,&n);
    g_last_raw_actions = n;
    return filter_verified_actions(m, s, acts, n);
}


int apply_action(State *s, const Action *a, char *status, size_t sz){
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


int apply_parallel_actions(const Model *m, State *s,
                                  const Action *acts, int na,
                                  char *status, size_t sz){
    State tmp=*s;
    Add merged[MAX_ACTIONS * MAX_ADDS];
    int nmerged=0;
    int source_actions=0;
    char err[240];

    if(na <= 0){
        snprintf(status, sz, "ERR: no actions to apply");
        return 0;
    }

    for(int ai=0; ai<na; ai++){
        int action_changed = 0;
        for(int aj=0; aj<acts[ai].nadd; aj++){
            const Add *add=&acts[ai].add[aj];
            if(add->ci < 0 || add->ci >= s->ncell){
                snprintf(status, sz, "ERR: action %d has invalid cell index", ai + 1);
                return 0;
            }
            if(add->slot < 0 || add->slot >= 6){
                snprintf(status, sz, "ERR: action %d has invalid slot", ai + 1);
                return 0;
            }
            if(tok_empty(&add->sym)){
                snprintf(status, sz, "ERR: action %d tries to add an empty symbol", ai + 1);
                return 0;
            }

            const Tok *old = &s->cells[add->ci].slot[add->slot];
            if(!tok_empty(old)){
                if(!tok_eq(old, &add->sym)){
                    snprintf(status, sz,
                             "ERR: action %d conflicts at (%d,%d).slot%d: has %s, tried %s",
                             ai + 1, s->cells[add->ci].q, s->cells[add->ci].r,
                             add->slot, old->s, add->sym.s);
                    return 0;
                }
                continue;
            }

            int dup = 0;
            for(int mi=0; mi<nmerged; mi++){
                if(merged[mi].ci == add->ci && merged[mi].slot == add->slot){
                    if(!tok_eq(&merged[mi].sym, &add->sym)){
                        snprintf(status, sz,
                                 "ERR: parallel conflict at (%d,%d).slot%d: %s vs %s",
                                 s->cells[add->ci].q, s->cells[add->ci].r,
                                 add->slot, merged[mi].sym.s, add->sym.s);
                        return 0;
                    }
                    dup = 1;
                    break;
                }
            }
            if(dup) continue;
            if(nmerged >= (int)(sizeof(merged) / sizeof(merged[0]))){
                snprintf(status, sz, "ERR: too many parallel additions");
                return 0;
            }
            merged[nmerged++] = *add;
            action_changed = 1;
        }
        if(action_changed) source_actions++;
    }

    if(nmerged <= 0){
        snprintf(status, sz, "ERR: all parallel writes were already known");
        return 0;
    }

    for(int mi=0; mi<nmerged; mi++){
        Add *add=&merged[mi];
        if(!set_cell_slot(&tmp.cells[add->ci], add->slot, &add->sym,
                          err, sizeof(err))){
            snprintf(status, sz, "ERR: %s", err);
            return 0;
        }
    }
    if(!validate_state_local(m, &tmp, err, sizeof(err))){
        snprintf(status, sz, "ERR: parallel update failed validation: %.180s", err);
        return 0;
    }

    *s=tmp;
    snprintf(status, sz, "parallel step wrote %d letter%s from %d action%s",
             nmerged, nmerged == 1 ? "" : "s",
             source_actions, source_actions == 1 ? "" : "s");
    return 1;
}

void push_hist(History *h,const State *s){ if(h->n>=MAX_HIST){ memmove(&h->s[0],&h->s[1],sizeof(State)*(MAX_HIST-1)); h->n=MAX_HIST-1; } h->s[h->n++]=*s; }

int pop_hist(History *h,State *s){ if(h->n<=0) return 0; *s=h->s[--h->n]; return 1; }


