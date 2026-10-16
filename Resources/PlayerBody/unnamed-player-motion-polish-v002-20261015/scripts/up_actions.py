"""Air clips, stationary turns, placeholders: authored on the control rig (sparse or dense keys)."""

import math

from mathutils import Euler, Matrix, Quaternion, Vector

import up_anim as A
import up_config as C
import up_loco as L

TAU = 2 * math.pi


def jump_trajectory(ticks=24, start_v=None):
    """The simulation's vertical update (FoundationSimulation: position += v; v = (v - g) * drag), in cm.

    Returns a list of (tick, feet_height_cm, velocity_cm_per_tick) on flat ground from the jump tick."""
    g, drag = C.GAME["gravity_cm_per_tick"], C.GAME["vertical_drag"]
    v = C.GAME["jump_cm_per_tick"] if start_v is None else start_v
    z = 0.0
    out = [(0, 0.0, v)]
    for t in range(1, ticks + 1):
        z += v
        v = (v - g) * drag
        if z <= 0.0 and t > 1:
            out.append((t, 0.0, v))
            break
        out.append((t, z, v))
    return out


def fall_trajectory(drop_cm, ticks=40):
    """Edge fall from rest (no jump), landing at -drop_cm."""
    g, drag = C.GAME["gravity_cm_per_tick"], C.GAME["vertical_drag"]
    v, z = 0.0, 0.0
    out = [(0, 0.0, 0.0)]
    v = (v - g) * drag
    for t in range(1, ticks + 1):
        z += v
        if z <= -drop_cm:
            out.append((t, -drop_cm, v))
            break
        out.append((t, z, v))
        v = (v - g) * drag
    return out


def foot_m(rig, side, xy, yaw, z=0.0):
    return A.foot_ik_matrix(rig, side, Vector(xy), yaw, z)


def pole_for(rig, p, side, xy, yaw, out=4.0):
    fwd = Vector((0, -1, 0))
    fwd.rotate(Euler((0, 0, math.radians(yaw + (out if side == "l" else -out)))))
    pole = Vector((xy[0], xy[1], .3)) + fwd * .7
    p.t("c_knee_pole_" + side, pole - rig.data.bones["c_knee_pole_" + side].head_local)


def air_pose(rig, kind, cyc=0.0):
    """Static air/landing poses. kind: takeoff, tuck, rise, fall, impact_soft, impact_firm."""
    p = A.Pose(rig)
    feet = {s: (L.STANCE_FEET[s][0].copy(), L.STANCE_FEET[s][1]) for s in "lr"}   # landings end on the idle spots
    if kind == "takeoff":
        pel, lean = Vector((0, .01, -.008)), 9.0
        fz = {"l": .0, "r": .03}
        roll = {"l": 34.0, "r": 26.0}
        arm, abd, elb = -20.0, 10.0, -22.0
        fy = {"l": .06, "r": .1}
    elif kind == "tuck":
        pel, lean = Vector((0, 0, -.02)), 4.0
        fz = {"l": .2, "r": .14}
        roll = {"l": 16.0, "r": 12.0}
        arm, abd, elb = -48.0, 22.0, -48.0
        fy = {"l": -.06, "r": .02}
    elif kind == "rise":
        pel, lean = Vector((0, 0, -.02)), 3.0
        fz = {"l": .17, "r": .12}
        roll = {"l": 12.0, "r": 10.0}
        arm, abd, elb = -40.0, 26.0, -45.0
        fy = {"l": -.05, "r": .03}
    elif kind == "fall":
        s = math.sin(TAU * cyc)
        c = math.cos(TAU * cyc)
        pel, lean = Vector((0, 0, -.015 + .004 * s)), 2.0 + 1.0 * s
        fz = {"l": .05 + .01 * s, "r": .07 - .01 * s}
        roll = {"l": 8.0 + 3 * c, "r": 6.0 - 3 * c}
        arm, abd, elb = -24.0 + 6 * c, 30.0 + 3 * s, -30.0
        fy = {"l": -.03 + .015 * s, "r": .03 - .015 * s}
    elif kind == "impact_soft":
        pel, lean = Vector((0, .02, -.10)), 8.0
        fz = {"l": 0.0, "r": 0.0}
        roll = {"l": 0.0, "r": 0.0}
        arm, abd, elb = -18.0, 16.0, -36.0
        fy = {"l": 0.0, "r": 0.0}
    elif kind == "impact_firm":
        pel, lean = Vector((0, .05, -.19)), 22.0
        fz = {"l": 0.0, "r": 0.0}
        roll = {"l": 0.0, "r": 0.0}
        arm, abd, elb = -34.0, 24.0, -42.0
        fy = {"l": 0.0, "r": 0.0}
    p.t("c_pelvis", pel)
    p.r("c_pelvis", ("X", lean * .6))
    p.r("c_spine_01", ("X", lean * .25)).r("c_spine_02", ("X", lean * .15))
    p.r("c_neck_01", ("X", -lean * .45)).r("c_head", ("X", -lean * .5))
    for side in "lr":
        sgn = 1 if side == "l" else -1
        xy, yaw = feet[side]
        xy = Vector((xy.x, xy.y + fy[side]))
        p.m("c_foot_ik_" + side, foot_m(rig, side, xy, yaw, fz[side]))
        p.r("c_ball_roll_" + side, ("X", roll[side]))
        pole_for(rig, p, side, xy, yaw)
        a = arm * (1.0 if side == "l" else .92)
        p.r("c_upperarm_" + side, ("X", a), ("Y", -sgn * abd))
        p.r("c_lowerarm_" + side, ("X", elb))
        p.r("c_hand_" + side, ("Y", sgn * 8.0))
        L.hand_relaxed(p, side, .55)
    return p


def key_pose(rig, p, f):
    A.reset_pose(rig)
    p.apply(reset=True)
    L.key_all(rig, f)


def author_sparse(rig, name, keyed, interp="BEZIER"):
    """keyed: list of (frame, Pose). Bezier (auto-clamped) between sparse keys for editability."""
    act = A.new_action(rig, name)
    A.set_switches(rig, frame=0)
    for f, p in keyed:
        key_pose(rig, p, f)
    for fc in A.action_fcurves(act):
        for kp in fc.keyframe_points:
            kp.interpolation = interp
            kp.handle_left_type = kp.handle_right_type = "AUTO_CLAMPED"
        fc.update()
    return act


def blend_poses(rig, pa, pb, t):
    """Linear blend of two Poses (loc lerp, rot slerp) -> new Pose."""
    p = A.Pose(rig)
    for b in set(pa.rot) | set(pb.rot):
        qa = pa.rot.get(b)
        qb = pb.rot.get(b)
        qa = qa if qa is not None else Quaternion()
        qb = qb if qb is not None else Quaternion()
        p.rot[b] = qa.slerp(qb, t)
    for b in set(pa.loc) | set(pb.loc):
        la = pa.loc.get(b, Vector())
        lb = pb.loc.get(b, Vector())
        p.loc[b] = la.lerp(lb, t)
    return p


def idle_pose(rig, crouch=0.0):
    return L.base_idle(A.Pose(rig), rig, 0.0, crouch)


def author_air(rig):
    acts = {}
    extension = air_pose(rig, "takeoff")
    extension.t("c_pelvis", (0, .01, -.005))
    for side in "lr":
        extension.r("c_upperarm_" + side, ("X", -58 * (1 if side == "l" else .92)), ("Y", -10 if side == "l" else 10))
        extension.r("c_lowerarm_" + side, ("X", -30))
    acts["Jump_Start"] = author_sparse(rig, "RIG_UP_Jump_Start", [
        (0, air_pose(rig, "takeoff")), (2, extension),
        (5, blend_poses(rig, extension, air_pose(rig, "tuck"), .45)),
        (10, air_pose(rig, "tuck")), (18, air_pose(rig, "rise"))])
    acts["Jump_Rise"] = author_sparse(rig, "RIG_UP_Jump_Rise", [
        (0, air_pose(rig, "rise")), (12, blend_poses(rig, air_pose(rig, "rise"), air_pose(rig, "fall"), .25)),
        (18, blend_poses(rig, air_pose(rig, "rise"), air_pose(rig, "fall"), .25))])
    act = A.new_action(rig, "RIG_UP_Jump_Fall")
    A.set_switches(rig, frame=0)
    for f in range(49):
        key_pose(rig, air_pose(rig, "fall", f / 48), f)
    A.set_interpolation(act, "LINEAR")
    acts["Jump_Fall"] = act
    fall0 = air_pose(rig, "fall", 0.0)
    soft, firm = air_pose(rig, "impact_soft"), air_pose(rig, "impact_firm")
    soft0 = with_feet(rig, blend_poses(rig, fall0, soft, .25), soft)
    soft0.t("c_pelvis", (0, .008, -.025))
    soft8 = blend_poses(rig, soft, idle_pose(rig), .75)
    soft8.t("c_pelvis", (-.008, .005, -.035))
    acts["Land_Soft"] = author_sparse(rig, "RIG_UP_Land_Soft", [
        (0, soft0), (3, soft), (8, soft8), (12, idle_pose(rig))])
    firm0 = with_feet(rig, blend_poses(rig, fall0, firm, .2), firm)
    firm0.t("c_pelvis", (0, .015, -.025))
    firm4 = air_pose(rig, "impact_firm")
    firm4.r("c_spine_01", ("X", 18*.25)).r("c_spine_02", ("X", 18*.15))
    firm6 = air_pose(rig, "impact_firm")
    firm6.t("c_pelvis", (0, .05, -.18))
    acts["Land_Firm"] = author_sparse(rig, "RIG_UP_Land_Firm", [
        (0, firm0), (4, firm4), (6, firm6),
        (11, blend_poses(rig, firm, idle_pose(rig), .45)), (24, idle_pose(rig))])
    return acts


FOOT_BONES = ("c_foot_ik_", "c_ball_roll_", "c_heel_roll_", "c_tip_roll_", "c_toe_ik_", "c_knee_pole_")


def with_feet(rig, body, feet):
    """Body pose with the feet (IK controls, rolls, poles) of another pose: landings start planted at frame 0."""
    p = A.Pose(rig)
    p.rot, p.loc = dict(body.rot), dict(body.loc)
    for b in list(p.rot) + list(p.loc):
        if b.startswith(FOOT_BONES):
            p.rot.pop(b, None)
            p.loc.pop(b, None)
    for b, q in feet.rot.items():
        if b.startswith(FOOT_BONES):
            p.rot[b] = q
    for b, v in feet.loc.items():
        if b.startswith(FOOT_BONES):
            p.loc[b] = v
    return p


# ------------------------------------------------------------------ placeholders (clearly labelled)


def author_placeholders(rig):
    acts = {}
    # Low/swim-compatible pose: prone in the 60 cm box, head near the box centre (eyes guide 40 cm), FK legs
    p = A.Pose(rig)
    p.t("c_pelvis", (0, .86, -.5)).r("c_pelvis", ("X", 88))
    p.r("c_spine_01", ("X", -4)).r("c_spine_02", ("X", -6))
    p.r("c_neck_01", ("X", -32)).r("c_head", ("X", -40))
    for s in "lr":
        sgn = 1 if s == "l" else -1
        p.r("c_upperarm_" + s, ("X", -150), ("Y", -sgn * 12)).r("c_lowerarm_" + s, ("X", -12))
        p.r("c_thigh_" + s, ("X", 3 * sgn)).r("c_calf_" + s, ("X", 8)).r("c_foot_" + s, ("X", 30))
        L.hand_relaxed(p, s, .3)
    act = A.new_action(rig, "RIG_UP_Low_Pose")
    A.set_switches(rig, frame=0, ik_leg_l=0, ik_leg_r=0)
    key_pose(rig, p, 0)
    acts["Low_Pose"] = act
    # Flight hover loop (1 s): legs dangle, slow bob of the limbs (the flight bob itself belongs to the camera/game)
    act = A.new_action(rig, "RIG_UP_Flight_Placeholder")
    A.set_switches(rig, frame=0, ik_leg_l=0, ik_leg_r=0)
    for f in range(61):
        c = math.cos(TAU * f / 60)
        q = A.Pose(rig)
        q.t("c_pelvis", (0, 0, .03 + .01 * c)).r("c_pelvis", ("X", 6 + 1.5 * c))
        q.r("c_neck_01", ("X", -3)).r("c_head", ("X", -3))
        for s in "lr":
            sgn = 1 if s == "l" else -1
            ph = math.cos(TAU * f / 60 + (0 if s == "l" else 1.3))
            q.r("c_thigh_" + s, ("X", -10 - 6 * ph)).r("c_calf_" + s, ("X", 22 + 8 * ph)).r("c_foot_" + s, ("X", 20))
            q.r("c_upperarm_" + s, ("X", -12 + 4 * ph), ("Y", -sgn * (14 + 3 * c))).r("c_lowerarm_" + s, ("X", -22))
            L.hand_relaxed(q, s, .5)
        key_pose(rig, q, f)
    A.set_interpolation(act, "LINEAR")
    acts["Flight_Placeholder"] = act
    # Death: knees buckle and the body tips onto its side over 0.5 s, then holds
    keys = []
    for f, k in ((0, 0.0), (8, .25), (18, .75), (26, 1.0), (30, 1.0)):
        q = idle_pose(rig) if k == 0 else A.Pose(rig)
        if k:
            q.t("c_pelvis", (.12 * k, .02, -.52 * k)).r("c_pelvis", ("Y", 84 * k), ("X", 8 * k))
            q.r("c_neck_01", ("Y", -12 * k)).r("c_head", ("Y", -14 * k))
            for s in "lr":
                sgn = 1 if s == "l" else -1
                q.r("c_thigh_" + s, ("X", -18 * k)).r("c_calf_" + s, ("X", 28 * k))
                q.r("c_upperarm_" + s, ("X", -20 * k), ("Y", -sgn * 30 * k)).r("c_lowerarm_" + s, ("X", -25 * k))
                L.hand_relaxed(q, s, .4)
        keys.append((f, q))
    act = author_sparse(rig, "RIG_UP_Death_Placeholder", keys)
    root = rig.pose.bones["c_root"]
    for k in ("ik_leg_l", "ik_leg_r"):   # FK legs for the whole fall
        root[k] = 0.0
        root.keyframe_insert(f'["{k}"]', frame=0)
    acts["Death_Placeholder"] = act
    return acts


# ------------------------------------------------------------------ stationary turns (root-neutral)


def ease_progress(t):
    """Body-yaw progress for the presentation controller (0..1), slow-in / slow-out with a late settle."""
    return L.smoother(min(max(t, 0.0), 1.0))


def turn_schedule(deg, frames):
    """Which foot steps when (fractions of the clip). Lead foot = the side turned toward."""
    if abs(deg) <= 50:
        return [("lead", .12, .48), ("trail", .52, .92)]
    return [("lead", .08, .46), ("trail", .5, .93)]


def author_turn(rig, name, side, deg, frames, crouch=0.0):
    """Turn toward `side` ('L' = +yaw / left, 'R' = -yaw) by deg. Root never rotates: the body yaw is metadata
    (progress p(t)); planted feet counter-rotate in mesh space, stepping feet land pre-rotated by the remaining yaw."""
    sgn = 1.0 if side == "L" else -1.0
    total = sgn * deg
    act = A.new_action(rig, name)
    A.set_switches(rig, frame=0)
    lead = "l" if side == "L" else "r"
    trail = "r" if lead == "l" else "l"
    sched = {lead: None, trail: None}
    for who, t0, t1 in turn_schedule(deg, frames):
        sched[lead if who == "lead" else trail] = (t0, t1)
    curve = []
    for f in range(frames + 1):
        t = f / frames
        prog = ease_progress(t)
        yaw_now = total * prog
        curve.append((f, round(yaw_now, 4)))
        p = L.base_idle(A.Pose(rig), rig, 0.0, crouch)
        # feet: overwrite the idle feet with turn-step feet
        for s in "lr":
            home, hyaw = L.STANCE_FEET[s]
            t0, t1 = sched[s]
            if t < t0:          # planted at the start spot (world fixed) -> counter-rotate in mesh space
                rot = -yaw_now
                xy = rotate2(home, rot)
                fyaw = hyaw + rot
                z = 0.0
                lift = 0.0
            elif t > t1:        # planted at the landing spot, which was pre-rotated by the remaining yaw
                land_rem = total * (1 - ease_progress(t1))
                rot = -(yaw_now - total * ease_progress(t1))
                xy = rotate2(rotate2(home, land_rem), rot)
                fyaw = hyaw + land_rem + rot
                z = 0.0
                lift = 0.0
            else:               # swing: from the lift-off spot to the pre-rotated landing spot
                u = (t - t0) / (t1 - t0)
                e = L.smoother(u)
                a0 = -total * ease_progress(t0) + (-(yaw_now - total * ease_progress(t0)))
                start_xy = rotate2(home, -yaw_now)
                start_yaw = hyaw - yaw_now
                land_rem = total * (1 - ease_progress(t1))
                end_xy = rotate2(home, land_rem - (yaw_now - total * ease_progress(t1)))
                end_yaw = hyaw + land_rem - (yaw_now - total * ease_progress(t1))
                xy = start_xy.lerp(end_xy, e)
                fyaw = start_yaw + (end_yaw - start_yaw) * e
                z = (.07 if crouch < .5 else .05) * math.sin(math.pi * u) ** 1.3
                lift = math.sin(math.pi * u)
            p.m("c_foot_ik_" + s, A.foot_ik_matrix(rig, s, xy, fyaw, z))
            p.r("c_ball_roll_" + s, ("X", 10.0 * lift))
            pole_for(rig, p, s, xy, fyaw)
        # body detail: head leads into the turn, pelvis lags, weight shifts onto the planted foot
        head_peak = 2 if deg <= 50 else 4
        chest_peak = 4 if deg <= 50 else 7
        head_lead = L.monotone_keys(f, ((0, 0), (head_peak, 1), (frames*.8, 0), (frames, 0)))
        chest_lead = L.monotone_keys(f, ((0, 0), (chest_peak, 1), (frames*.9, 0), (frames, 0)))
        p.add("c_head", ("Z", sgn * 9.0 * head_lead))
        p.add("c_spine_02", ("Z", sgn * 4.0 * chest_lead))
        p.add("c_pelvis", ("Z", -sgn * 3.0 * math.sin(math.pi * t)))
        shift = 0.0
        for s in "lr":
            t0, t1 = sched[s]
            pre = max(0, t0 - 2 / frames)
            if pre <= t <= t1:
                weight = L.monotone_keys(t, ((pre, 0), (t0, 1), ((t0+t1)*.5, 1), (t1, 0)))
                shift += (-.025 if s == "l" else .025) * weight
        p.loc["c_pelvis"] = p.loc.get("c_pelvis", Vector()) + A.local_vec(rig, "c_pelvis", Vector((shift, 0, -.01 * abs(shift) / .025)))
        A.reset_pose(rig)
        p.apply(reset=True)
        L.key_all(rig, f)
    A.set_interpolation(act, "LINEAR")
    return act, curve, sched


SNEAK_LEAN = L.SNEAK_BASE["lean"]


def rotate2(v, deg):
    a = math.radians(deg)
    return Vector((v.x * math.cos(a) - v.y * math.sin(a), v.x * math.sin(a) + v.y * math.cos(a)))
