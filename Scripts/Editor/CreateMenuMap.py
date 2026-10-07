import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # Unreal does not add the script folder.
import ProjectPaths  # noqa: E402

path = ProjectPaths.menu_map()
if unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.log("Preserving existing menu map: " + path)
else:
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.new_level(path):
        raise RuntimeError("Could not create empty menu map")
    world = unreal.EditorLevelLibrary.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.MenuGameMode)
    if not levels.save_current_level():
        raise RuntimeError("Could not save menu map")
    unreal.log("Created empty UI-only map: " + path)
