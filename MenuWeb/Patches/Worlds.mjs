// Singleplayer world list: playable built-in worlds beside the two fictional design entries.
// Only MenuApplication.openWorld can load a world, and only by its fixed catalog ID.
export const name = 'Worlds';

const playable = "['super-flat','foundation']";

export function apply(patch) {
  patch.line('const state=', "const singleplayerWorldIds=new Set(['foundation','super-flat','fictional-forest','fictional-build']);const state={settings:{...defaults},worlds:clone(defaultWorlds).filter(w=>singleplayerWorldIds.has(w.id)),servers:clone(defaultServers),bindings:{},language:'en_us',lastAddress:'',packs:[]};");
  patch.edit('const defaultWorlds=', 'const defaultWorlds=[',
    "const defaultWorlds=[{id:'foundation',name:'Player Foundation',folder:'Foundation',date:'Java 26.3 baseline lab',mode:'Test',version:'Unreal'},{id:'super-flat',name:'Super Flat',folder:'Super_Flat',date:'Built-in test map',mode:'Test',version:'Unreal'},");

  // Built-in worlds cannot be edited, deleted or re-created like mock saves.
  patch.edit('editworld(){', 'if(!w){', `if(!w||${playable}.includes(w.id)){`);
  patch.edit('function deleteWorld(){', 'if(!w)return;', `if(!w||${playable}.includes(w.id))return;`);

  // Give each action its real availability; MenuWorldsRuntime.js refreshes the buttons.
  patch.edit('worlds(){', 'worlds(){title(', "worlds(){if(!chosenWorld)chosenWorld='super-flat';title(");
  patch.edit('worlds(){', "!chosenWorld).dataset.worldAction='select'", "!chosenWorld).dataset.worldAction='play'");
  patch.edit('worlds(){', "if(i<3)b.dataset.worldAction='select'})", "if(i<3)b.dataset.worldAction='reference'});refreshWorldActions()");
  patch.edit('function drawWorlds(){', "el('span','world-play',r);", `if(${playable}.includes(w.id))el('span','world-play',r);`);
  patch.edit('function drawWorlds(){', "w.folder+' ('+w.date+')'",
    "w.id==='foundation'?'First-person movement, controls and diagnostics':w.id==='super-flat'?'2 km x 2 km - flat test ground':'Design reference - does not load'");
  patch.edit('function drawWorlds(){', "w.mode+', '+tr('selectWorld.version')+' '+w.version",
    "w.id==='foundation'?'Playable - Java 26.3 foundation':w.id==='super-flat'?'Playable - temporary fly camera':w.mode+' - menu design only'");
  patch.edit('function drawWorlds(){', "screen.querySelectorAll('[data-world-action]').forEach(e=>e.disabled=false)", 'refreshWorldActions()');
  patch.edit('function drawWorlds(){', "screen.querySelectorAll('[data-world-action]').forEach(e=>e.disabled=!chosenWorld)", 'refreshWorldActions()');
  patch.line('function loadWorld(){', '// Defined by MenuWorldsRuntime.js.');
}
