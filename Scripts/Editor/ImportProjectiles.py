"""Import the projectiles (survival T2 19c: the arrow) from Reference/minecraft-entities-26.3 under /Game/Entities
(EntityRoot):

- T_Arrow: entity/projectiles/arrow.png (32 x 32), sRGB, nearest, no mips (as the mobs' textures).
- DA_Projectiles (UFoundationMobAssets, models only): the arrow's arrow#main layer (parts and faces from
  facts/entity_models.json, as MobImportData.py reads the mobs' layers) and the mobs' M_Mob material (its Skin parameter
  takes the texture). The part asset has no scale, so the scales are baked in: the root part's (the arrow's 0.9) into
  every pivot and corner, a child part's own (the arrow's back, 0.8) into its corners. Scales must be uniform. DA_Mobs and its import are not touched.

The reference is hash-checked first. Every asset is rebuilt on each run (they are generated from the reference alone):

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\ImportProjectiles.py" -unattended -nop4 -nosplash
"""
import json
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import MobImportData as Data  # noqa: E402
import ProjectPaths  # noqa: E402

assets = unreal.EditorAssetLibrary
REGISTRY = ProjectPaths.entity_asset("DA_Projectiles")
MATERIAL = ProjectPaths.entity_asset("M_Mob")
# (id, layer, texture under textures/, texture asset)
PROJECTILES = [("arrow", "arrow#main", "entity/projectiles/arrow.png", "T_Arrow")]


def fail(message):
    raise RuntimeError("PROJECTILE_IMPORT FAIL: " + message)


def enum_of(type_name, names):
    enum = getattr(unreal, type_name)
    for name in names:
        if hasattr(enum, name):
            return getattr(enum, name)
    fail("%s has none of %s" % (type_name, names))


def models():
    """Each projectile's layer, in MobImportData.models()' shape."""
    facts = json.loads((Data.REF / "facts/entity_models.json").read_text(encoding="utf-8"))
    sizes = {t["name"]: (t["width"], t["height"])
             for t in json.loads((Data.REF / "textures.json").read_text(encoding="utf-8"))["textures"]}
    out = []
    for name, layer_name, texture, asset in PROJECTILES:
        layer = facts[layer_name]
        roots = [part for part in layer["parts"] if part["path"] == ""]
        if len(roots) != 1 or roots[0]["pivot"] != [0.0, 0.0, 0.0] or roots[0]["rot"] != [0.0, 0.0, 0.0] or \
                any(len(set(part["scale"])) != 1 or "/" in part["path"] for part in layer["parts"]):
            fail(layer_name + ": only an unmoved root and its direct children, uniformly scaled, are handled")
        root = roots[0]["scale"][0]
        parts = []
        for part in layer["parts"]:
            own = root if part["path"] == "" else root * part["scale"][0]
            faces = [{"normal": face["n"], "positions": [[c * own for c in v[:3]] for v in face["v"]],
                      "uvs": [v[3:5] for v in face["v"]]}
                     for cube in layer["cubes"] if cube["path"].strip("/") == part["path"] for face in cube["faces"]]
            pivot = part["pivot"] if part["path"] == "" else [c * root for c in part["pivot"]]
            parts.append({"path": part["path"], "pivot": pivot, "rotation": part["rot"], "faces": faces})
        out.append({"id": name, "layer": layer_name, "texture": texture, "texture_asset": asset,
                    "texture_size": sizes[texture], "parts": parts})
    return out


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
    parts = [struct(unreal.FoundationMobPartAsset, {
        "path": part["path"], "pivot": vec3(part["pivot"]), "rotation": vec3(part["rotation"]),
        "faces": [struct(unreal.FoundationMobFaceAsset, {
            "normal": vec3(face["normal"]), "positions": [vec3(p) for p in face["positions"]],
            "uvs": [vec2(uv) for uv in face["uvs"]]}) for face in part["faces"]]}) for part in model["parts"]]
    return struct(unreal.FoundationMobModelAsset, {
        "id": model["id"], "layer": model["layer"], "texture": texture,
        "texture_size": unreal.IntPoint(*model["texture_size"]), "parts": parts})


def main():
    digest = Data.verify_hashes()
    material = assets.load_asset(MATERIAL)
    if not material:
        fail(MATERIAL + " is missing: run ImportMobs.py first")
    projectiles = models()
    textures = {m["layer"]: import_texture(m["texture_asset"], m["texture"], m["texture_size"]) for m in projectiles}
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
    registry.set_editor_property("models", [model_struct(m, textures[m["layer"]]) for m in projectiles])
    registry.set_editor_property("material", material)
    if not assets.save_loaded_asset(registry, False):
        fail("could not save " + REGISTRY)
    unreal.log("PROJECTILE_IMPORT PASS: reference %s; %d models (%d faces): %s"
               % (digest[:12], len(projectiles), sum(len(p["faces"]) for m in projectiles for p in m["parts"]),
                  ", ".join(m["layer"] for m in projectiles)))


main()
