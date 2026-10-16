"""One-off patch: strike redesign (eye-level stone, lower ready pose, B wind-up in front of the left shoulder),
wrist-angle metric."""
import os

S = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = os.path.join(S, "up_pickaxe.py")
s = open(p, encoding="utf-8").read()
i0 = s.index("TARGET = Vector(")
i1 = s.index("CONTACT_FRAME = 9")
s = s[:i0] + '''TARGET = Vector((-.05, -.80, 1.25))          # contact on the near face of an eye-level stone block (z 1..2)
STONE = {"center": (-.05, -1.30, 1.5), "size": 1.0}
''' + s[i1:]
i0 = s.index("LINK = dict(")
i1 = s.index("def hold_pose(")
s = s[:i0] + '''LINK = dict(grip=(-.30, -.25, .92), handle=(.05, -.6, .8), tip=(0, -.6, -.8),
            body=dict(twist=-6, lean=2, look_down=4, clav_r=0, larm=(-12, 8, -35), elbow_pole_r=(-.75, .3, .8)))

STRIKES = {
    "A": [  # diagonal shoulder-led chop: wind up above the right shoulder, cut down across to the stone
        (3, dict(grip=(-.32, -.02, 1.36), handle=(-.2, .55, .81), tip=(.1, -.4, .9),
                 body=dict(twist=-22, lean=-5, side_bend=-4, look_down=0, clav_r=10, larm=(-38, 20, -40),
                           elbow_pole_r=(-.85, .25, 1.45)))),
        (6, dict(grip=(-.22, -.32, 1.42), handle=(.05, -.2, .98), tip=(.05, -1, 0),
                 body=dict(twist=-4, lean=6, look_down=2, clav_r=6, larm=(-20, 14, -38), elbow_pole_r=(-.85, .2, 1.2)))),
        (9, dict(contact=True, tip_dir=(.12, -.6, -.79), handle_hint=(.1, -.75, .6),
                 body=dict(twist=14, lean=14, side_bend=5, look_down=4, clav_r=0, larm=(10, 12, -30),
                           elbow_pole_r=(-.6, .3, .8)))),
        (12, dict(rebound=(0.0, .04, .06), rot_back=14,
                  body=dict(twist=10, lean=11, side_bend=3, look_down=4, larm=(4, 10, -32), elbow_pole_r=(-.65, .3, .8)))),
        (15, dict(grip=(-.28, -.28, 1.0), handle=(.05, -.45, .89), tip=(0, -.85, -.5),
                  body=dict(twist=0, lean=5, look_down=4, larm=(-6, 8, -34), elbow_pole_r=(-.72, .3, .8)))),
    ],
    "B": [  # opposite diagonal, two-handed: pick across in front of the left shoulder, chop down-right, unwind right
        (3, dict(grip=(.05, -.35, 1.2), handle=(.55, .05, .83), tip=(-.35, -.6, .7), support=1.0,
                 body=dict(twist=18, lean=-2, side_bend=4, look_down=0, clav_r=2, elbow_pole_r=(-.6, .3, .7),
                           elbow_pole_l=(.75, .25, .8)))),
        (6, dict(grip=(-.05, -.40, 1.36), handle=(.3, -.25, .92), tip=(-.15, -1, .1), support=1.0,
                 body=dict(twist=8, lean=6, look_down=2, clav_r=2, elbow_pole_r=(-.65, .25, .9),
                           elbow_pole_l=(.75, .25, .85)))),
        (9, dict(contact=True, tip_dir=(-.22, -.6, -.77), handle_hint=(-.18, -.75, .6), support=1.0,
                 body=dict(twist=-10, lean=15, side_bend=-4, look_down=4, clav_r=0, elbow_pole_r=(-.6, .3, .8),
                           elbow_pole_l=(.6, .3, .75)))),
        (12, dict(rebound=(0.0, .04, .06), rot_back=12, support=1.0,
                  body=dict(twist=-12, lean=12, side_bend=-3, look_down=4, elbow_pole_r=(-.6, .3, .8),
                            elbow_pole_l=(.6, .3, .8)))),
        (15, dict(grip=(-.3, -.26, 1.0), handle=(-.05, -.45, .89), tip=(0, -.85, -.5), support=0.0,
                  body=dict(twist=-10, lean=5, look_down=4, larm=(-8, 10, -36), elbow_pole_r=(-.72, .3, .8)))),
    ],
    "C": [  # compact chip: short wind-up, flatter arc, sharp rebound
        (3, dict(grip=(-.28, -.20, 1.10), handle=(0.0, -.15, .99), tip=(0, -1, .15),
                 body=dict(twist=-10, lean=0, look_down=2, clav_r=4, larm=(-16, 10, -36), elbow_pole_r=(-.8, .3, 1.0)))),
        (6, dict(grip=(-.20, -.30, 1.10), handle=(.05, -.5, .86), tip=(.02, -.9, -.4),
                 body=dict(twist=-2, lean=6, look_down=3, larm=(-10, 10, -36), elbow_pole_r=(-.75, .3, .9)))),
        (9, dict(contact=True, tip_dir=(.03, -.8, -.6), handle_hint=(.05, -.6, .8),
                 body=dict(twist=6, lean=9, look_down=4, larm=(0, 10, -34), elbow_pole_r=(-.65, .3, .8)))),
        (11, dict(rebound=(0.0, .03, .05), rot_back=20,
                  body=dict(twist=4, lean=7, look_down=4, larm=(-2, 10, -34), elbow_pole_r=(-.68, .3, .8)))),
        (15, dict(grip=(-.28, -.27, .98), handle=(.05, -.55, .83), tip=(0, -.8, -.6),
                  body=dict(twist=-3, lean=3, look_down=4, larm=(-10, 8, -35), elbow_pole_r=(-.74, .3, .8)))),
    ],
}


''' + s[i1:]
open(p, "w", encoding="utf-8").write(s)

p = os.path.join(S, "s06_pickaxe.py")
s = open(p, encoding="utf-8").read()
old = '''        ps = Vd.PartSet(body)
        arm_pen = max(arm_pen, ps.penetration("arm_l", "torso"), ps.penetration("arm_r", "torso"))'''
new = '''        ps = Vd.PartSet(body)
        arm_pen = max(arm_pen, ps.penetration("arm_l", "torso"), ps.penetration("arm_r", "torso"))
        P = skel.pose.bones
        for s_ in "rl":
            rel = (P["lowerarm_" + s_].matrix.to_quaternion().inverted() @ P["hand_" + s_].matrix.to_quaternion())
            rest = (skel.data.bones["lowerarm_" + s_].matrix_local.to_quaternion().inverted() @
                    skel.data.bones["hand_" + s_].matrix_local.to_quaternion())
            ang = math.degrees((rest.inverted() @ rel).angle)
            wrist[s_] = max(wrist.get(s_, 0.0), ang)'''
assert old in s
s = s.replace(old, new)
s = s.replace('''    path, pen_max, body_pen, arm_pen = [], 0.0, {}, 0.0''', '''    path, pen_max, body_pen, arm_pen, wrist = [], 0.0, {}, 0.0, {}''')
s = s.replace('''              "max_arm_in_torso_cm": round(arm_pen * 100, 2), "tip_path_m": path})''',
              '''              "max_arm_in_torso_cm": round(arm_pen * 100, 2),
              "max_wrist_angle_deg": {k_: round(v_, 1) for k_, v_ in wrist.items()}, "tip_path_m": path})''')
s = s.replace('''tgt = Vector((-.1, -.35, 1.0))
CAM3 = (tgt + Vector((-2.3, -1.5, .55)), tgt)''', '''tgt = Vector((-.1, -.4, 1.15))
CAM3 = (tgt + Vector((-2.4, -1.2, .35)), tgt)''')
open(p, "w", encoding="utf-8").write(s)
print("[patch] strikes v2")
