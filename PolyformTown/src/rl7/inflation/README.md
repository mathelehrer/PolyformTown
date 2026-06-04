# RL7 Mountain and Range inflation inspector

This directory contains the C implementation of the Mountain and Range
inflation inspection layer. Configurations are built from symbolic addresses,
checked for cell overlaps, and depicted only from computed cells. The original
handoff material was used as a work source, not as proof of the rules.

## Axiom codes

The stable command/data codes are:

- `D`: single dimer.
- `H`: single H. At level zero it is represented as an unclassified green H;
  inflation produces branch/leaf classes.
- `DH`: fixed-point straight-row seed. `D` occupies two adjacent cells and `H`
  occupies the third cell in the same row: `D0@e | H0@H3` in the current
  axial convention.
- `B3`: brown / branch C3 seed, with verified indexed shift `+2 mod 6`:
  `B4@e, B0@D1K3, B2@D2K4`.
- `L3`: green / leaf C3 seed, with verified indexed shift `+2 mod 6`:
  `G3@e, G5@D0K2K3, G1@D1K3K4`.
- `F`: false-center generator. It begins as one persistent false-center cell
  and inflates by `F -> F + {B_i@g_i : i=0..5}`. The surrounding ring then
  inflates recursively while the center persists.

The `B3` and `L3` records were recovered by searching monochromatic vertex
figures with the required CCW index shift and checking bounded short words
against the recovered target patches. Further reduction by scale-dependent
symbolic syzygies remains unchecked and is not applied.

## Intended inspector flow

The primary interface is the interactive one-screen inspector:

```sh
make rephex
./bin/rephex
```

The REPL follows the existing HCE one-screen terminal/input pattern rather than introducing a separate UI framework.

Commands in the REPL are `A` to choose an axiom, `A H 2` to jump directly
to an axiom and depth, a bare number to set the depth, `+`/`-` to adjust it,
`P [n]` to print/open the hat SVG, `PH [n]` to print/open the hex SVG, `T`
to switch ordinary/tree palettes, and `Q` to quit.  The optional `n` rotates
SVG output by `n * 30` degrees (`0` through `12`, default `0`). Selecting a
new axiom without a depth resets depth to zero.

The batch printer remains available as `rephex_print`:

```sh
./bin/rephex_print DH 2
./bin/rephex_print B3 1 --palette tree
./bin/rephex_print H 2 --palette tree --hat-svg
```

Terminal depiction is the default output mode for `rephex_print`. The ordinary
palette is the default palette: green H cells and blue/red dimer halves.
`--palette tree` shows brown/green branch/leaf H cells with yellow/orange
dimer halves.

The REPL computes the current symbolic pattern once and overwrites
`data/run/rl7/rephex/current.dat`. Printing reads that saved pattern rather
than recomputing inflation. Hat placement/audit status is overwritten in
`data/run/rl7/rephex/current_hat.dat`.

`--svg` writes and opens `img/rl7/rephex/current.svg`; `--hat-svg` writes and
opens `img/rl7/rephex/current_hat.svg`. Both are full-pattern, white-background
prints containing only the depicted geometry, palette fills, and ordinary
outlines. The terminal preview alone is cropped to the rows available within
the fixed 24-by-80 REPL frame. SVG output is installed atomically so a viewer
does not open a partially rewritten file.

The terminal printer and SVG display use the reflected/CCW convention required
by the RL5/RL6 cyclic tile data.

## Verification and optional review output

```sh
./bin/rephex_print --check
./bin/rephex_print --write-axioms data/rl7/inflation/axioms.dat
src/usage/rl7/render_inflation_gallery.sh img/rl7/inflation
```

The optional gallery script writes per-axiom SVG/PNG depictions and ANSI/plain
terminal samples for levels 0, 1, and 2. No CA search is included in this
step.

`--check` validates the inherited surrounding-ring generator checkpoint
counts (`6, 48, 336, 2310, 15840, 108576`). The public `F` axiom includes
the additional persistent false-center cell: it has `1`, `7`, and `49` cells
at levels `0`, `1`, and `2`.
