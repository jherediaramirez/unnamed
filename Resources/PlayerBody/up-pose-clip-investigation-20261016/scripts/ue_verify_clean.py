"""Second editor run: reload previous winners and freshly import them without baseline ensures."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
import json,traceback
import unreal
OUT=Path(r'C:\dev\output\up-pose-clip-investigation-20261016')
sys.path.insert(0,str(OUT/'scripts'))
import ue_investigate as I
L=I.L; EAL=I.EAL
old=json.loads((OUT/'evidence/ue_imports.json').read_text())
R={'engine':unreal.SystemLibrary.get_engine_version(),'reference':old['reference'],'reference_local':old['reference_local'],'variants':{}}
def checkpoint(): (OUT/'evidence/ue_clean.json').write_text(json.dumps(R,indent=2))
try:
    assert old.get('complete')
    mesh=EAL.load_asset(old['mesh']);skel=EAL.load_asset(old['skeleton'])
    for v in ('three_keys','set_range_0_2'):
        R['variants']['reload_'+v]={'clips':{}}
        for n,e in old['variants'][v]['clips'].items():
            seq=EAL.load_asset(e['path'])
            R['variants']['reload_'+v]['clips'][n]=I.capture(seq,mesh)
        checkpoint()
    for v,fbx,override in [('clean_three_keys',OUT/'variants/three_keys',False),('clean_set_range_0_2',Path(L.FBX),True)]:
        folder='/Game/PoseFixClean/'+v
        assert not EAL.does_directory_exist(folder)
        data={'clips':{},'additive':{}}
        R['variants'][v]=data; seqs={}
        for n in I.POSES:
            unreal.log('[POSEFIX_BEGIN] '+v+' '+n)
            p=L.make_pipeline(False,skel,n.replace('Sneak','Crouch'))
            if override:
                ap=p.get_editor_property('animation_pipeline')
                ap.set_editor_property('animation_range',unreal.InterchangeAnimationRange.SET_RANGE)
                interval=unreal.Int32Interval()
                interval.set_editor_property('min',0);interval.set_editor_property('max',2)
                ap.set_editor_property('frame_import_range',interval)
            if 'pipeline' not in data: data['pipeline']=L.readback(p)
            new=L.do_import(str(fbx/'anims'/('A_'+n+'.fbx')),folder,p)
            assert len(new)==1 and str(EAL.find_asset_data(new[0]).asset_class_path.asset_name)=='AnimSequence',new
            seq=EAL.load_asset(new[0]);seqs[n]=seq
            EAL.save_loaded_asset(seq,only_if_is_dirty=False)
            data['clips'][n]=I.capture(seq,mesh)
            unreal.log('[POSEFIX_END] '+v+' '+n)
            checkpoint()
        for n in sorted(seqs,key=lambda n:n.endswith('_C')):
            if 'Look_' not in n: continue
            seq=seqs[n];base=seqs['UP_SneakLook_C' if 'Sneak' in n else 'UP_Look_C']
            seq.set_editor_property('additive_anim_type',unreal.AdditiveAnimationType.AAT_ROTATION_OFFSET_MESH_SPACE)
            seq.set_editor_property('ref_pose_type',unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME)
            seq.set_editor_property('ref_pose_seq',base);seq.set_editor_property('ref_frame_index',0)
        EAL.save_directory(folder,only_if_is_dirty=False,recursive=True)
        for n,seq in seqs.items():
            if 'Look_' in n:
                data['additive'][n]={'type':str(seq.get_editor_property('additive_anim_type')),'base':seq.get_editor_property('ref_pose_seq').get_path_name(),
                    'base_type':str(seq.get_editor_property('ref_pose_type')),'frame':seq.get_editor_property('ref_frame_index'),'full_pose':I.capture(seq,mesh)}
        checkpoint()
    R['complete']=True;checkpoint();unreal.log('[POSEFIX_COMPLETE]')
except Exception:
    R['fatal_error']=traceback.format_exc();checkpoint();raise
