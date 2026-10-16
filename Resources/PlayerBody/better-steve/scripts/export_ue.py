"""Export BetterSteve for engines. Works on the generated .blend or on one you have edited by hand.

    blender -b foundry/BetterSteve.blend --python scripts/export_ue.py -- <project root>

The .blend stays at Minecraft scale (1 m = 1 block). Unreal wants centimetres *inside* the FBX: an FBX written in
metres makes Unreal convert units by scaling the root joint, which leaves every bone at scale 100 (sockets and
attached props then come out 100x too big). So the FBX is written from a temporary centimetre copy of the scene:
scene unit scale 0.01, armature + mesh scaled x100 and applied, pose-bone location keys x100. glTF is metres by
spec, so the GLB is written first, from the untouched scene. Nothing is saved afterwards.
"""

import bpy
import os
import sys


def action_fcurves(act):
    try:
        return list(act.fcurves)
    except AttributeError:  # Blender 5: layered actions
        return [fc for layer in act.layers for strip in layer.strips for bag in strip.channelbags for fc in bag.fcurves]


def reset_pose(arm):
    if arm.animation_data:
        arm.animation_data.action = None
    for pb in arm.pose.bones:
        pb.location = (0, 0, 0)
        pb.rotation_euler = (0, 0, 0)
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.scale = (1, 1, 1)


def select_only(objs, active):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = active


def export_all(export_dir, arm_name="Armature", mesh_name="SK_BetterSteve"):
    scene = bpy.context.scene
    arm = bpy.data.objects[arm_name]
    body = bpy.data.objects[mesh_name]
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    acts = [a for a in bpy.data.actions if any(fc.data_path.startswith("pose.bones") for fc in action_fcurves(a))]
    arm.animation_data_create()
    reset_pose(arm)
    select_only([arm, body], arm)

    # --- glTF, metres
    glb = os.path.join(export_dir, mesh_name + ".glb")
    tracks = []
    for act in acts:  # glTF "ACTIONS" mode collects actions from NLA tracks
        tr = arm.animation_data.nla_tracks.new()
        tr.name = act.name
        tr.strips.new(act.name, int(act.frame_range[0]), act)
        tr.mute = True
        tracks.append(tr)
    try:
        bpy.ops.export_scene.gltf(filepath=glb, export_format="GLB", use_selection=True, export_def_bones=True,
                                  export_animations=True, export_animation_mode="ACTIONS", export_force_sampling=True,
                                  export_yup=True)
        print(f"[export] {glb}")
    except Exception as e:
        print(f"[export] glb failed: {e}")
    for tr in tracks:
        arm.animation_data.nla_tracks.remove(tr)

    # --- FBX, centimetres
    scene.unit_settings.scale_length = .01
    for o in (arm, body):
        if o.parent is None:  # children inherit the scale; scaling them too would double it
            o.scale = [s * 100 for s in o.scale]
            o.location = [c * 100 for c in o.location]
    select_only([arm, body], arm)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    for act in acts:
        for fc in action_fcurves(act):
            if fc.data_path.endswith(".location"):
                for kp in fc.keyframe_points:
                    kp.co.y *= 100
                    kp.handle_left.y *= 100
                    kp.handle_right.y *= 100
                fc.update()
    reset_pose(arm)
    bpy.context.view_layer.update()
    zs = [(body.matrix_world @ v.co).z for v in body.data.vertices]
    print(f"[export] centimetre scene: mesh height {max(zs) - min(zs):.2f} BU, armature scale {tuple(arm.scale)}")
    fbx = os.path.join(export_dir, mesh_name + ".fbx")
    bpy.ops.export_scene.fbx(
        filepath=fbx, use_selection=True, object_types={"ARMATURE", "MESH"},
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", global_scale=1.0,
        axis_forward="-Z", axis_up="Y", use_mesh_modifiers=True, mesh_smooth_type="FACE", add_leaf_bones=False,
        primary_bone_axis="Y", secondary_bone_axis="X", use_armature_deform_only=True, armature_nodetype="NULL",
        bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False, bake_anim_use_all_actions=True,
        bake_anim_force_startend_keying=True, bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
        path_mode="COPY", embed_textures=True)
    print(f"[export] {fbx}")


if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    root = os.path.abspath(args[0]) if args else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    export_all(os.path.join(root, "export"))
