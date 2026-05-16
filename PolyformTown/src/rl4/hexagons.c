#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/cycle.h"
#include "core/tile.h"
#include "rl0/boundary0.h"
#include "rl0/forget_map.h"

#define MAX_REC 4096
#define MAX_TILES_LOCAL 128
#define MAX_COORDS_LOCAL (MAX_VERTS * MAX_CYCLES)
#define MAX_GROUPS_LOCAL 512
#define SIG_CAP 8192

typedef struct {
    int level, tile_count, start_index, dir;
    int have_boundary, have_center, have_hidden, have_tiles;
    Cycle center;
    Poly boundary;
    Coord hidden[MAX_COORDS_LOCAL]; int hidden_count;
    Cycle tiles[MAX_TILES_LOCAL]; int tiles_count;
} Rec;

typedef struct { RL0FMItem item; Coord prev,next; int tile, central, next_i, prev_i, used; } Inc;
typedef struct { int idx, kept, dropped, center_tile, center_p, opp_count, mirror_tile, mirror_p, mirror_i, mirror_cpos; unsigned mask; char key[256]; char sig[SIG_CAP]; } Info;
typedef struct { char key[256]; int count; int first; } Group;
typedef struct { int n; int present[MAX_VERTS]; char cell[MAX_VERTS][256]; } IndexMap;
typedef struct { int n; int cidx[MAX_VERTS]; char cell[MAX_VERTS][256]; } FigureSeq;


typedef struct { int p; int i; } PI;
typedef struct { int n; PI pi[RL0_FM_MAX_ITEMS]; } PIFigure;
typedef struct {
    int group;
    int mask_len;
    char mask[MAX_VERTS + 1];
    int record;
    int fig_count;
    PIFigure fig[MAX_VERTS];
    int hex_n;
    int hex[MAX_VERTS + 2];
} HexItem;

#define HEX_LABEL_A (-1001)
#define HEX_LABEL_B (-1002)

typedef struct { int n; int v[RL0_FM_MAX_ITEMS]; } NormFigure;
typedef struct { int sign; int x; int y; } SignedEdge;
typedef struct { SignedEdge a; SignedEdge b; } EdgeRule;
typedef struct { SignedEdge token; int parent; } EdgeNode;

#define MAX_HEX_ITEMS 512
#define MAX_EDGE_RULES 4096
#define MAX_EDGE_NODES 8192
#define MAX_UNIQUE_HEX 512

static void skip_ws(const char **p){ while(isspace((unsigned char)**p)) (*p)++; }
static int parse_int0(const char **p,int *out){ char *e; long v; skip_ws(p); v=strtol(*p,&e,10); if(e==*p) return 0; *out=(int)v; *p=e; return 1; }
static int expect(const char **p,char c){ skip_ws(p); if(**p!=c) return 0; (*p)++; return 1; }
static int parse_coord0(const char **p,Coord *q){ return expect(p,'(')&&parse_int0(p,&q->v)&&expect(p,',')&&parse_int0(p,&q->x)&&expect(p,',')&&parse_int0(p,&q->y)&&expect(p,')'); }
static int parse_cycle0(const char **p,Cycle *c){ c->n=0; if(!expect(p,'[')) return 0; skip_ws(p); if(**p==']'){(*p)++;return 1;} while(c->n<MAX_VERTS){ if(!parse_coord0(p,&c->v[c->n])) return 0; c->n++; skip_ws(p); if(**p==','){(*p)++;continue;} if(**p==']'){(*p)++; return c->n>0;} return 0;} return 0; }
static int parse_poly0(const char *s,Poly *poly){ const char *p=s; poly->cycle_count=0; if(!expect(&p,'[')) return 0; while(poly->cycle_count<MAX_CYCLES){ if(!parse_cycle0(&p,&poly->cycles[poly->cycle_count])) return 0; poly->cycle_count++; skip_ws(&p); if(*p=='|'){p++;continue;} if(*p==']'){p++; skip_ws(&p); return *p=='\0'||*p=='\n'||*p=='\r';} return 0;} return 0; }
static int parse_cycle_list0(const char *s,Cycle *out,int *n){ const char *p=s; int k=0; if(!expect(&p,'[')) return 0; skip_ws(&p); if(*p==']'){p++;*n=0;return 1;} while(k<MAX_TILES_LOCAL){ if(!parse_cycle0(&p,&out[k])) return 0; k++; skip_ws(&p); if(*p==','){p++;continue;} if(*p==']'){p++; skip_ws(&p); if(*p!='\0'&&*p!='\n'&&*p!='\r') return 0; *n=k; return 1;} return 0;} return 0; }
static int parse_coord_list0(const char *s,Coord *out,int *n){ const char *p=s; int k=0; if(!expect(&p,'[')) return 0; skip_ws(&p); if(*p==']'){p++;*n=0;return 1;} while(k<MAX_COORDS_LOCAL){ if(!parse_coord0(&p,&out[k])) return 0; k++; skip_ws(&p); if(*p==','){p++;continue;} if(*p==']'){p++; skip_ws(&p); if(*p!='\0'&&*p!='\n'&&*p!='\r') return 0; *n=k; return 1;} return 0;} return 0; }
static int parse_int_line(const char *line,const char *pre,int *out){ size_t n=strlen(pre); const char *p=line+n; return strncmp(line,pre,n)==0 && parse_int0(&p,out); }
static void rec_reset(Rec *r){ memset(r,0,sizeof(*r)); r->dir=0; }
static int load_recs(const char *path,Rec *recs,int *count){ FILE *fp=fopen(path,"r"); char line[262144]; Rec r; int n=0; if(!fp) return 0; rec_reset(&r); while(fgets(line,sizeof(line),fp)){ if(strncmp(line,"---[",4)==0){ if(r.have_center||r.have_tiles||r.have_boundary){ if(n>=MAX_REC){fclose(fp);return 0;} recs[n++]=r;} rec_reset(&r); continue;} if(parse_int_line(line,"level:",&r.level)) continue; if(parse_int_line(line,"tile_count:",&r.tile_count)) continue; if(parse_int_line(line,"start_index:",&r.start_index)) continue; if(strncmp(line,"direction:",10)==0){ r.dir=(strstr(line+10,"cw")&&!strstr(line+10,"ccw"))?-1:1; continue;} if(strncmp(line,"center_tile:",12)==0){ const char *p=line+12; r.have_center=parse_cycle0(&p,&r.center); continue;} if(strncmp(line,"boundary:",9)==0){ r.have_boundary=parse_poly0(line+9,&r.boundary); continue;} if(strncmp(line,"constellation:",14)==0){ r.have_hidden=parse_coord_list0(line+14,r.hidden,&r.hidden_count); continue;} if(strncmp(line,"tiles:",6)==0){ r.have_tiles=parse_cycle_list0(line+6,r.tiles,&r.tiles_count); continue;} }
 if(r.have_center||r.have_tiles||r.have_boundary){ if(n>=MAX_REC){fclose(fp);return 0;} recs[n++]=r;} fclose(fp); *count=n; return 1; }

static int coord_pos(const Cycle *c,Coord q){ for(int i=0;i<c->n;i++) if(coord_eq(c->v[i],q)) return i; return -1; }
static int cycle_eq_exact(const Cycle *a,const Cycle *b){ if(a->n!=b->n) return 0; for(int i=0;i<a->n;i++) if(!coord_eq(a->v[i],b->v[i])) return 0; return 1; }
static int center_tile_index(const Rec *r){ if(!r->have_center||!r->have_tiles) return -1; for(int i=0;i<r->tiles_count;i++) if(cycle_eq_exact(&r->tiles[i],&r->center)) return i; return r->tiles_count?0:-1; }
static int first_shared_pos(const Rec *r,int t,unsigned *mask){ int first=-1; *mask=0; for(int i=0;i<r->center.n && i<32;i++){ if(coord_pos(&r->tiles[t],r->center.v[i])>=0){ if(first<0) first=i; *mask |= 1u<<i; }} return first; }
static int item_less(RL0FMItem a,RL0FMItem b){ if(a.p!=b.p) return a.p<b.p; return a.i<b.i; }
static int inc_less(const Inc *a,const Inc *b){ if(a->central!=b->central) return a->central>b->central; if(item_less(a->item,b->item)) return 1; if(item_less(b->item,a->item)) return 0; return a->tile<b->tile; }
static int collect_inc(const Tile *tile,const Rec *r,Coord q,Inc *inc,int *cnt,int cti){ *cnt=0; for(int t=0;t<r->tiles_count;t++){ int pos=coord_pos(&r->tiles[t],q); RL0FMItem item; if(pos<0) continue; if(!boundary0_tile_item_at_vertex(tile,&r->tiles[t],q,&item)) return 0; if(*cnt>=RL0_FM_MAX_ITEMS) return 0; inc[*cnt].item=item; inc[*cnt].prev=r->tiles[t].v[(pos+r->tiles[t].n-1)%r->tiles[t].n]; inc[*cnt].next=r->tiles[t].v[(pos+1)%r->tiles[t].n]; inc[*cnt].tile=t; inc[*cnt].central=(t==cti); inc[*cnt].next_i=inc[*cnt].prev_i=-1; inc[*cnt].used=0; (*cnt)++; } return *cnt>0; }
static int order_inc(Inc *inc,int n,int *ord,int *on){ int start=-1,cur,vis=0; for(int i=0;i<n;i++){ inc[i].next_i=inc[i].prev_i=-1; inc[i].used=0;} for(int i=0;i<n;i++) for(int j=0;j<n;j++){ if(i==j) continue; if(coord_eq(inc[i].prev,inc[j].next)){ if(inc[i].next_i>=0) return 0; inc[i].next_i=j;} if(coord_eq(inc[i].next,inc[j].prev)){ if(inc[i].prev_i>=0) return 0; inc[i].prev_i=j;} } for(int i=0;i<n;i++) if(inc[i].prev_i<0 && (start<0||inc_less(&inc[i],&inc[start]))) start=i; if(start<0){ start=0; for(int i=1;i<n;i++) if(inc_less(&inc[i],&inc[start])) start=i; } cur=start; *on=0; while(cur>=0){ if(inc[cur].used) break; inc[cur].used=1; ord[(*on)++]=cur; vis++; cur=inc[cur].next_i; } return vis==n; }
static int shift_central(const Inc *inc,const int *ord,int n,int *sh){ int c=-1; for(int i=0;i<n;i++) if(inc[ord[i]].central){c=i;break;} if(c<0) return 0; for(int i=0;i<n;i++) sh[i]=ord[(c+i)%n]; return 1; }
static void sig_append(char *buf,size_t *off,const char *s){ size_t n=strlen(s); if(*off+n+1<SIG_CAP){ memcpy(buf+*off,s,n); *off+=n; buf[*off]='\0'; }}
static void sig_item(char *buf,size_t *off,RL0FMItem it){ char tmp[64]; snprintf(tmp,sizeof(tmp),"(%+d,%d)",it.p,it.i); sig_append(buf,off,tmp); }
static void figure_sig(const Tile *tile,const Rec *r,int cti,char *sig){ size_t off=0; sig[0]=0; for(int ci=0;ci<r->center.n;ci++){ Inc inc[RL0_FM_MAX_ITEMS]; int cnt=0,ord[RL0_FM_MAX_ITEMS],on=0,sh[RL0_FM_MAX_ITEMS]; char tmp[64]; if(!collect_inc(tile,r,r->center.v[ci],inc,&cnt,cti)) continue; if(cnt!=3&&cnt!=4) continue; if(!order_inc(inc,cnt,ord,&on)) continue; if(!shift_central(inc,ord,on,sh)) continue; snprintf(tmp,sizeof(tmp),"v%02d=",ci); sig_append(sig,&off,tmp); sig_append(sig,&off,"["); for(int k=0;k<on;k++){ if(k) sig_append(sig,&off," "); sig_item(sig,&off,inc[sh[k]].item);} sig_append(sig,&off,"];"); }}

static void item_to_short_str(RL0FMItem it,char *buf,size_t cap){ snprintf(buf,cap,"[%+d,%d]",it.p,it.i); }
static void cycle_to_str(const Inc *inc,const int *sh,int n,char *buf,size_t cap){ size_t off=0; off+=(size_t)snprintf(buf+off,cap-off,"["); for(int k=0;k<n&&off<cap;k++){ char tmp[64]; if(k) off+=(size_t)snprintf(buf+off,cap-off," "); item_to_short_str(inc[sh[k]].item,tmp,sizeof(tmp)); off+=(size_t)snprintf(buf+off,cap-off,"%s",tmp);} snprintf(buf+off,cap-off,"]"); }
static void build_index_map(const Tile *tile,const Rec *r,int cti,IndexMap *m){ m->n=r->center.n; for(int i=0;i<MAX_VERTS;i++){ m->present[i]=0; snprintf(m->cell[i],sizeof(m->cell[i]),"."); } for(int ci=0;ci<r->center.n;ci++){ Inc inc[RL0_FM_MAX_ITEMS]; int cnt=0,ord[RL0_FM_MAX_ITEMS],on=0,sh[RL0_FM_MAX_ITEMS]; if(!collect_inc(tile,r,r->center.v[ci],inc,&cnt,cti)) continue; if(cnt!=3&&cnt!=4) continue; if(!order_inc(inc,cnt,ord,&on)) { snprintf(m->cell[ci],sizeof(m->cell[ci]),"ORDER_FAIL_%d",cnt); m->present[ci]=1; continue; } if(!shift_central(inc,ord,on,sh)) { snprintf(m->cell[ci],sizeof(m->cell[ci]),"NO_CENTRAL_SHIFT_%d",cnt); m->present[ci]=1; continue; } cycle_to_str(inc,sh,on,m->cell[ci],sizeof(m->cell[ci])); m->present[ci]=1; } }
static void build_figure_seq(const IndexMap *m,FigureSeq *s){ s->n=0; for(int i=0;i<m->n && s->n<MAX_VERTS;i++){ if(!m->present[i]) continue; s->cidx[s->n]=i; snprintf(s->cell[s->n],sizeof(s->cell[s->n]),"%s",m->cell[i]); s->n++; } }
static int classify(const Tile *tile,const Rec *r,int idx,Info *info){ int cti=center_tile_index(r); RL0FMItem cit; int opp[MAX_TILES_LOCAL],oppn=0; memset(info,0,sizeof(*info)); info->idx=idx; info->center_tile=cti; info->mirror_tile=-1; info->mirror_cpos=-1; if(cti<0||!r->have_tiles){ info->dropped=1; snprintf(info->key,sizeof(info->key),"DROP:missing-center"); return 1;} if(!boundary0_tile_item_at_vertex(tile,&r->tiles[cti],r->tiles[cti].v[0],&cit)){ info->dropped=1; snprintf(info->key,sizeof(info->key),"DROP:center-item"); return 1;} info->center_p=cit.p; for(int t=0;t<r->tiles_count;t++){ RL0FMItem it; if(!boundary0_tile_item_at_vertex(tile,&r->tiles[t],r->tiles[t].v[0],&it)){ info->dropped=1; snprintf(info->key,sizeof(info->key),"DROP:item-fail"); return 1;} if(t!=cti && it.p!=cit.p) opp[oppn++]=t; } info->opp_count=oppn; if(oppn>1){ info->dropped=1; snprintf(info->key,sizeof(info->key),"DROP:reflected-surround-opposite-count-%d",oppn); return 1; } info->kept=1; if(oppn==1){ RL0FMItem mit; unsigned mask=0; int mt=opp[0]; int cpos=first_shared_pos(r,mt,&mask); Coord anchor = (cpos >= 0) ? r->center.v[cpos] : r->tiles[mt].v[0]; boundary0_tile_item_at_vertex(tile,&r->tiles[mt],anchor,&mit); info->mirror_tile=mt; info->mirror_p=mit.p; info->mirror_i=mit.i; info->mirror_cpos=cpos; info->mask=mask; snprintf(info->key,sizeof(info->key),"mirror:p=%+d:i=%d:cpos=%d:mask=%04x",mit.p,mit.i,cpos,mask); } else snprintf(info->key,sizeof(info->key),"singleton:record=%d:uniform-p=%+d",idx,cit.p); figure_sig(tile,r,cti,info->sig); return 1; }
static int mask_rotation_better(const int *same, int n, int a, int b){
    for(int k=0;k<n;k++){
        int va = same[(k+a)%n] ? 1 : 0;
        int vb = same[(k+b)%n] ? 1 : 0;
        if(va != vb) return va > vb;
    }
    return 0;
}

static int best_mask_shift(const int *same, int n){
    int best = 0;
    for(int s=1;s<n;s++){
        if(mask_rotation_better(same, n, s, best)) best = s;
    }
    return best;
}

static int group_add(Group *gs,int *gn,const char *key,int first){ for(int i=0;i<*gn;i++) if(strcmp(gs[i].key,key)==0){ gs[i].count++; return i;} if(*gn>=MAX_GROUPS_LOCAL) return -1; snprintf(gs[*gn].key,sizeof(gs[*gn].key),"%s",key); gs[*gn].count=1; gs[*gn].first=first; (*gn)++; return *gn-1; }


static int parse_pi_figure_cell(const char *cell, PIFigure *fig){
    const char *p = cell;
    fig->n = 0;
    while(*p){
        int sign = 1;
        int idx = 0;
        if(*p == '['){
            p++;
            if(*p == '+'){ sign = 1; p++; }
            else if(*p == '-'){ sign = -1; p++; }
            else { continue; }
            if(!isdigit((unsigned char)*p)) return 0;
            while(isdigit((unsigned char)*p)){ idx = idx * 10 + (*p - '0'); p++; }
            if(*p != ',') return 0;
            p++;
            int neg = 0;
            if(*p == '-'){ neg = 1; p++; }
            if(!isdigit((unsigned char)*p)) return 0;
            idx = 0;
            while(isdigit((unsigned char)*p)){ idx = idx * 10 + (*p - '0'); p++; }
            if(neg) idx = -idx;
            if(*p != ']') return 0;
            p++;
            if(fig->n >= RL0_FM_MAX_ITEMS) return 0;
            fig->pi[fig->n].p = sign;
            fig->pi[fig->n].i = idx;
            fig->n++;
        } else p++;
    }
    return fig->n > 0;
}

static int hex_label_from_negative(int idx){
    if(idx == 7) return HEX_LABEL_A;
    if(idx == 1) return HEX_LABEL_B;
    return idx;
}

static int split_label_for_edge(int x, int y){
    /* These are the two geometrically drawn split hexagon edges.
       The label placement is independent of the exposed negative marker
       used by lookup normalization. */
    if(x == 7 && y == 11) return HEX_LABEL_A;
    if(x == 1 && y == 5) return HEX_LABEL_B;
    return 0;
}

static int normalize_dominant_figure(const PIFigure *fig, NormFigure *out){
    int neg_pos = -1;
    int neg_count = 0;
    out->n = 0;
    if(fig->n != 3 && fig->n != 4) return 0;

    for(int i=0;i<fig->n;i++){
        if(fig->pi[i].p < 0){
            neg_pos = i;
            neg_count++;
        }
    }
    if(neg_count > 1) return 0;

    if(fig->n == 4){
        for(int i=0;i<fig->n;i++){
            if(fig->pi[i].p < 0) continue;
            if(out->n >= 3) return 0;
            out->v[out->n++] = fig->pi[i].i;
        }
        return out->n == 3;
    }

    if(neg_count == 1){
        int repl = hex_label_from_negative(fig->pi[neg_pos].i);
        if(repl != HEX_LABEL_A && repl != HEX_LABEL_B) return 0;
        out->v[out->n++] = repl;
        for(int step=1; step<fig->n; step++){
            int i = (neg_pos + step) % fig->n;
            if(fig->pi[i].p < 0) return 0;
            out->v[out->n++] = fig->pi[i].i;
        }
        return out->n == 3;
    }

    for(int i=0;i<fig->n;i++){
        if(fig->pi[i].p != 1) return 0;
        out->v[out->n++] = fig->pi[i].i;
    }
    return out->n == 3;
}

static void synthetic_norm_figure(int label, NormFigure *out){
    out->n = 3;
    out->v[0] = label;
    if(label == HEX_LABEL_A){
        out->v[1] = 1;
        out->v[2] = 3;
    } else {
        out->v[1] = 7;
        out->v[2] = 5;
    }
}

static int norm_pos(const NormFigure *f, int label){
    for(int i=0;i<f->n;i++) if(f->v[i] == label) return i;
    return -1;
}

static int norm_next(const NormFigure *f, int label, int *out){
    int p = norm_pos(f, label);
    if(p < 0 || f->n <= 0) return 0;
    *out = f->v[(p + 1) % f->n];
    return 1;
}

static int norm_prev(const NormFigure *f, int label, int *out){
    int p = norm_pos(f, label);
    if(p < 0 || f->n <= 0) return 0;
    *out = f->v[(p + f->n - 1) % f->n];
    return 1;
}

static void derive_hex_and_rules(HexItem *h, EdgeRule *rules, int *rule_count){
    int labels[MAX_VERTS + 2];
    NormFigure norms[MAX_VERTS + 2];
    int rn = 0;

    h->hex_n = 0;
    for(int k=0;k<h->fig_count;k++){
        PIFigure *A = &h->fig[k];
        PIFigure *B = &h->fig[(k + 1) % h->fig_count];
        NormFigure nf;
        int x, y, split = 0;
        if(A->n < 1) continue;
        x = A->pi[0].i;
        labels[rn] = x;
        if(!normalize_dominant_figure(A, &nf)) continue;
        norms[rn] = nf;
        rn++;
        if(rn >= MAX_VERTS + 2) break;

        if(h->fig_count == 5 && B->n > 0){
            y = B->pi[0].i;
            split = split_label_for_edge(x, y);
            if(split){
                labels[rn] = split;
                synthetic_norm_figure(split, &norms[rn]);
                rn++;
                if(rn >= MAX_VERTS + 2) break;
            }
        }
    }

    h->hex_n = rn;
    for(int i=0;i<rn;i++) h->hex[i] = labels[i];

    for(int i=0;i<rn;i++){
        int j = (i + 1) % rn;
        int x = labels[i];
        int y = labels[j];
        int a = 0, b = 0;
        if(!norm_next(&norms[j], y, &a)) continue;
        if(!norm_prev(&norms[i], x, &b)) continue;
        if(*rule_count + 1 < MAX_EDGE_RULES){
            rules[*rule_count].a = (SignedEdge){+1, x, y};
            rules[*rule_count].b = (SignedEdge){-1, a, b};
            (*rule_count)++;
        }
    }
}

static void canonicalize_hex_cycle(HexItem *h){
    if(h->hex_n <= 1) return;
    int best = -1;
    for(int i=0;i<h->hex_n;i++){
        if(h->hex[i] < 0) continue;
        if(best < 0 || h->hex[i] < h->hex[best]) best = i;
    }
    if(best <= 0) return;
    int tmp[MAX_VERTS + 2];
    for(int i=0;i<h->hex_n;i++) tmp[i] = h->hex[(best + i) % h->hex_n];
    for(int i=0;i<h->hex_n;i++) h->hex[i] = tmp[i];
}

static int hex_same(const HexItem *a, const HexItem *b){
    if(a->hex_n != b->hex_n) return 0;
    for(int i=0;i<a->hex_n;i++) if(a->hex[i] != b->hex[i]) return 0;
    return 1;
}

static int signed_edge_cmp(const void *A, const void *B){
    const SignedEdge *a = A;
    const SignedEdge *b = B;
    if(a->sign != b->sign) return b->sign - a->sign; /* + before - */
    if(a->x != b->x) return a->x - b->x;
    return a->y - b->y;
}

static int signed_edge_equal(SignedEdge a, SignedEdge b){
    return a.sign == b.sign && a.x == b.x && a.y == b.y;
}

static int edge_node_find_or_add(EdgeNode *nodes, int *node_count, SignedEdge e){
    for(int i=0;i<*node_count;i++) if(signed_edge_equal(nodes[i].token, e)) return i;
    if(*node_count >= MAX_EDGE_NODES) return -1;
    nodes[*node_count].token = e;
    nodes[*node_count].parent = *node_count;
    (*node_count)++;
    return *node_count - 1;
}

static int edge_find_root(EdgeNode *nodes, int i){
    while(nodes[i].parent != i){
        nodes[i].parent = nodes[nodes[i].parent].parent;
        i = nodes[i].parent;
    }
    return i;
}

static void edge_union(EdgeNode *nodes, int a, int b){
    int ra = edge_find_root(nodes, a);
    int rb = edge_find_root(nodes, b);
    if(ra != rb) nodes[rb].parent = ra;
}

static void print_label(int x){
    if(x == HEX_LABEL_A) printf("a");
    else if(x == HEX_LABEL_B) printf("b");
    else printf("%d", x);
}

static void print_signed_edge(SignedEdge e){
    printf("%ce(", e.sign >= 0 ? '+' : '-');
    print_label(e.x);
    printf(",");
    print_label(e.y);
    printf(")");
}

static void print_unique_hexagons(const HexItem *items, int item_count){
    int used[MAX_HEX_ITEMS];
    for(int i=0;i<item_count;i++) used[i]=0;
    for(int i=0;i<item_count;i++){
        if(used[i]) continue;
        used[i] = 1;
        printf("  ");
        for(int k=0;k<items[i].hex_n;k++){
            if(k) printf(" ");
            print_label(items[i].hex[k]);
        }
        printf("  # records");
        for(int j=i;j<item_count;j++){
            if(hex_same(&items[i], &items[j])){
                used[j] = 1;
                printf(" %d", items[j].record);
            }
        }
        printf("\n");
    }
}

static void print_edge_classes(const EdgeRule *rules, int rule_count){
    EdgeNode nodes[MAX_EDGE_NODES];
    int node_count = 0;
    for(int i=0;i<rule_count;i++){
        int a = edge_node_find_or_add(nodes, &node_count, rules[i].a);
        int b = edge_node_find_or_add(nodes, &node_count, rules[i].b);
        if(a >= 0 && b >= 0) edge_union(nodes, a, b);
    }
    for(int r=0;r<node_count;r++){
        SignedEdge toks[MAX_EDGE_NODES];
        int tc = 0;
        for(int i=0;i<node_count;i++){
            if(edge_find_root(nodes, i) == r) toks[tc++] = nodes[i].token;
        }
        if(tc < 2) continue;
        qsort(toks, (size_t)tc, sizeof(toks[0]), signed_edge_cmp);
        printf("  ");
        for(int i=0;i<tc;i++){
            if(i) printf(" = ");
            print_signed_edge(toks[i]);
        }
        printf("\n");
    }
}

static void print_full_header(void){
    printf("#\n");
    printf("# Hexagons as cyclic CCW lists of CCW vertex figures\n");
    printf("#     first of each vertex figure is interior to hexagon\n");
    printf("#     section labels ---[n:bits]--- use RL1 group number and per-position agreement mask\n");
    printf("#\n\n");
}



int main(int argc,char **argv){
    const char *data="data/rl1/completions.dat";
    int selected=0,n=0,gn=0,kept=0,dropped=0;
    Rec *recs=calloc(MAX_REC,sizeof(*recs));
    Info *infos=calloc(MAX_REC,sizeof(*infos));
    Group *groups=calloc(MAX_GROUPS_LOCAL,sizeof(*groups));
    HexItem *hex_items=calloc(MAX_HEX_ITEMS,sizeof(*hex_items));
    EdgeRule *edge_rules=calloc(MAX_EDGE_RULES,sizeof(*edge_rules));
    int hex_item_count = 0;
    int edge_rule_count = 0;
    Tile tile;

    if(argc>1) data=argv[1];
    if(argc>2) selected=atoi(argv[2]);
    if(argc>3){ fprintf(stderr,"usage: %s [data/rl1/completions.dat] [record_index]\n",argv[0]); return 1;}
    if(!recs||!infos||!groups||!hex_items||!edge_rules){fprintf(stderr,"alloc fail\n"); return 1;}
    if(!tile_load("tiles/hat.tile",&tile)){fprintf(stderr,"failed to load tiles/hat.tile\n"); return 1;}
    if(!load_recs(data,recs,&n)){fprintf(stderr,"failed to load %s\n",data); return 1;}
    if(selected<0||selected>n){fprintf(stderr,"record out of range\n"); return 1;}

    for(int i=0;i<n;i++){
        if(selected&&selected!=i+1) continue;
        classify(&tile,&recs[i],i+1,&infos[i]);
        if(infos[i].kept){
            kept++;
            if(group_add(groups,&gn,infos[i].key,i)<0){fprintf(stderr,"too many groups\n"); return 1;}
        } else if(infos[i].dropped) dropped++;
    }
    (void)kept;
    (void)dropped;

    print_full_header();

    for(int g=0; g<gn; g++){
        int emitted = 0;
        FigureSeq refseq;
        int have_ref = 0;
        int same[MAX_VERTS];
        int mask_len = 0;
        int group_shift = 0;
        char maskbuf[MAX_VERTS + 1];

        memset(&refseq, 0, sizeof(refseq));
        for(int k=0;k<MAX_VERTS;k++) same[k]=1;
        maskbuf[0] = '\0';

        for(int i=0;i<n;i++){
            IndexMap im;
            FigureSeq seq;
            if(selected&&selected!=i+1) continue;
            if(!infos[i].kept) continue;
            if(strcmp(infos[i].key, groups[g].key) != 0) continue;
            build_index_map(&tile,&recs[i],infos[i].center_tile,&im);
            build_figure_seq(&im,&seq);
            if(!have_ref){
                refseq = seq;
                mask_len = seq.n;
                have_ref = 1;
            } else {
                if(seq.n != mask_len){
                    int lim = seq.n < mask_len ? seq.n : mask_len;
                    for(int k=lim;k<mask_len;k++) same[k]=0;
                }
                for(int k=0;k<mask_len && k<seq.n;k++){
                    if(strcmp(seq.cell[k], refseq.cell[k]) != 0) same[k]=0;
                }
            }
        }

        if(have_ref && mask_len > 0) group_shift = best_mask_shift(same, mask_len);
        if(have_ref){
            for(int k=0;k<mask_len;k++) maskbuf[k] = same[(k+group_shift)%mask_len] ? '1' : '0';
            maskbuf[mask_len] = '\0';
        }

        printf("---[%d:%s]---\n", g+1, maskbuf);

        for(int i=0;i<n;i++){
            IndexMap im;
            FigureSeq seq;
            HexItem *hi;
            if(selected&&selected!=i+1) continue;
            if(!infos[i].kept) continue;
            if(strcmp(infos[i].key, groups[g].key) != 0) continue;
            build_index_map(&tile,&recs[i],infos[i].center_tile,&im);
            build_figure_seq(&im,&seq);
            for(int j=0;j<seq.n;j++){
                int pos = seq.n > 0 ? (j + group_shift) % seq.n : j;
                if(j) printf(" ");
                printf("%s", seq.cell[pos]);
            }
            printf("\n");
            emitted = 1;

            if(hex_item_count < MAX_HEX_ITEMS){
                hi = &hex_items[hex_item_count++];
                memset(hi, 0, sizeof(*hi));
                hi->group = g + 1;
                hi->mask_len = mask_len;
                snprintf(hi->mask, sizeof(hi->mask), "%s", maskbuf);
                hi->record = i + 1;
                for(int j=0;j<seq.n && j<MAX_VERTS;j++){
                    int pos = seq.n > 0 ? (j + group_shift) % seq.n : j;
                    if(parse_pi_figure_cell(seq.cell[pos], &hi->fig[hi->fig_count])) hi->fig_count++;
                }
                derive_hex_and_rules(hi, edge_rules, &edge_rule_count);
                canonicalize_hex_cycle(hi);
            }
        }
        if(!emitted) printf("\n");
        if(g+1<gn) printf("\n");
    }

    printf("\n#\n");
    printf("# Extraction to canonical vertex-label set\n");
    printf("#     fixed with symbolic a/b exposed vertices and normalized lookup\n");
    printf("#     item labels are original record indices from data/rl1/completions.dat\n");
    printf("#\n\n");

    for(int g=0; g<gn; g++){
        int any = 0;
        for(int i=0;i<hex_item_count;i++) if(hex_items[i].group == g + 1) any = 1;
        if(!any) continue;
        const char *gmask = "";
        for(int mi=0;mi<hex_item_count;mi++){
            if(hex_items[mi].group == g + 1){ gmask = hex_items[mi].mask; break; }
        }
        printf("---[%d:%s]---\n", g + 1, gmask);
        for(int i=0;i<hex_item_count;i++){
            if(hex_items[i].group != g + 1) continue;
            printf("  %d:", hex_items[i].record);
            for(int k=0;k<hex_items[i].hex_n;k++){ printf(" "); print_label(hex_items[i].hex[k]); }
            printf("\n");
        }
        printf("\n");
    }

    printf("#\n");
    printf("# Unique hexagons\n");
    printf("#\n\n");
    print_unique_hexagons(hex_items, hex_item_count);

    printf("\n#\n");
    printf("# Edge Rules\n");
    printf("#\n\n");
    print_edge_classes(edge_rules, edge_rule_count);

    free(recs); free(infos); free(groups); free(hex_items); free(edge_rules); return 0;
}
