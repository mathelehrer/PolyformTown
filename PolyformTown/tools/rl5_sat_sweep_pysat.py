#!/usr/bin/env python3
"""
rl5_sat_sweep_pysat.py

Sweep RL5 SAT existence checks with PySAT/Glucose.

This wrapper is for questions like:

  Which records are UNSAT at depth d?
  Does record 6 stay SAT across basic/super/overlap?
  Stop when a SAT or UNSAT case is found.

It calls ./bin/rl5_sat for each requested (model, record, depth), reads the
generated CNF, solves it with PySAT's Glucose wrapper, and prints a compact
table.

Dependency:
  python3 -m pip install python-sat
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterable, List, Tuple


MODEL_DEFAULTS = {
    "basic": "data/rl5/hexagons.dat",
    "super": "data/rl5/supertile_hexagons.dat",
    "overlap": "data/rl5/overlap_supertile_hexagons.dat",
}


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
        description="Sweep RL5 SAT/UNSAT checks using PySAT Glucose."
    )
    ap.add_argument("--models", default="basic,super,overlap",
                    help="comma list from basic,super,overlap; default all")
    ap.add_argument("--records", default="6",
                    help="record list/ranges, e.g. 6 or 1-9 or all; default 6")
    ap.add_argument("--boundary-depth", type=int, action="append",
                    help="depth to test; may be repeated")
    ap.add_argument("--depths",
                    help="depth list/ranges, e.g. 1-6,8; alternative to --boundary-depth")
    ap.add_argument("--fixed-center-orientation", action="store_true")
    ap.add_argument("--solver", default="glucose4",
                    choices=["glucose4", "glucose3", "glucose42"])
    ap.add_argument("--expect", choices=["SAT", "UNSAT"],
                    help="mark whether each row matches this expected status")
    ap.add_argument("--only-matches", action="store_true",
                    help="print only rows matching --expect")
    ap.add_argument("--stop-on-match", action="store_true",
                    help="stop after the first row matching --expect")
    ap.add_argument("--stop-on-mismatch", action="store_true",
                    help="stop after the first row not matching --expect")
    ap.add_argument("--tmp-dir", default="tmp/rl5_sat_sweep")
    ap.add_argument("--rl5-sat", default="./bin/rl5_sat")
    ap.add_argument("--keep-cnf", action="store_true",
                    help="keep generated CNF/map files")
    return ap.parse_args()


def parse_int_list(spec: str) -> List[int]:
    out: List[int] = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            a = int(a)
            b = int(b)
            step = 1 if a <= b else -1
            out.extend(range(a, b + step, step))
        else:
            out.append(int(part))
    return out


def count_records(model: str) -> int:
    path = Path(MODEL_DEFAULTS[model])
    if not path.exists():
        raise RuntimeError(f"missing model data: {path}; run make boot RL=6")

    count = 0
    in_unique = False
    for raw in path.read_text().splitlines():
        line = raw.rstrip()
        stripped = line.strip()

        # Supported sections include both:
        #   # Unique hexagons
        #   Unique Supertile Hexagons
        # Basic rows look like:
        #   0 3 6 8 11 12  # records 1 9
        # Super/overlap rows look like:
        #   1  [[v3],...]  # records 1 2 3 4
        if "Unique" in line and ("Hexagon" in line or "hexagon" in line):
            in_unique = True
            continue

        if in_unique:
            if not stripped:
                continue
            if "Edge Matches" in line or "Edge Rules" in line or "Rules" in line:
                break
            if stripped.startswith("#"):
                continue
            if "# records" in line:
                count += 1

    if count <= 0:
        raise RuntimeError(f"could not count unique hexagon records in {path}")
    return count


def parse_depths(args: argparse.Namespace) -> List[int]:
    depths: List[int] = []
    if args.boundary_depth:
        depths.extend(args.boundary_depth)
    if args.depths:
        depths.extend(parse_int_list(args.depths))
    if not depths:
        raise SystemExit("ERROR: provide --boundary-depth N or --depths LIST")
    return sorted(dict.fromkeys(depths))


def parse_models(args: argparse.Namespace) -> List[str]:
    models = [m.strip() for m in args.models.split(",") if m.strip()]
    bad = [m for m in models if m not in MODEL_DEFAULTS]
    if bad:
        raise SystemExit(f"ERROR: unknown model(s): {','.join(bad)}")
    return models


def records_for_model(spec: str, model: str) -> List[int]:
    if spec.strip() == "all":
        return list(range(1, count_records(model) + 1))
    return parse_int_list(spec)


def run_rl5_sat(rl5_sat: str,
                model: str,
                record: int,
                depth: int,
                fixed: bool,
                cnf: Path,
                map_path: Path) -> Tuple[int, str]:
    cmd = [
        rl5_sat,
        "--model", model,
        "--record", str(record),
        "--boundary-depth", str(depth),
        "--cnf", str(cnf),
        "--map", str(map_path),
    ]
    if fixed:
        cmd.append("--fixed-center-orientation")

    proc = subprocess.run(
        cmd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    return proc.returncode, proc.stdout


def parse_stats(text: str) -> Tuple[str, str, str]:
    cells = vars_ = clauses = "?"
    m = re.search(r"\bcells=(\d+)\b", text)
    if m:
        cells = m.group(1)
    m = re.search(r"\bvars=(\d+)\b", text)
    if m:
        vars_ = m.group(1)
    m = re.search(r"\bclauses=(\d+)\b", text)
    if m:
        clauses = m.group(1)
    return cells, vars_, clauses


def solve_cnf(CNF, Solver, cnf: Path) -> str:
    formula = CNF(from_file=str(cnf))
    with Solver(bootstrap_with=formula.clauses) as solver:
        return "SAT" if solver.solve() else "UNSAT"


def print_header(expect: str | None) -> None:
    cols = [
        "model", "record", "depth", "status",
        "cells", "vars", "clauses", "sec",
    ]
    if expect:
        cols.append("match")
    print(" ".join(f"{c:>10}" for c in cols))


def print_row(model: str,
              record: int,
              depth: int,
              status: str,
              cells: str,
              vars_: str,
              clauses: str,
              sec: float,
              expect: str | None) -> None:
    vals = [
        model,
        str(record),
        str(depth),
        status,
        cells,
        vars_,
        clauses,
        f"{sec:.3f}",
    ]
    if expect:
        vals.append("yes" if status == expect else "no")
    print(" ".join(f"{v:>10}" for v in vals), flush=True)


def main() -> int:
    args = parse_args()
    CNF, solvers = import_pysat()
    Solver = solvers[args.solver]

    models = parse_models(args)
    depths = parse_depths(args)

    tmp = Path(args.tmp_dir)
    tmp.mkdir(parents=True, exist_ok=True)

    print_header(args.expect)

    any_mismatch = False

    for model in models:
        records = records_for_model(args.records, model)
        for record in records:
            for depth in depths:
                tag = "fixed" if args.fixed_center_orientation else "rot"
                stem = f"{model}_r{record}_d{depth}_{tag}"
                cnf = tmp / f"{stem}.cnf"
                map_path = tmp / f"{stem}.map"

                t0 = time.time()
                rc, out = run_rl5_sat(
                    args.rl5_sat,
                    model,
                    record,
                    depth,
                    args.fixed_center_orientation,
                    cnf,
                    map_path,
                )
                if rc != 0:
                    cells, vars_, clauses = parse_stats(out)
                    status = "ERROR"
                    sec = time.time() - t0
                else:
                    cells, vars_, clauses = parse_stats(out)
                    try:
                        status = solve_cnf(CNF, Solver, cnf)
                    except Exception as e:
                        status = "ERROR"
                        out += f"\nsolver error: {e}\n"
                    sec = time.time() - t0

                matches = (args.expect is None or status == args.expect)
                if args.expect and not matches:
                    any_mismatch = True

                if not args.only_matches or matches:
                    print_row(model, record, depth, status, cells, vars_, clauses, sec, args.expect)

                if not args.keep_cnf:
                    cnf.unlink(missing_ok=True)
                    map_path.unlink(missing_ok=True)

                if args.expect and matches and args.stop_on_match:
                    return 0
                if args.expect and not matches and args.stop_on_mismatch:
                    return 1

    if args.expect and any_mismatch:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
