"""Check the locomotion actions in foundry/BetterSteve.blend on the solved rig and the deformed mesh.

    blender -b foundry/BetterSteve.blend --python scripts/check_locomotion.py -- <project root>

Per action (limits from docs/ANIMATION_HANDOFF.md):
    seam      loops: every deform bone's pose on the last frame == the first (mm, deg)
    slide     planted frames: the grounded sole point (heel, mid-sole or toe pad) moves speed/30 m per frame against
              the travel direction, within 2 mm; contact_z: it stays within 5 mm of the ground
    ik_miss   planted frames: the solved foot is where its IK control asked (mm)
    min_z     lowest deformed vertex on any frame, >= -5 mm
    eye_z     crouch idle: eye height (the game's crouching eyes are at 1.27)
Actions (animate_actions.py):
    tip       swings: where the pickaxe tip is on the contact frame (from the eyes: forward, up, elevation angle),
              its speed into and out of the hit, and the frame each joint (spine_03, upperarm, lowerarm, hand) peaks
    prop      any frame: pickaxe vertices more than 3 mm inside the deformed body, away from the fist
    pull      swims: the wrists' backward speed while pulling (frames above half the swim speed), against the speed
Writes previews/locomotion_check.json, which the Unreal check (see README) reads for the same numbers.
"""

import bpy
import json
import math
import os
import sys

import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ROOT = os.path.abspath(ARGS[0]) if ARGS else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FPS = 30
scene = bpy.context.scene
arm = bpy.data.objects["Armature"]
body = bpy.data.objects["SK_BetterSteve"]
PB = arm.pose.bones
DEFORM = [pb.name for pb in PB if pb.bone.use_deform]
SIDES = (("l", 1, 0.), ("r", -1, .5))


def sole_points():
    """Left sole sample points (rest, armature space) and the bone that carries each."""
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
    x = PB["foot_l"].bone.head_local.x
    heel = max(c.y for c in foot if c.z < sole + .01)
    ball = PB["ball_l"].bone.head_local.y
    tip = min(c.y for c in toes)
    return sole, {"heel": ("foot", Vector((x, heel, sole))), "mid": ("foot", Vector((x, (heel + ball) / 2, sole))),
                  "toe": ("ball", Vector((x, (ball + tip) / 2, sole)))}


SOLE, POINTS = sole_points()
PROP = bpy.data.objects.get("SM_StandInPickaxe")


def mirror(v, sx):
    return Vector((v.x * sx, v.y, v.z))


def posed(pb, p_rest):
    return arm.matrix_world @ (pb.matrix @ (pb.bone.matrix_local.inverted() @ p_rest))


def set_action(act):
    ad = arm.animation_data_create()
    ad.action = act
    try:
        if act and ad.action_slot is None:
            ad.action_slot = act.slots[0]
    except AttributeError:
        pass


def min_vertex_z():
    dg = bpy.context.evaluated_depsgraph_get()
    ev = body.evaluated_get(dg)
    me = ev.to_mesh()
    co = np.empty(len(me.vertices) * 3, np.float64)
    me.vertices.foreach_get("co", co)
    ev.to_mesh_clear()
    M = np.array(body.matrix_world)
    return float((co.reshape(-1, 3) @ M[2, :3] + M[2, 3]).min())


def sample(f):
    scene.frame_set(f)
    r = {"min_z": min_vertex_z(), "bones": {n: PB[n].matrix.copy() for n in DEFORM},
         "eye": (arm.matrix_world @ PB["eye_l"].head).z,
         "eyes": arm.matrix_world @ ((PB["eye_l"].head + PB["eye_r"].head) / 2),
         "wrist": {s: arm.matrix_world @ PB[f"hand_{s}"].head for s in ("l", "r")}}
    if PROP:
        r["tip"] = PROP.matrix_world @ Vector(PROP["tip_local"])
    for side, sx, _ in SIDES:
        r[side] = {k: posed(PB[f"{b}_{side}"], mirror(p, sx)) for k, (b, p) in POINTS.items()}
        ctrl = arm.matrix_world @ PB[f"CTRL_foot_ik_{side}"].head
        r[side + "_miss"] = (ctrl - arm.matrix_world @ PB[f"foot_{side}"].head).length
    return r


def check(act):
    set_action(act)
    f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    rows = [sample(f) for f in range(f0, f1 + 1)]
    out = {"frames": f1 - f0, "loop": bool(act.get("bs_loop")), "family": act.get("bs_family"),
           "min_z_mm": round(min(r["min_z"] for r in rows) * 1000, 2)}
    if out["loop"]:
        a, b = rows[0]["bones"], rows[-1]["bones"]
        out["seam_mm"] = round(max((a[n].translation - b[n].translation).length for n in DEFORM) * 1000, 3)
        out["seam_deg"] = round(max(math.degrees(a[n].to_quaternion().rotation_difference(b[n].to_quaternion()).angle)
                                    for n in DEFORM), 3)
    if "bs_speed" in act:
        v, beta, N = act["bs_speed"], act["bs_beta"], act["bs_cycle"]
        expect = -Vector(act["bs_travel"]) * v / FPS
        slide, cz, miss, pairs = 0., 0., 0., 0
        for side, sx, off in SIDES:
            for k in range(N):
                ph = (k / N + off) % 1
                if ph + 1 / N > beta + 1e-9:   # both frames of the pair must be planted
                    continue
                p0, p1 = rows[k][side], rows[k + 1][side]
                key = min(p0, key=lambda n: max(p0[n].z, p1[n].z))
                d = p1[key] - p0[key]
                slide = max(slide, (d.xy - expect).length)
                cz = max(cz, abs(p0[key].z - SOLE), abs(p1[key].z - SOLE))
                miss = max(miss, rows[k][side + "_miss"], rows[k + 1][side + "_miss"])
                pairs += 1
        out.update(speed=round(v, 6), cycle=N, beta=round(beta, 4), planted_pairs=pairs,
                   slide_mm=round(slide * 1000, 3), contact_z_mm=round(cz * 1000, 3), ik_miss_mm=round(miss * 1000, 3),
                   travel=list(act["bs_travel"]))
    if "bs_look" in act:   # where the face and the left eye point, against the asked yaw (+ right) / pitch (+ up)
        out["look"] = list(act["bs_look"])
        yaw, pitch = (math.radians(a) for a in act["bs_look"])
        want = Vector((-math.sin(yaw) * math.cos(pitch), -math.cos(yaw) * math.cos(pitch), math.sin(pitch)))
        scene.frame_set(f0)
        for key, bn in (("head", "head"), ("eye", "eye_l")):   # both face -Y at rest
            pb = PB[bn]
            d = pb.matrix.to_3x3() @ pb.bone.matrix_local.to_3x3().inverted() @ Vector((0, -1, 0))
            out[f"{key}_err_deg"] = round(math.degrees(d.angle(want)), 2)
    if act.name == "BS_CrouchIdle":
        out["eye_z"] = round(rows[0]["eye"], 4)
        out["eye_z_range"] = [round(min(r["eye"] for r in rows), 4), round(max(r["eye"] for r in rows), 4)]
    if "bs_contact" in act:
        c = int(act["bs_contact"]) - f0
        tip, eyes = rows[c]["tip"], rows[c]["eyes"]
        d = tip - eyes
        out.update(contact_frame=c, tip_contact=[round(x, 5) for x in tip],
                   tip_from_eyes=[round(-d.y, 3), round(d.z, 3)],
                   tip_elev_deg=round(math.degrees(math.atan2(d.z, d.xy.length)), 1),
                   tip_speed_in=round((tip - rows[c - 1]["tip"]).length * FPS, 2),
                   tip_speed_out=round((rows[c + 1]["tip"] - tip).length * FPS, 2))

        def ang(n, k):
            return math.degrees(rows[k - 1]["bones"][n].to_quaternion().rotation_difference(
                rows[k]["bones"][n].to_quaternion()).angle)
        out["peak_frame"] = {n: max(range(1, c + 1), key=lambda k: ang(n, k))
                             for n in ("spine_03", "upperarm_r", "lowerarm_r", "hand_r")}
    if act.get("bs_prop") and PROP:
        out.update(prop_inside(f0, f1))
    if act.get("bs_swim_speed"):
        v = act["bs_swim_speed"]
        out["swim_speed"] = v
        for s in ("l", "r"):
            vy = [(rows[k + 1]["wrist"][s].y - rows[k]["wrist"][s].y) * FPS for k in range(len(rows) - 1)]
            pull = [x for x in vy if x > .5 * v]
            out[f"pull_{s}"] = {"mean": round(sum(pull) / max(1, len(pull)), 2), "max": round(max(vy), 2),
                                "frames": len(pull)}
    return out


def prop_inside(f0, f1):
    """Pickaxe vertices more than 3 mm inside the deformed body (nearest face's normal), ignoring the fist."""
    grip, verts = Vector(PROP["grip_local"]), [v.co.copy() for v in PROP.data.vertices]
    worst, count, frame = 0., 0, None
    for f in range(f0, f1 + 1):
        scene.frame_set(f)
        bvh = BVHTree.FromObject(body, bpy.context.evaluated_depsgraph_get())
        M, B = PROP.matrix_world, body.matrix_world.inverted()
        g = M @ grip
        for co in verts:
            p = M @ co
            if (p - g).length < .08:
                continue
            loc, nrm, _, _ = bvh.find_nearest(B @ p)
            depth = -(B @ p - loc).dot(nrm) if loc is not None else 0.
            if depth > .003:
                count += 1
                if depth > worst:
                    worst, frame = depth, f
    return {"prop_inside_verts": count, "prop_depth_mm": round(worst * 1000, 1), "prop_worst_frame": frame}


set_action(None)
scene.frame_set(0)
res = {"sole_z": round(SOLE, 5), "points": {k: list(p) for k, (_, p) in POINTS.items()},
       "point_bones": {k: b for k, (b, _) in POINTS.items()}, "rest_min_z_mm": None, "actions": {}}
bpy.context.view_layer.update()
res["rest_min_z_mm"] = round(min_vertex_z() * 1000, 2)
if PROP:   # the stand-in pickaxe at rest (Blender metres), for the Unreal socket check
    res["pickaxe"] = {"grip": list(PROP["grip_rest"]), "tip": list(PROP["tip_rest"])}
fails = []
for act in sorted((a for a in bpy.data.actions if "bs_family" in a), key=lambda a: a.name):
    o = check(act)
    res["actions"][act.name] = o
    bad = []
    if o["min_z_mm"] < -5 and act.get("bs_ground", 1):
        bad.append("ground")
    if o.get("prop_depth_mm", 0) > 10:
        bad.append("prop")
    if o.get("seam_mm", 0) > .5 or o.get("seam_deg", 0) > .1:
        bad.append("seam")
    if o.get("slide_mm", 0) > 2:
        bad.append("slide")
    if o.get("head_err_deg", 0) > 10:
        bad.append("look")
    if o.get("contact_z_mm", 0) > 5:
        bad.append("contact_z")
    if bad:
        fails.append((act.name, bad))
    print(f"[check] {act.name:22s} f{o['frames']:3d} min_z {o['min_z_mm']:6.2f}mm"
          + (f" seam {o['seam_mm']:.3f}mm/{o['seam_deg']:.3f}deg" if o["loop"] else "")
          + (f" slide {o['slide_mm']:.3f}mm contact_z {o['contact_z_mm']:.2f}mm ik_miss {o['ik_miss_mm']:.2f}mm"
             f" pairs {o['planted_pairs']}" if "slide_mm" in o else "")
          + (f" eye_z {o['eye_z']} {o['eye_z_range']}" if "eye_z" in o else "")
          + (f" head_err {o['head_err_deg']}deg eye_err {o['eye_err_deg']}deg" if "head_err_deg" in o else "")
          + (f" tip fwd/up {o['tip_from_eyes']} elev {o['tip_elev_deg']}deg speed in/out {o['tip_speed_in']}/"
             f"{o['tip_speed_out']} m/s peaks {o['peak_frame']}" if "tip_contact" in o else "")
          + (f" prop inside {o['prop_inside_verts']} verts, {o['prop_depth_mm']}mm @f{o['prop_worst_frame']}"
             if "prop_depth_mm" in o else "")
          + (f" pull l {o['pull_l']} r {o['pull_r']} (swim {o['swim_speed']})" if "pull_l" in o else "")
          + ("  FAIL " + ",".join(bad) if bad else ""))
set_action(None)
path = os.path.join(ROOT, "previews", "locomotion_check.json")
with open(path, "w") as f:
    json.dump(res, f, indent=1)
print(f"[check] rest min_z {res['rest_min_z_mm']} mm; {len(res['actions'])} actions; failures: {fails or 'none'}")
print(f"[check] {path}")
