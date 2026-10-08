"""Build (or rebuild in place) the world-aligned dev grid material and its markup texture.

Run headless through the editor (a second process is fine while an interactive editor is open):

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' "C:\\dev\\projects\\Unnamed\\Unnamed.uproject" -run=pythonscript -script="C:\\dev\\projects\\Unnamed\\Scripts\\Editor\\CreateDevGridMaterial.py" -unattended -nop4 -nosplash

As written this builds the assets but does not compile shaders. To prove the HLSL compiles on D3D12/SM6 add
-AllowCommandletRendering (and no -NullRHI); the script then fails if no pixel shader was produced, and the log
must contain no "Failed to compile Material" lines (the run ends with "Success - 0 error(s)").

Inputs/outputs
  Art/DevGrid/T_DevGridMarkup.png      -> /Game/DevGrid/T_DevGridMarkup   (made by CreateDevGridTexture.py)
  /Game/DevGrid/M_DevGrid              clear coat, opaque; every pixel is a function of world position

M_DevGrid is mapped in world space (triplanar, dominant axis of the vertex normal, narrow blend), so the 2 km
floor, scaled engine cubes and 1 m voxel cubes all share one continuous metric grid. Runtime code only needs to
set the vector parameter "Color" on a dynamic material instance.

Tile coordinates (metres; u = viewer's right, v = down the face) so "1m" reads upright and unmirrored:
  +Z floor   (X, Y)     looking north (-Y): +X is right, south (+Y) is down
  -Z ceiling (-X, Y)
  +X face    (-Y, -Z)   -X face (Y, -Z)   +Y face (X, -Z)   -Y face (-X, -Z)

Re-running updates the texture import and rebuilds the material graph on the same asset, so material
instances that parent M_DevGrid keep working.
"""
import sys
from pathlib import Path

import unreal

FOLDER = "/Game/DevGrid"
TEXTURE_NAME = "T_DevGridMarkup"
MATERIAL_NAME = "M_DevGrid"
TEXTURE = FOLDER + "/" + TEXTURE_NAME
MATERIAL = FOLDER + "/" + MATERIAL_NAME
GROUP = "DevGrid"

# name, default, kind ("vector" linear RGB / "scalar")
PARAMETERS = [
    ("Color", (0.30, 0.34, 0.42), "vector"),
    ("SeamColor", (0.85, 0.88, 0.95), "vector"),
    ("SeamOpacity", 0.55, "scalar"),
    ("LabelOpacity", 0.35, "scalar"),
    ("CrossOpacity", 0.80, "scalar"),
    ("TileVariation", 0.035, "scalar"),
    ("MajorLineOpacity", 0.60, "scalar"),
    ("AxisLines", 1.0, "scalar"),
    ("Roughness", 0.45, "scalar"),
    ("ClearCoat", 0.30, "scalar"),
    ("ClearCoatRoughness", 0.08, "scalar"),
    ("ClearCoatFresnelFade", 1.0, "scalar"),
]

GRID_HLSL = r"""
// World-aligned triplanar dev grid. WorldPos is absolute world position in cm; one tile = 1 m.
float3 p = WorldPos * 0.01;
float3 n = Nrm;
float3 an = abs(n);
float3 sgn = float3(n.x < 0.0 ? -1.0 : 1.0, n.y < 0.0 ? -1.0 : 1.0, n.z < 0.0 ? -1.0 : 1.0);

// Tile coordinates in metres per projection axis (see the script docstring for the face convention).
float2 uvs[3];
uvs[0] = float2(-p.y * sgn.x, -p.z);
uvs[1] = float2( p.x * sgn.y, -p.z);
uvs[2] = float2( p.x * sgn.z,  p.y);

// Hard dominant-axis select with a narrow (0.1 in |n|) blend for future rounded meshes.
float mx = max(an.x, max(an.y, an.z));
float3 w = saturate((an - (mx - 0.1)) * 10.0);
w /= (w.x + w.y + w.z);

float3 mark = float3(0.0, 0.0, 0.0);
float major = 0.0;
[unroll]
for (int a = 0; a < 3; a++)
{
    float2 uv = uvs[a];
    float2 dx = ddx(uv);
    float2 dy = ddy(uv);
    float2 aa = max(abs(dx) + abs(dy), 1e-5);   // pixel footprint in metres
    if (w[a] > 0.001)
    {
        // frac() keeps float precision on the 2 km floor; explicit gradients avoid mip artifacts at the wrap.
        mark += w[a] * Tex.SampleGrad(TexSampler, frac(uv), dx, dy).rgb;

        // 10 m major lines: box-filtered edge, widened to ~1.5 px with energy kept (fades with distance).
        float2 dist = abs(frac(uv * 0.1 - 0.5) - 0.5) * 10.0;
        float2 wide = max(float2(0.05, 0.05), aa * 1.5);
        float2 cov = saturate((wide * 0.5 + aa * 0.5 - dist) / aa) * (0.05 / wide);
        major += w[a] * max(cov.x, cov.y);
    }
}

// Per-tile hash of the 1 m cell behind the face (1 cm into the surface: stable for faces on whole or half metres).
float3 axisMask = (an.x >= an.y && an.x >= an.z) ? float3(1.0, 0.0, 0.0)
                : ((an.y >= an.z) ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0));
float3 cell = floor(p - sgn * axisMask * 0.01);
uint3 q = (uint3)((int3)cell) * uint3(1597334673u, 3812015801u, 2798796415u);
uint hh = (q.x ^ q.y ^ q.z) * 1597334673u;
float h = (float)(hh >> 8) * (1.0 / 16777216.0);

float3 col = Color.rgb * (1.0 + (h - 0.5) * 2.0 * TileVariation);
col = lerp(col, SeamColor.rgb, saturate(mark.r * SeamOpacity));
col = lerp(col, SeamColor.rgb, saturate(mark.b * CrossOpacity));
col = lerp(col, SeamColor.rgb, saturate(major * MajorLineOpacity));
col *= 1.0 - saturate(mark.g * LabelOpacity);

// World axes on upward faces: red along X (y = 0), green along Y (x = 0).
float up = w.z * (n.z > 0.0 ? 1.0 : 0.0);
float2 ad = abs(p.xy);
float2 aaw = max(abs(ddx(p.xy)) + abs(ddy(p.xy)), 1e-5);
float2 awide = max(float2(0.06, 0.06), aaw * 1.5);
float2 acov = saturate((awide * 0.5 + aaw * 0.5 - ad) / aaw) * (0.06 / awide);
float axisAmt = AxisLines * up;
col = lerp(col, float3(0.85, 0.10, 0.06), saturate(acov.y * axisAmt));
col = lerp(col, float3(0.10, 0.65, 0.16), saturate(acov.x * axisAmt));

float printAmt = max(max(mark.r * SeamOpacity, mark.g * LabelOpacity),
                     max(max(mark.b * CrossOpacity, major * MajorLineOpacity), axisAmt * max(acov.x, acov.y)));
return float4(col, saturate(printAmt));
"""

SURFACE_HLSL = r"""
// Matte print (seams, label, lines) and a clear coat that fades at grazing angles so distant floors stay un-mirror-like.
float facing = saturate(dot(normalize(Nrm), normalize(CamVec)));
float f = saturate(CoatFade);
float fade = lerp(1.0, smoothstep(0.02, 0.45, facing), f);
float printAmt = saturate(Mark);
float rough = lerp(BaseRoughness, max(BaseRoughness, 0.7), printAmt);
float coat = CoatAmount * fade * (1.0 - 0.85 * printAmt);
float coatRough = saturate(CoatRoughness + (1.0 - fade) * f * 0.3);
return float3(rough, coat, coatRough);
"""


def fail(message):
    unreal.log_error("DEVGRID ERROR: " + message)
    raise RuntimeError(message)


def enum_value(enum_cls, *names):
    for name in names:
        if hasattr(enum_cls, name):
            return getattr(enum_cls, name)
    fail("None of {} exist on {}".format(names, enum_cls.__name__))


def import_texture():
    source = Path(unreal.Paths.project_dir()).resolve() / "Art" / "DevGrid" / "T_DevGridMarkup.png"
    if not source.exists():
        fail("Missing {} - run: uv run --with pillow python Scripts/Editor/CreateDevGridTexture.py".format(source))
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", FOLDER)
    task.set_editor_property("destination_name", TEXTURE_NAME)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    task.set_editor_property("factory", unreal.TextureFactory())
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.EditorAssetLibrary.load_asset(TEXTURE)
    if not texture:
        fail("Texture import failed: " + TEXTURE)
    settings = {
        "srgb": False,
        "compression_settings": unreal.TextureCompressionSettings.TC_MASKS,
        "mip_gen_settings": unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP,
        "lod_group": unreal.TextureGroup.TEXTUREGROUP_WORLD,
        "filter": unreal.TextureFilter.TF_DEFAULT,  # World group: anisotropic trilinear
        "address_x": unreal.TextureAddress.TA_WRAP,
        "address_y": unreal.TextureAddress.TA_WRAP,
        "never_stream": True,  # 1024px mask (~0.7 MB): keep every mip resident, no streaming blur
    }
    for key, value in settings.items():
        texture.set_editor_property(key, value)
    if not unreal.EditorAssetLibrary.save_loaded_asset(texture):
        fail("Could not save " + TEXTURE)
    unreal.log("DEVGRID texture {} {}x{}".format(TEXTURE, texture.blueprint_get_size_x(), texture.blueprint_get_size_y()))
    return texture


def get_or_create_material():
    if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL):
        material = unreal.EditorAssetLibrary.load_asset(MATERIAL)
        lib = unreal.MaterialEditingLibrary
        # delete_all_material_expressions skips entries while it erases, so repeat until the graph is empty.
        for _ in range(20):
            if lib.get_num_material_expressions(material) == 0:
                break
            lib.delete_all_material_expressions(material)
        if lib.get_num_material_expressions(material) != 0:
            fail("Could not clear the existing M_DevGrid graph")
        return material
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        MATERIAL_NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        fail("Could not create " + MATERIAL)
    return material


def build_material(texture):
    lib = unreal.MaterialEditingLibrary
    material = get_or_create_material()
    material.set_editor_property("shading_model", enum_value(unreal.MaterialShadingModel, "MSM_CLEAR_COAT"))
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("two_sided", False)
    # Placed blocks are instanced static meshes; without this usage a cooked build (and -game) renders them with
    # the engine's default checker material instead.
    material.set_editor_property("used_with_instanced_static_meshes", True)

    def node(cls, x, y):
        return lib.create_material_expression(material, cls, x, y)

    def custom(description, code, input_names, output_type, x, y):
        expr = node(unreal.MaterialExpressionCustom, x, y)
        expr.set_editor_property("description", description)
        expr.set_editor_property("output_type", output_type)
        inputs = []
        for name in input_names:
            item = unreal.CustomInput()
            item.set_editor_property("input_name", name)
            inputs.append(item)
        expr.set_editor_property("inputs", inputs)
        expr.set_editor_property("code", code)
        return expr

    def link(source, output, target, input_name):
        if not lib.connect_material_expressions(source, output, target, input_name):
            fail("Could not connect {} -> {}.{}; inputs are {}".format(
                source.get_class().get_name(), target.get_class().get_name(), input_name,
                list(lib.get_material_expression_input_names(target))))

    def mask(source, channel, x, y):
        expr = node(unreal.MaterialExpressionComponentMask, x, y)
        for name in "rgba":
            expr.set_editor_property(name, name in channel)
        link(source, "", expr, "")
        return expr

    params = {}
    for row, (name, default, kind) in enumerate(PARAMETERS):
        if kind == "vector":
            expr = node(unreal.MaterialExpressionVectorParameter, -1100, row * 120)
            expr.set_editor_property("default_value", unreal.LinearColor(default[0], default[1], default[2], 1.0))
        else:
            expr = node(unreal.MaterialExpressionScalarParameter, -1100, row * 120)
            expr.set_editor_property("default_value", float(default))
        expr.set_editor_property("parameter_name", name)
        expr.set_editor_property("group", GROUP)
        expr.set_editor_property("sort_priority", row)
        params[name] = expr

    position = node(unreal.MaterialExpressionWorldPosition, -1100, -400)
    normal = node(unreal.MaterialExpressionVertexNormalWS, -1100, -250)
    camera = node(unreal.MaterialExpressionCameraVectorWS, -1100, 1700)
    tex = node(unreal.MaterialExpressionTextureObject, -1100, -520)
    tex.set_editor_property("texture", texture)
    tex.set_editor_property("sampler_type", enum_value(unreal.MaterialSamplerType, "SAMPLERTYPE_MASKS", "SAMPLERTYPE_MASK"))

    grid_inputs = ["WorldPos", "Nrm", "Tex", "Color", "SeamColor", "SeamOpacity", "LabelOpacity", "CrossOpacity",
                   "TileVariation", "MajorLineOpacity", "AxisLines"]
    grid = custom("Triplanar dev grid: base colour (rgb) + print amount (a)", GRID_HLSL, grid_inputs,
                  unreal.CustomMaterialOutputType.CMOT_FLOAT4, -500, 0)
    link(position, "", grid, "WorldPos")
    link(normal, "", grid, "Nrm")
    link(tex, "", grid, "Tex")
    for name in grid_inputs[3:]:
        link(params[name], "", grid, name)

    surface_inputs = ["Mark", "Nrm", "CamVec", "BaseRoughness", "CoatAmount", "CoatRoughness", "CoatFade"]
    surface = custom("Roughness / clear coat with grazing-angle fade", SURFACE_HLSL, surface_inputs,
                     unreal.CustomMaterialOutputType.CMOT_FLOAT3, -500, 450)
    mark = mask(grid, "a", -250, 450)
    link(mark, "", surface, "Mark")
    link(normal, "", surface, "Nrm")
    link(camera, "", surface, "CamVec")
    link(params["Roughness"], "", surface, "BaseRoughness")
    link(params["ClearCoat"], "", surface, "CoatAmount")
    link(params["ClearCoatRoughness"], "", surface, "CoatRoughness")
    link(params["ClearCoatFresnelFade"], "", surface, "CoatFade")

    # MP_CUSTOM_DATA0/1 (clear coat, clear coat roughness) are not exposed to Python, so the outputs go through
    # a Make Material Attributes node, which does expose named ClearCoat / ClearCoatRoughness pins.
    attributes = node(unreal.MaterialExpressionMakeMaterialAttributes, 200, 300)
    link(mask(grid, "rgb", -250, 0), "", attributes, "BaseColor")
    link(mask(surface, "r", -250, 600), "", attributes, "Roughness")
    link(mask(surface, "g", -250, 720), "", attributes, "ClearCoat")
    link(mask(surface, "b", -250, 840), "", attributes, "ClearCoatRoughness")
    material.set_editor_property("use_material_attributes", True)
    if not lib.connect_material_property(attributes, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES):
        fail("Could not connect Make Material Attributes to the material output")

    lib.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material):
        fail("Could not save " + MATERIAL)
    return material


def verify():
    lib = unreal.MaterialEditingLibrary
    texture = unreal.EditorAssetLibrary.load_asset(TEXTURE)
    material = unreal.EditorAssetLibrary.load_asset(MATERIAL)
    if not texture or not material:
        fail("Could not load back the DevGrid assets")
    problems = []
    if texture.get_editor_property("srgb"):
        problems.append("texture sRGB is on")
    if texture.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_MASKS:
        problems.append("texture compression is not Masks")
    if material.get_editor_property("shading_model") != enum_value(unreal.MaterialShadingModel, "MSM_CLEAR_COAT"):
        problems.append("shading model is not Clear Coat")
    names = sorted(str(n) for n in list(lib.get_scalar_parameter_names(material)) + list(lib.get_vector_parameter_names(material)))
    missing = [name for name, _, _ in PARAMETERS if name not in names]
    if missing:
        problems.append("missing parameters: " + ", ".join(missing))
    refs = [e for e in lib.get_material_expressions(material)
            if isinstance(e, unreal.MaterialExpressionTextureObject) and e.get_editor_property("texture") == texture]
    if not refs:
        problems.append("material does not reference the markup texture")
    cmdline = unreal.SystemLibrary.get_command_line()
    compiled = "AllowCommandletRendering" in cmdline and "NullRHI" not in cmdline
    stats = lib.get_statistics(material)  # blocks until the shader map is built when rendering is allowed
    unreal.log("DEVGRID stats: PS instr {} VS instr {} samplers {} PS tex samples {} shader types {}".format(
        stats.num_pixel_shader_instructions, stats.num_vertex_shader_instructions,
        stats.num_samplers, stats.num_pixel_texture_samples, lib.get_num_shader_types(material)))
    if compiled and stats.num_pixel_shader_instructions == 0:
        problems.append("shader compile produced no pixel shader (see ShaderCompilers errors above)")
    elif not compiled:
        unreal.log_warning("DEVGRID shaders were NOT compiled in this run; add -AllowCommandletRendering (and no -NullRHI)")
    if problems:
        fail("; ".join(problems))
    unreal.log("DEVGRID OK {} + {} parameters: {}".format(MATERIAL, TEXTURE, ", ".join(names)))


def main():
    texture = import_texture()
    build_material(texture)
    verify()


try:
    main()
except Exception as exc:
    unreal.log_error("DEVGRID FAILED: {}".format(exc))
    sys.exit(1)
