# Input Tiles Data

This page documents the basic input tile type used by all methods.

## Primary location

- `tiles/*.tile`

## Tile schema

```text
name: <tile name>
lattice: <square|triangular>
cycle:
x y
x y
...
```

## Notes

- Coordinates are integer lattice coordinates.
- Keep outer cycle orientation consistent with project rules.
- Default workflows often use `preferences/focus.tile`.
