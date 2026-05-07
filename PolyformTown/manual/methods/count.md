# Count

## Definitions

Ennumerate counts of tile-polyforms on square, hexagonal, or tetrille tilings

```bash
./bin/poly_count [depth : integer] [tile : fs pointer] [options]
```

Ennumerate counts of tile-polyform constellations on square, hexagonal, or tetrille tilings

```bash
./bin/poly_count [depth : integer] [tile : fs pointer] [options]
```

## Inputs

- depth: number of terms to attempt
- tile: filesystem pointer to an edge-path tile datum 
- options: 
    --live-only: filters out any configuration with at least one edge 
                 that can not be grown from in the next iteration. 

## Outputs

- Scrolling counts per level

## Examples 

To Do: Add as many references from OEIS as possible. 
To Do: Validation examples for polyomino constellations

## Notes

- While useful, this method is secondary to print, which gets us shape data 
  for each item in our preferred form, what we actually need.

- It's assumed that the same result can be obtained using print functionality 
  by parsing and piping to "wc -l". This is by design.  

- According to our needs, this function is not by any means time-optimal. 
  It's intended more as a diagnostic to check stats against priors. 

## Hooks

- TODO: update defaults and examples after CLI normalization.
