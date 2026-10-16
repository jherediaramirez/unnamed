"""Pickaxe hold + strikes A/B/C, hand-led: the right arm is FK (natural arcs, wrist kept in human range) and the
pickaxe rides the right grip (pickaxe_in_hand = 1); B adds the left-hand support grip by IK. At the contact frame a
small numeric solve adjusts shoulder/elbow/wrist so the tip lands exactly on the target. Link pose shared by frames 0
and 18 of every strike, so any order chains (A->B->A->C).

Tool frame (m_pickaxe): origin = right grip centre, +Y along the handle toward the head, +Z toward the long point.
Character space: forward -Y, right -X, up +Z. Contact frame 9 (0.15 s) of 18 (0.30 s) = gameplay step 3 of 6.
Right-arm FK angles are rest-frame armature rotations: flex X (- = forward/up), abduct Y (+ = outward for the right
arm), twist Z (humeral rotation), elbow X (- = bend), wrist X (- = cock/handle back, + = snap) and Y (flex).
"""

import math

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

import up_anim as A
import up_config as C
import up_loco as L

TIP_LOCAL = Vector((0.0, .4275, .265))
TARGET = Vector((-.05, -.80, 1.25))          # contact on the near face of an eye-level stone block (z 1..2)
STONE = {"center": (-.05, -1.30, 1.5), "size": 1.0}
CONTACT_FRAME = 9
FRAMES = 18


def hand_grip(p, side, kind="grip"):
    c = (lambda d: ("Y", d)) if side == "l" else (lambda d: ("Y", -d))
    f1, f2, t1, t2 = {"grip": (90, 92, 62, 35), "relaxed": (48, 62, 18, 12), "loose": (70, 80, 30, 20)}[kind]
    S = "_" + side
    if "c_fingers_01" + S not in p.rig.pose.bones:          # box hands: the handle passes through the arm's end
        return
    p.r("c_fingers_01" + S, c(f1)).r("c_fingers_02" + S, c(f2))
    if "c_thumb_01" + S in p.rig.pose.bones:
        p.r("c_thumb_01" + S, c(t1), ("X", 8 if kind == "grip" else 0)).r("c_thumb_02" + S, c(t2))


def body(p, rig, twist=0.0, lean=0.0, side_bend=0.0, look_down=4.0, clav_r=0.0, clav_fwd=0.0,
         larm=(-12.0, 8.0, -35.0), elbow_pole_l=(.6, .3, .75), head_tilt=0.0):
    p.add("c_spine_01", ("X", lean * .45), ("Y", side_bend * .5), ("Z", twist * .45))
    p.add("c_spine_02", ("X", lean * .55), ("Y", side_bend * .5), ("Z", twist * .55))
    p.add("c_neck_01", ("X", look_down * .4 - lean * .35), ("Y", head_tilt * .5), ("Z", -twist * .4))
    p.add("c_head", ("X", look_down * .6 - lean * .45), ("Y", head_tilt * .5), ("Z", -twist * .5))
    p.add("c_clavicle_r", ("Y", clav_r), ("Z", -clav_fwd))
    ax, ab, el = larm
    p.r("c_upperarm_l", ("X", ax), ("Y", -ab)).r("c_lowerarm_l", ("X", el))
    p.t("c_elbow_pole_l", Vector(elbow_pole_l) - rig.data.bones["c_elbow_pole_l"].head_local)


def right_arm(p, flex, abd, twist, elbow, cock, wflex=0.0):
    p.r("c_upperarm_r", ("Z", twist), ("Y", abd), ("X", flex))
    p.r("c_lowerarm_r", ("X", elbow))
    p.r("c_hand_r", ("X", cock), ("Y", -wflex))


LINK = dict(arm=(-40, 18, -12, -66, 0), body=dict(twist=-6, lean=2, larm=(-12, 8, -35)))
STRIKES = {
    "A": [  # diagonal shoulder-led chop: up over the right shoulder, cut down across to the stone
        (3, dict(tool=dict(grip=(-.40, .05, 1.52), handle=(-.3, .9, .1), tip=(0, 0, 1)), arm=(-142, 60, -25, -76, -20), body=dict(twist=-22, lean=-5, side_bend=-5, clav_r=12, head_tilt=14,
                                                          larm=(-35, 18, -45)))),
        (6, dict(tool=dict(grip=(-.30, -.16, 1.72), handle=(-.15, .35, .92), tip=(0, -1, 0)), arm=(-122, 54, -18, -50, -8), body=dict(twist=-4, lean=5, clav_r=8, clav_fwd=4, head_tilt=16,
                                                         larm=(-22, 14, -40)))),
        (9, dict(contact=True, arm=(-92, 4, 0, -22, 14), body=dict(twist=22, lean=15, side_bend=5, clav_r=0,
                                                                   clav_fwd=8, larm=(14, 18, -30)))),
        (10, dict(arm=(-95, 5, 0, -30, 8), body=dict(twist=21, lean=14, side_bend=5, clav_fwd=7, larm=(12, 17, -31)))),
        (12, dict(arm=(-102, 8, 0, -48, -4), body=dict(twist=14, lean=11, side_bend=3, clav_fwd=4, larm=(4, 14, -33)))),
        (15, dict(arm=(-62, 20, -12, -66, -4), body=dict(twist=2, lean=5, larm=(-6, 9, -34)))),
    ],
    "B": [  # opposite diagonal (one-handed backhand): wind up across to the upper left, cut down to the right,
            # the recovery unwinds through the right side into the next stroke; left arm counter-swings
        (3, dict(tool=dict(grip=(.0, -.52, 1.16), handle=(.55, .05, .83), tip=(-.3, -.6, .7)), arm=(-100, -8, -10, -100, -18), body=dict(twist=32, lean=-3, side_bend=4, clav_r=4, clav_fwd=12,
                                                             larm=(18, 30, -50)))),
        (6, dict(tool=dict(grip=(-.08, -.42, 1.58), handle=(.22, .04, .97), tip=(-.15, -1, .3)), arm=(-104, -2, -8, -60, -6), body=dict(twist=20, lean=6, clav_r=4, clav_fwd=10,
                                                          larm=(10, 24, -45)))),
        (9, dict(contact=True, arm=(-90, 2, -6, -26, 12), body=dict(twist=24, lean=15, side_bend=-4, clav_fwd=10,
                                                                     larm=(-20, 14, -40)))),
        (10, dict(arm=(-93, 3, -6, -33, 7), body=dict(twist=22, lean=14, side_bend=-4, clav_fwd=9,
                                                       larm=(-22, 13, -40)))),
        (12, dict(arm=(-100, 6, -3, -50, -6), body=dict(twist=10, lean=11, side_bend=-3, clav_fwd=6,
                                                        larm=(-24, 12, -40)))),
        (15, dict(arm=(-62, 20, -15, -68, -4), body=dict(twist=-12, lean=5, larm=(-10, 10, -36)))),
    ],
    "C": [  # compact chip: short cocked wind-up, flat fast arc, sharp rebound
        (3, dict(tool=dict(grip=(-.34, -.16, 1.28), handle=(-.2, .5, .85), tip=(0, -1, .4)), arm=(-52, 26, -25, -92, -16), body=dict(twist=-10, lean=0, clav_r=4, head_tilt=10, larm=(-16, 10, -36)))),
        (6, dict(tool=dict(grip=(-.30, -.22, 1.30), handle=(-.12, .25, .96), tip=(0, -1, .1)), arm=(-66, 24, -20, -88, -14), body=dict(twist=-7, lean=2, clav_r=3, head_tilt=6, larm=(-14, 10, -36)))),
        (9, dict(contact=True, arm=(-84, 6, 0, -40, 16), body=dict(twist=12, lean=10, clav_fwd=6, larm=(4, 12, -34)))),
        (10, dict(arm=(-84, 7, 0, -48, 8), body=dict(twist=11, lean=9, clav_fwd=5, larm=(3, 12, -34)))),
        (12, dict(arm=(-78, 10, 0, -72, -16), body=dict(twist=4, lean=6, clav_fwd=2, larm=(-4, 10, -35)))),
        (15, dict(arm=(-56, 20, -12, -70, -4), body=dict(twist=-4, lean=3, larm=(-10, 8, -35)))),
    ],
}


# Final FK poses: these are the posed angles, not seeds for the wind-up target solve.
POLISH_KEYS = {
    "A": {
        3: ((-125, 25, -15, -55, -8), dict(twist=-18, lean=-3, side_bend=-3, clav_r=24, clav_fwd=-6, head_tilt=0, larm=(-15, 12, -35))),
        6: ((-100, 20, -10, -38, -5), dict(twist=0, lean=5, side_bend=0, clav_r=18, clav_fwd=-4, head_tilt=0, larm=(-20, 12, -35))),
        8: ((-63.55, -6.23, 0, -37.31, -14.99), dict(twist=16, lean=12, side_bend=4, clav_r=0, clav_fwd=6, head_tilt=5, larm=(4, 17, -33))),
        9: ((-34.6, -6.92, 0, -26.23, -29.38), {}),
        10: ((-36.53, -5.96, 0, -28.59, -31.43), dict(lean=15)),
        12: ((-42, 2, 0, -52, -25), {}),
    },
    "B": {
        1: ((-48, 18, -12, -45, 5), dict(twist=-3, lean=1, clav_r=8, clav_fwd=-4, larm=(-8, 12, -36))),
        2: ((-75, 8, 0, -38, 20), dict(twist=10, lean=0, clav_r=12, clav_fwd=-6, larm=(-4, 14, -36))),
        3: ((-95, -5, 15, -35, 35), dict(twist=25, lean=-2, side_bend=2, clav_r=12, clav_fwd=-6, larm=(6, 20, -38))),
        6: ((-90, 0, 10, -40, 35), dict(twist=20, lean=6, side_bend=0, clav_r=14, clav_fwd=-8, larm=(2, 18, -38))),
        7: ((-64.5323, -8.9871, -6, -50.8900, 1.4856), dict(twist=22, lean=9, side_bend=-1, clav_r=14, clav_fwd=-8, larm=(-6, 17, -40))),
        8: ((-46.24, -3.96, -9, -51.98, -18.02), dict(twist=23, lean=12, side_bend=-3, clav_r=12, clav_fwd=-4, larm=(-10, 17, -42))),
        9: ((-19.54, -6.5, -6, -40.79, -28.45), dict(clav_r=16, clav_fwd=0)),
        10: ((-20.72, .4, -9, -42.9, -29.44), dict(clav_r=15, clav_fwd=0)),
        12: ((-42, -8, 0, -52, -25), dict(clav_r=10, clav_fwd=0)),
    },
    "C": {
        3: ((-48, 14, -8, -62, -10), dict(twist=-8, lean=0, clav_r=12, clav_fwd=-4, head_tilt=0, larm=(-12, 10, -35))),
        6: ((-48, 12, -5, -45, -12), dict(twist=-3, lean=3, clav_r=12, clav_fwd=-3, head_tilt=0, larm=(-8, 10, -35))),
        8: ((-43.08, -6.06, 0, -44.74, -9.79), dict(twist=8, lean=8, clav_r=6, clav_fwd=-3, larm=(0, 11, -35))),
        9: ((-26.8, -3.69, 0, -35.6, -19.46), dict(clav_r=8, clav_fwd=-6)),
        10: ((-22.22, 10.78, 0, -42.95, -24.88), dict(clav_r=8, clav_fwd=-5)),
        11: ((-27.1, 12, -2, -47.5, -24.9), dict(twist=7, lean=7.5, clav_r=6, clav_fwd=-3, larm=(-1, 11, -35))),
        12: ((-32, 2, 0, -52, -25), dict(clav_r=4, clav_fwd=-2)),
    },
}
for _key, _changes in POLISH_KEYS.items():
    _specs = dict(STRIKES[_key])
    for _f, (_arm, _body) in _changes.items():
        _old = _specs.get(_f, {})
        _sp = dict(_old)
        _sp.pop("tool", None)
        _sp.update(arm=_arm, body={**_old.get("body", {}), **_body}, final_arm=True)
        _specs[_f] = _sp
    _specs[15]["arm"] = (-48, 18, -12, -64, 0)
    _specs[15]["final_arm"] = True
    if _key in "AB":
        _specs[8]["tip_target"] = (-.10, -.68, 1.63) if _key == "A" else (-.03, -.715, 1.59)
    if _key == "C":
        _specs[10]["tip_target"] = (-.14, -.78, 1.34)
        _specs[11]["tip_target"] = (-.20, -.72, 1.44)
    STRIKES[_key] = sorted(_specs.items())


def build_pose(rig, spec, crouch=0.0, arm=None):
    p = L.base_idle(A.Pose(rig), rig, 0.0, crouch, breathe=0.0)
    body(p, rig, **spec.get("body", {}))
    right_arm(p, *(arm or spec["arm"]))
    hand_grip(p, "r", "grip")
    hand_grip(p, "l", "grip" if spec.get("support", 0.0) > .5 else "loose")
    return p


def tool_tip(rig):
    m = rig.pose.bones["m_pickaxe"].matrix
    return rig.matrix_world @ m @ TIP_LOCAL


def solve_contact(rig, spec, crouch=0.0, iters=60, target=None):
    """Damped Gauss-Newton on (flex, abduct, elbow, cock) so the tip lands on TARGET, starting at the design."""
    x = list(spec["arm"])
    target = Vector(target) if target is not None else TARGET
    idx = [0, 1, 3, 4]
    root = rig.pose.bones["c_root"]
    root["pickaxe_in_hand"] = 1.0

    def tip_for(vals):
        arm = list(x)
        for k, i in enumerate(idx):
            arm[i] = vals[k]
        p = build_pose(rig, spec, crouch, arm)
        A.reset_pose(rig)
        p.apply(reset=True)
        bpy.context.view_layer.update()
        return tool_tip(rig)
    v = [x[i] for i in idx]
    for _ in range(iters):
        t = tip_for(v)
        err = target - t
        if err.length < 2e-5:
            break
        J = []
        for k in range(4):
            dv = list(v)
            dv[k] += .5
            J.append((tip_for(dv) - t) / .5)
        lam = 1e-6
        JT_J = Matrix([[sum(J[a][r] * J[b][r] for r in range(3)) + (lam if a == b else 0.0) for b in range(4)]
                       for a in range(4)])
        JT_e = Vector([sum(J[a][r] * err[r] for r in range(3)) for a in range(4)])
        try:
            step = JT_J.inverted() @ JT_e
        except ValueError:
            break
        v = [v[k] + max(-8.0, min(8.0, step[k])) for k in range(4)]
    arm = list(x)
    for k, i in enumerate(idx):
        arm[i] = v[k]
    return arm, (target - tip_for(v)).length


def solve_tool_pose(rig, spec, crouch=0.0, iters=40):
    """Solve (flex, abduct, twist, elbow, cock) so the grip sits at spec['tool']['grip'], the handle points along
    spec['tool']['handle'] and the tip leans toward spec['tool']['tip'] (weights), staying near the designed angles."""
    tgt = spec["tool"]
    g_t = Vector(tgt["grip"])
    y_t = Vector(tgt["handle"]).normalized()
    z_t = Vector(tgt.get("tip", (0, -1, 0))).normalized()
    x0 = list(spec["arm"])
    rig.pose.bones["c_root"]["pickaxe_in_hand"] = 1.0

    def resid(v):
        p = build_pose(rig, spec, crouch, v)
        A.reset_pose(rig)
        p.apply(reset=True)
        bpy.context.view_layer.update()
        m = rig.matrix_world @ rig.pose.bones["m_pickaxe"].matrix
        g = m.translation
        y = m.to_3x3().col[1].normalized()
        z = m.to_3x3().col[2].normalized()
        r = list(g_t - g) + [.35 * c for c in (y_t - y)] + [.12 * c for c in (z_t - z)]
        r += [.0012 * (a - b) for a, b in zip(x0, v)]          # stay near the designed angles
        return r
    v = list(x0)
    for _ in range(iters):
        r0 = resid(v)
        J = []
        for k in range(5):
            dv = list(v)
            dv[k] += .5
            J.append([(a - b) / .5 for a, b in zip(resid(dv), r0)])
        Jm = np.array(J)                       # 5 x n
        step = np.linalg.solve(Jm @ Jm.T + 1e-7 * np.eye(5), Jm @ np.array(r0))
        v = [v[k] - max(-6.0, min(6.0, float(step[k]))) for k in range(5)]
        v[4] = max(-35.0, min(35.0, v[4]))       # wrist cock limit
        if float(np.linalg.norm(step)) < .02:
            break
    return v


def author_strike(rig, key, name, crouch=0.0):
    """Sparse FK keys (Bezier, auto-clamped); the contact key is solved for exact tip placement."""
    rig.animation_data_create()
    rig.animation_data.action = None
    A.set_switches(rig, pickaxe_in_hand=1)
    keys = [(0, LINK)] + STRIKES[key] + [(FRAMES, LINK)]
    solved = {}
    errs = {}
    for f, spec in keys:
        if spec.get("tool"):
            solved[f] = solve_tool_pose(rig, spec, crouch)
        if spec.get("tip_target"):
            solved[f], _ = solve_contact(rig, spec, crouch, target=spec["tip_target"])
    for f, spec in keys:
        if spec.get("contact"):
            solved[f], errs[f] = solve_contact(rig, spec, crouch)
            delta = [a - b for a, b in zip(solved[f], spec["arm"])]
            # carry the correction into the neighbouring keys so the swing stays continuous through contact
            for f2, spec2 in keys:
                w = {f - 3: .5, f + 1: .9, f + 2: .75, f + 3: .55}.get(f2, 0.0)
                if w and f2 not in solved and not spec2.get("tool") and not spec2.get("final_arm"):
                    solved[f2] = [a + w * d for a, d in zip(spec2["arm"], delta)]
    act = A.new_action(rig, name)
    A.set_switches(rig, frame=0, pickaxe_in_hand=1)
    root = rig.pose.bones["c_root"]
    for f, spec in keys:
        p = build_pose(rig, spec, crouch, solved.get(f))
        A.reset_pose(rig)
        p.apply(reset=True)
        L.key_all(rig, f)
        rig.pose.bones["c_elbow_pole_l"].keyframe_insert("location", frame=f)
        sup = spec.get("support", 0.0)
        root["support_grip_l"] = sup
        root["ik_arm_l"] = 1.0 if sup > 0 else 0.0
        root.keyframe_insert('["support_grip_l"]', frame=f)
        root.keyframe_insert('["ik_arm_l"]', frame=f)
    for fc in A.action_fcurves(act):
        for kp in fc.keyframe_points:
            kp.interpolation = "BEZIER"
            kp.handle_left_type = kp.handle_right_type = "AUTO_CLAMPED"
            if abs(kp.co[0] - CONTACT_FRAME) < .5 and ("upperarm_r" in fc.data_path or "lowerarm_r" in fc.data_path
                                                       or "c_hand_r" in fc.data_path):
                kp.handle_left_type = kp.handle_right_type = "VECTOR"   # impact corner on the swing
        fc.update()
    act["contact_solve_error_m"] = max(errs.values()) if errs else 0.0
    act["contact_arm_solution"] = str({f: [round(a, 2) for a in arm] for f, arm in solved.items()})
    return act, solved


def author_hold(rig, name, crouch=0.0):
    act = A.new_action(rig, name)
    A.set_switches(rig, frame=0, pickaxe_in_hand=1)
    spec = dict(arm=(-30, 10, 0, -62, -14), body=dict(twist=-4, lean=1, larm=(-8, 7, -24)))
    p = build_pose(rig, spec, crouch)
    hand_grip(p, "l", "relaxed")
    A.reset_pose(rig)
    p.apply(reset=True)
    L.key_all(rig, 0)
    return act, None
