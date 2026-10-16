"""The furnace, smoker and blast furnace particles (T1 step 8b2b): T_FurnaceParticles and M_FurnaceParticle.

- T_FurnaceParticles: a texture array of the 26.3 jar's particle textures (Art/StandIns/particle, Docs/StandIns.md),
  layers 0-7 the smoke's frames in particles/smoke.json's order (generic_7 to generic_0), layer 8 flame. 8 x 8, one mip,
  nearest. Additive: an existing array is verified against the PNGs' pixel hashes, never rebuilt.
- M_FurnaceParticle: a masked, two-sided, instanced, lit quad from per-instance custom data (FFoundationBreakEffects
  writes CD0 the layer, CD2 1 for a full-bright flame): the smoke in SmokeGrey (provisional), the flame emissive. The
  graph is code, so rerunning rebuilds it on the same asset.

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\ImportFurnaceParticles.py" -unattended -nop4 -nosplash
Add -AllowCommandletRendering to prove the shader compiles.
"""
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402

ROOT = Path(unreal.Paths.project_dir()).resolve()
PNG_DIR = ROOT / "Art" / "StandIns" / "particle"
# (layer name, the PNG's RGBA SHA-256 from an independent decoder, Pillow): smoke.json's order, then the flame.
LAYERS = [
    ("generic_7", "1dff037116b027013b9748aec714642f2aa07338550d42f9e48b670727f29454"),
    ("generic_6", "541e4defbb0df1adfda27ae568401a8bc20c2c2b12a93648c126cbaaf769e62d"),
    ("generic_5", "563703a1ee895c623c7b48a34e2366653d3d7b63caa47a1ced7955049636fc37"),
    ("generic_4", "9aebaae821ee65ade9a0dba4ea164fa7760b3fd00e7a2db569e67940c405cc8b"),
    ("generic_3", "45d036293e5d6f0c4e15f129c36fa6ecd1130cee45cb29f9f386fe2568e85fa4"),
    ("generic_2", "7bf312d0e500c1d88349b86e1250c0f520cfc0017649adce8e5e3bd928296077"),
    ("generic_1", "03ab165f32be8ab16c33e686a76b93813cc56d2d7cb7f11c8a663560f6d91f5e"),
    ("generic_0", "ce5b4726611a8feb79345812798180c029082eec9235eada38c43743bf8977c2"),
    ("flame", "461c6c8382a4a2e8ca720309713e6f19bd6ad59b34dd8ff1c1c64f2f79b2c4a8"),
]
FRAME_SIZE, MIPS = 8, 1
# Provisional (provisional-values.md, T1 8b2b): the smoke's grey and the flame's brightness.
SMOKE_GREY, FLAME_EMISSIVE = 0.3, 1.0
Import = unreal.FoundationBlockImport
lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary


def fail(message):
    raise RuntimeError("FURNACE_PARTICLES FAIL: " + message)


def build_array():
    path = ProjectPaths.break_asset("T_FurnaceParticles")
    if assets.does_asset_exist(path):
        array = assets.load_asset(path)
        built = False
    else:
        array = Import.create_texture_array(path)
        error = Import.build_texture_array(array, [str(PNG_DIR / (name + ".png")) for name, _ in LAYERS],
                                           [0] * len(LAYERS), ["mean"] * len(LAYERS), [0.0] * len(LAYERS),
                                           FRAME_SIZE, MIPS)
        if error:
            fail("T_FurnaceParticles: " + error)
        if not assets.save_loaded_asset(array):
            fail("could not save T_FurnaceParticles")
        built = True
    for layer, (name, expected) in enumerate(LAYERS):
        if Import.hash_layer(array, layer) != expected:
            fail("T_FurnaceParticles layer %d (%s) differs from the PNG" % (layer, name))
    return array, built


def get_or_create_material(path):
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
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


def build_material(array):
    material = get_or_create_material(ProjectPaths.break_asset("M_FurnaceParticle"))
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("opacity_mask_clip_value", 0.5)

    def node(cls, x, y):
        return lib.create_material_expression(material, cls, x, y)

    def link(source, target, input_name):
        if not lib.connect_material_expressions(source, "", target, input_name):
            fail("M_FurnaceParticle: could not connect to " + input_name)

    def prop(source, material_property):
        if not lib.connect_material_property(source, "", material_property):
            fail("M_FurnaceParticle: could not connect %s" % material_property)

    def custom(description, code, output_type, inputs, y):
        expr = node(unreal.MaterialExpressionCustom, -400, y)
        expr.set_editor_property("description", description)
        expr.set_editor_property("output_type", output_type)
        expr.set_editor_property("code", code)
        items = []
        for name in inputs:
            item = unreal.CustomInput()
            item.set_editor_property("input_name", name)
            items.append(item)
        expr.set_editor_property("inputs", items)
        return expr

    def scalar(name, value, y):
        expr = node(unreal.MaterialExpressionScalarParameter, -900, y)
        expr.set_editor_property("parameter_name", name)
        expr.set_editor_property("default_value", value)
        return expr

    texture = node(unreal.MaterialExpressionTextureObjectParameter, -900, 200)
    texture.set_editor_property("parameter_name", "Particles")
    texture.set_editor_property("texture", array)
    uv = node(unreal.MaterialExpressionTextureCoordinate, -900, 0)
    layer = node(unreal.MaterialExpressionPerInstanceCustomData, -900, -300)
    layer.set_editor_property("data_index", 0)
    bright = node(unreal.MaterialExpressionPerInstanceCustomData, -900, -200)
    bright.set_editor_property("data_index", 2)
    grey = scalar("SmokeGrey", SMOKE_GREY, 300)
    emissive = scalar("FlameEmissive", FLAME_EMISSIVE, 380)
    sample = "float4 c = Texture2DArraySample(Particles, ParticlesSampler, float3(UV, round(Layer)));\n"
    outputs = [
        ("Furnace particle: the smoke's grey (none for the flame)", sample + "return c.rgb * Grey * (1 - Bright);",
         unreal.CustomMaterialOutputType.CMOT_FLOAT3, unreal.MaterialProperty.MP_BASE_COLOR, -100),
        ("Furnace particle: the flame full bright", sample + "return c.rgb * Emissive * Bright;",
         unreal.CustomMaterialOutputType.CMOT_FLOAT3, unreal.MaterialProperty.MP_EMISSIVE_COLOR, 100),
        ("Furnace particle: the texture's alpha", sample + "return c.a;",
         unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.MaterialProperty.MP_OPACITY_MASK, 300),
    ]
    for description, code, output_type, material_property, y in outputs:
        expr = custom(description, code, output_type, ["Layer", "Bright", "UV", "Particles", "Grey", "Emissive"], y)
        link(layer, expr, "Layer")
        link(bright, expr, "Bright")
        link(uv, expr, "UV")
        link(texture, expr, "Particles")
        link(grey, expr, "Grey")
        link(emissive, expr, "Emissive")
        prop(expr, material_property)
    lib.recompile_material(material)
    if not assets.save_loaded_asset(material):
        fail("could not save M_FurnaceParticle")
    return material


def main():
    array, built = build_array()
    material = build_material(array)
    cmdline = unreal.SystemLibrary.get_command_line()
    compiled = "AllowCommandletRendering" in cmdline and "NullRHI" not in cmdline
    stats = lib.get_statistics(material)
    if compiled and stats.num_pixel_shader_instructions == 0:
        fail("M_FurnaceParticle: the shader did not compile (see ShaderCompilers errors above)")
    unreal.log("FURNACE_PARTICLES PASS: T_FurnaceParticles %s, %d layers match the PNGs; M_FurnaceParticle PS instr %d, "
               "samplers %d%s" % ("built" if built else "verified", len(LAYERS), stats.num_pixel_shader_instructions,
                                  stats.num_samplers, "" if compiled else " (graph only; add -AllowCommandletRendering)"))


main()
