#ifndef POLYFORMTOWN_RL7_COMP7_H
#define POLYFORMTOWN_RL7_COMP7_H

#include <stddef.h>
#include "boundary7.h"

#define MAX_TILES       64
#define MAX_RULES       512
#define MAX_VWORDS      256
#define MAX_LINE        512
#define DEFAULT_MODEL   "data/rl6/refined_model.dat"

typedef struct { Tok v[6]; } TileWord;
typedef struct { Tok a, b, c, d; } EdgeRule;
typedef struct { Tok v[3]; } VWord;

typedef struct {
    TileWord tiles[MAX_TILES];
    int ntile;
    EdgeRule rules[MAX_RULES];
    int nrule;
    VWord vwords[MAX_VWORDS];
    int nvword;
} Model;

int load_model(const char *path, Model *m, char *err, size_t errsz);
Tok tile_sym(const Model *m, int tile, int rot, int slot);
int tile_matches(const Model *m, const Cell *c, int t, int r);
int vword_matches(const VWord *w, const Tok *known, int nk);
int vword_is_aaa(const VWord *w);
int validate_state_local(const Model *m, const State *s, char *err, size_t errsz);

#endif
