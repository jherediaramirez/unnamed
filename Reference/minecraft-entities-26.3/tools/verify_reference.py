"""Verify Reference/minecraft-entities-26.3: file hashes, re-derivation of textures.json from the copied textures, the
client jar when present (textures and loot tables), and spot values from the facts (mob attributes, model sizes).

usage: uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-entities-26.3/tools/verify_reference.py
Everything but step 3 needs only this folder; step 3 runs when the pinned client jar is present. facts/ is produced by
tools/EntityModelFacts.java and tools/mob_probe.py; here it is only hashed and checked for shape and spot values.
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
expected = set(build.GENERATED) | set(build.TOOLS) | {"README.md"} | {build.PREFIX + n for n in build.TEXTURES} \
    | {build.LOOT_PREFIX + n for n in build.LOOT} | {build.RECIPE_PREFIX + n for n in build.RECIPES} | set(build.FACTS)
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
            for name in build.LOOT:
                check(jar.read(build.LOOT_PREFIX + name) == (ROOT / build.LOOT_PREFIX / name).read_bytes(),
                      f"differs from the client jar: {name}")
                jar_checked += 1
            for name in build.RECIPES:
                check(jar.read(build.RECIPE_PREFIX + name) == (ROOT / build.RECIPE_PREFIX / name).read_bytes(),
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

# Survival 20a: the sheep sheets are 64 x 32, the cow sheets 64 x 64; cow max health 10, speed 0.2; the cow model has 7 parts
# (root, head, body, 4 legs) and 10 cubes; the attributes record the server jar pin.
check(all(by_name[f"entity/sheep/{n}.png"]["height"] == 32 and by_name[f"entity/sheep/{n}.png"]["width"] == 64
          for n in ("sheep", "sheep_wool", "sheep_wool_undercoat")), "sheep textures are not 64 x 32")
check(all((by_name[f"entity/cow/cow_{n}.png"]["width"], by_name[f"entity/cow/cow_{n}.png"]["height"]) == (64, 64)
          for n in ("temperate", "cold", "warm")), "cow textures are not 64 x 64")
attrs = json.loads((ROOT / "facts/mob_attributes.json").read_text(encoding="utf-8"))
check(attrs["source"]["server_jar_sha1"] == build.SERVER_SHA1, "mob_attributes.json: server jar pin differs")
cow = attrs["mobs"]["cow"]
check(cow["health"] == 10.0 and cow["attributes"]["max_health"] == 10.0, f"cow health is not 10: {cow}")
check(abs(cow["attributes"]["movement_speed"] - 0.2) < 1e-6, f"cow movement_speed is not 0.2: {cow}")
check(cow["attributes"]["attack_damage"] is None, "cow has an attack_damage")
check(sorted(attrs["mobs"]) == sorted(["cow", "pig", "sheep", "chicken", "zombie", "skeleton", "creeper", "spider"]),
      "mob_attributes.json: mobs differ")
models = json.loads((ROOT / "facts/entity_models.json").read_text(encoding="utf-8"))
for layer in ("cow#main", "pig#main", "sheep#main", "sheep#wool", "sheep#wool_undercoat", "chicken#main",
              "zombie#main", "skeleton#main", "creeper#main", "spider#main"):
    check(layer in models, f"entity_models.json: no {layer}")
cow_model = models.get("cow#main", {"parts": [], "cubes": []})
check((len(cow_model["parts"]), len(cow_model["cubes"])) == (7, 10),
      f"cow model is not 7 parts / 10 cubes: {len(cow_model['parts'])} / {len(cow_model['cubes'])}")
# Survival T2 19c: the arrow's layer comes last (EntityModelFacts' APPEND): the root and its three parts (back, cross_1,
# cross_2), one cube each; its texture
# is 32 x 32.
arrow_model = models.get("arrow#main", {"parts": [], "cubes": []})
check(list(models)[-5] == "arrow#main" and len(arrow_model["cubes"]) == 3 and
      [p["path"] for p in arrow_model["parts"]] == ["", "back", "cross_1", "cross_2"],
      f"arrow#main is not the 19c layer with 3 cubes: {list(models)[-5:-4]}, {len(arrow_model['cubes'])}")
# Survival T6 20e1: the cold and warm farm animal layers follow the arrow (APPEND), with their part / cube counts.
variants = {"cold_cow#main": (9, 11), "warm_cow#main": (7, 12), "cold_pig#main": (7, 8), "cold_chicken#main": (9, 10)}
check(list(models)[-4:] == list(variants), f"the variant layers are not last: {list(models)[-4:]}")
for layer, counts in variants.items():
    model = models.get(layer, {"parts": [], "cubes": []})
    check((len(model["parts"]), len(model["cubes"])) == counts,
          f"{layer} is not {counts[0]} parts / {counts[1]} cubes: {len(model['parts'])} / {len(model['cubes'])}")
arrow_tex = by_name.get("entity/projectiles/arrow.png", {})
check((arrow_tex.get("width"), arrow_tex.get("height")) == (32, 32), f"arrow.png is not 32 x 32: {arrow_tex}")
for name in build.LOOT:
    try:
        json.loads((ROOT / build.LOOT_PREFIX / name).read_text(encoding="utf-8"))
    except ValueError as error:
        errors.append(f"{name}: not JSON ({error})")
check(sum(1 for n in build.LOOT if n.startswith("entities/sheep/")) == 16, "not 16 sheep colour loot tables")
check(sum(1 for n in build.LOOT if n.startswith("shearing/sheep/")) == 16, "not 16 sheep shearing loot tables")
for name in build.RECIPES:
    recipe = json.loads((ROOT / build.RECIPE_PREFIX / name).read_text(encoding="utf-8"))
    meat = name.removeprefix("cooked_").removesuffix(".json")
    check(recipe.get("type") == "minecraft:smelting" and recipe.get("ingredient") == f"minecraft:{meat}"
          and recipe.get("result", {}).get("id") == f"minecraft:cooked_{meat}", f"{name}: not {meat}'s smelting")

if errors:
    print("REFERENCE FAIL")
    print("\n".join(errors[:40]))
    sys.exit(1)
print(f"REFERENCE OK: {len(sums)} files hashed, textures.json re-derived; {len(data['textures'])} textures; client jar "
      f"{'compared (%d)' % jar_checked if jar_checked else 'not compared'}")
