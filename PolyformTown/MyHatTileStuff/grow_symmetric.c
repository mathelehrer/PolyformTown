/*
 * grow_symmetric.c - grow hat tile clusters around a 3-fold symmetric seed.
 *
 * The seed is a "pinwheel" of three hats all sharing vertex 0 (0-based),
 * rotated 0°, 120°, and 240°.  Vertex 0 of the hat is the valence-6 lattice
 * origin (6,0,0), which is a fixed point of every rotation.  The BComp1
 * machinery then grows N optional rings around that seed, but only
 * 120°-rotationally-symmetric completions are retained.
 *
 * Note: vertex 6 of the hat (the v=3 triangular vertex) also has 120° interior
 * angle, but that configuration is forbidden by the RL0 vertex-arc rules.
 * Vertex 0 (the v=6 hexagonal vertex at the coordinate origin) is the correct
 * center for the 3-fold symmetric pinwheel.
 *
 * Build:  make grow_symmetric
 * Run:    ./bin/grow_symmetric [--level N] [--tile preferences/focus.tile]
 *                              [--remembrance data/rl0/remembrance.dat]
 *                              [--deletions data/rl0/deletions.dat]
 *                              [--out-dir data/sym3] [--svg out.svg]
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

/* ---------------------------------------------------- seed construction */

/*
 * Build the 3-hat pinwheel seed sharing vertex 0 of the base hat.
 *
 * v[0] = (6,0,0) is the valence-6 coordinate origin: 120° rotation (t=2)
 * maps it to itself, so no translation is ever needed to keep the centre
 * at (6,0,0).
 *
 * All three rotations already satisfy aligned.v[0] == (6,0,0).  We attach
 * them in order and verify via the coord_eq guard.
 *
 * Returns 1 on success; writes the BComp1State to *seed and the 3-hat
 * union boundary to *center_out.
 */
static int build_symmetric_seed(const Tile *tile, BComp1State *seed,
                                 Cycle *center_out)
{
    /* The shared vertex is tile.base.v[0] = (6,0,0) = coordinate origin. */
    Coord center = tile->base.v[0];

    /* Rotated variants: t=0 (base), t=2 (120°), t=4 (240°).
     * All keep v[0] at the origin since (6,0,0) is a fixed point. */
    Cycle rot[SEED_HATS];
    rot[0] = tile->base;
    cycle_transform_lattice(&tile->base, &rot[1], tile->lattice, 2);
    cycle_transform_lattice(&tile->base, &rot[2], tile->lattice, 4);

    /* Build union polygon by attaching rot[1] and rot[2] one at a time,
     * keeping only (be,te) pairs where the aligned tile's v[0] == center. */
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
            fprintf(stderr,
                    "ERROR: cannot attach hat%d (no (be,te) places v[0] at origin)\n",
                    k + 1);
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

/* Returns 1 if cycles a and b are equal as oriented polygons (same vertex
 * sequence, allowing any cyclic rotation of the starting vertex). */
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

/*
 * Check whether *boundary is invariant under 120° rotation around the
 * coordinate origin (6,0,0), which is hat.v[0] and the fixed point of the
 * lattice's t=2 rotation.
 */
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

static void emit_svg(FILE *f, const BComp1RecordVec *recs,
                     const Cycle *center) {
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

        for (int t=SEED_HATS; t<rec->tiles_count; t++)
            draw_hat(f, &rec->tiles[t], ox, oy, SC, "#a8d8ea", "#555", SW*0.7);
        /* colour each seed hat differently to show the 3-fold structure */
        static const char *seed_fills[] = {"#f39c12", "#e74c3c", "#8e44ad"};
        for (int t=0; t<SEED_HATS && t<rec->tiles_count; t++)
            draw_hat(f, &rec->tiles[t], ox, oy, SC, seed_fills[t], "#1a1a1a", SW*1.1);
        draw_hat(f, center, ox, oy, SC, "none", "#c0392b", SW*1.8);

        fprintf(f, "<text x=\"%.1f\" y=\"%.1f\" text-anchor=\"middle\""
                   " font-size=\"9\" font-family=\"sans-serif\" fill=\"#444\">%d tiles</text>\n",
                col*cw+cw*0.5, row*ch+ch-3.0, rec->tiles_count);
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
    int level = 1;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i],"--tile")        && i+1<argc) tile_path        = argv[++i];
        else if (!strcmp(argv[i],"--remembrance") && i+1<argc) remembrance_path = argv[++i];
        else if (!strcmp(argv[i],"--deletions")   && i+1<argc) deletions_path   = argv[++i];
        else if (!strcmp(argv[i],"--out-dir")     && i+1<argc) out_dir          = argv[++i];
        else if (!strcmp(argv[i],"--level")       && i+1<argc) level            = atoi(argv[++i]);
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

    /* Build the 3-hat pinwheel seed (sharing vertex 0 = coordinate origin). */
    Cycle center;
    BComp1State seed;
    if (!build_symmetric_seed(&ctx.tile, &seed, &center)) {
        fprintf(stderr, "ERROR: could not build symmetric 3-hat seed\n");
        bcomp1_context_clear(&ctx); return 1;
    }
    fprintf(stderr,
            "Symmetric 3-hat seed (center=v[0]=(6,0,0), tiles=%d)\n",
            seed.tile_count);

    /* Forced deterministic ring. */
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

    /* Write supertile record. */
    char path[1024]; FILE *fp;
    BComp1Record supertile;
    if (!make_record(&seed, &center, &supertile)) {
        fputs("ERROR: make_record failed\n", stderr);
        bcomp1_context_clear(&ctx); return 1;
    }
    snprintf(path, sizeof(path), "%s/supertile.dat", out_dir);
    fp = fopen(path, "w");
    if (!fp) { perror(path); bcomp1_context_clear(&ctx); return 1; }
    bcomp1_print_record(fp, 1, &supertile);
    fclose(fp);

    /* Level-N growth. */
    BComp1Options opts;
    bcomp1_options_default(&opts);
    opts.depth           = level;
    opts.collect_records = 1;
    opts.live_only       = 1;

    BComp1Result result;
    if (!bcomp1_complete_state(&ctx, &seed, &center, &opts, &result)) {
        fputs("ERROR: completion search failed\n", stderr);
        bcomp1_context_clear(&ctx); return 1;
    }

    /* Filter in-place: keep only 120°-symmetric completions. */
    size_t total = result.records.count;
    size_t sym_count = 0;
    for (size_t i = 0; i < total; i++) {
        if (has_120_symmetry(&result.records.items[i].boundary, ctx.tile.lattice))
            result.records.items[sym_count++] = result.records.items[i];
    }
    result.records.count = sym_count;

    snprintf(path, sizeof(path), "%s/sym_completions_level%d.dat", out_dir, level);
    fp = fopen(path, "w");
    if (!fp) { perror(path); bcomp1_result_clear(&result); bcomp1_context_clear(&ctx); return 1; }
    bcomp1_sort_records(&result.records);
    for (size_t i = 0; i < result.records.count; i++)
        bcomp1_print_record(fp, i+1, &result.records.items[i]);
    fclose(fp);

    fprintf(stderr,
            "grow_symmetric  level=%d\n"
            "  forced tiles=%d  closure_steps=%d  unresolved=%d\n"
            "  completions=%zu  symmetric=%zu  ->  %s\n",
            level,
            seed.tile_count, cstats.closure_steps, cstats.unresolved_vertices,
            total, sym_count, path);

    if (svg_path) {
        FILE *sf = fopen(svg_path, "w");
        if (!sf) { perror(svg_path); }
        else {
            emit_svg(sf, &result.records, &center);
            fclose(sf);
            fprintf(stderr, "  svg=%s\n", svg_path);
        }
    }

    bcomp1_result_clear(&result);
    bcomp1_context_clear(&ctx);
    return 0;
}
