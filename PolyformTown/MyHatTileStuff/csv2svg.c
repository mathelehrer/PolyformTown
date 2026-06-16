/*
 * csv2svg.c
 *
 * Render a Python hat-tile cluster (CSV with columns x,y,dir,ref,pt) as an SVG.
 *
 * The hat geometry is computed exactly as the Blender pipeline places it
 * (objects/hat_tile.py), the same way converter.c does:
 *
 *      world = Rot(theta) * _hat_vertices14(dir_in, ref, piv) + (x, y)
 *      dir_in = ref ? +1 : -1
 *      piv    = (pt - 1) mod 14
 *      theta  = dir * 30deg * (ref ? -1 : +1)
 *
 * This file is self-contained (no project headers) so it builds with:
 *
 *      cc -O2 -o bin/csv2svg MyHatTileStuff/csv2svg.c -lm
 *
 * Usage:
 *      ./bin/csv2svg [in.csv [out.svg]]
 *
 * Reads stdin / writes stdout when a path is omitted, e.g.
 *      ./bin/csv2svg from_python/SUPER_SUPER_H7_TILE.dat tile.svg
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#define HAT_N 14

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const double R3 = 1.7320508075688772935;

static int imod(int a, int m) { return ((a % m) + m) % m; }

/* Python _hat_vertices14, ported verbatim (matches converter.c). */
static void hv14(int d, int ref, int pt, double out[HAT_N][2]) {
    static const int raw_len[HAT_N] = {1,1,0,0,1,1,0,0,1,1,0,0,0,0};
    static const int raw_rot[HAT_N] = {-1,1,4,6,3,5,8,6,9,7,10,12,12,14};

    double elen[HAT_N];
    int    erot[HAT_N];
    for (int j = 0; j < HAT_N; j++) {
        int src = ref ? ((pt + (HAT_N - 1 - j)) % HAT_N) : ((pt + j) % HAT_N);
        double L = raw_len[src] ? R3 : 1.0;
        int    rot = raw_rot[src] + d;
        elen[j] = L;
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
        out[i][0] = vx[s];
        out[i][1] = vy[s];
    }
}

/* World vertices of the hat instanced at (x,y), orientation dir, reflection ref,
   0-indexed pivot piv -- exactly as the Blender pipeline places it. */
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

typedef struct { double v[HAT_N][2]; int ref; } Hat;

#define MAX_HATS 8192

int main(int argc, char **argv) {
    const char *inpath  = (argc >= 2) ? argv[1] : NULL;
    const char *outpath = (argc >= 3) ? argv[2] : NULL;

    FILE *in = stdin;
    if (inpath) {
        in = fopen(inpath, "r");
        if (!in) { fprintf(stderr, "csv2svg: cannot open %s\n", inpath); return 1; }
    }

    Hat *hats = malloc(sizeof(*hats) * MAX_HATS);
    if (!hats) { fprintf(stderr, "csv2svg: out of memory\n"); return 1; }
    int n = 0, line_no = 0;
    double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
    char line[512];

    while (fgets(line, sizeof(line), in)) {
        line_no++;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        /* skip blanks, comments and the header row */
        if (!(isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.'))
            continue;
        if (n >= MAX_HATS) {
            fprintf(stderr, "csv2svg: too many hats (max %d)\n", MAX_HATS);
            break;
        }

        double x, y;
        int dir, pt;
        char refbuf[16];
        if (sscanf(line, "%lf,%lf,%d,%15[^,],%d", &x, &y, &dir, refbuf, &pt) != 5) {
            fprintf(stderr, "csv2svg: skipping unparseable line %d\n", line_no);
            continue;
        }
        int ref = (refbuf[0] == 'T' || refbuf[0] == 't' || refbuf[0] == '1');
        int piv = imod(pt - 1, HAT_N);

        real_world(dir, ref, piv, x, y, hats[n].v);
        hats[n].ref = ref;
        for (int i = 0; i < HAT_N; i++) {
            double vx = hats[n].v[i][0], vy = hats[n].v[i][1];
            if (vx < minx) minx = vx;
            if (vx > maxx) maxx = vx;
            if (vy < miny) miny = vy;
            if (vy > maxy) maxy = vy;
        }
        n++;
    }
    if (in != stdin) fclose(in);

    if (n == 0) {
        fprintf(stderr, "csv2svg: no hats found in input\n");
        free(hats);
        return 1;
    }

    /* Map world coords -> SVG pixels: scale to a target size, flip y (SVG y is
       down), add a margin. */
    const double target = 1000.0, margin = 20.0;
    double w = maxx - minx, h = maxy - miny;
    double span = (w > h) ? w : h;
    if (span <= 0) span = 1;
    double scale = target / span;
    double W = w * scale + 2 * margin;
    double H = h * scale + 2 * margin;

    FILE *out = stdout;
    if (outpath) {
        out = fopen(outpath, "w");
        if (!out) { fprintf(stderr, "csv2svg: cannot open %s\n", outpath); free(hats); return 1; }
    }

    fprintf(out,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\">\n",
        W, H);
    fprintf(out, "<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>\n");

    /* Two fills so hats and reflected hats (antihats) are distinguishable. */
    const char *fill_hat  = "#5b9bd5";   /* blue  */
    const char *fill_anti = "#f39c12";   /* orange */

    for (int k = 0; k < n; k++) {
        fputs("<path d=\"", out);
        for (int i = 0; i < HAT_N; i++) {
            double px = (hats[k].v[i][0] - minx) * scale + margin;
            double py = H - ((hats[k].v[i][1] - miny) * scale + margin);
            fprintf(out, "%s%.2f,%.2f", i ? "L" : "M", px, py);
        }
        fprintf(out,
            " Z\" fill=\"%s\" fill-opacity=\"0.85\" stroke=\"#333\" "
            "stroke-width=\"1\" stroke-linejoin=\"round\"/>\n",
            hats[k].ref ? fill_anti : fill_hat);
    }

    fputs("</svg>\n", out);
    if (out != stdout) fclose(out);
    free(hats);

    fprintf(stderr, "csv2svg: rendered %d hats -> %s (%.0fx%.0f)\n",
            n, outpath ? outpath : "stdout", W, H);
    return 0;
}
