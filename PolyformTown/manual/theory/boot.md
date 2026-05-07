# Boot

## Standard boot sequence

1. Build:

```bash
make clean; make
```

2. Core sanity check:

```bash
./bin/poly_count 10 tiles/kite.tile
```

3. RL0 vertex completions:

```bash
./bin/rl0_generate
```

To Do: needs status printing
To Do: Add intermediary dictionary step

4. RL0 refine pass (current form):

```bash
./bin/rl0_refine --tile preferences/focus.tile \
    --hidden-bound 500 --optimized
```

To Do: needs status printing, needs flag removal

5. RL1 seed generation (current form):

```bash
./bin/rl1_generate preferences/focus.tile data/rl1/completions.dat 20 \
    --completions data/rl0/completions.dat \
    --deletions data/rl0/deletions.dat \
    --live-only
```

To Do: fix status printing, needs flag removal
To Do: Add intermediary dictionary step


## Notes

- By desgin, the boot process creates checkable persistent data files:
  - `preferences/focus.tile`
  - `data/rl0/completions.dat`
  - `data/rl0/deletions.dat`
- These are valuable artifacts whose properties are worth investigating
- However, quality analysis begins to fail after step 3
- Our project goal requires improving quality to a final product
