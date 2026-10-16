"""Clip validation on the DEFORM skeleton with the baked action and every constraint muted (no rig masking)."""

import math

import bpy
from mathutils import Matrix, Vector

import up_config as C

SOLE_POINTS = {  # name: (bone suffix, armature-rest point builder)
    "heel": ("foot", lambda hx: Vector((hx, C.J["heel_y"], 0.0))),
    "mid": ("foot", lambda hx: Vector((hx, .0, 0.0))),
    "ball": ("ball", lambda hx: Vector((hx, C.J["ball_y"], 0.0))),
    "tip": ("ball", lambda hx: Vector((hx, C.J["toe_tip_y"], 0.0))),
}


class SkelProbe:
    def __init__(self, skel):
        self.skel = skel
        self.saved = []
        for pb in skel.pose.bones:
            for c in pb.constraints:
                self.saved.append((c, c.mute))
                c.mute = True
        self.local_pts = {}
        for side in "lr":
            hx = (C.LEG_X[0] + C.LEG_X[1]) / 2 * (1 if side == "l" else -1)
            for nm, (bone, fn) in SOLE_POINTS.items():
                b = skel.data.bones[f"{bone}_{side}"]
                self.local_pts[(side, nm)] = (b.name, b.matrix_local.inverted() @ fn(hx))

    def restore(self):
        for c, m in self.saved:
            c.mute = m

    def set(self, act, frame):
        ad = self.skel.animation_data or self.skel.animation_data_create()
        if ad.action != act:
            ad.action = act
            if act.slots:
                ad.action_slot = act.slots[0]
        fi = math.floor(frame)
        bpy.context.scene.frame_set(int(fi), subframe=frame - fi)

    def sole(self):
        P = self.skel.pose.bones
        out = {}
        for key, (bone, lp) in self.local_pts.items():
            out[key] = P[bone].matrix @ lp
        return out

    def joints(self):
        return {pb.name: pb.matrix.copy() for pb in self.skel.pose.bones}


def drift(probe, act, n, velocity, fps=C.FPS, samples_per_frame=1, cycles=2, loop=True, z_tol=.003, window=None):
    """Max planted-point drift (m) in TRAVEL space (in-place + velocity * t), per side and point.

    A point is planted while its height is <= z_tol; drift = max distance from its first planted position.
    """
    vel = Vector((velocity[0], velocity[1], 0.0))
    total = n * cycles if loop else n
    steps = int(total * samples_per_frame)
    track = {}
    minz = 1e9
    for i in range(steps + 1):
        f = i / samples_per_frame
        local_f = f % n if loop and f < total else min(f, n)
        if loop and f >= n and abs(local_f) < 1e-9 and f > 0:
            local_f = 0.0
        probe.set(act, local_f)
        t = f / fps
        for key, p in probe.sole().items():
            w = p + vel * t
            minz = min(minz, p.z)
            inside = window is None or window(key[0], local_f)
            track.setdefault(key, []).append((f, w, p.z if inside else 1.0))
    worst = {}
    detail = []
    for key, seq in track.items():
        best = 0.0
        start = None
        for f, w, z in seq:
            if z <= z_tol:
                if start is None:
                    start = w
                else:
                    d = (w - start).to_2d().length
                    best = max(best, d)
            else:
                start = None
        worst[key] = best
    return worst, minz


def loop_closure(probe, act, n):
    """Pose difference frame N vs 0 (m / deg) and seam velocity mismatch (m per frame) on joint positions."""
    def pose_at(f):
        probe.set(act, f)
        return probe.joints()
    poses = [pose_at(f) for f in range(n + 1)]
    p0, pn = poses[0], poses[n]
    dpos = max((p0[b].translation - pn[b].translation).length for b in p0)
    drot = max(math.degrees(p0[b].to_quaternion().rotation_difference(pn[b].to_quaternion()).angle) for b in p0)

    def acc(prev, cur, nxt, b):  # second difference: seam (N-1, N=0, 1) vs the largest interior one
        return (nxt[b].translation - 2 * cur[b].translation + prev[b].translation).length
    seam = max(acc(poses[n - 1], p0, poses[1], b) for b in p0)
    interior = max(acc(poses[f - 1], poses[f], poses[f + 1], b) for f in range(1, n) for b in p0)
    return {"pose_pos_m": dpos, "pose_rot_deg": drot, "seam_accel_m_per_frame2": seam,
            "max_interior_accel_m_per_frame2": interior, "seam_accel_ratio": seam / max(interior, 1e-9)}


def channel_audit(act, skel_name="UP_Skeleton"):
    """Root motion / scale animation audit straight from the baked fcurves."""
    import up_anim as A
    root_motion = 0.0
    scale_dev = 0.0
    keys = 0
    for fc in A.action_fcurves(act):
        vals = [kp.co[1] for kp in fc.keyframe_points]
        keys = max(keys, len(vals))
        if fc.data_path == 'pose.bones["root"].location' or fc.data_path == 'pose.bones["root"].rotation_quaternion':
            ref = 1.0 if (fc.data_path.endswith("quaternion") and fc.array_index == 0) else 0.0
            root_motion = max(root_motion, max(abs(v - ref) for v in vals))
        if fc.data_path.endswith(".scale"):
            scale_dev = max(scale_dev, max(abs(v - 1.0) for v in vals))
    return {"root_motion_max": root_motion, "scale_dev_max": scale_dev, "keys_per_channel": keys}
