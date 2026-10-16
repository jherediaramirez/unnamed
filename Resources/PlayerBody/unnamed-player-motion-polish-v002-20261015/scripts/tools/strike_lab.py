"""Strike polish lab: author + bake the strikes in memory (master NOT saved) and print per-frame clearance.

    blender -b --factory-startup --python scripts/tools/strike_lab.py [-- A,B,C]

Per frame: depth of tool vertices inside head / torso / right & left arm boxes, which tool part (handle/head) and where
(relative to the character's head centre), right-wrist angle, tip position. Arm boxes into the torso too.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Vector  # noqa: E402

import up_anim as A  # noqa: E402
import up_config as C  # noqa: E402
import up_pickaxe as PK  # noqa: E402
import up_validate as Vd  # noqa: E402

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
WHICH = ARGS[0].split(",") if ARGS else list("ABC")
bpy.ops.wm.open_mainfile(filepath=C.MASTER)
scene = bpy.context.scene
rig = bpy.data.objects["UP_Rig"]
skel = bpy.data.objects["UP_Skeleton"]
body = bpy.data.objects["UP_Body"]
head = bpy.data.objects["UP_Head"]
pick = bpy.data.objects["UP_Pickaxe"]
RB = rig.data.bones
SOCKET = RB["c_hand_r"].matrix_local.inverted() @ RB["c_grip_r"].matrix_local
TOOL_CO = [v.co.copy() for v in pick.data.vertices]
names = list(pick["up_parts"])
tv = np.zeros(len(TOOL_CO), dtype=np.int64)
pick.data.attributes["up_part"].data.foreach_get("value", tv)
TPART = [names[i] for i in tv]
out = {}
for k in WHICH:
    act, _ = PK.author_strike(rig, k, f"LAB_{k}")
    baked = A.bake_to_skeleton(skel, rig, act, f"LAB_{k}_b", (0, PK.FRAMES))
    for pb in skel.pose.bones:
        for c in pb.constraints:
            c.mute = True
    ad = skel.animation_data
    ad.action = baked
    ad.action_slot = baked.slots[0]
    rows = []
    for f in range(PK.FRAMES + 1):
        scene.frame_set(f)
        tm = skel.matrix_world @ skel.pose.bones["hand_r"].matrix @ SOCKET
        pts = [tm @ co for co in TOOL_CO]
        ps = Vd.PartSet(body)
        ph = Vd.PartSet(head)
        hc = skel.matrix_world @ skel.pose.bones["head"].matrix @ (skel.data.bones["head"].matrix_local.inverted() @
                                                                  Vector((0, 0, C.TORSO_TOP + C.HEAD / 2)))
        row = {"f": f}
        for part, pset in (("head", ph), ("torso", ps), ("arm_r", ps), ("arm_l", ps)):
            lo, hi = pset.bbox(part)
            t = pset.tree(part)
            best = (0.0, None, None)
            for i, p in enumerate(pts):
                if (np.array(p) < lo).any() or (np.array(p) > hi).any():
                    continue
                if pset.inside(part, p):
                    d = t.find_nearest(p)[3]
                    if d > best[0]:
                        best = (d, TPART[i], p)
            if best[0] > .002:
                rel = best[2] - hc
                row[part] = [round(best[0] * 100, 1), best[1], [round(c * 100) for c in rel]]
        row["armR_torso"] = round(ps.penetration("arm_r", "torso") * 100, 1)
        row["armL_torso"] = round(ps.penetration("arm_l", "torso") * 100, 1)
        P = skel.pose.bones
        dq = (skel.data.bones["lowerarm_r"].matrix_local.to_quaternion().inverted() @
              skel.data.bones["hand_r"].matrix_local.to_quaternion()).inverted() @ \
            (P["lowerarm_r"].matrix.to_quaternion().inverted() @ P["hand_r"].matrix.to_quaternion())
        row["wrist"] = round(math.degrees(2 * math.acos(min(1.0, abs(dq.w)))), 1)
        row["hand"] = [round(c * 100) for c in (skel.matrix_world @ P["hand_r"].head)]
        rows.append(row)
        print("[lab]", k, json.dumps(row))
    for pb in skel.pose.bones:
        for c in pb.constraints:
            c.mute = False
    ad.action = None
    out[k] = rows
json.dump(out, open(os.path.join(C.ROOT, "validation", "strike_lab.json"), "w"), indent=1)
