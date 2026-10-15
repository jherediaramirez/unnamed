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
    # The torch (torch session T2): the floor and wall blocks of the torch item. build_reference.py appends their
    # textures and models after the groups above (APPENDED_BLOCKS), so no earlier index moves.
    "torch": "torch wall_torch",
    # Survival B1 (track T5): the 302 family blocks and the cauldrons and fires, appended after the torch the same
    # way (build_reference.py's APPENDED_BLOCKS, in this order). Their entries carry added: "survival-B1".
    "stairs": "white_wool_stairs orange_wool_stairs magenta_wool_stairs light_blue_wool_stairs yellow_wool_stairs "
               "lime_wool_stairs pink_wool_stairs gray_wool_stairs light_gray_wool_stairs cyan_wool_stairs "
               "purple_wool_stairs blue_wool_stairs brown_wool_stairs green_wool_stairs red_wool_stairs "
               "black_wool_stairs oak_stairs cobblestone_stairs brick_stairs stone_brick_stairs mud_brick_stairs "
               "resin_brick_stairs nether_brick_stairs sandstone_stairs spruce_stairs birch_stairs jungle_stairs "
               "quartz_stairs acacia_stairs cherry_stairs dark_oak_stairs pale_oak_stairs mangrove_stairs "
               "poplar_stairs bamboo_stairs bamboo_mosaic_stairs prismarine_stairs prismarine_brick_stairs "
               "dark_prismarine_stairs red_sandstone_stairs purpur_stairs white_concrete_stairs "
               "orange_concrete_stairs magenta_concrete_stairs light_blue_concrete_stairs yellow_concrete_stairs "
               "lime_concrete_stairs pink_concrete_stairs gray_concrete_stairs light_gray_concrete_stairs "
               "cyan_concrete_stairs purple_concrete_stairs blue_concrete_stairs brown_concrete_stairs "
               "green_concrete_stairs red_concrete_stairs black_concrete_stairs polished_granite_stairs "
               "smooth_red_sandstone_stairs mossy_stone_brick_stairs polished_diorite_stairs "
               "mossy_cobblestone_stairs end_stone_brick_stairs stone_stairs smooth_sandstone_stairs "
               "smooth_quartz_stairs granite_stairs andesite_stairs red_nether_brick_stairs "
               "polished_andesite_stairs diorite_stairs crimson_stairs warped_stairs blackstone_stairs "
               "polished_blackstone_brick_stairs polished_blackstone_stairs tuff_stairs polished_tuff_stairs "
               "tuff_brick_stairs sulfur_stairs polished_sulfur_stairs sulfur_brick_stairs cinnabar_stairs "
               "polished_cinnabar_stairs cinnabar_brick_stairs cut_copper_stairs exposed_cut_copper_stairs "
               "weathered_cut_copper_stairs oxidized_cut_copper_stairs waxed_cut_copper_stairs "
               "waxed_exposed_cut_copper_stairs waxed_weathered_cut_copper_stairs waxed_oxidized_cut_copper_stairs "
               "cobbled_deepslate_stairs polished_deepslate_stairs deepslate_tile_stairs deepslate_brick_stairs",
    "fences": "oak_fence nether_brick_fence spruce_fence birch_fence jungle_fence acacia_fence cherry_fence "
               "dark_oak_fence pale_oak_fence mangrove_fence poplar_fence bamboo_fence crimson_fence warped_fence",
    "fence_gates": "oak_fence_gate spruce_fence_gate birch_fence_gate jungle_fence_gate acacia_fence_gate "
                    "cherry_fence_gate dark_oak_fence_gate pale_oak_fence_gate mangrove_fence_gate "
                    "poplar_fence_gate bamboo_fence_gate crimson_fence_gate warped_fence_gate",
    "walls": "resin_brick_wall cobblestone_wall mossy_cobblestone_wall brick_wall prismarine_wall "
              "red_sandstone_wall mossy_stone_brick_wall granite_wall stone_brick_wall mud_brick_wall "
              "nether_brick_wall andesite_wall red_nether_brick_wall sandstone_wall end_stone_brick_wall "
              "diorite_wall blackstone_wall polished_blackstone_brick_wall polished_blackstone_wall tuff_wall "
              "polished_tuff_wall tuff_brick_wall sulfur_wall polished_sulfur_wall sulfur_brick_wall cinnabar_wall "
              "polished_cinnabar_wall cinnabar_brick_wall cobbled_deepslate_wall polished_deepslate_wall "
              "deepslate_tile_wall deepslate_brick_wall",
    "panes": "iron_bars copper_bars exposed_copper_bars weathered_copper_bars oxidized_copper_bars "
              "waxed_copper_bars waxed_exposed_copper_bars waxed_weathered_copper_bars waxed_oxidized_copper_bars "
              "glass_pane white_stained_glass_pane orange_stained_glass_pane magenta_stained_glass_pane "
              "light_blue_stained_glass_pane yellow_stained_glass_pane lime_stained_glass_pane "
              "pink_stained_glass_pane gray_stained_glass_pane light_gray_stained_glass_pane "
              "cyan_stained_glass_pane purple_stained_glass_pane blue_stained_glass_pane brown_stained_glass_pane "
              "green_stained_glass_pane red_stained_glass_pane black_stained_glass_pane",
    "doors": "oak_door iron_door spruce_door birch_door jungle_door acacia_door cherry_door dark_oak_door "
              "pale_oak_door mangrove_door poplar_door bamboo_door crimson_door warped_door copper_door "
              "exposed_copper_door weathered_copper_door oxidized_copper_door waxed_copper_door "
              "waxed_exposed_copper_door waxed_weathered_copper_door waxed_oxidized_copper_door",
    "trapdoors": "oak_trapdoor spruce_trapdoor birch_trapdoor jungle_trapdoor acacia_trapdoor cherry_trapdoor "
                  "dark_oak_trapdoor pale_oak_trapdoor mangrove_trapdoor poplar_trapdoor bamboo_trapdoor "
                  "iron_trapdoor crimson_trapdoor warped_trapdoor copper_trapdoor exposed_copper_trapdoor "
                  "weathered_copper_trapdoor oxidized_copper_trapdoor waxed_copper_trapdoor "
                  "waxed_exposed_copper_trapdoor waxed_weathered_copper_trapdoor waxed_oxidized_copper_trapdoor",
    "buttons": "stone_button oak_button spruce_button birch_button jungle_button acacia_button cherry_button "
                "dark_oak_button pale_oak_button mangrove_button poplar_button bamboo_button crimson_button "
                "warped_button polished_blackstone_button",
    "lever": "lever",
    "plates": "stone_pressure_plate oak_pressure_plate spruce_pressure_plate birch_pressure_plate "
               "jungle_pressure_plate acacia_pressure_plate cherry_pressure_plate dark_oak_pressure_plate "
               "pale_oak_pressure_plate mangrove_pressure_plate poplar_pressure_plate bamboo_pressure_plate "
               "light_weighted_pressure_plate heavy_weighted_pressure_plate crimson_pressure_plate "
               "warped_pressure_plate polished_blackstone_pressure_plate",
    "ladder": "ladder",
    "nether_vines": "weeping_vines weeping_vines_plant twisting_vines twisting_vines_plant",
    "beds": "white_bed orange_bed magenta_bed light_blue_bed yellow_bed lime_bed pink_bed gray_bed light_gray_bed "
             "cyan_bed purple_bed blue_bed brown_bed green_bed red_bed black_bed",
    "chests": "chest ender_chest trapped_chest",
    "farmland_path": "farmland dirt_path",
    "crops": "wheat pumpkin_stem melon_stem carrots potatoes torchflower_crop pitcher_crop beetroots",
    "saplings": "oak_sapling spruce_sapling birch_sapling jungle_sapling acacia_sapling cherry_sapling "
                 "dark_oak_sapling pale_oak_sapling poplar_sapling",
    "cauldrons_fire": "cauldron water_cauldron lava_cauldron powder_snow_cauldron fire soul_fire campfire "
                       "soul_campfire",
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


# Survival B1's groups: their entries get added: "survival-B1" (the importer's appended-family flag). The chests'
# blockstates name only a particle model (the chest is drawn by its block entity), so they carry stand_in: true and
# no textures; build_reference.py supplies the stand-in box and its tiles.
B1_GROUPS = ("stairs", "fences", "fence_gates", "walls", "panes", "doors", "trapdoors", "buttons", "lever", "plates",
             "ladder", "nether_vines", "beds", "chests", "farmland_path", "crops", "saplings", "cauldrons_fire")
B1_STAND_INS = {"chest", "trapped_chest", "ender_chest"}
b1_added = set()


def when_keys(when):
    """Property names a multipart condition tests, OR/AND lists included."""
    keys = set()
    for k, v in when.items():
        if k in ("OR", "AND"):
            for sub in v:
                keys |= when_keys(sub)
        else:
            keys.add(k)
    return keys


def model_force_translucent(model):
    """Whether a block model or one of its parents marks a texture force_translucent."""
    name = model
    while name:
        raw = jar.read(f"assets/minecraft/models/{name.removeprefix('minecraft:')}.json")
        if b"force_translucent" in raw:
            return True
        name = json.loads(raw).get("parent")
    return False


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
        if group in B1_GROUPS:
            b1_added.add(bid)
            # B1 only (earlier entries keep their rules): conditions inside OR/AND count, and a block whose models
            # (parents included) mark a texture force_translucent is translucent, as the 17 panes are.
            props = sorted(set(props) | {k for p in data.get("multipart", []) for k in when_keys(p.get("when", {}))})
            if any(model_force_translucent(m) for m in set(blockstate_models(data))):
                force_translucent.add(bid)
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
    all_tex = [tex_name(t) for t in a["textures"].split(";")] if a["textures"] else []
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
    if bid in B1_STAND_INS:
        blocks[-1]["stand_in"] = True
    if bid in b1_added:
        blocks[-1]["added"] = "survival-B1"
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
