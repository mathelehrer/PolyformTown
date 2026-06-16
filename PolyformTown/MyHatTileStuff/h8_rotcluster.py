#!/usr/bin/env python3
"""
h8_rotcluster.py  --  bundle of the H8 rotational-symmetry findings.

What it does (all for arrangement #13, the gap-free v[12]-type centre):
  1. Builds the 3-fold symmetric SUPER cluster from from_python/SUPER_SUPER_H8_TILE.dat
     by placing three rotated copies (copy0/1/2) about the v[12] triple vertex
     C13 = (-68.5, 0.8660).  The whole cluster is rotated by 180 deg (as it must
     be to come out of rotsym.c the right way up).
  2. Finds, at each grow_symmetric2 level (1..5), the completion record that
     matches the interior of the super cluster, and uses the level-5 match as a
     level-coloured overlay.
  3. Renders a nice image: super cluster with ANTI-HATS highlighted yellowish,
     the level-grown cluster overlaid and coloured inside->outside by level.
  4. Converts everything back to CSV (x,y,dir,ref,pt):
       from_python/SUPER_SUPER_H8_ROT.csv  -- the three copies, one after another
       from_python/LEVEL_ROT.csv           -- the grown cluster, levelwise in->out

Self-contained (numpy only).  Run from the project root:
    python3 MyHatTileStuff/h8_rotcluster.py
"""
import re, math, numpy as np
from collections import defaultdict

R3 = math.sqrt(3.0)
C13 = np.array((-68.5, 0.8660254037844386))     # arrangement #13 centre (v[12] type)
COPY_ANGLES = [180.0, 300.0, 60.0]              # 0/120/240 + 180 deg display rotation

# --------------------------------------------------------------- hat geometry
raw_len = [1,1,0,0,1,1,0,0,1,1,0,0,0,0]
raw_rot = [-1,1,4,6,3,5,8,6,9,7,10,12,12,14]

def hv14(d, ref, pt):
    elen = [0.0]*14; erot = [0]*14
    for j in range(14):
        src = (pt+(13-j)) % 14 if ref else (pt+j) % 14
        elen[j] = R3 if raw_len[src] else 1.0
        rot = raw_rot[src] + d
        erot[j] = (-rot) % 12 if ref else rot % 12
    vx = [0.0]*15; vy = [0.0]*15
    for j in range(14):
        a = math.pi*(erot[j]+1)/6.0
        vx[j+1] = vx[j]+elen[j]*math.cos(a); vy[j+1] = vy[j]+elen[j]*math.sin(a)
    return np.array([(vx[(i-pt) % 14], vy[(i-pt) % 14]) for i in range(14)])

def real_world(d, ref, piv, x, y):
    V = hv14(1 if ref else -1, ref, piv)
    th = d*(math.pi/6.0)*(-1 if ref else 1); c = math.cos(th); s = math.sin(th)
    Rm = np.array([[c,-s],[s,c]])
    return V@Rm.T + np.array([x, y])

def cworld(v, x, y):                              # tetrille Coord -> world
    if v == 6: sx, sy = 6*x, 6*y
    elif v == 4: sx, sy = 3*x, 3*y
    else: sx, sy = 2*(x-y), 2*(x+2*y)
    return ((3-sx-sy)/2.0, (sy-sx+3)/(2*R3))

def rotmat(deg):
    a = math.radians(deg); c = math.cos(a); s = math.sin(a)
    return np.array([[c,-s],[s,c]])

# --------------------------------------------------------------- IO
def load_csv(fn):
    """returns list of dict(x,y,dir,ref,pt, world[14,2])"""
    out = []
    for ln in open(fn):
        s = ln.strip()
        if not s or s[0] not in "-+.0123456789":
            continue
        p = s.split(",")
        x, y = float(p[0]), float(p[1]); d = int(p[2])
        ref = 1 if p[3][0] in "Tt1" else 0; pt = int(p[4])
        out.append(dict(x=x, y=y, dir=d, ref=ref, pt=pt,
                        world=real_world(d, ref, (pt-1) % 14, x, y)))
    return out

def load_records(level):
    txt = open(f"data/sym12/sym_completions_level{level}.dat").read()
    recs = []
    for line in re.findall(r'tiles:(\[\[.*?\]\])', txt):
        hats = [np.array([cworld(*map(int, m))
                          for m in re.findall(r'\((-?\d+),(-?\d+),(-?\d+)\)', hat)])
                for hat in re.findall(r'\[((?:\(-?\d+,-?\d+,-?\d+\),?)+)\]', line)]
        recs.append(hats)
    return recs

# --------------------------------------------------------------- rotate a CSV hat
def rotate_param(h, deg, ctr):
    """rotate hat-placement (x,y,dir,ref,pt) by `deg` about ctr -> new param dict"""
    u = deg / 30.0
    assert abs(u-round(u)) < 1e-9, "rotation must be a multiple of 30 deg"
    u = int(round(u))
    nd = (h["dir"] + (-u if h["ref"] else u)) % 12
    nx, ny = ctr + rotmat(deg) @ (np.array([h["x"], h["y"]]) - ctr)
    return dict(x=float(nx), y=float(ny), dir=nd, ref=h["ref"], pt=h["pt"],
                world=real_world(nd, h["ref"], (h["pt"]-1) % 14, nx, ny))

# =============================================================== 1. super cluster
print("building #13 super cluster (centre %.4f,%.4f, +180 deg)..." % (C13[0], C13[1]))
base = load_csv("from_python/SUPER_SUPER_H8_TILE.dat")
copies = []
maxerr = 0.0
for ci, ang in enumerate(COPY_ANGLES):
    copy = []
    for h in base:
        r = rotate_param(h, ang, C13)
        ref_world = (h["world"] - C13) @ rotmat(ang).T + C13     # geometric check
        maxerr = max(maxerr, np.abs(r["world"] - ref_world).max())
        r["copy"] = ci
        copy.append(r)
    copies.append(copy)
super_hats = [h for cp in copies for h in cp]
print("  %d hats (%d anti-hats); CSV-param vs geometry max error = %.2e"
      % (len(super_hats), sum(h["ref"] for h in super_hats), maxerr))
Scent = np.array([h["world"].mean(0) for h in super_hats])

# =============================================================== 2. grow overlay
gctr = np.array(cworld(3, 1, -1))
def fullalign(hats):
    G = np.array([h.mean(0) for h in hats]); best = (-1, None)
    for refl in (1, -1):
        P = (G-gctr).copy(); P[:, 0] *= refl
        for d0 in range(0, 360, 3):
            deg = float(d0)
            for _ in range(6):
                Rm = rotmat(deg); T = P@Rm.T + C13
                d2 = ((T[:, None]-Scent[None])**2).sum(-1); nn = d2.argmin(1); msk = d2.min(1) < 6e-2
                if msk.sum() < 3: break
                A = P[msk]; B = Scent[nn[msk]]-C13; H = A.T@B
                U, _, Vt = np.linalg.svd(H); Rk = Vt.T@U.T
                deg = math.degrees(math.atan2(Rk[1, 0], Rk[0, 0]))
            Rm = rotmat(deg); T = P@Rm.T + C13
            f = (((T[:, None]-Scent[None])**2).sum(-1).min(1) < 6e-2).mean()
            if f > best[0]: best = (f, (refl, Rm))
    return best

# fix the grow->super transform from the level-5 record that matches best
best5 = (-1, None, None)
for i, rec in enumerate(load_records(5)):
    f, tr = fullalign(rec)
    if f > best5[0]: best5 = (f, i, tr)
f5, idx5, (refl, Rm) = best5
print("  grow alignment locked from level5 rec%d = %.1f%%" % (idx5+1, f5*100))

def place(hats):
    out = []
    for h in hats:
        p = (h-gctr).copy(); p[:, 0] *= refl
        out.append(p@Rm.T + C13)
    return out

# per-level best record (under the fixed transform) -> birth level of super hats
birth = {}
level_match = {}
for L in range(1, 6):
    bestrec = (-1, None, None)
    for i, rec in enumerate(load_records(L)):
        placed = place(rec); pc = np.array([h.mean(0) for h in placed])
        cov = ((pc[:, None]-Scent[None])**2).sum(-1).min(1) < 6e-2
        if cov.mean() > bestrec[0]: bestrec = (cov.mean(), i, placed)
    frac, ri, placed = bestrec
    level_match[L] = (ri+1, frac)
    pc = np.array([h.mean(0) for h in placed])
    nn = ((pc[:, None]-Scent[None])**2).sum(-1)
    for j in range(len(placed)):
        si = nn[j].argmin()
        if nn[j, si] < 6e-2 and si not in birth:
            birth[si] = L
    print("  level %d: rec %d matches interior at %.1f%% (cumulative covered hats=%d)"
          % (L, ri+1, frac*100, len(birth)))

# =============================================================== 3. nice image
def boundary_edges(idxs):
    cnt = defaultdict(int); seg = {}
    def k(p): return (round(p[0]*1e4), round(p[1]*1e4))
    for si in idxs:
        h = super_hats[si]["world"]
        for i in range(14):
            a = h[i]; b = h[(i+1) % 14]; ka = k(a); kb = k(b)
            e = (ka, kb) if ka <= kb else (kb, ka); cnt[e] += 1; seg[e] = (a, b)
    return [seg[e] for e, c in cnt.items() if c == 1]

LV = ["#3b0f70", "#641a80", "#a52c60", "#de4968", "#fe9f6d"]   # magma-ish, in->out
allw = np.vstack([h["world"] for h in super_hats])
mnx, mny = allw.min(0); mxx, mxy = allw.max(0)
W, H = mxx-mnx, mxy-mny; span = max(W, H); SC = 1400/span; M = 24
PW, PH = W*SC+2*M, H*SC+2*M
def px(p): return (p[0]-mnx)*SC+M
def py(p): return PH-((p[1]-mny)*SC+M)
o = open("from_python/h8_rot_overlay.svg", "w")
o.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{PW:.0f}" height="{PH:.0f}">\n')
o.write('<rect width="100%" height="100%" fill="white"/>\n')
# super cluster: anti-hats yellowish, hats pale blue (opaque background)
for h in super_hats:
    d = "".join(("M" if i == 0 else "L")+f"{px(p):.1f},{py(p):.1f}" for i, p in enumerate(h["world"]))
    fill = "#f4d03f" if h["ref"] else "#c5d6ea"
    o.write(f'<path d="{d} Z" fill="{fill}" stroke="#7a8aaa" stroke-width="0.3"/>\n')
# grown overlay coloured inside->outside by level (alpha 0.5 so anti-hats show)
for si, L in birth.items():
    h = super_hats[si]["world"]
    d = "".join(("M" if i == 0 else "L")+f"{px(p):.1f},{py(p):.1f}" for i, p in enumerate(h))
    o.write(f'<path d="{d} Z" fill="{LV[L-1]}" fill-opacity="0.5" stroke="none"/>\n')
# bold outer boundary of the grown cluster
for a, b in boundary_edges(list(birth.keys())):
    o.write(f'<line x1="{px(a):.1f}" y1="{py(a):.1f}" x2="{px(b):.1f}" y2="{py(b):.1f}" '
            f'stroke="#111" stroke-width="2.2"/>\n')
# legend
for i, c in enumerate(LV):
    o.write(f'<rect x="{20}" y="{20+i*26}" width="20" height="20" fill="{c}" fill-opacity="0.7"/>'
            f'<text x="46" y="{36+i*26}" font-size="16" font-family="sans-serif">level {i+1}</text>\n')
o.write(f'<rect x="20" y="{20+5*26}" width="20" height="20" fill="#f4d03f"/>'
        f'<text x="46" y="{36+5*26}" font-size="16" font-family="sans-serif">anti-hat</text>\n')
o.write(f'<circle cx="{px(C13):.1f}" cy="{py(C13):.1f}" r="6" fill="red"/>\n')   # centre
o.write("</svg>\n"); o.close()
print("wrote from_python/h8_rot_overlay.svg")

# =============================================================== 4. CSV export
def write_csv(fn, hats):
    with open(fn, "w") as f:
        f.write("x,y,dir,ref,pt\n")
        for h in hats:
            f.write("%.12g,%.12g,%d,%s,%d\n"
                    % (h["x"], h["y"], h["dir"], "True" if h["ref"] else "False", h["pt"]))
# super cluster: three copies one after another
write_csv("from_python/SUPER_SUPER_H8_ROT.csv", super_hats)
print("wrote from_python/SUPER_SUPER_H8_ROT.csv (%d hats: %d+%d+%d)"
      % (len(super_hats), len(copies[0]), len(copies[1]), len(copies[2])))
# grown cluster: levelwise inside->outside (then by radius within a level)
order = sorted(birth.keys(), key=lambda si: (birth[si], np.hypot(*(Scent[si]-C13))))
write_csv("from_python/LEVEL_ROT.csv", [super_hats[si] for si in order])
print("wrote from_python/LEVEL_ROT.csv (%d hats; per-level %s)"
      % (len(order), {L: sum(1 for si in birth if birth[si] == L) for L in range(1, 6)}))
