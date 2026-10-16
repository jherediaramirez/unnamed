"""Hand style comparison renders (in memory, nothing saved). Run once per style:

    set UP_HANDS=box|pixel ; blender -b --factory-startup --python scripts/s10_hand_compare.py -- --tag box

Views: relaxed hand close-up, relaxed full body, gripping close-up (pickaxe), gripping at game distance, first person.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
TAG = ARGS[ARGS.index("--tag") + 1] if "--tag" in ARGS else "x"

import bpy  # noqa: E402
from mathutils import Euler, Vector  # noqa: E402

import up_anim as A  # noqa: E402
import up_build as B  # noqa: E402
import up_config as C  # noqa: E402
import up_loco as L  # noqa: E402
import up_render as R  # noqa: E402

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = C.FPS
col_export = B.ensure_collection("UP_EXPORT")
col_rig = B.ensure_collection("UP_RIG")
col_wgt = B.ensure_collection("UP_WGT", hide_render=True)
col_props = B.ensure_collection("UP_PROPS")
col_prev = B.ensure_collection("PREVIEW")
body, head = B.build_character(col_export, B.textured_material("M_UP_Body", C.SKIN_1024),
                               B.textured_material("M_UP_Head", C.SKIN_1024))
skel = B.build_skeleton(col_export)
B.bind([body, head], skel)
rig = B.build_rig(col_rig, col_wgt, skel)
pick, stone = B.build_props(col_props, rig)
B.floor(col_prev)
rig.hide_render = True
for o in col_wgt.objects:
    o.hide_render = True
R.setup_workbench(scene, res=(520, 520))
cam = R.camera("CMP_Cam", col_prev, lens=60)
scene.camera = cam
out = os.path.join(C.ROOT, "previews", "_tmp", "hands")
os.makedirs(out, exist_ok=True)
has_fingers = "c_fingers_01_r" in rig.pose.bones


def curl(p, deg1, deg2):
    if not has_fingers:
        return
    for s in "lr":
        c = (lambda d: ("Y", d)) if s == "l" else (lambda d: ("Y", -d))
        p.r("c_fingers_01_" + s, c(deg1)).r("c_fingers_02_" + s, c(deg2))


def shot(name, tgt, off, lens=60, res=(520, 520)):
    cam.data.lens = lens
    R.look_at(cam, Vector(tgt) + Vector(off), Vector(tgt))
    scene.camera = cam
    scene.render.resolution_x, scene.render.resolution_y = res
    scene.render.filepath = os.path.join(out, f"{TAG}_{name}.png")
    bpy.ops.render.render(write_still=True)


def pose(p, **sw):
    A.reset_pose(rig)
    A.set_switches(rig, **sw)
    p.apply(reset=True)
    bpy.context.view_layer.update()


def hand_pos():
    return skel.matrix_world @ skel.pose.bones["hand_r"].head


# relaxed (idle): fingers straight = vanilla look; pixel style also gets a loosely curled variant
p = L.base_idle(A.Pose(rig), rig, 0.0, 0.0, breathe=0.0)
curl(p, 0, 0)
pose(p)
pick.hide_render = True
h = hand_pos()
shot("relaxed_close", h + Vector((0, 0, -.06)), (-.55, -.75, .1), lens=70)
shot("relaxed_body", (0, 0, .95), (-1.9, -3.4, .6), lens=50, res=(420, 620))
if has_fingers:
    p = L.base_idle(A.Pose(rig), rig, 0.0, 0.0, breathe=0.0)
    curl(p, 50, 60)
    pose(p)
    shot("relaxed_curled_close", hand_pos() + Vector((0, 0, -.06)), (-.55, -.75, .1), lens=70)

# gripping the pickaxe (arm forward, pick upright) - close-up and game distance
p = L.base_idle(A.Pose(rig), rig, 0.0, 0.0, breathe=0.0)
p.r("c_upperarm_r", ("Z", -8), ("Y", 12), ("X", -48)).r("c_lowerarm_r", ("X", -48)).r("c_hand_r", ("X", -4))
curl(p, 90, 90)
pose(p, pickaxe_in_hand=1)
pick.hide_render = False
h = hand_pos()
shot("grip_close", h + Vector((0, -.04, -.04)), (-.5, -.7, .12), lens=70)
shot("grip_close_inner", h + Vector((0, -.04, -.04)), (.55, -.55, .05), lens=70)
shot("grip_game", (0, -.1, 1.0), (-2.6, -3.0, .9), lens=45, res=(420, 520))

# first person: camera fixed at the standing eye, pitch -40, vertical FOV 70, head hidden, strike link pose
p = L.base_idle(A.Pose(rig), rig, 0.0, 0.0, breathe=0.0)
p.r("c_upperarm_r", ("Z", -12), ("Y", 18), ("X", -40)).r("c_lowerarm_r", ("X", -66))
curl(p, 90, 90)
pose(p, pickaxe_in_hand=1)
head.hide_render = True
fp = R.camera("CMP_FP", col_prev)
R.fov_vertical(fp, 70)
fp.data.clip_start = .02
fp.location = (0, 0, C.GAME["stand_eyes_m"])
fp.rotation_mode = "XYZ"
fp.rotation_euler = Euler((math.radians(90 - 40), 0, math.radians(180)), "XYZ")
scene.camera = fp
scene.render.resolution_x, scene.render.resolution_y = 640, 360
scene.render.filepath = os.path.join(out, f"{TAG}_first_person.png")
bpy.ops.render.render(write_still=True)
print("[hands]", TAG, "fingers" if has_fingers else "no fingers", "bones", len(skel.data.bones))
