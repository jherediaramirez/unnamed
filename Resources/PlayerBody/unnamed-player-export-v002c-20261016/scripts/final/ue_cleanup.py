"""M0 cleanup (after a clean final run + log scan): delete the Proof, ProofB, ProofC folders, then gate 8 over /Game
and the final asset list."""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue_m0_lib as L  # noqa: E402

EAL = L.EAL
out = os.path.join(L.FINAL, "ue_cleanup_" + time.strftime("%Y%m%d_%H%M%S") + ".json")
R = {"deleted": {}}


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def point_tri(p, a, b, c):
    """Distance from p to triangle abc (Ericson, closest point on triangle)."""
    ab, ac, ap = sub(b, a), sub(c, a), sub(p, a)
    d1, d2 = dot(ab, ap), dot(ac, ap)
    if d1 <= 0 and d2 <= 0:
        q = a
    else:
        bp = sub(p, b)
        d3, d4 = dot(ab, bp), dot(ac, bp)
        cp = sub(p, c)
        d5, d6 = dot(ab, cp), dot(ac, cp)
        vc, vb, va = d1 * d4 - d3 * d2, d5 * d2 - d1 * d6, d3 * d6 - d5 * d4
        if d3 >= 0 and d4 <= d3:
            q = b
        elif d6 >= 0 and d5 <= d6:
            q = c
        elif vc <= 0 and d1 >= 0 and d3 <= 0:
            v = d1 / (d1 - d3)
            q = (a[0] + v * ab[0], a[1] + v * ab[1], a[2] + v * ab[2])
        elif vb <= 0 and d2 >= 0 and d6 <= 0:
            w = d2 / (d2 - d6)
            q = (a[0] + w * ac[0], a[1] + w * ac[1], a[2] + w * ac[2])
        elif va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0:
            w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
            q = (b[0] + w * (c[0] - b[0]), b[1] + w * (c[1] - b[1]), b[2] + w * (c[2] - b[2]))
        else:
            den = 1.0 / (va + vb + vc)
            v, w = vb * den, vc * den
            q = (a[0] + ab[0] * v + ac[0] * w, a[1] + ab[1] * v + ac[1] * w, a[2] + ab[2] * v + ac[2] * w)
    d = sub(p, q)
    return dot(d, d) ** 0.5


def pickaxe_tip():
    sm = EAL.load_asset("/Game/Characters/UnnamedPlayer/SM_UP_Pickaxe")
    tip = L.MAN["pickaxe"]["tip_local_m"]
    cands = {"(0,+42.75,+26.5)": (0.0, tip[1] * 100, tip[2] * 100), "(0,-42.75,+26.5)": (0.0, -tip[1] * 100, tip[2] * 100)}
    best = {k: 1e9 for k in cands}
    ntri = 0
    for sec in range(sm.get_num_sections(0)):
        r = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(sm, 0, sec)
        vs = [(v.x, v.y, v.z) for v in r[0]]
        tris = list(r[1])
        for i in range(0, len(tris), 3):
            a, b, c = vs[tris[i]], vs[tris[i + 1]], vs[tris[i + 2]]
            ntri += 1
            for k, p in cands.items():
                best[k] = min(best[k], point_tri(p, a, b, c))
    bb = sm.get_bounding_box()
    out = {"triangles": ntri, "tip_to_surface_cm": best, "bounds_min": [bb.min.x, bb.min.y, bb.min.z],
           "bounds_max": [bb.max.x, bb.max.y, bb.max.z],
           "origin_inside_bounds": bb.min.x <= 0 <= bb.max.x and bb.min.y <= 0 <= bb.max.y and bb.min.z <= 0 <= bb.max.z,
           "note": "UE axes: socket readback maps Blender tool +Y to UE -Y, so (0,-42.75,26.5) is the consistent tip"}
    out["pass"] = best["(0,-42.75,+26.5)"] <= 0.01 and out["origin_inside_bounds"]
    return out


import unreal  # noqa: E402

try:
    R["pickaxe_tip"] = pickaxe_tip()
    if not R["pickaxe_tip"]["pass"]:
        raise RuntimeError("pickaxe tip/origin check failed; proof folders kept")
    for d in ("/Game/Characters/UnnamedPlayer_Proof", "/Game/Characters/UnnamedPlayer_ProofB",
              "/Game/Characters/UnnamedPlayer_ProofC"):
        R["deleted"][d] = EAL.delete_directory(d) if EAL.does_directory_exist(d) else "absent"
    allp = EAL.list_assets("/Game", recursive=True, include_folder=False)
    cls = {a: str(EAL.find_asset_data(a).asset_class_path.asset_name) for a in allp}
    g8 = {"skeletons": [a for a, c in cls.items() if c == "Skeleton"],
          "redirectors": [a for a, c in cls.items() if c == "ObjectRedirector"],
          "skeletal_meshes": [a for a, c in cls.items() if c == "SkeletalMesh"],
          "static_meshes": [a for a, c in cls.items() if c == "StaticMesh"],
          "outside_final_folder": [a for a in allp if not a.startswith("/Game/Characters/UnnamedPlayer/")]}
    g8["pass"] = (len(g8["skeletons"]) == 1 and not g8["redirectors"] and len(g8["skeletal_meshes"]) == 1
                  and len(g8["static_meshes"]) == 1 and not g8["outside_final_folder"])
    R["8_assets_game"] = g8
    R["assets"] = {a: cls[a] for a in sorted(allp)}
    R["counts"] = {c: sum(1 for v in cls.values() if v == c) for c in sorted(set(cls.values()))}
except Exception:  # noqa: BLE001
    import traceback
    R["fatal"] = traceback.format_exc()
json.dump(R, open(out, "w"), indent=1, default=str)
L.log("CLEANUP", json.dumps({"pickaxe_tip": R.get("pickaxe_tip"), "deleted": R.get("deleted"), "gate8": R.get("8_assets_game", {}).get("pass"),
                             "counts": R.get("counts")}))
