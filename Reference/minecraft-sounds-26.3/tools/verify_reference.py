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
# The accepted first build (2026-10-13): its 193 events and 477 file entries, as SHA-256 of their canonical JSON (sorted
# keys, compact separators). Later builds only append (README, "Appending").
FIRST_BUILD = {"events": (193, "aab08a074aaf3b232ccdda33bc99e43356ae5d8cb6ba209af9157e8219a88d40"),
               "files": (477, "4633c998a2b405e75d9f7b9c73107d72ac0ede55e9ec2cd60be4c63e026ff366")}


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
frozen = build.read_own_files()
check(frozen is not None, f"{build.OWN_FILES} is missing")
frozen_events = build.read_frozen_events()
check(len(frozen_events) >= 332, f"{build.FROZEN_EVENTS} is missing or shorter than survival B1's 332 events")
try:
    derived = build.derive(block_sounds, block_csv, block_files, lambda name: own_files[name], source, frozen or (),
                           frozen_events)
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
    if name in block_files:  # allowed only for tools/own_files.txt's names, and only as a byte-equal copy
        check(name in (frozen or ()) and (ROOT / build.SOUNDS / f"{name}.ogg").read_bytes() ==
              (build.BLOCKS / build.SOUNDS / f"{name}.ogg").read_bytes(),
              f"own file also in the block reference (not in {build.OWN_FILES}, or not byte-equal): {name}")
for entry in files:
    if entry["home"] == build.BLOCK_HOME:
        check(block_files.get(entry["name"]) == (entry["sha1"], entry["size"]),
              f"blocks-homed file differs from the block reference: {entry['name']}")

# The first build's entries are unchanged; tools/own_files.txt is a prefix of the names homed here, in order.
for key, (count, digest) in FIRST_BUILD.items():
    canonical = json.dumps(data[key][:count], sort_keys=True, separators=(",", ":")).encode()
    check(len(data[key]) >= count and hashlib.sha256(canonical).hexdigest() == digest,
          f"the first build's {count} {key} entries changed")
own_order = [f["name"] for f in files if f["home"] == build.OWN]
check(frozen is not None and own_order[:len(frozen)] == frozen, f"{build.OWN_FILES} is not a prefix of the own files")
check([e["name"] for e in data["events"][:len(frozen_events)]] == frozen_events,
      f"{build.FROZEN_EVENTS} is not a prefix of the events")
# The freeze holds: one more APPENDED_NAMED event (a dummy, in memory) goes last and leaves every earlier event and
# file entry byte-identical.
DUMMY = "unnamed.verify.dummy"
dummy_sounds = dict(json.loads(block_sounds), **{DUMMY: {"sounds": ["random/pop"]}})
saved = build.APPENDED_NAMED
build.APPENDED_NAMED = saved + [DUMMY]
try:
    with_dummy = json.loads(build.derive(json.dumps(dummy_sounds).encode(), block_csv, block_files,
                                         lambda name: own_files[name], source, frozen or (), frozen_events))
    check(with_dummy["events"][:-1] == data["events"] and with_dummy["events"][-1]["name"] == DUMMY and
          with_dummy["files"] == data["files"], "an appended named event does not go last (the freeze is broken)")
except (SystemExit, KeyError, ValueError) as error:
    errors.append(f"dummy append failed: {error!r}")
finally:
    build.APPENDED_NAMED = saved

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
named = build.NAMED + build.APPENDED_NAMED
check([e["name"] for e in events if e["group"] == "named"] == named and
      [e["name"] for e in events[:21]] == build.NAMED and [e["name"] for e in events[193:326]] ==
      build.APPENDED_NAMED[:133] and [e["name"] for e in events[332:]] == build.APPENDED_LATER,
      "the named events differ or are out of order")
check(len(build.APPENDED_NAMED) == 134 and len(set(named)) == len(named) and
      build.APPENDED_NAMED[133:] == build.APPENDED_LATER, "APPENDED_NAMED is not B0's 133 and 1 later new events")
pattern = build.re.compile(build.T5_PATTERN)
check(build.APPENDED_T5[:-4] == sorted(n for n in json.loads(block_sounds) if pattern.match(n)) and
      len(build.APPENDED_T5) == 58, "APPENDED_T5 differs from the events T5_PATTERN matches in sounds.json")
counts = {g: sum(e["group"] == g for e in events) for g in ("named", "step", "fall")}
check(counts == {"named": 155, "step": 89, "fall": 89}, f"group counts differ: {counts}")
# Survival B1 (2026-10-14): block_sounds.csv's 3 new sound types append their step, then fall events after the 326.
B1_TYPES = ["block.cherry_sapling", "block.ladder", "block.weeping_vines"]
check([(e["name"], e["group"]) for e in events[326:332]] == [(f"{t}.{g}", g) for g in ("step", "fall") for t in B1_TYPES],
      "B1's step and fall events are not events 326-331 in order")
# Then B1's appended named event: the blast furnace (26.3's name has no underscore).
check(by_name.get("block.blastfurnace.fire_crackle", {}).get("sounds") ==
      [f"block/blastfurnace/blastfurnace{i}" for i in range(1, 6)], "block.blastfurnace.fire_crackle files differ")
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
