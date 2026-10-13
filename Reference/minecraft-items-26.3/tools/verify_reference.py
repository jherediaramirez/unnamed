"""Verify Reference/minecraft-items-26.3: file hashes, re-derivation of every generated file, copies, counts, spot values,
and the black-box facts (ItemFacts.java) against the data.

usage: uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-items-26.3/tools/verify_reference.py
Re-derivation and the jar comparisons need the pinned client jar; the report comparison runs when the data
generator's report folder is present (C:/dev/sandbox/mc-26.3-blocks/reports/reports). Hashes, block-reference
inputs and spot values need only this folder and the block reference.
"""
import hashlib
import importlib.util
import json
import sys
import zipfile
from decimal import ROUND_HALF_EVEN, Decimal
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_reference", Path(__file__).with_name("build_reference.py"))
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)
errors = []


def check(condition, message):
    if not condition:
        errors.append(message)


def load(name):
    return json.loads((ROOT / name).read_text(encoding="utf-8"))


# 1. Every file is listed with its SHA-256, and nothing else is in the folder.
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

# 2. The block reference inputs are the ones the reference was built from.
manifest = load("manifest.json")
for name, digest in manifest["block_reference_inputs"].items():
    check(hashlib.sha256((build.BLOCK_REF / name).read_bytes()).hexdigest() == digest,
          f"block reference input changed: {name}")

# 3. Re-derive every generated file from the folder's own copies and the jar; every copy equals its jar entry.
derived = jar_checked = 0
if build.CLIENT_JAR.exists():
    check(hashlib.sha1(build.CLIENT_JAR.read_bytes()).hexdigest() == build.CLIENT_SHA1, "client jar SHA-1 differs")
    jar = zipfile.ZipFile(build.CLIENT_JAR)
    src = build.Sources(jar, lambda item: (ROOT / build.COMPONENTS / f"{item}.json").read_bytes(),
                        (ROOT / build.REGISTRY).read_bytes())
    generated, copies = build.derive(src)
    for name, data in generated.items():
        check((ROOT / name).is_file() and (ROOT / name).read_bytes() == data, f"re-derived file differs: {name}")
        derived += 1
    expected = set(generated) | set(copies) | set(build.DUMPED) | {
        build.REGISTRY, "README.md", "tools/build_reference.py", "tools/verify_reference.py", "tools/ItemFacts.java"}
    check(expected == set(sums), f"files differ from the derivation: +{sorted(set(sums) - expected)[:5]} "
                                 f"-{sorted(expected - set(sums))[:5]}")
    for name in copies:
        if name.startswith((build.ASSETS, "data/")):
            jar_checked += 1
            check((ROOT / name).is_file() and (ROOT / name).read_bytes() == jar.read(name),
                  f"differs from the jar: {name}")
else:
    print(f"note: client jar absent ({build.CLIENT_JAR}); re-derivation and jar comparisons skipped")

# 4. The report copies equal the data generator's output.
report_checked = 0
if build.DEFAULT_REPORTS.exists():
    for name in sums:
        if name.startswith(build.COMPONENTS):
            report_checked += 1
            source = build.DEFAULT_REPORTS / "minecraft/components/item" / name[len(build.COMPONENTS):]
            check(source.read_bytes() == (ROOT / name).read_bytes(), f"differs from the report: {name}")
    registry = json.loads((build.DEFAULT_REPORTS / "registries.json").read_text(encoding="utf-8"))["minecraft:item"]
    check(load(build.REGISTRY) == {"minecraft:item": registry}, "item registry differs from registries.json")
    report_checked += 1
else:
    print(f"note: report folder absent ({build.DEFAULT_REPORTS}); report comparison skipped")

# 5. Counts and spot values (scout F's appendix A, and the break-time formula's worked cases).
items = {i["id"]: i for i in load("items.json")["items"]}
index = load("item_index.json")["items"]
check(len(items) == 86 and sum(i["group"] == "tool" for i in items.values()) == 35 and
      sum(i["group"] == "spear" for i in items.values()) == 7 and sum(i["group"] == "drop" for i in items.values()) == 23,
      "expected 86 items: 35 tools, 7 spears, 23 drop items")
# APPEND_LAST: the drop items, and the models and textures only they use, follow the 63 items, 70 models and 70
# textures of the build before loot step 2b.
groups = [i["group"] for i in items.values()]
late_models = [m["id"] for m in load("models.json")["models"]][70:]
late_textures = [t["name"] for t in load("textures.json")["textures"]][70:]
check(groups[63:] == ["drop"] * 23 and "drop" not in groups[:63] and len(late_models) == 23 and
      len(late_textures) == 23 and
      not {m["model"] for i in list(items.values())[:63] for m in i["model"]["models"]} & set(late_models) and
      not {t for i in list(items.values())[:63] for t in i["textures"]} & set(late_textures),
      "drop items, models and textures are not appended last")
check([items[i]["textures"] for i in build.TIER_A["drop"] if i.endswith("_sapling")] ==
      [[f"block/{i}"] for i in build.TIER_A["drop"] if i.endswith("_sapling")] and
      items["snowball"]["max_stack"] == 16 and items["flint"]["max_stack"] == 64, "drop item spot values")
check(len(index) == 1658 and len({e["id"] for e in index}) == 1658, "expected 1,658 registry items")
check(manifest["counts"]["definition_kinds"] == {"block_model": 834, "sprite": 761, "special": 62, "other": 1},
      "definition kinds differ from scout F's count")
clashes = sorted(e["id"] for e in index if e["block_with_same_id"] and e["places_block"] != e["id"])
check(clashes == ["air", "wheat"], f"same-id items that are not their block's item: {clashes}")


def tool_speed(item):
    # The mining rule (`mineable/<kind>`) is the last rule with a speed for pickaxes, axes, shovels and hoes.
    return [r["speed"] for r in items[item]["tool"]["rules"] if "speed" in r][-1]


for material, durability, speed, enchantability in [("wooden", 59, 2.0, 15), ("stone", 131, 4.0, 5),
                                                    ("copper", 190, 5.0, 13), ("iron", 250, 6.0, 14),
                                                    ("golden", 32, 12.0, 22), ("diamond", 1561, 8.0, 10),
                                                    ("netherite", 2031, 9.0, 15)]:
    for kind in build.TOOL_KINDS:
        item = items[f"{material}_{kind}"]
        check(item["max_damage"] == durability and item["enchantability"] == enchantability and
              item["max_stack"] == 1, f"durability or enchantability: {item['id']}")
        if kind != "sword":
            check(tool_speed(item["id"]) == speed, f"mining speed: {item['id']}")
for item, damage, speed in [("netherite_sword", 8.0, 1.6), ("iron_sword", 6.0, 1.6), ("iron_axe", 9.0, 0.9),
                            ("diamond_pickaxe", 5.0, 1.2), ("wooden_shovel", 2.5, 1.0), ("diamond_hoe", 1.0, 4.0),
                            ("netherite_spear", 5.0, 0.869565)]:
    check((items[item]["attack"]["damage"], items[item]["attack"]["speed"]) == (damage, speed), f"attack: {item}")
check(items["bucket"]["max_stack"] == 16 and items["water_bucket"]["max_stack"] == 1 and
      items["bread"]["food"] == {"nutrition": 5, "saturation": 6.0}, "bucket stacks or bread food")
check(items["iron_axe"]["weapon"]["disable_blocking_for_seconds"] == 5.0 and
      items["iron_sword"]["tool"]["damage_per_block"] == 2 and
      items["iron_sword"]["tool"]["can_destroy_blocks_in_creative"] is False and
      items["golden_hoe"]["weapon"]["item_damage_per_attack"] == 2, "sword, axe and hoe tool and weapon data")
check(all(len(items[f"{m}_spear"]["textures"]) == 2 for m in build.MATERIALS), "spears: icon and in-hand textures")
textures = {t["name"]: t for t in load("textures.json")["textures"]}
check(textures["item/iron_spear_in_hand"]["size"] == [32, 32] and textures["item/stick"]["size"] == [16, 16] and
      all(len(t["mask"]) == t["size"][1] for t in textures.values()), "texture sizes and masks")

# 6. The tooltip lang keys, and ItemFacts.java's black-box facts against the data: the durability bar at every damage
# value of each max damage, and every item's tooltip lines rebuilt from items.json and lang.json.
lang = load("lang.json")["keys"]
check(lang.get("item.modifiers.mainhand") == "When in Main Hand:" and lang.get("attribute.modifier.equals.0") ==
      "%s %s" and lang.get("attribute.name.attack_speed") == "Attack Speed", "lang spot values")
bar = load("facts/durability_bar.json")["maxima"]
by_max = {}
for item in items.values():
    if item["max_damage"]:
        by_max.setdefault(item["max_damage"], []).append(item["id"])
check([m["max_damage"] for m in bar] == sorted(by_max) and all(m["items"] == by_max[m["max_damage"]] for m in bar),
      "durability bar: one entry per max damage, with its items in registry order")
for m in bar:
    width, count = m["width"], m["max_damage"] + 1
    check(len(m["visible"]) == len(width) == len(m["color"]) == count and m["visible"] == [0] + [1] * (count - 1) and
          width[0] == 13 and width[-1] == 0 and all(a >= b for a, b in zip(width, width[1:])) and
          m["color"][0] == "00ff00" and m["color"][-1] == "ff0000", f"durability bar: max damage {m['max_damage']}")


def tooltip_number(value):
    """The amount as the tooltip shows it (two decimals at most, half-even, no trailing zeros)."""
    return format(Decimal(value).quantize(Decimal("0.01"), ROUND_HALF_EVEN).normalize(), "f")


tips = {t["id"]: t["lines"] for t in load("facts/tooltips.json")["items"]}
check(list(tips) == list(items), "tooltips: every item, in registry order")
for item in items.values():
    expected = [[[item["name"], "white"]]]
    if item["attack"]:
        expected += [[], [[lang["item.modifiers." + item["attack"]["modifiers"][0]["slot"]], "gray"]]]
        for modifier in item["attack"]["modifiers"]:
            # No player in the run: the base modifiers show their own amounts (the plus and take forms), and a zero
            # amount shows no line (the diamond hoe keeps only its header).
            if modifier["amount"] == 0:
                continue
            form, colour = ("plus", "blue") if modifier["amount"] > 0 else ("take", "red")
            pieces = lang[f"attribute.modifier.{form}.{build.OPERATIONS.index(modifier['operation'])}"].split("%s")
            texts = [pieces[0], tooltip_number(abs(modifier["amount"])), pieces[1],
                     lang["attribute.name." + build.bare(modifier["type"])], pieces[2]]
            expected.append([[text, colour] for text in texts if text])
    check(tips.get(item["id"]) == expected, f"tooltip lines: {item['id']}")

breaks = load("facts/break_times.json")
tools = breaks["tools"]
rows = {r["id"]: r for r in breaks["blocks"]}
check(len(rows) == 1286 and len(tools) == 36, "break times: 1,286 blocks x (hand + 35 tools)")
for block, tool, ticks, drops in [("stone", "wooden_pickaxe", 23, 1), ("stone", "hand", 150, 0),
                                  ("iron_ore", "stone_pickaxe", 23, 1), ("iron_ore", "golden_pickaxe", 25, 0),
                                  ("deepslate_diamond_ore", "iron_pickaxe", 23, 1),
                                  ("obsidian", "diamond_pickaxe", 188, 1), ("obsidian", "iron_pickaxe", 834, 0),
                                  ("oak_leaves", "iron_hoe", 0, 1), ("dirt", "hand", 15, 1),
                                  ("deepslate", "wooden_pickaxe", 45, 1), ("bedrock", "netherite_pickaxe", -1, 0)]:
    i = tools.index(tool)
    check((rows[block]["ticks"][i], rows[block]["drops"][i]) == (ticks, drops),
          f"break time: {block} with {tool}: {rows[block]['ticks'][i]}, {rows[block]['drops'][i]}")

# 7. Loot tables: the ores' drops against minecraft.wiki's ore pages.
loot = load("loot_tables.json")
tables = {t["block"]: t for t in loot["tables"]}
full = build.loot_blocks(src, json.loads((build.BLOCK_REF / "block_manifest.json").read_text(
    encoding="utf-8"))) if derived else list(tables)
check(list(tables) == full and full[:18] == build.LOOT_BLOCKS and len(full) == 602 and len(set(full)) == 602,
      "loot tables: the 18 ores, then every other imported block with a table, in order")
check(list(loot)[-1] == "extensions" and isinstance(loot["extensions"], str), "loot extensions key last")
candidates = sum(len(p["candidates"]) for t in loot["tables"] for p in t["pools"])
check(manifest["loot"] == {"blocks": full, "tables": 602, "predicates": 2, "candidates": candidates,
                           "source": "client jar data/minecraft/loot_table/blocks and data/minecraft/predicate"} and
      list(manifest)[-1] == "loot" and len(loot["predicates"]) == 2, "manifest loot counts")
DROPS = {"coal": ("coal", 1, 1), "copper": ("raw_copper", 2, 5), "iron": ("raw_iron", 1, 1),
         "gold": ("raw_gold", 1, 1), "diamond": ("diamond", 1, 1), "emerald": ("emerald", 1, 1),
         "lapis": ("lapis_lazuli", 4, 9), "redstone": ("redstone", 4, 5)}
for ore in DROPS:
    for block in (f"{ore}_ore", f"deepslate_{ore}_ore"):
        DROPS_EXPECTED = DROPS[ore]
        t = tables.get(block)
        check(t is not None and len(t["pools"]) == 1 and len(t["pools"][0]["candidates"]) == 2, f"loot shape: {block}")
        if t is not None and len(t["pools"][0]["candidates"]) == 2:
            first, second = t["pools"][0]["candidates"]
            check(t["pools"][0]["rolls"] == 1 and first["item"] == block and
                  first["conditions"] == ["tool/can_silk_touch"], f"loot silk touch candidate: {block}")
            check((second["item"], *second["count"]) == DROPS_EXPECTED and second["conditions"] == [] and
                  second["explosion_decay"], f"loot drop: {block}")
            check(second["bonus"] == {"enchantment": "fortune", "formula": "uniform_bonus_count" if ore == "redstone"
                                      else "ore_drops", "bonus_multiplier": 1 if ore == "redstone" else 0},
                  f"loot fortune bonus: {block}")
for block, item, low, high in [("nether_quartz_ore", "quartz", 1, 1), ("nether_gold_ore", "gold_nugget", 2, 6)]:
    cands = tables[block]["pools"][0]["candidates"]
    check(len(cands) == 2 and cands[0]["item"] == block and cands[0]["conditions"] == ["tool/can_silk_touch"] and
          (cands[1]["item"], *cands[1]["count"]) == (item, low, high), f"loot: {block}")
silk = {p["name"]: p for p in loot["predicates"]}.get("tool/can_silk_touch")
check([p["name"] for p in loot["predicates"]] == ["tool/can_silk_touch", "tool/can_shear"], "predicates in first-use order")
shear = {p["name"]: p for p in loot["predicates"]}.get("tool/can_shear")
check(shear is not None and shear["items"] == ["shears"] and shear["enchantments"] == [], "can_shear predicate")
check(silk is not None and silk["type"] == "match_tool" and silk["items"] == [] and
      silk["enchantments"] == [{"id": "silk_touch", "min_level": 1}], "can_silk_touch predicate")
# Loot step 2b: every item a table drops is an imported block's item or one of the items above.
imported = {b["id"] for b in json.loads((build.BLOCK_REF / "block_manifest.json").read_text(encoding="utf-8"))["blocks"]}
undefined = sorted({c["item"] for t in loot["tables"] for p in t["pools"] for c in p["candidates"]} - imported - set(items))
check(not undefined, f"loot items neither imported blocks nor items: {undefined}")

if errors:
    print("REFERENCE FAIL")
    print("\n".join(errors[:40]))
    sys.exit(1)
partial = "" if derived else " (PARTIAL: no client jar, nothing re-derived)"
print(f"REFERENCE OK{partial}: {len(sums)} files hashed, {derived} generated files re-derived, {jar_checked} compared with "
      f"the jar, {report_checked} with the data generator's report; {len(items)} items, {len(index)} registry items, "
      f"{len(textures)} textures, break times for {len(rows)} blocks x {len(tools)} tools; {len(lang)} lang keys, "
      f"the durability bar at {sum(len(m['width']) for m in bar)} damage values, {len(tips)} tooltips; {len(tables)} loot tables, "
      f"{len(loot['predicates'])} predicates, {candidates} candidates")
