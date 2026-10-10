"""Verify Reference/minecraft-blocks-26.3: file hashes, jar bytes, counts, state ids and texture layers.

usage: uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-blocks-26.3/tools/verify_reference.py
The jar comparisons run when the pinned jars are present; everything else needs only this folder.
"""
import hashlib
import importlib.util
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
check(len(blocks) == 614 and [f["id"] for f in manifest["fluids"]] == ["water", "lava"],
      "expected 614 blocks + water and lava")
check(sum(b["shape"] == "full_cube" for b in blocks) == 429 and sum(b["shape"] == "slab" for b in blocks) == 101 and
      sum(b["shape"] == "layers" for b in blocks) == 1 and sum(b["shape"] == "model" for b in blocks) == 83,
      "expected 429 cubes (powder snow among them), 101 slabs, 1 layered, 83 model blocks")
models = json.loads((ROOT / "block_models.json").read_text(encoding="utf-8"))["models"]
texture_names = {t["name"] for t in textures["textures"]}
for model in models:
    for quad in model["quads"]:
        check(quad["t"] in texture_names and len(quad["v"]) == 4 and len(quad["uv"]) == 4 and
              all(-.5 <= c <= 1.5 for p in quad["v"] for c in p), f"baked quad: {quad}")
state_count = 0
for block in states["blocks"]:
    entry = report["minecraft:" + block["id"]]
    expected = {s["id"]: s.get("properties", {}) for s in entry["states"]}
    got = {s["id"]: s["properties"] for s in block["states"]}
    check(expected == got, f"state ids or properties differ: {block['id']}")
    state_count += len(got)
    for state in block.get("states", []):
        if state.get("shape") == "model":
            check("faces" not in state and state["model"] and all(
                part and all(0 <= i < len(models) for i in part) for part in state["model"]),
                f"model parts: {block['id']} {state['id']}")
        else:
            check(len(state.get("faces", [None] * 6)) == 6, f"face count: {block['id']} {state['id']}")
check(state_count == 4419, f"expected 4419 states, found {state_count}")

# Per-state facts (FactsDump.java): every imported block and state, in registry order.
facts = json.loads((ROOT / "facts/block_state_facts.json").read_text(encoding="utf-8"))
check([b["id"] for b in facts["blocks"]] == [b["id"] for b in sorted(blocks + manifest["fluids"], key=lambda b: b["protocol_id"])],
      "block_state_facts.json: block list differs from the manifest")
fact_states = {s["id"] for b in facts["blocks"] for s in b["states"]}
check(fact_states == {s["id"] for b in states["blocks"] for s in b["states"]}, "block_state_facts.json: state ids differ")

# Items: block items for every block whose vanilla item has its id; model blocks' icons resolve.
items = json.loads((ROOT / "items.json").read_text(encoding="utf-8"))["items"]
for item in items:
    icon = item.get("icon")
    if icon and "sprite" in icon:
        for layer in icon["sprite"]:
            name = layer["texture"]
            check(f"assets/minecraft/textures/{name}.png" in sums if name.startswith("item/") else
                  name.removeprefix("block/") in texture_names, f"icon sprite: {item['id']} {name}")
    elif icon:
        check(0 <= icon["model"] < len(models), f"icon model: {item['id']}")
check(len(items) == 600 and sum("icon" in i for i in items) == 71, "expected 600 items, 71 with model icons")

# Re-bake every model state from the jar's models and compare (needs the client jar).
rebaked = 0
if jar_path.exists():
    def load(name):
        spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(f"{name}.py"))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    build, bake = load("build_reference"), load("bake_models")
    baker = bake.Baker(build, build.Models(jar))
    model_ids = {b["id"] for b in blocks if b["shape"] == "model"}
    for block in states["blocks"]:
        if block["id"] not in model_ids:
            continue
        blockstate = json.loads(jar.read(f"assets/minecraft/blockstates/{block['id']}.json"))
        tints = build.TINT_INDICES.get(block["id"], frozenset({0})) if block["id"] in build.TINT_SOURCES else frozenset()
        for state in block["states"]:
            parts = baker.bake_state(blockstate, state["properties"], tints)
            check([[baker.baked[i] for i in part] for part in parts] ==
                  [[models[i] for i in part] for part in state["model"]], f"re-bake differs: {block['id']} {state['id']}")
            rebaked += 1

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
# Budgets: face codes hold a texture index in 10 bits (index 0 is the importer's white texture); the block array
# holds every frame plus the white layer, within Unreal's 2,048-slice texture array limit.
TEXTURE_CODES, ARRAY_SLICES = 1024, 2048
check(len(textures["textures"]) + 1 <= TEXTURE_CODES, "texture indices overflow the 10-bit face code field")
check(layers["blocks"] + 1 <= ARRAY_SLICES, "block layers overflow the texture array")

if errors:
    print("REFERENCE FAIL")
    print("\n".join(errors[:40]))
    sys.exit(1)
print(f"REFERENCE OK: {len(sums)} files hashed, {jar_checked} compared with the jar, {len(blocks)} blocks + water and lava, "
      f"{state_count} states ({rebaked} model states re-baked), {layers['blocks']} block layers + {layers['water_flow']} "
      f"flow layers; budgets: {len(textures['textures']) + 1} of {TEXTURE_CODES} texture codes, "
      f"{layers['blocks'] + 1} of {ARRAY_SLICES} array slices")
