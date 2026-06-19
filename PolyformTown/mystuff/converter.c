/*
 * converter.c
 *
 * Convert hat-tile clusters between the Python CSV representation and this
 * library's per-hat tetrille-cycle representation (the `tiles:[...]` blocks
 * found in data/<set>/supertile.dat and completions.dat).
 *
 *   Python CSV  : one row per hat,  columns  x,y,dir,ref,pt
 *                 - (x,y)  continuous placement of the hat (Python world frame)
 *                 - dir    orientation, 0..11 in 30 deg steps (hats use even dir)
 *                 - ref    reflection flag (True/False)
 *                 - pt     1-indexed pivot vertex (see objects/hat_tile.py,
 *                          HatClusterCsvModifier._read_codes; converted on load
 *                          to the 0-indexed pt of _hat_vertices14 via (pt-1)%14)
 *                 The hat outline is  (x,y) + _hat_vertices14(dir, ref, pt-1).
 *
 *   C cluster   : tiles:[[(v,x,y),...14...],[...],...]
 *                 each hat = a 14-vertex cycle of tetrille lattice coords
 *                 (v in {3,4,6} = vertex valence class).
 *
 * The Blender pipeline (objects/hat_tile.py, _instance_hats/_hat_mesh) builds
 * each hat's *world* vertices as
 *
 *      world = Rot(theta) * _hat_vertices14(dir_in, ref, piv) + (x, y)
 *      dir_in = ref ? +1 : -1 ,   piv = (pt - 1) mod 14 ,
 *      theta  = dir * 30deg * (ref ? -1 : +1)
 *
 * i.e. the hat mesh is built in a fixed base orientation and the `dir` column
 * is applied as an instance rotation -- it is NOT the dir_in fed to
 * _hat_vertices14.
 *
 * A world point (x,y) maps to the tetrille "scaled" integer embedding (see
 * core/tetrille.c: tetrille_embed_point_scaled) by the fixed affine map
 *
 *      sx = -x - sqrt(3)*y + 3 ,   sy = -x + sqrt(3)*y
 *
 * with inverse  x = (3 - sx - sy)/2 ,  y = (sy - sx + 3)/(2*sqrt(3)).  (The
 * linear part was found by registering a hat at the world origin against the
 * matching hat.tile variant; det < 0, i.e. the Blender frame and the C scaled
 * embedding have opposite handedness -- harmless, the lattice contains both
 * hats and antihats and the reflection cancels on any round trip.  The
 * translation is chosen so the three valence sublattices land on the correct
 * cosets, i.e. every hat is an exact 6-step lattice translate of a variant.)
 *
 * Because the valence-3/4/6 sublattices are nested (every v6 point is also a
 * v4 and a v3 point), a vertex valence cannot be recovered from its position
 * alone.  CSV->C therefore never invents valences: it matches each hat to one
 * of the 12 orientation variants of tiles/hat.tile (which carry the correct
 * valences) and emits that variant, translated into place.
 *
 * Usage:
 *      ./bin/converter csv2c [in.csv]   > cluster.txt   (CSV  -> tiles:[...] )
 *      ./bin/converter c2csv [in.dat]   > out.csv       (tiles:[...] -> CSV  )
 *      ./bin/converter c2csv in.dat N                   (use record index N, 1-based)
 *
 * Missing file argument means read from stdin.  c2csv converts the first
 * `tiles:` record by default.
 *
 * Build:  make converter
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "core/tile.h"
#include "core/cycle.h"
#include "core/tetrille.h"

#define HAT_N 14
#define MAX_HATS 8192    /* a cluster record can hold many more hats than MAX_CYCLES */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const double R3 = 1.7320508075688772935;

/* ------------------------------------------------------------------ *
 *  Python _hat_vertices14 (objects/hat_tile.py), ported verbatim.
 *  Returns the 14 hat vertices for orientation `d`, reflection `ref`,
 *  0-indexed pivot `pt`, in the Python world frame (no translation).
 * ------------------------------------------------------------------ */
static int imod(int a, int m) { return ((a % m) + m) % m; }

static void hv14(int d, int ref, int pt, double out[HAT_N][2]) {
    /* 14-edge walk: (length-selector, rotation-index).  length-selector 1
       means sqrt(3), 0 means 1, matching the Python `raw` table. */
    static const int raw_len[HAT_N] = {1,1,0,0,1,1,0,0,1,1,0,0,0,0};
    static const int raw_rot[HAT_N] = {-1,1,4,6,3,5,8,6,9,7,10,12,12,14};

    double elen[HAT_N];
    int    erot[HAT_N];

    /* RotateLeft by pt, then (if ref) reverse + negate rotations mod 12. */
    for (int j = 0; j < HAT_N; j++) {
        int k = (pt + j) % HAT_N;            /* raw[pt:]+raw[:pt] */
        int src = ref ? ((pt + (HAT_N - 1 - j)) % HAT_N) : k;
        double L = raw_len[src] ? R3 : 1.0;
        int    rot = raw_rot[src] + d;
        elen[j] = L;
        erot[j] = ref ? imod(-rot, 12) : imod(rot, 12);
    }

    /* cumulative displacement: e[len,x] = len*(cos pi(x+1)/6, sin pi(x+1)/6) */
    double vx[HAT_N + 1], vy[HAT_N + 1];
    vx[0] = vy[0] = 0.0;
    for (int j = 0; j < HAT_N; j++) {
        double ang = M_PI * (erot[j] + 1) / 6.0;
        vx[j + 1] = vx[j] + elen[j] * cos(ang);
        vy[j + 1] = vy[j] + elen[j] * sin(ang);
    }
    /* drop closing duplicate -> 14 verts, then RotateRight by pt */
    for (int i = 0; i < HAT_N; i++) {
        int s = imod(i - pt, HAT_N);
        out[i][0] = vx[s];
        out[i][1] = vy[s];
    }
}

/* ------------------------------------------------------------------ *
 *  Frame bridge: Blender world frame <-> tetrille scaled embedding.
 * ------------------------------------------------------------------ */

/* World vertices of the hat instanced at (x,y) with orientation `dir`,
   reflection `ref` and 0-indexed pivot `piv`, exactly as the Blender pipeline
   places it. */
static void real_world(int dir, int ref, int piv, double x, double y,
                       double out[HAT_N][2]) {
    double V[HAT_N][2];
    hv14(ref ? 1 : -1, ref, piv, V);
    double th = dir * (M_PI / 6.0) * (ref ? -1.0 : 1.0);
    double c = cos(th), s = sin(th);
    for (int i = 0; i < HAT_N; i++) {
        out[i][0] = c * V[i][0] - s * V[i][1] + x;
        out[i][1] = s * V[i][0] + c * V[i][1] + y;
    }
}

/* world -> tetrille scaled embedding (exact, global). */
static double w2s_x(double x, double y) { return -x - R3 * y + 3.0; }
static double w2s_y(double x, double y) { return -x + R3 * y; }

/* scaled -> world (inverse). */
static void s2w(double sx, double sy, double *x, double *y) {
    *x = (3.0 - sx - sy) / 2.0;
    *y = (sy - sx + 3.0) / (2.0 * R3);
}

/* ------------------------------------------------------------------ *
 *  Small helpers for comparing 14-point sets.
 * ------------------------------------------------------------------ */
typedef struct { long a, b; } IPt;
static int ipt_cmp(const void *p, const void *q) {
    const IPt *u = p, *v = q;
    if (u->a != v->a) return u->a < v->a ? -1 : 1;
    if (u->b != v->b) return u->b < v->b ? -1 : 1;
    return 0;
}

typedef struct { double a, b; } DPt;
static int dpt_cmp(const void *p, const void *q) {
    const DPt *u = p, *v = q;
    double ka = round(u->a * 1e4), va = round(v->a * 1e4);
    if (ka != va) return ka < va ? -1 : 1;
    double kb = round(u->b * 1e4), vb = round(v->b * 1e4);
    if (kb != vb) return kb < vb ? -1 : 1;
    return 0;
}

/* ================================================================== *
 *  CSV  ->  C  (one hat)
 * ================================================================== */
/* Match the integer scaled-coordinate target set `tgt` (already sorted) to one
   of the 12 orientation variants of `tile`, translated into position.  Returns
   1 and fills *out with the placed cycle (carrying correct valences). */
static int place_hat_match(const Tile *tile, const IPt tgt[HAT_N], Cycle *out) {
    for (int vi = 0; vi < tile->variant_count; vi++) {
        const Cycle *var = &tile->variants[vi];
        if (var->n != HAT_N) continue;

        /* scaled points of the untranslated variant, sorted */
        IPt vp[HAT_N];
        for (int i = 0; i < HAT_N; i++) {
            long long sx, sy;
            tetrille_embed_point_scaled(var->v[i], &sx, &sy);
            vp[i].a = (long)sx; vp[i].b = (long)sy;
        }
        qsort(vp, HAT_N, sizeof(IPt), ipt_cmp);

        /* a unit v6 lattice translation shifts every scaled point by 6*(m,n) */
        long dx = tgt[0].a - vp[0].a;
        long dy = tgt[0].b - vp[0].b;
        if (dx % 6 || dy % 6) continue;
        int m = (int)(dx / 6), n = (int)(dy / 6);

        Cycle cand = *var;
        tetrille_translate_cycle(&cand, m, n);

        IPt cp[HAT_N];
        for (int i = 0; i < HAT_N; i++) {
            long long sx, sy;
            tetrille_embed_point_scaled(cand.v[i], &sx, &sy);
            cp[i].a = (long)sx; cp[i].b = (long)sy;
        }
        qsort(cp, HAT_N, sizeof(IPt), ipt_cmp);

        int ok = 1;
        for (int i = 0; i < HAT_N && ok; i++)
            if (cp[i].a != tgt[i].a || cp[i].b != tgt[i].b) ok = 0;
        if (ok) { *out = cand; return 1; }
    }
    return 0;
}

static void print_cycle_tuple(const Cycle *c) {
    putchar('[');
    for (int i = 0; i < c->n; i++) {
        if (i) putchar(',');
        printf("(%d,%d,%d)", c->v[i].v, c->v[i].x, c->v[i].y);
    }
    putchar(']');
}

typedef struct { double x, y; int dir, ref, pt; } CsvRow;

static int run_csv2c(FILE *in) {
    Tile tile;
    if (!tile_load("tiles/hat.tile", &tile)) {
        fprintf(stderr, "converter: cannot load tiles/hat.tile (run from project root)\n");
        return 1;
    }

    Cycle *hats = malloc(sizeof(*hats) * MAX_HATS);
    if (!hats) { fprintf(stderr, "converter: out of memory\n"); return 1; }
    int count = 0, bad = 0, line_no = 0;
    char line[512];
    while (fgets(line, sizeof(line), in)) {
        line_no++;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') continue;
        if (!(isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.')) continue;
        if (count >= MAX_HATS) {
            fprintf(stderr, "converter: too many hats (max %d)\n", MAX_HATS);
            break;
        }

        CsvRow row;
        char refbuf[16];
        if (sscanf(line, "%lf,%lf,%d,%15[^,],%d", &row.x, &row.y,
                   &row.dir, refbuf, &row.pt) != 5) {
            fprintf(stderr, "converter: skipping unparseable line %d\n", line_no);
            continue;
        }
        row.ref = (refbuf[0] == 'T' || refbuf[0] == 't' || refbuf[0] == '1');

        int piv = imod(row.pt - 1, HAT_N);        /* _read_codes convention */
        double W[HAT_N][2];
        real_world(row.dir, row.ref, piv, row.x, row.y, W);

        IPt tgt[HAT_N];
        int onlattice = 1;
        for (int i = 0; i < HAT_N; i++) {
            double fsx = w2s_x(W[i][0], W[i][1]);
            double fsy = w2s_y(W[i][0], W[i][1]);
            long sx = lround(fsx), sy = lround(fsy);
            if (fabs(fsx - sx) > 1e-3 || fabs(fsy - sy) > 1e-3) onlattice = 0;
            tgt[i].a = sx; tgt[i].b = sy;
        }
        qsort(tgt, HAT_N, sizeof(IPt), ipt_cmp);

        if (!onlattice || !place_hat_match(&tile, tgt, &hats[count])) {
            fprintf(stderr,
                    "converter: hat (x=%g y=%g dir=%d ref=%s pt=%d): %s\n",
                    row.x, row.y, row.dir, row.ref ? "True" : "False", row.pt,
                    onlattice ? "no matching hat variant"
                              : "vertices are not on the tetrille lattice");
            bad++;
            continue;
        }
        count++;
    }

    if (count == 0) {
        fprintf(stderr, "converter: no hats converted\n");
        free(hats);
        return 1;
    }

    printf("tile_count:%d\n", count);
    printf("tiles:[");
    for (int i = 0; i < count; i++) {
        if (i) putchar(',');
        print_cycle_tuple(&hats[i]);
    }
    printf("]\n");
    free(hats);
    return bad ? 2 : 0;
}

/* ================================================================== *
 *  C  ->  CSV  (one hat)
 * ================================================================== */
/* Recover (dir,ref,x,y) for a hat cycle.  pt is emitted canonically as 1
   (0-indexed pivot 0); (x,y) is chosen so the Blender pipeline reproduces the
   hat: world = Rot(theta)*_hat_vertices14(+-1,ref,0) + (x,y).  Returns 1 on
   success. */
static int recover_hat(const Cycle *c, int *dir, int *ref, double *x, double *y) {
    if (c->n != HAT_N) return 0;

    DPt T[HAT_N];
    double cx = 0, cy = 0;
    for (int i = 0; i < HAT_N; i++) {
        long long sx, sy;
        double px, py;
        tetrille_embed_point_scaled(c->v[i], &sx, &sy);
        s2w((double)sx, (double)sy, &px, &py);
        T[i].a = px; T[i].b = py;
        cx += px; cy += py;
    }
    cx /= HAT_N; cy /= HAT_N;

    DPt Ts[HAT_N];
    memcpy(Ts, T, sizeof Ts);
    qsort(Ts, HAT_N, sizeof(DPt), dpt_cmp);

    for (int d = 0; d < 12; d++) {
        for (int rf = 0; rf < 2; rf++) {
            double H[HAT_N][2];
            real_world(d, rf, 0, 0.0, 0.0, H);    /* template at origin, pivot 0 */
            double hx = 0, hy = 0;
            for (int i = 0; i < HAT_N; i++) { hx += H[i][0]; hy += H[i][1]; }
            hx /= HAT_N; hy /= HAT_N;
            double dx = cx - hx, dy = cy - hy;     /* candidate (x,y) */

            DPt Hs[HAT_N];
            for (int i = 0; i < HAT_N; i++) {
                Hs[i].a = H[i][0] + dx;
                Hs[i].b = H[i][1] + dy;
            }
            qsort(Hs, HAT_N, sizeof(DPt), dpt_cmp);

            int ok = 1;
            for (int i = 0; i < HAT_N && ok; i++)
                if (fabs(Hs[i].a - Ts[i].a) > 1e-4 || fabs(Hs[i].b - Ts[i].b) > 1e-4)
                    ok = 0;
            if (ok) { *dir = d; *ref = rf; *x = dx; *y = dy; return 1; }
        }
    }
    return 0;
}

/* Parse the `tiles:[[(v,x,y),...],...]` value of record `want` (1-based) from
   the stream.  Records are separated by lines beginning with "---[".  Returns
   the number of hats parsed into `hats`, or -1 on error. */
static int parse_tiles_record(FILE *in, int want, Cycle *hats, int max_hats) {
    char line[1 << 16];
    int record = 0;
    while (fgets(line, sizeof(line), in)) {
        if (strncmp(line, "---[", 4) == 0) { record++; continue; }
        if (strncmp(line, "tiles:", 6) != 0) continue;
        record += (record == 0);            /* file without ---[ headers */
        if (record != want) continue;

        const char *p = strchr(line, '[');
        if (!p) return -1;
        int depth = 0, count = 0;
        Cycle cur; cur.n = 0;
        for (; *p; p++) {
            if (*p == '[') {
                depth++;
                if (depth == 2) cur.n = 0;
            } else if (*p == ']') {
                if (depth == 2) {
                    if (count < max_hats && cur.n > 0) hats[count++] = cur;
                }
                depth--;
                if (depth <= 0) break;
            } else if (*p == '(') {
                int v, x, y;
                if (sscanf(p, "(%d,%d,%d)", &v, &x, &y) == 3 && cur.n < MAX_VERTS)
                    cur.v[cur.n++] = (Coord){v, x, y};
            }
        }
        return count;
    }
    return -1;
}

static int run_c2csv(FILE *in, int want) {
    Cycle *hats = malloc(sizeof(*hats) * MAX_HATS);
    if (!hats) { fprintf(stderr, "converter: out of memory\n"); return 1; }
    int n = parse_tiles_record(in, want, hats, MAX_HATS);
    if (n <= 0) {
        fprintf(stderr, n < 0 ? "converter: no `tiles:` record %d found in input\n"
                              : "converter: record %d has no hats\n", want);
        free(hats);
        return 1;
    }

    printf("x,y,dir,ref,pt\n");
    int bad = 0;
    for (int i = 0; i < n; i++) {
        int dir, ref;
        double x, y;
        if (!recover_hat(&hats[i], &dir, &ref, &x, &y)) {
            fprintf(stderr, "converter: hat %d does not match a hat outline "
                            "(n=%d)\n", i, hats[i].n);
            bad++;
            continue;
        }
        /* pivot emitted canonically: pt=1 -> _read_codes pt_internal 0 */
        printf("%.15g,%.15g,%d,%s,1\n", x, y, dir, ref ? "True" : "False");
    }
    free(hats);
    return bad ? 2 : 0;
}

/* ================================================================== */
static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s csv2c [in.csv]            CSV rows -> tiles:[...] block\n"
        "       %s c2csv [in.dat [record]]   tiles:[...] -> CSV rows\n"
        "(reads stdin when no file given; run from the project root so\n"
        " tiles/hat.tile is reachable)\n", prog, prog);
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 1; }

    const char *mode = argv[1];
    const char *path = (argc >= 3) ? argv[2] : NULL;
    FILE *in = stdin;
    if (path) {
        in = fopen(path, "r");
        if (!in) { fprintf(stderr, "converter: cannot open %s\n", path); return 1; }
    }

    int rc;
    if (strcmp(mode, "csv2c") == 0) {
        rc = run_csv2c(in);
    } else if (strcmp(mode, "c2csv") == 0) {
        int want = (argc >= 4) ? atoi(argv[3]) : 1;
        if (want < 1) want = 1;
        rc = run_c2csv(in, want);
    } else {
        usage(argv[0]);
        rc = 1;
    }

    if (in != stdin) fclose(in);
    return rc;
}