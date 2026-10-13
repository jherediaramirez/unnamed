"""Build only M_AtmosphereTwilight: an exposure-compensated terrain minimum before DOF and water/glass blending.

The existing cave material is untouched. Run with UnrealEditor-Cmd.exe Unnamed.uproject -run=pythonscript
-script=Scripts/Editor/CreateTwilightMaterial.py -unattended -nop4 -nosplash -AllowCommandletRendering.
Only this generator's tagged material can be rebuilt. Existing translucent materials are checked read-only.
"""
import sys
from pathlib import Path
import unreal

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ProjectPaths  # noqa: E402

LIB = unreal.MaterialEditingLibrary
ASSETS = unreal.EditorAssetLibrary
PACKAGE = ProjectPaths.block_asset("M_AtmosphereTwilight")
OWNER = "CreateTwilightMaterial.py"
TAG = "UnnamedGenerator"
FLOOR_SCALE = 0.08
FLOOR_TOP = 1.0
FLOOR_BOTTOM = 0.5
FLOOR_NS = 0.7
FLOOR_EW = 0.65

# PostProcessInput0 is exposed to a before-tonemapping material in un-pre-exposed scene-linear HDR. Engine shader
# code applies PreExposure again to its output. EyeAdaptationInverse therefore compensates the floor directly;
# multiplying by PreExposure here as well would apply it twice. The sky's black GBuffer base receives no floor.
TWILIGHT_FLOOR_HLSL = r"""
if (Strength <= 0.0 || max(Base.r, max(Base.g, Base.b)) <= 0.0) return Color;
float fade = 1.0 - saturate((Depth - FadeStart) / max(FadeEnd - FadeStart, 1.0));
if (fade <= 0.0) return Color;
float3 n = N / max(length(N), 0.000001);
float3 a = abs(n);
float face = a.z * (n.z > 0 ? %(top)s : %(bottom)s) + a.y * %(ns)s + a.x * %(ew)s;
float3 floor = %(scale)s * Strength * face * fade * saturate(Base) * float3(0.62, 0.72, 1.0);
return max(Color, floor * max(ExposureInverse, 0.0));
""" % {"scale": FLOOR_SCALE, "top": FLOOR_TOP, "bottom": FLOOR_BOTTOM, "ns": FLOOR_NS, "ew": FLOOR_EW}


def fail(message):
    raise RuntimeError("TWILIGHT_MATERIAL FAIL: " + message)


def verify_translucent_order():
    for name in ("M_BlockWater", "M_BlockTranslucent", "M_ChunkWater", "M_ChunkTranslucent",
                 "M_ChunkDepthWater", "M_ChunkFarDepthWater", "M_ChunkFarWater"):
        material = ASSETS.load_asset(ProjectPaths.block_asset(name))
        if not isinstance(material, unreal.Material):
            fail("Required existing material missing: " + name)
        if material.get_editor_property("translucency_pass") != unreal.MaterialTranslucencyPass.MTP_AFTER_DOF:
            fail(name + " must blend after DOF for the twilight terrain floor to stay behind it")
    unreal.log("TWILIGHT_MATERIAL water/glass pass ordering verified (read-only)")


def create_material():
    if ASSETS.does_asset_exist(PACKAGE):
        material = ASSETS.load_asset(PACKAGE)
        if not isinstance(material, unreal.Material) or ASSETS.get_metadata_tag(material, TAG) != OWNER:
            fail("Refusing an asset not owned by this generator: " + PACKAGE)
        for _ in range(20):
            if LIB.get_num_material_expressions(material) == 0:
                break
            LIB.delete_all_material_expressions(material)
        if LIB.get_num_material_expressions(material) != 0:
            fail("Could not clear the twilight graph")
    else:
        folder, name = ProjectPaths.split(PACKAGE)
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.Material, unreal.MaterialFactoryNew())
        if not material:
            fail("Could not create " + PACKAGE)
        ASSETS.set_metadata_tag(material, TAG, OWNER)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF)
    material.set_editor_property("disable_pre_exposure_scale", False)

    def node(kind, x, y):
        return LIB.create_material_expression(material, kind, x, y)

    def link(source, destination, pin):
        if not LIB.connect_material_expressions(source, "", destination, pin):
            fail("Could not connect " + pin)

    def scene(texture_id, channels, y):
        expression = node(unreal.MaterialExpressionSceneTexture, -900, y)
        expression.set_editor_property("scene_texture_id", texture_id)
        mask = node(unreal.MaterialExpressionComponentMask, -650, y)
        for channel in "rgba":
            mask.set_editor_property(channel, channel in channels)
        link(expression, mask, "")
        return mask

    def scalar(name, default, y):
        expression = node(unreal.MaterialExpressionScalarParameter, -900, y)
        expression.set_editor_property("parameter_name", name)
        expression.set_editor_property("default_value", default)
        return expression

    floor = node(unreal.MaterialExpressionCustom, -400, 0)
    floor.set_editor_property("description", "Twilight terrain minimum before water and glass")
    floor.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    floor.set_editor_property("code", TWILIGHT_FLOOR_HLSL)
    names = ("Color", "Base", "N", "Depth", "Strength", "FadeStart", "FadeEnd", "ExposureInverse")
    inputs = []
    for name in names:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        inputs.append(item)
    floor.set_editor_property("inputs", inputs)
    sources = (scene(unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, "rgb", 0),
               scene(unreal.SceneTextureId.PPI_BASE_COLOR, "rgb", 120),
               scene(unreal.SceneTextureId.PPI_WORLD_NORMAL, "rgb", 240),
               scene(unreal.SceneTextureId.PPI_SCENE_DEPTH, "r", 360),
               scalar("Strength", 0.0, 480), scalar("FadeStart", 1.0e6, 600), scalar("FadeEnd", 1.1e6, 720),
               node(unreal.MaterialExpressionEyeAdaptationInverse, -900, 840))
    for name, source in zip(names, sources):
        link(source, floor, name)
    if not LIB.connect_material_property(floor, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        fail("Could not connect emissive")
    LIB.recompile_material(material)
    if not ASSETS.save_loaded_asset(material, False):
        fail("Could not save " + PACKAGE)
    return material


def verify(material, compiled):
    material = ASSETS.load_asset(PACKAGE) or fail("Material did not save")
    if material.get_editor_property("material_domain") != unreal.MaterialDomain.MD_POST_PROCESS:
        fail("Not a post-process material")
    if material.get_editor_property("blendable_location") != unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF:
        fail("Not before DOF")
    if material.get_editor_property("disable_pre_exposure_scale"):
        fail("Engine pre-exposure conversion must remain enabled")
    if set(str(name) for name in LIB.get_scalar_parameter_names(material)) != {"Strength", "FadeStart", "FadeEnd"}:
        fail("Wrong material parameters")
    if LIB.get_num_material_expressions(material) != 13:
        fail("Expected the thirteen-node twilight graph")
    if ASSETS.get_metadata_tag(material, TAG) != OWNER:
        fail("Ownership tag did not stick")
    stats = LIB.get_statistics(material)
    unreal.log("TWILIGHT_MATERIAL PS instr %d" % stats.num_pixel_shader_instructions)
    if compiled and stats.num_pixel_shader_instructions == 0:
        fail("Shader did not compile; inspect ShaderCompilers errors")


def main():
    verify_translucent_order()
    command = unreal.SystemLibrary.get_command_line()
    compiled = "AllowCommandletRendering" in command and "NullRHI" not in command
    verify(create_material(), compiled)
    unreal.log("TWILIGHT_MATERIAL PASS" + ("" if compiled else " (graph only; shader compilation not requested)"))


main()
