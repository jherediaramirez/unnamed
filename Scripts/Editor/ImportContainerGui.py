"""Import the survival inventory screen's art (survival step 5b) from Reference/minecraft-menu as standalone GUI
textures under GuiRoot/Container (UUnnamedProjectSettings::GuiRoot): container/inventory.png, the empty armor and
offhand slot silhouettes and the recipe-book button; the crafting table's container/crafting_table.png (survival
step 7); the furnace's container/furnace.png with its flame and arrow sprites (T1 step 8a2); the smoker's
and blast furnace's backgrounds and sprites (T1 step 8b); and the vanilla button
sprites widget/button, button_highlighted and button_disabled (the death screen, survival step 11a). Same pixel
settings as ImportBlocks.py's creative textures.

Additive and idempotent: an existing texture is verified (its settings and the SHA-256 of the PNG it came from, kept
in a metadata tag), never reimported. A changed source refuses; move the old texture away deliberately first.

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\ImportContainerGui.py" -unattended -nop4 -nosplash
"""
import hashlib
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402

ROOT = Path(unreal.Paths.project_dir()).resolve()
TEXTURES = ROOT / "Reference/minecraft-menu/assets/minecraft/textures"
# Paths under textures/, without ".png"; the asset is GuiRoot/Container/<path>.
SOURCES = ["gui/container/inventory"] + ["gui/sprites/container/slot/" + name for name in
                                         ("helmet", "chestplate", "leggings", "boots", "shield")] + \
          ["gui/sprites/recipe_book/button", "gui/sprites/recipe_book/button_highlighted",
           "gui/container/crafting_table", "gui/container/furnace",
           "gui/sprites/container/furnace/lit_progress", "gui/sprites/container/furnace/burn_progress"] + \
          [part for name in ("smoker", "blast_furnace") for part in
           ("gui/container/" + name, "gui/sprites/container/" + name + "/lit_progress",
            "gui/sprites/container/" + name + "/burn_progress")] + \
          ["gui/sprites/widget/" + name for name in ("button", "button_highlighted", "button_disabled")]
# From the HUD reference's jar textures: the pumpkin's camera overlay (survival 19a2).
HUD_TEXTURES = ROOT / "Reference/minecraft-hud-26.3/assets/minecraft/textures"
HUD_SOURCES = ["misc/pumpkinblur"]
TAG = "SourceSha256"
assets = unreal.EditorAssetLibrary
SETTINGS = dict(compression_settings=unreal.TextureCompressionSettings.TC_EDITOR_ICON,
                mip_gen_settings=unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
                filter=unreal.TextureFilter.TF_NEAREST, lod_group=unreal.TextureGroup.TEXTUREGROUP_UI,
                srgb=True, never_stream=True, power_of_two_mode=unreal.TexturePowerOfTwoSetting.NONE)


def fail(message):
    raise RuntimeError("CONTAINER_GUI_IMPORT FAIL: " + message)


def main():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    created = verified = 0
    for relative, root in [(path, TEXTURES) for path in SOURCES] + [(path, HUD_TEXTURES) for path in HUD_SOURCES]:
        png = root / (relative + ".png")
        if not png.is_file():
            fail("missing reference texture: " + str(png))
        digest = hashlib.sha256(png.read_bytes()).hexdigest()
        package = ProjectPaths.gui_asset("Container/" + relative)
        if assets.does_asset_exist(package):
            texture = assets.load_asset(package)
            for key, value in SETTINGS.items():
                if texture.get_editor_property(key) != value:
                    fail("existing texture setting differs: %s %s" % (package, key))
            if assets.get_metadata_tag(texture, TAG) != digest:
                fail("existing texture came from a different source: " + package)
            verified += 1
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
            fail("import failed: " + package)
        for key, value in SETTINGS.items():
            texture.set_editor_property(key, value)
        assets.set_metadata_tag(texture, TAG, digest)
        if not assets.save_loaded_asset(texture, False):
            fail("save failed: " + package)
        created += 1
    unreal.log("CONTAINER_GUI_IMPORT PASS created=%d verified=%d" % (created, verified))


main()
