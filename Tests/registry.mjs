import fs from 'node:fs';
import {evidencePath, findEvidence} from './evidence.mjs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {readCaptureEvidence} from './capture-evidence.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const captureName=process.argv.find(x=>x.startsWith('--capture='))?.split('=')[1]||'review-accepted-packaged-1920x1080';
const videoCaptureName=process.argv.find(x=>x.startsWith('--video-capture='))?.split('=')[1]||'review-accepted-video-1920x1080';
const findCapture=name=>findEvidence('Captures',name)??evidencePath('Captures',name);
const capture=findCapture(captureName);
const {run,data,extras}=readCaptureEvidence(findCapture,captureName,videoCaptureName);
const live=new Set(['fullscreen','framerateLimit','enableVsync','guiScale','menuBackgroundBlurriness']);
const isQuit=c=>c.screen==='main'&&c.label==='Quit Game';
const isWorld=c=>c.screen==='worlds'&&(c.label==='Play Selected World'||c.id.includes('playground'));
const movement=new Set(['key.forward','key.back','key.left','key.right','key.sprint','key.sneak','key.jump']);
const isInput=c=>movement.has(c.binding)||['toggleSprint','toggleCrouch'].includes(c.setting)||(c.screen==='keybinds'&&c.label==='Reset Keys');
const isLive=c=>live.has(c.setting)||c.label==='Hard Reset'||isQuit(c)||isWorld(c)||isInput(c)||c.setting==='preferredGraphicsBackend';
const preferences=JSON.parse(fs.readFileSync(path.join(root,'Content/Menu/Web/preferences-schema.menuprefs'),'utf8'));
const preference=c=>c.setting in preferences.settings?c.setting:
  c.screen==='safety'&&c.type==='checkbox'?'skipWarning':
  c.screen==='telemetry'&&c.type==='checkbox'?'telemetryOptInExtra':
  c.screen==='language'&&(c.type==='option'||c.label==='Done')?'language':
  c.screen==='packs'&&(c.type==='option'||c.label==='Done'||/^(Select |Move )/.test(c.label))?'packs':null;
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
  if(isWorld(c))return 'Restricted native map travel to Playground after settings drain/flush; the other two entries remain references. See Playground.md.';
  if(isInput(c))return 'Native Enhanced Input binding/policy settings and movement intent; actual character movement remains pending. See Keybindings.md.';
  if(isQuit(c))return 'Implemented by restricted MenuApplication shutdown binding; queued settings drain, final preference flush, normal Unreal exit and editor protection are documented in QuitGame.md.';
  if(isLive(c))return 'Implemented by the restricted MenuVideoSettings/MenuVideo adapter; persistence and confirmed preference reset are documented in VideoSettings.md.';
  if(c.setting==='fullscreen'||c.setting==='exclusiveFullscreen')return 'Future UGameUserSettings/window mode adapter; distinguish borderless and supported exclusive modes.';
  if(c.setting==='fov')return 'Future camera projection adapter; investigate Minecraft versus Unreal horizontal/vertical FOV conventions first.';
  if(c.setting?.startsWith('sound_')||c.setting==='soundDevice')return 'Future Unreal audio mixer/device adapter; not bound in this phase.';
  if(['guiScale','forceUnicodeFont','japaneseGlyphVariants','menuBackgroundBlurriness','highContrast','hideSplashTexts','panoramaSpeed'].includes(c.setting))return 'Future presentation adapter separate from the frozen baseline; no current binding.';
  if(c.binding)return 'Planned action, disabled until its gameplay system/context and conflict policy are implemented. See Keybindings.md.';
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
  if(c.binding&&!movement.has(c.binding))disabled='Planned and disabled until its owning gameplay system and context are implemented.';
  else if(c.screen==='create'&&['draft.difficulty','draft.commands'].includes(c.local))disabled='Disabled in Hardcore (Hard difficulty, commands off); Bonus Chest is also disabled in Hardcore.';
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
    status:isLive(c)?'IMPLEMENTED':c.binding?'PLANNED':alwaysDisabled?'INTENTIONALLY_DISABLED':'UI_ONLY',
    evidence:{reference:'Reference/minecraft-menu/index.html',renderer:c.screen,definitionKey:c.setting||c.binding||rule?.id||null,observed:capture.replaceAll('\\','/')},
  };
});
for(const c of registry.filter(isLive)){
  if(isWorld(c)){
    c.status='WORLD_TRAVEL_IMPLEMENTED';
    c.mockInteraction='Select or open the built-in Playground level through the restricted native application service.';
    c.doneCancelEscape='Escape leaves the list; from the standalone preview world it returns to the main menu.';
    c.disabledConditions='Play requires Playground selected and a ready native game/PIE service. Reference entries never load. Edit/Delete/Re-Create are unavailable for the built-in map.';
    c.laterScope='The future FPS character replaces the stock preview camera after collision/input integration.';
    c.unresolved='Physical input and embedded PIE acceptance remain separate checks.';
    c.acceptance=['Play, double-click and Enter open the actual level.','Ground collision, clear spawn, repeated menu return, and retained settings.'];
    c.evidence.functional='Docs/Playground.md; Tests/Evidence/2026-10-07/Captures/super-flat-2026-10-07T18-36-54-558Z/results.json';
    continue;
  }
  if(c.setting==='preferredGraphicsBackend'){
    c.status='NATIVE_READ_ONLY';
    c.default='Default (DirectX 12)';
    c.range=['Default'];
    c.minecraftMeaning='The project startup graphics API default and currently active native Unreal RHI.';
    c.mockInteraction='Read the configured default and active RHI from Unreal; display both in the tooltip.';
    c.doneCancelEscape='Read-only display; navigation changes no graphics API.';
    c.disabledConditions='Switching graphics APIs is unavailable until the startup/restart path exists.';
    c.proposedUnrealIntegration='Native configured default and GDynamicRHI observation are implemented.';
    c.laterScope='A validated platform-specific startup/restart selector remains deferred.';
    c.unresolved='Graphics API switching, fallback recovery and other-platform policies are deferred.';
    c.acceptance=['Native configured default and actual active RHI match displayed label/tooltip.'];
    c.evidence.functional='Tests/Evidence/2026-10-07/Captures/input-foundation-*/results.json; Docs/Keybindings.md';
    continue;
  }
  if(isInput(c)){
    c.status='INPUT_SETTINGS_IMPLEMENTED';
    c.mockInteraction='Movement bindings and Hold/Toggle policies update the native Enhanced Input profile; settings persist in standalone mode.';
    c.doneCancelEscape='Changes save immediately; Done returns without reverting. Escape cancels capture; Backspace unbinds. Reset Keys restores the seven keys and preserves Hold/Toggle policies.';
    c.disabledConditions='Requires ready native service; disabled during a request or shutdown. Simultaneous movement-key conflicts and reserved Escape/F11/console keys are rejected.';
    c.laterScope='Actual CharacterMovement stance, speed, jump, camera, animation and networking remain deferred.';
    c.unresolved='Physical OS focus/input and actual character movement require later acceptance.';
    c.acceptance=['Native key-to-action-to-intent pipeline, conflicts and serializer.','Packaged key capture, both modifier sides, reload/relaunch persistence and resets.','Future character decides legal actual movement and server authority.'];
    c.evidence.functional='Docs/Keybindings.md; Tests/Evidence/2026-10-07/InputAutomation reports; Tests/Evidence/2026-10-07/Captures/input-foundation-*';
    continue;
  }
  c.mockInteraction='Functional native setting or confirmed reset; see VideoSettings.md.';
  c.doneCancelEscape='Settings apply immediately and persist after completion; Done/Escape returns without reverting. Reset confirmation Cancel/Escape changes nothing.';
  c.laterScope='Implemented scope only. Other settings remain deferred.';
  c.unresolved='Physical OS input, driver-level VSync and multi-monitor/DPI checks remain human acceptance gates.';
  c.acceptance=['Native validation and ordered asynchronous requests.','Actual window/client dimensions, engine cadence and rendered presentation.','Independent process relaunch and confirmed reset; unrelated save data preserved.'];
  c.evidence.functional='Docs/VideoSettings.md; Tests/Evidence/2026-10-07/VideoPersistenceResults.json; Tests/Evidence/2026-10-07/Captures/video-settings-1280x720/results.json';
  if(c.setting==='guiScale')c.dependencies='Integer Auto/explicit scale bounded by the live viewport, reserving 416x240 logical pixels; stored scale is clamped when rendering smaller windows.';
  c.disabledConditions=['fullscreen','framerateLimit','enableVsync'].includes(c.setting)||c.label==='Hard Reset'?'Unavailable in embedded PIE; requires a ready standalone native window.':'Requires a ready settings adapter.';
  if(isQuit(c)){
    c.mockInteraction='Normal standalone game shutdown, not a mock screen; no forced termination or preference reset.';
    c.doneCancelEscape='Activates immediately; drains and flushes settings first. Save failure cancels exit and allows retry.';
    c.disabledConditions='Unavailable in editor Play or without a valid local standalone owner/binding; disabled while quitting.';
    c.acceptance=['Normal owned-process exit through the visible main-menu button.','Queued settings survive normal exit and relaunch; unrelated saves preserved.','Repeated-request guard, failed-save retry and editor rejection.'];
    c.evidence.functional='Docs/QuitGame.md; Tests/Evidence/2026-10-07/QuitGameResults.json; Tests/Evidence/2026-10-07/QuitEditorSafetyResults.json';
    c.unresolved='Physical OS input, Shipping, other platforms and future gameplay save lifecycle remain separate acceptance gates.';
  }
}
for(const c of registry.filter(preference)){
  const id=preference(c),staged=['language','packs'].includes(id);
  c.preference=id;
  c.status=id==='skipWarning'?'PREFERENCE_IMPLEMENTED':'PREFERENCE_SAVED_EFFECT_PENDING';
  c.mockInteraction=staged?'Edit a staged preference. Native storage commits the chosen locale or ordered pack IDs on Done.':
    'Save the desired value through UMenuPreferences; shared controls read the same native value.';
  c.doneCancelEscape=staged?'Done waits for a successful native commit. Cancel/Escape discards the draft.':
    'Changes save on edit; Done/Escape returns without reverting.';
  c.proposedUnrealIntegration='Native UMenuPreferences persistence is implemented. '+c.proposedUnrealIntegration;
  c.unresolved=id==='skipWarning'?'No multiplayer connection is implemented.':
    'The feature effect remains pending; saving this choice does not activate its gameplay, renderer, audio, language or telemetry system.';
  c.acceptance=['Native schema validation and readback for all 101 scalar IDs.','Reload, process relaunch, ordered requests, staged Done/Cancel, reset and normal Quit.','Future effects require their own functional acceptance.'];
  c.evidence.functional='Docs/MenuPreferences.md; Tests/Evidence/2026-10-07/Captures/preferences-*/results.json; Tests/Evidence/2026-10-07/MenuPreferencesAutomation-*';
}
fs.writeFileSync(path.join(root,'Docs/ControlRegistry.json'),JSON.stringify({phase:'PREFERENCES_SAVED_EFFECTS_PARTIAL',captures:[captureName,videoCaptureName],htmlSHA256:run.htmlSHA256,controls:registry},null,2)+'\n');
const esc=x=>String(x??'').replaceAll('|','\\|').replaceAll('\n',' ');
let report='# Control Registry\n\nGenerated from rendered controls and structured definitions. Five video settings, confirmed preference reset and normal Quit Game are implemented; other controls remain mocks. See VideoSettings.md and QuitGame.md for functional acceptance and remaining human gates. `ControlRegistry.json` records the status of every entry.\n\n';
report+=`Observed ${registry.length} distinct controls across ${run.routes.length} source routes and Create tabs. Dynamic selected-pack controls are included in the supplemental observations.\n\n`;
report=report.replace('Five video settings, confirmed preference reset and normal Quit Game are implemented; other controls remain mocks.', 'Five video settings, confirmed preference reset and normal Quit Game are implemented. Seven movement binding settings and two Hold/Toggle policies use native Enhanced Input; actual character gameplay remains pending. Other binding rows are Planned and disabled. See Keybindings.md.');
report+='101 additional scalar preferences, language, pack order, warning dismissal and telemetry choice now use native persistence. Saved choices and implemented effects have separate statuses. See MenuPreferences.md.\n\n';
report+='| Screen | Stable ID | Exact observed label | Widget | Default | Status |\n|---|---|---|---|---|---|\n';
for(const c of registry)report+=`| ${esc(c.screen+(c.variant?'/'+c.variant:''))} | ${esc(c.id)} | ${esc(c.label)} | ${esc(c.type)} | ${esc(JSON.stringify(c.default))} | ${c.status} |\n`;
fs.writeFileSync(path.join(root,'Docs/ControlRegistry.md'),report);
const parents={main:'Boot',options:'main',video:'options',sounds:'options',controls:'options',accessibility:'main/options',online:'options',language:'main/options',keybinds:'controls',worlds:'main',create:'worlds',editworld:'worlds (selected)',safety:'main Multiplayer',multiplayer:'safety/skip warning',direct:'multiplayer',serveredit:'multiplayer (Add/Edit)',packs:'options',datapacks:'create/more',experiments:'create/more',rules:'create/more',flat:'create/world (Superflat)',presets:'flat',biomes:'create/world (Single Biome)',credits:'main/options',creditstext:'credits',telemetry:'options',linkconfirm:'accessibility/online/credits/telemetry',message:'reversible service endpoints',confirm:'friends and sample deletion',skin:'options',chat:'options',mouse:'controls',font:'language'};
fs.writeFileSync(path.join(root,'Docs/ScreenInventory.md'),'# Screen Inventory\n\nThe working interface retains '+run.routes.length+' renderer IDs. Minecraft Realms, Join Realm, their entry points and the Realms news/invites preference were deliberately removed. The mock Game closed route was removed for normal Quit Game; the immutable reference retains all original renderers and assets. Create has Game, World and More tabs. Friends, connection, creation, backup/folder and clipboard actions use the source message/confirmation routes without services.\n\n| Route | Expected entry | Return |\n|---|---|---|\n'+run.routes.map(r=>`| ${r} | ${parents[r]||'Source route'} | Source parent via Back/Done/Cancel/Escape; main remains main |`).join('\n')+'\n\nObserved route edges are recorded in the smoke results. Programmatic preparation is used to cover isolated renderers; native browser mouse/keyboard and targeted nested routes are tested separately.\n');
const features=[...new Map(registry.map(c=>[c.setting||c.binding||c.id,c])).values()].filter(c=>!isLive(c)&&preference(c)!=='skipWarning');
fs.writeFileSync(path.join(root,'Docs/FunctionalBacklog.md'),'# Functional Backlog\n\n## Implemented Batch\n\nFullscreen (borderless), Max Framerate, VSync, GUI Scale and Menu Background Blur are implemented with persistence and confirmed preference reset. Quit Game performs normal standalone shutdown after settings drain/flush and protects the editor. Seven movement binding settings and two Hold/Toggle policies are native and persistent; actual character movement remains pending. See Keybindings.md, VideoSettings.md and QuitGame.md for scope, evidence and remaining human gates.\n\n## Deferred Controls\n\nThe JSON registry is the per-control scope record. These effects remain pending even where their choices now save natively. The new preference store remembers 101 scalar choices plus language, resource-pack order, warning dismissal and the extra telemetry choice; see MenuPreferences.md. Minecraft-only concepts need an explicit design decision; native API availability alone is not acceptance. Exclusive fullscreen and monitor video-mode selection remain deferred.\n\n'+features.map(c=>`- \`${c.setting||c.binding||c.id}\`: ${esc(c.label)}. ${c.proposedUnrealIntegration} Later checks and questions are in its registry entry.`).join('\n')+'\n');
console.log(`Generated ${registry.length} controls, ${features.length} deferred features, ${run.routes.length} screen routes.`);
