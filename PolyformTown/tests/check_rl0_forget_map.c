#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/boundary.h"
#include "rl0/forget_map.h"
#include "rl0/boundary0.h"
#include "rl0/attach0.h"

#define MAX_REC_TILES 128
#define RL0_SOURCE_BLOCK_MAX 1048576

typedef struct {
    int records_checked;
    int records_failed;
    int vertices_missed;
} RL0SelfCheckStats;

typedef struct {
    int have_boundary;
    Poly boundary;
    int have_tiles;
    Cycle tiles[MAX_REC_TILES];
    int tile_count;
    int have_indices;
    int indices[MAX_REC_TILES];
    int index_count;
    int have_parities;
    int parities[MAX_REC_TILES];
    int parity_count;
    int file_record_no;
    int stream_record_no;
    char source_block[RL0_SOURCE_BLOCK_MAX];
    size_t source_len;
} RL0TestRecord;

static void assert_true(int cond, const char *msg){ if(!cond){fprintf(stderr,"FAIL: %s\n",msg); exit(1);} }

static void skip_ws(const char **pp){ while(isspace((unsigned char)**pp)) (*pp)++; }
static int parse_int0(const char **pp,int *out){ char *end=NULL; long v; skip_ws(pp); v=strtol(*pp,&end,10); if(end==*pp)return 0; *out=(int)v; *pp=end; return 1; }
static int expect_char(const char **pp,char ch){ skip_ws(pp); if(**pp!=ch)return 0; (*pp)++; return 1; }
static int parse_cycle0(const char **pp,Cycle *c){ c->n=0; if(!expect_char(pp,'['))return 0; skip_ws(pp); if(**pp==']'){(*pp)++; return 1;} while(c->n<MAX_VERTS){ Coord q; if(!expect_char(pp,'('))return 0; if(!parse_int0(pp,&q.v))return 0; if(!expect_char(pp,','))return 0; if(!parse_int0(pp,&q.x))return 0; if(!expect_char(pp,','))return 0; if(!parse_int0(pp,&q.y))return 0; if(!expect_char(pp,')'))return 0; c->v[c->n++]=q; skip_ws(pp); if(**pp==','){(*pp)++; continue;} if(**pp==']'){(*pp)++; return c->n>0;} return 0;} return 0; }
static int parse_poly0(const char *text, Poly *p){ const char *s=text; p->cycle_count=0; if(!expect_char(&s,'['))return 0; while(p->cycle_count<MAX_CYCLES){ if(!parse_cycle0(&s,&p->cycles[p->cycle_count]))return 0; p->cycle_count++; skip_ws(&s); if(*s=='|'){s++; continue;} if(*s==']'){s++; skip_ws(&s); return *s=='\0'||*s=='\n'||*s=='\r';} return 0;} return 0; }
static int parse_tile_list0(const char *text,Cycle *out,int *out_count){ const char *s=text; int count=0; if(!expect_char(&s,'['))return 0; skip_ws(&s); if(*s==']'){s++; *out_count=0; return 1;} while(count<MAX_REC_TILES){ if(!parse_cycle0(&s,&out[count]))return 0; count++; skip_ws(&s); if(*s==','){s++; continue;} if(*s==']'){s++; skip_ws(&s); if(*s!='\0'&&*s!='\n'&&*s!='\r')return 0; *out_count=count; return 1;} return 0;} return 0; }
static int parse_int_list0(const char *text,int *out,int *out_count){ const char *s=text; int count=0; if(!expect_char(&s,'['))return 0; skip_ws(&s); if(*s==']'){s++; *out_count=0; return 1;} while(count<MAX_REC_TILES){ if(!parse_int0(&s,&out[count]))return 0; count++; skip_ws(&s); if(*s==','){s++; continue;} if(*s==']'){s++; skip_ws(&s); if(*s!='\0'&&*s!='\n'&&*s!='\r')return 0; *out_count=count; return 1;} return 0;} return 0; }
static void reset_record(RL0TestRecord *r){ memset(r,0,sizeof(*r)); }

static void append_source_line(RL0TestRecord *r, const char *line){
    size_t n=strlen(line);
    if(r->source_len+n+1>=RL0_SOURCE_BLOCK_MAX){
        const char *trunc="\n[record source truncated]\n";
        size_t t=strlen(trunc);
        if(r->source_len+t+1<RL0_SOURCE_BLOCK_MAX){
            memcpy(r->source_block+r->source_len,trunc,t);
            r->source_len+=t;
            r->source_block[r->source_len]='\0';
        }
        return;
    }
    memcpy(r->source_block+r->source_len,line,n);
    r->source_len+=n;
    r->source_block[r->source_len]='\0';
}

static int parse_record_header_no(const char *line){
    const char *p=line;
    int n=0;
    if(strncmp(p,"---[",4)!=0) return 0;
    p+=4;
    if(!parse_int0(&p,&n)) return 0;
    return n;
}


static void test_basic_dictionary(void){
    RL0ForgetMap *map=calloc(1,sizeof(*map)); RL0FMCycle c; const RL0FMArc *values=NULL; int value_count=0; assert_true(map!=NULL,"allocate tiny map");
    rl0_fm_init(map); c.n=4; c.item[0].p=1;c.item[0].i=1; c.item[1].p=1;c.item[1].i=2; c.item[2].p=-1;c.item[2].i=3; c.item[3].p=1;c.item[3].i=4; map->cycles[map->cycle_count++]=c; assert_true(rl0_fm_build_from_cycles(map),"build tiny dictionary");
    RL0FMArc empty={0}; assert_true(rl0_fm_lookup(map,&empty,&values,&value_count),"empty basement lookup exists"); assert_true(value_count==1&&values[0].n==4,"empty lookup returns full cycle");
    RL0FMCycle canon; rl0_fm_canonicalize_cycle(&c,&canon); assert_true(rl0_fm_contains_complete(map,&canon),"complete ceiling lookup returns true");
    RL0FMArc kept; kept.n=2; kept.item[0]=c.item[3]; kept.item[1]=c.item[0]; assert_true(rl0_fm_lookup(map,&kept,&values,&value_count),"middle lookup exists"); assert_true(value_count>0,"middle lookup has completions"); free(map);
}


static void dump_record_failure(const RL0TestRecord *rec,
                                const Tile *tile,
                                const RL0ForgetMap *map,
                                const Boundary0Stats *stats){
    Coord verts[MAX_VERTS*MAX_CYCLES];
    int vc=build_boundary_vertices(&rec->boundary,verts);

    fprintf(stderr,
            "FAIL: RL0 file record %d (stream order %d) boundary0 live lookup failed\n",
            rec->file_record_no,
            rec->stream_record_no);
    fprintf(stderr,
            "  vertices_checked=%d dictionary_misses=%d max_incident=%d tile_count=%d\n",
            stats->vertices_checked,
            stats->dictionary_misses,
            stats->max_incident,
            rec->tile_count);

    fprintf(stderr,"\n--- reconstructed boundary lookup data, in boundary vertex order ---\n");
    for(int v=0; v<vc; v++){
        RL0FMArc arc;
        const RL0FMArc *values=NULL;
        int value_count=0;
        int lookup=0;
        if(!boundary0_build_vertex_arc(tile,rec->tiles,rec->tile_count,verts[v],&arc)){
            fprintf(stderr,
                    "  vertex (%d,%d,%d) arc:<none> lookup=0 values=0\n",
                    verts[v].v,verts[v].x,verts[v].y);
            continue;
        }
        lookup=rl0_fm_lookup_any_rotation(map,&arc,&values,&value_count,NULL);
        fprintf(stderr,
                "  vertex (%d,%d,%d) arc:",
                verts[v].v,verts[v].x,verts[v].y);
        for(int k=0;k<arc.n;k++) fprintf(stderr," [%d,%d]",arc.item[k].p,arc.item[k].i);
        fprintf(stderr," lookup=%d values=%d",lookup,value_count);
        if(!lookup) fprintf(stderr,"  <-- MISS");
        fprintf(stderr,"\n");
    }

    fprintf(stderr,"\n--- canonical RL0 source record follows ---\n");
    if(rec->source_len>0) fputs(rec->source_block,stderr);
    else fprintf(stderr,"[source block unavailable]\n");
    fprintf(stderr,"--- end RL0 source record ---\n");
}

static int check_record_live(const RL0TestRecord *rec,
                             const Tile *tile,
                             const RL0ForgetMap *map,
                             int dump_on_failure){
    Boundary0Stats stats; boundary0_stats_init(&stats);
    if(!rec->have_boundary||!rec->have_tiles||!rec->have_indices||!rec->have_parities) return 1;
    if(rec->tile_count!=rec->index_count||rec->tile_count!=rec->parity_count){
        fprintf(stderr,
                "FAIL: RL0 file record %d malformed counts tiles=%d indices=%d parities=%d\n",
                rec->file_record_no,
                rec->tile_count,
                rec->index_count,
                rec->parity_count);
        return 0;
    }
    if(!boundary0_poly_has_live_boundary(&rec->boundary,tile,rec->tiles,rec->tile_count,map,&stats)){
        if(dump_on_failure) dump_record_failure(rec,tile,map,&stats);
        return 0;
    }
    return 1;
}
static int record_deleted_through(const RL0TestRecord *rec,
                                  const RL0FMDeletionSet *deletions,
                                  int delete_through_level){
    RL0FMCycle c;
    if(!deletions || delete_through_level < 0) return 0;
    if(!rec->have_indices || !rec->have_parities) return 0;
    if(rec->index_count != rec->parity_count || rec->index_count <= 0) return 0;
    if(!rl0_fm_cycle_from_parity_indices(rec->parities,
                                         rec->indices,
                                         rec->index_count,
                                         &c)) return 0;
    return rl0_fm_deletions_contains_cycle(deletions, &c, delete_through_level);
}


typedef struct {
    int checked;
    int skipped;
    int failed;
    int fail_capacity;
    int *fail_record_no;
} RL0ScanResult;

static void scan_result_init(RL0ScanResult *r){ memset(r,0,sizeof(*r)); }

static int scan_result_add_failure(RL0ScanResult *r,int record_no){
    if(r->failed>=r->fail_capacity){
        int new_capacity=r->fail_capacity?r->fail_capacity*2:16;
        int *new_records=realloc(r->fail_record_no,(size_t)new_capacity*sizeof(r->fail_record_no[0]));
        if(!new_records) return 0;
        r->fail_record_no=new_records;
        r->fail_capacity=new_capacity;
    }
    r->fail_record_no[r->failed++]=record_no;
    return 1;
}

static int scan_rl0_records(const char *path,
                            const Tile *tile,
                            const RL0ForgetMap *map,
                            const RL0FMDeletionSet *skip,
                            int delete_through_level,
                            int dump_unexpected,
                            RL0ScanResult *result){
    FILE *fp=fopen(path,"r");
    char line[262144];
    RL0TestRecord rec;
    int stream_recno=0;
    assert_true(fp!=NULL,"open RL0 completions for self-check");
    scan_result_init(result);
    reset_record(&rec);
    while(fgets(line,sizeof(line),fp)){
        if(strncmp(line,"---[",4)==0){
            if(stream_recno>0){
                if(record_deleted_through(&rec,skip,delete_through_level)){
                    result->skipped++;
                } else {
                    result->checked++;
                    if(!check_record_live(&rec,tile,map,dump_unexpected)){
                        if(!scan_result_add_failure(result,rec.file_record_no)){ fclose(fp); return 0; }
                    }
                }
            }
            stream_recno++;
            reset_record(&rec);
            rec.stream_record_no=stream_recno;
            rec.file_record_no=parse_record_header_no(line);
            append_source_line(&rec,line);
            continue;
        }
        if(stream_recno<=0) continue;
        append_source_line(&rec,line);
        if(strncmp(line,"boundary:",9)==0){ rec.have_boundary=parse_poly0(line+9,&rec.boundary); continue; }
        if(strncmp(line,"tiles:",6)==0){ rec.have_tiles=parse_tile_list0(line+6,rec.tiles,&rec.tile_count); continue; }
        if(strncmp(line,"parities:",9)==0){ rec.have_parities=parse_int_list0(line+9,rec.parities,&rec.parity_count); continue; }
        if(strncmp(line,"indices:",8)==0){ rec.have_indices=parse_int_list0(line+8,rec.indices,&rec.index_count); continue; }
    }
    if(stream_recno>0){
        if(record_deleted_through(&rec,skip,delete_through_level)){
            result->skipped++;
        } else {
            result->checked++;
            if(!check_record_live(&rec,tile,map,dump_unexpected)){
                if(!scan_result_add_failure(result,rec.file_record_no)){ fclose(fp); return 0; }
            }
        }
    }
    fclose(fp);
    return 1;
}



typedef struct {
    int records_checked;
    int forced_vertices;
    int forced_attach_successes;
    int forced_attach_failures;
} RL0ForcedScan;

static void forced_scan_init(RL0ForcedScan *s){ memset(s,0,sizeof(*s)); }

static void scan_forced_record(const RL0TestRecord *rec,
                               const Tile *tile,
                               const RL0ForgetMap *map,
                               RL0ForcedScan *scan){
    Coord verts[MAX_VERTS*MAX_CYCLES];
    int vc;
    if(!rec->have_boundary||!rec->have_tiles) return;
    vc=build_boundary_vertices(&rec->boundary,verts);
    if(vc<0) return;
    scan->records_checked++;
    for(int v=0; v<vc; v++){
        RL0FMArc arc;
        const RL0FMArc *values=NULL;
        int value_count=0;
        if(!boundary0_build_vertex_arc(tile,rec->tiles,rec->tile_count,verts[v],&arc)) continue;
        if(!rl0_fm_lookup_any_rotation(map,&arc,&values,&value_count,NULL)) continue;
        if(value_count!=1 || values[0].n<=0) continue;
        scan->forced_vertices++;
        {
            Poly grown;
            Cycle out_tiles[MAX_REC_TILES];
            int out_tile_count=0;
            Attach0Stats astats;
            attach0_stats_init(&astats);
            if(attach0_try_attach_arc(&rec->boundary,
                                      tile,
                                      rec->tiles,
                                      rec->tile_count,
                                      verts[v],
                                      &values[0],
                                      &grown,
                                      out_tiles,
                                      &out_tile_count,
                                      &astats)){
                scan->forced_attach_successes++;
            } else {
                scan->forced_attach_failures++;
                fprintf(stderr,
                        "forced attach failed: RL0 record %d vertex (%d,%d,%d) arc",
                        rec->file_record_no,verts[v].v,verts[v].x,verts[v].y);
                for(int k=0;k<arc.n;k++) fprintf(stderr," [%d,%d]",arc.item[k].p,arc.item[k].i);
                fprintf(stderr," ->");
                for(int k=0;k<values[0].n;k++) fprintf(stderr," [%d,%d]",values[0].item[k].p,values[0].item[k].i);
                fprintf(stderr,"\n");
            }
        }
    }
}

static int scan_forced_rl0_records(const char *path,
                                   const Tile *tile,
                                   const RL0ForgetMap *map,
                                   const RL0FMDeletionSet *skip,
                                   int delete_through_level,
                                   RL0ForcedScan *forced){
    FILE *fp=fopen(path,"r");
    char line[262144];
    RL0TestRecord rec;
    int stream_recno=0;
    assert_true(fp!=NULL,"open RL0 completions for forced scan");
    forced_scan_init(forced);
    reset_record(&rec);
    while(fgets(line,sizeof(line),fp)){
        if(strncmp(line,"---[",4)==0){
            if(stream_recno>0 && !record_deleted_through(&rec,skip,delete_through_level)){
                scan_forced_record(&rec,tile,map,forced);
            }
            stream_recno++;
            reset_record(&rec);
            rec.stream_record_no=stream_recno;
            rec.file_record_no=parse_record_header_no(line);
            continue;
        }
        if(stream_recno<=0) continue;
        if(strncmp(line,"boundary:",9)==0){ rec.have_boundary=parse_poly0(line+9,&rec.boundary); continue; }
        if(strncmp(line,"tiles:",6)==0){ rec.have_tiles=parse_tile_list0(line+6,rec.tiles,&rec.tile_count); continue; }
        if(strncmp(line,"parities:",9)==0){ rec.have_parities=parse_int_list0(line+9,rec.parities,&rec.parity_count); continue; }
        if(strncmp(line,"indices:",8)==0){ rec.have_indices=parse_int_list0(line+8,rec.indices,&rec.index_count); continue; }
    }
    if(stream_recno>0 && !record_deleted_through(&rec,skip,delete_through_level)){
        scan_forced_record(&rec,tile,map,forced);
    }
    fclose(fp);
    return 1;
}



typedef struct {
    int records_checked;
    int records_failed;
    int forced_steps;
    int forced_vertices;
    int zero_choice_failures;
    int unresolved_vertices;
    int fail_capacity;
    int *fail_record_no;
} RL0ClosureScan;

static void closure_scan_init(RL0ClosureScan *s){ memset(s,0,sizeof(*s)); }

static int closure_scan_add_failure(RL0ClosureScan *s,int record_no){
    if(s->records_failed>=s->fail_capacity){
        int new_capacity=s->fail_capacity?s->fail_capacity*2:16;
        int *new_records=realloc(s->fail_record_no,(size_t)new_capacity*sizeof(s->fail_record_no[0]));
        if(!new_records) return 0;
        s->fail_record_no=new_records;
        s->fail_capacity=new_capacity;
    }
    s->fail_record_no[s->records_failed++]=record_no;
    return 1;
}

static void scan_closure_record(const RL0TestRecord *rec,
                                const Tile *tile,
                                const RL0ForgetMap *map,
                                RL0ClosureScan *scan){
    Poly p;
    Cycle tiles[MAX_REC_TILES];
    int tile_count;
    Attach0Stats astats;
    Attach0ClosureStats cstats;
    if(!rec->have_boundary||!rec->have_tiles) return;
    p = rec->boundary;
    tile_count = rec->tile_count;
    for(int i=0;i<tile_count;i++) tiles[i]=rec->tiles[i];
    attach0_stats_init(&astats);
    attach0_closure_stats_init(&cstats);
    scan->records_checked++;
    if(!attach0_force_live_closure(&p,
                                   tile,
                                   tiles,
                                   &tile_count,
                                   map,
                                   1024,
                                   &astats,
                                   &cstats)){
        (void)closure_scan_add_failure(scan,rec->file_record_no);
        fprintf(stderr,
                "forced closure failed: RL0 record %d forced_steps=%d zero=%d forced_fail=%d unresolved=%d\n",
                rec->file_record_no,
                cstats.closure_steps,
                cstats.zero_choice_failures,
                cstats.forced_failures,
                cstats.unresolved_vertices);
    }
    scan->forced_steps += cstats.closure_steps;
    scan->forced_vertices += cstats.forced_vertices;
    scan->zero_choice_failures += cstats.zero_choice_failures;
    scan->unresolved_vertices += cstats.unresolved_vertices;
}

static int scan_closure_rl0_records(const char *path,
                                    const Tile *tile,
                                    const RL0ForgetMap *map,
                                    const RL0FMDeletionSet *skip,
                                    int delete_through_level,
                                    RL0ClosureScan *closure){
    FILE *fp=fopen(path,"r");
    char line[262144];
    RL0TestRecord rec;
    int stream_recno=0;
    assert_true(fp!=NULL,"open RL0 completions for forced closure scan");
    closure_scan_init(closure);
    reset_record(&rec);
    while(fgets(line,sizeof(line),fp)){
        if(strncmp(line,"---[",4)==0){
            if(stream_recno>0 && !record_deleted_through(&rec,skip,delete_through_level)){
                scan_closure_record(&rec,tile,map,closure);
            }
            stream_recno++;
            reset_record(&rec);
            rec.stream_record_no=stream_recno;
            rec.file_record_no=parse_record_header_no(line);
            continue;
        }
        if(stream_recno<=0) continue;
        if(strncmp(line,"boundary:",9)==0){ rec.have_boundary=parse_poly0(line+9,&rec.boundary); continue; }
        if(strncmp(line,"tiles:",6)==0){ rec.have_tiles=parse_tile_list0(line+6,rec.tiles,&rec.tile_count); continue; }
        if(strncmp(line,"parities:",9)==0){ rec.have_parities=parse_int_list0(line+9,rec.parities,&rec.parity_count); continue; }
        if(strncmp(line,"indices:",8)==0){ rec.have_indices=parse_int_list0(line+8,rec.indices,&rec.index_count); continue; }
    }
    if(stream_recno>0 && !record_deleted_through(&rec,skip,delete_through_level)){
        scan_closure_record(&rec,tile,map,closure);
    }
    fclose(fp);
    return 1;
}


static void fprint_fm_cycle_test(FILE *fp, const RL0FMCycle *cycle){
    fprintf(fp,"[");
    for(int i=0;i<cycle->n;i++){
        if(i) fprintf(fp,",");
        fprintf(fp,"[%d,%d]",cycle->item[i].p,cycle->item[i].i);
    }
    fprintf(fp,"]");
}

static int scan_has_failure_record(const RL0ScanResult *scan,int record_no){
    for(int i=0;i<scan->failed;i++) if(scan->fail_record_no[i]==record_no) return 1;
    return 0;
}

static int write_level0_deletions_from_scan(const char *completions_path,
                                             const char *deletions_path,
                                             const RL0ScanResult *scan){
    FILE *in=fopen(completions_path,"r");
    FILE *out=NULL;
    char line[262144];
    RL0TestRecord rec;
    RL0FMCycle *cycles=NULL;
    int cycle_count=0;
    int cycle_capacity=0;
    int stream_recno=0;
    if(!in) return 0;
    reset_record(&rec);
    while(fgets(line,sizeof(line),in)){
        if(strncmp(line,"---[",4)==0){
            if(stream_recno>0 && scan_has_failure_record(scan,rec.file_record_no) &&
               rec.have_parities && rec.have_indices && rec.parity_count==rec.index_count){
                RL0FMCycle c;
                int dup=0;
                if(!rl0_fm_cycle_from_parity_indices(rec.parities,rec.indices,rec.parity_count,&c)){
                    fclose(in); return 0;
                }
                rl0_fm_canonicalize_cycle(&c,&c);
                for(int i=0;i<cycle_count;i++) if(rl0_fm_cycle_equal(&cycles[i],&c)) dup=1;
                if(!dup){
                    if(cycle_count>=cycle_capacity){
                        int new_capacity=cycle_capacity?cycle_capacity*2:16;
                        RL0FMCycle *new_cycles=realloc(cycles,(size_t)new_capacity*sizeof(cycles[0]));
                        if(!new_cycles){ fclose(in); free(cycles); return 0; }
                        cycles=new_cycles;
                        cycle_capacity=new_capacity;
                    }
                    cycles[cycle_count++]=c;
                }
            }
            stream_recno++;
            reset_record(&rec);
            rec.stream_record_no=stream_recno;
            rec.file_record_no=parse_record_header_no(line);
            continue;
        }
        append_source_line(&rec,line);
        if(strncmp(line,"boundary:",9)==0){ rec.have_boundary=parse_poly0(line+9,&rec.boundary); continue; }
        if(strncmp(line,"tiles:",6)==0){ rec.have_tiles=parse_tile_list0(line+6,rec.tiles,&rec.tile_count); continue; }
        if(strncmp(line,"indices:",8)==0){ rec.have_indices=parse_int_list0(line+8,rec.indices,&rec.index_count); continue; }
        if(strncmp(line,"parities:",9)==0){ rec.have_parities=parse_int_list0(line+9,rec.parities,&rec.parity_count); continue; }
    }
    if(stream_recno>0 && scan_has_failure_record(scan,rec.file_record_no) &&
       rec.have_parities && rec.have_indices && rec.parity_count==rec.index_count){
        RL0FMCycle c;
        int dup=0;
        if(!rl0_fm_cycle_from_parity_indices(rec.parities,rec.indices,rec.parity_count,&c)){
            fclose(in); return 0;
        }
        rl0_fm_canonicalize_cycle(&c,&c);
        for(int i=0;i<cycle_count;i++) if(rl0_fm_cycle_equal(&cycles[i],&c)) dup=1;
        if(!dup){
            if(cycle_count>=cycle_capacity){
                int new_capacity=cycle_capacity?cycle_capacity*2:16;
                RL0FMCycle *new_cycles=realloc(cycles,(size_t)new_capacity*sizeof(cycles[0]));
                if(!new_cycles){ fclose(in); free(cycles); return 0; }
                cycles=new_cycles;
                cycle_capacity=new_capacity;
            }
            cycles[cycle_count++]=c;
        }
    }
    fclose(in);
    out=fopen(deletions_path,"w");
    if(!out){ free(cycles); return 0; }
    fprintf(out,"# RL0 indexed vertex-figure deletions.\n");
    fprintf(out,"# Regenerated by check_rl0_forget_map because deletions.dat was missing.\n");
    fprintf(out,"# Reflections are generated at dictionary load time.\n\n");
    fprintf(out,"---[0]---\n");
    for(int i=0;i<cycle_count;i++){
        fprint_fm_cycle_test(out,&cycles[i]);
        fprintf(out,"\n");
    }
    fclose(out);
    return cycle_count>0;
}

static int raw_failures_are_expected(const RL0ScanResult *scan,
                                     const RL0FMDeletionSet *deletions){
    int expected[] = {25, 28, 36, 37, 38, 42};
    int expected_count = (int)(sizeof(expected) / sizeof(expected[0]));
    (void)deletions;
    if(scan->failed != expected_count) return 0;
    for(int i=0;i<expected_count;i++){
        if(scan->fail_record_no[i] != expected[i]) return 0;
    }
    return 1;
}

static void print_failure_list(const char *label,const RL0ScanResult *scan){
    printf("%s",label);
    if(scan->failed==0){ printf(" none\n"); return; }
    for(int i=0;i<scan->failed;i++){
        printf("%s%d",i?", ":" ",scan->fail_record_no[i]);
    }
    printf("\n");
}

int main(void){
    Tile tile;
    RL0ForgetMap *raw_map=calloc(1,sizeof(*raw_map));
    RL0ForgetMap *reduced_map=calloc(1,sizeof(*reduced_map));
    RL0FMDeletionSet deletions;
    RL0ScanResult raw_scan;
    RL0ScanResult reduced_scan;
    RL0ForcedScan forced_scan;
    RL0ClosureScan closure_scan;
    Attach0Stats attach_stats;

    test_basic_dictionary();
    assert_true(raw_map!=NULL&&reduced_map!=NULL,"allocate RL0 maps");
    assert_true(tile_load("tiles/hat.tile",&tile),"load hat tile");
    attach0_stats_init(&attach_stats);
    assert_true(attach0_verify_variant_order(&tile,&attach_stats),
                "tile variants preserve rotation/reversal order");

    rl0_fm_deletions_init(&deletions);

    rl0_fm_init(raw_map);
    assert_true(rl0_fm_load_completions(raw_map,"data/rl0/completions.dat"),
                "load raw RL0 completions");
    assert_true(rl0_fm_build_from_cycles(raw_map),"build raw RL0 forget map");
    assert_true(scan_rl0_records("data/rl0/completions.dat",
                                 &tile,
                                 raw_map,
                                 NULL,
                                 -1,
                                 0,
                                 &raw_scan),
                "scan raw RL0 records");
    print_failure_list("raw strict failures:",&raw_scan);

    if(!rl0_fm_load_deletions(&deletions,"data/rl0/deletions.dat")){
        assert_true(write_level0_deletions_from_scan("data/rl0/completions.dat",
                                                     "data/rl0/deletions.dat",
                                                     &raw_scan),
                    "regenerate missing RL0 deletions.dat from Level 0 scan");
        rl0_fm_deletions_init(&deletions);
        assert_true(rl0_fm_load_deletions(&deletions,"data/rl0/deletions.dat"),
                    "load regenerated RL0 deletions");
    }
    assert_true(deletions.count>0,"deletions file contains Level 0 cycles");
    assert_true(raw_failures_are_expected(&raw_scan,&deletions),
                "raw strict failures match Level 0 deletion seed records");

    rl0_fm_init(reduced_map);
    assert_true(rl0_fm_load_completions_filtered(reduced_map,
                                                 "data/rl0/completions.dat",
                                                 &deletions,
                                                 0),
                "load reduced RL0 completions");
    assert_true(rl0_fm_build_from_cycles(reduced_map),
                "build reduced RL0 forget map");
    assert_true(scan_rl0_records("data/rl0/completions.dat",
                                 &tile,
                                 reduced_map,
                                 &deletions,
                                 0,
                                 1,
                                 &reduced_scan),
                "scan reduced RL0 records");
    print_failure_list("post-deletion strict failures:",&reduced_scan);
    assert_true(reduced_scan.failed==0,
                "Level 0 deletions are a fixed point for strict RL0 self-check");
    assert_true(scan_forced_rl0_records("data/rl0/completions.dat",
                                        &tile,
                                        reduced_map,
                                        &deletions,
                                        0,
                                        &forced_scan),
                "scan reduced RL0 records for forced completions");
    assert_true(scan_closure_rl0_records("data/rl0/completions.dat",
                                         &tile,
                                         reduced_map,
                                         &deletions,
                                         0,
                                         &closure_scan),
                "scan reduced RL0 records with forced closure");

    printf("loaded %d raw canonical/reflected RL0 cycles; derived %d rows\n",
           raw_map->cycle_count,raw_map->row_count);
    printf("loaded %d reduced canonical/reflected RL0 cycles; derived %d rows\n",
           reduced_map->cycle_count,reduced_map->row_count);
    printf("Level 0 deletions: %d canonical/reflected cycles; remaining checked: %d; fixed point yes\n",
           deletions.count,reduced_scan.checked);
    printf("variant order checked: %d; failures: %d\n",
           attach_stats.variant_order_checked,attach_stats.variant_order_failures);
    printf("forced reduced RL0 scan: records=%d forced=%d attach_success=%d attach_fail=%d\n",
           forced_scan.records_checked,
           forced_scan.forced_vertices,
           forced_scan.forced_attach_successes,
           forced_scan.forced_attach_failures);
    assert_true(forced_scan.forced_attach_failures==0,
                "all reduced forced completions attach concretely");
    printf("forced closure scan: records=%d failures=%d forced_steps=%d forced_vertices=%d zero=%d unresolved=%d\n",
           closure_scan.records_checked,
           closure_scan.records_failed,
           closure_scan.forced_steps,
           closure_scan.forced_vertices,
           closure_scan.zero_choice_failures,
           closure_scan.unresolved_vertices);
    assert_true(closure_scan.records_failed==0,
                "forced live-dead closure has no new reduced RL0 failures");
    printf("rl0 deletion fixed-point tests passed\n");

    free(raw_map);
    free(reduced_map);
    return 0;
}
