"""Import the sound assets under /Game/Sounds from Reference/minecraft-sounds-26.3 (data derived by
SoundImportData.py; Docs/Breaking.md, "Other sound events"):

- Sounds/<dir>/<stem>: a sound wave for each .ogg file homed in the sounds reference.
- SA_Sound<d>: a sphere attenuation, linear over d blocks, for each distinct attenuation distance (SA_Sound16, ...).
- DA_Sounds (UFoundationSoundAssets): the sound events (files, volume, pitch, attenuation) and every block's step and
  fall events. Files homed in the blocks reference reuse the waves ImportBreakAssets.py made under the break root
  (loaded read-only; nothing under the break root is ever saved, renamed or modified).

Additive, like ImportBreakAssets.py: existing assets are verified, never rebuilt. A DA_Sounds made from other sound
data is refused (move it away deliberately), except an append: when the reference only appended events (its
"Appending" rule) and every old block entry is still there unchanged and in order (survival B1 adds block families),
the new waves are imported, the new events appended and the new blocks inserted at their places in the data's order;
the old entries are left as they are. Run after ImportBreakAssets.py:

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\ImportSounds.py" -unattended -nop4 -nosplash
"""
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402
import SoundImportData  # noqa: E402

# UUnnamedProjectSettings::SoundRoot (/Game/Sounds) and BreakRoot (/Game/Breaking); the C++ loads from the same settings.
SOUND_ROOT = ProjectPaths.sound_asset("").rstrip("/")
BREAK_ROOT = ProjectPaths.break_asset("").rstrip("/")
TAG = "SoundSourceSha256"
IMPORT_BATCH = 100
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def fail(message):
    raise RuntimeError("SOUND_IMPORT FAIL: " + message)


def path_of(asset):
    return asset.get_path_name().split(".")[0] if asset else ""


def set_first(obj, names, value, what):
    """Sets the first property name that exists (names are unverified guesses at UE 5.8's Python names)."""
    for name in names:
        try:
            obj.set_editor_property(name, value)
            unreal.log("SOUND_IMPORT %s: property %s set" % (what, name))
            return name
        except Exception:  # noqa: BLE001 - unreal raises plain Exception for unknown properties
            continue
    fail("%s: none of the properties %s exists (unverified names; fix in ImportSounds.py)" % (what, names))


def first_enum(cls_name, names):
    cls = getattr(unreal, cls_name, None)
    if cls is None:
        fail("unreal.%s does not exist (unverified enum name)" % cls_name)
    for name in names:
        if hasattr(cls, name):
            return getattr(cls, name)
    fail("unreal.%s has none of %s" % (cls_name, names))


def get_or_create_asset(package, asset_class, factory):
    if assets.does_asset_exist(package):
        return assets.load_asset(package), False
    folder, short = ProjectPaths.split(package)
    created = tools.create_asset(short, folder, asset_class, factory)
    if not created:
        fail("could not create " + package)
    return created, True


# ---------------------------------------------------------------------------------------------------- the data

data = SoundImportData.load()
digest = data["digest"]

# The per-block table indexes the same blocks, in the same order, as the break assets (read-only).
BREAK_ASSETS = BREAK_ROOT + "/DA_BreakAssets"
break_assets = assets.load_asset(BREAK_ASSETS) if assets.does_asset_exist(BREAK_ASSETS) else None
if not break_assets:
    fail("DA_BreakAssets is missing: run ImportBreakAssets.py first")
break_ids = [str(b.get_editor_property("id")) for b in break_assets.get_editor_property("blocks")]
if break_ids != [b["id"] for b in data["blocks"]]:
    fail("DA_BreakAssets' Blocks differ from the sound data's blocks (%d vs %d); the block lists must match in order"
         % (len(break_ids), len(data["blocks"])))

# ---------------------------------------------------------------------------------------------------- sounds


def wave_package(name):
    return "%s/Sounds/%s" % (SOUND_ROOT, name)


def block_wave_package(name):
    return "%s/Sounds/%s" % (BREAK_ROOT, name)


def source_file_of(wave):
    """The wave's first import source file, '' if none (get_first_filename is unverified; extract_filenames fallback)."""
    try:
        import_data = wave.get_editor_property("asset_import_data")
    except Exception:  # noqa: BLE001
        return ""
    if not import_data:
        return ""
    if hasattr(import_data, "get_first_filename"):
        return str(import_data.get_first_filename() or "")
    if hasattr(import_data, "extract_filenames"):
        names = import_data.extract_filenames()
        return str(names[0]) if names else ""
    return ""


def check_wave(package, name):
    """The wave exists, is a SoundWave and was imported from sounds/<name>.ogg; returns it."""
    wave = assets.load_asset(package) if assets.does_asset_exist(package) else None
    if not isinstance(wave, unreal.SoundWave):
        fail("%s is missing or not a sound wave" % package)
    source = source_file_of(wave).replace("\\", "/").lower()
    suffix = "sounds/%s.ogg" % name.lower()
    if not source or not source.endswith(suffix):
        fail("%s was imported from %r, expected a path ending %s (stale or wrong wave; move it away deliberately)"
             % (package, source, suffix))
    return wave


def ensure_waves():
    missing = [n for n in data["own_files"] if not assets.does_asset_exist(wave_package(n))]
    for start in range(0, len(missing), IMPORT_BATCH):
        tasks = []
        for name in missing[start:start + IMPORT_BATCH]:
            source = SoundImportData.SOUNDS / (name + ".ogg")
            if not source.is_file():
                fail("%s is missing" % source)
            folder, short = ProjectPaths.split(wave_package(name))
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", str(source))
            task.set_editor_property("destination_path", folder)
            task.set_editor_property("destination_name", short)
            task.set_editor_property("automated", True)
            task.set_editor_property("replace_existing", False)
            task.set_editor_property("save", True)
            tasks.append((name, task))
        tools.import_asset_tasks([t for _, t in tasks])
        for name, task in tasks:
            if not task.get_editor_property("imported_object_paths") and not assets.does_asset_exist(wave_package(name)):
                fail("importing %s produced no asset (UE 5.8 should import .ogg natively)" % name)
    return len(missing)


def verify_waves():
    """Own waves exist and came from the expected source; block-set waves are the break assets' (read-only);
    no stray waves under the sound root. Returns {file name: wave} for both homes."""
    waves = {}
    for name in data["own_files"]:
        waves[name] = check_wave(wave_package(name), name)
    for name in data["block_files"]:
        waves[name] = check_wave(block_wave_package(name), name)
    expected_names = set(data["own_files"])
    prefix = SOUND_ROOT + "/Sounds/"
    for listed in assets.list_assets(SOUND_ROOT + "/Sounds", True, False):
        package = str(listed).split(".")[0]
        if not package.startswith(prefix) or package[len(prefix):] in expected_names:
            continue
        if isinstance(assets.load_asset(package), unreal.SoundWave):
            fail("unexpected sound wave %s under %s/Sounds" % (package, SOUND_ROOT))
    return waves


# ---------------------------------------------------------------------------------------------------- attenuation


def attenuation_package(distance):
    return "%s/SA_Sound%g" % (SOUND_ROOT, distance)


def ensure_attenuation(distance):
    package = attenuation_package(distance)
    what = "SA_Sound%g" % distance
    asset, created = get_or_create_asset(package, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    settings = asset.get_editor_property("attenuation")
    if created:
        # Unverified UE 5.8 Python names: attenuate (bAttenuate), attenuation_shape, attenuation_shape_extents (sphere
        # radius in X), falloff_distance, distance_algorithm.
        set_first(settings, ["attenuate", "b_attenuate"], True, what)
        set_first(settings, ["attenuation_shape"], first_enum("AttenuationShape", ["SPHERE", "Sphere"]), what)
        set_first(settings, ["attenuation_shape_extents"], unreal.Vector(0.0, 0.0, 0.0), what)
        set_first(settings, ["falloff_distance"], distance * 100.0, what)
        set_first(settings, ["distance_algorithm"], first_enum("AttenuationDistanceModel", ["LINEAR", "Linear"]), what)
        asset.set_editor_property("attenuation", settings)
        if not assets.save_loaded_asset(asset, False):
            fail("save failed: " + package)
    check_attenuation(asset, distance)
    return asset


def check_attenuation(asset, distance):
    what = "SA_Sound%g" % distance
    settings = asset.get_editor_property("attenuation")
    if not bool(settings.get_editor_property("attenuate")):
        fail("%s's attenuation is not enabled" % what)
    if abs(float(settings.get_editor_property("falloff_distance")) - distance * 100.0) > 0.01:
        fail("%s's falloff distance is not %g" % (what, distance * 100.0))
    if settings.get_editor_property("distance_algorithm") != first_enum("AttenuationDistanceModel", ["LINEAR", "Linear"]):
        fail("%s's distance algorithm is not linear" % what)
    if settings.get_editor_property("attenuation_shape") != first_enum("AttenuationShape", ["SPHERE", "Sphere"]):
        fail("%s is not a sphere" % what)
    if settings.get_editor_property("attenuation_shape_extents").x != 0.0:
        fail("%s's inner radius is not 0" % what)


# ---------------------------------------------------------------------------------------------------- the data asset


def make_events(expected_events, waves, attenuations):
    events = []
    for event in expected_events:
        entry = unreal.FoundationSoundEvent()
        entry.set_editor_property("name", event["name"])
        files = []
        for f in event["files"]:
            item = unreal.FoundationBreakSoundFile()
            item.set_editor_property("name", f["name"])
            item.set_editor_property("wave", waves[f["name"]])
            item.set_editor_property("volume", f["volume"])
            item.set_editor_property("pitch", f["pitch"])
            files.append(item)
        entry.set_editor_property("files", files)
        entry.set_editor_property("attenuation", attenuations[event["attenuation"]])
        events.append(entry)
    return events


def fill(asset, waves, attenuations):
    events = make_events(data["events"], waves, attenuations)
    asset.set_editor_property("source_sha256", digest)
    asset.set_editor_property("events", events)
    asset.set_editor_property("blocks", [make_block(block) for block in data["blocks"]])


def make_block(block):
    entry = unreal.FoundationSoundBlock()
    entry.set_editor_property("id", block["id"])
    entry.set_editor_property("step_event", block["step_event"])
    entry.set_editor_property("fall_event", block["fall_event"])
    return entry


def close(a, b):
    return abs(float(a) - float(b)) < 1e-4


def expected_wave_package(f):
    return wave_package(f["name"]) if f["home"] == "sounds" else block_wave_package(f["name"])


def verify(asset):
    if asset.get_editor_property("source_sha256") != digest:
        fail("DA_Sounds was made from other sound data; move it away deliberately and rerun")
    verify_waves()
    for distance in data["attenuations"]:
        package = attenuation_package(distance)
        attenuation = assets.load_asset(package) if assets.does_asset_exist(package) else None
        if not attenuation:
            fail("%s is missing" % package)
        check_attenuation(attenuation, distance)
    events = asset.get_editor_property("events")
    if len(events) != len(data["events"]):
        fail("DA_Sounds has %d events, expected %d" % (len(events), len(data["events"])))
    file_count = sum(check_event(event, expected) for event, expected in zip(events, data["events"]))
    check_blocks(asset)
    return len(events), file_count, len(data["blocks"])


def check_event(event, expected):
    """The DA_Sounds event equals its sound data entry (name, attenuation, files); returns its file count."""
    file_count = 0
    if str(event.get_editor_property("name")) != expected["name"]:
        fail("event %s differs" % expected["name"])
    attenuation = event.get_editor_property("attenuation")
    if path_of(attenuation) != attenuation_package(expected["attenuation"]):
        fail("event %s attenuation is %s, not %s" % (expected["name"], path_of(attenuation),
                                                      attenuation_package(expected["attenuation"])))
    files = event.get_editor_property("files")
    if len(files) != len(expected["files"]):
        fail("event %s has %d files, expected %d" % (expected["name"], len(files), len(expected["files"])))
    for item, f in zip(files, expected["files"]):
        wave = item.get_editor_property("wave")
        if str(item.get_editor_property("name")) != f["name"] or path_of(wave) != expected_wave_package(f):
            fail("event %s file %s references the wrong wave" % (expected["name"], f["name"]))
        if not close(item.get_editor_property("volume"), f["volume"]) or \
                not close(item.get_editor_property("pitch"), f["pitch"]):
            fail("event %s file %s differs in volume or pitch" % (expected["name"], f["name"]))
        file_count += 1
    return file_count


def check_blocks(asset):
    blocks = asset.get_editor_property("blocks")
    if len(blocks) != len(data["blocks"]):
        fail("DA_Sounds has %d blocks, expected %d" % (len(blocks), len(data["blocks"])))
    for entry, block in zip(blocks, data["blocks"]):
        if not same_block(entry, block):
            fail("block entry %s differs from the sound data" % block["id"])


def same_block(entry, block):
    return (str(entry.get_editor_property("id")) == block["id"] and
            entry.get_editor_property("step_event") == block["step_event"] and
            entry.get_editor_property("fall_event") == block["fall_event"])


def append(asset):
    """Appends to a DA_Sounds made from earlier sound data: its events must be an exact prefix of the data's, and its
    blocks an ordered subsequence of the data's blocks (manifest blocks, then fluids), each unchanged. The new events go
    last; the new blocks go in at their places in the data's order. Equal events with more blocks is an append too;
    anything else is refused. Returns (old event count, waves imported)."""
    old = list(asset.get_editor_property("events"))
    old_blocks = list(asset.get_editor_property("blocks"))
    if len(old) > len(data["events"]) or (len(old) == len(data["events"]) and len(old_blocks) >= len(data["blocks"])):
        fail("DA_Sounds was made from other sound data (not a prefix); move it away deliberately and rerun")
    for event, expected in zip(old, data["events"]):
        check_event(event, expected)  # fails on any difference
    # Walk the data's blocks: each old entry turns up in order and unchanged (ids are unique); the rest are new.
    blocks, kept = [], 0
    for block in data["blocks"]:
        if kept < len(old_blocks) and str(old_blocks[kept].get_editor_property("id")) == block["id"]:
            if not same_block(old_blocks[kept], block):
                fail("block entry %s differs from the sound data" % block["id"])
            blocks.append(old_blocks[kept])
            kept += 1
        else:
            blocks.append(make_block(block))
    if kept != len(old_blocks):
        fail("DA_Sounds' block %s is not in the sound data's blocks in order; move it away deliberately and rerun"
             % old_blocks[kept].get_editor_property("id"))
    imported = ensure_waves()
    waves = verify_waves()
    attenuations = {d: ensure_attenuation(d) for d in data["attenuations"]}
    asset.set_editor_property("events", old + make_events(data["events"][len(old):], waves, attenuations))
    asset.set_editor_property("blocks", blocks)
    asset.set_editor_property("source_sha256", digest)
    return len(old), imported


def main():
    package = SOUND_ROOT + "/DA_Sounds"
    if assets.does_asset_exist(package):
        existing = assets.load_asset(package)
        if existing.get_editor_property("source_sha256") != digest:
            kept, imported = append(existing)
            if not assets.save_loaded_asset(existing, False):
                fail("save failed: " + package)
            counts = verify(existing)
            unreal.log("SOUND_IMPORT PASS: DA_Sounds appended (%d events kept, %d added; %d files, %d blocks; %d waves "
                       "imported)" % (kept, counts[0] - kept, counts[1], counts[2], imported))
            return
        counts = verify(existing)
        unreal.log("SOUND_IMPORT PASS: DA_Sounds matches the sound data (%d events, %d files, %d blocks); "
                   "not rebuilt" % counts)
        return
    imported = ensure_waves()
    waves = verify_waves()
    attenuations = {d: ensure_attenuation(d) for d in data["attenuations"]}
    folder, short = ProjectPaths.split(package)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.FoundationSoundAssets)
    asset = tools.create_asset(short, folder, unreal.FoundationSoundAssets, factory)
    if not asset:
        fail("could not create " + package)
    fill(asset, waves, attenuations)
    if not assets.save_loaded_asset(asset, False):
        fail("save failed: " + package)
    counts = verify(asset)
    unreal.log("SOUND_IMPORT PASS: DA_Sounds built (%d events, %d files, %d blocks; %d waves imported, "
               "%d block-set waves reused)" % (counts + (imported, len(data["block_files"]))))


main()
