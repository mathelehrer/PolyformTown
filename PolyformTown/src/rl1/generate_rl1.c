
/*
 * RL1 ring-completion producer.
 *
 * This is intentionally a depth-first producer.  It starts with one central
 * hat, chooses a central-tile boundary vertex and direction by a low-branching
 * heuristic, then walks around the central tile.  At each current central
 * boundary intersection it uses the RL0 dictionary as the branching oracle,
 * applies one completion choice with the RL0 attach code, recurses immediately,
 * and backtracks.
 *
 * Output keeps the first-pass RL1/naive record shape so existing depiction
 * tooling can continue to read data/rl1/completions.dat.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "core/boundary.h"
#include "core/cycle.h"
#include "core/hash.h"
#include "core/tile.h"
#include "rl0/attach0.h"
#include "rl0/boundary0.h"
#include "rl0/forget_map.h"

#define RL1_MAX_COORDS (MAX_VERTS * ATTACH0_MAX_TILES)
#define RL1_MAX_KEY 8192

typedef struct {
    Poly poly;
    Cycle tiles[ATTACH0_MAX_TILES];
    int tile_count;
    Coord hidden[RL1_MAX_COORDS];
    int hidden_count;
    char key[RL1_MAX_KEY];
} RL1State;

typedef struct {
    int start_index;
    int dir;
    int max_tiles;
    int live_only;
    int emit_seen;
    size_t dfs_calls;
    size_t attach_attempts;
    size_t attach_successes;
    size_t complete_count;
    size_t duplicate_count;
    HashTable seen_outputs;
} RL1Search;

typedef struct {
    int level;
    int tile_count;
    int start_index;
    int dir;
    Cycle center_tile;
    Poly boundary;
    char *boundary_str;
    Cycle tiles[ATTACH0_MAX_TILES];
    int hidden_count;
    Coord hidden[RL1_MAX_COORDS];
} RL1Record;

typedef struct {
    const Tile *tile;
    const char *output_path;
    RL1Record *records;
    size_t record_count;
    size_t record_cap;
} RL1Ctx;

static int ensure_dir(const char *path) {
    if (mkdir(path, 0777) == 0) return 1;
    if (errno == EEXIST) return 1;
    return 0;
}

static void ensure_parent_dirs(void) {
    (void)ensure_dir("data");
    (void)ensure_dir("data/rl1");
}

static int coord_less_local(Coord a, Coord b) {
    if (a.v != b.v) return a.v < b.v;
    if (a.x != b.x) return a.x < b.x;
    return a.y < b.y;
}

static int coord_cmp_local(const void *A, const void *B) {
    const Coord *a = A;
    const Coord *b = B;
    if (a->v != b->v) return a->v - b->v;
    if (a->x != b->x) return a->x - b->x;
    return a->y - b->y;
}

static int coord_in_list_local(const Coord *items, int count, Coord q) {
    for (int i = 0; i < count; i++) {
        if (coord_eq(items[i], q)) return 1;
    }
    return 0;
}

static int coord_on_boundary_local(Coord q, const Poly *p) {
    for (int c = 0; c < p->cycle_count; c++) {
        for (int i = 0; i < p->cycles[c].n; i++) {
            if (coord_eq(p->cycles[c].v[i], q)) return 1;
        }
    }
    return 0;
}

static int collect_tile_vertices_local(const Cycle *tiles, int tile_count, Coord *verts) {
    int n = 0;
    for (int t = 0; t < tile_count; t++) {
        for (int i = 0; i < tiles[t].n; i++) {
            if (!coord_in_list_local(verts, n, tiles[t].v[i])) {
                if (n >= RL1_MAX_COORDS) return -1;
                verts[n++] = tiles[t].v[i];
            }
        }
    }
    return n;
}

static int rebuild_hidden_local(const Poly *p, const Cycle *tiles, int tile_count, Coord *hidden) {
    Coord *all = malloc(sizeof(*all) * (size_t)RL1_MAX_COORDS);
    Coord *boundary = malloc(sizeof(*boundary) * (size_t)RL1_MAX_COORDS);
    int ac;
    int bc;
    int hc = 0;

    if (!all || !boundary) {
        free(all);
        free(boundary);
        return -1;
    }

    ac = collect_tile_vertices_local(tiles, tile_count, all);
    bc = build_boundary_vertices(p, boundary);
    if (ac < 0 || bc < 0) {
        free(all);
        free(boundary);
        return -1;
    }
    for (int i = 0; i < ac; i++) {
        if (!coord_in_list_local(boundary, bc, all[i])) {
            if (hc >= RL1_MAX_COORDS) {
                free(all);
                free(boundary);
                return -1;
            }
            hidden[hc++] = all[i];
        }
    }
    qsort(hidden, (size_t)hc, sizeof(Coord), coord_cmp_local);
    free(all);
    free(boundary);
    return hc;
}

static void poly_to_key_local(const Poly *p, char *buf, size_t cap) {
    size_t off = 0;
    Poly key;
    poly_hash_key_lattice(p, TILE_LATTICE_TETRILLE, &key);
    off += (size_t)snprintf(buf + off, cap - off, "[");
    for (int c = 0; c < key.cycle_count && off < cap; c++) {
        if (c) off += (size_t)snprintf(buf + off, cap - off, "|");
        off += (size_t)snprintf(buf + off, cap - off, "[");
        for (int i = 0; i < key.cycles[c].n && off < cap; i++) {
            if (i) off += (size_t)snprintf(buf + off, cap - off, ",");
            off += (size_t)snprintf(buf + off,
                                    cap - off,
                                    "(%d,%d,%d)",
                                    key.cycles[c].v[i].v,
                                    key.cycles[c].v[i].x,
                                    key.cycles[c].v[i].y);
        }
        off += (size_t)snprintf(buf + off, cap - off, "]");
    }
    snprintf(buf + off, cap - off, "]");
}

static int append_text(char **buf, size_t *cap, size_t *off, const char *text) {
    size_t n = strlen(text);
    if (*off + n + 1 > *cap) {
        size_t next = *cap ? *cap : 4096;
        while (*off + n + 1 > next) next *= 2;
        char *p = realloc(*buf, next);
        if (!p) return 0;
        *buf = p;
        *cap = next;
    }
    memcpy(*buf + *off, text, n);
    *off += n;
    (*buf)[*off] = '\0';
    return 1;
}

static int append_coord_text(char **buf, size_t *cap, size_t *off, Coord q) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "(%d,%d,%d)", q.v, q.x, q.y);
    return append_text(buf, cap, off, tmp);
}

static char *poly_to_string_local(const Poly *p) {
    char *buf = NULL;
    size_t cap = 0, off = 0;

    if (!append_text(&buf, &cap, &off, "[")) return NULL;
    for (int c = 0; c < p->cycle_count; c++) {
        if (c && !append_text(&buf, &cap, &off, "|")) goto fail;
        if (!append_text(&buf, &cap, &off, "[")) goto fail;
        for (int i = 0; i < p->cycles[c].n; i++) {
            if (i && !append_text(&buf, &cap, &off, ",")) goto fail;
            if (!append_coord_text(&buf, &cap, &off, p->cycles[c].v[i])) goto fail;
        }
        if (!append_text(&buf, &cap, &off, "]")) goto fail;
    }
    if (!append_text(&buf, &cap, &off, "]")) goto fail;
    return buf;

fail:
    free(buf);
    return NULL;
}

static void print_cycle(FILE *fp, const Cycle *c) {
    fprintf(fp, "[");
    for (int i = 0; i < c->n; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "(%d,%d,%d)", c->v[i].v, c->v[i].x, c->v[i].y);
    }
    fprintf(fp, "]");
}

static void print_tile_list(FILE *fp, const Cycle *tiles, int tile_count) {
    fprintf(fp, "[");
    for (int i = 0; i < tile_count; i++) {
        if (i) fprintf(fp, ",");
        print_cycle(fp, &tiles[i]);
    }
    fprintf(fp, "]");
}

static void print_coord_list(FILE *fp, const Coord *coords, int count) {
    fprintf(fp, "[");
    for (int i = 0; i < count; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "(%d,%d,%d)", coords[i].v, coords[i].x, coords[i].y);
    }
    fprintf(fp, "]");
}

static void write_one_record(FILE *fp, size_t index, const RL1Record *rec) {
    fprintf(fp, "---[%zu]---\n", index);
    fprintf(fp, "level:%d\n", rec->level);
    fprintf(fp, "tile_count:%d\n", rec->tile_count);
    fprintf(fp, "start_index:%d\n", rec->start_index);
    fprintf(fp, "direction:%s\n", rec->dir > 0 ? "ccw" : "cw");
    fprintf(fp, "center_tile:");
    print_cycle(fp, &rec->center_tile);
    fprintf(fp, "\n");
    fprintf(fp, "boundary:");
    fputs(rec->boundary_str, fp);
    fprintf(fp, "\n");
    fprintf(fp, "constellation:");
    print_coord_list(fp, rec->hidden, rec->hidden_count);
    fprintf(fp, "\n");
    fprintf(fp, "tiles:");
    print_tile_list(fp, rec->tiles, rec->tile_count);
    fprintf(fp, "\n");
}

static int cycle_cmp_local(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return (a->n < b->n) ? -1 : 1;
    for (int i = 0; i < a->n; i++) {
        if (coord_less_local(a->v[i], b->v[i])) return -1;
        if (coord_less_local(b->v[i], a->v[i])) return 1;
    }
    return 0;
}

static int record_cmp_local(const void *A, const void *B) {
    const RL1Record *a = A;
    const RL1Record *b = B;
    if (a->level != b->level) return (a->level < b->level) ? -1 : 1;
    if (a->tile_count != b->tile_count) return (a->tile_count < b->tile_count) ? -1 : 1;
    if (a->start_index != b->start_index) return a->start_index - b->start_index;
    if (a->dir != b->dir) return a->dir - b->dir;
    int ccmp = cycle_cmp_local(&a->center_tile, &b->center_tile);
    if (ccmp) return ccmp;
    return strcmp(a->boundary_str, b->boundary_str);
}

static int records_add(RL1Ctx *ctx,
                       const RL1State *state,
                       const Cycle *center_tile,
                       int start_index,
                       int dir) {
    if (ctx->record_count == ctx->record_cap) {
        size_t next = ctx->record_cap ? ctx->record_cap * 2 : 128;
        RL1Record *p = realloc(ctx->records, next * sizeof(*ctx->records));
        if (!p) return 0;
        ctx->records = p;
        ctx->record_cap = next;
    }

    RL1Record *rec = &ctx->records[ctx->record_count];
    rec->level = state->hidden_count;
    rec->tile_count = state->tile_count;
    rec->start_index = start_index;
    rec->dir = dir;
    rec->center_tile = *center_tile;
    rec->boundary = state->poly;
    rec->boundary_str = poly_to_string_local(&state->poly);
    if (!rec->boundary_str) return 0;
    rec->hidden_count = state->hidden_count;
    for (int i = 0; i < state->tile_count; i++) rec->tiles[i] = state->tiles[i];
    for (int i = 0; i < state->hidden_count; i++) rec->hidden[i] = state->hidden[i];

    ctx->record_count++;
    return 1;
}

static int make_seed_state(const Tile *tile, RL1State *s) {
    memset(s, 0, sizeof(*s));
    s->poly.cycle_count = 1;
    s->poly.cycles[0] = tile->base;
    s->tile_count = 1;
    s->tiles[0] = tile->base;
    s->hidden_count = rebuild_hidden_local(&s->poly, s->tiles, s->tile_count, s->hidden);
    if (s->hidden_count < 0) return 0;
    poly_to_key_local(&s->poly, s->key, sizeof(s->key));
    return 1;
}

static int all_center_vertices_gone(const RL1State *s, const Cycle *center) {
    for (int i = 0; i < center->n; i++) {
        if (coord_on_boundary_local(center->v[i], &s->poly)) return 0;
        if (!coord_in_list_local(s->hidden, s->hidden_count, center->v[i])) return 0;
    }
    return 1;
}

static int next_center_boundary_index(const RL1State *s,
                                      const Cycle *center,
                                      int cursor,
                                      int dir) {
    int n = center->n;
    for (int k = 0; k < n; k++) {
        int idx = (cursor + dir * k) % n;
        if (idx < 0) idx += n;
        if (coord_on_boundary_local(center->v[idx], &s->poly)) return idx;
    }
    return -1;
}

static int dictionary_choices_at(const Tile *tile,
                                 const RL0ForgetMap *map,
                                 const RL1State *s,
                                 Coord target,
                                 RL0FMArc *key,
                                 const RL0FMArc **values,
                                 int *value_count) {
    if (!boundary0_build_vertex_arc(tile, s->tiles, s->tile_count, target, key)) return 0;
    if (!rl0_fm_lookup_any_rotation(map, key, values, value_count, NULL)) return 0;
    return *value_count > 0;
}

static int nonempty_choice_count(const RL0FMArc *values, int value_count) {
    int n = 0;
    for (int i = 0; i < value_count; i++) {
        if (values[i].n > 0) n++;
    }
    return n;
}

static int seed_branch_count_at(const Tile *tile,
                                const RL0ForgetMap *map,
                                int index) {
    RL1State *seed = calloc(1, sizeof(*seed));
    RL0FMArc key;
    const RL0FMArc *values = NULL;
    int value_count = 0;
    int count = 1000000;

    if (!seed) return 1000000;
    if (!make_seed_state(tile, seed)) goto done;
    if (!dictionary_choices_at(tile, map, seed, tile->base.v[index], &key, &values, &value_count)) goto done;
    count = nonempty_choice_count(values, value_count);

done:
    free(seed);
    return count;
}

static void choose_heuristic(const Tile *tile,
                             const RL0ForgetMap *map,
                             int requested_start,
                             int requested_dir,
                             int *start_out,
                             int *dir_out) {
    int best_start = 0;
    int best_dir = 1;
    int best_score[4] = {1000000, 1000000, 1000000, 1000000};
    int n = tile->base.n;

    if (requested_start >= 0 && requested_start < n && requested_dir != 0) {
        *start_out = requested_start;
        *dir_out = requested_dir;
        return;
    }

    for (int i = 0; i < n; i++) {
        for (int dcase = 0; dcase < 2; dcase++) {
            int dir = dcase == 0 ? 1 : -1;
            int score[4];
            for (int k = 0; k < 4; k++) {
                int idx = (i + dir * k) % n;
                if (idx < 0) idx += n;
                score[k] = seed_branch_count_at(tile, map, idx);
            }

            if (requested_start >= 0 && requested_start != i) continue;
            if (requested_dir != 0 && requested_dir != dir) continue;

            int better = 0;
            for (int k = 0; k < 4; k++) {
                if (score[k] < best_score[k]) { better = 1; break; }
                if (score[k] > best_score[k]) break;
            }
            if (better) {
                for (int k = 0; k < 4; k++) best_score[k] = score[k];
                best_start = i;
                best_dir = dir;
            }
        }
    }

    *start_out = best_start;
    *dir_out = best_dir;
}

static int state_after_choice(const RL1State *s,
                              const Tile *tile,
                              const RL0ForgetMap *map,
                              Coord target,
                              const RL0FMArc *choice,
                              int max_tiles,
                              int live_only,
                              RL1State *out,
                              Attach0Stats *astats) {
    Cycle *grown_tiles = calloc((size_t)ATTACH0_MAX_TILES, sizeof(*grown_tiles));
    int grown_tile_count = 0;
    Boundary0Stats bstats;
    int ok = 0;

    if (!grown_tiles) return 0;
    *out = *s;

    if (!attach0_try_attach_arc(&s->poly,
                                tile,
                                s->tiles,
                                s->tile_count,
                                target,
                                choice,
                                &out->poly,
                                grown_tiles,
                                &grown_tile_count,
                                astats)) {
        goto done;
    }
    if (grown_tile_count < 0 || grown_tile_count > ATTACH0_MAX_TILES) goto done;
    if (grown_tile_count > max_tiles) {
        ok = -1;
        goto done;
    }

    out->tile_count = grown_tile_count;
    for (int i = 0; i < grown_tile_count; i++) out->tiles[i] = grown_tiles[i];

    out->hidden_count = rebuild_hidden_local(&out->poly, out->tiles, out->tile_count, out->hidden);
    if (out->hidden_count < 0) goto done;

    if (live_only) {
        boundary0_stats_init(&bstats);
        if (!boundary0_poly_has_live_boundary(&out->poly,
                                              tile,
                                              out->tiles,
                                              out->tile_count,
                                              map,
                                              &bstats)) {
            goto done;
        }
    }

    poly_to_key_local(&out->poly, out->key, sizeof(out->key));
    ok = 1;

done:
    free(grown_tiles);
    return ok;
}

static int dfs_ring(RL1Ctx *ctx,
                    RL1Search *search,
                    const Tile *tile,
                    const RL0ForgetMap *map,
                    const RL1State *s,
                    const Cycle *center,
                    int cursor) {
    int target_index;
    Coord target;
    RL0FMArc key;
    const RL0FMArc *values = NULL;
    int value_count = 0;
    Attach0Stats astats;

    search->dfs_calls++;

    if (all_center_vertices_gone(s, center)) {
        Poly *canonical = malloc(sizeof(*canonical));
        if (!canonical) return 0;
        poly_hash_key_lattice(&s->poly, TILE_LATTICE_TETRILLE, canonical);
        if (!hash_insert(&search->seen_outputs, canonical)) {
            free(canonical);
            search->duplicate_count++;
            return 1;
        }
        free(canonical);
        if (!records_add(ctx, s, center, search->start_index, search->dir)) return 0;
        search->complete_count++;
        return 1;
    }

    if (s->tile_count >= search->max_tiles) return 1;

    target_index = next_center_boundary_index(s, center, cursor, search->dir);
    if (target_index < 0) return 1;
    target = center->v[target_index];

    if (!dictionary_choices_at(tile, map, s, target, &key, &values, &value_count)) return 1;

    for (int i = 0; i < value_count; i++) {
        RL1State *next = NULL;
        int next_cursor;
        int r;

        if (values[i].n <= 0) continue;
        next = malloc(sizeof(*next));
        if (!next) return 0;

        attach0_stats_init(&astats);
        search->attach_attempts++;
        r = state_after_choice(s,
                               tile,
                               map,
                               target,
                               &values[i],
                               search->max_tiles,
                               search->live_only,
                               next,
                               &astats);
        if (r <= 0) {
            free(next);
            continue;
        }

        search->attach_successes++;
        next_cursor = (target_index + search->dir) % center->n;
        if (next_cursor < 0) next_cursor += center->n;

        if (!dfs_ring(ctx, search, tile, map, next, center, next_cursor)) {
            free(next);
            return 0;
        }
        free(next);
    }

    return 1;
}

static void rewrite_sorted_records(RL1Ctx *ctx) {
    qsort(ctx->records,
          ctx->record_count,
          sizeof(*ctx->records),
          record_cmp_local);

    FILE *fp = fopen(ctx->output_path, "w");
    if (!fp) {
        fprintf(stderr, "failed to rewrite output: %s\n", ctx->output_path);
        return;
    }

    for (size_t i = 0; i < ctx->record_count; i++) {
        write_one_record(fp, i + 1, &ctx->records[i]);
    }
    fclose(fp);
}

static void print_seed_counts(const Tile *tile, const RL0ForgetMap *map) {
    RL1State *seed = calloc(1, sizeof(*seed));
    if (!seed) return;
    if (!make_seed_state(tile, seed)) {
        free(seed);
        return;
    }
    fprintf(stderr, "first-step indexed arc counts:\n");
    for (int i = 0; i < tile->base.n; i++) {
        RL0FMArc key;
        const RL0FMArc *values = NULL;
        int value_count = 0;
        int count = 0;
        if (dictionary_choices_at(tile, map, seed, tile->base.v[i], &key, &values, &value_count)) {
            count = nonempty_choice_count(values, value_count);
        }
        fprintf(stderr,
                "  %2d: valence=%d coord=(%d,%d) choices=%d\n",
                i,
                tile->base.v[i].v,
                tile->base.v[i].x,
                tile->base.v[i].y,
                count);
    }
    free(seed);
}

static int deletion_file_level(const RL0FMDeletionSet *deletions) {
    int level = -1;
    if (!deletions) return -1;
    for (int i = 0; i < deletions->count; i++) {
        if (deletions->level[i] > level) level = deletions->level[i];
    }
    return level;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [tile_path] [output_path] [max_tiles] [options]\n"
            "default tile_path: tiles/hat.tile\n"
            "default output_path: data/rl1/completions.dat\n"
            "default max_tiles: 20\n"
            "options:\n"
            "  --remembrance PATH  RL0 remembrance dictionary (default data/rl0/remembrance.dat)\n"
            "  --deletions PATH    RL0 deletions file (default data/rl0/deletions.dat)\n"
            "  --live-only         prune states with dead outer boundary vertices\n"
            "  --start N           force central-hat starting vertex index\n"
            "  --cw | --ccw        force traversal direction\n"
            "  --counts-only       print first-step counts and exit\n",
            prog);
}

int main(int argc, char **argv) {
    const char *tile_path = "tiles/hat.tile";
    const char *output_path = "data/rl1/completions.dat";
    const char *remembrance_path = "data/rl0/remembrance.dat";
    const char *deletions_path = "data/rl0/deletions.dat";
    int max_tiles = 20;
    int live_only = 0;
    int requested_start = -1;
    int requested_dir = 0;
    int counts_only = 0;
    int positional = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--live-only") == 0) {
            live_only = 1;
        } else if (strcmp(argv[i], "--cw") == 0) {
            requested_dir = -1;
        } else if (strcmp(argv[i], "--ccw") == 0) {
            requested_dir = 1;
        } else if (strcmp(argv[i], "--counts-only") == 0) {
            counts_only = 1;
        } else if (strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            requested_start = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--remembrance") == 0 && i + 1 < argc) {
            remembrance_path = argv[++i];
        } else if (strcmp(argv[i], "--deletions") == 0 && i + 1 < argc) {
            deletions_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            return 1;
        } else {
            if (positional == 0) tile_path = argv[i];
            else if (positional == 1) output_path = argv[i];
            else if (positional == 2) max_tiles = atoi(argv[i]);
            else {
                usage(argv[0]);
                return 1;
            }
            positional++;
        }
    }

    if (max_tiles < 1) max_tiles = 1;
    if (max_tiles > ATTACH0_MAX_TILES) max_tiles = ATTACH0_MAX_TILES;

    Tile tile;
    RL0ForgetMap *map = NULL;
    RL1State *seed = NULL;
    RL1Ctx ctx;
    RL1Search search;
    int start_index = 0;
    int dir = 1;

    if (!tile_load(tile_path, &tile)) {
        fprintf(stderr, "failed to load tile: %s\n", tile_path);
        return 1;
    }

    map = malloc(sizeof(*map));
    if (!map) {
        fprintf(stderr, "failed to allocate RL0 map\n");
        return 1;
    }

    RL0FMDeletionSet deletions;
    rl0_fm_init(map);
    rl0_fm_deletions_init(&deletions);
    if (!rl0_fm_load_deletions(&deletions, deletions_path)) {
        fprintf(stderr, "failed to load deletions: %s\n", deletions_path);
        rl0_fm_deletions_clear(&deletions);
        free(map);
        return 1;
    }
    if (!rl0_fm_load_remembrance_filtered(map,
                                          remembrance_path,
                                          &deletions,
                                          deletion_file_level(&deletions))) {
        fprintf(stderr,
                "failed to load RL0 dictionary: remembrance=%s deletions=%s\n",
                remembrance_path,
                deletions_path);
        rl0_fm_deletions_clear(&deletions);
        free(map);
        return 1;
    }
    rl0_fm_deletions_clear(&deletions);

    if (counts_only) {
        print_seed_counts(&tile, map);
        return 0;
    }

    choose_heuristic(&tile, map, requested_start, requested_dir, &start_index, &dir);
    seed = calloc(1, sizeof(*seed));
    if (!seed || !make_seed_state(&tile, seed)) {
        fprintf(stderr, "failed to build central-hat seed\n");
        free(seed);
        free(map);
        return 1;
    }

    ensure_parent_dirs();

    memset(&ctx, 0, sizeof(ctx));
    ctx.tile = &tile;
    ctx.output_path = output_path;

    memset(&search, 0, sizeof(search));
    search.start_index = start_index;
    search.dir = dir;
    search.max_tiles = max_tiles;
    search.live_only = live_only;
    hash_init(&search.seen_outputs, 65536);

    if (!dfs_ring(&ctx, &search, &tile, map, seed, &tile.base, start_index)) {
        fprintf(stderr, "DFS failed\n");
        hash_destroy(&search.seen_outputs);
        free(seed);
        free(map);
        return 1;
    }

    rewrite_sorted_records(&ctx);

    fprintf(stderr,
            "wrote %zu records to %s\n"
            "dfs_calls=%zu complete=%zu duplicates=%zu\n",
            ctx.record_count,
            output_path,
            search.dfs_calls,
            search.complete_count,
            search.duplicate_count);

    for (size_t i = 0; i < ctx.record_count; i++) free(ctx.records[i].boundary_str);
    free(ctx.records);
    hash_destroy(&search.seen_outputs);
    free(seed);
    free(map);
    return 0;
}
