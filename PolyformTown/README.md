# PolyformTown V5.0

## Goal

Enumerate lattice polyforms and compare the resulting counts against
reference sequences. The current engine supports square-lattice,
triangular-lattice, and tetrille-oriented tile workflows through
the same edge-cycle framework.

## Schema

The code uses a data structure defined in terms of edge cycles. Each
polygon object consists of an outer boundary cycle and zero or more hole
cycles. The outer boundary is oriented counterclockwise (CCW), and hole
cycles are oriented clockwise (CW).

## Tile file format

Tile files live in `tiles/` and use this schema:

```text
name: <tile name>
lattice: <square|triangular>
cycle:
x y
x y
...
```

Coordinates are integer lattice coordinates. For triangular tiles the
coordinates are stored in a 2-component triangular basis, so geometry
remains exact and no floating point is used internally.

## Tetrille?

The tetrille tiles use tagged vertex systems rather than a single raw
coordinate plane. Vertices carry a type tag `v in {6,4,3}` and
coordinates are interpreted relative to that tag. Valid whole-poly
translations are integer translations in the 6-system, lifted to other
systems by fixed integer formulas. Direct conversions between valence
systems are valid when performed with the correct integer formulas.
Do not mix raw coordinate differences across tags.

## A Typical Procedure

1.   Load a tile boundary from a tile file. This becomes the initial
     data for the growth loop.  
2.   Precompute the distinct symmetry variants of the tile. 

     Enter Loop:   

3.   For each unique polyform in growth data, scan the full boundary 
     (or frontier) across all cycles including holes.
4.   Align frontier edge and precomputed tile edges antiparallel in 
     every possible way, then make them coincident by translation. 
5.   Cancel coincident antiparallel edges and break on coincidnet 
     parallel edges. 
6.   Extract all resulting boundary cycles and classify them as 
     one outer boundary and zero or more holes.
7.   Test interior cycles to reject intersection. A point in hole 
     should not belong to both attachment predecessors. 
8.   Canonicalize the result polyforms under translation, rotation, 
     and reflection in the active lattice.
9.   Scan over all polyforms, inserting unique results to the next 
     growth data level. 
10.  Recurse when current level exhausts, or Break at max depth. 


## Canonicalization

All cycles are transformed together. The outer cycle is the one with
maximum absolute signed area. Canonical form is
`[outer] + sorted(holes)`, with outer oriented CCW and holes CW,
sorted lexicographically. Hashing is performed on this canonical form
so symmetry-equivalent shapes map to one representative and are
counted once. This includes tetrille flows; there is no separate
boundary-anchor canonical rule.

## Build

```bash
make clean
make
```

## Programs

### poly_count

Print the count at each level `1..N`.

`--live-only` filters out states with DEAD boundaries.

```bash
./bin/poly_count N [tilefile] [--live-only]
```

### poly_print

Print the canonical edge form for each shape at level `N`.

```bash
./bin/poly_print N [tilefile] [--live-only]
```

### vcomp_count

Print the vertex-completion count at each level `1..N`.

`--live-only` filters out states with DEAD boundaries.

```bash
./bin/vcomp_count N [tilefile] [--live-only]
```

### vcomp_print

Print the canonical vertex-completion representatives at level `N`.

```bash
./bin/vcomp_print N [tilefile] [--live-only]
```

### rl0_generate

Generate RL0 completion records and write them to
`data/rl0/completions.dat`.  The same run also writes the derived
split/join remembrance dictionary to `data/rl0/remembrance.dat`.
Schema notes are stored in `data/rl0/completions.meta` and `../SCHEMA.md`.

Each record has attributes:
`valence`, `tile_count`, `canonical_boundary`, `tiles`, `indices`.
`tile_count` includes the seed tile plus attached tiles.
Records are emitted as multi-line blocks delimited by `---[N]---`.

```bash
./bin/rl0_generate [tilefile]
```

Quick refresh using defaults:

```bash
make rl0_data
```

Default tile is `preferences/focus.tile`.

Remembrance accounting check:

```bash
make check_rl0_remembrance_counts
```

This regenerates RL0 data and writes level-by-level LHS/RHS counts to
`data/rl0/remembrance_counts.txt`.  The remembrance file groups rows by
key size and prints each alternative value on its own indented line.

### rl0_refine

Refine RL0 deletion state from the saved remembrance dictionary.
By default it reads the saved dictionary from `data/rl0/remembrance.dat`,
uses `data/rl0/completions.dat` only as the seed-record source, reads
`preferences/optimize.dat`, prints progress stats to stdout, and updates the
essential data product `data/rl0/deletions.dat`. It does not write a graph
dump unless `--print` or `--print PATH` is given. With bare `--print`, graph data goes to stdout and the progress table moves to stderr so the graph can be piped directly into the depictor.

```bash
./bin/rl0_refine
./bin/rl0_refine 25 28 36
./bin/rl0_refine 25,28,36
./bin/rl0_refine All
```

Print graph data to stdout and pipe directly into the colored depictor with:

```bash
./bin/rl0_refine --print | ./bin/depict_search0_graph --colors > img/rl0_refine.svg
```

Or build the colored boot graph explicitly with:

```bash
make rl0_refine_svg
```

That target leaves `img/rl0_refine.svg` and does not create a temporary tracked graph file.

### meta dependency graph

The dependency scanner source lives in `src/meta/gen_graph.c`; the binary is
built as `bin/gen_graph`.  A normal `make` refreshes the generated report at
`data/meta/dependencies.txt`.  Do not write generated graph output back into
`src/meta/`.

```bash
make meta_graph_data
```

### rl0_depict

Convert RL0 records to `imgtable` input.

Then pipe to `imgtable` to build one SVG grid.

```bash
DATA=data/rl0/completions.dat
./bin/rl0_depict --data "$DATA" | \
  ./bin/imgtable > rl0.svg
```

Examples:

```bash
DATA=data/rl0/completions.dat
./bin/rl0_depict --data "$DATA" | \
  ./bin/imgtable > img/rl0_all.svg
./bin/rl0_depict --data "$DATA" \
  --limit 144 | \
  ./bin/imgtable > rl0_16x9.svg
./bin/rl0_depict --data "$DATA" \
  --valence 3 --tile-count 3 --grouped | ./bin/imgtable > rl0_v3_n3.svg
./bin/rl0_depict --data "$DATA" \
  --valence 6 --tile-count 3 --grouped | ./bin/imgtable > rl0_v6_n3.svg
./bin/rl0_depict --data "$DATA" \
  --valence 3 6 --tile-count 3 4 \
  --grouped | ./bin/imgtable > rl0_v36_n34.svg
./bin/rl0_depict --data "$DATA" \
  --valence 3 6 --tile-count 3 \
  --grouped --live-only | ./bin/imgtable > rl0_live.svg
```

`--valence` and `--tile-count` accept one or more values per flag,
and flags can be repeated.

`--grouped` emits aggregate + inlaid tiles and center vertex disks.
`--live-only` keeps only completions with live boundaries.

`rl0_depict` deduplicates by
`(valence, tile_count, canonical_boundary)` before emission.

Quick build for the full default RL0 SVG:

```bash
make rl0_svg
```


## RL1 hexagon extraction

`rl1_hexagons` is a read-only RL1 data extraction tool. It parses
`data/rl1/completions.dat`, filters the reflected-central surround case,
groups the remaining RL1 records by mirror-tile placement, and prints compact
cyclic CCW lists of parity-index vertex figures.

Build and run:

```bash
make rl1_hexagons
./bin/rl1_hexagons data/rl1/completions.dat > out/rl1_hexagons.txt
```

The checked-in derived data file is:

```text
data/rl1/hexagons.dat
```

It contains:

- grouped cyclic CCW vertex-figure lists,
- extracted six-label hexagon models,
- unique hexagon label sets,
- compact edge equivalence rules.

The vertex-figure section uses headers like `---[4:11100]---`, where the
bit mask marks which vertex-figure positions are identical across all items in
the group after cyclic alignment.

## QC / smoke test

Run the project smoke test with:

```bash
bash tests/smoke.sh
```

Run the broader regression sweep with:

```bash
bash tests/comprehensive.sh
```

The comprehensive sweep includes `vcomp_parity`, which checks that
tracked and untracked vcomp enumeration produce the same counts for
small domino, chair, and hat cases.

## Count Examples

Run with defaults (monomino, built-in `N`):

```bash
./bin/poly_count
./bin/poly_print
```

### Square lattice examples

```bash
./bin/poly_count 7 tiles/monomino.tile
./bin/poly_count 5 tiles/domino.tile
./bin/poly_count 4 tiles/chair.tile
./bin/poly_count 3 tiles/tetL.tile
```

### Triangular lattice examples

```bash
./bin/poly_count 8 tiles/triangle.tile
./bin/poly_count 6 tiles/hexagon.tile
./bin/poly_count 6 tiles/hh.tile
```

### Tetrille tiling examples

```bash
./bin/poly_count 8 tiles/kite.tile
./bin/vcomp_count 3 tiles/hat.tile
```

## Expected hole counts from print

### Monomino

```bash
./bin/poly_print 7 tiles/monomino.tile | grep '^1 ' | wc -l  -> 1
./bin/poly_print 8 tiles/monomino.tile | grep '^1 ' | wc -l  -> 6
```

### Domino

```bash
./bin/poly_print 4 tiles/domino.tile | grep '^1 ' | wc -l    -> 3
```

### tetL

```bash
./bin/poly_print 2 tiles/tetL.tile | grep '^1 ' | wc -l      -> 1
```

### Chair (not proven)

```bash
./bin/poly_print 3 tiles/chair.tile | grep '^1 ' | wc -l     -> 10
```


## Print format

Each printed polyform encodes both geometric embedding and
topological structure.

General forms:

```text
poly_print:
<0|1> cycle0 | cycle1 | ...

basis / mixed-system output:
[ hole? | constants | basis | outer cycle | hole 1 | ... ]
```

For `poly_print`, the leading flag indicates hole presence:
`0 = no holes`, `1 = one or more holes`. The outer boundary is
printed first; additional cycles represent holes.


## Depict Examples

Canonical tile data can be piped into an SVG converter:

```bash
./bin/vcomp_print 3 tiles/hat.tile | ./bin/imgtable > out.svg; open out.svg
```

Data is streamed into a text-to-SVG converter, saved to a local 
file, and opened. The generator and tile model can be changed 
to visualize different datasets.

`vcomp_print` emits grouped records with `Aggregate`, `Tiles`,
and `Hidden` blocks. `imgtable` renders hidden vertices as black
points and renders `Center` (when present) as a red point.


## OEIS Comparisons

(incomplete, work in progress)

### Square lattice targets

**A000104 (polyominoes without holes)**

```text
n:     0  1  2  3  4   5   6    7    8    9    10
count  1  1  1  2  5  12  35  107  363  1248  4460
```

**A000105 (free polyominoes)**

```text
n:     1  2  3  4   5   6    7    8    9    10    11
count  1  1  2  5  12  35  108  369  1285  4655  17073
```

**A056785 (polydominoes)**

```text
n:     1  2   3    4     5      6
count  1  4  23  211  2227  25824
```

### Triangular lattice targets

**A000577 (polyiamonds)**

```text
n:     1  2  3  4  5   6   7   8
count  1  1  1  3  4  12  24  66
```

**A000228 (polyhexes)**

```text
n:     1  2  3  4   5   6
count  1  1  3  7  22  82
```

**A057782 (polytriamonds / polytraps)**

```text
n:     1  2  3   4    5      6
count  1  9  94  1552 27285 509805
```


## Contributor metadata layout

Agent/process metadata now lives at repo root under `../meta/` to
keep runtime workflow files separate from user-facing docs and source:

- `../meta/RESET.md`, `../meta/SUSPEND.md`, `../meta/PERSONAE.md`
- `../meta/LESSONS.md`, `../meta/FUTURES.md`
- `../meta/memory/` (indexed lesson/future records)
- `../meta/history/` (planning artifacts)

There is no active `../AGENTS.md` in this snapshot; agent-facing workflow state
is currently represented by the `../meta/` files above. If an AGENTS file is
restored later, it should point to those files rather than duplicating stale
workflow rules.

`../meta/LESSONS.md` is interaction-first. Keep titles short in the
index and place detailed prompt context in
`../meta/memory/Lxxxx.txt`.

## Scope

The system now accepts square-lattice and triangular-lattice tiles
in the same edge-cycle framework, and has working tetrille-oriented
tooling and small-count regression coverage for the kite workflow.
Enumeration remains tractable only for small primitives, but the
architecture is no longer limited to square geometry.

## Status

V5.0 suspends in a stable, reproducible state with verified square and
triangular workflows, and tetrille also works to memory limits. 

Completions around a vertex now follow highly constrained logic about 
connected "constellations" of interior vertices. Sets of all vertex 
completions graded by constellation vertex count grow slowly enough
that the primary data of run levels zero and one can likely both be 
found by sifting raw data generated on a personal computer. 

Future directions include memory and performance refinement as well as
analysis of runlevels and their associated constellations. Proofs of
interesting hat properties are close within reach.
