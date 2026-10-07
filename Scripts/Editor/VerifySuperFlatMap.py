"""Read-only verification of the authored map; run in an isolated editor commandlet."""
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not levels.load_level("/Game/Maps/L_SuperFlat"):
    raise RuntimeError("Super Flat level did not load")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_world_settings().get_editor_property("default_game_mode") == unreal.SuperFlatGameMode.static_class()
floors = [a for a in actors.get_all_level_actors() if a.actor_has_tag("SuperFlatFloor")]
starts = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
assert len(floors) == 1 and len(starts) == 1, "Expected one floor and one spawn"
floor = floors[0]
origin, extent = floor.get_actor_bounds(False)
assert abs(extent.x - 100000) < 0.1 and abs(extent.y - 100000) < 0.1, "Floor must measure 2 km square"
assert abs(origin.z + extent.z) < 0.1, "Ground surface must be Z = 0"
assert str(floor.static_mesh_component.get_collision_profile_name()) == "BlockAll"
assert floor.static_mesh_component.get_collision_enabled() == unreal.CollisionEnabled.QUERY_AND_PHYSICS
assert floor.static_mesh_component.static_mesh.get_path_name() == "/Engine/BasicShapes/Cube.Cube"
assert floor.static_mesh_component.get_material(0).get_path_name() == "/Game/Maps/SuperFlat/M_SuperFlatGrid.M_SuperFlatGrid"
spawn = starts[0].get_actor_location()
assert abs(spawn.x) < 0.1 and abs(spawn.y) < 0.1 and abs(spawn.z - 110) < 0.1
unreal.log("SUPER_FLAT_ASSET PASS: 2000m x 2000m; collision floor Z=0; centered clear spawn; dedicated GameMode/material")
