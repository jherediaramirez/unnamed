"""Import the 26.3 crafting recipes (survival step 6) from Reference/minecraft-items-26.3/recipes.json into a new
standalone asset, ItemRoot/DA_Recipes (UFoundationRecipeAssets). DA_Items is not touched.

Every crafting_shaped and crafting_shapeless recipe goes in, in recipes.json's order (by id), except those whose result
carries components (suspicious stew), which the authority's stacks cannot hold yet. The authority resolves the item
names when it builds its rules: a recipe naming an item that does not exist yet waits for it.

Additive and idempotent: an existing DA_Recipes is verified (the SHA-256 of recipes.json and its recipes), never
rewritten. A changed reference refuses; move the old asset away deliberately first.

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\ImportRecipes.py" -unattended -nop4 -nosplash
"""
import hashlib
import json
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402
from ItemImportData import verify_hashes  # noqa: E402

ROOT = Path(unreal.Paths.project_dir()).resolve()
SOURCE = ROOT / "Reference/minecraft-items-26.3/recipes.json"
assets = unreal.EditorAssetLibrary


def fail(message):
    raise RuntimeError("RECIPE_IMPORT FAIL: " + message)


def load():
    raw = SOURCE.read_bytes()
    recipes = [r for r in json.loads(raw.decode("utf-8"))["recipes"] if "components" not in r["result"]]
    return hashlib.sha256(raw).hexdigest(), recipes


def view(recipe):
    """The recipe as DA_Recipes holds it, for building and for comparing."""
    cells = recipe["ingredients"] if recipe["type"] == "shapeless" else recipe["cells"]
    if any(c is not None and not c["items"] for c in cells):
        fail("%s has an ingredient that accepts no item" % recipe["id"])
    return (recipe["id"], recipe["type"] == "shapeless", recipe["width"], recipe["height"],
            tuple((c["tag"] or "", tuple(c["items"])) if c else ("", ()) for c in cells),
            recipe["result"]["item"], recipe["result"]["count"])


def held(asset):
    return (str(asset.get_editor_property("id")), asset.get_editor_property("shapeless"),
            asset.get_editor_property("width"), asset.get_editor_property("height"),
            tuple(((str(c.get_editor_property("tag")) if str(c.get_editor_property("tag")) != "None" else ""),
                   tuple(str(i) for i in c.get_editor_property("items"))) for c in asset.get_editor_property("cells")),
            str(asset.get_editor_property("result_item")), asset.get_editor_property("result_count"))


def build(recipe_view):
    rid, shapeless, width, height, cells, item, count = recipe_view
    entry = unreal.FoundationRecipeAsset()
    entry.set_editor_property("id", rid)
    entry.set_editor_property("shapeless", shapeless)
    entry.set_editor_property("width", width)
    entry.set_editor_property("height", height)
    structs = []
    for tag, items in cells:
        cell = unreal.FoundationRecipeIngredientAsset()
        if tag:
            cell.set_editor_property("tag", tag)
        cell.set_editor_property("items", list(items))
        structs.append(cell)
    entry.set_editor_property("cells", structs)
    entry.set_editor_property("result_item", item)
    entry.set_editor_property("result_count", count)
    return entry


def main():
    verify_hashes(ROOT)
    digest, recipes = load()
    views = [view(r) for r in recipes]
    path = ProjectPaths.item_asset("DA_Recipes")
    if assets.does_asset_exist(path):
        existing = assets.load_asset(path)
        if existing.get_editor_property("source_sha256") != digest:
            fail("DA_Recipes was imported from a different recipes.json; move it away and reimport")
        if [held(r) for r in existing.get_editor_property("recipes")] != views:
            fail("DA_Recipes' recipes differ from the reference; move it away and reimport")
        unreal.log("RECIPE_IMPORT PASS created=0 verified=1 recipes=%d" % len(views))
        return
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.FoundationRecipeAssets)
    folder, name = ProjectPaths.split(path)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.FoundationRecipeAssets,
                                                                    factory)
    if not asset:
        fail("could not create " + path)
    asset.set_editor_property("source_sha256", digest)
    asset.set_editor_property("recipes", [build(v) for v in views])
    if not assets.save_loaded_asset(asset, False):
        fail("save failed: " + path)
    if [held(r) for r in asset.get_editor_property("recipes")] != views:
        fail("DA_Recipes does not read back as written")
    unreal.log("RECIPE_IMPORT PASS created=1 verified=0 recipes=%d" % len(views))


main()
