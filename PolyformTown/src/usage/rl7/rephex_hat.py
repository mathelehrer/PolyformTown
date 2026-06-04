#!/usr/bin/env python3
"""Verified V1 hat depiction backend for rephex.

Pipeline:
  RL7 cells -> refined vertex labels -> ordinary/basic vertex-index lifts
  -> ordinary row domain pruning -> hat placement by forced indexed edges.

This renderer deliberately does not invent a lift for GGG.  It renders every
hat whose placement is forced by the supported indexed-edge component and
reports remaining unsupported/unplaced cells in the audit file.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict, deque
import math
from pathlib import Path
import re
import sys
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

Point = Tuple[float, float]
Pos = Tuple[int, int]
Index = str
Row = Tuple[Index, ...]

DIRS = [(-1, 0), (0, -1), (1, -1), (1, 0), (0, 1), (-1, 1)]
REFINED_ROWS = {
    "H": tuple("AFGHCE"),
    "D0": tuple("AFGHJC"),
    "D1": tuple("AKFHCE"),
}
PROJECTION = {
    "0": "A", "1": "A", "10": "C", "11": "C", "12": "E",
    "3": "F", "5": "F", "6": "G", "7": "H", "8": "H",
    "a": "J", "b": "K",
}


def rotate(row: Sequence[str], k: int) -> Tuple[str, ...]:
    k %= len(row)
    return tuple(row[k:]) + tuple(row[:k])


def canonical_cycle(row: Sequence[str]) -> Tuple[str, ...]:
    return min(rotate(row, k) for k in range(len(row)))


def section(text: str, name: str) -> List[str]:
    tag = f"---[{name}]---"
    if tag not in text:
        raise ValueError(f"missing section {tag}")
    chunk = text.split(tag, 1)[1]
    if "---[" in chunk:
        chunk = chunk.split("---[", 1)[0]
    return [ln.strip() for ln in chunk.splitlines()
            if ln.strip() and not ln.lstrip().startswith("#")]


def cell_kind(state: str) -> Optional[str]:
    if state == "0":
        return "D0"
    if state == "1":
        return "D1"
    if state in ("B", "G"):
        return "H"
    return None  # false center has no refined ordinary tile lift here


def load_cells(path: Path) -> Dict[Pos, Tuple[str, int]]:
    cells: Dict[Pos, Tuple[str, int]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        q, r, state, ori = line.split()[:4]
        cells[(int(q), int(r))] = (state, int(ori))
    return cells


def load_vertex_lifts(path: Path) -> Dict[Tuple[str, ...], Tuple[str, ...]]:
    if not path.exists():
        raise FileNotFoundError(
            f"missing boot-generated depiction conversion table: {path}\n"
            "run `make boot` or `make rl6_print_conversion` after RL5/RL6 data exist"
        )
    lifts: Dict[Tuple[str, ...], Tuple[str, ...]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        left, right = [x.strip() for x in line.split("=", 1)]
        refined = tuple(left)
        indexed = tuple(right.split())
        for k in range(3):
            key = rotate(refined, k)
            val = rotate(indexed, k)
            old = lifts.get(key)
            if old is not None and old != val:
                raise ValueError(f"nonunique lift for {key}: {old} / {val}")
            lifts[key] = val
    return lifts


def load_ordinary_rows(path: Path) -> List[Row]:
    text = path.read_text(encoding="utf-8")
    block = text.split("# Unique hexagons", 1)[1].split("# Edge Matches", 1)[0]
    rows: List[Row] = []
    for line in block.splitlines():
        tokens = line.split("#", 1)[0].split()
        if len(tokens) == 6:
            rows.append(tuple(tokens))
    if len(rows) != 10:
        raise ValueError(f"expected 10 ordinary rows, found {len(rows)}")
    return rows


def load_hat(path: Path) -> List[Point]:
    text = path.read_text(encoding="utf-8")
    cycle = text.split("cycle:\n", 1)[1]
    root3 = math.sqrt(3.0)
    points: List[Point] = []
    for line in cycle.strip().splitlines():
        val, a, b = (int(x) for x in line.split())
        if val == 6:
            points.append((root3 / 2.0 * (a + b), (-a + b) / 2.0))
        elif val == 4:
            points.append((root3 / 4.0 * (a + b), (-a + b) / 4.0))
        elif val == 3:
            points.append((a / root3 + b / (2.0 * root3), b / 2.0))
        else:
            raise ValueError(f"unsupported focus.tile coordinate class {val}")
    return points


def center(pos: Pos) -> Point:
    # Next-revision display convention: vertical reflection from old rephex;
    # CCW stored tile cycles appear CCW on the SVG page.
    q, r = pos
    return math.sqrt(3.0) * (q + 0.5 * r), 1.5 * r


def vertices(pos: Pos) -> Tuple[Point, ...]:
    x, y = center(pos)
    return tuple((round(x + math.cos(math.radians(30 + 60 * k)), 8),
                  round(y - math.sin(math.radians(30 + 60 * k)), 8))
                 for k in range(6))


def ordered_incidents(cells: Dict[Pos, Tuple[str, int]]) -> Dict[Point, Tuple[Tuple[Pos, int], ...]]:
    hits: Dict[Point, List[Tuple[Pos, int]]] = defaultdict(list)
    for pos in cells:
        for slot, vertex in enumerate(vertices(pos)):
            hits[vertex].append((pos, slot))
    ordered: Dict[Point, Tuple[Tuple[Pos, int], ...]] = {}
    for vertex, incident in hits.items():
        if len(incident) not in (2, 3):
            continue
        ordered[vertex] = tuple(sorted(
            incident,
            key=lambda hit: math.atan2(
                -(center(hit[0])[1] - vertex[1]),
                center(hit[0])[0] - vertex[0])))
    return ordered


def adjacent_edges(cells: Dict[Pos, Tuple[str, int]]) -> List[Tuple[Pos, Pos, int, int]]:
    cell_vertices = {pos: vertices(pos) for pos in cells}
    result: List[Tuple[Pos, Pos, int, int]] = []
    for left in sorted(cells):
        for dq, dr in DIRS:
            right = (left[0] + dq, left[1] + dr)
            if right not in cells or left >= right:
                continue
            shared = set(cell_vertices[left]) & set(cell_vertices[right])
            if len(shared) != 2:
                raise RuntimeError(f"adjacent cells do not share one side: {left}/{right}")
            def side(pos: Pos) -> int:
                pts = cell_vertices[pos]
                for k in range(6):
                    if {pts[k], pts[(k + 1) % 6]} == shared:
                        return k
                raise RuntimeError("shared edge has no slot")
            result.append((left, right, side(left), side(right)))
    return result


def cross(a: Point, b: Point, c: Point) -> float:
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def point_in_poly_strict(point: Point, poly: Sequence[Point]) -> bool:
    x, y = point
    inside = False
    for i, a in enumerate(poly):
        b = poly[(i + 1) % len(poly)]
        if abs(cross(a, b, point)) < 1e-9 and min(a[0], b[0]) - 1e-9 <= x <= max(a[0], b[0]) + 1e-9 and min(a[1], b[1]) - 1e-9 <= y <= max(a[1], b[1]) + 1e-9:
            return False
        if (a[1] > y) != (b[1] > y):
            at_x = (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]
            if x < at_x:
                inside = not inside
    return inside


def proper_segment_cross(a: Point, b: Point, c: Point, d: Point) -> bool:
    return (cross(a, b, c) * cross(a, b, d) < -1e-9 and
            cross(c, d, a) * cross(c, d, b) < -1e-9)


def polygons_overlap(poly_a: Sequence[Point], poly_b: Sequence[Point]) -> bool:
    for i, a in enumerate(poly_a):
        b = poly_a[(i + 1) % len(poly_a)]
        for j, c in enumerate(poly_b):
            d = poly_b[(j + 1) % len(poly_b)]
            if proper_segment_cross(a, b, c, d):
                return True
    return point_in_poly_strict(poly_a[0], poly_b) or point_in_poly_strict(poly_b[0], poly_a)


def rigid_fit(source: Sequence[Point], target: Sequence[Point]) -> Tuple[Tuple[float, float, float, float], Point]:
    (sx0, sy0), (sx1, sy1) = source
    (tx0, ty0), (tx1, ty1) = target
    source_angle = math.atan2(sy1 - sy0, sx1 - sx0)
    target_angle = math.atan2(ty1 - ty0, tx1 - tx0)
    angle = target_angle - source_angle
    c, s = math.cos(angle), math.sin(angle)
    rx, ry = sx0 * c - sy0 * s, sx0 * s + sy0 * c
    return (c, -s, s, c), (tx0 - rx, ty0 - ry)


def transform(points: Sequence[Point], transform_data: Tuple[Tuple[float, float, float, float], Point]) -> List[Point]:
    (a, b, c, d), (tx, ty) = transform_data
    return [(a * x + b * y + tx, c * x + d * y + ty) for x, y in points]


def transform_error(points: Sequence[Point], left, right) -> float:
    aa, bb = transform(points, left), transform(points, right)
    return max(math.hypot(x1 - x2, y1 - y2) for (x1, y1), (x2, y2) in zip(aa, bb))


def solve(root: Path, cells: Dict[Pos, Tuple[str, int]], palette: str):
    lift_table = load_vertex_lifts(root / "data/rl6/hat/print_conversion.dat")
    ordinary_rows = load_ordinary_rows(root / "data/rl5/hexagons.dat")
    hat = load_hat(root / "preferences/focus.tile")
    supported = {p: value for p, value in cells.items() if cell_kind(value[0]) is not None}
    false_cells = sorted(set(cells) - set(supported))

    # The direct refined orientation rule validated on the RL7 patches under the
    # corrected CCW display convention.
    refined_rows: Dict[Pos, Tuple[str, ...]] = {}
    for p, (state, orientation) in supported.items():
        refined_rows[p] = rotate(REFINED_ROWS[cell_kind(state)], orientation + 1)

    incidents = ordered_incidents(supported)
    assigned: Dict[Pos, Dict[int, str]] = defaultdict(dict)
    unresolved_figures: List[Tuple[str, Point]] = []
    for vertex, hits in incidents.items():
        if len(hits) != 3:
            continue
        seq = tuple(refined_rows[p][slot] for p, slot in hits)
        indexed = lift_table.get(seq)
        if indexed is None:
            unresolved_figures.append(("".join(seq), vertex))
            continue
        for (p, slot), index in zip(hits, indexed):
            old = assigned[p].get(slot)
            if old is not None and old != index:
                raise ValueError(f"inconsistent ordinary index at {p} slot {slot}: {old}/{index}")
            assigned[p][slot] = index

    # Candidate ordinary rows that agree with projected refined labels and
    # whatever indexed vertices have already been recovered.
    domains: Dict[Pos, List[Tuple[int, int, Row]]] = {}
    for p in supported:
        choices: List[Tuple[int, int, Row]] = []
        for tile_no, row in enumerate(ordinary_rows, start=1):
            for rotation in range(6):
                oriented = rotate(row, rotation)
                if tuple(PROJECTION[x] for x in oriented) != refined_rows[p]:
                    continue
                if all(oriented[slot] == index for slot, index in assigned[p].items()):
                    choices.append((tile_no, rotation, oriented))
        domains[p] = choices

    # Derive allowed ordinary-index triples and boundary ordered-pair support
    # directly from the supported lift table.
    triple_support = set(lift_table.values())
    pair_support = set()
    for triple in triple_support:
        for k in range(3):
            row = rotate(triple, k)
            pair_support.add((row[0], row[1]))
            pair_support.add((row[1], row[0]))

    constraints = [(hits, len(hits)) for hits in incidents.values() if len(hits) in (2, 3)]
    changed = True
    while changed:
        changed = False
        for hits, arity in constraints:
            positions = [p for p, _ in hits]
            supported_opts: List[set] = [set() for _ in positions]
            choices = [domains[p] for p in positions]
            if any(not opts for opts in choices):
                continue
            import itertools
            for options in itertools.product(*choices):
                local = tuple(option[2][slot] for option, (_, slot) in zip(options, hits))
                valid = local in triple_support if arity == 3 else local in pair_support
                if valid:
                    for i, opt in enumerate(options):
                        supported_opts[i].add(opt)
            for p, valid_opts in zip(positions, supported_opts):
                new = [option for option in domains[p] if option in valid_opts]
                if len(new) != len(domains[p]):
                    domains[p] = new
                    changed = True

    domain_empty = sorted(p for p, choices in domains.items() if not choices)
    edges = adjacent_edges(supported)
    adjacency: Dict[Pos, List[Tuple[Pos, int, int]]] = defaultdict(list)
    for p, q, sp, sq in edges:
        adjacency[p].append((q, sp, sq))
        adjacency[q].append((p, sq, sp))

    def forced_numeric_edge(pos: Pos, side: int) -> Optional[Tuple[str, str]]:
        choices = domains[pos]
        if not choices:
            return None
        pairs = {(row[side], row[(side + 1) % 6]) for _, _, row in choices}
        if len(pairs) != 1:
            return None
        pair = next(iter(pairs))
        if "a" in pair or "b" in pair:
            return None
        return pair

    # Grow all connected forced-edge components.  Only the largest can be given
    # a meaningful common placement without an intercomponent anchor.
    components = []
    unused = set(supported)
    while unused:
        seed_candidates = [p for p in unused if any(
            forced_numeric_edge(p, sp) is not None and forced_numeric_edge(q, sq) is not None
            for q, sp, sq in adjacency[p])]
        if not seed_candidates:
            break
        anchor = max(seed_candidates, key=lambda p: len(assigned[p]))
        placements = {anchor: ((1.0, 0.0, 0.0, 1.0), (0.0, 0.0))}
        queue = deque([anchor])
        cycle_conflicts = []
        while queue:
            p = queue.popleft()
            for q, sp, sq in adjacency[p]:
                ep = forced_numeric_edge(p, sp)
                eq = forced_numeric_edge(q, sq)
                if ep is None or eq is None:
                    continue
                actual_edge = transform([hat[int(ep[0])], hat[int(ep[1])]], placements[p])
                proposed = rigid_fit([hat[int(eq[1])], hat[int(eq[0])]], actual_edge)
                if q not in placements:
                    placements[q] = proposed
                    queue.append(q)
                elif transform_error(hat, placements[q], proposed) > 1e-7:
                    cycle_conflicts.append((p, q))
        components.append((placements, cycle_conflicts))
        unused -= set(placements)
    components.sort(key=lambda item: len(item[0]), reverse=True)
    placements, cycle_conflicts = components[0] if components else ({}, [])

    polygons = {p: transform(hat, t) for p, t in placements.items()}
    overlaps = []
    # Spatial bucket audit: avoid quadratic comparisons on level-3+ depictions.
    if polygons:
        all_points = [point for poly in polygons.values() for point in poly]
        width = max(x for x, _ in all_points) - min(x for x, _ in all_points)
        height = max(y for _, y in all_points) - min(y for _, y in all_points)
        bucket_size = max(1.0, math.sqrt((width * height) / max(1, len(polygons))))
        buckets: Dict[Tuple[int, int], List[Pos]] = defaultdict(list)
        bounds: Dict[Pos, Tuple[float, float, float, float]] = {}
        for pos, poly in polygons.items():
            x0 = min(x for x, _ in poly); x1 = max(x for x, _ in poly)
            y0 = min(y for _, y in poly); y1 = max(y for _, y in poly)
            bounds[pos] = (x0, y0, x1, y1)
            for ix in range(math.floor(x0 / bucket_size), math.floor(x1 / bucket_size) + 1):
                for iy in range(math.floor(y0 / bucket_size), math.floor(y1 / bucket_size) + 1):
                    buckets[(ix, iy)].append(pos)
        tested = set()
        for bucket in buckets.values():
            for i, left in enumerate(bucket):
                for right in bucket[i + 1:]:
                    pair = tuple(sorted((left, right)))
                    if pair in tested:
                        continue
                    tested.add(pair)
                    ax0, ay0, ax1, ay1 = bounds[left]
                    bx0, by0, bx1, by1 = bounds[right]
                    if ax1 <= bx0 + 1e-9 or bx1 <= ax0 + 1e-9 or ay1 <= by0 + 1e-9 or by1 <= ay0 + 1e-9:
                        continue
                    if polygons_overlap(polygons[left], polygons[right]):
                        overlaps.append((left, right))

    return {
        "supported": supported, "false_cells": false_cells,
        "refined_rows": refined_rows, "assigned": assigned, "domains": domains,
        "unresolved_figures": unresolved_figures, "domain_empty": domain_empty,
        "placements": placements, "components": components,
        "cycle_conflicts": cycle_conflicts, "overlaps": overlaps,
        "hat": hat, "palette": palette,
    }


def write_svg(path: Path, axiom: str, level: int, result, rotation_step: int = 0) -> None:
    """Write the print SVG as sparse hat geometry only, then atomically install it."""
    placements = result["placements"]
    polygons = {p: transform(result["hat"], t) for p, t in placements.items()}
    if rotation_step % 12:
        angle = math.radians(30.0 * (rotation_step % 12))
        ca, sa = math.cos(angle), math.sin(angle)
        polygons = {p: [(ca * x - sa * y, sa * x + ca * y) for x, y in poly]
                    for p, poly in polygons.items()}
    ordinary = {"0": "#7da9f7", "1": "#ef8d8d", "B": "#79c996", "G": "#79c996"}
    tree = {"0": "#f1d777", "1": "#f5af74", "B": "#c49a76", "G": "#79c996"}
    colors = tree if result["palette"] == "tree" else ordinary
    if polygons:
        points = [point for poly in polygons.values() for point in poly]
        xmin = min(x for x, _ in points); xmax = max(x for x, _ in points)
        ymin = min(y for _, y in points); ymax = max(y for _, y in points)
        margin = max(xmax - xmin, ymax - ymin, 1.0) * 0.035
        view_x = xmin - margin
        view_y = -(ymax + margin)
        view_w = xmax - xmin + 2.0 * margin
        view_h = ymax - ymin + 2.0 * margin
    else:
        view_x, view_y, view_w, view_h = 0.0, 0.0, 1.0, 1.0
    width = 1400.0
    height = max(260.0, width * view_h / view_w)
    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width:.0f}" height="{height:.0f}" viewBox="{view_x:.6f} {view_y:.6f} {view_w:.6f} {view_h:.6f}">',
        f'<rect x="{view_x:.6f}" y="{view_y:.6f}" width="{view_w:.6f}" height="{view_h:.6f}" fill="#ffffff"/>',
    ]
    for pos in sorted(polygons, key=lambda p: (p[1], p[0])):
        state = result["supported"][pos][0]
        fill = colors[state]
        points = " ".join(f"{x:.6f},{-y:.6f}" for x, y in polygons[pos])
        lines.append(f'<polygon points="{points}" fill="{fill}" stroke="#302d29" stroke-width="0.020" fill-rule="evenodd"/>')
    lines.append('</svg>')
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.new')
    tmp.write_text("\n".join(lines) + "\n", encoding="utf-8")
    tmp.replace(path)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path("."))
    ap.add_argument("--cells", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--audit", type=Path, required=True)
    ap.add_argument("--axiom", required=True)
    ap.add_argument("--level", required=True, type=int)
    ap.add_argument("--palette", choices=("ordinary", "tree"), default="ordinary")
    ap.add_argument("--rotation-step", type=int, choices=range(13), default=0)
    args = ap.parse_args()
    root = args.root.resolve()
    cells = load_cells(args.cells)
    result = solve(root, cells, args.palette)
    write_svg(args.output, args.axiom, args.level, result, args.rotation_step)
    placed = len(result["placements"])
    total = len(cells)
    result_status = "complete" if placed == total else "partial"
    domain_hist = Counter(len(v) for v in result["domains"].values())
    with args.audit.open("w", encoding="utf-8") as fp:
        fp.write("rephex --hat-svg audit\n")
        fp.write(f"axiom={args.axiom} level={args.level} palette={args.palette} rotation_step={args.rotation_step} status={result_status}\n")
        fp.write("pipeline=RL7 cells -> refined vertex labels -> ordinary indexed vertex lifts -> row pruning -> forced-edge hat placement\n")
        fp.write(f"cells={total} supported_non_F_cells={len(result['supported'])} false_center_cells={len(result['false_cells'])}\n")
        fp.write(f"placed_hats={placed} placement_components={len(result['components'])}\n")
        fp.write(f"unresolved_vertex_figures={len(result['unresolved_figures'])} empty_row_domains={len(result['domain_empty'])}\n")
        fp.write(f"row_domain_histogram={dict(sorted(domain_hist.items()))}\n")
        fp.write(f"placement_cycle_conflicts={len(result['cycle_conflicts'])} positive_area_overlaps={len(result['overlaps'])}\n")
        if result["unresolved_figures"]:
            fp.write("unresolved_figures:\n")
            for sequence, vertex in result["unresolved_figures"]:
                fp.write(f"  {sequence} at {vertex}\n")
        omitted = sorted(set(cells) - set(result["placements"]))
        if omitted:
            fp.write("omitted_cells:\n")
            for pos in omitted:
                fp.write(f"  {pos} state={cells[pos][0]}\n")
    print(f"hat-status: {result_status} placed={placed}/{total} unresolved_figures={len(result['unresolved_figures'])} conflicts={len(result['cycle_conflicts'])} overlaps={len(result['overlaps'])}")
    return 0 if not result["cycle_conflicts"] and not result["overlaps"] else 1


if __name__ == "__main__":
    sys.exit(main())
