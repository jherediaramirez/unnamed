import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import up_config as C
bpy.ops.wm.open_mainfile(filepath=C.MASTER)
rig = bpy.data.objects["UP_Rig"]; skel = bpy.data.objects["UP_Skeleton"]
act = bpy.data.actions["RIG_UP_Walk_F"]
rig.animation_data.action = act
print("[dbg] slot", rig.animation_data.action_slot, [s.identifier for s in act.slots])
sc = bpy.context.scene
for f in (0, 5, 10):
    sc.frame_set(f)
    P = rig.pose.bones
    con = P["c_calf_l"].constraints["IK_leg"]
    print("[dbg] f", f, "prop", P["c_root"]["ik_leg_l"], "infl", round(con.influence,3), "mute", con.mute, "enabled", getattr(con, "enabled", None),
          "ankle_target", tuple(round(c,3) for c in P["m_ankle_target_l"].head), "rig calf tail", tuple(round(c,3) for c in P["c_calf_l"].tail),
          "skel calf tail", tuple(round(c,3) for c in skel.pose.bones["calf_l"].tail), "pelvis", tuple(round(c,3) for c in P["c_pelvis"].head))
    dg = bpy.context.evaluated_depsgraph_get(); ev = rig.evaluated_get(dg)
    print("[dbg]    evaluated rig calf tail", tuple(round(c,3) for c in ev.pose.bones["c_calf_l"].tail), "eval infl", round(ev.pose.bones["c_calf_l"].constraints["IK_leg"].influence,3))
