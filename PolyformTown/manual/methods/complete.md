# Complete

Use this method to generate completion datasets used by runlevels.

## Inputs

- Seed tile (typically `preferences/focus.tile`)
- Target runlevel and output path
- Optional policy flags (`--live-only`, optimization flags)

## Usage

Generate RL0 completions:

```bash
./bin/rl0_generate [tilefile]
```

Generate RL1 completions from RL0 products:

```bash
./bin/rl1_generate preferences/focus.tile data/rl1/completions.dat 20 \
    --completions data/rl0/completions.dat \
    --deletions data/rl0/deletions.dat \
    --live-only
```

## Output

- RL0: `data/rl0/completions.dat` (+ related metadata)
- RL1: `data/rl1/completions.dat`

## Notes

- Complete RL0 first, then RL1.
- Keep commands and commit hash with every generated artifact.
