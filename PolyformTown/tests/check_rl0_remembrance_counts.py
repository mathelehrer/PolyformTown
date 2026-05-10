#!/usr/bin/env python3
import re
import sys
from collections import defaultdict

if len(sys.argv) != 3:
    print(f"usage: {sys.argv[0]} REMEMBRANCE OUT", file=sys.stderr)
    sys.exit(2)

src, out = sys.argv[1], sys.argv[2]
level = None
rows = defaultdict(int)
values = defaultdict(int)
current_level = None
current_row = False
with open(src, 'r', encoding='utf-8') as f:
    for raw in f:
        if 'RL0 seed records' in raw:
            break
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        m = re.match(r'^---\[(\d+)\]---$', line)
        if m:
            current_level = int(m.group(1))
            current_row = False
            continue
        if line.startswith('v='):
            if current_level is None:
                raise SystemExit('row before level header')
            rows[current_level] += 1
            current_row = True
            continue
        if line.startswith('-'):
            if current_level is None or not current_row:
                raise SystemExit('value before row header')
            values[current_level] += 1
            continue
        raise SystemExit(f'unparsed line: {raw.rstrip()}')

levels = sorted(set(rows) | set(values))
with open(out, 'w', encoding='utf-8') as g:
    g.write('# level rows values\n')
    for lev in levels:
        g.write(f'{lev} {rows[lev]} {values[lev]}\n')
