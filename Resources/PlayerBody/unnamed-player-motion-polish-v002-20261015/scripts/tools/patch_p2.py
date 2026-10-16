"""One-off patch: ball-roll pivot on the ball joint; hip solver constrains stance legs (swing legs at full length)."""
import os

S = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def patch(name, pairs):
    p = os.path.join(S, name)
    s = open(p, encoding="utf-8").read()
    for old, new in pairs:
        if old not in s:
            raise SystemExit(f"[patch] {name}: missing {old[:70]!r}")
        s = s.replace(old, new)
    open(p, "w", encoding="utf-8").write(s)
    print("[patch]", name, len(pairs))


patch("up_build.py", [
    ('''        nb("c_ball_roll" + S, (hx, C.J["ball_y"], 0), (hx, C.J["ball_y"] + .06, 0), "c_tip_roll" + S, (0, 0, 1))''',
     '''        # ball roll pivots on the toe hinge itself (the ball joint), so the flat toe segment never slides
        nb("c_ball_roll" + S, (hx, C.J["ball_y"], C.J["ball_z"]), (hx, C.J["ball_y"] + .06, C.J["ball_z"]),
           "c_tip_roll" + S, (0, 0, 1))'''),
])
patch("up_loco.py", [
    ('''    ball_p = Vector((hx, C.J["ball_y"], 0))''', '''    ball_p = Vector((hx, C.J["ball_y"], C.J["ball_z"]))'''),
    ('''    L = leg_len() * gait.g["hip_margin"]
    lo, hi = .25, nominal''', '''    L = leg_len() * gait.g["hip_margin"]
    Lsw = leg_len() * .995
    lo, hi = .25, nominal'''),
    ('''                st = gait.foot(side, phi)
                if (gait.hip_joint(side, phi, mid) - ankle_from_foot(rig, st, side)).length > L:
                    ok = False
                    break''', '''                st = gait.foot(side, phi)
                lim = L if st["stance"] else Lsw
                if (gait.hip_joint(side, phi, mid) - ankle_from_foot(rig, st, side)).length > lim:
                    ok = False
                    break'''),
])
