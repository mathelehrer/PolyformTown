#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "core/boundary.h"
#include "core/cycle.h"
#include "core/tile.h"
#include "rl0/attach0.h"
#include "rl0/boundary0.h"
#include "rl1/bcomp1.h"

#define RL2_FORCE_STEPS 1024

static int ensure_dir(const char *path) {
    if (mkdir(path, 0777) == 0) return 1;
    return errno == EEXIST;
}

static int ensure_output_dirs(void) {
    return ensure_dir("data") && ensure_dir("data/rl2");
}

static int cycle_eq_exact_local(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) if (!coord_eq(a->v[i], b->v[i])) return 0;
    return 1;
}

static int center_tile_index(const BComp1Record *r) {
    if (!r->have_center || !r->have_tiles) return -1;
    for (int i = 0; i < r->tiles_count; i++) {
        if (cycle_eq_exact_local(&r->tiles[i], &r->center)) return i;
    }
    return -1;
}

static Coord reflect_y_coord(int lattice, Coord q) {
    switch (lattice) {
        case TILE_LATTICE_SQUARE:
            return (Coord){ q.v, -q.x, q.y };
        case TILE_LATTICE_TRIANGULAR:
            return (Coord){ q.v, -q.x - q.y, q.y };
        case TILE_LATTICE_TETRILLE:
            if (q.v == 3) return (Coord){ q.v, -q.x - q.y, q.y };
            return (Coord){ q.v, -q.y, -q.x };
        default:
            return q;
    }
}

static void reflect_y_cycle_ccw(int lattice, const Cycle *src, Cycle *dst) {
    dst->n = src->n;
    for (int i = 0; i < src->n; i++) dst->v[i] = reflect_y_coord(lattice, src->v[i]);
    cycle_reverse(dst);
}

static int coord_cmp_local(const void *A, const void *B) {
    const Coord *a = A;
    const Coord *b = B;
    if (a->v != b->v) return a->v - b->v;
    if (a->x != b->x) return a->x - b->x;
    return a->y - b->y;
}

static int coord_in_list_local(const Coord *items, int count, Coord q) {
    for (int i = 0; i < count; i++) if (coord_eq(items[i], q)) return 1;
    return 0;
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

static void reflect_y_poly_ccw(int lattice, const Poly *src, Poly *dst) {
    dst->cycle_count = src->cycle_count;
    for (int c = 0; c < src->cycle_count; c++) reflect_y_cycle_ccw(lattice, &src->cycles[c], &dst->cycles[c]);
}

static int opposite_count_for_record(const Tile *tile, const BComp1Record *r, int *center_p_out) {
    int center_idx = center_tile_index(r);
    RL0FMItem center_item;
    int opp = 0;
    if (center_idx < 0) return -1;
    if (!boundary0_tile_item_at_vertex(tile, &r->tiles[center_idx], r->tiles[center_idx].v[0], &center_item)) return -1;
    for (int t = 0; t < r->tiles_count; t++) {
        RL0FMItem item;
        if (t == center_idx) continue;
        if (!boundary0_tile_item_at_vertex(tile, &r->tiles[t], r->tiles[t].v[0], &item)) return -1;
        if (item.p != center_item.p) opp++;
    }
    if (center_p_out) *center_p_out = center_item.p;
    return opp;
}

static int find_weird_record(const Tile *tile,
                             const BComp1RecordVec *records,
                             const BComp1Record **source,
                             int *source_record,
                             int *center_p,
                             int *opposite_count) {
    int matches = 0;
    *source = NULL;
    *source_record = 0;
    *center_p = 0;
    *opposite_count = 0;

    for (size_t i = 0; i < records->count; i++) {
        int cp = 0;
        int opp = opposite_count_for_record(tile, &records->items[i], &cp);
        if (opp < 0) continue;
        if (opp == records->items[i].tiles_count - 1) {
            matches++;
            *source = &records->items[i];
            *source_record = (int)i + 1;
            *center_p = cp;
            *opposite_count = opp;
        }
    }

    if (matches != 1 || !*source) {
        fprintf(stderr,
                "ERROR: weird RL1 record extraction found %d all-opposite records; expected one\n",
                matches);
        return 0;
    }

    fprintf(stderr,
            "rl2 weird extraction source_record=%d center_p=%+d opposite_tiles=%d/%d\n",
            *source_record,
            *center_p,
            *opposite_count,
            (*source)->tiles_count - 1);
    return 1;
}

static int reflected_state_from_record(const BComp1Context *ctx,
                                       const BComp1Record *record,
                                       BComp1State *state,
                                       Cycle *center) {
    if (!bcomp1_state_from_record(record, state)) return 0;
    reflect_y_cycle_ccw(ctx->tile.lattice, &record->center, center);
    reflect_y_poly_ccw(ctx->tile.lattice, &state->poly, &state->poly);
    for (int i = 0; i < state->tile_count; i++) reflect_y_cycle_ccw(ctx->tile.lattice, &state->tiles[i], &state->tiles[i]);
    state->hidden_count = rebuild_hidden(&state->poly, state->tiles, state->tile_count, NULL);
    return state->hidden_count >= 0;
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
                                    RL2_FORCE_STEPS,
                                    &astats,
                                    cstats)) {
        fprintf(stderr,
                "ERROR: RL2 forced closure failed vertices=%d forced=%d success=%d fail=%d unresolved=%d steps=%d\n",
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

static int item_p_at_first_vertex(const Tile *tile, const Cycle *c, int *p_out) {
    RL0FMItem item;
    if (!boundary0_tile_item_at_vertex(tile, c, c->v[0], &item)) return 0;
    *p_out = item.p;
    return 1;
}

int main(void) {
    const char *tile_path = "preferences/focus.tile";
    const char *input_path = "data/rl1/completions.dat";
    const char *supertile_path = "data/rl2/supertile.dat";
    const char *output_path = "data/rl2/completions.dat";
    const char *remembrance_path = "data/rl0/remembrance.dat";
    const char *deletions_path = "data/rl0/deletions.dat";
    BComp1Context ctx;
    BComp1RecordVec records = {0};
    BComp1State forced;
    BComp1Record supertile;
    BComp1Result result;
    BComp1Options opts;
    Attach0ClosureStats cstats;
    Cycle center;
    const BComp1Record *source = NULL;
    int source_record = 0;
    int source_center_p = 0;
    int source_opp = 0;
    int center_p = 0;

    if (!ensure_output_dirs()) {
        fprintf(stderr, "ERROR: failed to create data/rl2\n");
        return 1;
    }
    if (!bcomp1_context_init(&ctx, tile_path, remembrance_path, deletions_path)) {
        fprintf(stderr, "ERROR: failed to initialize RL2 context\n");
        return 1;
    }
    if (!bcomp1_load_records(input_path, &records)) {
        fprintf(stderr, "ERROR: failed to load RL1 completions: %s\n", input_path);
        bcomp1_context_clear(&ctx);
        return 1;
    }
    if (!find_weird_record(&ctx.tile, &records, &source, &source_record, &source_center_p, &source_opp)) {
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 1;
    }
    if (!reflected_state_from_record(&ctx, source, &forced, &center)) {
        fprintf(stderr, "ERROR: failed to reflect RL2 source record %d\n", source_record);
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 1;
    }
    if (!item_p_at_first_vertex(&ctx.tile, &center, &center_p)) {
        fprintf(stderr, "ERROR: failed to read reflected RL2 center parity\n");
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 1;
    }
    if (center_p != -1) {
        fprintf(stderr, "ERROR: RL2 central tile p=%+d after reflection; expected -1\n", center_p);
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 1;
    }

    if (!force_state(&ctx, &forced, &cstats)) {
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 1;
    }
    if (!make_record_from_state(&forced, &center, &supertile)) {
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 1;
    }
    if (!write_record_file(supertile_path, &supertile)) {
        fprintf(stderr, "ERROR: failed to write %s\n", supertile_path);
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 1;
    }

    bcomp1_options_default(&opts);
    opts.depth = 1;
    opts.collect_records = 1;
    opts.live_only = 1;
    if (!bcomp1_complete_state(&ctx, &forced, &center, &opts, &result)) {
        fprintf(stderr, "ERROR: RL2 depth-1 child search failed\n");
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 1;
    }
    if (!write_records_file(output_path, &result.records)) {
        fprintf(stderr, "ERROR: failed to write %s\n", output_path);
        bcomp1_result_clear(&result);
        bcomp1_free_records(&records);
        bcomp1_context_clear(&ctx);
        return 1;
    }

    fprintf(stderr,
            "rl2 generate source_record=%d source_center_p=%+d source_opposite=%d reflected=yes\n"
            "  forced_tiles=%d hidden=%d closure_steps=%d unresolved=%d children=%zu\n"
            "  output=%s\n"
            "  supertile=%s\n",
            source_record,
            source_center_p,
            source_opp,
            forced.tile_count,
            forced.hidden_count,
            cstats.closure_steps,
            cstats.unresolved_vertices,
            result.records.count,
            output_path,
            supertile_path);

    bcomp1_result_clear(&result);
    bcomp1_free_records(&records);
    bcomp1_context_clear(&ctx);
    return 0;
}
