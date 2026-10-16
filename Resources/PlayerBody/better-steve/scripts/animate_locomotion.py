"""Better Steve locomotion: procedural, foot-locked, in-place clips matched to Unnamed's movement speeds.

    blender -b foundry/BetterSteve.blend --python scripts/animate_locomotion.py -- <project root> [--no-export]

Adds these actions to foundry/BetterSteve.blend (earlier copies are replaced), saves it, then exports FBX + GLB:

    BS_Walk_<dir>        the game's walk, 4.317 blocks/s (4.405 on diagonals): an easy jog with a short flight, 18 f
    BS_Sprint_<dir>      sprint, 5.612 (5.727 on diagonals): a run, 16 f
    BS_CrouchWalk_<dir>  sneak, 1.295 (1.832 on diagonals: the game's sneak is 41% faster diagonally), 32 f
    BS_CrouchIdle        eyes at 1.27 (the game's crouching eye height), 60 f loop
    BS_StandToCrouch, BS_CrouchToStand   8 f, eased like the game's eye smoothing (half the gap per 50 ms tick)
    BS_Jump_Start        takeoff to apex, 9 f = the game's 0.3 s rise
    BS_Jump_Loop         in the air, 24 f loop (apex and any length of fall)
    BS_Jump_Land         impact, then recover to BS_Idle's first frame, 12 f
    BS_Look_<C|U|D|L|R|UL|UR|DL|DR>   Aim Offset poses: BS_Idle frame 0 looking at yaw/pitch 0/+-90 (2 equal frames);
                         make them mesh-space additive on BS_Look_C in Unreal

<dir> is F FR R BR B BL L FL = Blend Space direction 0 45 90 135 180 -135 -90 -45 (Unreal: + is to the right).
Loops key every frame (the last frame repeats the first) and start on the left foot's touchdown, so the clips of a
gait share phase. Everything is in place (the root never moves). A planted foot's contact point (heel, sole, then
toes) travels at exactly the game speed against the travel direction, so nothing slides at that speed.
Strafes turn the hips 60% of the way toward the travel direction and twist the spine back so the chest faces
forward; backward directions run backward with the hips turned the other way.
"""

import bpy
import math
import os
import sys

from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_better_steve as bs  # noqa: E402  (its argument parsing reads "-- <project root>")

FPS = 30
X_, Y_, Z_ = "X", "Y", "Z"

# Unnamed's movement: Source/Unnamed/Foundation/FoundationProfile.h and Tests/FoundationTests.cpp (blocks/s = m/s).
WALK, WALK_DIAG = 4.3171806, 4.40528634
SPRINT, SPRINT_DIAG = 5.6123348, 5.6123348 / .98     # ResolveInput: a diagonal reaches 1.0 where straight is 0.98
CROUCH, CROUCH_DIAG = 1.2951542, 1.8316246138
CROUCH_EYE = 1.27                                     # CrouchingEyesCm
EYE_SMOOTH, TICK = .5, .05                            # EyeSmoothingPerTick, TickSeconds
DIRS = [("F", 0), ("FR", 45), ("R", 90), ("BR", 135), ("B", 180), ("BL", -135), ("L", -90), ("FL", -45)]

arm = bpy.data.objects["Armature"]
body = bpy.data.objects["SK_BetterSteve"]
PB = arm.pose.bones
SIDES = (("l", 1), ("r", -1))


# ---------------------------------------------------------------- helpers
def ease(t):
    t = min(1., max(0., t))
    return t * t * (3 - 2 * t)


def lerp(a, b, w):
    if isinstance(a, tuple):
        return tuple(x + (y - x) * w for x, y in zip(a, b))
    return a + (b - a) * w


def mix(A, B, w):
    return {k: lerp(A[k], B[k], w) for k in A}


def keyed(keys, k):
    """Ease between (frame, params) keys."""
    for (f0, A), (f1, B) in zip(keys, keys[1:]):
        if k <= f1:
            return mix(A, B, ease((k - f0) / (f1 - f0)))
    return keys[-1][1]


def rotz(deg, xy):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return Vector((c * xy[0] - s * xy[1], s * xy[0] + c * xy[1]))


def side_pt(v, sx):
    return Vector((v.x * sx, v.y, v.z))


def foot_points():
    """Left-foot rest points from the mesh: ankle and ball joints, heel and toe pad on the sole, sole height."""
    gi = {g.name: g.index for g in body.vertex_groups}
    foot, toes = [], []
    for v in body.data.vertices:
        w = {g.group: g.weight for g in v.groups}
        co = body.matrix_world @ v.co
        if w.get(gi["foot_l"], 0) > .5:
            foot.append(co)
        elif w.get(gi["ball_l"], 0) > .5:
            toes.append(co)
    sole = min(c.z for c in foot + toes)
    heel = max(c.y for c in foot if c.z < sole + .01)
    return PB["foot_l"].bone.head_local.copy(), PB["ball_l"].bone.head_local.copy(), Vector((LEG_X, heel, sole)), sole


LEG_X = PB["foot_l"].bone.head_local.x
ANKLE, BALL, HEEL, SOLE = foot_points()
PELVIS = PB["pelvis"].bone.head_local.copy()
HIP = PB["thigh_l"].bone.head_local - PELVIS
LEG = ((PB["calf_l"].bone.head_local - PB["thigh_l"].bone.head_local).length +
       (PB["foot_l"].bone.head_local - PB["calf_l"].bone.head_local).length)


def place_foot(sx, ref, pitch, yaw):
    """Ankle position for a foot rolled by pitch (+ toes down) about its ground pivot: the heel when the toes are up,
    the ball joint when the heel is up. ref is the ankle's xy with the foot flat, so the pivot sits where it would
    on a flat foot and moves exactly with ref."""
    a0 = side_pt(ANKLE, sx)
    off = side_pt(HEEL if pitch < 0 else BALL, sx) - a0
    ry = Matrix.Rotation(math.radians(yaw), 3, "Z")
    r = ry @ Matrix.Rotation(math.radians(pitch), 3, "X")
    return Vector((ref.x, ref.y, a0.z)) + ry @ off - r @ off


def hip_joint(P, sx):
    r = Matrix.Rotation(math.radians(P["pel_yaw"]), 3, "Z") @ Matrix.Rotation(math.radians(P["pel_pitch"]), 3, "X")
    return PELVIS + Vector(P["pel"]) + r @ side_pt(HIP, sx)


def level(P, nod=0.):
    """Keep the eyes level: the head takes back most of the hip and spine lean."""
    P["head"] = -.85 * (P["pel_pitch"] + P["sp1"] + P["sp2"] + P["sp3"]) - P["neck"] + nod
    return P


# ---------------------------------------------------------------- posture parameters -> pose spec
STAND = dict(pel=(0., 0., -.002), pel_pitch=0., pel_yaw=0., sp1=0., sp2=0., sp3=0., sp1_yaw=0., sp2_yaw=0.,
             sp3_yaw=0., neck=0., neck_yaw=0., head=0., head_yaw=0., jaw=0., blink=0., lid=0., brow=0., look_x=0.,
             look_z=0., pack=0., pack_pitch=0.)
for _s, _ in SIDES:   # BS_Idle frame 0, so clips that end standing hand over to the idle cleanly
    STAND.update({f"ua_{_s}": 0., f"abd_{_s}": 4., f"el_{_s}": 8., f"wr_{_s}": 0., f"curl_{_s}": 0.,
                  f"foot_{_s}": (0., 0., 0.), f"pitch_{_s}": 0., f"fyaw_{_s}": 0., f"ball_{_s}": 0.,
                  f"pole_{_s}": (0., 0., 0.)})


def spec(P):
    s = {"pelvis": {"loc": P["pel"], "rot": [(X_, P["pel_pitch"]), (Z_, P["pel_yaw"])]},
         "neck_01": {"rot": [(X_, P["neck"]), (Z_, P["neck_yaw"])]},
         "head": {"rot": [(X_, P["head"]), (Z_, P["head_yaw"])]}}
    for i in (1, 2, 3):
        s[f"spine_0{i}"] = {"rot": [(X_, P[f"sp{i}"]), (Z_, P[f"sp{i}_yaw"])]}
    for side, sg in SIDES:
        s[f"upperarm_{side}"] = {"rot": [(Y_, -P[f"abd_{side}"] * sg), (X_, P[f"ua_{side}"])]}
        s[f"lowerarm_{side}"] = {"rot": [(X_, -P[f"el_{side}"])]}
        s[f"hand_{side}"] = {"rot": [(X_, P[f"wr_{side}"])]}
        c = P[f"curl_{side}"] * sg
        for fname, _, _ in bs.FINGERS:
            s[f"{fname}_01_{side}"] = {"rot": [(Y_, c)]}
            s[f"{fname}_02_{side}"] = {"rot": [(Y_, 1.15 * c)]}
        s[f"thumb_01_{side}"] = {"rot": [(Y_, .35 * c)]}
        s[f"thumb_02_{side}"] = {"rot": [(Y_, .5 * c)]}
        s[f"CTRL_foot_ik_{side}"] = {"loc": P[f"foot_{side}"],
                                     "rot": [(X_, P[f"pitch_{side}"]), (Z_, P[f"fyaw_{side}"])]}
        s[f"ball_{side}"] = {"rot": [(X_, P[f"ball_{side}"])]}
        s[f"CTRL_knee_pole_{side}"] = {"loc": P[f"pole_{side}"]}
    if "backpack" in PB:
        s["backpack"] = {"loc": (0, 0, P["pack"]), "rot": [(X_, P["pack_pitch"])]}
    s.update(bs.face(blink=P["blink"], jaw=P["jaw"], lid_down=P["lid"], brow_up=P["brow"],
                     look=(P["look_x"], P["look_z"])))
    return s


# ---------------------------------------------------------------- gaits
JOG = dict(family="Walk", v=WALK, v_diag=WALK_DIAG, N=18, run=True, S=.66, S_back=.56, fc=-.10, fc_back=.02,
           beta_max=.45, reach=.985, drop=-.05, bob=.022, sway=.012, H=.16, bow=.05, tan0=.6, tan1=.25,
           heel=10, toe=30, land=18, toe_b=20, kick=15, width=.06, hip=.6, twist=7, shoulder=8,
           arm=30, arm_bias=-4, el=78, el_sw=12, abd=10, curl=45, lean=(4, 3, 2), lean_back=(-2, -1, 0),
           pel_pitch=4, pel_pitch_back=0, jaw=0.)
SPRINT_G = dict(JOG, family="Sprint", v=SPRINT, v_diag=SPRINT_DIAG, N=16, S=.72, S_back=.58, fc=-.12, drop=-.065,
                bob=.028, H=.26, heel=6, toe=35, kick=20, width=.05, twist=10, shoulder=11, arm=48, arm_bias=-6,
                el=88, el_sw=14, curl=70, lean=(7, 5, 4), lean_back=(-3, -2, 0), pel_pitch=8, jaw=.25)
CROUCH_G = dict(JOG, family="CrouchWalk", v=CROUCH, v_diag=CROUCH_DIAG, N=32, run=False, S=1.05, S_back=1.05,
                fc=-.04, fc_back=.04, beta_max=.62, reach=.95, drop=0., bob=.008, sway=.015, H=.07, bow=.03,
                tan0=.8, tan1=.6, heel=8, toe=18, land=12, toe_b=14, kick=8, width=.10, twist=4, shoulder=4,
                arm=12, arm_bias=-10, el=32, el_sw=6, abd=7, curl=20, lean=(0, 0, 0), lean_back=(-3, -2, -1),
                pel_pitch=0, pel_pitch_back=-4, jaw=0.)


class Gait:
    """One direction of one gait. Phase p in [0, 1): the left foot touches down at 0, the right at 0.5; each foot
    is planted for the first beta of its own cycle."""

    def __init__(self, g, phi, base):
        self.g, self.phi = g, phi
        self.v = g["v_diag"] if phi % 90 else g["v"]
        self.N = g["N"]
        self.T = self.N / FPS
        self.back = abs(phi) > 90
        psi = g["hip"] * ((phi - math.copysign(180, phi)) if self.back else phi)   # hips, Unreal degrees (+ right)
        self.yaw = -psi                                                             # Blender Z (+ left)
        self.rel = math.radians(phi - psi)                                          # travel vs hips
        a = math.radians(phi)
        self.d = Vector((-math.sin(a), -math.cos(a)))   # travel in Blender xy: forward -Y, right -X
        self.sgn = -1 if self.back else 1
        self.base = dict(base)
        for i, l in enumerate(g["lean_back"] if self.back else g["lean"]):
            self.base[f"sp{i + 1}"] += l
        self.base["pel_pitch"] += g["pel_pitch_back"] if self.back else g["pel_pitch"]
        self.fc = g["fc_back"] if self.back else g["fc"]
        w = g["width"] * (1 + .6 * abs(math.sin(self.rel)))
        pel_xy = Vector(self.base["pel"][:2])
        self.Q = {s: pel_xy + rotz(self.yaw, (sx * w, 0)) for s, sx in SIDES}
        S, self.reach_max = (g["S_back"] if self.back else g["S"]), 0.
        for _ in range(60):   # shrink the stride until no leg stretches past `reach` of its length
            self.beta = min(g["beta_max"], S / (self.v * self.T))
            self.S = self.v * self.beta * self.T
            self.reach_max = max(self.reach(k / 2) for k in range(2 * self.N))
            if self.reach_max <= g["reach"]:
                break
            S = self.S * .97

    def reach(self, k):
        P = self.params(k)
        return max((side_pt(ANKLE, sx) + Vector(P[f"foot_{s}"]) - hip_joint(P, sx)).length / LEG for s, sx in SIDES)

    def stance_pitch(self, q):
        g = self.g
        if not self.back:   # heel strike, flat, toe-off
            if q < .3:
                return -g["heel"] * (1 - ease(q / .3))
            return 0. if q < .55 else g["toe"] * ((q - .55) / .45) ** 2
        if q < .35:         # backward: land on the ball, roll flat, push off the ball
            return g["land"] * (1 - ease(q / .35))
        return 0. if q < .6 else g["toe_b"] * ((q - .6) / .4) ** 2

    def stance(self, side, sx, q):
        ref = self.Q[side] + self.d * (self.fc + self.S * (.5 - q))
        pitch = self.stance_pitch(q)
        return place_foot(sx, ref, pitch, self.yaw), pitch, (-pitch if pitch > 0 else 0.)

    def swing(self, side, sx, u):
        g, b, e = self.g, self.beta, 1e-3
        A0, p0, b0 = self.stance(side, sx, 1.)
        A1, p1, b1 = self.stance(side, sx, 0.)
        k = (1 - b) / b / e   # d/dq -> d/du
        m0 = (A0 - self.stance(side, sx, 1 - e)[0]) * k * g["tan0"]
        m1 = (self.stance(side, sx, e)[0] - A1) * k * g["tan1"]
        u2, u3 = u * u, u * u * u   # horizontal: Hermite, so the foot leaves and lands moving with the ground
        A = A0 * (2 * u3 - 3 * u2 + 1) + m0 * (u3 - 2 * u2 + u) + A1 * (3 * u2 - 2 * u3) + m1 * (u3 - u2)
        A.z = A0.z + (A1.z - A0.z) * ease(u) + g["H"] * math.sin(math.pi * u ** .75) ** 2
        perp = Vector((-self.d.y, self.d.x))   # bow the swing foot out where the legs cross (strafes)
        out = 1 if (self.Q[side] - Vector(self.base["pel"][:2])).dot(perp) >= 0 else -1
        bow = g["bow"] * abs(math.sin(self.rel)) * math.sin(math.pi * u) * out
        A.x += perp.x * bow
        A.y += perp.y * bow
        if u < .3:
            pitch = p0 + g["kick"] * ease(u / .3)
        else:
            pitch = p0 + g["kick"] + (p1 - p0 - g["kick"]) * ease((u - .3) / .55)
        ball = b0 * (1 - ease(u / .3)) + b1 * ease((u - .7) / .3)
        # never below the ground: the pivot's own height for this pitch, plus the toes if they point down
        floor = place_foot(sx, A.xy, pitch, self.yaw).z + .09 * math.sin(math.radians(max(0., pitch + ball)))
        A.z = max(A.z, floor)
        return A, pitch, ball

    def params(self, k):
        g, b, sg = self.g, self.beta, self.sgn
        p = (k / self.N) % 1
        P = dict(self.base)
        c1 = math.cos(2 * math.pi * p) * sg   # +1: left foot at its forward extreme, left arm back
        bob = g["bob"] * math.cos(4 * math.pi * (p - b / 2)) * (-1 if g["run"] else 1)   # runs: low at midstance
        sway = g["sway"] * math.cos(2 * math.pi * (p - b / 2))   # toward the planted foot
        axis = rotz(self.yaw, (1, 0))
        x, y, z = P["pel"]
        P["pel"] = (x + sway * axis.x, y + sway * axis.y, z + g["drop"] + bob)
        P["pel_yaw"] = self.yaw - g["twist"] * c1
        sh = g["shoulder"] * c1
        P["sp1_yaw"] = -.45 * P["pel_yaw"]
        P["sp2_yaw"] = -.35 * P["pel_yaw"]
        P["sp3_yaw"] = -.2 * P["pel_yaw"] + sh
        P["head_yaw"] = -.9 * sh
        level(P)
        P["jaw"] = g["jaw"]
        lag = g["bob"] * math.cos(4 * math.pi * (p - b / 2) - 1.0) * (-1 if g["run"] else 1)
        P["pack"], P["pack_pitch"] = -.5 * lag, -60 * lag
        for side, sx in SIDES:
            P[f"ua_{side}"] = g["arm_bias"] + g["arm"] * c1 * sx
            P[f"el_{side}"] = g["el"] - g["el_sw"] * c1 * sx
            P[f"abd_{side}"] = g["abd"]
            P[f"curl_{side}"] = g["curl"]
            ph = (p + (0 if side == "l" else .5)) % 1
            A, pitch, ball = self.stance(side, sx, ph / b) if ph < b else self.swing(side, sx, (ph - b) / (1 - b))
            P[f"foot_{side}"] = tuple(A - side_pt(ANKLE, sx))
            P[f"pitch_{side}"], P[f"fyaw_{side}"], P[f"ball_{side}"] = pitch, self.yaw, ball
            rest = PB[f"CTRL_knee_pole_{side}"].bone.head_local
            pole = Vector(P["pel"][:2]) + rotz(self.yaw, rest.xy)
            P[f"pole_{side}"] = (pole.x - rest.x, pole.y - rest.y, .6 * P["pel"][2])
        return P


# ---------------------------------------------------------------- crouch and jump postures
def crouch_base(pz):
    P = dict(STAND, pel=(0., .05, pz), pel_pitch=12., sp1=8., sp2=6., sp3=4., neck=-8.)
    for s, _ in SIDES:
        P.update({f"ua_{s}": -10., f"abd_{s}": 7., f"el_{s}": 30., f"curl_{s}": 15.})
    return level(P)


def eye_z(P):
    bs.apply_pose(arm, spec(P))
    bpy.context.view_layer.update()
    return (arm.matrix_world @ PB["eye_l"].head).z


def solve_crouch():
    """Pelvis drop that puts the eyes at the game's crouching eye height (secant method)."""
    a, b = -.20, -.30
    fa, fb = eye_z(crouch_base(a)) - CROUCH_EYE, eye_z(crouch_base(b)) - CROUCH_EYE
    for _ in range(8):
        c = b - fb * (b - a) / (fb - fa)
        a, fa, b, fb = b, fb, c, eye_z(crouch_base(c)) - CROUCH_EYE
        if abs(fb) < 1e-4:
            break
    return crouch_base(b)


def on_toes(P, pitch):
    for s, sx in SIDES:
        a0 = side_pt(ANKLE, sx)
        P[f"foot_{s}"] = tuple(place_foot(sx, a0.xy, pitch, 0) - a0)
        P[f"pitch_{s}"], P[f"ball_{s}"] = pitch, -pitch
    return P


def air(P, side, dy, dz, pitch):
    P[f"foot_{side}"], P[f"pitch_{side}"], P[f"ball_{side}"] = (0., dy, dz), pitch, 0.
    return P


def arms(P, ua, abd, el, curl):
    for s, _ in SIDES:
        P.update({f"ua_{s}": ua, f"abd_{s}": abd, f"el_{s}": el, f"curl_{s}": curl})
    return P


LAUNCH = level(on_toes(arms(dict(STAND, pel=(0., 0., .025), pel_pitch=3., sp1=2.), -55, 10, 25, 20), 22))
RISE = level(air(air(arms(dict(STAND, pel=(0., 0., .02), sp1=4.), -80, 22, 35, 20), "l", -.06, .20, 20),
                 "r", .06, .12, 28))
APEX = level(air(air(arms(dict(STAND, pel=(0., 0., 0.), sp1=3., sp2=2.), -45, 32, 40, 25), "l", -.08, .17, 8),
                 "r", .07, .10, 18), nod=4)
TOUCH = dict(APEX, **{f"{k}_{s}": STAND[f"{k}_{s}"] for k in ("foot", "pitch", "ball") for s, _ in SIDES})
ABSORB = level(arms(dict(STAND, pel=(0., .03, -.13), pel_pitch=8., sp1=6., sp2=4.), -25, 18, 35, 15))


def jump_loop(k):
    a = math.sin(2 * math.pi * k / 24)
    P = dict(APEX)
    P["ua_l"], P["ua_r"] = APEX["ua_l"] + 6 * a, APEX["ua_r"] - 6 * a
    P["abd_l"], P["abd_r"] = APEX["abd_l"] + 3 * a, APEX["abd_r"] + 3 * a
    for s, sg in SIDES:
        x, y, z = APEX[f"foot_{s}"]
        P[f"foot_{s}"] = (x, y, z + .02 * a * sg)
        P[f"pitch_{s}"] = APEX[f"pitch_{s}"] + 3 * a * sg
    x, y, z = APEX["pel"]
    P["pel"] = (x, y, z + .008 * a)
    return P


# ---------------------------------------------------------------- look poses (Aim Offset)
# (tag, yaw, pitch) in Unreal's Aim Offset terms: yaw + is to the right, pitch + is up; the game's pitch reaches +/-90.
LOOK = [("C", 0, 0), ("U", 0, 90), ("D", 0, -90), ("L", -90, 0), ("R", 90, 0),
        ("UL", -90, 90), ("UR", 90, 90), ("DL", -90, -90), ("DR", 90, -90)]


def look_spec(yaw, pitch):
    """BS_Idle frame 0 turned to look at (yaw, pitch). The turn (yaw about the vertical, then pitch about the head's
    own side axis) is shared by spine, neck and head as fractions of the same rotation, so the chain adds up to it
    exactly and 90 degrees doesn't snap the neck. The eyes lead a little, the upper lids follow a downward look and
    the brows lift for an upward one."""
    P = dict(STAND)
    P["look_x"], P["look_z"] = -.10 * yaw / 90, .08 * pitch / 90
    P["lid"], P["brow"] = .25 * max(0., -pitch) / 90, .004 * max(0., pitch) / 90
    s = spec(P)
    turn = (Matrix.Rotation(math.radians(-yaw), 3, "Z") @ Matrix.Rotation(math.radians(-pitch), 3, "X")).to_quaternion()
    for bone, w in (("spine_02", .05), ("spine_03", .15), ("neck_01", .25), ("head", .55)):
        e = Quaternion().slerp(turn, w).to_euler("XYZ")   # apply_pose order X, Y, Z = Euler XYZ
        s[bone] = {"rot": [(X_, math.degrees(e.x)), (Y_, math.degrees(e.y)), (Z_, math.degrees(e.z))]}
    return s


# ---------------------------------------------------------------- bake
def bake(name, frames, fn, loop, family, meta=None, raw=False):
    old = bpy.data.actions.get(name)
    if old:
        bpy.data.actions.remove(old)
    act = bs.bake_action(arm, name, list(frames), fn if raw else lambda f: spec(fn(f)))
    act.use_cyclic = loop
    act["bs_family"], act["bs_loop"] = family, int(loop)
    for k, v in (meta or {}).items():
        act[k] = v
    return act


def main():
    arm.animation_data_create().action = None
    crouch = solve_crouch()
    print(f"[crouch] pelvis z {crouch['pel'][2]:+.4f} m -> eyes {eye_z(crouch):.4f} (target {CROUCH_EYE})")
    for g, base in ((JOG, STAND), (SPRINT_G, STAND), (CROUCH_G, crouch)):
        for tag, phi in DIRS:
            G = Gait(g, phi, base)
            name = f"BS_{g['family']}_{tag}"
            bake(name, range(G.N + 1), G.params, True, g["family"],
                 {"bs_speed": G.v, "bs_dir": float(phi), "bs_travel": tuple(G.d), "bs_beta": G.beta, "bs_cycle": G.N})
            step = G.v * G.T / 2
            flight = (.5 - G.beta) * G.T
            print(f"[gait] {name:20s} {G.v:.4f} m/s  {2 / G.T:.2f} steps/s  step {step:.3f} m  planted {G.S:.3f} m "
                  f"(beta {G.beta:.3f})  flight/step {max(0., flight) * 1000:.0f} ms  max leg reach {G.reach_max:.3f}")

    idle_n = 60

    def crouch_idle(k):
        br = math.sin(2 * math.pi * k / idle_n)
        P = dict(crouch)
        P["sp2"], P["sp3"] = crouch["sp2"] - 1.0 * br, crouch["sp3"] - .7 * br
        x, y, z = crouch["pel"]
        P["pel"] = (x, y, z - .003 * (1 - math.cos(2 * math.pi * k / idle_n)) / 2)
        for s, _ in SIDES:
            P[f"ua_{s}"] = crouch[f"ua_{s}"] + 1.2 * br
        t = k - 34
        P["blink"] = t / 2 if 0 <= t <= 2 else 1 if 2 < t <= 3 else 1 - (t - 3) / 3 if 3 < t <= 6 else 0
        return level(P)

    bake("BS_CrouchIdle", range(idle_n + 1), crouch_idle, True, "Crouch")
    tn = 8   # the game's eye moves half the remaining gap per tick: ~94% there after 0.2 s

    def smooth_eye(k):
        return (1 - EYE_SMOOTH ** (k / FPS / TICK)) / (1 - EYE_SMOOTH ** (tn / FPS / TICK))

    bake("BS_StandToCrouch", range(tn + 1), lambda k: mix(STAND, crouch, smooth_eye(k)), False, "Crouch")
    bake("BS_CrouchToStand", range(tn + 1), lambda k: mix(crouch, STAND, smooth_eye(k)), False, "Crouch")
    bake("BS_Jump_Start", range(10), lambda k: keyed([(0, LAUNCH), (4, RISE), (9, APEX)], k), False, "Jump")
    bake("BS_Jump_Loop", range(25), jump_loop, True, "Jump")
    bake("BS_Jump_Land", range(13), lambda k: keyed([(0, TOUCH), (3, ABSORB), (12, STAND)], k), False, "Jump")
    for tag, yaw, pitch in LOOK:   # 2 identical frames: a 1-frame action can drop out of the FBX bake
        bake(f"BS_Look_{tag}", range(2), lambda k, y=yaw, p=pitch: look_spec(y, p), False, "Look",
             {"bs_look": (float(yaw), float(pitch))}, raw=True)

    arm.animation_data.action = None
    bs.apply_pose(arm, {})
    bpy.context.view_layer.update()
    bpy.ops.wm.save_mainfile(compress=True)
    print(f"[save] {bpy.data.filepath}")
    if "--no-export" not in sys.argv:
        import export_ue
        export_ue.export_all(bs.EXPORT)


if __name__ == "__main__":  # importable: check scripts reuse the gaits
    main()
