"""Author M_HeldItem, the held items' cutout material (items S4).

It samples an item sprite array through its Sprites parameter (T_ItemTextures, 16 x 16, by default; the held view's
instance for the spears' 32 x 32 in-hand sprites sets T_ItemTextures32) at the layer each held mesh carries in UV 1's X
(FoundationItemMesh::BuildMesh), and discards texels below alpha 0.5 as 26.3's cutout does. Its emissive is the sprite
x Glow (nits) plus the sprite x LightGlow on screen (the eye adaptation undone); the held view sets one of them. The arrays hold mip 0 with
nearest filtering, so the extruded sides, which sample their pixel's centre, take exactly that pixel's colour.

Run after ImportItems.py; the graph is code, so rerunning rebuilds it on the same asset:

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\CreateItemMaterials.py" -unattended -nop4 -nosplash
Add -AllowCommandletRendering to prove the shader compiles.
"""
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402

lib = unreal.MaterialEditingLibrary
NAME = "M_HeldItem"
HLSL = "return Texture2DArraySample(Sprites, SpritesSampler, float3(UV, Layer.x));"


def fail(message):
    raise RuntimeError("ITEM_MATERIALS FAIL: " + message)


def get_or_create(name):
    path = ProjectPaths.item_asset(name)
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


def build(sprites):
    material = get_or_create(NAME)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("two_sided", False)  # the mesh has its own back face
    material.set_editor_property("opacity_mask_clip_value", 0.5)  # 26.3 cutout discards alpha below 0.5

    def node(cls, x, y):
        return lib.create_material_expression(material, cls, x, y)

    def link(source, output, target, input_name):
        if not lib.connect_material_expressions(source, output, target, input_name):
            fail("could not connect to " + input_name)

    custom = node(unreal.MaterialExpressionCustom, -400, 0)
    custom.set_editor_property("description", "Held item: the sprite's texel at its array layer")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("code", HLSL)
    inputs = []
    for input_name in ("UV", "Layer", "Sprites"):
        item = unreal.CustomInput()
        item.set_editor_property("input_name", input_name)
        inputs.append(item)
    custom.set_editor_property("inputs", inputs)
    link(node(unreal.MaterialExpressionTextureCoordinate, -900, 0), "", custom, "UV")
    layer = node(unreal.MaterialExpressionTextureCoordinate, -900, 80)
    layer.set_editor_property("coordinate_index", 1)
    link(layer, "", custom, "Layer")
    parameter = node(unreal.MaterialExpressionTextureObjectParameter, -900, 200)
    parameter.set_editor_property("parameter_name", "Sprites")
    parameter.set_editor_property("texture", sprites)
    link(parameter, "", custom, "Sprites")

    def mask(channels, y):
        expr = node(unreal.MaterialExpressionComponentMask, -150, y)
        for channel in "rgba":
            expr.set_editor_property(channel, channel in channels)
        link(custom, "", expr, "")
        return expr

    rgb = mask("rgb", 0)
    lib.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(mask("a", 120), "", unreal.MaterialProperty.MP_OPACITY_MASK)
    # Matte, as the block materials.
    for value, prop, y in ((1.0, unreal.MaterialProperty.MP_ROUGHNESS, 240),
                           (0.1, unreal.MaterialProperty.MP_SPECULAR, 320)):
        constant = node(unreal.MaterialExpressionConstant, -150, y)
        constant.set_editor_property("r", value)
        lib.connect_material_property(constant, "", prop)
    # Glow (foundation.Held.Glow, set by the held view): emissive = sprite colour x Glow, so a sprite turned from the
    # sun still reads near its own colours, as vanilla lights held items in view space. 0 here: lit by the scene alone.
    glow = node(unreal.MaterialExpressionScalarParameter, -150, 400)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 0.0)
    emissive = node(unreal.MaterialExpressionMultiply, 50, 400)
    link(rgb, "", emissive, "A")
    link(glow, "", emissive, "B")
    # Plus the light at the player (foundation.Held.Light 1, set by the held view): sprite colour x LightGlow on
    # screen, the eye adaptation undone, so a dark cave leaves the item dark under any exposure. 0 here: none.
    light_glow = node(unreal.MaterialExpressionScalarParameter, -150, 520)
    light_glow.set_editor_property("parameter_name", "LightGlow")
    light_glow.set_editor_property("default_value", 0.0)
    lit = node(unreal.MaterialExpressionMultiply, 50, 520)
    link(rgb, "", lit, "A")
    link(light_glow, "", lit, "B")
    shown = node(unreal.MaterialExpressionEyeAdaptationInverse, 200, 520)
    link(lit, "", shown, "LightValueInput")
    total = node(unreal.MaterialExpressionAdd, 350, 400)
    link(emissive, "", total, "A")
    link(shown, "", total, "B")
    lib.connect_material_property(total, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material):
        fail("could not save " + NAME)
    return material


def main():
    sprites = unreal.EditorAssetLibrary.load_asset(ProjectPaths.item_asset("T_ItemTextures"))
    if not sprites:
        fail("T_ItemTextures is missing: run ImportItems.py first")
    cmdline = unreal.SystemLibrary.get_command_line()
    compiled = "AllowCommandletRendering" in cmdline and "NullRHI" not in cmdline
    material = build(sprites)
    stats = lib.get_statistics(material)
    unreal.log("ITEM_MATERIALS %s: PS instr %d, samplers %d" % (NAME, stats.num_pixel_shader_instructions,
                                                                 stats.num_samplers))
    if compiled and stats.num_pixel_shader_instructions == 0:
        fail(NAME + ": the shader did not compile (see ShaderCompilers errors above)")
    unreal.log("ITEM_MATERIALS PASS%s" % ("" if compiled else " (graph only; add -AllowCommandletRendering to compile)"))


main()
