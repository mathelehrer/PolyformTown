#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/attach.h"
#include "core/boundary.h"
#include "core/cycle.h"
#include "core/dfs.h"
#include "core/hash.h"
#include "core/lattice.h"
#include "core/tetrille.h"
#include "core/tile.h"

enum { SAFETY_MAX_DEPTH = 8 };

typedef struct { Poly poly; Coord target; int tile_count; } HatState;
typedef struct { const Tile *tile; HashTable seen; char **out; int out_n; int out_cap; int max_tile_seen; int cap_blocked; } HatCtx;
typedef struct { Poly poly; Coord target; int depth; } CompState;
typedef struct { const Tile *tile; Coord target; HashTable seen; } CompCtx;

static int poly_has_vertex(const Poly *p, Coord q){
    for(int i=0;i<p->cycle_count;i++) for(int j=0;j<p->cycles[i].n;j++) if(coord_eq(p->cycles[i].v[j],q)) return 1;
    return 0;
}
static int edge_has_target(Edge e, Coord t){ return coord_eq(e.a,t)||coord_eq(e.b,t); }

static void coord_transform_lattice(Coord in, int lattice, int t, Coord *out){
    Cycle c; c.n=1; c.v[0]=in; cycle_transform_lattice(&c,&c,lattice,t); *out=c.v[0];
}

static int poly_to_string(const Poly *p, char *buf, size_t cap){
    size_t off=0; int n=snprintf(buf+off,cap-off,"["); if(n<0||(size_t)n>=cap-off) return 0; off+=(size_t)n;
    for(int i=0;i<p->cycle_count;i++){
        n=snprintf(buf+off,cap-off,"%s[",(i?"|":"")); if(n<0||(size_t)n>=cap-off) return 0; off+=(size_t)n;
        for(int j=0;j<p->cycles[i].n;j++){ Coord v=p->cycles[i].v[j];
            n=snprintf(buf+off,cap-off,"%s(%d,%d,%d)",(j?",":""),v.v,v.x,v.y); if(n<0||(size_t)n>=cap-off) return 0; off+=(size_t)n; }
        n=snprintf(buf+off,cap-off,"]"); if(n<0||(size_t)n>=cap-off) return 0; off+=(size_t)n;
    }
    n=snprintf(buf+off,cap-off,"]"); return (n>=0&&(size_t)n<cap-off);
}


static int coord_less(Coord a, Coord b){
    if(a.v!=b.v) return a.v<b.v;
    if(a.x!=b.x) return a.x<b.x;
    return a.y<b.y;
}
static int cycle_abs_area_cmp_desc_local(const Cycle *a,const Cycle *b,int lattice){
    long long aa=cycle_signed_area2(a,lattice), ab=cycle_signed_area2(b,lattice);
    if(aa<0) aa=-aa; if(ab<0) ab=-ab;
    if(aa!=ab) return aa>ab;
    return cycle_less(a,b);
}
static void poly_prepare_local(Poly *p,int lattice){
    int outer=0;
    for(int i=1;i<p->cycle_count;i++) if(cycle_abs_area_cmp_desc_local(&p->cycles[i],&p->cycles[outer],lattice)) outer=i;
    if(outer!=0){ Cycle t=p->cycles[0]; p->cycles[0]=p->cycles[outer]; p->cycles[outer]=t; }
    for(int i=0;i<p->cycle_count;i++){
        long long area=cycle_signed_area2(&p->cycles[i],lattice);
        if(i==0){ if(area<0) cycle_reverse(&p->cycles[i]); }
        else { if(area>0) cycle_reverse(&p->cycles[i]); }
        cycle_canonicalize_shift(&p->cycles[i]);
    }
    for(int i=1;i<p->cycle_count;i++) for(int j=i+1;j<p->cycle_count;j++) if(cycle_less(&p->cycles[j],&p->cycles[i])){ Cycle t=p->cycles[i]; p->cycles[i]=p->cycles[j]; p->cycles[j]=t; }
}
static void poly_translate_to_center(Poly *p, Coord c, int lattice){
    if(lattice==TILE_LATTICE_TETRILLE){
        int m=0,n=0;
        if(tetrille_delta_to_6(c.v, -c.x, -c.y, &m, &n)){
            for(int i=0;i<p->cycle_count;i++) tetrille_translate_cycle(&p->cycles[i], m, n);
        }
    } else {
        poly_translate(p, -c.x, -c.y);
    }
}

static int pair_key(const Poly *p, Coord center, int lattice, char *out, size_t cap){
    Poly centered = *p;
    Poly best = {0};
    Coord c0 = center;
    int first = 1;
    int cnt = lattice_transform_count(lattice);

    poly_translate_to_center(&centered, c0, lattice);

    for(int t=0; t<cnt; t++){
        Poly cur;
        Coord tc;
        poly_transform_lattice(&centered, &cur, lattice, t);
        coord_transform_lattice((Coord){center.v,0,0}, lattice, t, &tc);
        poly_prepare_local(&cur, lattice);
        if(first || poly_less(&cur, &best) || (!poly_less(&best,&cur) && coord_less(tc,(Coord){0,0,0}))){
            best = cur;
            first = 0;
        }
    }

    if(first) return 0;
    return poly_to_string(&best, out, cap);
}

static int add_out(HatCtx *ctx, const Poly *p, Coord c){
    char k[8400]; if(!pair_key(p,c,ctx->tile->lattice,k,sizeof(k))) return 0;
    for(int i=0;i<ctx->out_n;i++) if(strcmp(ctx->out[i],k)==0) return 1;
    if(ctx->out_n==ctx->out_cap){ int nc=ctx->out_cap?ctx->out_cap*2:128; char **nv=realloc(ctx->out,(size_t)nc*sizeof(*ctx->out)); if(!nv) return 0; ctx->out=nv; ctx->out_cap=nc; }
    ctx->out[ctx->out_n]=malloc(strlen(k)+1); if(!ctx->out[ctx->out_n]) return 0; strcpy(ctx->out[ctx->out_n],k); ctx->out_n++; return 1;
}

static int comp_next(const void *st,size_t d,int *cur,void *child,int *cid,void *vctx){
    const CompState *in=st; CompState*out=child; CompCtx*ctx=vctx; Edge edges[4096]; int ec=build_boundary_edges(&in->poly,edges); (void)d;
    while(*cur<ec*ctx->tile->variant_count){ int step=(*cur)++; int be=step/ctx->tile->variant_count; int tv=step%ctx->tile->variant_count; const Cycle *var=&ctx->tile->variants[tv];
        if(!edge_has_target(edges[be],ctx->target)) continue;
        for(int te=0;te<var->n;te++){ Poly grown,key; if(!try_attach_tile_poly(&in->poly,var,ctx->tile->lattice,be,te,&grown)) continue; poly_hash_key_lattice(&grown,ctx->tile->lattice,&key); if(!hash_insert(&ctx->seen,&key)) continue; *out=*in; out->poly=grown; out->depth=in->depth+1; if(cid)*cid=step; return 1; }
    } return 0;
}
static int comp_valid(const void *st,size_t d,void *vctx){ (void)d; (void)vctx; const CompState*s=st; return s->depth<=SAFETY_MAX_DEPTH; }
static int comp_done(const void *st,size_t d,void *vctx){ (void)d; const CompState*s=st; const CompCtx*ctx=vctx; return !poly_has_vertex(&s->poly,ctx->target); }
static int has_completion_dfs(const Poly *p,const Tile *tile,Coord target){
    CompCtx c={tile,target,{0}}; CompState init={*p,target,0}; DfsConfig cfg; DfsStats st;
    hash_init(&c.seen,256); {Poly k; poly_hash_key_lattice(&init.poly,tile->lattice,&k); hash_insert(&c.seen,&k);} 
    cfg=(DfsConfig){sizeof(CompState),SAFETY_MAX_DEPTH,1,1,comp_next,comp_valid,comp_done,NULL,NULL,&c};
    if(!dfs_run(&cfg,&init,&st)){ hash_destroy(&c.seen); return 0; }
    hash_destroy(&c.seen); return st.solutions_kept>0;
}
static int live_boundary_dfs(const Poly *p,const Tile *tile){ Coord v[4096]; int n=build_boundary_vertices(p,v); if(n<0) return 0; for(int i=0;i<n;i++) if(!has_completion_dfs(p,tile,v[i])) return 0; return 1; }

static int next_state(const void *st,size_t d,int *cur,void *child,int *cid,void *vctx){
    const HatState *in=st; HatState*out=child; HatCtx*ctx=vctx; Edge edges[4096]; int ec=build_boundary_edges(&in->poly,edges); (void)d;
    while(*cur<ec*ctx->tile->variant_count){ int step=(*cur)++; int be=step/ctx->tile->variant_count; int tv=step%ctx->tile->variant_count; const Cycle *var=&ctx->tile->variants[tv];
        if(!edge_has_target(edges[be],in->target)) continue;
        for(int te=0;te<var->n;te++){ Poly grown,key; if(!try_attach_tile_poly(&in->poly,var,ctx->tile->lattice,be,te,&grown)) continue; if(!live_boundary_dfs(&grown,ctx->tile)) continue; poly_hash_key_lattice(&grown,ctx->tile->lattice,&key); if(!hash_insert(&ctx->seen,&key)) continue; *out=*in; out->poly=grown; out->tile_count=in->tile_count+1; if(cid)*cid=step; return 1; }
    } return 0;
}
static int valid(const void *st,size_t d,void *vctx){ (void)d; const HatState*s=st; HatCtx*ctx=vctx;
    if(s->tile_count>ctx->max_tile_seen) ctx->max_tile_seen=s->tile_count;
    if(s->tile_count>=SAFETY_MAX_DEPTH && poly_has_vertex(&s->poly,s->target)) ctx->cap_blocked++;
    return s->tile_count<=SAFETY_MAX_DEPTH; }
static int done(const void *st,size_t d,void *vctx){ (void)d; (void)vctx; const HatState*s=st; return !poly_has_vertex(&s->poly,s->target); }
static void on_sol(const void *st,size_t d,void *vctx){ (void)d; const HatState*s=st; HatCtx*ctx=vctx; Poly k; poly_hash_key_lattice(&s->poly,ctx->tile->lattice,&k); add_out(ctx,&k,s->target); }
static int cmp_str(const void *a,const void *b){ const char*const*sa=a; const char*const*sb=b; return strcmp(*sa,*sb); }

int main(int argc,char **argv){
    const char *tile_path="tiles/hat.tile", *out_path=NULL; Tile tile; HatCtx *ctx; DfsStats total={0,0,0,0};
    for(int i=1;i<argc;i++){ if(strcmp(argv[i],"--tile")==0&&i+1<argc) tile_path=argv[++i]; else if(strcmp(argv[i],"--out")==0&&i+1<argc) out_path=argv[++i]; }
    if(!tile_load(tile_path,&tile)) return 1;
    ctx=calloc(1,sizeof(*ctx)); if(!ctx) return 1; ctx->tile=&tile;
    for(int i=0;i<tile.base.n;i++){
        HatState init; DfsConfig cfg; DfsStats st; hash_destroy(&ctx->seen); hash_init(&ctx->seen,1024);
        memset(&init,0,sizeof(init)); init.poly.cycle_count=1; init.poly.cycles[0]=tile.base; init.target=tile.base.v[i]; init.tile_count=1;
        { Poly key; poly_hash_key_lattice(&init.poly,tile.lattice,&key); hash_insert(&ctx->seen,&key); }
        cfg=(DfsConfig){sizeof(HatState),SAFETY_MAX_DEPTH,0,0,next_state,valid,done,on_sol,NULL,ctx};
        if(!dfs_run(&cfg,&init,&st)) return 1;
        total.nodes_visited+=st.nodes_visited; total.validity_prunes+=st.validity_prunes; total.solutions_found+=st.solutions_found; total.solutions_kept+=st.solutions_kept;
    }
    qsort(ctx->out,(size_t)ctx->out_n,sizeof(*ctx->out),cmp_str);
    FILE *fp=stdout; if(out_path) fp=fopen(out_path,"w"); if(!fp) return 1;
    for(int i=0;i<ctx->out_n;i++) fprintf(fp,"%s\n",ctx->out[i]); if(fp!=stdout) fclose(fp);
    printf("tile=%s boundary_vertices=%d centered_boundary=%d nodes=%zu prunes=%zu found=%zu kept=%zu depth_cap=%d max_tile_seen=%d cap_blocked=%d\n",tile_path,tile.base.n,ctx->out_n,total.nodes_visited,total.validity_prunes,total.solutions_found,total.solutions_kept,SAFETY_MAX_DEPTH,ctx->max_tile_seen,ctx->cap_blocked);
    for(int i=0;i<ctx->out_n;i++) free(ctx->out[i]); free(ctx->out); hash_destroy(&ctx->seen); free(ctx); return 0;
}
