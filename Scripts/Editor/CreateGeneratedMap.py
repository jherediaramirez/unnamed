"""Author the generated voxel world's map once, or verify it when it already exists.

/Game/Maps/L_Generated (UUnnamedProjectSettings::GeneratedMap) is the procedural map every generated world opens. It
holds only what cannot be built at runtime: the AVoxelWorld actor (it generates and meshes the terrain), a PlayerStart,
the Playground's daylight rig with a brighter sky light and eye adaptation for caves, and a thin height fog. Phase 2 shows a fixed test area there;
it is not a catalog world (generated worlds will come from Create New World).

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
FOG_DENSITY = 0.002
FOG_START_DISTANCE = 3000.0
# Except the sky light, three times the Playground's. Against a 75,000 lux sun the atmosphere's own sky leaves terrain
# that faces away from the sun near black at this exposure (about 12:1); at 3 those slopes read as clearly as a
# Minecraft side face, and caves stay dark because Lumen still occludes the sky (Docs/VoxelWorld.md, Lighting).
SKY_LIGHT_INTENSITY = 3.0
# Eye adaptation between two EV100 bounds (histogram method), slow, like eyes getting used to the dark
# (Docs/VoxelWorld.md, Lighting). Daylight keeps the Playground's fixed exposure: EV100 14.5 at the engine's default
# exposure compensation, +1. Metering reads sunlit terrain at only EV100 12.8 to 13.5 (the block textures are dark),
# so a 14.5 cap would brighten every outdoor view. Instead the cap is lower and the compensation lower by as much:
# every view metered at the cap or above gets exactly the daylight exposure. Local exposure pivots on middle grey times
# the compensation, so its middle grey is raised by as much to keep its pivot. Caves open up 6.5 stops below the cap.
DAYLIGHT_EV100 = 14.5
DEFAULT_EXPOSURE_BIAS = 1.0  # r.DefaultFeature.AutoExposure.Bias, which verify() checks
EXPOSURE_MAX_EV100 = 12.5
EXPOSURE_BIAS = DEFAULT_EXPOSURE_BIAS + EXPOSURE_MAX_EV100 - DAYLIGHT_EV100
# The old compensation minus the new one, over the engine's default middle-grey bias of 0.
LOCAL_EXPOSURE_MIDDLE_GREY_BIAS = DEFAULT_EXPOSURE_BIAS - EXPOSURE_BIAS
EXPOSURE_MIN_EV100 = EXPOSURE_MAX_EV100 - 6.5
EXPOSURE_SPEED_UP = 2.0  # f-stops per second, towards a brighter scene (leaving a cave)
EXPOSURE_SPEED_DOWN = 1.0  # towards a darker scene (entering a cave)
# The map's unbound PostProcessVolume overrides exactly these (the -VoxelShot camera speeds them up; AVoxelWorld).
EXPOSURE_SETTINGS = {
    "auto_exposure_method": unreal.AutoExposureMethod.AEM_HISTOGRAM,
    "auto_exposure_min_brightness": EXPOSURE_MIN_EV100,
    "auto_exposure_max_brightness": EXPOSURE_MAX_EV100,
    "auto_exposure_bias": EXPOSURE_BIAS,
    "local_exposure_middle_grey_bias": LOCAL_EXPOSURE_MIDDLE_GREY_BIAS,
    "auto_exposure_speed_up": EXPOSURE_SPEED_UP,
    "auto_exposure_speed_down": EXPOSURE_SPEED_DOWN,
}

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
    sky.light_component.set_editor_property("intensity", SKY_LIGHT_INTENSITY)
    exposure = spawn(unreal.PostProcessVolume, "Voxel world - eye adaptation", (0, 0, 0))
    exposure.set_editor_property("unbound", True)
    post = exposure.get_editor_property("settings")
    for name, value in EXPOSURE_SETTINGS.items():
        post.set_editor_property("override_" + name, True)
        post.set_editor_property(name, value)
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
    sky = exactly_one(of_type(unreal.SkyLight), "SkyLight")
    if sky and abs(sky.light_component.get_editor_property("intensity") - SKY_LIGHT_INTENSITY) > 0.001:
        problems.append("sky light intensity is %s, expected %s" % (sky.light_component.get_editor_property("intensity"),
                                                                   SKY_LIGHT_INTENSITY))
    volume = exactly_one(of_type(unreal.PostProcessVolume), "PostProcessVolume")
    if volume:
        post = volume.get_editor_property("settings")
        if not volume.get_editor_property("unbound"):
            problems.append("the PostProcessVolume must be unbound")
        for name, value in EXPOSURE_SETTINGS.items():
            found = post.get_editor_property(name)
            if not post.get_editor_property("override_" + name):
                problems.append("%s is not overridden (expected %s)" % (name, value))
            elif found != value if isinstance(value, unreal.AutoExposureMethod) else abs(found - value) > 0.001:
                problems.append("%s is %s, expected %s" % (name, found, value))
    default_bias = unreal.SystemLibrary.get_console_variable_float_value("r.DefaultFeature.AutoExposure.Bias")
    if abs(default_bias - DEFAULT_EXPOSURE_BIAS) > 0.001:
        problems.append("r.DefaultFeature.AutoExposure.Bias is %s, but the daylight exposure assumes %s"
                        % (default_bias, DEFAULT_EXPOSURE_BIAS))
    exactly_one(of_type(unreal.ExponentialHeightFog), "ExponentialHeightFog")
    if problems:
        raise RuntimeError("Generated world map %s is not set up: %s (delete Content/Maps/L_Generated.umap and rerun)"
                           % (MAP, "; ".join(problems)))
    unreal.log("GENERATED MAP OK: %s - VoxelWorld + PlayerStart, FoundationGameMode, kill_z %s, daylight rig, "
               "sky light %s, exposure EV100 %s to %s"
               % (MAP, KILL_Z, SKY_LIGHT_INTENSITY, EXPOSURE_MIN_EV100, EXPOSURE_MAX_EV100))


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        unreal.log("Preserving existing generated world level (verifying only): " + MAP)
    else:
        create_map()
    verify()


main()
