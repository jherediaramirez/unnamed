"""Author the look dials' own materials under /Game/Blocks (Docs/VoxelWorld.md "Lighting"):

- M_LookCaveFloor: voxel.Look.CaveFloor's ambient floor after tone mapping. Vanilla never lights a block below its
  dimension's ambient light (26.3's overworld `visual/ambient_light_color` #0a0a0a), so a cave wall no light reaches
  still shows as a dim copy of its texture. After tone mapping, in display sRGB, the final colour becomes
  max(scene, Strength x face(normal) x fade x sRGB(BaseColor)), with the per-face factors measured on vanilla at
  Brightness 50 (Docs/Records/2026-10-13-Cave-Floor.md). Being after tone mapping it ignores the exposure and the
  filmic toe; the max lifts only pixels darker than the floor, so lit walls, torches, daylight and the sky (BaseColor
  0) stay as they are, and the metering (read before it) is untouched.

The C++ mirror is VoxelLook::CaveFloorDisplay; Unnamed.Voxel.Look checks this file's constants and blend location
against it. AVoxelWorld::ApplyLook draws it through an unbound post-process component only while the dial is above 0.
Rerunning rebuilds the graph on the same asset:

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\CreateLookMaterials.py" -unattended -nop4 -nosplash
Add -AllowCommandletRendering to prove the shader compiles.
"""
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402

lib = unreal.MaterialEditingLibrary
CAVE_FLOOR = ProjectPaths.block_asset("M_LookCaveFloor")

# Display sRGB / texture sRGB at light 0, vanilla 26.3 Brightness 50 (scout AB, black-box frames): the top 0.105, the
# north and south walls (our +-Y) 0.071, east and west (+-X) 0.065; the bottom is unmeasured (0.5 / 0.8 x 0.071).
FLOOR_TOP = 0.105
FLOOR_NS = 0.071
FLOOR_EW = 0.065
FLOOR_BOTTOM = 0.05

# Inputs: Color (display sRGB after tone mapping), Base (linear BaseColor), N (world normal), Depth (cm), Strength,
# FadeStart and FadeEnd (cm; the floor fades out with the fog that hides the far scene). The sky has no BaseColor, so
# it gets no floor.
CAVE_FLOOR_HLSL = r"""
float3 a = abs(normalize(N));
float face = a.z * (N.z > 0 ? %(top)s : %(bottom)s) + a.y * %(ns)s + a.x * %(ew)s;
float fade = 1.0 - saturate((Depth - FadeStart) / max(FadeEnd - FadeStart, 1.0));
float3 b = saturate(Base);
float3 tex = lerp(b * 12.92, 1.055 * pow(b, 1.0 / 2.4) - 0.055, step(0.0031308, b));
return max(Color, Strength * face * fade * tex);
""" % {"top": FLOOR_TOP, "bottom": FLOOR_BOTTOM, "ns": FLOOR_NS, "ew": FLOOR_EW}


def fail(message):
    raise RuntimeError("LOOK_MATERIALS FAIL: " + message)


def get_or_create(path):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.EditorAssetLibrary.load_asset(path)
        for _ in range(20):  # delete_all_material_expressions skips entries while it erases
            if lib.get_num_material_expressions(material) == 0:
                break
            lib.delete_all_material_expressions(material)
        return material
    folder, short = ProjectPaths.split(path)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(short, folder, unreal.Material,
                                                                       unreal.MaterialFactoryNew())
    if not material:
        fail("could not create " + path)
    return material


def link(material, source, output, target, input_name):
    if not lib.connect_material_expressions(source, output, target, input_name):
        fail("%s: could not connect to %s" % (material.get_name(), input_name))


def build_cave_floor():
    m = get_or_create(CAVE_FLOOR)
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    m.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)

    def node(cls, x, y):
        return lib.create_material_expression(m, cls, x, y)

    def scene_texture(texture_id, channels, y):
        expr = node(unreal.MaterialExpressionSceneTexture, -900, y)
        expr.set_editor_property("scene_texture_id", texture_id)
        mask = node(unreal.MaterialExpressionComponentMask, -650, y)
        for channel in "rgba":
            mask.set_editor_property(channel, channel in channels)
        link(m, expr, "", mask, "")
        return mask

    def scalar(name, default, y):
        expr = node(unreal.MaterialExpressionScalarParameter, -900, y)
        expr.set_editor_property("parameter_name", name)
        expr.set_editor_property("default_value", default)
        return expr

    floor = node(unreal.MaterialExpressionCustom, -400, 0)
    floor.set_editor_property("description", "Cave floor after tone mapping")
    floor.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    floor.set_editor_property("code", CAVE_FLOOR_HLSL)
    names = ("Color", "Base", "N", "Depth", "Strength", "FadeStart", "FadeEnd")
    items = []
    for name in names:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        items.append(item)
    floor.set_editor_property("inputs", items)
    sources = (scene_texture(unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, "rgb", 0),
               scene_texture(unreal.SceneTextureId.PPI_BASE_COLOR, "rgb", 120),
               scene_texture(unreal.SceneTextureId.PPI_WORLD_NORMAL, "rgb", 240),
               scene_texture(unreal.SceneTextureId.PPI_SCENE_DEPTH, "r", 360),
               scalar("Strength", 0.0, 480), scalar("FadeStart", 1.0e6, 600), scalar("FadeEnd", 1.1e6, 720))
    for name, source in zip(names, sources):
        link(m, source, "", floor, name)
    if not lib.connect_material_property(floor, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        fail("M_LookCaveFloor: could not connect the emissive colour")
    lib.recompile_material(m)
    if not unreal.EditorAssetLibrary.save_loaded_asset(m):
        fail("could not save M_LookCaveFloor")
    return m


def verify(material, compiled):
    """The saved asset is what the C++ expects: a post-process material after tone mapping with its three scalars."""
    material = unreal.EditorAssetLibrary.load_asset(CAVE_FLOOR) or fail("M_LookCaveFloor did not save")
    if material.get_editor_property("material_domain") != unreal.MaterialDomain.MD_POST_PROCESS:
        fail("M_LookCaveFloor is not a post-process material")
    if material.get_editor_property("blendable_location") != \
            unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING:
        fail("M_LookCaveFloor is not after tone mapping")
    scalars = set(str(name) for name in lib.get_scalar_parameter_names(material))
    if not {"Strength", "FadeStart", "FadeEnd"} <= scalars:
        fail("M_LookCaveFloor's scalars are %s" % sorted(scalars))
    stats = lib.get_statistics(material)
    unreal.log("LOOK_MATERIALS M_LookCaveFloor: PS instr %d" % stats.num_pixel_shader_instructions)
    if compiled and stats.num_pixel_shader_instructions == 0:
        fail("M_LookCaveFloor: the shader did not compile (see ShaderCompilers errors above)")


def main():
    cmdline = unreal.SystemLibrary.get_command_line()
    compiled = "AllowCommandletRendering" in cmdline and "NullRHI" not in cmdline
    verify(build_cave_floor(), compiled)
    unreal.log("LOOK_MATERIALS PASS%s" % ("" if compiled else " (graphs only; add -AllowCommandletRendering to compile)"))


main()
