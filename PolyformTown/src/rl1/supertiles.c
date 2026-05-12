#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/boundary.h"
#include "core/tile.h"
#include "rl0/attach0.h"
#include "rl1/bcomp1.h"

#define SUPER_MAX_COORDS (MAX_VERTS * ATTACH0_MAX_TILES)

typedef struct {
    const char *tile_path;
    const char *input_path;
    const char *output_path;
    const char *d1_path;
    const char *d2_raw_path;
    const char *d2_path;
    const char *remembrance_path;
    const char *deletions_path;
    int record_index;
    int max_steps;
    int force_input_mode;
    int extract_only_mode;
    int workflow_mode;
} Options;

typedef struct {
    size_t records;
    size_t written;
    size_t force_failed;
    size_t hidden_failed;
    size_t total_forced;
    size_t total_steps;
} ForceSummary;

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [options]\n"
            "default with no mode flags: run full supertile workflow\n"
            "  1. extract preferences/focus.supertile from data/rl1/completions.dat record 6\n"
            "  2. write data/rl1/completions_d1.dat\n"
            "  3. write data/rl1/completions_d2.preimage.dat\n"
            "  4. force-complete d2 into data/rl1/completions_d2.dat without dropping records\n"
            "options:\n"
            "  --extract-only      only extract the focus supertile\n"
            "  --force-input       force-complete every record from --input; no reflection\n"
            "  --input PATH        default data/rl1/completions.dat\n"
            "  --tile PATH         default preferences/focus.tile\n"
            "  --output PATH       default preferences/focus.supertile\n"
            "  --d1 PATH           default data/rl1/completions_d1.dat\n"
            "  --d2-raw PATH       default data/rl1/completions_d2.preimage.dat\n"
            "  --d2 PATH           default data/rl1/completions_d2.dat\n"
            "  --record N          source RL1 record index, default 6\n"
            "  --max-steps N       forced-closure step limit, default 1024\n"
            "  --remembrance PATH  default data/rl0/remembrance.dat\n"
            "  --deletions PATH    default data/rl0/deletions.dat\n",
            prog);
}

static void options_default(Options *opt) {
    opt->tile_path = "preferences/focus.tile";
    opt->input_path = "data/rl1/completions.dat";
    opt->output_path = "preferences/focus.supertile";
    opt->d1_path = "data/rl1/completions_d1.dat";
    opt->d2_raw_path = "data/rl1/completions_d2.preimage.dat";
    opt->d2_path = "data/rl1/completions_d2.dat";
    opt->remembrance_path = "data/rl0/remembrance.dat";
    opt->deletions_path = "data/rl0/deletions.dat";
    opt->record_index = 6;
    opt->max_steps = 1024;
    opt->force_input_mode = 0;
    opt->extract_only_mode = 0;
    opt->workflow_mode = 1;
}

static int parse_args(int argc, char **argv, Options *opt) {
    options_default(opt);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--extract-only") == 0) { opt->extract_only_mode = 1; opt->workflow_mode = 0; }
        else if (strcmp(argv[i], "--force-input") == 0) { opt->force_input_mode = 1; opt->workflow_mode = 0; }
        else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) opt->input_path = argv[++i];
        else if (strcmp(argv[i], "--tile") == 0 && i + 1 < argc) opt->tile_path = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) opt->output_path = argv[++i];
        else if (strcmp(argv[i], "--d1") == 0 && i + 1 < argc) opt->d1_path = argv[++i];
        else if (strcmp(argv[i], "--d2-raw") == 0 && i + 1 < argc) opt->d2_raw_path = argv[++i];
        else if (strcmp(argv[i], "--d2") == 0 && i + 1 < argc) opt->d2_path = argv[++i];
        else if (strcmp(argv[i], "--record") == 0 && i + 1 < argc) opt->record_index = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-steps") == 0 && i + 1 < argc) opt->max_steps = atoi(argv[++i]);
        else if (strcmp(argv[i], "--remembrance") == 0 && i + 1 < argc) opt->remembrance_path = argv[++i];
        else if (strcmp(argv[i], "--deletions") == 0 && i + 1 < argc) opt->deletions_path = argv[++i];
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(argv[0]); exit(0); }
        else return 0;
    }
    return opt->record_index > 0;
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

static void reflect_y_poly_ccw(int lattice, const Poly *src, Poly *dst) {
    dst->cycle_count = src->cycle_count;
    for (int c = 0; c < src->cycle_count; c++) reflect_y_cycle_ccw(lattice, &src->cycles[c], &dst->cycles[c]);
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

static int collect_tile_vertices_local(const Cycle *tiles, int tile_count, Coord *verts) {
    int n = 0;
    for (int t = 0; t < tile_count; t++) {
        for (int i = 0; i < tiles[t].n; i++) {
            if (!coord_in_list_local(verts, n, tiles[t].v[i])) {
                if (n >= SUPER_MAX_COORDS) return -1;
                verts[n++] = tiles[t].v[i];
            }
        }
    }
    return n;
}

static int rebuild_hidden_local(const Poly *p, const Cycle *tiles, int tile_count, Coord *hidden) {
    Coord all[SUPER_MAX_COORDS];
    Coord boundary[SUPER_MAX_COORDS];
    int ac = collect_tile_vertices_local(tiles, tile_count, all);
    int bc = build_boundary_vertices(p, boundary);
    int hc = 0;
    if (ac < 0 || bc < 0) return -1;
    for (int i = 0; i < ac; i++) {
        if (!coord_in_list_local(boundary, bc, all[i])) {
            if (hc >= SUPER_MAX_COORDS) return -1;
            hidden[hc++] = all[i];
        }
    }
    qsort(hidden, (size_t)hc, sizeof(Coord), coord_cmp_local);
    return hc;
}

static void print_coord_pretty(FILE *fp, Coord q) {
    fprintf(fp, "(%d,%d,%d)", q.v, q.x, q.y);
}

static void print_cycle_pretty(FILE *fp, const Cycle *cycle, int indent) {
    for (int i = 0; i < indent; i++) fputc(' ', fp);
    fprintf(fp, "[\n");
    for (int i = 0; i < cycle->n; i++) {
        for (int j = 0; j < indent + 2; j++) fputc(' ', fp);
        print_coord_pretty(fp, cycle->v[i]);
        if (i + 1 < cycle->n) fputc(',', fp);
        fputc('\n', fp);
    }
    for (int i = 0; i < indent; i++) fputc(' ', fp);
    fprintf(fp, "]");
}

static void print_poly_pretty(FILE *fp, const Poly *poly) {
    fprintf(fp, "[\n");
    for (int c = 0; c < poly->cycle_count; c++) {
        print_cycle_pretty(fp, &poly->cycles[c], 2);
        if (c + 1 < poly->cycle_count) fprintf(fp, "|");
        fprintf(fp, "\n");
    }
    fprintf(fp, "]\n");
}

static void print_coord_list_pretty(FILE *fp, const Coord *coords, int count) {
    fprintf(fp, "[\n");
    for (int i = 0; i < count; i++) {
        fprintf(fp, "  ");
        print_coord_pretty(fp, coords[i]);
        if (i + 1 < count) fputc(',', fp);
        fputc('\n', fp);
    }
    fprintf(fp, "]\n");
}

static void print_tile_list_pretty(FILE *fp, const Cycle *tiles, int tile_count) {
    fprintf(fp, "[\n");
    for (int i = 0; i < tile_count; i++) {
        print_cycle_pretty(fp, &tiles[i], 2);
        if (i + 1 < tile_count) fputc(',', fp);
        fputc('\n', fp);
    }
    fprintf(fp, "]\n");
}

static int write_supertile_file(const char *path,
                                const BComp1Record *record,
                                const Options *opt,
                                int source_tiles,
                                int forced_tiles,
                                const Attach0ClosureStats *closure_stats) {
    FILE *fp = fopen(path, "w");
    if (!fp) return 0;
    fprintf(fp, "---[1]---\n");
    fprintf(fp, "name: focus.supertile\n");
    fprintf(fp, "source: %s\n", opt->input_path);
    fprintf(fp, "source_record: %d\n", opt->record_index);
    fprintf(fp, "operation: reflect_y forced_closure\n");
    fprintf(fp, "source_tiles: %d\n", source_tiles);
    fprintf(fp, "forced_tiles: %d\n", forced_tiles);
    fprintf(fp, "final_tiles: %d\n", record->tiles_count);
    fprintf(fp, "closure_steps: %d\n", closure_stats ? closure_stats->closure_steps : 0);
    fprintf(fp, "unresolved_vertices: %d\n", closure_stats ? closure_stats->unresolved_vertices : 0);
    fprintf(fp, "\n");
    fprintf(fp, "center_tile:\n");
    print_cycle_pretty(fp, &record->center, 0);
    fprintf(fp, "\n\n");
    fprintf(fp, "boundary:\n");
    print_poly_pretty(fp, &record->boundary);
    fprintf(fp, "\n");
    fprintf(fp, "constellation:\n");
    print_coord_list_pretty(fp, record->hidden, record->hidden_count);
    fprintf(fp, "\n");
    fprintf(fp, "tiles:\n");
    print_tile_list_pretty(fp, record->tiles, record->tiles_count);
    fclose(fp);
    return 1;
}

static int write_records(const char *path, const BComp1RecordVec *records) {
    FILE *fp = fopen(path, "w");
    if (!fp) return 0;
    for (size_t i = 0; i < records->count; i++) bcomp1_print_record(fp, i + 1, &records->items[i]);
    fclose(fp);
    return 1;
}

static int extract_supertile(const Options *opt,
                             const Tile *tile,
                             const BComp1Context *ctx,
                             const BComp1RecordVec *records,
                             BComp1Record *out_record) {
    BComp1State seed;
    Attach0Stats astats;
    Attach0ClosureStats cstats;
    int forced_before;

    if (opt->record_index < 1 || (size_t)opt->record_index > records->count) {
        fprintf(stderr, "record %d out of range 1..%zu\n", opt->record_index, records->count);
        return 0;
    }
    if (!bcomp1_state_from_record(&records->items[opt->record_index - 1], &seed)) {
        fprintf(stderr, "record %d malformed\n", opt->record_index);
        return 0;
    }

    memset(out_record, 0, sizeof(*out_record));
    out_record->level = records->items[opt->record_index - 1].level;
    out_record->start_index = records->items[opt->record_index - 1].start_index;
    out_record->dir = -records->items[opt->record_index - 1].dir;
    out_record->have_center = 1;
    out_record->have_boundary = 1;
    out_record->have_hidden = 1;
    out_record->have_tiles = 1;

    reflect_y_cycle_ccw(tile->lattice, &records->items[opt->record_index - 1].center, &out_record->center);
    reflect_y_poly_ccw(tile->lattice, &seed.poly, &seed.poly);
    for (int i = 0; i < seed.tile_count; i++) reflect_y_cycle_ccw(tile->lattice, &seed.tiles[i], &seed.tiles[i]);

    attach0_stats_init(&astats);
    attach0_closure_stats_init(&cstats);
    forced_before = seed.tile_count;
    if (!attach0_force_live_closure(&seed.poly,
                                    tile,
                                    seed.tiles,
                                    &seed.tile_count,
                                    &ctx->map,
                                    opt->max_steps,
                                    &astats,
                                    &cstats)) {
        fprintf(stderr,
                "forced closure failed: vertices=%d forced=%d success=%d fail=%d unresolved=%d steps=%d\n",
                cstats.vertices_checked,
                cstats.forced_vertices,
                cstats.forced_successes,
                cstats.forced_failures,
                cstats.unresolved_vertices,
                cstats.closure_steps);
        return 0;
    }

    out_record->boundary = seed.poly;
    out_record->tile_count = seed.tile_count;
    out_record->tiles_count = seed.tile_count;
    for (int i = 0; i < seed.tile_count; i++) out_record->tiles[i] = seed.tiles[i];
    out_record->hidden_count = rebuild_hidden_local(&seed.poly, seed.tiles, seed.tile_count, out_record->hidden);
    if (out_record->hidden_count < 0) {
        fprintf(stderr, "failed to rebuild constellation\n");
        return 0;
    }

    if (!write_supertile_file(opt->output_path,
                              out_record,
                              opt,
                              forced_before,
                              seed.tile_count - forced_before,
                              &cstats)) {
        fprintf(stderr, "failed to write output: %s\n", opt->output_path);
        return 0;
    }

    fprintf(stderr,
            "rl1_supertiles extract record=%d source_tiles=%d final_tiles=%d forced_tiles=%d closure_steps=%d unresolved=%d output=%s\n",
            opt->record_index,
            forced_before,
            seed.tile_count,
            seed.tile_count - forced_before,
            cstats.closure_steps,
            cstats.unresolved_vertices,
            opt->output_path);
    return 1;
}

static int search_from_record(const BComp1Context *ctx,
                              const BComp1Record *record,
                              int depth,
                              BComp1RecordVec *out_records,
                              BComp1Stats *out_stats) {
    BComp1State seed;
    BComp1Result result;
    BComp1Options opts;
    if (!bcomp1_state_from_record(record, &seed)) return 0;
    bcomp1_options_default(&opts);
    opts.depth = depth;
    opts.collect_records = 1;
    if (!bcomp1_complete_state(ctx, &seed, &record->center, &opts, &result)) return 0;
    *out_records = result.records;
    result.records.items = NULL;
    result.records.count = 0;
    result.records.cap = 0;
    if (out_stats) *out_stats = result.stats;
    bcomp1_result_clear(&result);
    return 1;
}

static int append_record(BComp1RecordVec *v, const BComp1Record *r) {
    if (v->count == v->cap) {
        size_t next = v->cap ? v->cap * 2 : 64;
        BComp1Record *p = realloc(v->items, next * sizeof(*p));
        if (!p) return 0;
        v->items = p;
        v->cap = next;
    }
    v->items[v->count++] = *r;
    return 1;
}

static int force_complete_records(const Options *opt,
                                  const BComp1Context *ctx,
                                  const BComp1RecordVec *records,
                                  const char *output_path,
                                  int preserve_on_failure,
                                  ForceSummary *summary) {
    BComp1RecordVec forced = {0};
    memset(summary, 0, sizeof(*summary));
    summary->records = records->count;

    for (size_t i = 0; i < records->count; i++) {
        BComp1State state;
        BComp1Record out;
        Attach0Stats astats;
        Attach0ClosureStats cstats;
        int before;
        int use_original = 0;

        if (!bcomp1_state_from_record(&records->items[i], &state)) {
            fprintf(stderr, "force record=%zu malformed; preserving original\n", i + 1);
            summary->force_failed++;
            use_original = 1;
        } else {
            attach0_stats_init(&astats);
            attach0_closure_stats_init(&cstats);
            before = state.tile_count;
            if (!attach0_force_live_closure(&state.poly,
                                            &ctx->tile,
                                            state.tiles,
                                            &state.tile_count,
                                            &ctx->map,
                                            opt->max_steps,
                                            &astats,
                                            &cstats)) {
                fprintf(stderr,
                        "force record=%zu failed vertices=%d forced=%d success=%d fail=%d unresolved=%d steps=%d; preserving original\n",
                        i + 1,
                        cstats.vertices_checked,
                        cstats.forced_vertices,
                        cstats.forced_successes,
                        cstats.forced_failures,
                        cstats.unresolved_vertices,
                        cstats.closure_steps);
                summary->force_failed++;
                use_original = 1;
            } else {
                memset(&out, 0, sizeof(out));
                out.level = records->items[i].level;
                out.start_index = records->items[i].start_index;
                out.dir = records->items[i].dir;
                out.have_center = records->items[i].have_center;
                out.center = records->items[i].center;
                out.have_boundary = 1;
                out.boundary = state.poly;
                out.have_tiles = 1;
                out.tile_count = state.tile_count;
                out.tiles_count = state.tile_count;
                for (int t = 0; t < state.tile_count; t++) out.tiles[t] = state.tiles[t];
                out.hidden_count = rebuild_hidden_local(&state.poly, state.tiles, state.tile_count, out.hidden);
                if (out.hidden_count < 0) {
                    fprintf(stderr, "force record=%zu hidden rebuild failed; preserving original\n", i + 1);
                    summary->hidden_failed++;
                    use_original = 1;
                } else {
                    out.have_hidden = 1;
                    summary->total_forced += (size_t)(state.tile_count - before);
                    summary->total_steps += (size_t)cstats.closure_steps;
                }
            }
        }

        if (use_original) {
            if (!preserve_on_failure) continue;
            out = records->items[i];
        }
        if (!append_record(&forced, &out)) {
            bcomp1_free_records(&forced);
            return 0;
        }
    }

    summary->written = forced.count;
    if (!write_records(output_path, &forced)) {
        bcomp1_free_records(&forced);
        return 0;
    }
    bcomp1_free_records(&forced);
    fprintf(stderr,
            "rl1_supertiles force records=%zu written=%zu force_failed=%zu hidden_failed=%zu forced_tiles=%zu closure_steps=%zu output=%s\n",
            summary->records,
            summary->written,
            summary->force_failed,
            summary->hidden_failed,
            summary->total_forced,
            summary->total_steps,
            output_path);
    return 1;
}

static int run_workflow(const Options *opt,
                        const Tile *tile,
                        const BComp1Context *ctx,
                        const BComp1RecordVec *source_records) {
    BComp1Record focus;
    BComp1RecordVec d1 = {0};
    BComp1RecordVec d2 = {0};
    BComp1Stats s1, s2;
    ForceSummary fs;
    size_t d2_count;

    if (!extract_supertile(opt, tile, ctx, source_records, &focus)) return 0;

    if (!search_from_record(ctx, &focus, 1, &d1, &s1)) return 0;
    if (!write_records(opt->d1_path, &d1)) {
        bcomp1_free_records(&d1);
        return 0;
    }
    fprintf(stderr,
            "rl1_supertiles search depth=1 outputs=%zu attempts=%zu successes=%zu dead=%zu illegal=%zu dfs=%zu output=%s\n",
            d1.count,
            s1.attach_attempts,
            s1.attach_successes,
            s1.filtered_dead,
            s1.filtered_illegal,
            s1.dfs_calls,
            opt->d1_path);

    if (!search_from_record(ctx, &focus, 2, &d2, &s2)) {
        bcomp1_free_records(&d1);
        return 0;
    }
    if (!write_records(opt->d2_raw_path, &d2)) {
        bcomp1_free_records(&d1);
        bcomp1_free_records(&d2);
        return 0;
    }
    fprintf(stderr,
            "rl1_supertiles search depth=2 outputs=%zu attempts=%zu successes=%zu dead=%zu illegal=%zu dfs=%zu raw_output=%s\n",
            d2.count,
            s2.attach_attempts,
            s2.attach_successes,
            s2.filtered_dead,
            s2.filtered_illegal,
            s2.dfs_calls,
            opt->d2_raw_path);

    d2_count = d2.count;
    if (!force_complete_records(opt, ctx, &d2, opt->d2_path, 1, &fs)) {
        bcomp1_free_records(&d1);
        bcomp1_free_records(&d2);
        return 0;
    }
    fprintf(stderr,
            "rl1_supertiles checkpoint d2_preimage=%zu d2_completed=%zu loss=%lld force_failures=%zu\n",
            d2_count,
            fs.written,
            (long long)d2_count - (long long)fs.written,
            fs.force_failed + fs.hidden_failed);

    bcomp1_free_records(&d1);
    bcomp1_free_records(&d2);
    return fs.written == d2_count;
}

int main(int argc, char **argv) {
    Options opt;
    Tile tile;
    BComp1Context ctx;
    BComp1RecordVec records = {0};
    int ok = 0;

    if (!parse_args(argc, argv, &opt)) {
        usage(argv[0]);
        return 1;
    }
    if (!tile_load(opt.tile_path, &tile)) {
        fprintf(stderr, "failed to load tile: %s\n", opt.tile_path);
        return 1;
    }
    if (!bcomp1_load_records(opt.input_path, &records)) {
        fprintf(stderr, "failed to load records: %s\n", opt.input_path);
        return 1;
    }
    if (!bcomp1_context_init(&ctx,
                             opt.tile_path,
                             opt.remembrance_path,
                             opt.deletions_path)) {
        fprintf(stderr, "failed to initialize RL1 context\n");
        bcomp1_free_records(&records);
        return 1;
    }

    if (opt.force_input_mode) {
        ForceSummary fs;
        ok = force_complete_records(&opt, &ctx, &records, opt.output_path, 1, &fs);
    } else if (opt.workflow_mode) {
        ok = run_workflow(&opt, &tile, &ctx, &records);
    } else {
        BComp1Record out;
        ok = extract_supertile(&opt, &tile, &ctx, &records, &out);
    }

    bcomp1_context_clear(&ctx);
    bcomp1_free_records(&records);
    return ok ? 0 : 1;
}
