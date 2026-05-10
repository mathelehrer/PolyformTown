#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "core/boundary.h"
#include "core/cycle.h"
#include "core/lattice.h"
#include "core/tile.h"
#include "core/tetrille.h"
#include "throughput/vcomp.h"

#define RL0_MAX_TRACE MAX_VERTS

typedef struct {
    int valence;
    int tile_count;
    Poly poly;
} RL0SeenKey;

typedef struct {
    int valence;
    int tile_count;
    Coord center;
    Poly poly;
    char *boundary_str;
    Cycle tiles[RL0_MAX_TRACE];
    int parities[RL0_MAX_TRACE];
    int indices[RL0_MAX_TRACE];
    Coord hidden[VCOMP_MAX_HIDDEN];
    int hidden_count;
} RL0Record;

typedef struct {
    FILE *fp;
    int lattice;
    const Tile *tile;
    Coord target;
    int record_no;
    RL0SeenKey *seen;
    size_t seen_count;
    size_t seen_cap;
    RL0Record *records;
    size_t record_count;
    size_t record_cap;
} RL0Ctx;

static int coord_less_local(Coord a, Coord b) {
    if (a.v != b.v) return a.v < b.v;
    if (a.x != b.x) return a.x < b.x;
    return a.y < b.y;
}

static int coord_cmp_local(const void *A, const void *B) {
    const Coord *a = A;
    const Coord *b = B;
    if (coord_less_local(*a, *b)) return -1;
    if (coord_less_local(*b, *a)) return 1;
    return 0;
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

static void print_indices(FILE *fp, const int *indices, int count) {
    fprintf(fp, "[");
    for (int i = 0; i < count; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "%d", indices[i]);
    }
    fprintf(fp, "]");
}

static void print_parities(FILE *fp, const int *parities, int count) {
    fprintf(fp, "[");
    for (int i = 0; i < count; i++) {
        if (i) fprintf(fp, ",");
        fprintf(fp, "%d", parities[i]);
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

static int cycle_vertex_index(const Cycle *c, Coord q) {
    for (int i = 0; i < c->n; i++) {
        if (coord_eq(c->v[i], q)) return i;
    }
    return -1;
}


static int cycle_matches_cyclic(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return 0;
    int n = a->n;
    for (int shift = 0; shift < n; shift++) {
        int ok = 1;
        for (int i = 0; i < n; i++) {
            if (!coord_eq(a->v[i], b->v[(shift + i) % n])) {
                ok = 0;
                break;
            }
        }
        if (ok) return 1;
        ok = 1;
        for (int i = 0; i < n; i++) {
            int j = (shift - i) % n;
            if (j < 0) j += n;
            if (!coord_eq(a->v[i], b->v[j])) {
                ok = 0;
                break;
            }
        }
        if (ok) return 1;
    }
    return 0;
}

static int canonicalize_tile(const Cycle *src, const Cycle *in, Coord center,
                             int lattice, Cycle *out, int *parity, int *index) {
    int n = src->n;
    if (in->n != n || n <= 0) return 0;
    long long src_area = cycle_signed_area2(src, lattice);
    int tcount = lattice_transform_count(lattice);

    for (int t = 0; t < tcount; t++) {
        Cycle tr = {0};
        cycle_transform_lattice(src, &tr, lattice, t);

        for (int j = 0; j < n; j++) {
            if (in->v[j].v != tr.v[0].v) continue;
            int dx = in->v[j].x - tr.v[0].x;
            int dy = in->v[j].y - tr.v[0].y;
            Cycle placed = tr;
            if (lattice == TILE_LATTICE_TETRILLE) {
                int m6 = 0, n6 = 0;
                if (!tetrille_delta_to_6(tr.v[0].v, dx, dy, &m6, &n6)) continue;
                tetrille_translate_cycle(&placed, m6, n6);
            } else {
                cycle_translate(&placed, dx, dy);
            }
            if (!cycle_matches_cyclic(&placed, in)) continue;

            int p = (cycle_signed_area2(&tr, lattice) * src_area >= 0) ? 1 : -1;
            *parity = p;
            out->n = n;
            if (p == 1) {
                for (int i = 0; i < n; i++) out->v[i] = placed.v[i];
            } else {
                for (int i = 0; i < n; i++) out->v[i] = placed.v[n - 1 - i];
            }
            *index = cycle_vertex_index(out, center);
            if (*index < 0) return 0;
            if (p == -1) *index = (n - 1 - *index);
            return 1;
        }
    }
    return 0;
}

static int poly_equal_local(const Poly *a, const Poly *b) {
    if (a->cycle_count != b->cycle_count) return 0;
    for (int i = 0; i < a->cycle_count; i++) {
        if (a->cycles[i].n != b->cycles[i].n) return 0;
        for (int j = 0; j < a->cycles[i].n; j++) {
            if (!coord_eq(a->cycles[i].v[j], b->cycles[i].v[j])) return 0;
        }
    }
    return 1;
}

static int seen_has(const RL0Ctx *ctx, int valence, int tile_count, const Poly *poly) {
    for (size_t i = 0; i < ctx->seen_count; i++) {
        if (ctx->seen[i].valence != valence) continue;
        if (ctx->seen[i].tile_count != tile_count) continue;
        if (poly_equal_local(&ctx->seen[i].poly, poly)) return 1;
    }
    return 0;
}

static int seen_add(RL0Ctx *ctx, int valence, int tile_count, const Poly *poly) {
    if (ctx->seen_count == ctx->seen_cap) {
        size_t nc = ctx->seen_cap ? (ctx->seen_cap * 2) : 128;
        RL0SeenKey *n = realloc(ctx->seen, nc * sizeof(*ctx->seen));
        if (!n) return 0;
        ctx->seen = n;
        ctx->seen_cap = nc;
    }
    ctx->seen[ctx->seen_count].valence = valence;
    ctx->seen[ctx->seen_count].tile_count = tile_count;
    ctx->seen[ctx->seen_count].poly = *poly;
    ctx->seen_count++;
    return 1;
}

static char *poly_to_string_local(const Poly *p) {
    size_t cap = 4096;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t off = 0;

    off += (size_t)snprintf(buf + off, cap - off, "[");
    for (int i = 0; i < p->cycle_count; i++) {
        if (i) off += (size_t)snprintf(buf + off, cap - off, "|");
        off += (size_t)snprintf(buf + off, cap - off, "[");
        for (int j = 0; j < p->cycles[i].n; j++) {
            if (j) off += (size_t)snprintf(buf + off, cap - off, ",");
            off += (size_t)snprintf(buf + off,
                                    cap - off,
                                    "(%d,%d,%d)",
                                    p->cycles[i].v[j].v,
                                    p->cycles[i].v[j].x,
                                    p->cycles[i].v[j].y);
        }
        off += (size_t)snprintf(buf + off, cap - off, "]");
    }
    off += (size_t)snprintf(buf + off, cap - off, "]");
    return buf;
}


static int find_center_pos(const Cycle *c, Coord center) {
    for (int i = 0; i < c->n; i++) {
        if (coord_eq(c->v[i], center)) return i;
    }
    return -1;
}

static int cyclic_item_seq_less(const int *parities,
                                const int *indices,
                                const int *order,
                                int n,
                                int shift_a,
                                int shift_b) {
    for (int k = 0; k < n; k++) {
        int ia = order[(shift_a + k) % n];
        int ib = order[(shift_b + k) % n];
        if (parities[ia] != parities[ib]) return parities[ia] < parities[ib];
        if (indices[ia] != indices[ib]) return indices[ia] < indices[ib];
    }
    return 0;
}

static int rotate_order_to_min_item_shift(const int *parities,
                                          const int *indices,
                                          int *order,
                                          int n) {
    int best = 0;
    int tmp[RL0_MAX_TRACE];
    if (n <= 1) return 1;
    for (int s = 1; s < n; s++) {
        if (cyclic_item_seq_less(parities, indices, order, n, s, best)) {
            best = s;
        }
    }
    for (int k = 0; k < n; k++) tmp[k] = order[(best + k) % n];
    for (int k = 0; k < n; k++) order[k] = tmp[k];
    return 1;
}

static int reorder_center_items_ccw(Cycle *tiles,
                                    int *parities,
                                    int *indices,
                                    int tile_count,
                                    Coord center) {
    Coord prevs[RL0_MAX_TRACE];
    Coord nexts[RL0_MAX_TRACE];
    int next_tile[RL0_MAX_TRACE];
    int used[RL0_MAX_TRACE] = {0};
    int order[RL0_MAX_TRACE];
    Cycle old_tiles[RL0_MAX_TRACE];
    int old_parities[RL0_MAX_TRACE];
    int old_indices[RL0_MAX_TRACE];

    if (tile_count <= 1) return 1;
    if (tile_count > RL0_MAX_TRACE) return 0;

    for (int i = 0; i < tile_count; i++) {
        int pos = find_center_pos(&tiles[i], center);
        if (pos < 0 || tiles[i].n <= 0) return 0;
        prevs[i] = tiles[i].v[(pos + tiles[i].n - 1) % tiles[i].n];
        nexts[i] = tiles[i].v[(pos + 1) % tiles[i].n];
        next_tile[i] = -1;
    }

    /*
     * The CCW walk through tile interiors crosses the edge from center to
     * prevs[i].  The next tile in the cyclic vertex figure is the unique tile
     * whose outgoing edge from center reaches the same geometric vertex.
     */
    for (int i = 0; i < tile_count; i++) {
        for (int j = 0; j < tile_count; j++) {
            if (i == j) continue;
            if (!coord_eq(nexts[j], prevs[i])) continue;
            if (next_tile[i] >= 0) return 0;
            next_tile[i] = j;
        }
        if (next_tile[i] < 0) return 0;
    }

    order[0] = 0;
    used[0] = 1;
    for (int k = 1; k < tile_count; k++) {
        int n = next_tile[order[k - 1]];
        if (n < 0 || n >= tile_count || used[n]) return 0;
        order[k] = n;
        used[n] = 1;
    }
    if (next_tile[order[tile_count - 1]] != order[0]) return 0;

    rotate_order_to_min_item_shift(parities, indices, order, tile_count);

    for (int i = 0; i < tile_count; i++) {
        old_tiles[i] = tiles[i];
        old_parities[i] = parities[i];
        old_indices[i] = indices[i];
    }
    for (int i = 0; i < tile_count; i++) {
        int src = order[i];
        tiles[i] = old_tiles[src];
        parities[i] = old_parities[src];
        indices[i] = old_indices[src];
    }
    return 1;
}

static int records_add(RL0Ctx *ctx,
                       int valence,
                       int tile_count,
                       Coord center,
                       const VCompRawState *raw,
                       const int *parities,
                       const int *indices) {
    if (ctx->record_count == ctx->record_cap) {
        size_t nc = ctx->record_cap ? (ctx->record_cap * 2) : 128;
        RL0Record *n = realloc(ctx->records, nc * sizeof(*ctx->records));
        if (!n) return 0;
        ctx->records = n;
        ctx->record_cap = nc;
    }

    RL0Record *rec = &ctx->records[ctx->record_count];
    rec->valence = valence;
    rec->tile_count = tile_count;
    rec->center = center;
    rec->poly = raw->poly;
    rec->boundary_str = poly_to_string_local(&raw->poly);
    if (!rec->boundary_str) return 0;
    rec->hidden_count = raw->hidden_count;

    for (int i = 0; i < tile_count; i++) {
        rec->tiles[i] = raw->tiles[i];
        rec->parities[i] = parities[i];
        rec->indices[i] = indices[i];
    }
    for (int i = 0; i < raw->hidden_count; i++) {
        rec->hidden[i] = raw->hidden[i];
    }

    ctx->record_count++;
    return 1;
}

static int record_cmp_local(const void *A, const void *B) {
    const RL0Record *a = A;
    const RL0Record *b = B;

    if (a->tile_count != b->tile_count) {
        return (a->tile_count < b->tile_count) ? -1 : 1;
    }

    if (a->hidden_count != b->hidden_count) {
        return (a->hidden_count < b->hidden_count) ? -1 : 1;
    }

    int pcmp = strcmp(a->boundary_str, b->boundary_str);
    if (pcmp != 0) return pcmp;

    if (a->valence != b->valence) {
        return (a->valence < b->valence) ? -1 : 1;
    }
    return 0;
}



typedef struct {
    int n;
    int p[RL0_MAX_TRACE];
    int idx[RL0_MAX_TRACE];
} RL0RemArc;

typedef struct {
    int level;
    int valence;
    RL0RemArc key;
    RL0RemArc *vals;
    size_t val_count;
    size_t val_cap;
} RL0RemRow;

typedef struct {
    RL0RemRow *rows;
    size_t count;
    size_t cap;
} RL0RemTable;

static int rem_item_cmp(int ap, int ai, int bp, int bi) {
    if (ap != bp) return (ap < bp) ? -1 : 1;
    if (ai != bi) return (ai < bi) ? -1 : 1;
    return 0;
}

static int rem_arc_cmp(const RL0RemArc *a, const RL0RemArc *b) {
    if (a->n != b->n) return (a->n < b->n) ? -1 : 1;
    for (int k = 0; k < a->n; k++) {
        int c = rem_item_cmp(a->p[k], a->idx[k], b->p[k], b->idx[k]);
        if (c) return c;
    }
    return 0;
}

static int rem_row_cmp(const void *A, const void *B) {
    const RL0RemRow *a = A;
    const RL0RemRow *b = B;
    if (a->level != b->level) return (a->level < b->level) ? -1 : 1;
    if (a->valence != b->valence) return (a->valence < b->valence) ? -1 : 1;
    return rem_arc_cmp(&a->key, &b->key);
}

static int rem_val_cmp(const void *A, const void *B) {
    const RL0RemArc *a = A;
    const RL0RemArc *b = B;
    return rem_arc_cmp(a, b);
}

static int rem_arc_equal(const RL0RemArc *a, const RL0RemArc *b) {
    return rem_arc_cmp(a, b) == 0;
}

static void rem_table_init(RL0RemTable *t) {
    memset(t, 0, sizeof(*t));
}

static void rem_table_free(RL0RemTable *t) {
    if (!t) return;
    for (size_t r = 0; r < t->count; r++) free(t->rows[r].vals);
    free(t->rows);
    memset(t, 0, sizeof(*t));
}

static RL0RemRow *rem_find_row(RL0RemTable *t, int level, int valence, const RL0RemArc *key) {
    for (size_t r = 0; r < t->count; r++) {
        if (t->rows[r].level == level &&
            t->rows[r].valence == valence &&
            rem_arc_equal(&t->rows[r].key, key)) {
            return &t->rows[r];
        }
    }
    return NULL;
}

static RL0RemRow *rem_add_row(RL0RemTable *t, int level, int valence, const RL0RemArc *key) {
    RL0RemRow *row = rem_find_row(t, level, valence, key);
    if (row) return row;
    if (t->count == t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 128;
        RL0RemRow *nr = realloc(t->rows, nc * sizeof(*nr));
        if (!nr) return NULL;
        t->rows = nr;
        t->cap = nc;
    }
    row = &t->rows[t->count++];
    memset(row, 0, sizeof(*row));
    row->level = level;
    row->valence = valence;
    row->key = *key;
    return row;
}

static int rem_row_add_val(RL0RemRow *row, const RL0RemArc *val) {
    if (row->val_count == row->val_cap) {
        size_t nc = row->val_cap ? row->val_cap * 2 : 8;
        RL0RemArc *nv = realloc(row->vals, nc * sizeof(*nv));
        if (!nv) return 0;
        row->vals = nv;
        row->val_cap = nc;
    }
    row->vals[row->val_count++] = *val;
    return 1;
}

static void rem_print_arc(FILE *fp, const RL0RemArc *a) {
    fprintf(fp, "[");
    for (int k = 0; k < a->n; k++) {
        if (k) fprintf(fp, ",");
        fprintf(fp, "[%d,%d]", a->p[k], a->idx[k]);
    }
    fprintf(fp, "]");
}

static void rem_record_variant(const RL0Record *rec, int reflected, int shift, RL0RemArc *out) {
    RL0RemArc base;
    int n = rec->tile_count;
    memset(&base, 0, sizeof(base));
    base.n = n;
    if (!reflected) {
        for (int k = 0; k < n; k++) {
            base.p[k] = rec->parities[k];
            base.idx[k] = rec->indices[k];
        }
    } else {
        for (int k = 0; k < n; k++) {
            int src = n - 1 - k;
            base.p[k] = -rec->parities[src];
            base.idx[k] = rec->indices[src];
        }
    }
    memset(out, 0, sizeof(*out));
    out->n = n;
    for (int k = 0; k < n; k++) {
        int src = (shift + k) % n;
        out->p[k] = base.p[src];
        out->idx[k] = base.idx[src];
    }
}

static void rem_split_variant(const RL0RemArc *full, int key_len, RL0RemArc *key, RL0RemArc *val) {
    memset(key, 0, sizeof(*key));
    memset(val, 0, sizeof(*val));
    key->n = key_len;
    val->n = full->n - key_len;
    for (int k = 0; k < key->n; k++) {
        key->p[k] = full->p[k];
        key->idx[k] = full->idx[k];
    }
    for (int k = 0; k < val->n; k++) {
        int src = key_len + k;
        val->p[k] = full->p[src];
        val->idx[k] = full->idx[src];
    }
}

static int rem_emit_split(RL0RemTable *tab, int level, int valence,
                          const RL0RemArc *key, const RL0RemArc *val) {
    RL0RemRow *row = rem_add_row(tab, level, valence, key);
    if (!row) return 0;
    if (val->n <= 0) return 1;
    return rem_row_add_val(row, val);
}

static int write_rememberance_file(const RL0Ctx *ctx, const char *path) {
    RL0RemTable tab;
    int ok = 1;
    rem_table_init(&tab);

    for (size_t r = 0; r < ctx->record_count && ok; r++) {
        const RL0Record *rec = &ctx->records[r];
        int n = rec->tile_count;
        for (int refl = 0; refl < 2 && ok; refl++) {
            for (int shift = 0; shift < n && ok; shift++) {
                RL0RemArc full;
                rem_record_variant(rec, refl, shift, &full);
                for (int k = 0; k <= n; k++) {
                    RL0RemArc key, val;
                    rem_split_variant(&full, k, &key, &val);
                    if (!rem_emit_split(&tab, k, rec->valence, &key, &val)) ok = 0;
                }
            }
        }
    }

    if (!ok) {
        rem_table_free(&tab);
        return 0;
    }

    qsort(tab.rows, tab.count, sizeof(*tab.rows), rem_row_cmp);
    for (size_t r = 0; r < tab.count; r++) {
        qsort(tab.rows[r].vals, tab.rows[r].val_count, sizeof(*tab.rows[r].vals), rem_val_cmp);
    }

    FILE *fp = fopen(path, "w");
    if (!fp) {
        rem_table_free(&tab);
        return 0;
    }
    fprintf(fp, "# RL0 rememberance dictionary.\n");
    fprintf(fp, "# Generated with data/rl0/completions.dat by rl0_generate.\n");
    fprintf(fp, "# Format: v=<valence> <key_arc> :\n");
    fprintf(fp, "# Values are listed one per indented line. Each value satisfies canonical(key + value) = valid vertex figure.\n");
    fprintf(fp, "# Full/maximal valid figures are present as rows with an empty value list.\n\n");

    int cur_level = -1;
    for (size_t r = 0; r < tab.count; r++) {
        const RL0RemRow *row = &tab.rows[r];
        if (row->level != cur_level) {
            if (cur_level >= 0) fprintf(fp, "\n");
            cur_level = row->level;
            fprintf(fp, "---[%d]---\n", cur_level);
        }
        fprintf(fp, "v=%d ", row->valence);
        rem_print_arc(fp, &row->key);
        fprintf(fp, " :\n");
        if (row->val_count == 0) {
            fprintf(fp, "  - []\n");
        } else {
            for (size_t j = 0; j < row->val_count; j++) {
                fprintf(fp, "  - ");
                rem_print_arc(fp, &row->vals[j]);
                fprintf(fp, "\n");
            }
        }
    }
    fclose(fp);
    rem_table_free(&tab);
    return 1;
}

static void write_records(RL0Ctx *ctx) {
    qsort(ctx->records,
          ctx->record_count,
          sizeof(*ctx->records),
          record_cmp_local);

    for (size_t i = 0; i < ctx->record_count; i++) {
        const RL0Record *rec = &ctx->records[i];
        fprintf(ctx->fp, "---[%zu]---\n", i + 1);
        fprintf(ctx->fp, "valence:%d\n", rec->valence);
        fprintf(ctx->fp, "tile_count:%d\n", rec->tile_count);
        fprintf(ctx->fp,
                "center:(%d,%d,%d)\n",
                rec->center.v,
                rec->center.x,
                rec->center.y);
        fprintf(ctx->fp, "boundary:");
        fputs(rec->boundary_str, ctx->fp);
        fprintf(ctx->fp, "\n");
        fprintf(ctx->fp, "constellation:");
        print_coord_list(ctx->fp, rec->hidden, rec->hidden_count);
        fprintf(ctx->fp, "\n");
        fprintf(ctx->fp, "tiles:");
        print_tile_list(ctx->fp, rec->tiles, rec->tile_count);
        fprintf(ctx->fp, "\n");
        fprintf(ctx->fp, "parities:");
        print_parities(ctx->fp, rec->parities, rec->tile_count);
        fprintf(ctx->fp, "\n");
        fprintf(ctx->fp, "indices:");
        print_indices(ctx->fp, rec->indices, rec->tile_count);
        fprintf(ctx->fp, "\n");
    }
}

static int emit_raw_completion(const VCompRawState *raw, RL0Ctx *ctx) {
    int indices[RL0_MAX_TRACE];
    int parities[RL0_MAX_TRACE];
    Cycle canon_tiles[RL0_MAX_TRACE];
    int total_tile_count = raw->tile_count;
    Coord center = raw->target;

    if (!poly_has_live_boundary(&raw->poly, ctx->tile)) return 1;

    for (int i = 0; i < total_tile_count; i++) {
        if (!canonicalize_tile(&ctx->tile->base,
                               &raw->tiles[i],
                               center,
                               ctx->lattice,
                               &canon_tiles[i],
                               &parities[i],
                               &indices[i])) {
            return 1;
        }
    }

    int valence = lattice_direction_count(ctx->lattice);
    if (ctx->lattice == TILE_LATTICE_TETRILLE &&
        (center.v == 3 || center.v == 4 || center.v == 6)) {
        valence = center.v;
    }
    if (seen_has(ctx, valence, total_tile_count, &raw->poly)) return 1;
    if (!seen_add(ctx, valence, total_tile_count, &raw->poly)) return 0;

    if (!reorder_center_items_ccw(canon_tiles,
                                  parities,
                                  indices,
                                  total_tile_count,
                                  center)) {
        return 1;
    }

    VCompRawState canon = *raw;
    for (int i = 0; i < total_tile_count; i++) canon.tiles[i] = canon_tiles[i];
    return records_add(ctx,
                       valence,
                       total_tile_count,
                       center,
                       &canon,
                       parities,
                       indices);
}

static int ensure_dir(const char *path) {
    if (mkdir(path, 0777) == 0) return 1;
    if (errno == EEXIST) return 1;
    return 0;
}

int main(int argc, char **argv) {
    const char *tile_path = "preferences/focus.tile";
    const char *output_path = "data/rl0/completions.dat";

    if (argc > 1) tile_path = argv[1];

    Tile tile;
    if (!tile_load(tile_path, &tile)) {
        fprintf(stderr, "failed to load tile: %s\n", tile_path);
        return 1;
    }

    if (!ensure_dir("data") || !ensure_dir("data/rl0")) {
        fprintf(stderr, "failed to create data/rl0\n");
        return 1;
    }

    FILE *fp = fopen(output_path, "w");
    if (!fp) {
        fprintf(stderr, "failed to open output: %s\n", output_path);
        return 1;
    }

    Poly seed;
    Coord verts[MAX_VERTS * MAX_CYCLES];
    int vc;

    memset(&seed, 0, sizeof(seed));
    seed.cycle_count = 1;
    seed.cycles[0] = tile.base;
    vc = build_boundary_vertices(&seed, verts);
    if (vc < 0) {
        fprintf(stderr, "failed to build boundary vertices\n");
        fclose(fp);
        return 1;
    }

    qsort(verts, (size_t)vc, sizeof(Coord), coord_cmp_local);

    RL0Ctx ctx;
    ctx.fp = fp;
    ctx.lattice = tile.lattice;
    ctx.tile = &tile;
    ctx.record_no = 0;
    ctx.seen = NULL;
    ctx.seen_count = 0;
    ctx.seen_cap = 0;
    ctx.records = NULL;
    ctx.record_count = 0;
    ctx.record_cap = 0;

    int ok = 1;
    for (int i = 0; i < vc && ok; i++) {
        VCompLevels raw;
        ctx.target = verts[i];
        vcomp_levels_init(&raw, VCOMP_MAX_LEVELS - 1);
        vcomp_enumerate_levels(&seed,
                               &tile,
                               verts[i],
                               NULL,
                               0,
                               NULL,
                               0,
                               NULL,
                               0,
                               VCOMP_MAX_LEVELS - 1,
                               1,
                               &raw);
        for (int level = 1; level <= raw.max_level; level++) {
            for (size_t r = 0; r < raw.levels[level].count; r++) {
                if (!emit_raw_completion(&raw.levels[level].data[r], &ctx)) {
                    ok = 0;
                    break;
                }
            }
            if (!ok) break;
        }
        vcomp_levels_destroy(&raw);
    }

    if (!ok) {
        fprintf(stderr, "allocation failure while generating RL0 records\n");
        fclose(fp);
        free(ctx.seen);
        for (size_t i = 0; i < ctx.record_count; i++) {
            free(ctx.records[i].boundary_str);
        }
        free(ctx.records);
        return 1;
    }

    write_records(&ctx);
    if (!write_rememberance_file(&ctx, "data/rl0/rememberance.dat")) {
        fprintf(stderr, "failed to write data/rl0/rememberance.dat\n");
        fclose(fp);
        free(ctx.seen);
        for (size_t i = 0; i < ctx.record_count; i++) {
            free(ctx.records[i].boundary_str);
        }
        free(ctx.records);
        return 1;
    }

    fclose(fp);
    free(ctx.seen);
    for (size_t i = 0; i < ctx.record_count; i++) {
        free(ctx.records[i].boundary_str);
    }
    free(ctx.records);
    return 0;
}
