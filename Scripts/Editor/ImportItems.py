"""Import the 26.3 item reference: the item sprites as GUI textures, the same sprites as texture array layers for held
items (T_ItemTextures, 16 x 16, and T_ItemTextures32, the spears' 32 x 32 in-hand sprites) and the item registry asset
(DA_Items).

Additive and idempotent, like ImportBlocks.py: assets that exist are verified, never rebuilt. An import from
different reference data refuses to touch existing assets (move them away first, deliberately); the one exception
is the explicit loot migrations (UNNAMED_ITEMS_MIGRATE=loot from the pre-loot reference, items S5b; =loot2 from the
S5b reference, loot step 2a). Run after the
editor target has compiled and with Config/Tags/MinecraftItemTags.ini in place (ItemImportData.py --write-tags):

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\ImportItems.py" -unattended -nop4 -nosplash
"""
import os
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
# The reference's digest before items S5b added the loot tables (ItemImportData.source_sha256).
PRE_LOOT_SHA256 = "4b17236f19f25bab4e23320c3a82e69989010fbf35d2c635e7f6e2cb5ffccc39"
# The reference's digest before loot step 2a added every imported block's loot table (the 18 ores' S5b reference).
PRE_LOOT2_SHA256 = "8d33db5560fe97bf2fbb744035c6e063d3c41ab2220f709efd6aa81a07664129"
# The reference's digest before survival step 6 added the recipes (new files only; every file DA_Items is imported
# from stayed byte-identical): a DA_Items imported from it is verified as below and left as it is.
PRE_RECIPES_SHA256 = "e97651c71fc169051dadfc6c0b5063c231065e1beeb7bb549dc1fd2fc4c1f2b3"


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
        package = ProjectPaths.item_asset("Gui/%s/%s" % (png.parent.name, png.stem))
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


def loot_structs():
    """DA_Items' loot tables and predicates (items S5b; loot step 2a's condition nodes and modifiers), from
    loot_tables.json."""
    def modifiers(values):
        return [struct(unreal.FoundationLootModifierAsset, m) for m in values]
    tables = []
    for table in data["loot_tables"]:
        pools = [struct(unreal.FoundationLootPoolAsset, {
            "rolls": pool["rolls"], "tests": pool["tests"], "modifiers": modifiers(pool["modifiers"]),
            "candidates": [struct(unreal.FoundationLootCandidateAsset, dict(c, modifiers=modifiers(c["modifiers"])))
                           for c in pool["candidates"]]})
            for pool in table["pools"]]
        tables.append(struct(unreal.FoundationLootTableAsset, {
            "block": table["block"], "pools": pools, "modifiers": modifiers(table["modifiers"]),
            "conditions": [struct(unreal.FoundationLootConditionAsset, n) for n in table["conditions"]]}))
    return tables, [struct(unreal.FoundationLootPredicateAsset, p) for p in data["loot_predicates"]]


LOOT_KEYS = (
    (unreal.FoundationLootTableAsset, ("block", "pools", "conditions", "modifiers")),
    (unreal.FoundationLootPoolAsset, ("rolls", "candidates", "tests", "modifiers")),
    (unreal.FoundationLootCandidateAsset, ("item", "conditions", "count_min", "count_max", "bonus_formula",
                                           "bonus_enchantment", "bonus_multiplier", "explosion_decay", "tests",
                                           "modifiers")),
    (unreal.FoundationLootConditionAsset, ("type", "name", "terms", "state_keys", "state_values", "chances", "items",
                                           "offset")),
    (unreal.FoundationLootModifierAsset, ("type", "tests", "min", "max", "add", "has_min", "has_max", "bonus_formula",
                                          "bonus_enchantment", "bonus_multiplier")),
    (unreal.FoundationLootPredicateAsset, ("name", "items", "enchantments", "enchantment_min_levels")))


def loot_view(tables, predicates):
    """Every loot property as text, so an existing DA_Items' loot compares with loot_structs()' fresh structs."""
    def plain(value):
        if isinstance(value, (list, unreal.Array)):
            return [plain(v) for v in value]
        for struct_type, keys in LOOT_KEYS:
            if isinstance(value, struct_type):
                return [plain(value.get_editor_property(key)) for key in keys]
        return str(value)
    return plain(tables), plain(predicates)


created = import_sprites()

registry_path = ProjectPaths.item_asset("DA_Items")
if assets.does_asset_exist(registry_path):
    existing = assets.load_asset(registry_path)
    # Items S5b: the S5b rebuild of the reference only added the ores' loot tables (every file DA_Items was imported
    # from stayed byte-identical), so a DA_Items imported from the reference before it migrates in place when asked:
    # it is verified as below, then only its loot tables and digest are written.
    # Loot step 2a: its rebuild only appended loot tables and a predicate the same way, so a DA_Items imported from
    # the S5b reference migrates when asked (UNNAMED_ITEMS_MIGRATE=loot2): verified as below, its 18 loot tables and
    # predicate against the new reference's first ones, then only its loot tables, predicates and digest are written.
    source = existing.get_editor_property("source_sha256")
    migrate = source == PRE_LOOT_SHA256 and \
        os.environ.get("UNNAMED_ITEMS_MIGRATE") == "loot" and not existing.get_editor_property("loot_tables")
    migrate2 = source == PRE_LOOT2_SHA256 and os.environ.get("UNNAMED_ITEMS_MIGRATE") == "loot2"
    if source not in (data["source_sha256"], PRE_RECIPES_SHA256) and not migrate and not migrate2:
        fail("DA_Items was imported from different reference data; preserve it and migrate explicitly (from the "
             "pre-loot reference: set UNNAMED_ITEMS_MIGRATE=loot; from the S5b reference: loot2)")
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
    tables, predicates = loot_structs()
    if migrate2:
        held_tables, held_predicates = existing.get_editor_property("loot_tables"), \
            existing.get_editor_property("loot_predicates")
        if len(held_tables) != 18 or len(held_predicates) != 1 or \
                loot_view(held_tables, held_predicates) != loot_view(tables[:18], predicates[:1]):
            fail("DA_Items' S5b loot tables differ from the reference's first 18; move it away and reimport")
    if migrate or migrate2:
        existing.set_editor_property("loot_tables", tables)
        existing.set_editor_property("loot_predicates", predicates)
        existing.set_editor_property("source_sha256", data["source_sha256"])
        if not assets.save_loaded_asset(existing, False):
            fail("save failed: " + existing.get_path_name())
        unreal.log("ITEM_IMPORT PASS: %d reference files match; DA_Items migrated from the %s reference: %d loot "
                   "tables and %d predicates written, nothing else changed"
                   % (files, "S5b" if migrate2 else "pre-loot", len(tables), len(predicates)))
        sys.exit(0)
    if loot_view(existing.get_editor_property("loot_tables"), existing.get_editor_property("loot_predicates")) != \
            loot_view(tables, predicates):
        fail("DA_Items' loot tables differ from the reference; move it away and reimport")
    unreal.log("ITEM_IMPORT PASS: %d reference files match; DA_Items (with %d loot tables) and its arrays match the "
               "reference; %d sprites created, existing assets were not rebuilt" % (files, len(tables), created))
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
loot_tables, loot_predicates = loot_structs()
registry.set_editor_property("loot_tables", loot_tables)
registry.set_editor_property("loot_predicates", loot_predicates)
for asset in (arrays[16], arrays[32], registry):
    if not assets.save_loaded_asset(asset, False):
        fail("save failed: " + asset.get_path_name())
unreal.log("ITEM_IMPORT PASS: %d reference files match; %d items, %d models, %d textures (%d + %d array layers), "
           "%d lang keys, %d sprites created" % (files, len(items), len(models), len(textures), len(data["array_layers"]),
                                                len(data["array_layers32"]), len(data["lang"]), created))
