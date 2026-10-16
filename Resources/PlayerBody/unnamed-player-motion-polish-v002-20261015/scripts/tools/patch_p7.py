"""One-off patch: contact-solve delta carried into neighbouring keys, B one-handed, handle butt flush with the fist."""
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


patch("up_build.py", [
    ('''    mb.block("handle", (-w / 2, w / 2), (-.09, .47), (-w / 2, w / 2), unit, rigid("pickaxe"),
             extra_cuts=((0.0,), [-.09 + .02 * k for k in range(29)], (0.0,)))''',
     '''    # handle butt flush with the fist's little-finger side (a longer butt clipped the forearm at cocked wrists)
    mb.block("handle", (-w / 2, w / 2), (-.065, .47), (-w / 2, w / 2), unit, rigid("pickaxe"),
             extra_cuts=((0.0,), [-.065 + .02 * k for k in range(27)], (0.0,)))'''),
])
patch("up_pickaxe.py", [
    ('''    "B": [  # opposite diagonal, two-handed: across to the left shoulder, chop down-right, unwind right
        (3, dict(arm=(-118, -26, -25, -112, -25), support=1.0,
                 body=dict(twist=26, lean=-3, side_bend=4, clav_r=4, clav_fwd=10, elbow_pole_l=(.75, .3, .85)))),
        (6, dict(arm=(-128, -14, -15, -70, -8), support=1.0,
                 body=dict(twist=12, lean=6, clav_r=4, clav_fwd=8, elbow_pole_l=(.75, .3, .85)))),
        (9, dict(contact=True, arm=(-90, -6, -8, -26, 12), support=1.0,
                 body=dict(twist=2, lean=15, side_bend=-4, clav_fwd=10, elbow_pole_l=(.6, .3, .75)))),
        (10, dict(arm=(-93, -5, -8, -33, 7), support=1.0,
                  body=dict(twist=0, lean=14, side_bend=-4, clav_fwd=9, elbow_pole_l=(.6, .3, .75)))),
        (12, dict(arm=(-100, 0, -4, -50, -6), support=1.0,
                  body=dict(twist=-6, lean=11, side_bend=-3, clav_fwd=6, elbow_pole_l=(.6, .3, .8)))),
        (15, dict(arm=(-66, 10, 0, -70, -20), support=0.0, body=dict(twist=-10, lean=5, larm=(-8, 10, -36)))),
    ],''',
     '''    "B": [  # opposite diagonal (one-handed backhand): wind up across to the upper left, cut down to the right,
            # the recovery unwinds through the right side into the next stroke; left arm counter-swings
        (3, dict(arm=(-128, -22, -20, -108, -24), body=dict(twist=24, lean=-3, side_bend=4, clav_r=4, clav_fwd=10,
                                                             larm=(18, 30, -50)))),
        (6, dict(arm=(-132, -12, -12, -66, -8), body=dict(twist=12, lean=6, clav_r=4, clav_fwd=8,
                                                          larm=(10, 24, -45)))),
        (9, dict(contact=True, arm=(-90, -4, -6, -26, 12), body=dict(twist=0, lean=15, side_bend=-4, clav_fwd=10,
                                                                     larm=(-20, 14, -40)))),
        (10, dict(arm=(-93, -3, -6, -33, 7), body=dict(twist=-2, lean=14, side_bend=-4, clav_fwd=9,
                                                       larm=(-22, 13, -40)))),
        (12, dict(arm=(-100, 2, -3, -50, -6), body=dict(twist=-8, lean=11, side_bend=-3, clav_fwd=6,
                                                        larm=(-24, 12, -40)))),
        (15, dict(arm=(-66, 10, 0, -70, -20), body=dict(twist=-12, lean=5, larm=(-10, 10, -36)))),
    ],'''),
    ('''    solved = {}
    errs = {}
    for f, spec in keys:
        if spec.get("contact"):
            solved[f], errs[f] = solve_contact(rig, spec, crouch)''',
     '''    solved = {}
    errs = {}
    for f, spec in keys:
        if spec.get("contact"):
            solved[f], errs[f] = solve_contact(rig, spec, crouch)
            delta = [a - b for a, b in zip(solved[f], spec["arm"])]
            # carry the correction into the neighbouring keys so the swing stays continuous through contact
            for f2, spec2 in keys:
                w = {f - 3: .5, f + 1: .9, f + 2: .75, f + 3: .55}.get(f2, 0.0)
                if w and f2 not in solved:
                    solved[f2] = [a + w * d for a, d in zip(spec2["arm"], delta)]'''),
])
