/*
 * surrounding.c
 *
 * Enumerate all distinct surroundings of the reflected hat tile (anti-hat):
 * configurations of at least MIN_SUR unreflected hat tiles where every tile
 * shares at least one vertex with the anti-hat center (vertex contact, not
 * necessarily edge contact) and the aggregate has no holes (no enclosed empty
 * regions).  Two configurations are considered the same if their combined
 * aggregate shapes are related by any symmetry of the tetrille lattice.
 *
 * The center anti-hat is drawn in coral; surrounding hats in light blue.
 *
 * Build:  make surrounding
 * Run:    ./bin/surrounding [--tile tiles/hat.tile] [--out surrounding.svg]
 *                           [--min N] [--maximal]
 *
 * --maximal  Only output configurations that cannot be further extended
 *            (every remaining center edge is geometrically blocked).
 *            Without this flag all configurations with >= N tiles are shown.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/attach.h"
#include "core/boundary.h"
#include "core/cycle.h"
#include "core/hash.h"
#include "core/lattice.h"
#include "core/tile.h"

/* ------------------------------------------------------------------ sizes */

#define HAT_NMAX  20    /* hat tile has 14 vertices; 20 is a safe bound */
#define MAX_SUR   18    /* maximum tiles that can surround the anti-hat  */
#define MAX_CFGS  5000  /* cap on stored configurations                  */

/* ------------------------------------------------------------------ types */

/* A single tile cycle stored compactly for SVG rendering. */
typedef struct {
    int    n;
    Coord  v[HAT_NMAX];
    int    reflected;   /* 1 = anti-hat, 0 = hat */
} TileCyc;

/* One surrounding configuration (center is implicit, always g_center). */
typedef struct {
    int     sur_count;
    TileCyc sur[MAX_SUR];
} Config;

/* ------------------------------------------------------------------ globals */

static Tile      g_tile;
static int       g_refl[MAX_VARIANTS]; /* g_refl[vi] = 1 if variant vi is anti-hat */
static Cycle     g_center;             /* the anti-hat center cycle                 */
static Coord     g_cverts[32];         /* vertices of the center (14 for the hat)   */
static int       g_cvert_n;
static HashTable g_seen;               /* canonical-aggregate dedup table           */
static Config    g_cfgs[MAX_CFGS];
static int       g_cfg_n;
static int       g_min_sur;            /* minimum surrounding tile count            */
static int       g_maximal_only;       /* 1 = only record maximal surroundings      */

/* DFS path: g_path[d] is the tile added at DFS depth d. */
static TileCyc   g_path[MAX_SUR];

/*
 * Per-depth aggregate polys.  g_agg[d] holds the combined shape of the
 * center plus the d surrounding tiles placed so far.  At depth d, the DFS
 * tries to grow g_agg[d] by one tile and writes the result to g_agg[d+1]
 * before recursing.  Deeper levels never overwrite g_agg[d], so the pointer
 * stays valid across the recursive call.
 */
static Poly      g_agg[MAX_SUR + 2];

/* Single shared canonical-form temp (overwritten at each call, used immediately). */
static Poly      g_canon_tmp;

/* Single shared aligned-tile temp (copied into g_path before recursing). */
static Cycle     g_aligned_tmp;

/* ------------------------------------------------------------------ helpers */

/* True if either endpoint of e is a vertex of the center anti-hat. */
static int has_center_vertex(Edge e) {
    for (int i = 0; i < g_cvert_n; i++)
        if (coord_eq(e.a, g_cverts[i]) || coord_eq(e.b, g_cverts[i]))
            return 1;
    return 0;
}

/*
 * Mark each stored variant as "reflected" (anti-hat) or not by comparing it
 * to the six pure-rotation transforms of the base cycle.  Variants matching
 * transforms t=0..5 are non-reflected; the rest (t=6..11) are reflected.
 */
static void identify_reflected(void) {
    for (int vi = 0; vi < g_tile.variant_count; vi++) {
        g_refl[vi] = 1;
        for (int t = 0; t < 6; t++) {
            Cycle tmp;
            cycle_transform_lattice(&g_tile.base, &tmp, g_tile.lattice, t);
            if (cycle_signed_area2(&tmp, g_tile.lattice) < 0)
                cycle_reverse(&tmp);
            cycle_normalize_position(&tmp, g_tile.lattice);
            if (!cycle_less(&tmp, &g_tile.variants[vi]) &&
                !cycle_less(&g_tile.variants[vi], &tmp)) {
                g_refl[vi] = 0;
                break;
            }
        }
    }
}

static void store_config(int n) {
    if (g_cfg_n >= MAX_CFGS) return;
    Config *c = &g_cfgs[g_cfg_n++];
    c->sur_count = n;
    memcpy(c->sur, g_path, (size_t)n * sizeof(TileCyc));
}

/* ------------------------------------------------------------------ DFS */

static void dfs(int depth) {
    /*
     * Dedup: each canonical aggregate state is explored at most once, even
     * when reachable via different tile-insertion orders.
     */
    poly_hash_key_lattice(&g_agg[depth], g_tile.lattice, &g_canon_tmp);
    if (!hash_insert(&g_seen, &g_canon_tmp))
        return;

    if (depth >= MAX_SUR) {
        if (depth >= g_min_sur) store_config(depth);
        return;
    }

    const Poly *agg = &g_agg[depth];
    Edge edges[256];
    int ec = build_boundary_edges(agg, edges);

    /* Try every extension via a center boundary edge. */
    int extended = 0;
    for (int be = 0; be < ec; be++) {
        if (!has_center_vertex(edges[be]))
            continue;
        for (int vi = 0; vi < g_tile.variant_count; vi++) {
            if (g_refl[vi]) continue;   /* only unreflected hats */
            for (int te = 0; te < g_tile.variants[vi].n; te++) {
                if (!try_attach_tile_poly_ex(agg, &g_tile.variants[vi],
                                             g_tile.lattice, be, te,
                                             &g_agg[depth + 1],
                                             &g_aligned_tmp))
                    continue;
                if (g_agg[depth + 1].cycle_count > 1) continue; /* hole created */
                extended = 1;

                int n = g_aligned_tmp.n;
                if (n > HAT_NMAX) n = HAT_NMAX;
                g_path[depth].n         = n;
                g_path[depth].reflected = g_refl[vi];
                for (int i = 0; i < n; i++)
                    g_path[depth].v[i] = g_aligned_tmp.v[i];

                dfs(depth + 1);
            }
        }
    }

    /*
     * Record this configuration if it qualifies.  In maximal mode we only
     * record when no further extension touching the center is possible.
     */
    if (depth >= g_min_sur && (!g_maximal_only || !extended))
        store_config(depth);
}

/* ------------------------------------------------------------------ SVG */

#define SVG_UNIT   24.0   /* logical-unit to pixel scale              */
#define SVG_MARGIN 18.0   /* cell padding in pixels                   */
#define SVG_SW      1.2   /* base stroke width                        */

static double g_r3;   /* sqrt(3) */

/* Convert a tetrille lattice point to screen x using the hat.tile basis. */
static double cx(Coord p) {
    if (p.v == 6) return (g_r3 * 0.5) * p.x + (g_r3 * 0.5) * p.y;
    if (p.v == 4) return (g_r3 * 0.25) * p.x + (g_r3 * 0.25) * p.y;
    if (p.v == 3) return (1.0 / g_r3) * p.x + (0.5 / g_r3) * p.y;
    return (double)p.x;
}

/* Convert a tetrille lattice point to screen y (logical, not SVG-flipped). */
static double cy_fn(Coord p) {
    if (p.v == 6) return -0.5 * p.x + 0.5 * p.y;
    if (p.v == 4) return -0.25 * p.x + 0.25 * p.y;
    if (p.v == 3) return 0.5 * p.y;
    return (double)p.y;
}

static void tile_bbox(const TileCyc *t,
                      double *x0, double *y0, double *x1, double *y1) {
    for (int i = 0; i < t->n; i++) {
        double x = cx(t->v[i]), y = cy_fn(t->v[i]);
        if (x < *x0) *x0 = x;
        if (x > *x1) *x1 = x;
        if (y < *y0) *y0 = y;
        if (y > *y1) *y1 = y;
    }
}

/* Draw one tile as an SVG filled polygon.  SVG y increases downward, so we
   use (oy - SVG_UNIT*cy_fn(v)) to flip the y-axis. */
static void draw_tile(FILE *fp, const TileCyc *t,
                      double ox, double oy,
                      const char *fill, const char *stroke, double sw) {
    if (t->n <= 0) return;
    fprintf(fp, "<path d=\"M %.3f,%.3f",
            ox + SVG_UNIT * cx(t->v[0]),
            oy - SVG_UNIT * cy_fn(t->v[0]));
    for (int i = 1; i < t->n; i++)
        fprintf(fp, " L %.3f,%.3f",
                ox + SVG_UNIT * cx(t->v[i]),
                oy - SVG_UNIT * cy_fn(t->v[i]));
    fprintf(fp,
            " Z\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%.2f\""
            " stroke-linejoin=\"round\"/>\n",
            fill, stroke, sw);
}

/* Choose grid dimensions for n cells to approximate a 16:9 aspect ratio. */
static void choose_grid(int n, double cell_w, double cell_h,
                        int *rows, int *cols) {
    double best = 1e18;
    *rows = 1;
    *cols = n > 0 ? n : 1;
    for (int r = 1; r <= n; r++) {
        int c = (n + r - 1) / r;
        double sc = fabs((c * cell_w) / (r * cell_h) - 16.0 / 9.0);
        if (sc < best) { best = sc; *rows = r; *cols = c; }
    }
}

static int cmp_cfg(const void *a, const void *b) {
    return ((const Config *)a)->sur_count - ((const Config *)b)->sur_count;
}

static void emit_svg(FILE *fp) {
    if (!g_cfg_n) {
        fprintf(fp,
            "<svg xmlns=\"http://www.w3.org/2000/svg\""
            " width=\"320\" height=\"60\">"
            "<text x=\"10\" y=\"40\" font-size=\"18\""
            " font-family=\"sans-serif\">No surroundings found.</text>"
            "</svg>\n");
        return;
    }

    qsort(g_cfgs, g_cfg_n, sizeof(Config), cmp_cfg);

    /* Build center TileCyc from the global center cycle. */
    TileCyc ctr;
    ctr.n         = g_center.n < HAT_NMAX ? g_center.n : HAT_NMAX;
    ctr.reflected = 1;
    for (int i = 0; i < ctr.n; i++) ctr.v[i] = g_center.v[i];

    /* Global bounding box over all configurations. */
    double gx0 =  1e18, gy0 =  1e18;
    double gx1 = -1e18, gy1 = -1e18;
    tile_bbox(&ctr, &gx0, &gy0, &gx1, &gy1);
    for (int i = 0; i < g_cfg_n; i++)
        for (int k = 0; k < g_cfgs[i].sur_count; k++)
            tile_bbox(&g_cfgs[i].sur[k], &gx0, &gy0, &gx1, &gy1);

    double bw = gx1 - gx0, bh = gy1 - gy0;
    if (bw < 1e-9) bw = 1.0;
    if (bh < 1e-9) bh = 1.0;

    const double LABEL_H = 16.0;
    double cell_w = 2.0 * SVG_MARGIN + SVG_UNIT * bw;
    double cell_h = 2.0 * SVG_MARGIN + SVG_UNIT * bh + LABEL_H;

    int rows, cols;
    choose_grid(g_cfg_n, cell_w, cell_h, &rows, &cols);
    double W = cols * cell_w, H = rows * cell_h;

    fprintf(fp,
            "<svg xmlns=\"http://www.w3.org/2000/svg\""
            " width=\"%.0f\" height=\"%.0f\""
            " viewBox=\"0 0 %.0f %.0f\">\n",
            W, H, W, H);
    fprintf(fp, "<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>\n");

    /* Title as comment. */
    fprintf(fp, "<!-- Surroundings of the reflected hat tile"
            " (n >= %d adjacent tiles) -->\n", g_min_sur);

    for (int i = 0; i < g_cfg_n; i++) {
        int row = i / cols, col = i % cols;
        /* Offset so the drawing bbox is centred in the cell (minus label). */
        double draw_h = cell_h - LABEL_H;
        double ox = col * cell_w + (cell_w - SVG_UNIT * bw) * 0.5 - SVG_UNIT * gx0;
        double oy = row * cell_h + (draw_h - SVG_UNIT * bh) * 0.5 + SVG_UNIT * gy1;

        const Config *cfg = &g_cfgs[i];

        /* Draw surrounding tiles first (they go behind the center). */
        for (int k = 0; k < cfg->sur_count; k++) {
            const char *fill = cfg->sur[k].reflected
                ? "#4a90d9"   /* anti-hat: steel blue  */
                : "#a8d8ea";  /* hat:      light blue  */
            draw_tile(fp, &cfg->sur[k], ox, oy,
                      fill, "#555555", SVG_SW * 0.7);
        }

        /* Center (anti-hat) on top in coral. */
        draw_tile(fp, &ctr, ox, oy,
                  "#e74c3c", "#1a1a1a", SVG_SW * 1.2);

        /* Label below the drawing. */
        fprintf(fp,
                "<text x=\"%.1f\" y=\"%.1f\""
                " text-anchor=\"middle\" font-size=\"10\""
                " font-family=\"sans-serif\" fill=\"#444\">n=%d</text>\n",
                col * cell_w + cell_w * 0.5,
                row * cell_h + cell_h - 3.0,
                cfg->sur_count);
    }

    /* Legend strip at the very bottom of the last row. */
    double legend_y = H - LABEL_H + 2.0;
    double legend_x = 10.0;
    double sq = 9.0;

    fprintf(fp, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\""
            " fill=\"#e74c3c\" stroke=\"#1a1a1a\" stroke-width=\"0.8\"/>"
            "<text x=\"%.1f\" y=\"%.1f\" font-size=\"9\""
            " font-family=\"sans-serif\" fill=\"#222\">center (anti-hat)</text>\n",
            legend_x, legend_y, sq, sq,
            legend_x + sq + 3.0, legend_y + sq - 1.0);
    legend_x += 110.0;
    fprintf(fp, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\""
            " fill=\"#a8d8ea\" stroke=\"#555\" stroke-width=\"0.8\"/>"
            "<text x=\"%.1f\" y=\"%.1f\" font-size=\"9\""
            " font-family=\"sans-serif\" fill=\"#222\">hat (surrounding)</text>\n",
            legend_x, legend_y, sq, sq,
            legend_x + sq + 3.0, legend_y + sq - 1.0);

    fprintf(fp, "</svg>\n");
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv) {
    const char *tile_path = "tiles/hat.tile";
    const char *out_path  = "surrounding.svg";
    g_min_sur      = 7;
    g_maximal_only = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--tile") && i + 1 < argc)
            tile_path = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out_path = argv[++i];
        else if (!strcmp(argv[i], "--min") && i + 1 < argc)
            g_min_sur = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--maximal"))
            g_maximal_only = 1;
    }

    if (!tile_load(tile_path, &g_tile)) {
        fprintf(stderr, "Failed to load tile: %s\n", tile_path);
        return 1;
    }
    g_r3 = sqrt(3.0);

    /* Build the anti-hat center: apply reflection transform (t=6) to base. */
    {
        Cycle tmp;
        cycle_transform_lattice(&g_tile.base, &tmp, g_tile.lattice, 6);
        if (cycle_signed_area2(&tmp, g_tile.lattice) < 0)
            cycle_reverse(&tmp);
        cycle_normalize_position(&tmp, g_tile.lattice);
        g_center = tmp;
    }

    /* Seed aggregate: single anti-hat tile. */
    g_agg[0].cycle_count = 1;
    g_agg[0].cycles[0]   = g_center;

    /* Record the center's vertices. */
    g_cvert_n = g_center.n < 32 ? g_center.n : 32;
    for (int i = 0; i < g_cvert_n; i++)
        g_cverts[i] = g_center.v[i];

    identify_reflected();

    int refl_count = 0;
    for (int i = 0; i < g_tile.variant_count; i++)
        if (g_refl[i]) refl_count++;

    fprintf(stderr, "=== Hat Tile Surroundings ===\n");
    fprintf(stderr, "Tile file:       %s\n", tile_path);
    fprintf(stderr, "Variants:        %d total"
            " (%d hat, %d anti-hat)\n",
            g_tile.variant_count,
            g_tile.variant_count - refl_count, refl_count);
    fprintf(stderr, "Center vertices: %d\n", g_cvert_n);
    fprintf(stderr, "Min surrounding: %d\n", g_min_sur);
    fprintf(stderr, "Mode:            %s\n\n",
            g_maximal_only ? "maximal only" : "all");

    hash_init(&g_seen, 256);

    dfs(0);

    fprintf(stderr, "Configurations:  %d\n", g_cfg_n);
    fprintf(stderr, "States visited:  %zu\n\n", g_seen.count);

    if (g_cfg_n >= MAX_CFGS)
        fprintf(stderr, "WARNING: reached MAX_CFGS=%d cap;"
                " output may be incomplete.\n\n", MAX_CFGS);

    /* Distribution histogram. */
    fprintf(stderr, "Distribution by surrounding count:\n");
    for (int k = g_min_sur; k <= MAX_SUR; k++) {
        int cnt = 0;
        for (int i = 0; i < g_cfg_n; i++)
            if (g_cfgs[i].sur_count == k) cnt++;
        if (cnt) fprintf(stderr, "  n=%2d: %d\n", k, cnt);
    }

    FILE *fp = fopen(out_path, "w");
    if (!fp) {
        perror(out_path);
        hash_destroy(&g_seen);
        return 1;
    }
    emit_svg(fp);
    fclose(fp);

    fprintf(stderr, "\nOutput: %s\n", out_path);

    hash_destroy(&g_seen);
    return 0;
}
