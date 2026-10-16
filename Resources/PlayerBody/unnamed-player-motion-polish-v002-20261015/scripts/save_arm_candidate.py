"""Keep the approved source intact and isolate the six revised strike actions."""
import os,sys,json,math
sys.dont_write_bytecode=True
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import bpy
import up_config as C,up_anim as A
baseline=r'C:\dev\output\unnamed-player-blender-v001-20261015\blend\UP_Player_master_v001.blend'
names=[f'{prefix}Pickaxe_Strike{k}' for prefix in ['UP_','RIG_UP_'] for k in 'ABC']
bpy.ops.wm.open_mainfile(filepath=baseline)
def channels(act):
    return {(fc.data_path,fc.array_index):[(float(p.co[0]),float(p.co[1])) for p in fc.keyframe_points] for fc in A.action_fcurves(act)}
old={name:channels(bpy.data.actions[name]) for name in names if name.startswith('UP_')}
unchanged={a.name:channels(a) for a in bpy.data.actions if a.name not in names}
for objname in ['UP_Rig','UP_Skeleton']:
    obj=bpy.data.objects[objname]
    if obj.animation_data:
        obj.animation_data.action=None
        for t in list(obj.animation_data.nla_tracks):obj.animation_data.nla_tracks.remove(t)
for name in names:bpy.data.actions.remove(bpy.data.actions[name])
with bpy.data.libraries.load(C.MASTER,link=False) as (src,dst):dst.actions=list(names)
report={'source':baseline,'scope':'Six strike actions only; other actions retained from v001.','lower_body_max_channel_delta':{},'other_actions_unchanged':True}
for name in old:
    new=channels(bpy.data.actions[name]);worst=0.0
    for ch,points in old[name].items():
        if not any(ch[0].startswith(f'pose.bones["{bone}"]') for bone in C.LOWER_BODY):continue
        assert len(new[ch])==len(points),(name,ch)
        worst=max(worst,max(abs(a[1]-b[1]) for a,b in zip(points,new[ch])))
    report['lower_body_max_channel_delta'][name]=worst
for name,ref in unchanged.items():
    assert channels(bpy.data.actions[name])==ref,name
rig=bpy.data.objects['UP_Rig'];skel=bpy.data.objects['UP_Skeleton']
for pb in skel.pose.bones:
    for c in pb.constraints:c.mute=False
act=bpy.data.actions['RIG_UP_Pickaxe_StrikeA'];rig.animation_data_create();rig.animation_data.action=act;rig.animation_data.action_slot=act.slots[0]
bpy.context.scene.frame_set(3)
bpy.context.scene.frame_start=0;bpy.context.scene.frame_end=18
for name in names:bpy.data.actions[name].use_fake_user=True
path=os.path.join(C.ROOT,'blend','UP_Player_arm_polish_v002.blend')
bpy.ops.wm.save_as_mainfile(filepath=path,compress=True)
json.dump(report,open(os.path.join(C.ROOT,'validation','focused_arm_preservation.json'),'w'),indent=2)
print('FOCUSED_CANDIDATE',json.dumps(report),path,flush=True)
