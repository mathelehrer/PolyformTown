#ifndef POLYFORMTOWN_RL7_BOUNDARY7_H
#define POLYFORMTOWN_RL7_BOUNDARY7_H

#include <stddef.h>

#define MAX_CELLS       81
#define TOKEN_MAX       32

typedef struct { char s[TOKEN_MAX]; } Tok;

typedef struct {
    int q, r;
    Tok slot[6];          /* empty token means unknown */
    int tile;             /* -1 unknown, else index into model */
    int rot;
} Cell;

typedef struct {
    Cell cells[MAX_CELLS];
    int ncell;
} State;

typedef struct { int ci[3], sl[3], n; } VGroup;

int tok_empty(const Tok *t);
int tok_eq(const Tok *a, const Tok *b);
void tok_set(Tok *t, const char *s);

void side_neighbor_coord(int q, int r, int side, int *nq, int *nr);
int find_cell(const State *s, int q, int r);
int add_cell(State *s, int q, int r);
void init_state(State *s);
int set_cell_slot(Cell *c, int slot, const Tok *sym, char *err, size_t errsz);
void edge_slots(int side, int *a, int *b);
int collect_vgroups(const State *s, VGroup *g, int maxg);

#endif
