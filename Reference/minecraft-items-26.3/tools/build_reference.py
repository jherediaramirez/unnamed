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

Tier A only (the items plan, S1): the 35 tools, the 7 spears, the ore and tier materials, stick, buckets and food;
then (APPEND_LAST, loot step 2b) the drop items the imported blocks' loot tables name. Copies are byte for byte;
every generated file comes from derive(), which verify_reference.py calls again on the folder's own copies, so the
verifier re-derives each file. The folder must hold only tools/ and README.md, so an
accepted reference is never overwritten; to rebuild, move the old folder away first. After a README or tool edit,
--sums-only rewrites only SHA256SUMS.txt.

The two black-box facts files (DUMPED: the durability bar and the tooltip lines, as the server jar's ItemStack
answers through its public API) come from tools/ItemFacts.java, run after the build; they are only hashed here, so
run --sums-only after writing them (README.md, "Rebuild").

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
    # Loot step 2b: the items the imported blocks' loot tables name that no other group holds.
    "drop": ["oak_sapling", "spruce_sapling", "birch_sapling", "jungle_sapling", "acacia_sapling", "cherry_sapling",
             "dark_oak_sapling", "pale_oak_sapling", "poplar_sapling", "resin_clump", "amethyst_shard", "wheat_seeds",
             "flint", "snowball", "clay_ball", "book", "glowstone_dust", "cocoa_beans", "melon_slice", "gold_nugget",
             "prismarine_crystals", "sweet_berries", "glow_berries"],
    # Survival T6 (mobs 20a): the items the vanilla mobs drop that no other group holds (arrow is another lane's).
    "mob_drop": ["beef", "leather", "porkchop", "cooked_porkchop", "mutton", "cooked_mutton", "chicken",
                 "cooked_chicken", "feather", "egg", "rotten_flesh", "bone", "gunpowder", "string", "spider_eye",
                 "carrot", "potato", "milk_bucket"],
    # Survival T1 (furnace 8a2): the cooking outputs whose inputs the game already has (logs, iron and copper tools,
    # clay ball, netherrack, cactus, sea pickle, kelp, resin clump, potato) that no other group holds.
    "cooking_output": ["charcoal", "iron_nugget", "copper_nugget", "brick", "nether_brick", "green_dye", "lime_dye",
                       "dried_kelp", "resin_brick", "baked_potato"],
    # Survival T2 (armor 19a2): the wearable armor pieces (not elytra, animal armor or trimmed variants).
    "armor": [f"{m}_{p}" for m in ("leather", "chainmail", "iron", "golden", "diamond", "netherite", "copper")
              for p in ("helmet", "chestplate", "leggings", "boots")] + ["turtle_helmet"],
    # Survival T2 (shield 19b): the shield, whose definition is a using_item condition between two special models.
    "shield": ["shield"],
}
# Groups added after the reference was first imported: their items, and the models and textures they bring, follow
# the earlier ones (each part sorted as before), so no item, model or texture layer of an earlier build moves. The
# groups follow each other in this list's order (not by protocol id), so a new group never interleaves with an old one.
APPEND_LAST = ["drop", "mob_drop", "cooking_output", "armor", "shield"]


def append_rank(group):
    """0 for the first build's groups, else 1 + the group's place in APPEND_LAST."""
    return APPEND_LAST.index(group) + 1 if group in APPEND_LAST else 0
# Component defaults when a component omits the field (minecraft.wiki "Data component format").
TOOL_DEFAULTS = {"default_mining_speed": 1.0, "damage_per_block": 1, "can_destroy_blocks_in_creative": True}
WEAPON_DEFAULTS = {"item_damage_per_attack": 1, "disable_blocking_for_seconds": 0.0}
# The player's base attack damage and attack speed; an item's base_attack_* modifiers add to them.
BASE_ATTACK = {"attack_damage": 1.0, "attack_speed": 4.0}
DEFINITION_ROOT = {"swap_animation_scale": 1.0, "hand_animation_on_swap": True}
# The equippable component's defaults when it omits a field (minecraft.wiki "Data component format").
EQUIPPABLE_DEFAULTS = {"equip_sound": "minecraft:item.armor.equip_generic", "asset_id": None, "swappable": True}
# The blocks_attacks component's defaults when it omits a field (minecraft.wiki "Data component format/blocks_attacks").
BLOCKS_ATTACKS_DEFAULTS = {"block_delay_seconds": 0.0, "disable_cooldown_scale": 1.0,
                           "damage_reductions": [{"base": 0.0, "factor": 1.0, "horizontal_blocking_angle": 90.0}],
                           "item_damage": {"threshold": 0.0, "base": 0.0, "factor": 1.5}, "bypassed_by": None,
                           "block_sound": None, "disabled_sound": None}
DAMAGE_REDUCTION_DEFAULTS = {"horizontal_blocking_angle": 90.0}
# The shield's special model renders this entity texture (no pattern); the record names it and the build copies it.
SHIELD_TEXTURE = "entity/shield/shield_base_nopattern"
# The armor pieces' attribute modifiers, summed per attribute into the record's "armor" field.
ARMOR_ATTRIBUTES = {"armor": "armor", "armor_toughness": "toughness", "knockback_resistance": "knockback_resistance"}
# Attribute modifier operations in the order of their lang key suffix (attribute.modifier.plus.0 is add_value).
OPERATIONS = ["add_value", "add_multiplied_base", "add_multiplied_total"]
# tools/ItemFacts.java's output: written after the build, hashed but not derived.
DUMPED = ["facts/durability_bar.json", "facts/tooltips.json"]
# Blocks whose loot tables are copied and flattened into loot_tables.json; later families append, so the order is
# the file order.
LOOT_BLOCKS = ["coal_ore", "deepslate_coal_ore", "copper_ore", "deepslate_copper_ore", "iron_ore", "deepslate_iron_ore",
               "gold_ore", "deepslate_gold_ore", "diamond_ore", "deepslate_diamond_ore", "emerald_ore",
               "deepslate_emerald_ore", "lapis_ore", "deepslate_lapis_ore", "redstone_ore", "deepslate_redstone_ore",
               "nether_quartz_ore", "nether_gold_ore"]
LOOT_TABLE = "data/minecraft/loot_table/blocks/"
# Imported blocks whose loot tables are held back (survival B1's 310 blocks): mining them drops themselves until their
# sessions append their tables after every earlier one.
LOOT_LATER = "tools/loot_later.txt"
LOOT_PREDICATE = "data/minecraft/predicate/"
LOOT_FORMAT = ("Each pool's single entry flattened into ordered candidates; the first candidate whose conditions all "
               "pass is taken; count is set_count's inclusive range; bonus and explosion_decay are kept for later "
               "(neutral without enchantments or explosions).")
LOOT_EXTENSIONS = (
    "Tables: `modifiers` (the table's modifier list) only when the jar table has one; a table without pools has "
    "`pools: []`. Pools: `conditions` (the pool's condition) and `modifiers` only when present. Candidates: the ore "
    "form (item, conditions, count, bonus, explosion_decay) when the entry's modifiers are, in order, at most one "
    "each of set_count (no add, no condition), apply_bonus and explosion_decay (no condition); otherwise "
    "(item, conditions, modifiers) with modifiers an ordered list of set_count {count [min, max], add}, apply_bonus "
    "{enchantment, formula, bonus_multiplier}, limit_count {min, max}, explosion_decay, copy_components {source, "
    "include}, copy_state {block, properties}, each with a last key `conditions` when it has a condition. A "
    "conditions list holds predicate names (strings) or objects: survives_explosion, match_block {block, state}, "
    "any_of/all_of {terms}, inverted {term}, table_bonus {enchantment, chances}, random_chance {chance}, match_tool "
    "{tag or null, items resolved}, entity_properties {entity}, location_check {offset [x, y, z], block, state}. Predicates are listed in order of first use.")
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


def model_refs(node, when=None, special=False):
    """Every `model` leaf of an item model definition, with the display contexts it is selected for. With `special`
    (the shield group only, so no earlier record or index entry changes), a `condition` selects on its property
    (on_false first) and a `special` leaf yields its base model and the special model's type."""
    kind = bare(node.get("type", ""))
    if kind == "model":
        yield {"model": bare(node["model"]), "when": when, "tints": node.get("tints")}
    elif kind == "select":
        for case in node["cases"]:
            cases = case["when"] if isinstance(case["when"], list) else [case["when"]]
            yield from model_refs(case["model"], {"property": bare(node["property"]), "is": cases}, special)
        if "fallback" in node:
            yield from model_refs(node["fallback"], {"property": bare(node["property"]), "is": "fallback"}, special)
    elif special and kind == "condition":
        for value, branch in ((False, "on_false"), (True, "on_true")):
            yield from model_refs(node[branch], {"property": bare(node["property"]), "is": [value]}, special)
    elif special and kind == "special":
        yield {"model": bare(node["base"]), "when": when, "tints": None, "special": bare(node["model"]["type"])}
    else:
        for value in node.values():
            if isinstance(value, dict):
                yield from model_refs(value, when, special)
            elif isinstance(value, list):
                for entry in value:
                    if isinstance(entry, dict):
                        yield from model_refs(entry, when, special)


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


def armor_totals(components, slot):
    """An armor piece's armor, toughness and knockback resistance: its add_value modifiers in its own slot, summed."""
    modifiers = components.get("minecraft:attribute_modifiers", [])
    totals = dict.fromkeys(ARMOR_ATTRIBUTES.values(), 0.0)
    for m in modifiers:
        attribute = bare(m["type"])
        if attribute not in ARMOR_ATTRIBUTES or m["operation"] != "add_value" or m["slot"] != slot:
            raise SystemExit(f"unexpected armor modifiers: {modifiers}")
        totals[ARMOR_ATTRIBUTES[attribute]] = round(totals[ARMOR_ATTRIBUTES[attribute]] + m["amount"], 6)
    return {**totals, "modifiers": modifiers}


def equippable(components):
    value = components.get("minecraft:equippable")
    if value is None:
        return None
    return {"slot": value["slot"], **{k: value.get(k, v) for k, v in EQUIPPABLE_DEFAULTS.items()}}


def blocks_attacks(components):
    """The blocks_attacks component with every omitted field at its default; None without one."""
    value = components.get("minecraft:blocks_attacks")
    if value is None:
        return None
    out = {k: value.get(k, v) for k, v in BLOCKS_ATTACKS_DEFAULTS.items()}
    out["damage_reductions"] = [{**DAMAGE_REDUCTION_DEFAULTS, **r} for r in out["damage_reductions"]]
    out["item_damage"] = {**BLOCKS_ATTACKS_DEFAULTS["item_damage"], **value.get("item_damage", {})}
    return out


def trim_fallback(refs):
    """An armor definition selects its model on the trim material: only the fallback (untrimmed) model is kept."""
    if all(r["when"] and r["when"]["property"] == "trim_material" for r in refs):
        return [r for r in refs if r["when"]["is"] == "fallback"]
    return refs


def model_entry(ref):
    """A model case of the item record; "tints" (one per layer, data order) only when the definition has them."""
    entry = {"model": ref["model"], "when": ref["when"]}
    if ref["tints"]:
        entry["tints"] = ref["tints"]
    return entry


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


def loot_fail(block, why):
    raise SystemExit(f"loot table {block}: {why}")


RANDOM_CONDITIONS = ("random_chance", "table_bonus")


def loot_has_random(element):
    """Whether a condition (as loot_condition returns it) holds a random one anywhere in its tree."""
    if not isinstance(element, dict):
        return False
    if element["type"] in RANDOM_CONDITIONS:
        return True
    return any(loot_has_random(t) for t in element.get("terms", [])) or loot_has_random(element.get("term"))


def loot_condition(block, value, item_tags):
    """One condition: a "minecraft:<path>" predicate reference (-> the bare path) or an object (see `extensions`)."""
    if isinstance(value, str):
        if not value.startswith("minecraft:"):
            loot_fail(block, f"unsupported condition {value!r}")
        return value[len("minecraft:"):]
    if not isinstance(value, dict):
        loot_fail(block, f"unsupported condition {value!r}")
    kind, keys = bare(value.get("type", "")), set(value)

    def need(allowed, required):
        if not keys <= allowed | {"type"} or not required <= keys:
            loot_fail(block, f"unsupported {kind} condition {value!r}")

    if kind == "survives_explosion":
        need(set(), set())
        return {"type": kind}
    if kind == "match_block":
        need({"blocks", "state"}, {"blocks"})
        state = value.get("state", {})
        if not isinstance(value["blocks"], str) or not isinstance(state, dict) or \
                not all(isinstance(k, str) and isinstance(v, str) for k, v in state.items()):
            loot_fail(block, f"unsupported match_block {value!r}")
        return {"type": kind, "block": bare(value["blocks"]), "state": state}
    if kind in ("any_of", "all_of"):
        need({"terms"}, {"terms"})
        if not isinstance(value["terms"], list) or not value["terms"]:
            loot_fail(block, f"unsupported {kind} {value!r}")
        return {"type": kind, "terms": [loot_condition(block, t, item_tags) for t in value["terms"]]}
    if kind == "inverted":
        need({"term"}, {"term"})
        return {"type": kind, "term": loot_condition(block, value["term"], item_tags)}
    if kind == "table_bonus":
        need({"enchantment", "chances"}, {"enchantment", "chances"})
        chances = value["chances"]
        if value["enchantment"] != "minecraft:fortune" or not isinstance(chances, list) or not chances or not all(
                isinstance(c, (int, float)) and not isinstance(c, bool) for c in chances):
            loot_fail(block, f"unsupported table_bonus {value!r}")
        return {"type": kind, "enchantment": "fortune", "chances": chances}
    if kind == "random_chance":
        need({"chance"}, {"chance"})
        if not isinstance(value["chance"], (int, float)) or isinstance(value["chance"], bool):
            loot_fail(block, f"unsupported random_chance {value!r}")
        return {"type": kind, "chance": value["chance"]}
    if kind == "match_tool":
        need({"predicate"}, {"predicate"})
        predicate = value["predicate"]
        if not isinstance(predicate, dict) or set(predicate) != {"items"}:
            loot_fail(block, f"unsupported match_tool predicate {predicate!r}")
        items = predicate["items"]
        if isinstance(items, str) and items.startswith("#"):
            tag = bare(items[1:])
            if tag not in item_tags.all:
                loot_fail(block, f"unresolvable item tag {items!r}")
            return {"type": kind, "tag": tag, "items": sorted(item_tags.resolve(tag))}
        listed = items if isinstance(items, list) else [items]
        if not listed or not all(isinstance(i, str) and not i.startswith("#") for i in listed):
            loot_fail(block, f"unsupported match_tool items {items!r}")
        return {"type": kind, "tag": None, "items": [bare(i) for i in listed]}
    if kind == "entity_properties":
        need({"entity", "predicate"}, {"entity", "predicate"})
        if value["entity"] != "this" or value["predicate"] != {}:
            loot_fail(block, f"unsupported entity_properties {value!r}")
        return {"type": kind, "entity": "this"}
    if kind == "location_check":
        need({"offsetX", "offsetY", "offsetZ", "predicate"}, {"predicate"})
        predicate = value["predicate"]
        offset = [value.get(k, 0) for k in ("offsetX", "offsetY", "offsetZ")]
        inner = predicate.get("block") if isinstance(predicate, dict) else None
        if set(predicate) != {"block"} or not isinstance(inner, dict) or not set(inner) <= {"blocks", "state"} or                 not isinstance(inner.get("blocks"), str) or not isinstance(inner.get("state", {}), dict) or                 not all(isinstance(v, int) and not isinstance(v, bool) for v in offset):
            loot_fail(block, f"unsupported location_check {value!r}")
        return {"type": kind, "offset": offset, "block": bare(inner["blocks"]), "state": inner.get("state", {})}
    return loot_fail(block, f"unsupported condition type {kind!r}")


def loot_conditions(block, value, item_tags):
    """A `condition` value (one condition or a list; all must pass) as a list."""
    values = value if isinstance(value, list) else [value]
    if not values:
        loot_fail(block, "empty condition list")
    return [loot_condition(block, v, item_tags) for v in values]


def loot_count(block, count):
    if isinstance(count, dict):
        kind = bare(count.get("type", ""))
        if kind == "uniform" and set(count) == {"type", "min", "max"}:
            low, high = count["min"], count["max"]
        elif kind == "constant" and set(count) == {"type", "value"}:
            low = high = count["value"]
        else:
            loot_fail(block, f"unsupported count {count!r}")
    else:
        low = high = count
    if isinstance(low, bool) or isinstance(high, bool) or not all(
            isinstance(v, (int, float)) and float(v).is_integer() for v in (low, high)):
        loot_fail(block, f"count is not whole: {count!r}")
    return [int(low), int(high)]


def loot_modifiers(block, value, item_tags):
    """A `modifier` value (one modifier or a list) as the ordered list of modifier objects."""
    values = value if isinstance(value, list) else [value]
    out = []
    for modifier in values:
        if not isinstance(modifier, dict):
            loot_fail(block, f"unsupported modifier {modifier!r}")
        kind, keys = bare(modifier.get("type", "")), set(modifier)

        def need(allowed, required):
            if not keys <= allowed | {"type", "condition"} or not required <= keys:
                loot_fail(block, f"unsupported {kind} {modifier!r}")

        if kind == "set_count":
            need({"count", "add"}, {"count"})
            if not isinstance(modifier.get("add", False), bool):
                loot_fail(block, f"unsupported set_count {modifier!r}")
            result = {"type": kind, "count": loot_count(block, modifier["count"]), "add": modifier.get("add", False)}
        elif kind == "apply_bonus":
            need({"enchantment", "formula", "parameters"}, {"enchantment", "formula"})
            formula = bare(modifier["formula"]) if isinstance(modifier["formula"], str) else None
            if modifier["enchantment"] != "minecraft:fortune" or formula not in ("ore_drops", "uniform_bonus_count"):
                loot_fail(block, f"unsupported apply_bonus {modifier!r}")
            multiplier = 0
            if formula == "uniform_bonus_count":
                parameters = modifier.get("parameters")
                if not isinstance(parameters, dict) or set(parameters) != {"bonusMultiplier"} or \
                        not isinstance(parameters["bonusMultiplier"], int):
                    loot_fail(block, f"unsupported uniform_bonus_count {modifier!r}")
                multiplier = parameters["bonusMultiplier"]
            elif "parameters" in modifier:
                loot_fail(block, "ore_drops with parameters")
            result = {"type": kind, "enchantment": "fortune", "formula": formula, "bonus_multiplier": multiplier}
        elif kind == "limit_count":
            need({"limit"}, {"limit"})
            limit = modifier["limit"]
            if not isinstance(limit, dict) or not limit or not set(limit) <= {"min", "max"} or not all(
                    isinstance(v, int) and not isinstance(v, bool) for v in limit.values()):
                loot_fail(block, f"unsupported limit_count {modifier!r}")
            result = {"type": kind, "min": limit.get("min"), "max": limit.get("max")}
        elif kind == "explosion_decay":
            need(set(), set())
            result = {"type": kind}
        elif kind == "copy_components":
            need({"source", "include"}, {"source", "include"})
            if not isinstance(modifier["source"], str) or not isinstance(modifier["include"], list) or not all(
                    isinstance(i, str) for i in modifier["include"]):
                loot_fail(block, f"unsupported copy_components {modifier!r}")
            result = {"type": kind, "source": modifier["source"], "include": [bare(i) for i in modifier["include"]]}
        elif kind == "copy_state":
            need({"block", "properties"}, {"block", "properties"})
            if not isinstance(modifier["block"], str) or not isinstance(modifier["properties"], list) or not all(
                    isinstance(i, str) for i in modifier["properties"]):
                loot_fail(block, f"unsupported copy_state {modifier!r}")
            result = {"type": kind, "block": bare(modifier["block"]), "properties": modifier["properties"]}
        else:
            return loot_fail(block, f"unsupported modifier {kind!r}")
        if "condition" in modifier:
            result["conditions"] = loot_conditions(block, modifier["condition"], item_tags)
        out.append(result)
    return out


def loot_candidate(block, entry, conditions, item_tags):
    """The ore form (item, conditions, count, bonus, explosion_decay) when the modifiers are the ores' own, else the
    general form (item, conditions, modifiers)."""
    modifiers = loot_modifiers(block, entry["modifier"], item_tags) if "modifier" in entry else []
    order = ["set_count", "apply_bonus", "explosion_decay"]
    ranks = [order.index(m["type"]) if m["type"] in order else -1 for m in modifiers]
    if -1 not in ranks and ranks == sorted(set(ranks)) and not any("conditions" in m or m.get("add") for m in modifiers):
        count, bonus, decay = [1, 1], None, False
        for m in modifiers:
            if m["type"] == "set_count":
                count = m["count"]
            elif m["type"] == "apply_bonus":
                bonus = {k: v for k, v in m.items() if k != "type"}
            else:
                decay = True
        return {"item": bare(entry["name"]), "conditions": conditions, "count": count, "bonus": bonus,
                "explosion_decay": decay}
    return {"item": bare(entry["name"]), "conditions": conditions, "modifiers": modifiers}


def loot_flatten(block, entry, inherited, item_tags):
    if not isinstance(entry, dict) or not set(entry) <= {"type", "name", "children", "condition", "modifier"}:
        loot_fail(block, f"unsupported entry keys {sorted(entry) if isinstance(entry, dict) else entry!r}")
    own = loot_conditions(block, entry["condition"], item_tags) if "condition" in entry else []
    conditions = inherited + own
    kind = bare(entry.get("type", ""))
    if kind == "item":
        if "children" in entry or not isinstance(entry.get("name"), str):
            loot_fail(block, "malformed item entry")
        return [loot_candidate(block, entry, conditions, item_tags)]
    if kind == "alternatives":
        if "name" in entry or "modifier" in entry or not entry.get("children"):
            loot_fail(block, "malformed alternatives entry")
        if any(loot_has_random(c) for c in own):
            loot_fail(block, "random condition on an alternatives entry (flattening would re-roll it per child)")
        return [c for child in entry["children"] for c in loot_flatten(block, child, conditions, item_tags)]
    return loot_fail(block, f"unsupported entry type {kind!r}")


def loot_predicate(name, data):
    """A match_tool predicate file as {name, file, type, items, enchantments}."""
    if bare(data.get("type", "")) != "match_tool" or set(data) != {"type", "predicate"}:
        raise SystemExit(f"predicate {name}: unsupported {sorted(data)}")
    body = data["predicate"]
    if not set(body) <= {"items", "predicates"}:
        raise SystemExit(f"predicate {name}: unsupported keys {sorted(body)}")
    items = body.get("items", [])
    items = [bare(i) for i in (items if isinstance(items, list) else [items])]
    predicates = body.get("predicates", {})
    if not set(predicates) <= {"minecraft:enchantments"}:
        raise SystemExit(f"predicate {name}: unsupported predicates {sorted(predicates)}")
    enchantments = []
    for e in predicates.get("minecraft:enchantments", []):
        if set(e) != {"enchantments", "levels"} or not isinstance(e["enchantments"], str) or \
                set(e["levels"]) != {"min"} or not isinstance(e["levels"]["min"], int):
            raise SystemExit(f"predicate {name}: unsupported enchantment entry {e!r}")
        enchantments.append({"id": bare(e["enchantments"]), "min_level": e["levels"]["min"]})
    return {"name": name, "file": f"{LOOT_PREDICATE}{name}.json", "type": "match_tool", "items": items,
            "enchantments": enchantments}


def loot_blocks(src, block_manifest):
    """LOOT_BLOCKS, then every other imported block (in block_manifest.json's order) with a loot table in the jar,
    except the blocks listed in tools/loot_later.txt (their tables wait for their own sessions, appended last)."""
    later = set((ROOT / LOOT_LATER).read_text(encoding="utf-8").split())
    unknown = sorted(later - {b["id"] for b in block_manifest["blocks"]})
    if unknown:
        raise ValueError(f"{LOOT_LATER} lists ids that are not blocks of block_manifest.json: {unknown}")
    return LOOT_BLOCKS + [b["id"] for b in block_manifest["blocks"]
                          if b["id"] not in LOOT_BLOCKS and b["id"] not in later and
                          f"{LOOT_TABLE}{b['id']}.json" in src.names]


def loot_names(node, names):
    """Predicate names in first-use order, walking conditions depth-first through a derived structure."""
    if isinstance(node, dict):
        for key, value in node.items():
            if key in ("conditions", "terms") and isinstance(value, list):
                for element in value:
                    if isinstance(element, str):
                        names.setdefault(element)
                    else:
                        loot_names(element, names)
            elif key == "term" and isinstance(value, str):
                names.setdefault(value)
            else:
                loot_names(value, names)
    elif isinstance(node, list):
        for value in node:
            loot_names(value, names)


def derive_loot(src, blocks, item_tags):
    """loot_tables.json's content, and the jar files (tables, predicates) it is read from."""
    tables, names, files = [], {}, set()
    for block in blocks:
        file = f"{LOOT_TABLE}{block}.json"
        table = src.jar_json(file)
        if table is None:
            loot_fail(block, "not in the jar")
        if bare(table.get("type", "")) != "block" or not set(table) <= {"type", "pools", "random_sequence", "modifier"} \
                or not isinstance(table.get("pools", []), list):
            loot_fail(block, f"unsupported table keys {sorted(table)}")
        files.add(file)
        pools = []
        for pool in table.get("pools", []):
            if not set(pool) <= {"rolls", "entries", "bonus_rolls", "condition", "modifier"} or \
                    not isinstance(pool.get("rolls"), int) or \
                    isinstance(pool["rolls"], bool) or pool.get("bonus_rolls", 0) != 0 or \
                    not isinstance(pool.get("entries"), list) or len(pool["entries"]) != 1:
                loot_fail(block, f"unsupported pool {sorted(pool)}")
            made = {"rolls": pool["rolls"], "candidates": loot_flatten(block, pool["entries"][0], [], item_tags)}
            if "condition" in pool:
                made["conditions"] = loot_conditions(block, pool["condition"], item_tags)
            if "modifier" in pool:
                made["modifiers"] = loot_modifiers(block, pool["modifier"], item_tags)
            pools.append(made)
        made = {"block": block, "file": file, "pools": pools}
        if "modifier" in table:
            made["modifiers"] = loot_modifiers(block, table["modifier"], item_tags)
        loot_names(made, names)
        tables.append(made)
    predicates = []
    for name in names:
        file = f"{LOOT_PREDICATE}{name}.json"
        data = src.jar_json(file)
        if data is None:
            raise SystemExit(f"predicate {name}: not in the jar")
        files.add(file)
        predicates.append(loot_predicate(name, data))
    loot = {"minecraft_version": "26.3", "format": LOOT_FORMAT, "predicates": predicates, "tables": tables,
            "extensions": LOOT_EXTENSIONS}
    return dumps(loot), files, sum(len(p["candidates"]) for t in tables for p in t["pools"]), len(predicates)


RECIPE_DIR = "data/minecraft/recipe/"
RECIPE_TYPES = {"minecraft:crafting_shaped": "shaped", "minecraft:crafting_shapeless": "shapeless"}
RECIPE_SOURCE = "client jar data/minecraft/recipe (crafting_shaped, crafting_shapeless) and data/minecraft/tags/item"


def tag_closure(tags, name, seen):
    """name and every tag nested in it, added to seen."""
    if name in seen:
        return
    seen.add(name)
    for value in tags.values(name):
        value = value["id"] if isinstance(value, dict) else value
        if value.startswith("#"):
            tag_closure(tags, bare(value[1:]), seen)


def derive_recipes(src, item_tags):
    """recipes.json's bytes, the jar entries it copies (recipe files and item tag files) and the manifest's recipes."""
    names = sorted(n for n in src.names if n.startswith(RECIPE_DIR) and n.endswith(".json"))
    used_tags, copies, skipped, out = set(), set(), {}, []

    def ingredient(value):
        values = value if isinstance(value, list) else [value]
        tag = None
        items = set()
        for v in values:
            if v.startswith("#"):
                tag = bare(v[1:]) if len(values) == 1 else tag
                tag_closure(item_tags, bare(v[1:]), used_tags)
                items |= item_tags.resolve(bare(v[1:]))
            else:
                items.add(bare(v))
        return {"tag": tag, "items": sorted(items)}

    for name in names:
        data = src.jar_json(name)
        kind = RECIPE_TYPES.get(data["type"]) if "/" not in name[len(RECIPE_DIR):] else None  # crafting recipes are top-level files
        if kind is None:
            skipped[data["type"]] = skipped.get(data["type"], 0) + 1
            continue
        copies.add(name)
        result = {"item": bare(data["result"]["id"]), "count": data["result"].get("count", 1)}
        if "components" in data["result"]:
            result["components"] = data["result"]["components"]
        entry = {"id": name[len(RECIPE_DIR):-5], "type": kind, "category": data.get("category"),
                 "group": data.get("group")}
        if kind == "shaped":
            pattern = data["pattern"]
            width, height = max(len(r) for r in pattern), len(pattern)
            keys = {k: ingredient(v) for k, v in data["key"].items()}
            entry |= {"width": width, "height": height,
                      "cells": [keys[row[x]] if x < len(row) and row[x] != " " else None
                                for row in pattern for x in range(width)]}
        else:
            entry |= {"width": 0, "height": 0, "ingredients": [ingredient(v) for v in data["ingredients"]]}
        entry["result"] = result
        out.append(entry)
    copies |= {f"data/minecraft/tags/item/{t}.json" for t in used_tags}
    listed = lambda e: [e["ingredients"]] if e["type"] == "shapeless" else [e["cells"]]
    manifest = {"source": RECIPE_SOURCE, "included": len(out),
                "shaped": sum(e["type"] == "shaped" for e in out), "shapeless": sum(e["type"] == "shapeless" for e in out),
                "with_result_components": [e["id"] for e in out if "components" in e["result"]],
                "with_bucket_ingredient": [e["id"] for e in out if any(
                    i and any(x.endswith("_bucket") for x in i["items"]) for i in listed(e)[0])],
                "skipped_types": dict(sorted(skipped.items()))}
    return dumps({"minecraft_version": "26.3", "recipes": out}), copies, manifest


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
    tier_a = sorted(group_of, key=lambda i: (append_rank(group_of[i]), protocol[i]))
    copies, models, textures = set(), {}, {}
    items, tool_tags = [], set()
    for item in tier_a:
        c = src.component(item)
        definition_name = f"{ASSETS}items/{item}.json"
        definition = src.jar_json(definition_name)
        copies.add(definition_name)
        shield = group_of[item] == "shield"
        refs = trim_fallback(list(model_refs(definition["model"], special=shield)))
        kinds = ("model", "select") + (("condition",) if shield else ())
        if bare(definition["model"]["type"]) not in kinds or not refs:
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
        equip = equippable(c)
        # Armor pieces: their modifiers are armor attributes, not the attack ones; "armor" and "equippable" are only
        # on the records that have them, so every earlier record stays byte-identical.
        extra = {} if equip is None else {"equippable": equip}
        if group_of[item] == "armor":
            extra["armor"] = armor_totals(c, equip["slot"])
        # The shield (19b): its special model (type, entity texture, the definition's transformation) and the
        # blocks_attacks component; only its record has them.
        if shield:
            if {r.get("special") for r in refs} != {"shield"}:
                raise SystemExit(f"{item}: expected the shield special model in every case")
            extra["special"] = {"type": "shield", "texture": SHIELD_TEXTURE,
                                "transformation": definition["model"].get("transformation")}
            extra["blocks_attacks"] = blocks_attacks(c)
            copies.add(f"{ASSETS}textures/{SHIELD_TEXTURE}.png")
        items.append({
            "id": item, "protocol_id": protocol[item], "group": group_of[item],
            "material": item[:-len(kind) - 1] if kind else None, "kind": kind,
            "name": lang[c["minecraft:item_name"]["translate"]],
            "max_stack": c["minecraft:max_stack_size"], "max_damage": c.get("minecraft:max_damage"),
            "enchantability": c.get("minecraft:enchantable", {}).get("value"),
            "repair": None if repair is None else {"items": repair, "resolved": sorted(item_tags.members(repair))},
            "attack": None if group_of[item] == "armor" else attack_totals(c), "tool": tool, "weapon": weapon,
            "food": c.get("minecraft:food"), "consumable": c.get("minecraft:consumable"),
            "components": sorted(bare(k) for k in c),
            "item_tags": sorted(t for t in item_tags.all if item in item_tags.resolve(t)),
            "creative": [{"tab": bare(tab["id"]), "index": index} for tab in tabs
                         for index, entry in enumerate(tab["display"]) if entry == "minecraft:" + item],
            "model": {"definition": f"items/{item}.json", "type": bare(definition["model"]["type"]),
                      "models": [model_entry(r) for r in refs],
                      **{k: definition.get(k, v) for k, v in DEFINITION_ROOT.items()}},
            "textures": item_textures,
            **extra,
        })
    # Tints: only the leather pieces' dye tint (a per-layer colour with a default) is handled.
    if any(t is not None and bare(t.get("type", "")) != "dye" for item in items for m in item["model"]["models"]
           for t in m.get("tints", [])):
        raise SystemExit("only dye tints are handled; add tint handling before importing other tinted items")
    # The tooltip text the items' attribute modifiers need, verbatim from en_us.json: each slot's header ("When in
    # Main Hand:"), each attribute's name, and each operation's equals, plus and take forms.
    lang_keys = set()
    for item in items:
        for modifier in (item["attack"] or item.get("armor") or {}).get("modifiers", []):
            operation = OPERATIONS.index(modifier["operation"])
            lang_keys |= {f"item.modifiers.{modifier['slot']}", f"attribute.name.{bare(modifier['type'])}"}
            lang_keys |= {f"attribute.modifier.{form}.{operation}" for form in ("equals", "plus", "take")}
    if any(key not in lang for key in lang_keys):
        raise SystemExit(f"lang keys missing from en_us.json: {sorted(k for k in lang_keys if k not in lang)}")

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

    # Models and textures sorted by name, those first used by an APPEND_LAST group after the others.
    # A model or texture takes the rank of the first group (in build order) that uses it.
    model_rank, texture_rank = {}, {}
    for i in items:
        rank = append_rank(i["group"])
        for m in i["model"]["models"]:
            model_rank[m["model"]] = min(rank, model_rank.get(m["model"], rank))
        for t in i["textures"]:
            texture_rank[t] = min(rank, texture_rank.get(t, rank))
    textures_list = [textures[t] for t in sorted(textures, key=lambda t: (texture_rank[t], t))]
    generated = {
        "items.json": dumps({"minecraft_version": "26.3", "tier": "A", "items": items}),
        "models.json": dumps({"models": [models[m] for m in sorted(models, key=lambda m: (model_rank[m], m))]}),
        "textures.json": dumps({"textures": textures_list}),
        "tags.json": dumps(tags),
        "lang.json": dumps({"lang": "en_us", "keys": {key: lang[key] for key in sorted(lang_keys)}}),
        "item_index.json": b"{\n \"items\": [\n" + ",\n".join(
            "  " + json.dumps(e) for e in index).encode("utf-8") + b"\n ]\n}\n",
        "facts/break_times.json": ("\n".join(lines) + "\n").encode("utf-8"),
        "facts/break_times.md": ("\n".join(md) + "\n").encode("utf-8"),
    }
    copies |= {COMPONENTS + f"{i}.json" for i in tier_a}
    loot_list = loot_blocks(src, block_manifest)
    generated["loot_tables.json"], loot_files, loot_candidates, loot_predicates = derive_loot(
        src, loot_list, item_tags)
    copies |= loot_files
    generated["recipes.json"], recipe_copies, recipes_manifest = derive_recipes(src, item_tags)
    copies |= recipe_copies
    generated["manifest.json"] = dumps({
        "minecraft_version": "26.3", "client_sha1": CLIENT_SHA1, "server_sha1": SERVER_SHA1,
        "report": "data generator --reports, minecraft/components/item (Default Components) and registries.json, "
                  "from the server jar above",
        "block_reference_inputs": {n: hashlib.sha256((block_ref / n).read_bytes()).hexdigest() for n in BLOCK_INPUTS},
        "tier_a": TIER_A,
        "counts": {"items": len(items), "registry_items": len(index), "models": len(models),
                   "textures": len(textures), "item_tags": len(tags["item"]), "block_tags": len(tags["block"]),
                   "lang_keys": len(lang_keys),
                   "break_time_blocks": len(rows), "break_time_tools": len(tools), "break_time_whole_inverse": whole,
                   "definition_kinds": {k: sum(e["definition"] == k for e in index)
                                        for k in ("block_model", "sprite", "special", "other")}},
        "generated": sorted(generated),
        "copied": sorted(copies) + [REGISTRY],
        "dumped": DUMPED,
        "recipes": recipes_manifest,
        "loot": {"blocks": loot_list, "tables": len(loot_list), "predicates": loot_predicates,
                 "candidates": loot_candidates,
                 "source": "client jar data/minecraft/loot_table/blocks and data/minecraft/predicate"},
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
