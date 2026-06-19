//
// Created by jmartin on 6/19/26.
//
#include <math.h>
#include <stdio.h>

#include "core/attach.h"
#include "core/boundary.h"
#include "core/cycle.h"
#include "core/tile.h"

int main(void)
{
    const char* tile_path = "tiles/hat.tile";
    const char* svg_path = "img/composing.svg";


    Tile tile;
    if (!tile_load(tile_path, &tile))
    {
        fprintf(stderr, "ERROR: cannot load %s\n", tile_path);
        return 1;
    }

    Cycle rot[3];
    rot[0] = tile.base;
    cycle_transform_lattice(&tile.base, &rot[1], tile.lattice, 2);
    cycle_transform_lattice(&tile.base, &rot[2], tile.lattice, 4);

    //check for all possible symmetric arrangments of the hat tile with two of its own rotated copies
    for (int v_center = 0; v_center < tile.base.n; v_center++)
    {
        Coord center = tile.base.v[v_center];

        //Build a polygon form the hats
        Poly poly[3];
        Cycle hats[3];
        hats[0] = rot[0];
        poly[0].cycle_count = 1;
        poly[0].cycles[0] = rot[0];

        Edge edges[512];
        for (int k = 1; k < 3; k++)
        {
            int ok = 0;
            int ec = build_boundary_edges(&poly[k - 1], edges);

            //here we don't have to check all possible cross combinations
            //We want to add clusters along identical vertices and edges
            for (int be = 0; be < ec && !ok; be++)
            {
                Poly out;
                Cycle aligned;
                if (!try_attach_tile_poly_ex(&poly[k - 1], &rot[k], tile.lattice, be, be, &out, &aligned))
                    continue;
                if (out.cycle_count > 1) continue;
                if (!coord_eq(aligned.v[v_center],center)) continue;
                hats[k] = aligned;
                poly[k]=out;
                ok = 1;
            }
            if (!ok)
            {
                fprintf(stderr, "ERROR: cannot attach hat %d with v[%d] at center\n", k, v_center);
                return 1;
            }
        }
    }
}

