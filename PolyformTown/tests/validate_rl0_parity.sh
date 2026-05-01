#!/usr/bin/env bash
set -euo pipefail
DATA="${1:-data/rl0/completions.dat}"
python - "$DATA" <<'PY'
import re,sys

def parse_cycle(txt):
    return [tuple(map(int,m)) for m in re.findall(r'\((-?\d+),(-?\d+),(-?\d+)\)',txt)]

def parse_cycles_array(text):
    inner=text.strip()[1:-1]
    out=[]; d=0; cur=''
    for ch in inner:
        if ch=='[': d+=1
        if d: cur+=ch
        if ch==']':
            d-=1
            if d==0: out.append(parse_cycle(cur)); cur=''
    return out

def embed(p):
    v,x,y=p
    if v==6: return (6*x+3*y, 3*y)
    if v==4: return (3*x+3*y, 3*y)
    if v==3: return (2*x+y, 3*y)
    raise ValueError(v)

def area2(c):
    pts=[embed(p) for p in c]
    s=0
    for i in range(len(pts)):
        x1,y1=pts[i]; x2,y2=pts[(i+1)%len(pts)]
        s += x1*y2 - x2*y1
    return s

hat=open('tiles/hat.tile').read().split('cycle:')[1]
base=[tuple(map(int,l.split())) for l in hat.splitlines() if l.strip() and l.strip()[0].isdigit()]
base_rev=list(reversed(base))
base_v=[p[0] for p in base]; base_rv=[p[0] for p in base_rev]
records=[r for r in open(sys.argv[1]).read().split('---[') if r.strip()]
assert len(records)==44
for rec in records:
    lines={k:v.strip() for ln in rec.splitlines()[1:] if ':' in ln for k,v in [ln.split(':',1)]}
    tiles=parse_cycles_array(lines['tiles'])
    boundary=parse_cycle(lines['canonical_boundary'])
    bset=set(boundary)
    par=[int(x) for x in re.findall(r'-?\d+',lines['parities'])]
    idx=[int(x) for x in re.findall(r'-?\d+',lines['indices'])]
    assert len(tiles)==len(par)==len(idx)
    assert area2(boundary) > 0, 'canonical boundary must be CCW'
    for t,p,i in zip(tiles,par,idx):
        assert p in (-1,1)
        assert 0<=i<len(t)
        assert area2(t) > 0, 'tile cycle must be CCW in output'
        vals=[q[0] for q in t]
        if p==1: assert vals==base_v
        else: assert vals==base_rv
        assert any(v in bset for v in t), 'tile must overlap aggregate boundary'
print('ok')
PY
