#include "rl5/hex5.h"

static int vfig_cmp_raw(const int a[3], const int b[3]){
    for(int i=0;i<3;i++){ if(a[i] != b[i]) return a[i] - b[i]; }
    return 0;
}

static void canon3(int h0, int h1, int h2, int out[3]){
    int cand[3][3] = {{h0,h1,h2},{h1,h2,h0},{h2,h0,h1}};
    int best = 0;
    for(int i=1;i<3;i++) if(vfig_cmp_raw(cand[i], cand[best]) < 0) best = i;
    out[0] = cand[best][0]; out[1] = cand[best][1]; out[2] = cand[best][2];
}

static int add_vfig(VComp5Dict *v, int h0, int h1, int h2){
    int c[3];
    canon3(h0,h1,h2,c);
    for(int i=0;i<v->nfigs;i++){
        if(v->figs[i].h[0] == c[0] && v->figs[i].h[1] == c[1] && v->figs[i].h[2] == c[2]) return 1;
    }
    if(v->nfigs >= HEX5_MAX_VFIGS) return 0;
    v->figs[v->nfigs].h[0] = c[0];
    v->figs[v->nfigs].h[1] = c[1];
    v->figs[v->nfigs].h[2] = c[2];
    v->nfigs++;
    return 1;
}

int vcomp5_build(const Attach5Dict *a, VComp5Dict *v){
    int list2[HEX5_MAX_ORIENTED];
    int list3[HEX5_MAX_ORIENTED];
    v->nfigs = 0;
    v->one_entries = 0;
    v->pair_entries = 0;
    for(int h1=0; h1<a->noriented; h1++){
        int n2 = attach5_lookup(a, h1, list2, HEX5_MAX_ORIENTED);
        if(n2 == 0 && h1 > 0) continue;
        for(int i=0;i<n2;i++){
            int h2 = list2[i];
            int n3 = attach5_lookup(a, h2, list3, HEX5_MAX_ORIENTED);
            for(int j=0;j<n3;j++){
                int h3 = list3[j];
                if(attach5_has(a, h3, h1)){
                    if(!add_vfig(v, h1, h2, h3)) return 0;
                }
            }
        }
    }
    v->one_entries = (long)v->nfigs * 3L;
    v->pair_entries = (long)v->nfigs * 3L;
    return 1;
}

int vcomp5_lookup_one(const VComp5Dict *v, int h, int (*out)[2], int cap){
    int n = 0;
    for(int i=0;i<v->nfigs;i++){
        const int *f = v->figs[i].h;
        for(int k=0;k<3;k++){
            if(f[k] == h){
                if(n < cap){ out[n][0] = f[(k+1)%3]; out[n][1] = f[(k+2)%3]; }
                n++;
            }
        }
    }
    return n;
}

int vcomp5_lookup_pair(const VComp5Dict *v, int h0, int h1, int *out, int cap){
    int n = 0;
    for(int i=0;i<v->nfigs;i++){
        const int *f = v->figs[i].h;
        for(int k=0;k<3;k++){
            if(f[k] == h0 && f[(k+1)%3] == h1){
                if(n < cap) out[n] = f[(k+2)%3];
                n++;
            }
        }
    }
    return n;
}
