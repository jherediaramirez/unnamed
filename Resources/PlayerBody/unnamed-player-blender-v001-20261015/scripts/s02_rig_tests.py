"""Stage 3: rig validation before authoring clips.

    blender -b --factory-startup --python scripts/s02_rig_tests.py

Poses the CONTROL RIG (FK and IK paths), evaluates the deformed meshes and measures per test pose:
edge/area ratios per part (collapse, candy-wrapper, UV stretch), junction gaps (detached pieces) and penetration
between parts that should not intersect (plus grip contact with the pickaxe handle). Renders a labelled sheet.
Writes validation/s02_rig_tests.json and previews/s02_*.png. Does not save the master.
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import up_anim as A  # noqa: E402
import up_config as C  # noqa: E402
import up_render as R  # noqa: E402
import up_validate as Vd  # noqa: E402

bpy.ops.wm.open_mainfile(filepath=C.MASTER)
scene = bpy.context.scene
rig = bpy.data.objects["UP_Rig"]
skel = bpy.data.objects["UP_Skeleton"]
body = bpy.data.objects["UP_Body"]
head = bpy.data.objects["UP_Head"]
pick = bpy.data.objects["UP_Pickaxe"]


def curl(side, deg):
    return ("Y", deg if side == "l" else -deg)


def hand_shape(p, side, kind):
    """relaxed / open / fist / grip (around a 4 cm handle)."""
    S = "_" + side
    if "c_fingers_01" + S not in p.rig.pose.bones:
        return p
    f1, f2, t1, t2 = {"relaxed": (28, 38, 12, 18), "open": (-12, -6, -18, 0), "fist": (95, 95, 55, 40),
                      "grip": (90, 92, 62, 35)}[kind]
    p.r("c_fingers_01" + S, curl(side, f1)).r("c_fingers_02" + S, curl(side, f2))
    if "c_thumb_01" + S in p.rig.pose.bones:
        p.r("c_thumb_01" + S, curl(side, t1), ("X", 8 if kind in ("grip", "fist") else 0))
        p.r("c_thumb_02" + S, curl(side, t2))
    return p


TESTS = []


def test(name, switches=None):
    def deco(fn):
        TESTS.append((name, fn, switches or {}))
        return fn
    return deco


@test("T01_rest")
def _(p):
    return p


@test("T02_bends_90", {"ik_leg_l": 0, "ik_leg_r": 0})
def _(p):
    for s in "lr":
        S = "_" + s
        p.r("c_upperarm" + S, ("X", -40)).r("c_lowerarm" + S, ("X", -90))
        p.r("c_thigh" + S, ("X", -60)).r("c_calf" + S, ("X", 90))
        hand_shape(p, s, "relaxed")
    p.t("c_pelvis", (0, 0, .25))
    return p


@test("T03_bends_135", {"ik_leg_l": 0, "ik_leg_r": 0})
def _(p):
    for s in "lr":
        S = "_" + s
        p.r("c_upperarm" + S, ("X", -25)).r("c_lowerarm" + S, ("X", -135))
        p.r("c_thigh" + S, ("X", -90)).r("c_calf" + S, ("X", 135))
    p.t("c_pelvis", (0, 0, .3))
    return p


@test("T04_wrists", {})
def _(p):
    p.r("c_upperarm_l", ("X", -60)).r("c_lowerarm_l", ("X", -30)).r("c_hand_l", curl("l", 60), ("X", -25))
    p.r("c_upperarm_r", ("X", -60)).r("c_lowerarm_r", ("X", -30)).r("c_hand_r", curl("r", -55), ("X", 30))
    hand_shape(p, "l", "relaxed")
    hand_shape(p, "r", "open")
    return p


@test("T05_ankles", {"ik_leg_l": 0, "ik_leg_r": 0})
def _(p):
    p.r("c_thigh_l", ("X", -35)).r("c_calf_l", ("X", 35)).r("c_foot_l", ("X", -28), ("Y", 12))
    p.r("c_thigh_r", ("X", 25)).r("c_calf_r", ("X", 40)).r("c_foot_r", ("X", 35)).r("c_ball_r", ("X", -30))
    p.t("c_pelvis", (0, 0, .05))
    return p


@test("T06_hands_relaxed_open_fist", {})
def _(p):
    p.r("c_upperarm_l", ("X", -70), ("Y", -10)).r("c_lowerarm_l", ("X", -20))
    p.r("c_upperarm_r", ("X", -70), ("Y", 10)).r("c_lowerarm_r", ("X", -20))
    hand_shape(p, "l", "open")
    hand_shape(p, "r", "fist")
    return p


@test("T07_grip_one_hand", {"pickaxe_in_hand": 1})
def _(p):
    p.r("c_upperarm_r", ("X", -55), ("Y", 8)).r("c_lowerarm_r", ("X", -35))
    hand_shape(p, "r", "grip")
    hand_shape(p, "l", "relaxed")
    return p


@test("T08_grip_two_hands", {"ik_arm_l": 1, "support_grip_l": 1, "ik_arm_r": 1, "hand_r_on_pickaxe": 1})
def _(p):
    # tool-led: the pickaxe is placed in front of the body, both hands follow it by IK
    p.m("c_pickaxe", A.frame_matrix((-.06, -.36, .98), (.18, -.32, .93), (0, -1, -.3)))
    p.r("c_spine_01", ("Z", -6)).r("c_spine_02", ("Z", -8))
    hand_shape(p, "r", "grip")
    hand_shape(p, "l", "grip")
    p.t("c_elbow_pole_l", (.35, .3, -.55)).t("c_elbow_pole_r", (-.35, .3, -.55))
    return p


@test("T09_toe_off_heel_lift", {})
def _(p):
    p.r("c_ball_roll_l", ("X", 35))
    p.t("c_foot_ik_l", (0, .12, 0))
    p.t("c_foot_ik_r", (0, -.18, 0))
    p.r("c_heel_roll_r", ("X", -14))
    p.t("c_pelvis", (0, 0, -.06))
    return p


@test("T10_torso_twist", {})
def _(p):
    p.r("c_spine_01", ("Z", 20), ("Y", 6)).r("c_spine_02", ("Z", 25), ("Y", 6))
    p.r("c_neck_01", ("Z", -10)).r("c_head", ("Z", -15))
    return p


@test("T11_extreme_look", {})
def _(p):
    p.r("c_spine_01", ("Z", 14), ("X", 10)).r("c_spine_02", ("Z", 22), ("X", 18))
    p.r("c_neck_01", ("Z", 22), ("X", 25)).r("c_head", ("Z", 32), ("X", 37))
    return p


@test("T12_crouch", {})
def _(p):
    p.t("c_pelvis", (0, .06, -.2)).r("c_pelvis", ("X", 12))
    p.r("c_spine_01", ("X", 10)).r("c_spine_02", ("X", 6)).r("c_neck_01", ("X", -10)).r("c_head", ("X", -16))
    p.t("c_foot_ik_l", (.02, -.06, 0)).t("c_foot_ik_r", (-.02, .1, 0))
    for s in "lr":
        p.r("c_upperarm_" + s, ("X", -18)).r("c_lowerarm_" + s, ("X", -30))
        hand_shape(p, s, "relaxed")
    return p


@test("T13_low_pose", {"ik_leg_l": 0, "ik_leg_r": 0})
def _(p):
    p.t("c_pelvis", (0, 0, -.48)).r("c_pelvis", ("X", 90))
    p.r("c_neck_01", ("X", -30)).r("c_head", ("X", -45))
    p.r("c_thigh_l", ("X", 4)).r("c_thigh_r", ("X", -6))
    for s in "lr":
        p.r("c_upperarm_" + s, ("X", -150), ("Y", 10 if s == "r" else -10)).r("c_lowerarm_" + s, ("X", -10))
    return p


@test("T14_forearm_twist_bad", {})
def _(p):
    p.r("c_upperarm_l", ("X", -30)).r("c_lowerarm_l", ("X", -45), ("Z", 90))
    p.r("c_upperarm_r", ("X", -30)).r("c_lowerarm_r", ("X", -45)).r("c_hand_r", ("Z", -90))
    return p


# pairs whose mutual penetration is a defect (contacts at shared faces read ~0)
PEN_PAIRS = [("arm_l", "torso"), ("arm_r", "torso"), ("leg_l", "leg_r"), ("leg_r", "leg_l"), ("arm_l", "leg_l"),
             ("arm_r", "leg_r"), ("palm_l", "leg_l"), ("palm_r", "leg_r"), ("fingers2_l", "leg_l"),
             ("fingers2_r", "leg_r"), ("palm_l", "torso"), ("palm_r", "torso"), ("thumb2_l", "palm_l"),
             ("fingers2_l", "palm_l"), ("fingers2_r", "palm_r"), ("thumb2_r", "palm_r"), ("toe_l", "toe_r")]
JUNCTIONS = [("palm_l", "arm_l"), ("fingers1_l", "palm_l"), ("fingers2_l", "fingers1_l"), ("thumb1_l", "palm_l"),
             ("thumb2_l", "thumb1_l"), ("toe_l", "leg_l"), ("palm_r", "arm_r"), ("fingers1_r", "palm_r"),
             ("fingers2_r", "fingers1_r"), ("thumb1_r", "palm_r"), ("thumb2_r", "thumb1_r"), ("toe_r", "leg_r")]

PART_NAMES = set(body["up_parts"])
PEN_PAIRS = [(a, b) for a, b in PEN_PAIRS if a in PART_NAMES and b in PART_NAMES]
JUNCTIONS = [(a, b) for a, b in JUNCTIONS if a in PART_NAMES and b in PART_NAMES]
act = A.new_action(rig, "TEST_RigPoses")
for i, (name, fn, sw) in enumerate(TESTS):
    f = i * 10
    p = fn(A.Pose(rig))
    A.reset_pose(rig)
    A.set_switches(rig, frame=f, **sw)
    # key every control that any test touches so poses never leak between frames
    p.apply(reset=True)
    for pb in rig.pose.bones:
        pb.keyframe_insert("rotation_quaternion", frame=f)
        pb.keyframe_insert("location", frame=f)
for fc in A.action_fcurves(act):
    for kp in fc.keyframe_points:
        kp.interpolation = "CONSTANT"

rest_body = Vd.rest_coords(body)
names, vpart = Vd.part_index(body)
results = {}
R.setup_workbench(scene, res=(480, 600))
R.set_visible(["REF_SOURCE_normalised", "GUIDES"], False)
cam = R.camera("PREVIEW_Cam_Test", None, lens=50)
shots = []
for i, (name, fn, sw) in enumerate(TESTS):
    f = i * 10
    scene.frame_set(f)
    co, polys = Vd.evaluated(body)
    ps = Vd.PartSet(body, co, polys)
    st = Vd.edge_face_stats(rest_body, co, polys, ps.fpart, ps.names)
    pen = {f"{a}>{b}": round(ps.penetration(a, b) * 100, 2) for a, b in PEN_PAIRS}
    hco, hpolys = Vd.evaluated(head)
    phead = Vd.PartSet(head, hco, hpolys)
    pen["head>torso"] = round(ps.penetration("head", "torso", other=phead) * 100, 2)
    pen["torso>head"] = round(phead.penetration("torso", "head", other=ps) * 100, 2)
    gaps = {f"{a}~{b}": round(ps.min_gap(a, b) * 100, 2) for a, b in JUNCTIONS if not ps.attached(a, b)}
    r = {"edge_min": round(min(v["edge_min"] for v in st.values()), 3),
         "edge_max": round(max(v["edge_max"] for v in st.values()), 3),
         "area_min": round(min(v["area_min"] for v in st.values() if v["area_min"]), 3),
         "area_max": round(max(v["area_max"] for v in st.values() if v["area_max"]), 3),
         "worst_parts": sorted(((k, round(v["edge_min"], 3), round(v["edge_max"], 3)) for k, v in st.items()),
                               key=lambda x: x[1])[:3],
         "penetration_cm": {k: v for k, v in pen.items() if v > .3},
         "detached_cm": gaps}
    if sw.get("pickaxe_in_hand") or sw.get("hand_r_on_pickaxe"):
        pco, ppolys = Vd.evaluated(pick)
        pkps = Vd.PartSet(pick, pco, ppolys)
        hp = {}
        hand_parts = [h for h in ("palm_r", "fingers1_r", "fingers2_r", "thumb1_r", "thumb2_r") + (
            ("palm_l", "fingers1_l", "fingers2_l", "thumb1_l", "thumb2_l") if sw.get("support_grip_l") else ())
            if h in PART_NAMES] or ["arm_r"] + (["arm_l"] if sw.get("support_grip_l") else [])
        for hpart in hand_parts:
            hp[hpart] = {"handle_in_part_cm": round(ps.penetration("handle", hpart, other=pkps) * 100, 2),
                         "part_in_handle_cm": round(pkps.penetration(hpart, "handle", other=ps) * 100, 2)}
            t = pkps.tree("handle")
            d = min(t.find_nearest(Vector(ps.co[vi]))[3] for vi in ps.verts(hpart))
            hp[hpart]["gap_to_handle_cm"] = round(d * 100, 2)
        r["grip"] = hp
    results[name] = r
    tgt = Vector((0, 0, .9))
    if name == "T13_low_pose":
        tgt = Vector((0, -.2, .3))
    for view, off in (("34", Vector((-1.6, -2.6, 1.0))), ("side", Vector((-3.0, 0, .35)))):
        R.look_at(cam, tgt + off, tgt)
        scene.camera = cam
        scene.render.filepath = os.path.join(C.ROOT, "previews", "_tmp", f"s02_{name}_{view}.png")
        bpy.ops.render.render(write_still=True)
        shots.append(scene.render.filepath)
    print("[rig]", name, json.dumps(r))

# grip close-ups
for i, (name, fn, sw) in enumerate(TESTS):
    if name not in ("T06_hands_relaxed_open_fist", "T07_grip_one_hand", "T08_grip_two_hands"):
        continue
    scene.frame_set(i * 10)
    bpy.context.view_layer.update()
    hr = skel.pose.bones["hand_r"]
    tgt = skel.matrix_world @ hr.head + Vector((0, -.05, -.04))
    for view, off in (("closeA", Vector((-.45, -.55, .25))), ("closeB", Vector((.35, -.6, -.1)))):
        R.look_at(cam, tgt + off, tgt)
        scene.render.filepath = os.path.join(C.ROOT, "previews", "_tmp", f"s02_{name}_{view}.png")
        bpy.ops.render.render(write_still=True)

json.dump({"tests": results, "note": "penetration lists only pairs > 0.3 cm; junction gaps only > 0.2 cm"},
          open(os.path.join(C.ROOT, "validation", "s02_rig_tests.json"), "w"), indent=1)
print("[rig] done")
