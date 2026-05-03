#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int x, y, k; } Cell;
typedef struct { Cell v[4]; } N4;
typedef struct { Cell *a; int n, cap; } CellVec;
typedef struct { char **a; int n, cap; } StrVec;

static const int DX[6] = { 1, 0,-1,-1, 0, 1 };
static const int DY[6] = { 0, 1, 1, 0,-1,-1 };
static const int TARGET[8] = {1,2,4,10,27,85,262,873};

/* Explicit seed hat occupancy supplied by user notes. */
static const Cell HAT8[8] = {
    {0,-1,0}, {0,-1,1}, {0,-1,2}, {0,-1,5},
    {0, 0,4}, {0, 0,5}, {1,-1,2}, {1,-1,3}
};

static const Cell C3_EXAMPLE[3] = {
    {0,  0, 0},
    {1, -1, 2},
    {1,  0, 4},
};

static const Cell C6_EXAMPLE[6] = {
    {0,0,0},{0,0,1},{0,0,2},{0,0,3},{0,0,4},{0,0,5},
};

static int mod6(int k);

static const Cell C4_EXAMPLE[4] = {
    {0, 0, 0},
    {0, 0, 1},
    {1, 0, 3},
    {1, 0, 4},
};


static void make_C6(int x,int y, Cell out[6]){
    for(int k=0;k<6;k++) out[k]=(Cell){x,y,k};
}

static void make_C3(int x,int y,int k, Cell out[3]){
    int ka=mod6(k-1), kb=mod6(k);
    out[0]=(Cell){x,y,mod6(k)};
    out[1]=(Cell){x+DX[ka],y+DY[ka],mod6(k+2)};
    out[2]=(Cell){x+DX[kb],y+DY[kb],mod6(k+4)};
}

static void make_C4(int x,int y,int s, Cell out[4]){
    int ks=mod6(s);
    out[0]=(Cell){x,y,ks};
    out[1]=(Cell){x,y,mod6(ks+1)};
    out[2]=(Cell){x+DX[ks],y+DY[ks],mod6(ks+3)};
    out[3]=(Cell){x+DX[ks],y+DY[ks],mod6(ks+4)};
}

static void die(const char *m){ fprintf(stderr, "%s\n", m); exit(1); }
static int mod6(int k){ k%=6; if(k<0) k+=6; return k; }

static N4 adjacent_kites(Cell c){
    N4 o;
    o.v[0]=(Cell){c.x,c.y,mod6(c.k-1)};
    o.v[1]=(Cell){c.x,c.y,mod6(c.k+1)};
    { int s=mod6(c.k-1); o.v[2]=(Cell){c.x+DX[s],c.y+DY[s],mod6(c.k+2)}; }
    { int s=mod6(c.k);   o.v[3]=(Cell){c.x+DX[s],c.y+DY[s],mod6(c.k+4)}; }
    return o;
}

static int cell_eq(Cell a, Cell b){ return a.x==b.x && a.y==b.y && a.k==b.k; }
static int cell_cmp(const void *pa,const void *pb){
    const Cell *a=(const Cell*)pa,*b=(const Cell*)pb;
    if(a->x!=b->x) return (a->x<b->x)?-1:1;
    if(a->y!=b->y) return (a->y<b->y)?-1:1;
    if(a->k!=b->k) return (a->k<b->k)?-1:1;
    return 0;
}
static int contains(const Cell *a,int n,Cell c){ for(int i=0;i<n;i++) if(cell_eq(a[i],c)) return 1; return 0; }

static void cv_push(CellVec *v, Cell c){
    if(v->n==v->cap){ v->cap=v->cap?2*v->cap:64; v->a=(Cell*)realloc(v->a,v->cap*sizeof(Cell)); if(!v->a) die("oom"); }
    v->a[v->n++]=c;
}
static void sv_push(StrVec *v, char *s){
    if(v->n==v->cap){ v->cap=v->cap?2*v->cap:64; v->a=(char**)realloc(v->a,v->cap*sizeof(char*)); if(!v->a) die("oom"); }
    v->a[v->n++]=s;
}
static int str_cmp(const void *a,const void *b){
    const char *sa=*(const char*const*)a,*sb=*(const char*const*)b; return strcmp(sa,sb);
}
static const char *live_label(int live_only){ return live_only ? "yes" : "maybe"; }
static char *xstrdup(const char *s){ size_t n=strlen(s)+1; char *p=(char*)malloc(n); if(!p) die("oom"); memcpy(p,s,n); return p; }

static Cell rot60(Cell c){ return (Cell){-c.y,c.x+c.y,mod6(c.k+1)}; }
static Cell reflect(Cell c){ return (Cell){c.x+c.y,-c.y,mod6(1-c.k)}; }
static Cell sym_apply(Cell c,int t){
    if(t>=6){ c=reflect(c); t-=6; }
    for(int i=0;i<t;i++) c=rot60(c);
    return c;
}

static char *canonical_key(const Cell *in,int n){
    char *best=NULL;
    for(int t=0;t<12;t++){
        Cell *tmp=(Cell*)malloc((size_t)n*sizeof(Cell)); if(!tmp) die("oom");
        for(int i=0;i<n;i++) tmp[i]=sym_apply(in[i],t);
        int minx=tmp[0].x,miny=tmp[0].y;
        for(int i=1;i<n;i++){ if(tmp[i].x<minx) minx=tmp[i].x; if(tmp[i].y<miny) miny=tmp[i].y; }
        for(int i=0;i<n;i++){ tmp[i].x-=minx; tmp[i].y-=miny; }
        qsort(tmp,n,sizeof(Cell),cell_cmp);
        size_t cap=(size_t)n*20+16; char *s=(char*)malloc(cap); if(!s) die("oom"); s[0]='\0';
        for(int i=0;i<n;i++){ char b[64]; snprintf(b,sizeof(b),"%d,%d,%d;",tmp[i].x,tmp[i].y,tmp[i].k); strncat(s,b,cap-strlen(s)-1); }
        free(tmp);
        if(!best || strcmp(s,best)<0){ free(best); best=s; } else free(s);
    }
    return best;
}


static char *translation_key(const Cell *in,int n){
    Cell *tmp=(Cell*)malloc((size_t)n*sizeof(Cell)); if(!tmp) die("oom");
    for(int i=0;i<n;i++) tmp[i]=in[i];
    int minx=tmp[0].x,miny=tmp[0].y;
    for(int i=1;i<n;i++){ if(tmp[i].x<minx) minx=tmp[i].x; if(tmp[i].y<miny) miny=tmp[i].y; }
    for(int i=0;i<n;i++){ tmp[i].x-=minx; tmp[i].y-=miny; }
    qsort(tmp,n,sizeof(Cell),cell_cmp);
    size_t cap=(size_t)n*20+16; char *k=(char*)malloc(cap); if(!k) die("oom"); k[0]='\0';
    for(int i=0;i<n;i++){ char b[64]; snprintf(b,sizeof(b),"%d,%d,%d;",tmp[i].x,tmp[i].y,tmp[i].k); strncat(k,b,cap-strlen(k)-1); }
    free(tmp);
    return k;
}
static int parse_key(const char *s, CellVec *out){
    out->a=NULL; out->n=out->cap=0;
    const char *p=s;
    while(*p){ Cell c; int used=0; if(sscanf(p,"%d,%d,%d;%n",&c.x,&c.y,&c.k,&used)!=3||used<=0) return 0; cv_push(out,c); p+=used; }
    return 1;
}

static int connected(const Cell *a,int n){
    int *vis=(int*)calloc((size_t)n,sizeof(int)); if(!vis) die("oom");
    int *q=(int*)malloc((size_t)n*sizeof(int)); if(!q) die("oom");
    int h=0,t=0; vis[0]=1; q[t++]=0;
    while(h<t){
        int i=q[h++]; N4 nb=adjacent_kites(a[i]);
        for(int z=0;z<4;z++) for(int j=0;j<n;j++) if(!vis[j]&&cell_eq(a[j],nb.v[z])){ vis[j]=1; q[t++]=j; }
    }
    for(int i=0;i<n;i++) if(!vis[i]){ free(vis); free(q); return 0; }
    free(vis); free(q); return 1;
}

static int hole_free(const Cell *a,int n){
    int minx=a[0].x,maxx=a[0].x,miny=a[0].y,maxy=a[0].y;
    for(int i=1;i<n;i++){ if(a[i].x<minx)minx=a[i].x; if(a[i].x>maxx)maxx=a[i].x; if(a[i].y<miny)miny=a[i].y; if(a[i].y>maxy)maxy=a[i].y; }
    minx-=2; maxx+=2; miny-=2; maxy+=2;
    int W=maxx-minx+1,H=maxy-miny+1,N=W*H*6;
    unsigned char *blk=(unsigned char*)calloc((size_t)N,1),*vis=(unsigned char*)calloc((size_t)N,1);
    if(!blk||!vis) die("oom");
    for(int i=0;i<n;i++){ int ix=(a[i].x-minx),iy=(a[i].y-miny),id=(iy*W+ix)*6+a[i].k; blk[id]=1; }
    int *q=(int*)malloc((size_t)N*sizeof(int)); if(!q) die("oom"); int h=0,t=0;
    for(int yy=0;yy<H;yy++) for(int xx=0;xx<W;xx++) for(int k=0;k<6;k++){
        if(xx>0&&xx<W-1&&yy>0&&yy<H-1) continue;
        int id=(yy*W+xx)*6+k; if(!blk[id]&&!vis[id]){ vis[id]=1; q[t++]=id; }
    }
    while(h<t){
        int id=q[h++],k=id%6,r=id/6,xx=r%W,yy=r/W; Cell c={xx+minx,yy+miny,k}; N4 nb=adjacent_kites(c);
        for(int z=0;z<4;z++){
            int nx=nb.v[z].x-minx,ny=nb.v[z].y-miny,nk=nb.v[z].k;
            if(nx<0||nx>=W||ny<0||ny>=H) continue;
            int nid=(ny*W+nx)*6+nk; if(!blk[nid]&&!vis[nid]){ vis[nid]=1; q[t++]=nid; }
        }
    }
    int ok=1;
    for(int yy=0;yy<H;yy++) for(int xx=0;xx<W;xx++) for(int k=0;k<6;k++){
        int id=(yy*W+xx)*6+k; if(!blk[id]&&!vis[id]) ok=0;
    }
    free(blk); free(vis); free(q); return ok;
}

static void test_adjacency(void){
    for(int x=-2;x<=2;x++)for(int y=-2;y<=2;y++)for(int k=0;k<6;k++){
        Cell c={x,y,k}; N4 nb=adjacent_kites(c);
        for(int i=0;i<4;i++) if(cell_eq(c,nb.v[i])) die("adj self neighbor");
        for(int i=0;i<4;i++) for(int j=i+1;j<4;j++) if(cell_eq(nb.v[i],nb.v[j])) die("adj dup neighbor");
        int same=0,cross=0;
        for(int i=0;i<4;i++){
            if(nb.v[i].x==x&&nb.v[i].y==y){ same++; if(!(nb.v[i].k==mod6(k-1)||nb.v[i].k==mod6(k+1))) die("same-hex wrong"); }
            else cross++;
            N4 back=adjacent_kites(nb.v[i]); int ok=0; for(int j=0;j<4;j++) if(cell_eq(back.v[j],c)) ok=1; if(!ok) die("adj not symmetric");
        }
        if(same!=2||cross!=2) die("adj wrong degree");
    }
}


static int is_neighbor(Cell a, Cell b){
    N4 nb=adjacent_kites(a);
    for(int i=0;i<4;i++) if(cell_eq(nb.v[i],b)) return 1;
    return 0;
}

static void test_symmetry_adjacency(void){
    for(int t=0;t<12;t++){
        for(int x=-2;x<=2;x++) for(int y=-2;y<=2;y++) for(int k=0;k<6;k++){
            Cell a={x,y,k};
            N4 nb=adjacent_kites(a);
            Cell sa=sym_apply(a,t);
            for(int i=0;i<4;i++){
                Cell sb=sym_apply(nb.v[i],t);
                if(!is_neighbor(sa,sb)) die("symmetry does not preserve adjacency");
            }
        }
    }
}

static void test_vertex_fixtures(void){
    char *c3k = canonical_key(C3_EXAMPLE,3);
    char *c4k = canonical_key(C4_EXAMPLE,4);
    if(!c3k||!c4k) die("fixture canonicalization failed");

    for(int t=0;t<12;t++){
        Cell c3[3], c4[4];
        for(int i=0;i<3;i++) c3[i]=sym_apply(C3_EXAMPLE[i],t);
        for(int i=0;i<4;i++) c4[i]=sym_apply(C4_EXAMPLE[i],t);
        {
            char *k=canonical_key(c3,3);
            if(strcmp(k,c3k)!=0) die("C3 fixture not symmetry-stable");
            free(k);
        }
        {
            char *k=canonical_key(c4,4);
            if(strcmp(k,c4k)!=0) die("C4 fixture not symmetry-stable");
            free(k);
        }
    }

    /* C3: all pairs share one C3 vertex, but not all pairs are same-hex. */
    {
        int pair_ok=1, same_hex_pairs=0;
        for(int i=0;i<3;i++) for(int j=i+1;j<3;j++){
            int adjacent=0;
            N4 nb=adjacent_kites(C3_EXAMPLE[i]);
            for(int z=0;z<4;z++) if(cell_eq(nb.v[z],C3_EXAMPLE[j])) adjacent=1;
            if(!adjacent) pair_ok=0;
            if(C3_EXAMPLE[i].x==C3_EXAMPLE[j].x &&
               C3_EXAMPLE[i].y==C3_EXAMPLE[j].y) same_hex_pairs++;
        }
        if(!pair_ok) die("C3 fixture pair adjacency failed");
        if(same_hex_pairs==3) die("C3 fixture wrongly all same-hex");
    }

    /* C4: exact side-0 local incidence set. */
    {
        static const Cell want[4] = {
            {0,0,0},{0,0,1},{1,0,3},{1,0,4}
        };
        for(int i=0;i<4;i++) if(!contains(C4_EXAMPLE,4,want[i]))
            die("C4 fixture mismatch vs side-0 incidence");
    }

    {
        Cell got3[3],got4[4],got6[6];
        make_C3(0,0,0,got3);
        make_C4(0,0,0,got4);
        make_C6(0,0,got6);
        for(int i=0;i<3;i++) if(!contains(got3,3,C3_EXAMPLE[i]))
            die("make_C3 disagrees with C3 fixture");
        for(int i=0;i<4;i++) if(!contains(got4,4,C4_EXAMPLE[i]))
            die("make_C4 disagrees with C4 fixture");
        for(int i=0;i<6;i++) if(!contains(got6,6,C6_EXAMPLE[i]))
            die("make_C6 disagrees with C6 fixture");
    }

    free(c3k);
    free(c4k);
}

static int unique_orients(const Cell *seed,int n, StrVec *out){
    for(int t=0;t<12;t++){
        Cell *tmp=(Cell*)malloc((size_t)n*sizeof(Cell)); if(!tmp) die("oom");
        for(int i=0;i<n;i++) tmp[i]=sym_apply(seed[i],t);
        char *k=translation_key(tmp,n); free(tmp);
        int dup=0; for(int i=0;i<out->n;i++) if(strcmp(out->a[i],k)==0){ dup=1; break; }
        if(!dup) sv_push(out,k); else free(k);
    }
    qsort(out->a,out->n,sizeof(char*),str_cmp);
    return out->n;
}



static void build_seed_orients(const Cell *seed,int n,StrVec *out){
    unique_orients(seed,n,out);
}


static char *exact_key(const Cell *a,int n);

static int placement_fits_no_overlap(const Cell *agg,int an,
                                     const Cell *hat,int hn,
                                     int tx,int ty){
    for(int i=0;i<hn;i++){
        Cell p={hat[i].x+tx,hat[i].y+ty,hat[i].k};
        if(contains(agg,an,p)) return 0;
    }
    return 1;
}

typedef struct { Cell cells[8]; int mask; } CandPlacement;
typedef struct { CandPlacement *a; int n, cap; } CandVec;
static void cand_push(CandVec *v, CandPlacement c){
    if(v->n==v->cap){ v->cap=v->cap?2*v->cap:64; v->a=(CandPlacement*)realloc(v->a,v->cap*sizeof(CandPlacement)); if(!v->a) die("oom"); }
    v->a[v->n++]=c;
}

static int local_cover_dfs(const CandVec *cand,int full,int mask,CellVec *added){
    if(mask==full) return 1;
    int bit=0;
    while(bit<30 && (mask&(1<<bit))) bit++;
    for(int i=0;i<cand->n;i++){
        const CandPlacement *cp=&cand->a[i];
        if(!(cp->mask & (1<<bit))) continue;
        if((cp->mask & ~mask)==0) continue;
        int ov=0;
        for(int j=0;j<8;j++) if(contains(added->a,added->n,cp->cells[j])){ ov=1; break; }
        if(ov) continue;
        int oldn=added->n;
        for(int j=0;j<8;j++) cv_push(added,cp->cells[j]);
        if(local_cover_dfs(cand,full,mask|cp->mask,added)) return 1;
        added->n=oldn;
    }
    return 0;
}

static int vertex_completion_possible(const Cell *agg,int an,const Cell *inc,int in,
                                      const StrVec *orients){
    Cell missing[6]; int mn=0;
    for(int i=0;i<in;i++) if(!contains(agg,an,inc[i])) missing[mn++]=inc[i];
    if(mn==0) return 1;
    int full=(1<<mn)-1;
    CandVec cand={0}; StrVec seen={0}; int cover=0;
    for(int mi=0;mi<mn;mi++){
        Cell e=missing[mi];
        for(int oi=0;oi<orients->n;oi++){
            CellVec hs={0}; if(!parse_key(orients->a[oi],&hs)) die("parse fail");
            for(int hj=0;hj<hs.n;hj++){
                if(hs.a[hj].k!=e.k) continue;
                int tx=e.x-hs.a[hj].x, ty=e.y-hs.a[hj].y;
                if(!placement_fits_no_overlap(agg,an,hs.a,hs.n,tx,ty)) continue;
                CandPlacement cp; cp.mask=0;
                for(int z=0;z<hs.n;z++){
                    cp.cells[z]=(Cell){hs.a[z].x+tx,hs.a[z].y+ty,hs.a[z].k};
                    for(int m=0;m<mn;m++) if(cell_eq(cp.cells[z],missing[m])) cp.mask|=(1<<m);
                }
                if(cp.mask==0) continue;
                char *ek=exact_key(cp.cells,8);
                int dup=0; for(int q=0;q<seen.n;q++) if(strcmp(seen.a[q],ek)==0){ dup=1; break; }
                if(dup){ free(ek); continue; }
                sv_push(&seen,ek);
                cover|=cp.mask;
                cand_push(&cand,cp);
            }
            free(hs.a);
        }
    }
    int ok=0;
    if(cover==full){ CellVec added={0}; ok=local_cover_dfs(&cand,full,0,&added); free(added.a); }
    for(int i=0;i<seen.n;i++) free(seen.a[i]);
    free(seen.a);
    free(cand.a);
    return ok;
}

typedef struct { Cell inc[6]; int n; char *key; } VertexRec;
typedef struct { VertexRec *a; int n, cap; } VertexVec;
static void vv_push(VertexVec *v, VertexRec r){
    if(v->n==v->cap){ v->cap=v->cap?2*v->cap:64; v->a=(VertexRec*)realloc(v->a,v->cap*sizeof(VertexRec)); if(!v->a) die("oom"); }
    v->a[v->n++]=r;
}
static char *incident_key(const Cell *inc,int n){ return exact_key(inc,n); }
static void add_boundary_vertex(VertexVec *vv,const Cell *agg,int an,const Cell *inc,int n){
    int occ=0;
    for(int i=0;i<n;i++) if(contains(agg,an,inc[i])) occ++;
    if(occ==0 || occ==n) return;
    char *k=incident_key(inc,n);
    for(int i=0;i<vv->n;i++) if(strcmp(vv->a[i].key,k)==0){ free(k); return; }
    VertexRec r; r.n=n; r.key=k; for(int i=0;i<n;i++) r.inc[i]=inc[i]; vv_push(vv,r);
}
static void collect_boundary_vertices(const Cell *agg,int an,VertexVec *vv){
    for(int i=0;i<an;i++){
        Cell c=agg[i]; Cell c6[6],c3[3],c4a[4],c4b[4];
        make_C6(c.x,c.y,c6); add_boundary_vertex(vv,agg,an,c6,6);
        make_C3(c.x,c.y,c.k,c3); add_boundary_vertex(vv,agg,an,c3,3);
        make_C4(c.x,c.y,mod6(c.k-1),c4a); add_boundary_vertex(vv,agg,an,c4a,4);
        make_C4(c.x,c.y,c.k,c4b); add_boundary_vertex(vv,agg,an,c4b,4);
    }
}
static void free_vertex_vec(VertexVec *vv){
    for(int i=0;i<vv->n;i++) free(vv->a[i].key);
    free(vv->a); vv->a=NULL; vv->n=vv->cap=0;
}

static int is_live_boundary(const Cell *agg,int an,const StrVec *orients){
    VertexVec vv={0}; collect_boundary_vertices(agg,an,&vv);
    for(int i=0;i<vv.n;i++){
        if(!vertex_completion_possible(agg,an,vv.a[i].inc,vv.a[i].n,orients)){
            free_vertex_vec(&vv); return 0;
        }
    }
    free_vertex_vec(&vv); return 1;
}

static int enum_hat_n(int n_hats,int live_only){
    StrVec cur={0};
    char *k0=canonical_key(HAT8,8); sv_push(&cur,k0);
    StrVec or={0}; unique_orients(HAT8,8,&or);
    for(int step=2; step<=n_hats; step++){
        StrVec nxt={0};
        for(int si=0;si<cur.n;si++){
            CellVec agg={0}; if(!parse_key(cur.a[si],&agg)) die("parse fail");
            CellVec frontier={0};
            for(int i=0;i<agg.n;i++){ N4 nb=adjacent_kites(agg.a[i]); for(int z=0;z<4;z++) if(!contains(agg.a,agg.n,nb.v[z])&&!contains(frontier.a,frontier.n,nb.v[z])) cv_push(&frontier,nb.v[z]); }
            for(int fi=0;fi<frontier.n;fi++){
                Cell e=frontier.a[fi];
                for(int oi=0;oi<or.n;oi++){
                    CellVec hs={0}; parse_key(or.a[oi],&hs);
                    for(int hj=0;hj<hs.n;hj++){
                        if(hs.a[hj].k!=e.k) continue;
                        int tx=e.x-hs.a[hj].x, ty=e.y-hs.a[hj].y;
                        CellVec uni={0};
                        for(int i=0;i<agg.n;i++) cv_push(&uni,agg.a[i]);
                        int ov=0;
                        for(int i=0;i<hs.n;i++){
                            Cell p={hs.a[i].x+tx,hs.a[i].y+ty,hs.a[i].k};
                            if(contains(uni.a,uni.n,p)){ ov=1; break; }
                            cv_push(&uni,p);
                        }
                        if(!ov && connected(uni.a,uni.n) &&
                           (!live_only || is_live_boundary(uni.a,uni.n,&or))){
                            char *kk=canonical_key(uni.a,uni.n);
                            int dup=0; for(int z=0;z<nxt.n;z++) if(strcmp(nxt.a[z],kk)==0){ dup=1; break; }
                            if(!dup) sv_push(&nxt,kk); else free(kk);
                        }
                        free(uni.a);
                    }
                    free(hs.a);
                }
            }
            free(frontier.a); free(agg.a);
        }
        for(int i=0;i<cur.n;i++) free(cur.a[i]);
        free(cur.a);
        cur=nxt;
    }
    int ans=cur.n;
    for(int i=0;i<cur.n;i++) free(cur.a[i]);
        free(cur.a);
    for(int i=0;i<or.n;i++) free(or.a[i]);
    free(or.a);
    return ans;
}

static void run_polykites(void){
    StrVec level={0}; sv_push(&level, canonical_key((Cell[]){{0,0,0}},1));
    StrVec kite_or={0}; build_seed_orients((Cell[]){{0,0,0}},1,&kite_or);
    printf("%-12s %4s %8s %8s %6s\n", "kind", "n", "got", "want", "pass");
    printf("%-12s %4d %8d %8d %6s\n", "polykite", 1, 1, 1, "yes");
    for(int n=2;n<=8;n++){
        StrVec nxt={0};
        for(int i=0;i<level.n;i++){
            CellVec sh={0}; parse_key(level.a[i],&sh);
            CellVec fr={0};
            for(int j=0;j<sh.n;j++){ N4 nb=adjacent_kites(sh.a[j]); for(int z=0;z<4;z++) if(!contains(sh.a,sh.n,nb.v[z])&&!contains(fr.a,fr.n,nb.v[z])) cv_push(&fr,nb.v[z]); }
            for(int f=0;f<fr.n;f++){
                CellVec ns={0}; for(int t=0;t<sh.n;t++) cv_push(&ns,sh.a[t]); cv_push(&ns,fr.a[f]);
                if(connected(ns.a,ns.n)){
                    char *k=canonical_key(ns.a,ns.n);
                    int dup=0; for(int q=0;q<nxt.n;q++) if(strcmp(nxt.a[q],k)==0){ dup=1; break; }
                    if(!dup) sv_push(&nxt,k); else free(k);
                }
                free(ns.a);
            }
            free(fr.a); free(sh.a);
        }
        int got=nxt.n,want=TARGET[n-1];
        printf("%-12s %4d %8d %8d %6s\n", "polykite", n, got, want, got==want ? "yes" : "no");
        if(got!=want) die("polykite count mismatch");
        for(int i=0;i<level.n;i++) free(level.a[i]);
        free(level.a);
        level=nxt;
    }
    for(int i=0;i<level.n;i++) free(level.a[i]);
    free(level.a);
    for(int i=0;i<kite_or.n;i++) free(kite_or.a[i]);
    free(kite_or.a);
}


typedef struct { Cell *cells; int n; int cover_mask; } Placement;
typedef struct { Placement *a; int n, cap; } PlaceVec;

static int exact_key_cellcmp(const void *pa,const void *pb){
    return cell_cmp(pa,pb);
}

static char *exact_key(const Cell *a,int n){
    Cell *tmp=(Cell*)malloc((size_t)n*sizeof(Cell)); if(!tmp) die("oom");
    for(int i=0;i<n;i++) tmp[i]=a[i];
    qsort(tmp,n,sizeof(Cell),exact_key_cellcmp);
    size_t cap=(size_t)n*24+16; char *k=(char*)malloc(cap); if(!k) die("oom"); k[0]='\0';
    for(int i=0;i<n;i++){ char b[64]; snprintf(b,sizeof(b),"%d,%d,%d;",tmp[i].x,tmp[i].y,tmp[i].k); strncat(k,b,cap-strlen(k)-1); }
    free(tmp);
    return k;
}

static void pv_push(PlaceVec *v, Placement p){
    if(v->n==v->cap){ v->cap=v->cap?2*v->cap:64; v->a=(Placement*)realloc(v->a,v->cap*sizeof(Placement)); if(!v->a) die("oom"); }
    v->a[v->n++]=p;
}

static int target_index(const Cell *target,int tn, Cell c){
    for(int i=0;i<tn;i++) if(cell_eq(target[i],c)) return i;
    return -1;
}

static void build_cover_placements(const Cell *target,int tn, PlaceVec *out){
    StrVec or={0}; unique_orients(HAT8,8,&or);
    StrVec seen={0};
    for(int oi=0;oi<or.n;oi++){
        CellVec hs={0}; parse_key(or.a[oi],&hs);
        for(int ti=0;ti<tn;ti++){
            Cell t=target[ti];
            for(int hj=0;hj<hs.n;hj++){
                if(hs.a[hj].k!=t.k) continue;
                int tx=t.x-hs.a[hj].x, ty=t.y-hs.a[hj].y;
                Cell cand[8];
                for(int i=0;i<8;i++) cand[i]=(Cell){hs.a[i].x+tx,hs.a[i].y+ty,hs.a[i].k};
                int mask=0;
                for(int i=0;i<8;i++){ int idx=target_index(target,tn,cand[i]); if(idx>=0) mask|=(1<<idx); }
                if(mask==0) continue;
                char *k=exact_key(cand,8);
                int dup=0; for(int z=0;z<seen.n;z++) if(strcmp(seen.a[z],k)==0){ dup=1; break; }
                if(dup){ free(k); continue; }
                sv_push(&seen,k);
                Placement p; p.n=8; p.cover_mask=mask; p.cells=(Cell*)malloc(8*sizeof(Cell)); if(!p.cells) die("oom");
                for(int i=0;i<8;i++) p.cells[i]=cand[i];
                pv_push(out,p);
            }
        }
        free(hs.a);
    }
    for(int i=0;i<or.n;i++) free(or.a[i]);
    free(or.a);
    for(int i=0;i<seen.n;i++) free(seen.a[i]);
    free(seen.a);
}

static int placement_overlap(const Cell *agg,int an, const Placement *p){
    for(int i=0;i<p->n;i++) if(contains(agg,an,p->cells[i])) return 1;
    return 0;
}

static void dfs_covers(const PlaceVec *pv,int full, CellVec *agg,int mask,
                       StrVec *done,int live_only,const StrVec *orients,
                       int hats_used,int exclude_one_hat){
    if(mask==full){
        if(exclude_one_hat && hats_used==1) return;
        if(live_only && !is_live_boundary(agg->a,agg->n,orients)) return;
        char *k=canonical_key(agg->a,agg->n);
        int dup=0; for(int i=0;i<done->n;i++) if(strcmp(done->a[i],k)==0){ dup=1; break; }
        if(!dup) sv_push(done,k); else free(k);
        return;
    }
    for(int i=0;i<pv->n;i++){
        const Placement *p=&pv->a[i];
        if((p->cover_mask & (~mask))==0) continue;
        if(placement_overlap(agg->a,agg->n,p)) continue;
        int oldn=agg->n;
        for(int j=0;j<p->n;j++) cv_push(agg,p->cells[j]);
        dfs_covers(pv,full,agg,mask|p->cover_mask,done,live_only,orients,hats_used+1,exclude_one_hat);
        agg->n=oldn;
    }
}


static int popcount_u32(unsigned x){
    int n=0; while(x){ n+=(x&1u); x>>=1u; } return n;
}

static void print_target(const char *name,const Cell *t,int tn){
    (void)name; (void)t; (void)tn;
}

static void audit_masks(const char *name,const Cell *target,int tn,const PlaceVec *pv){
    int full=(1<<tn)-1;
    int or_mask=0;
    for(int i=0;i<pv->n;i++){
        const Placement *p=&pv->a[i];
        int mask=0, inter=0;
        for(int c=0;c<p->n;c++){
            int idx=target_index(target,tn,p->cells[c]);
            if(idx>=0){
                inter++;
                mask|=(1<<idx);
            }
        }
        if(mask!=p->cover_mask) die("mask mismatch vs exact intersection");
        if(popcount_u32((unsigned)mask)!=inter) die("mask popcount mismatch");
        if((mask==0)!=(inter==0)) die("mask zero/intersection mismatch");
        for(int b=0;b<tn;b++){
            int present=0;
            for(int c=0;c<p->n;c++) if(cell_eq(p->cells[c],target[b])) present=1;
            if((((mask>>b)&1)!=0) != present) die("mask bit decode mismatch");
        }
        or_mask |= mask;
    }
    if(or_mask!=full) die("placement masks do not span full target mask");
    (void)name; (void)full;
}

static int count_complete_covers(const char *name,const Cell *target,int tn,
                                 int live_only,const StrVec *orients,int exclude_one_hat){
    PlaceVec pv={0};
    build_cover_placements(target,tn,&pv);
    print_target(name,target,tn);
    audit_masks(name,target,tn,&pv);
    StrVec done={0}; CellVec agg={0};
    int full=(1<<tn)-1;
    dfs_covers(&pv,full,&agg,0,&done,live_only,orients,0,exclude_one_hat);
    int ans=done.n;
    for(int i=0;i<done.n;i++) free(done.a[i]);
    free(done.a);
    free(agg.a);
    for(int i=0;i<pv.n;i++) free(pv.a[i].cells);
    free(pv.a);
    return ans;
}


typedef struct { char name[4]; Cell *cells; int n; int hats; char *occ_key; char *bkey; } CoverRec;
typedef struct { CoverRec *a; int n, cap; } CoverRecVec;
static void crv_push(CoverRecVec *v,CoverRec r){
    if(v->n==v->cap){ v->cap=v->cap?2*v->cap:64; v->a=(CoverRec*)realloc(v->a,v->cap*sizeof(CoverRec)); if(!v->a) die("oom"); }
    v->a[v->n++]=r;
}
static void free_cover_rec_vec(CoverRecVec *v){
    for(int i=0;i<v->n;i++){ free(v->a[i].cells); free(v->a[i].occ_key); free(v->a[i].bkey); }
    free(v->a); v->a=NULL; v->n=v->cap=0;
}
static void append_text(char **buf,size_t *cap,size_t *len,const char *s){
    size_t sl=strlen(s);
    if(*len+sl+1>*cap){ while(*len+sl+1>*cap) *cap=*cap?2*(*cap):256; *buf=(char*)realloc(*buf,*cap); if(!*buf) die("oom"); }
    memcpy(*buf+*len,s,sl+1); *len+=sl;
}
static void append_cell_list(char **buf,size_t *cap,size_t *len,const Cell *cells,int n){
    for(int i=0;i<n;i++){ char b[64]; snprintf(b,sizeof(b),"%d,%d,%d;",cells[i].x,cells[i].y,cells[i].k); append_text(buf,cap,len,b); }
}

static char *boundary_vertex_key(const Cell *agg,int an){
    VertexVec vv={0}; collect_boundary_vertices(agg,an,&vv);
    if(vv.n==0){ free_vertex_vec(&vv); return xstrdup(""); }
    char *best=NULL;
    for(int t=0;t<12;t++){
        CellVec all={0};
        for(int vi=0;vi<vv.n;vi++) for(int j=0;j<vv.a[vi].n;j++) cv_push(&all,sym_apply(vv.a[vi].inc[j],t));
        int minx=all.a[0].x,miny=all.a[0].y;
        for(int i=1;i<all.n;i++){ if(all.a[i].x<minx) minx=all.a[i].x; if(all.a[i].y<miny) miny=all.a[i].y; }
        free(all.a);
        StrVec recs={0};
        for(int vi=0;vi<vv.n;vi++){
            Cell inc[6],occ[6]; int in=vv.a[vi].n,on=0;
            for(int j=0;j<in;j++){
                Cell c=sym_apply(vv.a[vi].inc[j],t); c.x-=minx; c.y-=miny; inc[j]=c;
                if(contains(agg,an,vv.a[vi].inc[j])) occ[on++]=c;
            }
            qsort(inc,in,sizeof(Cell),cell_cmp); qsort(occ,on,sizeof(Cell),cell_cmp);
            size_t cap=0,len=0; char *r=NULL; char b[32];
            snprintf(b,sizeof(b),"V%d:I",in); append_text(&r,&cap,&len,b);
            append_cell_list(&r,&cap,&len,inc,in); append_text(&r,&cap,&len,"O");
            append_cell_list(&r,&cap,&len,occ,on); append_text(&r,&cap,&len,"|");
            sv_push(&recs,r);
        }
        qsort(recs.a,recs.n,sizeof(char*),str_cmp);
        size_t cap=0,len=0; char *key=NULL;
        for(int i=0;i<recs.n;i++){ append_text(&key,&cap,&len,recs.a[i]); free(recs.a[i]); }
        free(recs.a);
        if(!best || strcmp(key,best)<0){ free(best); best=key; } else free(key);
    }
    free_vertex_vec(&vv);
    return best;
}

static void dfs_cover_collect(const char *name,const PlaceVec *pv,int full,CellVec *agg,
                              int *masks,int nm,int mask,StrVec *seen,CoverRecVec *out,
                              int live_only,const StrVec *orients){
    if(mask==full){
        if(!connected(agg->a,agg->n)) return;
        if(live_only && !is_live_boundary(agg->a,agg->n,orients)) return;
        char *ok=canonical_key(agg->a,agg->n);
        for(int i=0;i<seen->n;i++) if(strcmp(seen->a[i],ok)==0){ free(ok); return; }
        sv_push(seen,xstrdup(ok));
        CoverRec r; snprintf(r.name,sizeof(r.name),"%s",name); r.n=agg->n; r.hats=nm; r.occ_key=ok; r.bkey=boundary_vertex_key(agg->a,agg->n);
        r.cells=(Cell*)malloc((size_t)agg->n*sizeof(Cell)); if(!r.cells) die("oom");
        for(int i=0;i<agg->n;i++) r.cells[i]=agg->a[i];
        crv_push(out,r);
        return;
    }
    for(int i=0;i<pv->n;i++){
        const Placement *p=&pv->a[i];
        if((p->cover_mask & (~mask))==0) continue;
        if(placement_overlap(agg->a,agg->n,p)) continue;
        int oldn=agg->n;
        for(int j=0;j<p->n;j++) cv_push(agg,p->cells[j]);
        masks[nm]=p->cover_mask;
        dfs_cover_collect(name,pv,full,agg,masks,nm+1,mask|p->cover_mask,seen,out,live_only,orients);
        agg->n=oldn;
    }
}

static void collect_covers_for_target(const char *name,const Cell *target,int tn,int live_only,
                                      const StrVec *orients,CoverRecVec *out){
    PlaceVec pv={0}; build_cover_placements(target,tn,&pv);
    StrVec seen={0}; CellVec agg={0}; int masks[8]; int full=(1<<tn)-1;
    dfs_cover_collect(name,&pv,full,&agg,masks,0,0,&seen,out,live_only,orients);
    for(int i=0;i<seen.n;i++) free(seen.a[i]);
    free(seen.a);
    free(agg.a);
    for(int i=0;i<pv.n;i++) free(pv.a[i].cells);
    free(pv.a);
}

static void covers_dedupe_summary(int live_only,int exclude_one_hat){
    Cell c3t[3],c4t[4],c6t[6]; make_C3(0,0,0,c3t); make_C4(0,0,0,c4t); make_C6(0,0,c6t);
    StrVec or={0}; unique_orients(HAT8,8,&or);
    CoverRecVec recs={0};
    collect_covers_for_target("C3",c3t,3,live_only,&or,&recs);
    collect_covers_for_target("C4",c4t,4,live_only,&or,&recs);
    collect_covers_for_target("C6",c6t,6,live_only,&or,&recs);
    int c3=0,c4=0,c6=0;
    StrVec bseen={0};
    for(int i=0;i<recs.n;i++){
        CoverRec *r=&recs.a[i];
        if(exclude_one_hat && r->hats==1) continue;
        int dup=0;
        for(int j=0;j<bseen.n;j++){
            if(strcmp(bseen.a[j],r->bkey)==0){ dup=1; break; }
        }
        if(dup) continue;
        sv_push(&bseen,xstrdup(r->bkey));
        if(strcmp(r->name,"C3")==0) c3++;
        else if(strcmp(r->name,"C4")==0) c4++;
        else if(strcmp(r->name,"C6")==0) c6++;
    }
    printf("%-10s %6s %7s %9s %6s %6s %6s %8s\n",
           "kind", "live", "dedupe", "nontriv", "C3", "C4", "C6", "count");
    printf("%-10s %6s %7s %9s %6d %6d %6d %8d\n",
           "covers", live_label(live_only), "yes", exclude_one_hat ? "yes" : "no",
           c3, c4, c6, c3+c4+c6);
    for(int i=0;i<bseen.n;i++) free(bseen.a[i]);
    free(bseen.a);
    free_cover_rec_vec(&recs);
    for(int i=0;i<or.n;i++) free(or.a[i]);
    free(or.a);
}

static void print_usage(void){
    printf("check_rl0: discrete polykite / hat verifier\n\n");
    printf("Usage:\n");
    printf("  ./check_rl0 polykites\n");
    printf("  ./check_rl0 hat 2|3 [--live-only]\n");
    printf("  ./check_rl0 covers [--live-only] [--dedupe] [--include-trivial]\n\n");
    printf("Common checks:\n");
    printf("  polykites                                  published benchmark 1,2,4,10,27,85,262,873\n");
    printf("  hat 2                                      2-hat aggregate count\n");
    printf("  hat 2 --live-only                          2-hat aggregate count after live filter\n");
    printf("  covers --live-only                         C3/C4/C6 live cover summary\n");
    printf("  covers --live-only --dedupe                boundary-deduped live cover summary\n");
}

int main(int argc,char **argv){
    int live_only=0;
    int dedupe=0;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--live-only")==0) live_only=1;
        if(strcmp(argv[i],"--dedupe")==0) dedupe=1;
    }
    test_adjacency();
    test_symmetry_adjacency();
    test_vertex_fixtures();

    if(argc>=2 && strcmp(argv[1],"polykites")==0){ run_polykites(); return 0; }
    if(argc>=2 && strcmp(argv[1],"covers")==0){
        int include_trivial=0;
        for(int i=1;i<argc;i++) if(strcmp(argv[i],"--include-trivial")==0) include_trivial=1;
        if(dedupe){
            covers_dedupe_summary(live_only,!include_trivial);
            return 0;
        }
        Cell c3t[3],c4t[4],c6t[6];
        StrVec or={0}; unique_orients(HAT8,8,&or);
        make_C3(0,0,0,c3t); make_C4(0,0,0,c4t); make_C6(0,0,c6t);
        int c3=count_complete_covers("C3",c3t,3,live_only,&or,!include_trivial);
        int c4=count_complete_covers("C4",c4t,4,live_only,&or,!include_trivial);
        int c6=count_complete_covers("C6",c6t,6,live_only,&or,!include_trivial);
        printf("%-10s %6s %7s %9s %6s %6s %6s %8s\n",
               "kind", "live", "dedupe", "nontriv", "C3", "C4", "C6", "count");
        printf("%-10s %6s %7s %9s %6d %6d %6d %8d\n",
               "covers", live_label(live_only), "no", include_trivial ? "no" : "yes",
               c3, c4, c6, c3+c4+c6);
        for(int i=0;i<or.n;i++) free(or.a[i]);
        free(or.a);
        return 0;
    }
    if(argc>=2 && (strcmp(argv[1],"covers-dedupe")==0 || strcmp(argv[1],"covers-boundary")==0)){
        int include_trivial=0;
        for(int i=1;i<argc;i++) if(strcmp(argv[i],"--include-trivial")==0) include_trivial=1;
        covers_dedupe_summary(live_only,!include_trivial);
        return 0;
    }
    if(argc>=3 && strcmp(argv[1],"hat")==0){
        int n=atoi(argv[2]);
        if(!connected(HAT8,8)) die("seed hat not connected");
        if(!hole_free(HAT8,8)) die("seed hat not simply connected");
        if(n==2 || n==3){
            int count=enum_hat_n(n,live_only);
            printf("%-10s %4s %6s %8s\n", "kind", "n", "live", "count");
            printf("%-10s %4d %6s %8d\n", "hat", n, live_label(live_only), count);
            return 0;
        }
        die("hat arg must be 2 or 3");
    }
    print_usage();
    return 2;
}
