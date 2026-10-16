"""Rebuild every review contact sheet from the rendered frames (so sheets never lag the renders).

    uv run --no-project --with pillow python scripts/tools/compose_sheets.py <work root>
"""
import os
import subprocess
import sys

root = os.path.abspath(sys.argv[1])
pv = os.path.join(root, "previews")
tmp = os.path.join(pv, "_tmp")
sheet = os.path.join(root, "scripts", "tools", "sheet.py")


def make(out, files, cols, width, labels, title=""):
    files = [f for f in files if os.path.exists(f)]
    if not files:
        print("[sheets] skip", out)
        return
    cmd = [sys.executable, sheet, os.path.join(pv, out)] + files + ["--cols", str(cols), "--width", str(width),
                                                                     "--labels", "|".join(labels)]
    if title:
        cmd += ["--title", title]
    subprocess.run(cmd, check=True)


T = lambda *p: os.path.join(tmp, *p)  # noqa: E731
P = lambda *p: os.path.join(pv, *p)   # noqa: E731
make("s01_sheet_views.png", [P("s01_front.png"), P("s01_right.png"), P("s01_back.png"), P("s01_front34.png")], 4, 1600,
     ["front", "right", "back", "front 3/4"], "Unnamed Player v001 with guides (1 m block, eyes 162/127/40, boxes)")
make("s01_sheet_close.png", [P(f"s01_close_{n}.png") for n in ("hand_l_front", "hand_l_outer", "hand_l_inner_below",
                                                                 "feet", "feet_side")], 5, 1500,
     ["left hand front (rest)", "outer", "inner, from below", "toe caps", "foot side"])
make("s07_presets_front.png", [P(f"s01_{p}_{v}.png") for v in ("front", "right") for p in ("A_uniform", "B_head40", "C_head36")],
     6, 1800, ["A uniform 0.225: head 45, eye 154.7 (-7.3 cm)", "B head 40 (CURRENT): eye 157.5 (-4.5)",
               "C head 36: eye 159.75 (-2.25)", "A side", "B side", "C side"],
     "Proportion presets at 1.80 m: red line = 162 cm gameplay eye, orange = 127 sneak, blue = 40 low")
tests = ["T01_rest", "T02_bends_90", "T03_bends_135", "T04_wrists", "T05_ankles", "T06_hands_relaxed_open_fist",
         "T07_grip_one_hand", "T08_grip_two_hands", "T09_toe_off_heel_lift", "T10_torso_twist", "T11_extreme_look",
         "T12_crouch", "T13_low_pose", "T14_forearm_twist_bad"]
for name, chunk, title in (("s02_sheet_A.png", tests[:7], "Rig tests (control rig -> deform skeleton)"),
                           ("s02_sheet_B.png", tests[7:], "Rig tests continued")):
    make(name, [T(f"s02_{t}_34.png") for t in chunk] + [T(f"s02_{t}_side.png") for t in chunk], 7, 1750,
         [t[4:].replace("_", " ") for t in chunk] + ["side"] * len(chunk), title)
make("s02_sheet_hands.png", [T(f"s02_{t}_{v}.png") for t in ("T06_hands_relaxed_open_fist", "T07_grip_one_hand",
                                                           "T08_grip_two_hands") for v in ("closeA", "closeB")],
     6, 1800, ["arms forward", "from below", "1-hand grip", "from below", "2-hand grip", "from below"])
for fam in ("Walk", "Sprint", "Sneak"):
    files, labels = [], []
    for d in ("F", "FR", "R", "BR", "B", "BL", "L", "FL"):
        for k in range(4):
            files.append(T("loco", f"{fam}_{d}_{k}.png"))
            labels.append(f"{d} {k}")
    make(f"s03_sheet_{fam}.png", files, 8, 1600, labels,
         f"{fam} (baked, constraints muted): each direction = contact L, 0.25, contact R, 0.75")
air = [("air_standing_jump", [30, 36, 45, 54, 66, 70]), ("air_edge_fall_firm_landing", [30, 42, 51, 57, 62, 73])]
files = [T("air", f"{n}_{f:03d}.png") for n, fs in air for f in fs]
files += [T("air", f"turn_Turn_L90_{f:03d}.png") for f in (10, 17, 25, 32, 40)] + [T("air", "transition_stand_sneak_028.png")]
files += [T("air", f"turn_SneakTurn_L90_{f:03d}.png") for f in (10, 17, 25, 32, 40)] + [T("air", "transition_stand_sneak_036.png")]
make("s04_sheet_air_turns.png", files, 6, 1680,
     ["jump tick (Jump_Start f0)", "+0.1 s", "apex (to Fall)", "falling", "landing (Land_Soft)", "+4 f",
      "walk off ledge", "Fall begins", "falling", "2-block impact (Land_Firm)", "+5 f compress", "recovering",
      "Turn_L90 start", "25%", "50%", "75%", "end (body +90 applied once)", "stand->sneak f8",
      "SneakTurn_L90 start", "25%", "50%", "75%", "end", "sneak idle"],
     "Air on the simulated 20 Hz jump arc, edge fall, root-neutral turns (yaw from metadata), stand->sneak")
order = ["UL", "U", "UR", "L", "C", "R", "DL", "D", "DR"]
make("s05_sheet_look_grids.png", [T("look", f"Look_{k}.png") for k in order] + [T("look", f"SneakLook_{k}.png") for k in order],
     6, 1500, order[:4] + ["C (ref)"] + order[5:] + ["sneak " + k for k in order[:4]] + ["sneak C (ref)"] +
     ["sneak " + k for k in order[5:]], "Look grid: yaw -90/0/+90 x pitch +90 up / 0 / -90 down (standing, sneak)")
fp = ["stand_p-90_v70_NO_OFFSET", "stand_p-90_v70", "stand_p-70_v70", "stand_p-45_v70", "stand_p-90_v110",
      "stand_p-70_v110", "stand_y60_p-60_v110", "stand_p0_v110", "sneak_p-60_v110", "sneak_p-90_v110", "low_p0_v110",
      "low_p-45_v70"]
make("s05_sheet_first_person.png", [T("look", f"FP_{n}.png") for n in fp], 4, 1600,
     ["stand 162 / vFOV70 / pitch -90 / NO body offset", "stand / v70 / -90 / FP body offset", "stand / v70 / -70",
      "stand / v70 / -45", "stand / vFOV110 / -90", "stand / v110 / -70", "stand / v110 / yaw+60 pitch-60",
      "stand / v110 / 0", "sneak 127 / v110 / -60", "sneak / v110 / -90", "low 40 / v110 / 0", "low 40 / v70 / -45"],
     "True first person: camera fixed at the gameplay eye (not head-parented), head hidden, vertical FOV labelled")
files, labels = [], []
for k in "ABC":
    for f, lab in ((0, "start (link)"), (3, "anticipation"), (6, "approach"), (9, "CONTACT f9"), (12, "rebound"),
                   (15, "recovery")):
        files.append(T("pick", f"strip_{k}_{f:02d}.png"))
        labels.append(f"{k} {lab}")
make("s06_strips.png", files, 6, 1680, labels,
     "Pickaxe strikes A / B / C (frames 0,3,6,9,12,15 of 18 at 60 fps; contact = frame 9 = 0.15 s)")
print("[sheets] done")
