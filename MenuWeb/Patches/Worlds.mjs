// Singleplayer world list. Playable worlds come from the native catalog (MenuApplication
// merges them into state.worlds with builtIn:true); the two fictional entries stay design
// references. Only MenuApplication.openWorld can load a world, and only by catalog ID.
export const name = 'Worlds';

export function apply(patch) {
  // Built-in worlds cannot be edited, deleted or re-created like mock saves.
  patch.edit('editworld(){', 'if(!w){', 'if(!w||w.builtIn){');
  patch.edit('function deleteWorld(){', 'if(!w)return;', 'if(!w||w.builtIn)return;');

  // Give each action its real availability; MenuWorldsRuntime.js refreshes the buttons.
  patch.edit('worlds(){', 'worlds(){title(', 'worlds(){if(!chosenWorld)chosenWorld=MenuApplication.defaultWorld();title(');
  patch.edit('worlds(){', "!chosenWorld).dataset.worldAction='select'", "!chosenWorld).dataset.worldAction='play'");
  patch.edit('worlds(){', "if(i<3)b.dataset.worldAction='select'})", "if(i<3)b.dataset.worldAction='reference'});refreshWorldActions()");
  patch.edit('function drawWorlds(){', "el('span','world-play',r);", "if(w.builtIn)el('span','world-play',r);");
  patch.edit('function drawWorlds(){', "w.folder+' ('+w.date+')'", "(w.summary??'Design reference - does not load')");
  patch.edit('function drawWorlds(){', "w.mode+', '+tr('selectWorld.version')+' '+w.version", "(w.status??w.mode+' - menu design only')");
  patch.edit('function drawWorlds(){', "screen.querySelectorAll('[data-world-action]').forEach(e=>e.disabled=false)", 'refreshWorldActions()');
  patch.edit('function drawWorlds(){', "screen.querySelectorAll('[data-world-action]').forEach(e=>e.disabled=!chosenWorld)", 'refreshWorldActions()');
  patch.line('function loadWorld(){', '// Defined by MenuWorldsRuntime.js.');
}
