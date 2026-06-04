#ifndef POLYFORMTOWN_RL7_INFLATION7_H
#define POLYFORMTOWN_RL7_INFLATION7_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Mountain and Range RL7 inflation states.  D occupies two axial cells;
 * B and G occupy one H-cell. */
typedef enum { MR7_D = 0, MR7_B = 1, MR7_G = 2, MR7_F = 3 } MR7Kind;

typedef enum {
    MR7_INIT_D = 0,
    MR7_INIT_H,
    MR7_INIT_DH,
    MR7_INIT_B3,
    MR7_INIT_L3,
    MR7_INIT_F,
    MR7_INIT_COUNT
} MR7Init;

typedef struct {
    char symbol;              /* D K H a b g h */
    uint8_t ori;             /* orientation modulo six */
} MR7Token;

typedef struct {
    MR7Kind kind;
    uint8_t ori;
    uint8_t color_index;     /* auxiliary inherited/relative-position colour */
    MR7Token *word;
    size_t word_len;
} MR7Tile;

typedef struct {
    MR7Tile *tile;
    size_t n;
    size_t cap;
} MR7Patch;

typedef struct {
    /* Child slot to auxiliary colour. Slots follow the source rule order:
     * 0..10 for a D parent and 0..5 for an H parent. */
    uint8_t d_child[11];
    uint8_t h_child[6];
    uint8_t seed[6];
} MR7ColorMap;

typedef struct {
    int q, r;
    char state;              /* '0'/'1' D halves, 'B', 'G', or 'F' */
    uint8_t ori;
    uint8_t color_index;
} MR7Cell;

typedef struct {
    MR7Cell *cell;
    size_t n;
} MR7Cells;

void mr7_patch_init(MR7Patch *p);
void mr7_patch_free(MR7Patch *p);
void mr7_cells_free(MR7Cells *c);
void mr7_color_map_default(MR7ColorMap *map);

int mr7_seed_false_center(MR7Patch *out, char *err, size_t errsz);
int mr7_seed_brown_g_ring(MR7Patch *out, const MR7ColorMap *map,
                          char *err, size_t errsz);
int mr7_seed_single_d(MR7Patch *out, char *err, size_t errsz);
int mr7_seed_single_h(MR7Patch *out, char *err, size_t errsz);
int mr7_seed_dh(MR7Patch *out, char *err, size_t errsz);
int mr7_extract_c3_seed(const char *colour_class, MR7Patch *out,
                        char *record, size_t recordsz,
                        char *err, size_t errsz);
int mr7_write_c3_reduction_audit(FILE *fp, unsigned max_generation,
                                 char *err, size_t errsz);
int mr7_make_init(MR7Init init, MR7Patch *out, const MR7ColorMap *map,
                  char *record, size_t recordsz,
                  char *err, size_t errsz);
int mr7_expand_init(MR7Init init, unsigned level, MR7Patch *out,
                    const MR7ColorMap *map, char *record, size_t recordsz,
                    char *err, size_t errsz);
const char *mr7_init_name(MR7Init init);
int mr7_parse_init(const char *s, MR7Init *out);
int mr7_inflate(const MR7Patch *in, MR7Patch *out, const MR7ColorMap *map,
                char *err, size_t errsz);
int mr7_expand_persistent_ring(unsigned level, MR7Patch *out,
                               const MR7ColorMap *map,
                               char *err, size_t errsz);
int mr7_tiles_to_cells(const MR7Patch *patch, MR7Cells *out,
                       char *err, size_t errsz);
int mr7_check_reference_counts(FILE *fp, const MR7ColorMap *map);
const char *mr7_kind_name(MR7Kind k);

#endif
