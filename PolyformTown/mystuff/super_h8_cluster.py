#!/usr/bin/env python3
"""
super_h8_cluster.py  --  SUPER_H8 3-fold rotational-symmetry cluster.

What it does (all for arrangement #13, the gap-free v[12]-type centre):
  1. Determines the 3-fold rotation centre from from_python/SUPER_H8_ROT.csv
     (rotsym.c output): copy 0 = base rotated 180 deg about C, so
     C = midpoint(base_pos, copy0_pos).  Falls back to the known value
     (-68.5, sqrt(3)/2) if the CSV is absent.
  2. Builds the 3-fold symmetric cluster from from_python/SUPER_H8_TILE.dat
     by placing three rotated copies (copy0/1/2) about C.
     Rotation convention: [180, 300, 60] deg  (= 0/120/240 + 180 deg).
  3. Renders tmp/rot_overlay_super_h8.svg:
       - copies 0/1/2 in pale blue / pale green / pale orange
       - anti-hats highlighted yellowish in each copy
       - outer boundary of each copy drawn in bold
       - rotation centre marked with a red dot
  4. Exports CSV (x,y,dir,ref,pt):
       tmp/SUPER_H8.csv       -- copy 0 (0 deg base orientation)
       tmp/SUPER_H8_120.csv   -- copy 1 (120 deg)
       tmp/SUPER_H8_240.csv   -- copy 2 (240 deg)

Note: the sym12 grow-overlay approach used in h8_rotcluster.py does not apply
here because the three SUPER_H8 copies orbit the centre C at radius ~54 units
while the sym12 grow records span only ~12 units around C.

Self-contained (numpy only).  Run from the project root:
    python3 MyHatTileStuff/super_h8_cluster.py
"""
import math, numpy as np, os
from collections import defaultdict

R3 = math.sqrt(3.0)
COPY_ANGLES = [180.0, 300.0, 60.0]      # 0/120/240 + 180 deg display rotation

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
    return V @ np.array([[c,-s],[s,c]]).T + np.array([x, y])

def rotmat(deg):
    a = math.radians(deg); c = math.cos(a); s = math.sin(a)
    return np.array([[c,-s],[s,c]])

# --------------------------------------------------------------- IO
def load_csv(fn):
    """Returns list of dict(x,y,dir,ref,pt, world[14,2])."""
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

# --------------------------------------------------------------- rotate a CSV hat
def rotate_param(h, deg, ctr):
    u = int(round(deg / 30.0))
    nd = (h["dir"] + (-u if h["ref"] else u)) % 12
    nx, ny = ctr + rotmat(deg) @ (np.array([h["x"], h["y"]]) - ctr)
    return dict(x=float(nx), y=float(ny), dir=nd, ref=h["ref"], pt=h["pt"],
                world=real_world(nd, h["ref"], (h["pt"]-1) % 14, nx, ny))

# --------------------------------------------------------------- centre determination
# The 3-fold centre lies OUTSIDE the single tile (arrangement #13, v[12]-type).
# It is derived from SUPER_H8_ROT.csv (rotsym.c output):
#   copy 0 = base rotated 180 deg about C  =>  C = (base + copy0) / 2
# Falls back to the known arrangement-13 value if the CSV is absent.

def find_center(base_hats, rot_csv="from_python/SUPER_H8_ROT.csv"):
    """Return (center_array, source_description)."""
    if os.path.exists(rot_csv):
        rot_pos = []
        for ln in open(rot_csv):
            s = ln.strip()
            if s and s[0] in "-+.0123456789":
                p = s.split(",")
                rot_pos.append([float(p[0]), float(p[1])])
        n = len(base_hats)
        if len(rot_pos) >= n:
            base_pos = np.array([[h["x"], h["y"]] for h in base_hats])
            copy0_pos = np.array(rot_pos[:n])
            # 180 deg rotation: copy0 = 2C - base  =>  C = (base + copy0) / 2
            ctr = ((base_pos + copy0_pos) / 2).mean(0)
            spread = float(np.abs((base_pos + copy0_pos) / 2 - ctr).max())
            print("  centre from rotsym CSV midpoint: (%.6f, %.6f)  spread=%.2e"
                  % (ctr[0], ctr[1], spread))
            return ctr, "rotsym CSV midpoint"

    # Fallback: arrangement-13, v[12]-type vertex (same for all H8 levels)
    ctr = np.array((-68.5, 0.8660254037844386))
    print("  %s not found; using known arrangement-13 value (-68.5, sqrt(3)/2)." % rot_csv)
    return ctr, "arrangement-13 fallback"

# =============================================================== 0. find centre
print("=== super_h8_cluster.py: SUPER_H8 3-fold cluster ===")
print("determining rotation centre...")
base = load_csv("from_python/SUPER_H8_TILE.dat")
C, csrc = find_center(base)

# =============================================================== 1. build cluster
print("building cluster (%d base hats, centre %.4f,%.4f)..." % (len(base), C[0], C[1]))
copies = []
maxerr = 0.0
for ci, ang in enumerate(COPY_ANGLES):
    copy = []
    for h in base:
        r = rotate_param(h, ang, C)
        ref_world = (h["world"] - C) @ rotmat(ang).T + C
        maxerr = max(maxerr, float(np.abs(r["world"] - ref_world).max()))
        r["copy"] = ci
        copy.append(r)
    copies.append(copy)
super_hats = [h for cp in copies for h in cp]
print("  %d hats total (%d anti-hats); param vs geometry max error = %.2e"
      % (len(super_hats), sum(h["ref"] for h in super_hats), maxerr))

# =============================================================== 2. SVG image
COPY_FILL  = ["#c5d6ea", "#c8e6c9", "#ffe0b2"]   # pale blue / green / orange per copy
ANTI_FILL  = "#f4d03f"                             # yellowish for anti-hats

def boundary_edges(hats_list):
    """Return list of (a, b) world-coordinate pairs forming the outer boundary."""
    cnt = defaultdict(int); seg = {}
    def k(p): return (round(p[0]*1e4), round(p[1]*1e4))
    for h in hats_list:
        w = h["world"]
        for i in range(14):
            a = w[i]; b = w[(i+1) % 14]; ka = k(a); kb = k(b)
            e = (ka, kb) if ka <= kb else (kb, ka)
            cnt[e] += 1; seg[e] = (a, b)
    return [seg[e] for e, c in cnt.items() if c == 1]

allw = np.vstack([h["world"] for h in super_hats])
mnx, mny = allw.min(0); mxx, mxy = allw.max(0)
W, H = mxx-mnx, mxy-mny; span = max(W, H); SC = 1400/span; M = 24
PW, PH = W*SC+2*M, H*SC+2*M
def px(p): return (p[0]-mnx)*SC+M
def py(p): return PH-((p[1]-mny)*SC+M)

o = open("tmp/rot_overlay_super_h8.svg", "w")
o.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{PW:.0f}" height="{PH:.0f}">\n')
o.write('<rect width="100%" height="100%" fill="white"/>\n')

# draw hats coloured by copy; anti-hats override with yellow
for h in super_hats:
    d = "".join(("M" if i == 0 else "L")+f"{px(p):.1f},{py(p):.1f}"
                for i, p in enumerate(h["world"]))
    fill = ANTI_FILL if h["ref"] else COPY_FILL[h["copy"]]
    o.write(f'<path d="{d} Z" fill="{fill}" stroke="#7a8aaa" stroke-width="0.3"/>\n')

# bold outer boundary of each copy
BSTROKE = ["#1a5276", "#1e8449", "#784212"]  # dark blue / green / brown
for ci, (cp, sc) in enumerate(zip(copies, BSTROKE)):
    for a, b in boundary_edges(cp):
        o.write(f'<line x1="{px(a):.1f}" y1="{py(a):.1f}" x2="{px(b):.1f}" y2="{py(b):.1f}" '
                f'stroke="{sc}" stroke-width="2.0"/>\n')

# rotation centre
o.write(f'<circle cx="{px(C):.1f}" cy="{py(C):.1f}" r="6" fill="red" stroke="white" stroke-width="1.5"/>\n')

# legend
for i, (cf, sc, label) in enumerate(zip(COPY_FILL, BSTROKE, ["copy 0 (0°)", "copy 1 (120°)", "copy 2 (240°)"])):
    o.write(f'<rect x="20" y="{20+i*26}" width="20" height="20" fill="{cf}" stroke="{sc}" stroke-width="1.5"/>'
            f'<text x="46" y="{36+i*26}" font-size="16" font-family="sans-serif">{label}</text>\n')
o.write(f'<rect x="20" y="{20+3*26}" width="20" height="20" fill="{ANTI_FILL}"/>'
        f'<text x="46" y="{36+3*26}" font-size="16" font-family="sans-serif">anti-hat</text>\n')
o.write(f'<circle cx="30" cy="{20+4*26+10}" r="6" fill="red"/>'
        f'<text x="46" y="{36+4*26}" font-size="16" font-family="sans-serif">centre C</text>\n')
o.write("</svg>\n"); o.close()
print("wrote tmp/rot_overlay_super_h8.svg")

# =============================================================== 3. CSV export
def write_csv(fn, hats):
    with open(fn, "w") as f:
        f.write("x,y,dir,ref,pt\n")
        for h in hats:
            f.write("%.12g,%.12g,%d,%s,%d\n"
                    % (h["x"], h["y"], h["dir"], "True" if h["ref"] else "False", h["pt"]))

for cp, suffix in zip(copies, ["", "_120", "_240"]):
    fn = f"tmp/SUPER_H8{suffix}.csv"
    write_csv(fn, cp)
    print("wrote %s (%d hats)" % (fn, len(cp)))
