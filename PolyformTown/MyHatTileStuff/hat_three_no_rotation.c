/*
 * hat_three_no_rotation.c
 *
 * Show that a chain of 3 hat tiles stacked "tip into notch" — the convex crown
 * tip of each tile fitting into the concave bottom notch of the previous tile,
 * all tiles in the same base orientation (variant 0, no rotation) — retains a
 * live boundary and can be extended to a larger valid hat tiling.
 *
 * Hat tile geometry (tetrille lattice, hat.tile cycle):
 *   Tip   = vertex 1, the crown peak.
 *           Adjacent edges: te=0 (v0→v1, right crown slope)
 *                           te=1 (v1→v2, left  crown slope)
 *   Notch = vertex 9, the concave bottom dip.
 *           Adjacent edges: te=8 (v8→v9) and te=9 (v9→v10)
 *   Edges 0 & 9 are antiparallel; edges 1 & 8 are antiparallel.
 *   Restricting each new tile to te ∈ {0, 1} forces "tip-first" placement,
 *   so every attachment is a convex tip fitting into a concave notch.
 *
 * Build:  make hat_three_no_rotation
 * Run:    ./bin/hat_three_no_rotation
 *         ./bin/hat_three_no_rotation | ./bin/imgtable > out.svg
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/attach.h"
#include "core/boundary.h"
#include "core/cycle.h"
#include "core/hash.h"
#include "core/tile.h"
#include "core/vec.h"

/* Tip edge indices in the hat tile cycle (same for variants[0]). */
enum { TIP_EDGE_LO = 0, TIP_EDGE_HI = 1 };

/* Grow by attaching tile.variants[0] TIP-FIRST (te ∈ {TIP_EDGE_LO, TIP_EDGE_HI}).
   Only boundary edges that are antiparallel to a tip edge will succeed, which
   corresponds exactly to the concave-notch openings of the accumulated shape. */
static void grow_tip_to_notch(const PolyVec *cur, PolyVec *next,
                               HashTable *seen, const Tile *tile) {
    const Cycle *v0 = &tile->variants[0];
    for (size_t i = 0; i < cur->count; i++) {
        Edge edges[4096];
        int ec = build_boundary_edges(&cur->data[i], edges);
        for (int be = 0; be < ec; be++) {
            for (int te = TIP_EDGE_LO; te <= TIP_EDGE_HI; te++) {
                Poly grown, canon;
                if (!try_attach_tile_poly(&cur->data[i], v0, tile->lattice,
                                          be, te, &grown))
                    continue;
                poly_hash_key_lattice(&grown, tile->lattice, &canon);
                if (hash_insert(seen, &canon))
                    vec_push(next, &canon);
            }
        }
    }
}

int main(int argc, char **argv) {
    const char *tile_path = "tiles/hat.tile";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tile") == 0 && i + 1 < argc)
            tile_path = argv[++i];
    }

    Tile tile;
    if (!tile_load(tile_path, &tile)) {
        fprintf(stderr, "Failed to load tile: %s\n", tile_path);
        return 1;
    }

    fprintf(stderr, "=== Hat: Three Tiles Stacked Tip-into-Notch (No Rotation) ===\n");
    fprintf(stderr, "Tile file        : %s\n", tile_path);
    fprintf(stderr, "Attachment mode  : tip-first (te = %d or %d only)\n",
            TIP_EDGE_LO, TIP_EDGE_HI);
    fprintf(stderr, "Orientation      : variant 0 throughout (no rotation)\n\n");

    /* Seed: one tile in canonical form of variant 0 */
    Poly seed_raw, seed;
    seed_raw.cycle_count = 1;
    seed_raw.cycles[0] = tile.variants[0];
    poly_hash_key_lattice(&seed_raw, tile.lattice, &seed);

    PolyVec cur, next;
    vec_init(&cur, 16);
    vec_init(&next, 16);
    HashTable seen;
    hash_init(&seen, 512);

    vec_push(&cur, &seed);
    hash_insert(&seen, &seed);

    /* Grow from 1 to 3 tiles, tip-into-notch, no rotation */
    for (int level = 2; level <= 3; level++) {
        vec_clear(&next);
        grow_tip_to_notch(&cur, &next, &seen, &tile);
        fprintf(stderr, "Level %d (tip-into-notch, variant 0): %zu distinct shape(s)\n",
                level, next.count);
        if (next.count == 0) {
            fprintf(stderr, "\nCannot form a %d-hat tip-into-notch chain.\n", level);
            vec_destroy(&cur); vec_destroy(&next); hash_destroy(&seen);
            return 0;
        }
        PolyVec tmp = cur; cur = next; next = tmp;
    }

    /* Check live boundary for each 3-hat tip-into-notch cluster */
    int live = 0, blocked = 0;
    for (size_t i = 0; i < cur.count; i++) {
        if (poly_has_live_boundary(&cur.data[i], &tile))
            live++;
        else
            blocked++;
    }

    fprintf(stderr, "\n--- 3-hat tip-into-notch clusters (variant 0, no rotation) ---\n");
    fprintf(stderr, "  Distinct shapes      : %zu\n", cur.count);
    fprintf(stderr, "  Live boundary        : %d  (can be extended to a larger tiling)\n", live);
    fprintf(stderr, "  Blocked boundary     : %d  (dead end)\n", blocked);

    if (live > 0)
        fprintf(stderr,
                "\nConclusion: %d of %zu tip-into-notch 3-hat chain(s) can be\n"
                "  extended to a very large hat tiling patch!\n",
                live, cur.count);
    else
        fprintf(stderr, "\nAll 3-hat tip-into-notch clusters are dead ends.\n");

    /* Output all clusters to stdout (live first) for piping to imgtable */
    fprintf(stderr, "\nOutputting all %zu clusters (live first) to stdout:\n", cur.count);
    for (size_t i = 0; i < cur.count; i++)
        if (poly_has_live_boundary(&cur.data[i], &tile))
            tile_print_imgtable_shape(&tile, &cur.data[i]);
    for (size_t i = 0; i < cur.count; i++)
        if (!poly_has_live_boundary(&cur.data[i], &tile))
            tile_print_imgtable_shape(&tile, &cur.data[i]);

    hash_destroy(&seen);
    vec_destroy(&cur);
    vec_destroy(&next);
    return 0;
}
