# RL5 SAT sweep usage

This wrapper checks SAT/UNSAT over many RL5 cases using the existing CNF
emitter and PySAT Glucose.

Install:

```bash
python3 -m pip install python-sat
make rl5_sat
chmod +x tools/rl5_sat_sweep_pysat.py
```

## Check record 6 across all three models

```bash
tools/rl5_sat_sweep_pysat.py \
  --records 6 \
  --depths 1-6 \
  --fixed-center-orientation
```

## Search for the first UNSAT case

```bash
tools/rl5_sat_sweep_pysat.py \
  --models super \
  --records 6 \
  --depths 1-10 \
  --fixed-center-orientation \
  --expect UNSAT \
  --only-matches \
  --stop-on-match
```

If nothing prints, no tested case matched `UNSAT`.

## Test every unique record in one model

```bash
tools/rl5_sat_sweep_pysat.py \
  --models super \
  --records all \
  --boundary-depth 3 \
  --fixed-center-orientation
```

## Test all records in all models for UNSAT

```bash
tools/rl5_sat_sweep_pysat.py \
  --models basic,super,overlap \
  --records all \
  --boundary-depth 3 \
  --fixed-center-orientation \
  --expect UNSAT
```

The `match` column says whether the row matched the requested expectation.

## Output columns

```text
model record depth status cells vars clauses sec match
```

`status` is one of:

```text
SAT
UNSAT
ERROR
```

For this project:

```text
SAT   = at least one bounded hex completion exists
UNSAT = no bounded hex completion exists at that depth
```

## Notes

This is an existence sweep, not a counter. For exact counts, use:

```bash
tools/rl5_count_pysat.py ...
```
