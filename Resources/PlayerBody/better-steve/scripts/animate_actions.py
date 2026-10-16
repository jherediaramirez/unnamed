"""Better Steve actions: mining swing with a stand-in pickaxe, placing a block, swimming.

    blender -b foundry/BetterSteve.blend --python scripts/animate_actions.py -- <project root> [--no-export]

Run after animate_locomotion.py (it reuses that script's posture spec and bake). Adds these actions (earlier copies are
replaced) and the stand-in pickaxe, saves the .blend, then exports SK_BetterSteve.fbx/.glb and SM_StandInPickaxe.fbx:

    BS_Hold_Pickaxe     60 f loop: right fist around the handle, head forward and up (right-arm overlay)
    BS_Mine_Swing       9 f = the game's 6-tick swing (0.3 s); the pick bites on frames 4-5, around tick 3 (0.15 s);
                        frame 9 == frame 0 == BS_Hold_Pickaxe, so held mining loops it back to back
    BS_Mine_Swing_Up / _Down   the same swing aimed at pitch +60 / -60 (blend the three by the view pitch)
    BS_Place            6 f = the game's held-use repeat (4 ticks): reach forward and set the block
    BS_Swim_Tread       48 f loop: upright, sculling hands, bicycle legs
    BS_Swim_F           30 f loop: breaststroke at 1.96 blocks/s, body pitched 45 degrees (fits the upright hitbox)
    BS_Swim_Sprint      40 f loop: front crawl at 3.92 blocks/s, body horizontal like vanilla's swimming pose (0.6 block
                        box, body centre 0.3 above the root); two stroke cycles, a breath to the right on the second

The swing chains the joints like a real strike: the torso uncoils first, the shoulder drives, the elbow whips and the
wrist snaps last, all arriving on the hit; the pick then bounces off and the arm settles back into the hold.
Swim arms are posed by the arm IK (hand paths, elbow poles, palm directions) and baked back to FK; FK legs are
handed to the leg IK controls. The rig's IK switches are never keyed, so every clip exports the same way.
"""

import bpy
import math
import os
import sys

from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import animate_locomotion as al  # noqa: E402  (imports build_better_steve too; both main()s are guarded)

bs = al.bs
arm, PB, SIDES = al.arm, al.PB, al.SIDES
X_, Y_, Z_ = "X", "Y", "Z"
FPS = al.FPS

# Unnamed's swimming (FoundationProfile.h): steady 1.96 blocks/s in water, 3.92 sprinting with the eyes under.
SWIM, SWIM_SPRINT = 1.96, 3.92
SWING_N, CONTACT = 9, 4     # 6 ticks at 20 Hz = 0.3 s = 9 frames; the hit tick (3) falls between frames 4 and 5
PLACE_N = 6                 # held use repeats every 4 ticks (FoundationInteraction.h)

EXTRA = dict(cl_up_l=0., cl_up_r=0., cl_fw_l=0., cl_fw_r=0., wf_l=0., wf_r=0., tw_l=0., tw_r=0., tilt=0., smile=0.,
             pel_roll=0.)
BASE = dict(al.STAND, **EXTRA)
IK_MISS = {}   # clip -> worst IK miss (m) while posing it


# ---------------------------------------------------------------- spec with clavicles, wrist flex, pelvis roll, face
def spec_x(P):
    s = al.spec(P)
    for side, sg in SIDES:
        # clavicle: + up = shrug, + fw = shoulder forward (protraction)
        s[f"clavicle_{side}"] = {"rot": [(Y_, -P[f"cl_up_{side}"] * sg), (Z_, -P[f"cl_fw_{side}"] * sg)]}
        # hand: tw + = turn the thumb side outward (about the forearm), wr + = toward the pinky, wf + = palm-ward
        s[f"hand_{side}"] = {"rot": [(Z_, P[f"tw_{side}"] * sg), (X_, P[f"wr_{side}"]), (Y_, P[f"wf_{side}"] * sg)]}
    if P["pel_roll"]:   # about the body's long axis once the pelvis is pitched flat (world Y)
        s["pelvis"]["rot"] = [(X_, P["pel_pitch"]), (Y_, P["pel_roll"]), (Z_, P["pel_yaw"])]
    s.update(bs.face(blink=P["blink"], jaw=P["jaw"], lid_down=P["lid"], brow_up=P["brow"], brow_tilt=P["tilt"],
                     smile=P["smile"], look=(P["look_x"], P["look_z"])))
    return s


# ---------------------------------------------------------------- curves
EASES = {"io": al.ease, "in": lambda t: t * t, "in3": lambda t: t ** 3, "out": lambda t: 1 - (1 - t) ** 2,
         "lin": lambda t: t}


def track(keys, f):
    """keys: [(frame, value, ease to the next key)]."""
    for (f0, a, e), (f1, b, _) in zip(keys, keys[1:]):
        if f <= f1:
            return al.lerp(a, b, EASES[e](min(1., max(0., (f - f0) / (f1 - f0)))))
    return keys[-1][1]


def grouped(base, groups, f):
    """Each group of params follows its own keys [(frame, pose dict, ease)], so joints can lead or lag."""
    P = dict(base)
    for names, keys in groups:
        for n in names:
            P[n] = track([(t, K[n], e) for t, K, e in keys], f)
    return P


def loop_curve(keys, u):
    """Periodic cubic through [(u, value[, hold])] on [0, 1), first key at 0. Tangents follow the neighbouring keys
    (scaled by the real spacing), so motion stays smooth; a held key has zero velocity."""
    T = [k[0] for k in keys]
    V = [k[1] for k in keys]
    H = [len(k) > 2 and k[2] for k in keys]
    n = len(T)
    Tx, Vx, Hx = [T[-1] - 1] + T + [T[0] + 1, T[1] + 1], [V[-1]] + V + [V[0], V[1]], [H[-1]] + H + [H[0], H[1]]
    u %= 1.
    j = max(i for i in range(n) if T[i] <= u) + 1
    h = Tx[j + 1] - Tx[j]

    def tangent(i):
        return 0 * Vx[i] if Hx[i] else (Vx[i + 1] - Vx[i - 1]) * (h / (Tx[i + 1] - Tx[i - 1]))

    m0, m1 = tangent(j), tangent(j + 1)
    t = (u - Tx[j]) / h
    t2, t3 = t * t, t * t * t
    return Vx[j] * (2 * t3 - 3 * t2 + 1) + m0 * (t3 - 2 * t2 + t) + Vx[j + 1] * (3 * t2 - 2 * t3) + m1 * (t3 - t2)


def W(sx, v):
    """(out, forward, up) offsets for the side with sign sx (+1 left) -> world (Blender faces -Y)."""
    return Vector((sx * v[0], -v[1], v[2]))


def loop_vec(keys, u, sx):
    return loop_curve([(k[0], W(sx, k[1])) + tuple(k[2:]) for k in keys], u)


# ---------------------------------------------------------------- evaluating the rig (IK <-> FK)
def deg_rot(R):
    """World-axes rotation -> apply_pose rotation list (applied X, Y, Z = Euler XYZ)."""
    e = R.to_euler("XYZ")
    return [(X_, math.degrees(e.x)), (Y_, math.degrees(e.y)), (Z_, math.degrees(e.z))]


def evaluate(spec, ik_arms=(), fk_legs=()):
    root = PB["root"]
    for s, _ in SIDES:
        root[f"arm_ik_{s}"] = 1. if s in ik_arms else 0.
        root[f"leg_ik_{s}"] = 0. if s in fk_legs else 1.
    bs.apply_pose(arm, spec)
    arm.update_tag()
    bpy.context.view_layer.update()


def fk_rot(name, frame=None):
    """A bone's evaluated pose (or the given world frame) as apply_pose's rotation relative to its parent."""
    pb = PB[name]
    par = pb.parent
    rest_rel = par.bone.matrix_local.to_3x3().inverted() @ pb.bone.matrix_local.to_3x3()
    basis = (par.matrix.to_3x3() @ rest_rel).inverted() @ (pb.matrix.to_3x3() if frame is None else frame)
    r = bs.rest3(pb)
    return deg_rot(r @ basis @ r.inverted())


def hand_frame(fingers, palm):
    """World frame of a hand bone: Y along the fingers, Z out of the palm (both hands)."""
    y = fingers.normalized()
    z = (palm - y * palm.dot(y)).normalized()
    return Matrix((y.cross(z), y, z)).transposed()


def miss(name, d):
    IK_MISS[name] = max(IK_MISS.get(name, 0.), d)


def solve_arms(clip, spec, want_fn):
    """want_fn(shoulders) -> {side: (wrist, elbow pole, palm normal)} in world space. The arm IK poses the arm, then
    the solved upper arm and forearm (and a hand turned so its palm faces `palm`, wrist straight) are returned as FK."""
    evaluate(spec)
    want = want_fn({s: PB[f"upperarm_{s}"].head.copy() for s, _ in SIDES})
    s = dict(spec)
    for side, (wrist, pole, _) in want.items():
        s[f"CTRL_hand_ik_{side}"] = {"loc": tuple(wrist - PB[f"CTRL_hand_ik_{side}"].bone.head_local)}
        s[f"CTRL_elbow_pole_{side}"] = {"loc": tuple(pole - PB[f"CTRL_elbow_pole_{side}"].bone.head_local)}
    evaluate(s, ik_arms=want)
    out = dict(spec)
    for side, (wrist, pole, palm) in want.items():
        lo = PB[f"lowerarm_{side}"]
        miss(clip, (lo.tail - wrist).length)
        out[f"upperarm_{side}"] = {"rot": fk_rot(f"upperarm_{side}")}
        out[f"lowerarm_{side}"] = {"rot": fk_rot(f"lowerarm_{side}")}
        out[f"hand_{side}"] = {"rot": fk_rot(f"hand_{side}", hand_frame(lo.tail - lo.head, palm))}
    evaluate({})
    return out


def legs_from_fk(clip, spec, legs):
    """legs: {side: {"thigh": rot, "calf": rot, "foot": rot}} posed as FK; returns spec with the leg IK controls put
    where FK puts the foot (and the knee pole in front of the FK knee), so the IK legs reproduce the FK pose."""
    s = dict(spec)
    for side, d in legs.items():
        for b, rot in d.items():
            s[f"{b}_{side}"] = {"rot": rot}
    evaluate(s, fk_legs=legs)
    out, fk = dict(spec), {}
    for side in legs:
        foot, calf, thigh = PB[f"foot_{side}"], PB[f"calf_{side}"], PB[f"thigh_{side}"]
        ctrl = PB[f"CTRL_foot_ik_{side}"]
        fwd = thigh.matrix.to_3x3() @ bs.rest3(thigh).inverted() @ Vector((0, -.5, 0))   # the thigh's front
        fk[side] = calf.head.copy()
        out[f"CTRL_foot_ik_{side}"] = {"loc": tuple(foot.head - ctrl.bone.head_local),
                                       "rot": deg_rot(foot.matrix.to_3x3() @ bs.rest3(ctrl).inverted())}
        out[f"CTRL_knee_pole_{side}"] = {"loc": tuple(calf.head + fwd - PB[f"CTRL_knee_pole_{side}"].bone.head_local)}
    evaluate(out)   # leg IK on: the knee must land where FK had it
    for side in legs:
        miss(clip, (PB[f"calf_{side}"].head - fk[side]).length)
    evaluate({})
    return out


def leg_rot(sg, th, abd, calf, ft, turn):
    """FK leg angles: th + = thigh back (hip extension), abd + = out, calf + = knee bend, ft + = toes down, turn + = out."""
    return {"thigh": [(Y_, -abd * sg), (X_, th)], "calf": [(X_, calf)], "foot": [(X_, ft), (Z_, turn * sg)]}


# ---------------------------------------------------------------- the stand-in pickaxe
GRIP = PB["hand_r"].bone.tail_local + Vector((.027, 0, .0175))   # handle centre in the closed right fist
HEAD_Y = GRIP.y - .50                                              # head centre; the handle runs along -Y at rest
TIP = Vector((GRIP.x, HEAD_Y + .016, GRIP.z - .135))               # the tip that leads the swing (lower one at rest)
PROP = "SM_StandInPickaxe"


def build_pickaxe():
    """Pixel-style iron pickaxe in rest world space, bone-parented to hand_r: the handle runs through the fist along
    the hand's local X (world -Y at rest), the head forward, the blade in the swing plane."""
    old = bpy.data.objects.get(PROP)
    if old:
        me = old.data
        bpy.data.objects.remove(old)
        bpy.data.meshes.remove(me)
    b = bs.Builder()
    w = {"hand_r": 1}
    gx, gy, gz = GRIP
    b.box(gx - .008, gx + .008, gy - .515, gy + .075, gz - .008, gz + .008, w, "wood")            # handle
    b.box(gx - .0095, gx + .0095, gy + .035, gy + .075, gz - .0095, gz + .0095, w, "strap")      # grip wrap
    b.box(gx - .0105, gx + .0105, HEAD_Y + .016, HEAD_Y + .03, gz - .0105, gz + .0105, w, "iron")  # collar
    b.box(gx - .012, gx + .012, HEAD_Y - .017, HEAD_Y + .017, gz - .024, gz + .024, w, "iron")    # hub
    for s in (1, -1):   # each arm of the head steps back toward the handle, like the sprite's crescent
        for z0, z1, dy, hw in ((.024, .07, 0., .013), (.07, .105, .007, .011), (.105, .135, .016, .008)):
            b.box(gx - hw + .002, gx + hw - .002, HEAD_Y + dy - hw, HEAD_Y + dy + hw, *sorted((gz + s * z0, gz + s * z1)),
                  w, "iron")
    obj = bs.mesh_from_builder(b, PROP, bpy.data.objects["SK_BetterSteve"].data.materials[0],
                               bpy.data.objects["SK_BetterSteve"].users_collection[0])
    arm.animation_data_create().action = None
    evaluate({})
    obj.parent, obj.parent_type, obj.parent_bone = arm, "BONE", "hand_r"
    bpy.context.view_layer.update()
    obj.matrix_world = Matrix.Identity(4)
    bpy.context.view_layer.update()
    inv = obj.matrix_world.inverted()
    obj["grip_local"], obj["tip_local"] = tuple(inv @ GRIP), tuple(inv @ TIP)
    obj["grip_rest"], obj["tip_rest"] = tuple(GRIP), tuple(TIP)
    obj.hide_select = True
    return obj


def export_prop(export_dir):
    """SM_StandInPickaxe.fbx: centimetres, pivot on the grip, axes as at rest (handle toward Unreal +Y). In Unreal,
    attach it to a hand_r socket at the grip whose rotation undoes hand_r's reference rotation (see README)."""
    scene = bpy.context.scene
    src = bpy.data.objects[PROP]
    src.name = PROP + "_src"   # the export copy takes the name Unreal should see
    me = src.data.copy()
    me.transform(Matrix.Scale(100, 4) @ Matrix.Translation(-GRIP))
    tmp = bpy.data.objects.new(PROP, me)
    scene.collection.objects.link(tmp)
    scale = scene.unit_settings.scale_length
    scene.unit_settings.scale_length = .01
    bpy.ops.object.select_all(action="DESELECT")
    tmp.select_set(True)
    bpy.context.view_layer.objects.active = tmp
    path = os.path.join(export_dir, PROP + ".fbx")
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"MESH"}, apply_unit_scale=True,
                             apply_scale_options="FBX_SCALE_NONE", global_scale=1., axis_forward="-Z", axis_up="Y",
                             mesh_smooth_type="FACE", bake_anim=False, path_mode="COPY", embed_textures=True)
    scene.unit_settings.scale_length = scale
    bpy.data.objects.remove(tmp)
    bpy.data.meshes.remove(me)
    src.name = PROP
    print(f"[export] {path}")


# ---------------------------------------------------------------- mining swing
HOLD = dict(BASE, ua_r=-12., abd_r=8., el_r=52., wr_r=14., curl_r=90., ua_l=-2., abd_l=6., el_l=16., curl_l=18.)


def swing_groups(pitch):
    """Key poses for a swing aimed at the view pitch (+ up), and when each joint group hits them."""
    p = pitch / 60.
    turn = -(.8 if p > 0 else .95) * pitch          # the arm's whole arc turns with the view about the shoulder
    lean = -2. * p if p > 0 else -6. * p             # per spine bone: flex to reach down, extend a little for up
    # windup: the arm out to the side, the hand cocked beside the ear; the forearm turns out (tw) so the pick hangs
    # behind the shoulder instead of swinging in behind the head. Low and high swings still wind up high.
    WIND = dict(HOLD, ua_r=max(-175., -120. + .4 * turn), abd_r=32., el_r=78., wr_r=-18., tw_r=35.,
                cl_up_r=12. + 6 * max(p, 0),
                cl_fw_r=-6., sp1=-2., sp2=-3., sp3=-3., sp1_yaw=-4., sp2_yaw=-6., sp3_yaw=-8., ua_l=-28., abd_l=14.,
                el_l=45., curl_l=30., cl_fw_l=4., brow=.003)
    HIT = dict(HOLD, ua_r=-76. + turn, abd_r=8., el_r=18., wr_r=6., curl_r=96., cl_up_r=-2. + 10 * max(p, 0),
               cl_fw_r=10., sp1=4. + lean, sp2=5. + lean, sp3=6. + lean, sp1_yaw=3., sp2_yaw=5., sp3_yaw=6., ua_l=14.,
               abd_l=10., el_l=32., curl_l=45., cl_fw_l=-3., tilt=8., lid=.12)
    BOUNCE = dict(HIT, ua_r=HIT["ua_r"] - 2, el_r=HIT["el_r"] + 5, wr_r=HIT["wr_r"] - 5)   # the pick bounces off
    FOLLOW = dict(HIT, sp1=HIT["sp1"] + 1, sp2=HIT["sp2"] + 1.5, sp3=HIT["sp3"] + 1.5, sp1_yaw=4., sp2_yaw=6.,
                  sp3_yaw=7.)
    arm_end = [(CONTACT, HIT, "lin"), (CONTACT + 1, BOUNCE, "io"), (SWING_N, HOLD, "io")]
    return [   # the torso leads; shoulder, elbow and wrist leave the windup later and later and all land on the hit
        (("sp1", "sp2", "sp3", "sp1_yaw", "sp2_yaw", "sp3_yaw"),
         [(0, HOLD, "io"), (1.3, WIND, "io"), (5, FOLLOW, "io"), (SWING_N, HOLD, "io")]),
        (("ua_r", "abd_r", "cl_up_r", "cl_fw_r"), [(0, HOLD, "io"), (1.6, WIND, "io")] + arm_end),
        (("el_r",), [(0, HOLD, "io"), (2., WIND, "in")] + arm_end),
        (("wr_r", "curl_r"), [(0, HOLD, "io"), (2.4, WIND, "in")] + arm_end),
        (("tw_r",), [(0, HOLD, "io"), (1.2, WIND, "io"), (2.6, WIND, "in")] + arm_end),   # turns out before the lift
        (("ua_l", "abd_l", "el_l", "curl_l", "cl_fw_l"),
         [(0, HOLD, "io"), (2, WIND, "io"), (4.2, HIT, "io"), (SWING_N, HOLD, "io")]),
        (("brow", "tilt", "lid"), [(0, HOLD, "io"), (2, WIND, "io"), (CONTACT, HIT, "io"), (6, HIT, "io"),
                                   (SWING_N, HOLD, "io")]),
    ]


def swing(pitch):
    groups = swing_groups(pitch)

    def pose(f):
        P = grouped(HOLD, groups, f)
        P["head_yaw"] = -.9 * (P["sp1_yaw"] + P["sp2_yaw"] + P["sp3_yaw"])   # keep looking at the block
        return spec_x(al.level(P))
    return pose


def hold(k, n=60):
    br = math.sin(2 * math.pi * k / n)
    P = dict(HOLD, sp2=-1. * br, sp3=-.7 * br)
    P["ua_r"] += 1.5 * br
    P["wr_r"] += 2. * math.sin(2 * math.pi * k / n - 1.)   # the head bobs a little behind the breath
    P["ua_l"] += 1.2 * br
    t = k - 40
    P["blink"] = t / 2 if 0 <= t <= 2 else 1 if 2 < t <= 3 else 1 - (t - 3) / 3 if 3 < t <= 6 else 0
    return spec_x(al.level(P))


# ---------------------------------------------------------------- place
REACH = dict(BASE, ua_r=-72., abd_r=3., el_r=20., wr_r=12., curl_r=10., cl_fw_r=9., cl_up_r=2., sp2=2., sp3=3.,
             sp2_yaw=3., sp3_yaw=4., ua_l=6., el_l=14.)
PLACE_GROUPS = [
    (("sp2", "sp3", "sp2_yaw", "sp3_yaw"), [(0, BASE, "io"), (2.2, REACH, "io"), (PLACE_N, BASE, "io")]),
    (("ua_r", "abd_r", "cl_fw_r", "cl_up_r"), [(0, BASE, "out"), (2.4, REACH, "io"), (PLACE_N, BASE, "io")]),
    (("el_r",), [(0, BASE, "io"), (1.2, dict(BASE, el_r=58.), "out"), (2.6, REACH, "io"), (PLACE_N, BASE, "io")]),
    (("wr_r",), [(0, BASE, "io"), (1.6, dict(BASE, wr_r=-10.), "in"), (2.8, REACH, "io"), (PLACE_N, BASE, "io")]),
    (("curl_r",), [(0, BASE, "io"), (1.3, dict(BASE, curl_r=38.), "io"), (3., REACH, "io"), (PLACE_N, BASE, "io")]),
    (("ua_l", "el_l"), [(0, BASE, "io"), (2.5, REACH, "io"), (PLACE_N, BASE, "io")]),
]


def place(f):
    P = grouped(BASE, PLACE_GROUPS, f)
    P["head_yaw"] = -.9 * (P["sp2_yaw"] + P["sp3_yaw"])
    return spec_x(al.level(P))


# ---------------------------------------------------------------- swimming
def tread(k, n=48):
    a = 2 * math.pi * k / n
    P = dict(BASE, pel=(0., .02, -.05 + .012 * math.cos(2 * a)), sp1=3., sp2=2., sp3=1., neck=-2.)
    al.level(P, nod=-5.)
    for s, sx in SIDES:
        b = 2 * a + (0. if s == "l" else math.pi)   # two leg circles per loop, the legs half a circle apart
        P[f"foot_{s}"] = (sx * .035, -.07 + .085 * math.cos(b), .30 + .085 * math.sin(b))
        P[f"pitch_{s}"] = 28 + 10 * math.sin(b)
        P[f"pole_{s}"] = (sx * .2, -.15, .1)
        P[f"curl_{s}"] = 15.

    def want(sh):   # both hands scull in and out together at belly height, palms leading the sweep
        sw, v = math.sin(2 * a), math.cos(2 * a)
        return {s: (sh[s] + W(sx, (.10 + .14 * sw, .30, -.33)), sh[s] + W(sx, (.5, -.25, -.25)),
                    W(sx, (.5 * v, 0, -.85))) for s, sx in SIDES}
    return solve_arms("BS_Swim_Tread", spec_x(P), want)


# breaststroke, (out, forward, up) from each shoulder; u = 0 starts the outsweep from the glide
BREAST_HAND = [(0, (-.10, .50, .04), True), (.15, (.30, .40, -.02)), (.26, (.20, .18, -.28)), (.36, (-.06, -.04, -.22)),
               (.46, (-.08, .26, -.14)), (.58, (-.10, .50, .04), True)]
BREAST_POLE = [(0, (.25, .10, .30)), (.15, (.45, .10, .25)), (.28, (.45, -.05, .20)), (.38, (.35, -.25, -.15)),
               (.48, (.20, -.10, -.35)), (.62, (.25, .05, .30))]
BREAST_PALM = [(0, (.3, 0, -1.)), (.15, (.9, -.2, -.4)), (.26, (-.5, -.7, -.5)), (.36, (-.8, 0, .4)),
               (.46, (-.6, 0, -.5)), (.58, (.3, 0, -1.))]
# legs: thigh (+ back), abduction, knee, toes (+ down), turn-out; glide, heels up, whip out and back, snap together
BREAST_LEGS = [(0, (14., 2., 6., 45., 0.), True), (.38, (14., 2., 6., 45., 0.), True), (.55, (-38., 22., 112., -12., 28.)),
               (.64, (-10., 34., 55., 10., 20.)), (.74, (16., 4., 4., 45., 0.), True)]


def breast(k, n=30):
    u = k / n
    lean = loop_curve([(0, 76.), (.28, 64.), (.45, 68.), (.72, 78.)], u)   # the chest rises in the pull
    P = dict(BASE, pel=(0., 0., .12), pel_pitch=.62 * lean, sp1=.14 * lean, sp2=.14 * lean, sp3=.1 * lean,
             curl_l=10., curl_r=10.)
    al.level(P, nod=loop_curve([(0, 14.), (.18, 2.), (.3, -12.), (.42, -8.), (.62, 14.)], u))   # lift to breathe
    P["jaw"] = .5 * math.exp(-((u - .33) / .06) ** 2)
    legs = {s: leg_rot(sg, *(loop_curve([(t, Vector(v)) + tuple(r) for t, v, *r in BREAST_LEGS], u)))
            for s, sg in SIDES}
    spec = legs_from_fk("BS_Swim_F", spec_x(P), legs)

    def want(sh):
        return {s: (sh[s] + loop_vec(BREAST_HAND, u, sx), sh[s] + loop_vec(BREAST_POLE, u, sx),
                    loop_vec(BREAST_PALM, u, sx)) for s, sx in SIDES}
    return solve_arms("BS_Swim_F", spec, want)


# front crawl, (out, forward, up) from the shoulder in world space (the body lies face down); u = 0 at hand entry
CRAWL_HAND = [(0, (.03, .50, -.08)), (.10, (.05, .44, -.24)), (.22, (-.05, .22, -.44)), (.34, (-.03, -.12, -.40)),
              (.45, (.06, -.42, -.20)), (.53, (.14, -.38, .04)), (.70, (.24, -.02, .24)), (.87, (.14, .34, .12))]
CRAWL_POLE = [(0, (.30, .05, .40)), (.12, (.50, .10, .20)), (.30, (.50, -.05, .15)), (.45, (.35, -.20, .30)),
              (.62, (.30, -.10, .55)), (.85, (.30, .15, .45))]
CRAWL_PALM = [(0, (.25, 0, -1.)), (.10, (0, .6, -.8)), (.22, (-.1, 1., -.2)), (.34, (.1, .8, .5)), (.50, (-.6, 0, .6)),
              (.70, (0, .7, .7)), (.87, (.3, -.2, -.9))]
CRAWL_CYCLE = 20   # frames per stroke cycle (each arm pulls once): the pull covers ~0.9 m in 7 frames, about 3.9 m/s


def crawl(k, n=40):
    ur = (k % CRAWL_CYCLE) / CRAWL_CYCLE
    roll = math.cos(2 * math.pi * (ur - .3))   # -1 = right side down, mid right pull
    breath = math.exp(-(min(abs(k - 34), n - abs(k - 34)) / 3.2) ** 2)   # second cycle, right arm recovering
    P = dict(BASE, pel=(0., .10, -.60), pel_pitch=90. + 2 * math.sin(4 * math.pi * ur), pel_roll=-25 * roll,
             curl_l=12., curl_r=12.)
    for i, w in ((1, .25), (2, .35), (3, .4)):
        P[f"sp{i}_yaw"] = 15 * w * roll          # shoulders roll further than the hips
    c = .9 * (-25 - 15) * roll                   # counter-roll that keeps the face down-forward
    P.update(neck=-15., head=-28., neck_yaw=.35 * c - 25 * breath, head_yaw=.5 * c - 55 * breath,
             jaw=.9 * breath, blink=0.)
    legs = {}
    for s, sg in SIDES:   # flutter kick: 4 per stroke cycle, legs opposite; knees bend into each downbeat
        ph = 2 * math.pi * k / 10 + (0. if s == "l" else math.pi)
        legs[s] = leg_rot(sg, 10 * math.sin(ph) - 2, -3., 6 + 14 * (1 + math.sin(ph + 1.)) / 2,
                          50 + 8 * math.sin(ph - .8), -6.)
    spec = legs_from_fk("BS_Swim_Sprint", spec_x(P), legs)

    def want(sh):
        return {s: (sh[s] + loop_vec(CRAWL_HAND, u, sx), sh[s] + loop_vec(CRAWL_POLE, u, sx),
                    loop_vec(CRAWL_PALM, u, sx))
                for (s, sx), u in zip(SIDES, ((ur + .5) % 1, ur))}
    return solve_arms("BS_Swim_Sprint", spec, want)


# ---------------------------------------------------------------- main
def main():
    arm.animation_data_create().action = None
    evaluate({})
    prop = build_pickaxe()
    print(f"[prop] {PROP}: grip {tuple(round(c, 4) for c in GRIP)}, tip {tuple(round(c, 4) for c in TIP)}, "
          f"{len(prop.data.vertices)} verts")
    pk = {"bs_prop": PROP, "bs_contact": CONTACT, "bs_hit_s": 3 / 20}
    al.bake("BS_Hold_Pickaxe", range(61), hold, True, "Action", {"bs_prop": PROP}, raw=True)
    for name, pitch in (("BS_Mine_Swing", 0), ("BS_Mine_Swing_Up", 60), ("BS_Mine_Swing_Down", -60)):
        al.bake(name, range(SWING_N + 1), swing(pitch), True, "Action", dict(pk, bs_pitch=float(pitch)), raw=True)
    al.bake("BS_Place", range(PLACE_N + 1), place, True, "Action", raw=True)
    al.bake("BS_Swim_Tread", range(49), tread, True, "Swim", {"bs_swim_speed": 0.}, raw=True)
    al.bake("BS_Swim_F", range(31), breast, True, "Swim", {"bs_swim_speed": SWIM}, raw=True)
    al.bake("BS_Swim_Sprint", range(41), crawl, True, "Swim", {"bs_swim_speed": SWIM_SPRINT, "bs_ground": 0},
            raw=True)
    for clip, d in sorted(IK_MISS.items()):
        print(f"[ik] {clip}: worst IK miss {d * 1000:.2f} mm")

    arm.animation_data.action = None
    evaluate({})
    bpy.ops.wm.save_mainfile(compress=True)
    print(f"[save] {bpy.data.filepath}")
    if "--no-export" not in sys.argv:
        import export_ue
        export_prop(bs.EXPORT)
        export_ue.export_all(bs.EXPORT)


if __name__ == "__main__":
    main()
