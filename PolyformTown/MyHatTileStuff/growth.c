/*
 * growth.c - grow hat tile clusters around an arbitrary centre metatile.
 *
 * The centre tile (e.g. tiles/h3.tile) is decomposed into its constituent hats
 * by computing the area ratio.  The BComp1 machinery then grows N levels of
 * hats around the decomposed seed:
 *
 *   Level 1  hats share an edge with the centre cluster.
 *   Level 2  hats share an edge with a level-1 hat.
 *   ...
 *
 * Only hole-free, live-boundary clusters survive.  The forced deterministic
 * ring (attach0_force_live_closure) is always applied first; --level counts
 * optional rings on top of that.
 *
 * Build:  make growth
 * Run:    ./bin/growth [--center tiles/h3.tile] [--level 2]
 *                      [--tile preferences/focus.tile]
 *                      [--remembrance data/rl0/remembrance.dat]
 *                      [--deletions data/rl0/deletions.dat]
 *                      [--out-dir data/h3]
 *
 * Output: data/<stem>/supertile.dat
 *         data/<stem>/completions_level<N>.dat
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "core/attach.h"
#include "core/boundary.h"
#include "core/cycle.h"
#include "core/tetrille.h"
#include "core/tile.h"
#include "rl0/attach0.h"
#include "rl0/boundary0.h"
#include "rl1/bcomp1.h"

#define FORCE_STEPS   1024
#define MAX_HAT_COUNT  256

/* ------------------------------------------------------------------ helpers */

static int ensure_dir(const char *p) { return mkdir(p, 0777) == 0 || errno == EEXIST; }

static int coord_in(const Coord *list, int n, Coord q) {
    for (int i = 0; i < n; i++) if (coord_eq(list[i], q)) return 1;
    return 0;
}
static int coord_cmp(const void *A, const void *B) {
    const Coord *a = A, *b = B;
    if (a->v != b->v) return a->v - b->v;
    if (a->x != b->x) return a->x - b->x;
    return a->y - b->y;
}

static int collect_verts(const Cycle *tiles, int n, Coord *out) {
    int c = 0;
    for (int t = 0; t < n; t++)
        for (int i = 0; i < tiles[t].n; i++)
            if (!coord_in(out, c, tiles[t].v[i])) {
                if (c >= BCOMP1_MAX_COORDS) return -1;
                out[c++] = tiles[t].v[i];
            }
    return c;
}

static int rebuild_hidden(const Poly *p, const Cycle *tiles, int n, Coord *hidden) {
    Coord all[BCOMP1_MAX_COORDS], bnd[BCOMP1_MAX_COORDS];
    int ac = collect_verts(tiles, n, all);
    int bc = build_boundary_vertices(p, bnd);
    if (ac < 0 || bc < 0) return -1;
    int hc = 0;
    for (int i = 0; i < ac; i++)
        if (!coord_in(bnd, bc, all[i])) {
            if (hc >= BCOMP1_MAX_COORDS) return -1;
            if (hidden) hidden[hc] = all[i];
            hc++;
        }
    if (hidden && hc > 1) qsort(hidden, (size_t)hc, sizeof(Coord), coord_cmp);
    return hc;
}

static int make_record(const BComp1State *s, const Cycle *center, BComp1Record *r) {
    Coord *hidden = malloc(sizeof(*hidden) * BCOMP1_MAX_COORDS);
    if (!hidden) return 0;
    int hc = rebuild_hidden(&s->poly, s->tiles, s->tile_count, hidden);
    if (hc < 0) { free(hidden); return 0; }
    memset(r, 0, sizeof(*r));
    r->have_center = r->have_boundary = r->have_hidden = r->have_tiles = 1;
    r->level = hc;
    r->center = *center;
    r->boundary = s->poly;
    r->hidden_count = hc;
    r->tiles_count = r->tile_count = s->tile_count;
    for (int i = 0; i < hc; i++) r->hidden[i] = hidden[i];
    for (int i = 0; i < s->tile_count; i++) r->tiles[i] = s->tiles[i];
    free(hidden);
    return 1;
}

/* ---------------------------------------------------------- decomposition */

static Cycle      g_center;
static long long  g_cx[MAX_VERTS], g_cy[MAX_VERTS];
static const Tile *g_hat;
static Cycle      g_pieces[MAX_HAT_COUNT];
static Poly       g_union;
static int        g_done, g_hat_count;

static int place_on(const Cycle *var, int j, Coord tgt, Cycle *out) {
    if (var->v[j].v != tgt.v) return 0;
    int dx = tgt.x - var->v[j].x, dy = tgt.y - var->v[j].y, m, n;
    if (!tetrille_delta_to_6(var->v[j].v, dx, dy, &m, &n)) return 0;
    *out = *var;
    tetrille_translate_cycle(out, m, n);
    return coord_eq(out->v[j], tgt);
}

static int inside_center(Coord q) {
    long long qx, qy;
    tetrille_embed_point_scaled(q, &qx, &qy);
    for (int i = 0; i < g_center.n; i++)
        if (tetrille_point_on_segment(q, g_center.v[i],
                                      g_center.v[(i+1) % g_center.n])) return 1;
    int c = 0;
    for (int i = 0, j = g_center.n-1; i < g_center.n; j = i++) {
        if ((g_cy[i] > qy) != (g_cy[j] > qy)) {
            long double xi = (long double)(g_cx[j]-g_cx[i]) *
                             (long double)(qy - g_cy[i]) /
                             (long double)(g_cy[j]-g_cy[i]) + g_cx[i];
            if ((long double)qx < xi) c ^= 1;
        }
    }
    return c;
}

static int hat_inside(const Cycle *h) {
    for (int i = 0; i < h->n; i++) if (!inside_center(h->v[i])) return 0;
    return 1;
}

static int same_verts(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) {
        int f = 0;
        for (int j = 0; j < b->n; j++) if (coord_eq(a->v[i], b->v[j])) { f=1; break; }
        if (!f) return 0;
    }
    return 1;
}

static void dfs(const Poly *agg, int count) {
    if (g_done) return;
    if (count == g_hat_count) {
        if (agg->cycle_count == 1 && same_verts(&agg->cycles[0], &g_center))
            { g_union = *agg; g_done = 1; }
        return;
    }
    Edge edges[256];
    int ec = build_boundary_edges(agg, edges);
    for (int be = 0; be < ec && !g_done; be++)
        for (int vi = 0; vi < g_hat->variant_count && !g_done; vi++) {
            const Cycle *var = &g_hat->variants[vi];
            for (int te = 0; te < var->n && !g_done; te++) {
                Poly out; Cycle aligned;
                if (!try_attach_tile_poly_ex(agg, var, g_hat->lattice,
                                             be, te, &out, &aligned)) continue;
                if (out.cycle_count > 1 || !hat_inside(&aligned)) continue;
                g_pieces[count] = aligned;
                dfs(&out, count + 1);
            }
        }
}

static int decompose(BComp1State *seed) {
    g_done = 0;
    for (int k = 0; k < g_center.n && !g_done; k++)
        for (int vi = 0; vi < g_hat->variant_count && !g_done; vi++) {
            const Cycle *var = &g_hat->variants[vi];
            for (int j = 0; j < var->n && !g_done; j++) {
                Cycle cand; Poly agg;
                if (!place_on(var, j, g_center.v[k], &cand) || !hat_inside(&cand)) continue;
                agg.cycle_count = 1; agg.cycles[0] = cand; g_pieces[0] = cand;
                dfs(&agg, 1);
            }
        }
    if (!g_done) return 0;
    BComp1Record rec;
    memset(&rec, 0, sizeof(rec));
    rec.have_boundary = rec.have_tiles = rec.have_center = 1;
    rec.boundary = g_union;
    rec.tiles_count = g_hat_count;
    for (int i = 0; i < g_hat_count; i++) rec.tiles[i] = g_pieces[i];
    rec.center = g_center;
    return bcomp1_state_from_record(&rec, seed);
}

/* --------------------------------------------------------------------- SVG */

static double g_r3;

static double svgx(Coord p) {
    if (p.v==6) return g_r3*.5*(p.x+p.y);
    if (p.v==4) return g_r3*.25*(p.x+p.y);
    return (p.x+.5*p.y)/g_r3;
}
static double svgy(Coord p) {
    if (p.v==6) return .5*(p.y-p.x);
    if (p.v==4) return .25*(p.y-p.x);
    return .5*p.y;
}

static void bbox_cycle(const Cycle *c, double *x0, double *y0, double *x1, double *y1) {
    for (int i = 0; i < c->n; i++) {
        double x=svgx(c->v[i]), y=svgy(c->v[i]);
        *x0=x<*x0?x:*x0; *x1=x>*x1?x:*x1;
        *y0=y<*y0?y:*y0; *y1=y>*y1?y:*y1;
    }
}

static void draw_hat(FILE *f, const Cycle *c, double ox, double oy, double sc,
                     const char *fill, const char *stroke, double sw) {
    fputs("<path d=\"", f);
    for (int i = 0; i < c->n; i++)
        fprintf(f, "%c%.2f,%.2f", i?'L':'M', ox+sc*svgx(c->v[i]), oy-sc*svgy(c->v[i]));
    fprintf(f, " Z\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%.2f\""
               " stroke-linejoin=\"round\"/>\n", fill, stroke, sw);
}

static void emit_svg(FILE *f, const BComp1RecordVec *recs, const Cycle *center) {
    const double SC=24., M=14., LABEL_H=14., SW=1.0;
    g_r3 = sqrt(3.0);

    double x0=1e9, y0=1e9, x1=-1e9, y1=-1e9;
    for (size_t k = 0; k < recs->count; k++) {
        const BComp1Record *r = &recs->items[k];
        for (int i = 0; i < r->tiles_count; i++)
            bbox_cycle(&r->tiles[i], &x0, &y0, &x1, &y1);
    }
    double bw=x1-x0, bh=y1-y0;
    if (bw<1e-9) bw=1;
    if (bh<1e-9) bh=1;

    double cw=2*M+SC*bw, ch=2*M+SC*bh+LABEL_H;

    /* grid dimensions aimed at 16:9 */
    int n=(int)recs->count, rows=1, cols=n>0?n:1;
    double best=1e18;
    for (int r=1; r<=n; r++) {
        int c=(n+r-1)/r;
        double sc=fabs((double)c*cw/((double)r*ch)-16.0/9.0);
        if (sc<best) { best=sc; rows=r; cols=c; }
    }

    fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\""
               " width=\"%.0f\" height=\"%.0f\">\n"
               "<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>\n",
               (double)cols*cw, (double)rows*ch);

    for (int i = 0; i < n; i++) {
        int row=i/cols, col=i%cols;
        double draw_h=ch-LABEL_H;
        double ox=col*cw+(cw-SC*bw)*0.5-SC*x0;
        double oy=row*ch+(draw_h-SC*bh)*0.5+SC*y1;
        const BComp1Record *rec=&recs->items[i];

        for (int t=g_hat_count; t<rec->tiles_count; t++)
            draw_hat(f, &rec->tiles[t], ox, oy, SC, "#a8d8ea", "#555", SW*0.7);
        for (int t=0; t<g_hat_count && t<rec->tiles_count; t++)
            draw_hat(f, &rec->tiles[t], ox, oy, SC, "#f39c12", "#1a1a1a", SW*1.1);
        draw_hat(f, center, ox, oy, SC, "none", "#c0392b", SW*1.8);

        fprintf(f, "<text x=\"%.1f\" y=\"%.1f\" text-anchor=\"middle\""
                   " font-size=\"9\" font-family=\"sans-serif\" fill=\"#444\">%d tiles</text>\n",
                col*cw+cw*0.5, row*ch+ch-3.0, rec->tiles_count);
    }
    fprintf(f, "</svg>\n");
}

/* -------------------------------------------------------------------- main */

int main(int argc, char **argv) {
    const char *center_path      = "tiles/h3.tile";
    const char *tile_path        = "preferences/focus.tile";
    const char *remembrance_path = "data/rl0/remembrance.dat";
    const char *deletions_path   = "data/rl0/deletions.dat";
    const char *out_dir          = NULL;
    const char *svg_path         = NULL;
    int level = 1;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i],"--center")      && i+1<argc) center_path      = argv[++i];
        else if (!strcmp(argv[i],"--tile")        && i+1<argc) tile_path        = argv[++i];
        else if (!strcmp(argv[i],"--remembrance") && i+1<argc) remembrance_path = argv[++i];
        else if (!strcmp(argv[i],"--deletions")   && i+1<argc) deletions_path   = argv[++i];
        else if (!strcmp(argv[i],"--out-dir")     && i+1<argc) out_dir          = argv[++i];
        else if (!strcmp(argv[i],"--level")       && i+1<argc) level            = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--svg")         && i+1<argc) svg_path         = argv[++i];
    }

    /* auto-derive out_dir from center filename stem */
    char auto_dir[512];
    if (!out_dir) {
        const char *base = strrchr(center_path, '/');
        base = base ? base+1 : center_path;
        char stem[256];
        strncpy(stem, base, sizeof(stem)-1); stem[sizeof(stem)-1] = '\0';
        char *dot = strrchr(stem, '.'); if (dot) *dot = '\0';
        snprintf(auto_dir, sizeof(auto_dir), "data/%s", stem);
        out_dir = auto_dir;
    }

    BComp1Context ctx;
    if (!bcomp1_context_init(&ctx, tile_path, remembrance_path, deletions_path)) {
        fprintf(stderr, "ERROR: failed to init context from %s\n", tile_path); return 1;
    }

    Tile center_tile;
    if (!tile_load(center_path, &center_tile)) {
        fprintf(stderr, "ERROR: failed to load %s\n", center_path);
        bcomp1_context_clear(&ctx); return 1;
    }
    if (center_tile.lattice != ctx.tile.lattice) {
        fprintf(stderr, "ERROR: lattice mismatch between center and hat tile\n");
        bcomp1_context_clear(&ctx); return 1;
    }

    /* infer hat count from area ratio */
    long long ca = cycle_signed_area2(&center_tile.base, center_tile.lattice);
    long long ha = cycle_signed_area2(&ctx.tile.base,    ctx.tile.lattice);
    if (ca < 0) ca = -ca;
    if (ha < 0) ha = -ha;
    if (ha == 0 || ca % ha != 0) {
        fprintf(stderr, "ERROR: center area (%lld) is not a multiple of hat area (%lld)\n", ca, ha);
        bcomp1_context_clear(&ctx); return 1;
    }
    g_hat_count = (int)(ca / ha);
    if (g_hat_count < 1 || g_hat_count > MAX_HAT_COUNT) {
        fprintf(stderr, "ERROR: implausible hat count %d\n", g_hat_count);
        bcomp1_context_clear(&ctx); return 1;
    }

    g_center = center_tile.base;
    if (cycle_signed_area2(&g_center, center_tile.lattice) < 0) cycle_reverse(&g_center);
    cycle_normalize_position(&g_center, center_tile.lattice);
    for (int i = 0; i < g_center.n; i++)
        tetrille_embed_point_scaled(g_center.v[i], &g_cx[i], &g_cy[i]);
    g_hat = &ctx.tile;

    if (!ensure_dir("data") || !ensure_dir(out_dir)) {
        fprintf(stderr, "ERROR: cannot create %s\n", out_dir);
        bcomp1_context_clear(&ctx); return 1;
    }

    /* decompose centre tile into individual hats */
    BComp1State seed;
    if (!decompose(&seed)) {
        fprintf(stderr, "ERROR: could not decompose %s into %d hats\n",
                center_path, g_hat_count);
        bcomp1_context_clear(&ctx); return 1;
    }
    fprintf(stderr, "Decomposed %s into %d hats (seed tiles=%d)\n",
            center_path, g_hat_count, seed.tile_count);

    /* forced deterministic ring */
    Attach0Stats astats;
    Attach0ClosureStats cstats;
    attach0_stats_init(&astats);
    attach0_closure_stats_init(&cstats);
    if (!attach0_force_live_closure(&seed.poly, &ctx.tile, seed.tiles,
                                    &seed.tile_count, &ctx.map,
                                    FORCE_STEPS, &astats, &cstats)) {
        fprintf(stderr, "ERROR: forced closure failed (unresolved=%d)\n",
                cstats.unresolved_vertices);
        bcomp1_context_clear(&ctx); return 1;
    }
    seed.hidden_count = rebuild_hidden(&seed.poly, seed.tiles, seed.tile_count, NULL);

    /* write supertile record */
    char path[1024]; FILE *fp;
    BComp1Record supertile;
    if (!make_record(&seed, &g_center, &supertile)) {
        fprintf(stderr, "ERROR: make_record failed\n");
        bcomp1_context_clear(&ctx); return 1;
    }
    snprintf(path, sizeof(path), "%s/supertile.dat", out_dir);
    fp = fopen(path, "w");
    if (!fp) { perror(path); bcomp1_context_clear(&ctx); return 1; }
    bcomp1_print_record(fp, 1, &supertile);
    fclose(fp);

    /* level-N optional growth */
    BComp1Options opts;
    bcomp1_options_default(&opts);
    opts.depth          = level;
    opts.collect_records = 1;
    opts.live_only      = 1;

    BComp1Result result;
    if (!bcomp1_complete_state(&ctx, &seed, &g_center, &opts, &result)) {
        fprintf(stderr, "ERROR: completion search failed\n");
        bcomp1_context_clear(&ctx); return 1;
    }

    snprintf(path, sizeof(path), "%s/completions_level%d.dat", out_dir, level);
    fp = fopen(path, "w");
    if (!fp) { perror(path); bcomp1_result_clear(&result); bcomp1_context_clear(&ctx); return 1; }
    bcomp1_sort_records(&result.records);
    for (size_t i = 0; i < result.records.count; i++)
        bcomp1_print_record(fp, i+1, &result.records.items[i]);
    fclose(fp);

    fprintf(stderr,
            "growth  center=%s  hats=%d  level=%d\n"
            "  forced tiles=%d  closure_steps=%d  unresolved=%d\n"
            "  completions=%zu  ->  %s\n",
            center_path, g_hat_count, level,
            seed.tile_count, cstats.closure_steps, cstats.unresolved_vertices,
            result.records.count, path);

    if (svg_path) {
        FILE *sf = fopen(svg_path, "w");
        if (!sf) { perror(svg_path); }
        else {
            emit_svg(sf, &result.records, &g_center);
            fclose(sf);
            fprintf(stderr, "  svg=%s\n", svg_path);
        }
    }

    bcomp1_result_clear(&result);
    bcomp1_context_clear(&ctx);
    return 0;
}
