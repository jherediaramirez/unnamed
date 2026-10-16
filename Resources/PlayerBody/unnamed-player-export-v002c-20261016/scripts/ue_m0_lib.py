"""M0 Unreal import proof library (UE 5.8.3 Interchange). Runs inside UnrealEditor-Cmd -run=pythonscript.
Entry scripts: ue_proof.py (mesh + 6 clips into /Game/Characters/UnnamedPlayer_Proof, gates 1-8) and
ue_batch.py (mesh + all 66 clips into /Game/Characters/UnnamedPlayer, gates 1-8, then delete the proof folder).

Coordinates: Blender metres (Z up, faces -Y) -> UE centimetres via M(v) = (SX*x, -y, z) * 100; SX is fitted on the
22 rest joint positions (expected +1) and reported, never assumed.
"""
import json
import math
import os
import time

import unreal

E = r"C:\dev\output\unnamed-player-export-v002c-20261016"
FBX = os.path.join(E, "fbx")
MAN = json.load(open(os.path.join(FBX, "UP_manifest_v002c.json")))
EXP = json.load(open(os.path.join(E, "evidence", "ue_expect.json")))
CLIPS = {c["name"]: c for c in MAN["clips"]}
BONES = MAN["skeleton"]["bones"]
EAL = unreal.EditorAssetLibrary
AL = unreal.AnimationLibrary
APE = unreal.AnimPoseExtensions
WORLD = unreal.AnimPoseSpaces.WORLD
LOCAL = unreal.AnimPoseSpaces.LOCAL
REPORT = {"started": time.strftime("%Y-%m-%d %H:%M:%S"), "engine": unreal.SystemLibrary.get_engine_version()}
LINES = []


def log(*a):
    s = "[m0] " + " ".join(str(x) for x in a)
    LINES.append(s)
    unreal.log(s)


def v3(v):
    return [round(v.x, 5), round(v.y, 5), round(v.z, 5)]


def M(v, sx=1):
    return unreal.Vector(sx * v[0] * 100.0, -v[1] * 100.0, v[2] * 100.0)


def dist(a, b):
    return math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2)


def qangle(a, b):
    d = abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w)
    return math.degrees(2 * math.acos(min(1.0, d)))


def qangle_wxyz(q):
    return math.degrees(2 * math.acos(min(1.0, abs(q[0]))))


def vangle(a, b):
    la = math.sqrt(a.x ** 2 + a.y ** 2 + a.z ** 2)
    lb = math.sqrt(b.x ** 2 + b.y ** 2 + b.z ** 2)
    c = (a.x * b.x + a.y * b.y + a.z * b.z) / (la * lb)
    return math.degrees(math.acos(max(-1.0, min(1.0, c))))


# ---------------------------------------------------------------- pipelines
PIPE_READBACK = {
    "": ["use_source_name_for_asset", "asset_name", "import_offset_translation", "import_offset_rotation",
         "import_offset_uniform_scale", "import_only_animation_adjusted", "reimport_strategy"],
    "common_meshes_properties": ["force_all_mesh_as_type", "import_lods", "bake_meshes", "keep_sections_separate",
                                 "import_sockets", "recompute_normals", "recompute_tangents", "vertex_color_import_option"],
    "common_skeletal_meshes_and_animations_properties": ["import_only_animations", "skeleton", "use_t0_as_ref_pose",
                                                         "add_curve_metadata_to_skeleton"],
    "mesh_pipeline": ["import_static_meshes", "import_skeletal_meshes", "combine_skeletal_meshes_behavior",
                      "import_morph_targets", "create_physics_asset", "update_skeleton_reference_pose",
                      "import_vertex_attributes", "skeletal_mesh_import_content_type", "build_nanite"],
    "animation_pipeline": ["import_animations", "import_bone_tracks", "animation_range", "frame_import_range",
                           "use30_hz_to_bake_bone_animation", "custom_bone_animation_sample_rate", "frame_alignment",
                           "import_custom_attribute"],
    "material_pipeline": ["import_materials", "material_import", "reuse_existing_materials"],
}


def sub(p, name):
    return p if not name else p.get_editor_property(name)


def readback(p):
    out = {}
    for s, props in PIPE_READBACK.items():
        o = sub(p, s)
        for k in props:
            try:
                v = o.get_editor_property(k)
                out[(s + "." if s else "") + k] = v.get_path_name() if isinstance(v, unreal.Object) else str(v)
            except Exception as ex:  # noqa: BLE001
                out[(s + "." if s else "") + k] = f"<unreadable: {ex}>"
    try:
        tp = p.get_editor_property("material_pipeline").get_editor_property("texture_pipeline")
        out["material_pipeline.texture_pipeline.import_textures"] = str(tp.get_editor_property("import_textures"))
    except Exception as ex:  # noqa: BLE001
        out["material_pipeline.texture_pipeline.import_textures"] = f"<unreadable: {ex}>"
    return out


def setp(o, names, value):
    """Set the first property name variant the Python binding accepts (UE's snake_case of digits varies)."""
    for k in names:
        try:
            o.set_editor_property(k, value)
            return k
        except Exception:  # noqa: BLE001
            continue
    log("WARN no property variant accepted", names)
    return None


def make_pipeline(mesh, skeleton=None, asset_name=""):
    p = unreal.InterchangeGenericAssetsPipeline()
    p.set_editor_property("use_source_name_for_asset", True)
    p.set_editor_property("asset_name", asset_name)
    p.set_editor_property("import_offset_translation", unreal.Vector(0, 0, 0))
    p.set_editor_property("import_offset_rotation", unreal.Rotator(0, 0, 0))
    p.set_editor_property("import_offset_uniform_scale", 1.0)
    cm = p.get_editor_property("common_meshes_properties")
    cm.set_editor_property("import_sockets", True)
    cm.set_editor_property("keep_sections_separate", False)
    cs = p.get_editor_property("common_skeletal_meshes_and_animations_properties")
    cs.set_editor_property("import_only_animations", not mesh)
    cs.set_editor_property("skeleton", skeleton)
    cs.set_editor_property("use_t0_as_ref_pose", False)
    mp = p.get_editor_property("mesh_pipeline")
    mp.set_editor_property("import_static_meshes", False)
    mp.set_editor_property("import_skeletal_meshes", mesh)
    mp.set_editor_property("combine_skeletal_meshes_behavior", unreal.InterchangeCombineSkeletalMeshesBehavior.BY_SKELETON)
    mp.set_editor_property("import_morph_targets", False)
    mp.set_editor_property("create_physics_asset", False)
    mp.set_editor_property("update_skeleton_reference_pose", False)
    ap = p.get_editor_property("animation_pipeline")
    ap.set_editor_property("import_animations", not mesh)
    ap.set_editor_property("import_bone_tracks", True)
    ap.set_editor_property("animation_range", unreal.InterchangeAnimationRange.TIMELINE)
    setp(ap, ["use30hz_to_bake_bone_animation", "use30_hz_to_bake_bone_animation", "use_30hz_to_bake_bone_animation"], False)
    ap.set_editor_property("custom_bone_animation_sample_rate", 60)
    mat = p.get_editor_property("material_pipeline")
    mat.set_editor_property("import_materials", mesh)
    mat.get_editor_property("texture_pipeline").set_editor_property("import_textures", mesh)
    return p


def fbx_translator_defaults():
    out = {}
    try:
        d = unreal.get_default_object(unreal.InterchangeFbxTranslatorSettings)
        for k in ("convert_scene", "force_front_x_axis", "convert_scene_unit", "keep_fbx_namespace"):
            try:
                out[k] = str(d.get_editor_property(k))
            except Exception as ex:  # noqa: BLE001
                out[k] = f"<unreadable: {ex}>"
    except Exception as ex:  # noqa: BLE001
        out["error"] = str(ex)
    out["Interchange.FeatureFlags.Import.FBX"] = unreal.SystemLibrary.get_console_variable_bool_value(
        "Interchange.FeatureFlags.Import.FBX")
    return out


def do_import(path, folder, pipeline):
    mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
    src = unreal.InterchangeManager.create_source_data(path)
    prm = unreal.ImportAssetParameters()
    prm.set_editor_property("is_automated", True)
    prm.set_editor_property("replace_existing", False)
    prm.set_editor_property("override_pipelines", [unreal.SoftObjectPath(pipeline.get_path_name())])
    before = set(EAL.list_assets(folder, recursive=True, include_folder=False)) if EAL.does_directory_exist(folder) else set()
    res = mgr.import_asset(folder, src, prm)
    after = set(EAL.list_assets(folder, recursive=True, include_folder=False))
    new = sorted(after - before)
    log("import", os.path.basename(path), "->", res if not isinstance(res, (list, tuple)) else res[0], "new", new)
    return new


def assets_of(folder, cls):
    out = []
    for a in EAL.list_assets(folder, recursive=True, include_folder=False):
        d = EAL.find_asset_data(a)
        if str(d.asset_class_path.asset_name) == cls:
            out.append(a)
    return out


# ---------------------------------------------------------------- pose helpers
def opts(kind, mesh):
    o = unreal.AnimPoseEvaluationOptions()
    o.set_editor_property("evaluation_type", kind)
    o.set_editor_property("should_retarget", False)
    o.set_editor_property("extract_root_motion", False)
    o.set_editor_property("optional_skeletal_mesh", mesh)
    try:
        o.set_editor_property("retrieve_additive_as_full_pose", True)
    except Exception:  # noqa: BLE001
        pass
    return o


def frame_poses(seq, n, kind, mesh):
    o = opts(kind, mesh)
    return [APE.get_anim_pose_at_frame(seq, f, o) for f in range(n + 1)]


# ---------------------------------------------------------------- mesh gates 1-5
def mesh_gates(folder, mesh, skel, any_seq):
    g = {}
    ref = APE.get_reference_pose(skel)
    names = [str(n) for n in APE.get_bone_names(ref)]
    parents = {}
    for n in names:
        path = [str(x) for x in AL.find_bone_path_to_root(any_seq, n)]
        parents[n] = path[1] if len(path) > 1 else None
    want_order = MAN["skeleton"]["order"]
    scale_dev = max(max(abs(c - 1.0) for c in v3(APE.get_ref_bone_pose(ref, n, LOCAL).scale3d)) for n in names)
    g1 = {"bones": len(names), "root": names[0], "names_match": sorted(names) == sorted(want_order),
          "hierarchy_match": all(parents.get(n) == BONES[n]["parent"] for n in want_order if n in parents),
          "extra_root": [n for n in names if n not in BONES], "ref_scale_max_dev": scale_dev}
    g1["pass"] = (g1["bones"] == 22 and g1["root"] == "root" and g1["names_match"] and g1["hierarchy_match"]
                  and not g1["extra_root"] and scale_dev <= 1e-4)
    g["1_skeleton"] = g1
    comp = {n: APE.get_ref_bone_pose(ref, n, WORLD) for n in names}
    fits = {}
    for sx in (1, -1):
        fits[sx] = max(dist(comp[n].translation, M(BONES[n]["head_m"], sx)) for n in want_order)
    sx = min(fits, key=fits.get)
    REPORT["SX"] = sx
    rot_err = 0.0
    for n in want_order:
        if BONES[n]["parent"] is None:
            continue
        a_ue = qangle(APE.get_ref_bone_pose(ref, n, LOCAL).rotation, unreal.Quat(0, 0, 0, 1))
        rot_err = max(rot_err, abs(a_ue - qangle_wxyz(BONES[n]["parent_relative_rotation_wxyz"])))
    g3 = {"sx_fit": sx, "joint_pos_max_err_cm": fits[sx], "other_sx_err_cm": fits[-sx],
          "parent_relative_rot_angle_max_err_deg": rot_err,
          "root_local": {"t": v3(comp["root"].translation), "r": str(comp["root"].rotation.rotator())}}
    g3["pass"] = sx == 1 and fits[sx] <= 0.01 and rot_err <= 0.01
    g["3_ref_pose"] = g3
    b = mesh.get_bounds()
    bi = None
    try:
        bi = mesh.get_imported_bounds()
    except Exception:  # noqa: BLE001
        pass
    bb = bi or b
    height, soles = 2 * bb.box_extent.z, bb.origin.z - bb.box_extent.z
    g2 = {"height_cm": height, "soles_cm": soles, "min": v3(bb.origin - bb.box_extent), "max": v3(bb.origin + bb.box_extent),
          "source": "imported_bounds" if bi else "bounds", "picture": "skipped (optional)"}
    g2["pass"] = abs(height - 180) <= 0.5 and abs(soles) <= 0.1
    g["2_bounds"] = g2
    toe = {s: comp["ball_" + s].translation.y - comp["foot_" + s].translation.y for s in "lr"}
    g4 = {"ball_minus_foot_y_cm": toe, "head_y": comp["head"].translation.y, "pelvis_y": comp["pelvis"].translation.y}
    g4["pass"] = all(v > 1.0 for v in toe.values()) and sx == 1
    g["4_facing_plusY"] = g4
    # gate 5: sections, materials, sockets
    mats = [str(m.get_editor_property("material_slot_name")) for m in mesh.get_editor_property("materials")]
    nsec = None
    for fn in (lambda: unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).get_num_sections(mesh, 0),
               lambda: mesh.get_num_sections(0)):
        try:
            nsec = fn()
            break
        except Exception:  # noqa: BLE001
            continue
    socks = {}
    sock_ok = True
    for s in MAN["sockets"]:
        name = s["unreal_socket"]
        so = mesh.find_socket(name)
        if so is None:
            socks[name] = "missing"
            sock_ok = False
            continue
        rel = unreal.Transform(so.get_editor_property("relative_location"), so.get_editor_property("relative_rotation"),
                               so.get_editor_property("relative_scale"))
        bone = str(so.get_editor_property("bone_name"))
        sc = rel.multiply(comp[bone])
        # expected component-space position and axes from Blender: bone rest (manifest) @ socket matrix
        bm = s["matrix_bone_space_cm"]
        ax = BONES[bone]["local_axes_world"]
        head = BONES[bone]["head_m"]

        def bl_world_vec(v):  # bone-local vector (Blender) -> Blender world direction
            return [ax["x"][i] * v[0] + ax["y"][i] * v[1] + ax["z"][i] * v[2] for i in range(3)]
        loc_bl = bl_world_vec([bm[0][3] / 100, bm[1][3] / 100, bm[2][3] / 100])
        want = M([head[i] + loc_bl[i] for i in range(3)], sx)
        want_y = M(bl_world_vec([bm[0][1], bm[1][1], bm[2][1]]), sx)
        want_z = M(bl_world_vec([bm[0][2], bm[1][2], bm[2][2]]), sx)
        got_y = sc.rotation.rotate_vector(unreal.Vector(0, 1, 0))
        got_z = sc.rotation.rotate_vector(unreal.Vector(0, 0, 1))
        got_x = sc.rotation.rotate_vector(unreal.Vector(1, 0, 0))
        # axis mapping is not assumed: report the best matching UE socket axis for Blender +Y / +Z (tool frame)
        cand = {"+X": got_x, "-X": got_x * -1, "+Y": got_y, "-Y": got_y * -1, "+Z": got_z, "-Z": got_z * -1}
        by = min(cand, key=lambda k: vangle(cand[k], want_y))
        bz = min(cand, key=lambda k: vangle(cand[k], want_z))
        e = {"bone": bone, "rel_loc": v3(rel.translation), "rel_rot": str(so.get_editor_property("relative_rotation")),
             "rel_scale": v3(rel.scale3d), "pos_err_cm": dist(sc.translation, want),
             "blender_Y_matches_ue": by, "blender_Y_err_deg": vangle(cand[by], want_y),
             "blender_Z_matches_ue": bz, "blender_Z_err_deg": vangle(cand[bz], want_z)}
        e["pass"] = (bone == s["bone"] and e["pos_err_cm"] <= 0.01 and e["blender_Y_err_deg"] <= 0.05
                     and e["blender_Z_err_deg"] <= 0.05 and max(abs(c - 1) for c in e["rel_scale"]) < 1e-4)
        sock_ok = sock_ok and e["pass"]
        socks[name] = e
    nsock = mesh.num_sockets() if hasattr(mesh, "num_sockets") else None
    g5 = {"sections_lod0": nsec, "material_slots": mats, "sockets": socks, "num_sockets": nsock}
    g5["pass"] = (nsec in (2, None) and sorted(mats) == ["M_UP_Body", "M_UP_Head"] and sock_ok)
    g["5_sections_sockets"] = g5
    return g, comp, sx


# ---------------------------------------------------------------- clip gates 6-7
def clip_gates(seq, name, mesh, comp_ref, sx):
    c = CLIPS[name]
    n = c["frames"]
    g = {}
    nkeys = AL.get_num_keys(seq)
    nfr = AL.get_num_frames(seq)
    length = seq.get_play_length()
    try:   # AnimationLibrary has no GetFrameRate in 5.8.3; derive the sampling rate from GetTimeAtFrame(1)
        t1 = AL.get_time_at_frame(seq, 1)
        rate = round(1.0 / t1, 6) if t1 else None
    except Exception as ex:  # noqa: BLE001
        rate = f"<unavailable: {ex}>"
    raw = frame_poses(seq, n, unreal.AnimDataEvalType.RAW, mesh)
    names = [str(b) for b in APE.get_bone_names(raw[0])]
    seam = max(dist(APE.get_bone_pose(raw[n], b, WORLD).translation, APE.get_bone_pose(raw[0], b, WORLD).translation)
               for b in names)
    r0 = APE.get_bone_pose(raw[0], "root", LOCAL)
    root_dev = max(max(dist(APE.get_bone_pose(p, "root", LOCAL).translation, r0.translation) for p in raw),
                   max(qangle(APE.get_bone_pose(p, "root", LOCAL).rotation, r0.rotation) for p in raw))
    scale_dev = max(max(abs(x - 1) for x in v3(APE.get_bone_pose(p, b, LOCAL).scale3d)) for p in raw for b in names)
    g6 = {"frames": n, "num_keys": nkeys, "num_frames": nfr, "play_length_s": length, "expected_s": n / 60.0,
          "frame_rate": rate, "seam_cm": seam, "root_static_dev": root_dev, "bone_scale_max_dev": scale_dev,
          "root_motion_enabled": seq.get_editor_property("enable_root_motion")}
    if n > 0:
        g6["pass"] = (abs(length - n / 60.0) <= 1e-4 and nkeys == n + 1 and seam <= 0.01 and root_dev <= 1e-4
                      and scale_dev <= 1e-4 and not g6["root_motion_enabled"])
        if c.get("pose_clip"):
            sp = single_pose_check(raw[0], comp_ref, name, sx)
            g6["pose_vs_roundtrip_t0"] = sp
            g6["pass"] = g6["pass"] and sp["max_err_cm"] <= 0.001 and sp["delta_angle_max_err_deg"] <= 0.01
    else:  # single-pose clip (RangeStop ensure on import): the imported pose must equal the Blender round trip
        sp = single_pose_check(raw[0], comp_ref, name, sx)
        g6["single_pose_vs_roundtrip"] = sp
        g6["pass"] = (nkeys >= 1 and sp["max_err_cm"] <= 0.01 and root_dev <= 1e-4 and scale_dev <= 1e-4
                      and not g6["root_motion_enabled"])
    g["6_timing"] = g6
    cmp_ = frame_poses(seq, n, unreal.AnimDataEvalType.COMPRESSED, mesh)
    perr, rerr, worst = 0.0, 0.0, None
    for f in range(n + 1):
        for b in names:
            a = APE.get_bone_pose(raw[f], b, WORLD)
            k = APE.get_bone_pose(cmp_[f], b, WORLD)
            d = dist(a.translation, k.translation)
            r = qangle(a.rotation, k.rotation)
            if d > perr:
                perr, worst = d, (f, b)
            rerr = max(rerr, r)
    g7 = {"compressed_pos_max_err_cm": perr, "compressed_rot_max_err_deg": rerr, "worst": worst,
          "compressed_identical_to_raw": perr == 0.0 and rerr == 0.0}
    try:
        g7["bone_compression_settings"] = seq.get_editor_property("bone_compression_settings").get_path_name()
    except Exception:  # noqa: BLE001
        pass
    ok7 = perr <= 0.05 and rerr <= 0.1
    if c.get("family") in ("Walk", "Sneak") and c.get("duty_factor") is not None:
        for label, poses in (("raw", raw), ("compressed", cmp_)):
            g7[f"planted_drift_{label}_cm"] = drift(poses, c, n, comp_ref, sx)
        ok7 = ok7 and g7["planted_drift_compressed_cm"] <= 0.1
    if name.startswith("UP_Pickaxe_Strike"):
        for label, poses, lim in (("raw", raw, 0.1), ("compressed", cmp_, 0.5)):
            e = tip_error(poses[9], mesh, comp_ref, sx)
            g7[f"tip_f9_{label}"] = e
            if name == "UP_Pickaxe_StrikeA":
                ok7 = ok7 and e["err_cm"] <= lim
    g7["pass"] = ok7
    g["7_compression"] = g7
    return g


RT = json.load(open(os.path.join(E, "evidence", "roundtrip_v002c.json")))


def single_pose_check(pose, comp_ref, name, sx):
    ref = RT["anims"][name]["pose0_world_m"]
    rest = RT["SK_rest_head_tail_m"]
    he, te, ae = 0.0, 0.0, 0.0
    for b, ht in ref.items():
        pc = APE.get_bone_pose(pose, b, WORLD)
        he = max(he, dist(pc.translation, M(ht["head"], sx)))
        if "delta_angle_deg" in ht:   # rotation vs rest; the angle is invariant under the axis conversion
            d = pc.multiply(comp_ref[b].inverse())
            ae = max(ae, abs(qangle(d.rotation, unreal.Quat(0, 0, 0, 1)) - ht["delta_angle_deg"]))
        tail_local = comp_ref[b].inverse().transform_location(M(rest[b]["tail"], sx))
        te = max(te, dist(pc.transform_location(tail_local), M(ht["tail"], sx)))
    return {"head_max_err_cm": he, "tail_max_err_cm": te, "max_err_cm": max(he, te), "delta_angle_max_err_deg": ae,
            "bones": len(ref)}


def drift(poses, c, n, comp_ref, sx):
    tv = c["travel_vector_char_space"]
    vel = M([tv[0], tv[1], 0.0], sx)          # cm/s
    D = c["duty_factor"]
    dmax = 0.0
    for side in "lr":
        for key, (bone, p) in EXP["soles_blender_m"][side].items():
            loc = comp_ref[bone].inverse().transform_location(M(p, sx))
            start = None
            for f in range(n + 1):
                w = APE.get_bone_pose(poses[f], bone, WORLD).transform_location(loc) + vel * (f / 60.0)
                ps = (f / n - (0.0 if side == "l" else .5)) % 1.0
                planted = w.z <= 0.3 and ps <= D + 1e-6
                if planted:
                    if start is None:
                        start = w
                    dmax = max(dmax, math.hypot(w.x - start.x, w.y - start.y))
                else:
                    start = None
    return dmax


def tip_error(pose9, mesh, comp_ref, sx):
    so = mesh.find_socket("hand_r_grip")
    rel = unreal.Transform(so.get_editor_property("relative_location"), so.get_editor_property("relative_rotation"),
                           so.get_editor_property("relative_scale"))
    rest_sock = rel.multiply(comp_ref["hand_r"])
    tip_local = rest_sock.inverse().transform_location(M(EXP["rest_tip_blender_m"], sx))
    s9 = rel.multiply(APE.get_bone_pose(pose9, "hand_r", WORLD))
    tip = s9.transform_location(tip_local)
    want = M(EXP["target_blender_m"], sx)
    return {"tip_cm": v3(tip), "target_cm": v3(want), "err_cm": dist(tip, want), "tip_local_in_socket_cm": v3(tip_local),
            "tip_local_len_cm": math.sqrt(tip_local.x ** 2 + tip_local.y ** 2 + tip_local.z ** 2)}


# ---------------------------------------------------------------- run
def tag(asset, sha):
    EAL.set_metadata_tag(asset, "UP.SourceSha256", sha)
    EAL.set_metadata_tag(asset, "UP.Manifest", "UP_manifest_v002c")


def legacy_import(path, folder, name):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", path)
    t.set_editor_property("destination_path", folder)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", False)
    t.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    return [str(x) for x in t.get_editor_property("imported_object_paths")]


def skin_texture(folder):
    """The FBX-embedded skin doesn't import through Interchange; import the 64 px skin itself and wire it into both
    material instances' base-colour texture parameter."""
    MEL = unreal.MaterialEditingLibrary
    out = {"file": os.path.join(FBX, "T_UP_Skin_64.png"), "sha256": MAN["files"].get("T_UP_Skin_64.png")}
    out["imported"] = legacy_import(out["file"], folder, "T_UP_Skin")
    tex = EAL.load_asset(folder + "/T_UP_Skin")
    tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("srgb", True)
    tag(tex, out["sha256"] or "")
    out["texture"] = {"path": tex.get_path_name(), "filter": str(tex.get_editor_property("filter")),
                      "mips": str(tex.get_editor_property("mip_gen_settings")), "srgb": tex.get_editor_property("srgb"),
                      "size": [tex.blueprint_get_size_x(), tex.blueprint_get_size_y()]}
    out["materials"] = {}
    for mname in ("M_UP_Body", "M_UP_Head"):
        mi = EAL.load_asset(f"{folder}/{mname}")
        e = {"class": mi.get_class().get_name()}
        try:
            e["parent"] = mi.get_editor_property("parent").get_path_name()
        except Exception:  # noqa: BLE001
            pass
        tparams = [str(x) for x in MEL.get_texture_parameter_names(mi)]
        sparams = [str(x) for x in MEL.get_static_switch_parameter_names(mi)]
        e["texture_params"], e["switch_params"] = tparams, sparams
        pick = None
        for key in ("basecolor", "diffuse", "albedo", "color"):
            pick = next((t for t in tparams if key in t.lower()), None)
            if pick:
                break
        if pick:
            for sw in sparams:
                if "basecolor" in sw.lower().replace(" ", "") and ("use" in sw.lower() or "map" in sw.lower()):
                    try:
                        MEL.set_material_instance_static_switch_parameter_value(mi, sw, True)
                        e.setdefault("switches_set", []).append(sw)
                    except Exception as ex:  # noqa: BLE001
                        e.setdefault("switch_errors", []).append(f"{sw}: {ex}")
            e["set_ok"] = MEL.set_material_instance_texture_parameter_value(mi, pick, tex)
            MEL.update_material_instance(mi)
            got = MEL.get_material_instance_texture_parameter_value(mi, pick)
            e["param"] = pick
            e["readback"] = got.get_path_name() if got else None
            e["assigned"] = bool(got) and got.get_path_name() == tex.get_path_name()
        else:
            e["assigned"] = False
            e["note"] = "no base-colour texture parameter on this material instance"
        out["materials"][mname] = e
    EAL.save_directory(folder, only_if_is_dirty=False, recursive=True)
    return out


def pickaxe(folder):
    p = make_pipeline(True, None, "SM_UP_Pickaxe")
    p.get_editor_property("mesh_pipeline").set_editor_property("import_static_meshes", True)
    p.get_editor_property("mesh_pipeline").set_editor_property("import_skeletal_meshes", False)
    p.get_editor_property("material_pipeline").set_editor_property("import_materials", False)
    p.get_editor_property("material_pipeline").get_editor_property("texture_pipeline").set_editor_property(
        "import_textures", False)
    new = do_import(os.path.join(FBX, "SM_UP_Pickaxe.fbx"), folder, p)
    sms = [a for a in new if str(EAL.find_asset_data(a).asset_class_path.asset_name) == "StaticMesh"]
    out = {"new_assets": new, "pipeline_effective": readback(p)}
    if len(sms) != 1:
        out["pass"] = False
        out["error"] = f"expected one StaticMesh, got {sms}"
        return out
    sm = EAL.load_asset(sms[0])
    tag(sm, MAN["files"]["SM_UP_Pickaxe.fbx"])
    verts = []
    for sec in range(sm.get_num_sections(0)):
        r = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(sm, 0, sec)
        verts += list(r[0])
    b = sm.get_bounding_box()
    tip = MAN["pickaxe"]["tip_local_m"]
    cands = {"(0,+42.75,+26.5)": unreal.Vector(0, tip[1] * 100, tip[2] * 100),
             "(0,-42.75,+26.5)": unreal.Vector(0, -tip[1] * 100, tip[2] * 100)}
    near = {k: min(dist(v, c) for v in verts) for k, c in cands.items()}
    best = min(near, key=near.get)
    out.update({"asset": sms[0], "verts": len(verts), "bounds_min": v3(b.min), "bounds_max": v3(b.max),
                "origin_inside_bounds": b.min.x <= 0 <= b.max.x and b.min.y <= 0 <= b.max.y and b.min.z <= 0 <= b.max.z,
                "tip_nearest_vertex_cm": near, "tip_axis_match": best,
                "socket_mapping_note": "socket readback maps Blender tool +Y to UE -Y, so (0,-42.75,26.5) is the "
                                       "consistent tip in UE axes; the Blender-frame tip is (0,42.75,26.5)"})
    out["pass"] = near[best] <= 0.01 and out["origin_inside_bounds"]
    return out


def run(folder, clip_names, out_json, delete_folders_after=(), with_pickaxe=False):
    REPORT["folder"] = folder
    REPORT["fbx_translator"] = fbx_translator_defaults()
    if EAL.does_directory_exist(folder) and EAL.list_assets(folder, recursive=True):
        raise RuntimeError(f"{folder} already has assets; refusing to import over them")
    # mesh
    mp = make_pipeline(True)
    REPORT["pipeline_mesh_effective"] = readback(mp)
    new_mesh = do_import(os.path.join(FBX, "SK_UnnamedPlayer.fbx"), folder, mp)
    REPORT["mesh_import_new_assets"] = new_mesh
    meshes, skels = assets_of(folder, "SkeletalMesh"), assets_of(folder, "Skeleton")
    REPORT["after_mesh"] = {"skeletal_meshes": meshes, "skeletons": skels,
                            "physics": assets_of(folder, "PhysicsAsset"), "materials": assets_of(folder, "Material"),
                            "textures": assets_of(folder, "Texture2D")}
    log("after mesh", json.dumps(REPORT["after_mesh"]))
    if len(meshes) != 1 or len(skels) != 1:
        raise RuntimeError(f"expected 1 mesh + 1 skeleton, got {meshes} {skels}")
    mesh = EAL.load_asset(meshes[0])
    skel = EAL.load_asset(skels[0])
    tag(mesh, MAN["files"]["SK_UnnamedPlayer.fbx"])
    REPORT["skin"] = skin_texture(folder)
    log("skin", json.dumps(REPORT["skin"], default=str))
    # clips
    REPORT["clips"] = {}
    seqs = {}
    first_pipe = None
    for name in clip_names:
        un = CLIPS[name]["unreal_name"]
        p = make_pipeline(False, skel, un)
        if first_pipe is None:
            first_pipe = readback(p)
            REPORT["pipeline_clip_effective"] = first_pipe
        new = do_import(os.path.join(FBX, CLIPS[name]["file"]), folder, p)
        anims = [a for a in new if str(EAL.find_asset_data(a).asset_class_path.asset_name) == "AnimSequence"]
        others = [a for a in new if a not in anims]
        e = {"new_assets": new, "unexpected_non_anim": others}
        if len(anims) != 1 or anims[0].split(".")[-1] != un:
            e["error"] = f"expected one AnimSequence named {un}, got {anims}"
            REPORT["clips"][name] = e
            log("CLIP NAME/COUNT FAIL", name, anims, others)
            continue
        seq = EAL.load_asset(anims[0])
        seqs[name] = seq
        tag(seq, CLIPS[name]["sha256"])
        REPORT["clips"][name] = e
    EAL.save_directory(folder, only_if_is_dirty=False, recursive=True)
    any_seq = seqs.get("UP_Idle") or next(iter(seqs.values()))
    g, comp, sx = mesh_gates(folder, mesh, skel, any_seq)
    REPORT["mesh_gates"] = g
    log("mesh gates", json.dumps({k: v["pass"] for k, v in g.items()}))
    for name, seq in seqs.items():
        try:
            REPORT["clips"][name]["gates"] = clip_gates(seq, name, mesh, comp, sx)
        except Exception as ex:  # noqa: BLE001
            REPORT["clips"][name]["error"] = f"gate exception: {ex}"
        gg = REPORT["clips"][name].get("gates", {})
        log("clip", name, {k: v.get("pass") for k, v in gg.items()}, REPORT["clips"][name].get("error", ""))
    # additive look clips (after their full-pose gates)
    ref_c = {"Look_": seqs.get("UP_Look_C"), "SneakLook_": seqs.get("UP_SneakLook_C")}
    REPORT["additive"] = {}
    order = sorted(seqs, key=lambda n: n.endswith("_C"))   # the two base clips last
    for name in order:
        seq = seqs[name]
        key = name[3:]
        for pre, ref in ref_c.items():
            if key.startswith(pre) and ref is not None:
                seq.set_editor_property("additive_anim_type", unreal.AdditiveAnimationType.AAT_ROTATION_OFFSET_MESH_SPACE)
                seq.set_editor_property("ref_pose_type", unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME)
                seq.set_editor_property("ref_pose_seq", ref)
                seq.set_editor_property("ref_frame_index", 0)
                REPORT["additive"][name] = {"type": str(seq.get_editor_property("additive_anim_type")),
                                            "ref_pose_type": str(seq.get_editor_property("ref_pose_type")),
                                            "ref": ref.get_name(), "frame": seq.get_editor_property("ref_frame_index")}
    EAL.save_directory(folder, only_if_is_dirty=False, recursive=True)
    for name, a in REPORT["additive"].items():   # informational: max additive rotation delta (base clips ~0)
        try:
            o = opts(unreal.AnimDataEvalType.COMPRESSED, mesh)
            o.set_editor_property("retrieve_additive_as_full_pose", False)
            pz = APE.get_anim_pose_at_frame(seqs[name], 0, o)
            a["delta_max_deg"] = max(qangle(APE.get_bone_pose(pz, b, LOCAL).rotation, unreal.Quat(0, 0, 0, 1))
                                     for b in [str(x) for x in APE.get_bone_names(pz)])
        except Exception as ex:  # noqa: BLE001
            a["delta_max_deg"] = f"<unavailable: {ex}>"
    looks = [n for n in seqs if n[3:].startswith(("Look_", "SneakLook_"))]
    REPORT["additive_ok"] = all(n in REPORT["additive"] and "ROTATION_OFFSET_MESH_SPACE" in REPORT["additive"][n]["type"]
                                and "ANIM_FRAME" in REPORT["additive"][n]["ref_pose_type"]
                                and REPORT["additive"][n]["frame"] == 0 for n in looks)
    if with_pickaxe:
        REPORT["pickaxe"] = pickaxe(folder)
        log("pickaxe", json.dumps({k: v for k, v in REPORT["pickaxe"].items() if k != "pipeline_effective"}, default=str))
        EAL.save_directory(folder, only_if_is_dirty=False, recursive=True)
    all_pass_before_delete = (all(v["pass"] for v in g.values()) and REPORT.get("pickaxe", {"pass": True})["pass"] and
                              all(all(v.get("pass") for v in e.get("gates", {}).values()) and "error" not in e
                                  and bool(e.get("gates")) for e in REPORT["clips"].values()))
    REPORT["deleted_folders"] = {}
    for d in delete_folders_after:
        if not all_pass_before_delete:
            REPORT["deleted_folders"][d] = "kept: a gate failed"
        elif EAL.does_directory_exist(d):
            REPORT["deleted_folders"][d] = EAL.delete_directory(d)
        else:
            REPORT["deleted_folders"][d] = "absent"
    scope = "/Game" if delete_folders_after else folder

    def of(cls):
        return [a for a in EAL.list_assets(scope, recursive=True, include_folder=False)
                if str(EAL.find_asset_data(a).asset_class_path.asset_name) in cls]
    all_skel, redirs = of(("Skeleton",)), of(("ObjectRedirector",))
    skm, stm = of(("SkeletalMesh",)), of(("StaticMesh",))
    g8 = {"scope": scope, "skeletons": all_skel, "redirectors": redirs, "skeletal_meshes": skm, "static_meshes": stm}
    g8["pass"] = len(all_skel) == 1 and not redirs and len(skm) == 1 and len(stm) == (1 if with_pickaxe else 0)
    REPORT["8_assets"] = g8
    REPORT["asset_list"] = sorted(EAL.list_assets("/Game", recursive=True, include_folder=False))
    clip_pass = {n: (all(v.get("pass") for v in e.get("gates", {}).values()) and "error" not in e and bool(e.get("gates")))
                 for n, e in REPORT["clips"].items()}
    REPORT["summary"] = {"mesh_gates": {k: v["pass"] for k, v in g.items()}, "clips_pass": clip_pass,
                         "gate8": g8["pass"],
                         "skin_assigned": all(m.get("assigned") for m in REPORT["skin"]["materials"].values()),
                         "pickaxe": REPORT.get("pickaxe", {}).get("pass"),
                         "additive_ok": REPORT.get("additive_ok"), "additive_count": len(REPORT.get("additive", {})),
                         "all_pass": all(v["pass"] for v in g.values()) and all(clip_pass.values()) and g8["pass"]
                         and REPORT.get("pickaxe", {"pass": True})["pass"] and REPORT.get("additive_ok", True)
                         and len(clip_pass) == len(clip_names)}
    REPORT["finished"] = time.strftime("%Y-%m-%d %H:%M:%S")
    json.dump(REPORT, open(out_json, "w"), indent=1, default=str)
    log("SUMMARY", json.dumps(REPORT["summary"]))
    log("report", out_json)
