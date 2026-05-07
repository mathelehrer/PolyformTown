# Constrain

Use this method to limit growth to the records you actually want.

## Inputs

- A base generator (`poly_count`, `vcomp_count`, `rl1_generate`)
- A tile file
- Constraint flags (for example `--live-only`)

## Usage

Count only live-boundary states:

```bash
./bin/poly_count N [tilefile] --live-only
./bin/vcomp_count N [tilefile] --live-only
```

Constrain RL1 generation to live RL0 records:

```bash
./bin/rl1_generate preferences/focus.tile data/rl1/completions.dat 20 \
    --completions data/rl0/completions.dat \
    --deletions data/rl0/deletions.dat \
    --live-only
```

## Output

- A reduced output set that excludes non-matching states
- Smaller downstream print/depict datasets

## Notes

- Record every active flag in provenance logs.
- Constraint choices directly affect reported totals.
