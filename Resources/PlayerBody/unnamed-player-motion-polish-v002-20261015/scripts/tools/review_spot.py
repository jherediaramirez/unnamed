import bpy, os
f = r"C:\dev\output\unnamed-player-blender-v001-20261015\blend\UP_Player_review_v001.blend"
bpy.ops.wm.open_mainfile(filepath=f)
sc = bpy.context.scene
sc.render.engine = "BLENDER_WORKBENCH"; sc.display.shading.color_type = "TEXTURE"; sc.display.shading.show_shadows = True
sc.render.resolution_x, sc.render.resolution_y = 480, 300
marks = {m.name: m.frame for m in sc.timeline_markers}
picks = [("Walk_R", 20), ("Sprint_F", 10), ("Sneak_FR", 30), ("Jump_Fall", 1), ("Turn_L90 (body yaw from metadata)", 18), ("Look_UL", 10), ("Strike A", 9), ("Strike B", 9)]
out = r"C:\dev\output\unnamed-player-blender-v001-20261015\previews\_tmp\review"
os.makedirs(out, exist_ok=True)
for name, off in picks:
    fr = marks[name] + off
    sc.frame_set(fr)
    sc.render.filepath = os.path.join(out, f"{fr:04d}.png")
    bpy.ops.render.render(write_still=True)
    print("[spot]", name, fr, tuple(round(c, 2) for c in bpy.data.objects["REVIEW_Travel (not exported)"].location))
