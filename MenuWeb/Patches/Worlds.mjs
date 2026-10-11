// Singleplayer world list and Create New World. The list is native: MenuApplication merges the
// catalog's built-in worlds (builtIn) and Create New World's saves (saved). Create, Play, Edit,
// Delete and Re-Create reach native code only through MenuApplication, by world id.
export const name = 'Worlds';

export function apply(patch) {
  // Only saved worlds can be renamed, deleted or re-created; built-in worlds belong to the game.
  patch.edit('editworld(){', 'if(!w){', 'if(!w||!w.saved){');
  patch.edit('editworld(){',
    "const actions=[[tr('selectWorld.edit.resetIcon'),'A fictional entry has no saved world icon.'],[tr('selectWorld.edit.openFolder'),'This browser prototype has no world-save folder.'],[tr('selectWorld.edit.backup'),'A game client is needed to create world backups.'],[tr('selectWorld.edit.backupFolder'),'This prototype has no Minecraft backup folder.'],[tr('selectWorld.edit.optimize'),'World optimization requires a real Minecraft world.']]",
    "const actions=[[tr('selectWorld.edit.resetIcon'),'Worlds have no icons yet.'],[tr('selectWorld.edit.openFolder'),'Opening the world folder is not available yet.'],[tr('selectWorld.edit.backup'),'World backups are not available yet.'],[tr('selectWorld.edit.backupFolder'),'World backups are not available yet.'],[tr('selectWorld.edit.optimize'),'Optimize World is not available yet.']]");
  // Delete is for good: the confirm returns to the list, then native deletes the folder and replies with the list.
  patch.line('function deleteWorld(){', "function deleteWorld(){const w=state.worlds.find(w=>w.id===chosenWorld);if(!w||!w.saved)return;confirm(tr('selectWorld.deleteQuestion'),tr('selectWorld.deleteWarning',w.name),tr('selectWorld.deleteButton'),()=>{chosenWorld=null;void MenuApplication.deleteWorld(w.id)})}");

  // Give each action its real availability; MenuWorldsRuntime.js refreshes the buttons.
  patch.edit('worlds(){', 'worlds(){title(', 'worlds(){if(!chosenWorld)chosenWorld=MenuApplication.defaultWorld();title(');
  patch.edit('worlds(){', "!chosenWorld).dataset.worldAction='select'", "!chosenWorld).dataset.worldAction='play'");
  patch.edit('worlds(){', 'draft=newDraft();draft.name=w.name;draft.gameMode=w.mode;', 'draft=recreateDraft(w);');
  patch.edit('worlds(){', "if(i<3)b.dataset.worldAction='select'})", "if(i<3)b.dataset.worldAction='saved'});refreshWorldActions()");
  patch.edit('function drawWorlds(){', "el('span','world-play',r);", "if(w.builtIn||w.saved)el('span','world-play',r);");
  patch.edit('function drawWorlds(){', "w.folder+' ('+w.date+')'", "(w.summary??'')");
  patch.edit('function drawWorlds(){', "w.mode+', '+tr('selectWorld.version')+' '+w.version", "(w.status??'')");
  patch.edit('function drawWorlds(){', "screen.querySelectorAll('[data-world-action]').forEach(e=>e.disabled=false)", 'refreshWorldActions()');
  patch.edit('function drawWorlds(){', "screen.querySelectorAll('[data-world-action]').forEach(e=>e.disabled=!chosenWorld)", 'refreshWorldActions()');
  patch.line('function loadWorld(){', '// Defined by MenuWorldsRuntime.js.');

  // Create New World: native checks and saves the world, then opens it (MenuApplication.createWorld).
  patch.edit('create(){',
    "()=>message('World creation stops here','No world is generated in this menu prototype. Your name, seed, game rules, and generation settings are kept so you can return and change them.',tr('gui.back'))",
    '()=>void MenuApplication.createWorld(draft)');
  // 26.3's GenericMessageScreen: one line of text in the middle, no buttons; shown while a world opens.
  patch.edit('message(){title(current.data.title', 'message(){',
    'generic(){title(current.data.title,Math.floor(H/2)-4)},message(){');
  // It cannot be closed (26.3's shouldCloseOnEsc is false): the world is already on its way.
  patch.edit("document.addEventListener('keydown',e=>{if(!e.repeat)startMenuMusic();",
    "if(e.key==='Escape'){e.preventDefault();if(current.name==='rules')",
    "if(e.key==='Escape'){e.preventDefault();if(current.name==='generic')return;if(current.name==='rules')");
}
