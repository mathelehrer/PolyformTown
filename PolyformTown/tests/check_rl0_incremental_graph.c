#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/boundary.h"
#include "core/cycle.h"
#include "core/tile.h"
#include "rl0/attach0.h"
#include "rl0/boundary0.h"
#include "rl0/forget_map.h"

#define KEY_CAP 8192
#define DEFAULT_TILE "preferences/focus.tile"
#define DEFAULT_REMEMBRANCE "data/rl0/remembrance.dat"
#define DEFAULT_DELETIONS "data/rl0/deletions.dat"

typedef struct {
    int id, have_boundary, have_focus, outgoing, terminal_mark;
    int discover_step, division;
    Poly boundary;
    Coord focus;
    int tile_count;
    Cycle tiles[ATTACH0_MAX_TILES];
    char *key;
} Node;

typedef struct { int src, dst; } GraphEdge;
typedef struct { Node *nodes; int count, cap; GraphEdge *edges; int edge_count, edge_cap; char **keys; int key_count, key_cap; } Graph;

static char *xstrdup(const char *s){ size_t n=strlen(s)+1; char *p=malloc(n); if(p) memcpy(p,s,n); return p; }
static void skip_ws(const char **p){ while(isspace((unsigned char)**p)) (*p)++; }
static int parse_int(const char **p,int *out){ char *e=NULL; long v; skip_ws(p); v=strtol(*p,&e,10); if(e==*p) return 0; *out=(int)v; *p=e; return 1; }
static int expect(const char **p,char ch){ skip_ws(p); if(**p!=ch) return 0; (*p)++; return 1; }
static int parse_coord_comma(const char **p, Coord *q){ return expect(p,'(')&&parse_int(p,&q->v)&&expect(p,',')&&parse_int(p,&q->x)&&expect(p,',')&&parse_int(p,&q->y)&&expect(p,')'); }

static int parse_tile_cycle(const char *text, Cycle *c){
    const char *p=text; c->n=0; if(!expect(&p,'(')) return 0; skip_ws(&p);
    while(*p && *p!=')' && c->n<MAX_VERTS){ Coord q; if(!parse_int(&p,&q.v)||!parse_int(&p,&q.x)||!parse_int(&p,&q.y)) return 0; c->v[c->n++]=q; skip_ws(&p); if(*p==',') p++; skip_ws(&p); }
    return expect(&p,')') && c->n>0;
}

static int parse_display_poly(const char *text, Poly *poly){
    const char *p=strstr(text,"poly{"); if(!p) return 0; p+=5; poly->cycle_count=0;
    while(*p && *p!='}'){
        char *end=NULL; Cycle *c; if(*p!='c') return 0; p++; (void)strtol(p,&end,10); if(end==p||*end!=':') return 0; p=end+1;
        if(poly->cycle_count>=MAX_CYCLES) return 0;
        c=&poly->cycles[poly->cycle_count];
        c->n=0;
        while(*p=='('){ Coord q; if(c->n>=MAX_VERTS || !parse_coord_comma(&p,&q)) return 0; c->v[c->n++]=q; }
        if(*p!=';' || c->n<=0) return 0;
        p++;
        poly->cycle_count++;
    }
    return *p=='}' && poly->cycle_count>0;
}

static void poly_to_key(const Poly *p, char *buf, size_t cap){
    Poly key; size_t off=0; poly_hash_key_lattice(p,TILE_LATTICE_TETRILLE,&key);
    off+=(size_t)snprintf(buf+off,cap-off,"[");
    for(int c=0;c<key.cycle_count && off<cap;c++){
        if(c) off+=(size_t)snprintf(buf+off,cap-off,"|");
        off+=(size_t)snprintf(buf+off,cap-off,"[");
        for(int i=0;i<key.cycles[c].n && off<cap;i++){
            if(i) off+=(size_t)snprintf(buf+off,cap-off,",");
            off+=(size_t)snprintf(buf+off,cap-off,"(%d,%d,%d)",key.cycles[c].v[i].v,key.cycles[c].v[i].x,key.cycles[c].v[i].y);
        }
        off+=(size_t)snprintf(buf+off,cap-off,"]");
    }
    snprintf(buf+off,cap-off,"]");
}

static int add_node(Graph *g,int id,Node **out){
    if(g->count==g->cap){ int nc=g->cap?2*g->cap:64; Node *nn=realloc(g->nodes,(size_t)nc*sizeof(*nn)); if(!nn) return 0; g->nodes=nn; g->cap=nc; }
    *out=&g->nodes[g->count++]; memset(*out,0,sizeof(**out)); (*out)->id=id; (*out)->discover_step=-999999; (*out)->division=-999999; return 1;
}
static int find_node(const Graph *g,int id){ for(int i=0;i<g->count;i++) if(g->nodes[i].id==id) return i; return -1; }
static int key_exists(const Graph *g,const char *key){ for(int i=0;i<g->key_count;i++) if(strcmp(g->keys[i],key)==0) return 1; return 0; }
static int add_edge(Graph *g,int src,int dst){ if(g->edge_count==g->edge_cap){ int nc=g->edge_cap?2*g->edge_cap:128; GraphEdge *ne=realloc(g->edges,(size_t)nc*sizeof(*ne)); if(!ne) return 0; g->edges=ne; g->edge_cap=nc; } g->edges[g->edge_count].src=src; g->edges[g->edge_count].dst=dst; g->edge_count++; return 1; }
static int add_key(Graph *g,const char *key){ if(key_exists(g,key)) return 1; if(g->key_count==g->key_cap){ int nc=g->key_cap?2*g->key_cap:128; char **nk=realloc(g->keys,(size_t)nc*sizeof(*nk)); if(!nk) return 0; g->keys=nk; g->key_cap=nc; } g->keys[g->key_count]=xstrdup(key); return g->keys[g->key_count++]!=NULL; }

static int read_stream(Graph *g){
    char line[262144]; Node *cur=NULL; memset(g,0,sizeof(*g));
    while(fgets(line,sizeof(line),stdin)){
        if(strncmp(line,"---node ",8)==0){ int id=-1; if(sscanf(line,"---node %d---",&id)!=1) return 0; if(!add_node(g,id,&cur)) return 0; continue; }
        if(strncmp(line,"---end-node---",14)==0){ if(cur && cur->key) add_key(g,cur->key); cur=NULL; continue; }
        if(cur){
            if(strncmp(line,"tile: ",6)==0){ if(cur->tile_count>=ATTACH0_MAX_TILES) return 0; if(!parse_tile_cycle(line+6,&cur->tiles[cur->tile_count])) return 0; cur->tile_count++; }
            else if(strncmp(line,"key: ",5)==0){ char *p=line+5; p[strcspn(p,"\r\n")]='\0'; cur->key=xstrdup(p); if(!cur->key) return 0; }
            else if(strncmp(line,"display_key: ",13)==0){ if(!parse_display_poly(line+13,&cur->boundary)) return 0; cur->have_boundary=1; }
            else if(strncmp(line,"discover_step: ",15)==0){ cur->discover_step=atoi(line+15); }
            else if(strncmp(line,"division: ",10)==0){ cur->division=atoi(line+10); }
            continue;
        }
        if(strncmp(line,"---focus---",11)==0){ int node=-1,hn=0,hp=0; Coord port={0,0,0};
            while(fgets(line,sizeof(line),stdin)){ if(strncmp(line,"---end-focus---",15)==0) break; if(strncmp(line,"node: ",6)==0){ if(sscanf(line+6,"%d",&node)==1) hn=1; } else if(strncmp(line,"port: ",6)==0){ const char *p=line+6; if(!parse_coord_comma(&p,&port)) return 0; hp=1; } }
            if(hn&&hp){ int idx=find_node(g,node); if(idx>=0){ g->nodes[idx].focus=port; g->nodes[idx].have_focus=1; } }
            continue;
        }
        if(strncmp(line,"---node-mark---",15)==0){ int node=-1; int terminal=0;
            while(fgets(line,sizeof(line),stdin)){
                if(strncmp(line,"---end-node-mark---",19)==0) break;
                if(strncmp(line,"id: ",4)==0) sscanf(line+4,"%d",&node);
                else if(strncmp(line,"status: prune:",14)==0 || strncmp(line,"status: escape:",15)==0) terminal=1;
            }
            if(node>=0 && terminal){ int idx=find_node(g,node); if(idx>=0) g->nodes[idx].terminal_mark=1; }
            continue;
        }
        if(strncmp(line,"---edge---",10)==0){ int src=-1,dst=-1; while(fgets(line,sizeof(line),stdin)){ if(strncmp(line,"---end-edge---",14)==0) break; if(strncmp(line,"src: ",5)==0) sscanf(line+5,"%d",&src); else if(strncmp(line,"dst: ",5)==0) sscanf(line+5,"%d",&dst); } if(src>=0){ int idx=find_node(g,src); if(idx>=0) g->nodes[idx].outgoing++; } if(src>=0&&dst>=0&&!add_edge(g,src,dst)) return 0; continue; }
    }
    return 1;
}



typedef enum { NODE_ACCEPT_DEAD, NODE_ACCEPT_KNOWN, NODE_UNEXPECTED, NODE_SKIPPED } NodeResult;


static int mark_reduced_suppressed(const Graph *g, unsigned char *suppress, int *visible_nodes, int *suppressed_count){
    int n=g->count; int *out=calloc((size_t)n,sizeof(int));
    if(!out||!suppress){ free(out); return 0; }
    memset(suppress,0,(size_t)n);
    for(int e=0;e<g->edge_count;e++){
        int si=find_node(g,g->edges[e].src), di=find_node(g,g->edges[e].dst);
        if(si>=0&&di>=0&&si!=di) out[si]++;
    }
    for(int i=0;i<n;i++){
        if(out[i]!=0 || !g->nodes[i].key) continue;
        for(int j=0;j<n;j++){
            if(i==j || !g->nodes[j].key) continue;
            if(strcmp(g->nodes[i].key,g->nodes[j].key)!=0) continue;
            if(out[j]>0 || j<i){ suppress[i]=1; break; }
        }
    }
    if(visible_nodes) *visible_nodes=0;
    if(suppressed_count) *suppressed_count=0;
    for(int i=0;i<n;i++){
        if(suppress[i]){ if(suppressed_count) (*suppressed_count)++; }
        else if(visible_nodes) (*visible_nodes)++;
    }
    free(out);
    return 1;
}

static int push_level_unique(int **levels,int *count,int *capacity,int level){
    for(int i=0;i<*count;i++) if((*levels)[i]==level) return 1;
    if(*count>=*capacity){
        int new_capacity=*capacity?*capacity*2:8;
        int *new_levels=realloc(*levels,(size_t)new_capacity*sizeof((*levels)[0]));
        if(!new_levels) return 0;
        *levels=new_levels;
        *capacity=new_capacity;
    }
    (*levels)[(*count)++]=level;
    return 1;
}

static int collect_delete_levels(const RL0FMDeletionSet *dels,int **levels,int *level_count,int *level_capacity){
    for(int i=0;i<dels->count;i++) if(!push_level_unique(levels,level_count,level_capacity,dels->level[i])) return 0;
    for(int i=0;i<*level_count;i++) for(int j=i+1;j<*level_count;j++) if((*levels)[j]<(*levels)[i]){ int t=(*levels)[i]; (*levels)[i]=(*levels)[j]; (*levels)[j]=t; }
    if(*level_count==0 && !push_level_unique(levels,level_count,level_capacity,0)) return 0;
    return 1;
}

static int parse_level_filter(const char *text,int **levels,int *count,int *capacity){
    const char *p=text;
    while(*p){
        char *end=NULL; long v;
        while(*p==','||isspace((unsigned char)*p)) p++;
        if(!*p) break;
        v=strtol(p,&end,10);
        if(end==p) return 0;
        if(!push_level_unique(levels,count,capacity,(int)v)) return 0;
        p=end;
        while(*p&&*p!=','){ if(!isspace((unsigned char)*p)) return 0; p++; }
    }
    return *count>0;
}

static NodeResult check_node_at_level(const Graph *g,const Node *n,const Tile *tile,const RL0ForgetMap *map,int examples,int level,int *raw,int *dead,int *known,int *unexpected){
    RL0FMArc arc; const RL0FMArc *vals=NULL; int val_count=0; Attach0Stats astats; Attach0ClosureStats cstats; int any_unexpected=0, any_known=0, any_raw=0;
    attach0_stats_init(&astats); attach0_closure_stats_init(&cstats);
    *raw=*dead=*known=*unexpected=0;
    if(!n->have_boundary || !n->have_focus || n->tile_count<=0) return NODE_SKIPPED;
    if(!boundary0_build_vertex_arc(tile,n->tiles,n->tile_count,n->focus,&arc) || !rl0_fm_lookup_any_rotation(map,&arc,&vals,&val_count,NULL) || val_count<=0){
        (*dead)++;
        return NODE_ACCEPT_DEAD;
    }
    for(int k=0;k<val_count;k++){
        Poly poly; Cycle out_tiles[ATTACH0_MAX_TILES]; int out_tile_count=0; char key[KEY_CAP];
        if(vals[k].n<=0) continue;
        any_raw=1; (*raw)++;
        if(!attach0_try_attach_arc(&n->boundary,tile,n->tiles,n->tile_count,n->focus,&vals[k],&poly,out_tiles,&out_tile_count,&astats)){
            (*dead)++; continue;
        }
        /* No hidden-bound shortcut here.  The only cap is the fixed tile storage;
           closure cannot add more than ATTACH0_MAX_TILES tiles. */
        if(!attach0_force_live_closure(&poly,tile,out_tiles,&out_tile_count,map,ATTACH0_MAX_TILES,&astats,&cstats)){
            (*dead)++; continue;
        }
        poly_to_key(&poly,key,sizeof(key));
        if(key_exists(g,key)){
            any_known=1; (*known)++;
        }else{
            any_unexpected=1; (*unexpected)++;
            if(*unexpected<=examples) printf("unexpected image: node=%d step=%d division=%d level=%d branch=%d focus=(%d,%d,%d) key=%s\n",n->id,n->discover_step,n->division,level,k,n->focus.v,n->focus.x,n->focus.y,key);
        }
    }
    if(!any_raw || (!any_known && !any_unexpected)) return NODE_ACCEPT_DEAD;
    if(any_unexpected) return NODE_UNEXPECTED;
    return NODE_ACCEPT_KNOWN;
}

int main(int argc,char **argv){
    const char *tile_path=DEFAULT_TILE,*rem_path=DEFAULT_REMEMBRANCE,*del_path=DEFAULT_DELETIONS; int examples=0; int *explicit_levels=NULL; int explicit_level_count=0; int explicit_level_cap=0;
    Tile tile; Graph graph; RL0FMDeletionSet *dels=calloc(1,sizeof(*dels)); int *levels=NULL; int level_count=0; int level_cap=0; RL0ForgetMap *map=calloc(1,sizeof(*map));
    unsigned char *pending=NULL; unsigned char *accepted_kind=NULL; unsigned char *suppressed=NULL; int *accepted_level_index=NULL; int reduced_nodes=0, reduced_suppressed=0;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--tile")==0&&i+1<argc) tile_path=argv[++i];
        else if(strcmp(argv[i],"--remembrance")==0&&i+1<argc) rem_path=argv[++i];
        else if(strcmp(argv[i],"--deletions")==0&&i+1<argc) del_path=argv[++i];
        else if(strcmp(argv[i],"--levels")==0&&i+1<argc){ if(!parse_level_filter(argv[++i],&explicit_levels,&explicit_level_count,&explicit_level_cap)){ fprintf(stderr,"bad --levels list\n"); return 2; } }
        else if(strcmp(argv[i],"--examples")==0&&i+1<argc) examples=atoi(argv[++i]);
        else { fprintf(stderr,"usage: %s [--levels 0,4,5] [--examples N] < graph.print\n",argv[0]); return 2; }
    }
    if(!dels||!map){ fprintf(stderr,"out of memory\n"); return 2; }
    if(!tile_load(tile_path,&tile)){ fprintf(stderr,"failed to load tile: %s\n",tile_path); return 2; }
    /* Consume all stdin before reading deletions.dat; in a pipe rl0_refine may still be writing later deletion sections. */
    if(!read_stream(&graph)){ fprintf(stderr,"failed to parse graph stream\n"); return 2; }
    if(!rl0_fm_load_deletions(dels,del_path)){ fprintf(stderr,"failed to load deletions: %s\n",del_path); return 2; }
    if(explicit_level_count>0){
        levels=explicit_levels;
        level_count=explicit_level_count;
        level_cap=explicit_level_cap;
        explicit_levels=NULL;
        explicit_level_count=explicit_level_cap=0;
    }else if(!collect_delete_levels(dels,&levels,&level_count,&level_cap)){ fprintf(stderr,"failed to collect deletion levels\n"); return 2; }

    pending=calloc((size_t)graph.count,1);
    accepted_kind=calloc((size_t)graph.count,1);
    suppressed=calloc((size_t)graph.count,1);
    accepted_level_index=malloc((size_t)graph.count*sizeof(*accepted_level_index));
    if(!pending||!accepted_kind||!suppressed||!accepted_level_index){ fprintf(stderr,"out of memory\n"); return 2; }
    if(!mark_reduced_suppressed(&graph,suppressed,&reduced_nodes,&reduced_suppressed)){ fprintf(stderr,"failed to mark reduced graph suppressions\n"); return 2; }
    for(int i=0;i<graph.count;i++){ pending[i]=(graph.nodes[i].have_focus && !suppressed[i] && !(graph.nodes[i].terminal_mark && graph.nodes[i].outgoing==0))?1:0; accepted_level_index[i]=-1; }

    int nodes_with_focus=0, skipped=0, accepted=0, accepted_dead=0, accepted_known=0, failed=0;
    int raw_total=0, dead_total=0, known_total=0, unexpected_total=0;
    int *accepted_by_level=calloc((size_t)level_count,sizeof(*accepted_by_level));
    if(!accepted_by_level){ fprintf(stderr,"out of memory\n"); return 2; }
    for(int i=0;i<graph.count;i++){
        if(graph.nodes[i].have_focus && !suppressed[i]){
            nodes_with_focus++;
            if(graph.nodes[i].terminal_mark && graph.nodes[i].outgoing==0){ accepted++; accepted_dead++; }
        }else skipped++;
    }

    printf("incremental_graph_check_levels:");
    for(int l=0;l<level_count;l++) printf(" %d",levels[l]);
    printf("\n");

    for(int l=0;l<level_count;l++){
        rl0_fm_clear(map);
        if(!rl0_fm_load_remembrance_filtered(map,rem_path,dels,levels[l])){ fprintf(stderr,"failed to load remembrance at delete level %d: %s\n",levels[l],rem_path); return 2; }
        for(int i=0;i<graph.count;i++){
            int raw=0,dead=0,known=0,unexpected=0;
            NodeResult r;
            if(!pending[i]) continue;
            r=check_node_at_level(&graph,&graph.nodes[i],&tile,map,examples,levels[l],&raw,&dead,&known,&unexpected);
            raw_total+=raw; dead_total+=dead; known_total+=known; unexpected_total+=unexpected;
            if(r==NODE_SKIPPED){ pending[i]=0; skipped++; continue; }
            if(r==NODE_ACCEPT_DEAD || r==NODE_ACCEPT_KNOWN){
                pending[i]=0; accepted++; accepted_by_level[l]++; accepted_level_index[i]=l;
                if(r==NODE_ACCEPT_DEAD){ accepted_dead++; accepted_kind[i]=1; }
                else { accepted_known++; accepted_kind[i]=2; }
            }
        }
    }

    for(int i=0;i<graph.count;i++){
        if(!pending[i]) continue;
        failed++;
        printf("failed node: node=%d step=%d division=%d focus=(%d,%d,%d)\n",graph.nodes[i].id,graph.nodes[i].discover_step,graph.nodes[i].division,graph.nodes[i].focus.v,graph.nodes[i].focus.x,graph.nodes[i].focus.y);
    }
    int escalated=0;
    for(int i=0;i<graph.count;i++) if(accepted_level_index[i]>0) escalated++;
    printf("incremental_graph_check: nodes=%d keys=%d reduced_nodes=%d reduced_suppressed=%d focus_nodes=%d accepted=%d accepted_dead=%d accepted_known=%d escalated=%d failed=%d skipped=%d raw_growths=%d dead_growths=%d known_images=%d unexpected_images=%d levels=%d\n",graph.count,graph.key_count,reduced_nodes,reduced_suppressed,nodes_with_focus,accepted,accepted_dead,accepted_known,escalated,failed,skipped,raw_total,dead_total,known_total,unexpected_total,level_count);
    printf("incremental_graph_check_accept_by_level:");
    for(int l=0;l<level_count;l++) printf(" %d:%d",levels[l],accepted_by_level[l]);
    printf("\n");
    return failed?1:0;
}
