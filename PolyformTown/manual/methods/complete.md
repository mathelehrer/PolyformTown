# Complete

Generate completion datasets inherent to each runlevel.

## Definition

Generate all completions around an arbitrary vertex by copies of tile

```bash
./bin/rl0_generate [tile : fs pointer]
```

Generate all completions around tile by copies of tile

```bash
./bin/rl1_generate [tile : fs pointer]
```


## Inputs (implicit)

- tile: datum read from `preferences/focus.tile`
- options: `--live-only` assumed true

## Output

- RL0: `data/rl0/completions.dat` 
- RL1: `data/rl1/completions.dat`

To Do: add another usage for RL1 ring completions

## Notes

- Complete RL0 first and produce lookup dictionaries 
- Vertex filtration is needed for lots of work on RL1
