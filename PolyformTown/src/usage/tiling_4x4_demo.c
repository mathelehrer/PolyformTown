#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/dfs.h"

enum {
    W = 4,
    H = 4,
    FULL = 0xFFFF,
    MAXP = 256,
    DEPTH = 4,
    ORDER_INDEX = 0,
    ORDER_RARE = 1,
    ORDER_COMMON = 2,
    ORDER_MRV = 3
};

typedef struct { uint16_t mask; uint8_t depth; int path[DEPTH]; } S;
typedef struct {
    uint16_t p[MAXP];
    int n;
    int ord[MAXP];
    int cover[16];
    int mode;
    int trace_on;
    FILE *trace_fp;
    size_t sol;
} C;

static int bit(int x, int y) { return 1 << (y * W + x); }
static int pc(uint16_t m){int c=0;while(m){c+=m&1;m>>=1;}return c;}
static int has(uint16_t *a,int n,uint16_t v){for(int i=0;i<n;i++)if(a[i]==v)return 1;return 0;}

static void add(C *c, const int pts[][2], int k) {
    int minx = 99, miny = 99, maxx = 0, maxy = 0, n[4][2];
    for (int i = 0; i < k; i++) {
        if (pts[i][0] < minx) minx = pts[i][0];
        if (pts[i][1] < miny) miny = pts[i][1];
    }
    for (int i = 0; i < k; i++) {
        n[i][0] = pts[i][0] - minx;
        n[i][1] = pts[i][1] - miny;
        if (n[i][0] > maxx) maxx = n[i][0];
        if (n[i][1] > maxy) maxy = n[i][1];
    }
    for (int y = 0; y + maxy < H; y++) for (int x = 0; x + maxx < W; x++) {
        uint16_t m = 0;
        for (int i = 0; i < k; i++) m |= bit(x + n[i][0], y + n[i][1]);
        if (!has(c->p, c->n, m)) c->p[c->n++] = m;
    }
}

static void init_ctx(C *c) {
    memset(c, 0, sizeof(*c));
    int I[2][4][2] = {{{0,0},{1,0},{2,0},{3,0}},{{0,0},{0,1},{0,2},{0,3}}};
    int O[1][4][2] = {{{0,0},{1,0},{0,1},{1,1}}};
    int T[4][4][2] = {{{0,0},{1,0},{2,0},{1,1}},{{0,1},{1,0},{1,1},{1,2}},{{1,0},{0,1},{1,1},{2,1}},{{0,0},{0,1},{0,2},{1,1}}};
    int L[8][4][2] = {{{0,0},{0,1},{0,2},{1,2}},{{0,0},{1,0},{2,0},{0,1}},{{0,0},{1,0},{1,1},{1,2}},{{2,0},{0,1},{1,1},{2,1}},{{1,0},{1,1},{1,2},{0,2}},{{0,0},{0,1},{1,1},{2,1}},{{0,0},{1,0},{0,1},{0,2}},{{0,0},{1,0},{2,0},{2,1}}};
    int Z[4][4][2] = {{{0,0},{1,0},{1,1},{2,1}},{{1,0},{0,1},{1,1},{0,2}},{{1,0},{2,0},{0,1},{1,1}},{{0,0},{0,1},{1,1},{1,2}}};
    for (int i = 0; i < 2; i++) add(c, I[i], 4);
    add(c, O[0], 4);
    for (int i = 0; i < 4; i++) add(c, T[i], 4);
    for (int i = 0; i < 8; i++) add(c, L[i], 4);
    for (int i = 0; i < 4; i++) add(c, Z[i], 4);
    for (int i = 0; i < c->n; i++) {
        c->ord[i] = i;
        for (int b = 0; b < 16; b++) if (c->p[i] & (1u << b)) c->cover[b]++;
    }
}

static int score(C *c, uint16_t m){int s=0;for(int b=0;b<16;b++)if(m&(1u<<b))s+=c->cover[b];return s;}
static int choose_mrv_cell(const C *c, const S *in) {
    int best_cell = -1;
    int best_count = 999;
    int min_idx = (in->depth > 0) ? in->path[in->depth - 1] : -1;
    for (int cell = 0; cell < 16; cell++) {
        if (in->mask & (1u << cell)) continue;
        int count = 0;
        for (int i = 0; i < c->n; i++) {
            uint16_t p = c->p[i];
            if (i <= min_idx) continue;
            if (!(p & (1u << cell))) continue;
            if (p & in->mask) continue;
            count++;
        }
        if (count < best_count) { best_count = count; best_cell = cell; }
    }
    return best_cell;
}
static void mkord(C *c){
    for(int i=0;i<c->n;i++) c->ord[i]=i;
    for(int i=0;i<c->n;i++) for(int j=i+1;j<c->n;j++){
        int a=c->ord[i], b=c->ord[j];
        int sa=score(c,c->p[a]), sb=score(c,c->p[b]);
        int sw=(c->mode==ORDER_RARE)?(sa>sb):(c->mode==ORDER_COMMON)?(sa<sb):(a>b);
        if(sw){int t=c->ord[i];c->ord[i]=c->ord[j];c->ord[j]=t;}
    }
}

static int nx(const void *st,size_t d,int *cur,void *ch,int *cid,void *vc){
    const S *in=st; S *out=ch; C *c=vc; (void)d;
    int mrv_cell = (c->mode == ORDER_MRV) ? choose_mrv_cell(c, in) : -1;
    while(*cur<c->n){
        int idx=c->ord[(*cur)++];
        uint16_t p = c->p[idx];
        if (mrv_cell >= 0 && !(p & (1u << mrv_cell))) continue;
        if (c->mode != ORDER_MRV && in->depth > 0 && idx <= in->path[in->depth - 1]) continue;
        *out=*in;
        out->depth=in->depth+1;
        out->path[in->depth]=idx;
        out->mask|=p;
        if(cid)*cid=idx;
        return 1;
    }
    return 0;
}
static int ok(const void *st,size_t d,void *vc){(void)d;(void)vc; const S *s=st; return pc(s->mask)==s->depth*4;}
static int done(const void *st,size_t d,void *vc){(void)d;(void)vc; return ((const S*)st)->mask==FULL;}
static void hit(const void *st,size_t d,void *vc){(void)st;(void)d; ((C*)vc)->sol++;}

static const char *event_name(int e) {
    if (e == DFS_TRACE_ENTER) return "E";
    if (e == DFS_TRACE_PRUNE) return "P";
    if (e == DFS_TRACE_BACKTRACK) return "B";
    if (e == DFS_TRACE_SOLUTION) return "S";
    return "?";
}
static void trace_line(size_t depth,const void *state,int cid,int event,void *vctx){
    C *c=vctx; const S*s=state;
    if(!c->trace_on||!c->trace_fp)return;
    fprintf(c->trace_fp,"%s\t%zu\t%d\t0x%04x\n",event_name(event),depth,cid,s->mask);
}

static void print_mask_binary(uint16_t mask) {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            putchar((mask & (1u << (y * W + x))) ? '1' : '0');
        }
    }
}

int main(int argc, char **argv){
    C c; S seed = {0}; DfsConfig cfg; DfsStats st;

    init_ctx(&c);
    c.mode = ORDER_MRV;
    for (int i = 1; i < argc; i++) {
        if(strcmp(argv[i],"--trace")==0 && i+1<argc){
            c.trace_on=1;
            c.trace_fp=fopen(argv[++i],"w");
            if(!c.trace_fp)return 1;
            fprintf(c.trace_fp,"ev\tdepth\tidx\tmask\n");
        } else if(strcmp(argv[i],"--order")==0 && i+1<argc){
            i++;
            if(strcmp(argv[i],"rare")==0)c.mode=ORDER_RARE;
            else if(strcmp(argv[i],"common")==0)c.mode=ORDER_COMMON;
            else if(strcmp(argv[i],"mrv")==0)c.mode=ORDER_MRV;
            else c.mode=ORDER_INDEX;
        }
    }

    c.sol = 0;
    mkord(&c);
    cfg=(DfsConfig){sizeof(S),DEPTH,0,0,nx,ok,done,hit,trace_line,&c};
    if (!dfs_run(&cfg,&seed,&st)) {
        if (c.trace_fp) fclose(c.trace_fp);
        fprintf(stderr, "dfs_run failed for order=%d\n", c.mode);
        return 1;
    }
    printf("order=%s placements=%d solutions=%zu nodes=%zu prunes=%zu kept=%zu full=",
           c.mode==ORDER_INDEX?"index":c.mode==ORDER_RARE?"rare":c.mode==ORDER_COMMON?"common":"mrv",
           c.n,c.sol,st.nodes_visited,st.validity_prunes,st.solutions_kept);
    print_mask_binary(FULL);
    printf("\n");

    if (c.trace_fp) fclose(c.trace_fp);
    return 0;
}
