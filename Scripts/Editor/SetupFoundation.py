"""Supported Unreal editor setup. Extends native assets; no binary fabrication.
Run only after compiling the Foundation classes. Does not change Super Flat.
"""
import json
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # Unreal does not add the script folder.
import ProjectPaths  # noqa: E402
from pathlib import Path  # noqa: E402

root = Path(unreal.Paths.project_dir())
catalog = json.loads((root / 'Docs/Foundation/controls.json').read_text(encoding='utf-8'))
tools = unreal.AssetToolsHelpers.get_asset_tools()
def key(name):
    k = unreal.Key()
    k.set_editor_property('key_name', name)
    return k
names = {'left':'LeftMouseButton','right':'RightMouseButton','middle':'MiddleMouseButton',
    'left.shift':'LeftShift','left.control':'LeftControl','space':'SpaceBar','slash':'Slash','tab':'Tab',
    '1':'One','2':'Two','3':'Three','4':'Four','5':'Five','6':'Six','7':'Seven','8':'Eight','9':'Nine'}

# Restore the project's Shift sprint / C crouch defaults if the Foundation
# setup previously replaced them with vanilla keys. Preserve other authored keys.
# Saved player-mappable keys remain in the native user profile, not in these assets.
old = unreal.load_asset(ProjectPaths.input_asset('OnFootContext'))
if old is None:
    raise RuntimeError('Run CreateInputAssets.py before SetupFoundation.py')
data = old.get_editor_property('default_key_mappings')
mappings = list(data.get_editor_property('mappings'))
for index, m in enumerate(mappings):
    settings = m.get_editor_property('player_mappable_key_settings')
    if settings:
        ident = str(settings.get_editor_property('name'))
        oldkey = str(m.get_editor_property('key').get_editor_property('key_name'))
        if ident == 'key.sprint' and oldkey == 'LeftControl': m.set_editor_property('key', key('LeftShift'))
        if ident == 'key.sneak' and oldkey == 'LeftShift': m.set_editor_property('key', key('C'))
        mappings[index] = m
        unreal.log(ident + ' factory key=' + str(m.get_editor_property('key').get_editor_property('key_name')))
data.set_editor_property('mappings', mappings)
old.set_editor_property('default_key_mappings', data)
if not unreal.EditorAssetLibrary.save_loaded_asset(old, only_if_is_dirty=False): raise RuntimeError('Could not save default-key migration')
look = unreal.load_asset(ProjectPaths.input_asset('LookAction'))
look.set_editor_property('accumulation_behavior', unreal.InputActionAccumulationBehavior.CUMULATIVE)
if not unreal.EditorAssetLibrary.save_loaded_asset(look, only_if_is_dirty=False): raise RuntimeError('Could not save mouse accumulation')
path = ProjectPaths.input_asset('FoundationActionsContext')
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    folder, name = ProjectPaths.split(path)
    ctx = tools.create_asset(name,folder,unreal.InputMappingContext,unreal.InputMappingContext_Factory())
    unreal.EditorAssetLibrary.make_directory('/Game/Input/Foundation')
    rows = []
    for row in catalog['rows']:
        if row['status'] == 'Deferred' or row['category'] == 'movement': continue
        ident = row['id']
        action = tools.create_asset('IA_'+ident.replace('.','_'),'/Game/Input/Foundation',unreal.InputAction,unreal.InputAction_Factory())
        action.set_editor_property('value_type',unreal.InputActionValueType.BOOLEAN)
        action.set_editor_property('action_description',row['label'])
        raw = row['defaultPhysical'].replace('key.keyboard.','').replace('key.mouse.','')
        native = names.get(raw,raw.upper() if len(raw)==1 else raw.title())
        m = unreal.EnhancedActionKeyMapping(action=action,key=key(native))
        settings = unreal.PlayerMappableKeySettings(outer=ctx)
        settings.set_editor_property('name',ident)
        settings.set_editor_property('display_name',row['label'])
        settings.set_editor_property('display_category',row['category'])
        m.set_editor_property('setting_behavior',unreal.PlayerMappableKeySettingBehaviors.OVERRIDE_SETTINGS)
        m.set_editor_property('player_mappable_key_settings',settings)
        rows.append(m)
        unreal.EditorAssetLibrary.save_loaded_asset(action)
    data = unreal.InputMappingContextMappingData()
    data.set_editor_property('mappings',rows)
    ctx.set_editor_property('default_key_mappings',data)
    unreal.EditorAssetLibrary.save_loaded_asset(ctx)

map_path = ProjectPaths.world_map('foundation')
if not unreal.EditorAssetLibrary.does_asset_exist(map_path):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.new_level(map_path): raise RuntimeError('Failed to create foundation map')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode',unreal.FoundationGameMode)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors.spawn_actor_from_class(unreal.PlayerStart,unreal.Vector(0,0,0))
    light = actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,1000),unreal.Rotator(-45,-30,0))
    light.light_component.set_editor_property('intensity',3)
    sky = actors.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,800))
    sky.light_component.set_editor_property('intensity',1)
    actors.spawn_actor_from_class(unreal.SkyAtmosphere,unreal.Vector(0,0,0))
    if not levels.save_current_level(): raise RuntimeError('Failed to save foundation map')
unreal.log('Foundation native assets and map ready; existing maps preserved.')
