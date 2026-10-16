"""Stage 1-2: build the editable master (character, skeleton, weights, control rig, reference + guides).

    blender -b --factory-startup --python scripts/s01_build_master.py -- [--preset B_head40] [--no-save] [--out path]

Writes blend/UP_Player_master_v001.blend and validation/s01_build.json, previews/s01_*.png.
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if "--preset" in ARGS:
    os.environ["UP_PRESET"] = ARGS[ARGS.index("--preset") + 1]

import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import up_build as B  # noqa: E402
import up_config as C  # noqa: E402
import up_render as R  # noqa: E402

OUT = ARGS[ARGS.index("--out") + 1] if "--out" in ARGS else C.MASTER
TAG = ARGS[ARGS.index("--tag") + 1] if "--tag" in ARGS else "s01"

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.name = "UP_Master"
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 1.0
scene.unit_settings.length_unit = "METERS"
scene.render.fps = C.FPS
scene.render.fps_base = 1.0
scene.frame_start, scene.frame_end = 0, 120

# collections
col_export = B.ensure_collection("UP_EXPORT")
col_rig = B.ensure_collection("UP_RIG")
col_wgt = B.ensure_collection("UP_WGT", hide_render=True)
col_props = B.ensure_collection("UP_PROPS")
col_ref = B.ensure_collection("REF_SOURCE_normalised")
col_guides = B.ensure_collection("GUIDES")
col_prev = B.ensure_collection("PREVIEW")

mat_body = B.textured_material("M_UP_Body", C.SKIN_1024)
mat_head = B.textured_material("M_UP_Head", C.SKIN_1024)
body, head = B.build_character(col_export, mat_body, mat_head)
skel = B.build_skeleton(col_export)
B.bind([body, head], skel)
rig = B.build_rig(col_rig, col_wgt, skel)
B.build_reference(col_ref, col_guides)
pick, stone = B.build_props(col_props, rig)
fl = B.floor(col_prev)
lc = B.find_layer_collection(bpy.context.view_layer.layer_collection, "UP_WGT")
if lc:
    lc.exclude = True

# provenance on the master
scene["up_provenance"] = json.dumps({
    "source": "minecraft_steve_rigged.glb (Sketchfab 'Minecraft Steve (Rigged)' by Bully Magraptor, CC-BY-4.0, "
              "https://sketchfab.com/3d-models/minecraft-steve-rigged-fbe1d1fab86d47b09171608103af3b2d)",
    "source_sha256": "88E80A7E1D993092C4143C62EF71505F22B292AECED58A1139368FF3817CA961",
    "modifications": "rebuilt boxes on the source pixel grid and UV layout; proportions preset " + C.PRESET +
                     "; added blocky palms, grouped two-segment fingers, two-segment thumbs, toe caps; new 30-bone "
                     "skeleton, procedural weights, separate control rig; derived lossless 64/1024 px skin",
})
scene["up_preset"] = C.PRESET

# ------------------------------------------------------------------ measurements at rest
bpy.context.view_layer.update()
dg = bpy.context.evaluated_depsgraph_get()


def world_bounds(objs):
    pts = []
    for o in objs:
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        pts += [ev.matrix_world @ v.co for v in me.vertices]
        ev.to_mesh_clear()
    return [min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]


mn, mx = world_bounds([body, head])
ref_objs = [o for o in col_ref.all_objects if o.type == "MESH" and o.modifiers]
rmn, rmx = world_bounds(ref_objs)
weights_ok = True
max_inf = 0
bad = []
for ob in (body, head):
    names = {g.index: g.name for g in ob.vertex_groups}
    for v in ob.data.vertices:
        ws = [g.weight for g in v.groups if g.weight > 1e-4]
        max_inf = max(max_inf, len(ws))
        if abs(sum(ws) - 1) > 1e-3:
            weights_ok = False
            bad.append((ob.name, v.index, sum(ws)))
report = {
    "preset": C.PRESET, "proportions": C.P, "height_m": mx[2] - mn[2], "soles_z": mn[2], "top_z": mx[2],
    "bounds_min": mn, "bounds_max": mx, "width_x": mx[0] - mn[0], "depth_y": mx[1] - mn[1],
    "reference_bounds": {"min": rmn, "max": rmx, "height": rmx[2] - rmn[2]},
    "painted_eye_row_m": C.EYE_ROW, "painted_eye_centre_m": sum(C.EYE_ROW) / 2,
    "eye_guide_m": C.GAME["stand_eyes_m"], "eye_guide_minus_eye_centre_cm": (C.GAME["stand_eyes_m"] - sum(C.EYE_ROW) / 2) * 100,
    "uniform_0p225_eye_centre_m": 1.8 - 4.5 * (1.8 / 32),
    "verts": {"body": len(body.data.vertices), "head": len(head.data.vertices)},
    "faces": {"body": len(body.data.polygons), "head": len(head.data.polygons)},
    "bones": len(skel.data.bones), "bone_names": [b.name for b in skel.data.bones],
    "rig_bones": len(rig.data.bones), "weights_normalised": weights_ok, "max_influences": max_inf,
    "bad_weights_sample": bad[:5],
    "joints": {n: [round(c, 4) for c in skel.data.bones[n].head_local] for n in
               ("pelvis", "thigh_l", "calf_l", "foot_l", "ball_l", "upperarm_l", "lowerarm_l", "hand_l", "head")},
}
os.makedirs(os.path.join(C.ROOT, "validation"), exist_ok=True)
json.dump(report, open(os.path.join(C.ROOT, "validation", f"{TAG}_build.json"), "w"), indent=1, default=float)
print("[build]", json.dumps({k: report[k] for k in ("preset", "height_m", "soles_z", "width_x", "depth_y",
                                                    "painted_eye_centre_m", "eye_guide_minus_eye_centre_cm",
                                                    "bones", "weights_normalised", "max_influences")}, default=float))

# ------------------------------------------------------------------ previews (front/side/back with guides)
R.setup_workbench(scene, res=(1600, 1000))
cam = R.camera("PREVIEW_Cam_Ortho", col_prev, ortho=4.4)
R.render_view(scene, cam, "front", (0.0, 0, 1.0), 8.0, os.path.join(C.ROOT, "previews", f"{TAG}_front_compare.png"), ortho=4.4)
scene.render.resolution_x, scene.render.resolution_y = 900, 1100
R.set_visible(["REF_SOURCE_normalised"], False)
cube = bpy.data.objects["GUIDE_Cube_1m"]
for o in (cube, bpy.data.objects["GUIDE_CubeLabel"]):
    o.hide_render = True
for view in ("front", "right", "back", "front34"):
    R.render_view(scene, cam, view, (0.0, 0, .95), 8.0,
                  os.path.join(C.ROOT, "previews", f"{TAG}_{view}.png"), ortho=2.25)
for o in (cube, bpy.data.objects["GUIDE_CubeLabel"]):
    o.hide_render = False
R.set_visible(["REF_SOURCE_normalised"], True)
# hand + foot close-ups
camc = R.camera("PREVIEW_Cam_Close", col_prev, lens=85)
R.set_visible(["REF_SOURCE_normalised", "GUIDES"], False)
for name, tgt, off in (("hand_l_front", (C.J["wrist"][0], 0, .79), (.0, -.75, .05)),
                       ("hand_l_outer", (C.J["wrist"][0], 0, .79), (.75, -.05, .05)),
                       ("hand_l_inner_below", (C.J["wrist"][0], 0, .79), (-.25, -.35, -.45)),
                       ("feet", (0, -.03, .08), (.75, -.95, .4)), ("feet_side", (0.1, -.0, .08), (.95, -.05, .12))):
    R.look_at(camc, Vector(tgt) + Vector(off), tgt)
    scene.camera = camc
    scene.render.resolution_x, scene.render.resolution_y = 700, 700
    scene.render.filepath = os.path.join(C.ROOT, "previews", f"{TAG}_close_{name}.png")
    bpy.ops.render.render(write_still=True)
R.set_visible(["REF_SOURCE_normalised", "GUIDES"], True)

if "--no-save" not in ARGS:
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print("[build] saved", OUT)
