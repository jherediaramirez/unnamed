"""Re-score gate 6 from a final-run JSON with the seam condition applied to LOOPS only (the contract: "a loop has
identical keys 0 and N"); one-shot clips keep keys/length/root/scale/root-motion. Writes a new *_regated.json.
    uv run --no-project --python 3.13 python -I regate_offline.py <ue_final_*.json> <manifest>"""
import json
import sys

src, manp = sys.argv[1], sys.argv[2]
r = json.load(open(src))
loop = {c["name"]: c["loop"] for c in json.load(open(manp))["clips"]}
changed = {}
for n, e in r["clips"].items():
    g = e["gates"]["6_timing"]
    if g["pass"] or loop[n] or g["frames"] == 0:
        continue
    ok = (abs(g["play_length_s"] - g["frames"] / 60.0) <= 1e-4 and g["num_keys"] == g["frames"] + 1
          and g["root_static_dev"] <= 1e-4 and g["bone_scale_max_dev"] <= 1e-4 and not g["root_motion_enabled"]
          and g.get("source_pose", {"pass": True})["pass"])
    g["seam_applies"] = False
    g["pass_original"] = g["pass"]
    g["pass"] = ok
    changed[n] = {"seam_cm_one_shot": g["seam_cm"], "pass": ok}
clip_pass = {n: all(v.get("pass") for v in e["gates"].values()) and "error" not in e for n, e in r["clips"].items()}
r["summary"]["clips_pass"] = clip_pass
r["summary"]["regated_one_shot_seam"] = changed
r["summary"]["all_pass_except_pickaxe"] = (all(r["summary"]["mesh_gates"].values()) and all(clip_pass.values())
                                           and r["summary"]["gate8"] and r["summary"].get("additive_ok"))
out = src.replace(".json", "_regated.json")
json.dump(r, open(out, "w"), indent=1)
print(json.dumps({"regated": changed, "clips_passing": sum(clip_pass.values()), "of": len(clip_pass),
                  "all_pass_except_pickaxe": r["summary"]["all_pass_except_pickaxe"]}))
