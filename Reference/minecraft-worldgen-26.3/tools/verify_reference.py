"""Verify Reference/minecraft-worldgen-26.3: file hashes, jar bytes, references between files, and the facts.

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
BLOCK_TAGS = ROOT / "data/minecraft/tags/block"
# The blocks reference's list of every block in the 26.3 registry, imported or not.
BLOCK_FACTS = ROOT.parent / "minecraft-blocks-26.3/facts/block_facts.csv"
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
COUNTS = {"noise_settings": 3, "density_function": 51, "noise": 62, "material_rule": 37, "material_condition": 8,
          "multi_noise_biome_source_parameter_list": 1, "biome": 56, "biome_tag": 1, "placed_feature": 210,
          "feature": 182, "block_state_provider": 8, "carver": 3, "block_tag": 127}
check(manifest["counts"] == COUNTS, f"counts {manifest['counts']}")
check(len(data_files) == sum(COUNTS.values()), f"expected {sum(COUNTS.values())} data files, found {len(data_files)}")

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


# Surface rules: in a rule, strings under "sequence" and "then_run" name rules, under "if_true" and "invert"
# conditions, under "noise" noises, under "biome_is" biomes, and the ore vein's "density", "richness" and
# "filler_gap" density functions. Block states ("result_state", the ore vein's blocks) are not files.
RULE_KEYS = {"sequence": "material_rule", "then_run": "material_rule", "if_true": "material_condition",
             "invert": "material_condition", "noise": "noise", "biome_is": "biome", "density": "density_function",
             "richness": "density_function", "filler_gap": "density_function"}


def rule_references(value, out, key=None):
    if isinstance(value, dict):
        for k, v in value.items():
            rule_references(v, out, k)
    elif isinstance(value, list):
        for v in value:
            rule_references(v, out, key)
    elif isinstance(value, str) and key in RULE_KEYS:
        out.append((RULE_KEYS[key], value))


def worldgen_file(folder, name):
    return WORLDGEN / folder / (name.removeprefix("minecraft:") + ".json")


rule_reached, pending, rule_refs = set(), [], 0
for preset in manifest["presets"]:
    settings = json.loads((WORLDGEN / "noise_settings" / f"{preset}.json").read_text(encoding="utf-8"))
    pending.append(("material_rule", settings["material_rule"]))
while pending:
    folder, name = pending.pop()
    path = worldgen_file(folder, name)
    rule_refs += 1
    check(path.is_file(), f"unresolved {folder} reference: {name}")
    if folder not in ("material_rule", "material_condition") or not path.is_file() or path in rule_reached:
        continue
    rule_reached.add(path)
    found = []
    rule_references(json.loads(path.read_text(encoding="utf-8")), found)
    pending += found
# Every rule file is reached; the 8 named conditions are copied whole, and the overworld tree leaves under_ceiling unused.
for path in (WORLDGEN / "material_rule").rglob("*.json"):
    check(path in rule_reached, f"not reached from the presets' surface rules: {path.relative_to(ROOT).as_posix()}")

# Biomes: the tag names exactly the biome files, the parameter list file names the dumped preset, and the facts'
# points and samples are well formed and name only those biomes.
tag = json.loads((ROOT / "data/minecraft/tags/worldgen/biome/is_overworld.json").read_text(encoding="utf-8"))
biome_files = {p.stem for p in (WORLDGEN / "biome").glob("*.json")}
check({v.removeprefix("minecraft:") for v in tag["values"]} == biome_files, "is_overworld differs from the biome files")
parameter_list = json.loads((WORLDGEN / "multi_noise_biome_source_parameter_list/overworld.json").read_text("utf-8"))
parameters = json.loads((ROOT / "facts/biome_parameters.json").read_text(encoding="utf-8"))
check(parameter_list == {"preset": parameters["preset"]}, f"parameter list file {parameter_list}")
check({b.removeprefix("minecraft:") for b in parameters["biomes"]} == biome_files, "parameter list biomes differ")
for point in parameters["points"]:
    check(len(point) == 14 and 0 <= point[0] < len(parameters["biomes"]) and
          all(point[i] <= point[i + 1] for i in range(1, 13, 2)), f"bad parameter point {point}")
lookups = json.loads((ROOT / "facts/biome_lookup_samples.json").read_text(encoding="utf-8"))
check(len(lookups["samples"]) == 5120, f"lookup samples {len(lookups['samples'])}")
for sample in lookups["samples"]:
    check(len(sample) == 8 and all(0 <= b < len(parameters["biomes"]) for b in sample[6:]), f"bad sample {sample}")

# Decoration: from the biomes' features and carvers lists, every placed feature, feature, provider, carver and block tag
# reference resolves, and every file in those folders is reached. Keys as in build_reference.py: "feature" names a
# feature inside a placed feature ({"feature", "placement"}) and a placed feature elsewhere, provider keys name block
# state providers, "tag" and "#" strings name block tags, and a weighted entry's "data" names what its list holds. Every
# other "minecraft:" string is a block, except fluids and the ids outside the reference (loot tables, structures,
# processor lists), and must be in the blocks reference's registry list, as must every block a tag lists.
PLACED_KEYS = {"features", "default", "feature", "feature_true", "feature_false", "vegetation_feature", "cap_feature"}
PROVIDER_KEYS = {"provider", "block_provider", "below_trunk_provider", "foliage_provider", "trunk_provider",
                 "cap_provider", "stem_provider", "root_provider", "hanging_root_state_provider", "root_state_provider",
                 "muddy_roots_provider", "above_root_provider", "state_provider", "block_state_provider", "to_place",
                 "fallback", "then", "source", "filling_provider", "inner_layer_provider",
                 "alternate_inner_layer_provider", "middle_layer_provider", "outer_layer_provider"}
TYPED_PROVIDER_KEYS = {("minecraft:rotated", "state"), ("minecraft:projected_random_patchy_square", "block")}
OUTSIDE_KEYS = {"fluids", "loot_table", "fossil_structures", "overlay_structures", "fossil_processors",
                "overlay_processors"}
TYPE_KEYS = ("type", "predicate_type", "processor_type")


def decoration_references(value, expect, out, owner=None, key=None):
    if isinstance(value, str):
        if value.startswith("#"):
            out.append(("tag", value[1:]))
        elif expect:
            out.append((expect, value))
        elif value.startswith("minecraft:") and key not in OUTSIDE_KEYS and owner != "minecraft:template":
            out.append(("block", value))
    elif isinstance(value, list):
        for item in value:
            decoration_references(item, expect, out, owner, key)
    elif isinstance(value, dict):
        owner = value.get("type", owner)
        placed = set(value) == {"feature", "placement"}
        for k, item in value.items():
            if k in TYPE_KEYS:
                continue
            if k == "tag":
                out.append(("tag", item))
            elif placed and k == "feature":
                decoration_references(item, "feature", out, owner, k)
            elif k in PLACED_KEYS:
                decoration_references(item, "placed_feature", out, owner, k)
            elif k in PROVIDER_KEYS or (owner, k) in TYPED_PROVIDER_KEYS:
                decoration_references(item, "block_state_provider", out, owner, k)
            else:
                decoration_references(item, expect if k == "data" else None, out, owner, k)


registry_blocks = set()
if BLOCK_FACTS.is_file():
    registry_blocks = {line.split(",")[1] for line in BLOCK_FACTS.read_text(encoding="utf-8").splitlines()[1:]}
check(len(registry_blocks) == 1286, f"{BLOCK_FACTS} lists {len(registry_blocks)} blocks, expected 1286")
decoration_pending, decoration_reached, decoration_refs, block_refs = [], set(), 0, set()
for path in (WORLDGEN / "biome").glob("*.json"):
    biome = json.loads(path.read_text(encoding="utf-8"))
    check(len(biome["features"]) == 11, f"{path.stem}: {len(biome['features'])} feature steps")
    decoration_pending += [("placed_feature", f) for step in biome["features"] for f in step]
    decoration_pending += [("carver", c) for c in biome["carvers"]]
# build_reference.py's CODE_TAGS: tags the game's code reads by name.
code_tags = ("replaceable", "replaceable_by_trees", "features_cannot_replace", "logs", "blocks_motion_in_heightmap",
             "blocks_motion_in_heightmap_no_leaves", "supports_vegetation", "supports_sugar_cane",
             "supports_sugar_cane_adjacently", "supports_cactus", "supports_lily_pad", "supports_bamboo",
             "supports_azalea", "supports_big_dripleaf", "supports_small_dripleaf", "supports_cocoa",
             "supports_dry_vegetation", "supports_mangrove_propagule", "supports_hanging_mangrove_propagule",
             "cannot_support_kelp", "cannot_support_seagrass", "cannot_support_snow_layer",
             "support_override_snow_layer", "support_override_cactus_flower")
decoration_pending += [("tag", "minecraft:" + tag) for tag in code_tags]
while decoration_pending:
    folder, name = decoration_pending.pop()
    if folder == "block":
        block_refs.add(name)
        continue
    decoration_refs += 1
    path = (BLOCK_TAGS / (name.removeprefix("minecraft:") + ".json") if folder == "tag"
            else worldgen_file(folder, name))
    if not name.startswith("minecraft:") or not path.is_file():
        check(False, f"unresolved {folder} reference: {name}")
        continue
    if path in decoration_reached:
        continue
    decoration_reached.add(path)
    data = json.loads(path.read_text(encoding="utf-8"))
    if folder == "tag":
        for value in data["values"]:
            value = value if isinstance(value, str) else value["id"]
            decoration_pending.append(("tag", value[1:]) if value.startswith("#") else ("block", value))
    else:
        decoration_references(data, None, decoration_pending)
for folder in ("placed_feature", "feature", "block_state_provider", "carver"):
    for path in (WORLDGEN / folder).rglob("*.json"):
        check(path in decoration_reached, f"not reached from the biomes: {path.relative_to(ROOT).as_posix()}")
for path in BLOCK_TAGS.rglob("*.json"):
    check(path in decoration_reached, f"not reached from the biomes or the code tags: {path.relative_to(ROOT).as_posix()}")
for block in sorted(block_refs):
    check(block.removeprefix("minecraft:") in registry_blocks, f"not a 26.3 block: {block}")

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
      f"{len(facts['splines'])} splines with {sample_count} samples, {len(rule_reached)} rule and condition files "
      f"with {rule_refs} references, {len(parameters['points'])} biome points over {len(parameters['biomes'])} biomes, "
      f"{len(lookups['samples'])} lookup samples, {len(decoration_reached)} decoration files and block tags with "
      f"{decoration_refs} references and {len(block_refs)} blocks")
