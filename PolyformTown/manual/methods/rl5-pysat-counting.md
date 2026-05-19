# RL5 PySAT / Glucose usage

Install dependency:

```bash
python3 -m pip install python-sat
```

Build the CNF emitter:

```bash
make rl5_sat
```

Make the helper executable:

```bash
chmod +x tools/rl5_count_pysat.py
```

## Existence only

This answers: does at least one depth-6 configuration exist?

```bash
tools/rl5_count_pysat.py \
  --model super \
  --record 6 \
  --boundary-depth 6 \
  --fixed-center-orientation \
  --exists-only
```

Expected output is `SAT` or `UNSAT`.

## Count all solutions

```bash
tools/rl5_count_pysat.py \
  --model super \
  --record 6 \
  --boundary-depth 6 \
  --fixed-center-orientation
```

This prints `1`, `2`, `3`, ... as models are blocked, then:

```text
TOTAL N
```

## Stop early

Useful if depth 6 has many solutions:

```bash
tools/rl5_count_pysat.py \
  --model super \
  --record 6 \
  --boundary-depth 6 \
  --fixed-center-orientation \
  --max-models 1000
```

## Use rotations of the center too

Omit `--fixed-center-orientation`:

```bash
tools/rl5_count_pysat.py \
  --model super \
  --record 6 \
  --boundary-depth 3
```

Known relation so far: center rotations allowed gave exactly `6 * fixed` for depths 1-5.

## Use existing CNF/map

Generate once:

```bash
mkdir -p tmp

./bin/rl5_sat \
  --model super \
  --record 6 \
  --boundary-depth 6 \
  --fixed-center-orientation \
  --cnf tmp/super6_d6.cnf \
  --map tmp/super6_d6.map
```

Then solve/count without regenerating:

```bash
tools/rl5_count_pysat.py \
  --boundary-depth 6 \
  --skip-generate \
  --cnf tmp/super6_d6.cnf \
  --map tmp/super6_d6.map
```

## Try a different PySAT Glucose wrapper

```bash
tools/rl5_count_pysat.py \
  --model super \
  --record 6 \
  --boundary-depth 6 \
  --fixed-center-orientation \
  --solver glucose3
```

Available options in the script:

```text
--solver glucose4
--solver glucose3
--solver glucose42
```

## Print decoded selected variables

```bash
tools/rl5_count_pysat.py \
  --model super \
  --record 6 \
  --boundary-depth 2 \
  --fixed-center-orientation \
  --print-models
```

## Known counts from the current CNF encoding

For:

```text
--model super --record 6
```

we already found:

```text
depth   fixed center orientation   center rotations allowed
1       20                         120
2       30                         180
3       720                        4320
4       96                         576
5       1920                       11520
```

Depth 6 is the next important test.
