"""Verify Reference/minecraft-items-26.3: file hashes, re-derivation of every generated file, copies, counts, spot values.

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
    expected = set(generated) | set(copies) | {build.REGISTRY, "README.md", "tools/build_reference.py",
                                                "tools/verify_reference.py"}
    check(expected == set(sums), f"files differ from the derivation: +{sorted(set(sums) - expected)[:5]} "
                                 f"-{sorted(expected - set(sums))[:5]}")
    for name in copies:
        if name.startswith(build.ASSETS):
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
check(len(items) == 63 and sum(i["group"] == "tool" for i in items.values()) == 35 and
      sum(i["group"] == "spear" for i in items.values()) == 7, "expected 63 Tier A items: 35 tools, 7 spears")
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

if errors:
    print("REFERENCE FAIL")
    print("\n".join(errors[:40]))
    sys.exit(1)
partial = "" if derived else " (PARTIAL: no client jar, nothing re-derived)"
print(f"REFERENCE OK{partial}: {len(sums)} files hashed, {derived} generated files re-derived, {jar_checked} compared with "
      f"the jar, {report_checked} with the data generator's report; {len(items)} items, {len(index)} registry items, "
      f"{len(textures)} textures, break times for {len(rows)} blocks x {len(tools)} tools")
