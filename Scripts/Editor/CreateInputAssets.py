"""Create the initial native Enhanced Input assets once; never overwrite existing assets."""
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # Unreal does not add the script folder.
import ProjectPaths  # noqa: E402

tools = unreal.AssetToolsHelpers.get_asset_tools()
def input_key(name):
    key = unreal.Key()
    key.set_editor_property("key_name", name)
    return key
context_path = ProjectPaths.input_asset("OnFootContext")
if unreal.EditorAssetLibrary.does_asset_exist(context_path):
    unreal.log("Input assets already exist; preserving authored mappings.")
else:
    context_folder, context_name = ProjectPaths.split(context_path)
    unreal.EditorAssetLibrary.make_directory(context_folder)
    actions = {}
    for name in ["Move", "Look", "Sprint", "Crouch", "Jump"]:
        path = ProjectPaths.input_asset(name + "Action")
        folder, asset_name = ProjectPaths.split(path)
        unreal.EditorAssetLibrary.make_directory(folder)
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            actions[name] = unreal.load_asset(path)
        else:
            action = tools.create_asset(asset_name, folder, unreal.InputAction, unreal.InputAction_Factory())
            action.set_editor_property("value_type", unreal.InputActionValueType.AXIS2D if name in ["Move", "Look"] else unreal.InputActionValueType.BOOLEAN)
            if name in ["Move", "Look"]:
                action.set_editor_property("accumulation_behavior", unreal.InputActionAccumulationBehavior.CUMULATIVE)
            action.set_editor_property("action_description", "On-foot " + name.lower() + " request")
            actions[name] = action
    context = tools.create_asset(context_name, context_folder, unreal.InputMappingContext, unreal.InputMappingContext_Factory())
    rows = [
        ("key.forward", "Walk Forward", "Move", "W", True, False),
        ("key.back", "Walk Backward", "Move", "S", True, True),
        ("key.left", "Strafe Left", "Move", "A", False, True),
        ("key.right", "Strafe Right", "Move", "D", False, False),
        ("key.sprint", "Sprint", "Sprint", "LeftShift", False, False),
        ("key.sneak", "Crouch", "Crouch", "C", False, False),
        ("key.jump", "Jump", "Jump", "SpaceBar", False, False),
    ]
    mappings = []
    for mapping_name, label, action_name, key, swizzle, negate in rows:
        mapping = unreal.EnhancedActionKeyMapping(action=actions[action_name], key=input_key(key))
        modifiers = []
        if negate:
            modifiers.append(unreal.InputModifierNegate(outer=context))
        if swizzle:
            modifier = unreal.InputModifierSwizzleAxis(outer=context)
            modifier.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
            modifiers.append(modifier)
        mapping.set_editor_property("modifiers", modifiers)
        settings = unreal.PlayerMappableKeySettings(outer=context)
        settings.set_editor_property("name", mapping_name)
        settings.set_editor_property("display_name", label)
        settings.set_editor_property("display_category", "Movement")
        mapping.set_editor_property("setting_behavior", unreal.PlayerMappableKeySettingBehaviors.OVERRIDE_SETTINGS)
        mapping.set_editor_property("player_mappable_key_settings", settings)
        mappings.append(mapping)
    for key, swizzle, negate in [("MouseX", False, False), ("MouseY", True, True)]:
        mapping = unreal.EnhancedActionKeyMapping(action=actions["Look"], key=input_key(key))
        modifiers = []
        if negate:
            modifiers.append(unreal.InputModifierNegate(outer=context))
        if swizzle:
            modifier = unreal.InputModifierSwizzleAxis(outer=context)
            modifier.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
            modifiers.append(modifier)
        mapping.set_editor_property("modifiers", modifiers)
        mappings.append(mapping)
    data = unreal.InputMappingContextMappingData()
    data.set_editor_property("mappings", mappings)
    context.set_editor_property("default_key_mappings", data)
    for action in actions.values():
        unreal.EditorAssetLibrary.save_loaded_asset(action)
    unreal.EditorAssetLibrary.save_loaded_asset(context)
    unreal.log("Created 5 actions, 7 player-mappable movement bindings and mouse look in IMC_OnFoot.")
