/*
 * centers.c - two-hat cluster: v1(tip) of hat2 meets v9(notch) of hat1.
 * Outputs h2.svg and tiles/h2.tile.
 *
 * Build: make centers
 * Run:   ./bin/centers [tiles/hat.tile]
 */
#include <math.h>
#include <stdio.h>
#include "core/attach.h"
#include "core/tile.h"
#include "core/boundary.h"

static double r3;

static double px(Coord p)
{
    if (p.v == 6) return r3 * .5 * (p.x + p.y);
    if (p.v == 4) return r3 * .25 * (p.x + p.y);
    return (p.x + .5 * p.y) / r3;
}

static double py(Coord p)
{
    if (p.v == 6) return .5 * (p.y - p.x);
    if (p.v == 4) return .25 * (p.y - p.x);
    return .5 * p.y;
}

static void svg_hat(FILE* f, const Cycle* c, double ox, double oy,
                    double sc, const char* fill, int ldy)
{
    fputs("<path d=\"", f);
    for (int i = 0; i < c->n; i++)
        fprintf(f, "%c%.2f,%.2f", i ? 'L' : 'M', ox + sc * px(c->v[i]), oy - sc * py(c->v[i]));
    fprintf(f, " Z\" fill=\"%s\" stroke=\"#333\" stroke-width=\"1.5\""
            " stroke-linejoin=\"round\"/>\n", fill);
    for (int i = 0; i < c->n; i++)
        fprintf(f, "<text x=\"%.2f\" y=\"%.2f\" font-size=\"7\""
                " text-anchor=\"middle\" fill=\"#111\">%d</text>\n",
                ox + sc * px(c->v[i]), oy - sc * py(c->v[i]) + ldy, i);
}

int main(int argc, char** argv)
{
    const char* tp = argc > 1 ? argv[1] : "tiles/hat.tile";
    Tile tile;
    if (!tile_load(tp, &tile))
    {
        fprintf(stderr, "load %s\n", tp);
        return 1;
    }
    r3 = sqrt(3.0);

    /* hat1 = base; attach hat2 (unreflected) so v[1](tip) lands on v[9](notch).
     * be=8: boundary edge v[8]->v[9]; te=1: variant v[1] aligns to edge endpoint. */
    Poly hat1 = {.cycle_count = 1};
    hat1.cycles[0] = tile.base;

    Poly merged;
    Cycle hat2;
    if (!try_attach_tile_poly_ex(&hat1, &tile.variants[0], tile.lattice,
                              8, 1, &merged, &hat2) || merged.cycle_count != 1)
    { fputs("attachment failed\n", stderr); return 1; }

    /* find the boundary edge of merged that is hat2's v[8]->v[9]*/
    Edge edges2[512];
    int ec2 = build_boundary_edges(&merged, edges2);
    int be2 = -1;
    for (int i = 0; i < ec2; i++)
        if (coord_eq(edges2[i].a, hat2.v[8]) && coord_eq(edges2[i].b, hat2.v[9]))
        {
            be2 = i;
            break;
        }
    if (be2 < 0)
    {
        fputs("notch of hat2 not on boundary\n", stderr);
        return 1;
    }

    /* attache hat3 into hat2's notch, same variant search as before */
    Poly merged2;
    Cycle hat3;
    if (!try_attach_tile_poly_ex(&merged, &tile.variants[0], tile.lattice,
                               be2, 1, &merged2, &hat3) || merged2.cycle_count != 1)
    { fputs("attachment failed\n", stderr); return 1; }

    /* bounding box over both hats */
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (int k = 0; k < 3; k++)
    {
        const Cycle* c = k==0 ? &tile.base : k==1? &hat2: & hat3;
        for (int i = 0; i < c->n; i++)
        {
            double x = px(c->v[i]), y = py(c->v[i]);
            x0 = x < x0 ? x : x0;
            x1 = x > x1 ? x : x1;
            y0 = y < y0 ? y : y0;
            y1 = y > y1 ? y : y1;
        }
    }
    const double SC = 40., M = 20.;
    double W = SC * (x1 - x0) + 2 * M, H = SC * (y1 - y0) + 2 * M;
    double ox = M - SC * x0, oy = H - M + SC * y0;

    /* SVG export */
    FILE* f = fopen("h3.svg", "w");
    fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\""
            " width=\"%.0f\" height=\"%.0f\">\n"
            "<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>\n", W, H);
    svg_hat(f, &tile.base, ox, oy, SC, "#f39c12", -3); /* hat1: labels above */
    svg_hat(f, &hat2, ox, oy, SC, "#e67e22", 8); /* hat2: labels below */
    svg_hat(f, &hat3, ox, oy, SC, "#e67442", 8); /* hat2: labels below */
    Coord sv = tile.base.v[9]; /* shared vertex: hat1's notch = hat2's tip */
    fprintf(f, "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"5\" fill=\"red\"/>\n</svg>\n",
            ox + SC * px(sv), oy - SC * py(sv));
    fclose(f);

    /* h2.tile: merged boundary of the two-hat cluster */
    const Cycle* out = &merged2.cycles[0];
    f = fopen("tiles/h3.tile", "w");
    fprintf(f, "name: h7\nlattice: tetrille\n\nconstants:\nr = sqrt(3)\n\n"
            "basis:\n6:\n  r/2 -1/2\n  r/2  1/2\n\n"
            "4:\n  r/4 -1/4\n  r/4  1/4\n\n"
            "3:\n  1/r      0\n  1/(2*r)  1/2\n\ncycle:\n");
    for (int i = 0; i < out->n; i++)
        fprintf(f, "%d %d %d\n", out->v[i].v, out->v[i].x, out->v[i].y);
    fclose(f);

    fprintf(stderr, "h3.svg  tiles/h3.tile  (%d verts in merged boundary)\n",
            out->n);
    return 0;
}
