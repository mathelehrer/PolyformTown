#include "rl5/hex5.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *path;
} Job;

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

static int run_one(const Job *j){
    Hex5Model *m = calloc(1, sizeof(*m));
    Attach5Dict *a = calloc(1, sizeof(*a));
    VComp5Dict *v = calloc(1, sizeof(*v));
    BComp5Result *b = calloc(1, sizeof(*b));
    Inner5Set *gen = calloc(1, sizeof(*gen));
    Inner5Set *ext = calloc(1, sizeof(*ext));
    int ok = 0, records_seen = 0, section_seen = 0;
    int missing_ext, extra_gen;
    if(!m || !a || !v || !b || !gen || !ext) goto done;
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
    if(!extracted_ordered_inner(j->path, ext, &records_seen, &section_seen)) goto done;
    missing_ext = inner_set_missing(ext, gen);
    extra_gen = inner_set_missing(gen, ext);

    printf("\n%s\n", j->name);
    printf("  source: %s\n", j->path);
    printf("  model: tiles=%d oriented=%d edge_rules=%d attach_edges=%d vertex_figures=%d\n",
           m->ntiles, m->noriented, m->nsource_rules, a->nedges, v->nfigs);
    printf("  bcomp: starts=%ld closed=%ld unique_rings=%d\n",
           b->starts, b->closure_hits, b->nrings);
    printf("  ordered inner/outer edge cycles:\n");
    printf("    generated_classes=%d\n", gen->nrows);
    if(section_seen){
        printf("    extracted_records=%d extracted_classes=%d\n", records_seen, ext->nrows);
        printf("    extracted_subset_of_generated=%s missing_extracted=%d extra_generated=%d\n",
               missing_ext == 0 ? "yes" : "no", missing_ext, extra_gen);
    } else {
        printf("    extracted_records=0 extracted_classes=0 status=missing_ordered_section\n");
    }
    ok = 1;
done:
    free(m); free(a); free(v); free(b); free(gen); free(ext);
    return ok;
}

int main(int argc, char **argv){
    Job jobs[] = {
        {"little", "data/rl5/hexagons.dat"},
        {"super", "data/rl5/supertile_hexagons.dat"},
        {"overlap", "data/rl5/overlap_supertile_hexagons.dat"},
    };
    int njobs = (int)(sizeof(jobs)/sizeof(jobs[0]));
    if(argc == 4){
        jobs[0].path = argv[1]; jobs[1].path = argv[2]; jobs[2].path = argv[3];
    } else if(argc != 1){
        fprintf(stderr, "usage: %s [little.dat super.dat overlap.dat]\n", argv[0]);
        return 2;
    }
    for(int i=0;i<njobs;i++) if(!run_one(&jobs[i])) return 1;
    return 0;
}
