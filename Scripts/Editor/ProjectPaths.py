"""Asset paths owned by UUnnamedProjectSettings (C++ defaults, overridable in DefaultGame.ini).

Editor scripts import this module instead of repeating /Game/... paths. Run them through
UnrealEditor-Cmd -run=pythonscript after building the editor target.
"""
import unreal

_settings = unreal.UnnamedProjectSettings


def menu_map():
    """Long package name of the menu level, e.g. /Game/Menu/L_Menu."""
    return _settings.get_menu_map_package()


def world_map(world_id):
    """Long package name of a Singleplayer catalog world, e.g. world_map("playground")."""
    path = _settings.get_world_map_package(world_id)
    if not path:
        raise RuntimeError("Unknown world ID: " + world_id)
    return path


def generated_map():
    """Long package name of the generated voxel world's map, e.g. /Game/Maps/L_Generated."""
    return _settings.get_generated_map_package()


def input_asset(property_name):
    """Package path of an input asset setting, e.g. input_asset("OnFootContext")."""
    path = _settings.get_input_asset_path(property_name)
    if not path:
        raise RuntimeError("Unknown input asset setting: " + property_name)
    return path.split(".")[0]


def dev_grid_material():
    """Package of the dev grid material (UUnnamedProjectSettings::DevGridMaterial), e.g. /Game/DevGrid/M_DevGrid."""
    return input_asset("DevGridMaterial")  # GetInputAssetPath resolves any soft-object setting by property name.


def split(package):
    """("/Game/Input/Actions", "IA_Move") for "/Game/Input/Actions/IA_Move"."""
    folder, name = package.rsplit("/", 1)
    return folder, name


def hud_registry():
    return _settings.get_hud_registry_path().split('.')[0]


def hud_texture(relative_texture):
    return _settings.get_hud_texture_package(relative_texture)


def block_asset(asset_name):
    """Package of a generated block asset under UUnnamedProjectSettings::BlockRoot, e.g. block_asset("DA_Blocks")."""
    return _settings.get_block_asset_package(asset_name)


def item_asset(asset_name):
    """Package of a generated item asset under UUnnamedProjectSettings::ItemRoot, e.g. item_asset("DA_Items")."""
    return _settings.get_item_asset_package(asset_name)


def gui_asset(asset_name):
    """Package of a container screen texture under UUnnamedProjectSettings::GuiRoot, e.g.
    gui_asset("Container/gui/container/inventory")."""
    return _settings.get_gui_asset_package(asset_name)


def break_asset(asset_name):
    """Package of a breaking-effects asset under UUnnamedProjectSettings::BreakRoot, e.g. break_asset("DA_BreakAssets")."""
    return _settings.get_break_asset_package(asset_name)


def sound_asset(asset_name):
    """Package of a sound asset under UUnnamedProjectSettings::SoundRoot, e.g. sound_asset("DA_Sounds")."""
    return _settings.get_sound_asset_package(asset_name)


def entity_asset(asset_name):
    """Package of an entity asset under UUnnamedProjectSettings::EntityRoot, e.g. entity_asset("T_ExperienceOrb")."""
    return _settings.get_entity_asset_package(asset_name)


def player_body_asset(asset_name):
    """Package of a player body asset under UUnnamedProjectSettings::PlayerBodyRoot, e.g. player_body_asset("BS_Idle")."""
    return _settings.get_player_body_asset_package(asset_name)
