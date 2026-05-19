#!/usr/bin/env python3
"""
rl5_count_pysat.py

Generate an RL5 SAT CNF with ./bin/rl5_sat and solve/count it using PySAT's
Glucose bindings.

Dependency:
    python3 -m pip install python-sat

Assumes the one-hot encoding emitted by rl5_sat:
    X(cell, oriented_hex)

For counting, each model is blocked by negating the one selected variable for
every cell. This keeps one Glucose solver instance alive and adds blocking
clauses incrementally.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Tuple


def import_pysat():
    try:
        from pysat.formula import CNF
        from pysat.solvers import Glucose3, Glucose4, Glucose42
    except Exception as e:
        print("ERROR: PySAT is not available.", file=sys.stderr)
        print("Install with: python3 -m pip install python-sat", file=sys.stderr)
        print(f"Import error: {e}", file=sys.stderr)
        raise SystemExit(2)
    return CNF, {
        "glucose4": Glucose4,
        "glucose3": Glucose3,
        "glucose42": Glucose42,
    }


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description="Count RL5 hex SAT solutions using PySAT/Glucose."
    )
    ap.add_argument("--model", default="super", choices=["basic", "super", "overlap"])
    ap.add_argument("--record", type=int, default=6)
    ap.add_argument("--boundary-depth", type=int, required=True)
    ap.add_argument("--fixed-center-orientation", action="store_true")
    ap.add_argument("--input", help="explicit hex model data path, passed to rl5_sat")
    ap.add_argument("--tmp-dir", default="tmp")
    ap.add_argument("--prefix", help="file prefix inside tmp-dir")
    ap.add_argument("--rl5-sat", default="./bin/rl5_sat", help="path to rl5_sat")
    ap.add_argument("--solver", default="glucose4",
                    choices=["glucose4", "glucose3", "glucose42"])
    ap.add_argument("--exists-only", action="store_true",
                    help="run one solver pass and report SAT/UNSAT only")
    ap.add_argument("--max-models", type=int, default=0,
                    help="stop after this many models; 0 means no limit")
    ap.add_argument("--print-models", action="store_true",
                    help="print selected (cell, oriented_hex, var) triples")
    ap.add_argument("--skip-generate", action="store_true",
                    help="use existing --cnf and --map instead of running rl5_sat")
    ap.add_argument("--cnf", help="CNF path; default derived from prefix")
    ap.add_argument("--map", dest="map_path", help="map path; default derived from prefix")
    return ap.parse_args()


def run_rl5_sat(args: argparse.Namespace, cnf_path: Path, map_path: Path) -> None:
    cmd = [
        args.rl5_sat,
        "--model", args.model,
        "--record", str(args.record),
        "--boundary-depth", str(args.boundary_depth),
        "--cnf", str(cnf_path),
        "--map", str(map_path),
    ]
    if args.fixed_center_orientation:
        cmd.append("--fixed-center-orientation")
    if args.input:
        cmd.extend(["--input", args.input])

    print("==> generate CNF")
    print(" ".join(cmd))
    proc = subprocess.run(
        cmd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    sys.stdout.write(proc.stdout)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)


def parse_map(map_path: Path) -> Tuple[int, Dict[int, int], Dict[int, int]]:
    """Return (cell_count, var_to_cell, var_to_oriented)."""
    cell_count = None
    var_to_cell: Dict[int, int] = {}
    var_to_oriented: Dict[int, int] = {}

    for raw in map_path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if parts[0] == "cells" and len(parts) >= 2:
            cell_count = int(parts[1])
            continue
        if parts[0].isdigit() and len(parts) >= 5:
            var = int(parts[0])
            cell = int(parts[1])
            oriented = int(parts[4])
            var_to_cell[var] = cell
            var_to_oriented[var] = oriented

    if cell_count is None:
        cell_count = len(set(var_to_cell.values()))

    if cell_count <= 0 or not var_to_cell:
        raise RuntimeError(f"could not parse usable variable map: {map_path}")

    return cell_count, var_to_cell, var_to_oriented


def selected_from_model(model: List[int],
                        var_to_cell: Dict[int, int],
                        var_to_oriented: Dict[int, int],
                        cell_count: int) -> List[int]:
    chosen_by_cell: Dict[int, int] = {}
    for lit in model:
        if lit <= 0:
            continue
        if lit not in var_to_cell:
            continue
        cell = var_to_cell[lit]
        if cell in chosen_by_cell:
            raise RuntimeError(
                f"model selected multiple vars for cell {cell}: "
                f"{chosen_by_cell[cell]} and {lit}"
            )
        chosen_by_cell[cell] = lit

    if len(chosen_by_cell) != cell_count:
        raise RuntimeError(
            f"model selected {len(chosen_by_cell)} cells; expected {cell_count}"
        )

    return [chosen_by_cell[c] for c in sorted(chosen_by_cell)]


def print_selected(selected: List[int],
                   var_to_cell: Dict[int, int],
                   var_to_oriented: Dict[int, int]) -> None:
    print("MODEL")
    for var in selected:
        print(f"  cell={var_to_cell[var]} oriented={var_to_oriented[var]} var={var}")


def main() -> int:
    args = parse_args()
    CNF, solvers = import_pysat()

    tmp_dir = Path(args.tmp_dir)
    tmp_dir.mkdir(parents=True, exist_ok=True)

    if args.prefix:
        prefix = args.prefix
    else:
        tag = "fixed" if args.fixed_center_orientation else "rot"
        prefix = f"{args.model}{args.record}_d{args.boundary_depth}_{tag}"

    cnf_path = Path(args.cnf) if args.cnf else tmp_dir / f"{prefix}.cnf"
    map_path = Path(args.map_path) if args.map_path else tmp_dir / f"{prefix}.map"

    if not args.skip_generate:
        run_rl5_sat(args, cnf_path, map_path)

    cell_count, var_to_cell, var_to_oriented = parse_map(map_path)

    print(f"==> solve with PySAT {args.solver}")
    print(f"cnf={cnf_path}")
    print(f"map={map_path}")
    print(f"cells={cell_count}")

    cnf = CNF(from_file=str(cnf_path))
    Solver = solvers[args.solver]

    count = 0
    with Solver(bootstrap_with=cnf.clauses) as solver:
        if args.exists_only:
            sat = solver.solve()
            print("SAT" if sat else "UNSAT")
            return 0

        while solver.solve():
            model = solver.get_model()
            selected = selected_from_model(
                model,
                var_to_cell,
                var_to_oriented,
                cell_count,
            )
            if args.print_models:
                print_selected(selected, var_to_cell, var_to_oriented)

            solver.add_clause([-v for v in selected])
            count += 1
            print(count, flush=True)

            if args.max_models and count >= args.max_models:
                print(f"STOP max_models={args.max_models}")
                print(f"PARTIAL {count}")
                return 0

    print(f"TOTAL {count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
