"""Procedural gait authoring on the CONTROL RIG (dense keys), designed for exact planted feet.

In place: the root never moves. During a stance the c_foot_ik control moves at exactly -v*d (cancelling the preview
travel), and every foot-roll pivot (heel, ball, toe tip) is a fixed offset of that control, so the ground contact point
moves with it whichever pivot is rolling. Phase 0 = left contact, 0.5 = right contact, shared by all directions.
"""

import math

from mathutils import Euler, Matrix, Quaternion, Vector

import up_anim as A
import up_config as C

TAU = 2 * math.pi


def smooth(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


def smoother(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * x * (x * (6 * x - 15) + 10)


def hermite(w, m0, m1):
    """Cubic from 0 to 1 with end slopes m0, m1."""
    w2, w3 = w * w, w * w * w
    return (w3 - 2 * w2 + w) * m0 + (-2 * w3 + 3 * w2) + (w3 - w2) * m1


def pspline(phi, keys):
    """Periodic Catmull-Rom through (phase, value) keys (phases in [0,1), sorted)."""
    n = len(keys)
    phi %= 1.0
    for i in range(n):
        p0, v0 = keys[i]
        p1, v1 = keys[(i + 1) % n]
        span = (p1 - p0) % 1.0 or 1.0
        t = ((phi - p0) % 1.0) / span
        if t <= 1.0 + 1e-9 and ((phi - p0) % 1.0) < span + 1e-9:
            pm, vm = keys[(i - 1) % n]
            p2, v2 = keys[(i + 2) % n]
            sm = (p0 - pm) % 1.0 or 1.0
            s2 = (p2 - p1) % 1.0 or 1.0
            m0 = (v1 - vm) / (span + sm) * span
            m1 = (v2 - v0) / (s2 + span) * span
            t2, t3 = t * t, t * t * t
            return (2 * t3 - 3 * t2 + 1) * v0 + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * v1 + (t3 - t2) * m1
    return keys[0][1]


def wrap180(a):
    return (a + 180.0) % 360.0 - 180.0


# ------------------------------------------------------------------ gait specs

GAITS = {
    # T, duty, swing lift, contact share, lean, bob, width, arm swing, elbow, pelvis yaw osc, chest counter, roll
    "Walk": dict(T=36 / 60, D=.25, D_diag=.25, lift=.17, lead=.47, lead_back=.5, lean=7.0, bob=.016, width=.085, arm=30.0, elbow=-72.0,
                 elbow_osc=10.0, pyaw=7.0, cyaw=9.0, proll=3.0, yaw_k=.42, strike=7.0, push=38.0, hip_margin=.965,
                 head_k=.8, crouch=0.0, stance_bias=.012),
    "Sprint": dict(T=32 / 60, D=.21, D_diag=.21, lift=.27, lead=.45, lead_back=.54, lean=13.0, bob=.02, width=.08, arm=48.0,
                   elbow=-88.0, elbow_osc=14.0, pyaw=9.0, cyaw=12.0, proll=3.5, yaw_k=.42, strike=4.0, push=46.0,
                   hip_margin=.965, head_k=.85, crouch=0.0, stance_bias=.01),
    # Sneak cycle 0.9 s (54 f) instead of 1.0667 s: the 1.8316 m/s diagonal needs 0.98 m steps at 1.0667 s, which
    # dropped the crouched hips to 0.27-0.40 m; at 0.9 s all eight directions hold 0.46-0.49 m (s03 exploration).
    "Sneak": dict(T=54 / 60, D=.58, D_diag=.47, lift=.075, lead=.5, lead_back=.52, lean=27.0, bob=.008, width=.105, arm=10.0,
                  elbow=-40.0, elbow_osc=6.0, pyaw=5.0, cyaw=6.0, proll=2.5, yaw_k=.38, strike=10.0, push=28.0,
                  hip_margin=.95, head_k=.95, crouch=1.0, stance_bias=.02),
}

SNEAK_BASE = {"pelvis_drop": .2, "pelvis_back": .06, "lean": 27.0}


class Gait:
    def __init__(self, family, direction):
        g = GAITS[family]
        self.family, self.dir = family, direction
        self.g = g
        self.T = g["T"]
        self.N = round(self.T * C.FPS)
        self.v = C.speed_of(family, direction)
        self.alpha = C.DIR_ANGLE[direction]
        self.d = Vector(C.dir_vector(direction))
        diag = C.DIRS.index(direction) % 2 == 1
        self.D = g["D_diag"] if diag else g["D"]
        self.stride = self.v * self.T            # in-place foot travel per cycle (m)
        self.stance_len = self.stride * self.D
        a = self.alpha
        # body/feet yaw toward travel (forward hemisphere) or away from it (backward hemisphere)
        if abs(a) <= 90.0:
            self.yaw = -g["yaw_k"] * a
            self.backward = False
        else:
            self.yaw = g["yaw_k"] * (180.0 - abs(a)) * (1 if a > 0 else -1)
            self.backward = True
        fwd = Vector((0, -1)).to_3d()
        fwd.rotate(Euler((0, 0, math.radians(self.yaw))))
        self.foot_fwd = fwd.to_2d().normalized()
        self.s_dir = self.d.dot(self.foot_fwd)   # travel along the foot axis (+ forward roll, - backward roll)
        # contact offset ahead of the hips along travel (the forefoot rocker adds reach at the back)
        self.a_on = self.stance_len * (g.get("lead_back", g["lead"]) if self.backward else g["lead"])
        self.hip_z = None

    # ---------------------------------------------------------------- feet
    def home(self, side):
        sgn = 1 if side == "l" else -1
        lateral = abs(self.d.x) > .5
        w = self.g["width"] + (.035 if lateral else 0.0)
        v = Vector((sgn * w, 0.0, 0.0))
        v.rotate(Euler((0, 0, math.radians(self.yaw))))
        return v.to_2d()

    def foot(self, side, phi):
        """Return foot control state: xy, z, yaw, heel_roll(-), ball_roll(+), tip_roll(+), stance flag, u."""
        g = self.g
        off = 0.0 if side == "l" else 0.5
        ps = (phi - off) % 1.0
        sgn = 1 if side == "l" else -1
        yaw = self.yaw + sgn * 4.0 + (1.5 if side == "l" else 0.0)   # toe-out, slight asymmetry
        home = self.home(side)
        x_on = home + self.d * self.a_on
        x_off = x_on - self.d * self.stance_len
        fw = max(abs(self.s_dir), .35)            # roll amount follows travel along the foot axis
        strike, push = g["strike"] * fw, g["push"] * fw
        if ps < self.D:                            # ---- stance
            u = ps / self.D
            xy = x_on - self.d * (self.v * self.T * ps)
            heel = ball = tip = 0.0
            if not self.backward:
                heel = -strike * (1 - smooth(u / .22))
                ball = push * smoother((u - .42) / .58) if u > .42 else 0.0
            else:                                  # time-reversed: land on the forefoot, leave from the heel
                ball = push * .8 * (1 - smoother(u / .45))
                heel = -strike * 1.4 * smooth((u - .7) / .3) if u > .7 else 0.0
            return dict(xy=xy, z=0.0, yaw=yaw, heel=heel, ball=ball, tip=tip, stance=True, u=u)
        w = (ps - self.D) / (1 - self.D)           # ---- swing
        dist = (x_on - x_off).length
        k = self.v * self.T * (1 - self.D) / max(dist, 1e-6)
        h = hermite(w, -.55 * k, -.7 * k)              # retract before touchdown (low foot speed at contact)
        xy = x_off + (x_on - x_off) * h
        peak = .38 if not self.backward else .5
        warp = (.5 - peak) / (peak * (1 - peak))
        lift = math.sin(math.pi * (w + warp * w * (1 - w))) ** 1.4   # finite lift-off speed, soft touchdown
        z = g["lift"] * lift
        if not self.backward:
            ball = push * (1 - smoother(w / .55))
            heel = -strike * smooth((w - .55) / .45)
        else:
            heel = -strike * 1.4 * (1 - smoother(w / .5))
            ball = push * .8 * smooth((w - .5) / .5)
        return dict(xy=xy, z=z, yaw=yaw, heel=heel, ball=ball, tip=0.0, stance=False, u=w)

    # ---------------------------------------------------------------- pelvis
    def pelvis(self, phi, hip_z):
        g = self.g
        bob = -g["bob"] * math.cos(2 * TAU * (phi - self.D / 2))      # low at mid-stance, high mid-flight
        bob += .0015 * math.cos(TAU * (phi - .1))                       # left/right asymmetry
        lat = Vector((1, 0)) * (g["stance_bias"] * math.cos(TAU * (phi - self.D / 2)))   # toward the stance foot
        fore = self.d * (.012 * math.sin(2 * TAU * (phi - self.D / 2)))
        xy = lat + fore
        if g["crouch"]:
            xy += Vector((0, SNEAK_BASE["pelvis_back"])) * g["crouch"]
        yaw = self.yaw - g["pyaw"] * math.cos(TAU * (phi - .03))
        roll = -g["proll"] * math.cos(TAU * (phi - self.D / 2 - .05))    # swing side drops (contralateral)
        pitch = g["lean"] * (.55 if g["crouch"] else .65) + 1.5 * math.sin(2 * TAU * (phi - .1))
        if self.backward:
            pitch -= g["lean"] * (.9 if not g["crouch"] else .3)
        return Vector((xy.x, xy.y, hip_z + bob)), yaw, roll, pitch

    def pelvis_matrix(self, phi, hip_z):
        loc, yaw, roll, pitch = self.pelvis(phi, hip_z)
        rest = Vector((0, 0, C.SPINE["pelvis"]))
        q = A.arm_quat([("X", pitch), ("Y", roll), ("Z", yaw)])
        return loc, q

    def hip_joint(self, side, phi, hip_z):
        loc, q = self.pelvis_matrix(phi, hip_z)
        sgn = 1 if side == "l" else -1
        hip_rest = Vector((sgn * (C.LEG_X[0] + C.LEG_X[1]) / 2, 0, C.J["hip_z"]))
        pel_rest = Vector((0, 0, C.SPINE["pelvis"]))
        return pel_rest + loc - Vector((0, 0, C.SPINE["pelvis"])) + q @ (hip_rest - pel_rest)


def ankle_from_foot(rig, state, side):
    """Ankle target (world) implied by a foot control state, from the reverse-foot geometry."""
    hx = (C.LEG_X[0] + C.LEG_X[1]) / 2 * (1 if side == "l" else -1)
    F = Matrix.Translation(Vector((state["xy"].x, state["xy"].y, state["z"]))) @ \
        Matrix.Rotation(math.radians(state["yaw"]), 4, "Z") @ Matrix.Translation((-hx, 0, 0))
    heel_p = Vector((hx, C.J["heel_y"], 0))
    tip_p = Vector((hx, C.J["toe_tip_y"], 0))
    ball_p = Vector((hx, C.J["ball_y"], C.J["ball_z"]))
    ankle = Vector((hx, 0, C.J["ankle_z"]))

    def about(p, ang):
        return Matrix.Translation(p) @ Matrix.Rotation(math.radians(ang), 4, "X") @ Matrix.Translation(-p)
    M = F @ about(heel_p, state["heel"]) @ about(tip_p, state["tip"]) @ about(ball_p, state["ball"])
    return M @ ankle


def leg_len():
    b = C.SKELETON
    th = (Vector(b["thigh_l"][1]) - Vector(b["thigh_l"][2])).length
    ca = (Vector(b["calf_l"][1]) - Vector(b["calf_l"][2])).length
    return th + ca


def solve_hip_height(gait, rig, nominal):
    """Highest base pelvis height (<= nominal) keeping both stance legs within hip_margin of full length."""
    L = leg_len() * gait.g["hip_margin"]
    Lsw = leg_len() * .995
    lo, hi = .25, nominal
    for _ in range(40):
        mid = (lo + hi) / 2
        ok = True
        for f in range(gait.N + 1):
            phi = f / gait.N
            for side in "lr":
                st = gait.foot(side, phi)
                lim = L if st["stance"] else Lsw
                if (gait.hip_joint(side, phi, mid) - ankle_from_foot(rig, st, side)).length > lim:
                    ok = False
                    break
            if not ok:
                break
        if ok:
            lo = mid
        else:
            hi = mid
    return lo


def max_extension(gait, rig, hip_z):
    L = leg_len()
    worst = 0.0
    for f in range(gait.N + 1):
        phi = f / gait.N
        for side in "lr":
            st = gait.foot(side, phi)
            worst = max(worst, (gait.hip_joint(side, phi, hip_z) - ankle_from_foot(rig, st, side)).length / L)
    return worst


# ------------------------------------------------------------------ upper body


def hand_relaxed(p, side, amount=1.0, extra=0.0):
    f1, f2, t1 = 48 * amount + extra, 62 * amount + extra, 18 * amount
    c = (lambda d: ("Y", d)) if side == "l" else (lambda d: ("Y", -d))
    if "c_fingers_01_" + side not in p.rig.pose.bones:     # box hands: nothing to curl
        return
    p.r("c_fingers_01_" + side, c(f1)).r("c_fingers_02_" + side, c(f2))
    if "c_thumb_01_" + side in p.rig.pose.bones:
        p.r("c_thumb_01_" + side, c(t1)).r("c_thumb_02_" + side, c(12 * amount))


def upper_body(p, gait, phi):
    g = gait.g
    cr = g["crouch"]
    _, pyaw, proll, ppitch = gait.pelvis(phi, 0.0)
    # chest counter-rotation and lean distribution
    cy = g["cyaw"] * math.cos(TAU * (phi + .02))
    twist_back = -(gait.yaw) * .92                 # chest faces the view while the hips turn toward travel
    s1_yaw = (cy * .45) + twist_back * .45 + g["pyaw"] * math.cos(TAU * (phi - .03)) * .5
    s2_yaw = (cy * .55) + twist_back * .47
    lean = g["lean"]
    if gait.backward:
        lean *= .25 if not cr else .8
    s1_pitch = (lean * .2 if not cr else lean * .25) - ppitch * .15 + .8 * math.sin(2 * TAU * (phi - .05))
    s2_pitch = (lean * .15 if not cr else lean * .2) + .6 * math.sin(2 * TAU * (phi - .12))
    s_roll = -proll * .6
    p.r("c_spine_01", ("X", s1_pitch), ("Y", s_roll * .5), ("Z", s1_yaw))
    p.r("c_spine_02", ("X", s2_pitch), ("Y", s_roll * .5), ("Z", s2_yaw))
    # head stabilisation: undo most of the accumulated pitch/yaw/roll so the head keeps facing the view
    total_pitch = ppitch + s1_pitch + s2_pitch
    total_yaw = pyaw + s1_yaw + s2_yaw
    total_roll = proll + s_roll
    k = g["head_k"]
    nod = 1.2 * math.sin(2 * TAU * (phi - .18))
    p.r("c_neck_01", ("X", -total_pitch * k * .45 + nod * .4), ("Y", -total_roll * .4), ("Z", -total_yaw * k * .45))
    p.r("c_head", ("X", -total_pitch * k * .55 + nod * .6), ("Y", -total_roll * .45), ("Z", -total_yaw * k * .55))
    # arms swing opposite the legs (right arm forward at left contact), slight lag; strafes reduce the swing
    amp = g["arm"] * (.75 + .25 * abs(gait.s_dir))
    for side in "lr":
        sgn = 1 if side == "l" else -1
        ph = phi + (.5 if side == "r" else 0.0) - .04
        swing = amp * math.cos(TAU * ph) * (1.06 if side == "r" else 1.0)   # + = arm back on this side
        if gait.backward:
            swing *= .8
        forward_bias = -8.0 - (14.0 if cr else 0.0)
        elbow = g["elbow"] - g["elbow_osc"] * (.5 - .5 * math.cos(TAU * ph))  # more bent when the arm is forward
        abd = sgn * -(6.0 + 2.0 * math.sin(TAU * ph))
        p.r("c_clavicle_" + side, ("Z", sgn * 4.0 * math.cos(TAU * ph)), ("Y", sgn * 1.0))
        p.r("c_upperarm_" + side, ("X", swing + forward_bias), ("Y", abd))
        p.r("c_lowerarm_" + side, ("X", elbow))
        p.r("c_hand_" + side, ("Y", sgn * 6.0), ("X", 6.0 * math.sin(TAU * ph)))
        hand_relaxed(p, side, 1.0 if gait.family != "Sneak" else .7)


def author_gait(rig, gait, action_name):
    """Dense keys (every frame 0..N) on the control rig; returns the action and diagnostic data."""
    rig.animation_data_create()
    act = A.new_action(rig, action_name)
    nominal = C.SPINE["pelvis"] - (SNEAK_BASE["pelvis_drop"] if gait.g["crouch"] else 0.0)
    hip = solve_hip_height(gait, rig, nominal - .015)
    gait.hip_z = hip
    A.set_switches(rig, frame=0)
    for f in range(gait.N + 1):
        phi = f / gait.N
        p = A.Pose(rig)
        loc, q = gait.pelvis_matrix(phi, hip)
        p.t("c_pelvis", loc - Vector((0, 0, C.SPINE["pelvis"])))
        p.rq("c_pelvis", q)
        for side in "lr":
            st = gait.foot(side, phi)
            p.m("c_foot_ik_" + side, A.foot_ik_matrix(rig, side, st["xy"], st["yaw"], st["z"]))
            p.r("c_heel_roll_" + side, ("X", st["heel"]))
            p.r("c_ball_roll_" + side, ("X", st["ball"]))
            p.r("c_tip_roll_" + side, ("X", st["tip"]))
            # knee pole in front of the knee along the foot yaw (slightly outward)
            hipj = gait.hip_joint(side, phi, hip)
            fwd = Vector((0, -1, 0))
            fwd.rotate(Euler((0, 0, math.radians(st["yaw"] + (4 if side == "l" else -4)))))
            pole = Vector((hipj.x, hipj.y, .3)) + fwd * .7
            rest_pole = rig.data.bones["c_knee_pole_" + side].head_local
            p.t("c_knee_pole_" + side, pole - rest_pole)
        upper_body(p, gait, phi)
        A.reset_pose(rig)
        p.apply(reset=True)
        for pb in rig.pose.bones:
            if pb.name.startswith(("c_", "m_")) and not pb.name.startswith(("c_pickaxe", "c_support", "c_grip",
                                                                            "c_hand_ik", "c_elbow_pole")):
                pb.keyframe_insert("rotation_quaternion", frame=f)
                pb.keyframe_insert("location", frame=f)
    A.set_interpolation(act, "LINEAR")
    return act, {"hip_z": hip, "max_extension": max_extension(gait, rig, hip), "D": gait.D,
                 "stance_len": gait.stance_len, "a_on": gait.a_on, "yaw": gait.yaw, "backward": gait.backward}


# ------------------------------------------------------------------ idle / sneak idle / transitions

STANCE_FEET = {"l": (Vector((.105, -.012)), 7.0), "r": (Vector((-.105, .022)), -5.0)}   # xy, yaw (toe-out)


def base_idle(p, rig, phi, crouch=0.0, breathe=1.0):
    """Static-feet idle body at phase phi; crouch 0..1 blends standing -> sneak base pose."""
    br = math.sin(TAU * phi)                          # one breath per loop
    sway = math.sin(TAU * (phi + .15))
    drop = SNEAK_BASE["pelvis_drop"] * crouch
    back = SNEAK_BASE["pelvis_back"] * crouch
    lean = SNEAK_BASE["lean"] * crouch
    pel = Vector((-.014 + .004 * sway, back + .002 * br, -.012 - drop + .002 * br))
    p.t("c_pelvis", pel)
    p.r("c_pelvis", ("X", lean * .55 + .4 * br), ("Y", 1.2 * (1 - crouch) + .4 * sway), ("Z", -2.0 + .8 * sway))
    p.r("c_spine_01", ("X", lean * .25 + .5 * br * breathe), ("Z", 1.0 - .6 * sway), ("Y", -.8))
    p.r("c_spine_02", ("X", lean * .2 - 1.1 * br * breathe), ("Z", 1.2 - .4 * sway), ("Y", -.6))
    look = 1.6 * math.sin(TAU * (phi - .1)) + .8 * math.sin(2 * TAU * (phi + .3))
    tot_pitch = lean - .2 * br
    p.r("c_neck_01", ("X", -tot_pitch * .45 + .6 * br), ("Z", look * .4 - .8))
    p.r("c_head", ("X", -tot_pitch * .55 - .5 * br + .7 * math.sin(2 * TAU * phi)), ("Z", look * .6 - 1.2),
        ("Y", .8))
    for side in "lr":
        sgn = 1 if side == "l" else -1
        fwd = -3.0 - 20.0 * crouch
        p.r("c_clavicle_" + side, ("X", .8 * br), ("Z", sgn * (-.6 * br)))
        p.r("c_upperarm_" + side, ("X", fwd + 1.2 * br * sgn), ("Y", sgn * -(5.0 + 1.0 * br)))
        p.r("c_lowerarm_" + side, ("X", -12.0 - 30.0 * crouch - 1.5 * br))
        p.r("c_hand_" + side, ("Y", sgn * 8.0), ("X", 3.0))
        hand_relaxed(p, side, .62 + .04 * br * sgn)
    for side in "lr":
        xy, yaw = STANCE_FEET[side]
        wide = Vector((0.0, 0.0))                        # feet stay planted through stand <-> sneak
        p.m("c_foot_ik_" + side, A.foot_ik_matrix(rig, side, xy + wide, yaw, 0.0))
        rest_pole = rig.data.bones["c_knee_pole_" + side].head_local
        fwd = Vector((0, -1, 0))
        fwd.rotate(Euler((0, 0, math.radians(yaw))))
        pole = Vector((xy.x + wide.x, xy.y, .3)) + fwd * .7
        p.t("c_knee_pole_" + side, pole - rest_pole)
    return p


KEY_SKIP = ("c_pickaxe", "c_support", "c_grip", "c_hand_ik", "c_elbow_pole")


def key_all(rig, f):
    for pb in rig.pose.bones:
        if pb.name.startswith(("c_", "m_")) and not pb.name.startswith(KEY_SKIP):
            pb.keyframe_insert("rotation_quaternion", frame=f)
            pb.keyframe_insert("location", frame=f)


def author_idle(rig, name, frames=120, crouch=0.0):
    act = A.new_action(rig, name)
    A.set_switches(rig, frame=0)
    for f in range(frames + 1):
        p = base_idle(A.Pose(rig), rig, f / frames, crouch)
        A.reset_pose(rig)
        p.apply(reset=True)
        key_all(rig, f)
    A.set_interpolation(act, "LINEAR")
    return act


def author_crouch_transition(rig, name, to_sneak=True, frames=16):
    """Feet planted; crouch weight eases with a small settle overshoot; ends on the target idle's frame 0."""
    act = A.new_action(rig, name)
    A.set_switches(rig, frame=0)
    for f in range(frames + 1):
        t = f / frames
        e = smoother(t)
        settle = .06 * math.sin(math.pi * min(1.0, t * 1.25)) * (1 - t) if to_sneak else -.04 * math.sin(math.pi * t)
        c = e + settle if to_sneak else 1.0 - e - settle
        c = min(max(c, -.05), 1.05)
        p = base_idle(A.Pose(rig), rig, 0.0, c)
        A.reset_pose(rig)
        p.apply(reset=True)
        key_all(rig, f)
    A.set_interpolation(act, "LINEAR")
    return act
