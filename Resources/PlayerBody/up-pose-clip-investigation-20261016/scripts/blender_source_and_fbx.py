"""Evaluate source and independently decode FBX matrices. No .blend is saved."""
import sys
sys.dont_write_bytecode = True
import bpy, json, math, hashlib
from pathlib import Path
from mathutils import Matrix, Vector, Euler
from bpy_extras.io_utils import axis_conversion
from io_scene_fbx import parse_fbx

OUT = Path(r'C:\dev\output\up-pose-clip-investigation-20261016')
BASE = Path(r'C:\dev\output\unnamed-player-export-v002c-20261016')
SRC = Path(r'C:\dev\output\unnamed-player-motion-polish-v002-20261015\blend\UP_Player_arm_polish_v002.blend')
MAN = json.loads((BASE/'fbx/UP_manifest_v002c.json').read_text())
CLIPS = [c['name'] for c in MAN['clips'] if c.get('pose_clip')]
def dump(name, data):
    (OUT/'evidence'/name).write_text(json.dumps(data, indent=2, default=lambda v:v.decode(errors='replace') if isinstance(v,bytes) else list(v)))
def rows(m): return [list(r) for r in m]
def angle(a,b):
    qa,qb = list(a.to_quaternion()), list(b.to_quaternion())
    d=abs(sum(x*y for x,y in zip(qa,qb))) / math.sqrt(sum(x*x for x in qa)*sum(x*x for x in qb))
    return math.degrees(2*math.acos(min(1.0,d)))
def compare(a,b):
    return {'cm': (a.to_translation()-b.to_translation()).length*100, 'deg':angle(a,b)}
before = hashlib.sha256(SRC.read_bytes()).hexdigest()
assert before.upper() == '26E585D5676D5A8A18A6E6A2FAE5D682DF3470FA7B83A01972CFAE138D4D22C7'
bpy.ops.wm.open_mainfile(filepath=str(SRC))
skel,rig = bpy.data.objects['UP_Skeleton'],bpy.data.objects['UP_Rig']
if rig.animation_data: rig.animation_data.action = None
for pb in skel.pose.bones:
    for c in list(pb.constraints): pb.constraints.remove(c)
skel.animation_data_create()
for t in list(skel.animation_data.nla_tracks): skel.animation_data.nla_tracks.remove(t)
source = {'source':str(SRC),'sha256':before,'blender':bpy.app.version_string,
          'evaluation':'UP action on deform skeleton; rig leftover action/NLA and rig-copy constraints removed in memory, as export contract; no source save',
          'rest':{b.name:rows(skel.matrix_world@b.matrix_local) for b in skel.data.bones},
          'parents':{b.name:b.parent.name if b.parent else None for b in skel.data.bones}, 'poses':{}}
for n in CLIPS:
    act=bpy.data.actions[n]
    skel.animation_data.action=act
    skel.animation_data.action_slot=act.slots[0]
    frames=[]
    for f in (0,1,2):
        bpy.context.scene.frame_set(f)
        bpy.context.view_layer.update()
        frames.append({pb.name:rows(skel.matrix_world@pb.matrix) for pb in skel.pose.bones})
    assert frames[0] == frames[1] == frames[2], n
    source['poses'][n]=frames[0]
dump('source_poses.json', source)

def child(e, name): return next((c for c in e.elems if c.id==name), None)
def props(e):
    p=child(e,b'Properties70')
    return {c.props[0].decode():list(c.props[4:]) for c in p.elems} if p else {}
def local_matrix(p, animated=None):
    p=dict(p)
    if animated: p.update(animated)
    for key in ('RotationOffset','RotationPivot','ScalingOffset','ScalingPivot','GeometricTranslation','GeometricRotation'):
        assert not any(p.get(key,[0,0,0])), (key,p[key])
    assert p.get('RotationOrder',[0])[0]==0, p
    def rot(key): return Euler([math.radians(v) for v in p.get(key,[0,0,0])],'XYZ').to_matrix().to_4x4()
    return Matrix.Translation(p.get('Lcl Translation',[0,0,0])) @ rot('PreRotation') @ rot('Lcl Rotation') @ rot('PostRotation').inverted() @ Matrix.Diagonal(Vector((*p.get('Lcl Scaling',[1,1,1]),1)))
G=axis_conversion(to_forward='-Z',to_up='Y').to_4x4()
report={}
for label,root in [('one_key',Path(r'C:\dev\output\unnamed-player-export-v002b-20261015\fbx')),('two_keys',BASE/'fbx'),('three_keys',OUT/'variants/three_keys')]:
    report[label]={}
    for n in CLIPS:
        path=root/'anims'/('A_'+n+'.fbx')
        tree,version=parse_fbx.parse(str(path))
        objects=child(tree,b'Objects')
        obj={e.props[0]:e for e in objects.elems}
        con=[list(e.props) for e in child(tree,b'Connections').elems]
        models={i:e for i,e in obj.items() if e.id==b'Model'}
        names={i:e.props[1].decode().split('\x00')[0] for i,e in models.items()}
        parents={c[1]:c[2] for c in con if c[0]==b'OO' and c[1] in models and (c[2] in models or c[2]==0)}
        animated={}; curves={}
        for c in con:
            if c[0]!=b'OP' or c[1] not in obj or obj[c[1]].id!=b'AnimationCurveNode' or c[2] not in models: continue
            mid,prop=c[2],c[3].decode()
            values=list(props(models[mid]).get(prop, [0,0,0] if prop!='Lcl Scaling' else [1,1,1]))
            for cc in con:
                if cc[0]==b'OP' and cc[2]==c[1] and obj[cc[1]].id==b'AnimationCurve':
                    curve=obj[cc[1]]; axis=cc[3].decode()[-1]; idx='XYZ'.index(axis)
                    times=list(child(curve,b'KeyTime').props[0]); vals=list(child(curve,b'KeyValueFloat').props[0])
                    values[idx]=vals[0]
                    curves.setdefault(names[mid],{}).setdefault(prop,{})[axis]={'times':times,'values':vals}
            animated.setdefault(mid,{})[prop]=values
        def world(mid, use_curve):
            if mid not in models: return Matrix.Identity(4)
            return world(parents.get(mid),use_curve) @ local_matrix(props(models[mid]), animated.get(mid) if use_curve else None)
        errors={}; static_errors={}
        for mid,name in names.items():
            if name not in source['rest']: continue
            target=G@Matrix(source['poses'][n][name]); target.translation*=100
            a=world(mid,True); b=world(mid,False)
            errors[name]={'cm':(a.translation-target.translation).length,'deg':angle(a,target)}
            static_errors[name]={'cm':(b.translation-target.translation).length,'deg':angle(b,target)}
        take=child(tree,b'Takes')
        stacks={e.props[1].decode().split('\x00')[0]:props(e) for e in objects.elems if e.id==b'AnimationStack'}
        all_times=sorted({t for bone in curves.values() for prop in bone.values() for c in prop.values() for t in c['times']})
        report[label][n]={'path':str(path),'version':version,'global':props(child(tree,b'GlobalSettings')),
            'stacks':stacks,'takes':[{c.id.decode():list(c.props) for c in e.elems} for e in take.elems if e.id==b'Take'] if take else [],
            'key_times':all_times,'max_curve_cm':max(v['cm'] for v in errors.values()),'max_curve_deg':max(v['deg'] for v in errors.values()),
            'curve_errors':errors,'static_errors':static_errors,'models':{names[i]:props(e) for i,e in models.items()},'curves':curves}
dump('fbx_decoded.json', report)

# Reproduce default Blender round-trip; independently calibrate each imported bone's rest basis.
roundtrip={}
for n in CLIPS:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps=60
    bpy.ops.import_scene.fbx(filepath=str(BASE/'fbx/anims'/('A_'+n+'.fbx')),use_anim=True,automatic_bone_orientation=False,primary_bone_axis='Y',secondary_bone_axis='X')
    arm=next(o for o in bpy.data.objects if o.type=='ARMATURE')
    start=arm.animation_data.action.frame_range[0]
    bpy.context.scene.frame_set(int(start))
    bpy.context.view_layer.update()
    per={}
    for pb in arm.pose.bones:
        src=Matrix(source['poses'][n][pb.name]); rest=Matrix(source['rest'][pb.name])
        imp_rest=arm.matrix_world@pb.bone.matrix_local
        expected=src@rest.inverted()@imp_rest
        actual=arm.matrix_world@pb.matrix
        per[pb.name]={'pos_cm':(actual.translation-src.translation).length*100,'rot_deg':angle(actual,expected),'matrix':rows(actual),'use_connect':pb.bone.use_connect,'pose_location':list(pb.location)}
    roundtrip[n]={'start':start,'max_cm':max(v['pos_cm'] for v in per.values()),'max_deg':max(v['rot_deg'] for v in per.values()),'bones':per}
    bpy.context.view_layer.objects.active=arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    arm.data.edit_bones['head'].use_connect=False
    bpy.ops.object.mode_set(mode='OBJECT')
    bpy.context.scene.frame_set(int(start)+1)
    bpy.context.scene.frame_set(int(start))
    bpy.context.view_layer.update()
    roundtrip[n]['head_only_unconnected_max_cm']=max(((arm.matrix_world@pb.matrix).translation-Matrix(source['poses'][n][pb.name]).translation).length*100 for pb in arm.pose.bones)
dump('blender_roundtrip.json',roundtrip)
after=hashlib.sha256(SRC.read_bytes()).hexdigest()
assert before==after
print('[posefix] source unchanged; clips',len(CLIPS))
print('[posefix] curve maxima',json.dumps({label:{'cm':max(e['max_curve_cm'] for e in es.values()),'deg':max(e['max_curve_deg'] for e in es.values())} for label,es in report.items()}))
print('[posefix] roundtrip',json.dumps({n:{k:e[k] for k in ('max_cm','max_deg')} for n,e in roundtrip.items()}))
