#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CYCLES 64
#define MAX_TILES 256
#define MAX_RULES 512
#define MAX_TOK 160
#define MAX_VERTS 768
#define MAX_CONS 2048
#define MAX_GROUPS 128
#define MAX_CLUSTER 8
#define MAX_ROWS 128
#define MAX_IO 512

typedef struct { char in[MAX_TOK], out[MAX_TOK]; } Pair6;
typedef struct { Pair6 p[6]; } Cycle6;
typedef struct { char e[6][MAX_TOK]; } Tile6;
typedef struct { char a[MAX_TOK], b[MAX_TOK]; } Rule6;
typedef struct {
    Cycle6 cycles[MAX_CYCLES]; int ncycles;
    Tile6 tiles[MAX_TILES]; int ntiles;
    Rule6 rules[MAX_RULES]; int nrules;
    char header[512];
} State6;

typedef uint64_t Matrix6[MAX_CYCLES][MAX_CYCLES];

typedef struct { int map[MAX_CYCLES]; int off[MAX_CYCLES]; int extra; } Iso6;

typedef struct { char la[MAX_TOK], lb[MAX_TOK], ra[MAX_TOK], rb[MAX_TOK]; } Cons6;

typedef struct { char name[MAX_TOK]; int parent; } DsuItem;
typedef struct { DsuItem item[MAX_VERTS]; int n; } Dsu;

typedef struct { char v[MAX_CLUSTER][MAX_TOK]; int n; } Cluster;
typedef struct { Cluster c[6]; int n; int row; int active; } Row6;
typedef struct { int row_of_rec[1024]; Row6 rows[MAX_ROWS]; int nrows; } GroupedRows;
typedef struct { int recs[32]; int nrecs; char inner[6][MAX_TOK]; char outer[6][MAX_TOK]; } Io6;

typedef struct { char key[MAX_TOK]; char val[MAX_TOK]; } StrMap;
typedef struct { StrMap m[512]; int n; } StrMapList;

static void die(const char *msg){ fprintf(stderr, "rl6_generate: %s\n", msg); exit(1); }
static void trim(char *s){
    char *p=s; while(*p && isspace((unsigned char)*p)) p++;
    if(p!=s) memmove(s,p,strlen(p)+1);
    size_t n=strlen(s); while(n && isspace((unsigned char)s[n-1])) s[--n]='\0';
}
static int starts(const char *s,const char *p){ return strncmp(s,p,strlen(p))==0; }
static void copy_tok(char d[MAX_TOK], const char *s){ if(!s) s=""; snprintf(d, MAX_TOK, "%.*s", MAX_TOK-1, s); }
static void copy_str(char *d, size_t cap, const char *s){
    if(!d || cap == 0) return;
    if(!s) s = "";
    snprintf(d, cap, "%.*s", (int)cap - 1, s);
}
static void join_edge_tok(char out[MAX_TOK], const char *a, const char *b){
    enum { HALF = (MAX_TOK - 2) / 2 };
    if(!a) a = "";
    if(!b) b = "";
    snprintf(out, MAX_TOK, "%.*s,%.*s", HALF, a, HALF, b);
}
static int pair_cmp(const Pair6 *a,const Pair6 *b){ int c=strcmp(a->in,b->in); return c?c:strcmp(a->out,b->out); }
static int cycle_cmp(const Cycle6 *a,const Cycle6 *b){ for(int i=0;i<6;i++){ int c=pair_cmp(&a->p[i],&b->p[i]); if(c) return c; } return 0; }
static void cycle_rot(const Cycle6 *in,int k,Cycle6 *out){ for(int i=0;i<6;i++) out->p[i]=in->p[(i+k)%6]; }
static void cycle_canon(const Cycle6 *in,Cycle6 *out){ Cycle6 best,r; cycle_rot(in,0,&best); for(int k=1;k<6;k++){ cycle_rot(in,k,&r); if(cycle_cmp(&r,&best)<0) best=r; } *out=best; }
static int cycle_equal(const Cycle6 *a,const Cycle6 *b){ return cycle_cmp(a,b)==0; }
static int bitcount64(uint64_t x){ int n=0; while(x){ n+=(int)(x&1u); x>>=1; } return n; }

static void parse_state_stream(FILE *f, const char *name, State6 *d){
    memset(d,0,sizeof(*d));
    char line[8192]; int in=0;
    while(fgets(line,sizeof(line),f)){
        trim(line); if(!*line) continue;
        if(starts(line,"RL6_STATE ")){ in=1; copy_str(d->header,sizeof(d->header),line); continue; }
        if(strcmp(line,"END_STATE")==0){ in=0; continue; }
        if(!in) continue;
        if(starts(line,"TILE ")){
            if(d->ntiles>=MAX_TILES) die("too many tiles");
            Tile6 *t=&d->tiles[d->ntiles++]; char tmp[8192]; snprintf(tmp,sizeof(tmp),"%s",line+5);
            char *save=NULL; int n=0; for(char *p=strtok_r(tmp," \t",&save);p;p=strtok_r(NULL," \t",&save)){ if(n<6) copy_tok(t->e[n++],p); }
            if(n!=6) die("bad TILE length");
        } else if(starts(line,"RULE ")){
            if(d->nrules>=MAX_RULES) die("too many rules");
            char a[MAX_TOK],b[MAX_TOK]; if(sscanf(line+5,"%159s %159s",a,b)!=2) die("bad RULE");
            copy_tok(d->rules[d->nrules].a,a); copy_tok(d->rules[d->nrules].b,b); d->nrules++;
        } else if(starts(line,"CYCLE ")){
            if(d->ncycles>=MAX_CYCLES) die("too many cycles");
            Cycle6 *c=&d->cycles[d->ncycles++]; char tmp[8192]; snprintf(tmp,sizeof(tmp),"%s",line+6);
            char *save=NULL; int n=0; for(char *p=strtok_r(tmp," \t",&save);p;p=strtok_r(NULL," \t",&save)){
                char *bar=strchr(p,'|'); if(!bar) die("bad CYCLE token"); *bar='\0'; if(n<6){ copy_tok(c->p[n].in,p); copy_tok(c->p[n].out,bar+1); } n++;
            }
            if(n!=6) die("bad CYCLE length");
        }
    }
    if(d->ncycles!=25){
        fprintf(stderr, "rl6_generate: %s has %d CYCLE rows; expected 25\n", name ? name : "state", d->ncycles);
        exit(1);
    }
}

static void parse_state_dat_file(const char *path, State6 *d){
    FILE *f=fopen(path,"r");
    if(!f){ perror(path); die("cannot open state file"); }
    parse_state_stream(f,path,d);
    fclose(f);
}

static void read_reduce_state(const char *model, int target, State6 *d){
    char cmd[512];
    snprintf(cmd,sizeof(cmd),"./bin/rl6_reduce --model %s --emit-state %d", model, target);
    FILE *f=popen(cmd,"r");
    if(!f){ perror("popen"); die("cannot run rl6_reduce"); }

    size_t cap = 16384;
    size_t len = 0;
    char *buf = malloc(cap);
    if(!buf) die("out of memory reading rl6_reduce output");
    buf[0] = '\0';

    char line[8192];
    while(fgets(line,sizeof(line),f)){
        size_t n = strlen(line);
        if(len + n + 1 > cap){
            while(len + n + 1 > cap) cap *= 2;
            char *tmp = realloc(buf, cap);
            if(!tmp){ free(buf); die("out of memory expanding rl6_reduce output"); }
            buf = tmp;
        }
        memcpy(buf + len, line, n);
        len += n;
        buf[len] = '\0';
    }

    int rc=pclose(f);
    if(rc != 0){
        fprintf(stderr,"rl6_generate: command failed before state extraction: %s\n",cmd);
        free(buf);
        exit(1);
    }

    FILE *mem = fmemopen(buf, len, "r");
    if(!mem){ free(buf); die("cannot parse rl6_reduce output"); }
    parse_state_stream(mem,cmd,d);
    fclose(mem);
    free(buf);
}

static void slot_matrix(const State6 *d, Matrix6 m){
    memset(m,0,sizeof(Matrix6));
    for(int a=0;a<d->ncycles;a++) for(int i=0;i<6;i++){
        const Pair6 *p=&d->cycles[a].p[i];
        for(int b=0;b<d->ncycles;b++) for(int j=0;j<6;j++){
            const Pair6 *q=&d->cycles[b].p[j];
            if(strcmp(p->in,q->out)==0 && strcmp(p->out,q->in)==0) m[a][b] |= ((uint64_t)1) << (6*i+j);
        }
    }
}
static uint64_t shift_mask(uint64_t mask,int ro,int co){
    ro%=6; if(ro<0) ro+=6; co%=6; if(co<0) co+=6; uint64_t out=0;
    for(int i=0;i<6;i++) for(int j=0;j<6;j++){
        int ti=(i+ro)%6, tj=(j+co)%6; if(mask & (((uint64_t)1)<<(6*ti+tj))) out |= ((uint64_t)1)<<(6*i+j);
    }
    return out;
}
static int pair_ok_exact(Matrix6 a, Matrix6 b,int li,int rj,int oj,int lk,int rk,int ok){
    return a[li][lk] == shift_mask(b[rj][rk],oj,ok) && a[lk][li] == shift_mask(b[rk][rj],ok,oj);
}
static int pair_ok_pruned(Matrix6 left, Matrix6 right,int li,int rj,int oj,int lk,int rk,int ok){
    uint64_t r1=shift_mask(right[rj][rk],oj,ok), r2=shift_mask(right[rk][rj],ok,oj);
    return ((r1 & ~left[li][lk])==0) && ((r2 & ~left[lk][li])==0);
}

typedef struct { int loop,out,in; int out_counts[MAX_CYCLES]; int in_counts[MAX_CYCLES]; uint64_t self_orbit[6]; } Sig6;
static int int_cmp(const void*a,const void*b){ return (*(const int*)a)-(*(const int*)b); }
static int u64_cmp(const void*a,const void*b){ uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b; return (x>y)-(x<y); }
static Sig6 node_sig(Matrix6 m,int n,int i){
    Sig6 s; memset(&s,0,sizeof(s)); s.loop=bitcount64(m[i][i]);
    for(int j=0;j<n;j++){ int oc=bitcount64(m[i][j]), ic=bitcount64(m[j][i]); s.out+=oc; s.in+=ic; s.out_counts[j]=oc; s.in_counts[j]=ic; }
    qsort(s.out_counts,n,sizeof(int),int_cmp); qsort(s.in_counts,n,sizeof(int),int_cmp);
    for(int k=0;k<6;k++) s.self_orbit[k]=shift_mask(m[i][i],k,k);
    qsort(s.self_orbit,6,sizeof(uint64_t),u64_cmp);
    return s;
}
static int sig_equal(const Sig6 *a,const Sig6 *b,int n){
    if(a->loop!=b->loop||a->out!=b->out||a->in!=b->in) return 0;
    for(int i=0;i<n;i++) if(a->out_counts[i]!=b->out_counts[i]||a->in_counts[i]!=b->in_counts[i]) return 0;
    for(int i=0;i<6;i++) if(a->self_orbit[i]!=b->self_orbit[i]) return 0;
    return 1;
}

typedef struct { int j,off; } Cand;
typedef struct { Cand v[256]; int n; } CandList;
static int rec_iso(Matrix6 a,Matrix6 b,int n,CandList cand[MAX_CYCLES],int exact,int map[MAX_CYCLES],int off[MAX_CYCLES],int used[MAX_CYCLES],Iso6 *first,int *count,int limit){
    if(*count>=limit) return 1;
    int done=1; for(int i=0;i<n;i++) if(map[i]<0){ done=0; break; }
    if(done){
        if(*count==0 && first){ for(int i=0;i<n;i++){ first->map[i]=map[i]; first->off[i]=off[i]; } first->extra=0; }
        (*count)++; return *count>=limit;
    }
    int bi=-1, bc=9999; Cand choices[256]; int nch=0;
    for(int i=0;i<n;i++) if(map[i]<0){
        int c=0; Cand tmp[256];
        for(int x=0;x<cand[i].n;x++){
            int j=cand[i].v[x].j, o=cand[i].v[x].off; if(used[j]) continue;
            int ok=1; for(int k=0;k<n;k++) if(map[k]>=0){ ok = exact ? pair_ok_exact(a,b,i,j,o,k,map[k],off[k]) : pair_ok_pruned(a,b,i,j,o,k,map[k],off[k]); if(!ok) break; }
            if(ok) tmp[c++]=cand[i].v[x];
        }
        if(c<bc){ bc=c; bi=i; nch=c; memcpy(choices,tmp,sizeof(Cand)*(size_t)c); if(c==0) break; }
    }
    if(nch==0) return 0;
    for(int x=0;x<nch;x++){
        map[bi]=choices[x].j; off[bi]=choices[x].off; used[map[bi]]=1;
        if(rec_iso(a,b,n,cand,exact,map,off,used,first,count,limit)) return 1;
        used[map[bi]]=0; map[bi]=-1; off[bi]=-1;
    }
    return 0;
}
static int find_exact_iso(Matrix6 a,Matrix6 b,int n,Iso6 *first,int limit){
    Sig6 sa[MAX_CYCLES],sb[MAX_CYCLES]; CandList cand[MAX_CYCLES];
    for(int i=0;i<n;i++){ sa[i]=node_sig(a,n,i); sb[i]=node_sig(b,n,i); cand[i].n=0; }
    for(int i=0;i<n;i++){
        for(int j=0;j<n;j++) if(sig_equal(&sa[i],&sb[j],n)) for(int o=0;o<6;o++) if(a[i][i]==shift_mask(b[j][j],o,o)){
            cand[i].v[cand[i].n++] = (Cand){j,o};
        }
        if(cand[i].n==0) return 0;
    }
    int map[MAX_CYCLES],off[MAX_CYCLES],used[MAX_CYCLES]={0}; for(int i=0;i<n;i++){map[i]=-1;off[i]=-1;}
    int count=0; rec_iso(a,b,n,cand,1,map,off,used,first,&count,limit); return count;
}
static int find_pruned_iso(Matrix6 left,Matrix6 right,int n,Iso6 *first,int limit){
    int lout[MAX_CYCLES],lin[MAX_CYCLES],lloop[MAX_CYCLES],rout[MAX_CYCLES],rin[MAX_CYCLES],rloop[MAX_CYCLES]; CandList cand[MAX_CYCLES];
    for(int i=0;i<n;i++){ lout[i]=lin[i]=rout[i]=rin[i]=0; lloop[i]=bitcount64(left[i][i]); rloop[i]=bitcount64(right[i][i]); cand[i].n=0; }
    for(int i=0;i<n;i++) for(int j=0;j<n;j++){ lout[i]+=bitcount64(left[i][j]); lin[j]+=bitcount64(left[i][j]); rout[i]+=bitcount64(right[i][j]); rin[j]+=bitcount64(right[i][j]); }
    for(int i=0;i<n;i++){
        for(int j=0;j<n;j++){
            if(rout[j]>lout[i]||rin[j]>lin[i]||rloop[j]>lloop[i]) continue;
            for(int o=0;o<6;o++) if((shift_mask(right[j][j],o,o)&~left[i][i])==0) cand[i].v[cand[i].n++]=(Cand){j,o};
        }
        if(cand[i].n==0) return 0;
    }
    int map[MAX_CYCLES],off[MAX_CYCLES],used[MAX_CYCLES]={0}; for(int i=0;i<n;i++){map[i]=-1;off[i]=-1;}
    int count=0; rec_iso(left,right,n,cand,0,map,off,used,first,&count,limit);
    if(count>0 && first){ int extra=0; for(int i=0;i<n;i++) for(int j=0;j<n;j++){ uint64_t r=shift_mask(right[first->map[i]][first->map[j]],first->off[i],first->off[j]); extra += bitcount64(left[i][j] & ~r); } first->extra=extra; }
    return count;
}

static void dsu_init(Dsu *d){ d->n=0; }
static int dsu_add(Dsu *d,const char *name){ for(int i=0;i<d->n;i++) if(strcmp(d->item[i].name,name)==0) return i; if(d->n>=MAX_VERTS) die("too many vertices"); copy_tok(d->item[d->n].name,name); d->item[d->n].parent=d->n; return d->n++; }
static int dsu_find(Dsu *d,int x){ if(d->item[x].parent!=x) d->item[x].parent=dsu_find(d,d->item[x].parent); return d->item[x].parent; }
static int dsu_union(Dsu *d,int a,int b){ int ra=dsu_find(d,a), rb=dsu_find(d,b); if(ra==rb) return 0; if(strcmp(d->item[rb].name,d->item[ra].name)<0){ int t=ra;ra=rb;rb=t; } d->item[rb].parent=ra; return 1; }
static int dsu_find_name(Dsu *d,const char *name){ int i=dsu_add(d,name); return dsu_find(d,i); }
static void endpoints(const char *edge,char a[MAX_TOK],char b[MAX_TOK]){ const char *c=strchr(edge,','); if(!c){ fprintf(stderr,"bad edge token: [%s]\n", edge); die("bad edge token"); } int n=(int)(c-edge); if(n<=0||n>=MAX_TOK) die("bad edge endpoint"); memcpy(a,edge,(size_t)n); a[n]='\0'; copy_tok(b,c+1); }
static void add_vertices_from_cycles(Dsu *d,const State6 *x){ char a[MAX_TOK],b[MAX_TOK]; for(int c=0;c<x->ncycles;c++) for(int s=0;s<6;s++){ endpoints(x->cycles[c].p[s].in,a,b); dsu_add(d,a); dsu_add(d,b); endpoints(x->cycles[c].p[s].out,a,b); dsu_add(d,a); dsu_add(d,b); } }
static int build_constraints(const State6 *l,const State6 *r,const Iso6 *iso,Cons6 cons[MAX_CONS]){ int n=0; char la[MAX_TOK],lb[MAX_TOK],ra[MAX_TOK],rb[MAX_TOK]; for(int c=0;c<l->ncycles;c++){ int rc=iso->map[c], off=iso->off[c]%6; for(int s=0;s<6;s++){ const Pair6 *lp=&l->cycles[c].p[s], *rp=&r->cycles[rc].p[(s+off)%6]; endpoints(lp->in,la,lb); endpoints(rp->in,ra,rb); if(n>=MAX_CONS) die("too many constraints"); copy_tok(cons[n].la,la);copy_tok(cons[n].lb,lb);copy_tok(cons[n].ra,ra);copy_tok(cons[n].rb,rb); n++; endpoints(lp->out,la,lb); endpoints(rp->out,ra,rb); if(n>=MAX_CONS) die("too many constraints"); copy_tok(cons[n].la,la);copy_tok(cons[n].lb,lb);copy_tok(cons[n].ra,ra);copy_tok(cons[n].rb,rb); n++; } } return n; }
static int refine_endpoint(Dsu *ld,Dsu *rd,const Cons6 cons[MAX_CONS],int ncons){ int rounds=0; for(;;){ rounds++; int changed=0; for(int i=0;i<ncons;i++) for(int j=i+1;j<ncons;j++){
            const char *l1[2]={cons[i].la,cons[i].lb}, *r1[2]={cons[i].ra,cons[i].rb}; const char *l2[2]={cons[j].la,cons[j].lb}, *r2[2]={cons[j].ra,cons[j].rb};
            for(int a=0;a<2;a++) for(int b=0;b<2;b++){ int lg1=dsu_find_name(ld,l1[a]), lg2=dsu_find_name(ld,l2[b]); int rg1=dsu_find_name(rd,r1[a]), rg2=dsu_find_name(rd,r2[b]); if(lg1==lg2) changed |= dsu_union(rd,rg1,rg2); if(rg1==rg2) changed |= dsu_union(ld,lg1,lg2); }
        }
        if(!changed) return rounds; }
}
static int group_members(Dsu *d,int root,char out[MAX_VERTS][MAX_TOK]){ int n=0; for(int i=0;i<d->n;i++) if(dsu_find(d,i)==root) copy_tok(out[n++],d->item[i].name); return n; }
static int str_cmp_ptr(const void*a,const void*b){ const char *x=(const char*)a,*y=(const char*)b; return strcmp(x,y); }
static int roots_sorted(Dsu *d,int roots[MAX_VERTS],int by_first){ int n=0; for(int i=0;i<d->n;i++){ int r=dsu_find(d,i), seen=0; for(int j=0;j<n;j++) if(roots[j]==r) seen=1; if(!seen) roots[n++]=r; }
    for(int i=0;i<n;i++) for(int j=i+1;j<n;j++){ char ai[MAX_VERTS][MAX_TOK], aj[MAX_VERTS][MAX_TOK]; int ni=group_members(d,roots[i],ai), nj=group_members(d,roots[j],aj); qsort(ai,(size_t)ni,MAX_TOK,str_cmp_ptr); qsort(aj,(size_t)nj,MAX_TOK,str_cmp_ptr); int c=strcmp(ai[0],aj[0]); if(!by_first && ni!=nj) c=ni-nj; if(c>0){ int t=roots[i];roots[i]=roots[j];roots[j]=t; } }
    return n; }

static void parse_vlist(const char *s, Cluster *c){ c->n=0; const char *p=s; while((p=strchr(p,'v'))){ int j=0; char buf[MAX_TOK]; buf[j++]=*p++; while(isdigit((unsigned char)*p) && j<MAX_TOK-1) buf[j++]=*p++; buf[j]='\0'; copy_tok(c->v[c->n++],buf); if(c->n>=MAX_CLUSTER) break; } }
static void cluster_join(const Cluster *c,char *buf,size_t cap){ buf[0]='\0'; for(int i=0;i<c->n;i++){ if(i) strncat(buf,".",cap-strlen(buf)-1); strncat(buf,c->v[i],cap-strlen(buf)-1); } }
static void down_edge(const Cluster *a,const Cluster *b,char out[MAX_TOK]){ join_edge_tok(out,a->v[0],b->v[b->n-1]); }
static void long_edge(const Cluster *a,const Cluster *b,char out[MAX_TOK]){ char x[MAX_TOK],y[MAX_TOK]; cluster_join(a,x,sizeof(x)); cluster_join(b,y,sizeof(y)); join_edge_tok(out,x,y); }
static void parse_grouped_rows(const char *path,GroupedRows *gr){ memset(gr,0,sizeof(*gr)); for(int i=0;i<1024;i++) gr->row_of_rec[i]=-1; FILE *f=fopen(path,"r"); if(!f) return; char line[4096]; while(fgets(line,sizeof(line),f)){
        char *p=line; while(isspace((unsigned char)*p)) p++; if(!isdigit((unsigned char)*p)) continue; int row=atoi(p); char *br=strstr(p,"[["); char *rec=strstr(p,"# records"); if(!br||!rec) continue; Row6 *r=&gr->rows[gr->nrows++]; memset(r,0,sizeof(*r)); r->row=row; r->active=!(row==6||row==7||row==12||row==13);
        char *q=br; while((q=strchr(q,'[')) && r->n<6){ if(q[1]=='['){q++; continue;} char *e=strchr(q,']'); if(!e) break; char tmp[512]; int len=(int)(e-q-1); if(len>500) len=500; memcpy(tmp,q+1,(size_t)len); tmp[len]='\0'; parse_vlist(tmp,&r->c[r->n]); if(r->c[r->n].n>0) r->n++; q=e+1; }
        char *rp=rec; while((rp=strpbrk(rp,"0123456789"))){ int v=atoi(rp); if(v>=0&&v<1024) gr->row_of_rec[v]=gr->nrows-1; while(isdigit((unsigned char)*rp)) rp++; }
    } fclose(f); }
static int parse_edges_from_line(const char *line,char out[6][MAX_TOK]){ int n=0; const char *p=line; while((p=strstr(p,"e(")) && n<6){ p+=2; const char *q=strchr(p,')'); if(!q) break; int len=(int)(q-p); if(len>=MAX_TOK) len=MAX_TOK-1; memcpy(out[n],p,(size_t)len); out[n][len]='\0'; n++; p=q+1; } return n; }
static int parse_io(const char *path,Io6 io[MAX_IO]){ FILE *f=fopen(path,"r"); if(!f) return 0; char line[4096],l2[4096],l3[4096]; int n=0; while(fgets(line,sizeof(line),f)){ if(!starts(line,"---[io:")) continue; Io6 *x=&io[n]; memset(x,0,sizeof(*x)); char *rec=strstr(line,"# records"); if(rec){ char *rp=rec; while((rp=strpbrk(rp,"0123456789")) && x->nrecs<32){ x->recs[x->nrecs++]=atoi(rp); while(isdigit((unsigned char)*rp)) rp++; } } if(!fgets(l2,sizeof(l2),f)||!fgets(l3,sizeof(l3),f)) break; if(parse_edges_from_line(l2,x->inner)==6 && parse_edges_from_line(l3,x->outer)==6) n++; } fclose(f); return n; }
static void map_put(StrMapList *m,const char *key,const char *val){ for(int i=0;i<m->n;i++) if(strcmp(m->m[i].key,key)==0){ copy_tok(m->m[i].val,val); return; } if(m->n<512){ copy_tok(m->m[m->n].key,key); copy_tok(m->m[m->n].val,val); m->n++; } }
static const char *map_get(StrMapList *m,const char *key){ for(int i=0;i<m->n;i++) if(strcmp(m->m[i].key,key)==0) return m->m[i].val; return NULL; }
static void build_long_overlap(const char *path,const State6 *shortd,State6 *longd){
    *longd=*shortd;

    /*
       These objects are intentionally heap allocated.  With ASan enabled, and
       on some default Linux stack limits, keeping the grouped rows, IO table,
       and two lookup tables on the stack can overflow during boot_rl6.
    */
    GroupedRows *gr = calloc(1, sizeof(*gr));
    Io6 *io = calloc(MAX_IO, sizeof(*io));
    Cycle6 *lut_short = calloc(MAX_IO, sizeof(*lut_short));
    Cycle6 *lut_long = calloc(MAX_IO, sizeof(*lut_long));
    if(!gr || !io || !lut_short || !lut_long) die("out of memory building long overlap table");

    parse_grouped_rows(path,gr);
    if(gr->nrows==0) goto done;
    int nio=parse_io(path,io);
    if(nio==0) goto done;

    StrMapList possible={0};
    for(int r=0;r<gr->nrows;r++) if(gr->rows[r].active && gr->rows[r].n==6){
        for(int i=0;i<6;i++){
            char key[MAX_TOK],val[MAX_TOK];
            down_edge(&gr->rows[r].c[i],&gr->rows[r].c[(i+1)%6],key);
            long_edge(&gr->rows[r].c[i],&gr->rows[r].c[(i+1)%6],val);
            if(!map_get(&possible,key)) map_put(&possible,key,val);
        }
    }

    int nlut=0;
    for(int x=0;x<nio;x++){
        if(io[x].nrecs<=0) continue;
        int ri=gr->row_of_rec[io[x].recs[0]];
        if(ri<0||ri>=gr->nrows||!gr->rows[ri].active||gr->rows[ri].n!=6) continue;
        Row6 *row=&gr->rows[ri];
        Cycle6 sh,shc,lg,lgc;
        for(int i=0;i<6;i++){
            copy_tok(sh.p[i].in,io[x].inner[i]);
            copy_tok(sh.p[i].out,io[x].outer[i]);
        }
        cycle_canon(&sh,&shc);
        char central[6][MAX_TOK];
        for(int i=0;i<6;i++) down_edge(&row->c[i],&row->c[(i+1)%6],central[i]);
        int rot=-1;
        for(int k=0;k<6;k++){
            int ok=1;
            for(int i=0;i<6;i++) if(strcmp(central[(k+i)%6],io[x].inner[i])!=0) ok=0;
            if(ok){ rot=k; break; }
        }
        for(int i=0;i<6;i++){
            if(rot>=0) long_edge(&row->c[(rot+i)%6],&row->c[(rot+i+1)%6],lg.p[i].in);
            else {
                const char *v=map_get(&possible,io[x].inner[i]);
                copy_tok(lg.p[i].in,v?v:io[x].inner[i]);
            }
            const char *v=map_get(&possible,io[x].outer[i]);
            copy_tok(lg.p[i].out,v?v:io[x].outer[i]);
        }
        cycle_canon(&lg,&lgc);
        int seen=0;
        for(int i=0;i<nlut;i++) if(cycle_equal(&lut_short[i],&shc)) seen=1;
        if(!seen && nlut<MAX_IO){ lut_short[nlut]=shc; lut_long[nlut]=lgc; nlut++; }
    }
    for(int c=0;c<shortd->ncycles;c++){
        Cycle6 sc;
        cycle_canon(&shortd->cycles[c],&sc);
        for(int i=0;i<nlut;i++) if(cycle_equal(&lut_short[i],&sc)){
            longd->cycles[c]=lut_long[i];
            break;
        }
    }

done:
    free(lut_long);
    free(lut_short);
    free(io);
    free(gr);
}

static void group_to_csv(Dsu *d,int root,char *buf,size_t cap){ char mem[MAX_VERTS][MAX_TOK]; int n=group_members(d,root,mem); qsort(mem,(size_t)n,MAX_TOK,str_cmp_ptr); buf[0]='\0'; for(int i=0;i<n;i++){ if(i) strncat(buf,", ",cap-strlen(buf)-1); strncat(buf,mem[i],cap-strlen(buf)-1); } }
static void var_label(int i,char out[MAX_TOK]){ if(i < 26) snprintf(out,MAX_TOK,"%c",'A'+i); else snprintf(out,MAX_TOK,"X%d",i-26); }
static int derive_root_map(Dsu *ld,Dsu *rd,const Cons6 cons[MAX_CONS],int ncons,const int lroots[MAX_VERTS],int nroots,int out[MAX_VERTS]){
    for(int i=0;i<nroots;i++) out[i] = -1;
    for(int i=0;i<ncons;i++){
        const char *ln[2] = { cons[i].la, cons[i].lb };
        const char *rn[2] = { cons[i].ra, cons[i].rb };
        for(int k=0;k<2;k++){
            int lr = dsu_find_name(ld, ln[k]);
            int rr = dsu_find_name(rd, rn[k]);
            for(int j=0;j<nroots;j++) if(lroots[j] == lr){
                if(out[j] < 0) out[j] = rr;
                else if(out[j] != rr) return 0;
            }
        }
    }
    return 1;
}
static int translate_roots_by_member(Dsu *from,Dsu *to,const int in[MAX_VERTS],int nroots,int out[MAX_VERTS]){
    for(int i=0;i<nroots;i++){
        char mem[MAX_VERTS][MAX_TOK];
        int n = group_members(from,in[i],mem);
        out[i] = -1;
        for(int j=0;j<n && out[i]<0;j++){
            for(int k=0;k<to->n;k++) if(strcmp(to->item[k].name,mem[j])==0){ out[i]=dsu_find(to,k); break; }
        }
        if(out[i] < 0) return 0;
    }
    return 1;
}
static void write_group_section(FILE *f,const char *name,Dsu *d,const int roots[MAX_VERTS],int nroots){
    fprintf(f,"---[%s]---\n",name);
    for(int i=0;i<nroots;i++){
        char lab[MAX_TOK], g[4096];
        var_label(i,lab);
        if(roots[i] >= 0) group_to_csv(d,roots[i],g,sizeof(g));
        else snprintf(g,sizeof(g),"?");
        fprintf(f,"%s: %s\n",lab,g);
    }
}
static const char *relabel_vertex_name(const char *name,Dsu *d,char vars[MAX_VERTS][MAX_TOK],int roots[MAX_VERTS],int nroots){
    int r=dsu_find_name(d,name);
    for(int i=0;i<nroots;i++) if(roots[i]==r) return vars[i];
    return "?";
}
static void relabel_edge(const char *edge,Dsu *d,char vars[MAX_VERTS][MAX_TOK],int roots[MAX_VERTS],int nroots,char out[MAX_TOK]){
    char a[MAX_TOK],b[MAX_TOK];
    const char *va,*vb;
    endpoints(edge,a,b);
    va = relabel_vertex_name(a,d,vars,roots,nroots);
    vb = relabel_vertex_name(b,d,vars,roots,nroots);
    snprintf(out,MAX_TOK,"%.70s,%.70s",va,vb);
}
static int tile_cmp(const void *A,const void *B){ const Tile6 *a=A,*b=B; for(int i=0;i<6;i++){ int c=strcmp(a->e[i],b->e[i]); if(c) return c; } return 0; }
static void canon_tile(Tile6 *t){ Tile6 best=*t; for(int k=1;k<6;k++){ Tile6 r; for(int i=0;i<6;i++) copy_tok(r.e[i],t->e[(i+k)%6]); if(tile_cmp(&r,&best)<0) best=r; } *t=best; }
static int rule_cmp(const void*A,const void*B){ const Rule6*a=A,*b=B; int c=strcmp(a->a,b->a); return c?c:strcmp(a->b,b->b); }
static int rule_eq(const Rule6*a,const Rule6*b){ return strcmp(a->a,b->a)==0 && strcmp(a->b,b->b)==0; }
static void write_unified(const char *path,const State6 *basic,Dsu *bd,Dsu *sd,Dsu *od,const int s_var_roots[MAX_VERTS],const int o_var_roots[MAX_VERTS],int have_overlap,int overlap_extra){
    int roots[MAX_VERTS]; int nroots=roots_sorted(bd,roots,1); char vars[MAX_VERTS][MAX_TOK]; for(int i=0;i<nroots;i++){ if(i<26) snprintf(vars[i],MAX_TOK,"%c",'A'+i); else snprintf(vars[i],MAX_TOK,"X%d",i-26); }
    Tile6 qtiles[MAX_TILES]; int nq=0; Rule6 qrules[MAX_RULES]; int nr=0;
    for(int t=0;t<basic->ntiles;t++){
        Tile6 qt;
        for(int i=0;i<6;i++){
            char a[MAX_TOK], b[MAX_TOK];
            (void)b;
            endpoints(basic->tiles[t].e[i],a,b);
            copy_tok(qt.e[i], relabel_vertex_name(a,bd,vars,roots,nroots));
        }
        canon_tile(&qt);
        int seen=0; for(int j=0;j<nq;j++) if(tile_cmp(&qt,&qtiles[j])==0) seen=1; if(!seen) qtiles[nq++]=qt;
    }
    for(int r=0;r<basic->nrules;r++){ Rule6 qr; relabel_edge(basic->rules[r].a,bd,vars,roots,nroots,qr.a); relabel_edge(basic->rules[r].b,bd,vars,roots,nroots,qr.b); int seen=0; for(int j=0;j<nr;j++) if(rule_eq(&qr,&qrules[j])) seen=1; if(!seen) qrules[nr++]=qr; }
    qsort(qtiles,(size_t)nq,sizeof(Tile6),tile_cmp); qsort(qrules,(size_t)nr,sizeof(Rule6),rule_cmp);
    Rule6 eclasses[MAX_RULES]; int ne=0; for(int i=0;i<nr;i++){ Rule6 ec=qrules[i]; Rule6 rev; copy_tok(rev.a,qrules[i].b); copy_tok(rev.b,qrules[i].a); if(rule_cmp(&rev,&ec)<0) ec=rev; int seen=0; for(int j=0;j<ne;j++) if(rule_eq(&ec,&eclasses[j])) seen=1; if(!seen) eclasses[ne++]=ec; } qsort(eclasses,(size_t)ne,sizeof(Rule6),rule_cmp);
    FILE *f=fopen(path,"w"); if(!f){ perror(path); die("cannot write unified model"); }
    fprintf(f,"# RL6 unified model, generated by ./bin/rl6_generate\n");
    fprintf(f,"# Source: C implementation of basic/super C6^25*S25 slot isomorphism.\n");
    fprintf(f,"# overlap_status: %s", have_overlap?"pruned_slot_embedding yes":"not included"); if(have_overlap) fprintf(f,"; deleted_slot_incidences %d",overlap_extra); fprintf(f,"\n\n");
    write_group_section(f,"basic",bd,roots,nroots);
    fprintf(f,"\n");
    write_group_section(f,"super",sd,s_var_roots,nroots);
    if(have_overlap){ fprintf(f,"\n"); write_group_section(f,"overlap",od,o_var_roots,nroots); }
    fprintf(f,"\n---[tiles]---\n"); for(int i=0;i<nq;i++){ for(int j=0;j<6;j++) fprintf(f,"%s%s",j?", ":"",qtiles[i].e[j]); fprintf(f,"\n"); }
    fprintf(f,"\n---[edge rules directed lex]---\n"); for(int i=0;i<nr;i++) fprintf(f,"%s = %s\n",qrules[i].a,qrules[i].b);
    fprintf(f,"\n---[edge classes forward_reverse_quotient]---\n"); for(int i=0;i<ne;i++) fprintf(f,"%s ~ %s\n",eclasses[i].a,eclasses[i].b);
    fprintf(f,"\n---[counts]---\n"); fprintf(f,"tiles: %d\n",nq); fprintf(f,"directed_edge_rules: %d\n",nr); fprintf(f,"edge_classes: %d\n",ne); fclose(f);
}

int main(int argc,char **argv){
    const char *out_path="data/rl6/unified_model.dat";
    State6 basic,sup,over,over_long;
    if(argc == 1 || argc == 2){
        if(argc == 2) out_path=argv[1];
        read_reduce_state("basic",10,&basic);
        read_reduce_state("super",8,&sup);
        read_reduce_state("overlap",13,&over);
    } else if(argc==5){
        parse_state_dat_file(argv[1],&basic);
        parse_state_dat_file(argv[2],&sup);
        parse_state_dat_file(argv[3],&over);
        out_path=argv[4];
    } else {
        fprintf(stderr,"usage: %s [OUT.dat]\n       %s BASIC.state SUPER.state OVERLAP.state OUT.dat\n", argv[0], argv[0]);
        return 2;
    }
    over_long=over; build_long_overlap("data/rl5/overlap_supertile_hexagons.dat",&over,&over_long);
    Matrix6 bm,sm,om; slot_matrix(&basic,bm); slot_matrix(&sup,sm); slot_matrix(&over,om);
    Iso6 iso; int exact=find_exact_iso(bm,sm,basic.ncycles,&iso,100); if(exact<=0) die("basic/super slot isomorphism not found");
    Cons6 cons[MAX_CONS]; int ncons=build_constraints(&basic,&sup,&iso,cons); Dsu bd,sd; dsu_init(&bd); dsu_init(&sd); add_vertices_from_cycles(&bd,&basic); add_vertices_from_cycles(&sd,&sup); (void)refine_endpoint(&bd,&sd,cons,ncons);
    int broots[MAX_VERTS], nbroots=roots_sorted(&bd,broots,1);
    int s_var_roots[MAX_VERTS], sd2_var_roots[MAX_VERTS], o_var_roots[MAX_VERTS];
    for(int i=0;i<MAX_VERTS;i++){ s_var_roots[i]=sd2_var_roots[i]=o_var_roots[i]=-1; }
    if(!derive_root_map(&bd,&sd,cons,ncons,broots,nbroots,s_var_roots)) die("basic/super vertex grouping is not one-to-one");
    Iso6 biso,oiso; (void)find_pruned_iso(bm,om,basic.ncycles,&biso,1);
    int sp=find_pruned_iso(sm,om,sup.ncycles,&oiso,1);
    Dsu od; dsu_init(&od); add_vertices_from_cycles(&od,&over_long); if(sp>0){ Cons6 oc[MAX_CONS]; int noc=build_constraints(&sup,&over_long,&oiso,oc); Dsu sd2; dsu_init(&sd2); add_vertices_from_cycles(&sd2,&sup); add_vertices_from_cycles(&od,&over_long); (void)refine_endpoint(&sd2,&od,oc,noc); if(translate_roots_by_member(&sd,&sd2,s_var_roots,nbroots,sd2_var_roots)) (void)derive_root_map(&sd2,&od,oc,noc,sd2_var_roots,nbroots,o_var_roots); }
    write_unified(out_path,&basic,&bd,&sd,&od,s_var_roots,o_var_roots,sp>0,oiso.extra);
    return 0;
}
