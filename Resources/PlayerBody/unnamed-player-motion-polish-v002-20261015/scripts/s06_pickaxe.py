"""Stage 6: pickaxe hold + strikes A/B/C, contact/penetration validation, comparison renders.

    blender -b --factory-startup --python scripts/s06_pickaxe.py [-- --no-render]

The baked strikes carry the arm/hand motion; the tool itself is NOT a skeleton bone: at runtime it attaches to the
hand_r_pickaxe socket (fixed grip transform, manifest). Validation reproduces exactly that (tool = hand_r @ socket).
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Euler, Matrix, Vector  # noqa: E402
from mathutils.bvhtree import BVHTree  # noqa: E402

import up_anim as A  # noqa: E402
import up_config as C  # noqa: E402
import up_pickaxe as PK  # noqa: E402
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
pick = bpy.data.objects["UP_Pickaxe"]
stone = bpy.data.objects["PROP_Stone"]
stone.location = PK.STONE["center"]
stone.hide_render = stone.hide_viewport = False

RB = rig.data.bones
SOCKET = RB["c_hand_r"].matrix_local.inverted() @ RB["c_grip_r"].matrix_local   # hand_r -> grip (tool) frame
TOOL_CO = [v.co.copy() for v in pick.data.vertices]
TOOL_PARTS = list(pick["up_parts"])
tvals = np.zeros(len(pick.data.vertices), dtype=np.int64)
pick.data.attributes["up_part"].data.foreach_get("value", tvals)

# ---------------------------------------------------------------- author + bake
results, meta = {}, {}
act, tool = PK.author_hold(rig, "RIG_UP_Pickaxe_Hold")
A.bake_to_skeleton(skel, rig, act, "UP_Pickaxe_Hold", (0, 0))
tools = {}
for k in "ABC":
    act, tools[k] = PK.author_strike(rig, k, f"RIG_UP_Pickaxe_Strike{k}")
    b = A.bake_to_skeleton(skel, rig, act, f"UP_Pickaxe_Strike{k}", (0, PK.FRAMES))
    results[k] = {"unreachable_cm": round(b.get("unreachable_m", 0.0) * 100, 3)}
rig.animation_data.action = None
print("[pick] authored + baked")


# ---------------------------------------------------------------- validation on the baked data (constraints muted)
def mute(on):
    for pb in skel.pose.bones:
        for c in pb.constraints:
            c.mute = on


def tool_matrix():
    return skel.matrix_world @ skel.pose.bones["hand_r"].matrix @ SOCKET


def stone_pen(tip):
    c = Vector(PK.STONE["center"])
    h = PK.STONE["size"] / 2
    inside = all(abs(tip[i] - c[i]) < h for i in range(3))
    if not inside:
        return 0.0
    return min(h - abs(tip[i] - c[i]) for i in range(3))


def tool_body_pen(tm):
    """Max depth of tool vertices inside body parts (excluding the right hand that holds it, and the left hand
    while it supports the handle) and inside the head."""
    ps = Vd.PartSet(body)
    ph = Vd.PartSet(head)
    pts = [tm @ co for co in TOOL_CO]
    worst = {}
    near_grip = [co.length < .13 for co in TOOL_CO]      # the handle passes through the hand end by design
    for part in ("torso", "leg_l", "leg_r", "arm_l", "arm_r"):
        t = ps.tree(part)
        lo, hi = ps.bbox(part)
        w = 0.0
        for i, p in enumerate(pts):
            if part == "arm_r" and near_grip[i]:
                continue
            if (np.array(p) < lo - 1e-4).any() or (np.array(p) > hi + 1e-4).any():
                continue
            if ps.inside(part, p):
                w = max(w, t.find_nearest(p)[3])
        worst[part] = w
    t = ph.tree("head")
    lo, hi = ph.bbox("head")
    w = 0.0
    for p in pts:
        if (np.array(p) >= lo).all() and (np.array(p) <= hi).all() and ph.inside("head", p):
            w = max(w, t.find_nearest(p)[3])
    worst["head"] = w
    return worst


mute(True)
ad = skel.animation_data or skel.animation_data_create()
for k in "ABC":
    act = bpy.data.actions[f"UP_Pickaxe_Strike{k}"]
    ad.action = act
    ad.action_slot = act.slots[0]
    path, pen_max, body_pen, arm_pen, arm_pen_l, wrist, worst_frame = [], 0.0, {}, 0.0, 0.0, {}, {}
    for f in range(PK.FRAMES + 1):
        scene.frame_set(f)
        tm = tool_matrix()
        tip = tm @ PK.TIP_LOCAL
        path.append([round(c, 4) for c in tip])
        pen_max = max(pen_max, stone_pen(tip))
        for part, v in tool_body_pen(tm).items():
            if v > body_pen.get(part, 0.0):
                worst_frame[part] = f
            body_pen[part] = max(body_pen.get(part, 0.0), v)
        ps = Vd.PartSet(body)
        arm_pen_l = max(arm_pen_l, ps.penetration("arm_l", "torso"))
        arm_pen = max(arm_pen, ps.penetration("arm_r", "torso"))
        P = skel.pose.bones
        for s_ in "rl":
            rel = (P["lowerarm_" + s_].matrix.to_quaternion().inverted() @ P["hand_" + s_].matrix.to_quaternion())
            rest = (skel.data.bones["lowerarm_" + s_].matrix_local.to_quaternion().inverted() @
                    skel.data.bones["hand_" + s_].matrix_local.to_quaternion())
            dq = rest.inverted() @ rel
            ang = math.degrees(2 * math.acos(min(1.0, abs(dq.w))))
            wrist[s_] = max(wrist.get(s_, 0.0), ang)
    scene.frame_set(PK.CONTACT_FRAME)
    tip9 = tool_matrix() @ PK.TIP_LOCAL
    scene.frame_set(PK.CONTACT_FRAME - 1)
    tip8 = tool_matrix() @ PK.TIP_LOCAL
    scene.frame_set(PK.CONTACT_FRAME + 1)
    tip10 = tool_matrix() @ PK.TIP_LOCAL
    r = results[k]
    r.update({"contact_frame": PK.CONTACT_FRAME, "contact_error_cm": round((tip9 - PK.TARGET).length * 100, 3),
              "tip_speed_into_contact_m_s": round((tip9 - tip8).length * C.FPS, 2),
              "tip_speed_out_of_contact_m_s": round((tip10 - tip9).length * C.FPS, 2),
              "max_tip_penetration_into_stone_cm": round(pen_max * 100, 2),
              "tool_in_body_cm": {p: round(v * 100, 2) for p, v in body_pen.items()},
              "tool_in_body_worst_frame": worst_frame,
              "max_arm_r_in_torso_cm": round(arm_pen * 100, 2), "max_arm_l_in_torso_cm": round(arm_pen_l * 100, 2),
              "max_wrist_angle_deg": {k_: round(v_, 1) for k_, v_ in wrist.items()}, "tip_path_m": path})
    # chain check: frame 18 of every strike equals frame 0 of every strike (shared link pose)
    print("[pick]", k, json.dumps({kk: vv for kk, vv in r.items() if kk != "tip_path_m"}))
# link-pose continuity across strikes
ends = {}
for k in "ABC":
    act = bpy.data.actions[f"UP_Pickaxe_Strike{k}"]
    ad.action = act
    ad.action_slot = act.slots[0]
    for f in (0, PK.FRAMES):
        scene.frame_set(f)
        ends[(k, f)] = {pb.name: pb.matrix.copy() for pb in skel.pose.bones}
link_err = max((ends[(a, PK.FRAMES)][b].translation - ends[(c, 0)][b].translation).length
               for a in "ABC" for c in "ABC" for b in ends[(a, 0)])
results["link_pose_max_joint_mismatch_cm"] = round(link_err * 100, 4)
ad.action = None
mute(False)

grip_r = RB["c_grip_r"].matrix_local
meta = {"socket_hand_r_pickaxe": {
    "bone": "hand_r", "matrix_in_bone_space": [list(r) for r in SOCKET],
    "location_m": list(SOCKET.to_translation()), "rotation_quat_wxyz": list(SOCKET.to_quaternion()),
    "tool_frame": "+Y along the handle toward the head, +Z toward the long pick point, origin = grip centre"},
    "tip_local_m": list(PK.TIP_LOCAL), "target_m": list(PK.TARGET), "stone_center_m": list(PK.STONE["center"]),
    "contact_frame": PK.CONTACT_FRAME, "frames": PK.FRAMES, "seconds": PK.FRAMES / C.FPS,
    "gameplay_timing": "0.30 s cycle = six 20 Hz steps; contact 0.15 s = frame 9 = step 3",
    "upper_body_mask": C.UPPER_BODY, "lower_body_from_locomotion": C.LOWER_BODY,
    "support_grip": {"B": "left hand on the handle 16 cm toward the head, frames ~2-14 (IK blend)"},
    "link_pose": "frames 0 and 18 identical in A, B and C (chain any order: A->B->A->C)"}
json.dump({"results": results, "meta": meta}, open(os.path.join(C.ROOT, "validation", "s06_pickaxe.json"), "w"),
          indent=1, default=float)
stone.hide_render = stone.hide_viewport = True
bpy.ops.wm.save_as_mainfile(filepath=C.MASTER, compress=True)
print("[pick] link mismatch cm", results["link_pose_max_joint_mismatch_cm"], "saved")
if "--no-render" in ARGS:
    sys.exit(0)

# ---------------------------------------------------------------- renders
pv = PV.BakedPreview(scene)
stone.hide_render = False
pick.hide_render = False
sock = bpy.data.objects.new("PREVIEW_Socket_hand_r_pickaxe", None)
bpy.data.collections["PREVIEW"].objects.link(sock)
sock.parent = skel
sock.parent_type = "BONE"
sock.parent_bone = "hand_r"
for pb in skel.pose.bones:
    pb.location, pb.rotation_quaternion = (0, 0, 0), (1, 0, 0, 0)
skel.animation_data.action = None
bpy.context.view_layer.update()
sock.matrix_world = skel.matrix_world @ RB["c_grip_r"].matrix_local
pick.parent = sock
pick.parent_type = "OBJECT"
pick.matrix_parent_inverse = Matrix()
pick.matrix_world = sock.matrix_world.copy()
tmp = os.path.join(C.ROOT, "previews", "_tmp", "pick")
os.makedirs(tmp, exist_ok=True)
R.setup_workbench(scene, res=(480, 540))
cam = R.camera("PREVIEW_Cam_Pick", bpy.data.collections["PREVIEW"], lens=45)
scene.camera = cam
tgt = Vector((-.1, -.4, 1.15))
CAM3 = (tgt + Vector((-2.4, -1.2, .35)), tgt)


def seq(strikes, base=None, start=0):
    """NLA: optional lower-body base track (looped) + strike strips back to back (upper-body copies if layered)."""
    ad = skel.animation_data or skel.animation_data_create()
    ad.action = None
    for t in list(ad.nla_tracks):
        ad.nla_tracks.remove(t)
    if base:
        tb = ad.nla_tracks.new()
        bact = bpy.data.actions[base]
        n = int(bact.frame_range[1])
        sb = tb.strips.new(base, start, bact)
        sb.repeat = max(1, math.ceil(len(strikes) * PK.FRAMES / n) + 1)
        sb.action_slot = bact.slots[0]
    tr = ad.nla_tracks.new()
    f = start
    for k in strikes:
        a = bpy.data.actions[f"UP_Pickaxe_Strike{k}" + ("_upper" if base else "")]
        s = tr.strips.new(f"{k}_{f}", f, a)
        s.action_slot = a.slots[0]
        f += PK.FRAMES
    return f


# upper-body-only copies for layering previews
for k in "ABC":
    src = bpy.data.actions[f"UP_Pickaxe_Strike{k}"]
    cp = src.copy()
    cp.name = f"UP_Pickaxe_Strike{k}_upper"
    for layer in cp.layers:
        for st in layer.strips:
            for bag in st.channelbags:
                for fc in list(bag.fcurves):
                    bone = fc.data_path.split('"')[1]
                    if bone in C.LOWER_BODY:
                        bag.fcurves.remove(fc)

# six-pose strips
labels = {0: "start (link)", 3: "anticipation", 6: "approach", 9: "CONTACT f9", 12: "rebound", 15: "recovery"}
for k in "ABC":
    seq([k])
    for f in labels:
        scene.frame_set(f if not (k == "C" and f == 12) else 11)
        R.look_at(cam, *CAM3)
        scene.render.filepath = os.path.join(tmp, f"strip_{k}_{f:02d}.png")
        bpy.ops.render.render(write_still=True)
print("[pick] strips")
if "--strips-only" in ARGS:
    pick.parent = None
    pv.restore()
    sys.exit(0)

# A-A-A-A vs A-B-A-C, third and first person, identical timing/cameras
fpcam = R.camera("PREVIEW_Cam_FP_Pick", bpy.data.collections["PREVIEW"])
R.fov_vertical(fpcam, 70)
fpcam.data.clip_start = .02
fpcam.location = (0, 0, C.GAME["stand_eyes_m"])
fpcam.rotation_mode = "XYZ"
fpcam.rotation_euler = Euler((math.radians(90 - 28), 0, math.radians(180)), "XYZ")
for tag, order in (("AAAA", "AAAA"), ("ABAC", "ABAC")):
    end = seq(list(order))
    for view in ("3p", "1p"):
        if view == "3p":
            scene.camera = cam
            R.look_at(cam, *CAM3)
            head.hide_render = False
            scene.render.resolution_x, scene.render.resolution_y = 480, 540
        else:
            scene.camera = fpcam
            head.hide_render = True
            scene.render.resolution_x, scene.render.resolution_y = 640, 360
        d = os.path.join(tmp, f"seq_{tag}_{view}")
        os.makedirs(d, exist_ok=True)
        for f in range(0, end):
            scene.frame_set(f)
            scene.render.filepath = os.path.join(d, f"{f:03d}.png")
            bpy.ops.render.render(write_still=True)
    head.hide_render = False
print("[pick] sequences")

# strikes layered over moving / sneaking legs (in place, same camera)
scene.camera = cam
scene.render.resolution_x, scene.render.resolution_y = 480, 540
for base in ("UP_Idle", "UP_Walk_F", "UP_Sneak_F"):
    seq(list("AB"), base=base)
    for f in (3, 9, 15, 21, 27, 33):
        scene.frame_set(f)
        R.look_at(cam, *CAM3)
        scene.render.filepath = os.path.join(tmp, f"layer_{base[3:]}_{f:02d}.png")
        bpy.ops.render.render(write_still=True)
print("[pick] layered")
pick.parent = None
pv.restore()
