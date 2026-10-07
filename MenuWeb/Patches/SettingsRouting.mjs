// Routes every settings row to its single native owner: MenuVideo, MenuInput or MenuPreferences.
// These two spans are shared by all three stores, so this patch owns them.
export const name = 'SettingsRouting';

export function apply(patch) {
  patch.line('function setSetting(',
    'function setSetting(id,value){if(liveVideoSettings.has(id))return MenuVideo.set(id,value);' +
    'if(liveInputPolicies.has(id))return MenuInput.setPolicy(id,value);return MenuPreferences.set(id,value)}');
  patch.text('const enabled=activeSetting(id),d=D[id]',
    'const enabled=activeSetting(id)&&(!liveVideoSettings.has(id)||MenuVideo.enabled(id))' +
    '&&(!liveInputPolicies.has(id)||MenuInput.enabled(id))&&(!preferenceSettings.has(id)||MenuPreferences.enabled(id)),d=D[id]');
}
