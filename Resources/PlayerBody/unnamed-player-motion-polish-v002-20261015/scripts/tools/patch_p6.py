"""One-off patch: strike refinements (twist at contact, C cock, B reach, recoil key at f10), metrics fixes."""
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


patch("up_pickaxe.py", [
    # A: more twist into contact so the right shoulder comes forward instead of the arm crossing the chest
    ('''        (9, dict(contact=True, tip_dir=(.12, -.6, -.79), handle_hint=(.1, -.75, .6),
                 body=dict(twist=14, lean=14, side_bend=5, look_down=4, clav_r=0, larm=(10, 12, -30),
                           elbow_pole_r=(-.6, .3, .8)))),''',
     '''        (9, dict(contact=True, tip_dir=(.12, -.6, -.79), handle_hint=(.1, -.75, .6),
                 body=dict(twist=24, lean=16, side_bend=5, look_down=4, clav_r=-4, larm=(14, 18, -30),
                           elbow_pole_r=(-.6, .3, .8)))),
        (10, dict(recoil=True, body=dict(twist=22, lean=15, side_bend=5, look_down=4, clav_r=-3, larm=(12, 17, -30),
                                         elbow_pole_r=(-.6, .3, .8)))),'''),
    ('''        (12, dict(rebound=(0.0, .04, .06), rot_back=14,
                  body=dict(twist=10, lean=11, side_bend=3, look_down=4, larm=(4, 10, -32), elbow_pole_r=(-.65, .3, .8)))),''',
     '''        (12, dict(rebound=(0.0, .04, .06), rot_back=14,
                  body=dict(twist=16, lean=11, side_bend=3, look_down=4, larm=(6, 14, -32), elbow_pole_r=(-.65, .3, .8)))),'''),
    # B: wind-up closer to the right shoulder's reach, more twist; recoil key
    ('''        (3, dict(grip=(.05, -.35, 1.2), handle=(.55, .05, .83), tip=(-.35, -.6, .7), support=1.0,
                 body=dict(twist=18,''',
     '''        (3, dict(grip=(-.02, -.30, 1.18), handle=(.55, .05, .83), tip=(-.35, -.6, .7), support=1.0,
                 body=dict(twist=26,'''),
    ('''        (9, dict(contact=True, tip_dir=(-.22, -.6, -.77), handle_hint=(-.18, -.75, .6), support=1.0,
                 body=dict(twist=-10, lean=15, side_bend=-4, look_down=4, clav_r=0, elbow_pole_r=(-.6, .3, .8),
                           elbow_pole_l=(.6, .3, .75)))),''',
     '''        (9, dict(contact=True, tip_dir=(-.22, -.6, -.77), handle_hint=(-.18, -.75, .6), support=1.0,
                 body=dict(twist=4, lean=15, side_bend=-4, look_down=4, clav_r=0, elbow_pole_r=(-.6, .3, .8),
                           elbow_pole_l=(.6, .3, .75)))),
        (10, dict(recoil=True, support=1.0, body=dict(twist=2, lean=14, side_bend=-4, look_down=4,
                                                      elbow_pole_r=(-.6, .3, .8), elbow_pole_l=(.6, .3, .75)))),'''),
    ('''        (12, dict(rebound=(0.0, .04, .06), rot_back=12, support=1.0,
                  body=dict(twist=-12, lean=12,''',
     '''        (12, dict(rebound=(0.0, .04, .06), rot_back=12, support=1.0,
                  body=dict(twist=-6, lean=12,'''),
    # C: cocked approach -> fast compact hit, sharp rebound; recoil key
    ('''        (6, dict(grip=(-.20, -.30, 1.10), handle=(.05, -.5, .86), tip=(.02, -.9, -.4),
                 body=dict(twist=-2, lean=6, look_down=3, larm=(-10, 10, -36), elbow_pole_r=(-.75, .3, .9)))),
        (9, dict(contact=True, tip_dir=(.03, -.8, -.6), handle_hint=(.05, -.6, .8),
                 body=dict(twist=6, lean=9, look_down=4, larm=(0, 10, -34), elbow_pole_r=(-.65, .3, .8)))),''',
     '''        (6, dict(grip=(-.24, -.18, 1.18), handle=(-.05, .1, .99), tip=(0, -1, .3),
                 body=dict(twist=-6, lean=3, look_down=3, larm=(-12, 10, -36), elbow_pole_r=(-.78, .3, 1.0)))),
        (9, dict(contact=True, tip_dir=(.03, -.8, -.6), handle_hint=(.05, -.6, .8),
                 body=dict(twist=14, lean=10, look_down=4, larm=(4, 12, -34), elbow_pole_r=(-.65, .3, .8)))),
        (10, dict(recoil=True, body=dict(twist=13, lean=9, look_down=4, larm=(3, 12, -34), elbow_pole_r=(-.65, .3, .8)))),'''),
    # recoil spec: the contact tool backed off 1.5 cm and rotated back 4 degrees about the grip
    ('''    elif spec.get("rebound") is not None:''', '''    elif spec.get("recoil"):
        tool = prev_tool.copy()
        back = Matrix.Rotation(math.radians(-4.0), 4, "X")
        tool = Matrix.Translation(tool.translation + Vector((0, .015, .006))) @ back.to_3x3().to_4x4() @ \\
            tool.to_3x3().to_4x4()
    elif spec.get("rebound") is not None:'''),
])
patch("s06_pickaxe.py", [
    ('''            ang = math.degrees((rest.inverted() @ rel).angle)''',
     '''            dq = rest.inverted() @ rel
            ang = math.degrees(2 * math.acos(min(1.0, abs(dq.w))))'''),
    ('''        arm_pen = max(arm_pen, ps.penetration("arm_l", "torso"), ps.penetration("arm_r", "torso"))''',
     '''        arm_pen_l = max(arm_pen_l, ps.penetration("arm_l", "torso"))
        arm_pen = max(arm_pen, ps.penetration("arm_r", "torso"))'''),
    ('''    path, pen_max, body_pen, arm_pen, wrist = [], 0.0, {}, 0.0, {}''',
     '''    path, pen_max, body_pen, arm_pen, arm_pen_l, wrist = [], 0.0, {}, 0.0, 0.0, {}'''),
    ('''              "max_arm_in_torso_cm": round(arm_pen * 100, 2),''',
     '''              "max_arm_r_in_torso_cm": round(arm_pen * 100, 2), "max_arm_l_in_torso_cm": round(arm_pen_l * 100, 2),'''),
])
