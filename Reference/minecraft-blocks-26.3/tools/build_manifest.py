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

blocks = []
for bid, a in assets.items():
    r = registry[bid]
    faces = {k: tex_name(v) for k, v in (p.split("=", 1) for p in a["faces"].split(";"))}
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

water = registry["water"]
fluid = {"id": "water", "protocol_id": int(water["protocol_id"]), "default_state_id": int(water["default_state_id"]),
         "state_id_range": [int(water["min_state_id"]), int(water["max_state_id"])], "properties": water["properties"],
         "textures": {"still": "water_still", "flow": "water_flow", "overlay": "water_overlay"},
         "tinted": True, "render_layer": "translucent"}
manifest = {"minecraft_version": "26.3", "source": "26.3.jar models/blockstates + server data-generator reports",
            "id_policy": "save files store string ids (palette per section); protocol_id/state ids are runtime-only and shift between versions",
            "block_count_in_registry": len(registry), "blocks": blocks, "fluids": [fluid]}
(out / "block_manifest.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")

def faces_short(f):
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
    rows.append(f"| {b['protocol_id']} | `{b['id']}` | {b['shape'].replace('_', ' ')} | {faces_short(b['faces'])} | "
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
                 "{{N_CUBE}}": str(shape["full_cube"]), "{{N_SLAB}}": str(shape["slab"]),
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
