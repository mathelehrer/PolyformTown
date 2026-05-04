#include "core/tile.h"
#include "core/lattice.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static int starts_with_label(const char *line, const char *label) {
    while (*line == ' ' || *line == '\t') line++;
    return strncmp(line, label, strlen(label)) == 0;
}

static void trim_in_place(char *s) {
    size_t n;
    while (*s && isspace((unsigned char)*s)) memmove(s, s + 1, strlen(s));
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

static void copy_trimmed(char *dst, size_t dstsz, const char *src) {
    size_t n = strcspn(src, "\r\n");
    if (n >= dstsz) n = dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    trim_in_place(dst);
}

static int parse_basis_row(const char *line, char *out1, char *out2) {
    char buf[256];
    copy_trimmed(buf, sizeof(buf), line);
    if (!buf[0]) return 0;

    char *split = NULL;
    size_t len = strlen(buf);
    for (size_t i = 1; i < len; i++) {
        if (isspace((unsigned char)buf[i - 1]) && isspace((unsigned char)buf[i])) {
            split = &buf[i];
            break;
        }
    }
    if (!split) {
        for (size_t i = 1; i < len; i++) {
            if (isspace((unsigned char)buf[i])) {
                split = &buf[i];
                break;
            }
        }
    }
    if (!split) return 0;

    *split = '\0';
    split++;
    while (*split && isspace((unsigned char)*split)) split++;
    trim_in_place(buf);
    trim_in_place(split);
    if (!buf[0] || !split[0]) return 0;

    copy_trimmed(out1, MAX_TILE_EXPR, buf);
    copy_trimmed(out2, MAX_TILE_EXPR, split);
    return 1;
}


static void print_imgtable_constants(const Tile *tile) {
    putchar('(');
    for (int i = 0; i < tile->constant_count; i++) {
        if (i) putchar(',');
        printf("%s=%s", tile->constants[i].name, tile->constants[i].expr);
    }
    putchar(')');
}

static void print_imgtable_basis(const Tile *tile) {
    putchar('(');
    for (int i = 0; i < tile->basis_count; i++) {
        const TileBasis *b = &tile->bases[i];
        if (i) putchar(',');
        printf("%d:%s,%s;%s,%s", b->valence, b->a11, b->a12, b->a21, b->a22);
    }
    putchar(')');
}

static void print_imgtable_cycle_raw(const Cycle *c) {
    putchar('(');
    for (int i = 0; i < c->n; i++) {
        if (i) putchar(',');
        printf("%d %d %d", c->v[i].v, c->v[i].x, c->v[i].y);
    }
    putchar(')');
}

void tile_print_imgtable_shape(const Tile *tile, const Poly *poly) {
    int hole_flag = (poly->cycle_count > 1) ? 1 : 0;
    printf("[ %d | ", hole_flag);
    print_imgtable_constants(tile);
    printf(" | ");
    print_imgtable_basis(tile);
    for (int i = 0; i < poly->cycle_count; i++) {
        printf(" | ");
        print_imgtable_cycle_raw(&poly->cycles[i]);
    }
    printf(" ]\n");
}

void tile_print_imgtable_cycle(const Tile *tile, const Cycle *cycle) {
    Poly poly;
    poly.cycle_count = 1;
    poly.cycles[0] = *cycle;
    tile_print_imgtable_shape(tile, &poly);
}

int tile_load(const char *path, Tile *tile) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    memset(tile, 0, sizeof(*tile));
    tile->lattice = TILE_LATTICE_SQUARE;
    strncpy(tile->name, path, sizeof(tile->name) - 1);

    enum {
        SEC_NONE,
        SEC_CONSTANTS,
        SEC_BASIS,
        SEC_CYCLE
    } sec = SEC_NONE;

    char line[256];
    int pending_basis_valence = -1;
    int pending_basis_row = 0;

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0' || *p == '\n' || *p == '\r') continue;

        if (starts_with_label(p, "name:")) {
            p += 5;
            while (*p == ' ' || *p == '\t') p++;
            copy_trimmed(tile->name, sizeof(tile->name), p);
            sec = SEC_NONE;
            continue;
        }
        if (starts_with_label(p, "lattice:")) {
            p += 8;
            while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "triangular", 10) == 0) tile->lattice = TILE_LATTICE_TRIANGULAR;
            else if (strncmp(p, "tetrille", 8) == 0) tile->lattice = TILE_LATTICE_TETRILLE;
            else tile->lattice = TILE_LATTICE_SQUARE;
            sec = SEC_NONE;
            continue;
        }
        if (starts_with_label(p, "constants:")) {
            sec = SEC_CONSTANTS;
            continue;
        }
        if (starts_with_label(p, "basis:")) {
            sec = SEC_BASIS;
            pending_basis_valence = -1;
            pending_basis_row = 0;
            continue;
        }
        if (starts_with_label(p, "cycle:")) {
            sec = SEC_CYCLE;
            continue;
        }

        if (sec == SEC_CONSTANTS) {
            char *eq = strchr(p, '=');
            if (!eq || tile->constant_count >= MAX_TILE_CONSTANTS) continue;
            *eq = '\0';
            copy_trimmed(tile->constants[tile->constant_count].name,
                         sizeof(tile->constants[tile->constant_count].name), p);
            copy_trimmed(tile->constants[tile->constant_count].expr,
                         sizeof(tile->constants[tile->constant_count].expr), eq + 1);
            if (tile->constants[tile->constant_count].name[0] && tile->constants[tile->constant_count].expr[0]) {
                tile->constant_count++;
            }
            continue;
        }

        if (sec == SEC_BASIS) {
            int valence = 0;
            char *colon = strchr(p, ':');
            if (colon) {
                char lhs[64];
                copy_trimmed(lhs, sizeof(lhs), p);
                char *lhs_colon = strchr(lhs, ':');
                if (lhs_colon) *lhs_colon = '\0';
                trim_in_place(lhs);
                if (sscanf(lhs, "%d", &valence) == 1) {
                    if (tile->basis_count >= MAX_TILE_BASES) continue;
                    pending_basis_valence = valence;
                    pending_basis_row = 0;
                    tile->bases[tile->basis_count].valence = valence;
                    tile->bases[tile->basis_count].a11[0] = '\0';
                    tile->bases[tile->basis_count].a12[0] = '\0';
                    tile->bases[tile->basis_count].a21[0] = '\0';
                    tile->bases[tile->basis_count].a22[0] = '\0';
                    continue;
                }
            }
            if (pending_basis_valence >= 0 && tile->basis_count < MAX_TILE_BASES) {
                char e1[MAX_TILE_EXPR], e2[MAX_TILE_EXPR];
                if (parse_basis_row(p, e1, e2)) {
                    TileBasis *b = &tile->bases[tile->basis_count];
                    if (pending_basis_row == 0) {
                        copy_trimmed(b->a11, sizeof(b->a11), e1);
                        copy_trimmed(b->a12, sizeof(b->a12), e2);
                        pending_basis_row = 1;
                    } else if (pending_basis_row == 1) {
                        copy_trimmed(b->a21, sizeof(b->a21), e1);
                        copy_trimmed(b->a22, sizeof(b->a22), e2);
                        pending_basis_row = 0;
                        pending_basis_valence = -1;
                        tile->basis_count++;
                    }
                }
            }
            continue;
        }

        if (sec != SEC_CYCLE) continue;

        int v, x, y;
        if (sscanf(p, "%d %d %d", &v, &x, &y) == 3) {
            if (tile->base.n >= MAX_VERTS) {
                fclose(fp);
                return 0;
            }
            tile->base.v[tile->base.n++] = (Coord){v, x, y};
            continue;
        }

        if (sscanf(p, "%d %d", &x, &y) == 2) {
            if (tile->base.n >= MAX_VERTS) {
                fclose(fp);
                return 0;
            }
            tile->base.v[tile->base.n++] = (Coord){0, x, y};
            continue;
        }
    }
    fclose(fp);
    if (tile->base.n < 3) return 0;
    if (cycle_signed_area2(&tile->base, tile->lattice) < 0) cycle_reverse(&tile->base);
    tile_build_variants(tile);
    return 1;
}

void tile_build_variants(Tile *tile) {
    tile->variant_count = 0;

    int count = lattice_transform_count(tile->lattice);
    for (int t = 0; t < count; t++) {
        Cycle cur;
        cycle_transform_lattice(&tile->base, &cur, tile->lattice, t);
        if (cycle_signed_area2(&cur, tile->lattice) < 0) cycle_reverse(&cur);
        cycle_normalize_position(&cur, tile->lattice);
        /*  not necessary, self defeating! */
        /* cycle_canonicalize_shift(&cur); */

        int dup = 0;
        for (int i = 0; i < tile->variant_count; i++) {
            if (!cycle_less(&cur, &tile->variants[i]) &&
                !cycle_less(&tile->variants[i], &cur)) {
                dup = 1;
                break;
            }
        }
        if (!dup && tile->variant_count < MAX_VARIANTS) {
            tile->variants[tile->variant_count++] = cur;
        }
    }
}
