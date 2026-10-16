import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import up_config as C
from mathutils import Vector
bpy.ops.wm.open_mainfile(filepath=C.MASTER)
exec(open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "s02_rig_tests.py")).read().split("# pairs whose mutual")[0].split("pick = bpy.data.objects[\"UP_Pickaxe\"]")[1])
import up_anim as A
rig = bpy.data.objects["UP_Rig"]
for name, fn, sw in TESTS:
    if name != "T08_grip_two_hands": continue
    p = fn(A.Pose(rig)); A.reset_pose(rig); A.set_switches(rig, **sw); p.apply(reset=True)
bpy.context.view_layer.update()
P = rig.pose.bones
sup = P["c_support_hand_l"].head; sh = P["c_upperarm_l"].head; wr = P["c_hand_l"].head
g = P["c_pickaxe"].matrix
print("[2h] support wrist", tuple(round(c,3) for c in sup), "shoulder_l", tuple(round(c,3) for c in sh), "dist", round((sup-sh).length,3),
      "chain", round(rig.data.bones["c_upperarm_l"].length + rig.data.bones["c_lowerarm_l"].length,3), "wrist_l now", tuple(round(c,3) for c in wr))
print("[2h] grip_r", tuple(round(c,3) for c in g.translation), "Xg", tuple(round(c,2) for c in g.col[0][:3]), "Yg(head)", tuple(round(c,2) for c in g.col[1][:3]), "Zg(tip)", tuple(round(c,2) for c in g.col[2][:3]))
