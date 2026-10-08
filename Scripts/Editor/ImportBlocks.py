"""Import the 26.3 block reference: texture arrays, the icon atlas and the block registry asset (DA_Blocks).

Additive and idempotent, like ImportHud.py: assets that exist are verified, never rebuilt. An import from
different reference data refuses to touch existing assets (move them away first, deliberately). Run after the
editor target has compiled and with Config/Tags/MinecraftBlockTags.ini in place (BlockImportData.py
--write-tags), then run CreateBlockMaterials.py:

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\ImportBlocks.py" -unattended -nop4 -nosplash
"""
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402
from BlockImportData import TAGS_INI, build  # noqa: E402

ROOT = Path(unreal.Paths.project_dir()).resolve()
BLOCK_MIPS, FLOW_MIPS, ICON_COLUMNS = 5, 6, 32
Import = unreal.FoundationBlockImport
assets = unreal.EditorAssetLibrary


def fail(message):
    raise RuntimeError("BLOCK_IMPORT FAIL: " + message)


def enum(enum_type, name):
    return getattr(enum_type, name)


def rgba(values):
    return unreal.Color(r=values[0], g=values[1], b=values[2], a=values[3])


data = build(ROOT)
if (ROOT / TAGS_INI).read_text(encoding="utf-8") != data["tags_ini"]:
    fail(TAGS_INI + " is missing or stale: run BlockImportData.py --write-tags, then restart the editor")

def import_gui():
    """The creative inventory art (GUI backgrounds, tab and scroller sprites, tab icon items) under BlockRoot/Gui,
    with the HUD's pixel settings. Additive: existing textures are verified, never reimported."""
    reference = ROOT / "Reference/minecraft-blocks-26.3/assets/minecraft/textures"
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    created = 0
    for png in sorted(list((reference / "gui").rglob("*.png")) + list((reference / "item").rglob("*.png"))):
        relative = png.relative_to(reference).with_suffix("").as_posix()
        package = ProjectPaths.block_asset("Gui/" + relative)
        settings = dict(compression_settings=unreal.TextureCompressionSettings.TC_EDITOR_ICON,
                        mip_gen_settings=unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
                        filter=unreal.TextureFilter.TF_NEAREST, lod_group=unreal.TextureGroup.TEXTUREGROUP_UI,
                        srgb=True, never_stream=True, power_of_two_mode=unreal.TexturePowerOfTwoSetting.NONE)
        if assets.does_asset_exist(package):
            texture = assets.load_asset(package)
            for key, value in settings.items():
                if texture.get_editor_property(key) != value:
                    fail("existing GUI texture setting differs: %s %s" % (package, key))
            continue
        folder, name = ProjectPaths.split(package)
        task = unreal.AssetImportTask()
        for key, value in (("filename", str(png)), ("destination_path", folder), ("destination_name", name),
                           ("automated", True), ("replace_existing", False), ("save", False),
                           ("factory", unreal.TextureFactory())):
            task.set_editor_property(key, value)
        tools.import_asset_tasks([task])
        texture = assets.load_asset(package)
        if not texture:
            fail("GUI import failed: " + package)
        for key, value in settings.items():
            texture.set_editor_property(key, value)
        if not assets.save_loaded_asset(texture, False):
            fail("save failed: " + package)
        created += 1
    unreal.log("BLOCK_IMPORT GUI: %d textures created, the rest verified" % created)


import_gui()

registry_path = ProjectPaths.block_asset("DA_Blocks")
if assets.does_asset_exist(registry_path):
    existing = assets.load_asset(registry_path)
    if existing.get_editor_property("source_sha256") != data["source_sha256"]:
        fail("DA_Blocks was imported from different reference data; preserve it and migrate explicitly")
    unreal.log("BLOCK_IMPORT PASS: DA_Blocks matches the reference; existing assets were not rebuilt")
    sys.exit(0)

for name in ("T_BlockTextures", "T_WaterFlowTextures", "T_BlockIcons"):
    if assets.does_asset_exist(ProjectPaths.block_asset(name)):
        fail(name + " exists without DA_Blocks: an interrupted import; remove it deliberately and rerun")


def build_array(name, layers, frame_size, mips):
    array = Import.create_texture_array(ProjectPaths.block_asset(name))
    error = Import.build_texture_array(array, [l[0] for l in layers], [l[1] for l in layers], [l[2] for l in layers],
                                       frame_size, mips)
    if error:
        fail(name + ": " + error)
    return array


block_textures = build_array("T_BlockTextures", data["block_layers"], 16, BLOCK_MIPS)
flow_textures = build_array("T_WaterFlowTextures", data["flow_layers"], 32, FLOW_MIPS)
# Mip 0 must be the jar's pixels: every layer against the hash the reference recorded with an independent decoder.
for texture in data["textures"][1:]:
    array = flow_textures if texture["water_flow"] else block_textures
    for frame, expected in enumerate(texture["layer_rgba_sha256"]):
        if Import.hash_layer(array, texture["first_layer"] + frame) != expected:
            fail("layer pixels differ from the reference: %s frame %d" % (texture["name"], frame))
icons = Import.create_texture(ProjectPaths.block_asset("T_BlockIcons"))
error = Import.build_icon_atlas(icons, block_textures, ICON_COLUMNS)
if error:
    fail("T_BlockIcons: " + error)

texture_assets = []
for texture in data["textures"]:
    entry = unreal.FoundationBlockTextureAsset()
    entry.set_editor_property("name", texture["name"])
    entry.set_editor_property("water_flow", texture["water_flow"])
    entry.set_editor_property("first_layer", texture["first_layer"])
    entry.set_editor_property("frame_count", texture["frame_count"])
    entry.set_editor_property("frame_indices", texture["frame_indices"])
    entry.set_editor_property("frame_ticks", texture["frame_ticks"])
    entry.set_editor_property("interpolate", texture["interpolate"])
    texture_assets.append(entry)

block_assets = []
for block in data["blocks"]:
    entry = unreal.FoundationBlockAsset()
    entry.set_editor_property("id", block["id"])
    entry.set_editor_property("display_name", block["name"])
    for key in ("protocol_id", "min_state_id", "default_state_id", "hardness", "blast_resistance",
                "requires_correct_tool_for_drops", "light_emission", "replaceable", "state_faces"):
        entry.set_editor_property(key, block[key])
    properties = []
    for name, values in block["properties"]:
        prop = unreal.FoundationBlockAssetProperty()
        prop.set_editor_property("name", name)
        prop.set_editor_property("values", values)
        properties.append(prop)
    entry.set_editor_property("properties", properties)
    entry.set_editor_property("layer", enum(unreal.FoundationRenderLayer, block["layer"]))
    entry.set_editor_property("placement", enum(unreal.FoundationPlacementRule, block["placement"]))
    entry.set_editor_property("tint", rgba(block["tint"]))
    entry.set_editor_property("state_shapes", [enum(unreal.FoundationBlockShape, s) for s in block["state_shapes"]])
    tags, error = Import.make_tags(block["tags"])
    if error:
        fail(block["id"] + ": " + error)
    entry.set_editor_property("tags", tags)
    if block["particle_texture"]:
        layer = data["textures"][data["index"][block["particle_texture"]]]["first_layer"]
        entry.set_editor_property("particle_color", Import.average_layer_color(block_textures, layer, rgba(block["tint"])))
    if block["item"]:
        entry.set_editor_property("item", block["item"])
        entry.set_editor_property("item_name", block["item_name"])
        entry.set_editor_property("item_max_stack", block["item_max_stack"])
        entry.set_editor_property("item_tint", rgba(block["item_tint"]))
    block_assets.append(entry)

tabs = []
for tab in data["creative_tabs"]:
    entry = unreal.FoundationCreativeTabAsset()
    for key in ("id", "title", "row", "column", "type", "aligned_right", "show_title", "can_scroll", "icon", "items"):
        entry.set_editor_property(key, tab[key])
    tabs.append(entry)

factory = unreal.DataAssetFactory()
factory.set_editor_property("data_asset_class", unreal.FoundationBlockAssets)
folder, name = ProjectPaths.split(registry_path)
registry = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.FoundationBlockAssets, factory)
registry.set_editor_property("source_sha256", data["source_sha256"])
registry.set_editor_property("blocks", block_assets)
registry.set_editor_property("textures", texture_assets)
registry.set_editor_property("creative_tabs", tabs)
registry.set_editor_property("block_textures", block_textures)
registry.set_editor_property("water_flow_textures", flow_textures)
registry.set_editor_property("icon_atlas", icons)
registry.set_editor_property("icon_atlas_columns", ICON_COLUMNS)
registry.set_editor_property("water_color", rgba(data["water_color"]))
for asset in (block_textures, flow_textures, icons, registry):
    if not assets.save_loaded_asset(asset, False):
        fail("save failed: " + asset.get_path_name())
unreal.log("BLOCK_IMPORT PASS: %d blocks, %d textures, %d + %d layers verified against the reference"
           % (len(block_assets), len(texture_assets), len(data["block_layers"]), len(data["flow_layers"])))
