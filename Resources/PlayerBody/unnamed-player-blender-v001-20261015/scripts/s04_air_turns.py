"""Stage 4b: air clips, stationary turns (stand + sneak), placeholders -> bake -> validate -> save.

    blender -b --factory-startup --python scripts/s04_air_turns.py

Writes RIG_UP_* / UP_* actions, validation/s04_air_turns.json and validation/clip_meta_air_turns.json
(jump trajectory per 20 Hz tick, landing selection proposal, turn body-yaw progress curves and step windows).
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import up_actions as AC  # noqa: E402
import up_anim as A  # noqa: E402
import up_check as K  # noqa: E402
import up_config as C  # noqa: E402

bpy.ops.wm.open_mainfile(filepath=C.MASTER)
rig = bpy.data.objects["UP_Rig"]
skel = bpy.data.objects["UP_Skeleton"]
report, meta = {}, {}


def sole_track(act, n, yaw_fn=None):
    """Per frame sole points in WORLD space (yaw_fn applies the presentation yaw about the root)."""
    probe = K.SkelProbe(skel)
    out = []
    try:
        for f in range(n + 1):
            probe.set(act, f)
            pts = probe.sole()
            if yaw_fn:
                R = Matrix.Rotation(math.radians(yaw_fn(f)), 4, "Z")
                pts = {k: R @ v for k, v in pts.items()}
            out.append(pts)
    finally:
        probe.restore()
        skel.animation_data.action = None
    return out


def planted_drift(track, window):
    worst, minz = 0.0, 1e9
    for key in track[0]:
        start = None
        for f, pts in enumerate(track):
            p = pts[key]
            minz = min(minz, p.z)
            if p.z <= .003 and window(key[0], f):
                if start is None:
                    start = p
                worst = max(worst, (p - start).to_2d().length)
            else:
                start = None
    return worst, minz


# ---------------------------------------------------------------- air
acts = AC.author_air(rig)
for name, ra in acts.items():
    n = C.CLIPS[name]["frames"]
    baked = A.bake_to_skeleton(skel, rig, ra, "UP_" + name, (0, n))
    tr = sole_track(baked, n)
    landing = name.startswith("Land")
    drift, minz = planted_drift(tr, lambda side, f: landing)
    r = {"min_sole_z_cm": round(minz * 100, 3), "planted_drift_cm": round(drift * 100, 3) if landing else None}
    r.update(K.channel_audit(baked))
    if C.CLIPS[name]["loop"]:
        probe = K.SkelProbe(skel)
        try:
            r["closure"] = {k: round(v, 5) for k, v in K.loop_closure(probe, baked, n).items()}
        finally:
            probe.restore()
            skel.animation_data.action = None
    report[name] = r
    print("[air]", name, json.dumps({k: v for k, v in r.items() if k != "closure"}))
jump = AC.jump_trajectory()
meta["air"] = {
    "jump_ticks_cm": [{"tick": t, "feet_cm": round(z, 3), "v_cm_per_tick": round(v, 3)} for t, z, v in jump],
    "apex_cm": round(max(z for _, z, _ in jump), 3),
    "flat_airtime_ticks": jump[-1][0],
    "sequence": "Ground -> Jump_Start (on the jump tick; no anticipation delay) -> Jump_Rise (hold while vz > 0) -> "
                "Jump_Fall (loop while vz <= 0) -> Land_Soft / Land_Firm -> Ground. Edge falls skip Jump_Start.",
    "landing_proposal": {"soft_if_impact_m_s_below": 10.0, "firm_weight": "clamp((v_impact - 8) / 4, 0, 1)",
                         "flat_jump_impact_m_s": round(abs(jump[-2][2]) * 20 / 100, 2),
                         "note": "a jump on flat ground lands at ~8.9 m/s (game gravity 32 m/s^2), so soft is the "
                                 "everyday landing; a 2-block drop lands at ~11 m/s -> firm. Tuning proposal only."},
    "rise_hold": "Jump_Rise frames 12..18 are static: hold the last frame while still rising (jump boost, ledges).",
    "interrupt": "a new jump may start from any landing frame; landings never delay input (game-owned).",
}

# ---------------------------------------------------------------- turns
meta["turns"] = {}
for stance, pre, crouch in (("Stand", "Turn", 0.0), ("Sneak", "SneakTurn", 1.0)):
    for side in "LR":
        for deg in (45, 90):
            name = f"{pre}_{side}{deg}"
            n = C.CLIPS[name]["frames"]
            ra, curve, sched = AC.author_turn(rig, "RIG_UP_" + name, side, deg, n, crouch)
            baked = A.bake_to_skeleton(skel, rig, ra, "UP_" + name, (0, n))
            ycurve = dict(curve)
            tr = sole_track(baked, n, yaw_fn=lambda f: ycurve[f])

            def window(s, f, sched=sched, n=n):
                t = f / n
                t0, t1 = sched[s]
                return t <= t0 - 1e-6 or t >= t1 + 1e-6
            drift, minz = planted_drift(tr, window)
            r = {"planted_drift_world_cm": round(drift * 100, 3), "min_sole_z_cm": round(minz * 100, 3)}
            r.update(K.channel_audit(baked))
            report[name] = r
            meta["turns"][name] = {
                "stance": stance, "direction": side, "degrees": deg, "frames": n, "seconds": n / C.FPS,
                "sign_convention": "+ = turn left (counter-clockwise from above); root stays at 0 deg",
                "body_yaw_total_deg": deg if side == "L" else -deg,
                "body_yaw_progress": [{"frame": f, "progress": round(abs(y) / deg, 5)} for f, y in curve],
                "steps": {s: {"lift_frame": round(sched[s][0] * n, 2), "plant_frame": round(sched[s][1] * n, 2)}
                          for s in "lr"},
                "lead_foot": "l" if side == "L" else "r"}
            print("[turn]", name, json.dumps(r))
meta["turn_controller_proposal"] = {
    "trigger_separation_deg": 50, "step_45_if_separation_below_deg": 95, "settle_within_deg": 15,
    "apply": "presentation yaw = start_yaw + body_yaw_total * progress(t) on the SAME clock as the pose; "
             "root-neutral clips never rotate the root, so apply the yaw exactly once (mesh/body yaw only)",
    "interrupt": "movement input cross-fades into locomotion over 0.1-0.15 s; keep the yaw reached so far"}

# ---------------------------------------------------------------- placeholders
for name, ra in AC.author_placeholders(rig).items():
    n = C.CLIPS[name]["frames"]
    baked = A.bake_to_skeleton(skel, rig, ra, "UP_" + name, (0, max(n, 1)) if n > 1 else (0, 0))
    r = K.channel_audit(baked)
    r["placeholder"] = True
    report[name] = r
    print("[ph]", name, json.dumps(r))

json.dump(report, open(os.path.join(C.ROOT, "validation", "s04_air_turns.json"), "w"), indent=1)
json.dump(meta, open(os.path.join(C.ROOT, "validation", "clip_meta_air_turns.json"), "w"), indent=1)
rig.animation_data.action = None
A.reset_pose(rig)
A.set_switches(rig)
bpy.ops.wm.save_as_mainfile(filepath=C.MASTER, compress=True)
print("[s04] saved")
