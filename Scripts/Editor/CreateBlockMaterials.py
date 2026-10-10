"""Author the block materials (M_BlockOpaque, M_BlockCutout, M_BlockTranslucent, M_BlockWater) and the voxel chunk
materials (M_ChunkOpaque, M_ChunkCutout, M_ChunkTranslucent, M_ChunkWater).

Each samples the block texture array by layer and reads the current animation frame from the TextureTable parameter
(UFoundationBlockTextureTable).
- Block materials: per-instance custom data carries the state's six face codes, overlay, tint and light
  (UFoundationInteractionView::MakeCustomData); UV channel 1 holds the face index, so a material picks its face's code.
  Model meshes (FoundationBlockMeshes::BuildModel) carry each vertex's own code in UV 1's second channel instead
  (marked by bit 20; cubes keep 0): texture and tint bit, no UV transform, and the element light in bits 16-19, which
  lights the vertex when brighter than the state's.
- Chunk materials: each vertex carries its own face (UVoxelChunkComponent::BuildStreams). UV 0 is the block UV,
  continuous across a greedy-merged quad; UV 1 the face and overlay codes; UV 2 the packed tint and face index; the
  vertex color's R the ambient occlusion and G the light. The UV is transformed first, then each block's fraction is
  sampled with the continuous UV's gradients, so mip selection never jumps at block edges.

M_ChunkOpaque also draws the render-only ore looks from T_OreLooks (CHUNK_OPAQUE_HLSL; voxel.Look.OreHost and
voxel.Look.OreGlint), so ImportOreLooks.py runs first.

Run after ImportBlocks.py; the graphs are code, so rerunning rebuilds them on the same assets. -Materials=A,B limits
the run to the named materials:

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\CreateBlockMaterials.py" -unattended -nop4 -nosplash
Add -AllowCommandletRendering to prove the shaders compile.
"""
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402

lib = unreal.MaterialEditingLibrary
CUSTOM_DATA = 9
# Emission of a light-level-15 block: color x light / 15 x LightEmissive. The Playground's block materials keep 4.
# The generated world is exposed for a 75,000 lux sun (EV100 14.5), where 4 reads as black: its chunk materials glow
# strongly enough that still lava reads in daylight and lights the caves around it (Docs/VoxelWorld.md, Lighting).
BLOCK_LIGHT_EMISSIVE = 4.0
CHUNK_LIGHT_EMISSIVE = 8000.0

# Shared unpacking: the face's code, its transformed UV and the plains tint (linear). FoundationBlockFaces in
# Source/Unnamed/Foundation/FoundationBlockRegistry.h defines the bits; TransformUV the transform.
UNPACK = r"""
int face = (int)round(Face.x);
float codes[6] = {CD0, CD1, CD2, CD3, CD4, CD5};
uint code = (uint)round(codes[clamp(face, 0, 5)]);
uint own = (uint)round(Face.y);
if (own & (1u << 20)) code = own;
uint packed = (uint)round(CD7);
float3 tint = float3((packed >> 16) & 255, (packed >> 8) & 255, packed & 255) / 255.0;
tint = lerp(tint / 12.92, pow((tint + 0.055) / 1.055, 2.4), step(0.04045, tint));
float2 uv = UV;
uint transform = (code >> 10) & 7;
if (transform & 4) uv.x = 1 - uv.x;
for (uint turn = 0; turn < (transform & 3); turn++) uv = float2(uv.y, 1 - uv.x);
float4 entry = Table.Load(int3(code & 1023, 0, 0));
"""

BLOCK_HLSL = UNPACK + r"""
float4 color = lerp(Texture2DArraySample(Blocks, BlocksSampler, float3(uv, entry.x)), Texture2DArraySample(Blocks, BlocksSampler, float3(uv, entry.y)), entry.z);
if (code & (1u << 13)) color.rgb *= tint;
uint overlay = (uint)round(CD6);
if ((overlay & (1u << 15)) && ((overlay >> (16 + face)) & 1))
{
    float4 layer = Table.Load(int3(overlay & 1023, 0, 0));
    float4 over = Texture2DArraySample(Blocks, BlocksSampler, float3(uv, layer.x));
    if (overlay & (1u << 13)) over.rgb *= tint;
    color.rgb = lerp(color.rgb, over.rgb, over.a);
}
return color;
"""

# The light a block instance's vertex emits, 0-15: the state's (Light, custom data 8), or the brighter element light a
# model vertex carries in its own code (Face.y, bits 16-19 under the bit-20 marker).
BLOCK_LIGHT_HLSL = r"""
uint own = (uint)round(Face.y);
return (own & (1u << 20)) ? max(Light, (float)((own >> 16) & 15)) : Light;
"""

# Water: the still texture on top and bottom; the 32x32 flow texture on the sides at half scale, as
# LiquidBlockRenderer samples it (a side of height h shows v (1 - h) / 2 .. 0.5).
WATER_HLSL = UNPACK + r"""
float4 color;
if (face >= 4)
    color = lerp(Texture2DArraySample(Blocks, BlocksSampler, float3(uv, entry.x)), Texture2DArraySample(Blocks, BlocksSampler, float3(uv, entry.y)), entry.z);
else
    color = lerp(Texture2DArraySample(Flow, FlowSampler, float3(uv * 0.5, entry.x)), Texture2DArraySample(Flow, FlowSampler, float3(uv * 0.5, entry.y)), entry.z);
if (code & (1u << 13)) color.rgb *= tint;
return color;
"""

# Chunk vertices carry their own face: Codes = (code, overlay), FaceTint = (packed tint, face), Shade.r = occlusion.
CHUNK_UNPACK = r"""
int face = (int)round(FaceTint.y);
uint code = (uint)round(Codes.x);
uint overlay = (uint)round(Codes.y);
uint packed = (uint)round(FaceTint.x);
float3 tint = float3((packed >> 16) & 255, (packed >> 8) & 255, packed & 255) / 255.0;
tint = lerp(tint / 12.92, pow((tint + 0.055) / 1.055, 2.4), step(0.04045, tint));
float2 uv = UV;
uint transform = (code >> 10) & 7;
if (transform & 4) uv.x = 1 - uv.x;
for (uint turn = 0; turn < (transform & 3); turn++) uv = float2(uv.y, 1 - uv.x);
float2 dx = ddx(uv);
float2 dy = ddy(uv);
uv = frac(uv);
float4 entry = Table.Load(int3(code & 1023, 0, 0));
float shade = lerp(0.45, 1.0, saturate(Shade.r));
"""

CHUNK_BLOCK_HLSL = CHUNK_UNPACK + r"""
float4 color = lerp(Texture2DArraySampleGrad(Blocks, BlocksSampler, float3(uv, entry.x), dx, dy), Texture2DArraySampleGrad(Blocks, BlocksSampler, float3(uv, entry.y), dx, dy), entry.z);
if (code & (1u << 13)) color.rgb *= tint;
if ((overlay & (1u << 15)) && ((overlay >> (16 + face)) & 1))
{
    float4 layer = Table.Load(int3(overlay & 1023, 0, 0));
    float4 over = Texture2DArraySampleGrad(Blocks, BlocksSampler, float3(uv, layer.x), dx, dy);
    if (overlay & (1u << 13)) over.rgb *= tint;
    color.rgb = lerp(color.rgb, over.rgb, over.a);
}
color.rgb *= shade;
return color;
"""

# M_ChunkOpaque adds the render-only ore looks (Source/UnnamedVoxel/Meshing/VoxelOreLooks.h, Scripts/OreLooks.py): an
# ore face whose overlay code carries bit 14 without the overlay's bit 15 samples T_OreLooks' layer (bits 0-9) instead
# of its block texture. Level (bits 10-11) 1 reads the gloss class from that layer's alpha (1 matte, 2/3 metal, 1/3
# gem) into roughness and specular; 3 is the debug mask, flat magenta. Every other face keeps roughness 1, specular
# 0.1 and no extra emission.
CHUNK_OPAQUE_HLSL = CHUNK_BLOCK_HLSL.replace("color.rgb *= shade;", r"""LookRoughness = 1.0;
LookSpecular = 0.1;
LookEmissive = float3(0, 0, 0);
if ((overlay & (3u << 14)) == (1u << 14))
{
    uint level = (overlay >> 10) & 3;
    float4 look = Texture2DArraySampleGrad(Looks, LooksSampler, float3(uv, overlay & 1023), dx, dy);
    color.rgb = look.rgb;
    if (level == 1)
    {
        float metal = step(0.5, look.a) * step(look.a, 0.9);
        float gem = step(look.a, 0.5);
        LookRoughness = 1.0 - 0.6 * metal - 0.8 * gem;
        LookSpecular = 0.1 + 0.5 * metal + 0.8 * gem;
    }
    else if (level == 3)
    {
        color.rgb = float3(1, 0, 1);
        LookEmissive = float3(20000, 0, 20000);
    }
}
color.rgb *= shade;""")
LOOK_OUTPUTS = ("LookRoughness", "LookSpecular", "LookEmissive")
if CHUNK_BLOCK_HLSL.count("color.rgb *= shade;") != 1:
    raise RuntimeError("BLOCK_MATERIALS FAIL: the ore looks' anchor in CHUNK_BLOCK_HLSL moved")

CHUNK_WATER_HLSL = CHUNK_UNPACK + r"""
float4 color;
if (face >= 4)
    color = lerp(Texture2DArraySampleGrad(Blocks, BlocksSampler, float3(uv, entry.x), dx, dy), Texture2DArraySampleGrad(Blocks, BlocksSampler, float3(uv, entry.y), dx, dy), entry.z);
else
    color = lerp(Texture2DArraySampleGrad(Flow, FlowSampler, float3(uv * 0.5, entry.x), dx * 0.5, dy * 0.5), Texture2DArraySampleGrad(Flow, FlowSampler, float3(uv * 0.5, entry.y), dx * 0.5, dy * 0.5), entry.z);
if (code & (1u << 13)) color.rgb *= tint;
return color;
"""

MATERIALS = {
    "M_BlockOpaque": {"blend": unreal.BlendMode.BLEND_OPAQUE, "hlsl": BLOCK_HLSL},
    "M_BlockCutout": {"blend": unreal.BlendMode.BLEND_MASKED, "hlsl": BLOCK_HLSL},
    "M_BlockTranslucent": {"blend": unreal.BlendMode.BLEND_TRANSLUCENT, "hlsl": BLOCK_HLSL},
    "M_BlockWater": {"blend": unreal.BlendMode.BLEND_TRANSLUCENT, "hlsl": WATER_HLSL},
    "M_ChunkOpaque": {"blend": unreal.BlendMode.BLEND_OPAQUE, "hlsl": CHUNK_OPAQUE_HLSL, "chunk": True, "looks": True},
    "M_ChunkCutout": {"blend": unreal.BlendMode.BLEND_MASKED, "hlsl": CHUNK_BLOCK_HLSL, "chunk": True},
    "M_ChunkTranslucent": {"blend": unreal.BlendMode.BLEND_TRANSLUCENT, "hlsl": CHUNK_BLOCK_HLSL, "chunk": True},
    "M_ChunkWater": {"blend": unreal.BlendMode.BLEND_TRANSLUCENT, "hlsl": CHUNK_WATER_HLSL, "chunk": True},
}


def fail(message):
    raise RuntimeError("BLOCK_MATERIALS FAIL: " + message)


def get_or_create(name):
    path = ProjectPaths.block_asset(name)
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


def build(name, spec, block_textures, flow_textures):
    material = get_or_create(name)
    water = name.endswith("Water")
    chunk = spec.get("chunk", False)
    material.set_editor_property("blend_mode", spec["blend"])
    material.set_editor_property("two_sided", False)
    # Chunk meshes use RealtimeMesh's own vertex factory, which needs no usage flag.
    material.set_editor_property("used_with_instanced_static_meshes", not chunk)
    if spec["blend"] == unreal.BlendMode.BLEND_MASKED:
        material.set_editor_property("opacity_mask_clip_value", 0.5)  # 26.3 cutout discards alpha below 0.5
    if spec["blend"] == unreal.BlendMode.BLEND_TRANSLUCENT:
        material.set_editor_property("translucency_lighting_mode",
                                     unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)

    def node(cls, x, y):
        return lib.create_material_expression(material, cls, x, y)

    def link(source, output, target, input_name):
        if not lib.connect_material_expressions(source, output, target, input_name):
            fail("%s: could not connect to %s" % (name, input_name))

    def texture_parameter(parameter, texture, y):
        expr = node(unreal.MaterialExpressionTextureObjectParameter, -900, y)
        expr.set_editor_property("parameter_name", parameter)
        expr.set_editor_property("texture", texture)
        return expr

    if chunk:
        inputs = ["UV", "Codes", "FaceTint", "Shade", "Blocks", "Table"]
    else:
        inputs = ["CD%d" % i for i in range(CUSTOM_DATA - 1)] + ["UV", "Face", "Blocks", "Table"]
    inputs += ["Flow"] if water else []
    looks = spec.get("looks", False)
    inputs += ["Looks"] if looks else []
    custom = node(unreal.MaterialExpressionCustom, -400, 0)
    custom.set_editor_property("description", "Block face: texture layer, UV transform, animation, tint, overlay")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("code", spec["hlsl"])
    if looks:
        outputs = []
        for output_name in LOOK_OUTPUTS:
            output = unreal.CustomOutput()
            output.set_editor_property("output_name", output_name)
            output.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3
                                       if output_name == "LookEmissive" else unreal.CustomMaterialOutputType.CMOT_FLOAT1)
            outputs.append(output)
        custom.set_editor_property("additional_outputs", outputs)
    items = []
    for input_name in inputs:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", input_name)
        items.append(item)
    custom.set_editor_property("inputs", items)
    uv0 = node(unreal.MaterialExpressionTextureCoordinate, -900, 0)
    link(uv0, "", custom, "UV")
    if chunk:
        for index, input_name in ((1, "Codes"), (2, "FaceTint")):
            channel = node(unreal.MaterialExpressionTextureCoordinate, -900, index * 80)
            channel.set_editor_property("coordinate_index", index)
            link(channel, "", custom, input_name)
        vertex_color = node(unreal.MaterialExpressionVertexColor, -900, 240)
        link(vertex_color, "", custom, "Shade")
        light_source, light_output, light_divisor = vertex_color, "G", 1.0  # G is light / 15 already
    else:
        data = []
        for index in range(CUSTOM_DATA):
            expr = node(unreal.MaterialExpressionPerInstanceCustomData, -900, -700 + index * 70)
            expr.set_editor_property("data_index", index)
            data.append(expr)
            if index < CUSTOM_DATA - 1:
                link(expr, "", custom, "CD%d" % index)
        # Both channels: the face index, and a model vertex's own code.
        uv1 = node(unreal.MaterialExpressionTextureCoordinate, -900, 80)
        uv1.set_editor_property("coordinate_index", 1)
        link(uv1, "", custom, "Face")
        light_source, light_output, light_divisor = data[CUSTOM_DATA - 1], "", 15.0
        if not water:
            # The state's light (custom data 8), or a model vertex's element light where brighter (firefly bush).
            light_source = node(unreal.MaterialExpressionCustom, -700, 450)
            light_source.set_editor_property("description", "Block light: the state's, or a model vertex's own")
            light_source.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
            light_source.set_editor_property("code", BLOCK_LIGHT_HLSL)
            light_inputs = []
            for input_name in ("Light", "Face"):
                item = unreal.CustomInput()
                item.set_editor_property("input_name", input_name)
                light_inputs.append(item)
            light_source.set_editor_property("inputs", light_inputs)
            link(data[CUSTOM_DATA - 1], "", light_source, "Light")
            link(uv1, "", light_source, "Face")
    link(texture_parameter("BlockTextures", block_textures, 200), "", custom, "Blocks")
    # The runtime replaces this with the live animation table; the default only satisfies the compiler.
    default_table = unreal.load_asset("/Engine/EngineResources/Black")
    link(texture_parameter("TextureTable", default_table, 320), "", custom, "Table")
    if water:
        link(texture_parameter("WaterFlowTextures", flow_textures, 440), "", custom, "Flow")
    if looks:
        ore_looks = unreal.EditorAssetLibrary.load_asset(ProjectPaths.block_asset("T_OreLooks"))
        if not ore_looks:
            fail("T_OreLooks is missing: run ImportOreLooks.py first")
        link(texture_parameter("OreLookTextures", ore_looks, 440), "", custom, "Looks")

    def mask(channels, y):
        expr = node(unreal.MaterialExpressionComponentMask, -150, y)
        for channel in "rgba":
            expr.set_editor_property(channel, channel in channels)
        link(custom, "", expr, "")
        return expr

    rgb = mask("rgb", 0)
    lib.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    if spec["blend"] == unreal.BlendMode.BLEND_MASKED:
        lib.connect_material_property(mask("a", 120), "", unreal.MaterialProperty.MP_OPACITY_MASK)
    if spec["blend"] == unreal.BlendMode.BLEND_TRANSLUCENT:
        lib.connect_material_property(mask("a", 120), "", unreal.MaterialProperty.MP_OPACITY)
    # Flat, matte blocks; water keeps a little sheen.
    if looks:
        # The same 1 and 0.1 on every face but a glossy ore look's (CHUNK_OPAQUE_HLSL).
        for output_name, prop in (("LookRoughness", unreal.MaterialProperty.MP_ROUGHNESS),
                                  ("LookSpecular", unreal.MaterialProperty.MP_SPECULAR)):
            if not lib.connect_material_property(custom, output_name, prop):
                fail("%s: could not connect %s" % (name, output_name))
    else:
        roughness = node(unreal.MaterialExpressionConstant, -150, 240)
        roughness.set_editor_property("r", 0.25 if water else 1.0)
        lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
        specular = node(unreal.MaterialExpressionConstant, -150, 320)
        specular.set_editor_property("r", 0.5 if water else 0.1)
        lib.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
    if not water:
        # Light-emitting blocks (sea lantern, glowstone...): color x light / 15 x LightEmissive.
        scale = node(unreal.MaterialExpressionScalarParameter, -400, 400)
        scale.set_editor_property("parameter_name", "LightEmissive")
        scale.set_editor_property("default_value", CHUNK_LIGHT_EMISSIVE if chunk else BLOCK_LIGHT_EMISSIVE)
        light = node(unreal.MaterialExpressionDivide, -250, 450)
        link(light_source, light_output, light, "A")
        light.set_editor_property("const_b", light_divisor)
        amount = node(unreal.MaterialExpressionMultiply, -100, 450)
        link(light, "", amount, "A")
        link(scale, "", amount, "B")
        emissive = node(unreal.MaterialExpressionMultiply, 50, 400)
        link(rgb, "", emissive, "A")
        link(amount, "", emissive, "B")
        if looks:
            # Plus the debug mask's glow, zero on every other face.
            total = node(unreal.MaterialExpressionAdd, 200, 400)
            link(emissive, "", total, "A")
            if not lib.connect_material_expressions(custom, "LookEmissive", total, "B"):
                fail(name + ": could not connect LookEmissive")
            emissive = total
        lib.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material):
        fail("could not save " + name)
    return material


def main():
    registry = unreal.EditorAssetLibrary.load_asset(ProjectPaths.block_asset("DA_Blocks"))
    if not registry:
        fail("DA_Blocks is missing: run ImportBlocks.py first")
    block_textures = registry.get_editor_property("block_textures")
    flow_textures = registry.get_editor_property("water_flow_textures")
    cmdline = unreal.SystemLibrary.get_command_line()
    compiled = "AllowCommandletRendering" in cmdline and "NullRHI" not in cmdline
    selected = set(MATERIALS)
    for token in cmdline.split():
        if token.lower().startswith("-materials="):
            selected = {name.strip() for name in token.split("=", 1)[1].strip('"').split(",") if name.strip()}
    unknown = selected - set(MATERIALS)
    if unknown:
        fail("unknown materials: " + ", ".join(sorted(unknown)))
    for name, spec in MATERIALS.items():
        if name not in selected:
            continue
        material = build(name, spec, block_textures, flow_textures)
        stats = lib.get_statistics(material)
        unreal.log("BLOCK_MATERIALS %s: PS instr %d, samplers %d" % (name, stats.num_pixel_shader_instructions,
                                                                      stats.num_samplers))
        if compiled and stats.num_pixel_shader_instructions == 0:
            fail(name + ": the shader did not compile (see ShaderCompilers errors above)")
    unreal.log("BLOCK_MATERIALS PASS%s" % ("" if compiled else " (graphs only; add -AllowCommandletRendering to compile)"))


main()
