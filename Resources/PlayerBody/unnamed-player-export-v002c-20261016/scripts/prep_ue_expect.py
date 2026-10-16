"""Blender-space expectations for the UE gates (rest tip point, sole points), from UP_manifest_v002c.json.
    uv run --no-project --python 3.13 python -I prep_ue_expect.py <manifest> <out.json>"""
import json
import sys

man = json.load(open(sys.argv[1]))
B = man["skeleton"]["bones"]


def qmul(a, b):
    w1, x1, y1, z1 = a
    w2, x2, y2, z2 = b
    return (w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2, w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2, w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2)


def qrot(q, v):
    w, x, y, z = q
    r = qmul(qmul(q, (0.0, *v)), (w, -x, -y, -z))
    return r[1:]


def world(name):  # (quat, translation) of bone.matrix_local in metres
    b = B[name]
    q, t = tuple(b["parent_relative_rotation_wxyz"]), tuple(b["parent_relative_location_m"])
    if b["parent"] is None:
        return q, t
    pq, pt = world(b["parent"])
    r = qrot(pq, t)
    return qmul(pq, q), tuple(pt[i] + r[i] for i in range(3))


def xform(qt, v):
    r = qrot(qt[0], v)
    return tuple(qt[1][i] + r[i] for i in range(3))


pk = man["pickaxe"]
sock = pk["socket_hand_r_pickaxe"]
hand = world("hand_r")
sq, st = tuple(sock["rotation_quat_wxyz"]), tuple(sock["location_m"])
grip = (qmul(hand[0], sq), xform(hand, st))
tip_rest = xform(grip, tuple(pk["tip_local_m"]))
head_err = max(max(abs(world(n)[1][i] - B[n]["head_m"][i]) for i in range(3)) for n in B)
J = {"heel_y": 0.1, "ball_y": -0.05, "toe_tip_y": -0.14}   # up_config.J (v001), metres, rest pose
LEG_X = (0.0, 0.2)
soles = {}
for side in "lr":
    hx = (LEG_X[0] + LEG_X[1]) / 2 * (1 if side == "l" else -1)
    soles[side] = {"heel": ["foot_" + side, [hx, J["heel_y"], 0.0]], "mid": ["foot_" + side, [hx, 0.0, 0.0]],
                   "ball": ["ball_" + side, [hx, J["ball_y"], 0.0]], "tip": ["ball_" + side, [hx, J["toe_tip_y"], 0.0]]}
out = {"rest_tip_blender_m": tip_rest, "grip_rest_blender_m": grip[1], "target_blender_m": pk["target_m"],
       "chain_head_check_max_err_m": head_err, "soles_blender_m": soles}
json.dump(out, open(sys.argv[2], "w"), indent=1)
print(json.dumps(out))
