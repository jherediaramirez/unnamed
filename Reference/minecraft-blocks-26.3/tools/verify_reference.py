"""Verify Reference/minecraft-blocks-26.3: file hashes, jar bytes, counts, state ids and texture layers.

usage: uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-blocks-26.3/tools/verify_reference.py
The jar comparisons run when the pinned jars are present; everything else needs only this folder.
"""
import csv
import hashlib
import importlib.util
import json
import os
import sys
import zipfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
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

def load(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(f"{name}.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


build = load("build_reference")  # shared constants and sound_files (the build's own rule for the sound file set)
manifest = json.loads((ROOT / "block_manifest.json").read_text(encoding="utf-8"))
states = json.loads((ROOT / "block_states.json").read_text(encoding="utf-8"))
textures = json.loads((ROOT / "textures.json").read_text(encoding="utf-8"))
report = json.loads((ROOT / "reports/blocks-imported.json").read_text(encoding="utf-8"))

jar_path = Path(os.environ.get("APPDATA", "")) / "ModrinthApp/meta/versions/26.3/26.3.jar"
jar_checked = 0
if jar_path.exists():
    check(hashlib.sha1(jar_path.read_bytes()).hexdigest() == manifest["client_sha1"], "client jar SHA-1 differs")
    jar = zipfile.ZipFile(jar_path)
    for name in sums:
        if name.startswith("assets/minecraft/") and name != "assets/minecraft/sounds.json" and                 not name.startswith("assets/minecraft/sounds/"):
            jar_checked += 1
            check((ROOT / name).read_bytes() == jar.read(name), f"differs from the jar: {name}")

# Sounds come from the asset index 34 (not the jar): each file's SHA-1 is its object hash, and its size matches. The
# local index is accepted when its non-lang entries match the official one (the Modrinth copy differs in lang only).
ASSET_INDEX_NONLANG_SHA1 = build.ASSET_INDEX_NONLANG_SHA1
# REFERENCE_ASSET_INDEX overrides the index path (tests only).
index_path = Path(os.environ.get("REFERENCE_ASSET_INDEX") or
                  Path(os.environ.get("APPDATA", "")) / "ModrinthApp/meta/assets/indexes/34.json")
sounds_checked = 0
if not index_path.exists():
    print(f"SOUNDS NOT CONTENT-CHECKED: asset index 34 not found ({index_path})")
else:
    objects = json.loads(index_path.read_text(encoding="utf-8"))["objects"]
    rest = {k: v for k, v in objects.items() if not k.startswith("minecraft/lang/")}
    check(hashlib.sha1(json.dumps(rest, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
          == ASSET_INDEX_NONLANG_SHA1, "asset index 34 non-lang entries differ from the pinned ones")
    if hashlib.sha1(json.dumps(rest, sort_keys=True, separators=(",", ":")).encode()).hexdigest() != ASSET_INDEX_NONLANG_SHA1:
        print(f"SOUNDS NOT CONTENT-CHECKED: asset index 34 rejected ({index_path})")
    for name in sums:
        if name == "assets/minecraft/sounds.json" or name.startswith("assets/minecraft/sounds/"):
            entry = objects.get(name.removeprefix("assets/"))
            data = (ROOT / name).read_bytes()
            check(entry is not None and hashlib.sha1(data).hexdigest() == entry["hash"] and len(data) == entry["size"],
                  f"differs from the asset index: {name}")
            sounds_checked += 1

# The sound file set is exactly what block_sounds.csv and sounds.json require (break and hit events, `type: event`
# entries followed, intentionally_empty skipped), whatever SHA256SUMS.txt lists: catches deleted and extra files.
sound_rows = list(csv.DictReader(open(ROOT / "facts/block_sounds.csv", encoding="utf-8")))
sounds_json = json.loads((ROOT / "assets/minecraft/sounds.json").read_text(encoding="utf-8"))
events = sorted({r[k] for r in sound_rows for k in ("break", "hit")} - {"intentionally_empty"})
required_sounds = {f"assets/minecraft/sounds/{n}.ogg" for n in build.sound_files(sounds_json, events)}
present_sounds = {p.relative_to(ROOT).as_posix() for p in (ROOT / "assets/minecraft/sounds").rglob("*") if p.is_file()}
check(present_sounds == required_sounds, f"sound files differ from the required set: missing "
      f"{sorted(required_sounds - present_sounds)[:5]}, extra {sorted(present_sounds - required_sounds)[:5]}")

# Counts and state ids: every imported state is in the report with the same id and properties.
blocks = manifest["blocks"]
check(len(blocks) == 926 and [f["id"] for f in manifest["fluids"]] == ["water", "lava"],
      "expected 926 blocks + water and lava")
check(sum(b["shape"] == "full_cube" for b in blocks) == 429 and sum(b["shape"] == "slab" for b in blocks) == 101 and
      sum(b["shape"] == "layers" for b in blocks) == 1 and sum(b["shape"] == "model" for b in blocks) == 395,
      "expected 429 cubes (powder snow among them), 101 slabs, 1 layered, 395 model blocks")
models = json.loads((ROOT / "block_models.json").read_text(encoding="utf-8"))["models"]
texture_names = {t["name"] for t in textures["textures"]}
# The beds' item icons (vanilla `minecraft:composite`: head, and foot one block south) reach z = 2.
bed_icons = {i["icon"]["model"] for i in json.loads((ROOT / "items.json").read_text(encoding="utf-8"))["items"]
             if i["id"].endswith("_bed")}
for index, model in enumerate(models):
    for quad in model["quads"]:
        check(quad["t"] in texture_names and len(quad["v"]) == 4 and len(quad["uv"]) == 4 and
              all(-.5 <= c <= (2.5 if index in bed_icons and axis == 2 else 1.5)
                  for p in quad["v"] for axis, c in enumerate(p)), f"baked quad: {quad}")
state_count = 0
for block in states["blocks"]:
    entry = report["minecraft:" + block["id"]]
    expected = {s["id"]: s.get("properties", {}) for s in entry["states"]}
    got = {s["id"]: s["properties"] for s in block["states"]}
    check(expected == got, f"state ids or properties differ: {block['id']}")
    state_count += len(got)
    for state in block.get("states", []):
        if state.get("shape") == "model":
            # A wall with no post and no side draws nothing in vanilla (no multipart case applies): an empty model.
            empty_wall = block["id"].endswith("_wall") and state["properties"].get("up") == "false" and all(
                state["properties"].get(d) == "none" for d in ("north", "east", "south", "west"))
            check("faces" not in state and (state["model"] or empty_wall) and all(
                part and all(0 <= i < len(models) for i in part) for part in state["model"]),
                f"model parts: {block['id']} {state['id']}")
        else:
            check(len(state.get("faces", [None] * 6)) == 6, f"face count: {block['id']} {state['id']}")
check(state_count == 28552, f"expected 28552 states, found {state_count}")

# Per-state facts (FactsDump.java): every imported block and state, in registry order.
facts = json.loads((ROOT / "facts/block_state_facts.json").read_text(encoding="utf-8"))
check([b["id"] for b in facts["blocks"]] == [b["id"] for b in sorted(blocks + manifest["fluids"], key=lambda b: b["protocol_id"])],
      "block_state_facts.json: block list differs from the manifest")
fact_states = {s["id"] for b in facts["blocks"] for s in b["states"]}
check(fact_states == {s["id"] for b in states["blocks"] for s in b["states"]}, "block_state_facts.json: state ids differ")

# Sounds and particles: block_sounds.csv covers the imported blocks in the same order, every break/hit event is
# defined in sounds.json, and block_particles.json names a texture for every imported block and state.
check([r["id"] for r in sound_rows] == [b["id"] for b in facts["blocks"]], "block_sounds.csv: ids or order differ from block_state_facts.json")
check(all(e in sounds_json for e in events), f"sound events missing from sounds.json: {sorted(e for e in events if e not in sounds_json)[:5]}")
particles = json.loads((ROOT / "block_particles.json").read_text(encoding="utf-8"))
outside = particles["textures_outside_textures_json"]
check(list(particles["blocks"]) == [b["id"] for b in blocks + manifest["fluids"]],
      "block_particles.json: blocks differ from the imported blocks (manifest order)")
for block in states["blocks"]:
    entry = particles["blocks"].get(block["id"])
    if entry is None:
        continue
    state_ids = {s["id"] for s in block["states"]}
    check(set(map(int, entry.get("states", {}))) <= state_ids and block["default_state_id"] in state_ids,
          f"block_particles.json: state ids: {block['id']}")
    for texture in [entry["default"], *entry.get("states", {}).values()]:
        check(texture in texture_names or (texture in outside and
              (ROOT / f"assets/minecraft/textures/block/{texture}.png").is_file()),
              f"block_particles.json: texture {texture} of {block['id']} is nowhere")
check(not set(outside) & texture_names, "block_particles.json: an outside texture is in textures.json")

# Torch support (FactsDump.java): a face mask for every imported state, in registry and state order, and spot checks
# against minecraft.wiki "Torch": stone and glass hold a torch on top and on every side, leaves on none, an
# upside-down slab on top; the torch item places the wall torch.
support = json.loads((ROOT / "facts/block_support.json").read_text(encoding="utf-8"))
check(support["face_order"] == ["west", "east", "north", "south", "down", "up"] and support["probes"]["down"] is None,
      "block_support.json: face order or probes")
check([(b["id"], b["first_state"], len(b["support"])) for b in support["blocks"]] ==
      [(b["id"], b["min_state_id"], len(b["states"])) for b in sorted(states["blocks"], key=lambda b: b["protocol_id"])],
      "block_support.json: blocks or states differ from block_states.json")
masks = {b["id"]: b["support"] for b in support["blocks"]}
SIDES_AND_TOP = 0b101111
check(masks["stone"] == [SIDES_AND_TOP] and masks["glass"] == [SIDES_AND_TOP], "support: stone and glass")
check(all(m == 0 for b in blocks if "leaves" in b["tags"] for m in masks[b["id"]]), "support: leaves hold none")
slab = next(b for b in states["blocks"] if b["id"] == "oak_slab")
check(all(m & 1 << 5 for s, m in zip(slab["states"], masks["oak_slab"]) if s["properties"]["type"] == "top"),
      "support: an upside-down slab holds a torch on top")
check(all(m & 1 << 4 == 0 for ms in masks.values() for m in ms), "support: no torch hangs from a face")
check({e["standing"]: e["wall"] for e in support["items"]}.get("torch") == "wall_torch", "support: the torch's wall block")

# Plant survival (FactsDump.java): for every imported state and eight probes, the set of neighbour states that let it
# survive (a signature id), every block and state present, and spot rows from the vanilla rules: a vine under a bottom
# slab pops, a glow lichen under a top slab pops, a plant needs soil below, sugar cane needs water beside its sand,
# a mushroom needs darkness on stone, a wall fan needs its support behind, and a full cube survives anywhere.
plant = json.loads((ROOT / "facts/plant_survival.json").read_text(encoding="utf-8"))
check(sorted(plant) == ["face_order", "light", "note", "signatures", "subjects"], "plant_survival.json: keys")
check(plant["face_order"] == states["face_order"], "plant_survival.json: face order differs from block_states.json")
check(plant["light"] == 15, "plant_survival.json: light")
sig_list = plant["signatures"]
by_id = {b["id"]: b for b in states["blocks"]}
check(sig_list[0] == {}, "plant_survival.json: signature 0 is not the empty set")
for number, signature in enumerate(sig_list):
    for bid, members in signature.items():
        if bid == "air":
            check(members == "all", f"plant_survival.json: signature {number} air")
        elif bid not in by_id:
            check(False, f"plant_survival.json: signature {number} names unknown block {bid}")
        elif members != "all":
            check(isinstance(members, list) and members == sorted(set(members)) and members and 0 <= members[0]
                  and members[-1] < len(by_id[bid]["states"]), f"plant_survival.json: signature {number} states of {bid}")
# The subjects are the frozen base list (tools/survival_base_ids.txt, FactsDump's third argument); the later blocks
# are in family_survival.json.
base_ids = Path(__file__).with_name("survival_base_ids.txt").read_text(encoding="utf-8").split()
registry_order = [b["id"] for b in sorted(states["blocks"], key=lambda b: b["protocol_id"])]
check(list(plant["subjects"]) == base_ids, "plant_survival.json: subjects differ from tools/survival_base_ids.txt")
check(base_ids == [b for b in registry_order if b in set(base_ids)] and set(base_ids) <= set(by_id),
      "survival_base_ids.txt: not imported blocks in registry order")
# Pinned: the base list is exactly the manifest's blocks and fluids in registry order without the blocks appended after
# the torch (APPENDED_BLOCKS[2:], session B1); torch and wall_torch stay base.
later_ids = set(build.APPENDED_BLOCKS[2:])
check(later_ids == {b["id"] for b in manifest["blocks"] if b.get("added") == "survival-B1"},
      "APPENDED_BLOCKS[2:] differ from the manifest's survival-B1 blocks")
check(base_ids == [e["id"] for e in sorted(manifest["blocks"] + manifest["fluids"], key=lambda e: e["protocol_id"])
                   if e["id"] not in later_ids],
      "survival_base_ids.txt: not the manifest's blocks and fluids without APPENDED_BLOCKS[2:], in registry order")
for bid, rows in plant["subjects"].items():
    check(len(rows) == len(by_id[bid]["states"]), f"plant_survival.json: state count of {bid}")
    check(all(len(r) == 8 and all(isinstance(i, int) and 0 <= i < len(sig_list) for i in r) for r in rows),
          f"plant_survival.json: a row of {bid} is not 8 signature ids in range")

PROBES = {"west": 0, "east": 1, "north": 2, "south": 3, "down": 4, "up": 5, "down_water": 6, "down_dark": 7}


def state_index(bid, **props):
    wanted = {k: str(v).lower() for k, v in props.items()}
    found = [i for i, s in enumerate(by_id[bid]["states"]) if all(s["properties"].get(k) == v for k, v in wanted.items())]
    check(len(found) == 1, f"plant spot rows: {bid} {props} resolves to {len(found)} states")
    return found[0] if found else 0


def spot(bid, own, probe, other, **candidate):
    """Does the state of bid with properties `own` survive with the state `candidate` of block `other` at the probed cell?"""
    entry = sig_list[plant["subjects"][bid][state_index(bid, **own)][PROBES[probe]]].get(other)
    if entry is None or entry == "all":
        return entry == "all"
    return state_index(other, **candidate) in entry


vine = dict(up=True, north=False, east=False, south=False, west=False)
check(not spot("vine", vine, "up", "stone_slab", type="bottom", waterlogged=False) and
      spot("vine", vine, "up", "stone_slab", type="top", waterlogged=False), "plant spot: vine under a slab")
lichen = dict(vine, down=False, waterlogged=False)
check(spot("glow_lichen", lichen, "up", "stone_slab", type="bottom", waterlogged=False) and
      not spot("glow_lichen", lichen, "up", "stone_slab", type="top", waterlogged=False), "plant spot: lichen under a slab")
check(spot("dandelion", {}, "down", "grass_block", snowy=False) and not spot("dandelion", {}, "down", "stone"),
      "plant spot: dandelion needs soil below")
check(not spot("sugar_cane", dict(age=0), "down", "sand") and spot("sugar_cane", dict(age=0), "down_water", "sand"),
      "plant spot: sugar cane needs water beside its sand")
check(not spot("brown_mushroom", {}, "down", "stone") and spot("brown_mushroom", {}, "down_dark", "stone"),
      "plant spot: a mushroom survives on stone only in the dark")
fan = dict(facing="north", waterlogged=False)
check(spot("tube_coral_wall_fan", fan, "south", "stone") and not spot("tube_coral_wall_fan", fan, "north", "stone"),
      "plant spot: a wall fan needs its support behind")
check(all(spot("stone", {}, probe, "air") for probe in PROBES), "plant spot: a full cube survives with air everywhere")

# Family survival (FactsDump.java): the same rows for the rule-bearing states of every imported block (a null row is a
# state that survives alone and with stone at each neighbour), candidates = air + every imported state. Projected onto
# the base subjects and base candidates it equals plant_survival.json; a base state with a null row passes every probe
# with every base candidate there.
family = json.loads((ROOT / "facts/family_survival.json").read_text(encoding="utf-8"))
check(sorted(family) == ["face_order", "light", "note", "signatures", "subjects"] and
      family["face_order"] == states["face_order"] and family["light"] == 15 and family["signatures"][0] == {},
      "family_survival.json: keys, face order, light or signature 0")
fam_sigs = family["signatures"]
for number, signature in enumerate(fam_sigs):
    for bid, members in signature.items():
        check(bid == "air" and members == "all" or bid in by_id and (members == "all" or (
              isinstance(members, list) and members == sorted(set(members)) and members and 0 <= members[0]
              and members[-1] < len(by_id[bid]["states"]))), f"family_survival.json: signature {number} entry {bid}")
check(list(family["subjects"]) == [b for b in registry_order if b in family["subjects"]],
      "family_survival.json: subjects are not imported blocks in registry order")
family_states = 0
for bid, rows in family["subjects"].items():
    check(bid in by_id and len(rows) == len(by_id[bid]["states"]) and any(r is not None for r in rows) and all(
          r is None or len(r) == 8 and all(isinstance(i, int) and 0 <= i < len(fam_sigs) for i in r) for r in rows),
          f"family_survival.json: rows of {bid}")
    family_states += sum(r is not None for r in rows)
check(family_states == 3472, f"family_survival.json: expected 3472 subject states, found {family_states}")
base_candidates = {"air", *base_ids}
everything = {b: "all" for b in base_candidates}
for bid, rows in plant["subjects"].items():
    frows = family["subjects"].get(bid) or [None] * len(rows)
    for index, (row, frow) in enumerate(zip(rows, frows)):
        if frow is None:
            check(all(sig_list[i] == everything for i in row),
                  f"family_survival.json: {bid}[{index}] has a null row but rules in plant_survival.json")
        else:
            check(all({b: v for b, v in fam_sigs[f].items() if b in base_candidates} == sig_list[p]
                      for f, p in zip(frow, row)), f"family_survival.json: {bid}[{index}] differs from plant_survival.json")

# Items: block items for every block whose vanilla item has its id; model blocks' icons resolve.
items = json.loads((ROOT / "items.json").read_text(encoding="utf-8"))["items"]
for item in items:
    icon = item.get("icon")
    if icon and "sprite" in icon:
        for layer in icon["sprite"]:
            name = layer["texture"]
            check(f"assets/minecraft/textures/{name}.png" in sums if name.startswith("item/") else
                  name.removeprefix("block/") in texture_names, f"icon sprite: {item['id']} {name}")
    elif icon:
        check(0 <= icon["model"] < len(models), f"icon model: {item['id']}")
# Each of the 16 bed icons is its own model: 30 quads, one block wide, two long (head north, foot south), 9/16 high.
beds = [i for i in items if i["id"].endswith("_bed")]
check(len(beds) == 16 and len({i["icon"]["model"] for i in beds}) == 16 and all(
    len(models[i["icon"]["model"]]["quads"]) == 30 and
    [[f(c[a] for q in models[i["icon"]["model"]]["quads"] for c in q["v"]) for f in (min, max)] for a in range(3)] ==
    [[0, 1], [0, .5625], [0, 2]] for i in beds), "bed icons: 16 models of 30 quads spanning [0,1]x[0,9/16]x[0,2]")
check(len(items) == 887 and sum("model" in i.get("icon", {}) for i in items) == 241 and
      sum("sprite" in i.get("icon", {}) for i in items) == 117, "expected 887 items, 241 3D icons, 117 sprite icons")
# Items the item reference owns (build_reference.ITEMS_REFERENCE_ITEMS: the nine saplings) are not here: no item id is
# in both references.
items_ref_ids = {i["id"] for i in json.loads((ROOT.parent / "minecraft-items-26.3/items.json").read_text(
    encoding="utf-8"))["items"]}
shared = sorted(items_ref_ids & {i["id"] for i in items})
check(not shared, f"items.json: ids also in the item reference: {shared[:5]}")
check(build.ITEMS_REFERENCE_ITEMS <= items_ref_ids and len(build.ITEMS_REFERENCE_ITEMS) == 9,
      "ITEMS_REFERENCE_ITEMS: not nine items of the item reference")

# Use sounds (FactsDump.java): every door, trapdoor, button, pressure plate, fence gate and the lever names events
# defined in sounds.json (open/close or click_on/click_off).
use_rows = list(csv.DictReader(open(ROOT / "facts/block_use_sounds.csv", encoding="utf-8")))
check(len(use_rows) == 90 and len({r["id"] for r in use_rows}) == 90 and all(r["id"] in by_id for r in use_rows),
      "block_use_sounds.csv: expected 90 distinct imported blocks")
use_events = {r[k] for r in use_rows for k in ("open", "close", "click_on", "click_off") if r[k]}
check(use_events <= set(sounds_json), f"block_use_sounds.csv: events missing from sounds.json: "
                                      f"{sorted(use_events - set(sounds_json))[:5]}")
check(all(bool(r["open"]) == bool(r["close"]) != bool(r["click_on"]) == bool(r["click_off"]) for r in use_rows),
      "block_use_sounds.csv: a row needs either open and close or click_on and click_off")

# Shape updates (FactsDump.java dumpShapeUpdates): BlockState.updateShape answers for stairs, fences, walls, fence gates,
# panes and bars, settled over at most 3 passes. Every state id is an imported state (or 0, air); spot rows pinned.
shapes = json.loads((ROOT / "facts/shape_updates.json").read_text(encoding="utf-8"))
check(list(shapes) == ["format", "direction_order", "max_passes", "subjects", "tag_mismatches", "single_neighbour",
                       "cross_family", "wall_up", "stairs", "gates_in_wall"] and
      shapes["direction_order"] == ["down", "up", "north", "south", "west", "east"] and shapes["max_passes"] == 3 and
      shapes["tag_mismatches"] == [] and list(shapes["stairs"]) == ["single", "pairs"],
      "shape_updates.json: top-level keys, direction order, passes or tag mismatches")
check({k: len(v) for k, v in shapes["subjects"].items()} == {"stairs": 97, "fence": 14, "wall": 32, "fence_gate": 13,
                                                              "bars": 26}, "shape_updates.json: subject counts by kind")
shape_subjects = [b for kind in shapes["subjects"].values() for b in kind]
check(len(shape_subjects) == 182 and len(set(shape_subjects)) == 182 and set(shape_subjects) <= set(by_id),
      "shape_updates.json: 182 distinct imported subjects")
state_of = {s["id"]: (b["id"], s["properties"]) for b in states["blocks"] for s in b["states"]}
valid_ids = set(state_of) | {0}
shape_ids, shape_runs = [], []


def shape_group(group):
    check(sorted(group) == ["m", "r"], "shape_updates.json: a group's keys are r and m")
    for result, runs in group["r"].items():
        shape_ids.append(int(result))
        shape_runs.extend(runs)
    shape_runs.extend(group["m"])


for rep, entry in shapes["single_neighbour"].items():
    check(rep in shape_subjects and entry["state"] == by_id[rep]["default_state_id"],
          f"shape_updates.json: single_neighbour {rep} is not a subject's default state")
    for d in shapes["direction_order"]:
        shape_group(entry[d])
check(list(shapes["cross_family"]["subjects"]) == [b for b in registry_order if b in set(shape_subjects)],
      "shape_updates.json: cross_family subjects are not the subjects in registry order")
shape_ids.extend(shapes["cross_family"]["samples"])
for entry in shapes["cross_family"]["subjects"].values():
    for value in entry.values():
        shape_group(value) if isinstance(value, dict) else shape_ids.append(value)
for subject, entry in shapes["stairs"]["single"].items():
    shape_ids.append(int(subject))
    for group in entry.values():
        shape_group(group)
for sides, above, result, _ in shapes["wall_up"]:
    shape_ids.extend((above, result))
for subject, _, main, _, side, result, _ in shapes["stairs"]["pairs"]:
    shape_ids.extend((subject, main, side, result))
for subject, _, neighbour, result, _ in shapes["gates_in_wall"]:
    shape_ids.extend((subject, neighbour, result))
check(all(a <= b for a, b in shape_runs) and all(i in valid_ids for a, b in shape_runs for i in range(a, b + 1)) and
      all(i in valid_ids for i in shape_ids), "shape_updates.json: a state id is not an imported state id or air")
# Section sizes, and each single-neighbour group covers air and every imported state exactly once (m within them).
check(len(shapes["single_neighbour"]) == 7 and len(shapes["cross_family"]["samples"]) == 195 and
      len(shapes["wall_up"]) == 32 and len(shapes["stairs"]["single"]) == 8 and len(shapes["stairs"]["pairs"]) == 512 and
      len(shapes["gates_in_wall"]) == 72, "shape_updates.json: section sizes (7, 195, 32, 8, 512, 72)")
def expand_runs(runs):
    """The state ids of [[first, last], ...] runs, as FactsDump writes both "r" and "m"."""
    return [i for a, b in runs for i in range(a, b + 1)]


def covers_once(group, ids):
    """A single-neighbour group's "r" runs cover `ids` exactly once, and its "m" runs (the cases that needed a second
    changing pass) lie within them."""
    covered = [i for runs in group["r"].values() for i in expand_runs(runs)]
    return len(covered) == len(ids) and set(covered) == ids and set(expand_runs(group["m"])) <= ids


# Self-test on synthetic groups (Codex review 16, #1): a non-empty "m" is expanded as "r" is, not hashed as lists.
check(covers_once({"r": {"0": [[0, 1]], "5": [[2, 3]]}, "m": [[1, 2]]}, {0, 1, 2, 3}) and
      not covers_once({"r": {"0": [[0, 3]]}, "m": [[3, 4]]}, {0, 1, 2, 3}) and
      not covers_once({"r": {"0": [[0, 2]], "5": [[2, 3]]}, "m": []}, {0, 1, 2, 3}),
      "verify_reference.py: the single-neighbour coverage self-test fails")
for rep, entry in shapes["single_neighbour"].items():
    for d in shapes["direction_order"]:
        check(covers_once(entry[d], valid_ids),
              f"shape_updates.json: single_neighbour {rep} {d} does not cover air and every state exactly once")


def state_id(bid, **props):
    """The imported state of `bid` with its default properties overridden by `props`."""
    wanted = {**state_of[by_id[bid]["default_state_id"]][1], **props}
    return next(s["id"] for s in by_id[bid]["states"] if s["properties"] == wanted)


def shape_answer(group, neighbour):
    return next((int(r) for r, runs in group["r"].items() for a, b in runs if a <= neighbour <= b), None)


single = shapes["single_neighbour"]
default = {b: by_id[b]["default_state_id"] for b in ("stone", "glass", "oak_fence", "nether_brick_fence", "spruce_fence",
                                                     "cobblestone_wall")}
check(shape_answer(single["oak_fence"]["east"], default["stone"]) == state_id("oak_fence", east="true") and
      shape_answer(single["oak_fence"]["east"], default["spruce_fence"]) == state_id("oak_fence", east="true"),
      "shape spot: oak_fence connects east to stone and to spruce_fence")
check(shape_answer(single["oak_fence"]["east"], default["nether_brick_fence"]) == default["oak_fence"] and
      shape_answer(single["cobblestone_wall"]["east"], default["oak_fence"]) == default["cobblestone_wall"],
      "shape spot: oak_fence ignores nether_brick_fence, cobblestone_wall ignores oak_fence")
check(shape_answer(single["glass_pane"]["east"], default["glass"]) == state_id("glass_pane", east="true"),
      "shape spot: glass_pane connects east to glass")
wall_up = {(tuple(sides), above): result for sides, above, result, _ in shapes["wall_up"]}
check(wall_up[("north",), 0] == state_id("cobblestone_wall", north="low") and
      wall_up[("north",), default["stone"]] == state_id("cobblestone_wall", north="tall") and
      wall_up[("north", "south"), 0] == state_id("cobblestone_wall", north="low", south="low", up="false"),
      "shape spot: a wall side is low under air, tall under stone; a straight wall drops its post")
oak_north = state_id("oak_stairs", facing="north")
check(oak_north == state_id("oak_stairs", facing="north", half="bottom", shape="straight", waterlogged="false") and
      shape_answer(shapes["stairs"]["single"][str(oak_north)]["north"], state_id("oak_stairs", facing="east")) ==
      state_id("oak_stairs", facing="north", shape="outer_right") and
      shape_answer(shapes["stairs"]["single"][str(oak_north)]["south"], state_id("oak_stairs", facing="east")) ==
      state_id("oak_stairs", facing="north", shape="inner_right"), "shape spot: oak_stairs outer and inner corners")
gate = state_id("oak_fence_gate", facing="north", in_wall="true", open="false", powered="false")
gates = {(s, tuple(sides), n): r for s, sides, n, r, _ in shapes["gates_in_wall"]}
check(gates[gate, ("west",), default["cobblestone_wall"]] == gate and
      gates[gate, ("west",), default["stone"]] == state_id("oak_fence_gate", facing="north", in_wall="false"),
      "shape spot: a fence gate stays in_wall beside a wall, leaves it beside stone")
# FactsDump's own cross-check: bottom straight dry oak_stairs turn into 16 outer and 16 inner corners with a straight dry
# bottom stair neighbour (160 each with any oak or cobblestone stair neighbour).
corners = {"outer": [0, 0], "inner": [0, 0]}
for subject, entry in shapes["stairs"]["single"].items():
    if state_of[int(subject)][1]["half"] != "bottom":
        continue
    for group in entry.values():
        for result, runs in group["r"].items():
            kind = state_of[int(result)][1]["shape"].split("_")[0]
            for a, b in runs:
                for n in range(a, b + 1) if kind in corners else ():
                    p = state_of[n][1]
                    corners[kind][1] += 1
                    corners[kind][0] += p["shape"] == "straight" and p["waterlogged"] == "false" and p["half"] == "bottom"
check(corners == {"outer": [16, 160], "inner": [16, 160]}, f"shape_updates.json: stairs corner counts {corners}")


# Appended blocks (build_reference.APPENDED_BLOCKS): the textures and models only they use are the last ones, so a
# later block never moves an earlier texture index, array layer or model index.
appended = set(build.APPENDED_BLOCKS)
check(appended <= {b["id"] for b in blocks}, "APPENDED_BLOCKS are imported")
texture_order = [t["name"] for t in textures["textures"]]


def used_by(ids):
    names, indices = set(), set()
    for block in states["blocks"]:
        if block["id"] not in ids:
            continue
        for state in block["states"]:
            for face in state.get("faces", []):
                names |= {face["t"], face.get("overlay", {}).get("t")}
            for part in state.get("model", []):
                indices |= set(part)
    for item in items:
        if item["block"] in ids and "model" in item.get("icon", {}):
            indices.add(item["icon"]["model"])
        if item["block"] in ids:
            names |= {l["texture"].removeprefix("block/") for l in item.get("icon", {}).get("sprite", [])
                      if l["texture"].startswith("block/")}
    for b in manifest["fluids"]:
        if b["id"] in ids:
            names |= set(b["textures"].values())
    names |= {models[i]["quads"][q]["t"] for i in indices for q in range(len(models[i]["quads"]))}
    return names, indices


base_names, base_models = used_by({b["id"] for b in blocks + manifest["fluids"]} - appended)
new_names, new_models = used_by(appended)
new_names, new_models = new_names - base_names, new_models - base_models
check(sorted(texture_order.index(n) for n in new_names) == list(range(len(texture_order) - len(new_names), len(texture_order))),
      f"appended textures are not the last ones: {sorted(new_names)}")
check(sorted(new_models) == list(range(len(models) - len(new_models), len(models))),
      f"appended models are not the last ones: {sorted(new_models)}")
# The accepted models (tools/frozen_models.txt) are unchanged at their indices; later batches only follow them.
frozen_models = build.read_frozen_models()
check(len(frozen_models) >= 4867, f"{build.FROZEN_MODELS} is missing or shorter than survival B1's 4,867 models")
check(len(models) >= len(frozen_models) and
      all(build.model_digest(models[i]) == d for i, d in enumerate(frozen_models)),
      f"{build.FROZEN_MODELS} is not a prefix of block_models.json's models")

# Re-bake every model state from the jar's models and compare (needs the client jar).
rebaked = 0
if jar_path.exists():
    bake = load("bake_models")
    jar_models = build.Models(jar)
    build.stand_in_models(jar_models)  # the chests' stand-in boxes
    baker = bake.Baker(build, jar_models)
    model_ids = {b["id"] for b in blocks if b["shape"] == "model"}
    for block in states["blocks"]:
        if block["id"] not in model_ids:
            continue
        blockstate = build.stand_in_blockstate(block["id"]) if block["id"] in build.STAND_IN_SHEETS else json.loads(
            jar.read(f"assets/minecraft/blockstates/{block['id']}.json"))
        tints = build.TINT_INDICES.get(block["id"], frozenset({0})) if block["id"] in build.TINT_SOURCES else frozenset()
        for state in block["states"]:
            parts = baker.bake_state(blockstate, state["properties"], tints)
            check([[baker.baked[i] for i in part] for part in parts] ==
                  [[models[i] for i in part] for part in state["model"]], f"re-bake differs: {block['id']} {state['id']}")
            rebaked += 1

# Chest stand-ins: the 18 tiles under stand_ins/ are hashed, one per textures.json entry with a `file`, and (with the
# client jar) re-deriving each from the chest entity sheets (build_reference.stand_in_pixels) gives the same pixels and
# the same derivation record.
derived = {t["name"]: t for t in textures["textures"] if "file" in t}
check(len(derived) == 18 and {n for n in sums if n.startswith("stand_ins/")} == {t["file"] for t in derived.values()}
      and all(t["file"] == f"stand_ins/{n}.png" and build.stand_in_tile(n) for n, t in derived.items()),
      "stand_ins/: files differ from the textures.json entries with `file`")
stand_ins_checked = 0
if jar_path.exists():
    for name, texture in derived.items():
        tile, record = build.stand_in_pixels(jar, *build.stand_in_tile(name))
        saved = Image.open(ROOT / texture["file"]).convert("RGBA")
        check(tile.size == saved.size and tile.tobytes() == saved.tobytes() and
              json.loads(json.dumps(record)) == texture["derived_from"], f"stand-in re-derivation differs: {name}")
        stand_ins_checked += 1

# Texture layers: decoding every PNG reproduces the recorded per-frame RGBA hashes.
layers = {"blocks": 0, "water_flow": 0}
for texture in textures["textures"]:
    img = Image.open(ROOT / texture.get("file", f"assets/minecraft/textures/block/{texture['name']}.png")).convert("RGBA")
    fw, fh = texture["frame_size"]
    for index, digest in enumerate(texture["layer_rgba_sha256"]):
        frame = img.crop((0, index * fh, fw, (index + 1) * fh))
        check(hashlib.sha256(frame.tobytes()).hexdigest() == digest, f"layer hash: {texture['name']} {index}")
    check(texture["first_layer"] == layers[texture["array"]], f"layer order: {texture['name']}")
    layers[texture["array"]] += texture["frame_count"]
check(layers == {k: v["layers"] for k, v in textures["arrays"].items()}, f"layer totals {layers}")
# Budgets: face codes hold a texture index in 10 bits (index 0 is the importer's white texture); the block array
# holds every frame plus the white layer, within Unreal's 2,048-slice texture array limit.
TEXTURE_CODES, ARRAY_SLICES = 1024, 2048
check(len(textures["textures"]) + 1 <= TEXTURE_CODES, "texture indices overflow the 10-bit face code field")
check(layers["blocks"] + 1 <= ARRAY_SLICES, "block layers overflow the texture array")

if errors:
    print("REFERENCE FAIL")
    print("\n".join(errors[:40]))
    sys.exit(1)
print(f"REFERENCE OK: {len(sums)} files hashed, {jar_checked} compared with the jar, {stand_ins_checked} stand-ins re-derived, {sounds_checked} with the asset index, {len(blocks)} blocks + water and lava, "
      f"{state_count} states ({rebaked} model states re-baked), {layers['blocks']} block layers + {layers['water_flow']} "
      f"flow layers; budgets: {len(textures['textures']) + 1} of {TEXTURE_CODES} texture codes, "
      f"{layers['blocks'] + 1} of {ARRAY_SLICES} array slices")
