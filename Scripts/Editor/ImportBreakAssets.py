"""Import the breaking effects' assets under /Game/Breaking from Reference/minecraft-blocks-26.3 (data derived by
BreakImportData.py; Docs/Breaking.md):

- T_BreakCracks: a 16 x 16 x 20 texture array (carved stages 0-9, vanilla's own 10-19), one mip, linear, nearest.
- Sounds/<dir>/<stem>: a sound wave for each of the 762 .ogg files of the reference.
- SA_Break (sphere, linear over 16 blocks) and SC_BreakHits (two voices, stop oldest).
- DA_BreakAssets (UFoundationBreakAssets): the sound events, every block's sounds and particle texture, the cracks.

Additive, like ImportOreLooks.py: existing assets are verified, never rebuilt. A DA_BreakAssets made from other break
data is refused (move it away deliberately). It loads DA_Blocks read-only and never changes it. Run before
CreateBreakMaterials.py:

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\ImportBreakAssets.py" -unattended -nop4 -nosplash

.ogg import is unverified on UE 5.8. If it imports nothing, decode the files outside UE and rerun with -BreakWav:

    uv run --no-project --python 3.13 --with soundfile python -I Scripts/Editor/BreakImportData.py
        --decode-wav Intermediate/Breaking/wav
"""
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402
import BreakImportData  # noqa: E402

# UUnnamedProjectSettings::BreakRoot (/Game/Breaking); the C++ loads from the same setting.
BREAK_ROOT = ProjectPaths.break_asset("").rstrip("/")
ROOT = Path(unreal.Paths.project_dir()).resolve()
TAG = "BreakSourceSha256"
ATTENUATION_CM = 1600.0  # 16 blocks
HIT_VOICES = 2
IMPORT_BATCH = 100
Import = unreal.FoundationBlockImport
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
cmdline = unreal.SystemLibrary.get_command_line()
USE_WAV = "breakwav" in cmdline.lower()


def fail(message):
    raise RuntimeError("BREAK_IMPORT FAIL: " + message)


def path_of(asset):
    return asset.get_path_name().split(".")[0] if asset else ""


def set_first(obj, names, value, what):
    """Sets the first property name that exists (names are unverified guesses at UE 5.8's Python names)."""
    for name in names:
        try:
            obj.set_editor_property(name, value)
            unreal.log("BREAK_IMPORT %s: property %s set" % (what, name))
            return name
        except Exception:  # noqa: BLE001 - unreal raises plain Exception for unknown properties
            continue
    fail("%s: none of the properties %s exists (unverified names; fix in ImportBreakAssets.py)" % (what, names))


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

data = BreakImportData.load()
digest = data["digest"]

# Every particle texture must be a texture of the block registry (read-only).
registry = assets.load_asset(ProjectPaths.block_asset("DA_Blocks"))
if not registry:
    fail("DA_Blocks is missing: run ImportBlocks.py first")
registry_textures = {str(t.get_editor_property("name")) for t in registry.get_editor_property("textures")}
for block in data["blocks"]:
    for texture in [block["particle"]] + block["override_particles"]:
        if texture not in registry_textures:
            fail("%s: particle texture %s is not in DA_Blocks' Textures" % (block["id"], texture))

# ---------------------------------------------------------------------------------------------------- cracks


def verify_cracks(array):
    for layer, expected in enumerate(data["layer_hashes"]):
        if Import.hash_layer(array, layer).lower() != expected:
            fail("T_BreakCracks layer %d differs from the break data" % layer)


def enum_of(cls_name, names):
    return first_enum(cls_name, names)


def verify_cracks_asset(array):
    """Identity, tag, slice count and the texture settings BuildTextureArray/ApplyPixelArtSettings leave, then the pixels."""
    if path_of(array) != BREAK_ROOT + "/T_BreakCracks":
        fail("DA_BreakAssets' cracks is %s, not %s/T_BreakCracks" % (path_of(array), BREAK_ROOT))
    if assets.get_metadata_tag(array, TAG) != digest:
        fail("T_BreakCracks lacks the break data digest tag or carries another")
    expected_slices = len(data["layer_hashes"])
    if expected_slices != 20:
        fail("the break data has %d layers, expected 20" % expected_slices)
    # The array's layer count: FoundationBlockImport reads any layer that exists and returns "" past the last.
    slices = 0
    while slices <= expected_slices and Import.hash_layer(array, slices):
        slices += 1
    if slices != expected_slices:
        fail("T_BreakCracks has %d slices, expected %d" % (slices, expected_slices))
    if bool(array.get_editor_property("srgb")):
        fail("T_BreakCracks is sRGB, expected linear")
    if array.get_editor_property("compression_settings") != enum_of("TextureCompressionSettings",
                                                                    ["TC_EDITOR_ICON", "TC_EditorIcon"]):
        fail("T_BreakCracks' compression is not TC_EditorIcon (uncompressed)")
    if array.get_editor_property("filter") != enum_of("TextureFilter", ["TF_NEAREST", "TF_Nearest"]):
        fail("T_BreakCracks' filter is not nearest")
    if array.get_editor_property("mip_gen_settings") != enum_of(
            "TextureMipGenSettings", ["TMGS_LEAVE_EXISTING_MIPS", "TMGS_LeaveExistingMips"]):
        fail("T_BreakCracks' mip generation is not leave-existing-mips (one mip)")
    verify_cracks(array)


def ensure_cracks():
    package = BREAK_ROOT + "/T_BreakCracks"
    if assets.does_asset_exist(package):
        existing = assets.load_asset(package)
        if assets.get_metadata_tag(existing, TAG) != digest:
            fail("T_BreakCracks was made from other break data; move it away deliberately and rerun")
        verify_cracks_asset(existing)
        return existing
    strip = ROOT / "Intermediate/Breaking/cracks_strip.png"
    hashes = BreakImportData.write_strip_png(strip, data["frames"])
    if hashes != data["layer_hashes"]:
        fail("the strip's hashes differ from the break data")
    layers = len(hashes)
    array = Import.create_texture_array(package)
    if not array:
        fail("could not create " + package)
    # "explicit" takes level 0 of layer i straight from frame i of the PNG (no mip generator), and one mip.
    error = Import.build_texture_array(array, [str(strip)] * layers, list(range(layers)), ["explicit"] * layers,
                                       [0.0] * layers, BreakImportData.SIZE, 1)
    if error:
        fail("T_BreakCracks: " + error)
    array.set_editor_property("srgb", False)  # grey data; BuildTextureArray set sRGB and nearest filtering
    assets.set_metadata_tag(array, TAG, digest)
    verify_cracks_asset(array)
    if not assets.save_loaded_asset(array, False):
        fail("save failed: " + package)
    return array


# ---------------------------------------------------------------------------------------------------- sounds


def wave_package(name):
    return "%s/Sounds/%s" % (BREAK_ROOT, name)


def ensure_waves():
    missing = [n for n in data["sound_files"] if not assets.does_asset_exist(wave_package(n))]
    for start in range(0, len(missing), IMPORT_BATCH):
        tasks = []
        for name in missing[start:start + IMPORT_BATCH]:
            source = (ROOT / "Intermediate/Breaking/wav" / (name + ".wav")) if USE_WAV else \
                (BreakImportData.ASSETS / "sounds" / (name + ".ogg"))
            if not source.is_file():
                fail("%s is missing%s" % (source, " (run BreakImportData.py --decode-wav)" if USE_WAV else ""))
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
                fail("importing %s produced no asset%s" % (
                    name, "" if USE_WAV else ": UE 5.8 may not import .ogg here; decode with "
                    "BreakImportData.py --decode-wav (soundfile) and rerun with -BreakWav"))
    waves = {}
    for name in data["sound_files"]:
        wave = assets.load_asset(wave_package(name))
        if not isinstance(wave, unreal.SoundWave):
            fail("%s is not a sound wave" % wave_package(name))
        waves[name] = wave
    return waves, len(missing)


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


def verify_waves():
    """Every expected wave exists, is a SoundWave, was imported from the expected source; no stray waves."""
    expected_names = set(data["sound_files"])
    for name in data["sound_files"]:
        wave = assets.load_asset(wave_package(name)) if assets.does_asset_exist(wave_package(name)) else None
        if not isinstance(wave, unreal.SoundWave):
            fail("%s is missing or not a sound wave" % wave_package(name))
        source = source_file_of(wave).replace("\\", "/").lower()
        suffixes = ["sounds/%s.ogg" % name.lower()]
        if USE_WAV:
            suffixes.append("breaking/wav/%s.wav" % name.lower())
        if not source or not any(source.endswith(s) for s in suffixes):
            fail("%s was imported from %r, expected a path ending %s (stale or wrong wave; move it away deliberately)"
                 % (wave_package(name), source, suffixes[0]))
    prefix = BREAK_ROOT + "/Sounds/"
    for listed in assets.list_assets(BREAK_ROOT + "/Sounds", True, False):
        package = str(listed).split(".")[0]
        if not package.startswith(prefix) or package[len(prefix):] in expected_names:
            continue
        if isinstance(assets.load_asset(package), unreal.SoundWave):
            fail("unexpected sound wave %s under %s/Sounds" % (package, BREAK_ROOT))


# ---------------------------------------------------------------------------------------------------- attenuation


def ensure_attenuation():
    package = BREAK_ROOT + "/SA_Break"
    asset, created = get_or_create_asset(package, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    settings = asset.get_editor_property("attenuation")
    if created:
        # Unverified UE 5.8 Python names: attenuate (bAttenuate), attenuation_shape, attenuation_shape_extents (sphere
        # radius in X), falloff_distance, distance_algorithm.
        set_first(settings, ["attenuate", "b_attenuate"], True, "SA_Break")
        set_first(settings, ["attenuation_shape"], first_enum("AttenuationShape", ["SPHERE", "Sphere"]), "SA_Break")
        set_first(settings, ["attenuation_shape_extents"], unreal.Vector(0.0, 0.0, 0.0), "SA_Break")
        set_first(settings, ["falloff_distance"], ATTENUATION_CM, "SA_Break")
        set_first(settings, ["distance_algorithm"], first_enum("AttenuationDistanceModel", ["LINEAR", "Linear"]),
                  "SA_Break")
        asset.set_editor_property("attenuation", settings)
        if not assets.save_loaded_asset(asset, False):
            fail("save failed: " + package)
        settings = asset.get_editor_property("attenuation")
    check_attenuation(asset)
    return asset


def check_attenuation(asset):
    settings = asset.get_editor_property("attenuation")
    if not bool(settings.get_editor_property("attenuate")):
        fail("SA_Break's attenuation is not enabled")
    if abs(float(settings.get_editor_property("falloff_distance")) - ATTENUATION_CM) > 0.01:
        fail("SA_Break's falloff distance is not %g" % ATTENUATION_CM)
    if settings.get_editor_property("distance_algorithm") != first_enum("AttenuationDistanceModel", ["LINEAR", "Linear"]):
        fail("SA_Break's distance algorithm is not linear")
    if settings.get_editor_property("attenuation_shape") != first_enum("AttenuationShape", ["SPHERE", "Sphere"]):
        fail("SA_Break is not a sphere")
    if settings.get_editor_property("attenuation_shape_extents").x != 0.0:
        fail("SA_Break's inner radius is not 0")


def check_concurrency(asset):
    settings = asset.get_editor_property("concurrency")
    if int(settings.get_editor_property("max_count")) != HIT_VOICES:
        fail("SC_BreakHits' max count is not %d" % HIT_VOICES)
    if settings.get_editor_property("resolution_rule") != first_enum("MaxConcurrentResolutionRule",
                                                                     ["STOP_OLDEST", "StopOldest"]):
        fail("SC_BreakHits' resolution rule is not stop oldest")


def ensure_concurrency():
    package = BREAK_ROOT + "/SC_BreakHits"
    asset, created = get_or_create_asset(package, unreal.SoundConcurrency, unreal.SoundConcurrencyFactory())
    settings = asset.get_editor_property("concurrency")
    stop_oldest = first_enum("MaxConcurrentResolutionRule", ["STOP_OLDEST", "StopOldest"])
    if created:
        # Unverified: the struct property "concurrency", with max_count and resolution_rule.
        set_first(settings, ["max_count"], HIT_VOICES, "SC_BreakHits")
        set_first(settings, ["resolution_rule"], stop_oldest, "SC_BreakHits")
        asset.set_editor_property("concurrency", settings)
        if not assets.save_loaded_asset(asset, False):
            fail("save failed: " + package)
        settings = asset.get_editor_property("concurrency")
    check_concurrency(asset)
    return asset


# ---------------------------------------------------------------------------------------------------- the data asset


def fill(asset, cracks, waves, attenuation, concurrency):
    events = []
    for event in data["events"]:
        entry = unreal.FoundationBreakSoundEvent()
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
        events.append(entry)
    blocks = []
    for block in data["blocks"]:
        entry = unreal.FoundationBreakBlock()
        entry.set_editor_property("id", block["id"])
        for key in ("break_event", "hit_event", "place_event", "volume", "pitch", "particle", "particle_tinted",
                    "override_states", "override_particles", "override_tinted"):
            entry.set_editor_property(key, block[key])
        blocks.append(entry)
    asset.set_editor_property("source_sha256", digest)
    asset.set_editor_property("events", events)
    asset.set_editor_property("blocks", blocks)
    asset.set_editor_property("cracks", cracks)
    asset.set_editor_property("attenuation", attenuation)
    asset.set_editor_property("hit_concurrency", concurrency)


def close(a, b):
    return abs(float(a) - float(b)) < 1e-4


def verify(asset):
    if asset.get_editor_property("source_sha256") != digest:
        fail("DA_BreakAssets was made from other break data; move it away deliberately and rerun")
    cracks = asset.get_editor_property("cracks")
    if not cracks:
        fail("DA_BreakAssets has no Cracks")
    verify_cracks_asset(cracks)
    attenuation = asset.get_editor_property("attenuation")
    concurrency = asset.get_editor_property("hit_concurrency")
    if not attenuation or not concurrency:
        fail("DA_BreakAssets lacks its attenuation or hit concurrency")
    if path_of(attenuation) != BREAK_ROOT + "/SA_Break":
        fail("DA_BreakAssets' attenuation is %s, not %s/SA_Break" % (path_of(attenuation), BREAK_ROOT))
    check_attenuation(attenuation)
    if path_of(concurrency) != BREAK_ROOT + "/SC_BreakHits":
        fail("DA_BreakAssets' hit concurrency is %s, not %s/SC_BreakHits" % (path_of(concurrency), BREAK_ROOT))
    check_concurrency(concurrency)
    verify_waves()
    events = asset.get_editor_property("events")
    if len(events) != len(data["events"]):
        fail("DA_BreakAssets has %d events, expected %d" % (len(events), len(data["events"])))
    file_count = 0
    for event, expected in zip(events, data["events"]):
        if str(event.get_editor_property("name")) != expected["name"]:
            fail("event %s differs" % expected["name"])
        files = event.get_editor_property("files")
        if len(files) != len(expected["files"]):
            fail("event %s has %d files, expected %d" % (expected["name"], len(files), len(expected["files"])))
        for item, f in zip(files, expected["files"]):
            wave = item.get_editor_property("wave")
            if str(item.get_editor_property("name")) != f["name"] or path_of(wave) != wave_package(f["name"]):
                fail("event %s file %s references the wrong wave" % (expected["name"], f["name"]))
            if not close(item.get_editor_property("volume"), f["volume"]) or \
                    not close(item.get_editor_property("pitch"), f["pitch"]):
                fail("event %s file %s differs in volume or pitch" % (expected["name"], f["name"]))
            file_count += 1
    blocks = asset.get_editor_property("blocks")
    if len(blocks) != len(data["blocks"]):
        fail("DA_BreakAssets has %d blocks, expected %d" % (len(blocks), len(data["blocks"])))
    for entry, block in zip(blocks, data["blocks"]):
        same = (str(entry.get_editor_property("id")) == block["id"] and
                entry.get_editor_property("break_event") == block["break_event"] and
                entry.get_editor_property("hit_event") == block["hit_event"] and
                entry.get_editor_property("place_event") == block["place_event"] and
                close(entry.get_editor_property("volume"), block["volume"]) and
                close(entry.get_editor_property("pitch"), block["pitch"]) and
                str(entry.get_editor_property("particle")) == block["particle"] and
                bool(entry.get_editor_property("particle_tinted")) == block["particle_tinted"] and
                list(entry.get_editor_property("override_states")) == block["override_states"] and
                [str(n) for n in entry.get_editor_property("override_particles")] == block["override_particles"] and
                [bool(b) for b in entry.get_editor_property("override_tinted")] == block["override_tinted"])
        if not same:
            fail("block entry %s differs from the break data" % block["id"])
    return len(events), file_count, len(blocks)


def main():
    package = BREAK_ROOT + "/DA_BreakAssets"
    if assets.does_asset_exist(package):
        existing = assets.load_asset(package)
        if existing.get_editor_property("source_sha256") != digest:
            fail("DA_BreakAssets was made from other break data; move it away deliberately and rerun")
        counts = verify(existing)
        unreal.log("BREAK_IMPORT PASS: DA_BreakAssets matches the break data (%d events, %d files, %d blocks); "
                   "not rebuilt" % counts)
        return
    cracks = ensure_cracks()
    waves, imported = ensure_waves()
    attenuation = ensure_attenuation()
    concurrency = ensure_concurrency()
    folder, short = ProjectPaths.split(package)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.FoundationBreakAssets)
    asset = tools.create_asset(short, folder, unreal.FoundationBreakAssets, factory)
    if not asset:
        fail("could not create " + package)
    fill(asset, cracks, waves, attenuation, concurrency)
    if not assets.save_loaded_asset(asset, False):
        fail("save failed: " + package)
    counts = verify(asset)
    unreal.log("BREAK_IMPORT PASS: DA_BreakAssets built (%d events, %d files, %d blocks; %d waves imported, "
               "%d layers verified)" % (counts + (imported, len(data["layer_hashes"]))))


main()
