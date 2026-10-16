"""Rest-pose integrity: deform bones (driven by the control rig) must sit exactly on their rest matrices."""
import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import up_config as C
bpy.ops.wm.open_mainfile(filepath=C.MASTER)
bpy.context.view_layer.update()
sk = bpy.data.objects["UP_Skeleton"]
worst = (0, None)
for pb in sk.pose.bones:
    d = (pb.matrix - pb.bone.matrix_local)
    err = max(abs(d[i][j]) for i in range(4) for j in range(4))
    if err > worst[0]:
        worst = (err, pb.name)
    if err > 1e-4:
        print("[rest] deviates", pb.name, round(err, 5))
print("[rest] worst", worst)
