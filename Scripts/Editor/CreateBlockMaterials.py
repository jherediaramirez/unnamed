"""Author the block materials (M_BlockOpaque, M_BlockCutout, M_BlockTranslucent, M_BlockWater) and the voxel chunk
materials (M_ChunkOpaque, M_ChunkCutout, M_ChunkTranslucent, M_ChunkWater), and M_WaterFog, the post-process fog while
the camera's eye is in a fluid (WATER_FOG_HLSL).

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

The far meshes' materials (far LOD step 2: M_ChunkFarOpaque, M_ChunkFarCutout, M_ChunkFarWater) are their layer's chunk
material plus the edge fade: they fade out between the FarFadeStart and FarFadeEnd parameters (cm of horizontal distance
to the camera, a cylinder as vanilla's fog), dithered on the masked layers and smooth on water, so the sky shows through
the drawn radius's last columns. Shadow passes take the unfaded mask.

The look dials' scalars (the brightness pass) come from MPC_VoxelLook, which AVoxelWorld sets from voxel.Look.*: LowGlow
multiplies the glow of emitters of light level 1 to 7 (0.03: glow lichen's soft glow), WaterSpecular is the water's
specular.

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

# Fluids sample the array their face's texture is in, which the table's fourth channel names (1 the 32x32 flow array,
# FoundationBlockTextureTable.h): the still texture on flat tops and bottoms, the flow texture on the sides and on sloped
# tops (water and sky W3), at half scale, as LiquidBlockRenderer samples it (a side of height h shows
# v (1 - h) / 2 .. 0.5; a sloped top's UVs come turned along its flow, Source/UnnamedVoxel/Meshing/VoxelFluidSurface.h).
FLOW_SAMPLE = r"""float4 color;
if (entry.w > 0.5)
    color = lerp(Texture2DArraySample(Flow, FlowSampler, float3(uv * 0.5, entry.x)), Texture2DArraySample(Flow, FlowSampler, float3(uv * 0.5, entry.y)), entry.z);
else
    color = lerp(Texture2DArraySample(Blocks, BlocksSampler, float3(uv, entry.x)), Texture2DArraySample(Blocks, BlocksSampler, float3(uv, entry.y)), entry.z);
"""
WATER_HLSL = UNPACK + FLOW_SAMPLE + r"""if (code & (1u << 13)) color.rgb *= tint;
return color;
"""
# Lava's faces are opaque: M_BlockOpaque samples the flow array too (flowing lava's sides, sloped tops).
BLOCK_SAMPLE = "float4 color = lerp(Texture2DArraySample(Blocks, BlocksSampler, float3(uv, entry.x)), Texture2DArraySample(Blocks, BlocksSampler, float3(uv, entry.y)), entry.z);\n"
if BLOCK_HLSL.count(BLOCK_SAMPLE) != 1:
    raise RuntimeError("BLOCK_MATERIALS FAIL: the block sample in BLOCK_HLSL moved")
BLOCK_FLOW_HLSL = BLOCK_HLSL.replace(BLOCK_SAMPLE, FLOW_SAMPLE)

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
// The flow array's faces take their block fraction at half scale (frac(raw * 0.5)), so a sloped top's turned UVs, which
// stay inside one texture but cross block lines, never wrap.
float2 raw = uv;
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

# Chunk fluids, as FLOW_SAMPLE: the flow array at half scale from the unwrapped UV.
CHUNK_FLOW_SAMPLE = r"""float4 color;
if (entry.w > 0.5)
    color = lerp(Texture2DArraySampleGrad(Flow, FlowSampler, float3(frac(raw * 0.5), entry.x), dx * 0.5, dy * 0.5), Texture2DArraySampleGrad(Flow, FlowSampler, float3(frac(raw * 0.5), entry.y), dx * 0.5, dy * 0.5), entry.z);
else
    color = lerp(Texture2DArraySampleGrad(Blocks, BlocksSampler, float3(uv, entry.x), dx, dy), Texture2DArraySampleGrad(Blocks, BlocksSampler, float3(uv, entry.y), dx, dy), entry.z);
"""
CHUNK_BLOCK_SAMPLE = "float4 color = lerp(Texture2DArraySampleGrad(Blocks, BlocksSampler, float3(uv, entry.x), dx, dy), Texture2DArraySampleGrad(Blocks, BlocksSampler, float3(uv, entry.y), dx, dy), entry.z);\n"
if CHUNK_BLOCK_HLSL.count(CHUNK_BLOCK_SAMPLE) != 1:
    raise RuntimeError("BLOCK_MATERIALS FAIL: the block sample in CHUNK_BLOCK_HLSL moved")

# M_ChunkOpaque adds the render-only ore looks (Source/UnnamedVoxel/Meshing/VoxelOreLooks.h, Scripts/OreLooks.py): an
# ore face whose overlay code carries bit 14 without the overlay's bit 15 samples T_OreLooks' layer (bits 0-9) instead
# of its block texture. Level (bits 10-11) 1 reads the gloss class from that layer's alpha (1 matte, 2/3 metal, 1/3
# gem) into roughness and specular; 3 is the debug mask, flat magenta. Every other face keeps roughness 1, specular
# 0.1 and no extra emission.
CHUNK_OPAQUE_HLSL = CHUNK_BLOCK_HLSL.replace(CHUNK_BLOCK_SAMPLE, CHUNK_FLOW_SAMPLE).replace("color.rgb *= shade;", r"""LookRoughness = 1.0;
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

CHUNK_WATER_HLSL = CHUNK_UNPACK + CHUNK_FLOW_SAMPLE + r"""if (code & (1u << 13)) color.rgb *= tint;
return color;
"""

MATERIALS = {
    "M_BlockOpaque": {"blend": unreal.BlendMode.BLEND_OPAQUE, "hlsl": BLOCK_FLOW_HLSL, "flow": True},
    "M_BlockCutout": {"blend": unreal.BlendMode.BLEND_MASKED, "hlsl": BLOCK_HLSL},
    "M_BlockTranslucent": {"blend": unreal.BlendMode.BLEND_TRANSLUCENT, "hlsl": BLOCK_HLSL},
    "M_BlockWater": {"blend": unreal.BlendMode.BLEND_TRANSLUCENT, "hlsl": WATER_HLSL},
    "M_ChunkOpaque": {"blend": unreal.BlendMode.BLEND_OPAQUE, "hlsl": CHUNK_OPAQUE_HLSL, "chunk": True, "looks": True,
                      "flow": True},
    "M_ChunkCutout": {"blend": unreal.BlendMode.BLEND_MASKED, "hlsl": CHUNK_BLOCK_HLSL, "chunk": True},
    "M_ChunkTranslucent": {"blend": unreal.BlendMode.BLEND_TRANSLUCENT, "hlsl": CHUNK_BLOCK_HLSL, "chunk": True},
    "M_ChunkWater": {"blend": unreal.BlendMode.BLEND_TRANSLUCENT, "hlsl": CHUNK_WATER_HLSL, "chunk": True},
    # The far meshes' layers (VoxelFarMesher writes only these three): their near graph plus the edge fade (FAR_FADE_HLSL).
    "M_ChunkFarOpaque": {"blend": unreal.BlendMode.BLEND_MASKED, "hlsl": CHUNK_OPAQUE_HLSL, "chunk": True,
                         "looks": True, "flow": True, "far": True},
    "M_ChunkFarCutout": {"blend": unreal.BlendMode.BLEND_MASKED, "hlsl": CHUNK_BLOCK_HLSL, "chunk": True, "far": True,
                         "fade_alpha": True},
    "M_ChunkFarWater": {"blend": unreal.BlendMode.BLEND_TRANSLUCENT, "hlsl": CHUNK_WATER_HLSL, "chunk": True,
                        "far": True},
    # The fog in fluids (Source/Unnamed/Voxel/VoxelWaterFog.h): a post-process blendable, not a block material.
    "M_WaterFog": {"post": True},
}

# The fog in fluids, after the tonemapper so Color mixes as the display value vanilla's fog colour is: linear fog by
# distance to the camera, 0 at Start and full at End (cm; Start may be negative), as vanilla's linear fog.
WATER_FOG_HLSL = r"""
float fog = saturate((Distance - Start) / max(End - Start, 1.0));
return lerp(Scene, Color, fog);
"""

# The edge fade: 1 inside FarFadeStart, 0 from FarFadeEnd, by horizontal distance to the camera (Rel is the camera-relative
# world position, cm). Return: the faded mask (the cutout's 26.3 test, alpha >= 0.5, times the fade); Unfaded: the mask
# alone, for shadow passes, whose view is the light's.
FAR_FADE_HLSL = r"""
float fade = saturate((End - length(Rel.xy)) / max(End - Start, 1.0));
Unfaded = step(0.5, Alpha);
return Unfaded * fade;
"""
# Unset parameters draw everything: the fade starts beyond any distance.
FAR_FADE_DEFAULT_CM = 1.0e9

# The look dials' collection (Source/Unnamed/Voxel/VoxelLook.h) and its scalars' defaults, which are the voxel.Look.*
# defaults: maps without the voxel world (the Playground) draw with these.
LOOK_COLLECTION = "MPC_VoxelLook"
LOOK_PARAMETERS = {"LowGlow": 0.03, "WaterSpecular": 0.25}
# Light is the level / 15: emitters of level 1 to 7 (glow lichen 7) glow x LowGlow, brighter ones as before.
LOW_GLOW_HLSL = r"""
return Light * (Light * 15.0 < 7.5 ? LowGlow : 1.0);
"""
_look_collection = []


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


def look_collection():
    """MPC_VoxelLook with LOOK_PARAMETERS' defaults. Created once: materials reference a parameter by its id, so existing
    parameters keep theirs and only take the default."""
    if _look_collection:
        return _look_collection[0]
    path = ProjectPaths.block_asset(LOOK_COLLECTION)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        collection = unreal.EditorAssetLibrary.load_asset(path)
    else:
        folder, short = ProjectPaths.split(path)
        collection = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            short, folder, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    if not collection:
        fail("could not create " + path)
    parameters = list(collection.get_editor_property("scalar_parameters"))
    names = [str(each.get_editor_property("parameter_name")) for each in parameters]
    changed = False
    for parameter_name, default in LOOK_PARAMETERS.items():
        if parameter_name in names:
            parameter = parameters[names.index(parameter_name)]
        else:
            parameter = unreal.CollectionScalarParameter()
            parameter.set_editor_property("parameter_name", parameter_name)
            parameters.append(parameter)
            changed = True
        if abs(parameter.get_editor_property("default_value") - default) > 1e-6:
            parameter.set_editor_property("default_value", default)
            changed = True
    # Only a real change is written: each edit gives the collection a new state, which materials not rebuilt in this
    # run (-Materials=) would recompile for.
    if changed:
        collection.set_editor_property("scalar_parameters", parameters)
        if not unreal.EditorAssetLibrary.save_loaded_asset(collection, False):
            fail("could not save " + path)
    found = {str(each.get_editor_property("parameter_name")): each.get_editor_property("default_value")
             for each in collection.get_editor_property("scalar_parameters")}
    if any(abs(found.get(key, -1.0) - value) > 1e-6 for key, value in LOOK_PARAMETERS.items()):
        fail("%s holds %s, expected %s" % (LOOK_COLLECTION, found, LOOK_PARAMETERS))
    _look_collection.append(collection)
    return collection


def build_water_fog(name):
    material = get_or_create(name)
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    location = getattr(unreal.BlendableLocation, "BL_SCENE_COLOR_AFTER_TONEMAPPING", None) or         unreal.BlendableLocation.BL_AFTER_TONEMAPPING
    material.set_editor_property("blendable_location", location)

    def node(cls, x, y):
        return lib.create_material_expression(material, cls, x, y)

    def link(source, output, target, input_name):
        if not lib.connect_material_expressions(source, output, target, input_name):
            fail("%s: could not connect to %s" % (name, input_name))

    scene = node(unreal.MaterialExpressionSceneTexture, -700, 0)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    # In a post-process material the world position is the scene depth's.
    distance = node(unreal.MaterialExpressionDistance, -450, 150)
    link(node(unreal.MaterialExpressionWorldPosition, -700, 120), "", distance, "A")
    link(node(unreal.MaterialExpressionCameraPositionWS, -700, 200), "", distance, "B")
    color = node(unreal.MaterialExpressionVectorParameter, -700, 300)
    color.set_editor_property("parameter_name", "Color")
    custom = node(unreal.MaterialExpressionCustom, -200, 150)
    custom.set_editor_property("description", "Water fog: linear by distance, Start to End")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", WATER_FOG_HLSL)
    inputs = []
    for input_name in ("Scene", "Distance", "Color", "Start", "End"):
        item = unreal.CustomInput()
        item.set_editor_property("input_name", input_name)
        inputs.append(item)
    custom.set_editor_property("inputs", inputs)
    link(scene, "Color", custom, "Scene")
    link(distance, "", custom, "Distance")
    link(color, "", custom, "Color")
    # Unset, the fog never reaches the view (the C++ sets both while it is on).
    for index, parameter in enumerate(("Start", "End")):
        expr = node(unreal.MaterialExpressionScalarParameter, -700, 400 + 80 * index)
        expr.set_editor_property("parameter_name", parameter)
        expr.set_editor_property("default_value", FAR_FADE_DEFAULT_CM * (index + 1))
        link(expr, "", custom, parameter)
    lib.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material):
        fail("could not save " + name)
    return material


def build(name, spec, block_textures, flow_textures):
    if spec.get("post", False):
        return build_water_fog(name)
    material = get_or_create(name)
    water = name.endswith("Water")
    chunk = spec.get("chunk", False)
    material.set_editor_property("blend_mode", spec["blend"])
    material.set_editor_property("two_sided", False)
    # Chunk meshes use RealtimeMesh's own vertex factory, which needs no usage flag.
    material.set_editor_property("used_with_instanced_static_meshes", not chunk)
    far = spec.get("far", False)
    if spec["blend"] == unreal.BlendMode.BLEND_MASKED:
        material.set_editor_property("opacity_mask_clip_value", 0.5)  # 26.3 cutout discards alpha below 0.5
        # The far fade dithers its mask (TSR resolves it); a near material's mask is already 0 or 1 where it matters.
        material.set_editor_property("dither_opacity_mask", far)
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

    def look_parameter(parameter, x, y):
        expr = node(unreal.MaterialExpressionCollectionParameter, x, y)
        expr.set_editor_property("collection", look_collection())
        expr.set_editor_property("parameter_name", parameter)
        return expr

    if chunk:
        inputs = ["UV", "Codes", "FaceTint", "Shade", "Blocks", "Table"]
    else:
        inputs = ["CD%d" % i for i in range(CUSTOM_DATA - 1)] + ["UV", "Face", "Blocks", "Table"]
    flow = water or spec.get("flow", False)
    inputs += ["Flow"] if flow else []
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
    if flow:
        link(texture_parameter("WaterFlowTextures", flow_textures, 440), "", custom, "Flow")
    if looks:
        ore_looks = unreal.EditorAssetLibrary.load_asset(ProjectPaths.block_asset("T_OreLooks"))
        if not ore_looks:
            fail("T_OreLooks is missing: run ImportOreLooks.py first")
        link(texture_parameter("OreLookTextures", ore_looks, 560), "", custom, "Looks")

    def mask(channels, y):
        expr = node(unreal.MaterialExpressionComponentMask, -150, y)
        for channel in "rgba":
            expr.set_editor_property(channel, channel in channels)
        link(custom, "", expr, "")
        return expr

    rgb = mask("rgb", 0)
    lib.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    if far:
        # The edge fade (FAR_FADE_HLSL). Cutout fades its 26.3 mask; opaque and water fade 1 (water's alpha multiplies
        # after).
        fade = node(unreal.MaterialExpressionCustom, -400, 700)
        fade.set_editor_property("description", "Far edge fade: horizontal distance to the camera")
        fade.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        fade.set_editor_property("code", FAR_FADE_HLSL)
        unfaded = unreal.CustomOutput()
        unfaded.set_editor_property("output_name", "Unfaded")
        unfaded.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        fade.set_editor_property("additional_outputs", [unfaded])
        fade_inputs = []
        for input_name in ("Rel", "Start", "End", "Alpha"):
            item = unreal.CustomInput()
            item.set_editor_property("input_name", input_name)
            fade_inputs.append(item)
        fade.set_editor_property("inputs", fade_inputs)
        rel = node(unreal.MaterialExpressionWorldPosition, -700, 700)
        rel.set_editor_property("world_position_shader_offset",
                                unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
        link(rel, "", fade, "Rel")
        for index, parameter in enumerate(("FarFadeStart", "FarFadeEnd")):
            expr = node(unreal.MaterialExpressionScalarParameter, -700, 780 + index * 70)
            expr.set_editor_property("parameter_name", parameter)
            expr.set_editor_property("default_value", FAR_FADE_DEFAULT_CM * (index + 1))
            link(expr, "", fade, ("Start", "End")[index])
        if spec.get("fade_alpha", False):
            link(mask("a", 120), "", fade, "Alpha")
        else:
            one = node(unreal.MaterialExpressionConstant, -700, 920)
            one.set_editor_property("r", 1.0)
            link(one, "", fade, "Alpha")
    if spec["blend"] == unreal.BlendMode.BLEND_MASKED:
        if far:
            # Shadow passes see the light's view, so they take the mask without the camera's fade.
            switch = node(unreal.MaterialExpressionShadowReplace, -150, 700)
            link(fade, "", switch, "Default")
            link(fade, "Unfaded", switch, "Shadow")
            lib.connect_material_property(switch, "", unreal.MaterialProperty.MP_OPACITY_MASK)
        else:
            lib.connect_material_property(mask("a", 120), "", unreal.MaterialProperty.MP_OPACITY_MASK)
    if spec["blend"] == unreal.BlendMode.BLEND_TRANSLUCENT:
        opacity = mask("a", 120)
        if far:
            faded = node(unreal.MaterialExpressionMultiply, -150, 700)
            link(opacity, "", faded, "A")
            link(fade, "", faded, "B")
            opacity = faded
        lib.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
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
        if water:  # voxel.Look.WaterSpecular
            specular = look_parameter("WaterSpecular", -150, 320)
        else:
            specular = node(unreal.MaterialExpressionConstant, -150, 320)
            specular.set_editor_property("r", 0.1)
        lib.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
    if not water:
        # Light-emitting blocks (sea lantern, glowstone...): color x light / 15 x LightEmissive.
        scale = node(unreal.MaterialExpressionScalarParameter, -400, 400)
        scale.set_editor_property("parameter_name", "LightEmissive")
        scale.set_editor_property("default_value", CHUNK_LIGHT_EMISSIVE if chunk else BLOCK_LIGHT_EMISSIVE)
        light = node(unreal.MaterialExpressionDivide, -250, 450)
        link(light_source, light_output, light, "A")
        light.set_editor_property("const_b", light_divisor)
        # voxel.Look.LowGlow on the dim emitters (LOW_GLOW_HLSL).
        low = node(unreal.MaterialExpressionCustom, -175, 520)
        low.set_editor_property("description", "Look: x LowGlow below light level 8")
        low.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        low.set_editor_property("code", LOW_GLOW_HLSL)
        low_inputs = []
        for input_name in ("Light", "LowGlow"):
            item = unreal.CustomInput()
            item.set_editor_property("input_name", input_name)
            low_inputs.append(item)
        low.set_editor_property("inputs", low_inputs)
        link(light, "", low, "Light")
        link(look_parameter("LowGlow", -400, 520), "", low, "LowGlow")
        amount = node(unreal.MaterialExpressionMultiply, -100, 450)
        link(low, "", amount, "A")
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
    look_collection()
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
