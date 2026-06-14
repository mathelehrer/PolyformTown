/*
 * h4_cluster.c
 *
 * Build clusters of hat tiles around an h4 metatile center, following the same
 * pipeline as src/rl2/generate_rl2.c but with tiles/h4.tile as the fixed center
 * of the cluster.
 *
 * The RL2 machinery (forced live closure + depth-1 completion search) operates
 * on a single tile type -- the hat -- and a dictionary of legal vertex arcs
 * (the RL0 forget map).  Every cycle handled by that machinery must therefore
 * be an individual hat.  The h4 metatile has exactly the area of four hats, so
 * the first step here is to DECOMPOSE h4 into its four constituent hat tiles;
 * those four hats form the seed BComp1State whose center is the h4 outline.
 *
 * From that seed the program then mirrors generate_rl2.c exactly:
 *
 *   1. attach0_force_live_closure  -- deterministically add every forced hat
 *      around the center (the canonical, hole-free surround).
 *   2. write the resulting supertile record.
 *   3. bcomp1_complete_state (depth 1, live only)  -- enumerate the distinct
 *      legal next-ring clusters (the "survivors").
 *   4. write those completions as a records .dat file.
 *
 * The .dat files use the standard BComp1Record format, so they render with the
 * existing pipeline into an SVG grid just like img/rl2_survivors.svg:
 *
 *   bin/rl1_depict preferences/focus.tile data/h4/completions.dat --grouped \
 *       | bin/imgtable > img/h4_survivors.svg
 *
 * (see the `h4_cluster_svg` Makefile target).
 *
 * Build:  make h4_cluster
 * Run:    ./bin/h4_cluster [--center tiles/h4.tile] [--tile preferences/focus.tile]
 *                          [--remembrance data/rl0/remembrance.dat]
 *                          [--deletions data/rl0/deletions.dat]
 *                          [--out-dir data/h4]
 */

#include <errno.h>
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

#define H4_FORCE_STEPS 1024
#define H4_HAT_COUNT   4      /* h4 has exactly four hats' worth of area */

/* ---------------------------------------------------------- dir helpers */

static int ensure_dir(const char *path) {
    if (mkdir(path, 0777) == 0) return 1;
    return errno == EEXIST;
}

/* ----------------------------------------------- hidden-vertex rebuild
 * (copied verbatim from src/rl2/generate_rl2.c -- the library does not expose
 *  rebuild_hidden, and make_record_from_state needs the same behaviour). */

static int coord_in_list_local(const Coord *items, int count, Coord q) {
    for (int i = 0; i < count; i++) if (coord_eq(items[i], q)) return 1;
    return 0;
}

static int coord_cmp_local(const void *A, const void *B) {
    const Coord *a = A;
    const Coord *b = B;
    if (a->v != b->v) return a->v - b->v;
    if (a->x != b->x) return a->x - b->x;
    return a->y - b->y;
}

static int collect_tile_vertices(const Cycle *tiles, int tile_count, Coord *verts) {
    int n = 0;
    for (int t = 0; t < tile_count; t++) {
        for (int i = 0; i < tiles[t].n; i++) {
            if (!coord_in_list_local(verts, n, tiles[t].v[i])) {
                if (n >= BCOMP1_MAX_COORDS) return -1;
                verts[n++] = tiles[t].v[i];
            }
        }
    }
    return n;
}

static int rebuild_hidden(const Poly *p, const Cycle *tiles, int tile_count, Coord *hidden) {
    Coord all[BCOMP1_MAX_COORDS];
    Coord boundary[BCOMP1_MAX_COORDS];
    int ac = collect_tile_vertices(tiles, tile_count, all);
    int bc = build_boundary_vertices(p, boundary);
    int hc = 0;
    if (ac < 0 || bc < 0) return -1;
    for (int i = 0; i < ac; i++) {
        if (!coord_in_list_local(boundary, bc, all[i])) {
            if (hc >= BCOMP1_MAX_COORDS) return -1;
            if (hidden) hidden[hc] = all[i];
            hc++;
        }
    }
    if (hidden && hc > 1) qsort(hidden, (size_t)hc, sizeof(Coord), coord_cmp_local);
    return hc;
}

static int make_record_from_state(const BComp1State *s, const Cycle *center, BComp1Record *r) {
    Coord *hidden = malloc(sizeof(*hidden) * BCOMP1_MAX_COORDS);
    int hidden_count;
    if (!hidden) return 0;
    hidden_count = rebuild_hidden(&s->poly, s->tiles, s->tile_count, hidden);
    if (hidden_count < 0) { free(hidden); return 0; }
    memset(r, 0, sizeof(*r));
    r->level = hidden_count;
    r->tile_count = s->tile_count;
    r->start_index = 0;
    r->dir = 1;
    r->have_center = 1;
    r->have_boundary = 1;
    r->have_hidden = 1;
    r->have_tiles = 1;
    r->center = *center;
    r->boundary = s->poly;
    r->hidden_count = hidden_count;
    r->tiles_count = s->tile_count;
    for (int i = 0; i < hidden_count; i++) r->hidden[i] = hidden[i];
    for (int i = 0; i < s->tile_count; i++) r->tiles[i] = s->tiles[i];
    free(hidden);
    return 1;
}

static int write_record_file(const char *path, const BComp1Record *record) {
    FILE *fp = fopen(path, "w");
    if (!fp) return 0;
    bcomp1_print_record(fp, 1, record);
    return fclose(fp) == 0;
}

static int write_records_file(const char *path, BComp1RecordVec *records) {
    FILE *fp = fopen(path, "w");
    if (!fp) return 0;
    bcomp1_sort_records(records);
    for (size_t i = 0; i < records->count; i++) bcomp1_print_record(fp, i + 1, &records->items[i]);
    return fclose(fp) == 0;
}

static int force_state(const BComp1Context *ctx,
                       BComp1State *state,
                       Attach0ClosureStats *cstats) {
    Attach0Stats astats;
    attach0_stats_init(&astats);
    attach0_closure_stats_init(cstats);
    if (!attach0_force_live_closure(&state->poly,
                                    &ctx->tile,
                                    state->tiles,
                                    &state->tile_count,
                                    &ctx->map,
                                    H4_FORCE_STEPS,
                                    &astats,
                                    cstats)) {
        fprintf(stderr,
                "ERROR: forced closure failed vertices=%d forced=%d success=%d "
                "fail=%d unresolved=%d steps=%d\n",
                cstats->vertices_checked,
                cstats->forced_vertices,
                cstats->forced_successes,
                cstats->forced_failures,
                cstats->unresolved_vertices,
                cstats->closure_steps);
        return 0;
    }
    state->hidden_count = rebuild_hidden(&state->poly, state->tiles, state->tile_count, NULL);
    return state->hidden_count >= 0;
}

/* ---------------------------------------------- h4 -> 4-hat decomposition */

static Cycle     g_h4;                 /* h4 boundary, normalized CCW           */
static long long g_hx[MAX_VERTS];      /* exact embedded x of each h4 vertex    */
static long long g_hy[MAX_VERTS];      /* exact embedded y of each h4 vertex    */
static const Tile *g_hat;              /* hat tile (with its variants)          */

static Cycle     g_pieces[H4_HAT_COUNT]; /* decomposition result                */
static Poly      g_union;                /* union boundary of the decomposition */
static int       g_done;                 /* decomposition found                 */

/* Translate variant so that its vertex j lands exactly on `target`. */
static int place_variant_on(const Cycle *var, int j, Coord target, Cycle *out) {
    int dx, dy, m, n;
    if (var->v[j].v != target.v) return 0;
    dx = target.x - var->v[j].x;
    dy = target.y - var->v[j].y;
    if (!tetrille_delta_to_6(var->v[j].v, dx, dy, &m, &n)) return 0;
    *out = *var;
    tetrille_translate_cycle(out, m, n);
    return coord_eq(out->v[j], target);
}

/* Exact "is q inside or on the h4 boundary?" test on embedded coordinates. */
static int point_inside_or_on(Coord q) {
    long long qx, qy;
    int c = 0;
    tetrille_embed_point_scaled(q, &qx, &qy);
    for (int i = 0; i < g_h4.n; i++) {
        Coord a = g_h4.v[i];
        Coord b = g_h4.v[(i + 1) % g_h4.n];
        if (tetrille_point_on_segment(q, a, b)) return 1;
    }
    for (int i = 0, j = g_h4.n - 1; i < g_h4.n; j = i++) {
        if ((g_hy[i] > qy) != (g_hy[j] > qy)) {
            long double xint = (long double)(g_hx[j] - g_hx[i]) *
                               (long double)(qy - g_hy[i]) /
                               (long double)(g_hy[j] - g_hy[i]) + g_hx[i];
            if ((long double)qx < xint) c ^= 1;
        }
    }
    return c;
}

static int hat_inside_h4(const Cycle *h) {
    for (int i = 0; i < h->n; i++)
        if (!point_inside_or_on(h->v[i])) return 0;
    return 1;
}

static int same_vertex_set(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) {
        int found = 0;
        for (int j = 0; j < b->n; j++)
            if (coord_eq(a->v[i], b->v[j])) { found = 1; break; }
        if (!found) return 0;
    }
    return 1;
}

/* DFS: grow `agg` (a hole-free union of `count` hats placed so far) by one hat
 * that stays inside h4, until the union exactly fills h4 with H4_HAT_COUNT hats. */
static void dfs_decompose(const Poly *agg, int count) {
    if (g_done) return;
    if (count == H4_HAT_COUNT) {
        if (agg->cycle_count == 1 && same_vertex_set(&agg->cycles[0], &g_h4)) {
            g_union = *agg;
            g_done = 1;
        }
        return;
    }
    Edge edges[256];
    int ec = build_boundary_edges(agg, edges);
    for (int be = 0; be < ec && !g_done; be++) {
        for (int vi = 0; vi < g_hat->variant_count && !g_done; vi++) {
            const Cycle *var = &g_hat->variants[vi];
            for (int te = 0; te < var->n && !g_done; te++) {
                Poly out;
                Cycle aligned;
                if (!try_attach_tile_poly_ex(agg, var, g_hat->lattice,
                                             be, te, &out, &aligned))
                    continue;
                if (out.cycle_count > 1) continue;      /* would create a hole */
                if (!hat_inside_h4(&aligned)) continue;  /* spills outside h4   */
                g_pieces[count] = aligned;
                dfs_decompose(&out, count + 1);
            }
        }
    }
}

/* Decompose h4 into four hats and build the seed BComp1State. */
static int decompose_h4(BComp1State *seed) {
    BComp1Record rec;
    g_done = 0;
    for (int k = 0; k < g_h4.n && !g_done; k++) {
        for (int vi = 0; vi < g_hat->variant_count && !g_done; vi++) {
            const Cycle *var = &g_hat->variants[vi];
            for (int j = 0; j < var->n && !g_done; j++) {
                Cycle cand;
                Poly agg;
                if (!place_variant_on(var, j, g_h4.v[k], &cand)) continue;
                if (!hat_inside_h4(&cand)) continue;
                agg.cycle_count = 1;
                agg.cycles[0] = cand;
                g_pieces[0] = cand;
                dfs_decompose(&agg, 1);
            }
        }
    }
    if (!g_done) return 0;

    memset(&rec, 0, sizeof(rec));
    rec.have_boundary = 1;
    rec.boundary = g_union;
    rec.have_tiles = 1;
    rec.tiles_count = H4_HAT_COUNT;
    for (int i = 0; i < H4_HAT_COUNT; i++) rec.tiles[i] = g_pieces[i];
    rec.have_center = 1;
    rec.center = g_h4;   /* highlight the whole h4 outline as the cluster center */

    return bcomp1_state_from_record(&rec, seed);
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv) {
    const char *center_path      = "tiles/h4.tile";
    const char *tile_path        = "preferences/focus.tile";
    const char *remembrance_path = "data/rl0/remembrance.dat";
    const char *deletions_path   = "data/rl0/deletions.dat";
    const char *out_dir          = "data/h4";
    char supertile_path[512];
    char completions_path[512];

    BComp1Context ctx;
    BComp1State seed;
    BComp1Record supertile;
    BComp1Result result;
    BComp1Options opts;
    Attach0ClosureStats cstats;
    Tile center_tile;
    Cycle center;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--center") && i + 1 < argc) center_path = argv[++i];
        else if (!strcmp(argv[i], "--tile") && i + 1 < argc) tile_path = argv[++i];
        else if (!strcmp(argv[i], "--remembrance") && i + 1 < argc) remembrance_path = argv[++i];
        else if (!strcmp(argv[i], "--deletions") && i + 1 < argc) deletions_path = argv[++i];
        else if (!strcmp(argv[i], "--out-dir") && i + 1 < argc) out_dir = argv[++i];
    }

    if (!ensure_dir("data") || !ensure_dir(out_dir)) {
        fprintf(stderr, "ERROR: failed to create output directory %s\n", out_dir);
        return 1;
    }
    snprintf(supertile_path, sizeof(supertile_path), "%s/supertile.dat", out_dir);
    snprintf(completions_path, sizeof(completions_path), "%s/completions.dat", out_dir);

    if (!bcomp1_context_init(&ctx, tile_path, remembrance_path, deletions_path)) {
        fprintf(stderr, "ERROR: failed to initialize context from %s\n", tile_path);
        return 1;
    }
    if (!tile_load(center_path, &center_tile)) {
        fprintf(stderr, "ERROR: failed to load center tile %s\n", center_path);
        bcomp1_context_clear(&ctx);
        return 1;
    }
    if (center_tile.lattice != ctx.tile.lattice) {
        fprintf(stderr, "ERROR: center and hat tiles use different lattices\n");
        bcomp1_context_clear(&ctx);
        return 1;
    }

    /* Normalize the h4 center cycle to CCW and record exact embedded coords. */
    g_h4 = center_tile.base;
    if (cycle_signed_area2(&g_h4, center_tile.lattice) < 0) cycle_reverse(&g_h4);
    cycle_normalize_position(&g_h4, center_tile.lattice);
    for (int i = 0; i < g_h4.n; i++)
        tetrille_embed_point_scaled(g_h4.v[i], &g_hx[i], &g_hy[i]);
    g_hat = &ctx.tile;

    if (!decompose_h4(&seed)) {
        fprintf(stderr, "ERROR: could not decompose %s into %d hats\n",
                center_path, H4_HAT_COUNT);
        bcomp1_context_clear(&ctx);
        return 1;
    }
    center = g_h4;
    fprintf(stderr, "Decomposed %s into %d hats (seed tiles=%d, hidden=%d)\n",
            center_path, H4_HAT_COUNT, seed.tile_count, seed.hidden_count);

    /* 1. Forced live closure around the center cluster. */
    if (!force_state(&ctx, &seed, &cstats)) {
        bcomp1_context_clear(&ctx);
        return 1;
    }

    /* 2. Write the forced supertile. */
    if (!make_record_from_state(&seed, &center, &supertile) ||
        !write_record_file(supertile_path, &supertile)) {
        fprintf(stderr, "ERROR: failed to write %s\n", supertile_path);
        bcomp1_context_clear(&ctx);
        return 1;
    }

    /* 3. Depth-1 live completions (the survivors). */
    bcomp1_options_default(&opts);
    opts.depth = 1;
    opts.collect_records = 1;
    opts.live_only = 1;
    if (!bcomp1_complete_state(&ctx, &seed, &center, &opts, &result)) {
        fprintf(stderr, "ERROR: depth-1 completion search failed\n");
        bcomp1_context_clear(&ctx);
        return 1;
    }

    /* 4. Write the completions. */
    if (!write_records_file(completions_path, &result.records)) {
        fprintf(stderr, "ERROR: failed to write %s\n", completions_path);
        bcomp1_result_clear(&result);
        bcomp1_context_clear(&ctx);
        return 1;
    }

    fprintf(stderr,
            "h4_cluster center=%s\n"
            "  forced_tiles=%d hidden=%d closure_steps=%d unresolved=%d children=%zu\n"
            "  supertile=%s\n"
            "  completions=%s\n",
            center_path,
            seed.tile_count,
            seed.hidden_count,
            cstats.closure_steps,
            cstats.unresolved_vertices,
            result.records.count,
            supertile_path,
            completions_path);

    bcomp1_result_clear(&result);
    bcomp1_context_clear(&ctx);
    return 0;
}