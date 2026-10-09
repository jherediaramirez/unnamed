"""Build Reference/minecraft-worldgen-26.3 from the 26.3 server jar: the overworld terrain's worldgen JSON, byte for byte.

usage: uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/build_reference.py [server jar]
Copies the noise settings of the three overworld world types, the density functions they use and every top-level noise
into data/, replacing it, then writes manifest.json and SHA256SUMS.txt over the whole folder. facts/ comes from
tools/WorldgenFacts.java and is only hashed here, so run this again after refreshing the facts.
"""
import hashlib
import json
import shutil
import sys
import zipfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_JAR = Path(r"C:\dev\sandbox\mc-26.3-blocks\datagen\versions\26.3\server-26.3.jar")
PREFIX = "data/minecraft/worldgen/"
PRESETS = ("overworld", "large_biomes", "amplified")
FUNCTION_FOLDERS = ("overworld/", "overworld_amplified/", "overworld_large_biomes/")


def wanted(name):
    if not name.startswith(PREFIX) or not name.endswith(".json") or ".." in name or "\\" in name:
        return False
    rel = name[len(PREFIX):]
    folder, _, rest = rel.partition("/")
    if folder == "noise_settings":
        return rest[:-5] in PRESETS
    if folder == "density_function":
        return "/" not in rest or rest.startswith(FUNCTION_FOLDERS)
    if folder == "noise":
        return "/" not in rest
    return False


jar_path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_JAR
jar_bytes = jar_path.read_bytes()
jar = zipfile.ZipFile(jar_path)
names = sorted(n for n in jar.namelist() if wanted(n))
counts = Counter(name[len(PREFIX):].split("/", 1)[0] for name in names)
EXPECTED = {"noise_settings": 3, "density_function": 51, "noise": 62}
if dict(counts) != EXPECTED:
    sys.exit(f"BUILD FAIL: {jar_path} gives {dict(counts)}, expected {EXPECTED}; data/ is left as it was")

data_root = ROOT / "data"
if data_root.exists():
    shutil.rmtree(data_root)
for name in names:
    target = ROOT / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(jar.read(name))

manifest = {
    "version": "26.3",
    "source": {
        "file": "server-26.3.jar, from the official 26.3 server download",
        "sha256": hashlib.sha256(jar_bytes).hexdigest(),
        "sha1": hashlib.sha1(jar_bytes).hexdigest(),
        "size": len(jar_bytes),
    },
    "presets": list(PRESETS),
    "counts": {"noise_settings": counts["noise_settings"], "density_function": counts["density_function"],
               "noise": counts["noise"]},
}
(ROOT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")

files = sorted(p for p in ROOT.rglob("*")
               if p.is_file() and p.name != "SHA256SUMS.txt" and "__pycache__" not in p.parts)
lines = [f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(ROOT).as_posix()}" for p in files]
(ROOT / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
print(f"BUILT: {len(names)} data files {dict(manifest['counts'])}, {len(lines)} files hashed")
