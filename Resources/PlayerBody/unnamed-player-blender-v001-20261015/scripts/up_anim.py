"""Posing, keying and baking helpers (Blender 5.2 layered actions).

Rotation convention for FK authoring: rotations are given about ARMATURE axes in the bone's REST frame (relative
to its parent), i.e. local = Mrest^-1 * R * Mrest. Armature axes: X = character's left, Y = back, Z = up.
So ("X", +a) pitches a vertical bone's top toward the front ... see AXES below for the sign of common motions.
"""

import math

import bpy
from mathutils import Matrix, Quaternion, Vector

import up_config as C

AX = {"X": Vector((1, 0, 0)), "Y": Vector((0, 1, 0)), "Z": Vector((0, 0, 1))}


def rest3(rig, bone):
    return rig.data.bones[bone].matrix_local.to_3x3()


def arm_quat(rotations):
    """[('X', deg), ('Z', deg), ...] applied in order (first item first) -> armature-space quaternion."""
    q = Quaternion()
    for axis, deg in rotations:
        v = AX[axis] if isinstance(axis, str) else Vector(axis).normalized()
        q = Quaternion(v, math.radians(deg)) @ q
    return q


def local_quat(rig, bone, q_arm):
    m = rest3(rig, bone)
    return (m.inverted() @ q_arm.to_matrix() @ m).to_quaternion()


def local_vec(rig, bone, v_arm):
    return rest3(rig, bone).inverted() @ Vector(v_arm)


class Pose:
    """Accumulates per-bone local transforms for one frame; .apply() writes them, .key() keys them."""

    def __init__(self, rig):
        self.rig = rig
        self.rot = {}
        self.loc = {}

    def r(self, bone, *rotations):
        self.rot[bone] = local_quat(self.rig, bone, arm_quat(rotations))
        return self

    def add(self, bone, *rotations):
        """Compose an extra rest-frame armature-axis rotation on top of what this pose already has."""
        extra = local_quat(self.rig, bone, arm_quat(rotations))
        self.rot[bone] = extra @ self.rot.get(bone, Quaternion())
        return self

    def rq(self, bone, q_arm):
        self.rot[bone] = local_quat(self.rig, bone, q_arm)
        return self

    def t(self, bone, v_arm):
        """Translation in armature axes (rest frame of the bone)."""
        self.loc[bone] = local_vec(self.rig, bone, v_arm)
        return self

    def m(self, bone, m_arm):
        """Full armature-space matrix for a bone whose parent chain is at rest (e.g. children of c_root)."""
        rest = self.rig.data.bones[bone].matrix_local
        basis = rest.inverted() @ m_arm
        loc, rot, _ = basis.decompose()
        self.loc[bone] = loc
        self.rot[bone] = rot
        return self

    def apply(self, reset=True):
        if reset:
            reset_pose(self.rig)
        P = self.rig.pose.bones
        for b, q in self.rot.items():
            P[b].rotation_quaternion = q
        for b, v in self.loc.items():
            P[b].location = v
        return self

    def key(self, frame, bones=None):
        P = self.rig.pose.bones
        for b in (bones or set(self.rot) | set(self.loc)):
            if b in self.rot:
                P[b].rotation_quaternion = self.rot[b]
                P[b].keyframe_insert("rotation_quaternion", frame=frame)
            if b in self.loc:
                P[b].location = self.loc[b]
                P[b].keyframe_insert("location", frame=frame)


def reset_pose(rig):
    for pb in rig.pose.bones:
        pb.location = (0, 0, 0)
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.rotation_euler = (0, 0, 0)
        pb.scale = (1, 1, 1)


SWITCH_DEFAULTS = {"ik_leg_l": 1.0, "ik_leg_r": 1.0, "ik_arm_l": 0.0, "ik_arm_r": 0.0, "pickaxe_in_hand": 0.0,
                   "support_grip_l": 0.0, "hand_r_on_pickaxe": 0.0}


def frame_matrix(origin, y_axis, z_hint):
    """Orthonormal frame with exact Y, Z as close to z_hint as possible, X = Y x Z."""
    y = Vector(y_axis).normalized()
    z = Vector(z_hint)
    z = (z - y * z.dot(y)).normalized()
    x = y.cross(z)
    m = Matrix((x, y, z)).transposed().to_4x4()
    m.translation = Vector(origin)
    return m


def set_switches(rig, frame=None, **kw):
    root = rig.pose.bones["c_root"]
    vals = dict(SWITCH_DEFAULTS)
    vals.update(kw)
    for k, v in vals.items():
        root[k] = float(v)
        if frame is not None:
            root.keyframe_insert(f'["{k}"]', frame=frame)


def new_action(obj, name):
    act = bpy.data.actions.get(name)
    if act:
        bpy.data.actions.remove(act)
    act = bpy.data.actions.new(name)
    act.use_fake_user = True
    obj.animation_data_create()
    obj.animation_data.action = act
    return act


def action_fcurves(act):
    out = []
    for layer in act.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                out += list(bag.fcurves)
    return out


def set_interpolation(act, kind="LINEAR"):
    for fc in action_fcurves(act):
        for kp in fc.keyframe_points:
            kp.interpolation = kind


# ------------------------------------------------------------------ IK control placement (world/armature space)


def foot_ik_matrix(rig, side, contact_xy, yaw_deg=0.0, z=0.0):
    """c_foot_ik rest is at the ankle's floor projection, Y toward the toes. Return its desired armature matrix."""
    b = rig.data.bones["c_foot_ik_" + side]
    m = b.matrix_local.copy()
    rot = Matrix.Rotation(math.radians(yaw_deg), 4, "Z")
    out = rot @ Matrix.Translation(-m.to_translation()) @ m
    out.translation = Vector((contact_xy[0], contact_xy[1], z))
    return out


def set_bone_matrix(rig, bone, m_arm):
    """Set a pose bone so its (unconstrained) armature-space matrix equals m_arm (parent pose must be current)."""
    pb = rig.pose.bones[bone]
    pb.matrix = m_arm
    bpy.context.view_layer.update()


# ------------------------------------------------------------------ baking to the deform skeleton


def _aim(m, new_dir, head):
    r = m.to_3x3().normalized()
    q = r.col[1].normalized().rotation_difference(new_dir.normalized())
    out = (q.to_matrix() @ r).to_4x4()
    out.translation = head
    return out


def exact_leg(skel, rig, side, M):
    """Analytic two-bone correction so the baked ankle lands EXACTLY on the rig's ankle target (legacy IK stops
    short by up to ~1 cm). Keeps the solver's bend plane; foot = target frame, ball rotation = c_toe_ik."""
    S = "_" + side
    bones = skel.data.bones
    a, b = bones["thigh" + S].length, bones["calf" + S].length
    H = M["thigh" + S].translation.copy()
    T = rig.pose.bones["m_ankle_target" + S].matrix.copy()
    Tp = T.translation.copy()
    K0 = M["calf" + S].translation.copy()
    dv = Tp - H
    d = min(dv.length, (a + b) * .99999)
    u = dv.normalized()
    ca = (a * a + d * d - b * b) / (2 * a * d)
    sa = math.sqrt(max(0.0, 1 - ca * ca))
    w = (K0 - H) - u * (K0 - H).dot(u)
    if w.length < 1e-7:
        w = rig.pose.bones["c_knee_pole" + S].head - H
        w = w - u * w.dot(u)
    w.normalize()
    K = H + u * (a * ca) + w * (a * sa)
    M["thigh" + S] = _aim(M["thigh" + S], K - H, H)
    M["calf" + S] = _aim(M["calf" + S], Tp - K, K)
    ft = T.to_3x3().normalized().to_4x4()
    ft.translation = Tp
    M["foot" + S] = ft
    off = bones["foot" + S].matrix_local.inverted() @ bones["ball" + S].matrix_local
    bt = rig.pose.bones["c_toe_ik" + S].matrix.to_3x3().normalized().to_4x4()
    bt.translation = (ft @ off).translation
    M["ball" + S] = bt
    return dv.length - d


def exact_arm(skel, rig, side, M):
    """Same analytic two-bone fix for arms in IK mode: hand = c_hand_ik frame exactly (tool grips stay rigid)."""
    S = "_" + side
    bones = skel.data.bones
    a, b = bones["upperarm" + S].length, bones["lowerarm" + S].length
    H = M["upperarm" + S].translation.copy()
    T = rig.pose.bones["c_hand_ik" + S].matrix.copy()
    Tp = T.translation.copy()
    E0 = M["lowerarm" + S].translation.copy()
    dv = Tp - H
    d = min(dv.length, (a + b) * .99999)
    u = dv.normalized()
    ca = (a * a + d * d - b * b) / (2 * a * d)
    sa = math.sqrt(max(0.0, 1 - ca * ca))
    w = (E0 - H) - u * (E0 - H).dot(u)
    if w.length < 1e-7:
        w = rig.pose.bones["c_elbow_pole" + S].head - H
        w = w - u * w.dot(u)
    w.normalize()
    E = H + u * (a * ca) + w * (a * sa)
    M["upperarm" + S] = _aim(M["upperarm" + S], E - H, H)
    M["lowerarm" + S] = _aim(M["lowerarm" + S], Tp - E, E)
    ht = T.to_3x3().normalized().to_4x4()
    ht.translation = Tp
    old_hand = M["hand" + S].copy()
    M["hand" + S] = ht
    # fingers/thumb ride the corrected hand (keep their pose relative to the hand)
    corr = ht @ old_hand.inverted()
    for n in ("fingers_01", "fingers_02", "thumb_01", "thumb_02"):
        M[n + S] = corr @ M[n + S]
    return dv.length - d


def locals_from_matrices(skel, M):
    out = {}
    for pb in skel.pose.bones:
        b = pb.bone
        if b.parent:
            ref = M[b.parent.name] @ b.parent.matrix_local.inverted() @ b.matrix_local
        else:
            ref = b.matrix_local
        out[pb.name] = (ref.inverted() @ M[pb.name]).decompose()
    return out


def bake_to_skeleton(skel, rig, rig_action, out_name, frames, step=1.0, switches_reset=True, exact_legs=True):
    """Evaluate rig_action frame by frame and write the deform bones' local transforms as a new skeleton action.

    Keys every frame (LINEAR), quaternion continuity enforced, scale keyed explicitly (all 1). With exact_legs, legs
    in IK mode are corrected analytically onto the rig's ankle targets.
    """
    scene = bpy.context.scene
    rig.animation_data_create()
    if switches_reset:
        set_switches(rig)
    reset_pose(rig)
    rig.animation_data.action = rig_action
    if rig_action.slots:
        rig.animation_data.action_slot = rig_action.slots[0]
    skel.animation_data_create()
    skel.animation_data.action = None
    samples = {pb.name: [] for pb in skel.pose.bones}
    f = frames[0]
    flist = []
    while f <= frames[1] + 1e-6:
        flist.append(f)
        f += step
    unreachable = 0.0
    for f in flist:
        scene.frame_set(int(math.floor(f)), subframe=f - math.floor(f))
        M = {pb.name: pb.matrix.copy() for pb in skel.pose.bones}
        if exact_legs:
            root = rig.pose.bones["c_root"]
            for side in "lr":
                if root.get("ik_leg_" + side, 0.0) > .999:
                    unreachable = max(unreachable, exact_leg(skel, rig, side, M))
                if root.get("ik_arm_" + side, 0.0) > .999:
                    unreachable = max(unreachable, exact_arm(skel, rig, side, M))
        for name, (loc, rot, sca) in locals_from_matrices(skel, M).items():
            samples[name].append((loc, rot, Vector((1.0, 1.0, 1.0))))
    act = bpy.data.actions.get(out_name)
    if act:
        bpy.data.actions.remove(act)
    act = bpy.data.actions.new(out_name)
    act.use_fake_user = True
    act["unreachable_m"] = unreachable
    slot = act.slots.new(id_type="OBJECT", name=skel.name)
    layer = act.layers.new("Baked")
    strip = layer.strips.new(type="KEYFRAME")
    bag = strip.channelbag(slot, ensure=True)
    for bone, vals in samples.items():
        prev = None
        quats = []
        for (_, q, _) in vals:
            q = q.copy()
            if prev is not None and prev.dot(q) < 0:
                q.negate()
            quats.append(q)
            prev = q
        base = f'pose.bones["{bone}"]'
        chans = [("location", 3, [v[0] for v in vals]), ("rotation_quaternion", 4, quats),
                 ("scale", 3, [v[2] for v in vals])]
        for prop, n, series in chans:
            for i in range(n):
                fc = bag.fcurves.new(f"{base}.{prop}", index=i)
                fc.group = bag.groups.get(bone) or bag.groups.new(bone)
                fc.keyframe_points.add(len(flist))
                co = []
                for fr, val in zip(flist, series):
                    co += [fr - frames[0], val[i]]
                fc.keyframe_points.foreach_set("co", co)
                for kp in fc.keyframe_points:
                    kp.interpolation = "LINEAR"
                fc.update()
    rig.animation_data.action = None
    reset_pose(rig)
    return act


def assign_action(obj, act):
    obj.animation_data_create()
    obj.animation_data.action = act
    if act.slots:
        try:
            obj.animation_data.action_slot = act.slots[0]
        except Exception:
            pass
