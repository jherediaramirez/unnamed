"""Sample the actual baked arm motion at 240 Hz, including between the 60 Hz keys."""
import os,sys,json,math
sys.dont_write_bytecode=True
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import bpy,numpy as np
from mathutils import Vector
import up_config as C,up_pickaxe as PK,up_check as K,up_validate as V
scene=bpy.context.scene;rig=bpy.data.objects['UP_Rig'];skel=bpy.data.objects['UP_Skeleton'];body=bpy.data.objects['UP_Body'];head=bpy.data.objects['UP_Head'];pick=bpy.data.objects['UP_Pickaxe']
for o in [rig,skel]:
    if o.animation_data:
        o.animation_data.action=None
        for t in list(o.animation_data.nla_tracks):o.animation_data.nla_tracks.remove(t)
skel.parent=None
probe=K.SkelProbe(skel)
socket=rig.data.bones['c_hand_r'].matrix_local.inverted()@rig.data.bones['c_grip_r'].matrix_local
def depth(obj,part,p):
    lo,hi=obj.bbox(part)
    if (np.array(p)>=lo-1e-5).all() and (np.array(p)<=hi+1e-5).all() and obj.inside(part,p):return obj.tree(part).find_nearest(p)[3]
    return 0
report={}
for key in 'ABC':
    rows=[];act=bpy.data.actions['UP_Pickaxe_Strike'+key]
    for i in range(73):
        f=i/4;probe.set(act,f);m=skel.matrix_world@skel.pose.bones['hand_r'].matrix@socket;tip=m@PK.TIP_LOCAL
        ps=V.PartSet(body);ph=V.PartSet(head)
        pen={n:0 for n in ['head','arm_r','arm_l','torso']}
        for v in pick.data.vertices:
            p=m@v.co
            for part,obj in [('head',ph),('arm_r',ps),('arm_l',ps),('torso',ps)]:
                if part=='arm_r' and v.co.length<.13:continue
                pen[part]=max(pen[part],depth(obj,part,p)*100)
        pen['shoulder_r']=ps.penetration('arm_r','torso')*100
        pen['shoulder_l']=ps.penetration('arm_l','torso')*100
        c=Vector(PK.STONE['center']);h=.5
        pen['stone_tip']=min(h-abs(tip[j]-c[j]) for j in range(3))*100 if all(abs(tip[j]-c[j])<h for j in range(3)) else 0
        rows.append(dict(frame=f,tip=list(tip),penetration_cm=pen))
    worst={p:max((r['penetration_cm'][p],r['frame']) for r in rows) for p in rows[0]['penetration_cm']}
    approach={p:max((r['penetration_cm'][p],r['frame']) for r in rows if r['frame']<9) for p in rows[0]['penetration_cm']}
    report[key]=dict(worst=worst,anticipation_approach_worst=approach,samples=rows,contact_error_cm=(Vector(rows[36]['tip'])-PK.TARGET).length*100)
    print('ARM_CHECK',key,'whole',worst,'precontact',approach,flush=True)
json.dump(report,open(os.path.join(C.ROOT,'validation','arm_polish_240hz.json'),'w'),indent=2)
