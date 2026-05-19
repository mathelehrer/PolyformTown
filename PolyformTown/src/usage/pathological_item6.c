#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/boundary.h"
#include "core/cycle.h"
#include "core/lattice.h"
#include "core/tetrille.h"
#include "rl1/bcomp1.h"

#define PATH_ARC_START 9
#define PATH_ARC_END 21
#define MAX_LOCAL (4 * MAX_VERTS)

typedef struct {
    const char *supertile_path;
    const char *data_out;
} Options;

typedef struct {
    Edge e;
    int canceled;
} LEdge;

enum {
    WALK_FAIL = 0,
    WALK_OK = 1,
    WALK_DUP = 2,
    WALK_BOUND = 3
};

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [--supertile PATH] [--data PATH]\n"
            "defaults:\n"
            "  --supertile preferences/focus.supertile\n"
            "  --data      data/rl5/pathological_item.dat\n",
            prog);
}

static int parse_args(int argc, char **argv, Options *opt) {
    opt->supertile_path = "preferences/focus.supertile";
    opt->data_out = "data/rl5/pathological_item.dat";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--supertile") == 0 && i + 1 < argc) {
            opt->supertile_path = argv[++i];
        } else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            opt->data_out = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 0;
        }
    }
    return 1;
}

static int coord_cmp_local(const void *A, const void *B) {
    const Coord *a = (const Coord *)A;
    const Coord *b = (const Coord *)B;
    if (a->v != b->v) return a->v - b->v;
    if (a->x != b->x) return a->x - b->x;
    return a->y - b->y;
}

static int coord_in_list(const Coord *items, int count, Coord q) {
    for (int i = 0; i < count; i++) if (coord_eq(items[i], q)) return 1;
    return 0;
}

static int collect_tile_vertices(const Cycle *tiles, int tile_count, Coord *verts) {
    int n = 0;
    for (int t = 0; t < tile_count; t++) {
        for (int i = 0; i < tiles[t].n; i++) {
            if (!coord_in_list(verts, n, tiles[t].v[i])) {
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
        if (!coord_in_list(boundary, bc, all[i])) {
            if (hc >= BCOMP1_MAX_COORDS) return -1;
            if (hidden) hidden[hc] = all[i];
            hc++;
        }
    }
    if (hidden && hc > 1) qsort(hidden, (size_t)hc, sizeof(Coord), coord_cmp_local);
    return hc;
}

static int edge_same(Edge a, Edge b) { return coord_eq(a.a, b.a) && coord_eq(a.b, b.b); }
static int edge_opp(Edge a, Edge b) { return coord_eq(a.a, b.b) && coord_eq(a.b, b.a); }

static int build_union_edges_from_tiles(const Cycle *tiles, int tile_count, LEdge *out, int *out_n) {
    int n = 0;
    for (int t = 0; t < tile_count; t++) {
        for (int i = 0; i < tiles[t].n; i++) {
            if (n >= MAX_LOCAL) return 0;
            out[n].e = cycle_edge(&tiles[t], i);
            out[n].canceled = 0;
            n++;
        }
    }
    for (int i = 0; i < n; i++) {
        if (out[i].canceled) continue;
        for (int j = i + 1; j < n; j++) {
            if (out[j].canceled) continue;
            if (edge_same(out[i].e, out[j].e)) return 0;
            if (edge_opp(out[i].e, out[j].e)) {
                out[i].canceled = 1;
                out[j].canceled = 1;
                break;
            }
        }
    }
    *out_n = n;
    return 1;
}

static int coord_seen_before(const Coord *seen, int seen_n, Coord c) {
    for (int i = 0; i < seen_n; i++) if (coord_eq(seen[i], c)) return 1;
    return 0;
}

static int pick_next_edge(const Edge *edges,
                          int m,
                          const int *used,
                          Coord v,
                          int prev_dir,
                          int prefer_left,
                          int lattice) {
    int dir_count = lattice_direction_count(lattice);
    int rev = (prev_dir + dir_count / 2) % dir_count;
    int best = -1;
    int best_score = prefer_left ? -1 : dir_count + 1;
    for (int i = 0; i < m; i++) {
        int d;
        int score;
        if (used[i]) continue;
        if (!coord_eq(edges[i].a, v)) continue;
        d = lattice_direction_index(lattice, edges[i]);
        if (d < 0) continue;
        score = (d - rev + dir_count) % dir_count;
        if (score == 0) continue;
        if (prefer_left) {
            if (score > best_score) {
                best_score = score;
                best = i;
            }
        } else {
            if (score < best_score) {
                best_score = score;
                best = i;
            }
        }
    }
    return best;
}

static int walk_one_cycle(const Edge *edges,
                          int m,
                          int *used,
                          int start,
                          int prefer_left,
                          int lattice,
                          Cycle *out) {
    Coord seen[MAX_VERTS];
    int seen_n = 0;
    int cur = start;
    out->n = 0;
    while (1) {
        int prev_dir;
        int next;
        Coord v;
        if (used[cur]) return WALK_FAIL;
        if (out->n >= MAX_VERTS) return WALK_BOUND;
        if (coord_seen_before(seen, seen_n, edges[cur].a)) return WALK_DUP;
        seen[seen_n++] = edges[cur].a;
        used[cur] = 1;
        out->v[out->n++] = edges[cur].a;
        v = edges[cur].b;
        if (coord_eq(v, edges[start].a)) break;
        prev_dir = lattice_direction_index(lattice, edges[cur]);
        if (prev_dir < 0) return WALK_FAIL;
        next = pick_next_edge(edges, m, used, v, prev_dir, prefer_left, lattice);
        if (next < 0) return WALK_FAIL;
        cur = next;
    }
    return WALK_OK;
}

static int find_start_edge(const Edge *edges, int m, const int *used, int lattice) {
    int start = -1;
    for (int i = 0; i < m; i++) {
        double ix, iy, sx, sy;
        int idir, sdir;
        if (used[i]) continue;
        lattice_embed_point(lattice, edges[i].a, &ix, &iy);
        idir = lattice_direction_index(lattice, edges[i]);
        if (start < 0) {
            start = i;
            continue;
        }
        lattice_embed_point(lattice, edges[start].a, &sx, &sy);
        sdir = lattice_direction_index(lattice, edges[start]);
        if (ix < sx || (ix == sx && iy < sy) || (ix == sx && iy == sy && idir < sdir)) start = i;
    }
    return start;
}

static int extract_cycles(const LEdge *in, int in_n, int prefer_left, int lattice, Poly *out) {
    Edge edges[MAX_LOCAL];
    int used[MAX_LOCAL] = {0};
    int m = 0;
    out->cycle_count = 0;
    for (int i = 0; i < in_n; i++) if (!in[i].canceled) edges[m++] = in[i].e;
    if (m == 0) return 0;
    while (1) {
        int start = find_start_edge(edges, m, used, lattice);
        int used_snapshot[MAX_LOCAL];
        Cycle c;
        int r;
        if (start < 0) break;
        if (out->cycle_count >= MAX_CYCLES) return 0;
        memcpy(used_snapshot, used, sizeof(used));
        r = walk_one_cycle(edges, m, used, start, prefer_left, lattice, &c);
        if (r == WALK_DUP) {
            memcpy(used, used_snapshot, sizeof(used));
            r = walk_one_cycle(edges, m, used, start, !prefer_left, lattice, &c);
        }
        if (r != WALK_OK) return 0;
        out->cycles[out->cycle_count++] = c;
    }
    return out->cycle_count;
}

static void transform_cycle_tetrille(const Cycle *src, Cycle *dst, int t, int m6, int n6) {
    cycle_transform_lattice(src, dst, TILE_LATTICE_TETRILLE, t);
    tetrille_translate_cycle(dst, m6, n6);
}

static int find_self_match_transform(const Cycle *boundary,
                                     int start,
                                     int end,
                                     int *out_t,
                                     int *out_m6,
                                     int *out_n6) {
    Cycle arc, tr;
    if (!boundary || start < 0 || end < start || end >= boundary->n) return 0;
    arc.n = 0;
    for (int i = start; i <= end; i++) arc.v[arc.n++] = boundary->v[i];
    for (int t = 0; t < 6; t++) {
        int dx, dy, m6, n6;
        int ok = 1;
        transform_cycle_tetrille(&arc, &tr, t, 0, 0);
        dx = arc.v[end - start].x - tr.v[0].x;
        dy = arc.v[end - start].y - tr.v[0].y;
        if (!tetrille_delta_to_6(tr.v[0].v, dx, dy, &m6, &n6)) continue;
        tetrille_translate_cycle(&tr, m6, n6);
        if (tr.n != arc.n) continue;
        for (int i = 0; i < arc.n; i++) {
            if (!coord_eq(tr.v[i], arc.v[arc.n - 1 - i])) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            *out_t = t;
            *out_m6 = m6;
            *out_n6 = n6;
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Options opt;
    BComp1RecordVec super_records = {0};
    BComp1Record out;
    Cycle all_tiles[ATTACH0_MAX_TILES];
    LEdge merged[MAX_LOCAL];
    int tile_count = 0;
    int merged_n = 0;
    int t = 0, m6 = 0, n6 = 0;
    FILE *fp = NULL;

    memset(&out, 0, sizeof(out));

    if (!parse_args(argc, argv, &opt)) {
        usage(argv[0]);
        return 1;
    }
    if (!bcomp1_load_records(opt.supertile_path, &super_records) || super_records.count < 1) {
        fprintf(stderr, "failed to load supertile: %s\n", opt.supertile_path);
        return 1;
    }
    if (!super_records.items[0].have_boundary || !super_records.items[0].have_tiles || !super_records.items[0].have_center) {
        fprintf(stderr, "supertile record missing boundary/tiles/center\n");
        bcomp1_free_records(&super_records);
        return 1;
    }

    const BComp1Record *src = &super_records.items[0];
    if (!find_self_match_transform(&src->boundary.cycles[0], PATH_ARC_START, PATH_ARC_END, &t, &m6, &n6)) {
        fprintf(stderr, "failed to find rotation for boundary arc v%d..v%d\n", PATH_ARC_START, PATH_ARC_END);
        bcomp1_free_records(&super_records);
        return 1;
    }
    if (src->tiles_count * 2 > ATTACH0_MAX_TILES) {
        fprintf(stderr, "too many tiles for output record\n");
        bcomp1_free_records(&super_records);
        return 1;
    }

    for (int i = 0; i < src->tiles_count; i++) all_tiles[tile_count++] = src->tiles[i];
    for (int i = 0; i < src->tiles_count; i++) {
        transform_cycle_tetrille(&src->tiles[i], &all_tiles[tile_count], t, m6, n6);
        tile_count++;
    }

    if (!build_union_edges_from_tiles(all_tiles, tile_count, merged, &merged_n) || !extract_cycles(merged, merged_n, 1, TILE_LATTICE_TETRILLE, &out.boundary)) {
        fprintf(stderr, "failed to build union boundary\n");
        bcomp1_free_records(&super_records);
        return 1;
    }

    out.level = tile_count;
    out.tile_count = tile_count;
    out.start_index = PATH_ARC_START;
    out.dir = 1;
    out.have_center = 1;
    out.have_boundary = 1;
    out.have_tiles = 1;
    out.have_hidden = 1;
    out.center = src->center;
    out.tiles_count = tile_count;
    for (int i = 0; i < tile_count; i++) out.tiles[i] = all_tiles[i];
    out.hidden_count = rebuild_hidden(&out.boundary, out.tiles, out.tiles_count, out.hidden);
    if (out.hidden_count < 0) {
        fprintf(stderr, "failed to rebuild hidden set\n");
        bcomp1_free_records(&super_records);
        return 1;
    }

    fp = fopen(opt.data_out, "w");
    if (!fp) {
        fprintf(stderr, "failed to open output: %s\n", opt.data_out);
        bcomp1_free_records(&super_records);
        return 1;
    }
    bcomp1_print_record(fp, 1, &out);
    if (fclose(fp) != 0) {
        fprintf(stderr, "failed while closing output: %s\n", opt.data_out);
        bcomp1_free_records(&super_records);
        return 1;
    }

    fprintf(stderr,
            "pathological_item: tiles=%d boundary_cycles=%d boundary_vertices=%d hidden=%d rotation=%d translate6=(%d,%d)\n",
            out.tiles_count,
            out.boundary.cycle_count,
            out.boundary.cycles[0].n,
            out.hidden_count,
            t,
            m6,
            n6);
    fprintf(stderr, "%s\n", opt.data_out);

    bcomp1_free_records(&super_records);
    return 0;
}
