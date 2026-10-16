import bpy, sys
print("VERSION", bpy.app.version_string)
print("ENGINES", [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items])
im = bpy.types.ImageFormatSettings.bl_rna.properties
print("IMG props", [p.identifier for p in im][:40])
try:
    print("media_type", [e.identifier for e in im["media_type"].enum_items])
except Exception as e:
    print("no media_type", e)
print("file_format", [e.identifier for e in im["file_format"].enum_items])
a = bpy.data.actions.new("t")
print("action attrs", [x for x in dir(a) if not x.startswith("_")])
ob = bpy.data.objects.new("e", None)
bpy.context.scene.collection.objects.link(ob)
ob.animation_data_create()
ob.animation_data.action = a
ob.keyframe_insert("location", index=0, frame=1)
print("slots", [s.identifier for s in a.slots], "layers", len(a.layers))
cb = a.layers[0].strips[0].channelbag(a.slots[0])
print("channelbag fcurves", [f.data_path for f in cb.fcurves])
import bpy_extras.anim_utils as au
print("anim_utils", [x for x in dir(au) if not x.startswith("_")])
print("strip attrs", [x for x in dir(a.layers[0].strips[0]) if not x.startswith("_")])
print("ffmpeg", [p.identifier for p in bpy.types.FFmpegSettings.bl_rna.properties][:30])
