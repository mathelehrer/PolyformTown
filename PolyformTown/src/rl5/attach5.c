#include "rl5/hex5.h"
#include <string.h>

static int match_label(const Hex5Model *m, const char *a, const char *b){
    if(strcmp(a, b) == 0){
        for(int i=0;i<m->nrules;i++) if(strcmp(m->rule_a[i], a)==0 && strcmp(m->rule_b[i], b)==0) return 1;
    }
    for(int i=0;i<m->nrules;i++) if(strcmp(m->rule_a[i], a)==0 && strcmp(m->rule_b[i], b)==0) return 1;
    return 0;
}

int attach5_build(const Hex5Model *m, Attach5Dict *a){
    a->nedges = 0;
    a->noriented = m->noriented;
    for(int i=0;i<m->noriented;i++){
        for(int j=0;j<m->noriented;j++){
            if(match_label(m, m->oriented[i].e[0], m->oriented[j].e[1])){
                if(a->nedges >= HEX5_MAX_ATTACH) return 0;
                a->edges[a->nedges].from = i;
                a->edges[a->nedges].to = j;
                a->nedges++;
            }
        }
    }
    return 1;
}

int attach5_has(const Attach5Dict *a, int from, int to){
    for(int i=0;i<a->nedges;i++) if(a->edges[i].from == from && a->edges[i].to == to) return 1;
    return 0;
}

int attach5_lookup(const Attach5Dict *a, int from, int *out, int cap){
    int n = 0;
    for(int i=0;i<a->nedges;i++){
        if(a->edges[i].from == from){
            if(n < cap) out[n] = a->edges[i].to;
            n++;
        }
    }
    return n;
}
