"""Previews: air sequence on the simulated jump arc, edge fall, 90-degree turns with the presentation yaw applied
once, stand <-> sneak. Baked data, constraints muted, one clock (pose, travel and camera keyed per frame).

    blender -b --factory-startup --python scripts/s04b_preview_air_turns.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import up_actions as AC  # noqa: E402
import up_config as C  # noqa: E402
import up_preview as PV  # noqa: E402
import up_render as R  # noqa: E402

bpy.ops.wm.open_mainfile(filepath=C.MASTER)
scene = bpy.context.scene
pv = PV.BakedPreview(scene)
skel = pv.skel
tmp = os.path.join(C.ROOT, "previews", "_tmp", "air")
os.makedirs(tmp, exist_ok=True)
R.setup_workbench(scene, res=(640, 480))
cam = R.camera("PREVIEW_Cam_Air", bpy.data.collections["PREVIEW"], lens=40)
scene.camera = cam


def nla(seq):
    """seq: list of (action name, start frame, length, blend_in frames, repeat). Alternating tracks for crossfades."""
    ad = skel.animation_data or skel.animation_data_create()
    ad.action = None
    for t in list(ad.nla_tracks):
        ad.nla_tracks.remove(t)
    tracks = [ad.nla_tracks.new(), ad.nla_tracks.new()]
    for i, (name, start, length, blend, rep) in enumerate(seq):
        act = bpy.data.actions[name]
        st = tracks[i % 2].strips.new(f"{name}_{start}", start, act)
        st.action_slot = act.slots[0]
        n = max(1, int(round(act.frame_range[1])))
        if rep:
            st.repeat = rep
        elif length < n:
            st.action_frame_end = max(1, length)      # crop instead of overlapping the next strip
        else:
            st.repeat = length / n
        st.blend_in = blend
        st.extrapolation = "HOLD_FORWARD" if i else "HOLD"
        st.use_auto_blend = False


def travel_keys(points):
    tr = pv.travel
    tr.animation_data_clear()
    for f, loc, yaw in points:
        tr.location = loc
        tr.rotation_euler = (0, 0, math.radians(yaw))
        tr.keyframe_insert("location", frame=f)
        tr.keyframe_insert("rotation_euler", frame=f)
    for layer in tr.animation_data.action.layers:
        for s in layer.strips:
            for bag in s.channelbags:
                for fc in bag.fcurves:
                    for kp in fc.keyframe_points:
                        kp.interpolation = "LINEAR"


def render(name, frames, video=True, strip=None, target=Vector((0, 0, .9)), off=Vector((-3.4, -2.4, 1.0)),
           follow=True):
    scene.frame_start, scene.frame_end = frames
    if strip:
        PV.still_settings(scene)
        for f in strip:
            scene.frame_set(f)
            tw = pv.travel.matrix_world.translation
            t = (Vector((tw.x, tw.y, 0.0)) if follow else Vector()) + target   # follow travel in XY only: arcs stay visible
            R.look_at(cam, t + off, t)
            scene.render.filepath = os.path.join(tmp, f"{name}_{f:03d}.png")
            bpy.ops.render.render(write_still=True)
    if video:
        cam.animation_data_clear()
        for f in range(frames[0], frames[1] + 1):
            scene.frame_set(f)
            tw = pv.travel.matrix_world.translation
            t = (Vector((tw.x, tw.y, 0.0)) if follow else Vector()) + target   # follow travel in XY only: arcs stay visible
            R.look_at(cam, t + off, t)
            cam.keyframe_insert("location", frame=f)
            cam.keyframe_insert("rotation_quaternion", frame=f)
        PV.video_settings(scene, os.path.join(C.ROOT, "previews", f"{name}.mp4"))
        bpy.ops.render.render(animation=True)
        cam.animation_data_clear()


# ---------------- standing jump on flat ground: Idle -> Jump_Start (jump tick) -> Fall (vz <= 0) -> Land_Soft -> Idle
traj = AC.jump_trajectory()
t0 = 30                                   # jump tick at frame 30
pts = [(0, (0, 0, 0), 0), (t0, (0, 0, 0), 0)]
for tick, z, v in traj:
    pts.append((t0 + tick * 3, (0, 0, z / 100.0), 0))
land = t0 + traj[-1][0] * 3
apex = t0 + 5 * 3
pts.append((land + 60, (0, 0, 0), 0))
travel_keys(pts)
nla([("UP_Idle", 0, t0, 0, 0), ("UP_Jump_Start", t0, apex - t0, 3, 1), ("UP_Jump_Fall", apex, land - apex, 6, 0),
     ("UP_Land_Soft", land, 12, 2, 1), ("UP_Idle", land + 12, 60, 6, 0)])
render("air_standing_jump", (0, land + 50), strip=[t0, t0 + 6, apex, apex + 9, land, land + 4, land + 12])
print("[air] standing jump: jump frame", t0, "apex", apex, "land", land)

# ---------------- edge fall at walking speed from a 2-block ledge: Walk_F -> Fall (no Jump_Start) -> Land_Firm -> Idle
v = C.speed_of("Walk", "F")
fall = AC.fall_trajectory(200)
edge = 36
pts = [(0, (0, 0, 2.0), 0), (edge, (0, -v * edge / 60, 2.0), 0)]
for tick, z, vz in fall:
    pts.append((edge + tick * 3, (0, -v * (edge + tick * 3) / 60, 2.0 + z / 100.0), 0))
landf = edge + fall[-1][0] * 3
pts.append((landf + 40, (0, -v * landf / 60 - .4, 0.0), 0))
travel_keys(pts)
nla([("UP_Walk_F", 0, edge, 0, 0), ("UP_Jump_Fall", edge, landf - edge, 6, 0), ("UP_Land_Firm", landf, 24, 2, 1),
     ("UP_Idle", landf + 24, 40, 8, 0)])
ledge = bpy.data.objects.new("PREVIEW_Ledge", bpy.data.meshes.new("PREVIEW_Ledge"))
bm_v = [(-2, 1, 0), (2, 1, 0), (2, -v * edge / 60 + .05, 0), (-2, -v * edge / 60 + .05, 0)]
bm_v += [(x, y, 2.0) for x, y, _ in bm_v]
ledge.data.from_pydata(bm_v, [], [(0, 1, 2, 3), (4, 7, 6, 5), (3, 2, 6, 7), (0, 3, 7, 4), (1, 5, 6, 2), (0, 4, 5, 1)])
ledge.data.materials.append(bpy.data.materials["M_Prop_Stone"])
bpy.data.collections["PREVIEW"].objects.link(ledge)
render("air_edge_fall_firm_landing", (0, landf + 34), strip=[edge - 6, edge + 6, edge + 15, landf, landf + 5, landf + 16],
       off=Vector((-4.2, -1.4, .9)))
bpy.data.objects.remove(ledge, do_unlink=True)
print("[air] edge fall: drop 2 blocks, land frame", landf, "impact v cm/tick", round(fall[-1][2], 2))

# ---------------- turns: the presentation yaw is applied ONCE (travel parent), root-neutral clips
for name in ("UP_Turn_L90", "UP_Turn_R45", "UP_SneakTurn_L90"):
    act = bpy.data.actions[name]
    key = name[3:]
    n = int(round(act.frame_range[1]))
    import json
    meta = json.load(open(os.path.join(C.ROOT, "validation", "clip_meta_air_turns.json")))["turns"][key]
    tot = meta["body_yaw_total_deg"]
    pts = [(0, (0, 0, 0), 0), (10, (0, 0, 0), 0)]
    for p in meta["body_yaw_progress"]:
        pts.append((10 + p["frame"], (0, 0, 0), tot * p["progress"]))
    pts.append((10 + n + 20, (0, 0, 0), tot))
    travel_keys(pts)
    base = "UP_SneakIdle" if "Sneak" in name else "UP_Idle"
    nla([(base, 0, 10, 0, 0), (name, 10, n, 0, 1), (base, 10 + n, 20, 0, 0)])
    render("turn_" + key, (0, 10 + n + 20), strip=[10, 10 + n // 4, 10 + n // 2, 10 + 3 * n // 4, 10 + n],
           off=Vector((0.0, -4.2, 1.6)), target=Vector((0, 0, .5)))
print("[air] turns")

# ---------------- stand <-> sneak
pv.travel.animation_data_clear()
pv.travel.location = (0, 0, 0)
pv.travel.rotation_euler = (0, 0, 0)
nla([("UP_Idle", 0, 20, 0, 0), ("UP_StandToSneak", 20, 16, 0, 1), ("UP_SneakIdle", 36, 30, 0, 0),
     ("UP_SneakToStand", 66, 16, 0, 1), ("UP_Idle", 82, 20, 0, 0)])
render("transition_stand_sneak", (0, 100), strip=[20, 24, 28, 32, 36, 66, 74, 82], off=Vector((-3.6, -1.2, .7)),
       target=Vector((0, 0, .8)))
pv.restore()
print("[air] done")
