"""Build Reference/minecraft-blocks-26.3 from the pinned 26.3 jars and the research outputs.

usage (from the repository root):
    uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-blocks-26.3/tools/build_reference.py
        [--research C:/dev/sandbox/mc-26.3-blocks] [--facts C:/dev/sandbox/mc-26.3-creative/facts]

Inputs: the client and server jars and the 26.3 asset index (SHA-1 pinned below), the research work folder (manifest, CSVs, notes,
textures, Mojang's blocks.json report) and the facts dumped by tools/FactsDump.java (creative tabs, hardness).
Every copied asset is compared byte for byte with its jar entry. The folder must not exist yet (only tools/),
so an accepted reference is never overwritten; to rebuild, move the old folder away first.

Generated files (see README.md for the formats):
    block_manifest.json  the research manifest merged with hardness, tints, names, items and creative positions
    block_states.json    every state of every imported block: 26.3 state id, properties, shape and six faces
    textures.json        every texture: size, animation, mipmap strategy, alpha class and its array layers
    facts/               FactsDump.java's output: hardness, creative tabs, per-state facts, the torch support table,
                         plant_survival.json (which neighbours let each state survive) and block_sounds.csv (each block's sound type)
    block_particles.json each state's particle texture, from the jar's block models
    items.json           block items: name, stack size, GUI tint, creative tab positions
    SHA256SUMS.txt       every file in the folder except itself
"""
import argparse
import csv
import hashlib
import importlib.util
import io
import json
import os
import shutil
import sys
import zipfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
CLIENT_SHA1 = "e877b6a07acd633fb3bb475002175cec036e7b87"
SERVER_SHA1 = "33680f5f2ac32864d6d7cf5e56a705fdb3e05f4c"
CLIENT_JAR = Path(os.environ.get("APPDATA", "")) / "ModrinthApp/meta/versions/26.3/26.3.jar"
ASSETS = "assets/minecraft/"
# Asset index 34 (the 26.3 client's assetIndex): sounds.json and the .ogg files of the block break and hit sounds.
# Modrinth's local copy differs from Mojang's published index (SHA-1 below) in its minecraft/lang/ entries only, so
# the pin is on the rest of the entries: SHA-1 of their canonical JSON (sorted keys, compact separators).
ASSET_INDEX_SHA1 = "abfaa525f923f807df8b4e4d29c1b5e3a104adbe"
ASSET_INDEX_NONLANG_SHA1 = "9ba09e4fc9e2245b7395eee4732096038d9f6e2d"
PARTICLE_ONLY_TEXTURES = frozenset({"shelf_mushroom_particle"})  # particle textures outside textures.json, copied as plain files
ASSET_DIR = Path(os.environ.get("APPDATA", "")) / "ModrinthApp/meta/assets"

# 26.3 BlockColors.createDefault (javap of net.minecraft.client.color.block.BlockColors), limited to imported
# blocks. A model tintindex without a registered source renders untinted (cherry, pale oak, azalea, poplar leaves).
GRASS_PLAINS, FOLIAGE_PLAINS, WATER_PLAINS = (0.8, 0.4), (0.8, 0.4), "#3F76E4"
TINT_SOURCES = {
    "grass_block": ("grass", None),
    "oak_leaves": ("foliage", None), "jungle_leaves": ("foliage", None), "acacia_leaves": ("foliage", None),
    "dark_oak_leaves": ("foliage", None), "mangrove_leaves": ("foliage", None),
    "spruce_leaves": ("constant", -10380959), "birch_leaves": ("constant", -8345771),
    "water": ("water", None),
    # The model blocks (phase 6), from minecraft.wiki "Tint" (grass, foliage and dry foliage colormaps; the lily pad
    # among the fixed colors) and "Lily Pad" (placed: #208030). Pink petals and wildflowers tint their stems only
    # (tintindex 1); a tintindex outside a block's indices renders untinted, like bamboo's.
    "short_grass": ("grass", None), "fern": ("grass", None), "tall_grass": ("grass", None),
    "large_fern": ("grass", None), "bush": ("grass", None), "sugar_cane": ("grass", None),
    "pink_petals": ("grass", None), "wildflowers": ("grass", None), "vine": ("foliage", None),
    "leaf_litter": ("dry_foliage", None), "lily_pad": ("constant", 0x208030),
}
TINT_INDICES = {"pink_petals": frozenset({1}), "wildflowers": frozenset({1})}

# FaceInfo vertex extents per direction (javap of net.minecraft.client.renderer.FaceInfo). (axis, max?) triples.
FACE_INFO = {
    "down": [("x0", "y0", "z1"), ("x0", "y0", "z0"), ("x1", "y0", "z0"), ("x1", "y0", "z1")],
    "up": [("x0", "y1", "z0"), ("x0", "y1", "z1"), ("x1", "y1", "z1"), ("x1", "y1", "z0")],
    "north": [("x1", "y1", "z0"), ("x1", "y0", "z0"), ("x0", "y0", "z0"), ("x0", "y1", "z0")],
    "south": [("x0", "y1", "z1"), ("x0", "y0", "z1"), ("x1", "y0", "z1"), ("x1", "y1", "z1")],
    "west": [("x0", "y1", "z0"), ("x0", "y0", "z0"), ("x0", "y0", "z1"), ("x0", "y1", "z1")],
    "east": [("x1", "y1", "z1"), ("x1", "y0", "z1"), ("x1", "y0", "z0"), ("x1", "y1", "z0")],
}
NORMALS = {"down": (0, -1, 0), "up": (0, 1, 0), "north": (0, 0, -1), "south": (0, 0, 1), "west": (-1, 0, 0),
           "east": (1, 0, 0)}
# Output face order = the project's EFoundationFace (West, East, North, South, Down, Up).
FACE_ORDER = ["west", "east", "north", "south", "down", "up"]
# 32 x 32 frame textures go to the flow array (still called water_flow), in name order: lava's, then water's.
FLOW_TEXTURES = {"water_flow", "lava_flow"}
LARGE_STILL = {"shelf_mushroom_stage0", "shelf_mushroom_stage1"}
# Drawn as a full cube from a model of six thin face plates (powder snow): each plate's outward face only. The
# inward faces show from inside the block, which the importer does not draw.
PLATE_CUBES = {"powder_snow"}
# Layered blocks (snow): layers 1 to 7 are a box k/8 high, shape "layers_<k>"; 8 uses the full cube's model. 26.3's
# layer models add an inverted inner box (from > to) whose face shows from inside; it is skipped like the plates'.
LAYERED = {"snow"}
# A block whose vanilla item has another id (powder snow's is powder_snow_bucket). Item id = block id here, so it gets
# no block item until buckets exist. Model blocks with another item id (cocoa: cocoa_beans, wall fans: their fans) get
# none either and record their vanilla item, for the inventory pass that adds those items.
OTHER_ITEM_BLOCKS = {"powder_snow"}
# Item models drawn as flat sprites from their layers; any other item model is a block model drawn in 3D.
SPRITE_PARENTS = {"item/generated", "item/handheld"}
# Blocks added after the first ordering was fixed, in the order they came (torch session T2). Their new textures and
# baked models go after every earlier one, so no existing texture index, array layer or model index moves; blocks,
# states, items and facts keep registry order. verify_reference.py asserts it. Later blocks join the end of the list.
APPENDED_BLOCKS = ["torch", "wall_torch"]


def default_uv(f, t, d):
    """FaceBakery.defaultFaceUV (javap): [minU, minV, maxU, maxV] in 0..16."""
    return {"down": [f[0], 16 - t[2], t[0], 16 - f[2]], "up": [f[0], f[2], t[0], t[2]],
            "north": [16 - t[0], 16 - t[1], 16 - f[0], 16 - f[1]], "south": [f[0], 16 - t[1], t[0], 16 - f[1]],
            "west": [f[2], 16 - t[1], t[2], 16 - f[1]], "east": [16 - t[2], 16 - t[1], 16 - f[2], 16 - f[1]]}[d]


def vertex_uv(uvs, shift, i):
    """CuboidFace.getU/getV with Quadrant.rotateVertexIndex: index (i + shift) % 4 picks min/max U and V."""
    k = (i + shift) % 4
    return (uvs[0] if k in (0, 1) else uvs[2]) / 16, (uvs[1] if k in (0, 3) else uvs[3]) / 16


def canonical_uv(d, p):
    """UV an unrotated full cube's face d shows at block-relative point p (x east, y up, z south; 0..1).
    Derived from defaultFaceUV + FaceInfo above; the Unreal meshes carry exactly these UVs."""
    x, y, z = p
    return {"down": (x, 1 - z), "up": (x, z), "north": (1 - x, 1 - y), "south": (x, 1 - y), "west": (z, 1 - y),
            "east": (1 - z, 1 - y)}[d]


def transform_uv(code, u, v):
    """The 8 UV transforms a face code may carry: bit 2 mirrors U first, bits 0-1 turn (u, v) -> (v, 1 - u)
    that many times. FoundationBlockFaces::TransformUV and the block materials implement the same."""
    if code & 4:
        u = 1 - u
    for _ in range(code & 3):
        u, v = v, 1 - u
    return u, v


def rotate(p, xr, yr):
    """Blockstate rotation about the block centre: X by -x degrees, then Y by -y degrees (furnace y=90 faces east)."""
    x, y, z = p[0] - .5, p[1] - .5, p[2] - .5
    for _ in range((xr // 90) % 4):  # Rx(-90): y' = z, z' = -y
        y, z = z, -y
    for _ in range((yr // 90) % 4):  # Ry(-90): x' = -z, z' = x
        x, z = -z, x
    return (round(x + .5, 6), round(y + .5, 6), round(z + .5, 6))


def direction_of(n):
    return next(d for d, v in NORMALS.items() if v == tuple(round(c) for c in n))


class Models:
    def __init__(self, jar):
        self.jar, self.cache = jar, {}

    def raw(self, name):
        name = name.removeprefix("minecraft:")
        if name not in self.cache:
            self.cache[name] = json.loads(self.jar.read(f"{ASSETS}models/{name}.json"))
        return self.cache[name]

    def resolve(self, name):
        """(textures, elements, chain) with parent inheritance; texture variables resolved to texture names."""
        chain, textures, elements = [], {}, None
        while name:
            data = self.raw(name)
            chain.append(name.removeprefix("minecraft:"))
            for key, value in data.get("textures", {}).items():
                textures.setdefault(key, value)
            if elements is None and "elements" in data:
                elements = data["elements"]
            name = data.get("parent")

        def texture(ref, depth=0):
            assert depth < 10, ref
            if isinstance(ref, dict):  # 26.x: {"sprite": ..., "force_translucent": true}
                ref = ref["sprite"]
            if ref.startswith("#"):
                return texture(textures[ref[1:]], depth + 1)
            return ref.removeprefix("minecraft:").removeprefix("block/")

        return texture, elements or [], chain


def matches(when, props):
    if "OR" in when:
        return any(matches(w, props) for w in when["OR"])
    if "AND" in when:
        return all(matches(w, props) for w in when["AND"])
    return all(props[k] in str(v).split("|") for k, v in when.items())


def variants_for(blockstate, props):
    """The applied variants for one state (first entry of weighted lists, as the default seed shows)."""
    if "variants" in blockstate:
        for key, value in blockstate["variants"].items():
            cond = dict(part.split("=") for part in key.split(",")) if key else {}
            if all(props[k] == v for k, v in cond.items()):
                return [value[0] if isinstance(value, list) else value], isinstance(value, list) and len(value) > 1
        raise AssertionError(f"no variant for {props}")
    applied = []
    for part in blockstate["multipart"]:
        if "when" not in part or matches(part["when"], props):
            a = part["apply"]
            applied.append(a[0] if isinstance(a, list) else a)
    return applied, False


def bake_state(models, blockstate, props, tinted_block, plate_cube=False, layered=False):
    variants, random = variants_for(blockstate, props)
    faces = {d: [] for d in FACE_ORDER}
    boxes = []
    for variant in variants:
        texture, elements, _ = models.resolve(variant["model"])
        xr, yr, uvlock = variant.get("x", 0), variant.get("y", 0), variant.get("uvlock", False)
        for element in elements:
            assert "rotation" not in element, "element rotation is not supported for cube imports"
            f, t = element["from"], element["to"]
            if (plate_cube or layered) and any(a > b for a, b in zip(f, t)):
                continue
            boxes.append((tuple(f), tuple(t)))
            ext = {"x0": f[0] / 16, "x1": t[0] / 16, "y0": f[1] / 16, "y1": t[1] / 16, "z0": f[2] / 16, "z1": t[2] / 16}
            for face_dir, face in element["faces"].items():
                if plate_cube and face.get("cullface") != face_dir:
                    continue
                uvs = face.get("uv", default_uv(f, t, face_dir))
                shift = (face.get("rotation", 0) // 90) % 4
                world = direction_of(tuple(a - .5 for a in rotate(tuple(.5 + c for c in NORMALS[face_dir]), xr, yr)))
                verts = [rotate(tuple(ext[e] for e in FACE_INFO[face_dir][i]), xr, yr) for i in range(4)]
                actual = [vertex_uv(uvs, shift, i) for i in range(4)]
                canon = [canonical_uv(world, p) for p in verts]
                if uvlock:
                    # Only the mushroom blocks use uvlock: full-face planes whose texture keeps the world
                    # orientation, i.e. the unrotated face's mapping.
                    assert uvs in ([0, 0, 16, 16], default_uv(f, t, face_dir)) and shift == 0, (variant, face_dir)
                    code = 0
                else:
                    code = next((c for c in range(8) if all(
                        abs(a - b) < 1e-6 for uv, cv in zip(actual, canon) for a, b in zip(uv, transform_uv(c, *cv)))),
                        None)
                    assert code is not None, f"no UV transform for {variant} {face_dir}: {actual} vs {canon}"
                tint = face.get("tintindex", -1) if tinted_block else -1
                faces[world].append({"t": texture(face["texture"]), "x": code, "tint": tint,
                                     "cull": "cullface" in face})
    full = ((0, 0, 0), (16, 16, 16))
    heights = {t[1] for f, t in boxes if f == (0, 0, 0) and t[0] == 16 and t[2] == 16}
    if plate_cube:
        boxes = [full]
    elif layered and len(boxes) == 1 and len(heights) == 1 and min(heights) in range(2, 16, 2):
        boxes = [("layers", min(heights) // 2)]
    shape = f"layers_{boxes[0][1]}" if boxes and boxes[0][0] == "layers" else "full" if full in boxes or any(
        b == ((0, 0, 0), (16, 16, 0)) for b in boxes) else (
        "slab_bottom" if ((0, 0, 0), (16, 8, 16)) in boxes else "slab_top" if ((0, 8, 0), (16, 16, 16)) in boxes
        else None)
    assert shape, f"unknown shape {boxes}"
    out = []
    for d in FACE_ORDER:
        layers = faces[d]
        assert 1 <= len(layers) <= 2, f"{d}: {len(layers)} layers"
        entry = dict(layers[0])
        if len(layers) == 2:
            assert layers[1]["x"] == layers[0]["x"], "overlay must share the base face's UV transform"
            entry["overlay"] = {"t": layers[1]["t"], "tint": layers[1]["tint"]}
        out.append(entry)
    return shape, out, random


def alpha_class(img):
    alphas = set(img.getchannel("A").get_flattened_data())
    return "opaque" if alphas == {255} else "cutout" if alphas <= {0, 255} else "translucent"


def colormap(path, temperature, downfall):
    """ColorMapColorUtil.get as GrassColor/FoliageColor call it: x = (1 - t) * 255, y = (1 - d * t) * 255."""
    img = Image.open(path).convert("RGB")
    t, d = min(max(temperature, 0), 1), min(max(downfall, 0), 1) * min(max(temperature, 0), 1)
    return "#%02X%02X%02X" % img.getpixel((int((1 - t) * 255), int((1 - d) * 255)))


def argb_hex(value):
    return "#%06X" % (value & 0xFFFFFF)


def sha1(path):
    return hashlib.sha1(path.read_bytes()).hexdigest()


def index_objects(asset_dir=None):
    """The asset index 34 objects: {name: {hash, size}}, after the pin check (see ASSET_INDEX_NONLANG_SHA1)."""
    objects = json.loads(((asset_dir or ASSET_DIR) / "indexes/34.json").read_text(encoding="utf-8"))["objects"]
    rest = {k: v for k, v in objects.items() if not k.startswith("minecraft/lang/")}
    digest = hashlib.sha1(json.dumps(rest, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    if digest != ASSET_INDEX_NONLANG_SHA1:
        raise SystemExit("asset index 34 differs from the pinned one (non-lang entries)")
    return objects


def asset_object(objects, name, asset_dir=None):
    """Bytes of the asset-index entry `name`, checked against its SHA-1 and size."""
    entry = objects[name]
    data = ((asset_dir or ASSET_DIR) / "objects" / entry["hash"][:2] / entry["hash"]).read_bytes()
    assert hashlib.sha1(data).hexdigest() == entry["hash"] and len(data) == entry["size"], name
    return data


def sound_files(sounds_json, events):
    """Every .ogg name (without extension) the sound events play; `type: event` entries follow their event."""
    names, seen = [], set()

    def visit(event):
        event = event.removeprefix("minecraft:")
        if event in seen:
            return
        seen.add(event)
        for sound in sounds_json[event]["sounds"]:
            if isinstance(sound, str):
                sound = {"name": sound}
            if sound.get("type", "sound") == "event":
                visit(sound["name"])
            else:
                name = sound["name"].removeprefix("minecraft:")
                if name not in names:
                    names.append(name)

    for event in events:
        visit(event)
    return names


def resolve_particles(jar, models, report, manifest):
    """{block: {"default": texture, "states": {state id: texture}}} (states only where they differ) and the
    (block, state id, texture) of every state, for the manifest blocks then the fluids (registry order). The texture is
    the `particle` texture of the model the state uses (the first applied variant or part, first entry of weighted
    lists), through parent chains and `#` variables. Fluids use their still texture."""
    particles, every = {}, []
    for block in manifest["blocks"] + manifest["fluids"]:
        bid = block["id"]
        if bid in {f["id"] for f in manifest["fluids"]}:
            particles[bid] = {"default": block["textures"]["still"]}
            every.append((bid, None, block["textures"]["still"]))
            continue
        rep = report[f"minecraft:{bid}"]
        states = sorted(rep["states"], key=lambda s: s["id"])
        blockstate = json.loads(jar.read(f"{ASSETS}blockstates/{bid}.json"))
        per_state = {}
        for state in states:
            variants, _ = variants_for(blockstate, state.get("properties", {}))
            texture, _, _ = models.resolve(variants[0]["model"])
            per_state[state["id"]] = texture("#particle")
        default = per_state[next(s["id"] for s in states if s.get("default"))]
        particles[bid] = {"default": default}
        differ = {str(i): t for i, t in per_state.items() if t != default}
        if differ:
            particles[bid]["states"] = differ
        every += [(bid, sid, t) for sid, t in per_state.items()]
    return particles, every


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--research", default="C:/dev/sandbox/mc-26.3-blocks")
    parser.add_argument("--facts", default="C:/dev/sandbox/mc-26.3-creative/facts")
    # Rewrites SHA256SUMS.txt only, after a documentation or tool edit; generated data is never rebuilt in place.
    parser.add_argument("--sums-only", action="store_true")
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("bake_models", Path(__file__).with_name("bake_models.py"))
    bake_models = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bake_models)
    if args.sums_only:
        print(f"SHA256SUMS.txt: {len(write_sums())} files")
        return
    research, facts = Path(args.research), Path(args.facts)
    server_jar = research / "downloads/server.jar"
    if [p.name for p in ROOT.iterdir()] != ["tools"]:
        raise SystemExit(f"{ROOT} already holds more than tools/: an accepted reference is never overwritten")
    for jar, pin in ((CLIENT_JAR, CLIENT_SHA1), (server_jar, SERVER_SHA1)):
        if sha1(jar) != pin:
            raise SystemExit(f"SHA-1 mismatch: {jar}")
    jar = zipfile.ZipFile(CLIENT_JAR)
    manifest = json.loads((research / "out/block_manifest.json").read_text(encoding="utf-8"))
    report = json.loads((research / "reports/reports/blocks.json").read_text(encoding="utf-8"))
    block_facts = {r["id"]: r for r in csv.DictReader(open(facts / "block_facts.csv", encoding="utf-8"))}
    tabs = json.loads((facts / "creative_tabs.json").read_text(encoding="utf-8"))
    lang = json.loads(jar.read(ASSETS + "lang/en_us.json"))
    ids = [b["id"] for b in manifest["blocks"]]
    fluid_ids = [f["id"] for f in manifest["fluids"]]
    imported = ids + fluid_ids

    # Everything that can fail on its inputs is loaded and checked before the first write: the asset index (pin),
    # sounds.json with every sound file, block_sounds.csv against the facts, and the particle textures.
    sound_rows = list(csv.DictReader(open(facts / "block_sounds.csv", encoding="utf-8")))
    state_fact_ids = [b["id"] for b in json.loads((facts / "block_state_facts.json").read_text(encoding="utf-8"))["blocks"]]
    if [r["id"] for r in sound_rows] != state_fact_ids or set(state_fact_ids) != set(imported):
        raise SystemExit("block_sounds.csv ids differ from block_state_facts.json (order) or from the imported blocks")
    objects = index_objects()
    sounds_json = asset_object(objects, "minecraft/sounds.json")
    events = sorted({r[k] for r in sound_rows for k in ("break", "hit")} - {"intentionally_empty"})
    sound_data = {name: asset_object(objects, f"minecraft/sounds/{name}.ogg")
                  for name in sound_files(json.loads(sounds_json), events)}
    models = Models(jar)
    particles, every_particle = resolve_particles(jar, models, report, manifest)
    block_pngs = {p.name[:-4] for p in (research / "textures/block").iterdir() if p.name.endswith(".png")}
    outside = {t for _, _, t in every_particle if t not in block_pngs}
    bad = [x for x in every_particle if x[2] in outside and x[2] not in PARTICLE_ONLY_TEXTURES]
    if bad:
        raise SystemExit("particle textures missing from the block textures:\n" + "\n".join(map(str, bad)))

    def copy_jar(entry, dest=None):
        target = ROOT / (dest or entry)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(jar.read(entry))
        return target

    # Research outputs, verbatim.
    for name in ("blocks_registry.csv", "block_tags.csv", "blocks_assets.csv", "edge_cases.txt", "water_research.md",
                 "worldgen_heights.md", "creative_tabs.md", "version_notes.md", "locations.json"):
        (ROOT / "research").mkdir(exist_ok=True)
        shutil.copyfile(research / "out" / name, ROOT / "research" / name)
    shutil.copyfile(research / "out/block_manifest.json", ROOT / "research/block_manifest.research.json")
    (ROOT / "facts").mkdir()
    for name in ("block_facts.csv", "creative_tabs.json", "block_state_facts.json", "block_support.json",
                 "block_sounds.csv", "plant_survival.json"):
        shutil.copyfile(facts / name, ROOT / "facts" / name)
    (ROOT / "reports").mkdir()
    subset = {f"minecraft:{i}": report[f"minecraft:{i}"] for i in imported}
    (ROOT / "reports/blocks-imported.json").write_text(json.dumps(subset, indent=1), encoding="utf-8")

    # Textures: every research copy must equal its jar entry.
    names = sorted(p.name for p in (research / "textures/block").iterdir())
    # APPENDED_BLOCKS' new textures follow every earlier texture, in the order those blocks came.
    by_manifest = {b["id"]: b for b in manifest["blocks"]}
    earlier = {t for b in manifest["blocks"] if b["id"] not in APPENDED_BLOCKS for t in b["all_textures"]}
    earlier |= {t for f in manifest["fluids"] for t in f["textures"].values()}
    appended_textures = list(dict.fromkeys(t for bid in APPENDED_BLOCKS for t in by_manifest[bid]["all_textures"]
                                           if t not in earlier))
    names = [n for n in names if n.split(".")[0] not in appended_textures] + [
        n for t in appended_textures for n in (f"{t}.png", f"{t}.png.mcmeta") if n in names]
    for name in names:
        entry = f"{ASSETS}textures/block/{name}"
        if (research / "textures/block" / name).read_bytes() != jar.read(entry):
            raise SystemExit(f"research texture differs from the jar: {name}")
        copy_jar(entry)
    for name in ("grass.png", "foliage.png", "dry_foliage.png"):
        copy_jar(f"{ASSETS}textures/colormap/{name}")
    # Crack overlay for mining progress. Not in textures.json: no texture index may shift.
    for stage in range(10):
        copy_jar(f"{ASSETS}textures/block/destroy_stage_{stage}.png")
    # Break and hit sounds of the imported blocks (block_sounds.csv), from the asset index; step, place and fall are
    # not copied.
    (ROOT / ASSETS / "sounds").mkdir(parents=True, exist_ok=True)
    (ROOT / ASSETS / "sounds.json").write_bytes(sounds_json)
    for name, data in sound_data.items():
        target = ROOT / f"{ASSETS}sounds/{name}.ogg"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    # GUI art for the creative inventory, item definitions of imported blocks and the tab icons, and the
    # block item display transform.
    gui_prefixes = (f"{ASSETS}textures/gui/container/creative_inventory/",
                    f"{ASSETS}textures/gui/sprites/container/creative_inventory/",
                    f"{ASSETS}textures/gui/sprites/container/slot_highlight_",
                    f"{ASSETS}textures/gui/sprites/container/slot.png")
    tab_icons = sorted({t["icon"].removeprefix("minecraft:") for t in tabs})
    for entry in jar.namelist():
        if entry.startswith(gui_prefixes) and not entry.endswith("/"):
            copy_jar(entry)
    for item in imported + tab_icons:
        if f"{ASSETS}items/{item}.json" in jar.namelist():
            copy_jar(f"{ASSETS}items/{item}.json")
    # Flat item sprites for the tab icons that are not blocks (compass frames, sign, redstone, tools...).
    for entry in jar.namelist():
        stem = entry.rsplit("/", 1)[-1].split(".")[0]
        if entry.startswith(f"{ASSETS}textures/item/") and stem and any(
                stem == icon or stem.startswith(icon + "_") for icon in tab_icons):
            copy_jar(entry)
    for name in ("block/block.json", "item/generated.json", "item/handheld.json"):
        copy_jar(f"{ASSETS}models/{name}")

    # Textures: size, animation, alpha and the array layer plan. The flow textures (32x32 frames) get their own array.
    textures, layer, flow_layer = [], 0, 0
    for name in names:
        if not name.endswith(".png"):
            continue
        stem = name[:-4]
        path = ROOT / f"{ASSETS}textures/block/{name}"
        img = Image.open(path)
        img.load()
        rgba = img.convert("RGBA")
        meta_path = Path(str(path) + ".mcmeta")
        meta = json.loads(meta_path.read_text()) if meta_path.exists() else {}
        anim = meta.get("animation")
        fw = anim.get("width", min(img.size)) if anim is not None else img.width
        fh = anim.get("height", min(img.size)) if anim is not None else img.height
        count = (img.width // fw) * (img.height // fh)
        frames = None
        if anim is not None:
            order = anim.get("frames", list(range(count)))
            frames = [{"index": f if isinstance(f, int) else f["index"],
                       "time": anim.get("frametime", 1) if isinstance(f, int) else f.get("time", anim.get("frametime", 1))}
                      for f in order]
        rgba_frames = [rgba.crop((0, i * fh, fw, (i + 1) * fh)) for i in range(count)]
        water_flow = (fw, fh) == (32, 32)
        # Two still 32 x 32 textures (shelf mushroom stages) stay in the block array; the importer stores their mip 1.
        water_flow = water_flow and stem in FLOW_TEXTURES
        assert water_flow == (stem in FLOW_TEXTURES) and (water_flow or (fw, fh) == (16, 16) or (
            stem in LARGE_STILL and count == 1)), stem
        entry = {"name": stem, "png_sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "size": list(img.size),
                 "frame_size": [fw, fh], "frame_count": count, "array": "water_flow" if water_flow else "blocks",
                 "first_layer": flow_layer if water_flow else layer,
                 "layer_rgba_sha256": [hashlib.sha256(f.tobytes()).hexdigest() for f in rgba_frames],
                 "alpha": alpha_class(rgba),
                 "mipmap_strategy": meta.get("texture", {}).get("mipmap_strategy", "auto")}
        if "alpha_cutoff_bias" in meta.get("texture", {}):
            entry["alpha_cutoff_bias"] = meta["texture"]["alpha_cutoff_bias"]
        if anim is not None:
            entry["animation"] = {"frametime": anim.get("frametime", 1), "interpolate": bool(anim.get("interpolate")),
                                  "frames": frames}
        textures.append(entry)
        if water_flow:
            flow_layer += count
        else:
            layer += count
    texture_index = {t["name"]: t for t in textures}

    # Tints (plains defaults, 26.3 BlockColors) and the block states with their faces.
    colormaps = ROOT / f"{ASSETS}textures/colormap"
    plains = {"grass": colormap(colormaps / "grass.png", *GRASS_PLAINS),
              "foliage": colormap(colormaps / "foliage.png", *FOLIAGE_PLAINS), "water": WATER_PLAINS,
              "dry_foliage": colormap(colormaps / "dry_foliage.png", *FOLIAGE_PLAINS)}

    def tint_of(block):
        source = TINT_SOURCES.get(block)
        if not source:
            return None
        kind, value = source
        return {"source": kind, "color": argb_hex(value) if kind == "constant" else plains[kind],
                "note": "plains biome default" if kind != "constant" else "fixed in BlockColors"}

    baker = bake_models.Baker(sys.modules[__name__], models)
    model_ids = {b["id"] for b in manifest["blocks"] if b["shape"] == "model"}
    states_out, shapes, randoms, by_id = [], {}, [], {}

    def bake(blocks):
        """Each block's states with their faces or baked models; new models take the next free indices."""
        for block in blocks:
            bid = block["id"]
            rep = report[f"minecraft:{bid}"]
            props = list(rep.get("properties", {}).items())
            states = sorted(rep["states"], key=lambda s: s["id"])
            base = states[0]["id"]
            # Mojang enumerates states as a cartesian product, last property fastest, values in declared order.
            for index, state in enumerate(states):
                expect, rest = {}, index
                for name, values in reversed(props):
                    expect[name] = values[rest % len(values)]
                    rest //= len(values)
                assert state["id"] == base + index and expect == state.get("properties", {}), (bid, state)
            entry = {"id": bid, "protocol_id": block["protocol_id"], "min_state_id": base,
                     "default_state_id": next(s["id"] for s in states if s.get("default")),
                     "properties": [[n, v] for n, v in props], "states": []}
            if bid in fluid_ids:
                for state in states:
                    level = int(state["properties"]["level"])
                    amount = 8 if level == 0 or level >= 8 else 8 - level
                    entry["states"].append({"id": state["id"], "properties": state["properties"], "shape": "fluid",
                                            "fluid": {"source": level == 0, "falling": level >= 8, "amount": amount}})
                entry["textures"] = block["textures"]
            elif bid in model_ids:
                blockstate = json.loads(jar.read(f"{ASSETS}blockstates/{bid}.json"))
                tints = TINT_INDICES.get(bid, frozenset({0})) if bid in TINT_SOURCES else frozenset()
                for state in states:
                    parts = baker.bake_state(blockstate, state.get("properties", {}), tints)
                    entry["states"].append({"id": state["id"], "properties": state.get("properties", {}),
                                            "shape": "model", "model": parts})
                    if any(len(part) > 1 for part in parts) and bid not in randoms:
                        randoms.append(bid)
                    for part in parts:
                        for index in part:
                            for quad in baker.baked[index]["quads"]:
                                assert quad["t"] in texture_index, (bid, quad["t"])
                shapes[bid] = ["model"]
            else:
                blockstate = json.loads(jar.read(f"{ASSETS}blockstates/{bid}.json"))
                for state in states:
                    shape, faces, random = bake_state(models, blockstate, state.get("properties", {}), bid in TINT_SOURCES,
                                                      bid in PLATE_CUBES, bid in LAYERED)
                    entry["states"].append({"id": state["id"], "properties": state.get("properties", {}),
                                            "shape": shape, "faces": faces})
                    if random and bid not in randoms:
                        randoms.append(bid)
                    for face in faces:
                        for name in (face["t"], face.get("overlay", {}).get("t")):
                            assert name is None or name in texture_index, (bid, name)
                shapes[bid] = sorted({s["shape"] for s in entry["states"]})
            states_out.append(entry)
            by_id[bid] = entry

    # Material layer per block: translucent if forced or any face texture has partial alpha, cutout if any base
    # face has binary holes (overlays are composited inside the opaque material), else opaque.
    def material_layer(block, entry):
        if block.get("force_translucent"):
            return "translucent"
        if block["shape"] == "model":
            classes = {texture_index[q["t"]]["alpha"] for s in entry["states"] for part in s["model"] for i in part
                       for q in baker.baked[i]["quads"]}
        else:
            classes = {texture_index[f["t"]]["alpha"] for s in entry["states"] for f in s["faces"]}
        return "translucent" if "translucent" in classes else "cutout" if "cutout" in classes else "opaque"

    creative = {}
    for tab in tabs:
        for index, item in enumerate(tab["display"]):
            item_id = (item if isinstance(item, str) else item["id"]).removeprefix("minecraft:")
            creative.setdefault(item_id, []).append({"tab": tab["id"].removeprefix("minecraft:"), "index": index})

    def item_icon(model, tint):
        """A model block's item icon: flat sprite layers (item/generated, item/handheld) or a baked block model with
        its gui display transform. Sprite textures under item/ join the assets."""
        chain, link = [], model
        while link and link not in SPRITE_PARENTS:
            chain.append(link)
            link = models.raw(link).get("parent", "").removeprefix("minecraft:")
        if link:
            layers = []
            for key in sorted(k for k in models.raw(model).get("textures", {}) if k.startswith("layer")):
                name = models.raw(model)["textures"][key].removeprefix("minecraft:")
                if name.startswith("item/"):
                    copy_jar(f"{ASSETS}textures/{name}.png")
                    assert f"{ASSETS}textures/{name}.png.mcmeta" not in jar.namelist(), name
                else:
                    assert name.removeprefix("block/") in texture_index, name
                layers.append({"texture": name, "tint": tint if key == "layer0" else None})
            return {"sprite": layers}
        index, gui = baker.bake_item(model, frozenset({0}) if tint else frozenset())
        return {"model": index, "gui": gui}

    merged_blocks, items = [], []

    def merge(blocks):
        """Each block's manifest entry and item; a model block's 3D item icon takes the next free model index."""
        for block in blocks:
            bid = block["id"]
            fact = block_facts[bid]
            merged = dict(block)
            merged.update({
                "name": lang[f"block.minecraft.{bid}"],
                "hardness": float(fact["destroy_time"]), "blast_resistance": float(fact["explosion_resistance"]),
                "requires_correct_tool_for_drops": fact["requires_correct_tool_for_drops"] == "true",
                "light_emission": int(fact["light_emission"]),
                "facts_source": "26.3 server registry at runtime (facts/block_facts.csv, tools/FactsDump.java)",
                "tint": tint_of(bid), "material_layer": material_layer(block, by_id[bid]),
                "random_variants": bid in randoms,
            })
            if block["shape"] == "model":
                merged["vanilla_item"] = fact["item"].removeprefix("minecraft:") or None
            merged_blocks.append(merged)
            if fact["item"] and fact["item"].removeprefix("minecraft:") != bid:
                assert bid in OTHER_ITEM_BLOCKS or bid in model_ids, (bid, fact["item"])
            elif fact["item"]:
                definition = json.loads(jar.read(f"{ASSETS}items/{bid}.json"))["model"]
                tint = None
                for source in definition.get("tints", []):
                    kind = source["type"].removeprefix("minecraft:")
                    tint = argb_hex(source["value"]) if kind == "constant" else colormap(
                        colormaps / f"{kind}.png", source["temperature"], source["downfall"])
                item = {"id": fact["item"].removeprefix("minecraft:"), "block": bid,
                        "name": fact["item_name"], "max_stack": int(fact["item_max_stack"]),
                        "model": definition.get("model", "").removeprefix("minecraft:"), "gui_tint": tint,
                        "creative": creative.get(bid, [])}
                if bid in model_ids:
                    item["icon"] = item_icon(item["model"], tint)
                items.append(item)

    # Earlier blocks and their item icons first, then APPENDED_BLOCKS, so every new model follows every earlier one;
    # the outputs return to registry order.
    base_blocks = [b for b in manifest["blocks"] if b["id"] not in APPENDED_BLOCKS]
    appended_blocks = [by_manifest[bid] for bid in APPENDED_BLOCKS]
    bake(base_blocks + [dict(f) for f in manifest["fluids"]])
    merge(base_blocks)
    bake(appended_blocks)
    merge(appended_blocks)
    order = {b["id"]: i for i, b in enumerate(manifest["blocks"] + manifest["fluids"])}
    states_out.sort(key=lambda e: order[e["id"]])
    merged_blocks.sort(key=lambda b: order[b["id"]])
    items.sort(key=lambda i: order[i["block"]])
    fluids = []
    for fluid in manifest["fluids"]:
        merged = dict(fluid)
        fact = block_facts[fluid["id"]]
        merged.update({"name": lang[f"block.minecraft.{fluid['id']}"], "hardness": float(fact["destroy_time"]),
                       "blast_resistance": float(fact["explosion_resistance"]), "tint": tint_of(fluid["id"]),
                       "light_emission": int(fact["light_emission"]), "material_layer": fluid["render_layer"],
                       "facts_source": merged_blocks[0]["facts_source"]})
        fluids.append(merged)

    # Particle textures (resolved above, before the first write) must all be in textures.json, fluids' still textures
    # included, or be one of the particle-only files copied as plain files.
    missing = [x for x in every_particle if x[2] not in texture_index and x[2] not in PARTICLE_ONLY_TEXTURES]
    if missing:
        raise SystemExit("particle textures missing from textures.json:\n" + "\n".join(map(str, missing)))
    # Particle-only textures are not in textures.json; they ship as plain files in assets/ (copied byte for byte).
    for t in sorted(outside):
        copy_jar(f"{ASSETS}textures/block/{t}.png")

    def write(name, data):
        (ROOT / name).write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")

    common = {"minecraft_version": "26.3", "client_sha1": CLIENT_SHA1, "server_sha1": SERVER_SHA1}
    write("block_manifest.json", {**common, "source": manifest["source"], "id_policy": manifest["id_policy"],
                                  "block_count_in_registry": manifest["block_count_in_registry"],
                                  "blocks": merged_blocks, "fluids": fluids})
    write("block_states.json", {**common, "face_order": FACE_ORDER,
                                "uv_transform": "bit 2 mirrors U (u = 1 - u), then bits 0-1 turn (u, v) -> (v, 1 - u)",
                                "blocks": states_out})
    write("textures.json", {**common, "arrays": {"blocks": {"frame_size": [16, 16], "layers": layer},
                                                 "water_flow": {"frame_size": [32, 32], "layers": flow_layer}},
                            "plains": plains, "textures": textures})
    write("block_models.json", {**common, "coordinates": "Mojang block space (x east, y up, z south), 0..1 per cell",
                                  "quad": {"v": "4 vertices in FaceInfo order", "uv": "texture UV per vertex, 0..1",
                                           "t": "texture", "tint": "tintindex, -1 untinted",
                                           "cull": "hidden by a full neighbour on this side, or null",
                                           "facing": "the declared face after the blockstate rotation",
                                           "shade": "shade_direction_override, else facing",
                                           "light": "light_emission 0-15"},
                                  "models": baker.baked})
    lines = ",\n".join(f" {json.dumps(b)}: {json.dumps(v, separators=(',', ':'))}" for b, v in particles.items())
    (ROOT / "block_particles.json").write_text(
        json.dumps({**common, "note": "particle texture per imported block (registry order): `default` for the default "
                    "state, `states` only for state ids (as strings) whose texture differs",
                    "textures_outside_textures_json": sorted(outside),
                    "outside_note": "these particle textures are not in textures.json; the files are in "
                    "assets/minecraft/textures/block/", "blocks": "@@"},
                   indent=1).replace('"@@"', "{\n" + lines + "\n }") + "\n", encoding="utf-8")
    write("items.json", {**common, "creative_tabs": [{k: v for k, v in t.items() if k not in ("display", "search")}
                                                     for t in tabs], "items": items})

    for source in ("build_manifest.py", "classify_blocks.py", "build_blocks.py", "build_worldgen.py", "doc_template.md"):
        shutil.copyfile(research / "scripts" / source, ROOT / "tools" / source)
    shutil.copyfile(facts.parent / "tools/FactsDump.java", ROOT / "tools/FactsDump.java")

    files = write_sums()
    print(f"blocks {len(merged_blocks)} + fluids {fluid_ids}; states {sum(len(e['states']) for e in states_out)}; "
          f"textures {len(textures)} -> {layer} block layers + {flow_layer} flow layers; items {len(items)}; "
          f"random-variant blocks {len(randoms)}; baked models {len(baker.baked)}; files {len(files) + 1}")


def write_sums():
    files = sorted(p for p in ROOT.rglob("*") if p.is_file() and p.name != "SHA256SUMS.txt" and "__pycache__" not in p.parts)
    (ROOT / "SHA256SUMS.txt").write_text("".join(
        f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(ROOT).as_posix()}\n" for p in files), encoding="utf-8")
    return files


if __name__ == "__main__":
    sys.exit(main())
