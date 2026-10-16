"""Stage 1 audit of the supplied Steve GLB. Read-only on the source.

    blender -b --factory-startup --python scripts/s00_audit_source.py -- <work root>

Writes validation/s00_source_audit.json, source/original_skin_texture.<ext> (exact embedded bytes) and
previews/s00_source_*.png. The source GLB is opened read-only and never saved.
"""

import bpy
import hashlib
import json
import math
import os
import struct
import sys

from mathutils import Vector

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ROOT = os.path.abspath(ARGS[0])
SRC = os.path.join(ROOT, "source", "minecraft_steve_rigged.glb")
OUT = {}


def sha256(data):
    return hashlib.sha256(data).hexdigest().upper()


# ---------------------------------------------------------------- raw glTF container
raw = open(SRC, "rb").read()
OUT["file"] = {"path": SRC, "bytes": len(raw), "sha256": sha256(raw)}
magic, version, length = struct.unpack("<4sII", raw[:12])
off = 12
chunks = []
while off < len(raw):
    clen, ctype = struct.unpack("<I4s", raw[off:off + 8])
    chunks.append((ctype, raw[off + 8:off + 8 + clen]))
    off += 8 + clen
gl = json.loads(chunks[0][1])
binchunk = chunks[1][1] if len(chunks) > 1 else b""
OUT["gltf"] = {
    "magic": magic.decode(), "version": version, "asset": gl.get("asset"),
    "extensionsUsed": gl.get("extensionsUsed"), "scene_extras": [s.get("extras") for s in gl.get("scenes", [])],
    "counts": {k: len(gl.get(k, [])) for k in ("nodes", "meshes", "skins", "animations", "materials", "textures",
                                                "images", "samplers", "accessors", "cameras")},
    "skins": [{"name": s.get("name"), "joints": len(s["joints"]), "skeleton": s.get("skeleton"),
               "joint_names": [gl["nodes"][j].get("name") for j in s["joints"]]} for s in gl.get("skins", [])],
    "meshes": [{"name": m.get("name"), "primitives": len(m["primitives"]),
                "attributes": sorted(m["primitives"][0]["attributes"].keys()),
                "material": m["primitives"][0].get("material")} for m in gl.get("meshes", [])],
    "nodes": [{"i": i, "name": n.get("name"), "mesh": n.get("mesh"), "skin": n.get("skin"),
               "children": n.get("children"), "t": n.get("translation"), "r": n.get("rotation"),
               "s": n.get("scale"), "extras": n.get("extras")} for i, n in enumerate(gl.get("nodes", []))],
    "materials": gl.get("materials"), "samplers": gl.get("samplers"), "textures": gl.get("textures"),
}
images = []
for i, im in enumerate(gl.get("images", [])):
    entry = {"name": im.get("name"), "mimeType": im.get("mimeType"), "bufferView": im.get("bufferView")}
    if "bufferView" in im:
        bv = gl["bufferViews"][im["bufferView"]]
        data = binchunk[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]]
        ext = {"image/jpeg": "jpg", "image/png": "png"}.get(im.get("mimeType"), "bin")
        dst = os.path.join(ROOT, "source", f"original_skin_texture.{ext}" if i == 0 else f"original_image_{i}.{ext}")
        with open(dst, "wb") as f:
            f.write(data)
        entry.update({"bytes": len(data), "sha256": sha256(data), "extracted_to": dst})
        if ext == "jpg":  # read the SOF marker for pixel size
            p = 2
            while p < len(data):
                if data[p] != 0xFF:
                    break
                marker = data[p + 1]
                seglen = struct.unpack(">H", data[p + 2:p + 4])[0]
                if marker in (0xC0, 0xC1, 0xC2):
                    h, w = struct.unpack(">HH", data[p + 5:p + 9])
                    entry["pixels"] = [w, h]
                    entry["sof_marker"] = hex(marker)
                    break
                p += 2 + seglen
        elif ext == "png":
            w, h = struct.unpack(">II", data[16:24])
            entry["pixels"] = [w, h]
    images.append(entry)
OUT["gltf"]["images"] = images

# ---------------------------------------------------------------- Blender import
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
scene = bpy.context.scene
dg = bpy.context.evaluated_depsgraph_get()
objs = []
for o in bpy.data.objects:
    e = {"name": o.name, "type": o.type, "parent": o.parent.name if o.parent else None,
         "parent_type": o.parent_type, "parent_bone": o.parent_bone or None,
         "location": [round(v, 6) for v in o.location], "rotation_mode": o.rotation_mode,
         "rotation_quaternion": [round(v, 6) for v in o.rotation_quaternion],
         "rotation_euler_deg": [round(math.degrees(v), 4) for v in o.rotation_euler],
         "scale": [round(v, 6) for v in o.scale], "hide_render": o.hide_render, "hide_viewport": o.hide_viewport,
         "custom_props": {k: str(o[k])[:200] for k in o.keys() if not k.startswith("_")},
         "collections": [c.name for c in o.users_collection]}
    if o.type == "MESH":
        me = o.data
        e["mesh"] = {"verts": len(me.vertices), "faces": len(me.polygons), "edges": len(me.edges),
                     "uv_layers": [u.name for u in me.uv_layers], "materials": [m.name if m else None for m in me.materials],
                     "vertex_groups": [g.name for g in o.vertex_groups],
                     "modifiers": [(m.type, getattr(m, "object", None) and m.object.name) for m in o.modifiers],
                     "shape_keys": [k.name for k in me.shape_keys.key_blocks] if me.shape_keys else []}
        # weight usage per group
        use = {}
        for v in me.vertices:
            for g in v.groups:
                if g.weight > 1e-4:
                    nm = o.vertex_groups[g.group].name
                    u = use.setdefault(nm, [0, 0])
                    u[0] += 1
                    if g.weight < .999:
                        u[1] += 1
        e["mesh"]["weighted_groups"] = {k: {"verts": v[0], "partial": v[1]} for k, v in sorted(use.items())}
        nw = [len([g for g in v.groups if g.weight > 1e-4]) for v in me.vertices]
        e["mesh"]["max_influences"] = max(nw) if nw else 0
        e["mesh"]["unweighted_verts"] = sum(1 for n in nw if n == 0)
        ev = o.evaluated_get(dg)
        em = ev.to_mesh()
        co = [ev.matrix_world @ v.co for v in em.vertices]
        e["evaluated_bounds"] = {"min": [round(min(c[i] for c in co), 6) for i in range(3)],
                                 "max": [round(max(c[i] for c in co), 6) for i in range(3)]}
        ev.to_mesh_clear()
        rest = [o.matrix_world @ v.co for v in me.vertices]
        e["rest_bounds"] = {"min": [round(min(c[i] for c in rest), 6) for i in range(3)],
                            "max": [round(max(c[i] for c in rest), 6) for i in range(3)]}
    if o.type == "ARMATURE":
        arm = o.data
        bones = []
        for b in arm.bones:
            bones.append({"name": b.name, "parent": b.parent.name if b.parent else None,
                          "head": [round(v, 5) for v in (o.matrix_world @ b.head_local)],
                          "tail": [round(v, 5) for v in (o.matrix_world @ b.tail_local)],
                          "length_local": round(b.length, 5), "use_deform": b.use_deform,
                          "use_connect": b.use_connect, "children": [c.name for c in b.children]})
        e["armature"] = {"bones": bones, "bone_count": len(bones), "display_type": arm.display_type}
        e["pose_custom_shapes"] = {pb.name: pb.custom_shape.name for pb in o.pose.bones if pb.custom_shape}
    objs.append(e)
OUT["blender_objects"] = objs
OUT["blender_actions"] = [a.name for a in bpy.data.actions]
OUT["blender_images"] = [{"name": i.name, "size": list(i.size), "packed": bool(i.packed_file),
                          "colorspace": i.colorspace_settings.name} for i in bpy.data.images]
OUT["blender_materials"] = []
for m in bpy.data.materials:
    nodes = []
    if m.node_tree:
        for n in m.node_tree.nodes:
            d = {"type": n.type}
            if n.type == "TEX_IMAGE":
                d.update({"image": n.image.name if n.image else None, "interpolation": n.interpolation})
            nodes.append(d)
    OUT["blender_materials"].append({"name": m.name, "nodes": nodes, "blend_method": getattr(m, "blend_method", None)})

# skinned character meshes vs helpers
skinned = [e for e in objs if e["type"] == "MESH" and any(t == "ARMATURE" for t, _ in e["mesh"]["modifiers"])]
helpers = [e for e in objs if e["type"] == "MESH" and e not in skinned]
OUT["summary"] = {
    "skinned_meshes": [e["name"] for e in skinned],
    "non_skinned_mesh_objects": [e["name"] for e in helpers],
    "empties": [e["name"] for e in objs if e["type"] == "EMPTY"],
    "armatures": [e["name"] for e in objs if e["type"] == "ARMATURE"],
}
if skinned:
    mn = [min(e["evaluated_bounds"]["min"][i] for e in skinned) for i in range(3)]
    mx = [max(e["evaluated_bounds"]["max"][i] for e in skinned) for i in range(3)]
    OUT["summary"]["evaluated_bounds_skinned"] = {"min": mn, "max": mx, "height": mx[2] - mn[2],
                                                 "width_x": mx[0] - mn[0], "depth_y": mx[1] - mn[1]}
    OUT["summary"]["normalize_factor_to_1p8"] = 1.8 / (mx[2] - mn[2])

json.dump(OUT, open(os.path.join(ROOT, "validation", "s00_source_audit.json"), "w"), indent=1, default=str)
print("[audit] wrote validation/s00_source_audit.json")
print(json.dumps(OUT["summary"], indent=1))

# ---------------------------------------------------------------- quick look renders (source scale)
mn = Vector(OUT["summary"]["evaluated_bounds_skinned"]["min"])
mx = Vector(OUT["summary"]["evaluated_bounds_skinned"]["max"])
ctr = (mn + mx) / 2
size = max(mx - mn)
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "FLAT"
scene.display.shading.color_type = "TEXTURE"
scene.render.resolution_x, scene.render.resolution_y = 600, 800
scene.render.film_transparent = False
world = bpy.data.worlds.new("w") if not scene.world else scene.world
scene.world = world
cam_data = bpy.data.cameras.new("audit_cam")
cam_data.type = "ORTHO"
cam_data.ortho_scale = size * 1.15
cam = bpy.data.objects.new("audit_cam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
for label, direction in (("front", Vector((0, -1, 0))), ("side", Vector((1, 0, 0))), ("back", Vector((0, 1, 0)))):
    cam.location = ctr + direction * size * 3
    cam.rotation_euler = (-direction).to_track_quat("-Z", "Z").to_euler()
    scene.render.filepath = os.path.join(ROOT, "previews", f"s00_source_{label}.png")
    bpy.ops.render.render(write_still=True)
print("[audit] renders done")
