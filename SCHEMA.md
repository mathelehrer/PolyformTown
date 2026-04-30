# SCHEMA

## What this project does
PolyformTown enumerates polyforms by repeatedly attaching a base
boundary tile to existing shapes, canonicalizing each result under
lattice symmetries, and deduplicating by hash. One core pipeline
supports square, triangular, and tetrille-style tagged lattices.

## High-level architecture
- `src/core/cycle.*`: geometry primitives (`Coord`, `Cycle`, `Poly`),
  transforms, orientation rules, and canonicalization.
- `src/core/tile.*`: tile parser plus symmetry-variant generation.
- `src/core/attach.*`: frontier-edge alignment, edge cancellation,
  cycle extraction, and overlap rejection.
- `src/core/boundary.*`: boundary vertex extraction and live/dead
  checks shared by pipelines.
- `src/core/hash.*`: open-addressed hash table for deduping canonical
  polyforms.
- `src/core/vec.*`: dynamic vector used by level expansion loops.
- `src/core/lattice.*` + `src/core/tetrille.*`: lattice-specific
  embedding and tetrille translation/tag rules.
- `src/usage/count_poly.c` / `src/usage/print_poly.c`: edge-attachment
  enumeration drivers.
- `src/throughput/vcomp.*`, `src/usage/count_vcomp.c`,
  `src/usage/print_vcomp.c`:
  vertex-completion enumeration mode.
- `src/usage/imgtable.c`, `src/usage/tile_to_imgtable.c`:
  imgtable-style
  conversion and SVG rendering support.

## Build and checks
- Build: `make`
- Full smoke test: `bash tests/smoke.sh`

## Differential vertex completion state

The vcomp pipeline carries boundary, hidden, and ports as vertex-set
state.  A completion event may contain several tile attachments, but
hidden and ports are committed only when the chosen target vertex
disappears from the boundary.

During a completion event, temporary event-local tile edges are used to
check hidden connectivity and to discover gained ports.
This event-local edge data is required even when aggregate tile
history is not being returned.
`track_tiles` controls persistent tile collection and output; it should
not change acceptance correctness.

## Input data and outputs
- Tile definitions live in `tiles/*.tile`.
- A legacy compatibility sample lives in
  `tests/legacy_monomino.tile`.
- Runtime output prints either count-by-level (`poly_count`) or
  canonical boundary rows (`poly_print` / `vcomp_print`).

## Project process/state files (for contributors)
- `../AGENTS.md` is the runtime entry point for task policy in this
  repo scope.
- Runtime metadata now lives one level above this directory:
  - `../meta/RESET.md` is a restart checklist.
  - `../meta/SUSPEND.md` is an end-of-pass quality gate.
  - `../meta/PERSONAE.md` defines contributor persona/roles.
  - `../meta/LESSONS.md` and `../meta/FUTURES.md` index
    `../meta/memory/` records.
  - `../meta/history/` stores prior planning artifacts.
- These files are workflow scaffolding for maintainers/agents,
  not runtime requirements for building or running enumerators.

### AGENTS.md vs memory files (Codex workflow)
- `../AGENTS.md` is the authoritative task-policy layer for files in
  its scope (how to edit, test, format, and deliver changes).
- The `../meta/memory/` structure preserves project continuity:
  lessons, constraints, and roadmap direction across sessions.
- In practice they are complementary: AGENTS gives immediate
  operating rules; memory preserves longer-term context.
- `../meta/LESSONS.md` is interaction-first and captures durable
  prompt context needed for future work.

## Suggested learning path
1. Read `README.md` for the algorithm overview and expected
   sequence values.
2. Skim `../meta/RESET.md`, `../meta/SUSPEND.md`,
   `../meta/LESSONS.md`, and `../meta/FUTURES.md` to understand
   team process around changes.
3. Trace one `poly_count` level in `src/usage/count_poly.c`.
4. Step through `try_attach_tile_poly` in `src/core/attach.c`.
5. Read `poly_canonicalize_lattice` in `src/core/cycle.c` to learn
   dedupe invariants.
6. Compare `src/usage/count_poly.c` vs
   `src/usage/count_vcomp.c` to see frontier-edge vs
   vertex-completion search styles.
7. Run `bash tests/smoke.sh`, then try small custom tiles to
   observe behavior changes.

## Important invariants to keep in mind
- Outer cycle must be CCW; holes must be CW in canonical form.
- For tetrille, coordinates are tagged (`v in {6,4,3}`) and
  translations must be lifted through the 6-system.
- Symmetry handling is lattice-specific (D4 for square, D6-style
  for triangular/tetrille).
- Only canonicalized polyforms are inserted into hash tables.
