"""Better Steve: a slimmer, fully articulated, face-rigged take on Minecraft's Steve.

Run headless (rebuilds everything from nothing):
    blender -b --factory-startup --python scripts/build_better_steve.py -- <project root> [--style adventurer|classic]

Styles share one body, skeleton and face rig:
    adventurer (default)  leather jacket, beard, cargo pants, boots, backpack with bedroll and pickaxe
    classic               Steve's teal shirt and blue pants

Outputs:
    foundry/BetterSteve.blend   rig + mesh + controls + actions (texture packed)
    export/SK_BetterSteve.fbx   Unreal-ready skeletal mesh + baked animations
    export/SK_BetterSteve.glb   same, glTF
    export/T_BetterSteve.png    pixel atlas (set Filter = Nearest in UE)

Units: 1 Blender metre = 1 Minecraft block = 100 Unreal units.
Height: vanilla Steve is 32 px drawn at 0.9375 scale -> 1.875 blocks. Eyes sit at 1.62 (the game's eye height).
Facing: -Y in Blender (front view), which lands on +Y in Unreal like the UE5 Mannequin.
"""

import bpy
import bmesh
import math
import os
import random
import sys

import numpy as np
from mathutils import Matrix, Vector

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
STYLE = ARGS[ARGS.index("--style") + 1] if "--style" in ARGS else "adventurer"
POSITIONAL = [a for i, a in enumerate(ARGS) if not a.startswith("--") and (i == 0 or ARGS[i - 1] != "--style")]
ROOT = os.path.abspath(POSITIONAL[0]) if POSITIONAL else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FOUNDRY = os.path.join(ROOT, "foundry")
EXPORT = os.path.join(ROOT, "export")
ADV = STYLE == "adventurer"

STEVE_PX = 0.9375 / 16          # one vanilla skin pixel as rendered, in blocks
HEIGHT = 32 * STEVE_PX          # 1.875
EYE_Z = 1.62

# ---------------------------------------------------------------- atlas
TILE, MARGIN = 64, 3
TEXEL = STEVE_PX / 3            # 3x vanilla texel density
PALETTES = {  # name, sRGB hex, noise amplitude, pattern
    "classic": [
        ("skin", "B98567", .055, None), ("skin_dark", "9A6A4E", .05, None), ("hair", "3A2614", .12, "strands"),
        ("brow", "24170B", .06, None), ("shirt", "00A8A8", .06, None), ("shirt_trim", "007F80", .05, None),
        ("pants", "3F359E", .07, None), ("shoe", "707070", .07, None), ("sole", "3C3C3C", .05, None),
        ("sclera", "F4F4F4", .012, None), ("iris", "4A3D9A", .05, None), ("pupil", "1C1838", .02, None),
        ("glint", "FFFFFF", 0.0, None), ("mouth", "3A1612", .05, None), ("teeth", "EFEADC", .02, None),
        ("spare", "B98567", .055, None)],
    "adventurer": [
        ("skin", "A87252", .07, None), ("skin_dark", "8C5A3E", .06, None), ("hair", "2B1C0F", .14, "strands"),
        ("brow", "21140A", .06, None), ("beard", "3B2614", .16, "strands"), ("jacket", "6F4A2B", .08, "leather"),
        ("jacket_trim", "4A2F1A", .07, "leather"), ("fleece", "D2C29A", .10, "fleece"),
        ("shirt", "1F8E8B", .07, "weave"), ("belt", "3A2414", .06, "leather"), ("metal", "C2A55E", .08, None),
        ("pants", "77714D", .08, "twill"), ("pants_trim", "626042", .08, "twill"),
        ("pants_dirt", "5F5A3E", .12, "twill"), ("boot", "4B301C", .08, "leather"),
        ("boot_trim", "5E3D24", .08, "leather"), ("sole", "2A221C", .05, None), ("strap", "3D2717", .07, "leather"),
        ("canvas", "7E7651", .09, "weave"), ("canvas_dark", "605A3C", .09, "weave"),
        ("bedroll", "8E3B2C", .10, "weave"), ("wood", "6E4F2E", .10, "wood"), ("iron", "B4B4B4", .08, None),
        ("sclera", "F2EFE8", .015, None), ("iris", "4A3D9A", .05, None), ("pupil", "1C1838", .02, None),
        ("glint", "FFFFFF", 0.0, None), ("mouth", "3A1612", .05, None), ("teeth", "E6DFCC", .03, None),
        # vanilla Steve colours, only for the comparison model
        ("steve_skin", "B98567", .055, None), ("steve_hair", "3A2614", .12, "strands"),
        ("steve_shirt", "00A8A8", .06, None), ("steve_pants", "3F359E", .07, None), ("steve_shoe", "707070", .07, None)],
}
PALETTE = PALETTES[STYLE]
T = {n: i for i, (n, _, _, _) in enumerate(PALETTE)}
GRID = 4 if len(PALETTE) <= 16 else 8
TEX = GRID * TILE


def tile_noise(rng, amp, pattern):
    n = (rng.random((TILE, TILE)) * 2 - 1) * amp
    n[rng.random((TILE, TILE)) < .06] -= amp * 1.2
    row, col = np.mgrid[0:TILE, 0:TILE]
    if pattern == "strands":    # hair: darker vertical strands
        n += (rng.random(TILE) < .25)[None, :] * -amp * .8
    elif pattern == "leather":  # 4 px blotches + light scuffs
        n += (np.kron(rng.random((TILE // 4, TILE // 4)), np.ones((4, 4))) - .5) * amp * 1.4
        n[rng.random((TILE, TILE)) < .025] += amp * 1.5
    elif pattern == "weave":    # canvas / cotton checker
        n += ((row + col) % 2) * amp * .5 - amp * .25
    elif pattern == "twill":    # diagonal work-pants weave
        n -= ((row + col) % 4 == 0) * amp * .8
    elif pattern == "fleece":   # curly 2 px clumps
        n += (np.kron(rng.random((TILE // 2, TILE // 2)), np.ones((2, 2))) - .5) * amp * 1.5
    elif pattern == "wood":     # grain along v
        n += (rng.random(TILE)[None, :] - .5) * amp * 1.5
    return n


def make_atlas(path):
    rng = np.random.default_rng(1806)
    img = np.ones((TEX, TEX, 4), np.float32)
    for i, (name, hx, amp, pattern) in enumerate(PALETTE):
        base = np.array([int(hx[k:k + 2], 16) / 255 for k in (0, 2, 4)], np.float32)
        n = tile_noise(rng, amp, pattern)
        x0, y0 = (i % GRID) * TILE, (i // GRID) * TILE
        img[y0:y0 + TILE, x0:x0 + TILE, :3] = np.clip(base * (1 + n[..., None]), 0, 1)
    im = bpy.data.images.new("T_BetterSteve", TEX, TEX, alpha=False)
    im.pixels.foreach_set(img.ravel())
    im.filepath_raw = path
    im.file_format = "PNG"
    im.save()
    im.pack()
    return im

# ---------------------------------------------------------------- geometry builder
def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def chain_w(z, joints):
    """joints top->bottom: (z_joint, half_zone, bone_above, bone_below) -> blended weights."""
    for zj, hz, a, b in joints:
        if z >= zj + hz:
            return {a: 1.0}
        if z > zj - hz:
            t = smooth((zj + hz - z) / (2 * hz))
            return {a: 1 - t, b: t}
    return {joints[-1][3]: 1.0}


def zrect(cx, cy, z, hw, hd):
    return [(cx - hw, cy - hd, z), (cx + hw, cy - hd, z), (cx + hw, cy + hd, z), (cx - hw, cy + hd, z)]


def yrect(cx, y, z0, z1, hw):
    return [(cx - hw, y, z0), (cx + hw, y, z0), (cx + hw, y, z1), (cx - hw, y, z1)]


def xrect(x, y0, y1, z0, z1):
    return [(x, y0, z0), (x, y1, z0), (x, y1, z1), (x, y0, z1)]


class Builder:
    def __init__(self):
        self.co, self.w, self.faces, self.tile = [], [], [], []
        self.vmap = {}

    def shell(self):
        self.vmap = {}

    def v(self, p, w):
        k = (round(p[0], 6), round(p[1], 6), round(p[2], 6))
        if k not in self.vmap:
            w = {b: x for b, x in w.items() if x > 1e-3}
            tot = sum(w.values())
            self.co.append(Vector(p))
            self.w.append({b: x / tot for b, x in w.items()})
            self.vmap[k] = len(self.co) - 1
        return self.vmap[k]

    def f(self, ids, tile):
        self.faces.append(list(ids))
        self.tile.append(T[tile])

    def box(self, x0, x1, y0, y1, z0, z1, w, tile, tiles=None):
        self.shell()
        wf = w if callable(w) else (lambda p: w)
        i = [self.v((x, y, z), wf((x, y, z))) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
        quads = {"-z": (0, 2, 3, 1), "+z": (4, 5, 7, 6), "-y": (0, 1, 5, 4),
                 "+y": (2, 6, 7, 3), "-x": (0, 4, 6, 2), "+x": (1, 3, 7, 5)}
        for key, q in quads.items():
            self.f([i[k] for k in q], (tiles or {}).get(key, tile))

    def rotate_shell(self, pivot, axis, deg):
        """Rotate the shell just built (e.g. the last box) about a pivot."""
        R = Matrix.Rotation(math.radians(deg), 3, axis)
        pv = Vector(pivot)
        for i in set(self.vmap.values()):
            self.co[i] = R @ (self.co[i] - pv) + pv

    def loft(self, rings, seg_tiles, cap0, cap1):
        """rings: [(4 corner points, weights)], consecutive rings joined by 4 quads.
        A segment tile may be a 4-tuple, one per side in corner order (zrect: front, +x, back, -x)."""
        self.shell()
        R = [[self.v(p, w) for p in pts] for pts, w in rings]
        for a in range(len(R) - 1):
            t = seg_tiles[a]
            for k in range(4):
                self.f([R[a][k], R[a][(k + 1) % 4], R[a + 1][(k + 1) % 4], R[a + 1][k]],
                       t[k] if isinstance(t, tuple) else t)
        self.f(list(reversed(R[0])), cap0)
        self.f(R[-1], cap1)

    def grid_box(self, X, Y, Z, holes, w, tile):
        """Box gridded on X/Y/Z breakpoints (all quads, no T-junctions) with recessed holes in its -Y face.
        holes: (x0, x1, z0, z1, depth, wall_bands, back_tile); wall_bands = [(depth_end, tile), ...] front to back."""
        self.shell()

        def V(x, y, z):
            return self.v((x, y, z), w)

        def inhole(cx, cz):
            return any(h[0] < cx < h[1] and h[2] < cz < h[3] for h in holes)

        y0, y1, x0, x1, z0, z1 = Y[0], Y[-1], X[0], X[-1], Z[0], Z[-1]
        for i in range(len(X) - 1):
            xa, xb = X[i], X[i + 1]
            for k in range(len(Z) - 1):
                za, zb = Z[k], Z[k + 1]
                if not inhole((xa + xb) / 2, (za + zb) / 2):
                    self.f([V(xa, y0, za), V(xb, y0, za), V(xb, y0, zb), V(xa, y0, zb)], tile)
                self.f([V(xa, y1, za), V(xb, y1, za), V(xb, y1, zb), V(xa, y1, zb)], tile)
            for j in range(len(Y) - 1):
                ya, yb = Y[j], Y[j + 1]
                self.f([V(xa, ya, z0), V(xb, ya, z0), V(xb, yb, z0), V(xa, yb, z0)], tile)
                self.f([V(xa, ya, z1), V(xb, ya, z1), V(xb, yb, z1), V(xa, yb, z1)], tile)
        for j in range(len(Y) - 1):
            ya, yb = Y[j], Y[j + 1]
            for k in range(len(Z) - 1):
                za, zb = Z[k], Z[k + 1]
                for x in (x0, x1):
                    self.f([V(x, ya, za), V(x, yb, za), V(x, yb, zb), V(x, ya, zb)], tile)
        for hx0, hx1, hz0, hz1, depth, bands, back in holes:
            xs = [x for x in X if hx0 - 1e-9 <= x <= hx1 + 1e-9]
            zs = [z for z in Z if hz0 - 1e-9 <= z <= hz1 + 1e-9]
            ring = ([(x, hz0) for x in xs] + [(hx1, z) for z in zs[1:]]
                    + [(x, hz1) for x in reversed(xs[:-1])] + [(hx0, z) for z in reversed(zs[1:-1])])
            yb = y0 + depth
            ya = y0
            for d_end, wall in bands:
                ye = y0 + d_end
                for a in range(len(ring)):
                    (xa, za), (xb, zb) = ring[a], ring[(a + 1) % len(ring)]
                    self.f([V(xa, ya, za), V(xb, ya, zb), V(xb, ye, zb), V(xa, ye, za)], wall)
                ya = ye
            for i in range(len(xs) - 1):
                for k in range(len(zs) - 1):
                    self.f([V(xs[i], yb, zs[k]), V(xs[i + 1], yb, zs[k]),
                            V(xs[i + 1], yb, zs[k + 1]), V(xs[i], yb, zs[k + 1])], back)


# ---------------------------------------------------------------- the character
# Head (skin) box and face plane
HX, FRONT, HBACK, HZ0, HZ1 = .185, -.195, .175, 1.465, 1.855
EYE_X, EYE_HW, EYE_Z0, EYE_Z1, EYE_DEPTH = .0825, .04, 1.5925, 1.6475, .014
MOUTH_HW, MOUTH_Z0, MOUTH_Z1, MOUTH_DEPTH, MOUTH_LINE = .055, 1.505, 1.545, .03, 1.525
ARM_X, LEG_X = (.258 if ADV else .245), .085
FINGERS = [("index", -.03345, .078), ("middle", -.01115, .086), ("ring", .01115, .081), ("pinky", .03345, .066)]
KNUCKLE_Z = .725
THUMB_Y, THUMB_Z = -.05, (.785, .76, .725)   # base joint, middle joint, tip
TORSO_J = [(1.22, .05, "spine_03", "spine_02"), (1.10, .05, "spine_02", "spine_01"), (.99, .04, "spine_01", "pelvis")]

# Station tables: (z, half width, half depth, tile of the segment up to the next station). Two stations at the
# same z make a ring (hem, cuff, boot top). A 4-tuple tile is per side: front, +x, back, -x.
if ADV:
    JK = ("shirt", "jacket", "jacket", "jacket")   # open jacket: the shirt shows between the front panels
    TORSO = [(.83, .170, .095, "pants"), (.905, .171, .096, "belt"), (.905, .176, .101, "belt"),
             (.945, .176, .101, "jacket_trim"), (.945, .181, .106, JK), (.99, .175, .102, JK),
             (1.04, .169, .099, JK), (1.08, .167, .099, JK), (1.13, .174, .104, JK), (1.18, .182, .109, JK),
             (1.24, .188, .112, JK), (1.30, .190, .113, JK), (1.40, .188, .111, None)]
    TORSO_CAPS = ("pants", "jacket")
    NECK = (.058, .052)
    PALM = "strap"                                  # fingerless leather gloves
    ARM = [(.725, .019, .043, PALM), (.76, .0195, .043, PALM), (.78, .021, .0428, PALM),            # palm
           (.80, .029, .041, "skin"), (.82, .031, .0415, "skin"),                                    # wrist
           (.845, .034, .042, "strap"), (.845, .038, .046, "strap"),                                 # bracer
           (.91, .044, .049, "strap"), (.91, .040, .045, "skin"),
           (.965, .048, .047, "jacket_trim"), (.965, .068, .066, "jacket_trim"),                      # rolled cuff
           (1.0, .068, .066, "jacket_trim"), (1.0, .064, .062, "jacket"), (1.035, .064, .062, "jacket"),
           (1.0525, .064, .062, "jacket"), (1.07, .064, .063, "jacket"), (1.0875, .064, .063, "jacket"),
           (1.105, .064, .064, "jacket"), (1.235, .065, .065, "jacket"), (1.40, .066, .066, None)]
    ARM_CAPS = (PALM, "jacket")
    LEG =[(.05, .066, .068, "boot"), (.10, .066, .068, "boot"), (.205, .066, .068, "boot_trim"),     # boot shaft
           (.205, .069, .071, "boot_trim"), (.24, .069, .071, "boot_trim"), (.24, .061, .063, "pants_dirt"),
           (.33, .062, .064, "pants"), (.44, .066, .069, "pants"), (.46, .0665, .0695, "pants"),
           (.48, .067, .070, "pants"), (.50, .0675, .0705, "pants"), (.52, .068, .071, "pants"),
           (.62, .072, .075, "pants"), (.97, .076, .079, None)]
    LEG_CAPS = ("boot", "pants")
    FOOT = dict(hw=.071, bottom=.014, tile="boot",
                tops=[(.075, .11), (.03, .11), (-.06, .10), (-.09, .085), (-.11, .076), (-.13, .07), (-.195, .064)])
    SOLE = dict(hw=.074, top=.016, ys=[.08, .03, -.06, -.09, -.11, -.13, -.20])
    FRINGE = ((-.195, -.13, 1.732), (-.13, -.075, 1.748), (-.03, .035, 1.738), (.085, .14, 1.747), (.14, .195, 1.728))
    HAIR_BACK_Z = 1.55
else:
    TORSO = [(.83, .165, .092, "pants"), (.90, .166, .093, "pants"), (.955, .164, .092, "shirt_trim"),
             (.955, .170, .099, "shirt"), (.99, .161, .094, "shirt"), (1.04, .153, .089, "shirt"),
             (1.08, .150, .088, "shirt"), (1.13, .157, .092, "shirt"), (1.18, .166, .097, "shirt"),
             (1.24, .174, .101, "shirt"), (1.30, .180, .103, "shirt"), (1.40, .180, .099, None)]
    TORSO_CAPS = ("pants", "shirt")
    NECK = (.052, .048)
    PALM = "skin"
    ARM = [(.725, .019, .043, PALM), (.76, .0195, .043, PALM), (.78, .021, .0428, PALM),            # palm
           (.80, .029, .041, "skin"), (.82, .031, .0415, "skin"), (.84, .033, .042, "skin"),          # wrist
           (1.035, .051, .051, "skin"), (1.0525, .052, .052, "skin"), (1.07, .053, .053, "skin"),
           (1.0875, .054, .054, "skin"), (1.105, .055, .055, "skin"), (1.235, .055, .055, "shirt_trim"),
           (1.235, .060, .060, "shirt"), (1.40, .060, .060, None)]
    ARM_CAPS = (PALM, "shirt")
    LEG =[(.05, .062, .062, "shoe"), (.10, .062, .062, "shoe"), (.135, .062, .062, "shoe"),
           (.135, .058, .060, "pants"), (.20, .058, .060, "pants"), (.44, .063, .066, "pants"),
           (.46, .0635, .0665, "pants"), (.48, .064, .067, "pants"), (.50, .0645, .0675, "pants"),
           (.52, .065, .068, "pants"), (.62, .068, .072, "pants"), (.97, .072, .075, None)]
    LEG_CAPS = ("shoe", "pants")
    FOOT = dict(hw=.066, bottom=.012, tile="shoe",
                tops=[(.075, .10), (.03, .10), (-.06, .095), (-.09, .08), (-.11, .072), (-.13, .066), (-.19, .06)])
    SOLE = dict(hw=.069, top=.014, ys=[.079, .03, -.06, -.09, -.11, -.13, -.194])
    FRINGE = ((-.195, -.115, 1.737), (-.055, .005, 1.746), (.095, .195, 1.742))
    HAIR_BACK_Z = 1.57


def side_name(name, s):
    return name.replace("_l", "_r") if s < 0 and name.endswith("_l") else name


def build_body(b):
    # --- torso: pelvis -> spine_03, waist tapered, hem overhangs the pants
    b.loft([(zrect(0, 0, z, hw, hd), chain_w(z, TORSO_J)) for z, hw, hd, _ in TORSO], [s[3] for s in TORSO[:-1]],
           *TORSO_CAPS)

    # --- neck
    nj = [(1.465, .025, "head", "neck_01"), (1.39, .025, "neck_01", "spine_03")]
    nz = [1.37, 1.39, 1.41, 1.44, 1.465, 1.49, 1.51]
    b.loft([(zrect(0, 0, z, *NECK), chain_w(z, nj)) for z in nz], ["skin"] * (len(nz) - 1), "skin", "skin")

    # --- head with recessed eye sockets and mouth
    holes = [(s * EYE_X - EYE_HW, s * EYE_X + EYE_HW, EYE_Z0, EYE_Z1, EYE_DEPTH, [(EYE_DEPTH, "skin_dark")], "sclera")
             for s in (1, -1)]
    # mouth lip of skin in front of the lip plugs, dark cavity behind them: a closed mouth reads as one line
    holes.append((-MOUTH_HW, MOUTH_HW, MOUTH_Z0, MOUTH_Z1, MOUTH_DEPTH, [(.0055, "skin"), (MOUTH_DEPTH, "mouth")],
                  "mouth"))
    X = sorted({-HX, HX} | {h[0] for h in holes} | {h[1] for h in holes})
    Z = sorted({HZ0, HZ1} | {h[2] for h in holes} | {h[3] for h in holes})
    b.grid_box(X, [FRONT, HBACK], Z, holes, {"head": 1}, "skin")

    head = {"head": 1}
    b.box(-.021, .021, FRONT - .016, FRONT + .007, 1.562, 1.598, head, "skin_dark")                 # nose
    b.box(-.035, .035, FRONT + .011, FRONT + .013, 1.509, 1.523, head, "teeth")                     # upper teeth

    # --- hair (union of blocks around the head)
    H = .195
    b.box(-H, H, FRONT - .01, HBACK + .01, 1.80, HEIGHT, head, "hair")                              # cap
    b.box(-H, H, HBACK - .01, HBACK + .01, HAIR_BACK_Z, 1.80, head, "hair")                         # back
    b.box(-H, H, FRONT - .01, FRONT + .01, 1.76, 1.80, head, "hair")                                # fringe
    for x0, x1, z0 in FRINGE:                                                                       # fringe steps
        b.box(x0, x1, FRONT - .01, FRONT + .01, z0, 1.76, head, "hair")
    for s in (1, -1):
        xa, xb = sorted((s * (HX - .01), s * H))
        b.box(xa, xb, -.05, HBACK - .01, 1.64, 1.80, head, "hair")                                   # side
        b.box(xa, xb, FRONT + .01, -.05, 1.73, 1.80, head, "hair")                                   # temple
        b.box(xa, xb, -.05, 0.0, 1.58, 1.64, head, "hair")                                           # sideburn

    # --- face rig pieces
    for s in (1, -1):
        ex = s * EYE_X
        n = lambda nm: side_name(nm, s)
        eye = {n("eye_l"): 1}
        b.box(ex - .016, ex + .016, FRONT + .0095, FRONT + .011, EYE_Z - .018, EYE_Z + .018, eye, "iris")
        b.box(ex - .007, ex + .007, FRONT + .0084, FRONT + .010, EYE_Z - .009, EYE_Z + .009, eye, "pupil")
        b.box(ex - .0105, ex - .0045, FRONT + .0076, FRONT + .0088, EYE_Z + .0045, EYE_Z + .0105, eye, "glint")
        b.box(ex - .048, ex + .048, FRONT + .0015, FRONT + .0035, EYE_Z1, EYE_Z1 + .0625,
              {n("eyelid_upper_l"): 1}, "skin")
        b.box(ex - .048, ex + .048, FRONT + .004, FRONT + .006, EYE_Z0 - .0425, EYE_Z0,
              {n("eyelid_lower_l"): 1}, "skin")
        brow, bz0, bz1 = {n("brow_l"): 1}, (1.672 if ADV else 1.675), (1.696 if ADV else 1.693)
        if ADV and s > 0:  # an old scar splits his left brow
            b.box(ex - .0475, ex + .004, FRONT - .006, FRONT + .010, bz0, bz1, brow, "brow")
            b.box(ex + .012, ex + .0475, FRONT - .006, FRONT + .010, bz0, bz1, brow, "brow")
        else:
            b.box(ex - .0475, ex + .0475, FRONT - .006, FRONT + .010, bz0, bz1, brow, "brow")

    # lips: two plugs behind the mouth opening; corners follow lip_corner_*, middles follow lip_upper / jaw
    def lip_w(x, mid):
        c = smooth((abs(x) - .02) / .03)
        return {mid: 1 - c, ("lip_corner_l" if x > 0 else "lip_corner_r"): c}
    xs = [-.07, -.05, -.035, -.02, 0, .02, .035, .05, .07]
    for z0, z1, mid in ((MOUTH_LINE + .0015, 1.568, "lip_upper"), (1.49, MOUTH_LINE - .0015, "jaw")):
        b.loft([(xrect(x, FRONT + .002, FRONT + .0055, z0, z1), lip_w(x, mid)) for x in xs],
               ["skin"] * (len(xs) - 1), "skin", "skin")

    # --- arms: one continuous loft from the knuckles to the shoulder. The forearm tapers into the wrist and the
    # wrist flares into a full-size palm, so the hand reads as part of the arm instead of a stub on a square post.
    for s in (1, -1):
        cx = s * ARM_X
        n = lambda nm: side_name(nm, s)
        aj = [(1.07, .035, n("upperarm_l"), n("lowerarm_l")), (.815, .02, n("lowerarm_l"), n("hand_l"))]
        b.loft([(zrect(cx, 0, z, hw, hd), chain_w(z, aj)) for z, hw, hd, _ in ARM], [x[3] for x in ARM[:-1]],
               *ARM_CAPS)
        for fname, fy, flen in FINGERS:
            mid = KNUCKLE_Z - .55 * flen
            fj = [(KNUCKLE_Z, .007, n("hand_l"), n(fname + "_01_l")), (mid, .006, n(fname + "_01_l"), n(fname + "_02_l"))]
            fz = [KNUCKLE_Z + .01, KNUCKLE_Z, KNUCKLE_Z - .01, mid + .006, mid, mid - .006, KNUCKLE_Z - flen]
            b.loft([(zrect(cx, fy, z, .0115, .0095 if z > KNUCKLE_Z - flen else .0088), chain_w(z, fj)) for z in fz],
                   ["skin"] * (len(fz) - 1), "skin", "skin")
        t0, t1, t2 = THUMB_Z
        tj_ = [(t0, .006, n("hand_l"), n("thumb_01_l")), (t1, .006, n("thumb_01_l"), n("thumb_02_l"))]
        tz = [t0 + .01, t0, t0 - .01, t1 + .006, t1, t1 - .006, t2]
        b.loft([(zrect(cx, THUMB_Y, z, .0115, .0105), chain_w(z, tj_)) for z in tz], ["skin"] * (len(tz) - 1),
               "skin", "skin")

    # --- legs (knee bend zone, shoe collar / boot shaft) and feet (ball bend zone, separate sole)
    for s in (1, -1):
        cx = s * LEG_X
        n = lambda nm: side_name(nm, s)
        lj = [(.48, .04, n("thigh_l"), n("calf_l")), (.095, .03, n("calf_l"), n("foot_l"))]
        b.loft([(zrect(cx, 0, z, hw, hd), chain_w(z, lj)) for z, hw, hd, _ in LEG], [x[3] for x in LEG[:-1]],
               *LEG_CAPS)

        def foot_w(y):   # front is -y: the heel side of the ball joint (y -0.11) is foot, the toes are ball
            return chain_w(y, [(-.11, .02, n("foot_l"), n("ball_l"))])
        fy = FOOT["tops"]
        b.loft([(yrect(cx, y, FOOT["bottom"], top, FOOT["hw"]), foot_w(y)) for y, top in fy],
               [FOOT["tile"]] * (len(fy) - 1), FOOT["tile"], FOOT["tile"])
        sy = SOLE["ys"]
        b.loft([(yrect(cx, y, 0.0, SOLE["top"], SOLE["hw"]), foot_w(y)) for y in sy], ["sole"] * (len(sy) - 1),
               "sole", "sole")

    if ADV:
        build_beard(b)
        build_gear(b)


def build_beard(b):
    """Blocky beard framing the mouth opening, so the lip plugs still show talking, smiles and frowns."""
    head = {"head": 1}
    y0, y1, W = FRONT - .012, FRONT + .008, .197
    b.box(-W, W, y0, y1, 1.445, MOUTH_Z0, head, "beard")                                      # chin
    b.box(-HX + .01, HX - .01, y1, -.06, 1.445, 1.47, head, "beard")                         # under the chin
    for s in (1, -1):
        b.box(*sorted((s * MOUTH_HW, s * W)), y0, y1, MOUTH_Z0, MOUTH_Z1, head, "beard")     # beside the mouth
        b.box(*sorted((s * .075, s * W)), y0, y1, MOUTH_Z1, 1.585, head, "beard")            # cheeks
        b.box(*sorted((s * (HX - .01), s * W)), y1, 0.0, 1.455, 1.58, head, "beard")         # jaw line to sideburn
        b.box(*sorted((s * .052, s * .072)), FRONT - .015, y1, 1.513, MOUTH_Z1, head, "hair")  # moustache ends
    b.box(-.075, .075, FRONT - .014, y1, MOUTH_Z1, 1.562, head, "hair")                       # moustache


def build_gear(b):
    """Open jacket fronts, fleece collar, belt buckle, backpack harness, cargo pockets, backpack."""
    tw = lambda p: chain_w(p[2], TORSO_J)
    i0 = next(i for i, st in enumerate(TORSO) if st[3] == JK)
    for s in (1, -1):  # jacket front panels follow the torso taper, leaving a strip of shirt in the middle
        rings = []
        for z, hw, hd, _ in TORSO[i0:]:
            x0, x1 = sorted((s * .035, s * (hw + .005)))
            y0, y1 = -hd - .008, -hd + .004
            rings.append(([(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)], chain_w(z, TORSO_J)))
        edge = ("jacket", "jacket", "jacket", "jacket_trim") if s > 0 else ("jacket", "jacket_trim", "jacket", "jacket")
        b.loft(rings, [edge] * (len(rings) - 1), "jacket_trim", "jacket")
        for z0, z1, xe in ((1.355, 1.40, .115), (1.325, 1.355, .09), (1.295, 1.325, .065)):   # fleece lapels
            b.box(*sorted((s * .035, s * xe)), -.127, -.119, z0, z1, {"spine_03": 1}, "fleece")
        b.box(*sorted((s * .06, s * .105)), -.07, .055, 1.385, 1.44, {"spine_03": 1}, "fleece")   # collar sides
        xa, xb = sorted((s * .115, s * .148))
        b.box(xa, xb, -.131, -.119, 1.12, 1.398, tw, "strap")                                  # harness, front
        b.box(xa, xb, -.131, .125, 1.398, 1.414, {"spine_03": 1}, "strap")                     # over the shoulder
    b.box(-.105, .105, .055, .095, 1.385, 1.455, {"spine_03": 1}, "fleece")                    # collar back
    b.box(-.024, .024, -.108, -.099, .909, .941, tw, "metal")                                  # belt buckle
    b.box(-.148, .148, -.137, -.129, 1.21, 1.23, tw, "strap")                                  # chest strap
    b.box(-.014, .014, -.141, -.135, 1.205, 1.235, tw, "metal")                                # its buckle

    for s in (1, -1):  # cargo pockets on the outer thighs
        cx = s * LEG_X
        thigh = {side_name("thigh_l", s): 1}
        b.box(*sorted((cx + s * .066, cx + s * .086)), -.045, .045, .60, .72, thigh, "pants_trim")
        b.box(*sorted((cx + s * .066, cx + s * .089)), -.048, .048, .70, .725, thigh, "pants")

    pk = {"backpack": 1}  # everything below rides the backpack bone (give it physics in UE)
    b.box(-.135, .135, .108, .255, 1.02, 1.30, pk, "canvas")                                   # body
    b.box(-.142, .142, .104, .262, 1.29, 1.345, pk, "canvas_dark")                             # lid
    b.box(-.09, .09, .255, .29, 1.05, 1.17, pk, "canvas")                                      # front pocket
    b.box(-.095, .095, .253, .296, 1.15, 1.185, pk, "canvas_dark")                             # its flap
    b.box(-.17, .17, .13, .245, 1.345, 1.435, pk, "bedroll")                                   # bedroll
    for s in (1, -1):
        b.box(*sorted((s * .133, s * .16)), .14, .23, 1.04, 1.16, pk, "canvas")               # side pockets
        b.box(*sorted((s * .05, s * .07)), .253, .26, 1.185, 1.30, pk, "strap")               # lid straps
        b.box(*sorted((s * .10, s * .12)), .125, .25, 1.34, 1.44, pk, "strap")                # bedroll straps
    # iron pickaxe strapped diagonally across the pack, head up by the left shoulder (built upright, then tilted)
    pivot, tilt = (0, .306, 1.19), 35
    for box, tile in (((-.0075, .0075, .298, .314, .93, 1.45), "wood"),          # handle
                      ((-.10, .10, .295, .317, 1.44, 1.468), "iron"),            # head
                      ((-.10, -.078, .295, .317, 1.418, 1.44), "iron"),          # head tips
                      ((.078, .10, .295, .317, 1.418, 1.44), "iron"),
                      ((-.012, .012, .293, .319, 1.08, 1.10), "strap")):         # grip wrap
        b.box(*box, pk, tile)
        b.rotate_shell(pivot, "Y", tilt)


def build_vanilla_reference(b, ox):
    """Vanilla Steve's six boxes at render scale, for side-by-side comparison."""
    p = STEVE_PX
    w = {"ref": 1}
    c = {k: ("steve_" + k if "steve_" + k in T else k) for k in ("skin", "hair", "shirt", "pants", "shoe")}
    b.box(ox - 4 * p, ox + 4 * p, -4 * p, 4 * p, 24 * p, 32 * p, w, c["skin"], {"+z": c["hair"]})
    b.box(ox - 4 * p, ox + 4 * p, -2 * p, 2 * p, 12 * p, 24 * p, w, c["shirt"])
    for s in (1, -1):
        b.box(*sorted((ox + s * 4 * p, ox + s * 8 * p)), -2 * p, 2 * p, 12 * p, 20 * p, w, c["skin"])
        b.box(*sorted((ox + s * 4 * p, ox + s * 8 * p)), -2 * p, 2 * p, 20 * p, 24 * p, w, c["shirt"])
        b.box(*sorted((ox, ox + s * 4 * p)), -2 * p, 2 * p, 1 * p, 12 * p, w, c["pants"])
        b.box(*sorted((ox, ox + s * 4 * p)), -2 * p, 2 * p, 0, 1 * p, w, c["shoe"])


def mesh_from_builder(b, name, mat, coll):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(c) for c in b.co], [], b.faces)
    if me.validate(verbose=True):
        raise RuntimeError(f"{name}: mesh needed repairs; face/tile order is no longer trustworthy")
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    uv = bm.loops.layers.uv.new("UVMap")
    rng = random.Random(7)
    worst = 0
    for f in bm.faces:
        tile = b.tile[f.index]
        n = f.normal
        ax = max(range(3), key=lambda k: abs(n[k]))
        a, c = [k for k in range(3) if k != ax]
        pts = [(l.vert.co[a] / TEXEL, l.vert.co[c] / TEXEL) for l in f.loops]
        amin = math.floor(min(p[0] for p in pts))
        cmin = math.floor(min(p[1] for p in pts))
        ea = max(p[0] for p in pts) - amin
        ec = max(p[1] for p in pts) - cmin
        worst = max(worst, ea, ec)
        room = TILE - 2 * MARGIN
        ra = rng.randint(0, max(0, int(room - math.ceil(ea))))
        rc = rng.randint(0, max(0, int(room - math.ceil(ec))))
        tx, ty = (tile % GRID) * TILE + MARGIN, (tile // GRID) * TILE + MARGIN
        for l, (pa, pc) in zip(f.loops, pts):
            l[uv].uv = ((tx + ra + pa - amin) / TEX, (ty + rc + pc - cmin) / TEX)
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = False
    me.materials.append(mat)
    obj = bpy.data.objects.new(name, me)
    coll.objects.link(obj)
    print(f"[mesh] {name}: {len(me.vertices)} verts, {len(me.polygons)} quads, largest UV island {worst:.1f} texels "
          f"(tile room {TILE - 2 * MARGIN})")
    return obj


# ---------------------------------------------------------------- skeleton
BONES = []


def bone(name, h, t, parent=None, roll=(0, -1, 0), deform=True, connect=False, mirror=True):
    BONES.append((name, h, t, parent, roll, deform, connect))
    if mirror and name.endswith("_l"):
        mx = lambda v: (-v[0], v[1], v[2])
        BONES.append((side_name(name, -1), mx(h), mx(t), side_name(parent, -1) if parent else None, mx(roll),
                      deform, connect))


def define_bones():
    up, fwd, palm = (0, 0, 1), (0, -1, 0), (-1, 0, 0)
    bone("root", (0, 0, 0), (0, -.3, 0), roll=up)
    bone("pelvis", (0, 0, .90), (0, 0, .99), "root")
    bone("spine_01", (0, 0, .99), (0, 0, 1.10), "pelvis", connect=True)
    bone("spine_02", (0, 0, 1.10), (0, 0, 1.22), "spine_01", connect=True)
    bone("spine_03", (0, 0, 1.22), (0, 0, 1.37), "spine_02", connect=True)
    bone("neck_01", (0, 0, 1.37), (0, 0, 1.48), "spine_03", connect=True)
    bone("head", (0, 0, 1.48), (0, 0, 1.86), "neck_01", connect=True)

    bone("clavicle_l", (.03, 0, 1.36), (ARM_X, 0, 1.345), "spine_03")
    bone("upperarm_l", (ARM_X, 0, 1.345), (ARM_X, .008, 1.07), "clavicle_l", connect=True)
    bone("lowerarm_l", (ARM_X, .008, 1.07), (ARM_X, 0, .815), "upperarm_l", connect=True)
    bone("hand_l", (ARM_X, 0, .815), (ARM_X, 0, KNUCKLE_Z), "lowerarm_l", roll=palm, connect=True)
    bone("thumb_01_l", (ARM_X, THUMB_Y, THUMB_Z[0]), (ARM_X, THUMB_Y, THUMB_Z[1]), "hand_l", roll=palm)
    bone("thumb_02_l", (ARM_X, THUMB_Y, THUMB_Z[1]), (ARM_X, THUMB_Y, THUMB_Z[2]), "thumb_01_l", roll=palm,
         connect=True)
    for fname, fy, flen in FINGERS:
        mid = KNUCKLE_Z - .55 * flen
        bone(f"{fname}_01_l", (ARM_X, fy, KNUCKLE_Z), (ARM_X, fy, mid), "hand_l", roll=palm)
        bone(f"{fname}_02_l", (ARM_X, fy, mid), (ARM_X, fy, KNUCKLE_Z - flen), f"{fname}_01_l", roll=palm,
             connect=True)

    bone("thigh_l", (LEG_X, 0, .90), (LEG_X, -.008, .48), "pelvis")
    bone("calf_l", (LEG_X, -.008, .48), (LEG_X, 0, .095), "thigh_l", connect=True)
    bone("foot_l", (LEG_X, 0, .095), (LEG_X, -.11, .035), "calf_l", roll=up, connect=True)
    bone("ball_l", (LEG_X, -.11, .035), (LEG_X, -.19, .035), "foot_l", roll=up, connect=True)

    # face (all children of head)
    bone("eye_l", (EYE_X, -.01, EYE_Z), (EYE_X, FRONT, EYE_Z), "head", roll=up)
    bone("eyelid_upper_l", (EYE_X, FRONT + .005, EYE_Z1), (EYE_X, FRONT + .005, EYE_Z0), "head", roll=fwd)
    bone("eyelid_lower_l", (EYE_X, FRONT + .005, EYE_Z0), (EYE_X, FRONT + .005, EYE_Z), "head", roll=fwd)
    bone("brow_l", (EYE_X, FRONT, 1.684), (EYE_X, FRONT - .05, 1.684), "head", roll=up)
    bone("lip_corner_l", (MOUTH_HW, FRONT + .035, MOUTH_LINE), (MOUTH_HW, FRONT - .015, MOUTH_LINE), "head", roll=up)
    bone("jaw", (0, .12, MOUTH_Z0), (0, FRONT, MOUTH_Z0), "head", roll=up)
    bone("lip_upper", (0, FRONT + .035, 1.535), (0, FRONT - .015, 1.535), "head", roll=up)
    if ADV:
        bone("backpack", (0, .19, 1.10), (0, .19, 1.30), "spine_03")

    # animation controls (not exported)
    bone("CTRL_foot_ik_l", (LEG_X, 0, .095), (LEG_X, -.11, .035), "root", roll=up, deform=False)
    bone("CTRL_knee_pole_l", (LEG_X, -.55, .48), (LEG_X, -.60, .48), "root", roll=up, deform=False)
    bone("CTRL_hand_ik_l", (ARM_X, 0, .815), (ARM_X, 0, KNUCKLE_Z), "root", roll=palm, deform=False)
    bone("CTRL_elbow_pole_l", (ARM_X, .45, 1.07), (ARM_X, .50, 1.07), "root", roll=up, deform=False)
    bone("CTRL_eyes_target", (0, -1.0, EYE_Z), (0, -1.05, EYE_Z), "head", roll=up, deform=False)
    bone("CTRL_eye_target_l", (EYE_X, -1.0, EYE_Z), (EYE_X, -1.03, EYE_Z), "CTRL_eyes_target", roll=up, deform=False)


# ---------------------------------------------------------------- scene assembly
def new_coll(name, parent=None):
    c = bpy.data.collections.new(name)
    (parent or bpy.context.scene.collection).children.link(c)
    return c


def make_widgets(coll):
    def wgt(name, verts, edges):
        me = bpy.data.meshes.new(name)
        me.from_pydata(verts, edges, [])
        o = bpy.data.objects.new(name, me)
        coll.objects.link(o)
        return o
    seg = 32
    circ = [(math.cos(2 * math.pi * i / seg), 0, math.sin(2 * math.pi * i / seg)) for i in range(seg)]
    ring_edges = [(i, (i + 1) % seg) for i in range(seg)]
    cube = [(x, y, z) for z in (-1, 1) for y in (-1, 1) for x in (-1, 1)]
    cube_edges = [(0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)]
    dia = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    dia_edges = [(a, b) for a in range(2) for b in range(2, 6)] + [(2, 4), (2, 5), (3, 4), (3, 5)]
    root_pts = [(x, z, 0) for x, _, z in circ] + [(0, 1.0, 0), (0, 1.35, 0), (-.15, 1.2, 0), (.15, 1.2, 0)]
    root_edges = ring_edges + [(seg, seg + 1), (seg + 1, seg + 2), (seg + 1, seg + 3)]
    return {"circle": wgt("WGT_circle", circ, ring_edges), "box": wgt("WGT_box", cube, cube_edges),
            "diamond": wgt("WGT_diamond", dia, dia_edges), "root": wgt("WGT_root", root_pts, root_edges)}


def make_material(img):
    mat = bpy.data.materials.new("M_BetterSteve")
    try:
        mat.use_nodes = True
    except Exception:
        pass
    nt = mat.node_tree
    bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    tex.interpolation = "Closest"
    tex.location = (-400, 200)
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = .8
    return mat


def build_rig(coll, widgets):
    arm = bpy.data.armatures.new("BetterSteve_Skeleton")
    obj = bpy.data.objects.new("Armature", arm)   # "Armature" -> UE drops the node and roots at bone "root"
    coll.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    for name, h, t, parent, roll, deform, connect in BONES:
        eb = arm.edit_bones.new(name)
        eb.head, eb.tail = Vector(h), Vector(t)
        eb.align_roll(Vector(roll))
        eb.use_deform = deform
    for name, h, t, parent, roll, deform, connect in BONES:
        if parent:
            eb = arm.edit_bones[name]
            eb.parent = arm.edit_bones[parent]
            eb.use_connect = connect
    bpy.ops.object.mode_set(mode="OBJECT")
    arm.display_type = "OCTAHEDRAL"
    obj.show_in_front = True

    P = obj.pose.bones
    for pb in P:
        pb.rotation_mode = "XYZ"

    # --- collections + colours
    bc = {n: arm.collections.new(n) for n in ("Body", "Fingers", "Face", "IK Controls")}
    face_bones = {"eye", "eyelid", "brow", "lip", "jaw"}
    for b in arm.bones:
        n = b.name
        if n.startswith("CTRL_") or n == "root":
            grp, pal = "IK Controls", "THEME09"
        elif any(n.startswith(f) for f in face_bones):
            grp, pal = "Face", "THEME03"
        elif any(n.startswith(f) for f in ("thumb", "index", "middle", "ring", "pinky")):
            grp, pal = "Fingers", None
        else:
            grp, pal = "Body", None
        if pal is None:
            pal = "THEME04" if n.endswith("_l") else "THEME01" if n.endswith("_r") else "THEME09"
        bc[grp].assign(b)
        b.color.palette = pal

    # --- custom shapes (sizes in metres)
    def shape(name, w, size, offset=(0, 0, 0), world_aligned=False, mid=False):
        pb = P[name]
        pb.custom_shape = widgets[w]
        pb.use_custom_shape_bone_size = False
        pb.custom_shape_scale_xyz = size if isinstance(size, tuple) else (size, size, size)
        rest = pb.bone.matrix_local.to_3x3()
        off = Vector(offset)
        if world_aligned:
            pb.custom_shape_rotation_euler = rest.inverted().to_euler()
            off = rest.inverted() @ off
        if mid:
            off = off + Vector((0, pb.bone.length / 2, 0))
        pb.custom_shape_translation = off

    shape("root", "root", .45)
    shape("pelvis", "circle", .26)
    for n, r in (("spine_01", .2), ("spine_02", .21), ("spine_03", .23), ("neck_01", .09), ("head", .26)):
        shape(n, "circle", r, mid=True)
    for s in (1, -1):
        n = lambda nm: side_name(nm, s)
        shape(n("clavicle_l"), "circle", .05, mid=True)
        for nm, r in (("upperarm_l", .085), ("lowerarm_l", .075), ("hand_l", .065), ("thigh_l", .11),
                      ("calf_l", .095), ("foot_l", .08), ("ball_l", .075)):
            shape(n(nm), "circle", r, mid=True)
        for fname, _, _ in FINGERS + [("thumb", 0, 0)]:
            shape(n(f"{fname}_01_l"), "circle", .016, mid=True)
            shape(n(f"{fname}_02_l"), "circle", .014, mid=True)
        shape(n("CTRL_foot_ik_l"), "box", (.085, .15, .01), (0, -.045, -.093), world_aligned=True)
        shape(n("CTRL_hand_ik_l"), "box", (.035, .055, .06), (0, 0, -.045), world_aligned=True)
        shape(n("CTRL_knee_pole_l"), "diamond", .03)
        shape(n("CTRL_elbow_pole_l"), "diamond", .03)
        shape(n("eye_l"), "circle", .03, (0, .2, 0))
        shape(n("eyelid_upper_l"), "box", (.045, .004, .004), world_aligned=True, offset=(0, -.012, 0))
        shape(n("eyelid_lower_l"), "box", (.045, .004, .004), world_aligned=True, offset=(0, -.012, 0))
        shape(n("brow_l"), "box", (.05, .012, .012), world_aligned=True)
        shape(n("lip_corner_l"), "diamond", .012, (0, .05, 0))
        shape(n("CTRL_eye_target_l"), "circle", .04)
    shape("jaw", "box", (.07, .012, .012), world_aligned=True, offset=(0, -.33, -.03))
    shape("lip_upper", "box", (.04, .008, .008), world_aligned=True, offset=(0, -.05, .012))
    shape("CTRL_eyes_target", "box", (.18, .01, .06), world_aligned=True)
    if "backpack" in P:
        shape("backpack", "box", (.18, .1, .21), (0, 0, .13), world_aligned=True)

    # --- switches on the root bone
    root = P["root"]
    for prop, val, desc in (("leg_ik_l", 1.0, "Left leg: 1 = IK foot control, 0 = FK"),
                            ("leg_ik_r", 1.0, "Right leg: 1 = IK foot control, 0 = FK"),
                            ("arm_ik_l", 0.0, "Left arm: 1 = IK hand control, 0 = FK"),
                            ("arm_ik_r", 0.0, "Right arm: 1 = IK hand control, 0 = FK"),
                            ("eyes_track", 1.0, "1 = eyes aim at CTRL_eyes_target, 0 = rotate eye bones by hand")):
        root[prop] = val
        root.id_properties_ui(prop).update(min=0.0, max=1.0, soft_min=0.0, soft_max=1.0, description=desc)

    def drive(con, prop):
        d = con.driver_add("influence").driver
        d.type = "AVERAGE"
        v = d.variables.new()
        v.type = "SINGLE_PROP"
        v.targets[0].id_type = "OBJECT"
        v.targets[0].id = obj
        v.targets[0].data_path = f'pose.bones["root"]["{prop}"]'

    iks = []
    for s in (1, -1):
        n = lambda nm: side_name(nm, s)
        sd = "l" if s > 0 else "r"
        for owner, end, target, pole, prop in (("calf_l", "foot_l", "CTRL_foot_ik_l", "CTRL_knee_pole_l", "leg_ik"),
                                               ("lowerarm_l", "hand_l", "CTRL_hand_ik_l", "CTRL_elbow_pole_l", "arm_ik")):
            ik = P[n(owner)].constraints.new("IK")
            ik.target, ik.subtarget = obj, n(target)
            ik.pole_target, ik.pole_subtarget = obj, n(pole)
            ik.chain_count = 2
            drive(ik, f"{prop}_{sd}")
            cr = P[n(end)].constraints.new("COPY_ROTATION")
            cr.target, cr.subtarget = obj, n(target)
            drive(cr, f"{prop}_{sd}")
            iks.append((ik, f"{prop}_{sd}", [n(owner), side_name(owner.replace("calf", "thigh").replace("lowerarm", "upperarm"), s)]))
        dt = P[n("eye_l")].constraints.new("DAMPED_TRACK")
        dt.target, dt.subtarget, dt.track_axis = obj, n("CTRL_eye_target_l"), "TRACK_Y"
        drive(dt, "eyes_track")

    # --- pick each IK pole angle so the chain sits exactly in its rest pose
    for ik, prop, chain in iks:
        old = root[prop]
        root[prop] = 1.0
        best = None
        for ang in (-90, 0, 90, 180):
            ik.pole_angle = math.radians(ang)
            obj.update_tag()
            bpy.context.view_layer.update()
            err = sum(P[c].matrix.to_3x3().col[0].angle(P[c].bone.matrix_local.to_3x3().col[0]) for c in chain)
            if best is None or err < best[1]:
                best = (ang, err)
        ik.pole_angle = math.radians(best[0])
        root[prop] = old
        print(f"[rig] {ik.id_data.name}:{chain[0]} pole_angle {best[0]} (rest error {math.degrees(best[1]):.3f} deg)")
    obj.data.pose_position = "POSE"
    return obj


# ---------------------------------------------------------------- animation helpers
def rest3(pb):
    return pb.bone.matrix_local.to_3x3()


def apply_pose(arm, spec):
    """spec: bone -> {"loc": world delta, "rot": [(axis, deg), ...] applied in order about rest-pose world axes}."""
    for pb in arm.pose.bones:
        pb.location = (0, 0, 0)
        pb.rotation_euler = (0, 0, 0)
    for name, d in spec.items():
        pb = arm.pose.bones[name]
        r = rest3(pb)
        if "loc" in d:
            pb.location = r.inverted() @ Vector(d["loc"])
        if "rot" in d:
            R = Matrix.Identity(3)
            for axis, deg in d["rot"]:
                R = Matrix.Rotation(math.radians(deg), 3, axis) @ R
            pb.rotation_euler = (r.inverted() @ R @ r).to_euler("XYZ")


def bake_action(arm, name, frames, pose_fn):
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    arm.animation_data_create()
    arm.animation_data.action = act
    keyed = set()
    specs = {f: pose_fn(f) for f in frames}
    for s in specs.values():
        keyed |= set(s)
    for f in frames:
        apply_pose(arm, specs[f])
        for n in sorted(keyed):
            pb = arm.pose.bones[n]
            pb.keyframe_insert("location", frame=f)
            pb.keyframe_insert("rotation_euler", frame=f)
    act.use_frame_range = True
    act.frame_start, act.frame_end = frames[0], frames[-1]
    act.use_cyclic = name in ("BS_Idle", "BS_Walk")
    return act


X_, Y_, Z_ = "X", "Y", "Z"


def mirror_side(spec_l):
    """Mirror a left-side partial pose onto the right side (world X flipped)."""
    out = {}
    for n, d in spec_l.items():
        m = {}
        if "loc" in d:
            m["loc"] = (-d["loc"][0], d["loc"][1], d["loc"][2])
        if "rot" in d:
            m["rot"] = [(a, deg if a == "X" else -deg) for a, deg in d["rot"]]
        out[side_name(n, -1)] = m
    return out


def face(blink=0.0, smile=0.0, jaw=0.0, brow_up=0.0, brow_tilt=0.0, lid_down=0.0, lower_up=0.0, look=(0, 0),
         corner_out=0.0):
    """Expression parameters -> pose spec for both sides (brow_tilt > 0 = angry, < 0 = sad)."""
    s = {}
    lid = (EYE_Z1 - EYE_Z0) * max(blink, lid_down)
    left = {
        "eyelid_upper_l": {"loc": (0, 0, -lid)},
        "eyelid_lower_l": {"loc": (0, 0, max(lower_up, blink * .012))},
        "brow_l": {"loc": (0, 0, brow_up), "rot": [(Y_, -brow_tilt)]},
        "lip_corner_l": {"loc": (corner_out, 0, smile * .012)},
    }
    s.update(left)
    s.update(mirror_side(left))
    s["jaw"] = {"rot": [(X_, 3.4 * jaw)]}
    s["CTRL_eyes_target"] = {"loc": (look[0], 0, look[1])}
    return s


def walk_pose(f):
    ph = 2 * math.pi * f / 32
    feet = {0: (-.22, .014, -12), 4: (-.11, 0, 0), 8: (0, 0, 0), 12: (.11, 0, 0), 16: (.22, .06, 22),
            20: (.12, .09, 12), 24: (-.01, .10, 0), 28: (-.14, .05, -8)}
    s = {}
    for side, off in (("l", 0), ("r", 16)):
        dy, dz, pitch = feet[(f + off) % 32]
        s[f"CTRL_foot_ik_{side}"] = {"loc": (0, dy, dz), "rot": [(X_, pitch)]}
    s["pelvis"] = {"loc": (.015 * math.sin(ph), 0, -.015 - .015 * math.cos(2 * ph)), "rot": [(Z_, -6 * math.cos(ph))]}
    s["spine_03"] = {"rot": [(Z_, 8 * math.cos(ph))]}
    s["head"] = {"rot": [(Z_, -2 * math.cos(ph))]}
    for side, sg in (("l", 1), ("r", -1)):
        swing = 20 * math.cos(ph) * sg
        s[f"upperarm_{side}"] = {"rot": [(Y_, -5 * sg), (X_, swing)]}
        s[f"lowerarm_{side}"] = {"rot": [(X_, -(12 + 14 * (1 - math.cos(ph) * sg) / 2))]}
    s.update(face())
    return s


def idle_pose(f):
    br = math.sin(2 * math.pi * f / 90)
    s = {"spine_02": {"rot": [(X_, -1.2 * br)]}, "spine_03": {"rot": [(X_, -.8 * br)]},
         "head": {"rot": [(X_, 1.5 * br)]}, "pelvis": {"loc": (0, 0, -.004 * (1 - br) / 2)}}
    for side, sg in (("l", 1), ("r", -1)):
        s[f"upperarm_{side}"] = {"rot": [(Y_, -4 * sg), (X_, 1.5 * br)]}
        s[f"lowerarm_{side}"] = {"rot": [(X_, -8)]}
    t = f - 52
    blink = t / 2 if 0 <= t <= 2 else 1 if 2 < t <= 3 else 1 - (t - 3) / 3 if 3 < t <= 6 else 0
    look = {18: .10, 40: .10, 44: -.06, 70: -.06, 74: 0}
    lx = 0
    for k in sorted(look):
        if f >= k:
            lx = look[k]
    s.update(face(blink=blink, look=(lx, 0)))
    return s


IDLE_FRAMES = sorted(set(range(0, 91, 6)) | {14, 18, 40, 44, 52, 54, 55, 58, 70, 74, 90})


def face_test_pose(f):
    talk = {50: 0, 54: .8, 58: .2, 62: 1, 66: .3, 70: .7, 74: 0, 78: .9, 82: .4, 86: .6, 90: 0}
    table = {
        0: {}, 8: {}, 10: dict(blink=1), 11: dict(blink=1), 14: {}, 20: {},
        30: dict(smile=1, lower_up=.006, brow_up=.003, corner_out=.004), 40: dict(smile=1, lower_up=.006, brow_up=.003, corner_out=.004),
        46: {},
        100: dict(brow_up=.012, jaw=1, corner_out=-.004), 112: dict(brow_up=.012, jaw=1, corner_out=-.004),
        120: dict(brow_up=-.006, brow_tilt=14, lower_up=.01, lid_down=.18, smile=-.7),
        132: dict(brow_up=-.006, brow_tilt=14, lower_up=.01, lid_down=.18, smile=-.7),
        140: dict(brow_up=.004, brow_tilt=-14, lid_down=.28, smile=-.9, look=(0, -.15)),
        152: dict(brow_up=.004, brow_tilt=-14, lid_down=.28, smile=-.9, look=(0, -.15)),
        160: dict(look=(.25, 0)), 170: dict(look=(-.25, 0)), 180: {},
    }
    for k, v in talk.items():
        table[k] = dict(jaw=v)
    return face(**table[f])


FACE_FRAMES = [0, 8, 10, 11, 14, 20, 30, 40, 46, 50, 54, 58, 62, 66, 70, 74, 78, 82, 86, 90, 100, 112, 120, 132, 140,
               152, 160, 170, 180]


def flex_pose(f):
    s = face()
    if f == 20:   # deep crouch, arms reaching forward, fists
        s.update(face(jaw=.6, brow_up=.01))
        s["pelvis"] = {"loc": (0, .06, -.32)}
        s["spine_01"] = {"rot": [(X_, 14)]}
        s["spine_02"] = {"rot": [(X_, 10)]}
        s["head"] = {"rot": [(X_, -18)]}
        for side, sg in (("l", 1), ("r", -1)):
            s[f"upperarm_{side}"] = {"rot": [(X_, -75)]}
            s[f"lowerarm_{side}"] = {"rot": [(X_, -95)]}
            for fname, _, _ in FINGERS:
                s[f"{fname}_01_{side}"] = {"rot": [(Y_, 85 * sg)]}
                s[f"{fname}_02_{side}"] = {"rot": [(Y_, 95 * sg)]}
            s[f"thumb_01_{side}"] = {"rot": [(Y_, 35 * sg)]}
            s[f"thumb_02_{side}"] = {"rot": [(Y_, 50 * sg)]}
    elif f == 40:  # high knee, one arm out, one arm overhead, twist
        s.update(face(smile=1, lower_up=.006, look=(.2, .1)))
        s["CTRL_foot_ik_l"] = {"loc": (0, -.28, .42), "rot": [(X_, 15)]}
        s["pelvis"] = {"loc": (-.02, 0, -.02)}
        s["spine_02"] = {"rot": [(Z_, 18)]}
        s["spine_03"] = {"rot": [(Y_, 8)]}
        s["neck_01"] = {"rot": [(Y_, -10)]}
        s["head"] = {"rot": [(Z_, -15), (X_, 8)]}
        s["upperarm_l"] = {"rot": [(Y_, -85)]}
        s["lowerarm_l"] = {"rot": [(X_, -40)]}
        s["hand_l"] = {"rot": [(Y_, 30)]}
        s["upperarm_r"] = {"rot": [(Y_, 28), (X_, -160)]}
        s["lowerarm_r"] = {"rot": [(X_, -30)]}
        s["hand_r"] = {"rot": [(X_, -30)]}
        for fname, _, _ in FINGERS:
            s[f"{fname}_01_r"] = {"rot": [(Y_, -15)]}
    return s


# ---------------------------------------------------------------- main
def main():
    os.makedirs(FOUNDRY, exist_ok=True)
    os.makedirs(EXPORT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    scene.render.fps = 30

    img = make_atlas(os.path.join(EXPORT, "T_BetterSteve.png"))
    mat = make_material(img)

    char = new_coll("BetterSteve")
    ref = new_coll("Reference")
    wcol = new_coll("Widgets")
    widgets = make_widgets(wcol)

    define_bones()
    arm = build_rig(char, widgets)

    b = Builder()
    build_body(b)
    body = mesh_from_builder(b, "SK_BetterSteve", mat, char)
    body.parent = arm
    mod = body.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    deform = {bn.name for bn in arm.data.bones if bn.use_deform}
    groups = {}
    for i, w in enumerate(b.w):
        for bn, x in w.items():
            assert bn in deform, f"vertex group {bn} has no deform bone"
            if bn not in groups:
                groups[bn] = body.vertex_groups.new(name=bn)
            groups[bn].add([i], x, "REPLACE")
    unweighted = [bn for bn in deform if bn not in groups and bn != "root"]
    print(f"[rig] {len(arm.data.bones)} bones ({len(deform)} deform); deform bones without weights: {unweighted}")

    rb = Builder()
    build_vanilla_reference(rb, 1.0)
    vanilla = mesh_from_builder(rb, "REF_VanillaSteve", mat, ref)

    # --- actions
    acts = [bake_action(arm, "BS_Idle", IDLE_FRAMES, idle_pose),
            bake_action(arm, "BS_Walk", list(range(0, 33, 4)), walk_pose),
            bake_action(arm, "BS_FaceTest", FACE_FRAMES, face_test_pose),
            bake_action(arm, "BS_FlexTest", [0, 20, 40, 60], flex_pose)]
    arm.animation_data.action = None
    apply_pose(arm, {})
    bpy.context.view_layer.update()

    # rest check: every deform bone must sit exactly at rest with all constraints live
    worst = max((pb.matrix.to_translation() - pb.bone.matrix_local.to_translation()).length
                for pb in arm.pose.bones if pb.bone.use_deform)
    print(f"[rig] max deform-bone drift at rest with IK/tracking on: {worst * 1000:.4f} mm")

    co = [body.matrix_world @ v.co for v in body.data.vertices]
    mn = Vector([min(c[k] for c in co) for k in range(3)])
    mx = Vector([max(c[k] for c in co) for k in range(3)])
    print(f"[size] min {tuple(round(x, 4) for x in mn)} max {tuple(round(x, 4) for x in mx)}")
    print(f"[size] height {mx.z - mn.z:.4f} m (vanilla Steve {HEIGHT:.4f}); width {mx.x - mn.x:.3f}; depth {mx.y - mn.y:.3f}")
    tris = sum(len(p.vertices) - 2 for p in body.data.polygons)
    print(f"[mesh] {tris} triangles after triangulation")

    scene.frame_start, scene.frame_end = 0, 90
    for o in (vanilla,):
        o.hide_select = True
    wcol.hide_viewport = True
    wcol.hide_render = True
    ref.hide_render = True
    blend = os.path.join(FOUNDRY, "BetterSteve.blend")
    bpy.ops.wm.save_as_mainfile(filepath=blend, compress=True)
    print(f"[save] {blend}")

    # --- exports (mutates the scene into centimetres; nothing is saved after this)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import export_ue
    export_ue.export_all(EXPORT)


if __name__ == "__main__":  # importable: animation scripts reuse apply_pose, bake_action, face, constants
    main()
