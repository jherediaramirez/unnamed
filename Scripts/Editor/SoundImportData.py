"""The sound import's data, derived from Reference/minecraft-sounds-26.3 (and the block reference's step/fall sounds)
with the standard library only (Docs/Breaking.md, "Other sound events").

Importable from UE's Python (ImportSounds.py) and runnable standalone, which prints a summary
(uv run --no-project --python 3.13 python -I Scripts/Editor/SoundImportData.py).

Files homed in minecraft-blocks-26.3 are already waves under the break root (ImportBreakAssets.py); only the files
homed in minecraft-sounds-26.3 are imported here.
"""
import csv
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REF = ROOT / "Reference/minecraft-sounds-26.3"
BLOCK_REF = ROOT / "Reference/minecraft-blocks-26.3"
SOUNDS = REF / "assets/minecraft/sounds"
BLOCK_SOUNDS = BLOCK_REF / "assets/minecraft/sounds"

DEFAULT_ATTENUATION = 16  # blocks
ENTRY_KEYS = {"name", "volume", "pitch", "attenuation_distance"}
GROUPS = ("named", "step", "fall")
HOMES = {"minecraft-sounds-26.3": "sounds", "minecraft-blocks-26.3": "blocks"}

_reads = {}  # relative path -> sha256 of every input file read, for the source digest


def _read(path):
    data = Path(path).read_bytes()
    resolved = Path(path).resolve()
    if resolved.is_relative_to(REF) or resolved.is_relative_to(BLOCK_REF):
        _reads[resolved.relative_to(ROOT).as_posix()] = hashlib.sha256(data).hexdigest()
    return data


def _json(path):
    return json.loads(_read(path).decode("utf-8"))


def _check_file(path, entry, what):
    """The file exists and matches its entry's SHA-1 and size (read without joining the digest)."""
    if not path.is_file():
        raise ValueError("%s %s is missing on disk" % (what, entry["name"]))
    data = path.read_bytes()
    if len(data) != entry["size"] or hashlib.sha1(data).hexdigest() != entry["sha1"]:
        raise ValueError("%s %s differs from its sound_events.json entry (size/SHA-1)" % (what, entry["name"]))
    return len(data)


def build_tables():
    document = _json(REF / "sound_events.json")
    file_entries = {}
    for entry in document["files"]:
        if entry["name"] in file_entries:
            raise ValueError("sound_events.json lists file %s twice" % entry["name"])
        if entry["home"] not in HOMES:
            raise ValueError("file %s has an unknown home %r" % (entry["name"], entry["home"]))
        file_entries[entry["name"]] = entry

    events, event_index = [], {}
    for raw in document["events"]:
        name = raw["name"]
        if name in event_index:
            raise ValueError("sound_events.json lists event %s twice" % name)
        if raw["group"] not in GROUPS:
            raise ValueError("event %s has an unknown group %r" % (name, raw["group"]))
        files, distances = [], set()
        for entry in raw["sounds"]:
            if isinstance(entry, str):
                entry = {"name": entry}
            if not isinstance(entry, dict) or "name" not in entry or not set(entry) <= ENTRY_KEYS:
                raise ValueError("event %s has an unsupported sound entry: %r" % (name, entry))
            # As the reference's build: a "minecraft:" prefix names the same file.
            entry = dict(entry, name=entry["name"].removeprefix("minecraft:"))
            if entry["name"] not in file_entries:
                raise ValueError("event %s file %s is not in the files list" % (name, entry["name"]))
            distances.add(float(entry.get("attenuation_distance", DEFAULT_ATTENUATION)))
            files.append({"name": entry["name"], "volume": float(entry.get("volume", 1.0)),
                          "pitch": float(entry.get("pitch", 1.0)),
                          "home": HOMES[file_entries[entry["name"]]["home"]]})
        if len(distances) > 1:
            raise ValueError("event %s files disagree on attenuation_distance: %s" % (name, sorted(distances)))
        distance = distances.pop() if distances else float(DEFAULT_ATTENUATION)
        event_index[name] = len(events)
        events.append({"name": name, "group": raw["group"], "attenuation": distance, "files": files})

    own_files = [e["name"] for e in document["files"] if HOMES[e["home"]] == "sounds"]
    block_files = [e["name"] for e in document["files"] if HOMES[e["home"]] == "blocks"]
    own_bytes = 0
    for name in own_files:
        own_bytes += _check_file(SOUNDS / (name + ".ogg"), file_entries[name], "own file")
        _read(SOUNDS / (name + ".ogg"))  # joins the digest
    for name in block_files:
        _check_file(BLOCK_SOUNDS / (name + ".ogg"), file_entries[name], "blocks-homed file")
    disk = sorted(p.relative_to(SOUNDS).with_suffix("").as_posix() for p in SOUNDS.rglob("*.ogg"))
    if disk != sorted(own_files):
        raise ValueError("own .ogg files on disk and sound_events.json differ: %d only listed %s, %d only on disk %s" % (
            len(set(own_files) - set(disk)), sorted(set(own_files) - set(disk))[:5],
            len(set(disk) - set(own_files)), sorted(set(disk) - set(own_files))[:5]))

    manifest = _json(BLOCK_REF / "block_manifest.json")
    ids = [b["id"] for b in manifest["blocks"] + manifest["fluids"]]  # DA_BreakAssets' order
    # The CSV is in its own order (fluids first); the manifest sets ours.
    text = _read(BLOCK_REF / "facts/block_sounds.csv").decode("utf-8")
    rows = {r["id"]: r for r in csv.DictReader(text.splitlines())}
    if len(rows) != len(ids) or set(ids) != set(rows):
        raise ValueError("block_manifest.json and block_sounds.csv disagree on the block list")
    blocks, silent = [], {"step": 0, "fall": 0}
    for block_id in ids:
        row = rows[block_id]
        record = {"id": block_id}
        for kind in ("step", "fall"):
            event = row[kind]
            if event == "intentionally_empty":
                record[kind + "_event"] = -1
                silent[kind] += 1
            elif event in event_index:
                record[kind + "_event"] = event_index[event]
            else:
                raise ValueError("block %s %s event %s is not in sound_events.json" % (row["id"], kind, event))
        blocks.append(record)

    return {"events": events, "blocks": blocks, "own_files": own_files, "block_files": block_files,
            "attenuations": sorted({e["attenuation"] for e in events}), "own_bytes": own_bytes, "silent": silent}


def source_digest():
    """SHA-256 over (relative path, file sha256) of every input read, sorted by path, plus this script's own hash."""
    own = Path(__file__).resolve()
    digest = hashlib.sha256()
    entries = dict(_reads)
    entries[own.relative_to(ROOT).as_posix()] = hashlib.sha256(own.read_bytes()).hexdigest()
    for path in sorted(entries):
        digest.update(("%s\0%s\n" % (path, entries[path])).encode("utf-8"))
    return digest.hexdigest()


def load():
    """Everything the import needs: events, per-block table, file lists, attenuations and the source digest."""
    tables = build_tables()
    tables["digest"] = source_digest()
    return tables


# ---------------------------------------------------------------------------------------------------- standalone


def main():
    t = load()
    by_group = {g: sum(e["group"] == g for e in t["events"]) for g in GROUPS}
    print("events %d (%s)" % (len(t["events"]), ", ".join("%s %d" % kv for kv in by_group.items())))
    print("files: %d own (%d bytes), %d blocks-homed" % (len(t["own_files"]), t["own_bytes"], len(t["block_files"])))
    print("attenuations (blocks): %s" % ", ".join("%g" % a for a in t["attenuations"]))
    print("blocks %d, silent step %d, silent fall %d" % (len(t["blocks"]), t["silent"]["step"], t["silent"]["fall"]))
    print("digest " + t["digest"])


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        print("SOUND DATA FAIL: %s" % error)
        sys.exit(1)
