"""Build Reference/minecraft-sounds-26.3: the sound events the game plays beyond the block break/hit/place sounds, with
the .ogg files the block reference does not already hold.

usage (from the repository root):
    uv run --no-project --python 3.13 python -I Reference/minecraft-sounds-26.3/tools/build_reference.py

Inputs (read-only): asset index 34 (pinned below; sounds.json and the .ogg objects) and Reference/minecraft-blocks-26.3
(its sounds.json copy, facts/block_sounds.csv and assets/minecraft/sounds/). Nothing is copied from the block reference;
files it already has are referenced ("home": "minecraft-blocks-26.3"). sounds.json is not duplicated here.

The folder must hold only tools/ and README.md, so an accepted reference is never overwritten; to rebuild, move the old
folder away first. Every generated file comes from derive(), which verify_reference.py runs again without the asset index.
Data only: the game code is never read.
"""
import csv
import hashlib
import io
import json
import os
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
ENTRY_KEYS = {"name", "volume", "pitch", "attenuation_distance"}
GENERATED = ["sound_events.json"]
TOOLS = ["tools/build_reference.py", "tools/verify_reference.py"]


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


def derive(sounds_json_bytes, csv_bytes, blocks_oggs, own_info, source):
    """sound_events.json's bytes. `blocks_oggs` is {name: (sha1, size)} of the block reference's files, `own_info(name)`
    returns (sha1, size) of a file homed here, `source` is the `source` object (pins and hashes)."""
    sounds_json = json.loads(sounds_json_bytes)
    rows = list(csv.DictReader(io.StringIO(csv_bytes.decode("utf-8"), newline="")))
    groups = []
    for column in ("step", "fall"):
        values = sorted({r[column] for r in rows} - {"intentionally_empty"})
        groups += [(v, column) for v in values]
    events, files, seen = [], [], set()
    for name, group in [(n, "named") for n in NAMED] + groups:
        if name not in sounds_json:
            raise SystemExit(f"sounds.json has no event {name}")
        sounds = sounds_json[name]["sounds"]
        events.append({"name": name, "group": group, "subtitle": sounds_json[name].get("subtitle"), "sounds": sounds})
        for file in event_files(name, sounds):
            if file in seen:
                continue
            seen.add(file)
            home = BLOCK_HOME if file in blocks_oggs else OWN
            digest, size = blocks_oggs[file] if home == BLOCK_HOME else own_info(file)
            files.append({"name": file, "sha1": digest, "size": size, "home": home})
    return dumps({"source": source, "events": events, "files": files})


def source_object(sounds_json_sha1, csv_bytes):
    return {"asset_index": "34", "asset_index_sha1": ASSET_INDEX_SHA1,
            "asset_index_nonlang_sha1": ASSET_INDEX_NONLANG_SHA1, "sounds_json_sha1": sounds_json_sha1,
            "block_sounds_csv_sha256": sha256(csv_bytes)}


def write_sums():
    files = sorted(p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*")
                   if p.is_file() and p.name != "SHA256SUMS.txt" and "__pycache__" not in p.parts)
    (ROOT / "SHA256SUMS.txt").write_text(
        "".join(f"{sha256((ROOT / f).read_bytes())}  {f}\n" for f in files), encoding="utf-8", newline="\n")
    print(f"SHA256SUMS.txt: {len(files)} files")


def main():
    extra = [p.name for p in ROOT.iterdir() if p.name not in ("tools", "README.md", "__pycache__")]
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

    out = derive(sounds_bytes, csv_bytes, blocks, own_info, source_object(objects[SOUNDS_JSON]["hash"], csv_bytes))
    for name, data in copies.items():
        path = ROOT / SOUNDS / f"{name}.ogg"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (ROOT / "sound_events.json").write_bytes(out)
    print(f"built: sound_events.json, {len(copies)} own .ogg files")
    write_sums()


if __name__ == "__main__":
    main()
