#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/cycle.h"
#include "core/lattice.h"
#include "core/tetrille.h"
#include "core/tile.h"
#include "rl0/boundary0.h"
#include "rl1/bcomp1.h"

static int cycle_equal_exact(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) {
        if (!coord_eq(a->v[i], b->v[i])) return 0;
    }
    return 1;
}

static int coord_index_exact(const Cycle *c, Coord q) {
    for (int i = 0; i < c->n; i++) {
        if (coord_eq(c->v[i], q)) return i;
    }
    return -1;
}

static void reverse_copy(const Cycle *src, Cycle *dst) {
    dst->n = src->n;
    for (int i = 0; i < src->n; i++) dst->v[i] = src->v[src->n - 1 - i];
}

static int translate_like_first_vertex(const Cycle *src, Coord dst_first, int lattice, Cycle *out) {
    *out = *src;
    if (src->n <= 0) return 0;
    if (lattice == TILE_LATTICE_TETRILLE) {
        int dx = dst_first.x - src->v[0].x;
        int dy = dst_first.y - src->v[0].y;
        int m6 = 0, n6 = 0;
        if (dst_first.v != src->v[0].v) return 0;
        if (!tetrille_delta_to_6(src->v[0].v, dx, dy, &m6, &n6)) return 0;
        tetrille_translate_cycle(out, m6, n6);
        return 1;
    }
    if (dst_first.v != src->v[0].v) return 0;
    cycle_translate(out, dst_first.x - src->v[0].x, dst_first.y - src->v[0].y);
    return 1;
}

static int strict_source_order_item(const Tile *tile, const Cycle *placed, Coord q, RL0FMItem *out) {
    int n = tile->base.n;
    long long src_area = cycle_signed_area2(&tile->base, tile->lattice);
    if (placed->n != n || n <= 0) return 0;

    for (int t = 0; t < lattice_transform_count(tile->lattice); t++) {
        Cycle tr = {0};
        Cycle oriented = {0};
        Cycle translated = {0};
        int p;
        cycle_transform_lattice(&tile->base, &tr, tile->lattice, t);
        p = (cycle_signed_area2(&tr, tile->lattice) * src_area >= 0) ? 1 : -1;
        if (p == 1) oriented = tr;
        else reverse_copy(&tr, &oriented);
        if (!translate_like_first_vertex(&oriented, placed->v[0], tile->lattice, &translated)) continue;
        if (!cycle_equal_exact(&translated, placed)) continue;
        {
            int pos = coord_index_exact(placed, q);
            if (pos < 0) return 0;
            out->p = p;
            out->i = (p == 1) ? pos : (n - 1 - pos);
            return 1;
        }
    }
    return 0;
}

static int check_record(const Tile *tile, const BComp1Record *rec, size_t rec_idx,
                        size_t *tile_count, size_t *vertex_count, size_t *fail_count) {
    int ok = 1;
    if (!rec->have_tiles) {
        fprintf(stderr, "record=%zu missing tiles\n", rec_idx);
        (*fail_count)++;
        return 0;
    }
    if (rec->tile_count != rec->tiles_count) {
        fprintf(stderr, "record=%zu tile_count=%d tiles_list=%d mismatch\n",
                rec_idx, rec->tile_count, rec->tiles_count);
        (*fail_count)++;
        ok = 0;
    }
    for (int t = 0; t < rec->tiles_count; t++) {
        const Cycle *placed = &rec->tiles[t];
        RL0FMItem first;
        if (!strict_source_order_item(tile, placed, placed->v[0], &first)) {
            fprintf(stderr, "record=%zu tile=%d not in strict source/reverse-source order\n", rec_idx, t);
            (*fail_count)++;
            ok = 0;
            continue;
        }
        (*tile_count)++;
        for (int i = 0; i < placed->n; i++) {
            RL0FMItem strict_item;
            RL0FMItem extracted_item;
            if (!strict_source_order_item(tile, placed, placed->v[i], &strict_item)) {
                fprintf(stderr, "record=%zu tile=%d vertex=%d strict extraction failed\n", rec_idx, t, i);
                (*fail_count)++;
                ok = 0;
                continue;
            }
            if (!boundary0_tile_item_at_vertex(tile, placed, placed->v[i], &extracted_item)) {
                fprintf(stderr, "record=%zu tile=%d vertex=%d boundary0 extraction failed\n", rec_idx, t, i);
                (*fail_count)++;
                ok = 0;
                continue;
            }
            if (strict_item.p != extracted_item.p || strict_item.i != extracted_item.i) {
                fprintf(stderr,
                        "record=%zu tile=%d vertex=%d index mismatch strict=(%d,%d) boundary0=(%d,%d)\n",
                        rec_idx, t, i,
                        strict_item.p, strict_item.i,
                        extracted_item.p, extracted_item.i);
                (*fail_count)++;
                ok = 0;
            }
            (*vertex_count)++;
        }
    }
    return ok;
}

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s [records.dat] [tile.tile]\n", prog);
}

int main(int argc, char **argv) {
    const char *records_path = argc > 1 ? argv[1] : "data/rl1/completions.dat";
    const char *tile_path = argc > 2 ? argv[2] : "tiles/hat.tile";
    Tile tile;
    BComp1RecordVec records = {0};
    size_t checked_tiles = 0, checked_vertices = 0, failures = 0;

    if (argc > 3) {
        usage(argv[0]);
        return 1;
    }
    if (!tile_load(tile_path, &tile)) {
        fprintf(stderr, "failed to load tile: %s\n", tile_path);
        return 1;
    }
    if (!bcomp1_load_records(records_path, &records)) {
        fprintf(stderr, "failed to load records: %s\n", records_path);
        return 1;
    }

    for (size_t i = 0; i < records.count; i++) {
        check_record(&tile, &records.items[i], i + 1, &checked_tiles, &checked_vertices, &failures);
    }

    printf("records=%zu tiles=%zu vertices=%zu failures=%zu\n",
           records.count, checked_tiles, checked_vertices, failures);

    bcomp1_free_records(&records);
    return failures ? 1 : 0;
}
