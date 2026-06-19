/*
 * surrounding2.c
 *
 * Enumerate all distinct surroundings of a two-hat center cluster.  The
 * center consists of two unreflected hat tiles in "tip-into-notch"
 * configuration: vertex 9 (the concave notch) of the first hat coincides
 * with vertex 1 (the convex tip) of the second hat, sharing two boundary
 * edges.
 *
 * Surrounding tiles may be either reflected (anti-hat) or unreflected (hat).
 * The aggregate must remain hole-free throughout the search.
 *
 * Compact-growth constraint: while any boundary edge of the current aggregate
 * has an endpoint on the center cluster, only those "inner" edges are eligible
 * for tile attachment.  Expansion to "outer" edges (touching only already-placed
 * surrounding tiles) is deferred until all center-adjacent positions are filled
 * or geometrically blocked.
 *
 * Configurations with at least MIN_SUR surrounding tiles (default: 10) are
 * stored and rendered into an SVG grid.
 *
 * Build:  make surrounding2
 * Run:    ./bin/surrounding2 [--tile tiles/hat.tile] [--out surrounding2.svg]
 *                            [--min N] [--max-depth N] [--maximal]
 *                            [--max-states N]
 *
 * --min N        Minimum surrounding tile count to record (default: 10).
 * --max-depth N  DFS depth cap, i.e. max surrounding tiles (default: 26).
 * --maximal      Only output configurations where no further tile can be placed.
 * --max-states N Cap on distinct DFS states (default: 2000000; 0 = unlimited).
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/attach.h"
#include "core/boundary.h"
#include "core/cycle.h"
#include "core/lattice.h"
#include "core/tile.h"

/* ------------------------------------------------------------------ sizes */

#define HAT_NMAX  20    /* hat tile has 14 vertices; 20 is a safe bound */
#define MAX_SUR   26    /* depth cap; two-hat center can admit more tiles */
#define MAX_CFGS  5000  /* cap on stored configurations                   */

/* ------------------------------------------------------------------ types */

typedef struct {
    int   n;
    Coord v[HAT_NMAX];
    int   reflected;   /* 1 = anti-hat, 0 = hat */
} TileCyc;

typedef struct {
    int     sur_count;
    TileCyc sur[MAX_SUR];
} Config;

/* ------------------------------------------------------------------ globals */

static Tile      g_tile;
static int       g_refl[MAX_VARIANTS];  /* 1 if variant is anti-hat         */
static TileCyc   g_center_tiles[2];     /* the two center hats (for SVG)     */
static Coord     g_cverts[64];          /* all vertices of both center tiles  */
static int       g_cvert_n;
static Config    g_cfgs[MAX_CFGS];
static int       g_cfg_n;
static int       g_min_sur;
static int       g_max_depth;    /* DFS depth cap (max surrounding tiles)   */
static int       g_maximal_only;
static size_t    g_max_states;

static TileCyc   g_path[MAX_SUR];
static Poly      g_agg[MAX_SUR + 2];
static Poly      g_canon_tmp;
static Cycle     g_aligned_tmp;

/*
 * Lightweight fingerprint-based dedup.  Stores 64-bit FNV-1a hashes of
 * canonical poly forms rather than full Poly objects (8 bytes/state vs
 * ~144 KB/state with the standard HashTable).  False-collision probability
 * is negligible (<1e-8 for 2M states).
 */
static uint64_t *g_fp_table  = NULL;
static size_t    g_fp_size   = 0;    /* number of slots (power of two) */
static size_t    g_fp_count  = 0;    /* states visited                  */

static uint64_t poly_fingerprint(const Poly *p) {
    uint64_t h = 14695981039346656037ULL;
    const uint64_t prime = 1099511628211ULL;
    h ^= (uint64_t)(unsigned)p->cycle_count; h *= prime;
    for (int k = 0; k < p->cycle_count; k++) {
        const Cycle *c = &p->cycles[k];
        h ^= (uint64_t)(unsigned)c->n; h *= prime;
        for (int i = 0; i < c->n; i++) {
            h ^= (uint64_t)(unsigned)(c->v[i].v + 10000); h *= prime;
            h ^= (uint64_t)(unsigned)(c->v[i].x + 10000); h *= prime;
            h ^= (uint64_t)(unsigned)(c->v[i].y + 10000); h *= prime;
        }
    }
    return h ? h : 1u;   /* 0 is the "empty" sentinel */
}

/* Returns 1 if fp is new (inserted), 0 if already present. */
static int fp_insert(uint64_t fp) {
    if (!g_fp_table) {
        g_fp_size  = 1u << 18;   /* 256k slots = 2 MB initial */
        g_fp_table = calloc(g_fp_size, sizeof(uint64_t));
        if (!g_fp_table) { fputs("OOM\n", stderr); exit(1); }
    }
    /* Resize when load exceeds 70%. */
    if (g_fp_count * 10 >= g_fp_size * 7) {
        size_t ns = g_fp_size * 2;
        uint64_t *nt = calloc(ns, sizeof(uint64_t));
        if (!nt) { fputs("OOM\n", stderr); exit(1); }
        for (size_t i = 0; i < g_fp_size; i++) {
            if (!g_fp_table[i]) continue;
            size_t j = g_fp_table[i] & (ns - 1);
            while (nt[j]) j = (j + 1) & (ns - 1);
            nt[j] = g_fp_table[i];
        }
        free(g_fp_table); g_fp_table = nt; g_fp_size = ns;
    }
    size_t s = fp & (g_fp_size - 1);
    while (g_fp_table[s]) {
        if (g_fp_table[s] == fp) return 0;
        s = (s + 1) & (g_fp_size - 1);
    }
    g_fp_table[s] = fp;
    g_fp_count++;
    return 1;
}

/* ------------------------------------------------------------------ helpers */

static int has_center_vertex(Edge e) {
    for (int i = 0; i < g_cvert_n; i++)
        if (coord_eq(e.a, g_cverts[i]) || coord_eq(e.b, g_cverts[i]))
            return 1;
    return 0;
}

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
                g_refl[vi] = 0; break;
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

static void add_center_vertex(Coord vert) {
    for (int i = 0; i < g_cvert_n; i++)
        if (coord_eq(g_cverts[i], vert)) return;
    if (g_cvert_n < 64)
        g_cverts[g_cvert_n++] = vert;
}

/* ------------------------------------------------------------------ DFS */

static void dfs(int depth) {
    if (g_max_states && g_fp_count >= g_max_states) return;

    poly_hash_key_lattice(&g_agg[depth], g_tile.lattice, &g_canon_tmp);
    uint64_t fp = poly_fingerprint(&g_canon_tmp);
    if (!fp_insert(fp)) return;

    /*
     * Non-maximal: record at ENTRY before any recursive call so that the
     * state cap cannot exhaust itself entirely at deeper levels and prevent
     * recording shallow configurations.
     */
    if (!g_maximal_only && depth >= g_min_sur)
        store_config(depth);

    if (depth >= g_max_depth) {
        /* Depth cap counts as "cannot extend" for the maximal mode. */
        if (g_maximal_only && depth >= g_min_sur)
            store_config(depth);
        return;
    }

    const Poly *agg = &g_agg[depth];
    Edge edges[512];
    int ec = build_boundary_edges(agg, edges);

    /*
     * Two-pass compact growth:
     *   Pass 1 — inner edges: those with an endpoint on the center cluster.
     *             Fills the immediate ring first.
     *   Pass 2 — outer edges: tried ONLY when no inner placement succeeded,
     *             i.e. the inner ring is either fully enclosed (inner_n = 0)
     *             or geometrically blocked.
     *
     * Vertex-type pre-filter (applied in both passes):
     *   On the tetrille lattice each vertex has valence v ∈ {3, 4, 6}.
     *   Attachment at (be, te) requires variant.v[te] to land on edges[be].b
     *   and variant.v[(te+1)%n] to land on edges[be].a.  Lattice positions
     *   with different valences can never coincide, so we skip (vi, te) pairs
     *   whose valences do not match before calling the expensive
     *   try_attach_tile_poly_ex.  This prunes ~80 % of candidate calls.
     */
    int extended = 0;

#define TRY_ATTACH(BE, VI, TE) do {                                        \
    const Cycle *_var = &g_tile.variants[(VI)];                            \
    int _te1 = ((TE) + 1) % _var->n;                                      \
    if (_var->v[(TE)].v != edges[(BE)].b.v) break;                        \
    if (_var->v[_te1 ].v != edges[(BE)].a.v) break;                       \
    if (!try_attach_tile_poly_ex(agg, _var,                                \
                                  g_tile.lattice, (BE), (TE),              \
                                  &g_agg[depth + 1], &g_aligned_tmp))     \
        break;                                                             \
    if (g_agg[depth + 1].cycle_count > 1) break; /* hole */              \
    extended = 1;                                                          \
    int _n = g_aligned_tmp.n > HAT_NMAX ? HAT_NMAX : g_aligned_tmp.n;    \
    g_path[depth].n         = _n;                                          \
    g_path[depth].reflected = g_refl[(VI)];                               \
    for (int _i = 0; _i < _n; _i++)                                       \
        g_path[depth].v[_i] = g_aligned_tmp.v[_i];                       \
    dfs(depth + 1);                                                        \
    if (g_max_states && g_fp_count >= g_max_states) return;               \
} while (0)

    /* --- Pass 1: inner edges --- */
    for (int be = 0; be < ec; be++) {
        if (!has_center_vertex(edges[be])) continue;
        for (int vi = 0; vi < g_tile.variant_count; vi++)
            for (int te = 0; te < g_tile.variants[vi].n; te++)
                TRY_ATTACH(be, vi, te);
    }

    /* --- Pass 2: outer edges (fall-back when inner is exhausted/blocked) --- */
    if (!extended) {
        for (int be = 0; be < ec; be++) {
            if (has_center_vertex(edges[be])) continue;
            for (int vi = 0; vi < g_tile.variant_count; vi++)
                for (int te = 0; te < g_tile.variants[vi].n; te++)
                    TRY_ATTACH(be, vi, te);
        }
    }

#undef TRY_ATTACH

    if (g_maximal_only && depth >= g_min_sur && !extended)
        store_config(depth);
}

/* ------------------------------------------------------------------ SVG */

#define SVG_UNIT   24.0
#define SVG_MARGIN 18.0
#define SVG_SW      1.2

static double g_r3;

static double cx(Coord p) {
    if (p.v == 6) return (g_r3 * 0.5) * p.x + (g_r3 * 0.5) * p.y;
    if (p.v == 4) return (g_r3 * 0.25) * p.x + (g_r3 * 0.25) * p.y;
    if (p.v == 3) return (1.0 / g_r3) * p.x + (0.5 / g_r3) * p.y;
    return (double)p.x;
}
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
    fprintf(fp, " Z\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%.2f\""
            " stroke-linejoin=\"round\"/>\n", fill, stroke, sw);
}

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
        fprintf(fp, "<svg xmlns=\"http://www.w3.org/2000/svg\""
                " width=\"320\" height=\"60\">"
                "<text x=\"10\" y=\"40\" font-size=\"18\""
                " font-family=\"sans-serif\">No surroundings found.</text>"
                "</svg>\n");
        return;
    }

    qsort(g_cfgs, g_cfg_n, sizeof(Config), cmp_cfg);

    double gx0 =  1e18, gy0 =  1e18;
    double gx1 = -1e18, gy1 = -1e18;
    tile_bbox(&g_center_tiles[0], &gx0, &gy0, &gx1, &gy1);
    tile_bbox(&g_center_tiles[1], &gx0, &gy0, &gx1, &gy1);
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

    fprintf(fp, "<svg xmlns=\"http://www.w3.org/2000/svg\""
            " width=\"%.0f\" height=\"%.0f\""
            " viewBox=\"0 0 %.0f %.0f\">\n", W, H, W, H);
    fprintf(fp, "<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>\n");
    fprintf(fp, "<!-- Two-hat center cluster surroundings (n >= %d tiles) -->\n",
            g_min_sur);

    for (int i = 0; i < g_cfg_n; i++) {
        int row = i / cols, col = i % cols;
        double draw_h = cell_h - LABEL_H;
        double ox = col * cell_w + (cell_w - SVG_UNIT * bw) * 0.5 - SVG_UNIT * gx0;
        double oy = row * cell_h + (draw_h - SVG_UNIT * bh) * 0.5 + SVG_UNIT * gy1;

        const Config *cfg = &g_cfgs[i];

        for (int k = 0; k < cfg->sur_count; k++) {
            const char *fill = cfg->sur[k].reflected
                ? "#4a90d9"   /* anti-hat: steel blue */
                : "#a8d8ea";  /* hat:      light blue */
            draw_tile(fp, &cfg->sur[k], ox, oy,
                      fill, "#555555", SVG_SW * 0.7);
        }

        /* Center cluster on top in two shades of amber. */
        draw_tile(fp, &g_center_tiles[0], ox, oy,
                  "#f39c12", "#1a1a1a", SVG_SW * 1.2);
        draw_tile(fp, &g_center_tiles[1], ox, oy,
                  "#e67e22", "#1a1a1a", SVG_SW * 1.2);

        fprintf(fp, "<text x=\"%.1f\" y=\"%.1f\""
                " text-anchor=\"middle\" font-size=\"10\""
                " font-family=\"sans-serif\" fill=\"#444\">n=%d</text>\n",
                col * cell_w + cell_w * 0.5,
                row * cell_h + cell_h - 3.0,
                cfg->sur_count);
    }

    /* Legend. */
    double legend_y = H - LABEL_H + 2.0;
    double legend_x = 10.0;
    double sq = 9.0;

#define LEG(fill, stroke, label) do { \
    fprintf(fp, "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\"" \
            " fill=\"%s\" stroke=\"%s\" stroke-width=\"0.8\"/>" \
            "<text x=\"%.1f\" y=\"%.1f\" font-size=\"9\"" \
            " font-family=\"sans-serif\" fill=\"#222\">%s</text>\n", \
            legend_x, legend_y, sq, sq, (fill), (stroke), \
            legend_x + sq + 3.0, legend_y + sq - 1.0, (label)); \
} while(0)

    LEG("#f39c12", "#1a1a1a", "center hat 1"); legend_x += 90.0;
    LEG("#e67e22", "#1a1a1a", "center hat 2"); legend_x += 90.0;
    LEG("#a8d8ea", "#555",    "hat");          legend_x += 50.0;
    LEG("#4a90d9", "#333",    "anti-hat");
#undef LEG

    fprintf(fp, "</svg>\n");
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv) {
    const char *tile_path = "tiles/hat.tile";
    const char *out_path  = "surrounding2.svg";
    g_min_sur      = 10;
    g_max_depth    = MAX_SUR;
    g_maximal_only = 0;
    g_max_states   = 2000000;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--tile") && i + 1 < argc)
            tile_path = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out_path = argv[++i];
        else if (!strcmp(argv[i], "--min") && i + 1 < argc)
            g_min_sur = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max-depth") && i + 1 < argc)
            g_max_depth = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--maximal"))
            g_maximal_only = 1;
        else if (!strcmp(argv[i], "--max-states") && i + 1 < argc)
            g_max_states = (size_t)atol(argv[++i]);
    }

    if (!tile_load(tile_path, &g_tile)) {
        fprintf(stderr, "Failed to load tile: %s\n", tile_path);
        return 1;
    }
    g_r3 = sqrt(3.0);

    identify_reflected();

    /*
     * Build the two-hat center cluster: hat (base) + hat (tip-into-notch).
     *
     * Boundary edge 8 of the base cycle is v[8]→v[9] (the notch approach).
     * Attaching a variant at te=1 places that variant's vertex 1 exactly on
     * vertex 9 of the base, giving the "tip-into-notch" configuration.
     * (be=9, te=0 is tried as fallback for the same result via the other
     * notch edge.)
     */
    {
        Poly hat1;
        hat1.cycle_count = 1;
        hat1.cycles[0]   = g_tile.base;

        static const int BE_TE[][2] = { {8, 1}, {9, 0} };
        int found = 0;

        for (int attempt = 0; attempt < 2 && !found; attempt++) {
            int be_try = BE_TE[attempt][0];
            int te_try = BE_TE[attempt][1];

            for (int vi = 0; vi < g_tile.variant_count && !found; vi++) {
                if (g_refl[vi]) continue;

                Cycle aligned;
                if (!try_attach_tile_poly_ex(&hat1, &g_tile.variants[vi],
                                             g_tile.lattice,
                                             be_try, te_try,
                                             &g_agg[0], &aligned))
                    continue;
                if (g_agg[0].cycle_count != 1)
                    continue;

                g_center_tiles[0].n         = g_tile.base.n < HAT_NMAX
                                              ? g_tile.base.n : HAT_NMAX;
                g_center_tiles[0].reflected = 0;
                for (int i = 0; i < g_center_tiles[0].n; i++)
                    g_center_tiles[0].v[i] = g_tile.base.v[i];

                g_center_tiles[1].n         = aligned.n < HAT_NMAX
                                              ? aligned.n : HAT_NMAX;
                g_center_tiles[1].reflected = 0;
                for (int i = 0; i < g_center_tiles[1].n; i++)
                    g_center_tiles[1].v[i] = aligned.v[i];

                g_cvert_n = 0;
                for (int i = 0; i < g_tile.base.n; i++)
                    add_center_vertex(g_tile.base.v[i]);
                for (int i = 0; i < aligned.n; i++)
                    add_center_vertex(aligned.v[i]);

                found = 1;
                fprintf(stderr,
                        "Center cluster: be=%d te=%d vi=%d"
                        "  center vertices=%d\n",
                        be_try, te_try, vi, g_cvert_n);
            }
        }

        if (!found) {
            fprintf(stderr,
                    "Error: could not construct tip-into-notch cluster.\n");
            return 1;
        }
    }

    int refl_count = 0;
    for (int i = 0; i < g_tile.variant_count; i++)
        if (g_refl[i]) refl_count++;

    fprintf(stderr, "\n=== Two-Hat Center Cluster Surroundings ===\n");
    fprintf(stderr, "Tile file:       %s\n", tile_path);
    fprintf(stderr, "Variants:        %d total (%d hat, %d anti-hat)\n",
            g_tile.variant_count,
            g_tile.variant_count - refl_count, refl_count);
    fprintf(stderr, "Center vertices: %d\n", g_cvert_n);
    fprintf(stderr, "Min surrounding: %d\n", g_min_sur);
    fprintf(stderr, "Max depth:       %d\n", g_max_depth);
    fprintf(stderr, "Mode:            %s\n",
            g_maximal_only ? "maximal only" : "all");
    if (g_max_states)
        fprintf(stderr, "State cap:       %zu\n", g_max_states);
    fprintf(stderr, "\n");

    dfs(0);

    fprintf(stderr, "Configurations:  %d\n", g_cfg_n);
    fprintf(stderr, "States visited:  %zu\n\n", g_fp_count);

    if (g_cfg_n >= MAX_CFGS)
        fprintf(stderr, "WARNING: reached MAX_CFGS=%d cap;"
                " output may be incomplete.\n\n", MAX_CFGS);
    if (g_max_states && g_fp_count >= g_max_states)
        fprintf(stderr, "WARNING: reached --max-states=%zu cap;"
                " output may be incomplete.\n\n", g_max_states);

    fprintf(stderr, "Distribution by surrounding count:\n");
    for (int k = g_min_sur; k <= MAX_SUR; k++) {
        int cnt = 0;
        for (int i = 0; i < g_cfg_n; i++)
            if (g_cfgs[i].sur_count == k) cnt++;
        if (cnt) fprintf(stderr, "  n=%2d: %d\n", k, cnt);
    }

    FILE *fp = fopen(out_path, "w");
    if (!fp) { perror(out_path); return 1; }
    emit_svg(fp);
    fclose(fp);

    fprintf(stderr, "\nOutput: %s\n", out_path);
    free(g_fp_table);
    return 0;
}
