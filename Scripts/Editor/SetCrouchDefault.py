"""Explicit one-time authored default update; preserves every other context row."""
import unreal

context = unreal.load_asset("/Game/Input/MappingContexts/IMC_OnFoot")
if not context:
    raise RuntimeError("Create the input foundation before updating its crouch default.")
data = context.get_editor_property("default_key_mappings")
mappings = list(data.get_editor_property("mappings"))
matches = 0
for mapping in mappings:
    settings = mapping.get_editor_property("player_mappable_key_settings")
    if settings and str(settings.get_editor_property("name")) == "key.sneak":
        key = unreal.Key()
        key.set_editor_property("key_name", "C")
        mapping.set_editor_property("key", key)
        matches += 1
if matches != 1:
    raise RuntimeError("Expected exactly one authored crouch mapping, found " + str(matches))
data.set_editor_property("mappings", mappings)
context.set_editor_property("default_key_mappings", data)
if not unreal.EditorAssetLibrary.save_loaded_asset(context):
    raise RuntimeError("Crouch default could not be saved.")
unreal.log("Crouch authored default is C; all other mappings preserved.")
