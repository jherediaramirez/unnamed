"""Import the ore look textures (Art/OreLooks, made by Scripts/OreLooks.py) as /Game/Blocks/T_OreLooks, the texture
array the chunk material's ore looks sample (voxel.Look.OreHost, voxel.Look.OreGlint; Docs/VoxelWorld.md, "Ore looks").

Layer i is slot i of the manifest. Slots with 26.3's mips get them from mip 0 by the ore texture's own strategy
(textures.json), as ImportBlocks.py builds T_BlockTextures; the ore-aware slots take their mips from the strip.
Additive, like ImportBlocks.py: an existing asset is verified against the manifest (its metadata tag and every layer's
mip 0), never rebuilt; one made from other data is refused (move it away deliberately). The ore-aware mips are checked
against the strip by the test Unnamed.Voxel.OreLooks.Texture. Run before CreateBlockMaterials.py:

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\ImportOreLooks.py" -unattended -nop4 -nosplash
"""
import hashlib
import json
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402

ROOT = Path(unreal.Paths.project_dir()).resolve()
ART = ROOT / "Art/OreLooks"
TAG = "OreLooksStripSha256"
Import = unreal.FoundationBlockImport
assets = unreal.EditorAssetLibrary


def fail(message):
    raise RuntimeError("ORE_LOOKS_IMPORT FAIL: " + message)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


manifest = json.loads((ART / "manifest.json").read_text(encoding="utf-8"))
strip = ART / manifest["strip"]
if manifest["generator_sha256"] != sha256(ROOT / manifest["generator"]):
    fail("Art/OreLooks is older than Scripts/OreLooks.py: rerun the generator")
if manifest["strip_sha256"] != sha256(strip):
    fail(strip.name + " does not match manifest.json: rerun the generator")
slots = manifest["slots"]
if [s["slot"] for s in slots] != list(range(len(slots))):
    fail("manifest slots are not contiguous")


def verify(array):
    for s in slots:
        if Import.hash_layer(array, s["slot"]).lower() != s["rgba_sha256"]:
            fail("layer %d (%s %s on %s) differs from the manifest" % (s["slot"], s["mode"], s["texture"], s["host"]))


package = ProjectPaths.block_asset("T_OreLooks")
if assets.does_asset_exist(package):
    existing = assets.load_asset(package)
    if assets.get_metadata_tag(existing, TAG) != manifest["strip_sha256"]:
        fail("T_OreLooks was made from other ore look data; move it away deliberately and rerun")
    verify(existing)
    unreal.log("ORE_LOOKS_IMPORT PASS: T_OreLooks matches the manifest (%d layers); not rebuilt" % len(slots))
    sys.exit(0)

textures = {t["name"]: t for t in json.loads(
    (ROOT / "Reference/minecraft-blocks-26.3/textures.json").read_text(encoding="utf-8"))["textures"]}
frames, strategies, biases = [], [], []
for s in slots:
    frames.append(s["slot"] * manifest["frames_per_slot"])
    if s["mips"] == "explicit":
        strategies.append("explicit")
        biases.append(0.0)
    else:
        texture = textures[s["texture"]]
        strategies.append(texture["mipmap_strategy"])
        biases.append(float(texture.get("alpha_cutoff_bias", 0.0)))
array = Import.create_texture_array(package)
if not array:
    fail("could not create " + package)
error = Import.build_texture_array(array, [str(strip)] * len(slots), frames, strategies, biases,
                                   manifest["frame_size"], manifest["mip_count"])
if error:
    fail(error)
verify(array)
assets.set_metadata_tag(array, TAG, manifest["strip_sha256"])
if not assets.save_loaded_asset(array, False):
    fail("save failed: " + package)
unreal.log("ORE_LOOKS_IMPORT PASS: T_OreLooks built, %d layers verified against the manifest" % len(slots))
