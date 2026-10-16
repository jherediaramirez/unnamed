import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import up_config as C, up_loco as L
def run(fam, **kw):
    L.GAITS[fam].update(**kw)
    hs = {}
    for d in C.DIRS:
        g = L.Gait(fam, d)
        nominal = C.SPINE["pelvis"] - (L.SNEAK_BASE["pelvis_drop"] if g.g["crouch"] else 0.0)
        hs[d] = round(L.solve_hip_height(g, None, nominal - .015), 3)
    print(f"[ex] {fam} {kw} hips", hs, "spread", round(max(hs.values()) - min(hs.values()), 3))
for lb in (.5, .56, .62, .68):
    run("Walk", D=.25, D_diag=.25, lead=.47, lead_back=lb)
for lb in (.5, .58, .66):
    run("Sprint", D=.21, D_diag=.21, lead=.45, lead_back=lb)
for lb in (.52, .6, .68):
    run("Sneak", T=54/60, D=.58, D_diag=.47, lead=.5, lead_back=lb)
for lb in (.52, .6):
    run("Sneak", T=48/60, D=.6, D_diag=.5, lead=.5, lead_back=lb)
