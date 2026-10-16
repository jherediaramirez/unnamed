"""One-off patch of ue_m0_lib.py / entry scripts for v002c: 2-key pose clips, all 18 looks additive."""
import os

D = os.path.dirname(os.path.abspath(__file__))


def patch(fn, pairs):
    p = os.path.join(D, fn)
    s = open(p).read()
    for a, b in pairs:
        assert s.count(a) == 1, (fn, a[:70])
        s = s.replace(a, b)
    open(p, "w").write(s)


patch("ue_m0_lib.py", [
    # gate 6: pose clips (2 identical keys) must match the round trip at t=0 (<=0.001 cm, <=0.01 deg)
    ('''    if n > 0:
        g6["pass"] = (abs(length - n / 60.0) <= 1e-4 and nkeys == n + 1 and seam <= 0.01 and root_dev <= 1e-4
                      and scale_dev <= 1e-4 and not g6["root_motion_enabled"])''',
     '''    if n > 0:
        g6["pass"] = (abs(length - n / 60.0) <= 1e-4 and nkeys == n + 1 and seam <= 0.01 and root_dev <= 1e-4
                      and scale_dev <= 1e-4 and not g6["root_motion_enabled"])
        if c.get("pose_clip"):
            sp = single_pose_check(raw[0], comp_ref, name, sx)
            g6["pose_vs_roundtrip_t0"] = sp
            g6["pass"] = g6["pass"] and sp["max_err_cm"] <= 0.001 and sp["delta_angle_max_err_deg"] <= 0.01'''),
    ('''    he, te = 0.0, 0.0
    for b, ht in ref.items():
        pc = APE.get_bone_pose(pose, b, WORLD)
        he = max(he, dist(pc.translation, M(ht["head"], sx)))''',
     '''    he, te, ae = 0.0, 0.0, 0.0
    for b, ht in ref.items():
        pc = APE.get_bone_pose(pose, b, WORLD)
        he = max(he, dist(pc.translation, M(ht["head"], sx)))
        if "delta_angle_deg" in ht:   # rotation vs rest; the angle is invariant under the axis conversion
            d = pc.multiply(comp_ref[b].inverse())
            ae = max(ae, abs(qangle(d.rotation, unreal.Quat(0, 0, 0, 1)) - ht["delta_angle_deg"]))'''),
    ('''    return {"head_max_err_cm": he, "tail_max_err_cm": te, "max_err_cm": max(he, te), "bones": len(ref)}''',
     '''    return {"head_max_err_cm": he, "tail_max_err_cm": te, "max_err_cm": max(he, te), "delta_angle_max_err_deg": ae,
            "bones": len(ref)}'''),
    # additive: all 18 looks (incl. the _C references themselves, set last), read back, delta sanity
    ('''    for name, seq in seqs.items():
        key = name[3:]
        for pre, ref in ref_c.items():
            if key.startswith(pre) and not key.endswith("_C") and ref is not None:''',
     '''    order = sorted(seqs, key=lambda n: n.endswith("_C"))   # the two base clips last
    for name in order:
        seq = seqs[name]
        key = name[3:]
        for pre, ref in ref_c.items():
            if key.startswith(pre) and ref is not None:'''),
    ('''                REPORT["additive"][name] = {"type": str(seq.get_editor_property("additive_anim_type")),
                                            "ref": ref.get_name(), "frame": seq.get_editor_property("ref_frame_index")}
    EAL.save_directory(folder, only_if_is_dirty=False, recursive=True)''',
     '''                REPORT["additive"][name] = {"type": str(seq.get_editor_property("additive_anim_type")),
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
                                and REPORT["additive"][n]["frame"] == 0 for n in looks)'''),
    ('''                         "pickaxe": REPORT.get("pickaxe", {}).get("pass"),''',
     '''                         "pickaxe": REPORT.get("pickaxe", {}).get("pass"),
                         "additive_ok": REPORT.get("additive_ok"), "additive_count": len(REPORT.get("additive", {})),'''),
    ('''                         and REPORT.get("pickaxe", {"pass": True})["pass"]''',
     '''                         and REPORT.get("pickaxe", {"pass": True})["pass"] and REPORT.get("additive_ok", True)'''),
])

patch("ue_proof.py", [
    ('''PROOF = ["UP_Idle", "UP_Walk_F", "UP_Sneak_FR", "UP_Look_C", "UP_Look_D", "UP_Pickaxe_StrikeA"]''',
     '''PROOF = ["UP_Idle", "UP_Walk_F", "UP_Sneak_FR", "UP_Look_C", "UP_Look_D", "UP_Pickaxe_StrikeA"]
PROOF += sorted(n for n, c in L.CLIPS.items() if c.get("pose_clip") and n not in PROOF)   # all 20 pose clips'''),
    ('''"/Game/Characters/UnnamedPlayer_ProofB"''', '''"/Game/Characters/UnnamedPlayer_ProofC"'''),
])

patch("ue_batch.py", [
    ('''delete_folders_after=("/Game/Characters/UnnamedPlayer_ProofB", "/Game/Characters/UnnamedPlayer_Proof")''',
     '''delete_folders_after=("/Game/Characters/UnnamedPlayer_ProofC", "/Game/Characters/UnnamedPlayer_ProofB",
                                                           "/Game/Characters/UnnamedPlayer_Proof")'''),
])
print("patched")
