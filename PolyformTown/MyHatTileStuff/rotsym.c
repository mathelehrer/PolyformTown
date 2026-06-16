/*
 * rotsym.c
 *
 * Check whether a Python hat-tile supertile (CSV columns x,y,dir,ref,pt) can be
 * combined with two copies of itself -- rotated by 120 deg and 240 deg about a
 * shared vertex -- into a 3-fold rotationally symmetric cluster with no overlaps
 * and no gap at the shared vertex.  This is the "the supertile behaves like a
 * single hat" claim: a hat meets two of its rotated images at a 120-degree
 * vertex.
 *
 * Method (all in the Blender world frame, exactly as csv2svg.c / converter.c):
 *
 *   1. Build the 14-vertex world polygon of every hat (real_world()).
 *   2. For every distinct boundary vertex sum the interior corner angle of each
 *      hat meeting there ("covered angle").  A vertex whose covered angle is
 *      120 deg is a convex corner where three rotated copies would fill exactly
 *      360 deg -> candidate rotation centre.
 *   3. For each candidate centre rotate all hats by +120 and +240 deg about it
 *      and check:
 *        (a) no hat of one copy overlaps (positive area) a hat of another, and
 *        (b) the copies are edge-connected (share >=1 boundary edge), so the
 *            result is one cluster, not three pieces meeting at a single point.
 *   4. Report PASS/FAIL per candidate and render a passing cluster to SVG.
 *
 * Self-contained:  cc -O2 -o bin/rotsym MyHatTileStuff/rotsym.c -lm
 *
 * Usage:
 *   ./bin/rotsym in.csv [out.svg]
 *     e.g.  ./bin/rotsym from_python/SUPER_SUPER_H7_TILE.dat from_python/rotsym.svg
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#define HAT_N 14
#define MAX_HATS 8192

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const double R3 = 1.7320508075688772935;

/* ---- hat geometry (ported from converter.c / csv2svg.c) ------------------ */
static int imod(int a, int m) { return ((a % m) + m) % m; }

static void hv14(int d, int ref, int pt, double out[HAT_N][2]) {
    static const int raw_len[HAT_N] = {1,1,0,0,1,1,0,0,1,1,0,0,0,0};
    static const int raw_rot[HAT_N] = {-1,1,4,6,3,5,8,6,9,7,10,12,12,14};
    double elen[HAT_N]; int erot[HAT_N];
    for (int j = 0; j < HAT_N; j++) {
        int src = ref ? ((pt + (HAT_N - 1 - j)) % HAT_N) : ((pt + j) % HAT_N);
        elen[j] = raw_len[src] ? R3 : 1.0;
        int rot = raw_rot[src] + d;
        erot[j] = ref ? imod(-rot, 12) : imod(rot, 12);
    }
    double vx[HAT_N + 1], vy[HAT_N + 1];
    vx[0] = vy[0] = 0.0;
    for (int j = 0; j < HAT_N; j++) {
        double ang = M_PI * (erot[j] + 1) / 6.0;
        vx[j + 1] = vx[j] + elen[j] * cos(ang);
        vy[j + 1] = vy[j] + elen[j] * sin(ang);
    }
    for (int i = 0; i < HAT_N; i++) {
        int s = imod(i - pt, HAT_N);
        out[i][0] = vx[s]; out[i][1] = vy[s];
    }
}

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

/* ---- small geometry helpers --------------------------------------------- */
typedef struct { double x, y; } P2;
typedef struct { P2 v[HAT_N]; double minx, miny, maxx, maxy; } Hat;

#define GEOM_EPS 1e-7      /* orientation sign threshold (lattice scale ~1) */
#define ON_EPS   1e-4      /* "point lies on an edge" distance */
#define QSCALE   100000.0  /* vertex/edge coincidence quantum (~1e-5) */

static double orient(P2 a, P2 b, P2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/* proper segment crossing: interiors cross transversally (excludes collinear
   overlap and endpoint/edge touching -- so tiles that merely share an edge are
   NOT reported as overlapping). */
static int seg_cross(P2 a, P2 b, P2 c, P2 d) {
    double o1 = orient(a, b, c), o2 = orient(a, b, d);
    double o3 = orient(c, d, a), o4 = orient(c, d, b);
    if (fabs(o1) <= GEOM_EPS || fabs(o2) <= GEOM_EPS ||
        fabs(o3) <= GEOM_EPS || fabs(o4) <= GEOM_EPS) return 0;
    return (o1 > 0) != (o2 > 0) && (o3 > 0) != (o4 > 0);
}

static double dist_pt_seg(P2 p, P2 a, P2 b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double L2 = dx * dx + dy * dy;
    double t = L2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / L2 : 0;
    if (t < 0) t = 0; else if (t > 1) t = 1;
    double cx = a.x + t * dx, cy = a.y + t * dy;
    return hypot(p.x - cx, p.y - cy);
}

/* strictly inside polygon (not on its boundary). */
static int strictly_inside(P2 p, const P2 *poly, int n) {
    for (int i = 0; i < n; i++)
        if (dist_pt_seg(p, poly[i], poly[(i + 1) % n]) <= ON_EPS) return 0;
    int in = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if ((poly[i].y > p.y) != (poly[j].y > p.y)) {
            double xi = poly[i].x + (p.y - poly[i].y) *
                        (poly[j].x - poly[i].x) / (poly[j].y - poly[i].y);
            if (p.x < xi) in = !in;
        }
    }
    return in;
}

/* do two hats have overlapping interior (positive area)? touching edges = no. */
static int hats_overlap(const Hat *A, const Hat *B) {
    if (A->maxx < B->minx - ON_EPS || B->maxx < A->minx - ON_EPS ||
        A->maxy < B->miny - ON_EPS || B->maxy < A->miny - ON_EPS) return 0;
    for (int i = 0; i < HAT_N; i++)
        for (int j = 0; j < HAT_N; j++)
            if (seg_cross(A->v[i], A->v[(i + 1) % HAT_N],
                          B->v[j], B->v[(j + 1) % HAT_N])) return 1;
    for (int i = 0; i < HAT_N; i++) if (strictly_inside(A->v[i], B->v, HAT_N)) return 1;
    for (int j = 0; j < HAT_N; j++) if (strictly_inside(B->v[j], A->v, HAT_N)) return 1;
    return 0;
}

static void bbox(Hat *h) {
    h->minx = h->miny = 1e300; h->maxx = h->maxy = -1e300;
    for (int i = 0; i < HAT_N; i++) {
        if (h->v[i].x < h->minx) h->minx = h->v[i].x;
        if (h->v[i].x > h->maxx) h->maxx = h->v[i].x;
        if (h->v[i].y < h->miny) h->miny = h->v[i].y;
        if (h->v[i].y > h->maxy) h->maxy = h->v[i].y;
    }
}

static P2 rotate_about(P2 p, P2 c, double ca, double sa) {
    double dx = p.x - c.x, dy = p.y - c.y;
    P2 r = { c.x + dx * ca - dy * sa, c.y + dx * sa + dy * ca };
    return r;
}

/* a whole copy of the supertile, rotated by `deg` about centre `c`. */
static void make_copy(const Hat *src, int n, P2 c, double deg, Hat *dst) {
    double a = deg * M_PI / 180.0, ca = cos(a), sa = sin(a);
    for (int k = 0; k < n; k++) {
        for (int i = 0; i < HAT_N; i++) dst[k].v[i] = rotate_about(src[k].v[i], c, ca, sa);
        bbox(&dst[k]);
    }
}

/* any cross-copy overlap between two whole copies? */
static int copies_overlap(const Hat *A, const Hat *B, int n) {
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (hats_overlap(&A[i], &B[j])) return 1;
    return 0;
}

/* ---- edge bookkeeping (boundary detection + shared-edge counting) -------- */
typedef struct { long ax, ay, bx, by; } EKey;

static EKey edge_key(P2 a, P2 b) {
    long ax = lround(a.x * QSCALE), ay = lround(a.y * QSCALE);
    long bx = lround(b.x * QSCALE), by = lround(b.y * QSCALE);
    EKey k;
    if (ax < bx || (ax == bx && ay <= by)) { k.ax = ax; k.ay = ay; k.bx = bx; k.by = by; }
    else { k.ax = bx; k.ay = by; k.bx = ax; k.by = ay; }
    return k;
}
static int ekey_cmp(const void *p, const void *q) {
    const EKey *a = p, *b = q;
    if (a->ax != b->ax) return a->ax < b->ax ? -1 : 1;
    if (a->ay != b->ay) return a->ay < b->ay ? -1 : 1;
    if (a->bx != b->bx) return a->bx < b->bx ? -1 : 1;
    if (a->by != b->by) return a->by < b->by ? -1 : 1;
    return 0;
}

/* count edges shared (coincident) between two copies. */
static int shared_edges(const Hat *A, const Hat *B, int n) {
    int m = n * HAT_N;
    EKey *eb = malloc(sizeof(EKey) * m);
    int c = 0;
    for (int k = 0; k < n; k++)
        for (int i = 0; i < HAT_N; i++)
            eb[c++] = edge_key(B[k].v[i], B[k].v[(i + 1) % HAT_N]);
    qsort(eb, m, sizeof(EKey), ekey_cmp);
    int shared = 0;
    for (int k = 0; k < n; k++)
        for (int i = 0; i < HAT_N; i++) {
            EKey e = edge_key(A[k].v[i], A[k].v[(i + 1) % HAT_N]);
            int lo = 0, hi = m - 1, found = 0;
            while (lo <= hi) {
                int mid = (lo + hi) / 2, r = ekey_cmp(&e, &eb[mid]);
                if (r == 0) { found = 1; break; }
                if (r < 0) hi = mid - 1; else lo = mid + 1;
            }
            if (found) shared++;
        }
    free(eb);
    return shared;
}

/* ---- distinct-vertex covered-angle aggregation -------------------------- */
typedef struct { long kx, ky; double ang; double x, y; } VAng;
static int vang_cmp(const void *p, const void *q) {
    const VAng *a = p, *b = q;
    if (a->kx != b->kx) return a->kx < b->kx ? -1 : 1;
    if (a->ky != b->ky) return a->ky < b->ky ? -1 : 1;
    return 0;
}

/* interior corner angle of hat `h` at vertex i, given polygon orientation sign. */
static double corner_angle(const Hat *h, int i, double areasign) {
    P2 prev = h->v[(i + HAT_N - 1) % HAT_N], cur = h->v[i], next = h->v[(i + 1) % HAT_N];
    double ix = cur.x - prev.x, iy = cur.y - prev.y;   /* incoming edge */
    double ox = next.x - cur.x, oy = next.y - cur.y;   /* outgoing edge */
    double cross = ix * oy - iy * ox, dot = ix * ox + iy * oy;
    double turn = atan2(cross, dot);                   /* exterior turn */
    double interior = M_PI - areasign * turn;
    return interior;                                   /* in (0, 2pi) */
}

static double signed_area2(const Hat *h) {
    double s = 0;
    for (int i = 0; i < HAT_N; i++) {
        P2 a = h->v[i], b = h->v[(i + 1) % HAT_N];
        s += a.x * b.y - b.x * a.y;
    }
    return s;
}

/* ---- SVG output ---------------------------------------------------------- */
static void emit_svg(const char *path, const Hat *copies[3], int n, P2 centre) {
    double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
    for (int c = 0; c < 3; c++)
        for (int k = 0; k < n; k++) {
            const Hat *h = &copies[c][k];
            if (h->minx < minx) minx = h->minx;
            if (h->maxx > maxx) maxx = h->maxx;
            if (h->miny < miny) miny = h->miny;
            if (h->maxy > maxy) maxy = h->maxy;
        }
    const double target = 1000.0, margin = 20.0;
    double w = maxx - minx, h = maxy - miny, span = (w > h) ? w : h;
    if (span <= 0) span = 1;
    double scale = target / span;
    double W = w * scale + 2 * margin, H = h * scale + 2 * margin;

    FILE *o = path ? fopen(path, "w") : stdout;
    if (!o) { fprintf(stderr, "rotsym: cannot open %s\n", path); return; }
    fprintf(o, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\">\n", W, H);
    fprintf(o, "<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>\n");
    const char *fill[3] = { "#5b9bd5", "#f39c12", "#27ae60" };  /* blue/orange/green */
    for (int c = 0; c < 3; c++)
        for (int k = 0; k < n; k++) {
            const Hat *hh = &copies[c][k];
            fputs("<path d=\"", o);
            for (int i = 0; i < HAT_N; i++) {
                double px = (hh->v[i].x - minx) * scale + margin;
                double py = H - ((hh->v[i].y - miny) * scale + margin);
                fprintf(o, "%s%.2f,%.2f", i ? "L" : "M", px, py);
            }
            fprintf(o, " Z\" fill=\"%s\" fill-opacity=\"0.8\" stroke=\"#333\" "
                       "stroke-width=\"0.6\" stroke-linejoin=\"round\"/>\n", fill[c]);
        }
    double cx = (centre.x - minx) * scale + margin;
    double cy = H - ((centre.y - miny) * scale + margin);
    fprintf(o, "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"6\" fill=\"red\"/>\n", cx, cy);
    fputs("</svg>\n", o);
    if (o != stdout) fclose(o);
}
/* ---- per-arrangement characterisation ----------------------------------- */
/* central-pinwheel radius: distance from centre P to the centroid of a base hat
   that has P as a vertex.  This identifies WHICH 120-deg vertex of the hat the
   three copies meet at (each hat vertex has a distinct centroid distance), so it
   distinguishes the different rotationally symmetric arrangements. */
static double center_radius(const Hat *base, int n, P2 P) {
    for (int k = 0; k < n; k++)
        for (int i = 0; i < HAT_N; i++)
            if (fabs(base[k].v[i].x - P.x) < 1e-4 && fabs(base[k].v[i].y - P.y) < 1e-4) {
                double cx = 0, cy = 0;
                for (int j = 0; j < HAT_N; j++) { cx += base[k].v[j].x; cy += base[k].v[j].y; }
                cx /= HAT_N; cy /= HAT_N;
                return hypot(P.x - cx, P.y - cy);
            }
    return -1;
}

typedef struct { P2 c; double radius; int shared; } Arr;
static int arr_cmp(const void *p, const void *q) {
    const Arr *a = p, *b = q;
    long ra = lround(a->radius * 1000), rb = lround(b->radius * 1000);
    if (ra != rb) return ra < rb ? -1 : 1;          /* group by vertex type */
    return b->shared - a->shared;                   /* most interlocking first */
}

/* contact sheet: draw every valid arrangement in a grid cell, labelled with its
   centre and central-pinwheel radius. */
static void emit_contact(const char *path, Hat *base, int n, const Arr *arr, int na) {
    if (na == 0) return;
    int cols = (int)ceil(sqrt((double)na)); if (cols < 1) cols = 1;
    int rows = (na + cols - 1) / cols;
    const double cw = 360.0, ch = 380.0, pad = 10.0, labelh = 22.0;

    FILE *o = fopen(path, "w");
    if (!o) { fprintf(stderr, "rotsym: cannot open %s\n", path); return; }
    fprintf(o, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\">\n",
            cols * cw, rows * ch);
    fprintf(o, "<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>\n");
    const char *fill[3] = { "#5b9bd5", "#f39c12", "#27ae60" };

    Hat *c1 = malloc(sizeof(Hat) * n), *c2 = malloc(sizeof(Hat) * n);
    for (int t = 0; t < na; t++) {
        P2 P = arr[t].c;
        make_copy(base, n, P, 120.0, c1);
        make_copy(base, n, P, 240.0, c2);
        const Hat *copies[3] = { base, c1, c2 };

        double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
        for (int cc = 0; cc < 3; cc++)
            for (int k = 0; k < n; k++) {
                const Hat *h = &copies[cc][k];
                if (h->minx < minx) minx = h->minx;
                if (h->maxx > maxx) maxx = h->maxx;
                if (h->miny < miny) miny = h->miny;
                if (h->maxy > maxy) maxy = h->maxy;
            }
        double w = maxx - minx, h = maxy - miny, span = (w > h) ? w : h;
        if (span <= 0) span = 1;
        double avail = (cw - 2 * pad < ch - 2 * pad - labelh) ? cw - 2 * pad
                                                              : ch - 2 * pad - labelh;
        double scale = avail / span;
        double drawW = w * scale, drawH = h * scale;
        double ox = (t % cols) * cw + (cw - drawW) / 2.0;
        double oy = (t / cols) * ch + pad + (ch - labelh - 2 * pad - drawH) / 2.0;

        for (int cc = 0; cc < 3; cc++)
            for (int k = 0; k < n; k++) {
                const Hat *hh = &copies[cc][k];
                fputs("<path d=\"", o);
                for (int i = 0; i < HAT_N; i++) {
                    double px = ox + (hh->v[i].x - minx) * scale;
                    double py = oy + drawH - (hh->v[i].y - miny) * scale;
                    fprintf(o, "%s%.2f,%.2f", i ? "L" : "M", px, py);
                }
                fprintf(o, " Z\" fill=\"%s\" fill-opacity=\"0.8\" stroke=\"#333\" "
                           "stroke-width=\"0.3\" stroke-linejoin=\"round\"/>\n", fill[cc]);
            }
        double pcx = ox + (P.x - minx) * scale, pcy = oy + drawH - (P.y - miny) * scale;
        fprintf(o, "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"4\" fill=\"red\"/>\n", pcx, pcy);
        fprintf(o, "<text x=\"%.1f\" y=\"%.1f\" text-anchor=\"middle\" font-size=\"13\" "
                   "font-family=\"sans-serif\" fill=\"#222\">#%d  r=%.3f  edges=%d</text>\n",
                (t % cols) * cw + cw / 2.0, (t / cols) * ch + ch - 6.0,
                t + 1, arr[t].radius, arr[t].shared);
    }
    free(c1); free(c2);
    fputs("</svg>\n", o);
    fclose(o);
}

/* ------------------------------------------------------------------------- */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s in.csv [out.svg]\n", argv[0]);
        return 1;
    }
    const char *inpath = argv[1];
    const char *outpath = (argc >= 3) ? argv[2] : NULL;

    FILE *in = fopen(inpath, "r");
    if (!in) { fprintf(stderr, "rotsym: cannot open %s\n", inpath); return 1; }

    Hat *base = malloc(sizeof(Hat) * MAX_HATS);
    int n = 0, line_no = 0;
    char line[512];
    while (fgets(line, sizeof(line), in)) {
        line_no++;
        char *p = line; while (*p == ' ' || *p == '\t') p++;
        if (!(isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.')) continue;
        if (n >= MAX_HATS) { fprintf(stderr, "rotsym: too many hats\n"); break; }
        double x, y; int dir, pt; char refbuf[16];
        if (sscanf(line, "%lf,%lf,%d,%15[^,],%d", &x, &y, &dir, refbuf, &pt) != 5) continue;
        int ref = (refbuf[0] == 'T' || refbuf[0] == 't' || refbuf[0] == '1');
        double W[HAT_N][2];
        real_world(dir, ref, imod(pt - 1, HAT_N), x, y, W);
        for (int i = 0; i < HAT_N; i++) { base[n].v[i].x = W[i][0]; base[n].v[i].y = W[i][1]; }
        bbox(&base[n]);
        n++;
    }
    fclose(in);
    if (n == 0) { fprintf(stderr, "rotsym: no hats found\n"); return 1; }
    printf("loaded %d hats from %s\n", n, inpath);

    /* covered angle per distinct vertex */
    int nv = n * HAT_N;
    VAng *va = malloc(sizeof(VAng) * nv);
    int c = 0;
    for (int k = 0; k < n; k++) {
        double s2 = signed_area2(&base[k]);
        double asign = s2 >= 0 ? 1.0 : -1.0;
        for (int i = 0; i < HAT_N; i++) {
            va[c].x = base[k].v[i].x; va[c].y = base[k].v[i].y;
            va[c].kx = lround(base[k].v[i].x * QSCALE);
            va[c].ky = lround(base[k].v[i].y * QSCALE);
            va[c].ang = corner_angle(&base[k], i, asign);
            c++;
        }
    }
    qsort(va, nv, sizeof(VAng), vang_cmp);

    /* candidate centres: covered angle == 120 deg (within tolerance) */
    const double TARGET = 2.0 * M_PI / 3.0, ATOL = 1e-2;
    P2 *cand = malloc(sizeof(P2) * nv);
    int ncand = 0;
    for (int i = 0; i < nv; ) {
        int j = i; double sum = 0;
        while (j < nv && va[j].kx == va[i].kx && va[j].ky == va[i].ky) { sum += va[j].ang; j++; }
        if (fabs(sum - TARGET) < ATOL) { cand[ncand].x = va[i].x; cand[ncand].y = va[i].y; ncand++; }
        i = j;
    }
    printf("candidate 120-degree corner vertices: %d\n", ncand);

    /* test each candidate; collect EVERY valid rotationally symmetric arrangement */
    Hat *c1 = malloc(sizeof(Hat) * n), *c2 = malloc(sizeof(Hat) * n);
    Arr *arr = malloc(sizeof(Arr) * ncand);
    int na = 0;
    for (int t = 0; t < ncand; t++) {
        P2 P = cand[t];
        make_copy(base, n, P, 120.0, c1);
        make_copy(base, n, P, 240.0, c2);
        int ov = copies_overlap(base, c1, n) || copies_overlap(base, c2, n) ||
                 copies_overlap(c1, c2, n);
        if (ov) continue;
        int sh = shared_edges(base, c1, n);   /* connectivity to copy 1 */
        if (sh <= 0) continue;
        arr[na].c = P; arr[na].shared = sh; arr[na].radius = center_radius(base, n, P);
        na++;
    }

    if (na == 0) {
        printf("RESULT: FAIL - no shared vertex gives an overlap-free, "
               "connected 3-fold cluster.\n");
        free(base); free(va); free(cand); free(c1); free(c2); free(arr);
        return 2;
    }

    /* sort/group by central-pinwheel radius (= which hat vertex the copies meet at) */
    qsort(arr, na, sizeof(Arr), arr_cmp);

    printf("RESULT: PASS - %d distinct rotationally symmetric arrangement(s).\n", na);
    printf("  (radius = distance from centre to a meeting hat's centroid; it is the\n"
           "   signature of which 120-deg hat vertex the three copies share)\n");
    double prev = -1;
    for (int t = 0; t < na; t++) {
        if (fabs(arr[t].radius - prev) > 1e-3) {
            const char *tag = fabs(arr[t].radius - 2.574) < 0.02
                ? "   <-- v[12]-type (matches grow_symmetric2)" : "";
            printf("  --- radius %.3f ---%s\n", arr[t].radius, tag);
            prev = arr[t].radius;
        }
        printf("    #%2d  centre (%9.4f,%9.4f)  shared edges=%3d\n",
               t + 1, arr[t].c.x, arr[t].c.y, arr[t].shared);
    }

    /* contact sheet of all arrangements */
    char sheet[1024];
    if (outpath) {
        snprintf(sheet, sizeof(sheet), "%s", outpath);
        char *dot = strrchr(sheet, '.'); if (dot) *dot = '\0';
        strncat(sheet, "_all.svg", sizeof(sheet) - strlen(sheet) - 1);
    } else {
        snprintf(sheet, sizeof(sheet), "rotsym_all.svg");
    }
    emit_contact(sheet, base, n, arr, na);
    printf("rendered contact sheet of all %d arrangements -> %s\n", na, sheet);

    /* primary single render: prefer the v[12]-type (radius 2.574), else most interlocking */
    int pick = 0; double pickbest = -1;
    for (int t = 0; t < na; t++)
        if (fabs(arr[t].radius - 2.574) < 0.02 && arr[t].shared > pickbest) { pick = t; pickbest = arr[t].shared; }
    if (pickbest < 0) { /* no v[12]-type: fall back to most interlocking overall */
        for (int t = 0; t < na; t++) if (arr[t].shared > pickbest) { pick = t; pickbest = arr[t].shared; }
    }
    make_copy(base, n, arr[pick].c, 120.0, c1);
    make_copy(base, n, arr[pick].c, 240.0, c2);
    const Hat *copies[3] = { base, c1, c2 };
    emit_svg(outpath, copies, n, arr[pick].c);
    printf("rendered arrangement #%d (centre %.4f,%.4f, radius %.3f) -> %s\n",
           pick + 1, arr[pick].c.x, arr[pick].c.y, arr[pick].radius,
           outpath ? outpath : "stdout");

    free(base); free(va); free(cand); free(c1); free(c2); free(arr);
    return 0;
}
