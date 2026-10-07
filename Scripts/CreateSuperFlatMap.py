"""Author Super Flat once. Existing level/material edits are always preserved."""
import unreal

MAP = "/Game/Maps/L_SuperFlat"
MATERIAL = "/Game/Maps/SuperFlat/M_SuperFlatGrid"


def create_floor_material():
    if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL):
        return unreal.load_asset(MATERIAL)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_SuperFlatGrid", "/Game/Maps/SuperFlat", unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError("Could not create Super Flat floor material")
    edit = unreal.MaterialEditingLibrary
    position = edit.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -600, 0)
    grid = edit.create_material_expression(material, unreal.MaterialExpressionCustom, -350, 0)
    grid.set_editor_property("description", "Anti-aliased 1 m / 10 m grid, world centimetres")
    grid.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    position_input = unreal.CustomInput()
    position_input.set_editor_property("input_name", "WorldPosition")
    grid.set_editor_property("inputs", [position_input])
    grid.set_editor_property("code", """
float2 p = WorldPosition.xy / 100.0;
float2 aa = max(fwidth(p), 0.0001);
float2 d = abs(frac(p - 0.5) - 0.5) / aa;
float minor = (1 - saturate(min(d.x, d.y) - 0.3)) * (1 - saturate(max(aa.x, aa.y)));
float2 big = p / 10.0;
float2 ba = max(fwidth(big), 0.0001);
float2 bd = abs(frac(big - 0.5) - 0.5) / ba;
float major = (1 - saturate(min(bd.x, bd.y) - 0.7)) * (1 - saturate(max(ba.x, ba.y)));
float3 color = lerp(float3(0.19, 0.25, 0.28), float3(0.11, 0.16, 0.19), minor * 0.75);
color = lerp(color, float3(0.055, 0.09, 0.12), major * 0.85);
float2 axes = 1 - smoothstep(3.0, 6.0 + fwidth(WorldPosition.xy), abs(WorldPosition.xy));
color = lerp(color, float3(0.5, 0.14, 0.09), axes.y * 0.8);
color = lerp(color, float3(0.10, 0.39, 0.19), axes.x * 0.8);
return color;
""")
    edit.connect_material_expressions(position, "", grid, "WorldPosition")
    edit.connect_material_property(grid, "", unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = edit.create_material_expression(material, unreal.MaterialExpressionConstant, -350, 200)
    roughness.set_editor_property("r", 0.9)
    edit.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material):
        raise RuntimeError("Could not save Super Flat floor material")
    return material


def create_map():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        unreal.log("Preserving existing Super Flat level: " + MAP)
        return
    material = create_floor_material()
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not levels.new_level(MAP):
        raise RuntimeError("Could not create Super Flat level")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    settings = world.get_world_settings()
    settings.set_editor_property("default_game_mode", unreal.SuperFlatGameMode)
    settings.set_editor_property("kill_z", -10000.0)

    def spawn(cls, label, location, rotation=unreal.Rotator()):
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(*location), rotation)
        if not actor:
            raise RuntimeError("Could not spawn " + label)
        actor.set_actor_label(label)
        return actor

    floor = spawn(unreal.StaticMeshActor, "Super Flat - 2000m collision floor", (0, 0, -50))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"))
    floor.static_mesh_component.set_material(0, material)
    floor.static_mesh_component.set_collision_profile_name("BlockAll")
    floor.set_actor_scale3d(unreal.Vector(2000, 2000, 1))
    floor.set_editor_property("tags", ["SuperFlatFloor"])
    spawn(unreal.PlayerStart, "Super Flat - central FPS spawn", (0, 0, 110))
    sun = spawn(unreal.DirectionalLight, "Super Flat - Sun", (0, 0, 1500), unreal.Rotator(-38, -35, 0))
    sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_editor_property("intensity", 75000.0)
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    spawn(unreal.SkyAtmosphere, "Super Flat - Sky", (0, 0, 0))
    sky = spawn(unreal.SkyLight, "Super Flat - Ambient", (0, 0, 1000))
    sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    exposure = spawn(unreal.PostProcessVolume, "Super Flat - stable daylight exposure", (0, 0, 0))
    exposure.set_editor_property("unbound", True)
    post = exposure.get_editor_property("settings")
    post.set_editor_property("override_auto_exposure_min_brightness", True)
    post.set_editor_property("override_auto_exposure_max_brightness", True)
    post.set_editor_property("auto_exposure_min_brightness", 12.0)
    post.set_editor_property("auto_exposure_max_brightness", 12.0)
    exposure.set_editor_property("settings", post)
    if not levels.save_current_level():
        raise RuntimeError("Could not save Super Flat level")
    unreal.log("Created Super Flat: 2000m x 2000m, floor Z=0cm, PlayerStart Z=110cm")


create_map()
