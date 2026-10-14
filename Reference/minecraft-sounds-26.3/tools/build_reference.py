"""Build Reference/minecraft-sounds-26.3: the sound events the game plays beyond the block break/hit/place sounds, with
the .ogg files the block reference does not already hold.

usage (from the repository root):
    uv run --no-project --python 3.13 python -I Reference/minecraft-sounds-26.3/tools/build_reference.py

Inputs (read-only): asset index 34 (pinned below; sounds.json and the .ogg objects) and Reference/minecraft-blocks-26.3
(its sounds.json copy, facts/block_sounds.csv and assets/minecraft/sounds/). Nothing is copied from the block reference;
files it already has are referenced ("home": "minecraft-blocks-26.3"). sounds.json is not duplicated here.

The folder must hold only tools/ and README.md, so an accepted reference is never overwritten; to rebuild, move the old
folder away first (keep tools/own_files.txt). Every generated file comes from derive(), which verify_reference.py runs again without the asset index.
Data only: the game code is never read.
"""
import csv
import hashlib
import io
import json
import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BLOCKS = ROOT.parent / "minecraft-blocks-26.3"
OWN, BLOCK_HOME = "minecraft-sounds-26.3", "minecraft-blocks-26.3"
SOUNDS = "assets/minecraft/sounds/"
# Asset index 34 (the 26.3 client's assetIndex). Modrinth's local copy differs from Mojang's published index (SHA-1
# below) in its minecraft/lang/ entries only, so the pin is on the rest of the entries: SHA-1 of their canonical JSON
# (sorted keys, compact separators).
ASSET_INDEX_SHA1 = "abfaa525f923f807df8b4e4d29c1b5e3a104adbe"
ASSET_INDEX_NONLANG_SHA1 = "9ba09e4fc9e2245b7395eee4732096038d9f6e2d"
ASSET_DIR = Path(os.environ.get("APPDATA", "")) / "ModrinthApp/meta/assets"
SOUNDS_JSON = "minecraft/sounds.json"

NAMED = ["entity.item.pickup", "entity.item.break", "entity.generic.eat", "entity.player.burp", "entity.player.hurt",
         "entity.player.hurt_drown", "entity.player.hurt_freeze", "entity.player.hurt_on_fire",
         "entity.player.hurt_sweet_berry_bush", "entity.player.death", "entity.player.small_fall",
         "entity.player.big_fall", "item.axe.strip", "block.furnace.fire_crackle", "block.barrel.open",
         "block.barrel.close", "block.chest.open", "block.chest.close", "entity.player.splash",
         "entity.player.splash.high_speed", "entity.player.swim"]
# Append-only from here (README, "Appending"): no event or file index of an accepted build may shift.
# The block sound types of the first build (86): their step and fall events keep their places; types that
# block_sounds.csv gains later go after every earlier event (step, then fall, each sorted).
SOUND_TYPE_BASE = ["block.amethyst_block", "block.amethyst_cluster", "block.ancient_debris", "block.azalea",
                   "block.azalea_leaves", "block.bamboo", "block.bamboo_wood", "block.basalt", "block.big_dripleaf",
                   "block.bone_block", "block.calcite", "block.cave_vines", "block.cherry_leaves", "block.cherry_wood",
                   "block.cinnabar", "block.copper", "block.copper_bulb", "block.copper_grate", "block.coral_block",
                   "block.creaking_heart", "block.deepslate", "block.deepslate_bricks", "block.deepslate_tiles",
                   "block.dripstone_block", "block.flowering_azalea", "block.froglight", "block.gilded_blackstone",
                   "block.glass", "block.grass", "block.gravel", "block.hanging_roots", "block.iron",
                   "block.leaf_litter", "block.lodestone", "block.mangrove_roots", "block.metal", "block.moss",
                   "block.moss_carpet", "block.mud", "block.mud_bricks", "block.muddy_mangrove_roots",
                   "block.nether_bricks", "block.nether_gold_ore", "block.nether_ore", "block.nether_wood",
                   "block.netherite_block", "block.netherrack", "block.nylium", "block.packed_mud", "block.pink_petals",
                   "block.pointed_dripstone", "block.polished_deepslate", "block.polished_tuff", "block.poplar_leaves",
                   "block.potent_sulfur", "block.powder_snow", "block.resin", "block.resin_bricks", "block.rooted_dirt",
                   "block.sand", "block.sculk", "block.sculk_catalyst", "block.sculk_vein", "block.shelf_mushroom",
                   "block.shroomlight", "block.slime_block", "block.small_dripleaf", "block.snow", "block.soul_sand",
                   "block.soul_soil", "block.sponge", "block.spore_blossom", "block.stem", "block.stone",
                   "block.sulfur", "block.sulfur_spike", "block.suspicious_gravel", "block.suspicious_sand",
                   "block.tuff", "block.tuff_bricks", "block.vine", "block.wart_block", "block.wet_grass",
                   "block.wet_sponge", "block.wood", "block.wool"]
APPENDED_T5 = ["block.bamboo_wood_button.click_off", "block.bamboo_wood_button.click_on",
               "block.bamboo_wood_door.close", "block.bamboo_wood_door.open", "block.bamboo_wood_fence_gate.close",
               "block.bamboo_wood_fence_gate.open", "block.bamboo_wood_pressure_plate.click_off",
               "block.bamboo_wood_pressure_plate.click_on", "block.bamboo_wood_trapdoor.close",
               "block.bamboo_wood_trapdoor.open", "block.cherry_wood_button.click_off",
               "block.cherry_wood_button.click_on", "block.cherry_wood_door.close", "block.cherry_wood_door.open",
               "block.cherry_wood_fence_gate.close", "block.cherry_wood_fence_gate.open",
               "block.cherry_wood_pressure_plate.click_off", "block.cherry_wood_pressure_plate.click_on",
               "block.cherry_wood_trapdoor.close", "block.cherry_wood_trapdoor.open", "block.copper_door.close",
               "block.copper_door.open", "block.copper_trapdoor.close", "block.copper_trapdoor.open",
               "block.fence_gate.close", "block.fence_gate.open", "block.iron_door.close", "block.iron_door.open",
               "block.iron_trapdoor.close", "block.iron_trapdoor.open", "block.metal_pressure_plate.click_off",
               "block.metal_pressure_plate.click_on", "block.nether_wood_button.click_off",
               "block.nether_wood_button.click_on", "block.nether_wood_door.close", "block.nether_wood_door.open",
               "block.nether_wood_fence_gate.close", "block.nether_wood_fence_gate.open",
               "block.nether_wood_pressure_plate.click_off", "block.nether_wood_pressure_plate.click_on",
               "block.nether_wood_trapdoor.close", "block.nether_wood_trapdoor.open", "block.stone_button.click_off",
               "block.stone_button.click_on", "block.stone_pressure_plate.click_off",
               "block.stone_pressure_plate.click_on", "block.wooden_button.click_off", "block.wooden_button.click_on",
               "block.wooden_door.close", "block.wooden_door.open", "block.wooden_pressure_plate.click_off",
               "block.wooden_pressure_plate.click_on", "block.wooden_trapdoor.close", "block.wooden_trapdoor.open",
               "block.lever.click", "item.hoe.till", "item.shovel.flatten", "item.crop.plant"]
APPENDED_BATCH = ["block.smoker.smoke", "entity.experience_orb.pickup", "entity.player.levelup",
                  "item.armor.equip_leather", "item.armor.equip_chain", "item.armor.equip_iron",
                  "item.armor.equip_gold", "item.armor.equip_diamond", "item.armor.equip_netherite",
                  "item.armor.equip_generic", "item.armor.equip_turtle", "item.shield.block", "item.shield.break",
                  "entity.arrow.shoot", "entity.arrow.hit", "entity.arrow.hit_player", "item.bucket.fill",
                  "item.bucket.empty", "item.bucket.fill_lava", "item.bucket.empty_lava", "entity.generic.drink",
                  "entity.generic.extinguish_fire", "block.fire.extinguish", "block.lava.pop", "block.lava.ambient",
                  "entity.generic.burn", "block.lava.extinguish", "entity.cow.ambient", "entity.cow.hurt",
                  "entity.cow.death", "entity.cow.step", "entity.player.attack.strong", "entity.player.attack.weak",
                  "entity.player.attack.knockback", "entity.player.attack.crit", "entity.player.attack.nodamage",
                  "entity.player.attack.sweep", "entity.generic.hurt", "entity.generic.death",
                  "entity.generic.big_fall", "entity.generic.small_fall", "entity.pig.ambient", "entity.pig.hurt",
                  "entity.pig.death", "entity.pig.step", "entity.sheep.ambient", "entity.sheep.hurt",
                  "entity.sheep.death", "entity.sheep.step", "entity.chicken.ambient", "entity.chicken.hurt",
                  "entity.chicken.death", "entity.chicken.step", "entity.chicken.egg", "entity.zombie.ambient",
                  "entity.zombie.hurt", "entity.zombie.death", "entity.zombie.step", "entity.spider.ambient",
                  "entity.spider.hurt", "entity.spider.death", "entity.spider.step", "entity.creeper.hurt",
                  "entity.creeper.death", "entity.creeper.primed", "entity.skeleton.ambient", "entity.skeleton.hurt",
                  "entity.skeleton.death", "entity.skeleton.step", "entity.skeleton.shoot", "entity.generic.explode",
                  "entity.sheep.shear", "entity.cow.milk", "entity.generic.splash", "entity.generic.swim"]
# T5's events are exactly the sounds.json events this pattern matches (sorted), then four by name.
T5_PATTERN = r"^block\.([a-z_]+_)?(door|trapdoor|fence_gate|button|pressure_plate)\.(open|close|click_on|click_off)$"
# B0, the program's sound batch (2026-10-14): T5's events, then the program's sounds-batch.md
# in its order (block.blast_furnace.fire_crackle has no sounds.json entry: silent in 26.3, left out).
APPENDED_NAMED = APPENDED_T5 + APPENDED_BATCH
# Files homed here stay homed here even if the block reference later gains a copy (written by the first build that
# lacks it, then an input).
OWN_FILES = "tools/own_files.txt"
ENTRY_KEYS = {"name", "volume", "pitch", "attenuation_distance"}
GENERATED = ["sound_events.json"]
TOOLS = ["tools/build_reference.py", "tools/verify_reference.py", OWN_FILES]


def dumps(value):
    return (json.dumps(value, indent=1) + "\n").encode("utf-8")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def sha1(data):
    return hashlib.sha1(data).hexdigest()


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


def oggs(root):
    """{name: (sha1, size)} of the .ogg files under root/assets/minecraft/sounds/."""
    base = root / SOUNDS
    if not base.is_dir():
        return {}
    return {p.relative_to(base).with_suffix("").as_posix(): (sha1(p.read_bytes()), p.stat().st_size)
            for p in sorted(base.rglob("*.ogg"))}


def event_files(event, sounds):
    """The .ogg names one event plays (entries checked: no type event, only known keys, one attenuation distance)."""
    names, attenuation = [], set()
    for sound in sounds:
        entry = {"name": sound} if isinstance(sound, str) else sound
        if not isinstance(entry, dict) or "name" not in entry:
            raise SystemExit(f"{event}: odd sounds entry {sound!r}")
        if set(entry) - ENTRY_KEYS:
            raise SystemExit(f"{event}: sounds entry with unknown keys (type event?): {sound!r}")
        attenuation.add(entry.get("attenuation_distance"))
        name = entry["name"].removeprefix("minecraft:")
        if name not in names:
            names.append(name)
    if len(attenuation) > 1:
        raise SystemExit(f"{event}: entries disagree on attenuation_distance: {sorted(map(str, attenuation))}")
    return names


def event_order(sounds_json, csv_bytes):
    """[(event, group)]: NAMED, the base step and fall events, APPENDED_NAMED, then step and fall events of new types."""
    rows = list(csv.DictReader(io.StringIO(csv_bytes.decode("utf-8"), newline="")))
    order = [(n, "named") for n in NAMED]
    added = []
    for column in ("step", "fall"):
        values = {r[column] for r in rows} - {"intentionally_empty"}
        base = [f"{t}.{column}" for t in SOUND_TYPE_BASE]
        if not set(base) <= values:
            raise SystemExit(f"block_sounds.csv lost {column} events: {sorted(set(base) - values)[:5]}")
        order += [(v, column) for v in base]
        added += [(v, column) for v in sorted(values - set(base))]
    pattern = re.compile(T5_PATTERN)
    if APPENDED_T5[:-4] != sorted(k for k in sounds_json if pattern.match(k)):
        raise SystemExit("APPENDED_T5 differs from the sounds.json events T5_PATTERN matches")
    return order + [(n, "named") for n in APPENDED_NAMED] + added


def derive(sounds_json_bytes, csv_bytes, blocks_oggs, own_info, source, frozen_own=()):
    """sound_events.json's bytes. `blocks_oggs` is {name: (sha1, size)} of the block reference's files, `own_info(name)`
    returns (sha1, size) of a file homed here, `source` is the `source` object (pins and hashes), `frozen_own` the names
    in tools/own_files.txt (homed here whatever the block reference holds)."""
    sounds_json = json.loads(sounds_json_bytes)
    events, files, seen = [], [], set()
    for name, group in event_order(sounds_json, csv_bytes):
        if name not in sounds_json:
            raise SystemExit(f"sounds.json has no event {name}")
        sounds = sounds_json[name]["sounds"]
        events.append({"name": name, "group": group, "subtitle": sounds_json[name].get("subtitle"), "sounds": sounds})
        for file in event_files(name, sounds):
            if file in seen:
                continue
            seen.add(file)
            home = BLOCK_HOME if file in blocks_oggs and file not in frozen_own else OWN
            digest, size = blocks_oggs[file] if home == BLOCK_HOME else own_info(file)
            files.append({"name": file, "sha1": digest, "size": size, "home": home})
    return dumps({"source": source, "events": events, "files": files})


def source_object(sounds_json_sha1, csv_bytes):
    return {"asset_index": "34", "asset_index_sha1": ASSET_INDEX_SHA1,
            "asset_index_nonlang_sha1": ASSET_INDEX_NONLANG_SHA1, "sounds_json_sha1": sounds_json_sha1,
            "block_sounds_csv_sha256": sha256(csv_bytes)}


def read_own_files(root=ROOT):
    """tools/own_files.txt's names in order, or None when absent."""
    path = root / OWN_FILES
    return path.read_text(encoding="utf-8").splitlines() if path.is_file() else None


def own_names(events_bytes):
    """The names homed here in sound_events.json's files order."""
    return [f["name"] for f in json.loads(events_bytes)["files"] if f["home"] == OWN]


def write_sums():
    files = sorted(p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*")
                   if p.is_file() and p.name != "SHA256SUMS.txt" and "__pycache__" not in p.parts)
    (ROOT / "SHA256SUMS.txt").write_text(
        "".join(f"{sha256((ROOT / f).read_bytes())}  {f}\n" for f in files), encoding="utf-8", newline="\n")
    print(f"SHA256SUMS.txt: {len(files)} files")


def main():
    extra = [p.name for p in ROOT.iterdir() if p.name not in ("tools", "README.md", "__pycache__")]
    frozen = read_own_files()
    if extra:
        raise SystemExit(f"refusing to overwrite an accepted reference ({extra}); move the folder away first")
    objects = index_objects()
    sounds_bytes = asset_object(objects, SOUNDS_JSON)
    if (BLOCKS / "assets" / SOUNDS_JSON).read_bytes() != sounds_bytes:
        raise SystemExit("the block reference's sounds.json differs from the asset index's")
    csv_bytes = (BLOCKS / "facts/block_sounds.csv").read_bytes()
    blocks = oggs(BLOCKS)
    for name, (digest, size) in blocks.items():
        entry = objects.get(f"minecraft/sounds/{name}.ogg")
        if entry is None or entry["hash"] != digest or entry["size"] != size:
            raise SystemExit(f"block reference file {name} differs from the asset index")
    copies = {}

    def own_info(name):
        data = copies[name] = asset_object(objects, f"minecraft/sounds/{name}.ogg")
        return sha1(data), len(data)

    out = derive(sounds_bytes, csv_bytes, blocks, own_info, source_object(objects[SOUNDS_JSON]["hash"], csv_bytes),
                 frozen or ())
    if frozen is None:
        (ROOT / OWN_FILES).write_text("".join(f"{n}\n" for n in own_names(out)), encoding="utf-8", newline="\n")
        print(f"{OWN_FILES}: written ({len(own_names(out))} names)")
    for name, data in copies.items():
        path = ROOT / SOUNDS / f"{name}.ogg"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (ROOT / "sound_events.json").write_bytes(out)
    print(f"built: sound_events.json, {len(copies)} own .ogg files")
    write_sums()


if __name__ == "__main__":
    main()
