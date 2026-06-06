#include "rl7/inflation7.h"

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define T(S,O) ((MR7Token){(S), mod6(O)})

static const int dirs[6][2] = {
    {-1, 0}, {0, -1}, {1, -1}, {1, 0}, {0, 1}, {-1, 1}
};

static int fail(char *err, size_t errsz, const char *fmt, ...) {
    if (err && errsz) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errsz, fmt, ap);
        va_end(ap);
    }
    return 0;
}

static void eval_word(const MR7Token *word, size_t n, int *q, int *r);

static uint8_t mod6(int x) {
    x %= 6;
    return (uint8_t)(x < 0 ? x + 6 : x);
}

void mr7_patch_init(MR7Patch *p) {
    if (p) memset(p, 0, sizeof(*p));
}

static void tile_free(MR7Tile *t) {
    if (!t) return;
    free(t->word);
    t->word = NULL;
    t->word_len = 0;
}

void mr7_patch_free(MR7Patch *p) {
    if (!p) return;
    for (size_t i = 0; i < p->n; i++) tile_free(&p->tile[i]);
    free(p->tile);
    memset(p, 0, sizeof(*p));
}

void mr7_cells_free(MR7Cells *c) {
    if (!c) return;
    free(c->cell);
    memset(c, 0, sizeof(*c));
}

void mr7_color_map_default(MR7ColorMap *map) {
    if (!map) return;
    for (int i = 0; i < 11; i++) map->d_child[i] = (uint8_t)i;
    for (int i = 0; i < 6; i++) {
        map->h_child[i] = (uint8_t)i;
        map->seed[i] = (uint8_t)i;
    }
}

const char *mr7_kind_name(MR7Kind k) {
    switch (k) {
        case MR7_D: return "D";
        case MR7_B: return "B";
        case MR7_G: return "G";
        case MR7_F: return "F";
    }
    return "?";
}

static int reserve_tiles(MR7Patch *p, size_t add, char *err, size_t errsz) {
    if (add > SIZE_MAX - p->n) return fail(err, errsz, "tile count overflow");
    size_t need = p->n + add;
    if (need <= p->cap) return 1;
    size_t cap = p->cap ? p->cap : 32;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) { cap = need; break; }
        cap *= 2;
    }
    MR7Tile *a = realloc(p->tile, cap * sizeof(*a));
    if (!a) return fail(err, errsz, "out of memory allocating %zu tiles", cap);
    p->tile = a;
    p->cap = cap;
    return 1;
}

static int append_tile_skin(MR7Patch *p, MR7Kind kind, uint8_t ori, uint8_t color,
                            uint8_t dark_cap,
                            const MR7Token *word, size_t word_len,
                            char *err, size_t errsz) {
    if (!reserve_tiles(p, 1, err, errsz)) return 0;
    MR7Token *copy = NULL;
    if (word_len) {
        copy = malloc(word_len * sizeof(*copy));
        if (!copy) return fail(err, errsz, "out of memory allocating tile word");
        memcpy(copy, word, word_len * sizeof(*copy));
    }
    MR7Tile *t = &p->tile[p->n++];
    t->kind = kind;
    t->ori = mod6(ori);
    t->color_index = color;
    t->dark_cap = dark_cap;
    t->word = copy;
    t->word_len = word_len;
    return 1;
}

static int append_tile(MR7Patch *p, MR7Kind kind, uint8_t ori, uint8_t color,
                       const MR7Token *word, size_t word_len,
                       char *err, size_t errsz) {
    return append_tile_skin(p, kind, ori, color, 0, word, word_len, err, errsz);
}

static size_t phi_token_len(char symbol) {
    return (symbol == 'D' || symbol == 'g' || symbol == 'h') ? 2 : 3;
}

static void phi_emit(MR7Token token, MR7Token *dst, size_t *at) {
    uint8_t i = mod6(token.ori);
    switch (token.symbol) {
        case 'D': dst[(*at)++] = T('D', i); dst[(*at)++] = T('K', i); break;
        case 'K': case 'H':
            dst[(*at)++] = T('D', i); dst[(*at)++] = T('K', i); dst[(*at)++] = T('H', i); break;
        case 'a':
            dst[(*at)++] = T('D', mod6(i - 2)); dst[(*at)++] = T('a', i); dst[(*at)++] = T('H', i); break;
        case 'b':
            dst[(*at)++] = T('D', mod6(i - 1)); dst[(*at)++] = T('b', i); dst[(*at)++] = T('H', i); break;
        case 'g': case 'h':
            dst[(*at)++] = (MR7Token){token.symbol, i}; dst[(*at)++] = T('H', i); break;
        default: break;
    }
}

static int root_word(const MR7Tile *t, MR7Token **word, size_t *len,
                     char *err, size_t errsz) {
    size_t n = 0;
    for (size_t i = 0; i < t->word_len; i++) {
        size_t plus = phi_token_len(t->word[i].symbol);
        if (plus > SIZE_MAX - n) return fail(err, errsz, "word expansion overflow");
        n += plus;
    }
    MR7Token *a = n ? malloc(n * sizeof(*a)) : NULL;
    if (n && !a) return fail(err, errsz, "out of memory expanding word");
    size_t at = 0;
    for (size_t i = 0; i < t->word_len; i++) phi_emit(t->word[i], a, &at);
    *word = a;
    *len = n;
    return 1;
}

static int append_child_skin(MR7Patch *out, const MR7Token *root, size_t root_len,
                             MR7Kind kind, uint8_t ori, uint8_t color,
                             uint8_t dark_cap,
                             const MR7Token *off, size_t off_len,
                             char *err, size_t errsz) {
    if (off_len > SIZE_MAX - root_len) return fail(err, errsz, "child word overflow");
    size_t n = root_len + off_len;
    MR7Token *word = n ? malloc(n * sizeof(*word)) : NULL;
    if (n && !word) return fail(err, errsz, "out of memory building child word");
    if (root_len) memcpy(word, root, root_len * sizeof(*word));
    if (off_len) memcpy(word + root_len, off, off_len * sizeof(*word));
    int ok = append_tile_skin(out, kind, ori, color, dark_cap, word, n, err, errsz);
    free(word);
    return ok;
}

static int append_child(MR7Patch *out, const MR7Token *root, size_t root_len,
                        MR7Kind kind, uint8_t ori, uint8_t color,
                        const MR7Token *off, size_t off_len,
                        char *err, size_t errsz) {
    return append_child_skin(out, root, root_len, kind, ori, color, 0,
                             off, off_len, err, errsz);
}

static int tile_key_cmp(const void *aa, const void *bb) {
    const MR7Tile *a = aa, *b = bb;
    if (a->kind != b->kind) return (int)a->kind - (int)b->kind;
    if (a->ori != b->ori) return (int)a->ori - (int)b->ori;
    size_t n = a->word_len < b->word_len ? a->word_len : b->word_len;
    for (size_t i = 0; i < n; i++) {
        if (a->word[i].symbol != b->word[i].symbol) return (unsigned char)a->word[i].symbol - (unsigned char)b->word[i].symbol;
        if (a->word[i].ori != b->word[i].ori) return (int)a->word[i].ori - (int)b->word[i].ori;
    }
    return (a->word_len > b->word_len) - (a->word_len < b->word_len);
}

static int sort_unique(MR7Patch *p, char *err, size_t errsz) {
    if (p->n < 2) return 1;
    qsort(p->tile, p->n, sizeof(*p->tile), tile_key_cmp);
    size_t keep = 1;
    for (size_t i = 1; i < p->n; i++) {
        MR7Tile *prev = &p->tile[keep - 1];
        MR7Tile *cur = &p->tile[i];
        if (tile_key_cmp(prev, cur) == 0) {
            if (prev->color_index != cur->color_index) {
                return fail(err, errsz, "auxiliary colour conflict on duplicate tile");
            }
            if (prev->dark_cap != cur->dark_cap) {
                return fail(err, errsz, "tree-skin cap conflict on duplicate tile");
            }
            tile_free(cur);
        } else {
            if (keep != i) p->tile[keep] = p->tile[i];
            keep++;
        }
    }
    p->n = keep;
    return 1;
}

int mr7_seed_false_center(MR7Patch *out, char *err, size_t errsz) {
    mr7_patch_init(out);
    return append_tile(out, MR7_F, 0, 0, NULL, 0, err, errsz);
}

int mr7_seed_brown_g_ring(MR7Patch *out, const MR7ColorMap *map,
                          char *err, size_t errsz) {
    mr7_patch_init(out);
    for (uint8_t i = 0; i < 6; i++) {
        MR7Token w = T('g', i);
        if (!append_tile(out, MR7_B, i, map->seed[i], &w, 1, err, errsz)) {
            mr7_patch_free(out);
            return 0;
        }
    }
    return 1;
}

int mr7_seed_single_d(MR7Patch *out, char *err, size_t errsz) {
    mr7_patch_init(out);
    return append_tile(out, MR7_D, 0, 0, NULL, 0, err, errsz);
}

int mr7_seed_single_h(MR7Patch *out, char *err, size_t errsz) {
    mr7_patch_init(out);
    return append_tile(out, MR7_G, 0, 0, NULL, 0, err, errsz);
}

int mr7_seed_dh(MR7Patch *out, char *err, size_t errsz) {
    /* Fixed-point seed D|H: one dimer followed by one H in a straight
     * three-cell row.  D0 occupies (-1,0),(0,0); B0@H3 occupies (1,0).
     * The bar denotes the H-step relation, not an intervening second dimer. */
    MR7Token hword = T('H', 3);
    mr7_patch_init(out);
    if (!append_tile(out, MR7_D, 0, 0, NULL, 0, err, errsz) ||
        !append_tile(out, MR7_B, 0, 0, &hword, 1, err, errsz)) {
        mr7_patch_free(out);
        return 0;
    }
    return 1;
}

static int mr7_seed_c3_bbb(MR7Patch *out, char *err, size_t errsz) {
    MR7Token b0[] = { T('D', 1), T('K', 3) };
    MR7Token b2[] = { T('D', 2), T('K', 4) };
    mr7_patch_init(out);
    if (!append_tile(out, MR7_B, 4, 0, NULL, 0, err, errsz) ||
        !append_tile(out, MR7_B, 0, 0, b0, sizeof(b0) / sizeof(b0[0]), err, errsz) ||
        !append_tile(out, MR7_B, 2, 0, b2, sizeof(b2) / sizeof(b2[0]), err, errsz)) {
        mr7_patch_free(out);
        return 0;
    }
    return 1;
}

static int mr7_seed_c3_ggg(MR7Patch *out, char *err, size_t errsz) {
    MR7Token g5[] = { T('D', 0), T('K', 2), T('K', 3) };
    MR7Token g1[] = { T('D', 1), T('K', 3), T('K', 4) };
    mr7_patch_init(out);
    if (!append_tile(out, MR7_G, 3, 0, NULL, 0, err, errsz) ||
        !append_tile(out, MR7_G, 5, 0, g5, sizeof(g5) / sizeof(g5[0]), err, errsz) ||
        !append_tile(out, MR7_G, 1, 0, g1, sizeof(g1) / sizeof(g1[0]), err, errsz)) {
        mr7_patch_free(out);
        return 0;
    }
    return 1;
}

int mr7_inflate(const MR7Patch *in, MR7Patch *out, const MR7ColorMap *map,
                char *err, size_t errsz) {
    mr7_patch_init(out);
    for (size_t ti = 0; ti < in->n; ti++) {
        const MR7Tile *t = &in->tile[ti];
        uint8_t i = mod6(t->ori);
        MR7Token *root = NULL;
        size_t nroot = 0;
        if (!root_word(t, &root, &nroot, err, errsz)) goto bad;
#define CHILD(K,O,C,...) do { MR7Token x[] = {__VA_ARGS__}; if (!append_child(out, root, nroot, (K), mod6(O), (C), x, sizeof(x)/sizeof(x[0]), err, errsz)) { free(root); goto bad; } } while (0)
#define CHILD0(K,O,C) do { if (!append_child(out, root, nroot, (K), mod6(O), (C), NULL, 0, err, errsz)) { free(root); goto bad; } } while (0)
        if (t->kind == MR7_F) {
            CHILD0(MR7_F, 0, 0);
            for (uint8_t j = 0; j < 6; j++) {
                MR7Token g = T('g', j);
                if (!append_child(out, root, nroot, MR7_B, j, map->seed[j],
                                  &g, 1, err, errsz)) { free(root); goto bad; }
            }
        } else if (t->kind == MR7_D) {
            CHILD0(MR7_D, i, map->d_child[0]);
            CHILD(MR7_D, i, map->d_child[1], T('D', i), T('K', i));
            CHILD(MR7_B, i, map->d_child[2], T('D', i), T('K', i), T('D', i), T('K', i));
            CHILD(MR7_G, i+2, map->d_child[3], T('D', i), T('a', mod6(i+2)));
            CHILD(MR7_G, i+1, map->d_child[4], T('D', i), T('b', mod6(i+1)));
            CHILD(MR7_B, i+2, map->d_child[5], T('D', i), T('K', i), T('D', i), T('a', mod6(i+2)));
            CHILD(MR7_B, i+1, map->d_child[6], T('D', i), T('K', i), T('D', i), T('b', mod6(i+1)));
            CHILD(MR7_B, i-1, map->d_child[7], T('g', mod6(i-1)));
            CHILD(MR7_B, i-2, map->d_child[8], T('h', mod6(i-2)));
            CHILD(MR7_G, i-1, map->d_child[9], T('D', i), T('K', i), T('g', mod6(i-1)));
            CHILD(MR7_G, i-2, map->d_child[10], T('D', i), T('K', i), T('h', mod6(i-2)));
        } else {
            uint8_t axis_cap = (uint8_t)(t->kind == MR7_G || t->dark_cap);
            CHILD0(MR7_D, i, map->h_child[0]);
            { MR7Token x[] = { T('D', i), T('K', i) };
              if (!append_child_skin(out, root, nroot, MR7_B, mod6(i),
                                     map->h_child[1], axis_cap, x, 2, err, errsz)) {
                  free(root); goto bad;
              }
            }
            CHILD(MR7_G, i+2, map->h_child[2], T('D', i), T('a', mod6(i+2)));
            CHILD(MR7_G, i+1, map->h_child[3], T('D', i), T('b', mod6(i+1)));
            CHILD(MR7_G, i-1, map->h_child[4], T('g', mod6(i-1)));
            CHILD(MR7_G, i-2, map->h_child[5], T('h', mod6(i-2)));
        }
#undef CHILD
#undef CHILD0
        free(root);
    }
    return sort_unique(out, err, errsz);
bad:
    mr7_patch_free(out);
    return 0;
}

static int append_patch_copy(MR7Patch *dst, const MR7Patch *src,
                             char *err, size_t errsz) {
    for (size_t i = 0; i < src->n; i++) {
        const MR7Tile *t = &src->tile[i];
        if (!append_tile_skin(dst, t->kind, t->ori, t->color_index, t->dark_cap,
                              t->word, t->word_len, err, errsz)) return 0;
    }
    return 1;
}

static int same_token(MR7Token a, MR7Token b) {
    return a.symbol == b.symbol && a.ori == b.ori;
}

static void append_token_text(char *dst, size_t cap, MR7Token token) {
    size_t used = strlen(dst);
    if (used + 5 >= cap) return;
    (void)snprintf(dst + used, cap - used, "%c%u", token.symbol,
                   (unsigned)token.ori);
}

static void append_word_text(char *dst, size_t cap, const MR7Tile *t) {
    if (!t->word_len) {
        if (strlen(dst) + 2 < cap) strcat(dst, "e");
        return;
    }
    for (size_t i = 0; i < t->word_len; i++) append_token_text(dst, cap, t->word[i]);
}

static void append_tile_text(char *dst, size_t cap, const MR7Tile *t) {
    size_t used = strlen(dst);
    if (used + 5 >= cap) return;
    (void)snprintf(dst + used, cap - used, "%c%u@", t->kind == MR7_B ? 'B' : 'G',
                   (unsigned)t->ori);
    append_word_text(dst, cap, t);
}

static int h_index_at(const MR7Patch *p, int q, int r) {
    for (size_t i = 0; i < p->n; i++) {
        if (p->tile[i].kind == MR7_D) continue;
        int tq, tr;
        eval_word(p->tile[i].word, p->tile[i].word_len, &tq, &tr);
        if (tq == q && tr == r) return (int)i;
    }
    return -1;
}

static int c3_colour_matches(const MR7Patch *p, const int ix[3], const char *want) {
    char got[4];
    for (int i = 0; i < 3; i++) got[i] = p->tile[ix[i]].kind == MR7_B ? 'B' : 'G';
    got[3] = '\0';
    for (int i = 0; i < 3; i++) for (int j = i + 1; j < 3; j++)
        if (got[j] < got[i]) { char c = got[i]; got[i] = got[j]; got[j] = c; }
    return strcmp(got, want) == 0;
}

typedef struct {
    int ix[3];
    size_t removed;
    size_t residual_total;
} C3Choice;

static int c3_reduce_choice(const MR7Patch *source, const int ix[3], MR7Patch *reduced,
                            char *raw, size_t rawsz, char *common, size_t commonsz,
                            char *normal, size_t normalsz, size_t *removed_out,
                            size_t *residual_out, char *err, size_t errsz) {
    unsigned char *remove[3] = {NULL, NULL, NULL};
    if (raw && rawsz) raw[0] = '\0';
    if (common && commonsz) common[0] = '\0';
    if (normal && normalsz) normal[0] = '\0';
    for (int k = 0; k < 3; k++) {
        remove[k] = calloc(source->tile[ix[k]].word_len, 1);
        if (!remove[k] && source->tile[ix[k]].word_len) {
            for (int j = 0; j < k; j++) free(remove[j]);
            return fail(err, errsz, "out of memory normalizing C3 axiom");
        }
        if (raw && rawsz) {
            if (k) strncat(raw, ",", rawsz - strlen(raw) - 1);
            append_tile_text(raw, rawsz, &source->tile[ix[k]]);
        }
    }
    size_t removed = 0;
    for (size_t a = 0; a < source->tile[ix[0]].word_len; a++) {
        if (remove[0][a]) continue;
        int hit1 = -1, hit2 = -1;
        for (size_t b = 0; b < source->tile[ix[1]].word_len; b++)
            if (!remove[1][b] && same_token(source->tile[ix[0]].word[a], source->tile[ix[1]].word[b])) { hit1 = (int)b; break; }
        if (hit1 < 0) continue;
        for (size_t c = 0; c < source->tile[ix[2]].word_len; c++)
            if (!remove[2][c] && same_token(source->tile[ix[0]].word[a], source->tile[ix[2]].word[c])) { hit2 = (int)c; break; }
        if (hit2 < 0) continue;
        remove[0][a] = 1; remove[1][hit1] = 1; remove[2][hit2] = 1;
        if (common && commonsz) append_token_text(common, commonsz, source->tile[ix[0]].word[a]);
        removed++;
    }
    MR7Patch tmp;
    mr7_patch_init(&tmp);
    size_t residual = 0;
    for (int k = 0; k < 3; k++) {
        const MR7Tile *t = &source->tile[ix[k]];
        size_t n = 0;
        for (size_t i = 0; i < t->word_len; i++) if (!remove[k][i]) n++;
        MR7Token *word = n ? malloc(n * sizeof(*word)) : NULL;
        if (n && !word) {
            for (int j = 0; j < 3; j++) free(remove[j]);
            mr7_patch_free(&tmp);
            return fail(err, errsz, "out of memory writing normalized C3 axiom");
        }
        size_t at = 0;
        for (size_t i = 0; i < t->word_len; i++) if (!remove[k][i]) word[at++] = t->word[i];
        if (!append_tile_skin(&tmp, t->kind, t->ori, t->color_index, t->dark_cap, word, n, err, errsz)) {
            free(word);
            for (int j = 0; j < 3; j++) free(remove[j]);
            mr7_patch_free(&tmp);
            return 0;
        }
        free(word);
        residual += n;
        if (normal && normalsz) {
            if (k) strncat(normal, ",", normalsz - strlen(normal) - 1);
            append_tile_text(normal, normalsz, &tmp.tile[tmp.n - 1]);
        }
    }
    for (int k = 0; k < 3; k++) free(remove[k]);
    if (removed_out) *removed_out = removed;
    if (residual_out) *residual_out = residual;
    if (reduced) *reduced = tmp; else mr7_patch_free(&tmp);
    return 1;
}

static int c3_first_best_at_generation(const MR7Patch *p, const char *colour_class,
                                       size_t *count, C3Choice *best,
                                       char *err, size_t errsz) {
    const int pat[2][2][2] = {{{1,0},{0,1}}, {{1,-1},{1,0}}};
    *count = 0;
    best->ix[0] = best->ix[1] = best->ix[2] = -1;
    best->removed = 0;
    best->residual_total = SIZE_MAX;
    for (size_t a = 0; a < p->n; a++) {
        if (p->tile[a].kind == MR7_D) continue;
        int q, r; eval_word(p->tile[a].word, p->tile[a].word_len, &q, &r);
        for (int t = 0; t < 2; t++) {
            int ix[3] = {(int)a, h_index_at(p, q + pat[t][0][0], r + pat[t][0][1]),
                                  h_index_at(p, q + pat[t][1][0], r + pat[t][1][1])};
            if (ix[1] < 0 || ix[2] < 0 || !c3_colour_matches(p, ix, colour_class)) continue;
            (*count)++;
            size_t removed = 0, residual = 0;
            if (!c3_reduce_choice(p, ix, NULL, NULL, 0, NULL, 0, NULL, 0,
                                  &removed, &residual, err, errsz)) return 0;
            if (residual < best->residual_total ||
                (residual == best->residual_total && removed > best->removed)) {
                for (int k = 0; k < 3; k++) best->ix[k] = ix[k];
                best->removed = removed;
                best->residual_total = residual;
            }
        }
    }
    return 1;
}

int mr7_extract_c3_seed(const char *colour_class, MR7Patch *out,
                        char *record, size_t recordsz,
                        char *err, size_t errsz) {
    /* Accepted short witnesses from the indexed-C3 recovery pass.  The
     * legacy monochromatic scan below remains diagnostic only. */
    if (!strcmp(colour_class, "BBB")) {
        if (record && recordsz)
            snprintf(record, recordsz, "B3: B4@e, B0@D1K3, B2@D2K4; C3 shift +2");
        return mr7_seed_c3_bbb(out, err, errsz);
    }
    if (!strcmp(colour_class, "GGG")) {
        if (record && recordsz)
            snprintf(record, recordsz, "L3: G3@e, G5@D0K2K3, G1@D1K3K4; C3 shift +2");
        return mr7_seed_c3_ggg(out, err, errsz);
    }
    return fail(err, errsz, "supported C3 classes are BBB and GGG");
}

int mr7_write_c3_reduction_audit(FILE *fp, unsigned max_generation,
                                 char *err, size_t errsz) {
    const char *classes[2] = {"BBB", "GGG"};
    MR7ColorMap map; mr7_color_map_default(&map);
    fprintf(fp, "# Mountain and Range legacy monochromatic-triple scan\n");
    fprintf(fp, "# WARNING: this diagnostic does not enforce indexed C3 shift symmetry.\n");
    fprintf(fp, "# It must not be used as an axiom source; accepted C3 records are in axioms.dat.\n");
    fprintf(fp, "# Search source: successive inflation of the single-D init.\n");
    fprintf(fp, "# Applied reduction: exact common token multiset subtraction only.\n");
    fprintf(fp, "# Not applied: scale-dependent symbolic syzygies.\n\n");
    for (int ci = 0; ci < 2; ci++) {
        MR7Patch cur;
        if (!mr7_seed_single_d(&cur, err, errsz)) return 0;
        fprintf(fp, "[%s]\n", classes[ci]);
        char previous[4096] = "";
        for (unsigned gen = 1; gen <= max_generation; gen++) {
            MR7Patch next;
            if (!mr7_inflate(&cur, &next, &map, err, errsz)) { mr7_patch_free(&cur); return 0; }
            mr7_patch_free(&cur); cur = next;
            size_t count = 0; C3Choice best;
            if (!c3_first_best_at_generation(&cur, classes[ci], &count, &best, err, errsz)) { mr7_patch_free(&cur); return 0; }
            if (!count) { fprintf(fp, "generation %u: occurrences=0\n", gen); continue; }
            char raw[4096], common[4096], normal[4096];
            size_t removed = 0, residual = 0;
            if (!c3_reduce_choice(&cur, best.ix, NULL, raw, sizeof(raw), common, sizeof(common), normal, sizeof(normal), &removed, &residual, err, errsz)) { mr7_patch_free(&cur); return 0; }
            fprintf(fp, "generation %u: occurrences=%zu best_removed=%zu residual_tokens=%zu repeated_previous_best=%s\n",
                    gen, count, removed, residual, previous[0] && !strcmp(previous, normal) ? "yes" : "no");
            fprintf(fp, "  raw        = [%s]\n", raw);
            fprintf(fp, "  common     = [%s]\n", common[0] ? common : "e");
            fprintf(fp, "  normalized = [%s]\n", normal);
            fprintf(fp, "  exact_common_remaining = 0\n");
            snprintf(previous, sizeof(previous), "%s", normal);
        }
        fprintf(fp, "\n");
        mr7_patch_free(&cur);
    }
    return 1;
}

const char *mr7_init_name(MR7Init init) {
    switch (init) {
        case MR7_INIT_D: return "D";
        case MR7_INIT_H: return "H";
        case MR7_INIT_DH: return "DH";
        case MR7_INIT_B3: return "B3";
        case MR7_INIT_L3: return "L3";
        case MR7_INIT_F: return "F";
        case MR7_INIT_COUNT: break;
    }
    return "?";
}

int mr7_parse_init(const char *s, MR7Init *out) {
    if (!strcmp(s, "D")) *out = MR7_INIT_D;
    else if (!strcmp(s, "H")) *out = MR7_INIT_H;
    else if (!strcmp(s, "DH")) *out = MR7_INIT_DH;
    else if (!strcmp(s, "B3")) *out = MR7_INIT_B3;
    else if (!strcmp(s, "L3")) *out = MR7_INIT_L3;
    else if (!strcmp(s, "F")) *out = MR7_INIT_F;
    else return 0;
    return 1;
}

int mr7_make_init(MR7Init init, MR7Patch *out, const MR7ColorMap *map,
                  char *record, size_t recordsz,
                  char *err, size_t errsz) {
    (void)map; /* Reserved for future init/palette annotations. */
    if (record && recordsz) record[0] = '\0';
    switch (init) {
        case MR7_INIT_D:
            if (record && recordsz) snprintf(record, recordsz, "D@e");
            return mr7_seed_single_d(out, err, errsz);
        case MR7_INIT_H:
            if (record && recordsz) snprintf(record, recordsz, "H@e (unclassified H stored as G for display)");
            return mr7_seed_single_h(out, err, errsz);
        case MR7_INIT_DH:
            if (record && recordsz) snprintf(record, recordsz, "DH: D0@e | B0@H3; straight cells (-1,0),(0,0),(1,0)");
            return mr7_seed_dh(out, err, errsz);
        case MR7_INIT_B3:
            return mr7_extract_c3_seed("BBB", out, record, recordsz, err, errsz);
        case MR7_INIT_L3:
            return mr7_extract_c3_seed("GGG", out, record, recordsz, err, errsz);
        case MR7_INIT_F:
            if (record && recordsz) snprintf(record, recordsz, "F: F@e; F -> F + {B_i@g_i : i=0..5}");
            return mr7_seed_false_center(out, err, errsz);
        case MR7_INIT_COUNT:
            break;
    }
    return fail(err, errsz, "unknown init");
}

int mr7_expand_init(MR7Init init, unsigned level, MR7Patch *out,
                    const MR7ColorMap *map, char *record, size_t recordsz,
                    char *err, size_t errsz) {
    MR7Patch cur, next;
    if (!mr7_make_init(init, &cur, map, record, recordsz, err, errsz)) return 0;
    for (unsigned n = 0; n < level; n++) {
        if (!mr7_inflate(&cur, &next, map, err, errsz)) { mr7_patch_free(&cur); return 0; }
        mr7_patch_free(&cur); cur = next;
    }
    *out = cur;
    return 1;
}

int mr7_expand_persistent_ring(unsigned level, MR7Patch *out,
                               const MR7ColorMap *map,
                               char *err, size_t errsz) {
    MR7Patch seed, cur;
    if (!mr7_seed_brown_g_ring(&seed, map, err, errsz)) return 0;
    mr7_patch_init(&cur);
    if (!append_patch_copy(&cur, &seed, err, errsz)) goto bad;
    for (unsigned n = 0; n < level; n++) {
        MR7Patch next;
        if (!mr7_inflate(&cur, &next, map, err, errsz)) goto bad;
        if (!append_patch_copy(&next, &seed, err, errsz) || !sort_unique(&next, err, errsz)) {
            mr7_patch_free(&next);
            goto bad;
        }
        mr7_patch_free(&cur);
        cur = next;
    }
    mr7_patch_free(&seed);
    *out = cur;
    return 1;
bad:
    mr7_patch_free(&seed);
    mr7_patch_free(&cur);
    return 0;
}

static void eval_word(const MR7Token *word, size_t n, int *q, int *r) {
    *q = *r = 0;
    for (size_t i = 0; i < n; i++) {
        int d = mod6(word[i].ori);
        *q += dirs[d][0];
        *r += dirs[d][1];
    }
}

static int cell_cmp(const void *aa, const void *bb) {
    const MR7Cell *a = aa, *b = bb;
    if (a->r != b->r) return a->r - b->r;
    if (a->q != b->q) return a->q - b->q;
    return (unsigned char)a->state - (unsigned char)b->state;
}

int mr7_tiles_to_cells(const MR7Patch *patch, MR7Cells *out,
                       char *err, size_t errsz) {
    memset(out, 0, sizeof(*out));
    size_t max = 0;
    for (size_t i = 0; i < patch->n; i++) max += patch->tile[i].kind == MR7_D ? 2 : 1;
    MR7Cell *a = max ? malloc(max * sizeof(*a)) : NULL;
    if (max && !a) return fail(err, errsz, "out of memory allocating cells");
    size_t n = 0;
    for (size_t i = 0; i < patch->n; i++) {
        const MR7Tile *t = &patch->tile[i];
        int q, r;
        eval_word(t->word, t->word_len, &q, &r);
        char state = t->kind == MR7_D ? '0' : (t->kind == MR7_B && t->dark_cap ? 'C' : mr7_kind_name(t->kind)[0]);
        a[n++] = (MR7Cell){q, r, state, t->ori, t->color_index, t->dark_cap, 'N'};
        if (t->kind == MR7_D) a[n++] = (MR7Cell){q + dirs[t->ori][0], r + dirs[t->ori][1], '1', t->ori, t->color_index, 0, 'N'};
    }
    qsort(a, n, sizeof(*a), cell_cmp);
    for (size_t i = 1; i < n; i++) {
        if (a[i-1].q == a[i].q && a[i-1].r == a[i].r) {
            if (a[i-1].state != a[i].state || a[i-1].ori != a[i].ori) {
                int q = a[i].q, r = a[i].r;
                free(a);
                return fail(err, errsz, "cell overlap conflict at (%d,%d)", q, r);
            }
        }
    }
    out->cell = a;
    out->n = n;
    return 1;
}

int mr7_check_reference_counts(FILE *fp, const MR7ColorMap *map) {
    static const size_t expect[6] = {6, 48, 336, 2310, 15840, 108576};
    char err[160];
    for (unsigned level = 0; level < 6; level++) {
        MR7Patch p; MR7Cells c;
        if (!mr7_expand_persistent_ring(level, &p, map, err, sizeof(err)) ||
            !mr7_tiles_to_cells(&p, &c, err, sizeof(err))) {
            if (fp) fprintf(fp, "level %u: error: %s\n", level, err);
            mr7_patch_free(&p);
            return 0;
        }
        int ok = c.n == expect[level];
        if (fp) fprintf(fp, "level %u: %zu cells %s expected %zu\n", level, c.n, ok ? "=" : "!=", expect[level]);
        mr7_cells_free(&c); mr7_patch_free(&p);
        if (!ok) return 0;
    }
    return 1;
}
