"""One-off patch: exact analytic correction for IK arms in the bake (hands land exactly on c_hand_ik)."""
import os

S = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = os.path.join(S, "up_anim.py")
s = open(p, encoding="utf-8").read()
old = '''def locals_from_matrices(skel, M):'''
new = '''def exact_arm(skel, rig, side, M):
    """Same analytic two-bone fix for arms in IK mode: hand = c_hand_ik frame exactly (tool grips stay rigid)."""
    S = "_" + side
    bones = skel.data.bones
    a, b = bones["upperarm" + S].length, bones["lowerarm" + S].length
    H = M["upperarm" + S].translation.copy()
    T = rig.pose.bones["c_hand_ik" + S].matrix.copy()
    Tp = T.translation.copy()
    E0 = M["lowerarm" + S].translation.copy()
    dv = Tp - H
    d = min(dv.length, (a + b) * .99999)
    u = dv.normalized()
    ca = (a * a + d * d - b * b) / (2 * a * d)
    sa = math.sqrt(max(0.0, 1 - ca * ca))
    w = (E0 - H) - u * (E0 - H).dot(u)
    if w.length < 1e-7:
        w = rig.pose.bones["c_elbow_pole" + S].head - H
        w = w - u * w.dot(u)
    w.normalize()
    E = H + u * (a * ca) + w * (a * sa)
    M["upperarm" + S] = _aim(M["upperarm" + S], E - H, H)
    M["lowerarm" + S] = _aim(M["lowerarm" + S], Tp - E, E)
    ht = T.to_3x3().normalized().to_4x4()
    ht.translation = Tp
    old_hand = M["hand" + S].copy()
    M["hand" + S] = ht
    # fingers/thumb ride the corrected hand (keep their pose relative to the hand)
    corr = ht @ old_hand.inverted()
    for n in ("fingers_01", "fingers_02", "thumb_01", "thumb_02"):
        M[n + S] = corr @ M[n + S]
    return dv.length - d


def locals_from_matrices(skel, M):'''
assert old in s
s = s.replace(old, new, 1)
old = '''            for side in "lr":
                if root.get("ik_leg_" + side, 0.0) > .999:
                    unreachable = max(unreachable, exact_leg(skel, rig, side, M))'''
new = '''            for side in "lr":
                if root.get("ik_leg_" + side, 0.0) > .999:
                    unreachable = max(unreachable, exact_leg(skel, rig, side, M))
                if root.get("ik_arm_" + side, 0.0) > .999:
                    unreachable = max(unreachable, exact_arm(skel, rig, side, M))'''
assert old in s
s = s.replace(old, new, 1)
open(p, "w", encoding="utf-8").write(s)
print("[patch] up_anim exact_arm")
