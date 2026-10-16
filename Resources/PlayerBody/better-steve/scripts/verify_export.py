"""Re-import the exported FBX/GLB into an empty scene and report what an engine will see.

    blender -b --factory-startup --python scripts/verify_export.py -- <project root>
"""

import bpy
import json
import os
import struct
import sys

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ROOT = os.path.abspath(ARGS[0]) if ARGS else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXPORT = os.path.join(ROOT, "export")

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=os.path.join(EXPORT, "SK_BetterSteve.fbx"), use_anim=True)
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
mesh = next(o for o in bpy.data.objects if o.type == "MESH")
bpy.context.view_layer.update()
co = [mesh.matrix_world @ v.co for v in mesh.data.vertices]
zs = [c.z for c in co]
xs = [c.x for c in co]
print(f"[fbx] armature '{arm.name}' scale {tuple(round(s, 4) for s in arm.scale)}; "
      f"{len(arm.data.bones)} bones; root bone '{arm.data.bones[0].name if arm.data.bones else None}'")
print(f"[fbx] parentless bones: {[b.name for b in arm.data.bones if b.parent is None]}")
print(f"[fbx] mesh height {max(zs) - min(zs):.4f} (min z {min(zs):.4f}), width {max(xs) - min(xs):.4f}; "
      f"{len(mesh.data.vertices)} verts; groups {len(mesh.vertex_groups)}; materials {[m.name for m in mesh.data.materials]}")
img = [i.name for i in bpy.data.images]
print(f"[fbx] embedded images: {img}")
for a in bpy.data.actions:
    print(f"[fbx] action {a.name}: frames {tuple(round(f) for f in a.frame_range)}")

with open(os.path.join(EXPORT, "SK_BetterSteve.glb"), "rb") as f:
    head = f.read(20)
    ln = struct.unpack("<I", head[12:16])[0]
    gl = json.loads(f.read(ln))
print(f"[glb] {len(gl.get('nodes', []))} nodes, {len(gl.get('skins', [{}])[0].get('joints', []))} joints, "
      f"animations {[a.get('name') for a in gl.get('animations', [])]}")
