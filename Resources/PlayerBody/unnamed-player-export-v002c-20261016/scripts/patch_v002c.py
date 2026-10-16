"""One-off patch v002b -> v002c: single-pose clips exported as 2 identical keys (frames 0..1)."""
import os

D = os.path.dirname(os.path.abspath(__file__))


def patch(fn, pairs):
    p = os.path.join(D, fn)
    s = open(p).read()
    for a, b in pairs:
        assert s.count(a) == 1, (fn, a[:70])
        s = s.replace(a, b)
    open(p, "w").write(s)


patch("export_up.py", [
    ('''    n = frames[name]
    scene.frame_start, scene.frame_end = 0, max(n, 0)''',
     '''    n = frames[name]
    # v002c: a single-pose clip (source action has one key at 0) is baked over 0..1; constant extrapolation makes
    # frame 1 the exact frame-0 value. UE 5.8.3 Interchange rejects a zero-length bake range (v002b proof).
    scene.frame_start, scene.frame_end = 0, (n if n > 0 else 1)'''),
    ('''    clips.append(e)''',
     '''    if frames[name] == 0:
        e.update({"frames": 1, "keys": 2, "seconds": round(1 / 60, 6), "pose_clip": True,
                  "pose_clip_note": "pose clip: runtime samples t=0", "source_frames": 0})
    clips.append(e)'''),
])

patch("roundtrip_up.py", [
    ('''    if c["frames"] == 0:   # v002c: single-pose reference for the UE RangeStop check (world metres, head + tail)
        sc.frame_set(start)
        e["pose0_world_m"] = {pb.name: {"head": list(arm.matrix_world @ pb.head), "tail": list(arm.matrix_world @ pb.tail)}
                              for pb in arm.pose.bones}''',
     '''    if c.get("pose_clip"):   # v002c: 2-key pose clip; keys 0 and 1 must match, and equal the v002b pose
        poses = []
        for f in (0, 1):
            sc.frame_set(start + f)
            poses.append({pb.name: {"head": list(arm.matrix_world @ pb.head), "tail": list(arm.matrix_world @ pb.tail),
                                    "delta_angle_deg": math.degrees((arm.matrix_world @ pb.matrix).to_quaternion()
                                                                    .rotation_difference((arm.matrix_world @ pb.bone.matrix_local)
                                                                                         .to_quaternion()).angle)}
                          for pb in arm.pose.bones})
        e["pose0_world_m"] = poses[0]
        e["key01_max_diff_m"] = max(max((Vector(poses[0][b][k]) - Vector(poses[1][b][k])).length for k in ("head", "tail"))
                                    for b in poses[0])
        old = V002B["anims"][name]["pose0_world_m"]
        e["vs_v002b_max_diff_m"] = max(max((Vector(poses[0][b][k]) - Vector(old[b][k])).length for k in ("head", "tail"))
                                       for b in old)
        e["pose_clip_ok"] = e["key01_max_diff_m"] <= 1e-6 and e["vs_v002b_max_diff_m"] <= 1e-6'''),
    ('''man = json.load(open(os.path.join(OUT, "UP_manifest_v002c.json")))''',
     '''man = json.load(open(os.path.join(OUT, "UP_manifest_v002c.json")))
V002B = json.load(open(r"C:\\dev\\output\\unnamed-player-export-v002b-20261015\\evidence\\roundtrip_v002b.json"))'''),
])
print("patched")
