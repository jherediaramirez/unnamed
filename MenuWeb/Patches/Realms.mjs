// Minecraft Realms is retired: no main-menu entry, renderers, styles, setting or preference.
export const name = 'Realms';

export function apply(patch) {
  patch.line('realms(){', '');
  patch.line('realmjoin(){', '');
  patch.data(data => {
    if (!Object.hasOwn(data.definitions, 'realmsNotifications')) throw new Error('Missing retired Realms preference');
    delete data.definitions.realmsNotifications;
  });

  // Remove the main-menu row and move every later row up by one (24 px).
  patch.edit('main(){', "button(tr('menu.online'),C-100,y+48,200,()=>go('realms'));", '');
  for (const [before, after] of [['C-34,y+72,', 'C-34,y+48,'], ['C-10,y+72,', 'C-10,y+48,'], ['C+14,y+72,', 'C+14,y+48,'],
    ['C-100,y+96,98,', 'C-100,y+72,98,'], ['C+2,y+96,98,', 'C+2,y+72,98,']])
    patch.edit('main(){', before, after);

  patch.text("y=heading(tr('options.online.realms.header'),c,y,18);setting('realmsNotifications',0,y,310,c);finishList(c,y+25)", 'finishList(c,y)');
  for (const rule of [
    '.realm-feature{position:absolute;width:290px;min-height:49px}',
    '.realm-feature img{position:absolute;width:80px;height:45px;object-fit:cover}',
    '.realm-feature .description{position:absolute;left:96px;top:14px;width:182px;line-height:10px;white-space:pre-wrap}',
    '.realm-feature .feature-title{position:absolute;left:96px;top:1px;width:182px;line-height:10px}',
  ]) patch.text(rule, '');
}

export function verify(output) {
  for (const pattern of [/^realms\(\)\{/m, /^realmjoin\(\)\{/m, /go\('realms'\)/, /go\('realmjoin'\)/, /realm-join/, /\.realm-feature/,
    /setting\('realmsNotifications'/])
    if (pattern.test(output)) throw new Error(`Retired Realms interface remains: ${pattern}`);
}
