import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {readCaptureEvidence} from './capture-evidence.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const captureName=process.argv.find(x=>x.startsWith('--capture='))?.split('=')[1]||'review-accepted-packaged-1920x1080';
const videoCaptureName=process.argv.find(x=>x.startsWith('--video-capture='))?.split('=')[1]||'review-accepted-video-1920x1080';
const capture=path.join(root,'Tests/Captures',captureName);
const {run,data,extras}=readCaptureEvidence(path.join(root,'Tests/Captures'),captureName,videoCaptureName);
const live=new Set(['fullscreen','framerateLimit','enableVsync','guiScale','menuBackgroundBlurriness']);
const isQuit=c=>c.screen==='main'&&c.label==='Quit Game';
const isLive=c=>live.has(c.setting)||c.label==='Hard Reset'||isQuit(c);
const byID=new Map();
for(const c of [...run.controls,...extras]){
  if(!run.routes.includes(c.screen)||c.setting==='realmsNotifications')continue;
  const old=byID.get(c.id);
  byID.set(c.id,{...c,contexts:[...new Set([...(old?.contexts||[]),...(c.context?[c.context]:[])])]});
}
const controls=[...byID.values()];
const dependencies={maxAnisotropyBit:'Texture Filtering must be Anisotropic.',rotateWithMinecart:'Intentionally unavailable in source.',allowFriendRequests:'Friends List must be on.',inGameNotification:'Friends List must be on.',sharePresence:'Friends List must be on.',graphicsPreset:'Fast/Fancy/Fabulous update the source quality group; manual quality edits set Custom.',highContrast:'Shared with the staged High Contrast pack selection. Presentation remains unchanged.',guiScale:'Integer Auto/explicit scale is bounded by the live viewport; stored preferences render clamped in smaller windows.',narrator:'Available as a mock; no OS narrator capability is required.'};
const fields={
  'Enter World Name':'New World','World Name':'New World','Seed for the World Generator':'',
  'Server Address':'','Server Name':'Minecraft Server',
  'Language':'','Search Game Rules':'','Search Biomes':'','Search Worlds':'',
};
const integration=(c)=>{
  if(isQuit(c))return 'Implemented by restricted MenuApplication shutdown binding; queued settings drain, final preference flush, normal Unreal exit and editor protection are documented in QuitGame.md.';
  if(isLive(c))return 'Implemented by the restricted MenuVideoSettings/MenuVideo adapter; persistence and confirmed preference reset are documented in VideoSettings.md.';
  if(c.setting==='fullscreen'||c.setting==='exclusiveFullscreen')return 'Future UGameUserSettings/window mode adapter; distinguish borderless and supported exclusive modes.';
  if(c.setting==='fov')return 'Future camera projection adapter; investigate Minecraft versus Unreal horizontal/vertical FOV conventions first.';
  if(c.setting?.startsWith('sound_')||c.setting==='soundDevice')return 'Future Unreal audio mixer/device adapter; not bound in this phase.';
  if(['guiScale','forceUnicodeFont','japaneseGlyphVariants','menuBackgroundBlurriness','highContrast','hideSplashTexts','panoramaSpeed'].includes(c.setting))return 'Future presentation adapter separate from the frozen baseline; no current binding.';
  if(c.binding)return 'Future Enhanced Input mapping adapter; conflict policy and persistence require separate approval.';
  if(['framerateLimit','enableVsync','gamma'].includes(c.setting))return 'Future supported user-settings adapter; actual outcome must be measured and reversible.';
  return 'TBD. No existing Unreal gameplay, world, server, account, voxel renderer or telemetry service is assumed.';
};
const stageScreens=new Set(['create','language','packs','datapacks','experiments','rules','flat','presets','biomes','serveredit','editworld','direct']);
const registry=controls.map(c=>{
  const def=c.setting?data.settings[c.setting]:null;
  const binding=c.binding?data.bindings.find(b=>b.id===c.binding):null;
  const ruleID=c.local?.startsWith('rule.')?c.local.slice(5):null;
  const rule=ruleID?data.rules.find(r=>r.id===ruleID):data.rules.find(r=>r.label===c.label);
  const edge=run.edges.find(e=>e.control===c.id);
  const isScroll=c.type==='scrollbar';
  const defaultValue=def?.default??binding?.default??rule?.default??fields[c.label]??(isScroll?0:c.type==='checkbox'?false:c.type==='option'?(['worlds','multiplayer','packs','presets'].includes(c.screen)?false:c.value):c.type==='text'||c.type==='range'?c.value:'Not applicable (command)');
  const bounds=rule?{min:rule.min??null,max:rule.max??null,step:rule.type==='Boolean'?null:1,choices:rule.type==='Boolean'?[false,true]:null}:isScroll?{min:0,max:'scrollHeight - clientHeight',step:'continuous logical CSS pixels'}:c.type==='text'?{minLength:0,maxLength:c.screen==='presets'?1024:128}:c.range;
  const alwaysDisabled=c.setting==='rotateWithMinecart'||c.label.startsWith('Exclusive Fullscreen Mode')||c.label==='Reset Icon';
  let disabled=dependencies[c.setting]||'Enabled; fields are subject to source validation.';
  if(c.screen==='create'&&['draft.difficulty','draft.commands'].includes(c.local))disabled='Disabled in Hardcore (Hard difficulty, commands off); Bonus Chest is also disabled in Hardcore.';
  else if(['worlds','multiplayer'].includes(c.screen)&&['Play Selected World','Join Server','Edit','Delete','Re-Create'].includes(c.label))disabled='Requires a selected fictional list entry.';
  else if(c.label==='Reset'||c.label==='Reset Keys')disabled='Disabled when the relevant mock binding(s) equal their defaults.';
  else if(c.id.includes('move_up')||c.label.endsWith(' up'))disabled='Disabled at the top of the selected pack order.';
  else if(c.id.includes('move_down')||c.label.endsWith(' down'))disabled='Disabled at the bottom of the selected pack order.';
  else if(c.id.includes('server-save'))disabled='Nonempty trimmed name and syntactically valid address required; no DNS, server or account validation.';
  else if(c.id.includes('direct-connect'))disabled='Nonempty syntactically valid address required; no DNS, server or account validation.';
  else if(c.id.includes('rules-done'))disabled='Disabled while any integer draft is invalid or outside its supplied min/max, including filtered-out fields.';
  else if(c.screen==='create'&&c.label==='Customize')disabled='Enabled only for mock Superflat or Single Biome world type.';
  else if(c.screen==='flat'&&c.label==='Remove Layer')disabled='Disabled when the staged mock layer list is empty.';
  else if(c.screen==='create'&&c.label==='Create New World'||c.id.includes('world-save'))disabled='Requires a nonempty trimmed mock world name.';
  else if(isScroll)disabled='Hidden when content does not exceed the viewport; otherwise wheel/drag/page keys affect only list position.';
  const meaning=def?`${def.label}. ${def.tip||c.tooltip||'Source setting '+c.setting+' controls the named Minecraft option.'}`:binding?`Minecraft action: ${binding.label||binding.id}; source key ${binding.id}.`:rule?`Minecraft game rule ${rule.id}: ${rule.tip||rule.label}; source default ${JSON.stringify(rule.default)}.`:edge?`Source menu command ${c.label} opens ${edge.to}.`:c.type==='option'?`Select the displayed ${c.screen} entry; source sample data or bundled Minecraft metadata.`:`Source ${c.screen} control: ${c.label||c.id}. Behavior is defined by its renderer, not an assumed engine equivalent.`;
  return {...c,label:c.label,default:defaultValue,range:bounds,
    dependencies:dependencies[c.setting]||'No additional dependency in source.',disabledConditions:disabled,
    mockInteraction:c.setting?'Update shared in-memory value and label only. No engine/window/device effect.':c.binding?'Capture a keyboard or mouse symbol in session state; Reset restores source default.':rule?'Edit staged rule value; validate source bounds.':isScroll?'Scroll the menu list only; never change a graphics or mouse-sensitivity setting.':edge?`Navigate to ${edge.to}; no real service starts.`:c.type==='option'?'Select a session-only entry; pack movement changes staged order.':c.type==='text'?'Edit a session-only draft/search value; no disk or network output.':'Execute the source navigation/mock command. Service commands end at reversible explanation screens.',
    doneCancelEscape:stageScreens.has(c.screen)?'Done/Save/Use Preset commits the staged domain; Cancel/Escape discards it. Child commits remain staged until the parent is committed. Create opens an explanation, never generates a world.':'Shared settings change immediately in memory; Done/Escape navigates back without engine changes. Confirmation Cancel/Escape never runs its affirmative action.',
    minecraftMeaning:meaning,proposedUnrealIntegration:integration(c),
    laterScope:'Separate explicitly authorized task for this control only; preserve visual control, add one real adapter, no unrelated gameplay framework.',
    acceptance:['Verify actual supported outcome, not label alone.','Verify failure, revert, shared state, supported bounds and input paths.','Verify editor and packaged application; persistence/network only if separately authorized.'],
    unresolved:c.setting==='fov'?'Projection/aspect conventions and supported cameras remain uninvestigated.':alwaysDisabled?'Supported native capability and enablement policy remain TBD.':'Native equivalence, supported platforms, ownership, persistence and failure policy remain TBD.',
    status:isLive(c)?'IMPLEMENTED':alwaysDisabled?'INTENTIONALLY_DISABLED':'UI_ONLY',
    evidence:{reference:'Reference/minecraft-menu/index.html',renderer:c.screen,definitionKey:c.setting||c.binding||rule?.id||null,observed:capture.replaceAll('\\','/')},
  };
});
for(const c of registry.filter(isLive)){
  c.mockInteraction='Functional native setting or confirmed reset; see VideoSettings.md.';
  c.doneCancelEscape='Settings apply immediately and persist after completion; Done/Escape returns without reverting. Reset confirmation Cancel/Escape changes nothing.';
  c.laterScope='Implemented scope only. Other settings remain deferred.';
  c.unresolved='Physical OS input, driver-level VSync and multi-monitor/DPI checks remain human acceptance gates.';
  c.acceptance=['Native validation and ordered asynchronous requests.','Actual window/client dimensions, engine cadence and rendered presentation.','Independent process relaunch and confirmed reset; unrelated save data preserved.'];
  c.evidence.functional='Docs/VideoSettings.md; Tests/VideoPersistenceResults.json; Tests/Captures/video-settings-1280x720/results.json';
  if(c.setting==='guiScale')c.dependencies='Integer Auto/explicit scale bounded by the live viewport, reserving 416x240 logical pixels; stored scale is clamped when rendering smaller windows.';
  c.disabledConditions=['fullscreen','framerateLimit','enableVsync'].includes(c.setting)||c.label==='Hard Reset'?'Unavailable in embedded PIE; requires a ready standalone native window.':'Requires a ready settings adapter.';
  if(isQuit(c)){
    c.mockInteraction='Normal standalone game shutdown, not a mock screen; no forced termination or preference reset.';
    c.doneCancelEscape='Activates immediately; drains and flushes settings first. Save failure cancels exit and allows retry.';
    c.disabledConditions='Unavailable in editor Play or without a valid local standalone owner/binding; disabled while quitting.';
    c.acceptance=['Normal owned-process exit through the visible main-menu button.','Queued settings survive normal exit and relaunch; unrelated saves preserved.','Repeated-request guard, failed-save retry and editor rejection.'];
    c.evidence.functional='Docs/QuitGame.md; Tests/QuitGameResults.json; Tests/QuitEditorSafetyResults.json';
    c.unresolved='Physical OS input, Shipping, other platforms and future gameplay save lifecycle remain separate acceptance gates.';
  }
}
fs.writeFileSync(path.join(root,'Docs/ControlRegistry.json'),JSON.stringify({phase:'PARTIAL_FUNCTIONAL',captures:[captureName,videoCaptureName],htmlSHA256:run.htmlSHA256,controls:registry},null,2)+'\n');
const esc=x=>String(x??'').replaceAll('|','\\|').replaceAll('\n',' ');
let report='# Control Registry\n\nGenerated from rendered controls and structured definitions. Five video settings, confirmed preference reset and normal Quit Game are implemented; other controls remain mocks. See VideoSettings.md and QuitGame.md for functional acceptance and remaining human gates. `ControlRegistry.json` records the status of every entry.\n\n';
report+=`Observed ${registry.length} distinct controls across ${run.routes.length} source routes and Create tabs. Dynamic selected-pack controls are included in the supplemental observations.\n\n`;
report+='| Screen | Stable ID | Exact observed label | Widget | Default | Status |\n|---|---|---|---|---|---|\n';
for(const c of registry)report+=`| ${esc(c.screen+(c.variant?'/'+c.variant:''))} | ${esc(c.id)} | ${esc(c.label)} | ${esc(c.type)} | ${esc(JSON.stringify(c.default))} | ${c.status} |\n`;
fs.writeFileSync(path.join(root,'Docs/ControlRegistry.md'),report);
const parents={main:'Boot',options:'main',video:'options',sounds:'options',controls:'options',accessibility:'main/options',online:'options',language:'main/options',keybinds:'controls',worlds:'main',create:'worlds',editworld:'worlds (selected)',safety:'main Multiplayer',multiplayer:'safety/skip warning',direct:'multiplayer',serveredit:'multiplayer (Add/Edit)',packs:'options',datapacks:'create/more',experiments:'create/more',rules:'create/more',flat:'create/world (Superflat)',presets:'flat',biomes:'create/world (Single Biome)',credits:'main/options',creditstext:'credits',telemetry:'options',linkconfirm:'accessibility/online/credits/telemetry',message:'reversible service endpoints',confirm:'friends and sample deletion',skin:'options',chat:'options',mouse:'controls',font:'language'};
fs.writeFileSync(path.join(root,'Docs/ScreenInventory.md'),'# Screen Inventory\n\nThe working interface retains '+run.routes.length+' renderer IDs. Minecraft Realms, Join Realm, their entry points and the Realms news/invites preference were deliberately removed. The mock Game closed route was removed for normal Quit Game; the immutable reference retains all original renderers and assets. Create has Game, World and More tabs. Friends, connection, creation, backup/folder and clipboard actions use the source message/confirmation routes without services.\n\n| Route | Expected entry | Return |\n|---|---|---|\n'+run.routes.map(r=>`| ${r} | ${parents[r]||'Source route'} | Source parent via Back/Done/Cancel/Escape; main remains main |`).join('\n')+'\n\nObserved route edges are recorded in the smoke results. Programmatic preparation is used to cover isolated renderers; native browser mouse/keyboard and targeted nested routes are tested separately.\n');
const features=[...new Map(registry.map(c=>[c.setting||c.binding||c.id,c])).values()].filter(c=>!isLive(c));
fs.writeFileSync(path.join(root,'Docs/FunctionalBacklog.md'),'# Functional Backlog\n\n## Implemented Batch\n\nFullscreen (borderless), Max Framerate, VSync, GUI Scale and Menu Background Blur are implemented with persistence and confirmed preference reset. Quit Game performs normal standalone shutdown after settings drain/flush and protects the editor. See VideoSettings.md and QuitGame.md for scope, evidence and remaining human gates.\n\n## Deferred Controls\n\nThe JSON registry is the per-control scope record. Each item below remains deferred real functionality, even where its mock is interactive. Minecraft-only concepts need an explicit design decision; native API availability alone is not acceptance. Exclusive fullscreen and monitor video-mode selection remain deferred.\n\n'+features.map(c=>`- \`${c.setting||c.binding||c.id}\`: ${esc(c.label)}. ${c.proposedUnrealIntegration} Later checks and questions are in its registry entry.`).join('\n')+'\n');
console.log(`Generated ${registry.length} controls, ${features.length} deferred features, ${run.routes.length} screen routes.`);
