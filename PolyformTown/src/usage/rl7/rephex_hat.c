#define _POSIX_C_SOURCE 200809L
#include "usage/rl7/rephex_hat.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define MAX_OPTS 60
#define DH_HAT_BASE_ROTATION_STEP 8u /* 240 degrees; verified on DH levels 1..3. */
#define MAX_HAT 32
#define IDX_A 13
#define IDX_B 14

typedef struct { double x, y; } Point;
typedef struct { int x, y; } VKey;
typedef struct { VKey key; int cell, slot; } VHit;
typedef struct { int arity; int cell[3], slot[3]; } Constraint;
typedef struct { int a, b, sa, sb; } Edge;
typedef struct { int other, self_side, other_side; } Adj;
typedef struct { int tile, rot, row[6], active; } Option;
typedef struct { char key[4]; int val[3]; } Lift;
typedef struct { int q, r, state, ori; char refined[6]; unsigned mask; int assigned[6]; Option opt[MAX_OPTS]; int nopt; } HCell;
typedef struct { double a,b,c,d,tx,ty; } Transform;
typedef struct { int ix, iy, cell; } Bucket;

static const int DIRS[6][2] = {{-1,0},{0,-1},{1,-1},{1,0},{0,1},{-1,1}};
static const int VX[6] = {1,0,-1,-1,0,1};
static const int VY[6] = {-1,-2,-1,1,2,1};
static const int ORDINARY[10][6] = {
    {0,3,6,8,11,12},{0,3,6,8,10,12},{0,3,6,7,10,12},{0,3,6,7,IDX_A,11},
    {1,IDX_B,5,7,10,12},{1,3,6,8,11,12},{1,3,6,8,10,12},{1,3,6,7,10,12},
    {1,5,6,8,10,12},{1,5,6,7,10,12}
};
static const char *BASE_H = "AFGHCE", *BASE_D0 = "AFGHJC", *BASE_D1 = "AKFHCE";
static int imod(int x, int n) { x %= n; return x < 0 ? x+n : x; }
static int idx_parse(const char *s) { return !strcmp(s,"a") ? IDX_A : !strcmp(s,"b") ? IDX_B : atoi(s); }
static char project_index(int x) { switch (x) { case 0: case 1:return 'A'; case 10: case 11:return 'C'; case 12:return 'E'; case 3: case 5:return 'F'; case 6:return 'G'; case 7: case 8:return 'H'; case IDX_A:return 'J'; case IDX_B:return 'K'; default:return '?'; } }
static int make_dir(const char *path) { if (mkdir(path,0775) && errno != EEXIST) return 0; return 1; }
static int ensure_parent(const char *path) { char tmp[512]; snprintf(tmp,sizeof(tmp),"%s",path); char *p=strrchr(tmp,'/'); if(!p)return 1; *p=0; char parent[512]; snprintf(parent,sizeof(parent),"%s",tmp); p=strrchr(parent,'/'); if(p){*p=0;if(!make_dir(parent))return 0;} return make_dir(tmp); }
static int cmp_pos(const void *aa, const void *bb) { const HCell *a=aa,*b=bb; return a->q!=b->q ? (a->q<b->q?-1:1) : a->r!=b->r ? (a->r<b->r?-1:1):0; }
static int find_pos(const HCell *c, int n, int q, int r) { int lo=0,hi=n-1; while(lo<=hi){int m=(lo+hi)/2; if(c[m].q==q&&c[m].r==r)return m; if(c[m].q<q||(c[m].q==q&&c[m].r<r))lo=m+1;else hi=m-1;} return -1; }
static VKey vertex_key(const HCell *c, int s) { VKey v={2*c->q+c->r+VX[s],3*c->r+VY[s]}; return v; }
static int vkey_cmp(VKey a,VKey b){return a.x!=b.x?(a.x<b.x?-1:1):a.y!=b.y?(a.y<b.y?-1:1):0;}
static int cmp_vhit(const void *aa,const void *bb){const VHit*a=aa,*b=bb;int c=vkey_cmp(a->key,b->key);return c?c:(a->cell-b->cell);}
static Point center(const HCell *c){Point p={sqrt(3.0)*(c->q+0.5*c->r),1.5*c->r};return p;}
static int hit_angle_cmp_ctx(const void *aa,const void *bb,void *ctx){const VHit*a=aa,*b=bb;const HCell*c=ctx;Point pa=center(&c[a->cell]),pb=center(&c[b->cell]); VKey v=a->key; double va=atan2(-(pa.y-v.y/2.0),pa.x-(sqrt(3.0)/2.0)*v.x); double vb=atan2(-(pb.y-v.y/2.0),pb.x-(sqrt(3.0)/2.0)*v.x);return va<vb?-1:va>vb?1:0;}
static void sort_hits_angle(VHit *h,int n,const HCell*c){for(int i=1;i<n;i++){VHit x=h[i];int j=i-1;while(j>=0&&hit_angle_cmp_ctx(&h[j],&x,(void*)c)>0){h[j+1]=h[j];j--;}h[j+1]=x;}}
static const char *base_for_state(int s){return s=='0'?BASE_D0:s=='1'?BASE_D1:(s=='B'||s=='C'||s=='G'?BASE_H:NULL);}
static void set_refined(HCell*c){const char*b=base_for_state(c->state);for(int s=0;s<6;s++)c->refined[s]=b[imod(s+c->ori+1,6)];}
static int read_lifts(const char *path,Lift *out,int *nout,char *err,size_t errsz){FILE*fp=fopen(path,"r");char line[256];*nout=0;if(!fp){snprintf(err,errsz,"missing boot-generated depiction conversion table: %.120s; run make boot or make rl6_print_conversion",path);return 0;}while(fgets(line,sizeof(line),fp)){char key[8],a[8],b[8],c[8];if(line[0]=='#'||line[0]=='\n')continue;if(sscanf(line," %7s = %7s %7s %7s",key,a,b,c)!=4)continue;int vals[3]={idx_parse(a),idx_parse(b),idx_parse(c)};for(int k=0;k<3;k++){Lift*l=&out[(*nout)++];for(int i=0;i<3;i++){l->key[i]=key[(i+k)%3];l->val[i]=vals[(i+k)%3];}l->key[3]=0;}}fclose(fp);return 1;}
static const Lift *find_lift(const Lift*l,int n,const char key[4]){for(int i=0;i<n;i++)if(!strcmp(l[i].key,key))return &l[i];return NULL;}
static int triple_supported(const Lift*l,int n,const int v[3]){for(int i=0;i<n;i++)if(l[i].val[0]==v[0]&&l[i].val[1]==v[1]&&l[i].val[2]==v[2])return 1;return 0;}
static int pair_supported(const Lift*l,int n,int a,int b){for(int i=0;i<n;i++)for(int k=0;k<3;k++){int x=l[i].val[k],y=l[i].val[(k+1)%3];if((x==a&&y==b)||(x==b&&y==a))return 1;}return 0;}
static int build_cells(const MR7Cells*src,HCell**out,int*n,int*false_n){HCell*c=calloc(src->n,sizeof(*c));if(!c)return 0;*n=0;*false_n=0;for(size_t i=0;i<src->n;i++){if(src->cell[i].state=='F'){(*false_n)++;continue;}HCell*x=&c[(*n)++];x->q=src->cell[i].q;x->r=src->cell[i].r;x->state=src->cell[i].state;x->ori=src->cell[i].ori;set_refined(x);}qsort(c,*n,sizeof(*c),cmp_pos);*out=c;return 1;}
static int side_between(const HCell*a,const HCell*b){for(int s=0;s<6;s++){VKey v1=vertex_key(a,s),v2=vertex_key(a,(s+1)%6);for(int t=0;t<6;t++){VKey w1=vertex_key(b,t),w2=vertex_key(b,(t+1)%6);if((!vkey_cmp(v1,w1)&&!vkey_cmp(v2,w2))||(!vkey_cmp(v1,w2)&&!vkey_cmp(v2,w1)))return s;}}return -1;}
static int build_constraints(HCell*c,int n,Constraint**out,int*nout){VHit*h=malloc((size_t)n*6*sizeof(*h));Constraint*cons=malloc((size_t)n*6*sizeof(*cons));if(!h||!cons){free(h);free(cons);return 0;}int nh=0,nc=0;for(int i=0;i<n;i++)for(int s=0;s<6;s++){h[nh].key=vertex_key(&c[i],s);h[nh].cell=i;h[nh].slot=s;nh++;}qsort(h,nh,sizeof(*h),cmp_vhit);for(int i=0;i<nh;){int j=i+1;while(j<nh&&!vkey_cmp(h[i].key,h[j].key))j++;int k=j-i;if(k==2||k==3){VHit tmp[3];for(int z=0;z<k;z++)tmp[z]=h[i+z];sort_hits_angle(tmp,k,c);cons[nc].arity=k;for(int z=0;z<k;z++){cons[nc].cell[z]=tmp[z].cell;cons[nc].slot[z]=tmp[z].slot;}nc++;}i=j;}free(h);*out=cons;*nout=nc;return 1;}
static int build_edges(HCell*c,int n,Edge**out,int*nout,Adj(**adj)[6],int**deg){Edge*e=malloc((size_t)n*3*sizeof(*e));Adj(*a)[6]=calloc((size_t)n,sizeof(*a));int*d=calloc((size_t)n,sizeof(*d));if(!e||!a||!d){free(e);free(a);free(d);return 0;}int ne=0;for(int i=0;i<n;i++)for(int k=0;k<6;k++){int j=find_pos(c,n,c[i].q+DIRS[k][0],c[i].r+DIRS[k][1]);if(j<=i)continue;int si=side_between(&c[i],&c[j]),sj=side_between(&c[j],&c[i]);if(si<0||sj<0){free(e);free(a);free(d);return 0;}e[ne]=(Edge){i,j,si,sj};a[i][d[i]++]=(Adj){j,si,sj};a[j][d[j]++]=(Adj){i,sj,si};ne++;}*out=e;*nout=ne;*adj=a;*deg=d;return 1;}
static void assign_vertices(HCell*c,Constraint*cons,int ncons,const Lift*l,int nl,size_t*unresolved){*unresolved=0;for(int i=0;i<ncons;i++){if(cons[i].arity!=3)continue;char key[4];for(int k=0;k<3;k++)key[k]=c[cons[i].cell[k]].refined[cons[i].slot[k]];key[3]=0;const Lift*found=find_lift(l,nl,key);if(!found){(*unresolved)++;continue;}for(int k=0;k<3;k++){HCell*x=&c[cons[i].cell[k]];int s=cons[i].slot[k],v=found->val[k];if((x->mask&(1u<<s))&&x->assigned[s]!=v){} else{x->mask|=1u<<s;x->assigned[s]=v;}}}}
static void build_domains(HCell*c,int n){for(int i=0;i<n;i++){c[i].nopt=0;for(int t=0;t<10;t++)for(int r=0;r<6;r++){Option o={t,r,{0},1};int ok=1;for(int s=0;s<6;s++){o.row[s]=ORDINARY[t][(s+r)%6];if(project_index(o.row[s])!=c[i].refined[s])ok=0;if((c[i].mask&(1u<<s))&&c[i].assigned[s]!=o.row[s])ok=0;}if(ok&&c[i].nopt<MAX_OPTS)c[i].opt[c[i].nopt++]=o;}}}
static int active_count(HCell*c){int n=0;for(int i=0;i<c->nopt;i++)if(c->opt[i].active)n++;return n;}
static int prune_domains(HCell*c,Constraint*cons,int nc,const Lift*l,int nl){int changed=1;while(changed){changed=0;for(int z=0;z<nc;z++){Constraint*q=&cons[z];unsigned char support[3][MAX_OPTS]={{0}};if(q->arity==2){HCell*a=&c[q->cell[0]],*b=&c[q->cell[1]];for(int i=0;i<a->nopt;i++)if(a->opt[i].active)for(int j=0;j<b->nopt;j++)if(b->opt[j].active){int x=a->opt[i].row[q->slot[0]],y=b->opt[j].row[q->slot[1]];if(pair_supported(l,nl,x,y)){support[0][i]=support[1][j]=1;}}for(int i=0;i<a->nopt;i++)if(a->opt[i].active&&!support[0][i]){a->opt[i].active=0;changed=1;}for(int j=0;j<b->nopt;j++)if(b->opt[j].active&&!support[1][j]){b->opt[j].active=0;changed=1;}}
            else {HCell*a=&c[q->cell[0]],*b=&c[q->cell[1]],*d=&c[q->cell[2]];for(int i=0;i<a->nopt;i++)if(a->opt[i].active)for(int j=0;j<b->nopt;j++)if(b->opt[j].active)for(int k=0;k<d->nopt;k++)if(d->opt[k].active){int v[3]={a->opt[i].row[q->slot[0]],b->opt[j].row[q->slot[1]],d->opt[k].row[q->slot[2]]};if(triple_supported(l,nl,v)){support[0][i]=support[1][j]=support[2][k]=1;}}for(int i=0;i<a->nopt;i++)if(a->opt[i].active&&!support[0][i]){a->opt[i].active=0;changed=1;}for(int i=0;i<b->nopt;i++)if(b->opt[i].active&&!support[1][i]){b->opt[i].active=0;changed=1;}for(int i=0;i<d->nopt;i++)if(d->opt[i].active&&!support[2][i]){d->opt[i].active=0;changed=1;}}
        }}return 1;}
static int forced_edge(HCell*c,int side,int out[2]){int have=0;for(int i=0;i<c->nopt;i++)if(c->opt[i].active){int a=c->opt[i].row[side],b=c->opt[i].row[(side+1)%6];if(!have){out[0]=a;out[1]=b;have=1;}else if(out[0]!=a||out[1]!=b)return 0;}if(!have||out[0]>=IDX_A||out[1]>=IDX_A)return 0;return 1;}
static int read_hat(Point *p,int *n){FILE*fp=fopen("preferences/focus.tile","r");char line[256];int in=0;*n=0;if(!fp)return 0;while(fgets(line,sizeof(line),fp)){if(!strncmp(line,"cycle:",6)){in=1;continue;}if(!in)continue;int v,a,b;if(sscanf(line," %d %d %d",&v,&a,&b)!=3)continue;Point q={0,0};if(v==6){q.x=sqrt(3.0)/2*(a+b);q.y=(-a+b)/2.0;}else if(v==4){q.x=sqrt(3.0)/4*(a+b);q.y=(-a+b)/4.0;}else if(v==3){q.x=a/sqrt(3.0)+b/(2*sqrt(3.0));q.y=b/2.0;}else{fclose(fp);return 0;}p[(*n)++]=q;}fclose(fp);return *n>0;}
static Point tx_point(Transform t,Point p){Point q={t.a*p.x+t.b*p.y+t.tx,t.c*p.x+t.d*p.y+t.ty};return q;}
static Transform rigid_fit(Point s0,Point s1,Point t0,Point t1){double ang=atan2(t1.y-t0.y,t1.x-t0.x)-atan2(s1.y-s0.y,s1.x-s0.x),co=cos(ang),si=sin(ang);Transform t={co,-si,si,co,0,0};Point r=tx_point(t,s0);t.tx=t0.x-r.x;t.ty=t0.y-r.y;return t;}
static double transform_error(const Point*hat,int nh,Transform a,Transform b){double m=0;for(int i=0;i<nh;i++){Point p=tx_point(a,hat[i]),q=tx_point(b,hat[i]);double e=hypot(p.x-q.x,p.y-q.y);if(e>m)m=e;}return m;}
static int popcount_u(unsigned x){int n=0;while(x){n+=x&1u;x>>=1;}return n;}
static void place_largest(HCell*c,int n,Adj(*adj)[6],int*deg,const Point*hat,int nh,Transform*best,unsigned char*bestplaced,size_t*components,size_t*conflicts){unsigned char*unused=malloc(n),*placed=calloc(n,1);Transform*tr=calloc(n,sizeof(*tr));int*queue=malloc((size_t)n*sizeof(*queue));memset(unused,1,n);*components=0;*conflicts=0;size_t bestn=0;while(1){int seed=-1,score=-1;for(int i=0;i<n;i++)if(unused[i]){int eligible=0;for(int j=0;j<deg[i];j++){int ep[2],eq[2];int q=adj[i][j].other;if(unused[q]&&forced_edge(&c[i],adj[i][j].self_side,ep)&&forced_edge(&c[q],adj[i][j].other_side,eq)){eligible=1;break;}}if(eligible&&popcount_u(c[i].mask)>score){score=popcount_u(c[i].mask);seed=i;}}if(seed<0)break;memset(placed,0,n);int head=0,tail=0;size_t comp_conf=0,count=0;placed[seed]=1;tr[seed]=(Transform){1,0,0,1,0,0};queue[tail++]=seed;while(head<tail){int i=queue[head++];count++;for(int j=0;j<deg[i];j++){Adj a=adj[i][j];int ep[2]={0,0},eq[2]={0,0};if(!forced_edge(&c[i],a.self_side,ep)||!forced_edge(&c[a.other],a.other_side,eq))continue;Point t0=tx_point(tr[i],hat[ep[0]]),t1=tx_point(tr[i],hat[ep[1]]);Transform prop=rigid_fit(hat[eq[1]],hat[eq[0]],t0,t1);if(!placed[a.other]){placed[a.other]=1;tr[a.other]=prop;queue[tail++]=a.other;}else if(transform_error(hat,nh,tr[a.other],prop)>1e-7)comp_conf++;}}
        (*components)++; for(int i=0;i<n;i++)if(placed[i])unused[i]=0; if(count>bestn){bestn=count;*conflicts=comp_conf;memcpy(bestplaced,placed,n);memcpy(best,tr,(size_t)n*sizeof(*tr));}}
    free(unused);free(placed);free(tr);free(queue);}
static double cross(Point a,Point b,Point c){return(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
static int proper_cross(Point a,Point b,Point c,Point d){return cross(a,b,c)*cross(a,b,d)<-1e-9&&cross(c,d,a)*cross(c,d,b)<-1e-9;}
static int point_inside(Point p,const Point*poly,int n){int inside=0;for(int i=0;i<n;i++){Point a=poly[i],b=poly[(i+1)%n];if(fabs(cross(a,b,p))<1e-9&&fmin(a.x,b.x)-1e-9<=p.x&&p.x<=fmax(a.x,b.x)+1e-9&&fmin(a.y,b.y)-1e-9<=p.y&&p.y<=fmax(a.y,b.y)+1e-9)return 0;if((a.y>p.y)!=(b.y>p.y)){double x=(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x;if(p.x<x)inside=!inside;}}return inside;}
static int poly_overlap(const Point*a,const Point*b,int n){for(int i=0;i<n;i++)for(int j=0;j<n;j++)if(proper_cross(a[i],a[(i+1)%n],b[j],b[(j+1)%n]))return 1;return point_inside(a[0],b,n)||point_inside(b[0],a,n);}
static size_t audit_overlap(HCell*c,int n,const Point*hat,int nh,const Transform*tr,const unsigned char*placed){(void)c;Point(*poly)[MAX_HAT]=calloc((size_t)n,sizeof(*poly));double(*bb)[4]=calloc((size_t)n,sizeof(*bb));int*np=malloc((size_t)n*sizeof(*np));int m=0;for(int i=0;i<n;i++)if(placed[i]){np[m++]=i;for(int k=0;k<nh;k++){poly[i][k]=tx_point(tr[i],hat[k]);if(k==0)bb[i][0]=bb[i][2]=poly[i][k].x,bb[i][1]=bb[i][3]=poly[i][k].y;else{bb[i][0]=fmin(bb[i][0],poly[i][k].x);bb[i][1]=fmin(bb[i][1],poly[i][k].y);bb[i][2]=fmax(bb[i][2],poly[i][k].x);bb[i][3]=fmax(bb[i][3],poly[i][k].y);}}}size_t overlaps=0;for(int a=0;a<m&&!overlaps;a++)for(int b=a+1;b<m;b++){int i=np[a],j=np[b];if(bb[i][2]<=bb[j][0]+1e-9||bb[j][2]<=bb[i][0]+1e-9||bb[i][3]<=bb[j][1]+1e-9||bb[j][3]<=bb[i][1]+1e-9)continue;if(poly_overlap(poly[i],poly[j],nh)){overlaps++;break;}}free(poly);free(bb);free(np);return overlaps;}
static const char*fill(char state,int tree){if(tree)return state=='0'?"#f1d777":state=='1'?"#f5af74":state=='B'?"#ad825f":state=='C'?"#d1ae8c":"#79c996";return state=='0'?"#7da9f7":state=='1'?"#ef8d8d":"#79c996";}
static int write_svg(const char*path,HCell*c,int n,const Point*hat,int nh,const Transform*tr,const unsigned char*placed,int tree,unsigned step,char*err,size_t errsz){if(!ensure_parent(path)){snprintf(err,errsz,"cannot create SVG directory");return 0;}double ca=cos(M_PI*(30.0*(step%12))/180.0),sa=sin(M_PI*(30.0*(step%12))/180.0),xmin=0,xmax=0,ymin=0,ymax=0;int first=1;for(int i=0;i<n;i++)if(placed[i])for(int k=0;k<nh;k++){Point p=tx_point(tr[i],hat[k]),r={ca*p.x-sa*p.y,sa*p.x+ca*p.y};if(first){xmin=xmax=r.x;ymin=ymax=r.y;first=0;}else{xmin=fmin(xmin,r.x);xmax=fmax(xmax,r.x);ymin=fmin(ymin,r.y);ymax=fmax(ymax,r.y);}}if(first){xmin=ymin=0;xmax=ymax=1;}double span=fmax(fmax(xmax-xmin,ymax-ymin),1),margin=span*.035,vx=xmin-margin,vy=-(ymax+margin),vw=xmax-xmin+2*margin,vh=ymax-ymin+2*margin,width=1400,height=fmax(260,width*vh/vw);char tmp[512];snprintf(tmp,sizeof(tmp),"%s.new",path);FILE*fp=fopen(tmp,"w");if(!fp){snprintf(err,errsz,"cannot write hat SVG");return 0;}fprintf(fp,"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" viewBox=\"%.6f %.6f %.6f %.6f\">\n",width,height,vx,vy,vw,vh);fprintf(fp,"<rect x=\"%.6f\" y=\"%.6f\" width=\"%.6f\" height=\"%.6f\" fill=\"#ffffff\"/>\n",vx,vy,vw,vh);for(int i=0;i<n;i++)if(placed[i]){fprintf(fp,"<polygon points=\"");for(int k=0;k<nh;k++){Point p=tx_point(tr[i],hat[k]),r={ca*p.x-sa*p.y,sa*p.x+ca*p.y};fprintf(fp,"%.6f,%.6f%s",r.x,-r.y,k==nh-1?"":" ");}fprintf(fp,"\" fill=\"%s\" stroke=\"#302d29\" stroke-width=\"0.020\" fill-rule=\"evenodd\"/>\n",fill(c[i].state,tree));}fputs("</svg>\n",fp);if(fclose(fp)||rename(tmp,path)){remove(tmp);snprintf(err,errsz,"cannot install hat SVG");return 0;}return 1;}
int rephex_hat_render(const MR7Cells*cells,const char*output_svg,const char*output_data,const char*axiom,unsigned level,int tree_palette,unsigned rotation_step,RephexHatStats*stats,char*err,size_t errsz){memset(stats,0,sizeof(*stats));stats->total_cells=cells->n;Lift lifts[64];int nl=0;if(!read_lifts("data/rl6/hat/print_conversion.dat",lifts,&nl,err,errsz))return 0;HCell*c=NULL;int n=0,false_n=0;if(!build_cells(cells,&c,&n,&false_n)){snprintf(err,errsz,"out of memory");return 0;}stats->supported_cells=n;stats->false_cells=false_n;Constraint*cons=NULL;int nc=0;Edge*edges=NULL;int ne=0;Adj(*adj)[6]=NULL;int*deg=NULL;Point hat[MAX_HAT];int nh=0;Transform*tr=NULL;unsigned char*placed=NULL;int ok=0;if(!build_constraints(c,n,&cons,&nc)||!build_edges(c,n,&edges,&ne,&adj,&deg)||!read_hat(hat,&nh)){snprintf(err,errsz,"cannot build hat constraints");goto done;}assign_vertices(c,cons,nc,lifts,nl,&stats->unresolved_figures);build_domains(c,n);prune_domains(c,cons,nc,lifts,nl);for(int i=0;i<n;i++){int m=active_count(&c[i]);if(m==0)stats->empty_domains++;if(m<=60)stats->domain_histogram[m]++;}tr=calloc((size_t)n,sizeof(*tr));placed=calloc((size_t)n,1);if(!tr||!placed){snprintf(err,errsz,"out of memory");goto done;}place_largest(c,n,adj,deg,hat,nh,tr,placed,&stats->components,&stats->cycle_conflicts);for(int i=0;i<n;i++)if(placed[i])stats->placed_hats++;stats->overlaps=audit_overlap(c,n,hat,nh,tr,placed);unsigned effective_rotation_step=(DH_HAT_BASE_ROTATION_STEP+rotation_step)%12u;if(!write_svg(output_svg,c,n,hat,nh,tr,placed,tree_palette,effective_rotation_step,err,errsz))goto done;if(!ensure_parent(output_data)){snprintf(err,errsz,"cannot create hat data directory");goto done;}FILE*fp=fopen(output_data,"w");if(!fp){snprintf(err,errsz,"cannot write hat data");goto done;}const char*status=stats->placed_hats==stats->total_cells?"complete":"partial";fprintf(fp,"rephex --hat-svg audit\naxiom=%s level=%u palette=%s rotation_step=%u base_rotation_step=%u effective_rotation_step=%u status=%s\n",axiom,level,tree_palette?"tree":"ordinary",rotation_step,DH_HAT_BASE_ROTATION_STEP,effective_rotation_step,status);fprintf(fp,"pipeline=RL7 cells -> refined vertex labels -> ordinary indexed vertex lifts -> row pruning -> forced-edge hat placement\n");fprintf(fp,"cells=%zu supported_non_F_cells=%zu false_center_cells=%zu\nplaced_hats=%zu placement_components=%zu\n",stats->total_cells,stats->supported_cells,stats->false_cells,stats->placed_hats,stats->components);fprintf(fp,"unresolved_vertex_figures=%zu empty_row_domains=%zu\n",stats->unresolved_figures,stats->empty_domains);
    fputs("row_domain_histogram={", fp); int first_hist = 1;
    for (int i = 0; i <= 60; i++) if (stats->domain_histogram[i]) {
        fprintf(fp, "%s%d: %zu", first_hist ? "" : ", ", i, stats->domain_histogram[i]); first_hist = 0;
    }
    fputs("}\n", fp);
    fprintf(fp,"placement_cycle_conflicts=%zu positive_area_overlaps=%zu\n",stats->cycle_conflicts,stats->overlaps);
    HCell *omitted = calloc(cells->n, sizeof(*omitted)); size_t nomit = 0;
    if (omitted) {
        for (size_t i = 0; i < cells->n; i++) {
            int at = find_pos(c, n, cells->cell[i].q, cells->cell[i].r);
            if (cells->cell[i].state == 'F' || at < 0 || !placed[at]) {
                omitted[nomit].q = cells->cell[i].q; omitted[nomit].r = cells->cell[i].r; omitted[nomit].state = cells->cell[i].state; nomit++;
            }
        }
        qsort(omitted, nomit, sizeof(*omitted), cmp_pos);
        if (nomit) { fputs("omitted_cells:\n", fp); for (size_t i = 0; i < nomit; i++) fprintf(fp, "  (%d, %d) state=%c\n", omitted[i].q, omitted[i].r, omitted[i].state); }
        free(omitted);
    }
    fclose(fp);ok=stats->cycle_conflicts==0&&stats->overlaps==0;if(!ok)snprintf(err,errsz,"hat placement audit failed");done:free(c);free(cons);free(edges);free(adj);free(deg);free(tr);free(placed);return ok;}
