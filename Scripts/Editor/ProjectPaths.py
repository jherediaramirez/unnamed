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
    """Long package name of a Singleplayer catalog world, e.g. world_map("super-flat")."""
    path = _settings.get_world_map_package(world_id)
    if not path:
        raise RuntimeError("Unknown world ID: " + world_id)
    return path


def input_asset(property_name):
    """Package path of an input asset setting, e.g. input_asset("OnFootContext")."""
    path = _settings.get_input_asset_path(property_name)
    if not path:
        raise RuntimeError("Unknown input asset setting: " + property_name)
    return path.split(".")[0]


def split(package):
    """("/Game/Input/Actions", "IA_Move") for "/Game/Input/Actions/IA_Move"."""
    folder, name = package.rsplit("/", 1)
    return folder, name
