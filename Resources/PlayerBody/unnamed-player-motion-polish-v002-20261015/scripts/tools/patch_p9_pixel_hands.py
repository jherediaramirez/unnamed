"""Patch: hand style 'pixel' = hidden pixel fingers. Relaxed, the hand is exactly the vanilla arm box (same pixels);
the four 1-px bars along the inner (medial) bottom edge fold in two segments to wrap a handle held against the inner
face of the arm end. Grouped curl (fingers_01/02), no thumb.
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
    # pixel-finger geometry constants (left side; x medial face = ARM_X[0])
    ('''KNUCKLE = (PALM["x"][0], 0.0, PALM["z"][0])   # inner (medial) bottom edge of the palm''',
     '''KNUCKLE = (PALM["x"][0], 0.0, PALM["z"][0])   # inner (medial) bottom edge of the palm
PIX = {"knuckle_z": LEG_TOP + 1.5 * (V if "V" in dir() else .0583), "bar": None}   # filled below'''),
    ('''    if HAND_STYLE == "blocky":
        s["fingers_01_l"] = ("hand_l", k, (k[0], 0, k[2] - FINGER["l1"]), MED)''',
     '''    if HAND_STYLE == "pixel":
        kz = LEG_TOP + 1.5 * V                        # knuckle line: the arm's last 1.5 px rows are the fingers
        mz = (kz + LEG_TOP) / 2
        s["fingers_01_l"] = ("hand_l", (ARM_X[0], 0, kz), (ARM_X[0], 0, mz), MED)
        s["fingers_02_l"] = ("fingers_01_l", (ARM_X[0], 0, mz), (ARM_X[0], 0, LEG_TOP), MED)
    if HAND_STYLE == "blocky":
        s["fingers_01_l"] = ("hand_l", k, (k[0], 0, k[2] - FINGER["l1"]), MED)'''),
    ('''if HAND_STYLE == "blocky":
    GRIP_L = (KNUCKLE[0] - .025, sum(FINGER["y"]) / 2, KNUCKLE[2] + .02)''',
     '''if HAND_STYLE == "pixel":
    GRIP_L = (ARM_X[0] - .022, 0.0, LEG_TOP + 1.5 * V + .02)   # handle against the inner face, on the folded bars
elif HAND_STYLE == "blocky":
    GRIP_L = (KNUCKLE[0] - .025, sum(FINGER["y"]) / 2, KNUCKLE[2] + .02)'''),
    ('''     (("thumb_01", "thumb_02", "fingers_01", "fingers_02") if HAND_STYLE == "blocky" else ())] + \\''',
     '''     (("thumb_01", "thumb_02", "fingers_01", "fingers_02") if HAND_STYLE == "blocky" else
      ("fingers_01", "fingers_02") if HAND_STYLE == "pixel" else ())] + \\'''),
])
patch("up_build.py", [
    ('''        if C.HAND_STYLE != "blocky":
            # vanilla arm: one full-length box with its original 12 rows and bottom face; the end block is the hand
            body.block("arm" + S, at.x, at.y, at.z, at, arm_weights(side),
                       extra_cuts=((), (), (C.J["elbow"][2], C.J["wrist"][2])))
            continue''',
     '''        if C.HAND_STYLE == "pixel":
            # vanilla arm down to the knuckle line, then the last 1.5 px rows: a 3-px block (back of the hand) plus
            # four 1-px finger bars on the inner edge, every face projected from the same arm pixels
            kz = C.LEG_TOP + 1.5 * C.V
            mz = (kz + C.LEG_TOP) / 2
            body.block("arm" + S, at.x, at.y, (kz, C.TORSO_TOP), at, arm_weights(side),
                       face_rects={"bottom": skin_bottom}, extra_cuts=((), (), (C.J["elbow"][2], C.J["wrist"][2])))
            inner = (C.ARM_X[0], C.ARM_X[0] + C.H)
            back = (C.ARM_X[0] + C.H, C.ARM_X[1])
            body.block("palm" + S, mirror_range(back, side), C.ARM_Y, (C.LEG_TOP, kz), at, rigid("hand" + S))
            for i in range(4):
                y0 = C.ARM_Y[0] + i * C.H
                gap = .002
                fy = (y0 + gap, y0 + C.H - gap)
                body.block("fingers1" + S, mirror_range(inner, side), fy, (mz, kz), at, rigid("fingers_01" + S))
                body.block("fingers2" + S, mirror_range(inner, side), fy, (C.LEG_TOP, mz + .004), at,
                           rigid("fingers_02" + S))
            continue
        if C.HAND_STYLE != "blocky":
            # vanilla arm: one full-length box with its original 12 rows and bottom face; the end block is the hand
            body.block("arm" + S, at.x, at.y, at.z, at, arm_weights(side),
                       extra_cuts=((), (), (C.J["elbow"][2], C.J["wrist"][2])))
            continue'''),
    ('''        if C.HAND_STYLE == "blocky":
            return {"upperarm" + s: 1 - t, "lowerarm" + s: t}''',
     '''        if C.HAND_STYLE == "blocky":
            return {"upperarm" + s: 1 - t, "lowerarm" + s: t}
        if C.HAND_STYLE == "pixel" and co[2] < C.LEG_TOP + 1.5 * C.V + 1e-6:
            return {"hand" + s: 1.0}'''),
    ('''    S = mp.matrix @ Matrix.Translation((0, .16 if C.HAND_STYLE == "blocky" else .22, 0))''',
     '''    S = mp.matrix @ Matrix.Translation((0, .16 if C.HAND_STYLE == "blocky" else .24, 0))'''),
])
# PIX helper line is unused; drop it to keep the config clean
p = os.path.join(S, "up_config.py")
s = open(p, encoding="utf-8").read()
s = s.replace('''PIX = {"knuckle_z": LEG_TOP + 1.5 * (V if "V" in dir() else .0583), "bar": None}   # filled below\n''', "")
open(p, "w", encoding="utf-8").write(s)
print("[patch] done")
