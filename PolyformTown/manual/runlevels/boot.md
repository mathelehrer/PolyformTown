# Boot

## Standard boot sequence

1. Build:

```bash
make clean
make
```

2. Core sanity check:

```bash
./bin/poly_count 10 tiles/kite.tile
```

3. RL0 base generation:

```bash
./bin/rl0_generate
```

4. RL0 refine pass (current form):

```bash
./bin/rl0_refine --tile preferences/focus.tile \
    --hidden-bound 500 --optimized
```

5. RL1 seed generation (current form):

```bash
./bin/rl1_generate preferences/focus.tile data/rl1/completions.dat 20 \
    --completions data/rl0/completions.dat \
    --deletions data/rl0/deletions.dat \
    --live-only
```

## Transition notes

- RL0 boot paths are currently fixed by convention:
  - tile focus at `preferences/focus.tile`
  - RL0 outputs at `data/rl0/completions.dat`
  - RL0 deletion map at `data/rl0/deletions.dat`
- `rl0_refine` remains provisional because its product set is not yet
  fully audited.
