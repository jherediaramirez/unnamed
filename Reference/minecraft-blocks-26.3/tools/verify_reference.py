"""Verify Reference/minecraft-blocks-26.3: file hashes, jar bytes, counts, state ids and texture layers.

usage: uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-blocks-26.3/tools/verify_reference.py
The jar comparisons run when the pinned jars are present; everything else needs only this folder.
"""
import hashlib
import json
import os
import sys
import zipfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
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

manifest = json.loads((ROOT / "block_manifest.json").read_text(encoding="utf-8"))
states = json.loads((ROOT / "block_states.json").read_text(encoding="utf-8"))
textures = json.loads((ROOT / "textures.json").read_text(encoding="utf-8"))
report = json.loads((ROOT / "reports/blocks-imported.json").read_text(encoding="utf-8"))

jar_path = Path(os.environ.get("APPDATA", "")) / "ModrinthApp/meta/versions/26.3/26.3.jar"
jar_checked = 0
if jar_path.exists():
    check(hashlib.sha1(jar_path.read_bytes()).hexdigest() == manifest["client_sha1"], "client jar SHA-1 differs")
    jar = zipfile.ZipFile(jar_path)
    for name in sums:
        if name.startswith("assets/minecraft/"):
            jar_checked += 1
            check((ROOT / name).read_bytes() == jar.read(name), f"differs from the jar: {name}")

# Counts and state ids: every imported state is in the report with the same id and properties.
blocks = manifest["blocks"]
check(len(blocks) == 529 and len(manifest["fluids"]) == 1, "expected 529 blocks + water")
check(sum(b["shape"] == "full_cube" for b in blocks) == 428 and sum(b["shape"] == "slab" for b in blocks) == 101,
      "expected 428 cubes + 101 slabs")
state_count = 0
for block in states["blocks"]:
    entry = report["minecraft:" + block["id"]]
    expected = {s["id"]: s.get("properties", {}) for s in entry["states"]}
    got = {s["id"]: s["properties"] for s in block["states"]}
    check(expected == got, f"state ids or properties differ: {block['id']}")
    state_count += len(got)
    for state in block.get("states", []):
        check(len(state.get("faces", [None] * 6)) == 6, f"face count: {block['id']} {state['id']}")
check(state_count == 3445, f"expected 3445 states, found {state_count}")

# Texture layers: decoding every PNG reproduces the recorded per-frame RGBA hashes.
layers = {"blocks": 0, "water_flow": 0}
for texture in textures["textures"]:
    img = Image.open(ROOT / f"assets/minecraft/textures/block/{texture['name']}.png").convert("RGBA")
    fw, fh = texture["frame_size"]
    for index, digest in enumerate(texture["layer_rgba_sha256"]):
        frame = img.crop((0, index * fh, fw, (index + 1) * fh))
        check(hashlib.sha256(frame.tobytes()).hexdigest() == digest, f"layer hash: {texture['name']} {index}")
    check(texture["first_layer"] == layers[texture["array"]], f"layer order: {texture['name']}")
    layers[texture["array"]] += texture["frame_count"]
check(layers == {k: v["layers"] for k, v in textures["arrays"].items()}, f"layer totals {layers}")

if errors:
    print("REFERENCE FAIL")
    print("\n".join(errors[:40]))
    sys.exit(1)
print(f"REFERENCE OK: {len(sums)} files hashed, {jar_checked} compared with the jar, {len(blocks)} blocks + water, "
      f"{state_count} states, {layers['blocks']} block layers + {layers['water_flow']} water flow layers")
