"""Stage 5: look/aim grid (stand + sneak), mesh-space additive emulation checks, first-person previews.

    blender -b --factory-startup --python scripts/s05_look_fp.py [-- --no-render]

Look_* / SneakLook_* are single-frame baked poses (UP_Look_C = the documented additive reference for standing,
UP_SneakLook_C for the sneak grid). Validation: head/arm penetration into the torso for every sample, plus the
STANDING grid applied as a mesh-space rotation additive onto the sneak idle and Walk_F (what the runtime would do).
First person: camera fixed at the gameplay eye point (not parented to the head), head hidden, vertical FOV 70/110.
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
from mathutils import Euler, Matrix, Quaternion, Vector  # noqa: E402

import up_anim as A  # noqa: E402
import up_config as C  # noqa: E402
import up_loco as L  # noqa: E402
import up_look as LK  # noqa: E402
import up_preview as PV  # noqa: E402
import up_render as R  # noqa: E402
import up_validate as Vd  # noqa: E402

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
bpy.ops.wm.open_mainfile(filepath=C.MASTER)
scene = bpy.context.scene
rig = bpy.data.objects["UP_Rig"]
skel = bpy.data.objects["UP_Skeleton"]
body = bpy.data.objects["UP_Body"]
head = bpy.data.objects["UP_Head"]
report = {"stand": {}, "sneak_grid": {}, "stand_grid_on_sneak_additive": {}, "stand_grid_on_walk_additive": {}}

# ---------------------------------------------------------------- author + bake the grids
for prefix, crouch in (("Look", 0.0), ("SneakLook", 1.0)):
    for key, (yaw, pitch) in LK.GRID.items():
        act = A.new_action(rig, f"RIG_UP_{prefix}_{key}")
        A.set_switches(rig, frame=0)
        p = LK.look_pose(rig, yaw, pitch, crouch, stance_grid=crouch > .5)
        A.reset_pose(rig)
        p.apply(reset=True)
        L.key_all(rig, 0)
        A.bake_to_skeleton(skel, rig, act, f"UP_{prefix}_{key}", (0, 0))
print("[look] grids baked")


def mute(on):
    for pb in skel.pose.bones:
        for c in pb.constraints:
            c.mute = on


def pose_skel_from_action(act, frame=0):
    ad = skel.animation_data or skel.animation_data_create()
    ad.action = act
    ad.action_slot = act.slots[0]
    scene.frame_set(frame)
    return {pb.name: pb.matrix.copy() for pb in skel.pose.bones}


def set_skel_matrices(M):
    ad = skel.animation_data
    ad.action = None
    for name, (loc, rot, sca) in A.locals_from_matrices(skel, M).items():
        pb = skel.pose.bones[name]
        pb.location, pb.rotation_quaternion, pb.scale = loc, rot, (1, 1, 1)
    bpy.context.view_layer.update()


def apply_additive(ref, sample, base):
    """UE-style mesh-space rotation additive (delta = sample * ref^-1 pre-multiplied), parents first."""
    out = {}
    for pb in skel.pose.bones:
        n = pb.name
        d = sample[n].to_quaternion() @ ref[n].to_quaternion().inverted()
        rot = (d @ base[n].to_quaternion()).to_matrix().to_4x4()
        if pb.parent:
            off = base[pb.parent.name].inverted() @ base[n].translation
            pos = out[pb.parent.name] @ off
        else:
            pos = base[n].translation
        rot.translation = pos
        out[n] = rot
    return out


def penetration_now():
    ps = Vd.PartSet(body)
    ph = Vd.PartSet(head)
    return {"head_in_torso_cm": round(ps.penetration("head", "torso", other=ph) * 100, 2),
            "torso_in_head_cm": round(ph.penetration("torso", "head", other=ps) * 100, 2),
            "arm_l_in_torso_cm": round(ps.penetration("arm_l", "torso") * 100, 2),
            "arm_r_in_torso_cm": round(ps.penetration("arm_r", "torso") * 100, 2)}


mute(True)
ref_stand = pose_skel_from_action(bpy.data.actions["UP_Look_C"])
sneak_base = pose_skel_from_action(bpy.data.actions["UP_SneakIdle"])
walk_base = pose_skel_from_action(bpy.data.actions["UP_Walk_F"], 0)
for key in LK.GRID:
    sample = pose_skel_from_action(bpy.data.actions[f"UP_Look_{key}"])
    report["stand"][key] = penetration_now()
    pose_skel_from_action(bpy.data.actions[f"UP_SneakLook_{key}"])
    report["sneak_grid"][key] = penetration_now()
    set_skel_matrices(apply_additive(ref_stand, sample, sneak_base))
    report["stand_grid_on_sneak_additive"][key] = penetration_now()
    set_skel_matrices(apply_additive(ref_stand, sample, walk_base))
    report["stand_grid_on_walk_additive"][key] = penetration_now()
    print("[look]", key, report["stand"][key]["head_in_torso_cm"], report["sneak_grid"][key]["head_in_torso_cm"],
          report["stand_grid_on_sneak_additive"][key]["head_in_torso_cm"],
          report["stand_grid_on_walk_additive"][key]["head_in_torso_cm"])
for pb in skel.pose.bones:
    pb.location, pb.rotation_quaternion = (0, 0, 0), (1, 0, 0, 0)
skel.animation_data.action = None
mute(False)
json.dump(report, open(os.path.join(C.ROOT, "validation", "s05_look.json"), "w"), indent=1)
rig.animation_data.action = None
A.reset_pose(rig)
bpy.ops.wm.save_as_mainfile(filepath=C.MASTER, compress=True)
print("[look] saved master")

if "--no-render" in ARGS:
    sys.exit(0)

# ---------------------------------------------------------------- pre-bake first-person poses (constraints live)
shots = [("stand_p0", 0, 0, 0, C.GAME["stand_eyes_m"]), ("stand_p-45", 0, 0, -45, C.GAME["stand_eyes_m"]),
         ("stand_p-70", 0, 0, -70, C.GAME["stand_eyes_m"]), ("stand_p-90", 0, 0, -90, C.GAME["stand_eyes_m"]),
         ("stand_y60_p-60", 0, 60, -60, C.GAME["stand_eyes_m"]), ("stand_y-60_p-60", 0, -60, -60, C.GAME["stand_eyes_m"]),
         ("sneak_p0", 1, 0, 0, C.GAME["sneak_eyes_m"]), ("sneak_p-60", 1, 0, -60, C.GAME["sneak_eyes_m"]),
         ("sneak_p-90", 1, 0, -90, C.GAME["sneak_eyes_m"])]
fp_acts = {}
for nm, cr, yw, pt, eye in shots:
    act = A.new_action(rig, "TMP_FP_" + nm)
    A.set_switches(rig, frame=0)
    p = LK.look_pose(rig, yw, pt, cr, stance_grid=cr > .5)
    A.reset_pose(rig)
    p.apply(reset=True)
    L.key_all(rig, 0)
    fp_acts[nm] = A.bake_to_skeleton(skel, rig, act, "TMP_FPB_" + nm, (0, 0))
fp_acts["low_p0"] = fp_acts["low_p-45"] = bpy.data.actions["UP_Low_Pose"]
rig.animation_data.action = None

# ---------------------------------------------------------------- renders
pv = PV.BakedPreview(scene)
tmp = os.path.join(C.ROOT, "previews", "_tmp", "look")
os.makedirs(tmp, exist_ok=True)
R.setup_workbench(scene, res=(420, 520))
cam = R.camera("PREVIEW_Cam_Look", bpy.data.collections["PREVIEW"], lens=50)
scene.camera = cam
for prefix in ("Look", "SneakLook"):
    for key in LK.GRID:
        pv.play(bpy.data.actions[f"UP_{prefix}_{key}"], frames=1)
        scene.frame_set(0)
        tgt = Vector((0, 0, 1.15 if prefix == "Look" else .95))
        R.look_at(cam, tgt + Vector((-1.7, -3.2, .5)), tgt)
        scene.render.filepath = os.path.join(tmp, f"{prefix}_{key}.png")
        bpy.ops.render.render(write_still=True)
print("[look] grid renders")


# first person: pose = direct look function for (yaw, pitch); camera at the eye guide, same frame
EYE_LOCAL = None


def eye_socket_world():
    """FP anchor: the rest painted-eye point carried by the CHEST (spine_02), so the hidden head's own pitch/slide
    never drags the body around under the camera; torso lean and stance still move it."""
    global EYE_LOCAL
    hb = skel.data.bones["spine_02"]
    if EYE_LOCAL is None:
        EYE_LOCAL = hb.matrix_local.inverted() @ Vector((0, -C.HEAD / 2, sum(C.EYE_ROW) / 2))
    return skel.matrix_world @ skel.pose.bones["spine_02"].matrix @ EYE_LOCAL


def fp_shot(name, key, yaw, pitch, eye, fov, offset=True):
    pv.play(fp_acts[key], frames=1)
    scene.frame_set(0)
    off = Vector((0, 0, 0))
    if offset:   # presentation-only first-person mesh offset: put the head's eye point under the camera (XY)
        e = eye_socket_world()
        off = Vector((-e.x, -e.y, 0.0))
        pv.travel.animation_data_clear()
        pv.travel.location = off
        bpy.context.view_layer.update()
    head.hide_render = True
    fpcam = R.camera("PREVIEW_Cam_FP", bpy.data.collections["PREVIEW"])
    R.fov_vertical(fpcam, fov)
    fpcam.data.clip_start = .02
    fpcam.location = (0, 0, eye)
    fpcam.rotation_mode = "XYZ"
    # Blender cameras look down -Z with +Y up; X 90 faces +Y (the character's BACK), so add 180 deg of yaw to face
    # forward (-Y); view yaw + = right (clockwise from above), pitch + = up
    fpcam.rotation_euler = Euler((math.radians(90 + pitch), 0, math.radians(180 - yaw)), "XYZ")
    scene.camera = fpcam
    scene.render.resolution_x, scene.render.resolution_y = 768, 432
    scene.render.filepath = os.path.join(tmp, f"FP_{name}.png")
    bpy.ops.render.render(write_still=True)
    # nearest body surface to the eye (camera / shoulder intersection check)
    co, polys = Vd.evaluated(body)
    d = min((Vector(c) - Vector((0, 0, eye))).length for c in co)
    head.hide_render = False
    scene.camera = cam
    scene.render.resolution_x, scene.render.resolution_y = 420, 520
    return {"nearest_body_cm": round(d * 100, 2), "fp_mesh_offset_cm": [round(off.x * 100, 2), round(off.y * 100, 2)]}


fp = {}
for fov in (70, 110):
    for nm, cr, yw, pt, eye in shots:
        fp[f"{nm}_v{fov}"] = fp_shot(f"{nm}_v{fov}", nm, yw, pt, eye, fov)
    for nm, pt in (("low_p0", 0), ("low_p-45", -45)):
        fp[f"{nm}_v{fov}"] = fp_shot(f"{nm}_v{fov}", nm, 0, pt, C.GAME["low_eyes_m"], fov)
    fp[f"stand_p-90_v{fov}_NO_OFFSET"] = fp_shot(f"stand_p-90_v{fov}_NO_OFFSET", "stand_p-90", 0, -90,
                                                 C.GAME["stand_eyes_m"], fov, offset=False)
for a in list(bpy.data.actions):
    if a.name.startswith(("TMP_FP", "TMP_FPB")):
        bpy.data.actions.remove(a)
json.dump({"shots": fp, "near_clip_cm": 2.0, "fp_offset_rule": "mesh XY offset = camera XY - head_eye socket XY "
           "(presentation only, first person only; no vertical offset so feet stay on the floor)"}, open(os.path.join(C.ROOT, "validation",
                                                                                   "s05_first_person.json"), "w"), indent=1)
print("[fp]", json.dumps(fp))
pv.restore()
