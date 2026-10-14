"""Verify Reference/minecraft-sounds-26.3: file hashes, re-derivation of sound_events.json from the block reference's
sounds.json and block_sounds.csv, the .ogg files against their entries and homes, the asset index when present, and spot
values.

usage: uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-sounds-26.3/tools/verify_reference.py
Everything but step 4 needs only this folder and Reference/minecraft-blocks-26.3; step 4 runs when the asset index is
present and its non-lang pin holds.
"""
import hashlib
import importlib.util
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_reference", Path(__file__).with_name("build_reference.py"))
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)
errors = []


def check(condition, message):
    if not condition:
        errors.append(message)


# 1. Every file is listed with its SHA-256, and nothing else is in the folder.
sums = {}
for line in (ROOT / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines():
    digest, _, name = line.partition("  ")
    check(len(digest) == 64 and name and name not in sums, f"SHA256SUMS.txt: bad or repeated line {line!r}")
    sums[name] = digest
on_disk = {p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*")
           if p.is_file() and p.name != "SHA256SUMS.txt" and "__pycache__" not in p.parts}
check(on_disk == set(sums), f"file list differs from SHA256SUMS.txt: +{sorted(on_disk - set(sums))[:5]} "
                            f"-{sorted(set(sums) - on_disk)[:5]}")
check(sorted(sums) == list(sums), "SHA256SUMS.txt is not sorted by path")
for name, digest in sums.items():
    path = ROOT / name
    if path.is_file():
        check(hashlib.sha256(path.read_bytes()).hexdigest() == digest, f"hash mismatch: {name}")

# 2. Re-derive sound_events.json from the block reference's inputs and the on-disk files.
block_sounds = (build.BLOCKS / "assets/minecraft/sounds.json").read_bytes()
block_csv = (build.BLOCKS / "facts/block_sounds.csv").read_bytes()
block_files = build.oggs(build.BLOCKS)
own_files = build.oggs(ROOT)
events_path = ROOT / "sound_events.json"
data = json.loads(events_path.read_text(encoding="utf-8"))
source = build.source_object(build.sha1(block_sounds), block_csv)
check(data["source"] == source, "source object differs from the pins and the block reference's inputs")
try:
    derived = build.derive(block_sounds, block_csv, block_files, lambda name: own_files[name], source)
    check(events_path.read_bytes() == derived, "re-derived sound_events.json differs")
except (SystemExit, OSError, KeyError, ValueError) as error:
    errors.append(f"re-derivation failed: {error!r}")
expected = set(build.GENERATED) | set(build.TOOLS) | {"README.md"} | {f"{build.SOUNDS}{n}.ogg" for n in own_files}
check(expected == set(sums), f"files differ from the build's list: +{sorted(set(sums) - expected)[:5]} "
                             f"-{sorted(expected - set(sums))[:5]}")

# 3. Own .ogg files equal their entries; blocks-homed files exist there with the same SHA-1 and size.
files = data["files"]
check(len({f["name"] for f in files}) == len(files), "repeated file entries")
own_entries = {f["name"]: f for f in files if f["home"] == build.OWN}
check(all(f["home"] in (build.OWN, build.BLOCK_HOME) for f in files), "unknown home")
check(set(own_entries) == set(own_files), "own .ogg set on disk differs from the own-homed entries")
for name, entry in own_entries.items():
    check(own_files.get(name) == (entry["sha1"], entry["size"]), f"own file differs from its entry: {name}")
    check(name not in block_files, f"own file also in the block reference: {name}")
for entry in files:
    if entry["home"] == build.BLOCK_HOME:
        check(block_files.get(entry["name"]) == (entry["sha1"], entry["size"]),
              f"blocks-homed file differs from the block reference: {entry['name']}")

# 4. The asset index.
index_checked = 0
index_dir = build.ASSET_DIR / "indexes/34.json"
if index_dir.exists():
    try:
        objects = build.index_objects()
        check(build.asset_object(objects, build.SOUNDS_JSON) == block_sounds,
              "asset index sounds.json differs from the block reference's copy")
        check(objects[build.SOUNDS_JSON]["hash"] == source["sounds_json_sha1"], "sounds_json_sha1 differs from the index")
        for entry in files:
            index_entry = objects.get(f"minecraft/sounds/{entry['name']}.ogg")
            check(index_entry is not None and index_entry["hash"] == entry["sha1"] and
                  index_entry["size"] == entry["size"], f"differs from the asset index: {entry['name']}")
            index_checked += 1
    except (SystemExit, OSError, AssertionError, KeyError) as error:
        errors.append(f"asset index check failed: {error!r}")
    print(f"asset index check ran: {index_checked} file entries compared")
else:
    print(f"note: asset index absent ({index_dir}); index comparisons skipped")

# 5. Counts and spot values.
events = data["events"]
by_name = {e["name"]: e for e in events}
check(len(by_name) == len(events), "repeated event names")
check([e["name"] for e in events if e["group"] == "named"] == build.NAMED and
      [e["name"] for e in events[:21]] == build.NAMED, "the 21 named events differ or are out of order")
counts = {g: sum(e["group"] == g for e in events) for g in ("named", "step", "fall")}
check(counts == {"named": 21, "step": 86, "fall": 86}, f"group counts differ: {counts}")
check(by_name["entity.item.pickup"]["sounds"] == ["random/pop"], "entity.item.pickup does not play only random/pop")
for name in ("block.chest.open", "block.chest.close"):
    check(all(isinstance(s, dict) and s.get("attenuation_distance") == 12 for s in by_name[name]["sounds"]),
          f"{name}: attenuation_distance is not 12")
check(len(own_files) > 0 and all(f["name"] for f in files), "no own files")

if errors:
    print("REFERENCE FAIL")
    print("\n".join(errors[:40]))
    sys.exit(1)
by_home = {h: sum(f["home"] == h for f in files) for h in (build.OWN, build.BLOCK_HOME)}
print(f"REFERENCE OK: {len(sums)} files hashed, sound_events.json re-derived; {len(events)} events {counts}; "
      f"{len(files)} files ({by_home[build.OWN]} own, {by_home[build.BLOCK_HOME]} in the block reference, "
      f"{sum(f['size'] for f in own_entries.values())} own bytes); asset index "
      f"{'compared' if index_checked else 'not compared'}")
