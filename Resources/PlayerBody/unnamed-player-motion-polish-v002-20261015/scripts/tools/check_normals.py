import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import up_config as C
import numpy as np
bpy.ops.wm.open_mainfile(filepath=C.MASTER)
for obn in ("UP_Body", "UP_Head", "UP_Pickaxe"):
    ob = bpy.data.objects[obn]
    names = list(ob["up_parts"]); vals = np.zeros(len(ob.data.vertices), dtype=np.int64)
    ob.data.attributes["up_part"].data.foreach_get("value", vals)
    me = ob.data
    co = np.array([tuple(v.co) for v in me.vertices])
    bad = {}
    for pi, nm in enumerate(names):
        idx = np.where(vals == pi)[0]
        ctr = co[idx].mean(axis=0)
        for p in me.polygons:
            if vals[p.vertices[0]] != pi: continue
            c = np.array(tuple(p.center)); n = np.array(tuple(p.normal))
            if np.dot(c - ctr, n) < 0:
                bad.setdefault(nm, []).append(tuple(np.round(n, 2)))
    print("[normals]", obn, {k: (len(v), sorted(set(v))[:6]) for k, v in bad.items()})
