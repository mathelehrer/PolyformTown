#ifndef CYCLE_H
#define CYCLE_H

#include <stddef.h>

#define MAX_VERTS 384
#define MAX_CYCLES 32

typedef struct {
    int v;
    int x;
    int y;
} Coord;

typedef struct {
    int n;          /* number of vertices */
    Coord v[MAX_VERTS];
} Cycle;

typedef struct {
    int cycle_count;
    Cycle cycles[MAX_CYCLES];
} Poly;

typedef struct {
    Coord a;
    Coord b;
} Edge;

int coord_eq(Coord a, Coord b);
Edge cycle_edge(const Cycle *c, int i);
long long cycle_signed_area2(const Cycle *c, int lattice);
void cycle_reverse(Cycle *c);
void cycle_translate(Cycle *c, int dx, int dy);
void cycle_normalize_position(Cycle *c, int lattice);
void cycle_canonicalize_shift(Cycle *c);
void cycle_transform(const Cycle *src, Cycle *dst, int t);
void cycle_transform_lattice(const Cycle *src, Cycle *dst, int lattice, int t);
int cycle_less(const Cycle *a, const Cycle *b);
void cycle_canonicalize(const Cycle *src, Cycle *out);
void cycle_canonicalize_lattice(const Cycle *src, Cycle *out, int lattice);

void poly_translate(Poly *p, int dx, int dy);
void poly_normalize_position(Poly *p, int lattice);
void poly_transform(const Poly *src, Poly *dst, int t);
void poly_transform_lattice(const Poly *src, Poly *dst, int lattice, int t);
int poly_less(const Poly *a, const Poly *b);
void poly_canonicalize(const Poly *src, Poly *out);
void poly_canonicalize_lattice(const Poly *src, Poly *out, int lattice);
void poly_hash_key_lattice(const Poly *src, int lattice, Poly *key);
int poly_has_holes(const Poly *p);

#endif
