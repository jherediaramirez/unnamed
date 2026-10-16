"""Build a review .blend: one timeline reel of the BAKED clips (constraints muted) with travel, jump arc, turn yaw,
look poses and pickaxe strikes; markers name every segment. Separate file; the master is not modified.

    blender -b --factory-startup --python scripts/s09_review_scene.py
Output: blend/UP_Player_review_v001.blend  (open it, press Space to play; Numpad 0 = follow camera)
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import up_actions as AC  # noqa: E402
import up_config as C  # noqa: E402
import up_pickaxe as PK  # noqa: E402

bpy.ops.wm.open_mainfile(filepath=C.MASTER)
scene = bpy.context.scene
scene.name = "UP_Review"
skel = bpy.data.objects["UP_Skeleton"]
rig = bpy.data.objects["UP_Rig"]
pick = bpy.data.objects["UP_Pickaxe"]
stone = bpy.data.objects["PROP_Stone"]
for pb in skel.pose.bones:
    for c in pb.constraints:
        c.mute = True
    pb.location, pb.rotation_quaternion = (0, 0, 0), (1, 0, 0, 0)
rig.hide_viewport = rig.hide_render = True
for n in ("REF_SOURCE_normalised", "GUIDES", "UP_WGT"):
    c = bpy.data.collections.get(n)
    if c:
        c.hide_viewport = True
        c.hide_render = True
meta = json.load(open(os.path.join(C.ROOT, "validation", "clip_meta_locomotion.json")))
turns = json.load(open(os.path.join(C.ROOT, "validation", "clip_meta_air_turns.json")))["turns"]

travel = bpy.data.objects.new("REVIEW_Travel (not exported)", None)
bpy.data.collections["PREVIEW"].objects.link(travel)
skel.parent = travel
# pickaxe rides the hand_r socket (same transform the manifest gives Unreal)
RB = rig.data.bones
sock = bpy.data.objects.new("REVIEW_Socket_hand_r_pickaxe", None)
bpy.data.collections["PREVIEW"].objects.link(sock)
sock.parent = skel
sock.parent_type = "BONE"
sock.parent_bone = "hand_r"
bpy.context.view_layer.update()
sock.matrix_world = skel.matrix_world @ RB["c_grip_r"].matrix_local
pick.parent = sock
pick.parent_type = "OBJECT"
pick.matrix_parent_inverse = Matrix()
pick.matrix_world = sock.matrix_world.copy()
stone.location = PK.STONE["center"]

ad = skel.animation_data or skel.animation_data_create()
ad.action = None
for t in list(ad.nla_tracks):
    ad.nla_tracks.remove(t)
tracks = [ad.nla_tracks.new(), ad.nla_tracks.new()]
for i, t in enumerate(tracks):
    t.name = f"Reel_{i}"
state = {"f": 0, "i": 0, "pos": Vector((0, 0, 0)), "yaw": 0.0}
segs = []           # (start frame, length, velocity, z curve, yaw curve)
vis = []            # (frame, pickaxe/stone visible)


def add(name, frames, label=None, velocity=(0.0, 0.0), blend=4, z_curve=None, yaw_curve=None, show_pick=False):
    act = bpy.data.actions["UP_" + name]
    n = max(1, int(round(act.frame_range[1])))
    f0 = state["f"]
    st = tracks[state["i"] % 2].strips.new(f"{name}_{f0}", f0, act)
    st.action_slot = act.slots[0]
    if frames <= n:
        st.action_frame_end = max(1, frames)
    else:
        st.repeat = frames / n
    st.blend_in = min(blend, frames // 3)
    st.extrapolation = "HOLD_FORWARD"
    scene.timeline_markers.new(label or name, frame=f0)
    # travel per segment: linear at the clip speed from the origin, or along a z (jump) / yaw (turn) curve
    segs.append((f0, frames, Vector((velocity[0], velocity[1], 0.0)), z_curve, yaw_curve))
    vis.append((f0, show_pick))
    state["f"] = f0 + frames
    state["i"] += 1


add("Idle", 120)
for fam in ("Walk", "Sprint"):
    for d in C.DIRS:
        m = meta[f"{fam}_{d}"]
        add(f"{fam}_{d}", m["frames"] * 3, velocity=m["travel_vector_char_space"])
add("StandToSneak", 16)
add("SneakIdle", 60)
for d in C.DIRS:
    m = meta[f"Sneak_{d}"]
    add(f"Sneak_{d}", m["frames"] * 2, velocity=m["travel_vector_char_space"])
add("SneakToStand", 16)
add("Idle", 30, label="Idle (before jump)")
traj = AC.jump_trajectory()
zc = [(tick * 3, z / 100.0, 0.0) for tick, z, v in traj]
add("Jump_Start", 15, label="Jump_Start (on the 20 Hz jump arc)", z_curve=zc)
add("Jump_Fall", traj[-1][0] * 3 - 15, label="Jump_Fall", z_curve=[(f - 15, z, y) for f, z, y in zc])
add("Land_Soft", 12)
add("Idle", 40, label="Idle")
for name in ("Turn_L45", "Turn_L90", "Turn_R45", "Turn_R90", "SneakTurn_L90", "SneakTurn_R90"):
    tm = turns[name]
    yc = [(p["frame"], 0.0, tm["body_yaw_total_deg"] * p["progress"]) for p in tm["body_yaw_progress"]]
    add(name, tm["frames"], label=f"{name} (body yaw from metadata)", yaw_curve=yc)
    add("Idle" if "Sneak" not in name else "SneakIdle", 24, label="settle")
for k in ("C", "L", "R", "U", "D", "UL", "UR", "DL", "DR"):
    add(f"Look_{k}", 24, blend=6)
add("Pickaxe_Hold", 40, show_pick=True)
for k in "ABACABAC":
    add(f"Pickaxe_Strike{k}", 18, label=f"Strike {k}", blend=0, show_pick=True)
add("Pickaxe_Hold", 30, show_pick=True)
for name, n in (("Low_Pose", 60), ("Flight_Placeholder", 120), ("Death_Placeholder", 60)):
    add(name, n, label=name + " (placeholder)")
end = state["f"]

# travel keys: every frame keyed (segments restart at the origin; one clock with the pose)
def interp(curve, t):
    pts = sorted(curve)
    if t <= pts[0][0]:
        return pts[0]
    for a, b in zip(pts, pts[1:]):
        if a[0] <= t <= b[0]:
            w = (t - a[0]) / max(1e-9, b[0] - a[0])
            return (t, a[1] + (b[1] - a[1]) * w, a[2] + (b[2] - a[2]) * w)
    return pts[-1]


travel.animation_data_clear()
for f0, n, vel, zc_, yc_ in segs:
    for t in range(n):
        if zc_:
            _, z, _ = interp(zc_, t)
            loc, yaw = Vector((0, 0, z)), 0.0
        elif yc_:
            _, _, yaw = interp(yc_, t)
            loc = Vector()
        else:
            loc, yaw = vel * (t / C.FPS), 0.0
        travel.location = loc
        travel.rotation_euler = (0, 0, math.radians(yaw))
        travel.keyframe_insert("location", frame=f0 + t)
        travel.keyframe_insert("rotation_euler", frame=f0 + t)
for o, on_key in ((pick, True), (stone, True)):
    o.hide_render = False
    for f, show in vis:
        o.hide_viewport = not show
        o.keyframe_insert("hide_viewport", frame=f)
        o.hide_render = not show
        o.keyframe_insert("hide_render", frame=f)

# follow camera riding the travel parent (same clock as the pose), side-front view
cam_data = bpy.data.cameras.new("REVIEW_FollowCam")
cam = bpy.data.objects.new("REVIEW_FollowCam", cam_data)
bpy.data.collections["PREVIEW"].objects.link(cam)
cam.parent = travel
cam.location = (-3.0, -3.4, 1.6)
d = Vector((0, 0, .9)) - Vector(cam.location)
cam.rotation_mode = "QUATERNION"
cam.rotation_quaternion = d.to_track_quat("-Z", "Y")
cam_data.lens = 35
scene.camera = cam
scene.frame_start, scene.frame_end = 0, end
scene.frame_current = 0
scene.render.fps = C.FPS
try:
    scene.sync_mode = "FRAME_DROP"
except Exception:
    pass
# viewport: textured solid shading, shadows, look through the follow camera
for scr in bpy.data.screens:
    for area in scr.areas:
        if area.type == "VIEW_3D":
            for sp in area.spaces:
                if sp.type == "VIEW_3D":
                    sp.shading.type = "SOLID"
                    sp.shading.color_type = "TEXTURE"
                    sp.shading.light = "STUDIO"
                    sp.shading.show_shadows = True
                    sp.overlay.show_relationship_lines = False
                    sp.overlay.show_bones = False
                    sp.region_3d.view_perspective = "CAMERA"
out = os.path.join(C.ROOT, "blend", "UP_Player_review_v001.blend")
bpy.ops.wm.save_as_mainfile(filepath=out, compress=True)
print(f"[review] saved {out}; frames 0..{end} ({end / C.FPS:.1f} s); markers {len(scene.timeline_markers)}")
