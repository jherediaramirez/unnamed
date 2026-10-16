"""UP posefix3 export runner (derived from v001 s07_export.py; PLAN 4.2). The source is opened, never saved.

    blender -b --factory-startup --python export_up.py -- --source <blend> --expect-sha256 <hex> --dest <empty dir>

Differences from s07: explicit source gated on SHA-256, refuses a non-empty dest, no rmtree, never reads
up_config.MASTER, asserts the 66 UP_* actions + rest pose against the v001 manifest, clears the rig's leftover
RIG_UP_Pickaxe_StrikeA assignment, adds bone-parented SOCKET_* empties to the mesh FBX only, drops fp_anchor and
UP_AllClips. FBX settings and the in-memory centimetre conversion are s07's, unchanged.
"""

import argparse
import hashlib
import json
import os
import shutil
import sys
sys.dont_write_bytecode = True

V001 = r"C:\dev\output\unnamed-player-blender-v001-20261015"
POLISH = r"C:\dev\output\unnamed-player-motion-polish-v002-20261015"
sys.path.insert(0, os.path.join(V001, "scripts"))
import addon_utils  # noqa: E402
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import up_anim as A  # noqa: E402
import up_config as C  # noqa: E402  (constants only; C.MASTER is never used)

ap = argparse.ArgumentParser()
ap.add_argument("--source", required=True)
ap.add_argument("--expect-sha256", required=True)
ap.add_argument("--dest", required=True)
args = ap.parse_args(sys.argv[sys.argv.index("--") + 1:])


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest().upper()


SRC = os.path.abspath(args.source)
src_hash_before = sha(SRC)
assert src_hash_before == args.expect_sha256.upper(), f"source hash {src_hash_before} != expected"
OUT = os.path.abspath(args.dest)
if os.path.exists(OUT):
    assert os.path.isdir(OUT) and not os.listdir(OUT), f"dest not empty: {OUT}"
os.makedirs(OUT, exist_ok=True)
ANIMS = os.path.join(OUT, "anims")
os.makedirs(ANIMS)

man1 = json.load(open(os.path.join(V001, "exports", "UP_manifest_v001.json")))
pk_polish = json.load(open(os.path.join(POLISH, "validation", "s06_pickaxe.json")))

bpy.ops.wm.open_mainfile(filepath=SRC)
scene = bpy.context.scene
skel = bpy.data.objects["UP_Skeleton"]
body = bpy.data.objects["UP_Body"]
head = bpy.data.objects["UP_Head"]
rig = bpy.data.objects["UP_Rig"]
pick = bpy.data.objects["UP_Pickaxe"]
checks = {}

# ---------------------------------------------------------------- assertions against the v001 manifest
clip_actions = sorted(a.name for a in bpy.data.actions if a.name.startswith("UP_") and not a.name.endswith("_upper"))
m1clips = {c["name"]: c for c in man1["clips"]}
assert clip_actions == sorted(m1clips), f"action set differs: {set(clip_actions) ^ set(m1clips)}"
frames = {n: int(round(bpy.data.actions[n].frame_range[1])) for n in clip_actions}
bad = {n: (frames[n], m1clips[n]["frames"]) for n in clip_actions if frames[n] != m1clips[n]["frames"]}
assert not bad, f"frame counts differ: {bad}"
starts = {n: bpy.data.actions[n].frame_range[0] for n in clip_actions if abs(bpy.data.actions[n].frame_range[0]) > 1e-6}
assert not starts, f"actions not starting at 0: {starts}"
checks["actions"] = {"count": len(clip_actions), "names_frames_match_v001_manifest": True}

rest = {}
rest_dev = 0.0
for b in skel.data.bones:
    m = b.matrix_local
    rel = (b.parent.matrix_local.inverted() @ m) if b.parent else m
    rest[b.name] = {"parent": b.parent.name if b.parent else None,
                    "head_m": [round(c, 6) for c in b.head_local], "tail_m": [round(c, 6) for c in b.tail_local],
                    "length_m": round(b.length, 6),
                    "parent_relative_location_m": [round(c, 6) for c in rel.to_translation()],
                    "parent_relative_rotation_wxyz": [round(c, 6) for c in rel.to_quaternion()],
                    "local_axes_world": {"x": [round(c, 4) for c in m.col[0][:3]], "y": [round(c, 4) for c in m.col[1][:3]],
                                         "z": [round(c, 4) for c in m.col[2][:3]]}}
    o = man1["skeleton"]["bones"][b.name]
    assert o["parent"] == rest[b.name]["parent"], b.name
    rest_dev = max(rest_dev, (Vector(o["head_m"]) - b.head_local).length, (Vector(o["tail_m"]) - b.tail_local).length)
assert len(rest) == 22 and sorted(rest) == sorted(man1["skeleton"]["order"])
assert rest_dev <= 1e-4, f"rest pose deviates {rest_dev} m"
checks["rest_pose_max_dev_vs_v001_m"] = rest_dev

RB = rig.data.bones
sock_pick = RB["c_hand_r"].matrix_local.inverted() @ RB["c_grip_r"].matrix_local
sock_grip_l = RB["c_hand_l"].matrix_local.inverted() @ RB["c_grip_l"].matrix_local
eye_world = Vector((0, -C.HEAD / 2, sum(C.EYE_ROW) / 2))
sock_eye = skel.data.bones["head"].matrix_local.inverted() @ Matrix.Translation(eye_world)
SOCKETS = [("SOCKET_hand_r_grip", "hand_r", sock_pick, "hand_r_pickaxe",
            "pickaxe grip: tool +Y along the handle to the head, +Z toward the long point; SM_UP_Pickaxe origin/axes "
            "match, so attach with an identity relative transform"),
           ("SOCKET_hand_l_grip", "hand_l", sock_grip_l, "hand_l_grip", "left-hand grip centre (same tool frame)"),
           ("SOCKET_head_eye", "head", sock_eye, "head_eye", "painted-eye point on the head front face (debug)")]
m1sock = {s["name"]: s for s in man1["sockets"]}
sockets = []
for name, bone, m, old, note in SOCKETS:
    loc = [round(c * 100, 4) for c in m.to_translation()]
    q = [round(c, 6) for c in m.to_quaternion()]
    dl = max(abs(a - b) for a, b in zip(loc, m1sock[old]["location_cm"]))
    assert dl <= .01, f"{name} moved {dl} cm vs v001"
    sockets.append({"name": name, "unreal_socket": name[len("SOCKET_"):], "bone": bone, "location_cm": loc,
                    "rotation_wxyz": q, "matrix_bone_space_cm": [[round(v * (100 if (j == 3 and i < 3) else 1), 6)
                                                                    for j, v in enumerate(r)] for i, r in enumerate(m)],
                    "v001_name": old, "note": note})
checks["sockets_vs_v001_cm"] = "<=0.01"

# Investigation: export only the 20 pose actions, with unchanged constant extrapolation.
clip_actions = [n for n in clip_actions if frames[n] == 0]

# ---------------------------------------------------------------- clean export copy (in memory)
leftover = {"rig_action": rig.animation_data.action.name if rig.animation_data and rig.animation_data.action else None,
            "skeleton_constraints": sum(len(pb.constraints) for pb in skel.pose.bones), "frame_current": scene.frame_current}
if rig.animation_data:
    rig.animation_data.action = None
scene.frame_set(0)
for pb in skel.pose.bones:
    for c in list(pb.constraints):
        pb.constraints.remove(c)
    pb.location, pb.rotation_quaternion, pb.scale = (0, 0, 0), (1, 0, 0, 0), (1, 1, 1)
for o in list(bpy.data.objects):
    if o not in (skel, body, head, pick):
        bpy.data.objects.remove(o, do_unlink=True)
if skel.animation_data:
    skel.animation_data.action = None
    for t in list(skel.animation_data.nla_tracks):
        skel.animation_data.nla_tracks.remove(t)
bpy.context.view_layer.update()
pose_dev = max((pb.matrix - pb.bone.matrix_local).to_translation().length for pb in skel.pose.bones)
assert pose_dev < 1e-6, f"skeleton not at rest: {pose_dev}"
leftover["cleared"] = True
leftover["pose_vs_rest_after_clear_m"] = pose_dev
checks["leftover"] = leftover
pick.parent = None
pick.matrix_world = Matrix()
body.name, head.name = "SK_UnnamedPlayer_Body", "SK_UnnamedPlayer_Head"
skel.name = "Armature"   # posefix3: UE drops a root-level parent node named "armature" (FbxScene.cpp:940-943); "UP_Skeleton" became an extra root bone in v002a
skel.data.name = "UP_Skeleton"
scene.unit_settings.scale_length = .01
skel.scale = (100, 100, 100)
pick.scale = (100, 100, 100)
bpy.context.view_layer.update()
for o in (skel, body, head, pick):
    o.select_set(True)
bpy.context.view_layer.objects.active = skel
bpy.ops.object.transform_apply(location=False, rotation=False, scale=True, properties=True)
for name in clip_actions:
    for fc in A.action_fcurves(bpy.data.actions[name]):
        if fc.data_path.endswith(".location"):
            for kp in fc.keyframe_points:
                kp.co.y *= 100
                kp.handle_left.y *= 100
                kp.handle_right.y *= 100
            fc.update()
bpy.context.view_layer.update()
zs = [(body.matrix_world @ v.co).z for v in body.data.vertices] + [(head.matrix_world @ v.co).z for v in head.data.vertices]
checks["cm_copy"] = {"height_BU": max(zs) - min(zs), "soles_BU": min(zs), "skeleton_scale": list(skel.scale)}
print(f"[export] cm copy: height {max(zs) - min(zs):.3f} BU, soles {min(zs):.4f}, skeleton scale {tuple(skel.scale)}")

# socket empties (cm), parented to their bones; world = bone rest (cm) @ socket (cm)
empties = []
for s in sockets:
    e = bpy.data.objects.new(s["name"], None)
    scene.collection.objects.link(e)
    e.empty_display_size = 2
    e.parent = skel
    e.parent_type = "BONE"
    e.parent_bone = s["bone"]
    bpy.context.view_layer.update()
    sm = Matrix(s["matrix_bone_space_cm"])
    e.matrix_world = skel.matrix_world @ skel.data.bones[s["bone"]].matrix_local @ sm
    empties.append(e)
bpy.context.view_layer.update()
for e, s in zip(empties, sockets):
    got = skel.data.bones[s["bone"]].matrix_local.inverted() @ e.matrix_world
    d = (got.to_translation() - Matrix(s["matrix_bone_space_cm"]).to_translation()).length
    assert d < 1e-4 and max(abs(v - 1) for v in e.matrix_world.to_scale()) < 1e-5, (s["name"], d)

FBX = dict(apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", global_scale=1.0, axis_forward="-Z",
           axis_up="Y", use_mesh_modifiers=False, mesh_smooth_type="FACE", add_leaf_bones=False,
           primary_bone_axis="Y", secondary_bone_axis="X", use_armature_deform_only=True, armature_nodetype="NULL",
           bake_anim_force_startend_keying=True, bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
           use_custom_props=False)


def select(objs, active):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = active


select([skel, body, head] + empties, skel)
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "SK_UnnamedPlayer.fbx"), use_selection=True,
                         object_types={"ARMATURE", "MESH", "EMPTY"}, bake_anim=False, path_mode="COPY",
                         embed_textures=True, **FBX)
print("[export] SK_UnnamedPlayer.fbx")
for e in empties:
    bpy.data.objects.remove(e, do_unlink=True)
select([pick], pick)
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "SM_UP_Pickaxe.fbx"), use_selection=True, object_types={"MESH"},
                         bake_anim=False, path_mode="AUTO", **FBX)
print("[export] SM_UP_Pickaxe.fbx")
skel.animation_data_create()
select([skel, body, head], skel)
for name in clip_actions:
    act = bpy.data.actions[name]
    skel.animation_data.action = act
    skel.animation_data.action_slot = act.slots[0]
    n = frames[name]
    # posefix3: a single-pose clip (source action has one key at 0) is baked over 0..2; constant extrapolation makes
    # frame 1 the exact frame-0 value. UE 5.8.3 Interchange rejects a zero-length bake range (v002b proof).
    scene.frame_start, scene.frame_end = 0, (n if n > 0 else 2)
    scene.name = name
    bpy.ops.export_scene.fbx(filepath=os.path.join(ANIMS, f"A_{name}.fbx"), use_selection=True,
                             object_types={"ARMATURE", "MESH"}, bake_anim=True, bake_anim_use_all_bones=True,
                             bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False, path_mode="AUTO",
                             embed_textures=False, **FBX)
print(f"[export] {len(clip_actions)} animation FBXs")
shutil.copy2(C.SKIN_1024, os.path.join(OUT, "T_UP_Skin_1024.png"))
shutil.copy2(C.SKIN_64, os.path.join(OUT, "T_UP_Skin_64.png"))

# ---------------------------------------------------------------- raw FBX parse
addon_utils.enable("io_scene_fbx")
from io_scene_fbx import parse_fbx  # noqa: E402


def s_(v):
    return (v.decode(errors="replace") if isinstance(v, bytes) else v)


def props70(elem):
    out = {}
    for c in elem.elems:
        if c.id == b"Properties70":
            for p in c.elems:
                if p.id == b"P":
                    out[s_(p.props[0])] = [s_(v) for v in p.props[4:]]
    return out


def raw(path):
    root, version = parse_fbx.parse(path)
    info = {"fbx_version": version}
    models, conns = {}, []
    for e in root.elems:
        if e.id == b"GlobalSettings":
            g = props70(e)
            info["global"] = {k: g.get(k) for k in ("UpAxis", "UpAxisSign", "FrontAxis", "FrontAxisSign", "CoordAxis",
                                                    "CoordAxisSign", "UnitScaleFactor", "OriginalUnitScaleFactor")}
        if e.id == b"Objects":
            for o in e.elems:
                if o.id == b"Model":
                    p = props70(o)
                    models[o.props[0]] = {"name": s_(o.props[1]).split("\x00")[0], "kind": s_(o.props[2]),
                                          "t": p.get("Lcl Translation", [0, 0, 0]), "r": p.get("Lcl Rotation", [0, 0, 0]),
                                          "s": p.get("Lcl Scaling", [1.0, 1.0, 1.0])}
            info["takes"] = [s_(o.props[1]).split("\x00")[0] for o in e.elems if o.id == b"AnimationStack"]
        if e.id == b"Connections":
            for c in e.elems:
                if c.id == b"C" and s_(c.props[0]) == "OO":
                    conns.append((c.props[1], c.props[2]))
    parent = {ch: pa for ch, pa in conns if ch in models and pa in models}
    children = {}
    for ch, pa in parent.items():
        children.setdefault(pa, []).append(ch)
    limbs = [k for k, m in models.items() if m["kind"] == "LimbNode"]
    info["limb_nodes"] = len(limbs)
    info["limb_names"] = sorted(models[k]["name"] for k in limbs)
    info["limb_scale_max_dev"] = max(max(abs(v - 1.0) for v in models[k]["s"]) for k in limbs) if limbs else None
    info["leaf_bone_like"] = [models[k]["name"] for k in limbs if models[k]["name"].endswith("_end")]
    info["null_nodes"] = [(m["name"], m["s"]) for m in models.values() if m["kind"] == "Null"]
    info["root_models"] = [models[k]["name"] for k in models if parent.get(k, 0) not in models]
    socks = []
    for k, m in models.items():
        if m["name"].startswith("SOCKET_"):
            pa = models.get(parent.get(k))
            socks.append({"name": m["name"], "kind": m["kind"], "parent": pa["name"] if pa else None,
                          "parent_kind": pa["kind"] if pa else None, "leaf": not children.get(k),
                          "lcl_t": m["t"], "lcl_r": m["r"], "lcl_s": m["s"]})
    info["sockets"] = socks
    return info


parse = {"SK_UnnamedPlayer.fbx": raw(os.path.join(OUT, "SK_UnnamedPlayer.fbx")),
         "SM_UP_Pickaxe.fbx": raw(os.path.join(OUT, "SM_UP_Pickaxe.fbx"))}
sk = parse["SK_UnnamedPlayer.fbx"]
exp_parent = {s["name"]: s["bone"] for s in sockets}
sock_ok = (len(sk["sockets"]) == 3 and all(s["parent"] == exp_parent[s["name"]] and s["parent_kind"] == "LimbNode"
                                            and s["leaf"] for s in sk["sockets"]))
anim_parse = {}
for name in clip_actions:
    r = raw(os.path.join(ANIMS, f"A_{name}.fbx"))
    anim_parse[name] = {"unit": r["global"]["UnitScaleFactor"], "limbs": r["limb_nodes"],
                        "limb_scale_max_dev": r["limb_scale_max_dev"], "takes": r["takes"],
                        "sockets": len(r["sockets"]), "leaf_bone_like": r["leaf_bone_like"], "roots": r["root_models"]}
gates = {
    "SK_unit_1": sk["global"]["UnitScaleFactor"] == [1.0],
    "SK_22_limbs": sk["limb_nodes"] == 22 and sk["limb_names"] == sorted(rest),
    "SK_limb_scale_1": sk["limb_scale_max_dev"] < 1e-4,
    "SK_no_leaf_bones": not sk["leaf_bone_like"],
    "SK_armature_null_named_Armature_at_scene_root": "Armature" in sk["root_models"] and "UP_Skeleton" not in
    [n for n, _ in sk["null_nodes"]],
    "anims_armature_at_scene_root": all("Armature" in a["roots"] for a in anim_parse.values()),
    "SK_sockets_leaf_children_of_right_bones": sock_ok,
    "anims_unit_1": all(a["unit"] == [1.0] for a in anim_parse.values()),
    "anims_22_limbs": all(a["limbs"] == 22 for a in anim_parse.values()),
    "anims_limb_scale_1": all(a["limb_scale_max_dev"] < 1e-4 for a in anim_parse.values()),
    "anims_one_take_named_clip": all(len(a["takes"]) == 1 and a["takes"][0].split("|")[-1] == n
                                     for n, a in anim_parse.items()),
    "anims_no_sockets_no_leaf": all(a["sockets"] == 0 and not a["leaf_bone_like"] for a in anim_parse.values()),
}
parse["anims"] = anim_parse
parse["gates"] = gates
json.dump(parse, open(os.path.join(OUT, "raw_fbx_parse_report.json"), "w"), indent=1, default=str)
print("[export] raw parse gates", json.dumps(gates))

# ---------------------------------------------------------------- manifest
STRIKES = {"UP_Pickaxe_StrikeA": "A", "UP_Pickaxe_StrikeB": "B", "UP_Pickaxe_StrikeC": "C"}
files = {}
for root_, _, fs in os.walk(OUT):
    for f in fs:
        p = os.path.join(root_, f)
        files[os.path.relpath(p, OUT).replace("\\", "/")] = sha(p)
clips = []
for i, name in enumerate(clip_actions):
    e = dict(m1clips[name])
    e.pop("take_in_UP_AllClips", None)
    e["index"] = i
    e["file"] = f"anims/A_{name}.fbx"
    e["sha256"] = files[e["file"]]
    e["unreal_name"] = name.replace("Sneak", "Crouch")
    if name in STRIKES:
        e["status"] = "accepted-arm"
        e["strike_validation"] = {k: v for k, v in pk_polish["results"][STRIKES[name]].items() if k != "tip_path_m"}
        e["metadata_source"] = "polish validation/s06_pickaxe.json"
    elif e.get("placeholder"):
        e["status"] = "placeholder"
        e["metadata_source"] = "v001 exports/UP_manifest_v001.json"
    else:
        e["status"] = "inherited-v001"
        e["metadata_source"] = "v001 exports/UP_manifest_v001.json"
    if frames[name] == 0:
        e.update({"frames": 2, "keys": 3, "seconds": round(2 / 60, 6), "pose_clip": True,
                  "pose_clip_note": "pose clip: runtime samples t=0", "source_frames": 0})
    clips.append(e)
src_hash_after = sha(SRC)
manifest = {k: v for k, v in man1.items() if k not in ("sockets", "clips", "skeleton", "pickaxe", "provenance")}
manifest.update({
    "asset": "Unnamed Player posefix3 (SK_UnnamedPlayer)", "version": "posefix3",
    "source": {"blend": SRC, "sha256_before": src_hash_before, "sha256_after": src_hash_after,
               "unchanged": src_hash_before == src_hash_after, "saved": False},
    "blender": {"version": bpy.app.version_string, "build_hash": s_(bpy.app.build_hash),
                "build_date": s_(bpy.app.build_date)},
    "skeleton": dict(man1["skeleton"], bones=rest, name="SK_UnnamedPlayer_Skeleton (new; 22 bones)"),
    "sockets": sockets, "sockets_dropped": ["fp_anchor (not exported)"],
    "pickaxe": pk_polish["meta"], "clips": clips, "files": files,
    "unreal_name_map": {c["name"]: c["unreal_name"] for c in clips if c["name"] != c["unreal_name"]},
    "status_counts": {s: sum(c["status"] == s for c in clips) for s in ("accepted-arm", "inherited-v001", "placeholder")},
    "checks": checks,
})
json.dump(manifest, open(os.path.join(OUT, "UP_manifest_posefix3.json"), "w"), indent=1, default=float)
json.dump({"checks": checks, "raw_gates": gates, "source_unchanged": src_hash_before == src_hash_after,
           "files": len(files)}, open(os.path.join(OUT, "export_report.json"), "w"), indent=1, default=float)
assert src_hash_before == src_hash_after
print("[export] manifest", len(clips), "clips; status", manifest["status_counts"], "; all raw gates",
      all(gates.values()), "; source unchanged", src_hash_before == src_hash_after)

