# converter — Python CSV ⇄ C hat-cluster

`converter.c` converts hat-tile clusters between the Python CSV representation
(`objects/hat_tile.py`) and this library's per-hat tetrille-cycle representation
(the `tiles:[...]` blocks in `data/<set>/supertile.dat` and `completions.dat`).

## Usage

```
./bin/converter csv2c [in.csv]            # Python CSV  ->  tiles:[...] cluster
./bin/converter c2csv [in.dat [record]]   # tiles:[...] cluster  ->  Python CSV
```

Reads stdin if no file is given. Run from the project root so `tiles/hat.tile`
is reachable. `record` (1-based) selects which `---[ ]---` record of a `.dat`
to convert (default 1). Build with `make converter`.

### Formats

- **CSV**: one row per hat, header `x,y,dir,ref,pt`.
  - `(x,y)` continuous placement (Blender world frame)
  - `dir` orientation, 0..11 in 30° steps (hats use even `dir`)
  - `ref` reflection flag (`True`/`False`)
  - `pt` 1-indexed pivot vertex (converted on load to `(pt-1)%14`)
- **C cluster**: `tiles:[[(v,x,y),...14...],[...],...]`, each hat a 14-vertex
  cycle of tetrille coords, `v ∈ {3,4,6}` the vertex valence class.

## How it works (the non-obvious parts)

### The real Blender geometry
`_read_codes` is not the whole story. `_instance_hats` / `_hat_mesh` build each
hat's world vertices as

```
world = Rot(theta) * _hat_vertices14(dir_in, ref, piv) + (x, y)
dir_in = ref ? +1 : -1        # FIXED, not the CSV dir
piv    = (pt - 1) mod 14
theta  = dir * 30deg * (ref ? -1 : +1)
```

i.e. the hat mesh is built in a fixed base orientation and the CSV `dir` column
is applied as an *instance rotation* — it is **not** the `dir_in` fed to
`_hat_vertices14`. The converter replicates this exactly (`real_world()`).

### Frame bridge (world ⇄ tetrille scaled embedding)
A world point maps to the C "scaled" integer embedding
(`core/tetrille.c: tetrille_embed_point_scaled`) by the fixed affine map

```
sx = -x - sqrt(3)*y + 3
sy = -x + sqrt(3)*y
```

with inverse `x = (3 - sx - sy)/2`, `y = (sy - sx + 3)/(2*sqrt(3))`.

- The linear part was found by registering a hat at the world origin against the
  matching `hat.tile` variant. `det < 0`: the Blender frame and the C scaled
  embedding have **opposite handedness**. This is harmless — the lattice
  contains both hats and antihats, and the reflection cancels on any round trip.
- The `+3` translation is chosen so the three valence sublattices land on the
  correct cosets, i.e. every hat is an exact 6-step lattice translate of a
  variant. (The naive registration produced a translation off by `(3,3) mod 6`,
  which put `v6` vertices on non-`v6` points — fixed here.)

### Valences come from the template, never invented
The `v3`/`v4`/`v6` sublattices are **nested** (every `v6` point is also a `v4`
and a `v3` point), so a vertex valence cannot be recovered from its position
alone. `csv2c` therefore matches each hat to one of the 12 orientation variants
of `tiles/hat.tile` (which carry the correct valences) and emits that variant,
translated into place. `place_hat_match` aligns by a 6-step lattice translation,
which preserves valences.

## Verification

- The exact example CSV → 12 valid hats; `csv2c → c2csv → csv2c` reproduces the
  identical lattice.
- C-origin round-trips are byte/shape identical for: `h2` supertile (2 hats),
  `h2` completions (10), `h3` supertile (3), `h3` completions level-4 (**88
  hats**), and `sym12` supertile.

## Things to know

- `c2csv` emits `pt=1` canonically. The pivot is a free relabeling that the
  Blender pipeline renders identically, so it will **not** echo back original
  `pt` values (e.g. 3/7/11) — the rendered cluster is the same.
- `csv2c` emits a `tile_count:` + `tiles:` block. To drop it straight into a
  `supertile.dat`-style file, wrap it with the other record fields
  (`---[1]---`, `level:`, `boundary:`, …). The `tiles:` line itself is what the
  library parses.
- Capacity: up to `MAX_HATS` (8192) hats per cluster.
