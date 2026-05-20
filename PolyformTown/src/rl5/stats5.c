#include "rl5/hex5.h"
#include "core/term_color.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *name;
    const char *path;
} Job;

#define STATS5_MAX_SELECT 4096

typedef struct {
    int selected[STATS5_MAX_SELECT];
    int count;
    int all;
} Selection5;

typedef struct {
    const char *only_model;
    int boundary_probe;
    int boundary_depth;
    int max_states;
    int print_table;
    Selection5 sel;
} Stats5Options;

#define RL5_DEFAULT_MAX_STATES 1000000

static int ring_cmp_stats(const Ring5 *a, const Ring5 *b){
    if(a->center != b->center) return a->center - b->center;
    for(int i=0;i<HEX5_SIDES;i++) if(a->r[i] != b->r[i]) return a->r[i] - b->r[i];
    return 0;
}

static int ring_set_contains_stats(const BComp5Result *b, const Ring5 *r){
    for(int i=0;i<b->nrings;i++) if(ring_cmp_stats(&b->rings[i], r) == 0) return 1;
    return 0;
}

static int ring_set_missing_stats(const BComp5Result *needles, const BComp5Result *haystack){
    int n = 0;
    for(int i=0;i<needles->nrings;i++) if(!ring_set_contains_stats(haystack, &needles->rings[i])) n++;
    return n;
}


static const char *boundary_status_name(const Boundary5DepthCounts *c){
    if(c->escape > 0) return "escaped";
    if(c->live > 0) return "live";
    return "dead";
}

static int boundary_has_escape(const Boundary5DepthCounts *c){
    return c->escape > 0;
}

static void boundary_print_header(void){
    printf("%6s  %-7s %10s %7s %9s %10s %10s\n",
           "record", "status", "dfs", "esc",
           "max_tiles", "max_hidden", "max_bverts");
}

static void boundary_print_row(size_t record, const Boundary5DepthCounts *c){
    const char *status = boundary_status_name(c);
    printf("%6zu  %s%-7s%s %10ld %7d %9d %10d %10d\n",
           record,
           term_color_word(stdout, status),
           status,
           term_color_reset_for_word(stdout, status),
           c->processed,
           boundary_has_escape(c),
           c->max_cells,
           c->max_frontier,
           c->max_cycle);
}

static void trim5(char *s){
    char *p = s;
    size_t n;
    while(*p && isspace((unsigned char)*p)) p++;
    if(p != s) memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while(n && isspace((unsigned char)s[n-1])) s[--n] = '\0';
}

static int inner_cmp_raw(const Inner5 *x, const Inner5 *y){
    for(int i=0;i<HEX5_SIDES;i++){
        int c = strcmp(x->a[i], y->a[i]); if(c) return c;
        c = strcmp(x->b[i], y->b[i]); if(c) return c;
    }
    return 0;
}

static void inner_canon(const Inner5 *in, Inner5 *out){
    Inner5 best, c;
    best = *in;
    for(int k=1;k<HEX5_SIDES;k++){
        for(int i=0;i<HEX5_SIDES;i++){
            snprintf(c.a[i], HEX5_TOK, "%s", in->a[(i+k)%HEX5_SIDES]);
            snprintf(c.b[i], HEX5_TOK, "%s", in->b[(i+k)%HEX5_SIDES]);
        }
        if(inner_cmp_raw(&c, &best) < 0) best = c;
    }
    *out = best;
}

static int inner_set_add(Inner5Set *s, const Inner5 *in){
    Inner5 c;
    inner_canon(in, &c);
    for(int i=0;i<s->nrows;i++) if(inner_cmp_raw(&s->rows[i], &c) == 0) return 1;
    if(s->nrows >= HEX5_MAX_INNER) return 0;
    s->rows[s->nrows++] = c;
    return 1;
}

static int inner_set_contains(const Inner5Set *s, const Inner5 *in){
    Inner5 c;
    inner_canon(in, &c);
    for(int i=0;i<s->nrows;i++) if(inner_cmp_raw(&s->rows[i], &c) == 0) return 1;
    return 0;
}

static int inner_set_missing(const Inner5Set *needles, const Inner5Set *haystack){
    int n = 0;
    for(int i=0;i<needles->nrows;i++) if(!inner_set_contains(haystack, &needles->rows[i])) n++;
    return n;
}

static int parse_edge_list(const char *p, char out[HEX5_SIDES][HEX5_TOK]){
    int n = 0;
    while((p = strstr(p, "e(")) && n < HEX5_SIDES){
        const char *q = strchr(p, ')');
        int len;
        p += 2;
        if(!q) break;
        len = (int)(q - p);
        if(len <= 0 || len >= HEX5_TOK) return 0;
        memcpy(out[n], p, (size_t)len);
        out[n][len] = '\0';
        n++;
        p = q + 1;
    }
    return n == HEX5_SIDES;
}

static int parse_ordered_cycle_pair(const char *inner_line, const char *outer_line, Inner5 *out){
    const char *inner = strstr(inner_line, "inner [");
    const char *outer = strstr(outer_line, "outer [");
    if(!inner || !outer) return 0;
    return parse_edge_list(inner, out->a) && parse_edge_list(outer, out->b);
}

static int extracted_ordered_inner(const char *path, Inner5Set *s, int *records_seen, int *section_seen){
    FILE *f = fopen(path, "r");
    char line[4096];
    char pending_inner[4096];
    int in = 0, have_inner = 0;
    s->nrows = 0;
    if(records_seen) *records_seen = 0;
    if(section_seen) *section_seen = 0;
    if(!f) return 0;
    pending_inner[0] = '\0';
    while(fgets(line, sizeof(line), f)){
        char tmp[4096];
        snprintf(tmp, sizeof(tmp), "%s", line);
        trim5(tmp);
        if(strcmp(tmp, "# Inner/Outer Edge Cycles") == 0 || strcmp(tmp, "Inner/Outer Edge Cycles") == 0){
            in = 1;
            have_inner = 0;
            if(section_seen) *section_seen = 1;
            continue;
        }
        if(in && (strncmp(tmp, "Summary", 7) == 0 || strncmp(tmp, "# inner_outer_cycles=", 21) == 0)) break;
        if(!in || !*tmp) continue;
        if(strncmp(tmp, "inner [", 7) == 0){
            snprintf(pending_inner, sizeof(pending_inner), "%s", tmp);
            have_inner = 1;
            continue;
        }
        if(strncmp(tmp, "outer [", 7) == 0){
            Inner5 row;
            if(!have_inner){ fclose(f); return 0; }
            if(!parse_ordered_cycle_pair(pending_inner, tmp, &row)){ fclose(f); return 0; }
            if(!inner_set_add(s, &row)){ fclose(f); return 0; }
            if(records_seen) (*records_seen)++;
            have_inner = 0;
            continue;
        }
    }
    fclose(f);
    return 1;
}

static int run_one(const Job *j, const Stats5Options *opts){
    Hex5Model *m = calloc(1, sizeof(*m));
    Attach5Dict *a = calloc(1, sizeof(*a));
    VComp5Dict *v = calloc(1, sizeof(*v));
    BComp5Result *b = calloc(1, sizeof(*b));
    BComp5Result *bg = calloc(1, sizeof(*bg));
    Inner5Set *gen = calloc(1, sizeof(*gen));
    Inner5Set *geng = calloc(1, sizeof(*geng));
    Inner5Set *ext = calloc(1, sizeof(*ext));
    int ok = 0, records_seen = 0, section_seen = 0;
    int missing_ext, extra_gen;
    int boundary_dead = 0, boundary_escape = 0;
    int ring_missing = -1, ring_extra = -1, io_missing = -1, io_extra = -1;
    if(!m || !a || !v || !b || !bg || !gen || !geng || !ext) goto done;
    if(!hex5_parse_file(j->path, m)){
        fprintf(stderr, "rl5_stats: failed to parse %s\n", j->path);
        goto done;
    }
    if(!attach5_build(m, a)){
        fprintf(stderr, "rl5_stats: attach overflow on %s\n", j->name);
        goto done;
    }
    if(!vcomp5_build(a, v)){
        fprintf(stderr, "rl5_stats: vcomp overflow on %s\n", j->name);
        goto done;
    }
    if(!bcomp5_build(m, v, b)){
        fprintf(stderr, "rl5_stats: bcomp overflow on %s\n", j->name);
        goto done;
    }
    if(!bcomp5_inner_generated(m, b, gen)) goto done;
    if(!boundary5_collect_depth1_rings(m, v, opts->max_states, bg, &boundary_dead, &boundary_escape)) goto done;
    if(!bcomp5_inner_generated(m, bg, geng)) goto done;
    ring_missing = ring_set_missing_stats(b, bg);
    ring_extra = ring_set_missing_stats(bg, b);
    io_missing = inner_set_missing(gen, geng);
    io_extra = inner_set_missing(geng, gen);
    if(!extracted_ordered_inner(j->path, ext, &records_seen, &section_seen)) goto done;
    missing_ext = inner_set_missing(ext, gen);
    extra_gen = inner_set_missing(gen, ext);

    if(!opts->boundary_probe){
        printf("\n%s\n", j->name);
        printf("  source: %s\n", j->path);
        printf("  model: tiles=%d oriented=%d edge_rules=%d attach_edges=%d vertex_figures=%d\n",
               m->ntiles, m->noriented, m->nsource_rules, a->nedges, v->nfigs);
        printf("  bcomp: starts=%ld closed=%ld unique_rings=%d\n",
               b->starts, b->closure_hits, b->nrings);
        printf("  boundary5 depth1 exact: unique_rings=%d missing_legacy=%d extra_general=%d dead=%d escape=%d\n",
               bg->nrings, ring_missing, ring_extra, boundary_dead, boundary_escape);
        printf("  boundary5 depth1 inner/outer exact: generated_classes=%d missing_legacy=%d extra_general=%d\n",
               geng->nrows, io_missing, io_extra);
        printf("  ordered inner/outer edge cycles:\n");
        printf("    generated_classes=%d\n", gen->nrows);
        if(section_seen){
            printf("    extracted_records=%d extracted_classes=%d\n", records_seen, ext->nrows);
            printf("    extracted_subset_of_generated=%s missing_extracted=%d extra_generated=%d\n",
                   missing_ext == 0 ? "yes" : "no", missing_ext, extra_gen);
        } else {
            printf("    extracted_records=0 extracted_classes=0 status=missing_ordered_section\n");
        }
    }

    if(opts->boundary_probe){
        printf("%s\n", j->path);
        printf("%s==>%s RL5 boundary depth %d (%s)\n",
               term_color_word(stdout, "==>"),
               term_color_reset_for_word(stdout, "==>"),
               opts->boundary_depth,
               j->name);
        int max_states = opts->max_states;
        int max_depth = opts->boundary_depth;
        size_t begin = opts->sel.all ? 1u : 0u;
        size_t end = opts->sel.all ? (size_t)m->ntiles + 1u : (size_t)opts->sel.count;
        if(opts->print_table) boundary_print_header();
        for(size_t pos=begin; pos<end; pos++){
            size_t record = opts->sel.all ? pos : (size_t)opts->sel.selected[pos];
            int seed = (int)record - 1;
            Boundary5SeedReport rep;
            if(record < 1 || record > (size_t)m->ntiles){
                fprintf(stderr, "record=%zu out_of_range count=%d\n", record, m->ntiles);
                continue;
            }
            if(!boundary5_probe_seed(m, v, seed, max_depth, max_states, &rep)){
                Boundary5DepthCounts err;
                memset(&err, 0, sizeof(err));
                err.escape = 1;
                if(opts->print_table) boundary_print_row(record, &err);
                else printf("record=%zu status=escaped escapes=1 dfs=0\n", record);
                continue;
            }
            Boundary5DepthCounts *c = &rep.depth[rep.max_depth];
            if(opts->print_table) boundary_print_row(record, c);
            else {
                const char *status = boundary_status_name(c);
                printf("record=%zu status=%s escapes=%d dfs=%ld\n",
                       record, status, boundary_has_escape(c), c->processed);
            }
        }
    }
    ok = 1;
done:
    free(m); free(a); free(v); free(b); free(bg); free(gen); free(geng); free(ext);
    return ok;
}


static int selection5_add(Selection5 *sel, int idx){
    if(idx <= 0) return 0;
    if(sel->count >= STATS5_MAX_SELECT) return 0;
    sel->selected[sel->count++] = idx;
    return 1;
}

static int selection5_parse_token(Selection5 *sel, const char *arg){
    const char *p = arg;
    if(strcmp(arg, "all") == 0 || strcmp(arg, "--all") == 0){
        sel->all = 1;
        return 1;
    }
    while(*p){
        char *end = NULL;
        long v;
        while(*p == ',' || isspace((unsigned char)*p)) p++;
        if(!*p) break;
        v = strtol(p, &end, 10);
        if(end == p || v <= 0 || v > 2147483647L) return 0;
        if(!selection5_add(sel, (int)v)) return 0;
        p = end;
        while(*p == ',' || isspace((unsigned char)*p)) p++;
        if(*p && !isdigit((unsigned char)*p)) return 0;
    }
    return 1;
}

static int model_name_ok(const char *s){
    return strcmp(s, "basic") == 0 || strcmp(s, "little") == 0 ||
           strcmp(s, "super") == 0 || strcmp(s, "overlap") == 0;
}

static const char *canonical_model_name(const char *s){
    return strcmp(s, "little") == 0 ? "basic" : s;
}

static void usage5(const char *prog){
    fprintf(stderr,
            "usage: %s [all | N[,N...] ...] [options] [basic.dat super.dat overlap.dat]\n"
            "defaults:\n"
            "  model: all\n"
            "  depth: 1\n"
            "  max-states: 1000000\n"
            "options:\n"
            "  --model basic|super|overlap\n"
            "  --boundary-depth N\n"
            "  --max-states N\n"
            "  --print\n"
            "  --quiet\n",
            prog);
}

static int parse_nonneg5(const char *s, int *out){
    char *end = NULL;
    long v;
    if(!s || !*s) return 0;
    v = strtol(s, &end, 10);
    if(*end || v < 0 || v > 2147483647L) return 0;
    *out = (int)v;
    return 1;
}

int main(int argc, char **argv){
    Job jobs[] = {
        {"basic", "data/rl5/hexagons.dat"},
        {"super", "data/rl5/supertile_hexagons.dat"},
        {"overlap", "data/rl5/overlap_supertile_hexagons.dat"},
    };
    int njobs = (int)(sizeof(jobs)/sizeof(jobs[0]));
    Stats5Options opts;
    const char *paths[3];
    int npaths = 0;
    memset(&opts, 0, sizeof(opts));
    opts.only_model = NULL;
    opts.boundary_probe = 0;
    opts.boundary_depth = 1;
    opts.max_states = RL5_DEFAULT_MAX_STATES;
    opts.print_table = 1;

    for(int i=1; i<argc; i++){
        if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0){
            usage5(argv[0]);
            return 0;
        } else if(strcmp(argv[i], "--model") == 0){
            if(++i >= argc || !model_name_ok(argv[i])){
                usage5(argv[0]);
                return 2;
            }
            opts.only_model = canonical_model_name(argv[i]);
        } else if(strcmp(argv[i], "--boundary-depth") == 0 || strcmp(argv[i], "--depth") == 0){
            if(++i >= argc || !parse_nonneg5(argv[i], &opts.boundary_depth)){
                usage5(argv[0]);
                return 2;
            }
            opts.boundary_probe = 1;
        } else if(strcmp(argv[i], "--max-states") == 0){
            if(++i >= argc || !parse_nonneg5(argv[i], &opts.max_states) || opts.max_states <= 0){
                usage5(argv[0]);
                return 2;
            }
        } else if(strcmp(argv[i], "--print") == 0){
            opts.print_table = 1;
        } else if(strcmp(argv[i], "--quiet") == 0){
            opts.print_table = 0;
        } else if(argv[i][0] == '-'){
            fprintf(stderr, "rl5_stats: unknown option '%s'\n", argv[i]);
            usage5(argv[0]);
            return 2;
        } else {
            Selection5 tmp = opts.sel;
            if(selection5_parse_token(&tmp, argv[i])){
                opts.sel = tmp;
                opts.boundary_probe = 1;
            } else {
                if(npaths >= 3){ usage5(argv[0]); return 2; }
                paths[npaths++] = argv[i];
            }
        }
    }
    if(npaths != 0 && npaths != 3){
        usage5(argv[0]);
        return 2;
    }
    if(npaths == 3){
        jobs[0].path = paths[0];
        jobs[1].path = paths[1];
        jobs[2].path = paths[2];
    }
    if(opts.sel.count == 0 && !opts.sel.all) opts.sel.all = 1;

    for(int i=0;i<njobs;i++){
        if(opts.only_model && strcmp(opts.only_model, jobs[i].name) != 0) continue;
        if(!run_one(&jobs[i], &opts)) return 1;
    }
    return 0;
}
