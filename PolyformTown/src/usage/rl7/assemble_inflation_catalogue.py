#!/usr/bin/env python3
"""Assemble an RL7 inflation catalogue page from C-rendered SVG panels."""
from __future__ import annotations
import argparse
from pathlib import Path
from xml.sax.saxutils import escape

ROWS = [
    ("D", "D"),
    ("H", "H"),
    ("DH — fixed point", "DH"),
    ("B3 — brown branch C3", "B3"),
    ("L3 — green leaf C3", "L3"),
    ("F — false center", "F"),
]

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir", type=Path)
    ap.add_argument("palette", choices=["ordinary", "tree"])
    args = ap.parse_args()
    out = args.outdir
    palette = args.palette
    w, h = 1820, 3320
    col_w, row_h = 580, 514
    x0, y0 = 30, 122
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',
        '<rect width="100%" height="100%" fill="#f4f1ea"/>',
        f'<text x="{w/2}" y="50" text-anchor="middle" font-family="DejaVu Sans, sans-serif" font-size="30" font-weight="bold" fill="#171717">Mountain and Range — RL7 inflation catalogue ({escape(palette)} palette)</text>',
        f'<text x="{w/2}" y="84" text-anchor="middle" font-family="DejaVu Sans, sans-serif" font-size="17" fill="#555">C-rendered configurations; hex cells use ⬢ orientation without rotating patch placement.</text>',
    ]
    for row, (title, tag) in enumerate(ROWS):
        y = y0 + row * row_h
        parts.append(f'<text x="{x0}" y="{y-12}" font-family="DejaVu Sans, sans-serif" font-size="20" font-weight="bold" fill="#222">{escape(title)}</text>')
        for level, label in enumerate(("axiom", "inflation 1", "inflation 2")):
            x = x0 + level * (col_w + 10)
            name = f'{tag}_inflation{level}_{palette}.svg'
            parts.append(f'<rect x="{x}" y="{y}" width="{col_w}" height="470" rx="10" fill="#fcfbf8" stroke="#d8d3c7"/>')
            parts.append(f'<text x="{x+14}" y="{y+26}" font-family="DejaVu Sans, sans-serif" font-size="15" fill="#555">{escape(label)}</text>')
            panel = (out / name).read_text(encoding="utf-8")
            body = panel.split(">", 1)[1].rsplit("</svg>", 1)[0]
            scale = min((col_w - 8) / 1200.0, 438 / 980.0)
            px = x + (col_w - 1200 * scale) / 2
            py = y + 30
            parts.append(f'<g transform="translate({px:.2f},{py:.2f}) scale({scale:.6f})">{body}</g>')
    parts.append('</svg>')
    dest = out / f'catalogue_{palette}.svg'
    dest.write_text('\n'.join(parts), encoding='utf-8')
    print(dest)
    return 0
if __name__ == '__main__':
    raise SystemExit(main())
