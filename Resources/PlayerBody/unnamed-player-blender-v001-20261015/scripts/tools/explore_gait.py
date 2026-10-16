import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import up_config as C, up_loco as L
def run(fam, T, D, Dd, lead, extra=""):
    L.GAITS[fam].update(T=T, D=D, D_diag=Dd, lead=lead)
    hs = {}
    for d in C.DIRS:
        g = L.Gait(fam, d)
        nominal = C.SPINE["pelvis"] - (L.SNEAK_BASE["pelvis_drop"] if g.g["crouch"] else 0.0)
        hs[d] = round(L.solve_hip_height(g, None, nominal - .015), 3)
    print(f"[ex] {fam} T={T:.4f} D={D} Dd={Dd} lead={lead} {extra} hips", hs, "spread", round(max(hs.values()) - min(hs.values()), 3))
for D in (.27, .24):
    for lead in (.46, .40):
        run("Walk", 36/60, D, D, lead)
for D in (.225, .2):
    run("Sprint", 32/60, D, D, .44)
for T, D, Dd in ((64/60, .58, .46), (54/60, .6, .5), (54/60, .58, .47), (48/60, .6, .5), (48/60, .62, .52)):
    for lead in (.48, .42):
        run("Sneak", T, D, Dd, lead)
