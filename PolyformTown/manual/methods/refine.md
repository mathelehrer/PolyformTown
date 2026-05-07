# Refine

Eliminate false positives from en gros data

## Definitions

Run RL0 refine and keep the resulting deletion decisions:

```bash
./bin/rl0_refine --tile preferences/focus.tile \
    --hidden-bound 500 --optimized
```

Validate that deletion logic still matches expected behavior:

```bash
bash tests/validate_rl0_parity.sh
```

## Inputs

- RL0 completion data
- RL0 deletion map
- Optional refine parameters



## Inputs (implicit)

- tile: datum read from `preferences/focus.tile`
- options: `--live-only` assumed true

## Output

- Updated deletion decisions for RL0-derived workflows
- Reduced candidate set for RL1 seed generation

## Notes

- `rl0_refine` is still provisional; track provenance carefully.
- Re-run parity checks after changing elimination policy.
