"""The Unnamed Player's import gates (PLAN.md section 4.4), measured on the assets ImportUnnamedPlayer.py made.

Ported from M0's proven checks (Resources/PlayerBody/unnamed-player-export-v002c-20261016/scripts/final/ue_m0_lib.py,
ue_cleanup.py, regate_offline.py) with the paths taken from Art/Characters/UnnamedPlayer and M0's two check corrections
built in: the seam test applies to loops only, and the pickaxe tip is measured to the nearest triangle (it is the
centre of the pick's end face, not a vertex).

Coordinates: Blender metres (Z up, faces -Y) map to UE centimetres by M(v) = (x, -y, z) * 100; the X sign is fitted on
the 22 rest joints (expected +1) and reported, never assumed. Rotations compare as normalized shortest-arc quaternions.
"""
import json
import math
from pathlib import Path

import unreal

EAL = unreal.EditorAssetLibrary
AL = unreal.AnimationLibrary
APE = unreal.AnimPoseExtensions
WORLD = unreal.AnimPoseSpaces.WORLD
LOCAL = unreal.AnimPoseSpaces.LOCAL
# The pose clips import over frames 0..2 at 60 Hz: 3 keys, 2/60 s (the source has 2 keys, 1/60 s; the runtime samples t=0).
POSE_IMPORTED_FRAMES = 2


class Oracle:
    """The export manifest and M0's oracle files under Art/Characters/UnnamedPlayer."""

    def __init__(self, art: Path, export_manifest: str):
        self.man = json.loads((art / export_manifest).read_text(encoding="utf-8"))
        self.exp = json.loads((art / "gates/ue_expect.json").read_text(encoding="utf-8"))
        self.src = json.loads((art / "gates/source_poses.json").read_text(encoding="utf-8"))
        self.clips = {c["name"]: c for c in self.man["clips"]}
        self.bones = self.man["skeleton"]["bones"]


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


def class_of(path):
    return str(EAL.find_asset_data(path).asset_class_path.asset_name)


def assets_of(folder, cls):
    return [a for a in EAL.list_assets(folder, recursive=True, include_folder=False) if class_of(a) == cls]


def frame_poses(seq, n, kind, mesh):
    o = unreal.AnimPoseEvaluationOptions()
    o.set_editor_property("evaluation_type", kind)
    o.set_editor_property("should_retarget", False)
    o.set_editor_property("extract_root_motion", False)
    o.set_editor_property("optional_skeletal_mesh", mesh)
    try:
        o.set_editor_property("retrieve_additive_as_full_pose", True)
    except Exception:  # noqa: BLE001 (absent in some engine builds)
        pass
    return [APE.get_anim_pose_at_frame(seq, f, o) for f in range(n + 1)]


# ---------------------------------------------------------------- gates 1-5: skeleton, bounds, ref pose, facing, sockets
def mesh_gates(o: Oracle, mesh, skel, any_seq):
    g = {}
    ref = APE.get_reference_pose(skel)
    names = [str(n) for n in APE.get_bone_names(ref)]
    parents = {}
    for n in names:
        path = [str(x) for x in AL.find_bone_path_to_root(any_seq, n)]
        parents[n] = path[1] if len(path) > 1 else None
    want_order = o.man["skeleton"]["order"]
    scale_dev = max(max(abs(c - 1.0) for c in v3(APE.get_ref_bone_pose(ref, n, LOCAL).scale3d)) for n in names)
    g1 = {"bones": len(names), "root": names[0], "names_match": sorted(names) == sorted(want_order),
          "hierarchy_match": all(parents.get(n) == o.bones[n]["parent"] for n in want_order if n in parents),
          "extra_bones": [n for n in names if n not in o.bones], "ref_scale_max_dev": scale_dev}
    g1["pass"] = (g1["bones"] == 22 and g1["root"] == "root" and g1["names_match"] and g1["hierarchy_match"]
                  and not g1["extra_bones"] and scale_dev <= 1e-4)
    g["1_skeleton"] = g1
    comp = {n: APE.get_ref_bone_pose(ref, n, WORLD) for n in names}
    fits = {sx: max(dist(comp[n].translation, M(o.bones[n]["head_m"], sx)) for n in want_order) for sx in (1, -1)}
    sx = min(fits, key=fits.get)
    rot_err = 0.0
    for n in want_order:
        if o.bones[n]["parent"] is None:
            continue
        a_ue = qangle(APE.get_ref_bone_pose(ref, n, LOCAL).rotation, unreal.Quat(0, 0, 0, 1))
        rot_err = max(rot_err, abs(a_ue - qangle_wxyz(o.bones[n]["parent_relative_rotation_wxyz"])))
    g3 = {"sx_fit": sx, "joint_pos_max_err_cm": fits[sx], "other_sx_err_cm": fits[-sx],
          "parent_relative_rot_angle_max_err_deg": rot_err}
    g3["pass"] = sx == 1 and fits[sx] <= 0.01 and rot_err <= 0.01
    g["3_ref_pose"] = g3
    try:
        bb = mesh.get_imported_bounds()
    except Exception:  # noqa: BLE001
        bb = mesh.get_bounds()
    height, soles = 2 * bb.box_extent.z, bb.origin.z - bb.box_extent.z
    g2 = {"height_cm": height, "soles_cm": soles, "min": v3(bb.origin - bb.box_extent),
          "max": v3(bb.origin + bb.box_extent)}
    g2["pass"] = abs(height - 180) <= 0.5 and abs(soles) <= 0.1
    g["2_bounds"] = g2
    toe = {s: comp["ball_" + s].translation.y - comp["foot_" + s].translation.y for s in "lr"}
    g4 = {"ball_minus_foot_y_cm": toe}
    g4["pass"] = all(v > 1.0 for v in toe.values()) and sx == 1
    g["4_facing_plusY"] = g4
    mats = [str(m.get_editor_property("material_slot_name")) for m in mesh.get_editor_property("materials")]
    try:
        nsec = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).get_num_sections(mesh, 0)
    except Exception:  # noqa: BLE001
        nsec = None
    socks, sock_ok = {}, True
    for s in o.man["sockets"]:
        name = s["unreal_socket"]
        so = mesh.find_socket(name)
        if so is None:
            socks[name], sock_ok = "missing", False
            continue
        rel = unreal.Transform(so.get_editor_property("relative_location"), so.get_editor_property("relative_rotation"),
                               so.get_editor_property("relative_scale"))
        bone = str(so.get_editor_property("bone_name"))
        sc = rel.multiply(comp[bone])
        bm, ax, head = s["matrix_bone_space_cm"], o.bones[bone]["local_axes_world"], o.bones[bone]["head_m"]

        def bl_world_vec(v):  # bone-local vector (Blender) -> Blender world direction
            return [ax["x"][i] * v[0] + ax["y"][i] * v[1] + ax["z"][i] * v[2] for i in range(3)]
        loc_bl = bl_world_vec([bm[0][3] / 100, bm[1][3] / 100, bm[2][3] / 100])
        want = M([head[i] + loc_bl[i] for i in range(3)], sx)
        want_y = M(bl_world_vec([bm[0][1], bm[1][1], bm[2][1]]), sx)
        want_z = M(bl_world_vec([bm[0][2], bm[1][2], bm[2][2]]), sx)
        gx = sc.rotation.rotate_vector(unreal.Vector(1, 0, 0))
        gy = sc.rotation.rotate_vector(unreal.Vector(0, 1, 0))
        gz = sc.rotation.rotate_vector(unreal.Vector(0, 0, 1))
        cand = {"+X": gx, "-X": gx * -1, "+Y": gy, "-Y": gy * -1, "+Z": gz, "-Z": gz * -1}
        by = min(cand, key=lambda k: vangle(cand[k], want_y))
        bz = min(cand, key=lambda k: vangle(cand[k], want_z))
        e = {"bone": bone, "rel_loc": v3(rel.translation), "rel_scale": v3(rel.scale3d),
             "pos_err_cm": dist(sc.translation, want), "blender_Y_is_ue": by, "blender_Y_err_deg": vangle(cand[by], want_y),
             "blender_Z_is_ue": bz, "blender_Z_err_deg": vangle(cand[bz], want_z)}
        e["pass"] = (bone == s["bone"] and e["pos_err_cm"] <= 0.01 and e["blender_Y_err_deg"] <= 0.05
                     and e["blender_Z_err_deg"] <= 0.05 and max(abs(c - 1) for c in e["rel_scale"]) < 1e-4)
        sock_ok = sock_ok and e["pass"]
        socks[name] = e
    g5 = {"sections_lod0": nsec, "material_slots": mats, "sockets": socks}
    g5["pass"] = nsec in (2, None) and sorted(mats) == ["M_UP_Body", "M_UP_Head"] and sock_ok
    g["5_sections_sockets"] = g5
    return g, comp, sx


# ---------------------------------------------------------------- source-pose oracle (pose clips, additive looks)
def _norm(q):
    d = math.sqrt(sum(x * x for x in q))
    return [x / d for x in q]


def _qmatrix(q):
    x, y, z, w = _norm(q)
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def _matq(m):
    tr = sum(m[i][i] for i in range(3))
    if tr > 0:
        s = math.sqrt(tr + 1) * 2
        return _norm([(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, s / 4])
    i = max(range(3), key=lambda i: m[i][i])
    j, k = (i + 1) % 3, (i + 2) % 3
    s = math.sqrt(1 + m[i][i] - m[j][j] - m[k][k]) * 2
    q = [0.0] * 4
    q[i] = s / 4
    q[j] = (m[j][i] + m[i][j]) / s
    q[k] = (m[k][i] + m[i][k]) / s
    q[3] = (m[k][j] - m[j][k]) / s
    return _norm(q)


def _mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def _tr(a):
    return [list(r) for r in zip(*a)]


def _qang(q, r):  # normalized, shortest arc (q == -q)
    q, r = _norm(q), _norm(r)
    d = min(math.sqrt(sum((a - b) ** 2 for a, b in zip(q, r))), math.sqrt(sum((a + b) ** 2 for a, b in zip(q, r))))
    return math.degrees(4 * math.asin(min(1.0, d / 2)))


_C3 = [[1, 0, 0], [0, -1, 0], [0, 0, 1]]


def source_compare(o: Oracle, name, sets, comp_ref):
    """Every bone of every key against the source blend's evaluated pose: R = C R_pose R_rest^-1 C R_UE_rest."""
    mx, worst, nkeys = {"cm": 0.0, "deg": 0.0}, {}, {}
    for kind, poses in sets.items():
        nkeys[kind] = len(poses)
        for f, pose in enumerate(poses):
            for b in [str(x) for x in APE.get_bone_names(pose)]:
                t = APE.get_bone_pose(pose, b, WORLD)
                pose_m, rest_m = o.src["poses"][name][b], o.src["rest"][b]
                rq = comp_ref[b].rotation
                eq = _matq(_mul(_mul(_mul(_mul(_C3, _qmatrix(_matq(pose_m))), _tr(_qmatrix(_matq(rest_m)))), _C3),
                                _qmatrix([rq.x, rq.y, rq.z, rq.w])))
                ep = [pose_m[0][3] * 100, -pose_m[1][3] * 100, pose_m[2][3] * 100]
                cm = math.sqrt((t.translation.x - ep[0]) ** 2 + (t.translation.y - ep[1]) ** 2
                               + (t.translation.z - ep[2]) ** 2)
                dg = _qang([t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w], eq)
                if cm > mx["cm"]:
                    mx["cm"], worst["cm"] = cm, [kind, f, b]
                if dg > mx["deg"]:
                    mx["deg"], worst["deg"] = dg, [kind, f, b]
    return {"max_cm": mx["cm"], "max_deg": mx["deg"], "worst": worst, "keys_checked": nkeys,
            "pass": mx["cm"] <= 0.001 and mx["deg"] <= 0.01}


def source_reference_calibration(o: Oracle, comp_ref):
    return max(math.sqrt((comp_ref[b].translation.x - m[0][3] * 100) ** 2 + (comp_ref[b].translation.y + m[1][3] * 100) ** 2
                         + (comp_ref[b].translation.z - m[2][3] * 100) ** 2) for b, m in o.src["rest"].items())


# ---------------------------------------------------------------- gates 6-7: timing, compression, drift, strike tip
def drift(o: Oracle, poses, c, n, comp_ref, sx):
    """Largest horizontal slide of a planted sole point (world = pose + travel), over both feet."""
    tv = c["travel_vector_char_space"]
    vel = M([tv[0], tv[1], 0.0], sx)  # cm/s
    duty, dmax = c["duty_factor"], 0.0
    for side in "lr":
        for bone, p in o.exp["soles_blender_m"][side].values():
            loc = comp_ref[bone].inverse().transform_location(M(p, sx))
            start = None
            for f in range(n + 1):
                w = APE.get_bone_pose(poses[f], bone, WORLD).transform_location(loc) + vel * (f / 60.0)
                phase = (f / n - (0.0 if side == "l" else .5)) % 1.0
                if w.z <= 0.3 and phase <= duty + 1e-6:
                    if start is None:
                        start = w
                    dmax = max(dmax, math.hypot(w.x - start.x, w.y - start.y))
                else:
                    start = None
    return dmax


def tip_error(o: Oracle, pose9, mesh, comp_ref, sx):
    so = mesh.find_socket("hand_r_grip")
    rel = unreal.Transform(so.get_editor_property("relative_location"), so.get_editor_property("relative_rotation"),
                           so.get_editor_property("relative_scale"))
    tip_local = rel.multiply(comp_ref["hand_r"]).inverse().transform_location(M(o.exp["rest_tip_blender_m"], sx))
    tip = rel.multiply(APE.get_bone_pose(pose9, "hand_r", WORLD)).transform_location(tip_local)
    want = M(o.exp["target_blender_m"], sx)
    return {"tip_cm": v3(tip), "target_cm": v3(want), "err_cm": dist(tip, want)}


def clip_gates(o: Oracle, seq, name, mesh, comp_ref, sx):
    c = o.clips[name]
    n = POSE_IMPORTED_FRAMES if c.get("pose_clip") else c["frames"]
    nkeys, length = AL.get_num_keys(seq), seq.get_play_length()
    raw = frame_poses(seq, n, unreal.AnimDataEvalType.RAW, mesh)
    cmp_ = frame_poses(seq, n, unreal.AnimDataEvalType.COMPRESSED, mesh)
    names = [str(b) for b in APE.get_bone_names(raw[0])]
    seam = max(dist(APE.get_bone_pose(raw[n], b, WORLD).translation, APE.get_bone_pose(raw[0], b, WORLD).translation)
               for b in names)
    r0 = APE.get_bone_pose(raw[0], "root", LOCAL)
    root_dev = max(max(dist(APE.get_bone_pose(p, "root", LOCAL).translation, r0.translation) for p in raw),
                   max(qangle(APE.get_bone_pose(p, "root", LOCAL).rotation, r0.rotation) for p in raw))
    scale_dev = max(max(abs(x - 1) for x in v3(APE.get_bone_pose(p, b, LOCAL).scale3d)) for p in raw for b in names)
    g6 = {"frames": n, "num_keys": nkeys, "play_length_s": length, "expected_s": n / 60.0, "loop": c["loop"],
          "seam_cm": seam, "root_static_dev": root_dev, "bone_scale_max_dev": scale_dev,
          "root_motion_enabled": seq.get_editor_property("enable_root_motion")}
    # The seam (pose(N) = pose(0)) is a loop's contract only (M0 correction 1).
    g6["pass"] = (abs(length - n / 60.0) <= 1e-4 and nkeys == n + 1 and (seam <= 0.01 or not c["loop"])
                  and root_dev <= 1e-4 and scale_dev <= 1e-4 and not g6["root_motion_enabled"])
    if c.get("pose_clip"):
        tracks = [str(t) for t in AL.get_animation_track_names(seq)]
        g6["bone_tracks"] = len(tracks)
        g6["source_pose"] = source_compare(o, name, {"raw": raw, "compressed": cmp_}, comp_ref)
        g6["pass"] = g6["pass"] and len(tracks) == 22 and g6["source_pose"]["pass"]
    perr, rerr = 0.0, 0.0
    for f in range(n + 1):
        for b in names:
            a, k = APE.get_bone_pose(raw[f], b, WORLD), APE.get_bone_pose(cmp_[f], b, WORLD)
            perr, rerr = max(perr, dist(a.translation, k.translation)), max(rerr, qangle(a.rotation, k.rotation))
    g7 = {"compressed_pos_max_err_cm": perr, "compressed_rot_max_err_deg": rerr}
    ok7 = perr <= 0.05 and rerr <= 0.1
    if c.get("family") in ("Walk", "Sneak") and c.get("duty_factor") is not None:
        g7["planted_drift_raw_cm"] = drift(o, raw, c, n, comp_ref, sx)
        g7["planted_drift_compressed_cm"] = drift(o, cmp_, c, n, comp_ref, sx)
        ok7 = ok7 and g7["planted_drift_compressed_cm"] <= 0.1
    if name.startswith("UP_Pickaxe_Strike"):
        for label, poses, limit in (("raw", raw, 0.1), ("compressed", cmp_, 0.5)):
            g7["tip_f9_" + label] = tip_error(o, poses[9], mesh, comp_ref, sx)
            if name == "UP_Pickaxe_StrikeA":
                ok7 = ok7 and g7["tip_f9_" + label]["err_cm"] <= limit
    g7["pass"] = ok7
    return {"6_timing": g6, "7_compression": g7}


# ---------------------------------------------------------------- pickaxe
def _point_tri(p, a, b, c):
    """Distance from p to triangle abc (Ericson's closest point on a triangle)."""
    def sub(u, v):
        return (u[0] - v[0], u[1] - v[1], u[2] - v[2])

    def dot(u, v):
        return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]
    ab, ac, ap = sub(b, a), sub(c, a), sub(p, a)
    d1, d2 = dot(ab, ap), dot(ac, ap)
    if d1 <= 0 and d2 <= 0:
        q = a
    else:
        bp, cp = sub(p, b), sub(p, c)
        d3, d4, d5, d6 = dot(ab, bp), dot(ac, bp), dot(ab, cp), dot(ac, cp)
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


def pickaxe_gate(o: Oracle, sm):
    """The tip (manifest, tool frame; the grip socket maps tool +Y to UE -Y) lies on the mesh; the grip is the origin."""
    tip = o.man["pickaxe"]["tip_local_m"]
    p = (0.0, -tip[1] * 100, tip[2] * 100)
    best, ntri = 1e9, 0
    for sec in range(sm.get_num_sections(0)):
        r = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(sm, 0, sec)
        vs, tris = [(v.x, v.y, v.z) for v in r[0]], list(r[1])
        for i in range(0, len(tris), 3):
            ntri += 1
            best = min(best, _point_tri(p, vs[tris[i]], vs[tris[i + 1]], vs[tris[i + 2]]))
    bb = sm.get_bounding_box()
    out = {"triangles": ntri, "tip_cm": list(p), "tip_to_surface_cm": best, "bounds_min": v3(bb.min),
           "bounds_max": v3(bb.max),
           "origin_inside_bounds": bb.min.x <= 0 <= bb.max.x and bb.min.y <= 0 <= bb.max.y and bb.min.z <= 0 <= bb.max.z}
    out["pass"] = best <= 0.01 and out["origin_inside_bounds"]
    return out


# ---------------------------------------------------------------- additive looks
LOOK_BASES = (("UP_Look_", "UP_Look_C"), ("UP_CrouchLook_", "UP_CrouchLook_C"))


def look_base(unreal_name):
    """The additive base of a look pose ("UP_Look_C" or "UP_CrouchLook_C"), or None for other clips."""
    return next((base for prefix, base in LOOK_BASES if unreal_name.startswith(prefix)), None)


def additive_gate(o: Oracle, name, seq, base, mesh, comp_ref):
    """Mesh-space rotation offset on the base's frame 0, and the reconstructed full pose equals the source pose."""
    a = {"type": str(seq.get_editor_property("additive_anim_type")),
         "ref_pose_type": str(seq.get_editor_property("ref_pose_type")),
         "ref": seq.get_editor_property("ref_pose_seq").get_name() if seq.get_editor_property("ref_pose_seq") else None,
         "frame": seq.get_editor_property("ref_frame_index")}
    sets = {k: frame_poses(seq, POSE_IMPORTED_FRAMES, ev, mesh)
            for k, ev in (("raw", unreal.AnimDataEvalType.RAW), ("compressed", unreal.AnimDataEvalType.COMPRESSED))}
    a["full_pose_vs_source"] = source_compare(o, name, sets, comp_ref)
    a["pass"] = ("ROTATION_OFFSET_MESH_SPACE" in a["type"] and "ANIM_FRAME" in a["ref_pose_type"] and a["ref"] == base
                 and a["frame"] == 0 and a["full_pose_vs_source"]["pass"])
    return a
