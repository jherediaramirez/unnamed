"""Identical cameras on original/revised baked poses; no pose changes during rendering."""
import os,sys,math
sys.dont_write_bytecode=True
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import bpy
from mathutils import Matrix,Vector
import up_config as C,up_render as R,up_preview as PV
args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
tag=args[0] if args else 'after'
scene=bpy.context.scene;rig=bpy.data.objects['UP_Rig'];skel=bpy.data.objects['UP_Skeleton'];pick=bpy.data.objects['UP_Pickaxe'];head=bpy.data.objects['UP_Head']
pv=PV.BakedPreview(scene)
for o in (rig,skel,pv.travel):o.animation_data_clear()
pv.travel.location=(0,0,0);pv.travel.rotation_euler=(0,0,0)
for o in bpy.data.objects:
    if o.type=='MESH':o.hide_render=o.name not in ['UP_Body','UP_Head','UP_Pickaxe']
pick.parent=None
for c in list(pick.constraints):pick.constraints.remove(c)
socket=rig.data.bones['c_hand_r'].matrix_local.inverted()@rig.data.bones['c_grip_r'].matrix_local
R.setup_workbench(scene,res=(600,640),shadows=False,world=(.18,.205,.245))
scene.display.shading.background_type='WORLD'
scene.display.shading.studio_light='paint.sl'
PV.still_settings(scene)
cam=R.camera('ARM_Review_Camera',ortho=1.72);scene.camera=cam
out=os.path.join(C.ROOT,'previews','arm_comparison',tag);os.makedirs(out,exist_ok=True)
def pose(k,f):
    act=bpy.data.actions['UP_Pickaxe_Strike'+k];skel.animation_data_create();skel.animation_data.action=act;skel.animation_data.action_slot=act.slots[0]
    scene.frame_set(int(f),subframe=f-int(f));pick.matrix_world=skel.matrix_world@skel.pose.bones['hand_r'].matrix@socket
def render(path):
    scene.render.filepath=path;bpy.ops.render.render(write_still=True)
for k in 'ABC':
    for f in [0,1,2,3,4,5,6,7,8,9]:
        pose(k,f)
        for view,off in [('threequarter',(-3.2,-3.4,.65)),('front',(0,-4,.3))]:
            target=Vector((-.03,-.1,1.45));R.look_at(cam,target+Vector(off),target)
            render(os.path.join(out,f'{k}_f{f:02d}_{view}.png'))
# A true 60 fps ABAC sequence, excluding the duplicated loop endpoint.
scene.render.resolution_x=640;scene.render.resolution_y=720;cam.data.ortho_scale=2.6
target=Vector((-.03,-.20,1.1));R.look_at(cam,target+Vector((-3.2,-3.4,.65)),target)
for f in range(72):
    pose('ABAC'[f//18],f%18);render(os.path.join(out,f'chain_{f:03d}.png'))
# Fixed-eye first-person views for the two named phases. No camera animation.
cam.data.type='PERSP';R.fov_vertical(cam,70);head.hide_render=True
R.look_at(cam,(0,0,1.62),(0,-1,1.62-math.tan(math.radians(28))))
scene.render.resolution_x=960;scene.render.resolution_y=540
for k in 'ABC':
    for f in [3,6]:
        pose(k,f);render(os.path.join(out,f'{k}_f{f:02d}_firstperson.png'))
print('Arm comparisons rendered',tag,out,flush=True)
