# Print

Use this method when you need canonical representatives at one level.

## Inputs

- Level `N`
- Tile file (optional; defaults from binary)
- Optional filter flags (`--live-only`)

## Usage

Canonical polyform boundaries:

```bash
./bin/poly_print N [tilefile] [--live-only]
```

Vertex-completion representatives:

```bash
./bin/vcomp_print N [tilefile] [--live-only]
```

## Output

- One canonical record per equivalence class at level `N`
- Hole marker and cycle payload for `poly_print`
- Aggregate/tiles/hidden blocks for `vcomp_print`

## Notes

- Prefer this method when count totals are known but structure is not.
- Pipe into `imgtable` when you need a fast visual scan.
