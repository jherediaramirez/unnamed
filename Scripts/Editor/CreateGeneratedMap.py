"""Author the generated voxel world's map once, or verify it when it already exists.

/Game/Maps/L_Generated (UUnnamedProjectSettings::GeneratedMap) is the procedural map every generated world opens. It
holds only what cannot be built at runtime: the AVoxelWorld actor (it generates and meshes the terrain), a PlayerStart,
the Playground's daylight rig and a thin height fog. Phase 2 shows a fixed test area there; it is not a catalog world
(generated worlds will come from Create New World).

Run after compiling the editor target (AVoxelWorld, AFoundationGameMode):

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' "C:\\dev\\projects\\Unnamed\\Unnamed.uproject" -run=pythonscript -script="C:\\dev\\projects\\Unnamed\\Scripts\\Editor\\CreateGeneratedMap.py" -unattended -nop4 -nosplash

An existing map is never rewritten: it is only verified, and the script raises if it is not set up as expected.
To rebuild it from scratch, close any editor that has it open, delete Content/Maps/L_Generated.umap and rerun.
"""
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # Unreal does not add the script folder.
import ProjectPaths  # noqa: E402

MAP = ProjectPaths.generated_map()

WORLD_CLASS = "/Script/Unnamed.VoxelWorld"
GAME_MODE_CLASS = "/Script/Unnamed.FoundationGameMode"
# Below the world's floor (Z -64 blocks) and its fall-out height (-128 blocks): the voxel world handles falling out.
KILL_Z = -20000.0

# The Playground's daylight rig (Scripts/Editor/CreatePlaygroundMap.py).
SUN_INTENSITY = 75000.0
EXPOSURE_EV100 = 14.5
FOG_DENSITY = 0.002
FOG_START_DISTANCE = 3000.0

ORIGIN_TOLERANCE = 0.1


def native_class(path):
    loaded = unreal.load_class(None, path)
    if not loaded:
        raise RuntimeError("Could not load " + path + " - compile the Unnamed editor target first")
    return loaded


def create_map():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not levels.new_level(MAP):
        raise RuntimeError("Could not create the generated world's level")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    settings = world.get_world_settings()
    settings.set_editor_property("default_game_mode", native_class(GAME_MODE_CLASS))
    settings.set_editor_property("kill_z", KILL_Z)

    def spawn(cls, label, location, rotation=unreal.Rotator()):
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(*location), rotation)
        if not actor:
            raise RuntimeError("Could not spawn " + label)
        actor.set_actor_label(label)
        return actor

    spawn(native_class(WORLD_CLASS), "Voxel world", (0, 0, 0))
    spawn(unreal.PlayerStart, "Voxel world - spawn", (0, 0, 0))  # The world moves the player to dry ground.
    sun = spawn(unreal.DirectionalLight, "Voxel world - Sun", (0, 0, 1500), unreal.Rotator(roll=0, pitch=-38, yaw=-35))
    sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_editor_property("intensity", SUN_INTENSITY)
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    spawn(unreal.SkyAtmosphere, "Voxel world - Sky", (0, 0, 0))
    sky = spawn(unreal.SkyLight, "Voxel world - Ambient", (0, 0, 1000))
    sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    exposure = spawn(unreal.PostProcessVolume, "Voxel world - stable daylight exposure", (0, 0, 0))
    exposure.set_editor_property("unbound", True)
    post = exposure.get_editor_property("settings")
    post.set_editor_property("override_auto_exposure_min_brightness", True)
    post.set_editor_property("override_auto_exposure_max_brightness", True)
    post.set_editor_property("auto_exposure_min_brightness", EXPOSURE_EV100)
    post.set_editor_property("auto_exposure_max_brightness", EXPOSURE_EV100)
    exposure.set_editor_property("settings", post)
    fog = spawn(unreal.ExponentialHeightFog, "Voxel world - haze", (0, 0, 0))
    fog_component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fog_component.set_editor_property("fog_density", FOG_DENSITY)
    fog_component.set_editor_property("start_distance", FOG_START_DISTANCE)
    if not levels.save_current_level():
        raise RuntimeError("Could not save the generated world's level")
    unreal.log("Created the generated world's map: " + MAP)


def verify():
    """Reload the saved map and check what the game relies on. Raises listing every problem found."""
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not levels.load_level(MAP):
        raise RuntimeError("Generated world level did not load: " + MAP)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    settings = world.get_world_settings()
    level_actors = list(actors.get_all_level_actors())
    problems = []

    def of_type(cls):
        return [actor for actor in level_actors if isinstance(actor, cls)]

    def exactly_one(found, what):
        if len(found) != 1:
            problems.append("expected one %s, found %d" % (what, len(found)))
            return None
        return found[0]

    mode = settings.get_editor_property("default_game_mode")
    if not mode or mode.get_path_name() != GAME_MODE_CLASS:
        problems.append("game mode is %s, expected %s" % (mode.get_path_name() if mode else None, GAME_MODE_CLASS))
    if abs(settings.get_editor_property("kill_z") - KILL_Z) > ORIGIN_TOLERANCE:
        problems.append("kill_z is %s, expected %s" % (settings.get_editor_property("kill_z"), KILL_Z))
    voxel_world = exactly_one(
        [actor for actor in level_actors if actor.get_class().get_path_name() == WORLD_CLASS], "VoxelWorld actor")
    if voxel_world:
        location = voxel_world.get_actor_location()
        if max(abs(location.x), abs(location.y), abs(location.z)) > ORIGIN_TOLERANCE:
            problems.append("VoxelWorld must sit at the origin, found %s" % location)
    exactly_one(of_type(unreal.PlayerStart), "PlayerStart")
    if of_type(unreal.StaticMeshActor):
        problems.append("the map must not contain static meshes; the voxel world builds the ground")
    sun = exactly_one(of_type(unreal.DirectionalLight), "DirectionalLight")
    if sun and abs(sun.light_component.get_editor_property("intensity") - SUN_INTENSITY) > 0.5:
        problems.append("sun intensity is %s, expected %s" % (sun.light_component.get_editor_property("intensity"),
                                                              SUN_INTENSITY))
    exactly_one(of_type(unreal.SkyAtmosphere), "SkyAtmosphere")
    exactly_one(of_type(unreal.SkyLight), "SkyLight")
    exactly_one(of_type(unreal.PostProcessVolume), "PostProcessVolume")
    exactly_one(of_type(unreal.ExponentialHeightFog), "ExponentialHeightFog")
    if problems:
        raise RuntimeError("Generated world map %s is not set up: %s (delete Content/Maps/L_Generated.umap and rerun)"
                           % (MAP, "; ".join(problems)))
    unreal.log("GENERATED MAP OK: %s - VoxelWorld + PlayerStart, FoundationGameMode, kill_z %s, daylight rig" % (MAP, KILL_Z))


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        unreal.log("Preserving existing generated world level (verifying only): " + MAP)
    else:
        create_map()
    verify()


main()
