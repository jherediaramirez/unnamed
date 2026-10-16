"""Unnamed Player (UP) v001 - single source of truth for dimensions, names, speeds and clip specs.

Authoring convention (Blender master): metres, Z up, character faces -Y, character's left = +X, soles at Z = 0.
Export convention: a temporary centimetre copy (scale x100 applied), FBX_SCALE_NONE, axis_forward -Z / axis_up Y,
which lands in Unreal as +Y facing (like the UE5 Mannequin and the old Better Steve), Z up, centimetres, import scale 1.

Every number the game owns comes from the survival checkout (FoundationProfile.h, FoundationBody.cpp at
3e7c5493fcb19bf53de68d622156d2d8dd73ab12); nothing here changes gameplay.
"""

import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = "v002"
MASTER = os.path.join(ROOT, "blend", f"UP_Player_master_{VERSION}.blend")
SOURCE_GLB = os.path.join(ROOT, "source", "minecraft_steve_rigged.glb")
SKIN_64 = os.path.join(ROOT, "textures", "T_UP_Skin_64.png")
SKIN_1024 = os.path.join(ROOT, "textures", "T_UP_Skin_1024.png")
FPS = 60

# ------------------------------------------------------------------ game contract (cm in the game, m here)
GAME = {
    "block_m": 1.0, "width_m": .60, "stand_height_m": 1.80, "stand_eyes_m": 1.62,
    "sneak_height_m": 1.50, "sneak_eyes_m": 1.27, "low_height_m": .60, "low_eyes_m": .40,
    "step_m": .60, "tick_s": .05, "reach_m": 4.50,
    "jump_cm_per_tick": 42.0, "gravity_cm_per_tick": 8.0, "vertical_drag": .98,
    "eye_smoothing_per_tick": .5,
}
SPEEDS = {  # m/s, from FoundationBodyClip::GetSampleSpeed (cm/s / 100)
    "Walk": {"cardinal": 4.3172, "diagonal": 4.4053},
    "Sprint": {"cardinal": 5.6123, "diagonal": 5.7269},
    "Sneak": {"cardinal": 1.2952, "diagonal": 1.8316},
}
DIRS = ["F", "FR", "R", "BR", "B", "BL", "L", "FL"]  # game order; angle = index * 45 deg, + = right
DIR_ANGLE = {d: i * 45.0 if i * 45.0 <= 180 else i * 45.0 - 360 for i, d in enumerate(DIRS)}


def dir_vector(d):
    """Unit travel vector in character space (Blender: forward -Y, right -X)."""
    a = math.radians(DIR_ANGLE[d])
    return (-math.sin(a), -math.cos(a))


def speed_of(gait, d):
    return SPEEDS[gait]["diagonal" if DIRS.index(d) % 2 else "cardinal"]


# ------------------------------------------------------------------ proportions
# Painted eyes sit on face row 4 of 8 (centre 4.5 head-px below the top). With the top at 1.80 m the uniform 0.225
# normalisation (head 45 cm) paints the eye centre at 1.547 m. Preset B shrinks the head modestly and lengthens body
# and limbs so the painted eye row moves up 2.8 cm and the 1.62 m guide sits 2 cm above the eye row (brow).
PRESETS = {
    "A_uniform": {"head": .45, "h": .05625, "v": .05625},
    "B_head40": {"head": .40, "h": .05, "v": (1.80 - .40) / 24},
    "C_head36": {"head": .36, "h": .045, "v": (1.80 - .36) / 24},
}
PRESET = os.environ.get("UP_PRESET", "B_head40")
P = PRESETS[PRESET]
HEAD = P["head"]          # head cube edge (m)
HP = HEAD / 8             # head pixel (m)
H = P["h"]                # body/limb horizontal pixel (m)
V = P["v"]                # body/limb vertical pixel (m)
LEG_TOP = 12 * V          # crotch / torso bottom
TORSO_TOP = 24 * V        # shoulders / head bottom
HEIGHT = TORSO_TOP + HEAD
EYE_ROW = (HEIGHT - 5 * HP, HEIGHT - 4 * HP)  # painted eye row band (bottom, top)

# ------------------------------------------------------------------ geometry (left side; right mirrors x -> -x)
LEG_X = (0.0, 4 * H)                     # left leg x range
LEG_Y = (-2 * H, 2 * H)
ARM_X = (4 * H, 8 * H)                   # left arm x range (outer face at 8H)
ARM_Y = (-2 * H, 2 * H)
TORSO_X = (-4 * H, 4 * H)
TORSO_Y = (-2 * H, 2 * H)

# joints (left side, metres)
J = {}
J["hip_z"] = LEG_TOP - .5 * V
J["ankle_z"] = 1.5 * V
J["knee_z"] = J["ankle_z"] + (J["hip_z"] - J["ankle_z"]) * .475
J["ball_y"] = LEG_Y[0] + H               # 1 px behind the leg's front face
J["ball_z"] = .015
J["toe_tip_y"] = LEG_Y[0] - .04          # toe cap protrudes 4 cm
J["heel_y"] = LEG_Y[1]
J["shoulder"] = (ARM_X[0] + 1.6 * H, 0.0, TORSO_TOP - 2 * V)
# small rest bends (knees forward, elbows back) give the legacy IK solver a bend direction; the mesh is unchanged
J["knee_y"] = -.012
J["elbow"] = ((ARM_X[0] + ARM_X[1]) / 2, .012, TORSO_TOP - 6 * V)
HAND_STYLE = os.environ.get("UP_HANDS", "box")   # "box" = vanilla arm box (default), "blocky" = palm+fingers+thumb
WRIST_Z = TORSO_TOP - 12 * V + .15       # wrist pivot; the last 15 cm of the arm is the hand
J["wrist"] = ((ARM_X[0] + ARM_X[1]) / 2, 0.0, WRIST_Z)
FOREARM_BOTTOM = WRIST_Z - .012
PALM = {"x": (ARM_X[1] - .005 - .125, ARM_X[1] - .005), "y": (-.075, .075), "z": (WRIST_Z - .07, FOREARM_BOTTOM + .03)}
FINGER = {"t": .045, "y": (-.06, .075), "l1": .05, "l2": .035}  # thickness, span, proximal, distal lengths
THUMB = {"w": .04, "l1": .035, "l2": .03}
KNUCKLE = (PALM["x"][0], 0.0, PALM["z"][0])   # inner (medial) bottom edge of the palm
TOE_CAP = {"x": (LEG_X[0] + .0075, LEG_X[1] - .0075), "y": (J["toe_tip_y"], J["ball_y"]), "z": (0.0, .055)}
SPINE = {"pelvis": LEG_TOP, "spine_01": LEG_TOP + 1.7 * V, "spine_02": LEG_TOP + 5.4 * V,
         "neck_01": TORSO_TOP - .045, "head": TORSO_TOP}

# ------------------------------------------------------------------ skeleton (deform/export)
# name: (parent, head, tail, roll_axis) ; left bones listed, right ones mirror. roll_axis = world vector for local Z.
FWD = (0.0, -1.0, 0.0)
UP = (0.0, 0.0, 1.0)


def _skeleton():
    s = {}
    s["root"] = (None, (0, 0, 0), (0, .25, 0), UP)
    s["pelvis"] = ("root", (0, 0, SPINE["pelvis"]), (0, 0, SPINE["spine_01"]), FWD)
    s["spine_01"] = ("pelvis", (0, 0, SPINE["spine_01"]), (0, 0, SPINE["spine_02"]), FWD)
    s["spine_02"] = ("spine_01", (0, 0, SPINE["spine_02"]), (0, 0, SPINE["neck_01"]), FWD)
    s["neck_01"] = ("spine_02", (0, 0, SPINE["neck_01"]), (0, 0, SPINE["head"]), FWD)
    s["head"] = ("neck_01", (0, 0, SPINE["head"]), (0, 0, HEIGHT), FWD)
    sh, el, wr = J["shoulder"], J["elbow"], J["wrist"]
    s["clavicle_l"] = ("spine_02", (.04, 0, sh[2] + .025), sh, FWD)
    s["upperarm_l"] = ("clavicle_l", sh, el, FWD)
    s["lowerarm_l"] = ("upperarm_l", el, wr, FWD)
    s["hand_l"] = ("lowerarm_l", wr, (wr[0], 0, PALM["z"][0] if HAND_STYLE == "blocky" else LEG_TOP), FWD)
    k = KNUCKLE
    MED = (-1.0, 0.0, 0.0)  # local Z points medially, so +X rotation curls toward the palm on both hands
    if HAND_STYLE == "pixel":
        kz = LEG_TOP + 1.5 * V                        # knuckle line: the arm's last 1.5 px rows are the fingers
        mz = (kz + LEG_TOP) / 2
        s["fingers_01_l"] = ("hand_l", (ARM_X[0], 0, kz), (ARM_X[0], 0, mz), MED)
        s["fingers_02_l"] = ("fingers_01_l", (ARM_X[0], 0, mz), (ARM_X[0], 0, LEG_TOP), MED)
    if HAND_STYLE == "blocky":
        s["fingers_01_l"] = ("hand_l", k, (k[0], 0, k[2] - FINGER["l1"]), MED)
        s["fingers_02_l"] = ("fingers_01_l", (k[0], 0, k[2] - FINGER["l1"]),
                             (k[0], 0, k[2] - FINGER["l1"] - FINGER["l2"]), MED)
    tr = THUMB_ROOT
    t1 = (tr[0] - .004, tr[1] - .006, tr[2] - THUMB["l1"])
    t2 = (t1[0] - .003, t1[1] - .004, t1[2] - THUMB["l2"])
    if HAND_STYLE == "blocky":
        s["thumb_01_l"] = ("hand_l", tr, t1, MED)
        s["thumb_02_l"] = ("thumb_01_l", t1, t2, MED)
    hx = (LEG_X[0] + LEG_X[1]) / 2
    s["thigh_l"] = ("pelvis", (hx, 0, J["hip_z"]), (hx, J["knee_y"], J["knee_z"]), FWD)
    s["calf_l"] = ("thigh_l", (hx, J["knee_y"], J["knee_z"]), (hx, 0, J["ankle_z"]), FWD)
    s["foot_l"] = ("calf_l", (hx, 0, J["ankle_z"]), (hx, J["ball_y"], J["ball_z"]), UP)
    s["ball_l"] = ("foot_l", (hx, J["ball_y"], J["ball_z"]), (hx, J["toe_tip_y"], J["ball_z"]), UP)
    out = {}
    for n, (p, h, t, r) in s.items():
        out[n] = (p, h, t, r)
        if n.endswith("_l"):
            m = lambda v: (-v[0], v[1], v[2])
            out[n[:-2] + "_r"] = (p[:-2] + "_r" if p and p.endswith("_l") else p, m(h), m(t), m(r))
    return out


THUMB_ROOT = (PALM["x"][0] + .02, PALM["y"][0] - .012, PALM["z"][1] - .018)
SKELETON = _skeleton()
if HAND_STYLE == "pixel":
    GRIP_L = (ARM_X[0] - .022, 0.0, LEG_TOP + 1.5 * V + .02)   # handle against the inner face, on the folded bars
elif HAND_STYLE == "blocky":
    GRIP_L = (KNUCKLE[0] - .025, sum(FINGER["y"]) / 2, KNUCKLE[2] + .02)
else:
    GRIP_L = ((ARM_X[0] + ARM_X[1]) / 2, 0.0, LEG_TOP + .07)   # 7 cm above the arm's bottom face, box centre
BONE_ORDER = ["root", "pelvis", "spine_01", "spine_02", "neck_01", "head"] + \
    [f"{b}_{s}" for s in "lr" for b in ("clavicle", "upperarm", "lowerarm", "hand") +
     (("thumb_01", "thumb_02", "fingers_01", "fingers_02") if HAND_STYLE == "blocky" else
      ("fingers_01", "fingers_02") if HAND_STYLE == "pixel" else ())] + \
    [f"{b}_{s}" for s in "lr" for b in ("thigh", "calf", "foot", "ball")]
assert sorted(BONE_ORDER) == sorted(SKELETON), set(BONE_ORDER) ^ set(SKELETON)

LOWER_BODY = ["root", "pelvis"] + [f"{b}_{s}" for s in "lr" for b in ("thigh", "calf", "foot", "ball")]
UPPER_BODY = [b for b in BONE_ORDER if b not in LOWER_BODY]

# Mapping to the old runtime skeleton (UE5 Mannequin names used by Better Steve) where a counterpart exists.
OLD_BONE_MAP = {
    "root": "root", "pelvis": "pelvis", "spine_01": "spine_01 (+spine_02 partial)", "spine_02": "spine_03-05",
    "neck_01": "neck_01-02", "head": "head", "clavicle_l": "clavicle_l", "upperarm_l": "upperarm_l",
    "lowerarm_l": "lowerarm_l", "hand_l": "hand_l", "thigh_l": "thigh_l", "calf_l": "calf_l", "foot_l": "foot_l",
    "ball_l": "ball_l",
}

# ------------------------------------------------------------------ clips
CLIP_PREFIX = "UP_"


def clip_table():
    """name -> spec. frames = N (keys 0..N, duration N/60 s)."""
    t = {}
    t["Idle"] = dict(family="Idle", frames=120, loop=True, old="BS_Idle")
    for d in DIRS:
        t[f"Walk_{d}"] = dict(family="Walk", dir=d, frames=36, loop=True, old=f"BS_Walk_{d}")
        t[f"Sprint_{d}"] = dict(family="Sprint", dir=d, frames=32, loop=True, old=f"BS_Sprint_{d}")
        t[f"Sneak_{d}"] = dict(family="Sneak", dir=d, frames=54, loop=True, old=f"BS_CrouchWalk_{d}",
                               brief_frames=64)
    t["SneakIdle"] = dict(family="SneakIdle", frames=120, loop=True, old="BS_CrouchIdle")
    t["StandToSneak"] = dict(family="Transition", frames=16, loop=False, old="BS_StandToCrouch")
    t["SneakToStand"] = dict(family="Transition", frames=16, loop=False, old="BS_CrouchToStand")
    t["Jump_Start"] = dict(family="Air", frames=18, loop=False, old="BS_Jump_Start")
    t["Jump_Rise"] = dict(family="Air", frames=18, loop=False, hold=True, old="(new; BS_Jump_Loop covered rise+fall)")
    t["Jump_Fall"] = dict(family="Air", frames=48, loop=True, old="BS_Jump_Loop")
    t["Land_Soft"] = dict(family="Air", frames=12, loop=False, old="BS_Jump_Land (soft half)")
    t["Land_Firm"] = dict(family="Air", frames=24, loop=False, old="BS_Jump_Land")
    for stance, pre in (("Stand", "Turn"), ("Sneak", "SneakTurn")):
        for side in "LR":
            t[f"{pre}_{side}45"] = dict(family="Turn", stance=stance, side=side, deg=45, frames=18, loop=False)
            t[f"{pre}_{side}90"] = dict(family="Turn", stance=stance, side=side, deg=90, frames=30, loop=False)
    for n in ("DL", "D", "DR", "L", "C", "R", "UL", "U", "UR"):
        t[f"Look_{n}"] = dict(family="Look", frames=1, loop=False, old=f"BS_Look_{n}")
        t[f"SneakLook_{n}"] = dict(family="LookSneak", frames=1, loop=False)
    t["Pickaxe_Hold"] = dict(family="Pickaxe", frames=1, loop=False, old="BS_Hold_Pickaxe (pose)")
    for k in "ABC":
        t[f"Pickaxe_Strike{k}"] = dict(family="Pickaxe", frames=18, loop=False, contact_frame=9,
                                       old="BS_Mine (single swing)")
    t["Low_Pose"] = dict(family="Placeholder", frames=1, loop=False, placeholder=True)
    t["Flight_Placeholder"] = dict(family="Placeholder", frames=60, loop=True, placeholder=True)
    t["Death_Placeholder"] = dict(family="Placeholder", frames=30, loop=False, placeholder=True)
    return t


CLIPS = clip_table()
