"""Stage 7a: clean export copy -> FBX (centimetres, import scale 1) + manifest. Never saves the master.

    blender -b --factory-startup --python scripts/s07_export.py

Unreal wants centimetres INSIDE the FBX: a metre FBX makes Unreal scale the root, leaving every bone at scale 100.
So the export copy is converted in memory: scene unit 0.01, skeleton (+ child meshes) scaled x100 and applied,
baked location keys x100; written with FBX_SCALE_NONE, global scale 1, forward -Z / up Y (lands +Y-facing in UE).
Outputs (exports/): SK_UnnamedPlayer.fbx (mesh+skeleton, no animation), anims/A_UP_<clip>.fbx (one action each,
armature only), UP_AllClips.fbx (every clip as its own take), SM_UP_Pickaxe.fbx (origin = grip),
UP_manifest_v001.json.
"""

import json
import math
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import up_anim as A  # noqa: E402
import up_config as C  # noqa: E402

bpy.ops.wm.open_mainfile(filepath=C.MASTER)
scene = bpy.context.scene
OUT = os.path.join(C.ROOT, "exports")
ANIMS = os.path.join(OUT, "anims")
if os.path.isdir(ANIMS):
    shutil.rmtree(ANIMS)
os.makedirs(ANIMS, exist_ok=True)

skel = bpy.data.objects["UP_Skeleton"]
body = bpy.data.objects["UP_Body"]
head = bpy.data.objects["UP_Head"]
rig = bpy.data.objects["UP_Rig"]
pick = bpy.data.objects["UP_Pickaxe"]

# ---------------------------------------------------------------- manifest facts read BEFORE the cm conversion (m)
rest = {}
for b in skel.data.bones:
    m = b.matrix_local
    rel = (b.parent.matrix_local.inverted() @ m) if b.parent else m
    rest[b.name] = {"parent": b.parent.name if b.parent else None,
                    "head_m": [round(c, 6) for c in b.head_local], "tail_m": [round(c, 6) for c in b.tail_local],
                    "length_m": round(b.length, 6),
                    "parent_relative_location_m": [round(c, 6) for c in rel.to_translation()],
                    "parent_relative_rotation_wxyz": [round(c, 6) for c in rel.to_quaternion()],
                    "local_axes_world": {"x": [round(c, 4) for c in m.col[0][:3]], "y": [round(c, 4) for c in m.col[1][:3]],
                                         "z": [round(c, 4) for c in m.col[2][:3]]}}
RB = rig.data.bones
sock_pick = RB["c_hand_r"].matrix_local.inverted() @ RB["c_grip_r"].matrix_local
sock_grip_l = RB["c_hand_l"].matrix_local.inverted() @ RB["c_grip_l"].matrix_local
eye_world = Vector((0, -C.HEAD / 2, sum(C.EYE_ROW) / 2))
sock_eye = skel.data.bones["head"].matrix_local.inverted() @ Matrix.Translation(eye_world)
sock_fp = skel.data.bones["spine_02"].matrix_local.inverted() @ Matrix.Translation(eye_world)


def sock(name, bone, m, note):
    return {"name": name, "bone": bone, "location_cm": [round(c * 100, 4) for c in m.to_translation()],
            "rotation_wxyz": [round(c, 6) for c in m.to_quaternion()], "note": note}


sockets = [
    sock("hand_r_pickaxe", "hand_r", sock_pick, "pickaxe grip: tool +Y along the handle to the head, +Z toward the long "
         "point; SM_UP_Pickaxe origin/axes match, so attach with an identity relative transform"),
    sock("hand_l_grip", "hand_l", sock_grip_l, "left-hand grip centre (same tool frame convention)"),
    sock("head_eye", "head", sock_eye, "painted-eye point on the head front face (third-person eye line)"),
    sock("fp_anchor", "spine_02", sock_fp, "first-person body anchor: offset the mesh in XY so this point sits under the "
         "camera when looking steeply down (presentation only, see handoff)"),
]
clip_actions = sorted(a.name for a in bpy.data.actions if a.name.startswith("UP_") and not a.name.endswith("_upper"))

# ---------------------------------------------------------------- clean export copy (in memory)
for pb in skel.pose.bones:
    for c in list(pb.constraints):
        pb.constraints.remove(c)
    pb.location, pb.rotation_quaternion, pb.scale = (0, 0, 0), (1, 0, 0, 0), (1, 1, 1)
for o in list(bpy.data.objects):
    if o not in (skel, body, head, pick):
        bpy.data.objects.remove(o, do_unlink=True)
if skel.animation_data:
    skel.animation_data.action = None
    for t in list(skel.animation_data.nla_tracks):
        skel.animation_data.nla_tracks.remove(t)
pick.parent = None
pick.matrix_world = Matrix()          # tool frame = object frame (mesh data is already in tool space)
body.name, head.name = "SK_UnnamedPlayer_Body", "SK_UnnamedPlayer_Head"
skel.name = "UP_Skeleton"
skel.data.name = "UP_Skeleton"
scene.unit_settings.scale_length = .01
skel.scale = (100, 100, 100)
pick.scale = (100, 100, 100)
bpy.context.view_layer.update()
for o in (skel, body, head, pick):
    o.select_set(True)
bpy.context.view_layer.objects.active = skel
bpy.ops.object.transform_apply(location=False, rotation=False, scale=True, properties=True)
for name in clip_actions:
    for fc in A.action_fcurves(bpy.data.actions[name]):
        if fc.data_path.endswith(".location"):
            for kp in fc.keyframe_points:
                kp.co.y *= 100
                kp.handle_left.y *= 100
                kp.handle_right.y *= 100
            fc.update()
bpy.context.view_layer.update()
zs = [(body.matrix_world @ v.co).z for v in body.data.vertices] + [(head.matrix_world @ v.co).z for v in head.data.vertices]
print(f"[export] cm copy: height {max(zs) - min(zs):.3f} BU, skeleton scale {tuple(skel.scale)}")

# use the 1024 skin for the embedded texture (nearest-upscaled 64 px skin; crisp under default filtering)
FBX = dict(apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, axis_forward="-Z",
           axis_up="Y", use_mesh_modifiers=False, mesh_smooth_type="FACE", add_leaf_bones=False,
           primary_bone_axis="Y", secondary_bone_axis="X", use_armature_deform_only=True, armature_nodetype="NULL",
           bake_anim_force_startend_keying=True, bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
           use_custom_props=False)


def select(objs, active):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = active


# skeletal mesh (rest pose, no animation)
select([skel, body, head], skel)
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "SK_UnnamedPlayer.fbx"), use_selection=True,
                         object_types={"ARMATURE", "MESH"}, bake_anim=False, path_mode="COPY", embed_textures=True, **FBX)
print("[export] SK_UnnamedPlayer.fbx")
# pickaxe static mesh
select([pick], pick)
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "SM_UP_Pickaxe.fbx"), use_selection=True, object_types={"MESH"},
                         bake_anim=False, path_mode="AUTO", **FBX)
print("[export] SM_UP_Pickaxe.fbx")
# one FBX per clip, the active action only. The skinned meshes ride along (texture not embedded) because an
# armature-only FBX carries no bind pose: importers then take frame 0 as the rest pose (s08 caught a 1.6 m offset).
skel.animation_data_create()
select([skel, body, head], skel)
for name in clip_actions:
    act = bpy.data.actions[name]
    skel.animation_data.action = act
    skel.animation_data.action_slot = act.slots[0]
    n = int(round(act.frame_range[1]))
    scene.frame_start, scene.frame_end = 0, max(n, 0)
    scene.name = name                 # take name = clip name
    bpy.ops.export_scene.fbx(filepath=os.path.join(ANIMS, f"A_{name}.fbx"), use_selection=True,
                             object_types={"ARMATURE", "MESH"}, bake_anim=True, bake_anim_use_all_bones=True,
                             bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False, path_mode="AUTO",
                             embed_textures=False, **FBX)
print(f"[export] {len(clip_actions)} animation FBXs")
# every clip as its own take in one file (explicitly indexed in the manifest)
for name in clip_actions:
    bpy.data.actions[name].use_fake_user = True
skel.animation_data.action = None
select([skel, body, head], skel)
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "UP_AllClips.fbx"), use_selection=True,
                         object_types={"ARMATURE", "MESH"}, bake_anim=True, bake_anim_use_all_bones=True,
                         bake_anim_use_nla_strips=False, bake_anim_use_all_actions=True, path_mode="AUTO",
                         embed_textures=False, **FBX)
print("[export] UP_AllClips.fbx")
shutil.copy2(C.SKIN_1024, os.path.join(OUT, "T_UP_Skin_1024.png"))
shutil.copy2(C.SKIN_64, os.path.join(OUT, "T_UP_Skin_64.png"))

# ---------------------------------------------------------------- manifest
V = os.path.join(C.ROOT, "validation")
loco = json.load(open(os.path.join(V, "clip_meta_locomotion.json")))
air = json.load(open(os.path.join(V, "clip_meta_air_turns.json")))
pk = json.load(open(os.path.join(V, "s06_pickaxe.json")))
clips = []
for i, name in enumerate(clip_actions):
    key = name[3:]
    spec = C.CLIPS.get(key, {})
    act = bpy.data.actions[name]
    n = int(round(act.frame_range[1]))
    e = {"index": i, "name": name, "file": f"anims/A_{name}.fbx", "take_in_UP_AllClips": name, "frames": n,
         "keys": n + 1, "fps": C.FPS, "seconds": round(n / C.FPS, 6), "loop": bool(spec.get("loop", False)),
         "family": spec.get("family"), "maps_to_old": spec.get("old"),
         "placeholder": bool(spec.get("placeholder", False)), "root_motion": False}
    if key in loco:
        e.update({k: v for k, v in loco[key].items() if k not in ("frames", "seconds", "loop")})
    if key in air.get("turns", {}):
        e["turn"] = air["turns"][key]
    if spec.get("brief_frames"):
        e["brief_frames"] = spec["brief_frames"]
        e["duration_change"] = "brief 64 f (1.0667 s) -> 54 f (0.9 s): diagonal sneak 1.8316 m/s needs 0.98 m steps " \
                               "at 1.0667 s; crouched legs then forced the hips to 0.27-0.40 m (s03)"
    if key.startswith("Look_"):
        e["additive"] = {"type": "mesh-space rotation", "reference": "UP_Look_C frame 0"}
    if key.startswith("SneakLook_"):
        e["additive"] = {"type": "mesh-space rotation", "reference": "UP_SneakLook_C frame 0"}
    if key.startswith("Pickaxe_Strike"):
        e.update({"contact_frame": 9, "contact_seconds": .15, "layer": "upper body (mask in pickaxe.upper_body_mask)"})
    clips.append(e)

manifest = {
    "asset": "Unnamed Player v001 (SK_UnnamedPlayer)", "version": C.VERSION, "proportion_preset": C.PRESET,
    "units": {"fbx": "centimetres (UnitScaleFactor 1), import scale 1.0", "blender_master": "metres"},
    "axes": {"blender_master": "Z up, character faces -Y, character left = +X",
             "fbx": "Y up, forward -Z (Blender exporter axis_forward -Z, axis_up Y)",
             "unreal_expected": "Z up, mesh faces +Y (same as UE5 Mannequin / old Better Steve) -> keep the existing "
                                "body yaw - 90 deg mesh offset (verify on first import)",
             "bone_axes": "Blender bone axes kept (primary Y along the bone, secondary X); not Mannequin X-forward"},
    "dimensions_cm": {"height": round(C.HEIGHT * 100, 3), "soles_z": 0.0, "head_cube": round(C.HEAD * 100, 2),
                      "body_px_h_cm": round(C.H * 100, 4), "body_px_v_cm": round(C.V * 100, 4),
                      "painted_eye_row_cm": [round(z * 100, 2) for z in C.EYE_ROW],
                      "gameplay_eyes_cm": {"stand": 162, "sneak": 127, "low": 40},
                      "width_with_arms_cm": round(16 * C.H * 100, 2)},
    "skeleton": {"name": "UP_Skeleton (new skeleton; NOT compatible with SK_BetterSteve's 56-bone skeleton)",
                 "bone_count": len(rest), "order": C.BONE_ORDER, "bones": rest, "deform_only": True,
                 "hand_style": C.HAND_STYLE + (" (vanilla arm box; the end block is the hand, bent by hand_l/r; "
                                               "the pickaxe handle passes through it)" if C.HAND_STYLE == "box" else ""),
                 "rest_pose": "I-pose (arms down, palms in, thumbs forward); knees 1.2 cm forward, elbows 1.2 cm back "
                              "of straight (IK bend hint)"},
    "bone_map_old": C.OLD_BONE_MAP, "sockets": sockets,
    "meshes": {"SK_UnnamedPlayer_Body": {"material": "M_UP_Body"}, "SK_UnnamedPlayer_Head": {
        "material": "M_UP_Head", "note": "separate material section: hide it for first person; the torso top face closes "
                                         "the neck"}},
    "texture": {"file": "T_UP_Skin_1024.png (embedded)", "source_64": "T_UP_Skin_64.png", "filter": "nearest/point "
                "recommended (pixel art); sRGB; no mip blur on the 64 px version"},
    "clips": clips,
    "gaits": {"phase": "0 = left foot contact, 0.5 = right foot contact, shared by all 8 directions of a gait; walk "
                       "and sprint compatible for phase blending; sneak has its own phase",
              "directions": "F FR R BR B BL L FL = 0,45,90,135,180,-135,-90,-45 deg (+ = right)",
              "seam": "keys at frame 0 and frame N are identical; duration = N/60 s (no endpoint dwell)",
              "speeds_mps": C.SPEEDS},
    "air": air["air"], "turn_controller_proposal": air["turn_controller_proposal"],
    "look": {"grid": {"DL": [-90, -90], "D": [0, -90], "DR": [90, -90], "L": [-90, 0], "C": [0, 0], "R": [90, 0],
                      "UL": [-90, 90], "U": [0, 90], "UR": [90, 90]},
             "convention": "[yaw, pitch] degrees; yaw + = right, pitch + = UP",
             "reference": "UP_Look_C (standing), UP_SneakLook_C (sneak)",
             "why_sneak_grid": "standing grid applied as a mesh-space additive on the sneak pose pushes the head 8-10 cm "
                               "into the torso looking up (s05); the sneak grid spreads more pitch into the spine"},
    "pickaxe": pk["meta"],
    "provenance": json.loads(bpy.data.scenes[0].get("up_provenance", "{}")) if bpy.data.scenes[0].get("up_provenance") else {},
}
json.dump(manifest, open(os.path.join(OUT, "UP_manifest_v001.json"), "w"), indent=1, default=float)
print("[export] manifest", len(clips), "clips")
