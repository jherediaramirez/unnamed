"""Build only M_AtmosphereStars, the optional stars and twilight backdrop's depth-tested additive material.

Run after the editor target builds:
    UnrealEditor-Cmd.exe Unnamed.uproject -run=pythonscript -script=Scripts/Editor/CreateStarMaterial.py
        -unattended -nop4 -nosplash -AllowCommandletRendering

The generated material lives under the existing cooked BlockRoot. This script refuses an existing asset without
its ownership tag. Re-running rebuilds only this graph, never block materials, textures, maps or references.
"""
import sys
from pathlib import Path
import unreal

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ProjectPaths  # noqa: E402

LIB = unreal.MaterialEditingLibrary
ASSETS = unreal.EditorAssetLibrary
PACKAGE = ProjectPaths.block_asset("M_AtmosphereStars")
OWNER = "CreateStarMaterial.py"
TAG = "UnnamedGenerator"


def fail(message):
    raise RuntimeError("STAR_MATERIAL FAIL: " + message)


def main():
    if ASSETS.does_asset_exist(PACKAGE):
        material = ASSETS.load_asset(PACKAGE)
        if not isinstance(material, unreal.Material) or ASSETS.get_metadata_tag(material, TAG) != OWNER:
            fail("Refusing an asset not owned by this generator: " + PACKAGE)
        for _ in range(20):
            if LIB.get_num_material_expressions(material) == 0:
                break
            LIB.delete_all_material_expressions(material)
        if LIB.get_num_material_expressions(material) != 0:
            fail("Could not clear the star graph")
    else:
        folder, name = ProjectPaths.split(PACKAGE)
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.Material, unreal.MaterialFactoryNew())
        if not material:
            fail("Could not create " + PACKAGE)
        ASSETS.set_metadata_tag(material, TAG, OWNER)

    properties = {
        "blend_mode": unreal.BlendMode.BLEND_ADDITIVE,
        "shading_model": unreal.MaterialShadingModel.MSM_UNLIT,
        "two_sided": True,
        "disable_depth_test": False,
        "use_translucency_vertex_fog": False,
        "apply_cloud_fogging": False,
        "use_emissive_for_dynamic_area_lighting": False,
        # After motion blur: the sky's stars stay sharp squares in fast turns (T7a); still depth-tested.
        "translucency_pass": unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR,
    }
    for name, value in properties.items():
        material.set_editor_property(name, value)

    def node(kind, x, y):
        return LIB.create_material_expression(material, kind, x, y)

    def link(source, output, destination, pin):
        if not LIB.connect_material_expressions(source, output, destination, pin):
            fail("Could not connect " + pin)

    shade = node(unreal.MaterialExpressionVertexColor, -650, 0)
    intensity = node(unreal.MaterialExpressionScalarParameter, -650, 150)
    intensity.set_editor_property("parameter_name", "Intensity")
    intensity.set_editor_property("default_value", 0.0)
    tint = node(unreal.MaterialExpressionVectorParameter, -850, -180)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    tinted = node(unreal.MaterialExpressionMultiply, -650, -100)
    link(shade, "", tinted, "A")
    link(tint, "", tinted, "B")
    scaled = node(unreal.MaterialExpressionMultiply, -400, 0)
    link(tinted, "", scaled, "A")
    link(intensity, "", scaled, "B")
    # Unconnected inverse inputs default to white and full compensation in UE 5.8. This cancels eye adaptation,
    # preventing changes in eye adaptation from making stars or the twilight backdrop flare or disappear.
    exposure = node(unreal.MaterialExpressionEyeAdaptationInverse, -400, 200)
    compensated = node(unreal.MaterialExpressionMultiply, -250, 0)
    link(scaled, "", compensated, "A")
    link(exposure, "", compensated, "B")
    # After motion blur the pass's own depth test does not hold terrain back (T7a captures: stars and the backdrop
    # showed over the ground), so the graph compares the scene's depth with its own and draws only where nothing
    # opaque is nearer.
    behind = node(unreal.MaterialExpressionCustom, -400, 350)
    behind.set_editor_property("description", "Nothing opaque nearer than this sky pixel")
    behind.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    behind.set_editor_property("code", "return Scene >= Pixel * 0.999 ? 1.0 : 0.0;")
    behind_inputs = []
    for input_name in ("Scene", "Pixel"):
        item = unreal.CustomInput()
        item.set_editor_property("input_name", input_name)
        behind_inputs.append(item)
    behind.set_editor_property("inputs", behind_inputs)
    link(node(unreal.MaterialExpressionSceneDepth, -650, 330), "", behind, "Scene")
    link(node(unreal.MaterialExpressionPixelDepth, -650, 420), "", behind, "Pixel")
    emissive = node(unreal.MaterialExpressionMultiply, -130, 0)
    link(compensated, "", emissive, "A")
    link(behind, "", emissive, "B")
    if not LIB.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        fail("Could not connect emissive")
    opacity = node(unreal.MaterialExpressionConstant, -130, 220)
    opacity.set_editor_property("r", 1.0)
    if not LIB.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY):
        fail("Could not connect opacity")
    LIB.recompile_material(material)
    if not ASSETS.save_loaded_asset(material, False):
        fail("Could not save " + PACKAGE)

    for name, value in properties.items():
        if material.get_editor_property(name) != value:
            fail("Property did not stick: " + name)
    if LIB.get_num_material_expressions(material) != 12:
        fail("Expected the twelve-node sky graph")
    if ASSETS.get_metadata_tag(material, TAG) != OWNER:
        fail("Ownership tag did not stick")
    unreal.log("STAR_MATERIAL PASS: " + PACKAGE + " (unlit, additive, scene-depth masked, exposure compensated, after motion blur)")


main()
