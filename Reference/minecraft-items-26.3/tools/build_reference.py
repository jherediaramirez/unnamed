"""Build Reference/minecraft-items-26.3 from the pinned 26.3 client jar and the data generator's item components report.

usage (from the repository root):
    uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-items-26.3/tools/build_reference.py
        [--reports C:/dev/sandbox/mc-26.3-blocks/reports/reports] [--sums-only]

Inputs:
    the client jar (SHA-1 pinned below): item definitions, item models, item textures, en_us names, item and block tags
    the data generator's reports (`--reports`, from the server jar pinned below): minecraft/components/item/*.json
        (the "Default Components" report) and registries.json (the item registry's protocol ids)
    Reference/minecraft-blocks-26.3 (hash-checked): facts/block_facts.csv (hardness, correct tool, block items),
        facts/creative_tabs.json (creative positions) and block_manifest.json (which blocks are imported)

Tier A only (the items plan, S1): the 35 tools, the 7 spears, the ore and tier materials, stick, buckets and food.
Copies are byte for byte; every generated file comes from derive(), which verify_reference.py calls again on the
folder's own copies, so the verifier re-derives each file. The folder must hold only tools/ and README.md, so an
accepted reference is never overwritten; to rebuild, move the old folder away first. After a README or tool edit,
--sums-only rewrites only SHA256SUMS.txt.

Data only: the game's code is never read. Rule semantics (component defaults, the break-time formula) come from
minecraft.wiki ("Data component format", "Breaking"), as listed in README.md.
"""
import argparse
import csv
import hashlib
import io
import json
import math
import os
import sys
import zipfile
from fractions import Fraction
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
BLOCK_REF = REPO / "Reference/minecraft-blocks-26.3"
CLIENT_SHA1 = "e877b6a07acd633fb3bb475002175cec036e7b87"
SERVER_SHA1 = "33680f5f2ac32864d6d7cf5e56a705fdb3e05f4c"
CLIENT_JAR = Path(os.environ.get("APPDATA", "")) / "ModrinthApp/meta/versions/26.3/26.3.jar"
DEFAULT_REPORTS = Path("C:/dev/sandbox/mc-26.3-blocks/reports/reports")
ASSETS = "assets/minecraft/"
COMPONENTS = "reports/components/item/"
REGISTRY = "reports/registries-item.json"
BLOCK_INPUTS = ["facts/block_facts.csv", "facts/creative_tabs.json", "block_manifest.json"]

MATERIALS = ["wooden", "stone", "copper", "iron", "golden", "diamond", "netherite"]
TOOL_KINDS = ["pickaxe", "axe", "shovel", "hoe", "sword"]
TIER_A = {
    "tool": [f"{m}_{k}" for k in TOOL_KINDS for m in MATERIALS],
    "spear": [f"{m}_spear" for m in MATERIALS],
    "material": ["coal", "raw_copper", "raw_iron", "raw_gold", "copper_ingot", "iron_ingot", "gold_ingot", "diamond",
                 "emerald", "lapis_lazuli", "redstone", "quartz", "netherite_ingot", "netherite_scrap"],
    "stick": ["stick"],
    "bucket": ["bucket", "water_bucket", "lava_bucket"],
    "food": ["apple", "bread", "cooked_beef"],
}
# Component defaults when a component omits the field (minecraft.wiki "Data component format").
TOOL_DEFAULTS = {"default_mining_speed": 1.0, "damage_per_block": 1, "can_destroy_blocks_in_creative": True}
WEAPON_DEFAULTS = {"item_damage_per_attack": 1, "disable_blocking_for_seconds": 0.0}
# The player's base attack damage and attack speed; an item's base_attack_* modifiers add to them.
BASE_ATTACK = {"attack_damage": 1.0, "attack_speed": 4.0}
DEFINITION_ROOT = {"swap_animation_scale": 1.0, "hand_animation_on_swap": True}
BREAK_SAMPLES = ["dirt", "sand", "gravel", "oak_log", "oak_planks", "oak_leaves", "cobweb", "netherrack", "stone",
                 "cobblestone", "deepslate", "coal_ore", "copper_ore", "iron_ore", "gold_ore", "diamond_ore",
                 "deepslate_diamond_ore", "iron_block", "obsidian", "ancient_debris", "bedrock"]


def bare(name):
    return name.removeprefix("minecraft:")


def dumps(value):
    return (json.dumps(value, indent=1, ensure_ascii=False) + "\n").encode("utf-8")


class Sources:
    """Where derive() reads from: the report folder and jar when building, the folder's own copies when verifying."""

    def __init__(self, jar, component_bytes, registry_bytes):
        self.jar = jar
        self.names = set(jar.namelist())
        self.component_bytes = component_bytes
        self.registry_bytes = registry_bytes

    def component(self, item):
        return json.loads(self.component_bytes(item))["components"]

    def jar_json(self, name):
        return json.loads(self.jar.read(name)) if name in self.names else None


class Tags:
    def __init__(self, src, kind):
        self.src, self.kind, self.cache = src, kind, {}
        prefix = f"data/minecraft/tags/{kind}/"
        self.all = sorted(n[len(prefix):-5] for n in src.names if n.startswith(prefix) and n.endswith(".json"))

    def values(self, name):
        return self.src.jar_json(f"data/minecraft/tags/{self.kind}/{name}.json")["values"]

    def resolve(self, name):
        if name not in self.cache:
            self.cache[name] = set()  # a cycle resolves to what was found so far
            out = set()
            for value in self.values(name):
                value = value["id"] if isinstance(value, dict) else value
                out |= self.resolve(bare(value[1:])) if value.startswith("#") else {bare(value)}
            self.cache[name] = out
        return self.cache[name]

    def members(self, selector):
        """A tool rule's or repairable's `blocks`/`items`: a #tag, one id or a list of ids."""
        if isinstance(selector, list):
            return set().union(*(self.members(v) for v in selector))
        return self.resolve(bare(selector[1:])) if selector.startswith("#") else {bare(selector)}


def model_refs(node, when=None):
    """Every `model` leaf of an item model definition, with the display contexts it is selected for."""
    kind = bare(node.get("type", ""))
    if kind == "model":
        yield {"model": bare(node["model"]), "when": when, "tints": node.get("tints")}
    elif kind == "select":
        for case in node["cases"]:
            cases = case["when"] if isinstance(case["when"], list) else [case["when"]]
            yield from model_refs(case["model"], {"property": bare(node["property"]), "is": cases})
        if "fallback" in node:
            yield from model_refs(node["fallback"], {"property": bare(node["property"]), "is": "fallback"})
    else:
        for value in node.values():
            if isinstance(value, dict):
                yield from model_refs(value, when)
            elif isinstance(value, list):
                for entry in value:
                    if isinstance(entry, dict):
                        yield from model_refs(entry, when)


def model_chain(src, model):
    """The model's parents up to the root, child first; `builtin/*` ends the chain without a file."""
    chain, files = [], []
    while model:
        chain.append(model)
        if model.startswith("builtin/"):
            break
        name = f"{ASSETS}models/{model}.json"
        data = src.jar_json(name)
        if data is None:
            raise SystemExit(f"model file missing from the jar: {name}")
        files.append((name, data))
        model = bare(data["parent"]) if "parent" in data else None
    return chain, files


def resolve_model(src, model):
    chain, files = model_chain(src, model)
    textures, display, display_from, gui_light = {}, {}, {}, None
    for name, data in files:
        for key, value in data.get("textures", {}).items():
            textures.setdefault(key, value)
        for context, transform in data.get("display", {}).items():
            if context not in display:
                display[context], display_from[context] = transform, bare(name[len(ASSETS) + 7:-5])
        if gui_light is None and "gui_light" in data:
            gui_light = data["gui_light"]
    for key, value in list(textures.items()):
        seen = set()
        while value.startswith("#") and value not in seen:
            seen.add(value)
            value = textures.get(value[1:], value)
        textures[key] = bare(value)
    layers = [textures[f"layer{i}"] for i in range(len(textures)) if f"layer{i}" in textures]
    return {"id": model, "parent": chain[1] if len(chain) > 1 else None, "chain": chain, "textures": textures,
            "layers": layers, "generator": chain[-1] if chain[-1].startswith("builtin/") else None,
            "gui_light": gui_light or "side", "display": display, "display_from": display_from}, [n for n, _ in files]


def texture_entry(src, texture):
    name = f"{ASSETS}textures/{texture}.png"
    png = src.jar.read(name)
    image = Image.open(io.BytesIO(png)).convert("RGBA")
    width, height = image.size
    alpha = image.getchannel("A").tobytes()
    classes = set(alpha)
    kind = "opaque" if classes == {255} else "cutout" if classes <= {0, 255} else "translucent"
    rows = []
    for y in range(height):
        bits = 0
        for x in range(width):
            bits = (bits << 1) | (alpha[y * width + x] != 0)
        rows.append(f"{bits:0{width // 4}x}")
    return {"name": texture, "size": [width, height], "png_sha256": hashlib.sha256(png).hexdigest(),
            "rgba_sha256": hashlib.sha256(image.tobytes()).hexdigest(), "alpha": kind,
            "opaque_pixels": sum(a != 0 for a in alpha), "mask": rows}, name


def attack_totals(components):
    modifiers = components.get("minecraft:attribute_modifiers", [])
    if not modifiers:
        return None
    totals = {}
    for attribute, base in BASE_ATTACK.items():
        amounts = [m["amount"] for m in modifiers if m["type"] == f"minecraft:{attribute}"
                   and m["id"] == f"minecraft:base_{attribute}" and m["operation"] == "add_value"
                   and m["slot"] == "mainhand"]
        if len(amounts) != 1:
            raise SystemExit(f"unexpected {attribute} modifiers: {modifiers}")
        totals[attribute] = round(base + amounts[0], 6)
    if len(modifiers) != 2:
        raise SystemExit(f"unexpected attribute modifiers: {modifiers}")
    return {"damage": totals["attack_damage"], "speed": totals["attack_speed"], "modifiers": modifiers}


def break_ticks(speed, hardness, drops):
    """minecraft.wiki "Breaking": d = speed / hardness / (30 if it drops else 100); instant at d >= 1, otherwise
    ceil(1 / d) ticks. Exact arithmetic on the data's decimal values; also returns whether 1 / d is a whole number,
    where the game's float steps could land one tick either side (probe P5 settles it)."""
    if hardness < 0:
        return -1, False
    if hardness == 0:
        return 0, False
    inverse = hardness * (30 if drops else 100) / speed
    boundary = inverse.denominator == 1
    return (0 if inverse <= 1 else math.ceil(inverse)), boundary


def seconds(ticks, drops):
    text = "unbreakable" if ticks < 0 else "instant" if ticks == 0 else f"{ticks / 20:.2f}s"
    return text + ("" if drops or ticks < 0 else " ✗")


def derive(src, block_ref=BLOCK_REF):
    """Every generated file of the reference, plus the jar entries it copies. Pure: same inputs, same bytes."""
    lang = src.jar_json(f"{ASSETS}lang/en_us.json")
    registry = json.loads(src.registry_bytes)["minecraft:item"]["entries"]
    protocol = {bare(k): v["protocol_id"] for k, v in registry.items()}
    item_tags, block_tags = Tags(src, "item"), Tags(src, "block")
    facts = list(csv.DictReader(io.StringIO((block_ref / BLOCK_INPUTS[0]).read_text(encoding="utf-8"))))
    tabs = json.loads((block_ref / BLOCK_INPUTS[1]).read_text(encoding="utf-8"))
    block_manifest = json.loads((block_ref / BLOCK_INPUTS[2]).read_text(encoding="utf-8"))
    imported = {b["id"] for b in block_manifest["blocks"] + block_manifest["fluids"]}

    group_of = {item: group for group, items in TIER_A.items() for item in items}
    tier_a = sorted(group_of, key=lambda i: protocol[i])
    copies, models, textures = set(), {}, {}
    items, tool_tags = [], set()
    for item in tier_a:
        c = src.component(item)
        definition_name = f"{ASSETS}items/{item}.json"
        definition = src.jar_json(definition_name)
        copies.add(definition_name)
        refs = list(model_refs(definition["model"]))
        if bare(definition["model"]["type"]) not in ("model", "select") or not refs:
            raise SystemExit(f"{item}: Tier A definitions are a model or a display-context select")
        item_textures = []
        for ref in refs:
            if ref["model"] not in models:
                models[ref["model"]], files = resolve_model(src, ref["model"])
                copies.update(files)
            for texture in models[ref["model"]]["layers"]:
                if texture not in textures:
                    textures[texture], png = texture_entry(src, texture)
                    copies.add(png)
                    if png + ".mcmeta" in src.names:
                        copies.add(png + ".mcmeta")
                if texture not in item_textures:
                    item_textures.append(texture)
        tool = c.get("minecraft:tool")
        if tool is not None:
            tool = {"rules": tool["rules"], **{k: tool.get(k, v) for k, v in TOOL_DEFAULTS.items()}}
            tool_tags |= {bare(r["blocks"][1:]) for r in tool["rules"] if isinstance(r["blocks"], str)
                          and r["blocks"].startswith("#")}
        weapon = c.get("minecraft:weapon")
        if weapon is not None:
            weapon = {k: weapon.get(k, v) for k, v in WEAPON_DEFAULTS.items()}
        repair = c.get("minecraft:repairable", {}).get("items")
        kind = next((k for k in TOOL_KINDS + ["spear"] if item.endswith("_" + k)), None)
        items.append({
            "id": item, "protocol_id": protocol[item], "group": group_of[item],
            "material": item[:-len(kind) - 1] if kind else None, "kind": kind,
            "name": lang[c["minecraft:item_name"]["translate"]],
            "max_stack": c["minecraft:max_stack_size"], "max_damage": c.get("minecraft:max_damage"),
            "enchantability": c.get("minecraft:enchantable", {}).get("value"),
            "repair": None if repair is None else {"items": repair, "resolved": sorted(item_tags.members(repair))},
            "attack": attack_totals(c), "tool": tool, "weapon": weapon,
            "food": c.get("minecraft:food"), "consumable": c.get("minecraft:consumable"),
            "components": sorted(bare(k) for k in c),
            "item_tags": sorted(t for t in item_tags.all if item in item_tags.resolve(t)),
            "creative": [{"tab": bare(tab["id"]), "index": index} for tab in tabs
                         for index, entry in enumerate(tab["display"]) if entry == "minecraft:" + item],
            "model": {"definition": f"items/{item}.json", "type": bare(definition["model"]["type"]),
                      "models": [{"model": r["model"], "when": r["when"]} for r in refs],
                      **{k: definition.get(k, v) for k, v in DEFINITION_ROOT.items()}},
            "textures": item_textures,
        })
    if any(r["tints"] for item in items for r in model_refs(src.jar_json(f"{ASSETS}items/{item['id']}.json")["model"])):
        raise SystemExit("Tier A has no tinted layers; add tint handling before importing tinted items")

    # Tags: every item tag holding a Tier A item, the repair tags, the tool rules' block tags and the tier tags.
    wanted_items = sorted({t for i in items for t in i["item_tags"]} |
                          {bare(i["repair"]["items"][1:]) for i in items
                           if i["repair"] and isinstance(i["repair"]["items"], str) and i["repair"]["items"].startswith("#")})
    wanted_blocks = sorted(tool_tags | {f"needs_{m}_tool" for m in ("stone", "iron", "diamond")})
    tags = {"item": {t: {"values": item_tags.values(t), "resolved": sorted(item_tags.resolve(t))} for t in wanted_items},
            "block": {t: {"values": block_tags.values(t), "resolved": sorted(block_tags.resolve(t))} for t in wanted_blocks}}

    # The index of all items: which block each places (block_facts.csv's item column), and its definition kind.
    places = {}
    for row in facts:
        if row["item"]:
            places.setdefault(bare(row["item"]), []).append(row["id"])
    block_ids = {row["id"] for row in facts}
    index = []
    for item in sorted(protocol, key=protocol.get):
        refs = list(model_refs(src.jar_json(f"{ASSETS}items/{item}.json")["model"]))
        if not refs:
            kind = "special" if '"minecraft:special"' in json.dumps(src.jar_json(f"{ASSETS}items/{item}.json")) else "other"
        else:
            chain, _ = model_chain(src, refs[0]["model"])
            kind = "sprite" if "builtin/generated" in chain else "block_model" if any(
                m.startswith("block/") for m in chain) else "other"
        blocks = places.get(item, [])
        index.append({"id": item, "protocol_id": protocol[item],
                      "places_block": item if item in blocks else (blocks[0] if blocks else None),
                      "block_with_same_id": item in block_ids, "definition": kind})

    # Break times: the hand and the 35 tools on every block of block_facts.csv.
    tools = ["hand"] + TIER_A["tool"]
    tool_rules = {i["id"]: i["tool"] for i in items if i["id"] in TIER_A["tool"]}

    def evaluate(tool, block):
        if tool == "hand":
            return Fraction(1), False
        speed = correct = None
        for rule in tool_rules[tool]["rules"]:
            if block in block_tags.members(rule["blocks"]):
                if speed is None and "speed" in rule:
                    speed = Fraction(repr(rule["speed"]))
                if correct is None and "correct_for_drops" in rule:
                    correct = rule["correct_for_drops"]
        return (Fraction(repr(tool_rules[tool]["default_mining_speed"])) if speed is None else speed), bool(correct)

    rows, whole = [], 0
    for row in facts:
        block, hardness = row["id"], Fraction(row["destroy_time"])
        needs = row["requires_correct_tool_for_drops"] == "true"
        ticks, drops = [], []
        for tool in tools:
            speed, correct = evaluate(tool, block)
            drop = (not needs) or correct
            t, b = break_ticks(speed, hardness, drop)
            ticks.append(t)
            drops.append(int(drop and t >= 0))
            whole += b
        rows.append({"id": block, "hardness": float(hardness), "requires_correct_tool": needs,
                     "imported": block in imported,
                     "mineable": [k for k in TOOL_KINDS[:4] if block in block_tags.resolve(f"mineable/{k}")],
                     "ticks": ticks, "drops": drops})
    break_json = {
        "formula": "minecraft.wiki Breaking: d = speed / hardness / (30 if the block drops with the tool else 100); "
                   "instant (ticks 0) at d >= 1, otherwise ceil(1 / d) ticks; -1 unbreakable (drops 0). Standing on ground, out "
                   "of water, no enchantments or effects. speed: the first tool rule that matches and states one, "
                   "else default_mining_speed; drops: the block does not need the correct tool, or the first matching "
                   "rule stating correct_for_drops says true. Exact arithmetic: where 1 / d is a whole number the game's float steps "
                   "could differ by one tick (see README.md).",
        "tools": tools,
    }
    lines = ["{"] + [f' "{k}": {json.dumps(v, ensure_ascii=False)},' for k, v in break_json.items()] + [' "blocks": [']
    lines += [f"  {json.dumps(r, ensure_ascii=False)}," for r in rows]
    lines[-1] = lines[-1].rstrip(",")
    lines += [" ]", "}"]
    by_id = {r["id"]: r for r in rows}
    md = ["# Break times: scout F's sample blocks (generated from break_times.json)", "",
          "Seconds at 20 ticks per second, standing on ground, no enchantments or effects. ✗: breaks without "
          "dropping. The tool column is the block's `mineable` kind.", "",
          "| Block | Hardness | Needs tool | Tool | Hand | " + " | ".join(MATERIALS) + " | iron sword |",
          "|---|---|---|---|---|" + "---|" * len(MATERIALS) + "---|"]
    for block in BREAK_SAMPLES:
        r = by_id[block]
        kind = r["mineable"][0] if r["mineable"] else None
        cells = [tools.index("hand")] + ([tools.index(f"{m}_{kind}") for m in MATERIALS] if kind else [])
        text = [seconds(r["ticks"][i], r["drops"][i]) for i in cells]
        text += ["-"] * (1 + len(MATERIALS) - len(text))
        s = tools.index("iron_sword")
        md.append(f"| {block} | {r['hardness']:g} | {'yes' if r['requires_correct_tool'] else 'no'} | {kind or '-'} | "
                  + " | ".join(text) + f" | {seconds(r['ticks'][s], r['drops'][s])} |")

    textures_list = [textures[t] for t in sorted(textures)]
    generated = {
        "items.json": dumps({"minecraft_version": "26.3", "tier": "A", "items": items}),
        "models.json": dumps({"models": [models[m] for m in sorted(models)]}),
        "textures.json": dumps({"textures": textures_list}),
        "tags.json": dumps(tags),
        "item_index.json": b"{\n \"items\": [\n" + ",\n".join(
            "  " + json.dumps(e) for e in index).encode("utf-8") + b"\n ]\n}\n",
        "facts/break_times.json": ("\n".join(lines) + "\n").encode("utf-8"),
        "facts/break_times.md": ("\n".join(md) + "\n").encode("utf-8"),
    }
    copies |= {COMPONENTS + f"{i}.json" for i in tier_a}
    generated["manifest.json"] = dumps({
        "minecraft_version": "26.3", "client_sha1": CLIENT_SHA1, "server_sha1": SERVER_SHA1,
        "report": "data generator --reports, minecraft/components/item (Default Components) and registries.json, "
                  "from the server jar above",
        "block_reference_inputs": {n: hashlib.sha256((block_ref / n).read_bytes()).hexdigest() for n in BLOCK_INPUTS},
        "tier_a": TIER_A,
        "counts": {"items": len(items), "registry_items": len(index), "models": len(models),
                   "textures": len(textures), "item_tags": len(tags["item"]), "block_tags": len(tags["block"]),
                   "break_time_blocks": len(rows), "break_time_tools": len(tools), "break_time_whole_inverse": whole,
                   "definition_kinds": {k: sum(e["definition"] == k for e in index)
                                        for k in ("block_model", "sprite", "special", "other")}},
        "generated": sorted(generated),
        "copied": sorted(copies) + [REGISTRY],
    })
    return generated, sorted(copies)


def write_sums():
    files = sorted(p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*")
                   if p.is_file() and p.name != "SHA256SUMS.txt" and "__pycache__" not in p.parts)
    (ROOT / "SHA256SUMS.txt").write_text(
        "".join(f"{hashlib.sha256((ROOT / f).read_bytes()).hexdigest()}  {f}\n" for f in files), encoding="utf-8",
        newline="\n")
    print(f"SHA256SUMS.txt: {len(files)} files")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--reports", type=Path, default=DEFAULT_REPORTS)
    parser.add_argument("--sums-only", action="store_true")
    args = parser.parse_args()
    if args.sums_only:
        write_sums()
        return
    extra = [p.name for p in ROOT.iterdir() if p.name not in ("tools", "README.md")]
    if extra:
        raise SystemExit(f"refusing to overwrite an accepted reference ({extra}); move the folder away first")
    if hashlib.sha1(CLIENT_JAR.read_bytes()).hexdigest() != CLIENT_SHA1:
        raise SystemExit("client jar SHA-1 differs from the pinned one")
    registry = json.loads((args.reports / "registries.json").read_text(encoding="utf-8"))
    registry_bytes = (json.dumps({"minecraft:item": registry["minecraft:item"]}, indent=2) + "\n").encode("utf-8")
    components = args.reports / "minecraft/components/item"
    jar = zipfile.ZipFile(CLIENT_JAR)
    src = Sources(jar, lambda item: (components / f"{item}.json").read_bytes(), registry_bytes)
    generated, copies = derive(src)
    for name in copies:
        data = (components / name[len(COMPONENTS):]).read_bytes() if name.startswith(COMPONENTS) else jar.read(name)
        (ROOT / name).parent.mkdir(parents=True, exist_ok=True)
        (ROOT / name).write_bytes(data)
    (ROOT / REGISTRY).write_bytes(registry_bytes)
    for name, data in generated.items():
        (ROOT / name).parent.mkdir(parents=True, exist_ok=True)
        (ROOT / name).write_bytes(data)
    print(f"built: {len(generated)} generated files, {len(copies) + 1} copies")
    write_sums()


if __name__ == "__main__":
    main()
