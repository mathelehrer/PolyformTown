# Manual

This directory is an operator-facing manual for current runlevel
tooling.

The structure is method-first rather than binary-first. Pages describe
what to do in plain terms, then map to current commands.

## Methods

- `methods/counting.md`
- `methods/depicting.md`
- `methods/constellations.md`
- `methods/vertex-figures.md`
- `methods/eliminations.md`
- `methods/ring-completion.md`
- `methods/wang-tiles.md`

## Runlevels

- `runlevels/overview.md`
- `runlevels/boot.md`
- `runlevels/data-trust.md`

## Data

- `data/rl0.md`
- `data/rl1.md`

## Appendices

- `appendices/glossary.md`
- `appendices/flags-and-parameters.md`
- `appendices/known-issues.md`

## Notes

- Current boot flow is centered on:
  - `preferences/focus.tile`
  - `data/rl0/completions.dat`
  - `data/rl0/deletions.dat`
  - `preferences/optimize.dat`
- Prefer explicit flags over positional magic parameters.
