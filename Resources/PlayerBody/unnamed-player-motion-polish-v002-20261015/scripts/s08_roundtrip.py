"""Stage 7b: round-trip inspection of the exported FBX files in FRESH scenes (no rig, no constraints).

    blender -b --factory-startup --python scripts/s08_roundtrip.py

1) Raw FBX parse (Blender's own io_scene_fbx parser): GlobalSettings units/axes, every LimbNode's Lcl Scaling, takes.
2) Import SK_UnnamedPlayer.fbx: bones, hierarchy, dimensions, materials/texture.
3) Import every animation FBX: same bone set, same rest pose as the SK, frame count, root/scale audit, and for
   locomotion/turn/landing clips the planted-foot drift recomputed on the IMPORTED motion.
Blender re-import success is NOT proof of Unreal import/runtime acceptance.
"""

import glob
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import addon_utils  # noqa: E402
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import up_config as C  # noqa: E402

addon_utils.enable("io_scene_fbx")
from io_scene_fbx import parse_fbx  # noqa: E402

OUT = os.path.join(C.ROOT, "exports")
man = json.load(open(os.path.join(OUT, "UP_manifest_v001.json")))
report = {"note": "Blender round trip only; Unreal import/runtime acceptance still untested"}


def props70(elem):
    out = {}
    for c in elem.elems:
        if c.id == b"Properties70":
            for p in c.elems:
                if p.id == b"P":
                    key = p.props[0].decode() if isinstance(p.props[0], bytes) else p.props[0]
                    out[key] = [v.decode() if isinstance(v, bytes) else v for v in p.props[4:]]
    return out


def raw(path):
    root, version = parse_fbx.parse(path)
    info = {"fbx_version": version}
    for e in root.elems:
        if e.id == b"GlobalSettings":
            g = props70(e)
            info["global"] = {k: g.get(k) for k in ("UpAxis", "UpAxisSign", "FrontAxis", "FrontAxisSign", "CoordAxis",
                                                    "CoordAxisSign", "UnitScaleFactor", "OriginalUnitScaleFactor")}
        if e.id == b"Objects":
            limbs, nulls, scales = [], [], []
            for o in e.elems:
                if o.id == b"Model":
                    kind = o.props[2].decode() if isinstance(o.props[2], bytes) else o.props[2]
                    name = (o.props[1].decode() if isinstance(o.props[1], bytes) else o.props[1]).split("\x00")[0]
                    p = props70(o)
                    s = p.get("Lcl Scaling", [1.0, 1.0, 1.0])
                    if kind == "LimbNode":
                        limbs.append(name)
                        scales.append(max(abs(v - 1.0) for v in s))
                    elif kind == "Null":
                        nulls.append((name, s))
            info["limb_nodes"] = len(limbs)
            info["limb_scale_max_dev"] = max(scales) if scales else None
            info["null_nodes"] = nulls
            info["takes"] = [(o.props[1].decode() if isinstance(o.props[1], bytes) else o.props[1]).split("\x00")[0]
                             for o in e.elems if o.id == b"AnimationStack"]
    return info


report["raw_SK"] = raw(os.path.join(OUT, "SK_UnnamedPlayer.fbx"))
report["raw_SM_Pickaxe"] = raw(os.path.join(OUT, "SM_UP_Pickaxe.fbx"))
r_all = raw(os.path.join(OUT, "UP_AllClips.fbx"))
report["raw_AllClips"] = {"takes": len(r_all.get("takes", [])), "limb_scale_max_dev": r_all.get("limb_scale_max_dev"),
                          "global": r_all.get("global"), "take_names_sample": r_all.get("takes", [])[:5],
                          "non_clip_takes": [t for t in r_all.get("takes", []) if not t.split("|")[-1].startswith("UP_")]}
print("[rt] raw SK", json.dumps(report["raw_SK"]["global"]), "limbs", report["raw_SK"]["limb_nodes"],
      "limb scale dev", report["raw_SK"]["limb_scale_max_dev"], "nulls", report["raw_SK"]["null_nodes"])


def fresh():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = C.FPS


def import_fbx(path):
    bpy.ops.import_scene.fbx(filepath=path, use_anim=True, automatic_bone_orientation=False,
                             primary_bone_axis="Y", secondary_bone_axis="X")
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    return arm


# ---------------------------------------------------------------- skeletal mesh
fresh()
arm = import_fbx(os.path.join(OUT, "SK_UnnamedPlayer.fbx"))
bpy.context.view_layer.update()
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
pts = []
dg = bpy.context.evaluated_depsgraph_get()
for o in meshes:
    ev = o.evaluated_get(dg)
    me = ev.to_mesh()
    pts += [ev.matrix_world @ v.co for v in me.vertices]
    ev.to_mesh_clear()
mn = [min(p[i] for p in pts) for i in range(3)]
mx = [max(p[i] for p in pts) for i in range(3)]
sk_rest = {b.name: (arm.matrix_world @ b.matrix_local) for b in arm.data.bones}
dev_rest = max((Vector(sk_rest[n].to_translation()) - Vector(man["skeleton"]["bones"][n]["head_m"])).length
               for n in man["skeleton"]["bones"])
report["SK"] = {"armature_object_scale": list(arm.scale), "bones": len(arm.data.bones),
                "bone_names_match": sorted(b.name for b in arm.data.bones) == sorted(man["skeleton"]["order"]),
                "parentless": [b.name for b in arm.data.bones if b.parent is None],
                "hierarchy_match": all((arm.data.bones[n].parent.name if arm.data.bones[n].parent else None) ==
                                       man["skeleton"]["bones"][n]["parent"] for n in man["skeleton"]["order"]),
                "height_m": mx[2] - mn[2], "soles_z_m": mn[2], "bounds_min": mn, "bounds_max": mx,
                "rest_head_max_dev_vs_manifest_m": dev_rest,
                "meshes": [o.name for o in meshes], "materials": sorted({m.name for o in meshes for m in o.data.materials}),
                "images": [(i.name, list(i.size), bool(i.packed_file)) for i in bpy.data.images],
                "vertex_groups": sorted({g.name for o in meshes for g in o.vertex_groups})}
print("[rt] SK", json.dumps({k: v for k, v in report["SK"].items() if k not in ("vertex_groups", "bounds_min", "bounds_max")},
                           default=str))
SK_REST = {n: m.copy() for n, m in sk_rest.items()}

# ---------------------------------------------------------------- animation files
SOLE = {"heel": ("foot", (C.J["heel_y"], 0.0)), "mid": ("foot", (0.0, 0.0)), "ball": ("ball", (C.J["ball_y"], 0.0)),
        "tip": ("ball", (C.J["toe_tip_y"], 0.0))}
clips = {c["name"]: c for c in man["clips"]}
anim_rep = {}
worst = {"rest_dev_m": 0.0, "root_motion": 0.0, "scale_dev": 0.0, "frame_mismatch": []}
for path in sorted(glob.glob(os.path.join(OUT, "anims", "A_*.fbx"))):
    name = os.path.basename(path)[2:-4]
    fresh()
    arm = import_fbx(path)
    act = arm.animation_data.action if arm.animation_data else None
    c = clips[name]
    bpy.context.view_layer.update()
    rest_dev = max(((arm.matrix_world @ b.matrix_local).to_translation() - SK_REST[b.name].to_translation()).length
                   for b in arm.data.bones)
    fr = tuple(act.frame_range) if act else (0, 0)
    nframes = int(round(fr[1] - fr[0]))
    e = {"bones": len(arm.data.bones), "rest_dev_vs_SK_m": rest_dev, "frames": nframes,
         "expected_frames": c["frames"], "action": act.name if act else None}
    if nframes != c["frames"]:
        worst["frame_mismatch"].append((name, nframes, c["frames"]))
    sc = bpy.context.scene
    start = int(fr[0])
    rootw, scdev = [], 0.0
    vel = Vector((*c.get("travel_vector_char_space", (0.0, 0.0)), 0.0)) if c.get("speed_mps") else Vector()
    yaw_by_frame = {}
    if c.get("turn"):
        tot = c["turn"]["body_yaw_total_deg"]
        yaw_by_frame = {p["frame"]: tot * p["progress"] for p in c["turn"]["body_yaw_progress"]}
    track = {}
    local_pts = {}
    for side in "lr":
        hx = (C.LEG_X[0] + C.LEG_X[1]) / 2 * (1 if side == "l" else -1)
        for key, (bone, (y, z)) in SOLE.items():
            b = arm.data.bones[f"{bone}_{side}"]
            p_world_rest = Vector((hx, y, z))
            local_pts[(side, key)] = (b.name, (arm.matrix_world @ b.matrix_local).inverted() @ p_world_rest)
    for f in range(nframes + 1):
        sc.frame_set(start + f)
        P = arm.pose.bones
        rootw.append((arm.matrix_world @ P["root"].matrix).copy())
        for pb in P:
            scdev = max(scdev, max(abs(s - 1.0) for s in pb.matrix.to_scale()))
        R = Matrix.Rotation(math.radians(yaw_by_frame.get(f, 0.0)), 4, "Z")
        for k, (bn, lp) in local_pts.items():
            w = R @ (arm.matrix_world @ P[bn].matrix @ lp)
            track.setdefault(k, []).append((f, w + vel * (f / C.FPS)))
    e["root_motion_m"] = max((m.to_translation() - rootw[0].to_translation()).length for m in rootw)
    e["scale_dev"] = scdev - 0.0
    # planted drift within the clip's own stance windows (gaits) / always-planted clips (idle, turns, landings)
    fam = c.get("family")
    if fam in ("Walk", "Sprint", "Sneak") or name in ("UP_Idle", "UP_SneakIdle", "UP_Land_Soft", "UP_Land_Firm") or \
            c.get("turn"):
        D = c.get("duty_factor")
        dmax = 0.0
        for k, seq in track.items():
            side = k[0]
            start_p = None
            for f, w in seq:
                planted = w.z <= .003
                if D is not None:
                    ps = (f / nframes - (0.0 if side == "l" else .5)) % 1.0
                    planted = planted and ps <= D + 1e-6
                if c.get("turn"):
                    st = c["turn"]["steps"][side]
                    planted = planted and not (st["lift_frame"] - 1e-6 <= f <= st["plant_frame"] + 1e-6)
                if planted:
                    if start_p is None:
                        start_p = w
                    dmax = max(dmax, (w - start_p).to_2d().length)
                else:
                    start_p = None
        e["planted_drift_cm"] = round(dmax * 100, 3)
    worst["rest_dev_m"] = max(worst["rest_dev_m"], rest_dev)
    worst["root_motion"] = max(worst["root_motion"], e["root_motion_m"])
    worst["scale_dev"] = max(worst["scale_dev"], scdev)
    anim_rep[name] = e
report["anims"] = anim_rep
report["anims_summary"] = {"files": len(anim_rep), **worst,
                           "max_planted_drift_cm": max((e.get("planted_drift_cm", 0.0) for e in anim_rep.values())),
                           "bones_all_match": all(e["bones"] == man["skeleton"]["bone_count"] for e in anim_rep.values())}
json.dump(report, open(os.path.join(C.ROOT, "validation", "s08_roundtrip.json"), "w"), indent=1, default=str)
print("[rt] anims", json.dumps(report["anims_summary"], default=str))
print("[rt] AllClips takes", report["raw_AllClips"]["takes"], "non-clip takes", report["raw_AllClips"]["non_clip_takes"][:5])
