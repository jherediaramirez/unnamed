"""Stage 4 previews for baked ground locomotion (constraints muted: what you see is the exported data).

    blender -b --factory-startup --python scripts/s03b_preview_locomotion.py -- [--sheets] [--videos]

--sheets: per family, 8 directions x 4 phases (left contact, 0.25, right contact, 0.75), fixed 3/4 front camera.
--videos: travelling MP4s on the 1 m grid at the authored speed (camera rides the travel parent = same timestamp).
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import up_config as C  # noqa: E402
import up_preview as PV  # noqa: E402
import up_render as R  # noqa: E402

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
bpy.ops.wm.open_mainfile(filepath=C.MASTER)
scene = bpy.context.scene
meta = json.load(open(os.path.join(C.ROOT, "validation", "clip_meta_locomotion.json")))
pv = PV.BakedPreview(scene)
R.setup_workbench(scene, res=(320, 400))
out_dir = os.path.join(C.ROOT, "previews", "_tmp", "loco")
os.makedirs(out_dir, exist_ok=True)
cam = R.camera("PREVIEW_Cam_Loco", bpy.data.collections["PREVIEW"], lens=50)
scene.camera = cam

if "--sheets" in ARGS:
    PV.still_settings(scene)
    for fam in ARGS[ARGS.index("--sheets") + 1].split(",") if len(ARGS) > ARGS.index("--sheets") + 1 and \
            not ARGS[ARGS.index("--sheets") + 1].startswith("--") else ("Walk", "Sprint", "Sneak"):
        for d in C.DIRS:
            name = f"{fam}_{d}"
            act = bpy.data.actions["UP_" + name]
            n = meta[name]["frames"]
            pv.play(act)
            tgt = Vector((0, 0, .8))
            R.look_at(cam, tgt + Vector((-2.4, -3.6, 1.2)), tgt)
            for k, ph in enumerate((0.0, .25, .5, .75)):
                scene.frame_set(int(round(ph * n)))
                scene.render.filepath = os.path.join(out_dir, f"{name}_{k}.png")
                bpy.ops.render.render(write_still=True)
        print("[prev] sheet frames", fam)

if "--videos" in ARGS:
    R.setup_workbench(scene, res=(960, 540))
    cam.parent = pv.travel
    vids = ARGS[ARGS.index("--videos") + 1].split(",")
    for name in vids:
        m = meta[name]
        act = bpy.data.actions["UP_" + name]
        loops = 3 if m["frames"] < 60 else 2
        end = pv.play(act, velocity=m.get("travel_vector_char_space", (0, 0)), loops=loops)
        scene.frame_start, scene.frame_end = 0, end - 1
        # side-on camera relative to travel, riding the travel parent
        tv = Vector((*m.get("travel_vector_char_space", (0, -1)), 0)).normalized() if m.get("speed_mps") else Vector((0, -1, 0))
        side = Vector((-tv.y, tv.x, 0))
        cam.location = side * 4.2 + Vector((0, 0, 1.25)) - tv * .4
        R.look_at(cam, cam.location, Vector((0, 0, .75)))
        PV.video_settings(scene, os.path.join(C.ROOT, "previews", "loco_" + name + ".mp4"))
        bpy.ops.render.render(animation=True)
        print("[prev] video", name)
    cam.parent = None
pv.restore()
