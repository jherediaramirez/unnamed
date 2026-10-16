"""Stage 4a: author ground locomotion on the control rig, bake to the deform skeleton, validate, save.

    blender -b --factory-startup --python scripts/s03_locomotion.py [-- --only Walk_F,Sprint_R]

Clips: Idle, SneakIdle, StandToSneak, SneakToStand, Walk/Sprint/Sneak x 8 directions.
Writes RIG_UP_<clip> (editable control-rig actions) and UP_<clip> (baked deform actions) into the master,
validation/s03_locomotion.json (drift, closure, root/scale audit) and validation/clip_meta_locomotion.json.
"""

import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402

import up_anim as A  # noqa: E402
import up_check as K  # noqa: E402
import up_config as C  # noqa: E402
import up_loco as L  # noqa: E402

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ONLY = set(ARGS[ARGS.index("--only") + 1].split(",")) if "--only" in ARGS else None

bpy.ops.wm.open_mainfile(filepath=C.MASTER)
scene = bpy.context.scene
rig = bpy.data.objects["UP_Rig"]
skel = bpy.data.objects["UP_Skeleton"]
meta_path = os.path.join(C.ROOT, "validation", "clip_meta_locomotion.json")
val_path = os.path.join(C.ROOT, "validation", "s03_locomotion.json")
meta = json.load(open(meta_path)) if (ONLY and os.path.exists(meta_path)) else {}
report = json.load(open(val_path)) if (ONLY and os.path.exists(val_path)) else {}


def want(name):
    return ONLY is None or name in ONLY


def validate(name, act, n, velocity, loop, window=None):
    probe = K.SkelProbe(skel)
    try:
        dr, minz = K.drift(probe, act, n, velocity, cycles=2 if loop else 1, loop=loop, window=window)
        dr120, _ = K.drift(probe, act, n, velocity, samples_per_frame=2, cycles=1, loop=loop, window=window)
        dr30, _ = K.drift(probe, act, n, velocity, samples_per_frame=.5, cycles=2 if loop else 1, loop=loop,
                          window=window)
        out = {"drift_cm_60fps": {f"{s}_{p}": round(v * 100, 3) for (s, p), v in dr.items()},
               "drift_cm_max_60fps": round(max(dr.values()) * 100, 3),
               "drift_cm_max_120fps": round(max(dr120.values()) * 100, 3),
               "drift_cm_max_30fps": round(max(dr30.values()) * 100, 3),
               "unreachable_cm": round(act.get("unreachable_m", 0.0) * 100, 3),
               "min_sole_z_cm": round(minz * 100, 3)}
        if loop:
            out["closure"] = {k: round(v, 5) for k, v in K.loop_closure(probe, act, n).items()}
    finally:
        probe.restore()
        skel.animation_data.action = None
    out.update(K.channel_audit(act))
    return out


t0 = time.time()
# ---------------------------------------------------------------- idle family
for name, kind in (("Idle", "idle"), ("SneakIdle", "sneakidle"), ("StandToSneak", "s2c"), ("SneakToStand", "c2s")):
    if not want(name):
        continue
    if kind == "idle":
        ra = L.author_idle(rig, "RIG_UP_" + name, 120, 0.0)
    elif kind == "sneakidle":
        ra = L.author_idle(rig, "RIG_UP_" + name, 120, 1.0)
    else:
        ra = L.author_crouch_transition(rig, "RIG_UP_" + name, kind == "s2c", 16)
    n = C.CLIPS[name]["frames"]
    baked = A.bake_to_skeleton(skel, rig, ra, "UP_" + name, (0, n))
    loop = C.CLIPS[name]["loop"]
    report[name] = validate(name, baked, n, (0.0, 0.0), loop)
    meta[name] = {"frames": n, "seconds": n / C.FPS, "loop": loop, "speed_mps": 0.0, "root_motion": False}
    print("[loco]", name, json.dumps({k: report[name][k] for k in ("drift_cm_max_60fps", "drift_cm_max_120fps",
                                                                    "min_sole_z_cm", "unreachable_cm")}))

# ---------------------------------------------------------------- gaits
for family in ("Walk", "Sprint", "Sneak"):
    for d in C.DIRS:
        name = f"{family}_{d}"
        if not want(name):
            continue
        gait = L.Gait(family, d)
        ra, diag = L.author_gait(rig, gait, "RIG_UP_" + name)
        n = gait.N
        baked = A.bake_to_skeleton(skel, rig, ra, "UP_" + name, (0, n))
        vel = (gait.d.x * gait.v, gait.d.y * gait.v)
        def window(side, f, gait=gait, n=n):
            ps = (f / n - (0.0 if side == "l" else .5)) % 1.0
            return ps <= gait.D + 1e-6
        report[name] = validate(name, baked, n, vel, True, window)
        report[name]["diag"] = {k: (round(v, 4) if isinstance(v, float) else v) for k, v in diag.items()}
        meta[name] = {
            "family": family, "direction": d, "direction_deg": C.DIR_ANGLE[d], "frames": n, "seconds": n / C.FPS,
            "loop": True, "speed_mps": gait.v, "travel_vector_char_space": [round(vel[0], 5), round(vel[1], 5)],
            "duty_factor": gait.D, "contacts": {
                "left_contact": {"phase": 0.0, "frame": 0.0}, "right_contact": {"phase": .5, "frame": n / 2},
                "left_toe_off": {"phase": round(gait.D, 4), "frame": round(gait.D * n, 3)},
                "right_toe_off": {"phase": round(.5 + gait.D, 4), "frame": round((.5 + gait.D) * n, 3)}},
            "body_yaw_toward_travel_deg": round(gait.yaw, 2), "backward_style": gait.backward,
            "pelvis_base_height_m": round(gait.hip_z, 4)}
        r = report[name]
        print("[loco]", name, f"drift60 {r['drift_cm_max_60fps']} cm  drift120 {r['drift_cm_max_120fps']} cm  "
              f"minz {r['min_sole_z_cm']} cm  ext {r['diag']['max_extension']}  hip {r['diag']['hip_z']}  "
              f"seam {r['closure']['seam_accel_ratio']:.2f} unreach {r['unreachable_cm']}  root {r['root_motion_max']:.2e}  "
              f"scale {r['scale_dev_max']:.2e}")

json.dump(report, open(val_path, "w"), indent=1)
json.dump(meta, open(meta_path, "w"), indent=1)
rig.animation_data.action = None
A.reset_pose(rig)
A.set_switches(rig)
bpy.ops.wm.save_as_mainfile(filepath=C.MASTER, compress=True)
print(f"[loco] saved master; {time.time() - t0:.1f} s")
