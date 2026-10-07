import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const capture=path.join(root,'Tests/Captures/packaged-1920x1080');
const run=JSON.parse(fs.readFileSync(path.join(capture,'results.json')));
const data=JSON.parse(fs.readFileSync(path.join(capture,'definitions.json')));
const extras=fs.existsSync(path.join(root,'Tests/AdditionalControls.json'))?JSON.parse(fs.readFileSync(path.join(root,'Tests/AdditionalControls.json'))):[];
const byID=new Map();
for(const c of [...run.controls,...extras]){
  const old=byID.get(c.id);
  byID.set(c.id,{...c,contexts:[...new Set([...(old?.contexts||[]),...(c.context?[c.context]:[])])]});
}
const controls=[...byID.values()];
const dependencies={maxAnisotropyBit:'Texture Filtering must be Anisotropic.',rotateWithMinecart:'Intentionally unavailable in source.',allowFriendRequests:'Friends List must be on.',inGameNotification:'Friends List must be on.',sharePresence:'Friends List must be on.',graphicsPreset:'Fast/Fancy/Fabulous update the source quality group; manual quality edits set Custom.',highContrast:'Shared with the staged High Contrast pack selection. Presentation remains unchanged.',guiScale:'Choices are limited by the source maximumScale calculation; only mock value changes.',narrator:'Available as a mock; no OS narrator capability is required.'};
const fields={
  'Enter World Name':'New World','World Name':'New World','Seed for the World Generator':'',
  'Server Address':'','Server Name':'Minecraft Server',
  'Language':'','Search Game Rules':'','Search Biomes':'','Search Worlds':'',
};
const integration=(c)=>{
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
  const bounds=rule?{min:rule.min??null,max:rule.max??null,step:rule.type==='Boolean'?null:1,choices:rule.type==='Boolean'?[false,true]:null}:isScroll?{min:0,max:'scrollHeight - clientHeight',step:'continuous logical CSS pixels'}:c.type==='text'?{minLength:0,maxLength:c.screen==='realmjoin'?32:c.screen==='presets'?1024:128}:c.range;
  const alwaysDisabled=c.setting==='rotateWithMinecart'||c.label.startsWith('Exclusive Fullscreen Mode')||c.label==='Reset Icon';
  let disabled=dependencies[c.setting]||'Enabled; fields are subject to source validation.';
  if(c.screen==='create'&&['draft.difficulty','draft.commands'].includes(c.local))disabled='Disabled in Hardcore (Hard difficulty, commands off); Bonus Chest is also disabled in Hardcore.';
  else if(['worlds','multiplayer'].includes(c.screen)&&['Play Selected World','Join Server','Edit','Delete','Re-Create'].includes(c.label))disabled='Requires a selected fictional list entry.';
  else if(c.label==='Reset'||c.label==='Reset Keys')disabled='Disabled when the relevant mock binding(s) equal their defaults.';
  else if(c.id.includes('move_up')||c.label.endsWith(' up'))disabled='Disabled at the top of the selected pack order.';
  else if(c.id.includes('move_down')||c.label.endsWith(' down'))disabled='Disabled at the bottom of the selected pack order.';
  else if(c.label==='Join Realm')disabled='Join-code submission is enabled only for nonempty mock input.';
  else if(c.id.includes('direct-connect')||c.id.includes('server-save'))disabled='Nonempty syntactically valid address required; no DNS, server or account validation.';
  else if(c.id.includes('rules-done'))disabled='Disabled while any displayed integer field is invalid or outside its supplied min/max.';
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
    status:alwaysDisabled?'INTENTIONALLY_DISABLED':'UI_ONLY',
    evidence:{reference:'Reference/minecraft-menu/index.html',renderer:c.screen,definitionKey:c.setting||c.binding||rule?.id||null,observed:capture.replaceAll('\\','/')},
  };
});
fs.writeFileSync(path.join(root,'Docs/ControlRegistry.json'),JSON.stringify({phase:'UI_ONLY',controls:registry},null,2)+'\n');
const esc=x=>String(x??'').replaceAll('|','\\|').replaceAll('\n',' ');
let report='# Control Registry\n\nGenerated from rendered controls in the packaged browser, plus the supplied structured definitions. No real feature is marked implemented. `ControlRegistry.json` contains defaults, ranges, dependencies, transactions, source meaning, proposed integration, later acceptance and unresolved questions for every entry.\n\n';
report+=`Observed ${registry.length} distinct controls across ${run.routes.length} source routes and Create tabs. Dynamic selected-pack controls are included in the supplemental observations.\n\n`;
report+='| Screen | Stable ID | Exact observed label | Widget | Default | Status |\n|---|---|---|---|---|---|\n';
for(const c of registry)report+=`| ${esc(c.screen+(c.variant?'/'+c.variant:''))} | ${esc(c.id)} | ${esc(c.label)} | ${esc(c.type)} | ${esc(JSON.stringify(c.default))} | ${c.status} |\n`;
fs.writeFileSync(path.join(root,'Docs/ControlRegistry.md'),report);
const parents={main:'Boot',options:'main',video:'options',sounds:'options',controls:'options',accessibility:'main/options',online:'options',language:'main/options',keybinds:'controls',worlds:'main',create:'worlds',editworld:'worlds (selected)',safety:'main Multiplayer',multiplayer:'safety/skip warning',direct:'multiplayer',serveredit:'multiplayer (Add/Edit)',realms:'main',realmjoin:'realms',packs:'options',datapacks:'create/more',experiments:'create/more',rules:'create/more',flat:'create/world (Superflat)',presets:'flat',biomes:'create/world (Single Biome)',credits:'main/options',creditstext:'credits',telemetry:'options',linkconfirm:'accessibility/online/realms/credits/telemetry',message:'reversible service endpoints',confirm:'friends and sample deletion',quit:'main',skin:'options',chat:'options',mouse:'controls',font:'language'};
fs.writeFileSync(path.join(root,'Docs/ScreenInventory.md'),'# Screen Inventory\n\nAll 36 renderer IDs from the supplied source are retained. Create has Game, World and More tabs. Friends, connection, creation, backup/folder, purchase and clipboard actions use the source message/confirmation routes without services.\n\n| Route | Expected entry | Return |\n|---|---|---|\n'+run.routes.map(r=>`| ${r} | ${parents[r]||'Source route'} | Source parent via Back/Done/Cancel/Escape; main remains main |`).join('\n')+'\n\nObserved route edges are recorded in the smoke results. Programmatic preparation is used to cover isolated renderers; native browser mouse/keyboard and targeted nested routes are tested separately.\n');
const features=[...new Map(registry.map(c=>[c.setting||c.binding||c.id,c])).values()];
fs.writeFileSync(path.join(root,'Docs/FunctionalBacklog.md'),'# Functional Backlog\n\nNothing in this backlog is authorized in the interface phase.\n\n## First Task: Fullscreen\n\nKeep the current control. Add a narrow Unreal user-settings/window adapter only after explicit approval. Distinguish windowed, borderless and supported exclusive behavior. Verify the actual resulting mode and viewport dimensions, not the displayed label. Test Alt+Enter/F11 policy, unsupported exclusive modes, focus loss, monitors/DPI, failure/revert and packaged execution. Do not enable resolution, audio or other settings in the same task. Persistence is a separate decision.\n\n## One Control At A Time\n\nThe JSON registry is the authoritative per-control scope and acceptance record. Each item below is deferred real functionality, even where its mock is interactive. Minecraft-only concepts require a design decision before an Unreal adapter can exist.\n\n'+features.map(c=>`- \`${c.setting||c.binding||c.id}\`: ${esc(c.label)}. ${c.proposedUnrealIntegration} Later checks and questions are in its registry entry.`).join('\n')+'\n');
console.log(`Generated ${registry.length} controls, ${features.length} deferred features, ${run.routes.length} screen routes.`);
