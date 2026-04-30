#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/dfs.h"

enum { BOARD_FULL = 0x1FF, BOARD_W = 3, BOARD_H = 3, MAX_DEPTH = 3 };
enum { MAX_PLACEMENTS = 32, ORDER_INDEX = 0, ORDER_RARE = 1, ORDER_COMMON = 2, ORDER_MRV = 3 };

typedef struct { uint16_t mask; uint8_t depth; int path[MAX_DEPTH]; } DemoState;
typedef struct {
    uint16_t placements[MAX_PLACEMENTS];
    int order[MAX_PLACEMENTS];
    int n;
    int order_mode;
    int anchor_mode;
    int trace_on;
    FILE *trace_fp;
    size_t solution_count;
    size_t canonical_count;
    uint32_t canonical[16];
    int cover[9];
} DemoCtx;

static int bit_at(int x, int y) { return 1 << (y * BOARD_W + x); }
static int popc(uint16_t m){int c=0; while(m){c+=m&1; m>>=1;} return c;}
static int has_u16(uint16_t *a,int n,uint16_t v){for(int i=0;i<n;i++) if(a[i]==v) return 1; return 0;}
static int has_u32(uint32_t *a,int n,uint32_t v){for(int i=0;i<n;i++) if(a[i]==v) return 1; return 0;}
static int first_zero(uint16_t m){for(int i=0;i<9;i++) if(!(m&(1u<<i))) return i; return -1;}

static int choose_mrv_cell(const DemoCtx *ctx, const DemoState *in) {
    int best_cell = -1;
    int best_count = 999;
    int min_idx = (in->depth > 0) ? in->path[in->depth - 1] : -1;
    for (int cell = 0; cell < 9; cell++) {
        if (in->mask & (1u << cell)) continue;
        int count = 0;
        for (int i = 0; i < ctx->n; i++) {
            uint16_t p = ctx->placements[i];
            if (i <= min_idx) continue;
            if (!(p & (1u << cell))) continue;
            if (p & in->mask) continue;
            count++;
        }
        if (count < best_count) { best_count = count; best_cell = cell; }
    }
    return best_cell;
}

static uint16_t tx_mask(uint16_t m, int t) {
    uint16_t out = 0;
    for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++) {
        if (!(m & bit_at(x, y))) continue;
        int nx=x, ny=y;
        if (t==1){nx=2-y;ny=x;} else if (t==2){nx=2-x;ny=2-y;}
        else if (t==3){nx=y;ny=2-x;} else if (t==4){nx=2-x;ny=y;}
        else if (t==5){nx=x;ny=2-y;} else if (t==6){nx=y;ny=x;}
        else if (t==7){nx=2-y;ny=2-x;}
        out |= bit_at(nx, ny);
    }
    return out;
}

static void add_orient(uint16_t *arr, int *n, const int pts[][2], int k) {
    int minx = 99, miny = 99;
    for (int i = 0; i < k; i++) { if (pts[i][0] < minx) minx = pts[i][0]; if (pts[i][1] < miny) miny = pts[i][1]; }
    int norm[4][2]; int maxx = 0, maxy = 0;
    for (int i = 0; i < k; i++) { norm[i][0] = pts[i][0] - minx; norm[i][1] = pts[i][1] - miny; if (norm[i][0] > maxx) maxx = norm[i][0]; if (norm[i][1] > maxy) maxy = norm[i][1]; }
    for (int oy = 0; oy + maxy < BOARD_H; oy++) for (int ox = 0; ox + maxx < BOARD_W; ox++) {
        uint16_t m = 0; for (int i = 0; i < k; i++) m |= bit_at(ox + norm[i][0], oy + norm[i][1]);
        if (!has_u16(arr, *n, m)) arr[(*n)++] = m;
    }
}

static void placements_init(DemoCtx *ctx) {
    int i3[2][3][2] = {{{0,0},{1,0},{2,0}}, {{0,0},{0,1},{0,2}}};
    int l3[4][3][2] = {{{0,0},{1,0},{0,1}}, {{0,0},{1,0},{1,1}},{{0,0},{0,1},{1,1}}, {{1,0},{0,1},{1,1}}};
    ctx->n = 0;
    for (int i = 0; i < 2; i++) add_orient(ctx->placements, &ctx->n, i3[i], 3);
    for (int i = 0; i < 4; i++) add_orient(ctx->placements, &ctx->n, l3[i], 3);
    for (int i = 0; i < 9; i++) ctx->cover[i] = 0;
    for (int i = 0; i < ctx->n; i++) {
        for (int b = 0; b < 9; b++) if (ctx->placements[i] & (1u << b)) ctx->cover[b]++;
        ctx->order[i] = i;
    }
}

static int score(const DemoCtx *ctx, uint16_t m){int s=0; for(int b=0;b<9;b++) if(m&(1u<<b)) s+=ctx->cover[b]; return s;}
static void order_build(DemoCtx *ctx){
    for(int i=0;i<ctx->n;i++) ctx->order[i]=i;
    for(int i=0;i<ctx->n;i++) for(int j=i+1;j<ctx->n;j++){
        int a=ctx->order[i], b=ctx->order[j]; int sa=score(ctx,ctx->placements[a]), sb=score(ctx,ctx->placements[b]);
        int swap = (ctx->order_mode==ORDER_RARE)?(sa>sb):(ctx->order_mode==ORDER_COMMON)?(sa<sb):(a>b);
        if(swap){int t=ctx->order[i]; ctx->order[i]=ctx->order[j]; ctx->order[j]=t;}
    }
}

static int next_state(const void *state,size_t depth,int *cursor,void *child,int *cid,void *vctx){
    const DemoState *in=state; DemoState *out=child; DemoCtx *ctx=vctx; (void)depth;
    int anchor = first_zero(in->mask);
    int mrv_cell = (ctx->order_mode == ORDER_MRV) ? choose_mrv_cell(ctx, in) : -1;
    while(*cursor<ctx->n){int k=*cursor; int idx=ctx->order[k]; uint16_t p=ctx->placements[idx]; (*cursor)++;
        if (ctx->anchor_mode && anchor >= 0 && !(p & (1u << anchor))) continue;
        if (mrv_cell >= 0 && !(p & (1u << mrv_cell))) continue;
        if (ctx->order_mode != ORDER_MRV && in->depth > 0 && idx <= in->path[in->depth - 1]) continue;
        *out=*in; out->depth=(uint8_t)(in->depth+1); out->path[in->depth]=idx; out->mask|=p; if(cid) *cid=idx; return 1;}
    return 0;
}
static int validity(const void *state,size_t depth,void *vctx){(void)depth;(void)vctx; const DemoState *s=state; return popc(s->mask)==(int)s->depth*3;}
static int veracity(const void *state,size_t depth,void *vctx){(void)depth;(void)vctx; return ((DemoState*)state)->mask==BOARD_FULL;}
static void on_solution(const void *state,size_t depth,void *vctx){
    const DemoState *s=state; DemoCtx *ctx=vctx; (void)depth;
    ctx->solution_count++;
    uint16_t a=ctx->placements[s->path[0]], b=ctx->placements[s->path[1]], c=ctx->placements[s->path[2]];
    uint32_t best=0xFFFFFFFFu;
    for(int t=0;t<8;t++){
        uint16_t ta=tx_mask(a,t), tb=tx_mask(b,t), tc=tx_mask(c,t), v[3]={ta,tb,tc};
        for(int i=0;i<3;i++)for(int j=i+1;j<3;j++) if(v[j]<v[i]){uint16_t q=v[i];v[i]=v[j];v[j]=q;}
        uint32_t key=((uint32_t)v[0]) | ((uint32_t)v[1]<<9) | ((uint32_t)v[2]<<18);
        if(key<best) best=key;
    }
    if(!has_u32(ctx->canonical,(int)ctx->canonical_count,best)) ctx->canonical[ctx->canonical_count++]=best;
}
static const char *event_name(int e) {
    if (e == DFS_TRACE_ENTER) return "E";
    if (e == DFS_TRACE_PRUNE) return "P";
    if (e == DFS_TRACE_BACKTRACK) return "B";
    if (e == DFS_TRACE_SOLUTION) return "S";
    return "?";
}
static void trace_line(size_t depth,const void *state,int cid,int event,void *vctx){
    DemoCtx *ctx=vctx; const DemoState*s=state;
    if(!ctx->trace_on||!ctx->trace_fp)return;
    fprintf(ctx->trace_fp,"%s\t%zu\t%d\t0x%03x\n",event_name(event),depth,cid,s->mask);
}

static void print_mask_binary(uint16_t mask) {
    for (int y = 0; y < BOARD_H; y++) {
        for (int x = 0; x < BOARD_W; x++) {
            putchar((mask & (1u << (y * BOARD_W + x))) ? '1' : '0');
        }
    }
}

int main(int argc,char **argv){
    DemoCtx ctx; DemoState init; DfsConfig cfg; DfsStats st; memset(&ctx,0,sizeof(ctx)); ctx.anchor_mode=0; ctx.order_mode=ORDER_MRV; placements_init(&ctx);
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--trace")==0 && i+1<argc){ctx.trace_on=1; ctx.trace_fp=fopen(argv[++i],"w"); if(!ctx.trace_fp)return 1; fprintf(ctx.trace_fp,"ev\tdepth\tidx\tmask\n");}
        else if(strcmp(argv[i],"--order")==0 && i+1<argc){i++; if(strcmp(argv[i],"index")==0)ctx.order_mode=ORDER_INDEX; else if(strcmp(argv[i],"rare")==0)ctx.order_mode=ORDER_RARE; else if(strcmp(argv[i],"common")==0)ctx.order_mode=ORDER_COMMON; else if(strcmp(argv[i],"mrv")==0)ctx.order_mode=ORDER_MRV; else { fprintf(stderr, "unknown --order value: %s\n", argv[i]); return 1; }}
        else if(strcmp(argv[i],"--anchor")==0){ctx.anchor_mode=1;}
    }
    order_build(&ctx); memset(&init,0,sizeof(init));
    cfg=(DfsConfig){sizeof(DemoState),MAX_DEPTH,0,0,next_state,validity,veracity,on_solution,trace_line,&ctx};
    if (!dfs_run(&cfg,&init,&st)) {
        if (ctx.trace_fp) fclose(ctx.trace_fp);
        fprintf(stderr, "dfs_run failed\n");
        return 1;
    }
    if(ctx.trace_fp) fclose(ctx.trace_fp);
    printf("placements=%d solutions=%zu canonical=%zu nodes=%zu prunes=%zu kept=%zu order=%s anchor=%d full=",
           ctx.n,ctx.solution_count,ctx.canonical_count,st.nodes_visited,
           st.validity_prunes,st.solutions_kept,
           ctx.order_mode==ORDER_RARE?"rare":ctx.order_mode==ORDER_COMMON?"common":ctx.order_mode==ORDER_MRV?"mrv":"index",
           ctx.anchor_mode);
    print_mask_binary(BOARD_FULL);
    printf("\n");
    return 0;
}
