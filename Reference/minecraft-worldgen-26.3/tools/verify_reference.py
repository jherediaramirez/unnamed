"""Verify Reference/minecraft-worldgen-26.3: file hashes, jar bytes, references between files, and the spline facts.

usage: uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/verify_reference.py [server jar]
The jar comparison runs when the jar is present; everything else needs only this folder.
"""
import hashlib
import json
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_JAR = Path(r"C:\dev\sandbox\mc-26.3-blocks\datagen\versions\26.3\server-26.3.jar")
WORLDGEN = ROOT / "data/minecraft/worldgen"
errors = []


def check(condition, message):
    if not condition:
        errors.append(message)


sums = {}
for line in (ROOT / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines():
    digest, name = line.split("  ", 1)
    sums[name] = digest
on_disk = {p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*")
           if p.is_file() and p.name != "SHA256SUMS.txt" and "__pycache__" not in p.parts}
check(on_disk == set(sums), f"file list differs from SHA256SUMS.txt: +{sorted(on_disk - set(sums))[:5]} "
                            f"-{sorted(set(sums) - on_disk)[:5]}")
for name, digest in sums.items():
    path = ROOT / name
    if path.is_file():
        check(hashlib.sha256(path.read_bytes()).hexdigest() == digest, f"hash mismatch: {name}")

manifest = json.loads((ROOT / "manifest.json").read_text(encoding="utf-8"))
data_files = sorted(n for n in sums if n.startswith("data/"))
check(manifest["counts"] == {"noise_settings": 3, "density_function": 51, "noise": 62}, f"counts {manifest['counts']}")
check(len(data_files) == 116, f"expected 116 data files, found {len(data_files)}")

jar_path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_JAR
jar_checked = 0
if jar_path.exists():
    check(hashlib.sha256(jar_path.read_bytes()).hexdigest() == manifest["source"]["sha256"], "server jar SHA-256 differs")
    jar = zipfile.ZipFile(jar_path)
    for name in data_files:
        jar_checked += 1
        check((ROOT / name).read_bytes() == jar.read(name), f"differs from the jar: {name}")


def function_file(name):
    return WORLDGEN / "density_function" / (name.removeprefix("minecraft:") + ".json")


def noise_file(name):
    return WORLDGEN / "noise" / (name.removeprefix("minecraft:") + ".json")


# References: in a density function, "noise" names a noise, "type" and "axis" are not references, and every other
# string (arguments and spline coordinates) names a density function.
def references(value, out, key=None):
    if isinstance(value, dict):
        for k, v in value.items():
            references(v, out, k)
    elif isinstance(value, list):
        for v in value:
            references(v, out, key)
    elif isinstance(value, str) and key not in ("type", "axis"):
        out.append((key == "noise", value))


reached = []
for preset in manifest["presets"]:
    settings = json.loads((WORLDGEN / "noise_settings" / f"{preset}.json").read_text(encoding="utf-8"))
    check(settings["sea_level"] == 63 and settings["noise"] == {"min_y": -64, "height": 384}, f"{preset}: height or sea")
    check(len(settings["noise_router"]) == 8, f"{preset}: router entries {sorted(settings['noise_router'])}")
    found = []
    references([settings["noise_router"], settings["aquifers"], [d["function"] for d in settings["debug_functions"]]],
               found)
    reached += found
for path in (WORLDGEN / "density_function").rglob("*.json"):
    references(json.loads(path.read_text(encoding="utf-8")), reached)
for is_noise, name in reached:
    check((noise_file if is_noise else function_file)(name).is_file(), f"unresolved reference: {name}")

# Facts: every sampled function holds exactly one spline over the listed coordinates.
facts = json.loads((ROOT / "facts/spline_samples.json").read_text(encoding="utf-8"))
sample_count = 0
for entry in facts["splines"]:
    text = function_file(entry["function"]).read_text(encoding="utf-8")
    check(text.count('"minecraft:spline"') == 1, f"{entry['function']}: expected one spline")
    for coordinate in entry["coordinates"]:
        check(f'"coordinate": "{coordinate}"' in text, f"{entry['function']}: no coordinate {coordinate}")
    check(len(entry["samples"]) == facts["samples_per_spline"], f"{entry['function']}: sample count")
    for sample in entry["samples"]:
        check(len(sample) == len(entry["coordinates"]) + 1, f"{entry['function']}: sample width")
    sample_count += len(entry["samples"])
check(len(facts["splines"]) == 9, "expected 9 splines in the facts")

if errors:
    print("REFERENCE FAIL")
    print("\n".join(errors[:40]))
    sys.exit(1)
jar_note = f"{jar_checked} compared with the jar" if jar_path.exists() else f"NO JAR at {jar_path}, bytes not compared"
print(f"REFERENCE OK: {len(sums)} files hashed, {jar_note}, {len(reached)} references resolved, "
      f"{len(facts['splines'])} splines with {sample_count} samples")
