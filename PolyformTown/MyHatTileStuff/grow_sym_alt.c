/*
 * grow_sym_alt.c - all level-3 extensions of the unique symmetric level-2 cluster.
 *
 * Pipeline:
 *   1. Build the 3-hat pinwheel seed (three hats sharing vertex 0 = origin).
 *   2. Apply forced closure.
 *   3. Grow to depth 2, filter for 120° symmetry → one unique level-2 state.
 *   4. Use that state as a new seed, grow one more ring (depth 1, no filter).
 *   5. Output ALL resulting level-3 clusters and an SVG grid.
 *
 * SVG coloring:
 *   - Seed hats (0-2): orange / red / purple
 *   - Level-2 cluster tiles (3 .. n_l2-1): amber
 *   - New level-3 tiles (n_l2 ..): light blue
 *   - Red outline: the 3-hat pinwheel boundary
 *
 * Build:  make grow_sym_alt
 * Run:    ./bin/grow_sym_alt [--level2 2] [--tile preferences/focus.tile]
 *                            [--remembrance data/rl0/remembrance.dat]
 *                            [--deletions data/rl0/deletions.dat]
 *                            [--out-dir data/sym3] [--svg out.svg]
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

#define SEED_HATS    3
#define FORCE_STEPS  1024

/* ---------------------------------------------------------------- helpers */

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

/* ---------------------------------------------------- seed construction */

static int build_symmetric_seed(const Tile *tile, BComp1State *seed,
                                 Cycle *center_out)
{
    Coord center = tile->base.v[0];  /* (6,0,0) = coordinate origin */
    Cycle rot[SEED_HATS];
    rot[0] = tile->base;
    cycle_transform_lattice(&tile->base, &rot[1], tile->lattice, 2);
    cycle_transform_lattice(&tile->base, &rot[2], tile->lattice, 4);

    Poly poly[SEED_HATS];
    Cycle hats[SEED_HATS];
    poly[0].cycle_count = 1;
    poly[0].cycles[0]   = rot[0];
    hats[0]             = rot[0];

    Edge edges[512];
    for (int k = 1; k < SEED_HATS; k++) {
        int ok = 0;
        int ec = build_boundary_edges(&poly[k-1], edges);
        for (int be = 0; be < ec && !ok; be++)
            for (int te = 0; te < rot[k].n && !ok; te++) {
                Cycle aligned;
                if (!try_attach_tile_poly_ex(&poly[k-1], &rot[k], tile->lattice,
                                              be, te, &poly[k], &aligned)) continue;
                if (poly[k].cycle_count > 1) continue;
                if (!coord_eq(aligned.v[0], center)) continue;
                hats[k] = aligned;
                ok = 1;
            }
        if (!ok) {
            fprintf(stderr, "ERROR: cannot attach hat%d\n", k + 1);
            return 0;
        }
    }

    Cycle seed_center = poly[SEED_HATS-1].cycles[0];
    if (center_out) *center_out = seed_center;

    BComp1Record rec;
    memset(&rec, 0, sizeof(rec));
    rec.have_boundary = rec.have_tiles = rec.have_center = 1;
    rec.center      = seed_center;
    rec.boundary    = poly[SEED_HATS-1];
    rec.tiles_count = SEED_HATS;
    for (int k = 0; k < SEED_HATS; k++) rec.tiles[k] = hats[k];
    return bcomp1_state_from_record(&rec, seed);
}

/* -------------------------------------------------- 120° symmetry check */

static int cycles_match_cyclic(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return 0;
    for (int s = 0; s < b->n; s++) {
        if (!coord_eq(b->v[s], a->v[0])) continue;
        int ok = 1;
        for (int i = 1; i < a->n && ok; i++)
            if (!coord_eq(a->v[i], b->v[(s+i) % b->n])) ok = 0;
        if (ok) return 1;
    }
    return 0;
}

static int has_120_symmetry(const Poly *boundary, int lattice) {
    if (boundary->cycle_count != 1) return 0;
    const Cycle *c = &boundary->cycles[0];
    Cycle rotated;
    cycle_transform_lattice(c, &rotated, lattice, 2);
    return cycles_match_cyclic(c, &rotated);
}

/* ----------------------------------------------------------------------- SVG */

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

static void bbox_cycle(const Cycle *c,
                       double *x0, double *y0, double *x1, double *y1) {
    for (int i = 0; i < c->n; i++) {
        double x=svgx(c->v[i]), y=svgy(c->v[i]);
        *x0=x<*x0?x:*x0; *x1=x>*x1?x:*x1;
        *y0=y<*y0?y:*y0; *y1=y>*y1?y:*y1;
    }
}

static void draw_hat(FILE *f, const Cycle *c,
                     double ox, double oy, double sc,
                     const char *fill, const char *stroke, double sw) {
    fputs("<path d=\"", f);
    for (int i = 0; i < c->n; i++)
        fprintf(f, "%c%.2f,%.2f",
                i?'L':'M', ox+sc*svgx(c->v[i]), oy-sc*svgy(c->v[i]));
    fprintf(f, " Z\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%.2f\""
               " stroke-linejoin=\"round\"/>\n", fill, stroke, sw);
}

/*
 * n_l2: number of tiles in the level-2 base cluster (these get amber fill).
 * Tiles 0..SEED_HATS-1: three distinct pinwheel colours.
 * Tiles SEED_HATS..n_l2-1: amber (inherited from level-2).
 * Tiles n_l2..: light blue (new level-3 additions).
 */
static void emit_svg(FILE *f, const BComp1RecordVec *recs,
                     const Cycle *pinwheel_center, int n_l2) {
    const double SC=14., M=10., LABEL_H=12., SW=0.8;
    static const char *seed_fills[] = {"#f39c12", "#e74c3c", "#8e44ad"};
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

    int n=(int)recs->count, rows=1, cols=n>0?n:1;
    double best=1e18;
    for (int r=1; r<=n; r++) {
        int c2=(n+r-1)/r;
        double sc2=fabs((double)c2*cw/((double)r*ch)-16.0/9.0);
        if (sc2<best) { best=sc2; rows=r; cols=c2; }
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

        /* new level-3 tiles */
        for (int t=n_l2; t<rec->tiles_count; t++)
            draw_hat(f, &rec->tiles[t], ox, oy, SC, "#a8d8ea", "#555", SW*0.7);
        /* amber: level-2 cluster tiles beyond the seed */
        for (int t=SEED_HATS; t<n_l2 && t<rec->tiles_count; t++)
            draw_hat(f, &rec->tiles[t], ox, oy, SC, "#f0c040", "#888", SW*0.6);
        /* pinwheel seed hats */
        for (int t=0; t<SEED_HATS && t<rec->tiles_count; t++)
            draw_hat(f, &rec->tiles[t], ox, oy, SC, seed_fills[t], "#1a1a1a", SW*1.2);
        /* red outline: 3-hat pinwheel boundary */
        draw_hat(f, pinwheel_center, ox, oy, SC, "none", "#c0392b", SW*2.0);

        int new_tiles = rec->tiles_count - n_l2;
        fprintf(f, "<text x=\"%.1f\" y=\"%.1f\" text-anchor=\"middle\""
                   " font-size=\"8\" font-family=\"sans-serif\" fill=\"#444\">+%d tiles</text>\n",
                col*cw+cw*0.5, row*ch+ch-2.5, new_tiles > 0 ? new_tiles : 0);
    }
    fputs("</svg>\n", f);
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv) {
    const char *tile_path        = "preferences/focus.tile";
    const char *remembrance_path = "data/rl0/remembrance.dat";
    const char *deletions_path   = "data/rl0/deletions.dat";
    const char *out_dir          = "data/sym3";
    const char *svg_path         = NULL;
    int sym_depth = 2;   /* depth to reach the symmetric base state */

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i],"--tile")        && i+1<argc) tile_path        = argv[++i];
        else if (!strcmp(argv[i],"--remembrance") && i+1<argc) remembrance_path = argv[++i];
        else if (!strcmp(argv[i],"--deletions")   && i+1<argc) deletions_path   = argv[++i];
        else if (!strcmp(argv[i],"--out-dir")     && i+1<argc) out_dir          = argv[++i];
        else if (!strcmp(argv[i],"--level2")      && i+1<argc) sym_depth        = atoi(argv[++i]);
        else if (!strcmp(argv[i],"--svg")         && i+1<argc) svg_path         = argv[++i];
    }

    BComp1Context ctx;
    if (!bcomp1_context_init(&ctx, tile_path, remembrance_path, deletions_path)) {
        fprintf(stderr, "ERROR: failed to init context from %s\n", tile_path);
        return 1;
    }
    if (!ensure_dir("data") || !ensure_dir(out_dir)) {
        fprintf(stderr, "ERROR: cannot create %s\n", out_dir);
        bcomp1_context_clear(&ctx); return 1;
    }

    /* --- Step 1: pinwheel seed + forced closure ----------------------------- */
    Cycle pinwheel_center;
    BComp1State seed;
    if (!build_symmetric_seed(&ctx.tile, &seed, &pinwheel_center)) {
        fputs("ERROR: could not build symmetric 3-hat seed\n", stderr);
        bcomp1_context_clear(&ctx); return 1;
    }
    fprintf(stderr, "Symmetric 3-hat seed (tiles=%d)\n", seed.tile_count);

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
    fprintf(stderr, "After forced closure: tiles=%d  closure_steps=%d\n",
            seed.tile_count, cstats.closure_steps);

    /* --- Step 2: grow to sym_depth, keep only the symmetric result ---------- */
    BComp1Options opts;
    bcomp1_options_default(&opts);
    opts.depth           = sym_depth;
    opts.collect_records = 1;
    opts.live_only       = 1;

    BComp1Result sym_result;
    if (!bcomp1_complete_state(&ctx, &seed, &pinwheel_center, &opts, &sym_result)) {
        fputs("ERROR: level-2 completion search failed\n", stderr);
        bcomp1_context_clear(&ctx); return 1;
    }
    fprintf(stderr, "Level-%d completions: %zu total\n",
            sym_depth, sym_result.records.count);

    /* Find the (unique) symmetric result */
    BComp1Record *sym_rec = NULL;
    size_t sym_count = 0;
    for (size_t i = 0; i < sym_result.records.count; i++) {
        if (has_120_symmetry(&sym_result.records.items[i].boundary, ctx.tile.lattice)) {
            sym_rec = &sym_result.records.items[i];
            sym_count++;
        }
    }
    if (sym_count == 0) {
        fprintf(stderr, "ERROR: no 120°-symmetric completion found at depth %d\n", sym_depth);
        bcomp1_result_clear(&sym_result); bcomp1_context_clear(&ctx); return 1;
    }
    fprintf(stderr, "Symmetric level-%d states: %zu  (using first, tiles=%d)\n",
            sym_depth, sym_count, sym_rec->tiles_count);

    int n_l2 = sym_rec->tiles_count;   /* tile count of the level-2 base */

    /* --- Step 3: convert symmetric record → BComp1State -------------------- */
    BComp1State l2_state;
    if (!bcomp1_state_from_record(sym_rec, &l2_state)) {
        fputs("ERROR: bcomp1_state_from_record failed for level-2 record\n", stderr);
        bcomp1_result_clear(&sym_result); bcomp1_context_clear(&ctx); return 1;
    }

    /* --- Step 3b: diagnose the level-2 state with forced closure ----------- */
    {
        BComp1State probe = l2_state;  /* local copy — do not modify l2_state */
        Attach0Stats pas; Attach0ClosureStats pcs;
        attach0_stats_init(&pas);
        attach0_closure_stats_init(&pcs);
        int probe_ok = attach0_force_live_closure(
                &probe.poly, &ctx.tile, probe.tiles,
                &probe.tile_count, &ctx.map, FORCE_STEPS, &pas, &pcs);
        fprintf(stderr,
                "Forced closure probe on level-%d state: %s "
                "(added %d tiles, steps=%d, unresolved=%d)\n",
                sym_depth,
                probe_ok ? "ok — state is extendable"
                         : "FAILED — state is a dead end (RL0 contradiction)",
                probe.tile_count - n_l2,
                pcs.closure_steps, pcs.unresolved_vertices);
    }

    /* --- Step 4: grow one more ring, no symmetry filter -------------------- */
    opts.depth = 1;
    BComp1Result result3;
    if (!bcomp1_complete_state(&ctx, &l2_state, &pinwheel_center, &opts, &result3)) {
        fputs("ERROR: level-3 completion search failed\n", stderr);
        bcomp1_result_clear(&sym_result); bcomp1_context_clear(&ctx); return 1;
    }

    /* --- Step 5: write output ----------------------------------------------- */
    char path[1024]; FILE *fp;
    snprintf(path, sizeof(path), "%s/alt_completions_level%d.dat", out_dir, sym_depth + 1);
    fp = fopen(path, "w");
    if (!fp) { perror(path); goto done; }
    bcomp1_sort_records(&result3.records);
    for (size_t i = 0; i < result3.records.count; i++)
        bcomp1_print_record(fp, i+1, &result3.records.items[i]);
    fclose(fp);

    fprintf(stderr,
            "grow_sym_alt  sym_depth=%d  base_tiles=%d\n"
            "  level-3 completions=%zu  ->  %s\n",
            sym_depth, n_l2, result3.records.count, path);

    if (svg_path) {
        FILE *sf = fopen(svg_path, "w");
        if (!sf) { perror(svg_path); }
        else {
            emit_svg(sf, &result3.records, &pinwheel_center, n_l2);
            fclose(sf);
            fprintf(stderr, "  svg=%s\n", svg_path);
        }
    }

done:
    bcomp1_result_clear(&result3);
    bcomp1_result_clear(&sym_result);
    bcomp1_context_clear(&ctx);
    return 0;
}
