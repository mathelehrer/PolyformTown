#include <stdio.h>

#include "core/cycle.h"
#include "core/lattice.h"
#include "core/tile.h"

static int cycle_equal_exact(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) {
        if (!coord_eq(a->v[i], b->v[i])) return 0;
    }
    return 1;
}

static int source_slot_for_parity(int n, int p, int source_index) {
    if (source_index < 0 || source_index >= n) return -1;
    if (p == 1) return source_index;
    if (p == -1) return n - 1 - source_index;
    return -1;
}

static void print_cycle_line(const Cycle *c) {
    printf("[");
    for (int i = 0; i < c->n; i++) {
        if (i) printf(",");
        printf("(%d,%d,%d)", c->v[i].v, c->v[i].x, c->v[i].y);
    }
    printf("]");
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "tiles/hat.tile";
    Tile tile;
    int failures = 0;

    if (!tile_load(path, &tile)) {
        fprintf(stderr, "failed to load tile: %s\n", path);
        return 1;
    }

    printf("tile:%s variants:%d vertices:%d\n", path, tile.variant_count, tile.base.n);

    for (int v = 0; v < tile.variant_count; v++) {
        int matches = 0;
        printf("variant:%d ", v);
        for (int t = 0; t < lattice_transform_count(tile.lattice); t++) {
            Cycle expected = {0};
            int p;
            cycle_transform_lattice(&tile.base, &expected, tile.lattice, t);
            p = cycle_signed_area2(&expected, tile.lattice) >= 0 ? 1 : -1;
            if (p < 0) {
                Cycle rev = {0};
                rev.n = expected.n;
                for (int i = 0; i < expected.n; i++) rev.v[i] = expected.v[expected.n - 1 - i];
                expected = rev;
            }
            cycle_normalize_position(&expected, tile.lattice);
            if (!cycle_equal_exact(&tile.variants[v], &expected)) continue;
            matches++;
            printf("match(transform=%d,p=%d,slots=", t, p);
            for (int i = 0; i < tile.base.n; i++) {
                if (i) printf(",");
                printf("%d->%d", i, source_slot_for_parity(tile.base.n, p, i));
            }
            printf(") ");
        }
        if (matches == 0) {
            failures++;
            printf("NO_EXACT_MATCH ");
        }
        print_cycle_line(&tile.variants[v]);
        printf("\n");
    }

    if (failures) {
        fprintf(stderr, "FAIL: %d variants did not preserve exact indexed order\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
