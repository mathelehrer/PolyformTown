#include "core/cycle.h"
#include "core/tile.h"
#include "core/lattice.h"
#include "core/tetrille.h"
#include <stdio.h>

static void tetrille_embed6_scaled(Coord p, long long *sx, long long *sy) {
    tetrille_embed_point_scaled(p, sx, sy);
}

static int floor_div6(long long a) {
    if (a >= 0) return (int)(a / 6LL);
    return (int)(-(((-a) + 5LL) / 6LL));
}

static void cycle_translate_tetrille(Cycle *c, int m6, int n6) {
    for (int i = 0; i < c->n; i++) {
        Coord *p = &c->v[i];
        if (p->v == 6) {
            p->x += m6;
            p->y += n6;
        } else if (p->v == 4) {
            p->x += 2 * m6;
            p->y += 2 * n6;
        } else if (p->v == 3) {
            p->x += 2 * m6 + n6;
            p->y += -m6 + n6;
        } else {
            p->x += m6;
            p->y += n6;
        }
    }
}

int coord_eq(Coord a, Coord b) {
    return a.v == b.v && a.x == b.x && a.y == b.y;
}

Edge cycle_edge(const Cycle *c, int i) {
    Edge e;
    e.a = c->v[i];
    e.b = c->v[(i + 1) % c->n];
    return e;
}

long long cycle_signed_area2(const Cycle *c, int lattice) {
    int tetrille = (lattice == TILE_LATTICE_TETRILLE);

    long long s = 0;
    for (int i = 0; i < c->n; i++) {
        Coord a = c->v[i];
        Coord b = c->v[(i + 1) % c->n];
        if (tetrille) {
            long long ax, ay, bx, by;
            tetrille_embed6_scaled(a, &ax, &ay);
            tetrille_embed6_scaled(b, &bx, &by);
            s += ax * by - bx * ay;
        } else {
            s += (long long)a.x * b.y - (long long)b.x * a.y;
        }
    }
    return s;
}

void cycle_reverse(Cycle *c) {
    if (c->n <= 1) return;
    Coord tmp[MAX_VERTS];
    for (int i = 0; i < c->n; i++) tmp[i] = c->v[c->n - i - 1];
    for (int i = 0; i < c->n; i++) c->v[i] = tmp[i];
}

void cycle_translate(Cycle *c, int dx, int dy) {
    for (int i = 0; i < c->n; i++) {
        c->v[i].x += dx;
        c->v[i].y += dy;
    }
}

void cycle_normalize_position(Cycle *c, int lattice) {
    int tetrille = (lattice == TILE_LATTICE_TETRILLE);

    if (!tetrille) {
        int minx = c->v[0].x, miny = c->v[0].y;
        for (int i = 1; i < c->n; i++) {
            if (c->v[i].x < minx) minx = c->v[i].x;
            if (c->v[i].y < miny) miny = c->v[i].y;
        }
        cycle_translate(c, -minx, -miny);
        return;
    }

    long long minx, miny;
    tetrille_embed6_scaled(c->v[0], &minx, &miny);
    for (int i = 1; i < c->n; i++) {
        long long x, y;
        tetrille_embed6_scaled(c->v[i], &x, &y);
        if (x < minx) minx = x;
        if (y < miny) miny = y;
    }

    cycle_translate_tetrille(c, -floor_div6(minx), -floor_div6(miny));
}

static int coord_less(Coord a, Coord b) {
    if (a.v != b.v) return a.v < b.v;
    if (a.x != b.x) return a.x < b.x;
    return a.y < b.y;
}

void cycle_canonicalize_shift(Cycle *c) {
    int best = 0;
    for (int s = 1; s < c->n; s++) {
        for (int k = 0; k < c->n; k++) {
            Coord a = c->v[(s + k) % c->n];
            Coord b = c->v[(best + k) % c->n];
            if (coord_eq(a, b)) continue;
            if (coord_less(a, b)) best = s;
            break;
        }
    }
    if (best != 0) {
        Coord tmp[MAX_VERTS];
        for (int i = 0; i < c->n; i++) tmp[i] = c->v[(best + i) % c->n];
        for (int i = 0; i < c->n; i++) c->v[i] = tmp[i];
    }
}

static int cycle_abs_area_cmp_desc(const Cycle *a, const Cycle *b, int lattice);

static Coord square_apply(Coord p, int t) {
    int x = p.x, y = p.y;
    switch (t) {
        case 0: return (Coord){p.v,  x,  y};
        case 1: return (Coord){p.v,  y, -x};
        case 2: return (Coord){p.v, -x, -y};
        case 3: return (Coord){p.v, -y,  x};
        case 4: return (Coord){p.v, -x,  y};
        case 5: return (Coord){p.v,  y,  x};
        case 6: return (Coord){p.v,  x, -y};
        case 7: return (Coord){p.v, -y, -x};
        default: return p;
    }
}

static Coord tri_rot60(Coord p) {
    return (Coord){p.v, -p.y, p.x + p.y};
}

static Coord tri_reflect(Coord p) {
    return (Coord){p.v, p.y, p.x};
}

static Coord triangular_apply(Coord p, int t) {
    Coord q = p;
    if (t >= 6) {
        q = tri_reflect(q);
        t -= 6;
    }
    for (int i = 0; i < t; i++) q = tri_rot60(q);
    return q;
}

static Coord tetrille_reflect(Coord p) {
    if (p.v == 3) return (Coord){p.v, p.x + p.y, -p.y};
    return (Coord){p.v, p.y, p.x};
}

static Coord tetrille_apply(Coord p, int t) {
    Coord q = p;
    if (t >= 6) {
        q = tetrille_reflect(q);
        t -= 6;
    }
    for (int i = 0; i < t; i++) q = tri_rot60(q);
    return q;
}

void cycle_transform(const Cycle *src, Cycle *dst, int t) {
    cycle_transform_lattice(src, dst, TILE_LATTICE_SQUARE, t);
}

void cycle_transform_lattice(const Cycle *src, Cycle *dst, int lattice, int t) {
    dst->n = src->n;
    for (int i = 0; i < src->n; i++) {
        Coord p = src->v[i];
        if (lattice == TILE_LATTICE_TRIANGULAR) dst->v[i] = triangular_apply(p, t);
        else if (lattice == TILE_LATTICE_TETRILLE) dst->v[i] = tetrille_apply(p, t);
        else dst->v[i] = square_apply(p, t);
    }
}

int cycle_less(const Cycle *a, const Cycle *b) {
    if (a->n != b->n) return a->n < b->n;
    for (int i = 0; i < a->n; i++) {
        if (a->v[i].v != b->v[i].v) return a->v[i].v < b->v[i].v;
        if (a->v[i].x != b->v[i].x) return a->v[i].x < b->v[i].x;
        if (a->v[i].y != b->v[i].y) return a->v[i].y < b->v[i].y;
    }
    return 0;
}

void cycle_canonicalize(const Cycle *src, Cycle *out) {
    cycle_canonicalize_lattice(src, out, TILE_LATTICE_SQUARE);
}

static void cycle_rotate_to_start(const Cycle *src, Cycle *dst, int start) {
    dst->n = src->n;
    for (int i = 0; i < src->n; i++) dst->v[i] = src->v[(start + i) % src->n];
}

void cycle_canonicalize_lattice(const Cycle *src, Cycle *out, int lattice) {
    if (lattice != TILE_LATTICE_TETRILLE) {
        Cycle best = {0}, cur;
        int first = 1;
        int count = lattice_transform_count(lattice);
        for (int t = 0; t < count; t++) {
            cycle_transform_lattice(src, &cur, lattice, t);
            if (cycle_signed_area2(&cur, lattice) < 0) cycle_reverse(&cur);
            cycle_normalize_position(&cur, lattice);
            cycle_canonicalize_shift(&cur);
            if (first || cycle_less(&cur, &best)) {
                best = cur;
                first = 0;
            }
        }
        *out = best;
        return;
    }

    Cycle best = {0}, rooted, cur;
    int first = 1;
    for (int root = 0; root < src->n; root++) {
        if (src->v[root].v != 6) continue;
        cycle_rotate_to_start(src, &rooted, root);
        cycle_translate_tetrille(&rooted, -rooted.v[0].x, -rooted.v[0].y);
        for (int t = 0; t < 12; t++) {
            cycle_transform_lattice(&rooted, &cur, lattice, t);
            if (first || cycle_less(&cur, &best)) {
                best = cur;
                first = 0;
            }
        }
    }

    if (first) {
        best = *src;
        cycle_normalize_position(&best, lattice);
        cycle_canonicalize_shift(&best);
    }
    *out = best;
}


void poly_translate(Poly *p, int dx, int dy) {
    for (int i = 0; i < p->cycle_count; i++) cycle_translate(&p->cycles[i], dx, dy);
}

void poly_normalize_position(Poly *p, int lattice) {
    int tetrille = (lattice == TILE_LATTICE_TETRILLE);

    if (!tetrille) {
        int minx = p->cycles[0].v[0].x;
        int miny = p->cycles[0].v[0].y;
        for (int i = 0; i < p->cycle_count; i++) {
            Cycle *c = &p->cycles[i];
            for (int j = 0; j < c->n; j++) {
                if (c->v[j].x < minx) minx = c->v[j].x;
                if (c->v[j].y < miny) miny = c->v[j].y;
            }
        }
        poly_translate(p, -minx, -miny);
        return;
    }

    long long minx = 0, miny = 0;
    int first = 1;
    for (int i = 0; i < p->cycle_count; i++) {
        Cycle *c = &p->cycles[i];
        for (int j = 0; j < c->n; j++) {
            long long x, y;
            tetrille_embed6_scaled(c->v[j], &x, &y);
            if (first || x < minx) minx = x;
            if (first || y < miny) miny = y;
            first = 0;
        }
    }

    int tx = -floor_div6(minx);
    int ty = -floor_div6(miny);
    for (int i = 0; i < p->cycle_count; i++) cycle_translate_tetrille(&p->cycles[i], tx, ty);
}

void poly_transform(const Poly *src, Poly *dst, int t) {
    poly_transform_lattice(src, dst, TILE_LATTICE_SQUARE, t);
}

void poly_transform_lattice(const Poly *src, Poly *dst, int lattice, int t) {
    dst->cycle_count = src->cycle_count;
    for (int i = 0; i < src->cycle_count; i++) cycle_transform_lattice(&src->cycles[i], &dst->cycles[i], lattice, t);
}

static int cycle_abs_area_cmp_desc(const Cycle *a, const Cycle *b, int lattice) {
    long long aa = cycle_signed_area2(a, lattice);
    long long ab = cycle_signed_area2(b, lattice);
    if (aa < 0) aa = -aa;
    if (ab < 0) ab = -ab;
    if (aa != ab) return aa > ab;
    return cycle_less(a, b);
}

static void poly_prepare_cycles(Poly *p, int lattice) {
    int outer = 0;
    for (int i = 1; i < p->cycle_count; i++) {
        if (cycle_abs_area_cmp_desc(&p->cycles[i], &p->cycles[outer], lattice)) outer = i;
    }

    if (outer != 0) {
        Cycle tmp = p->cycles[0];
        p->cycles[0] = p->cycles[outer];
        p->cycles[outer] = tmp;
    }

    for (int i = 0; i < p->cycle_count; i++) {
        long long area = cycle_signed_area2(&p->cycles[i], lattice);
        if (i == 0) {
            if (area < 0) cycle_reverse(&p->cycles[i]);
        } else {
            if (area > 0) cycle_reverse(&p->cycles[i]);
        }
        cycle_canonicalize_shift(&p->cycles[i]);
    }

    for (int i = 1; i < p->cycle_count; i++) {
        for (int j = i + 1; j < p->cycle_count; j++) {
            if (cycle_less(&p->cycles[j], &p->cycles[i])) {
                Cycle tmp = p->cycles[i];
                p->cycles[i] = p->cycles[j];
                p->cycles[j] = tmp;
            }
        }
    }
}

int poly_less(const Poly *a, const Poly *b) {
    if (a->cycle_count != b->cycle_count) return a->cycle_count < b->cycle_count;
    for (int i = 0; i < a->cycle_count; i++) {
        if (a->cycles[i].n != b->cycles[i].n) return a->cycles[i].n < b->cycles[i].n;
        for (int j = 0; j < a->cycles[i].n; j++) {
            if (a->cycles[i].v[j].v != b->cycles[i].v[j].v) return a->cycles[i].v[j].v < b->cycles[i].v[j].v;
            if (a->cycles[i].v[j].x != b->cycles[i].v[j].x) return a->cycles[i].v[j].x < b->cycles[i].v[j].x;
            if (a->cycles[i].v[j].y != b->cycles[i].v[j].y) return a->cycles[i].v[j].y < b->cycles[i].v[j].y;
        }
    }
    return 0;
}

void poly_canonicalize(const Poly *src, Poly *out) {
    poly_canonicalize_lattice(src, out, TILE_LATTICE_SQUARE);
}

static void poly_canonicalize_tetrille(const Poly *src, Poly *out) {
    Poly best = {0}, cur;
    int first = 1;
    for (int t = 0; t < 12; t++) {
        poly_transform_lattice(src, &cur, TILE_LATTICE_TETRILLE, t);
        poly_normalize_position(&cur, TILE_LATTICE_TETRILLE);
        poly_prepare_cycles(&cur, TILE_LATTICE_TETRILLE);
        if (first || poly_less(&cur, &best)) {
            best = cur;
            first = 0;
        }
    }
    *out = best;
}

void poly_canonicalize_lattice(const Poly *src, Poly *out, int lattice) {
    if (lattice != TILE_LATTICE_TETRILLE) {
        Poly best = {0}, cur;
        int first = 1;
        int count = lattice_transform_count(lattice);
        for (int t = 0; t < count; t++) {
            poly_transform_lattice(src, &cur, lattice, t);
            poly_normalize_position(&cur, lattice);
            poly_prepare_cycles(&cur, lattice);
            if (first || poly_less(&cur, &best)) {
                best = cur;
                first = 0;
            }
        }
        *out = best;
        return;
    }

    poly_canonicalize_tetrille(src, out);
}

void poly_hash_key_lattice(const Poly *src, int lattice, Poly *key) {
    poly_canonicalize_lattice(src, key, lattice);
}
