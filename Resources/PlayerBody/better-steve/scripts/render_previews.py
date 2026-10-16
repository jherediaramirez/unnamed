"""Render preview sheets of BetterSteve.blend (turnaround, vanilla comparison, face rig, poses, skeleton).

    blender -b foundry/BetterSteve.blend --python scripts/render_previews.py -- <project root>
"""

import bpy
import math
import os
import sys

import numpy as np
from mathutils import Vector

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ROOT = os.path.abspath(ARGS[0]) if ARGS else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ONLY = set(ARGS[1].split(",")) if len(ARGS) > 1 else None
OUT = os.path.join(ROOT, "previews")
TILES = os.path.join(OUT, "_tiles")
os.makedirs(TILES, exist_ok=True)

scene = bpy.context.scene
arm = bpy.data.objects["Armature"]
body = bpy.data.objects["SK_BetterSteve"]
vanilla = bpy.data.objects["REF_VanillaSteve"]

# ---------------------------------------------------------------- look
try:
    scene.render.engine = "BLENDER_EEVEE"
except TypeError as e:
    print(e)
scene.view_settings.view_transform = "Standard"
scene.eevee.taa_render_samples = 32
world = bpy.data.worlds.new("PreviewWorld")
scene.world = world
try:
    world.use_nodes = True
except Exception:
    pass
bg = next(n for n in world.node_tree.nodes if n.type == "BACKGROUND")
bg.inputs[0].default_value = (.60, .70, .82, 1)
bg.inputs[1].default_value = .7


def sun(name, energy, rot, color=(1, 1, 1)):
    d = bpy.data.lights.new(name, "SUN")
    d.energy, d.color, d.angle = energy, color, math.radians(3)
    o = bpy.data.objects.new(name, d)
    o.rotation_euler = [math.radians(a) for a in rot]
    scene.collection.objects.link(o)


sun("Key", 2.6, (52, 0, -28), (1, .97, .92))
sun("Rim", 1.2, (-60, 0, 150), (.85, .9, 1))

ground_mat = bpy.data.materials.new("Ground")
try:
    ground_mat.use_nodes = True
except Exception:
    pass
nt = ground_mat.node_tree
bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
chk = nt.nodes.new("ShaderNodeTexChecker")
coord = nt.nodes.new("ShaderNodeTexCoord")
chk.inputs["Scale"].default_value = 1.0
chk.inputs["Color1"].default_value = (.36, .50, .25, 1)
chk.inputs["Color2"].default_value = (.32, .45, .22, 1)
nt.links.new(coord.outputs["Object"], chk.inputs["Vector"])
nt.links.new(chk.outputs["Color"], bsdf.inputs["Base Color"])
bsdf.inputs["Roughness"].default_value = .95
gme = bpy.data.meshes.new("Ground")
gme.from_pydata([(-20, -20, 0), (20, -20, 0), (20, 20, 0), (-20, 20, 0)], [], [(0, 1, 2, 3)])
gme.materials.append(ground_mat)
ground = bpy.data.objects.new("Ground", gme)
scene.collection.objects.link(ground)

# hitbox wire (0.6 x 0.6 x 1.8 blocks), used in the comparison shot
hb_mat = bpy.data.materials.new("Hitbox")
try:
    hb_mat.use_nodes = True
except Exception:
    pass
hb = next(n for n in hb_mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
hb.inputs["Base Color"].default_value = (1, .85, .1, 1)
hb.inputs["Emission Color"].default_value = (1, .85, .1, 1)
hb.inputs["Emission Strength"].default_value = 1.5


def wire_box(name, x0, x1, y0, y1, z0, z1, r=.004):
    import bmesh
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    P = [(x, y, z) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
    E = [(0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)]
    for a, b in E:
        pa, pb = Vector(P[a]), Vector(P[b])
        c = (pa + pb) / 2
        d = pb - pa
        ext = [abs(d.x) / 2 + r, abs(d.y) / 2 + r, abs(d.z) / 2 + r]
        res = bmesh.ops.create_cube(bm, size=1.0)
        for v in res["verts"]:
            v.co = Vector((c.x + v.co.x * 2 * ext[0], c.y + v.co.y * 2 * ext[1], c.z + v.co.z * 2 * ext[2]))
    bm.to_mesh(me)
    me.materials.append(hb_mat)
    o = bpy.data.objects.new(name, me)
    scene.collection.objects.link(o)
    return o


hitbox = wire_box("Hitbox", -.3, .3, -.3, .3, 0, 1.8)
hitbox.hide_render = True
vanilla.hide_render = True
for c in bpy.data.collections:
    if c.name == "Reference":
        c.hide_render = False

# ---------------------------------------------------------------- camera / pose / sheets
cam_data = bpy.data.cameras.new("Cam")
cam = bpy.data.objects.new("Cam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
target = bpy.data.objects.new("Target", None)
scene.collection.objects.link(target)
tc = cam.constraints.new("TRACK_TO")
tc.target, tc.track_axis, tc.up_axis = target, "TRACK_NEGATIVE_Z", "UP_Y"


prop = bpy.data.objects.get("SM_StandInPickaxe")   # the stand-in pickaxe shows only in clips that hold it


def pose(action=None, frame=0):
    ad = arm.animation_data
    if prop:
        prop.hide_render = not (action and bpy.data.actions[action].get("bs_prop"))
    if action:
        act = bpy.data.actions[action]
        ad.action = act
        try:
            if ad.action_slot is None:
                ad.action_slot = act.slots[0]
        except AttributeError:
            pass
    else:
        ad.action = None
        for pb in arm.pose.bones:
            pb.location = (0, 0, 0)
            pb.rotation_euler = (0, 0, 0)
    scene.frame_set(frame)


def shoot(name, tgt, direction, dist=8.0, ortho=None, lens=85, res=(512, 768)):
    path = os.path.join(TILES, name + ".png")
    target.location = Vector(tgt)
    cam.location = Vector(tgt) + Vector(direction).normalized() * dist
    if ortho:
        cam_data.type, cam_data.ortho_scale = "ORTHO", ortho
    else:
        cam_data.type, cam_data.lens = "PERSP", lens
    cam_data.clip_end = 100
    scene.render.resolution_x, scene.render.resolution_y = res
    scene.render.resolution_percentage = 100
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    return path


def sheet(paths, cols, name, pad=6):
    imgs = []
    for p in paths:
        im = bpy.data.images.load(p)
        a = np.empty(im.size[0] * im.size[1] * 4, np.float32)
        im.pixels.foreach_get(a)
        imgs.append(a.reshape(im.size[1], im.size[0], 4)[::-1])   # top-down
        bpy.data.images.remove(im)
    h = max(i.shape[0] for i in imgs)
    w = max(i.shape[1] for i in imgs)
    rows = math.ceil(len(imgs) / cols)
    canvas = np.ones((rows * h + (rows + 1) * pad, cols * w + (cols + 1) * pad, 4), np.float32)
    canvas[..., :3] = .12
    for k, a in enumerate(imgs):
        r, c = divmod(k, cols)
        y, x = pad + r * (h + pad), pad + c * (w + pad)
        canvas[y:y + a.shape[0], x:x + a.shape[1]] = a
    out = bpy.data.images.new(name, canvas.shape[1], canvas.shape[0], alpha=False)
    out.pixels.foreach_set(canvas[::-1].ravel())
    path = os.path.join(OUT, name + ".png")
    out.filepath_raw = path
    out.file_format = "PNG"
    out.save()
    print(f"[sheet] {path}")


def want(name):
    return ONLY is None or name in ONLY


BODY_C = (0, 0, .95)

if want("hero"):
    pose("BS_Idle", 0)
    shots = [shoot("hero_front", (0, 0, .98), (.75, -1, .22), dist=5.2, lens=60, res=(768, 1024)),
             shoot("hero_back", (0, .05, 1.0), (-.8, .9, .35), dist=5.2, lens=60, res=(768, 1024))]
    sheet(shots, 2, "00_hero")

if want("turnaround"):
    pose()
    shots = [shoot("turn_front", BODY_C, (0, -1, .05), ortho=2.15),
             shoot("turn_34", BODY_C, (.8, -1, .2), ortho=2.15),
             shoot("turn_side", BODY_C, (1, 0, .05), ortho=2.15),
             shoot("turn_back", BODY_C, (-.35, 1, .12), ortho=2.15)]
    sheet(shots, 4, "01_turnaround")

if want("compare"):
    pose()
    vanilla.hide_render = False
    hitbox.hide_render = False
    p = shoot("compare", (.5, 0, .97), (.45, -1, .15), ortho=3.0, res=(1024, 768))
    vanilla.hide_render = True
    hitbox.hide_render = True
    sheet([p], 1, "02_vanilla_vs_better")

if want("face"):
    head_c, d = (0, -.12, 1.655), (.3, -1, .06)
    frames = [("neutral", 0), ("blink", 10), ("smile", 35), ("talk", 62), ("surprise", 106), ("angry", 126),
              ("sad", 146), ("look", 160)]
    shots = []
    for nm, f in frames:
        pose("BS_FaceTest", f)
        shots.append(shoot(f"face_{nm}", head_c, d, dist=1.5, lens=85, res=(420, 420)))
    sheet(shots, 4, "03_face_rig")

if want("poses"):
    shots = []
    for f in (0, 8, 16, 24):
        pose("BS_Walk", f)
        shots.append(shoot(f"walk_{f:02d}", (0, 0, .9), (1, -.15, .08), ortho=2.15))
    pose("BS_FlexTest", 20)
    shots.append(shoot("flex_crouch", (0, -.1, .75), (.9, -1, .3), ortho=2.15))
    pose("BS_FlexTest", 40)
    shots.append(shoot("flex_reach", (0, 0, 1.0), (-.6, -1, .15), ortho=2.4))
    sheet(shots, 3, "04_poses")

if want("hands"):
    pose()
    bpy.context.view_layer.update()
    rest_hand = arm.matrix_world @ arm.pose.bones["hand_l"].matrix.to_translation()
    r0 = shoot("hand_rest", rest_hand - Vector((0, 0, .03)), (1, -.7, .12), dist=.9, lens=85, res=(420, 420))
    pose("BS_FlexTest", 20)
    bpy.context.view_layer.update()
    hand = arm.matrix_world @ arm.pose.bones["hand_l"].matrix.to_translation()
    a = shoot("hand_fist", hand, (.6, -1, .5), dist=1.0, lens=85, res=(420, 420))
    pose("BS_FlexTest", 40)
    bpy.context.view_layer.update()
    knee = arm.matrix_world @ arm.pose.bones["calf_l"].matrix.to_translation()
    b = shoot("knee_bend", knee, (1, -.4, .1), dist=1.6, lens=85, res=(420, 420))
    pose("BS_FlexTest", 20)
    bpy.context.view_layer.update()
    elbow = arm.matrix_world @ arm.pose.bones["lowerarm_r"].matrix.to_translation()
    c = shoot("elbow_bend", elbow, (-1, .15, .1), dist=1.2, lens=85, res=(420, 420))
    sheet([r0, a, b, c], 4, "05_joint_closeups")

if want("rig"):
    pose()
    import bmesh
    mat = body.data.materials[0]
    p_bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    p_bsdf.inputs["Alpha"].default_value = .22
    try:
        mat.surface_render_method = "BLENDED"
    except Exception:
        mat.blend_method = "BLEND"
    cols = {"l": (.15, .45, 1, 1), "r": (1, .25, .2, 1), "c": (1, .85, .15, 1), "f": (.2, 1, .4, 1)}
    mats = {}
    for k, cv in cols.items():
        m = bpy.data.materials.new("Bone_" + k)
        try:
            m.use_nodes = True
        except Exception:
            pass
        bb = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
        bb.inputs["Base Color"].default_value = cv
        bb.inputs["Emission Color"].default_value = cv
        bb.inputs["Emission Strength"].default_value = 2.0
        mats[k] = m
    me = bpy.data.meshes.new("RigViz")
    bm = bmesh.new()
    order = list(cols)
    for pb in arm.pose.bones:
        if not pb.bone.use_deform or pb.name == "root":
            continue
        h, t = arm.matrix_world @ pb.head, arm.matrix_world @ pb.tail
        y = (t - h)
        L = y.length
        y.normalize()
        x = y.orthogonal().normalized()
        z = y.cross(x)
        w = min(.1 * L, .025)
        mid = h + y * L * .2
        vs = [bm.verts.new(h), bm.verts.new(t)] + [bm.verts.new(mid + (x * a + z * b) * w)
                                                    for a, b in ((1, 0), (0, 1), (-1, 0), (0, -1))]
        face_bone = any(pb.name.startswith(s) for s in ("eye", "brow", "lip", "jaw"))
        key = "f" if face_bone else pb.name[-1] if pb.name[-2:] in ("_l", "_r") else "c"
        mi = order.index(key)
        for i in range(4):
            a, b = vs[2 + i], vs[2 + (i + 1) % 4]
            for tri in ((vs[0], b, a), (vs[1], a, b)):
                bm.faces.new(tri).material_index = mi
    bm.to_mesh(me)
    for k in order:
        me.materials.append(mats[k])
    viz = bpy.data.objects.new("RigViz", me)
    scene.collection.objects.link(viz)
    a = shoot("rig_front", BODY_C, (0, -1, .05), ortho=2.15)
    b = shoot("rig_side", BODY_C, (1, -.25, .08), ortho=2.15)
    c = shoot("rig_face", (0, -.1, 1.62), (.35, -1, .1), dist=1.5, lens=85, res=(512, 768))
    sheet([a, b, c], 3, "06_skeleton")

if want("locomotion"):   # one sheet per family (10_<family>.png): a 6-frame strip per action, one action per row
    fams = {}
    for act in sorted((a for a in bpy.data.actions if "bs_family" in a), key=lambda a: a.name):
        if act["bs_family"] in ("Look", "Action", "Swim"):   # framed by their own sheets below
            continue
        f0, f1 = (int(f) for f in act.frame_range)
        span = (f1 - f0) if act.get("bs_loop") else (f1 - f0) * 6 / 5   # loops: the last frame repeats the first
        tiles = []
        for i in range(6):
            f = f0 + round(span * i / 6)
            pose(act.name, f)
            tiles.append(shoot(f"{act.name}_{f:02d}", (0, 0, .92), (1, -.75, .18), ortho=2.4, res=(200, 300)))
        fams.setdefault(act["bs_family"], []).extend(tiles)
    for fam, tiles in fams.items():
        sheet(tiles, 6, f"10_{fam}")

SIDE_R, FRONT_R = (-1, 0, .05), (-.8, -1, .25)   # his right side (the pick hand), front-right three-quarter


def strip(name, frames, views, tgt, ortho, res):
    tiles = []
    for tag, d in views:
        for f in frames:
            pose(name, f)
            tiles.append(shoot(f"{tag}_{name}_{f:02d}", tgt, d, ortho=ortho, res=res))
    return tiles


if want("actions"):   # 12_actions.png: one row per view per clip, every frame of the swings
    tiles, cols = [], 9
    for name in ("BS_Mine_Swing", "BS_Mine_Swing_Up", "BS_Mine_Swing_Down"):
        tiles += strip(name, range(9), (("side", SIDE_R), ("q", FRONT_R)), (0, -.25, 1.15), 2.5, (180, 260))
    tiles += strip("BS_Place", range(6), (("side", SIDE_R),), (0, -.25, 1.15), 2.5, (180, 260))
    tiles += strip("BS_Hold_Pickaxe", (0, 20, 40), (("q", FRONT_R),), (0, -.25, 1.15), 2.5, (180, 260))
    sheet(tiles, cols, "12_actions")

if want("swim"):   # 13_swim.png: 8 frames per row
    tiles = strip("BS_Swim_Tread", range(0, 48, 6), (("q", (-.6, -1, .2)),), (0, 0, .95), 2.3, (180, 240))
    tiles += strip("BS_Swim_F", [round(30 * i / 8) for i in range(8)], (("side", (-1, 0, .1)), ("q", (-.7, -1, .35))),
                   (0, -.25, .95), 2.4, (180, 240))
    tiles += strip("BS_Swim_Sprint", [20 + round(20 * i / 8) for i in range(8)],
                   (("side", (-1, 0, .12)), ("q", (-.6, -.8, .7))), (0, -.15, .3), 2.6, (180, 240))
    sheet(tiles, 8, "13_swim")

if want("look"):   # 11_look.png: laid out like the Aim Offset grid (pitch +90 on top, yaw -90 to +90 left to right);
    # seen from the front, so his right (R) looks toward the image's left
    tiles = []
    for tag in ("UL", "U", "UR", "L", "C", "R", "DL", "D", "DR"):
        pose(f"BS_Look_{tag}", 0)
        tiles.append(shoot(f"look_{tag}", (0, 0, 1.38), (.25, -1, .12), ortho=1.25, res=(260, 260)))
    sheet(tiles, 3, "11_look")
