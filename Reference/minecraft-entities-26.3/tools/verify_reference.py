"""Verify Reference/minecraft-entities-26.3: file hashes, re-derivation of textures.json from the copied textures, the
client jar when present, and spot values.

usage: uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-entities-26.3/tools/verify_reference.py
Everything but step 3 needs only this folder; step 3 runs when the pinned client jar is present.
"""
import hashlib
import importlib.util
import io
import json
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_reference", Path(__file__).with_name("build_reference.py"))
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)
errors = []


def check(condition, message):
    if not condition:
        errors.append(message)


# 1. Every file is listed with its SHA-256, and nothing else is in the folder.
sums = {}
for line in (ROOT / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines():
    digest, _, name = line.partition("  ")
    check(len(digest) == 64 and name and name not in sums, f"SHA256SUMS.txt: bad or repeated line {line!r}")
    sums[name] = digest
on_disk = {p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*")
           if p.is_file() and p.name != "SHA256SUMS.txt" and "__pycache__" not in p.parts}
check(on_disk == set(sums), f"file list differs from SHA256SUMS.txt: +{sorted(on_disk - set(sums))[:5]} "
                            f"-{sorted(set(sums) - on_disk)[:5]}")
check(sorted(sums) == list(sums), "SHA256SUMS.txt is not sorted by path")
for name, digest in sums.items():
    path = ROOT / name
    if path.is_file():
        check(hashlib.sha256(path.read_bytes()).hexdigest() == digest, f"hash mismatch: {name}")
expected = set(build.GENERATED) | set(build.TOOLS) | {"README.md"} | {build.PREFIX + n for n in build.TEXTURES}
check(expected == set(sums), f"files differ from the build's list: +{sorted(set(sums) - expected)[:5]} "
                             f"-{sorted(expected - set(sums))[:5]}")

# 2. Re-derive textures.json from the copied files.
manifest_path = ROOT / "textures.json"
try:
    derived = build.derive(lambda name: (ROOT / build.PREFIX / name).read_bytes())
    check(manifest_path.read_bytes() == derived, "re-derived textures.json differs")
except (SystemExit, OSError) as error:
    errors.append(f"re-derivation failed: {error!r}")
data = json.loads(manifest_path.read_text(encoding="utf-8"))

# 3. The client jar.
jar_checked = 0
if build.CLIENT_JAR.is_file():
    jar_bytes = build.CLIENT_JAR.read_bytes()
    if build.sha1(jar_bytes) == build.CLIENT_SHA1:
        with zipfile.ZipFile(io.BytesIO(jar_bytes)) as jar:
            for name in build.TEXTURES:
                check(jar.read(build.PREFIX + name) == (ROOT / build.PREFIX / name).read_bytes(),
                      f"differs from the client jar: {name}")
                jar_checked += 1
    else:
        print("note: the client jar differs from the pin; jar comparisons skipped")
else:
    print(f"note: client jar absent ({build.CLIENT_JAR}); jar comparisons skipped")

# 4. Spot values: the orb sheet is 64 x 64 (value report `xp.value_icons`: 16 x 16 tiles of a 64 x 64 texture).
by_name = {t["name"]: t for t in data["textures"]}
check([t["name"] for t in data["textures"]] == build.TEXTURES, "textures out of order or missing")
orb = by_name.get("entity/experience/experience_orb.png", {})
check((orb.get("width"), orb.get("height")) == (64, 64), f"experience_orb.png is not 64 x 64: {orb}")

if errors:
    print("REFERENCE FAIL")
    print("\n".join(errors[:40]))
    sys.exit(1)
print(f"REFERENCE OK: {len(sums)} files hashed, textures.json re-derived; {len(data['textures'])} textures; client jar "
      f"{'compared (%d)' % jar_checked if jar_checked else 'not compared'}")
