"""Import the 26.3 item reference: the item sprites as GUI textures, the same sprites as texture array layers for held
items (T_ItemTextures, 16 x 16, and T_ItemTextures32, the spears' 32 x 32 in-hand sprites) and the item registry asset
(DA_Items).

Additive and idempotent, like ImportBlocks.py: assets that exist are verified, never rebuilt. An import from
different reference data refuses to touch existing assets (move them away first, deliberately). Run after the
editor target has compiled and with Config/Tags/MinecraftItemTags.ini in place (ItemImportData.py --write-tags):

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\ImportItems.py" -unattended -nop4 -nosplash
"""
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402
from ItemImportData import TAGS_INI, build, verify_hashes  # noqa: E402

ROOT = Path(unreal.Paths.project_dir()).resolve()
assets = unreal.EditorAssetLibrary
Import = unreal.FoundationBlockImport
ARRAYS = (("T_ItemTextures", "array_layers", 16, "item_textures"), ("T_ItemTextures32", "array_layers32", 32,
                                                                   "item_textures32"))


def fail(message):
    raise RuntimeError("ITEM_IMPORT FAIL: " + message)


try:
    files = verify_hashes(ROOT)
except RuntimeError as error:
    fail(str(error))
data = build(ROOT)
if (ROOT / TAGS_INI).read_text(encoding="utf-8") != data["tags_ini"]:
    fail(TAGS_INI + " is missing or stale: run ItemImportData.py --write-tags, then restart the editor")


def import_sprites():
    """Every item sprite under ItemRoot/Gui/item with BlockRoot/Gui's pixel settings, for the creative inventory.
    Additive: existing textures are verified, never reimported. Returns how many were created."""
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    settings = dict(compression_settings=unreal.TextureCompressionSettings.TC_EDITOR_ICON,
                    mip_gen_settings=unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
                    filter=unreal.TextureFilter.TF_NEAREST, lod_group=unreal.TextureGroup.TEXTUREGROUP_UI,
                    srgb=True, never_stream=True, power_of_two_mode=unreal.TexturePowerOfTwoSetting.NONE)
    created = 0
    for png in data["sprite_pngs"]:
        package = ProjectPaths.item_asset("Gui/item/" + png.stem)
        if assets.does_asset_exist(package):
            texture = assets.load_asset(package)
            for key, value in settings.items():
                if texture.get_editor_property(key) != value:
                    fail("existing item sprite setting differs: %s %s" % (package, key))
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
            fail("sprite import failed: " + package)
        for key, value in settings.items():
            texture.set_editor_property(key, value)
        if not assets.save_loaded_asset(texture, False):
            fail("save failed: " + package)
        created += 1
    return created


def check_layers(arrays):
    """Every sprite's layer holds the jar's pixels: its decoded RGBA against the reference's hash."""
    for texture in data["textures"]:
        array = arrays[16 if texture["width"] == 16 else 32]
        if Import.hash_layer(array, texture["layer"]) != texture["rgba_sha256"]:
            fail("layer pixels differ from the reference: %s (layer %d)" % (texture["name"], texture["layer"]))


def struct(struct_type, values):
    entry = struct_type()
    for key, value in values.items():
        entry.set_editor_property(key, value)
    return entry


created = import_sprites()

registry_path = ProjectPaths.item_asset("DA_Items")
if assets.does_asset_exist(registry_path):
    existing = assets.load_asset(registry_path)
    if existing.get_editor_property("source_sha256") != data["source_sha256"]:
        fail("DA_Items was imported from different reference data; preserve it and migrate explicitly")
    ids = [str(item.get_editor_property("id")) for item in existing.get_editor_property("items")]
    if ids != [item["id"] for item in data["items"]] or \
            len(existing.get_editor_property("models")) != len(data["models"]) or \
            len(existing.get_editor_property("textures")) != len(data["textures"]):
        fail("DA_Items does not hold the reference's items, models and textures; move it away and reimport")
    arrays = {}
    for name, key, size, prop in ARRAYS:
        # Soft references: load the asset, then check DA_Items names it.
        array = assets.load_asset(ProjectPaths.item_asset(name))
        held = existing.get_editor_property(prop)
        if not array or not held or held.get_path_name() != array.get_path_name():
            fail("DA_Items does not point at %s; move it away and reimport" % name)
        arrays[size] = array
    if [t.get_editor_property("layer") for t in existing.get_editor_property("textures")] !=             [t["layer"] for t in data["textures"]] or dict(existing.get_editor_property("lang")) != data["lang"]:
        fail("DA_Items' texture layers or lang keys differ from the reference; move it away and reimport")
    check_layers(arrays)
    unreal.log("ITEM_IMPORT PASS: %d reference files match; DA_Items and its arrays match the reference; %d sprites "
               "created, existing assets were not rebuilt" % (files, created))
    sys.exit(0)

for name, _, _, _ in ARRAYS:
    if assets.does_asset_exist(ProjectPaths.item_asset(name)):
        fail(name + " exists without DA_Items: an interrupted import; remove it deliberately and rerun")
arrays = {}
for name, key, size, _ in ARRAYS:
    # Mip 0 only: vanilla's item mips are not measured (26.3's generator is ported for block sprites only).
    array = Import.create_texture_array(ProjectPaths.item_asset(name))
    layers = [str(png) for png in data[key]]
    error = Import.build_texture_array(array, layers, [0] * len(layers), ["auto"] * len(layers), [0.0] * len(layers),
                                       size, 1)
    if error:
        fail(name + ": " + error)
    arrays[size] = array
check_layers(arrays)

items = []
for item in data["items"]:
    values = dict(item)
    values["rules"] = [struct(unreal.FoundationItemRuleAsset, rule) for rule in item["rules"]]
    values["creative"] = [struct(unreal.FoundationItemCreativeAsset, c) for c in item["creative"]]
    values["models"] = [struct(unreal.FoundationItemModelCaseAsset, case) for case in item["models"]]
    values["modifiers"] = [struct(unreal.FoundationItemModifierAsset, m) for m in item["modifiers"]]
    items.append(struct(unreal.FoundationItemAsset, values))
models = []
for model in data["models"]:
    values = dict(model)
    values["display"] = [struct(unreal.FoundationItemDisplayAsset,
                                {"context": d["context"], "rotation": unreal.Vector(*d["rotation"]),
                                 "translation": unreal.Vector(*d["translation"]), "scale": unreal.Vector(*d["scale"])})
                         for d in model["display"]]
    models.append(struct(unreal.FoundationItemModelAsset, values))
textures = [struct(unreal.FoundationItemTextureAsset, texture) for texture in data["textures"]]

factory = unreal.DataAssetFactory()
factory.set_editor_property("data_asset_class", unreal.FoundationItemAssets)
folder, name = ProjectPaths.split(registry_path)
registry = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.FoundationItemAssets, factory)
registry.set_editor_property("source_sha256", data["source_sha256"])
registry.set_editor_property("items", items)
registry.set_editor_property("models", models)
registry.set_editor_property("textures", textures)
registry.set_editor_property("item_textures", arrays[16])
registry.set_editor_property("item_textures32", arrays[32])
registry.set_editor_property("lang", data["lang"])
for asset in (arrays[16], arrays[32], registry):
    if not assets.save_loaded_asset(asset, False):
        fail("save failed: " + asset.get_path_name())
unreal.log("ITEM_IMPORT PASS: %d reference files match; %d items, %d models, %d textures (%d + %d array layers), "
           "%d lang keys, %d sprites created" % (files, len(items), len(models), len(textures), len(data["array_layers"]),
                                                len(data["array_layers32"]), len(data["lang"]), created))
