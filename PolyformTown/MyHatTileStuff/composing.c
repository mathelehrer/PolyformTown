/*
 * composing.c - three hat tiles sharing vertex 12 (v[12] = (3,1,-1)).
 *
 * Loads preferences/focus.tile, places the base hat, then attaches the 120°
 * and 240° rotations so all three share v[12].  Writes img/composing.svg.
 *
 * Build:  make composing
 * Run:    ./bin/composing
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/attach.h"
#include "core/boundary.h"
#include "core/cycle.h"
#include "core/tile.h"

#define CENTER_VERTEX 12
#define NUM_HATS      3

/* --------------------------------------------------------- SVG helpers */

static double g_r3;

static double svgx(Coord p) {
    if (p.v == 6) return g_r3 * 0.5  * (p.x + p.y);
    if (p.v == 4) return g_r3 * 0.25 * (p.x + p.y);
    return (p.x + 0.5 * p.y) / g_r3;
}
static double svgy(Coord p) {
    if (p.v == 6) return 0.5  * (p.y - p.x);
    if (p.v == 4) return 0.25 * (p.y - p.x);
    return 0.5 * p.y;
}

static void draw_hat(FILE *f, const Cycle *c,
                     double ox, double oy, double sc,
                     const char *fill, const char *stroke, double sw) {
    fputs("<path d=\"", f);
    for (int i = 0; i < c->n; i++)
        fprintf(f, "%c%.2f,%.2f",
                i ? 'L' : 'M',
                ox + sc * svgx(c->v[i]),
                oy - sc * svgy(c->v[i]));
    fprintf(f, " Z\" fill=\"%s\" stroke=\"%s\" stroke-width=\"%.2f\""
               " stroke-linejoin=\"round\" fill-opacity=\"0.75\"/>\n",
            fill, stroke, sw);
}

/* ---------------------------------------------------------------- main */

int main(void) {
    const char *tile_path = "tiles/hat.tile";
    const char *svg_path  = "img/composing.svg";

    Tile tile;
    if (!tile_load(tile_path, &tile)) {
        fprintf(stderr, "ERROR: cannot load %s\n", tile_path);
        return 1;
    }

    /* Base hat and its 120° / 240° rotations around the origin. */
    Cycle rot[NUM_HATS];
    rot[0] = tile.base;
    cycle_transform_lattice(&tile.base, &rot[1], tile.lattice, 2);
    cycle_transform_lattice(&tile.base, &rot[2], tile.lattice, 4);

    Coord center = tile.base.v[CENTER_VERTEX];

    /* Build a polygon from hat 0, then attach hats 1 and 2 so that their
       v[CENTER_VERTEX] lands exactly on 'center'. */
    Poly  poly[NUM_HATS];
    Cycle hats[NUM_HATS];
    hats[0]             = rot[0];
    poly[0].cycle_count = 1;
    poly[0].cycles[0]   = rot[0];

    Edge edges[512];
    for (int k = 1; k < NUM_HATS; k++) {
        int ok = 0;
        int ec = build_boundary_edges(&poly[k-1], edges);
        for (int be = 0; be < ec && !ok; be++) {
            for (int te = 0; te < rot[k].n && !ok; te++) {
                Poly  out;
                Cycle aligned;
                if (!try_attach_tile_poly_ex(&poly[k-1], &rot[k], tile.lattice,
                                             be, te, &out, &aligned))
                    continue;
                if (out.cycle_count > 1)                         continue;
                if (!coord_eq(aligned.v[CENTER_VERTEX], center)) continue;
                hats[k] = aligned;
                poly[k] = out;
                ok = 1;
            }
        }
        if (!ok) {
            fprintf(stderr,
                    "ERROR: cannot attach hat %d with v[%d] at center\n",
                    k, CENTER_VERTEX);
            return 1;
        }
    }

    /* Bounding box over all three tiles. */
    g_r3 = sqrt(3.0);
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (int k = 0; k < NUM_HATS; k++) {
        for (int i = 0; i < hats[k].n; i++) {
            double x = svgx(hats[k].v[i]), y = svgy(hats[k].v[i]);
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    }

    const double SC = 50.0, M = 24.0;
    double W  = 2*M + SC*(x1 - x0);
    double H  = 2*M + SC*(y1 - y0);
    double ox = M - SC*x0;
    double oy = M + SC*y1;

    static const char *fills[]   = {"#f39c12", "#e74c3c", "#8e44ad"};
    static const char *strokes[] = {"#b7770d", "#a93226", "#6c3483"};

    FILE *f = fopen(svg_path, "w");
    if (!f) { perror(svg_path); return 1; }

    fprintf(f,
            "<svg xmlns=\"http://www.w3.org/2000/svg\""
            " width=\"%.0f\" height=\"%.0f\">\n"
            "<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>\n",
            W, H);

    for (int k = 0; k < NUM_HATS; k++)
        draw_hat(f, &hats[k], ox, oy, SC, fills[k], strokes[k], 1.5);

    /* Shared center vertex marker. */
    fprintf(f,
            "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"5\""
            " fill=\"#c0392b\" stroke=\"white\" stroke-width=\"1.5\"/>\n",
            ox + SC*svgx(center), oy - SC*svgy(center));

    /* Vertex-12 label. */
    fprintf(f,
            "<text x=\"%.2f\" y=\"%.2f\" text-anchor=\"middle\""
            " font-size=\"11\" font-family=\"sans-serif\" fill=\"#222\""
            " dy=\"-8\">v[12]</text>\n",
            ox + SC*svgx(center), oy - SC*svgy(center));

    fputs("</svg>\n", f);
    fclose(f);

    fprintf(stderr, "Wrote %s\n", svg_path);
    return 0;
}
