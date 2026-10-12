"""Author the Playground map once, or verify it when it already exists.

The Playground (catalog world "playground", /Game/Maps/L_Playground) is the one world every feature is
tested in. The map holds only what cannot be built at runtime: the AFoundationPlayground actor (it builds the
2 km dev grid ground, the interaction stations and the movement course at BeginPlay), a PlayerStart, the
daylight rig and a thin height fog. There is deliberately no floor mesh.

Run after compiling the editor target (AFoundationPlayground, AFoundationGameMode) and after
CreateDevGridMaterial.py:

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' "C:\\dev\\projects\\Unnamed\\Unnamed.uproject" -run=pythonscript -script="C:\\dev\\projects\\Unnamed\\Scripts\\Editor\\CreatePlaygroundMap.py" -unattended -nop4 -nosplash

An existing map is never rewritten: it is only verified, and the script raises if it is not set up as expected.
To rebuild the map from scratch, close any editor that has it open, delete Content/Maps/L_Playground.umap and
rerun (deleting and recreating a level inside one editor process is unreliable).
"""
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # Unreal does not add the script folder.
import ProjectPaths  # noqa: E402

MAP = ProjectPaths.world_map("playground")

PLAYGROUND_CLASS = "/Script/Unnamed.FoundationPlayground"
GAME_MODE_CLASS = "/Script/Unnamed.FoundationGameMode"
KILL_Z = -10000.0

# Lighting copied from the retired Super Flat map (stable daylight; exposure pinned at EV100 12).
SUN_INTENSITY = 75000.0
# sunny-16: a 75,000 lux sun wants ~EV 14-15; 12 washes the pale grid out. The Playground follows the generated world's
# picked look (2026-10-12, Source/Unnamed/Voxel/VoxelLook.h): daylight 0.6 EV darker than 14.5, the sky as bright as
# before (its luminance factor 2^0.6 x 1.08).
EXPOSURE_EV100 = 15.1
SKY_LUMINANCE_FACTOR = 1.637
# Thin height fog so the 2 km ground edge fades into the haze: roughly a third of the way to opaque at the
# 1 km edge, nothing within the first 30 m.
FOG_DENSITY = 0.004
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
        raise RuntimeError("Could not create Playground level")
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

    spawn(native_class(PLAYGROUND_CLASS), "Playground", (0, 0, 0))
    spawn(unreal.PlayerStart, "Playground - spawn", (0, 0, 0))  # The pawn resolves its own safe spawn.
    sun = spawn(unreal.DirectionalLight, "Playground - Sun", (0, 0, 1500), unreal.Rotator(roll=0, pitch=-38, yaw=-35))
    sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_editor_property("intensity", SUN_INTENSITY)
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    set_sky_luminance(spawn(unreal.SkyAtmosphere, "Playground - Sky", (0, 0, 0)))
    sky = spawn(unreal.SkyLight, "Playground - Ambient", (0, 0, 1000))
    sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    exposure = spawn(unreal.PostProcessVolume, "Playground - stable daylight exposure", (0, 0, 0))
    exposure.set_editor_property("unbound", True)
    post = exposure.get_editor_property("settings")
    post.set_editor_property("override_auto_exposure_min_brightness", True)
    post.set_editor_property("override_auto_exposure_max_brightness", True)
    post.set_editor_property("auto_exposure_min_brightness", EXPOSURE_EV100)
    post.set_editor_property("auto_exposure_max_brightness", EXPOSURE_EV100)
    exposure.set_editor_property("settings", post)
    fog = spawn(unreal.ExponentialHeightFog, "Playground - edge fog", (0, 0, 0))
    fog_component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fog_component.set_editor_property("fog_density", FOG_DENSITY)
    fog_component.set_editor_property("start_distance", FOG_START_DISTANCE)
    if not levels.save_current_level():
        raise RuntimeError("Could not save Playground level")
    unreal.log("Created Playground: " + MAP)


def verify():
    """Reload the saved map and check what the game relies on. Raises listing every problem found."""
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not levels.load_level(MAP):
        raise RuntimeError("Playground level did not load: " + MAP)
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

    def at_origin(actor, what):
        location = actor.get_actor_location()
        if max(abs(location.x), abs(location.y), abs(location.z)) > ORIGIN_TOLERANCE:
            problems.append("%s must sit at the origin, found %s" % (what, location))

    mode = settings.get_editor_property("default_game_mode")
    if not mode or mode.get_path_name() != GAME_MODE_CLASS:
        problems.append("game mode is %s, expected %s" % (mode.get_path_name() if mode else None, GAME_MODE_CLASS))
    if abs(settings.get_editor_property("kill_z") - KILL_Z) > ORIGIN_TOLERANCE:
        problems.append("kill_z is %s, expected %s" % (settings.get_editor_property("kill_z"), KILL_Z))

    playground = exactly_one(
        [actor for actor in level_actors if actor.get_class().get_path_name() == PLAYGROUND_CLASS],
        "FoundationPlayground actor")
    if playground:
        at_origin(playground, "FoundationPlayground")
    start = exactly_one(of_type(unreal.PlayerStart), "PlayerStart")
    if start:
        at_origin(start, "PlayerStart")
    if of_type(unreal.StaticMeshActor):
        problems.append("the map must not contain static meshes; the actor builds its own ground")

    sun = exactly_one(of_type(unreal.DirectionalLight), "DirectionalLight")
    if sun:
        component = sun.light_component
        if component.get_editor_property("mobility") != unreal.ComponentMobility.MOVABLE:
            problems.append("sun must be movable")
        if not component.get_editor_property("atmosphere_sun_light"):
            problems.append("sun must be the atmosphere sun light")
        if abs(component.get_editor_property("intensity") - SUN_INTENSITY) > 0.5:
            problems.append("sun intensity is %s, expected %s" % (component.get_editor_property("intensity"), SUN_INTENSITY))
    atmosphere = exactly_one(of_type(unreal.SkyAtmosphere), "SkyAtmosphere")
    if atmosphere:
        factor = atmosphere.get_component_by_class(unreal.SkyAtmosphereComponent).get_editor_property(
            "sky_and_aerial_perspective_luminance_factor")
        if max(abs(factor.r - SKY_LUMINANCE_FACTOR), abs(factor.g - SKY_LUMINANCE_FACTOR),
               abs(factor.b - SKY_LUMINANCE_FACTOR)) > 0.0001:
            problems.append("sky luminance factor is %s, expected %s" % (factor, SKY_LUMINANCE_FACTOR))
    sky = exactly_one(of_type(unreal.SkyLight), "SkyLight")
    if sky:
        if sky.light_component.get_editor_property("mobility") != unreal.ComponentMobility.MOVABLE:
            problems.append("sky light must be movable")
        if not sky.light_component.get_editor_property("real_time_capture"):
            problems.append("sky light must capture in real time")
    exposure = exactly_one(of_type(unreal.PostProcessVolume), "PostProcessVolume")
    if exposure:
        post = exposure.get_editor_property("settings")
        if not exposure.get_editor_property("unbound"):
            problems.append("post process volume must be unbound")
        for name in ("auto_exposure_min_brightness", "auto_exposure_max_brightness"):
            if abs(post.get_editor_property(name) - EXPOSURE_EV100) > 0.001:
                problems.append("%s is %s, expected %s" % (name, post.get_editor_property(name), EXPOSURE_EV100))
    fog = exactly_one(of_type(unreal.ExponentialHeightFog), "ExponentialHeightFog")
    if fog:
        density = fog.get_component_by_class(unreal.ExponentialHeightFogComponent).get_editor_property("fog_density")
        if abs(density - FOG_DENSITY) > 0.0001:
            problems.append("fog density is %s, expected %s" % (density, FOG_DENSITY))

    if problems:
        raise RuntimeError("Playground map %s is not set up: %s (delete Content/Maps/L_Playground.umap and rerun to rebuild it)"
                           % (MAP, "; ".join(problems)))
    unreal.log("PLAYGROUND MAP OK: %s - FoundationPlayground + PlayerStart at origin, FoundationGameMode, "
               "kill_z %s, daylight rig and edge fog, no floor mesh" % (MAP, KILL_Z))


def set_sky_luminance(sky_actor):
    """The SkyAtmosphere's sky and aerial perspective luminance factor; True when it changed."""
    component = sky_actor.get_component_by_class(unreal.SkyAtmosphereComponent)
    current = component.get_editor_property("sky_and_aerial_perspective_luminance_factor")
    if max(abs(current.r - SKY_LUMINANCE_FACTOR), abs(current.g - SKY_LUMINANCE_FACTOR),
           abs(current.b - SKY_LUMINANCE_FACTOR)) <= 0.0001:
        return False
    component.set_editor_property("sky_and_aerial_perspective_luminance_factor",
                                  unreal.LinearColor(SKY_LUMINANCE_FACTOR, SKY_LUMINANCE_FACTOR, SKY_LUMINANCE_FACTOR, 1))
    return True


def apply_look():
    """The picked look on an existing map: the daylight exposure and the sky's luminance factor; saved on a change."""
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.load_level(MAP):
        raise RuntimeError("Playground level did not load: " + MAP)
    changed = False
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if isinstance(actor, unreal.SkyAtmosphere):
            changed = set_sky_luminance(actor) or changed
        if isinstance(actor, unreal.PostProcessVolume) and actor.get_editor_property("unbound"):
            post = actor.get_editor_property("settings")
            if any(abs(post.get_editor_property(name) - EXPOSURE_EV100) > 0.001
                   for name in ("auto_exposure_min_brightness", "auto_exposure_max_brightness")):
                post.set_editor_property("auto_exposure_min_brightness", EXPOSURE_EV100)
                post.set_editor_property("auto_exposure_max_brightness", EXPOSURE_EV100)
                actor.set_editor_property("settings", post)
                changed = True
    if changed and not levels.save_current_level():
        raise RuntimeError("Could not save the Playground level")
    unreal.log("Playground look: EV100 %s, sky luminance %s (%s)"
               % (EXPOSURE_EV100, SKY_LUMINANCE_FACTOR, "set" if changed else "already"))


def main():
    exists = unreal.EditorAssetLibrary.does_asset_exist(MAP)
    if exists:
        unreal.log("Preserving existing Playground level (verifying, applying the look): " + MAP)
        apply_look()
    else:
        create_map()
    verify()


main()
