# Polyform tiles

Edge-path representations of lattice polygons

## Filesystem

- `tiles/*.tile`

## Schema

```text
name:    [key value] 
lattice: [key value]

constants:
r = [surds?]

basis:
[valence]:
  [basis vec 1]
  [basis vec 2]
...

cycle:
[v1 x1 y1]
[v1 x2 y2]
...

```

## Notes

- Name and lattice may be used as keys in more difficult cases
- Extra constants allow for symbolic analysis on non-square lattices 
- Basis vectors explain how to project from v1 x1 y1  
- Basis vectors may reference constant r  
- Cycles must have CCW orientation throughout.
- Bootloader only cares about `preferences/focus.tile`
