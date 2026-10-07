import vm from 'node:vm';

// Both the native validator and browser use this generated, packaged contract.
export function preferenceSchema(data, lines) {
  const excluded = new Set(['fullscreen','framerateLimit','enableVsync','guiScale',
    'menuBackgroundBlurriness','toggleSprint','toggleCrouch',
    'preferredGraphicsBackend','soundDevice','rotateWithMinecart']);
  const definitions = structuredClone(data.definitions);
  for (const part of ['cape','jacket','left_sleeve','right_sleeve','left_pants_leg','right_pants_leg','hat'])
    definitions['skin_' + part] = {kind:'cycle',values:[false,true],default:true};
  const settings = Object.fromEntries(Object.entries(definitions).filter(([id]) => !excluded.has(id))
    .map(([id,d]) => [id, Object.fromEntries(['default','values','min','max','step'].filter(key => key in d).map(key => [key,d[key]]))]));
  const presetLine = lines.find(line => line.startsWith('function preset('));
  const qualityLine = lines.find(line => line.startsWith('const quality='));
  if (!presetLine || !qualityLine || Object.keys(settings).length !== 101) throw Error('Preference schema coverage changed; review its owners.');
  const context = vm.createContext({state:{settings:{}}});
  vm.runInContext(presetLine + '\n' + qualityLine, context);
  const presets = {};
  for (const name of ['Fast','Fancy','Fabulous!']) {
    context.state.settings = {};
    vm.runInContext('preset(' + JSON.stringify(name) + ')', context);
    presets[name] = Object.fromEntries(Object.entries(context.state.settings).filter(([id]) => id in settings));
  }
  const quality = [...vm.runInContext('quality', context)].filter(id => id in settings);
  return {version:1,settings,presets,quality,languages:Object.keys(data.languages)};
}
