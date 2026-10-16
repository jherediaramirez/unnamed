"""Patch: hand style switch. 'box' (default, Jesus 2026-10-15: "revert back to the previous hands and just have it go
through his hand like in regular Minecraft") = the original full-length arm box, wrist pivot kept as a hand bone that
bends the last 15 cm of the box, pickaxe handle straight through the box end. 'blocky' = the palm/fingers/thumb build.
"""
import os

S = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def patch(name, pairs):
    p = os.path.join(S, name)
    s = open(p, encoding="utf-8").read()
    for old, new in pairs:
        if old not in s:
            raise SystemExit(f"[patch] {name}: missing {old[:80]!r}")
        s = s.replace(old, new)
    open(p, "w", encoding="utf-8").write(s)
    print("[patch]", name, len(pairs))


patch("up_config.py", [
    ('''WRIST_Z = TORSO_TOP - 12 * V + .15       # wrist pivot; hand = 15 cm (palm 7 + fingers 8)''',
     '''HAND_STYLE = os.environ.get("UP_HANDS", "box")   # "box" = vanilla arm box (default), "blocky" = palm+fingers+thumb
WRIST_Z = TORSO_TOP - 12 * V + .15       # wrist pivot; the last 15 cm of the arm is the hand'''),
    ('''    s["hand_l"] = ("lowerarm_l", wr, (wr[0], 0, PALM["z"][0]), FWD)
    k = KNUCKLE''',
     '''    s["hand_l"] = ("lowerarm_l", wr, (wr[0], 0, PALM["z"][0] if HAND_STYLE == "blocky" else LEG_TOP), FWD)
    k = KNUCKLE'''),
    ('''    s["fingers_01_l"] = ("hand_l", k, (k[0], 0, k[2] - FINGER["l1"]), MED)''',
     '''    if HAND_STYLE == "blocky":
        s["fingers_01_l"] = ("hand_l", k, (k[0], 0, k[2] - FINGER["l1"]), MED)'''),
    ('''    s["fingers_02_l"] = ("fingers_01_l", (k[0], 0, k[2] - FINGER["l1"]),
                         (k[0], 0, k[2] - FINGER["l1"] - FINGER["l2"]), MED)''',
     '''        s["fingers_02_l"] = ("fingers_01_l", (k[0], 0, k[2] - FINGER["l1"]),
                             (k[0], 0, k[2] - FINGER["l1"] - FINGER["l2"]), MED)'''),
    ('''    s["thumb_01_l"] = ("hand_l", tr, t1, MED)
    s["thumb_02_l"] = ("thumb_01_l", t1, t2, MED)''',
     '''    if HAND_STYLE == "blocky":
        s["thumb_01_l"] = ("hand_l", tr, t1, MED)
        s["thumb_02_l"] = ("thumb_01_l", t1, t2, MED)'''),
    ('''    [f"{b}_{s}" for s in "lr" for b in ("clavicle", "upperarm", "lowerarm", "hand", "thumb_01", "thumb_02",
                                         "fingers_01", "fingers_02")] + \\''',
     '''    [f"{b}_{s}" for s in "lr" for b in ("clavicle", "upperarm", "lowerarm", "hand") +
     (("thumb_01", "thumb_02", "fingers_01", "fingers_02") if HAND_STYLE == "blocky" else ())] + \\'''),
    ('''    "ball_l": "ball_l", "fingers_01_l": "(none; grouped fingers)", "thumb_01_l": "(none in Better Steve)",''',
     '''    "ball_l": "ball_l",'''),
])
# grip centre (handle axis, runs front-back along Y): box hands = through the middle of the arm's end block
cfg = os.path.join(S, "up_config.py")
s = open(cfg, encoding="utf-8").read()
s = s.replace('''SKELETON = _skeleton()''', '''SKELETON = _skeleton()
if HAND_STYLE == "blocky":
    GRIP_L = (KNUCKLE[0] - .025, sum(FINGER["y"]) / 2, KNUCKLE[2] + .02)
else:
    GRIP_L = ((ARM_X[0] + ARM_X[1]) / 2, 0.0, LEG_TOP + .07)   # 7 cm above the arm's bottom face, box centre''')
open(cfg, "w", encoding="utf-8").write(s)
print("[patch] up_config grip")

patch("up_build.py", [
    ('''def arm_weights(side):
    s = "_" + side
    ez = C.J["elbow"][2]

    def fn(co):
        t = smoothstep(ez + .045, ez - .045, co[2])
        return {"upperarm" + s: 1 - t, "lowerarm" + s: t}
    return fn''',
     '''def arm_weights(side):
    s = "_" + side
    ez = C.J["elbow"][2]
    wz = C.J["wrist"][2]

    def fn(co):
        t = smoothstep(ez + .045, ez - .045, co[2])
        if C.HAND_STYLE == "blocky":
            return {"upperarm" + s: 1 - t, "lowerarm" + s: t}
        h = smoothstep(wz + .025, wz - .025, co[2])     # the arm's end block bends at the wrist (box hands)
        return {"upperarm" + s: 1 - t, "lowerarm" + s: t * (1 - h), "hand" + s: t * h}
    return fn'''),
    ('''        skin_bottom = (U + 4, Vv + 4 + 8, U + 8, Vv + 4 + 12)  # forearm end cap uses front-face skin rows
        body.block("arm" + S, at.x, at.y, (C.FOREARM_BOTTOM, C.TORSO_TOP), at, arm_weights(side),
                   face_rects={"bottom": skin_bottom}, extra_cuts=((), (), (C.J["elbow"][2],)))''',
     '''        skin_bottom = (U + 4, Vv + 4 + 8, U + 8, Vv + 4 + 12)  # forearm end cap uses front-face skin rows
        if C.HAND_STYLE != "blocky":
            # vanilla arm: one full-length box with its original 12 rows and bottom face; the end block is the hand
            body.block("arm" + S, at.x, at.y, at.z, at, arm_weights(side),
                       extra_cuts=((), (), (C.J["elbow"][2], C.J["wrist"][2])))
            continue
        body.block("arm" + S, at.x, at.y, (C.FOREARM_BOTTOM, C.TORSO_TOP), at, arm_weights(side),
                   face_rects={"bottom": skin_bottom}, extra_cuts=((), (), (C.J["elbow"][2],)))'''),
    ('''        # ---- hand curl + grips
        k = sk["fingers_01" + S]
        nb("c_fingers_curl" + S, k.head_local, k.head_local + Vector((0, 0, -.06)), "c_hand" + S, (0, -1, 0))
        E["c_fingers_curl" + S].matrix = k.matrix_local
        E["c_fingers_curl" + S].length = .07''',
     '''        # ---- hand curl (blocky hands only) + grips
        if "fingers_01" + S in sk:
            k = sk["fingers_01" + S]
            nb("c_fingers_curl" + S, k.head_local, k.head_local + Vector((0, 0, -.06)), "c_hand" + S, (0, -1, 0))
            E["c_fingers_curl" + S].matrix = k.matrix_local
            E["c_fingers_curl" + S].length = .07'''),
    ('''        g = Vector((sgn * (C.KNUCKLE[0] - .025), sum(C.FINGER["y"]) / 2, C.KNUCKLE[2] + .02))''',
     '''        g = Vector((sgn * C.GRIP_L[0], C.GRIP_L[1], C.GRIP_L[2]))'''),
    ('''    S = mp.matrix @ Matrix.Translation((0, .16, 0))''',
     '''    S = mp.matrix @ Matrix.Translation((0, .16 if C.HAND_STYLE == "blocky" else .22, 0))'''),
    ('''        for fb in ("c_fingers_01" + S, "c_fingers_02" + S):''',
     '''        for fb in [b for b in ("c_fingers_01" + S, "c_fingers_02" + S) if b in P]:'''),
])
patch("up_loco.py", [
    ('''    p.r("c_fingers_01_" + side, c(f1)).r("c_fingers_02_" + side, c(f2))
    p.r("c_thumb_01_" + side, c(t1)).r("c_thumb_02_" + side, c(12 * amount))''',
     '''    if "c_fingers_01_" + side not in p.rig.pose.bones:     # box hands: nothing to curl
        return
    p.r("c_fingers_01_" + side, c(f1)).r("c_fingers_02_" + side, c(f2))
    p.r("c_thumb_01_" + side, c(t1)).r("c_thumb_02_" + side, c(12 * amount))'''),
])
patch("up_pickaxe.py", [
    ('''    S = "_" + side
    p.r("c_fingers_01" + S, c(f1)).r("c_fingers_02" + S, c(f2))
    p.r("c_thumb_01" + S, c(t1), ("X", 8 if kind == "grip" else 0)).r("c_thumb_02" + S, c(t2))''',
     '''    S = "_" + side
    if "c_fingers_01" + S not in p.rig.pose.bones:          # box hands: the handle passes through the arm's end
        return
    p.r("c_fingers_01" + S, c(f1)).r("c_fingers_02" + S, c(f2))
    p.r("c_thumb_01" + S, c(t1), ("X", 8 if kind == "grip" else 0)).r("c_thumb_02" + S, c(t2))'''),
])
patch("s02_rig_tests.py", [
    ('''    S = "_" + side
    f1, f2, t1, t2 = {"relaxed": (28, 38, 12, 18), "open": (-12, -6, -18, 0), "fist": (95, 95, 55, 40),
                      "grip": (90, 92, 62, 35)}[kind]''',
     '''    S = "_" + side
    if "c_fingers_01" + S not in p.rig.pose.bones:
        return p
    f1, f2, t1, t2 = {"relaxed": (28, 38, 12, 18), "open": (-12, -6, -18, 0), "fist": (95, 95, 55, 40),
                      "grip": (90, 92, 62, 35)}[kind]'''),
    ('''act = A.new_action(rig, "TEST_RigPoses")''',
     '''PART_NAMES = set(body["up_parts"])
PEN_PAIRS = [(a, b) for a, b in PEN_PAIRS if a in PART_NAMES and b in PART_NAMES]
JUNCTIONS = [(a, b) for a, b in JUNCTIONS if a in PART_NAMES and b in PART_NAMES]
act = A.new_action(rig, "TEST_RigPoses")'''),
    ('''        for hpart in ("palm_r", "fingers1_r", "fingers2_r", "thumb1_r", "thumb2_r") + (
                ("palm_l", "fingers1_l", "fingers2_l", "thumb1_l", "thumb2_l") if sw.get("support_grip_l") else ()):''',
     '''        hand_parts = [h for h in ("palm_r", "fingers1_r", "fingers2_r", "thumb1_r", "thumb2_r") + (
            ("palm_l", "fingers1_l", "fingers2_l", "thumb1_l", "thumb2_l") if sw.get("support_grip_l") else ())
            if h in PART_NAMES] or ["arm_r"] + (["arm_l"] if sw.get("support_grip_l") else [])
        for hpart in hand_parts:'''),
])
patch("s06_pickaxe.py", [
    ('''    for part in ("torso", "leg_l", "leg_r", "arm_l", "arm_r"):
        t = ps.tree(part)
        lo, hi = ps.bbox(part)
        w = 0.0
        for p in pts:''',
     '''    near_grip = [co.length < .13 for co in TOOL_CO]      # the handle passes through the hand end by design
    for part in ("torso", "leg_l", "leg_r", "arm_l", "arm_r"):
        t = ps.tree(part)
        lo, hi = ps.bbox(part)
        w = 0.0
        for i, p in enumerate(pts):
            if part == "arm_r" and near_grip[i]:
                continue'''),
])
patch("s08_roundtrip.py", [
    ('''                           "bones_all_30": all(e["bones"] == 30 for e in anim_rep.values())}''',
     '''                           "bones_all_match": all(e["bones"] == man["skeleton"]["bone_count"] for e in anim_rep.values())}'''),
])
patch("s07_export.py", [
    ('''                 "rest_pose": "I-pose (arms down, palms in, thumbs forward); knees 1.2 cm forward, elbows 1.2 cm back "''',
     '''                 "hand_style": C.HAND_STYLE + (" (vanilla arm box; the end block is the hand, bent by hand_l/r; "
                                               "the pickaxe handle passes through it)" if C.HAND_STYLE == "box" else ""),
                 "rest_pose": "I-pose (arms down, palms in, thumbs forward); knees 1.2 cm forward, elbows 1.2 cm back "'''),
])
