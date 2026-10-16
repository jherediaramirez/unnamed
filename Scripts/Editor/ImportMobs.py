"""Import the mobs (survival T6 20a) from Reference/minecraft-entities-26.3 under /Game/Entities (EntityRoot):

- T_Mob_<id>: each mob's default texture (MobImportData.MOBS), each extra layer's (MobImportData.LAYERS) and each
  variant's (MobImportData.VARIANTS, 20e1), a Java 26.3 stand-in (Docs/StandIns.md), sRGB,
  uncompressed, nearest, one mip.
- M_Mob: the mobs' cutout material. Its Skin parameter takes the texture (FFoundationMobView makes one instance per
  mob layer); texels below alpha 0.1 are cut, as the entity cutout does; per-instance custom data 0 is the red overlay
  [hurt.overlay]: the skin times OVERLAY_TINT (OVERLAY_MODE "multiply", the look call), or mixed toward red by
  OVERLAY_RED ("mix"); provisional. Custom data 1-3 (20e1) hold one minus a tint that multiplies the skin's colour (the
  sheep's wool), so a user that sets none (the arrows, the shield, other mobs) reads 0 and draws untinted.
- DA_Mobs (UFoundationMobAssets): the models (parts and faces from facts/entity_models.json), the entity loot
  tables flattened by MobImportData.py, and every biome's creature and monster spawn lists and chunk-generation chance (read from
  Reference/minecraft-worldgen-26.3, hash-checked, never written).

The reference is hash-checked first. Every asset is rebuilt on each run (they are generated from the reference alone):

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\ImportMobs.py" -unattended -nop4 -nosplash
Add -AllowCommandletRendering to prove the shader compiles.
"""
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import MobImportData as Data  # noqa: E402
import ProjectPaths  # noqa: E402

lib = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary
MATERIAL = ProjectPaths.entity_asset("M_Mob")
REGISTRY = ProjectPaths.entity_asset("DA_Mobs")
# Provisional (value report `hurt.overlay`: a red overlay, its blend not a universal tint). "multiply" (the SM's look
# call, 2026-10-15): the hurt texture is the skin times OVERLAY_TINT; "mix": the skin mixed toward red by OVERLAY_RED
# (20a-20b1).
OVERLAY_MODE = "multiply"
OVERLAY_TINT = (1.0, 0.55, 0.55)
OVERLAY_RED = 0.3


def fail(message):
    raise RuntimeError("MOB_IMPORT FAIL: " + message)


def enum_of(type_name, names):
    enum = getattr(unreal, type_name)
    for name in names:
        if hasattr(enum, name):
            return getattr(enum, name)
    fail("%s has none of %s" % (type_name, names))


def import_texture(asset, relative, size):
    png = Data.REF / Data.TEXTURES / relative
    path = ProjectPaths.entity_asset(asset)
    folder, name = ProjectPaths.split(path)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(png))
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = assets.load_asset(path)
    if not texture:
        fail("could not import " + path)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("compression_settings", enum_of("TextureCompressionSettings",
                                                                ["TC_EDITOR_ICON", "TC_EditorIcon"]))
    texture.set_editor_property("filter", enum_of("TextureFilter", ["TF_NEAREST", "TF_Nearest"]))
    texture.set_editor_property("mip_gen_settings", enum_of("TextureMipGenSettings",
                                                            ["TMGS_NO_MIPMAPS", "TMGS_NoMipmaps"]))
    texture.set_editor_property("lod_group", enum_of("TextureGroup", ["TEXTUREGROUP_PIXELS2D", "TEXTUREGROUP_Pixels2D"]))
    if not assets.save_loaded_asset(texture):
        fail("could not save " + path)
    if (texture.blueprint_get_size_x(), texture.blueprint_get_size_y()) != tuple(size):
        fail("%s is not %d x %d" % (path, size[0], size[1]))
    return texture


def build_material(default_texture):
    if assets.does_asset_exist(MATERIAL):
        m = assets.load_asset(MATERIAL)
        for _ in range(20):  # delete_all_material_expressions skips entries while it erases
            if lib.get_num_material_expressions(m) == 0:
                break
            lib.delete_all_material_expressions(m)
    else:
        folder, name = ProjectPaths.split(MATERIAL)
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material,
                                                                    unreal.MaterialFactoryNew())
        if not m:
            fail("could not create " + MATERIAL)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("opacity_mask_clip_value", 0.1)
    m.set_editor_property("used_with_instanced_static_meshes", True)

    def node(cls, x, y):
        return lib.create_material_expression(m, cls, x, y)

    def link(source, target, input_name, output=""):
        if not lib.connect_material_expressions(source, output, target, input_name):
            fail("could not connect to " + input_name)

    def prop(source, material_property, output=""):
        if not lib.connect_material_property(source, output, material_property):
            fail("could not connect %s" % material_property)

    skin = node(unreal.MaterialExpressionTextureSampleParameter2D, -800, 0)
    skin.set_editor_property("parameter_name", "Skin")
    skin.set_editor_property("texture", default_texture)
    overlay = node(unreal.MaterialExpressionPerInstanceCustomData, -800, 300)
    overlay.set_editor_property("data_index", 0)
    # 20e1: the per-instance tint, stored as one minus it in custom data 1-3 (0 where unset: untinted), times the skin.
    tint_in = node(unreal.MaterialExpressionPerInstanceCustomData3Vector, -1100, -200)
    tint_in.set_editor_property("data_index", 1)
    tint = node(unreal.MaterialExpressionOneMinus, -900, -200)
    link(tint_in, tint, "")
    tinted = node(unreal.MaterialExpressionMultiply, -650, -100)
    link(skin, tinted, "A", "RGB")
    link(tint, tinted, "B")
    mix = node(unreal.MaterialExpressionLinearInterpolate, -250, 0)
    link(tinted, mix, "A")
    if OVERLAY_MODE == "multiply":
        tint = node(unreal.MaterialExpressionConstant3Vector, -650, 150)
        tint.set_editor_property("constant", unreal.LinearColor(*OVERLAY_TINT, 1))
        hurt = node(unreal.MaterialExpressionMultiply, -500, 150)
        link(tinted, hurt, "A")
        link(tint, hurt, "B")
        link(hurt, mix, "B")
        link(overlay, mix, "Alpha")
    else:
        share = node(unreal.MaterialExpressionMultiply, -500, 300)
        share.set_editor_property("const_b", OVERLAY_RED)
        link(overlay, share, "A")
        red = node(unreal.MaterialExpressionConstant3Vector, -500, 150)
        red.set_editor_property("constant", unreal.LinearColor(1, 0, 0, 1))
        link(red, mix, "B")
        link(share, mix, "Alpha")
    prop(mix, unreal.MaterialProperty.MP_BASE_COLOR)
    prop(skin, unreal.MaterialProperty.MP_OPACITY_MASK, "A")
    for value, material_property, y in ((1.0, unreal.MaterialProperty.MP_ROUGHNESS, 450),
                                         (0.1, unreal.MaterialProperty.MP_SPECULAR, 520)):
        constant = node(unreal.MaterialExpressionConstant, -250, y)
        constant.set_editor_property("r", value)
        prop(constant, material_property)
    lib.recompile_material(m)
    if not assets.save_loaded_asset(m):
        fail("could not save " + MATERIAL)
    return m


def struct(struct_type, values):
    entry = struct_type()
    for key, value in values.items():
        entry.set_editor_property(key, value)
    return entry


def vec3(values):
    return unreal.Vector(*[float(v) for v in values])


def vec2(values):
    return unreal.Vector2D(float(values[0]), float(values[1]))


def model_struct(model, texture):
    parts = []
    for part in model["parts"]:
        faces = [struct(unreal.FoundationMobFaceAsset, {
            "normal": vec3(face["normal"]), "positions": [vec3(p) for p in face["positions"]],
            "uvs": [vec2(uv) for uv in face["uvs"]]}) for face in part["faces"]]
        parts.append(struct(unreal.FoundationMobPartAsset, {
            "path": part["path"], "pivot": vec3(part["pivot"]), "rotation": vec3(part["rotation"]), "faces": faces}))
    return struct(unreal.FoundationMobModelAsset, {
        "id": model["id"], "layer": model["layer"], "texture": texture,
        "texture_size": unreal.IntPoint(*model["texture_size"]), "parts": parts})


def loot_struct(table):
    def modifiers(values):
        return [struct(unreal.FoundationLootModifierAsset, m) for m in values]
    pools = [struct(unreal.FoundationLootPoolAsset, {
        "rolls": pool["rolls"], "tests": pool["tests"], "modifiers": modifiers(pool["modifiers"]),
        "candidates": [struct(unreal.FoundationLootCandidateAsset, dict(c, modifiers=modifiers(c["modifiers"])))
                       for c in pool["candidates"]]}) for pool in table["pools"]]
    return struct(unreal.FoundationLootTableAsset, {
        "block": table["block"], "pools": pools, "modifiers": modifiers(table["modifiers"]),
        "conditions": [struct(unreal.FoundationLootConditionAsset, n) for n in table["conditions"]]})


def spawn_struct(biome):
    return struct(unreal.FoundationBiomeSpawnAsset, {
        "biome": biome["biome"], "chance": biome["chance"],
        "creatures": [struct(unreal.FoundationMobSpawnAsset, c) for c in biome["creatures"]],
        "monsters": [struct(unreal.FoundationMobSpawnAsset, c) for c in biome["monsters"]]})


def main():
    digest = Data.verify_hashes()
    models = Data.models()
    textures = {m["texture_asset"]: import_texture(m["texture_asset"], m["texture"], m["texture_size"]) for m in models}
    material = build_material(textures["T_Mob_Cow"])
    tables, left_out = Data.loot_tables()
    biomes, worldgen = Data.biome_spawns()
    if assets.does_asset_exist(REGISTRY):
        registry = assets.load_asset(REGISTRY)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.FoundationMobAssets)
        folder, name = ProjectPaths.split(REGISTRY)
        registry = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.FoundationMobAssets,
                                                                           factory)
    if not registry:
        fail("could not create " + REGISTRY)
    registry.set_editor_property("source_sha256", digest)
    registry.set_editor_property("models", [model_struct(m, textures[m["texture_asset"]]) for m in models])
    registry.set_editor_property("material", material)
    registry.set_editor_property("loot_tables", [loot_struct(t) for t in tables])
    registry.set_editor_property("worldgen_sha256", worldgen)
    registry.set_editor_property("biome_spawns", [spawn_struct(b) for b in biomes])
    if not assets.save_loaded_asset(registry, False):
        fail("could not save " + REGISTRY)
    cmdline = unreal.SystemLibrary.get_command_line()
    compiled = "AllowCommandletRendering" in cmdline and "NullRHI" not in cmdline
    stats = lib.get_statistics(material)
    if compiled and stats.num_pixel_shader_instructions == 0:
        fail("M_Mob: the shader did not compile (see ShaderCompilers errors above)")
    for mob, why in left_out.items():
        unreal.log_warning("MOB_IMPORT: entities/%s left out of DA_Mobs: %s" % (mob, why))
    unreal.log("MOB_IMPORT PASS: reference %s; %d models (%d faces), %d loot tables, %d left out; worldgen %s, %d "
               "biome spawn lists (%d creature, %d monster entries); M_Mob overlay %s, PS instr %d%s"
               % (digest[:12], len(models), sum(len(p["faces"]) for m in models for p in m["parts"]), len(tables),
                  len(left_out), worldgen[:12], len(biomes), sum(len(b["creatures"]) for b in biomes),
                  sum(len(b["monsters"]) for b in biomes), OVERLAY_MODE,
                  stats.num_pixel_shader_instructions,
                  "" if compiled else " (graphs only; add -AllowCommandletRendering to compile)"))


main()
