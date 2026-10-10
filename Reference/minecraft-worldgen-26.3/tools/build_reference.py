"""Build Reference/minecraft-worldgen-26.3 from the 26.3 server jar: the overworld terrain's worldgen JSON, byte for byte.

usage: uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/build_reference.py
       [server jar] [--probes <staging folder>]
Copies the noise settings of the three overworld world types, the density functions they use, every top-level noise,
the overworld's surface rules (material_rule/overworld, bedrock_floor and overworld/, with every material_condition),
the overworld biome parameter list's file, the biome tag is_overworld and the biomes it names into data/, replacing
it. Decoration comes in by reachability from those biomes' features and carvers lists: the placed features, features,
block state providers and carvers they reach, and the block tags those name, the tags the game's code reads by name
(CODE_TAGS) and every tag those include. Then it writes manifest.json and SHA256SUMS.txt over the whole folder.
facts/ comes from tools/WorldgenFacts.java and is only hashed here, so run this again after refreshing the facts.
--probes copies the feature probe facts that tools/feature_probe.py (<ore>.json) and tools/tree_probe.py
(tree_<probe>.json) staged into facts/feature_probes/; a file that is already there must come out byte-identical.
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
# The jar's own list of overworld biomes decides which biome files are copied.
BIOME_TAG = "data/minecraft/tags/worldgen/biome/is_overworld.json"
BLOCK_TAGS = "data/minecraft/tags/block/"
# Block tags the game's code reads by name rather than through the data, each with what reads it.
CODE_TAGS = {
    "replaceable": "plants and patches place only into air or these blocks",
    "replaceable_by_trees": "trees, huge mushrooms and their decorators overwrite only these blocks",
    "features_cannot_replace": "blocks that features never overwrite",
    "logs": "a leaves block's distance counts from these",
    "blocks_motion_in_heightmap": "the blocks the MOTION_BLOCKING heightmaps count",
    "blocks_motion_in_heightmap_no_leaves": "the blocks MOTION_BLOCKING_NO_LEAVES counts",
    "supports_vegetation": "grass, ferns, flowers, saplings and bushes survive on these",
    "supports_sugar_cane": "sugar cane survives on these",
    "supports_sugar_cane_adjacently": "sugar cane needs water or one of these beside its support",
    "supports_cactus": "cactus survives on these",
    "supports_lily_pad": "lily pads survive on these",
    "supports_bamboo": "bamboo survives on these",
    "supports_azalea": "azaleas survive on these",
    "supports_big_dripleaf": "big dripleaf survives on these",
    "supports_small_dripleaf": "small dripleaf survives on these",
    "supports_cocoa": "cocoa survives on these",
    "supports_dry_vegetation": "dead bushes and dry grass survive on these",
    "supports_mangrove_propagule": "mangrove propagules survive on these",
    "supports_hanging_mangrove_propagule": "hanging mangrove propagules survive under these",
    "cannot_support_kelp": "kelp does not survive on these",
    "cannot_support_seagrass": "seagrass does not survive on these",
    "cannot_support_snow_layer": "snow layers do not survive on these (the freeze pass)",
    "support_override_snow_layer": "snow layers survive on these whatever their shape (the freeze pass)",
    "support_override_cactus_flower": "cactus flowers survive on these",
}
# Keys whose string values name placed features or providers; a string anywhere else is a block, a fluid or an id
# outside the reference (loot tables, structures). "feature" names a feature inside a placed feature
# ({"feature", "placement"}) and a placed feature everywhere else; "tag" and strings starting with "#" name block tags;
# a weighted entry's "data" names what its list holds.
PLACED_KEYS = {"features", "default", "feature", "feature_true", "feature_false", "vegetation_feature", "cap_feature"}
PROVIDER_KEYS = {"provider", "block_provider", "below_trunk_provider", "foliage_provider", "trunk_provider",
                 "cap_provider", "stem_provider", "root_provider", "hanging_root_state_provider", "root_state_provider",
                 "muddy_roots_provider", "above_root_provider", "state_provider", "block_state_provider", "to_place",
                 "fallback", "then", "source", "filling_provider", "inner_layer_provider",
                 "alternate_inner_layer_provider", "middle_layer_provider", "outer_layer_provider"}
TYPED_PROVIDER_KEYS = {("minecraft:rotated", "state"), ("minecraft:projected_random_patchy_square", "block")}
TYPE_KEYS = ("type", "predicate_type", "processor_type")


def references(value, expect, out, owner=None):
    """Appends (folder, id) for every reference in value; expect is the folder a bare string here names."""
    if isinstance(value, str):
        if value.startswith("#"):
            out.append(("tag", value[1:]))
        elif expect:
            out.append((expect, value))
    elif isinstance(value, list):
        for item in value:
            references(item, expect, out, owner)
    elif isinstance(value, dict):
        owner = value.get("type", owner)
        placed = set(value) == {"feature", "placement"}
        for key, item in value.items():
            if key in TYPE_KEYS:
                continue
            if key == "tag":
                out.append(("tag", item))
            elif placed and key == "feature":
                references(item, "feature", out, owner)
            elif key in PLACED_KEYS:
                references(item, "placed_feature", out, owner)
            elif key in PROVIDER_KEYS or (owner, key) in TYPED_PROVIDER_KEYS:
                references(item, "block_state_provider", out, owner)
            else:
                references(item, expect if key == "data" else None, out, owner)


def reachable(jar, biomes):
    """The decoration files and block tags the biomes reach, as jar entry names."""
    found, pending, entries = set(), [], set(jar.namelist())
    for biome in biomes:
        data = json.loads(jar.read(f"{PREFIX}biome/{biome}.json"))
        pending += [("placed_feature", f) for step in data["features"] for f in step]
        pending += [("carver", c) for c in data["carvers"]]
    pending += [("tag", "minecraft:" + tag) for tag in CODE_TAGS]
    while pending:
        folder, id_ = pending.pop()
        if not id_.startswith("minecraft:"):
            sys.exit(f"BUILD FAIL: {folder} reference outside minecraft: {id_}")
        name = (BLOCK_TAGS if folder == "tag" else f"{PREFIX}{folder}/") + id_[10:] + ".json"
        if name in found:
            continue
        if ".." in name or "\\" in name or name not in entries:
            sys.exit(f"BUILD FAIL: unresolved {folder} reference {id_}")
        found.add(name)
        data = json.loads(jar.read(name))
        if folder == "tag":
            for value in data["values"]:
                value = value if isinstance(value, str) else value["id"]
                if value.startswith("#"):
                    pending.append(("tag", value[1:]))
        else:
            references(data, None, pending)
    return found


def wanted(name, biomes):
    if name == BIOME_TAG:
        return True
    if not name.startswith(PREFIX) or not name.endswith(".json") or ".." in name or "\\" in name:
        return False
    rel = name[len(PREFIX):]
    folder, _, rest = rel.partition("/")
    if folder == "noise_settings":
        return rest[:-5] in PRESETS
    if folder == "density_function":
        return "/" not in rest or rest.startswith(FUNCTION_FOLDERS)
    if folder in ("noise", "material_condition"):
        return "/" not in rest
    if folder == "material_rule":
        return rest in ("overworld.json", "bedrock_floor.json") or rest.startswith("overworld/")
    if folder == "multi_noise_biome_source_parameter_list":
        return rest == "overworld.json"
    if folder == "biome":
        return rest[:-5] in biomes
    return False


def folder_of(name):
    if name == BIOME_TAG:
        return "biome_tag"
    return "block_tag" if name.startswith(BLOCK_TAGS) else name[len(PREFIX):].split("/", 1)[0]


args = sys.argv[1:]
probes = None
if "--probes" in args:
    at = args.index("--probes")
    if at + 1 >= len(args):
        sys.exit("BUILD FAIL: --probes needs the staging folder")
    probes = Path(args[at + 1])
    del args[at:at + 2]
jar_path = Path(args[0]) if args else DEFAULT_JAR
jar_bytes = jar_path.read_bytes()
jar = zipfile.ZipFile(jar_path)
tag_values = json.loads(jar.read(BIOME_TAG))["values"]
if any(not v.startswith("minecraft:") for v in tag_values):
    sys.exit(f"BUILD FAIL: {BIOME_TAG} holds entries other than minecraft biomes (nested tags?): {tag_values}")
overworld_biomes = {v.removeprefix("minecraft:") for v in tag_values}
names = sorted({n for n in jar.namelist() if wanted(n, overworld_biomes)} | reachable(jar, sorted(overworld_biomes)))
counts = Counter(folder_of(name) for name in names)
EXPECTED = {"noise_settings": 3, "density_function": 51, "noise": 62, "material_rule": 37, "material_condition": 8,
            "multi_noise_biome_source_parameter_list": 1, "biome": 56, "biome_tag": 1, "placed_feature": 210,
            "feature": 182, "block_state_provider": 8, "carver": 3, "block_tag": 127}
if dict(counts) != EXPECTED:
    sys.exit(f"BUILD FAIL: {jar_path} gives {dict(counts)}, expected {EXPECTED}; data/ is left as it was")

# Staged probe facts are checked before anything is replaced: a file already in facts/feature_probes must match.
staged = sorted(probes.glob("*.json")) if probes else []
if probes and not staged:
    sys.exit(f"BUILD FAIL: no probe facts in {probes}")
for path in staged:
    try:
        made_by = json.loads(path.read_text(encoding="utf-8")).get("generated_by")
    except (ValueError, AttributeError):
        made_by = None
    # Ore probes are named after their feature; tree probes are tree_<probe>.json, so they sort after the ores.
    if made_by not in ("tools/feature_probe.py", "tools/tree_probe.py") or \
            path.name.startswith("tree_") != (made_by == "tools/tree_probe.py"):
        sys.exit(f"BUILD FAIL: {path} is neither an <ore>.json from tools/feature_probe.py nor a tree_<probe>.json "
                 f"from tools/tree_probe.py")
    target = ROOT / "facts/feature_probes" / path.name
    if target.exists() and target.read_bytes() != path.read_bytes():
        sys.exit(f"BUILD FAIL: {target.relative_to(ROOT).as_posix()} exists and differs from {path}")

data_root = ROOT / "data"
if data_root.exists():
    shutil.rmtree(data_root)
for name in names:
    target = ROOT / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(jar.read(name))

if staged:
    (ROOT / "facts/feature_probes").mkdir(parents=True, exist_ok=True)
    for path in staged:
        (ROOT / "facts/feature_probes" / path.name).write_bytes(path.read_bytes())

manifest = {
    "version": "26.3",
    "source": {
        "file": "server-26.3.jar, from the official 26.3 server download",
        "sha256": hashlib.sha256(jar_bytes).hexdigest(),
        "sha1": hashlib.sha1(jar_bytes).hexdigest(),
        "size": len(jar_bytes),
    },
    "presets": list(PRESETS),
    "counts": {folder: counts[folder] for folder in EXPECTED},
}
(ROOT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")

files = sorted(p for p in ROOT.rglob("*")
               if p.is_file() and p.name != "SHA256SUMS.txt" and "__pycache__" not in p.parts)
lines = [f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(ROOT).as_posix()}" for p in files]
(ROOT / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
print(f"BUILT: {len(names)} data files {dict(manifest['counts'])}, {len(lines)} files hashed"
      + (f", {len(staged)} probe facts from {probes}" if probes else ""))
