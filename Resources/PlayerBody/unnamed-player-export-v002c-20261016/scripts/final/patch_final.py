"""One-off patch of scripts/final/ue_m0_lib.py for the M0 final run:
- the 20 pose clips import with Set Range 0..2 at 60 Hz (per up-pose-clip-investigation-20261016 REPORT.md);
- pose clips are verified per bone, every key, raw and compressed, against the SOURCE .blend pose
  (method copied from that report's compare_ue.py: (x,-y,z)*100; C*R_src_pose*inv(R_src_rest)*C*R_UE_rest; normalized,
  shortest-arc quaternions); additive looks are verified as reconstructed full poses the same way;
- no folder deletion here (cleanup is a separate run after the log scan)."""
import os
import sys

D = os.path.dirname(os.path.abspath(__file__))
FINAL = sys.argv[1]


def patch(fn, pairs):
    p = os.path.join(D, fn)
    s = open(p).read()
    for a, b in pairs:
        assert s.count(a) == 1, (fn, a[:70])
        s = s.replace(a, b)
    open(p, "w").write(s)


patch("ue_m0_lib.py", [
    ('''LINES = []''', f'''LINES = []
FINAL = r"{FINAL}"
SRC = json.load(open(os.path.join(FINAL, "source_poses.json")))
POSE_IMPORTED_FRAMES = 2   # Set Range 0..2 at 60 Hz -> 3 keys over 2/60 s (source: 2 keys, 1/60 s)'''),
    # pose-clip import settings
    ('''        p = make_pipeline(False, skel, un)
''', '''        p = make_pipeline(False, skel, un)
        if CLIPS[name].get("pose_clip"):
            ap = p.get_editor_property("animation_pipeline")
            ap.set_editor_property("animation_range", unreal.InterchangeAnimationRange.SET_RANGE)
            interval = unreal.Int32Interval()
            interval.set_editor_property("min", 0)
            interval.set_editor_property("max", POSE_IMPORTED_FRAMES)
            ap.set_editor_property("frame_import_range", interval)
            ap.set_editor_property("custom_bone_animation_sample_rate", 60)
            if "pipeline_pose_clip_effective" not in REPORT:
                REPORT["pipeline_pose_clip_effective"] = readback(p)
'''),
    # gate 6/7 frame count for pose clips
    ('''    c = CLIPS[name]
    n = c["frames"]
    g = {}''', '''    c = CLIPS[name]
    n = POSE_IMPORTED_FRAMES if c.get("pose_clip") else c["frames"]
    g = {}'''),
    ('''        if c.get("pose_clip"):
            sp = single_pose_check(raw[0], comp_ref, name, sx)
            g6["pose_vs_roundtrip_t0"] = sp
            g6["pass"] = g6["pass"] and sp["max_err_cm"] <= 0.001 and sp["delta_angle_max_err_deg"] <= 0.01''',
     '''        if c.get("pose_clip"):
            g6["source_timing"] = {"keys": 2, "seconds": 1 / 60.0}
            g6["imported_timing_expected"] = {"keys": 3, "seconds": 2 / 60.0}
            try:
                tracks = [str(t) for t in AL.get_animation_track_names(seq)]
            except Exception as ex:  # noqa: BLE001
                tracks = [f"<unavailable: {ex}>"]
            g6["bone_tracks"] = len(tracks)
            sc = source_compare(name, {"raw": raw, "compressed": frame_poses(seq, n, unreal.AnimDataEvalType.COMPRESSED,
                                                                              mesh)}, comp_ref)
            g6["source_pose"] = sc
            g6["pass"] = g6["pass"] and len(tracks) == 22 and sc["pass"]'''),
    # source comparison helpers
    ('''def drift(poses''', '''def _norm(q):
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


def _qang(q, r):   # normalized, shortest arc (q == -q)
    q, r = _norm(q), _norm(r)
    d = min(math.sqrt(sum((a - b) ** 2 for a, b in zip(q, r))), math.sqrt(sum((a + b) ** 2 for a, b in zip(q, r))))
    return math.degrees(4 * math.asin(min(1.0, d / 2)))


_C3 = [[1, 0, 0], [0, -1, 0], [0, 0, 1]]


def source_expected(name, b, comp_ref):
    pose, rest = SRC["poses"][name][b], SRC["rest"][b]
    rq = comp_ref[b].rotation
    ur = _qmatrix([rq.x, rq.y, rq.z, rq.w])
    q = _matq(_mul(_mul(_mul(_mul(_C3, _qmatrix(_matq(pose))), _tr(_qmatrix(_matq(rest)))), _C3), ur))
    return [pose[0][3] * 100, -pose[1][3] * 100, pose[2][3] * 100], q


def source_compare(name, sets, comp_ref):
    mx = {"cm": 0.0, "deg": 0.0}
    worst = {}
    nkeys = {}
    for kind, poses in sets.items():
        nkeys[kind] = len(poses)
        for f, pose in enumerate(poses):
            names = [str(x) for x in APE.get_bone_names(pose)]
            assert len(names) == 22 and set(names) == set(SRC["rest"]), (name, kind, f, names)
            for b in names:
                t = APE.get_bone_pose(pose, b, WORLD)
                ep, eq = source_expected(name, b, comp_ref)
                cm = math.sqrt((t.translation.x - ep[0]) ** 2 + (t.translation.y - ep[1]) ** 2 + (t.translation.z - ep[2]) ** 2)
                dg = _qang([t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w], eq)
                if cm > mx["cm"]:
                    mx["cm"], worst["cm"] = cm, [kind, f, b]
                if dg > mx["deg"]:
                    mx["deg"], worst["deg"] = dg, [kind, f, b]
    return {"max_cm": mx["cm"], "max_deg": mx["deg"], "worst": worst, "keys_checked": nkeys,
            "pass": mx["cm"] <= 0.001 and mx["deg"] <= 0.01}


def source_reference_calibration(comp_ref):
    return max(math.sqrt((comp_ref[b].translation.x - m[0][3] * 100) ** 2 + (comp_ref[b].translation.y + m[1][3] * 100) ** 2
                         + (comp_ref[b].translation.z - m[2][3] * 100) ** 2) for b, m in SRC["rest"].items())


def drift(poses'''),
    # additive: full-pose reconstruction vs source (replaces the delta sanity)
    ('''    for name, a in REPORT["additive"].items():   # informational: max additive rotation delta (base clips ~0)
        try:
            o = opts(unreal.AnimDataEvalType.COMPRESSED, mesh)
            o.set_editor_property("retrieve_additive_as_full_pose", False)
            pz = APE.get_anim_pose_at_frame(seqs[name], 0, o)
            a["delta_max_deg"] = max(qangle(APE.get_bone_pose(pz, b, LOCAL).rotation, unreal.Quat(0, 0, 0, 1))
                                     for b in [str(x) for x in APE.get_bone_names(pz)])
        except Exception as ex:  # noqa: BLE001
            a["delta_max_deg"] = f"<unavailable: {ex}>"''',
     '''    REPORT["source_reference_calibration_cm"] = source_reference_calibration(comp)
    for name, a in REPORT["additive"].items():   # additive look reconstructed as a full pose vs the source pose
        try:
            sets = {k: frame_poses(seqs[name], POSE_IMPORTED_FRAMES, ev, mesh)
                    for k, ev in (("raw", unreal.AnimDataEvalType.RAW), ("compressed", unreal.AnimDataEvalType.COMPRESSED))}
            a["full_pose_vs_source"] = source_compare(name, sets, comp)
        except Exception as ex:  # noqa: BLE001
            a["full_pose_vs_source"] = {"pass": False, "error": str(ex)}'''),
    ('''                                and REPORT["additive"][n]["frame"] == 0 for n in looks)''',
     '''                                and REPORT["additive"][n]["frame"] == 0
                                and REPORT["additive"][n].get("full_pose_vs_source", {}).get("pass") for n in looks)
    REPORT["additive_count_looks"] = len(looks)'''),
])

patch("run_ue.ps1", [
    ('''$log = Join-Path $E ("evidence\\ue_{0}_{1}.log" -f $Tag, (Get-Date -Format 'yyyyMMdd_HHmmss'))''',
     f'''$log = Join-Path '{FINAL}' ("ue_{{0}}_{{1}}.log" -f $Tag, (Get-Date -Format 'yyyyMMdd_HHmmss'))'''),
])
print("patched")
