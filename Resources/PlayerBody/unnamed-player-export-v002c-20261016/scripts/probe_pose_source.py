"""Read-only: evaluate the source blend's UP_Look*/pose actions on UP_Skeleton (constraints removed in memory, never
saved) and compare joint heads/tails with the v002c round trip.
    blender -b --factory-startup --python probe_pose_source.py -- <source.blend> <roundtrip.json> <out.json>"""
import json
import sys

import bpy
from mathutils import Vector

src, rtp, outp = sys.argv[sys.argv.index("--") + 1:][:3]
bpy.ops.wm.open_mainfile(filepath=src)
rt = json.load(open(rtp))["anims"]
skel = bpy.data.objects["UP_Skeleton"]
rig = bpy.data.objects["UP_Rig"]
if rig.animation_data:
    rig.animation_data.action = None
for pb in skel.pose.bones:
    for c in list(pb.constraints):
        pb.constraints.remove(c)
skel.animation_data_create()
out = {}
for name, e in rt.items():
    if "pose0_world_m" not in e:
        continue
    act = bpy.data.actions[name]
    skel.animation_data.action = act
    skel.animation_data.action_slot = act.slots[0]
    bpy.context.scene.frame_set(0)
    bpy.context.view_layer.update()
    worst, wb = 0.0, None
    for pb in skel.pose.bones:
        r = e["pose0_world_m"][pb.name]
        d = (skel.matrix_world @ pb.head - Vector(r["head"])).length   # heads only: FBX import guesses tail lengths
        if d > worst:
            worst, wb = d, pb.name
    out[name] = {"source_vs_roundtrip_max_cm": round(worst * 100, 5), "bone": wb}
json.dump(out, open(outp, "w"), indent=1)
print("[probe]", json.dumps(out))
