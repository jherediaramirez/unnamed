"""One-off patch: exact-leg bake, gait parameter update, stance-window drift, seam acceleration metric."""
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


patch("up_loco.py", [
    ('''    "Walk": dict(T=36 / 60, D=.27, D_diag=.27, lift=.17, lead=.46,''',
     '''    "Walk": dict(T=36 / 60, D=.25, D_diag=.25, lift=.17, lead=.47, lead_back=.5,'''),
    ('''    "Sprint": dict(T=32 / 60, D=.225, D_diag=.225, lift=.27, lead=.44,''',
     '''    "Sprint": dict(T=32 / 60, D=.21, D_diag=.21, lift=.27, lead=.45, lead_back=.54,'''),
    ('''    "Sneak": dict(T=64 / 60, D=.58, D_diag=.46, lift=.075, lead=.48,''',
     '''    # Sneak cycle 0.9 s (54 f) instead of 1.0667 s: the 1.8316 m/s diagonal needs 0.98 m steps at 1.0667 s, which
    # dropped the crouched hips to 0.27-0.40 m; at 0.9 s all eight directions hold 0.46-0.49 m (s03 exploration).
    "Sneak": dict(T=54 / 60, D=.58, D_diag=.47, lift=.075, lead=.5, lead_back=.52,'''),
    ('''        h = hermite(w, -.55 * k, -.35 * k)''',
     '''        h = hermite(w, -.55 * k, -.7 * k)              # retract before touchdown (low foot speed at contact)'''),
    ('''        lift = math.sin(math.pi * (w ** (math.log(.5) / math.log(peak)))) ** 1.15''',
     '''        warp = (.5 - peak) / (peak * (1 - peak))
        lift = math.sin(math.pi * (w + warp * w * (1 - w))) ** 1.4   # finite lift-off speed, soft touchdown'''),
    ('''        wide = Vector((.02 * (1 if side == "l" else -1), 0)) * crouch''',
     '''        wide = Vector((0.0, 0.0))                        # feet stay planted through stand <-> sneak'''),
])

patch("up_config.py", [
    ('''        t[f"Sneak_{d}"] = dict(family="Sneak", dir=d, frames=64, loop=True, old=f"BS_CrouchWalk_{d}")''',
     '''        t[f"Sneak_{d}"] = dict(family="Sneak", dir=d, frames=54, loop=True, old=f"BS_CrouchWalk_{d}",
                               brief_frames=64)'''),
])

anim = open(os.path.join(S, "up_anim.py"), encoding="utf-8").read()
i0, i1 = anim.index("def bake_to_skeleton("), anim.index("def assign_action(")
NEW_BAKE = '''def _aim(m, new_dir, head):
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


'''
open(os.path.join(S, "up_anim.py"), "w", encoding="utf-8").write(anim[:i0] + NEW_BAKE + anim[i1:])
print("[patch] up_anim.py bake")

patch("up_check.py", [
    ('''def drift(probe, act, n, velocity, fps=C.FPS, samples_per_frame=1, cycles=2, loop=True, z_tol=.005):''',
     '''def drift(probe, act, n, velocity, fps=C.FPS, samples_per_frame=1, cycles=2, loop=True, z_tol=.003, window=None):'''),
    ('''        for key, p in probe.sole().items():
            w = p + vel * t
            minz = min(minz, p.z)
            track.setdefault(key, []).append((f, w, p.z))''',
     '''        for key, p in probe.sole().items():
            w = p + vel * t
            minz = min(minz, p.z)
            inside = window is None or window(key[0], local_f)
            track.setdefault(key, []).append((f, w, p.z if inside else 1.0))'''),
    ('''    p0, p1, pn1, pn = pose_at(0), pose_at(1), pose_at(n - 1), pose_at(n)
    dpos = max((p0[b].translation - pn[b].translation).length for b in p0)
    drot = max(math.degrees(p0[b].to_quaternion().rotation_difference(pn[b].to_quaternion()).angle) for b in p0)
    dvel = max(((p1[b].translation - p0[b].translation) - (pn[b].translation - pn1[b].translation)).length for b in p0)
    typical = max((p1[b].translation - p0[b].translation).length for b in p0)
    return {"pose_pos_m": dpos, "pose_rot_deg": drot, "seam_velocity_mismatch_m_per_frame": dvel,
            "typical_joint_speed_m_per_frame": typical}''',
     '''    poses = [pose_at(f) for f in range(n + 1)]
    p0, pn = poses[0], poses[n]
    dpos = max((p0[b].translation - pn[b].translation).length for b in p0)
    drot = max(math.degrees(p0[b].to_quaternion().rotation_difference(pn[b].to_quaternion()).angle) for b in p0)

    def acc(prev, cur, nxt, b):  # second difference: seam (N-1, N=0, 1) vs the largest interior one
        return (nxt[b].translation - 2 * cur[b].translation + prev[b].translation).length
    seam = max(acc(poses[n - 1], p0, poses[1], b) for b in p0)
    interior = max(acc(poses[f - 1], poses[f], poses[f + 1], b) for f in range(1, n) for b in p0)
    return {"pose_pos_m": dpos, "pose_rot_deg": drot, "seam_accel_m_per_frame2": seam,
            "max_interior_accel_m_per_frame2": interior, "seam_accel_ratio": seam / max(interior, 1e-9)}'''),
])

patch("s03_locomotion.py", [
    ('''def validate(name, act, n, velocity, loop):
    probe = K.SkelProbe(skel)
    try:
        dr, minz = K.drift(probe, act, n, velocity, cycles=2 if loop else 1, loop=loop)
        dr120, _ = K.drift(probe, act, n, velocity, samples_per_frame=2, cycles=1, loop=loop)''',
     '''def validate(name, act, n, velocity, loop, window=None):
    probe = K.SkelProbe(skel)
    try:
        dr, minz = K.drift(probe, act, n, velocity, cycles=2 if loop else 1, loop=loop, window=window)
        dr120, _ = K.drift(probe, act, n, velocity, samples_per_frame=2, cycles=1, loop=loop, window=window)
        dr30, _ = K.drift(probe, act, n, velocity, samples_per_frame=.5, cycles=2 if loop else 1, loop=loop,
                          window=window)'''),
    ('''               "drift_cm_max_120fps": round(max(dr120.values()) * 100, 3),''',
     '''               "drift_cm_max_120fps": round(max(dr120.values()) * 100, 3),
               "drift_cm_max_30fps": round(max(dr30.values()) * 100, 3),
               "unreachable_cm": round(act.get("unreachable_m", 0.0) * 100, 3),'''),
    ('''        report[name] = validate(name, baked, n, vel, True)''',
     '''        def window(side, f, gait=gait, n=n):
            ps = (f / n - (0.0 if side == "l" else .5)) % 1.0
            return ps <= gait.D + 1e-6
        report[name] = validate(name, baked, n, vel, True, window)'''),
    ('''f"seam {r['closure']['seam_velocity_mismatch_m_per_frame']:.4f}''',
     '''f"seam {r['closure']['seam_accel_ratio']:.2f} unreach {r['unreachable_cm']}'''),
    ('''    print("[loco]", name, json.dumps({k: report[name][k] for k in ("drift_cm_max_60fps", "min_sole_z_cm")}))''',
     '''    print("[loco]", name, json.dumps({k: report[name][k] for k in ("drift_cm_max_60fps", "drift_cm_max_120fps",
                                                                    "min_sole_z_cm", "unreachable_cm")}))'''),
])
