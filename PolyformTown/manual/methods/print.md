# Print

Output canonical polyform data per level

## Definitions

Output the polyforms of tile at a certain level:

```bash
./bin/poly_print [level : integer] [tile : fs pointer] [options]
```

Output the polyform constellations of tile at a certain level:

```bash
./bin/vcomp_print [level : integer] [tile : fs pointer] [options]
```

## Inputs

- level: number of tiles to combine into each polyform
- tile: filesystem pointer to an edge-path tile datum 
- options: 
    --live-only: filters out any configuration with at least one edge 
                 that can not be grown from in the next iteration. 

## Output

- One record per canonical equivalence class at level 

## Examples 

To Do: need good examples for both usages.

## Notes

- Pipe into `imgtable` when you need a fast visual scan.
