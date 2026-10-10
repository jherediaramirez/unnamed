"""Merge the 26.3 research outputs into an import manifest and a markdown block table.

usage: build_manifest.py <work_dir> <client_jar> <doc_template> <doc_out>
"""
import csv, json, sys, zipfile
from collections import Counter
from pathlib import Path

work, jar_path, template_path, doc_out = (Path(a) for a in sys.argv[1:5])
out = work / "out"
tex_dir = work / "textures" / "block"
jar = zipfile.ZipFile(jar_path)

def read_csv(name):
    with open(out / name, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))

registry = {r["id"]: r for r in read_csv("blocks_registry.csv")}
assets = {r["id"]: r for r in read_csv("blocks_assets.csv")}
tags = {r["id"]: r["tags"].split(";") for r in read_csv("block_tags.csv")}
loc_file = out / "locations.json"
locations = json.loads(loc_file.read_text(encoding="utf-8")) if loc_file.exists() else {}

# Ore heights straight from 26.3 placed_feature data (worldgen_heights.md). Deepslate variants share the feature.
ORE_Y = {
    "coal_ore": "0..192 (peak 96) + 136..top",
    "iron_ore": "-24..56 (peak 16) + 80..top (peak 232) + small -64..72",
    "copper_ore": "-16..112 (peak 48)",
    "gold_ore": "-64..32 (peak -16) + -64..-48; badlands +32..256",
    "diamond_ore": "-64..16 (peak -64) + -64..-4",
    "lapis_ore": "-32..32 (peak 0) + buried -64..64",
    "redstone_ore": "-64..15 + -64..-32 (peak -64)",
    "emerald_ore": "-16..top (peak 232), mountain biomes",
    "nether_gold_ore": "10..246",
    "nether_quartz_ore": "10..246",
    "ancient_debris": "8..24 (peak 16) + 8..248",
}
for k in list(ORE_Y):
    if not k.startswith("nether") and k != "ancient_debris":
        ORE_Y["deepslate_" + k] = ORE_Y[k] + " (where it replaces deepslate, ~Y<8)"

def tex_name(p):
    return p.rsplit("/", 1)[-1].removesuffix(".png")

def is_animated(names):
    for n in names:
        meta = tex_dir / (n + ".png.mcmeta")
        if meta.exists() and "animation" in json.loads(meta.read_text(encoding="utf-8")):
            return True
    return False

# 26.x block models can set force_translucent; collect model names that do.
force_translucent = set()
for name in jar.namelist():
    if name.startswith("assets/minecraft/models/block/") and name.endswith(".json"):
        if b"force_translucent" in jar.read(name):
            force_translucent.add(tex_name(name.removesuffix(".json")))

# Mushroom blocks are per-face multipart cubes; default state (all faces true) shows the cap/stem texture everywhere.
MUSHROOMS = {"brown_mushroom_block": "brown_mushroom_block", "red_mushroom_block": "red_mushroom_block",
             "mushroom_stem": "mushroom_stem"}
for bid, tex in MUSHROOMS.items():
    for t in (tex, "mushroom_block_inside"):
        dst = tex_dir / f"{t}.png"
        if not dst.exists():
            dst.write_bytes(jar.read(f"assets/minecraft/textures/block/{t}.png"))
    faces = ";".join(f"{f}=assets/minecraft/textures/block/{tex}.png" for f in ("down", "up", "north", "south", "east", "west"))
    assets[bid] = {"id": bid, "shape": "full_cube", "template": "multipart_per_face", "faces": faces,
                   "textures": f"assets/minecraft/textures/block/{tex}.png;assets/minecraft/textures/block/mushroom_block_inside.png",
                   "state_props": "down,east,north,south,up,west", "random_rotation": "false", "tinted": "false",
                   "animated": "false", "model_count": "2", "render_layer": "opaque", "tex_size": "16x16"}

# Blocks the classifier leaves out but the world generator places (phase 5's surface rules and snow line), drawn
# with shapes the importer knows: powder snow as a full cube (its model is six face plates, so it also shows from
# inside), and snow layers (1-7 a box k/8 high, 8 the snow block's cube; edge_cases.txt lists snow as mixed_states).
GENERATOR_BLOCKS = {"powder_snow": ("powder_snow", "full_cube", "", "1"), "snow": ("snow", "layers", "layers", "8")}
for bid, (tex, shape, props, count) in GENERATOR_BLOCKS.items():
    dst = tex_dir / f"{tex}.png"
    if not dst.exists():
        dst.write_bytes(jar.read(f"assets/minecraft/textures/block/{tex}.png"))
    faces = ";".join(f"{f}=assets/minecraft/textures/block/{tex}.png" for f in ("down", "up", "north", "south", "east", "west"))
    assets[bid] = {"id": bid, "shape": shape, "template": "generator_" + shape, "faces": faces,
                   "textures": f"assets/minecraft/textures/block/{tex}.png", "state_props": props,
                   "random_rotation": "false", "tinted": "false", "animated": "false", "model_count": count,
                   "render_layer": "opaque", "tex_size": "16x16"}

# Blocks drawn from their own baked models (phase 6, session 5): every block decoration places that is not a cube,
# slab or layers block. Groups follow the phase 6 scout's shape groups; chest and spawner (block entities) and
# sculk_shrieker (ancient cities) stay out. The faces are baked per quad by tools/bake_models.py, so the manifest
# keeps only the textures, the layer and the flags.
MODEL_GROUPS = {
    "cross": "allium azure_bluet blue_orchid brain_coral brown_mushroom bubble_coral cactus_flower cave_vines "
             "cave_vines_plant closed_eyeblossom cornflower dandelion dead_bush fire_coral firefly_bush hanging_roots "
             "horn_coral kelp kelp_plant lily_of_the_valley orange_tulip oxeye_daisy pale_hanging_moss pink_tulip poppy "
             "red_mushroom red_shrub red_tulip short_dry_grass sweet_berry_bush tall_dry_grass tube_coral white_tulip",
    "cross_facing": "amethyst_cluster large_amethyst_bud medium_amethyst_bud small_amethyst_bud",
    "tinted_cross": "bush fern large_fern short_grass sugar_cane tall_grass",
    "double_cross": "lilac peony rose_bush sunflower",
    "seagrass": "seagrass tall_seagrass",
    "flowerbed": "leaf_litter pink_petals wildflowers",
    "carpet": "moss_carpet pale_moss_carpet",
    "multiface": "glow_lichen sculk_vein vine",
    "custom": "azalea bamboo big_dripleaf big_dripleaf_stem brain_coral_fan brain_coral_wall_fan bubble_coral_fan "
              "bubble_coral_wall_fan cactus cocoa fire_coral_fan fire_coral_wall_fan flowering_azalea horn_coral_fan "
              "horn_coral_wall_fan lily_pad mangrove_propagule mangrove_roots pointed_dripstone sea_pickle "
              "shelf_mushroom small_dripleaf spore_blossom sulfur_spike tube_coral_fan tube_coral_wall_fan",
}


def model_textures(model, seen=None):
    """Face textures of a block model (parents resolved), in face order of the elements."""
    chain, textures, elements = [], {}, None
    name = model
    while name:
        data = json.loads(jar.read(f"assets/minecraft/models/{name.removeprefix('minecraft:')}.json"))
        for k, v in data.get("textures", {}).items():
            textures.setdefault(k, v)
        if elements is None and "elements" in data:
            elements = data["elements"]
        name = data.get("parent")

    def resolve(ref):
        ref = ref["sprite"] if isinstance(ref, dict) else ref
        return resolve(textures[ref[1:]]) if ref.startswith("#") else ref.removeprefix("minecraft:").removeprefix("block/")

    faces = [face for e in elements or [] for face in e["faces"].values()]
    return [resolve(face["texture"]) for face in faces], any("tintindex" in face for face in faces)


def blockstate_models(data):
    refs = []
    for value in list(data.get("variants", {}).values()) + [p["apply"] for p in data.get("multipart", [])]:
        refs += [v["model"] for v in (value if isinstance(value, list) else [value])]
    return refs


def alpha_of(name):
    from PIL import Image
    alphas = set(Image.open(tex_dir / f"{name}.png").convert("RGBA").getchannel("A").get_flattened_data())
    return "opaque" if alphas == {255} else "cutout" if alphas <= {0, 255} else "translucent"


for group, ids in MODEL_GROUPS.items():
    for bid in ids.split():
        data = json.loads(jar.read(f"assets/minecraft/blockstates/{bid}.json"))
        names, tinted = [], False
        for model in blockstate_models(data):
            textures, model_tinted = model_textures(model)
            names += [t for t in dict.fromkeys(textures) if t not in names]
            tinted |= model_tinted
        for t in names:
            for suffix in (".png", ".png.mcmeta"):
                entry = f"assets/minecraft/textures/block/{t}{suffix}"
                if not (tex_dir / f"{t}{suffix}").exists() and entry in jar.namelist():
                    (tex_dir / f"{t}{suffix}").write_bytes(jar.read(entry))
        classes = {alpha_of(t) for t in names}
        props = sorted({k.split("=")[0] for key in data.get("variants", {}) for k in key.split(",") if k} |
                       {k for p in data.get("multipart", []) for k in p.get("when", {}) if k not in ("OR", "AND")})
        assets[bid] = {"id": bid, "shape": "model", "template": group, "faces": "",
                       "textures": ";".join(names), "state_props": ",".join(props),
                       "random_rotation": str(any(isinstance(v, list) and len(v) > 1 for v in
                                                  list(data.get("variants", {}).values()) +
                                                  [p["apply"] for p in data.get("multipart", [])])).lower(),
                       "tinted": str(tinted).lower(), "animated": "false", "model_count": str(len(set(blockstate_models(data)))),
                       "render_layer": "translucent" if "translucent" in classes else "cutout" if "cutout" in classes
                       else "opaque", "tex_size": "16x16"}

blocks = []
for bid, a in assets.items():
    r = registry[bid]
    faces = {k: tex_name(v) for k, v in (p.split("=", 1) for p in a["faces"].split(";"))} if a["faces"] else {}
    all_tex = [tex_name(t) for t in a["textures"].split(";")]
    loc = locations.get(bid, {})
    layer = "translucent" if bid in force_translucent else a["render_layer"]
    blocks.append({
        "id": bid,
        "protocol_id": int(r["protocol_id"]),
        "default_state_id": int(r["default_state_id"]),
        "state_id_range": [int(r["min_state_id"]), int(r["max_state_id"])],
        "properties": r["properties"],
        "shape": a["shape"],
        "template": a["template"],
        "faces": faces,
        "all_textures": all_tex,
        "render_layer": layer,
        "force_translucent": bid in force_translucent,
        "tinted": a["tinted"] == "true",
        "animated": is_animated(all_tex),
        "random_rotation": a["random_rotation"] == "true",
        "rotates_by": a["state_props"],
        "tex_size": a["tex_size"],
        "tags": tags.get(bid, []),
        "where": loc.get("where"),
        "dimension": loc.get("dimension"),
        "y_range": ORE_Y.get(bid) or loc.get("y_range"),
    })
blocks.sort(key=lambda b: b["protocol_id"])

# Fluids: water, and still lava for the aquifers (phase 4; its textures join the research copies here).
for name in ("lava_still.png", "lava_still.png.mcmeta", "lava_flow.png", "lava_flow.png.mcmeta"):
    if not (tex_dir / name).exists():
        (tex_dir / name).write_bytes(jar.read(f"assets/minecraft/textures/block/{name}"))


def fluid_entry(fid, textures, tinted, layer):
    r = registry[fid]
    return {"id": fid, "protocol_id": int(r["protocol_id"]), "default_state_id": int(r["default_state_id"]),
            "state_id_range": [int(r["min_state_id"]), int(r["max_state_id"])], "properties": r["properties"],
            "textures": textures, "tinted": tinted, "render_layer": layer}


fluids = [fluid_entry("water", {"still": "water_still", "flow": "water_flow", "overlay": "water_overlay"}, True,
                      "translucent"),
          fluid_entry("lava", {"still": "lava_still", "flow": "lava_flow"}, False, "opaque")]
manifest = {"minecraft_version": "26.3", "source": "26.3.jar models/blockstates + server data-generator reports",
            "id_policy": "save files store string ids (palette per section); protocol_id/state ids are runtime-only and shift between versions",
            "block_count_in_registry": len(registry), "blocks": blocks, "fluids": fluids}
(out / "block_manifest.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")

def faces_short(f, all_textures):
    if not f:
        return " · ".join(all_textures)
    if len(set(f.values())) == 1:
        return f["up"]
    sides = {f[k] for k in ("north", "south", "east", "west")}
    if len(sides) == 1:
        if f["up"] == f["down"]:
            return f"ends: {f['up']} · side: {f['north']}"
        return f"top: {f['up']} · bottom: {f['down']} · side: {f['north']}"
    return " · ".join(f"{k}: {v}" for k, v in f.items())

def flags(b):
    out = []
    if b["tinted"]: out.append("tint")
    if b["animated"]: out.append("anim")
    if b["random_rotation"]: out.append("rand-rot")
    if b["rotates_by"] and b["template"] != "multipart_per_face" and b["shape"] == "full_cube":
        out.append(b["rotates_by"])
    return ", ".join(out)

def found(b):
    if b["where"]:
        s = b["where"]
        if b["y_range"]:
            s += f" — Y {b['y_range']}"
        return s
    if b["y_range"]:
        return f"Y {b['y_range']}"
    return "—"

rows = ["| # | Block ID | Shape | Textures (`textures/block/*.png`) | Layer | Flags | State IDs | Where found |",
        "|---:|---|---|---|---|---|---|---|"]
for b in blocks:
    lo, hi = b["state_id_range"]
    sid = str(lo) if lo == hi else f"{lo}–{hi}"
    rows.append(f"| {b['protocol_id']} | `{b['id']}` | {b['shape'].replace('_', ' ')} | {faces_short(b['faces'], b['all_textures'])} | "
                f"{b['render_layer']} | {flags(b)} | {sid} | {found(b).replace('|', '/')} |")
table = "\n".join(rows)

TAGS = ["mineable/pickaxe", "mineable/axe", "mineable/shovel", "mineable/hoe", "needs_stone_tool", "needs_iron_tool",
        "needs_diamond_tool", "base_stone_overworld", "base_stone_nether", "stone_ore_replaceables",
        "deepslate_ore_replaceables", "dirt", "sand", "logs", "planks", "leaves", "slabs", "wooden_slabs", "wool",
        "terracotta", "ice", "infiniburn_overworld", "dragon_immune", "wither_immune"]
cnt = Counter(t for b in blocks for t in b["tags"])
tag_rows = ["| Tag | Blocks in this sheet |", "|---|---:|"] + [f"| `#minecraft:{t}` | {cnt[t]} |" for t in TAGS if cnt[t]]

# Embed the worldgen table under the doc's own appendix heading: drop its H1, demote the rest two levels.
wg_lines = (out / "worldgen_heights.md").read_text(encoding="utf-8").splitlines()[1:]
worldgen = "\n".join("##" + l if l.startswith("#") else l for l in wg_lines).strip()
shape = Counter(b["shape"] for b in blocks)
layers = Counter(b["render_layer"] for b in blocks)
doc = template_path.read_text(encoding="utf-8")
for key, val in {"{{BLOCK_TABLE}}": table, "{{TAG_TABLE}}": "\n".join(tag_rows), "{{WORLDGEN}}": worldgen,
                 "{{N_CUBE}}": str(shape["full_cube"]), "{{N_SLAB}}": str(shape["slab"]), "{{N_MODEL}}": str(shape["model"]),
                 "{{N_TOTAL}}": str(len(blocks)), "{{N_REGISTRY}}": str(len(registry)),
                 "{{N_TEX}}": str(len(list(tex_dir.glob("*.png")))), "{{TRANSLUCENT}}": ", ".join(b["id"] for b in blocks if b["render_layer"] == "translucent" and "stained_glass" not in b["id"]) + ", 16 stained glasses", "{{CUTOUT}}": ", ".join(b["id"] for b in blocks if b["render_layer"] == "cutout"), "{{LAYERS}}": ", ".join(f"{k} {v}" for k, v in layers.most_common())}.items():
    doc = doc.replace(key, val)
doc_out.write_text(doc, encoding="utf-8")

print(f"blocks={len(blocks)} {dict(shape)} layers={dict(layers)}")
print("no location:", sum(1 for b in blocks if not b["where"] and not b["y_range"]))
print("locations.json ids not in sheet:", sorted(set(locations) - {b['id'] for b in blocks})[:40])
print("force_translucent:", sorted(force_translucent))
print("animated:", [b["id"] for b in blocks if b["animated"]])
print("tinted:", [b["id"] for b in blocks if b["tinted"]])
