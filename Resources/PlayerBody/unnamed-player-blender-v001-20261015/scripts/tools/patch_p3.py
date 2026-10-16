"""One-off patch: compose turn details onto the idle pose, non-overlapping step windows, planted landing frame 0,
placeholders (low pose, flight, death)."""
import os

S = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def patch(name, pairs):
    p = os.path.join(S, name)
    s = open(p, encoding="utf-8").read()
    for old, new in pairs:
        if old not in s:
            raise SystemExit(f"[patch] {name}: missing {old[:70]!r}")
        s = s.replace(old, new)
    open(p, "w", encoding="utf-8").write(s)
    print("[patch]", name, len(pairs))


patch("up_anim.py", [
    ('''    def rq(self, bone, q_arm):''', '''    def add(self, bone, *rotations):
        """Compose an extra rest-frame armature-axis rotation on top of what this pose already has."""
        extra = local_quat(self.rig, bone, arm_quat(rotations))
        self.rot[bone] = extra @ self.rot.get(bone, Quaternion())
        return self

    def rq(self, bone, q_arm):'''),
])
patch("up_actions.py", [
    ('''import math

from mathutils import Euler, Matrix, Vector''', '''import math

from mathutils import Euler, Matrix, Quaternion, Vector'''),
    ('''        qa = pa.rot.get(b)
        qb = pb.rot.get(b)
        from mathutils import Quaternion
        qa = qa if qa is not None else Quaternion()''', '''        qa = pa.rot.get(b)
        qb = pb.rot.get(b)
        qa = qa if qa is not None else Quaternion()'''),
    ('''    if abs(deg) <= 50:
        return [("lead", .12, .62), ("trail", .5, .95)]
    return [("lead", .06, .5), ("trail", .44, .94)]''', '''    if abs(deg) <= 50:
        return [("lead", .06, .48), ("trail", .52, .92)]
    return [("lead", .04, .46), ("trail", .5, .93)]'''),
    ('''        p.r("c_head", ("Z", sgn * 9.0 * lead_amt))
        p.r("c_spine_02", ("Z", sgn * 4.0 * lead_amt))
        p.r("c_pelvis", ("Z", -sgn * 3.0 * math.sin(math.pi * t)), ("X", SNEAK_LEAN * crouch * .55))''',
     '''        p.add("c_head", ("Z", sgn * 9.0 * lead_amt))
        p.add("c_spine_02", ("Z", sgn * 4.0 * lead_amt))
        p.add("c_pelvis", ("Z", -sgn * 3.0 * math.sin(math.pi * t)))'''),
    ('''    fall0 = air_pose(rig, "fall", 0.0)
    acts["Land_Soft"] = author_sparse(rig, "RIG_UP_Land_Soft", [
        (0, blend_poses(rig, fall0, air_pose(rig, "impact_soft"), .55)), (3, air_pose(rig, "impact_soft")),
        (12, idle_pose(rig))])
    acts["Land_Firm"] = author_sparse(rig, "RIG_UP_Land_Firm", [
        (0, blend_poses(rig, fall0, air_pose(rig, "impact_firm"), .45)), (5, air_pose(rig, "impact_firm")),
        (11, blend_poses(rig, air_pose(rig, "impact_firm"), idle_pose(rig), .45)), (24, idle_pose(rig))])
    return acts''', '''    fall0 = air_pose(rig, "fall", 0.0)
    soft, firm = air_pose(rig, "impact_soft"), air_pose(rig, "impact_firm")
    acts["Land_Soft"] = author_sparse(rig, "RIG_UP_Land_Soft", [
        (0, with_feet(rig, blend_poses(rig, fall0, soft, .55), soft)), (3, soft), (12, idle_pose(rig))])
    acts["Land_Firm"] = author_sparse(rig, "RIG_UP_Land_Firm", [
        (0, with_feet(rig, blend_poses(rig, fall0, firm, .45), firm)), (5, firm),
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
    for k in ("ik_leg_l", "ik_leg_r"):
        root[k] = 1.0
        root.keyframe_insert(f'["{k}"]', frame=0)
        root[k] = 0.0
        root.keyframe_insert(f'["{k}"]', frame=1)
    acts["Death_Placeholder"] = act
    return acts'''),
])
