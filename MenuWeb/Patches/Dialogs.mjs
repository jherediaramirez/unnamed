// Dialog drafts: text survives redraws and child screens, Done validates the whole draft,
// and confirmed dialogs commit their staged changes (MenuDialogRuntime.js holds the helpers).
export const name = 'Dialogs';

export function apply(patch) {
  patch.edit('editworld(){', 'let newName=w.name;', 'let newName=current.data.worldName??w.name;');
  patch.edit('editworld(){', 'newName=v;', 'current.data.worldName=newName=v;');
  patch.edit('editworld(){', 'w.name=newName.trim();persist();back()});save.id=', 'w.name=newName.trim();commitStage();back()},!newName.trim());save.id=');

  patch.edit('presets(){', "const f=field(flatEdit.slice().reverse().map(l=>(l.height>1?l.height+'*':'')+l.block).join(',')+';'+flatBiomeEdit,",
    "const f=field(current.data.presetText??(flatEdit.slice().reverse().map(l=>(l.height>1?l.height+'*':'')+l.block).join(',')+';'+flatBiomeEdit),");
  patch.edit('presets(){', 'current.data.presetText||f.value', 'current.data.presetText??f.value');

  patch.edit('rules(){', '()=>{draft.rules=clone(rulesEdit);back()', '()=>{if(!ruleDraftsValid())return;draft.rules=clone(rulesEdit);commitStage();back()');
  patch.edit('rules(){', "b.id='rules-done';", "b.id='rules-done';refreshRuleValidation();");
  patch.line('function drawRules(){', '');

  patch.edit('serveredit(){', 'v=>editServer.name=v', 'v=>{editServer.name=v;refreshServerValidation()}');
  patch.edit('serveredit(){', "$('#server-save').disabled=!validAddress(v)", 'refreshServerValidation()');
  patch.edit('serveredit(){', '200,()=>{const i=state.servers', '200,()=>{if(!validServerDraft())return;const i=state.servers');
  patch.edit('serveredit(){', "editServer.name.trim()||tr('selectServer.defaultName')", 'editServer.name.trim()');
  patch.edit('serveredit(){', '!validAddress(editServer.address)', '!validServerDraft()');

  for (const [before, after] of [
    ['draft.experiments=clone(experimentsEdit);back()', 'draft.experiments=clone(experimentsEdit);commitStage();back()'],
    ['draft.flatBiome=flatBiomeEdit;back()', 'draft.flatBiome=flatBiomeEdit;commitStage();back()'],
    ['flatBiomeEdit=biome;back()', 'flatBiomeEdit=biome;commitStage();back()'],
    ['draft.biome=biomeEdit;back()', 'draft.biome=biomeEdit;commitStage();back()'],
    ['chosenServer=s.id;persist();back()', 'chosenServer=s.id;commitStage();back()'],
  ]) patch.text(before, after);
  patch.text("if(v!=='Hardcore')draft.commands=v==='Creative';render()", "if(v==='Hardcore'){draft.difficulty='Hard';draft.commands=false;draft.bonus=false}else draft.commands=v==='Creative';render()");
  patch.text("state.lastAddress=v;persist();$('#direct-connect')", "state.lastAddress=v;$('#direct-connect')");
  patch.text('()=>stopConnection(state.lastAddress)', '()=>{commitStage();stopConnection(state.lastAddress)}');
}
