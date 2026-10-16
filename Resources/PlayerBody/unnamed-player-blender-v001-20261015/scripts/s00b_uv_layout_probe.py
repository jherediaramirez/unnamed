"""Probe the source GLB's per-face UV layout (skin pixels) so rebuilt parts keep the exact pixel mapping.

    blender -b --factory-startup --python scripts/s00b_uv_layout_probe.py -- <work root>
"""
import bpy
import json
import os
import sys
from collections import defaultdict

ARGS = sys.argv[sys.argv.index("--") + 1:]
ROOT = os.path.abspath(ARGS[0])
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=os.path.join(ROOT, "source", "minecraft_steve_rigged.glb"))
dg = bpy.context.evaluated_depsgraph_get()
out = {}
for o in bpy.data.objects:
    if o.type != "MESH" or not o.modifiers:
        continue
    ev = o.evaluated_get(dg)
    me = ev.to_mesh()
    uv = me.uv_layers.active.data
    groups = defaultdict(list)
    for p in me.polygons:
        n = (ev.matrix_world.to_3x3() @ p.normal).normalized()
        key = max(range(3), key=lambda i: abs(n[i]))
        key = ("+" if n[key] > 0 else "-") + "XYZ"[key]
        for li in p.loop_indices:
            co = ev.matrix_world @ me.vertices[me.loops[li].vertex_index].co
            u, v = uv[li].uv
            groups[key].append((tuple(co), (u * 64, (1 - v) * 64)))  # skin px, origin top-left
    part = {}
    for key, pts in groups.items():
        us = [p[1][0] for p in pts]
        vs = [p[1][1] for p in pts]
        cs = [p[0] for p in pts]
        # orientation: which 3D axis increases with u and with v
        def corr(a, b):
            ma, mb = sum(a) / len(a), sum(b) / len(b)
            num = sum((x - ma) * (y - mb) for x, y in zip(a, b))
            da = sum((x - ma) ** 2 for x in a) ** .5
            db = sum((y - mb) ** 2 for y in b) ** .5
            return num / (da * db) if da > 1e-9 and db > 1e-9 else 0
        ucorr = {ax: round(corr(us, [c[i] for c in cs]), 3) for i, ax in enumerate("xyz")}
        vcorr = {ax: round(corr(vs, [c[i] for c in cs]), 3) for i, ax in enumerate("xyz")}
        part[key] = {"u": [round(min(us), 3), round(max(us), 3)], "v": [round(min(vs), 3), round(max(vs), 3)],
                     "u_axis": max(ucorr, key=lambda k: abs(ucorr[k])), "u_sign": ucorr[max(ucorr, key=lambda k: abs(ucorr[k]))],
                     "v_axis": max(vcorr, key=lambda k: abs(vcorr[k])), "v_sign": vcorr[max(vcorr, key=lambda k: abs(vcorr[k]))],
                     "bounds": [[round(min(c[i] for c in cs), 4) for i in range(3)], [round(max(c[i] for c in cs), 4) for i in range(3)]]}
    out[o.name] = part
    ev.to_mesh_clear()
json.dump(out, open(os.path.join(ROOT, "validation", "s00b_uv_layout.json"), "w"), indent=1)
for name, part in out.items():
    print("PART", name)
    for k in sorted(part):
        d = part[k]
        print(f"  {k}: u{d['u']} v{d['v']}  u->{d['u_axis']}{'+' if d['u_sign'] > 0 else '-'} v->{d['v_axis']}{'+' if d['v_sign'] > 0 else '-'}  b{d['bounds']}")
