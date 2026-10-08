"""Additive, idempotent import of the pinned 194 PNGs. Existing assets are verified, never reimported.

Run through BuildFoundation.ps1 or UnrealEditor-Cmd -run=pythonscript.
MCMeta animation/slicing is recorded for future consumers; deferred overlays are not presented.
"""
import json
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths
from HudImportData import build

root = Path(unreal.Paths.project_dir()).resolve()
data = build(root / 'Reference/minecraft-hud-26.3', ProjectPaths.hud_texture)
canonical = json.dumps(data, sort_keys=True, separators=(',', ':'))
registry_path = ProjectPaths.hud_registry()
existing = unreal.EditorAssetLibrary.load_asset(registry_path) if unreal.EditorAssetLibrary.does_asset_exist(registry_path) else None
if existing and existing.get_editor_property('registry_json') != canonical:
    raise RuntimeError('HUD registry differs. Preserve it and explicitly migrate; additive import will not overwrite it.')
tools = unreal.AssetToolsHelpers.get_asset_tools()
sprites, created = [], 0
for record in data['sprites']:
    package = record['asset']
    found = unreal.EditorAssetLibrary.does_asset_exist(package)
    if found:
        texture = unreal.EditorAssetLibrary.load_asset(package)
    else:
        folder, name = ProjectPaths.split(package)
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', str(root / 'Reference/minecraft-hud-26.3' / record['source_path']))
        task.set_editor_property('destination_path', folder)
        task.set_editor_property('destination_name', name)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', False)
        task.set_editor_property('save', False)
        task.set_editor_property('factory', unreal.TextureFactory())
        tools.import_asset_tasks([task])
        texture = unreal.EditorAssetLibrary.load_asset(package)
        if not texture:
            raise RuntimeError('Import failed: ' + package)
        created += 1
    blur = record['import_preset'] == 'blurred_overlay'
    tiled = record['import_preset'] == 'tiled_animation' or record['id'] == 'misc/enchanted_glint_item'
    settings = dict(compression_settings=unreal.TextureCompressionSettings.TC_EDITOR_ICON,
                    mip_gen_settings=unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
                    filter=unreal.TextureFilter.TF_BILINEAR if blur else unreal.TextureFilter.TF_NEAREST,
                    lod_group=unreal.TextureGroup.TEXTUREGROUP_UI,
                    srgb=True, compression_no_alpha=False, never_stream=True,
                    address_x=unreal.TextureAddress.TA_WRAP if tiled else unreal.TextureAddress.TA_CLAMP,
                    address_y=unreal.TextureAddress.TA_WRAP if tiled else unreal.TextureAddress.TA_CLAMP,
                    power_of_two_mode=unreal.TexturePowerOfTwoSetting.NONE)
    for key, value in settings.items():
        if found:
            if texture.get_editor_property(key) != value:
                raise RuntimeError('Existing HUD texture setting differs: ' + package + ':' + key)
        else:
            texture.set_editor_property(key, value)
    if [texture.blueprint_get_size_x(), texture.blueprint_get_size_y()] != record['native_size']:
        raise RuntimeError('Native dimensions differ: ' + package)
    if not found and not unreal.EditorAssetLibrary.save_loaded_asset(texture):
        raise RuntimeError('Save failed: ' + package)
    entry = unreal.HudSpriteAsset()
    for key in ('id', 'source_path', 'source_sha256', 'frames', 'frame_ticks', 'role', 'import_preset'):
        entry.set_editor_property(key, record[key])
    entry.set_editor_property('texture', texture)
    entry.set_editor_property('native_size', unreal.IntPoint(*record['native_size']))
    entry.set_editor_property('frame_size', unreal.IntPoint(*record['frame_size']))
    entry.set_editor_property('metadata_json', json.dumps(record['metadata'], sort_keys=True, separators=(',', ':')))
    sprites.append(entry)
if not existing:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.HudAssets)
    folder, name = ProjectPaths.split(registry_path)
    registry = tools.create_asset(name, folder, unreal.HudAssets, factory)
    registry.set_editor_property('registry_json', canonical)
    registry.set_editor_property('sprites', sprites)
    glyphs = []
    for record in data['glyphs']:
        glyph = unreal.HudGlyph()
        for key in ('code', 'ink_width', 'advance'):
            glyph.set_editor_property(key, record[key])
        glyph.set_editor_property('origin', unreal.IntPoint(*record['origin']))
        glyphs.append(glyph)
    registry.set_editor_property('glyphs', glyphs)
    registry.set_editor_property('font_height', data['font_height'])
    registry.set_editor_property('font_ascent', data['font_ascent'])
    if not unreal.EditorAssetLibrary.save_loaded_asset(registry):
        raise RuntimeError('HUD registry save failed')
unreal.log('HUD_IMPORT PASS: 194 verified PNGs; created=' + str(created) + '; existing assets were not saved or reimported')
