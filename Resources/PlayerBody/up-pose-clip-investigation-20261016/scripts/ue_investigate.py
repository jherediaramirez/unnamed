"""First editor run: controlled imports and full per-bone raw/compressed captures."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
import json, traceback
import unreal

OUT=Path(r'C:\dev\output\up-pose-clip-investigation-20261016')
sys.path.insert(0,str(OUT/'scripts'))
import ue_m0_lib_reference as L
EAL=unreal.EditorAssetLibrary
AL=unreal.AnimationLibrary
APE=unreal.AnimPoseExtensions
POSES=[c['name'] for c in L.MAN['clips'] if c.get('pose_clip')]
RESULT={'engine':unreal.SystemLibrary.get_engine_version(),'translator':L.fbx_translator_defaults(),'variants':{}}
def checkpoint():
    (OUT/'evidence/ue_imports.json').write_text(json.dumps(RESULT,indent=2))
def v(x): return [x.x,x.y,x.z]
def tf(t): return {'p':v(t.translation),'q':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],'s':v(t.scale3d)}
def capture(seq,mesh):
    result={'path':seq.get_path_name(),'keys':AL.get_num_keys(seq),'frames':AL.get_num_frames(seq),
            'seconds':seq.get_play_length(),'sample_dt':AL.get_time_at_frame(seq,1),'samples':{}}
    try: result['track_names']=[str(n) for n in AL.get_animation_track_names(seq)]
    except Exception as ex: result['track_api_error']=str(ex)
    for label,kind in [('raw',unreal.AnimDataEvalType.RAW),('compressed',unreal.AnimDataEvalType.COMPRESSED)]:
        opt=L.opts(kind,mesh)
        result['samples'][label]={}
        for f in range(result['keys']):
            pose=APE.get_anim_pose_at_frame(seq,f,opt)
            result['samples'][label][str(f)]={str(n):tf(APE.get_bone_pose(pose,n,unreal.AnimPoseSpaces.WORLD)) for n in APE.get_bone_names(pose)}
    return result
def main():
    root='/Game/PoseFix'
    assert not EAL.does_directory_exist(root), 'Fresh import folder required'
    p=L.make_pipeline(True)
    p.get_editor_property('material_pipeline').set_editor_property('import_materials',False)
    p.get_editor_property('material_pipeline').get_editor_property('texture_pipeline').set_editor_property('import_textures',False)
    L.do_import(str(Path(L.FBX)/'SK_UnnamedPlayer.fbx'),root+'/Mesh',p)
    meshes=L.assets_of(root,'SkeletalMesh'); skels=L.assets_of(root,'Skeleton')
    assert len(meshes)==len(skels)==1,(meshes,skels)
    mesh=EAL.load_asset(meshes[0]); skel=EAL.load_asset(skels[0])
    RESULT['mesh']=mesh.get_path_name(); RESULT['skeleton']=skel.get_path_name()
    ref=APE.get_reference_pose(skel)
    RESULT['reference']={str(n):tf(APE.get_ref_bone_pose(ref,n,unreal.AnimPoseSpaces.WORLD)) for n in APE.get_bone_names(ref)}
    RESULT['reference_local']={str(n):tf(APE.get_ref_bone_pose(ref,n,unreal.AnimPoseSpaces.LOCAL)) for n in APE.get_bone_names(ref)}
    variants=[
      ('one_key',Path(r'C:\dev\output\unnamed-player-export-v002b-20261015\fbx'),POSES,{}),
      ('two_keys',Path(L.FBX),POSES,{}),
      ('three_keys',OUT/'variants/three_keys',POSES,{}),
      ('set_range_0_2',Path(L.FBX),POSES,{'range':2}),
      ('set_range_0_1',Path(L.FBX),['UP_Look_D','UP_Pickaxe_Hold'],{'range':1}),
      ('set_range_0_0',Path(L.FBX),['UP_Look_D'],{'range':0}),
      ('snap_closest',Path(L.FBX),['UP_Look_D'],{'snap':True}),
      ('rate_30',Path(L.FBX),['UP_Look_D','UP_Pickaxe_Hold'],{'rate':30}),
    ]
    for variant,fbx,names,settings in variants:
        folder=root+'/'+variant
        data={'settings':settings,'clips':{}}
        RESULT['variants'][variant]=data
        seqs={}
        for name in names:
            unreal.log('[POSEFIX_BEGIN] '+variant+' '+name)
            try:
                p=L.make_pipeline(False,skel,name.replace('Sneak','Crouch'))
                ap=p.get_editor_property('animation_pipeline')
                if 'range' in settings:
                    ap.set_editor_property('animation_range',unreal.InterchangeAnimationRange.SET_RANGE)
                    interval=unreal.Int32Interval()
                    interval.set_editor_property('min',0); interval.set_editor_property('max',settings['range'])
                    ap.set_editor_property('frame_import_range',interval)
                if settings.get('snap'): ap.set_editor_property('frame_alignment',unreal.InterchangeFrameAlignment.SNAP_TO_CLOSEST)
                if 'rate' in settings: ap.set_editor_property('custom_bone_animation_sample_rate',settings['rate'])
                if 'pipeline' not in data: data['pipeline']=L.readback(p)
                new=L.do_import(str(fbx/'anims'/('A_'+name+'.fbx')),folder,p)
                anims=[a for a in new if str(EAL.find_asset_data(a).asset_class_path.asset_name)=='AnimSequence']
                assert len(anims)==1 and len(new)==1,new
                seq=EAL.load_asset(anims[0]); seqs[name]=seq
                EAL.save_loaded_asset(seq,only_if_is_dirty=False)
                data['clips'][name]=capture(seq,mesh)
            except Exception:
                data['clips'][name]={'error':traceback.format_exc()}
                unreal.log_warning(data['clips'][name]['error'])
            unreal.log('[POSEFIX_END] '+variant+' '+name)
            checkpoint()
        # Include the actual M0 additive configuration on complete grids, then capture full poses again.
        if variant in ('three_keys','set_range_0_2'):
            data['additive']={}
            for name in sorted(seqs,key=lambda n:n.endswith('_C')):
                if 'Look_' not in name: continue
                seq=seqs[name]; base=seqs['UP_SneakLook_C' if 'Sneak' in name else 'UP_Look_C']
                seq.set_editor_property('additive_anim_type',unreal.AdditiveAnimationType.AAT_ROTATION_OFFSET_MESH_SPACE)
                seq.set_editor_property('ref_pose_type',unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME)
                seq.set_editor_property('ref_pose_seq',base)
                seq.set_editor_property('ref_frame_index',0)
            EAL.save_directory(folder,only_if_is_dirty=False,recursive=True)
            for name,seq in seqs.items():
                if 'Look_' in name:
                    data['additive'][name]={'type':str(seq.get_editor_property('additive_anim_type')),'base':seq.get_editor_property('ref_pose_seq').get_path_name(),
                        'base_type':str(seq.get_editor_property('ref_pose_type')),'frame':seq.get_editor_property('ref_frame_index'),'full_pose':capture(seq,mesh)}
        checkpoint()
    RESULT['inventory']={c:L.assets_of(root,c) for c in ('Skeleton','SkeletalMesh','AnimSequence','ObjectRedirector')}
    EAL.save_directory(root,only_if_is_dirty=False,recursive=True)
    RESULT['complete']=True
    checkpoint()
    unreal.log('[POSEFIX_COMPLETE]')
if __name__=='__main__':
    try: main()
    except Exception:
        RESULT['fatal_error']=traceback.format_exc(); checkpoint(); raise
