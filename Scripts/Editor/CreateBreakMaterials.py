"""Author the breaking effects' materials under /Game/Breaking (Docs/Breaking.md):

- M_BreakCracks: a modulate overlay for the mined block's cube. Object-space position and normal pick the face's 16 x 16
  texel (no filtering), and the crack strip layer (Stage, blended to the next with Fade) gives the grey; the factor is
  2 x grey, so 128 is neutral. A small world position offset toward the camera avoids depth fighting.
- M_BreakParticle: a masked, lit, two-sided particle quad from instanced custom data: texture layer, the 4 x 4 texel
  patch, packed tint.
- M_BreakOutline: a translucent unlit colour from instanced custom data (linear R, G, B and opacity).

Run after ImportBreakAssets.py (T_BreakCracks); the graphs are code, so rerunning rebuilds them on the same assets:

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\CreateBreakMaterials.py" -unattended -nop4 -nosplash
Add -AllowCommandletRendering to prove the shaders compile.
"""
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402

lib = unreal.MaterialEditingLibrary
# The break assets' root; the same constant as ImportBreakAssets.py until a BreakRoot project setting exists.
BREAK_ROOT = "/Game/Breaking"
STAGES = 10

# Cracks: the face under the pixel from the object-space normal's dominant axis, u to the viewer's right on side faces
# and v down from the top (the top face is seen from above, +Y toward its bottom edge). Layers: LayerBase + stage.
CRACKS_HLSL = r"""
float3 p = LocalPosition / 100.0;
float3 n = Normal;
float ax = abs(n.x), ay = abs(n.y), az = abs(n.z);
float2 uv;
if (az >= ax && az >= ay)
    uv = n.z >= 0 ? float2(p.x, p.y) : float2(p.x, 1 - p.y);
else if (ax >= ay)
    uv = n.x >= 0 ? float2(1 - p.y, 1 - p.z) : float2(p.y, 1 - p.z);
else
    uv = n.y >= 0 ? float2(p.x, 1 - p.z) : float2(1 - p.x, 1 - p.z);
int2 texel = clamp((int2)floor(uv * 16.0), int2(0, 0), int2(15, 15));
int stage = (int)clamp(round(Stage), 0.0, 9.0);
int first = (int)round(LayerBase) + stage;
int second = (int)round(LayerBase) + min(stage + 1, 9);
float a = Cracks.Load(int4(texel, first, 0)).r;
float b = Cracks.Load(int4(texel, second, 0)).r;
float grey = lerp(a, b, Fade);
return float3(grey, grey, grey) * 2.0;
"""

# Particles: CD0 the block texture layer, CD1 the patch (u | v << 4, texel offsets 0-12), CD2 the packed sRGB tint
# (R << 16 | G << 8 | B; 0xFFFFFF is untinted), converted to linear as UNPACK in CreateBlockMaterials.py does.
PARTICLE_HLSL = r"""
uint patch = (uint)round(CD1);
float2 offset = float2(patch & 15, (patch >> 4) & 15);
float2 uv = (offset + UV * 4.0) / 16.0;
float4 color = Texture2DArraySample(Blocks, BlocksSampler, float3(uv, round(CD0)));
uint packed = (uint)round(CD2);
float3 tint = float3((packed >> 16) & 255, (packed >> 8) & 255, packed & 255) / 255.0;
tint = lerp(tint / 12.92, pow((tint + 0.055) / 1.055, 2.4), step(0.04045, tint));
return float4(color.rgb * tint, color.a);
"""


def fail(message):
    raise RuntimeError("BREAK_MATERIALS FAIL: " + message)


def get_or_create(name):
    path = "%s/%s" % (BREAK_ROOT, name)
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


class Graph:
    """A material being authored: node and link helpers that fail loudly."""

    def __init__(self, name, blend, unlit, two_sided=False, instanced=False):
        self.name = name
        self.material = get_or_create(name)
        m = self.material
        m.set_editor_property("blend_mode", blend)
        m.set_editor_property("two_sided", two_sided)
        m.set_editor_property("used_with_instanced_static_meshes", instanced)
        if blend == unreal.BlendMode.BLEND_MASKED:
            m.set_editor_property("opacity_mask_clip_value", 0.5)
        if unlit:
            try:  # unverified: the legacy single shading model property
                m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
            except Exception as error:  # noqa: BLE001
                fail("%s: could not set the unlit shading model (%s)" % (name, error))

    def node(self, cls, x, y):
        return lib.create_material_expression(self.material, cls, x, y)

    def link(self, source, output, target, input_name):
        if not lib.connect_material_expressions(source, output, target, input_name):
            fail("%s: could not connect to %s" % (self.name, input_name))

    def prop(self, source, output, material_property):
        if not lib.connect_material_property(source, output, material_property):
            fail("%s: could not connect %s" % (self.name, material_property))

    def texture_parameter(self, parameter, texture, y):
        expr = self.node(unreal.MaterialExpressionTextureObjectParameter, -900, y)
        expr.set_editor_property("parameter_name", parameter)
        expr.set_editor_property("texture", texture)
        return expr

    def scalar_parameter(self, parameter, default, y):
        expr = self.node(unreal.MaterialExpressionScalarParameter, -900, y)
        expr.set_editor_property("parameter_name", parameter)
        expr.set_editor_property("default_value", default)
        return expr

    def custom(self, description, code, output_type, input_names, x=-400, y=0):
        expr = self.node(unreal.MaterialExpressionCustom, x, y)
        expr.set_editor_property("description", description)
        expr.set_editor_property("output_type", output_type)
        expr.set_editor_property("code", code)
        items = []
        for input_name in input_names:
            item = unreal.CustomInput()
            item.set_editor_property("input_name", input_name)
            items.append(item)
        expr.set_editor_property("inputs", items)
        return expr

    def custom_data(self, index, y):
        expr = self.node(unreal.MaterialExpressionPerInstanceCustomData, -900, y)
        expr.set_editor_property("data_index", index)
        return expr

    def mask(self, source, channels, x, y):
        expr = self.node(unreal.MaterialExpressionComponentMask, x, y)
        for channel in "rgba":
            expr.set_editor_property(channel, channel in channels)
        self.link(source, "", expr, "")
        return expr

    def constant(self, value, x, y):
        expr = self.node(unreal.MaterialExpressionConstant, x, y)
        expr.set_editor_property("r", value)
        return expr

    def finish(self):
        lib.recompile_material(self.material)
        if not unreal.EditorAssetLibrary.save_loaded_asset(self.material):
            fail("could not save " + self.name)
        return self.material


def local_position(g):
    """The pixel's position in the mesh's own space (cm). Unverified: MaterialExpressionLocalPosition (newer
    engines), else a world to local TransformPosition."""
    cls = getattr(unreal, "MaterialExpressionLocalPosition", None)
    if cls:
        unreal.log("BREAK_MATERIALS %s: using MaterialExpressionLocalPosition" % g.name)
        return g.node(cls, -900, -300)
    transform = g.node(unreal.MaterialExpressionTransformPosition, -700, -300)
    try:  # unverified enum and property names
        transform.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
        transform.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    except Exception as error:  # noqa: BLE001
        fail("%s: could not configure TransformPosition (%s)" % (g.name, error))
    g.link(g.node(unreal.MaterialExpressionWorldPosition, -900, -300), "", transform, "")
    unreal.log("BREAK_MATERIALS %s: using TransformPosition world to local" % g.name)
    return transform


def build_cracks():
    cracks = unreal.EditorAssetLibrary.load_asset(BREAK_ROOT + "/T_BreakCracks")
    if not cracks:
        fail("T_BreakCracks is missing: run ImportBreakAssets.py first")
    g = Graph("M_BreakCracks", unreal.BlendMode.BLEND_MODULATE, unlit=True)
    custom = g.custom("Crack grey for the face under the pixel, doubled for the modulate factor", CRACKS_HLSL,
                      unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                      ["LocalPosition", "Normal", "Cracks", "Stage", "Fade", "LayerBase"])
    g.link(local_position(g), "", custom, "LocalPosition")
    g.link(g.node(unreal.MaterialExpressionVertexNormalWS, -900, -150), "", custom, "Normal")
    g.link(g.texture_parameter("Cracks", cracks, 0), "", custom, "Cracks")
    g.link(g.scalar_parameter("Stage", 0.0, 120), "", custom, "Stage")
    g.link(g.scalar_parameter("Fade", 0.0, 200), "", custom, "Fade")
    g.link(g.scalar_parameter("LayerBase", 0.0, 280), "", custom, "LayerBase")
    g.prop(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)  # the modulate factor
    # A depth nudge toward the camera. The camera vector node is pixel-only, so the vertex direction is
    # normalize(camera position - world position).
    toward = g.node(unreal.MaterialExpressionSubtract, -750, 300)
    g.link(g.node(unreal.MaterialExpressionCameraPositionWS, -950, 260), "", toward, "A")
    g.link(g.node(unreal.MaterialExpressionWorldPosition, -950, 340), "", toward, "B")
    direction = g.node(unreal.MaterialExpressionNormalize, -550, 300)
    g.link(toward, "", direction, "")
    nudge = g.node(unreal.MaterialExpressionMultiply, -300, 300)
    g.link(direction, "", nudge, "A")
    nudge.set_editor_property("const_b", 0.2)
    g.prop(nudge, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    return g.finish()


def build_particle(block_textures):
    g = Graph("M_BreakParticle", unreal.BlendMode.BLEND_MASKED, unlit=False, two_sided=True, instanced=True)
    # Particles light like an up-facing surface: a world-space normal of +Z, so caves stay dark.
    g.material.set_editor_property("tangent_space_normal", False)
    custom = g.custom("Break particle: block texture patch times the tint", PARTICLE_HLSL,
                      unreal.CustomMaterialOutputType.CMOT_FLOAT4, ["CD0", "CD1", "CD2", "UV", "Blocks"])
    for index in range(3):
        g.link(g.custom_data(index, -300 + index * 70), "", custom, "CD%d" % index)
    g.link(g.node(unreal.MaterialExpressionTextureCoordinate, -900, 0), "", custom, "UV")
    g.link(g.texture_parameter("BlockTextures", block_textures, 200), "", custom, "Blocks")
    g.prop(g.mask(custom, "rgb", -150, 0), "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.prop(g.mask(custom, "a", -150, 120), "", unreal.MaterialProperty.MP_OPACITY_MASK)
    normal = g.node(unreal.MaterialExpressionConstant3Vector, -150, 400)
    normal.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
    g.prop(normal, "", unreal.MaterialProperty.MP_NORMAL)
    g.prop(g.constant(1.0, -150, 240), "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.prop(g.constant(0.1, -150, 320), "", unreal.MaterialProperty.MP_SPECULAR)
    return g.finish()


def build_outline():
    g = Graph("M_BreakOutline", unreal.BlendMode.BLEND_TRANSLUCENT, unlit=True, instanced=True)
    data = [g.custom_data(index, -300 + index * 70) for index in range(4)]
    rg = g.node(unreal.MaterialExpressionAppendVector, -500, 0)
    g.link(data[0], "", rg, "A")
    g.link(data[1], "", rg, "B")
    rgb = g.node(unreal.MaterialExpressionAppendVector, -300, 0)
    g.link(rg, "", rgb, "A")
    g.link(data[2], "", rgb, "B")
    # The colour holds under any exposure (the high-contrast cyan read black in daylight): undo the eye adaptation.
    shown = g.node(unreal.MaterialExpressionEyeAdaptationInverse, -150, 0)
    g.link(rgb, "", shown, "LightValueInput")
    g.prop(shown, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.prop(data[3], "", unreal.MaterialProperty.MP_OPACITY)
    return g.finish()


def main():
    registry = unreal.EditorAssetLibrary.load_asset(ProjectPaths.block_asset("DA_Blocks"))
    if not registry:
        fail("DA_Blocks is missing: run ImportBlocks.py first")
    block_textures = registry.get_editor_property("block_textures")
    cmdline = unreal.SystemLibrary.get_command_line()
    compiled = "AllowCommandletRendering" in cmdline and "NullRHI" not in cmdline
    materials = [build_cracks(), build_particle(block_textures), build_outline()]
    for material in materials:
        stats = lib.get_statistics(material)
        unreal.log("BREAK_MATERIALS %s: PS instr %d, samplers %d" % (material.get_name(),
                                                                      stats.num_pixel_shader_instructions,
                                                                      stats.num_samplers))
        if compiled and stats.num_pixel_shader_instructions == 0:
            fail(material.get_name() + ": the shader did not compile (see ShaderCompilers errors above)")
    unreal.log("BREAK_MATERIALS PASS%s" % ("" if compiled else " (graphs only; add -AllowCommandletRendering to compile)"))


main()
