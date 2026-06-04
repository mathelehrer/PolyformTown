#!/usr/bin/env python3
"""Generate RL6 ordinary-hat print conversion data from RL5/RL6.

The output is derived, not source input.  It records only refined three-vertex
figures whose ordinary/basic indexed lift is unique in the closed-cycle atlas.
"""
from __future__ import annotations

from collections import defaultdict
from pathlib import Path
import os
import re
import subprocess
import sys
from typing import Dict, Iterable, List, Sequence, Set, Tuple

Row = Tuple[str, ...]


def section(text: str, name: str) -> List[str]:
    tag = f"---[{name}]---"
    if tag not in text:
        raise ValueError(f"missing section {tag}")
    chunk = text.split(tag, 1)[1]
    if "---[" in chunk:
        chunk = chunk.split("---[", 1)[0]
    return [line.strip() for line in chunk.splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


def rotate(row: Sequence[str], k: int) -> Row:
    k %= len(row)
    return tuple(row[k:]) + tuple(row[:k])


def canon(row: Sequence[str]) -> Row:
    return min(rotate(row, k) for k in range(len(row)))


def run_atlas(root: Path, model: str) -> str:
    env = dict(os.environ)
    env["RL6_DUMP_CYCLE_FIGS"] = "1"
    proc = subprocess.run(
        [str(root / "bin" / "rl6_reduce"), "--model", model],
        cwd=root, env=env, text=True, capture_output=True, check=False)
    if proc.returncode:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise RuntimeError(f"rl6_reduce --model {model} failed")
    marker = "---[cycle vertex figures dump]---"
    if marker not in proc.stdout:
        raise RuntimeError("rl6_reduce lacks RL6_DUMP_CYCLE_FIGS support")
    return proc.stdout


BRACKETS = re.compile(r"\[([^\]]+)\]")


def atlas_rows(text: str) -> List[Tuple[Row, Row, Row]]:
    text = text.split("---[cycle vertex figures dump]---", 1)[1]
    rows: List[Tuple[Row, Row, Row]] = []
    for line in text.splitlines():
        if not line.startswith("("):
            continue
        groups = BRACKETS.findall(line)
        if len(groups) != 3:
            continue
        triple = tuple(tuple(pair.split(",")[0] for pair in group.split())
                       for group in groups)
        rows.append(triple)  # type: ignore[arg-type]
    return rows


def main(argv: List[str]) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else Path.cwd().resolve()
    output = (Path(argv[2]) if len(argv) > 2
              else root / "data/rl6/hat/print_conversion.dat")
    if not output.is_absolute():
        output = root / output

    refined_text = (root / "data/rl6/refined_model.dat").read_text(encoding="utf-8")
    unified_text = (root / "data/rl6/unified_model.dat").read_text(encoding="utf-8")
    refined_figures = [tuple(x.strip() for x in line.split(","))
                       for line in section(refined_text, "valid vertex triples")]
    valid_refined = {canon(row) for row in refined_figures}

    index_to_unified: Dict[str, str] = {}
    for line in section(unified_text, "basic"):
        letter, values = line.split(":")
        for value in values.split(","):
            index_to_unified[value.strip()] = letter.strip()
    quotient = {"A":"A", "B":"A", "C":"C", "D":"C", "E":"E",
                "F":"F", "G":"G", "H":"H", "I":"H", "J":"J", "K":"K"}
    index_to_refined = {idx: quotient[letter] for idx, letter in index_to_unified.items()}

    unified_cycles = atlas_rows(run_atlas(root, "unified"))
    basic_cycles = atlas_rows(run_atlas(root, "basic"))

    # Identify the local corner slot from RL6 data itself: it is the only slot
    # for which every unified closed-cycle figure projects to a valid refined figure.
    viable_slots = []
    for slot in range(6):
        if all(canon(tuple(quotient[row[slot]] for row in triple)) in valid_refined
               for triple in unified_cycles):
            viable_slots.append(slot)
    if viable_slots != [1]:
        raise RuntimeError(f"expected unique meeting slot 1; found {viable_slots}")
    slot = viable_slots[0]

    refined_to_unified: Dict[Row, Set[Row]] = defaultdict(set)
    for triple in unified_cycles:
        full = tuple(row[slot] for row in triple)
        refined = canon(tuple(quotient[x] for x in full))
        refined_to_unified[refined].add(canon(full))

    refined_to_basic: Dict[Row, Set[Row]] = defaultdict(set)
    for triple in basic_cycles:
        basic = tuple(row[slot] for row in triple)
        refined = canon(tuple(index_to_refined[x] for x in basic))
        refined_to_basic[refined].add(canon(basic))

    lines = [
        "# Mountain and Range refined vertex -> ordinary/basic hat-index print conversion",
        "# GENERATED DATA: do not edit; regenerate with `make boot` or `make rl6_print_conversion`.",
        "# Inputs: data/rl5/hexagons.dat, data/rl6/unified_model.dat,",
        "#         data/rl6/refined_model.dat, and RL6 closed-cycle atlas computation.",
        "# Cyclic rows are CCW; all cyclic rotations are equivalent.",
        f"# Verified atlas meeting vertex slot = {slot}.",
    ]
    unsupported: List[str] = []
    for refined in sorted(valid_refined):
        fulls = refined_to_unified.get(refined, set())
        basics = refined_to_basic.get(refined, set())
        key = "".join(refined)
        if not fulls or not basics:
            unsupported.append(key)
            continue
        if len(basics) != 1:
            raise RuntimeError(f"ambiguous ordinary indexed lift for {key}: {sorted(basics)}")
        basic = next(iter(basics))
        lines.append(f"{key} = {' '.join(basic)}")
    for key in unsupported:
        lines.append(f"# unresolved: {key}")
    if unsupported != ["GGG"]:
        raise RuntimeError(f"expected only GGG unsupported; found {unsupported}")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(output.relative_to(root))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
