import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import up_config as C, up_validate as Vd
from mathutils import Vector
bpy.ops.wm.open_mainfile(filepath=C.MASTER)
body = bpy.data.objects["UP_Body"]
ps = Vd.PartSet(body)
t = ps.tree("leg_r")
worst = None
for vi in ps.verts("leg_l"):
    p = Vector(ps.co[vi]); loc, nrm, idx, dist = t.find_nearest(p)
    if (p - loc).dot(nrm) < 0 and (worst is None or dist > worst[0]):
        worst = (dist, tuple(p), tuple(loc), tuple(nrm), idx)
print("[dbg]", worst)
fs = ps.faces("leg_r")
print("[dbg] face", idx if worst else None, fs[worst[4]] if worst else None, [tuple(ps.co[i]) for i in fs[worst[4]]] if worst else None)
