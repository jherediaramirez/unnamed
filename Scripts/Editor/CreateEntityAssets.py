"""Import the entities' stand-in textures and author their materials under /Game/Entities (EntityRoot; Docs/Items.md,
"Experience orbs"; Docs/StandIns.md):

- T_ExperienceOrb: Reference/minecraft-entities-26.3's experience_orb.png (64 x 64, sixteen 16 x 16 icons), sRGB,
  uncompressed, nearest, one mip.
- M_ExperienceOrb: a translucent, unlit, two-sided quad from instanced custom data (value report `xp.value`,
  `xp.value_icons`): CD0 the icon (0-10, row-major in 4 columns), CD1 and CD2 the sRGB red and blue of the colour pulse
  (0-255; green is 255), alpha 128. Full bright: the eye adaptation is undone, as for the break outline.

The texture is imported again on every run (the reference is hash-checked, so the bytes are the same); the graph is code,
so rerunning rebuilds it on the same asset:

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\CreateEntityAssets.py" -unattended -nop4 -nosplash
Add -AllowCommandletRendering to prove the shader compiles.
"""
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402

lib = unreal.MaterialEditingLibrary
REPO = Path(__file__).resolve().parents[2]
ORB_PNG = REPO / "Reference/minecraft-entities-26.3/assets/minecraft/textures/entity/experience/experience_orb.png"
TEXTURE = ProjectPaths.entity_asset("T_ExperienceOrb")
MATERIAL = ProjectPaths.entity_asset("M_ExperienceOrb")

# CD0 the icon, CD1/CD2 the pulse's sRGB red and blue; the sRGB tint is made linear as in CreateBreakMaterials.py.
ORB_HLSL = r"""
uint icon = (uint)clamp(round(CD0), 0.0, 15.0);
float2 uv = (float2(icon % 4, icon / 4) + saturate(UV)) * 16.0 / 64.0;
float4 color = Texture2DSample(Orb, OrbSampler, uv);
float3 tint = float3(CD1, 255.0, CD2) / 255.0;
tint = lerp(tint / 12.92, pow((tint + 0.055) / 1.055, 2.4), step(0.04045, tint));
return float4(color.rgb * tint, color.a * 128.0 / 255.0);
"""


def fail(message):
    raise RuntimeError("ENTITY_ASSETS FAIL: " + message)


def enum_of(type_name, names):
    enum = getattr(unreal, type_name)
    for name in names:
        if hasattr(enum, name):
            return getattr(enum, name)
    fail("%s has none of %s" % (type_name, names))


def import_texture():
    if not ORB_PNG.is_file():
        fail("missing " + str(ORB_PNG))
    folder, name = ProjectPaths.split(TEXTURE)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(ORB_PNG))
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.EditorAssetLibrary.load_asset(TEXTURE)
    if not texture:
        fail("could not import " + TEXTURE)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("compression_settings", enum_of("TextureCompressionSettings",
                                                                ["TC_EDITOR_ICON", "TC_EditorIcon"]))
    texture.set_editor_property("filter", enum_of("TextureFilter", ["TF_NEAREST", "TF_Nearest"]))
    texture.set_editor_property("mip_gen_settings", enum_of("TextureMipGenSettings",
                                                            ["TMGS_NO_MIPMAPS", "TMGS_NoMipmaps"]))
    texture.set_editor_property("lod_group", enum_of("TextureGroup", ["TEXTUREGROUP_PIXELS2D", "TEXTUREGROUP_Pixels2D"]))
    if not unreal.EditorAssetLibrary.save_loaded_asset(texture):
        fail("could not save " + TEXTURE)
    width, height = texture.blueprint_get_size_x(), texture.blueprint_get_size_y()
    if (width, height) != (64, 64):
        fail("T_ExperienceOrb is %d x %d, expected 64 x 64" % (width, height))
    return texture


def get_or_create_material():
    if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL):
        material = unreal.EditorAssetLibrary.load_asset(MATERIAL)
        for _ in range(20):  # delete_all_material_expressions skips entries while it erases
            if lib.get_num_material_expressions(material) == 0:
                break
            lib.delete_all_material_expressions(material)
        return material
    folder, name = ProjectPaths.split(MATERIAL)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material,
                                                                       unreal.MaterialFactoryNew())
    if not material:
        fail("could not create " + MATERIAL)
    return material


def build_material(texture):
    m = get_or_create_material()
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

    def node(cls, x, y):
        return lib.create_material_expression(m, cls, x, y)

    def link(source, target, input_name):
        if not lib.connect_material_expressions(source, "", target, input_name):
            fail("could not connect to " + input_name)

    def prop(source, material_property):
        if not lib.connect_material_property(source, "", material_property):
            fail("could not connect %s" % material_property)

    custom = node(unreal.MaterialExpressionCustom, -400, 0)
    custom.set_editor_property("description", "XP orb: the icon's texels times the colour pulse, alpha 128")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("code", ORB_HLSL)
    inputs = []
    for input_name in ("CD0", "CD1", "CD2", "UV", "Orb"):
        item = unreal.CustomInput()
        item.set_editor_property("input_name", input_name)
        inputs.append(item)
    custom.set_editor_property("inputs", inputs)
    for index in range(3):
        data = node(unreal.MaterialExpressionPerInstanceCustomData, -900, -300 + index * 70)
        data.set_editor_property("data_index", index)
        link(data, custom, "CD%d" % index)
    link(node(unreal.MaterialExpressionTextureCoordinate, -900, 0), custom, "UV")
    orb = node(unreal.MaterialExpressionTextureObjectParameter, -900, 200)
    orb.set_editor_property("parameter_name", "Orb")
    orb.set_editor_property("texture", texture)
    link(orb, custom, "Orb")

    def mask(channels, x, y):
        expr = node(unreal.MaterialExpressionComponentMask, x, y)
        for channel in "rgba":
            expr.set_editor_property(channel, channel in channels)
        link(custom, expr, "")
        return expr

    shown = node(unreal.MaterialExpressionEyeAdaptationInverse, -150, 0)
    link(mask("rgb", -250, 0), shown, "LightValueInput")
    prop(shown, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    prop(mask("a", -250, 120), unreal.MaterialProperty.MP_OPACITY)
    lib.recompile_material(m)
    if not unreal.EditorAssetLibrary.save_loaded_asset(m):
        fail("could not save " + MATERIAL)
    return m


def main():
    texture = import_texture()
    material = build_material(texture)
    cmdline = unreal.SystemLibrary.get_command_line()
    compiled = "AllowCommandletRendering" in cmdline and "NullRHI" not in cmdline
    stats = lib.get_statistics(material)
    unreal.log("ENTITY_ASSETS M_ExperienceOrb: PS instr %d, samplers %d" % (stats.num_pixel_shader_instructions,
                                                                           stats.num_samplers))
    if compiled and stats.num_pixel_shader_instructions == 0:
        fail("M_ExperienceOrb: the shader did not compile (see ShaderCompilers errors above)")
    unreal.log("ENTITY_ASSETS PASS%s" % ("" if compiled else " (graphs only; add -AllowCommandletRendering to compile)"))


main()
