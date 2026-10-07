import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import vm from 'node:vm';
import {preferenceSchema} from './MenuPreferenceSchema.mjs';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const reference = path.join(root, 'Reference/minecraft-menu');
const debugToolsEnabled = !process.argv.includes('--without-debug-tools');
const source = fs.readFileSync(path.join(reference, 'index.html'), 'utf8');
const hash = crypto.createHash('sha256').update(source).digest('hex');
if (hash !== '72f3db289f4ecb7e55261ec48cd44b5feaa170e8b4891641245390b473f1cd18')
  throw new Error('Reference changed. Review adapter before regeneration.');
let lines = source.split(/\r?\n/);
const change = (prefix, replacement) => {
  const hits = lines.map((l, i) => l.startsWith(prefix) ? i : -1).filter(i => i >= 0);
  if (hits.length !== 1) throw new Error(`Expected one source line: ${prefix}`);
  lines[hits[0]] = replacement;
};
change('let saved=', `const debugMenuToolsEnabled=${debugToolsEnabled};const saved={};`);
// Public Singleplayer scope: Player Foundation and Super Flat load; the two
// fictional entries remain menu design references.
change('const state=', "const singleplayerWorldIds=new Set(['foundation','super-flat','fictional-forest','fictional-build']);const state={settings:{...defaults},worlds:clone(defaultWorlds).filter(w=>singleplayerWorldIds.has(w.id)),servers:clone(defaultServers),bindings:{},language:'en_us',lastAddress:'',packs:[]};");
const worldDefaults = lines.find(line => line.startsWith('const defaultWorlds='));
change('const defaultWorlds=', worldDefaults.replace('const defaultWorlds=[', "const defaultWorlds=[{id:'foundation',name:'Player Foundation',folder:'Foundation',date:'Java 26.3 baseline lab',mode:'Test',version:'Unreal'},{id:'super-flat',name:'Super Flat',folder:'Super_Flat',date:'Built-in test map',mode:'Test',version:'Unreal'},"));
change('state.settings.skipWarning=', 'state.settings.skipWarning=false;');
change('function persist()', 'function persist(){} // Mock state intentionally never leaves this document.');
change('async function initAudio()', 'function initAudio(){}');
change('let creditScroll=', 'let creditScroll=0,creditKeys={space:false,up:false,control:new Set()};function startMenuMusic(){}');
change('function clickSound()', 'function clickSound(){lastInteraction=Date.now()}');
change('function setSetting(', `function setSetting(id,value){if(liveVideoSettings.has(id))return MenuVideo.set(id,value);const d=D[id];if(!d||(d.values?!d.values.includes(value):!Number.isFinite(value)||value<d.min||value>d.max))throw Error('Invalid mock setting '+id);state.settings[id]=value;if(quality.has(id))state.settings.graphicsPreset='Custom';if(id==='graphicsPreset'){const blur=state.settings.menuBackgroundBlurriness;preset(value);state.settings.menuBackgroundBlurriness=blur}if(id==='highContrast'){state.packs=state.packs.filter(x=>x!=='high_contrast');if(value)state.packs.push('high_contrast')}if(id==='friendsList'&&!value){state.settings.allowFriendRequests=false;state.settings.inGameNotification=false}refreshSettings()}`);
change('function applyAppearance()', `const presentationBaseline=Object.freeze({language:'en_us',guiScale:0,forceUnicodeFont:false,highContrast:false,menuBackgroundBlurriness:defaults.menuBackgroundBlurriness,panoramaSpeed:defaults.panoramaSpeed,panoramaFPS:30,hideSplashTexts:false});function applyAppearance(){gui.classList.remove('high-contrast','uniform-font');const blur=['main','quit'].includes(current.name)?0:state.settings.menuBackgroundBlurriness;$('#panorama').style.filter='blur('+blur+'px)';$('#fallback').style.filter='blur('+blur+'px)'}`);
change('async function toggleFullscreen(', 'function toggleFullscreen(value){setSetting("fullscreen",value)}');
change('function resize()', `function resize(){const focus=document.activeElement?.dataset.controlId;rememberScroll();heldPanoramaDrawn=false;S=Math.max(1,Math.min(state.settings.guiScale||maximumScale(),maximumScale()));W=Math.ceil(innerWidth/S);H=Math.ceil(innerHeight/S);C=Math.floor(W/2);Object.assign(gui.style,{width:W+'px',height:H+'px',transform:'scale('+S+')'});surface.style.width=W+'px';surface.style.height=H+'px';render();if(focus)[...screen.querySelectorAll('[data-control-id]')].find(n=>n.dataset.controlId===focus)?.focus({preventScroll:true})}`);
change('function maximumScale()', `function maximumScale(){let n=1;while(n<32&&innerWidth/(n+1)>=416&&innerHeight/(n+1)>=240)n++;return n}`);
change('function refreshSettings()', `function refreshSettings(){screen.querySelectorAll('[data-setting]').forEach(refreshSettingElement);const reset=$('#hard-reset');if(reset)reset.disabled=!MenuVideo.enabled('fullscreen');const r=$('#restart-warning');if(r)r.style.display=state.settings.preferredGraphicsBackend==='Default'?'none':'block'}`);
change('function go(', `function go(name,data={}){rememberScroll();current.focusID=document.activeElement?.dataset.controlId;stack.push({...current});current={name,data};beginStage(name);render()}`);
change('function back()', `function back(){if(bindingCapture){bindingCapture=null;render();return}if(current.name==='main')return;rememberScroll();rollbackStage();current=stack.pop()||{name:'main',data:{}};render();if(current.focusID){const node=[...screen.querySelectorAll('[data-control-id]')].find(n=>n.dataset.controlId===current.focusID);node?.focus({preventScroll:true})}}`);
change('async function applyLanguage(', `async function applyLanguage(code,close=false){if(!DATA.languages[code])throw Error('Unknown mock locale');state.language=code;chosenLanguage=code;activeLabels=L;document.documentElement.lang='en-US';commitStage();if(close)back();else render()}`);
change('linkconfirm()', `linkconfirm(){title(tr('chat.link.confirmTrusted'),Math.floor(H/2)-54);text(current.data.url,C-155,Math.floor(H/2)-29,310,'center');const y=Math.floor(H/2)+40;button(tr('chat.link.open'),C-154,y,100,()=>message(tr('chat.link.open'),'External navigation is deferred. No browser or account service was opened.'));button(tr('chat.copy'),C-50,y,100,()=>message(tr('chat.copy'),'Clipboard integration is deferred. Nothing was copied.'));button(tr('gui.cancel'),C+54,y,100,back)},`);
change('document.addEventListener(\'fullscreenchange\'', '// Window mode belongs to Unreal; never request DOM fullscreen.');
change('const context=document.modelContext;', '// No browser agent bindings or gameplay services.');
change('if(context?.registerTool)', '');
change('quit(){', '');
change('realms(){', '');
change('realmjoin(){', '');
// Remove the retired preference before source defaults and diagnostics are built.
const dataLine = lines.find(line => line.startsWith('const DATA = '));
const data = JSON.parse(dataLine.slice('const DATA = '.length).replace(/;$/, ''));
// The native assets own the live defaults; these are the matching pre-bridge labels.
data.bindings.find(row => row.id === 'key.sprint').default = 'Left Shift';
data.bindings.find(row => row.id === 'key.sneak').default = 'C';
data.bindings.find(row => row.id === 'key.sneak').label = 'Crouch';
data.labels['key.sneak'] = 'Crouch';
data.definitions.toggleCrouch.label = 'Crouch';
const graphicsAPI = data.definitions.preferredGraphicsBackend;
graphicsAPI.values = ['Default'];
graphicsAPI.displayValues = ['DirectX 12'];
graphicsAPI.default = 'Default';
graphicsAPI.tip = 'Default is DirectX 12. Graphics API switching will be added later.';
delete graphicsAPI.valueKeys;
if (!Object.hasOwn(data.definitions, 'realmsNotifications')) throw new Error('Missing retired Realms preference');
delete data.definitions.realmsNotifications;
change('const DATA = ', `const DATA = ${JSON.stringify(data)};`);
const replace = (before, after) => {
  const joined = lines.join('\n');
  if (!joined.includes(before)) throw new Error(`Missing adaptation anchor: ${before}`);
  lines = joined.replaceAll(before, after).split('\n');
};
const editRenderer = (name, before, after) => {
  const line = lines.find(line => line.startsWith(name));
  if (!line || line.split(before).length !== 2) throw new Error(`Expected one ${name} anchor: ${before}`);
  change(name, line.replace(before, after));
};
editRenderer('editworld(){', 'let newName=w.name;', 'let newName=current.data.worldName??w.name;');
editRenderer('editworld(){', 'if(!w){', "if(!w||['super-flat','foundation'].includes(w.id)){");
editRenderer('editworld(){', 'newName=v;', 'current.data.worldName=newName=v;');
editRenderer('editworld(){', 'persist();back()});save.id=', 'persist();back()},!newName.trim());save.id=');
editRenderer('presets(){', "const f=field(flatEdit.slice().reverse().map(l=>(l.height>1?l.height+'*':'')+l.block).join(',')+';'+flatBiomeEdit,", "const f=field(current.data.presetText??(flatEdit.slice().reverse().map(l=>(l.height>1?l.height+'*':'')+l.block).join(',')+';'+flatBiomeEdit),");
editRenderer('presets(){', 'current.data.presetText||f.value', 'current.data.presetText??f.value');
editRenderer('rules(){', '()=>{draft.rules=', '()=>{if(!ruleDraftsValid())return;draft.rules=');
editRenderer('rules(){', "b.id='rules-done';", "b.id='rules-done';refreshRuleValidation();");
change('function drawRules(){', '');
editRenderer('serveredit(){', 'v=>editServer.name=v', 'v=>{editServer.name=v;refreshServerValidation()}');
editRenderer('serveredit(){', "$('#server-save').disabled=!validAddress(v)", 'refreshServerValidation()');
editRenderer('serveredit(){', "200,()=>{const i=state.servers", "200,()=>{if(!validServerDraft())return;const i=state.servers");
editRenderer('serveredit(){', "editServer.name.trim()||tr('selectServer.defaultName')", 'editServer.name.trim()');
editRenderer('serveredit(){', '!validAddress(editServer.address)', '!validServerDraft()');
// Give each action its real availability; built-in levels are not mock saves.
editRenderer('worlds(){', "!chosenWorld).dataset.worldAction='select'", "!chosenWorld).dataset.worldAction='play'");
// The remaining shared action assignment belongs to Edit/Delete/Re-Create.
editRenderer('worlds(){', "b.dataset.worldAction='select'", "b.dataset.worldAction='reference'");
editRenderer('worlds(){', "if(i<3)b.dataset.worldAction='reference'})", "if(i<3)b.dataset.worldAction='reference'});refreshWorldActions()");
editRenderer('worlds(){', "worlds(){title(", "worlds(){if(!chosenWorld)chosenWorld='super-flat';title(");
editRenderer('function drawWorlds(){', "el('span','world-play',r);", "if(['super-flat','foundation'].includes(w.id))el('span','world-play',r);");
editRenderer('function drawWorlds(){', "w.folder+' ('+w.date+')'", "w.id==='foundation'?'First-person movement, controls and diagnostics':w.id==='super-flat'?'2 km x 2 km - flat test ground':'Design reference - does not load'");
editRenderer('function drawWorlds(){', "w.mode+', '+tr('selectWorld.version')+' '+w.version", "w.id==='foundation'?'Playable - Java 26.3 foundation':w.id==='super-flat'?'Playable - temporary fly camera':w.mode+' - menu design only'");
editRenderer('function drawWorlds(){', "screen.querySelectorAll('[data-world-action]').forEach(e=>e.disabled=false)", 'refreshWorldActions()');
editRenderer('function drawWorlds(){', "screen.querySelectorAll('[data-world-action]').forEach(e=>e.disabled=!chosenWorld)", 'refreshWorldActions()');
change('function loadWorld(){', '// Defined by MenuWorldsRuntime.js.');
editRenderer('function deleteWorld(){', 'if(!w)return;', "if(!w||['super-flat','foundation'].includes(w.id))return;");
replace("if(id==='narrator')return 'speechSynthesis'in window;", "if(id==='narrator')return true;");
replace("button(tr('menu.quit'),C+2,y+96,98,()=>go('quit'))", "const quit=button(tr('menu.quit'),C+2,y+96,98,()=>void MenuApplication.quit(),!MenuApplication.enabled());quit.dataset.applicationQuit=''");
replace("button(tr('menu.online'),C-100,y+48,200,()=>go('realms'));", '');
const mainLine = lines.find(line => line.startsWith('main(){'));
if (!mainLine.includes('y+72') || !mainLine.includes('y+96')) throw new Error('Main menu row anchors changed');
change('main(){', mainLine.replaceAll('y+72', 'y+48').replaceAll('y+96', 'y+72'));
replace("y=heading(tr('options.online.realms.header'),c,y,18);setting('realmsNotifications',0,y,310,c);finishList(c,y+25)", 'finishList(c,y)');
for (const rule of [
  '.realm-feature{position:absolute;width:290px;min-height:49px}',
  '.realm-feature img{position:absolute;width:80px;height:45px;object-fit:cover}',
  '.realm-feature .description{position:absolute;left:96px;top:14px;width:182px;line-height:10px;white-space:pre-wrap}',
  '.realm-feature .feature-title{position:absolute;left:96px;top:1px;width:182px;line-height:10px}'
]) replace(rule, '');
replace("setSetting('fullscreen',!document.fullscreenElement)", "setSetting('fullscreen',!state.settings.fullscreen)");
replace("if(e.key==='F11'){e.preventDefault();", "if(e.key==='F11'||(e.altKey&&e.key==='Enter')){e.preventDefault();if(e.repeat)return;");
replace("const enabled=activeSetting(id),d=D[id]", "const enabled=activeSetting(id)&&(!liveVideoSettings.has(id)||MenuVideo.enabled(id)),d=D[id]");
replace('if(liveVideoSettings.has(id))return MenuVideo.set(id,value);', 'if(liveVideoSettings.has(id))return MenuVideo.set(id,value);if(liveInputPolicies.has(id))return MenuInput.setPolicy(id,value);');
replace('(!liveVideoSettings.has(id)||MenuVideo.enabled(id)),d=D[id]', '(!liveVideoSettings.has(id)||MenuVideo.enabled(id))&&(!liveInputPolicies.has(id)||MenuInput.enabled(id)),d=D[id]');
replace("function activeSetting(id){", "function activeSetting(id){if(id==='preferredGraphicsBackend')return false;");
replace("function settingLabel(id){", "function settingLabel(id){if(id==='preferredGraphicsBackend')return 'Graphics API: '+(window.MenuVideo?.getState()?.observed.defaultGraphicsAPI||'DirectX 12');");
replace("function settingTooltip(id){", "function settingTooltip(id){if(id==='preferredGraphicsBackend')return 'Default is '+(window.MenuVideo?.getState()?.observed.defaultGraphicsAPI||'DirectX 12')+'.\\nCurrently running: '+(window.MenuVideo?.getState()?.observed.graphicsAPI||'Checking...')+'.\\nGraphics API switching will be added later.';");
replace('tip(b,settingTooltip(id));', "if(id==='preferredGraphicsBackend'){const info=pos(el('div','graphics-api-info',parent),x,y,w,20);info.style.position='absolute';b.style.left='0px';b.style.top='0px';b.style.pointerEvents='none';info.append(b);tip(info,settingTooltip(id));}else tip(b,settingTooltip(id));");
replace('e.dataset.tooltip=settingTooltip(id);', "e.dataset.tooltip=settingTooltip(id);if(id==='preferredGraphicsBackend'&&e.parentElement.classList.contains('graphics-api-info'))e.parentElement.dataset.tooltip=e.dataset.tooltip;");
change('keybinds(){', `keybinds(){title(tr('controls.keybinds.title'));const{c}=scroll(33,33,340);let y=0,cat='';for(const b of [...DATA.bindings].sort((a,b)=>['movement','misc','multiplayer','gameplay','inventory','creative','spectator','debug'].indexOf(a.category)-['movement','misc','multiplayer','gameplay','inventory','creative','spectator','debug'].indexOf(b.category)||(a.order||0)-(b.order||0)||a.label.localeCompare(b.label))){if(b.category!==cat){cat=b.category;text(tr('key.category.minecraft.'+cat),0,y+11,340,'center',c).style.textDecoration='underline';y+=20}const supported=MenuInput.supported(b.id),value=state.bindings[b.id];text((b.id==='key.sneak'?'Crouch':tr(b.id))+(supported?'':' (Planned)'),2,y+6,201,'',c);const bind=button(bindingCapture===b.id?'> '+value+' <':value,208,y,75,()=>MenuInput.startCapture(b.id),!MenuInput.enabled(b.id),c);bind.dataset.binding=b.id;bind.dataset.tooltip=supported?'Press a keyboard key or mouse button. Backspace unbinds. Escape cancels.':'This action will be wired when its gameplay system exists.';const reset=button(tr('controls.reset'),288,y,50,()=>void MenuInput.reset(b.id),!MenuInput.enabled(b.id)||value===b.default,c);reset.dataset.bindingReset=b.id;y+=20}finishList(c,y);footerPair(tr('controls.resetAll'),()=>void MenuInput.resetAll().catch(()=>{}),tr('gui.done'),back);screen.querySelector(':scope > button:nth-last-child(2)').disabled=!MenuInput.enabled('key.forward')||DATA.bindings.filter(b=>MenuInput.supported(b.id)).every(b=>state.bindings[b.id]===b.default);},`);
replace("keybinds(){title(tr('controls.keybinds.title'));", "keybinds(){title(tr('controls.keybinds.title'));const inputStatus=text(MenuInput.statusLabel(),0,24,W,'center muted');inputStatus.dataset.inputStatus='true';");
replace("bind.dataset.tooltip=supported?'Press a keyboard key or mouse button. Backspace unbinds. Escape cancels.'", "bind.dataset.tooltip=supported?'Default: '+b.default+'. Press a keyboard key or mouse button. Backspace unbinds. Escape cancels.'");
change("document.addEventListener('pointerdown'", "document.addEventListener('pointerdown',e=>{void initAudio();startMenuMusic();if(bindingCapture)MenuInput.capturePointer(e)},true);");
const captureOld="if(bindingCapture){e.preventDefault();state.bindings[bindingCapture]=e.key==='Escape'?'Not Bound':({Shift:'Left Shift',Control:'Left Control',Alt:'Left Alt',' ':'Space',ArrowUp:'Up',ArrowDown:'Down',ArrowLeft:'Left',ArrowRight:'Right'}[e.key]||e.key.toUpperCase());bindingCapture=null;persist();render();return}";
replace(captureOld, 'if(bindingCapture){MenuInput.captureKey(e);return}');
replace("document.addEventListener('keyup'", "window.addEventListener('blur',()=>{if(bindingCapture){bindingCapture=null;render()}});document.addEventListener('contextmenu',e=>{if(current.name==='keybinds')e.preventDefault()});\ndocument.addEventListener('keyup'");
// Temporary debugging UI; excluded as one unit when debug tools are retired.
replace("y=listSettings(['showAutosaveIndicator','vignette','attackIndicator','chunkSectionFadeInTime'],c,y);finishList(c,y);done()", "y=listSettings(['showAutosaveIndicator','vignette','attackIndicator','chunkSectionFadeInTime'],c,y);if(debugMenuToolsEnabled){y+=10;const reset=button('Hard Reset',55,y,200,()=>confirm('Hard Reset','Restore all settings and menu preferences to their first-run defaults? World saves and player progress will not be deleted.','Hard Reset',()=>void MenuVideo.hardReset()),false,c);reset.id='hard-reset';reset.dataset.debugControl='hard-reset';y+=25}finishList(c,y);done()");
replace('dt*2*state.settings.panoramaSpeed', 'dt*2*presentationBaseline.panoramaSpeed');
replace('Math.min(30,state.settings.framerateLimit===260?60:state.settings.framerateLimit)', 'presentationBaseline.panoramaFPS');
replace("if(current.name!=='quit'&&!document.hidden)spin=", "if(frozenFrame!==null){spin=frozenFrame.angle;t=frozenFrame.phase;lastDraw=-1000}if(frozenFrame===null&&current.name!=='quit'&&!document.hidden)spin=");
replace("fetch('assets/minecraft/texts/credits.json').then(r=>{if(!r.ok)throw Error('Credits unavailable');return r.json()})", 'Promise.resolve(BUNDLED_CREDITS)');
replace("im.src=A+'title/background/panorama_'+i+'.png'", 'im.src=PANORAMA_FACES[i]');
replace("draft.experiments=clone(experimentsEdit);back()", "draft.experiments=clone(experimentsEdit);commitStage();back()");
replace("draft.rules=clone(rulesEdit);back()", "draft.rules=clone(rulesEdit);commitStage();back()");
replace("draft.flatBiome=flatBiomeEdit;back()", "draft.flatBiome=flatBiomeEdit;commitStage();back()");
replace("flatBiomeEdit=biome;back()", "flatBiomeEdit=biome;commitStage();back()");
replace("draft.biome=biomeEdit;back()", "draft.biome=biomeEdit;commitStage();back()");
replace("w.name=newName.trim();persist();back()", "w.name=newName.trim();commitStage();back()");
replace("chosenServer=s.id;persist();back()", "chosenServer=s.id;commitStage();back()");
replace("persist();applyAppearance()}back()", "persist();applyAppearance()}commitStage();back()");
replace("if(v!=='Hardcore')draft.commands=v==='Creative';render()", "if(v==='Hardcore'){draft.difficulty='Hard';draft.commands=false;draft.bonus=false}else draft.commands=v==='Creative';render()");
replace("state.lastAddress=v;persist();$('#direct-connect')", "state.lastAddress=v;$('#direct-connect')");
replace("()=>stopConnection(state.lastAddress)", "()=>{commitStage();stopConnection(state.lastAddress)}");
replace("$('#status').textContent=(screen.querySelector('.text')?.textContent||current.name)+' screen';", "$('#status').textContent=(screen.querySelector('.text')?.textContent||current.name)+' screen';annotateControls();");
replace("<audio id=\"menu-music\" preload=\"none\" aria-hidden=\"true\"></audio>", '');
replace('<meta charset="UTF-8">', `<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src 'self' data:; font-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'none'; media-src 'none'; form-action 'none'; frame-src 'none'; base-uri 'none'"><meta charset="UTF-8">`);
// Desired preferences have their own native owner. Keep video/input and world
// drafts out of this store; do not restore browser-localStorage persistence.
const preferences = preferenceSchema(data, lines);
change('function setSetting(', "function setSetting(id,value){if(liveVideoSettings.has(id))return MenuVideo.set(id,value);if(liveInputPolicies.has(id))return MenuInput.setPolicy(id,value);return MenuPreferences.set(id,value)}");
replace("(!liveInputPolicies.has(id)||MenuInput.enabled(id)),d=D[id]", "(!liveInputPolicies.has(id)||MenuInput.enabled(id))&&(!preferenceSettings.has(id)||MenuPreferences.enabled(id)),d=D[id]");
replace("function activeSetting(id){", "function activeSetting(id){if(id==='soundDevice')return false;");
const originalTooltip = lines.find(line => line.startsWith('function settingTooltip('));
change('function settingTooltip(', originalTooltip.replace('function settingTooltip(', 'function referenceSettingTooltip(') +
  "\nfunction settingTooltip(id){const detail=referenceSettingTooltip(id);const live=['sensitivity','invertXMouse','invertYMouse','mouseWheelSensitivity','discreteMouseScroll','fov','fovEffectScale','autoJump','sprintWindow','toggleAttack','toggleUse'].includes(id);return live?'Applies in Player Foundation; saved automatically.'+(detail?'\\n\\n'+detail:''):preferenceSettings.has(id)?MenuPreferences.tooltip()+(detail?'\\n\\n'+detail:''):detail}");
replace("function refreshSettings(){", "function refreshSettings(){window.MenuPreferences?.refreshAdditional();");
replace("function back(){", "function back(){if(window.MenuPreferences?.isCommitting())return;");
replace("function go(name,data={}){", "function go(name,data={}){if(window.MenuPreferences?.isCommitting())return;");
change('async function applyLanguage(', "async function applyLanguage(code,close=false){if(!DATA.languages[code])throw Error('Unknown locale');return MenuPreferences.commit({language:code},()=>{chosenLanguage=state.language;activeLabels=L;document.documentElement.lang='en-US';commitStage();if(close)back();else render()})}");
replace("if(!data){state.packs=clone(packDraft);state.settings.highContrast=packDraft.includes('high_contrast');persist();applyAppearance()}commitStage();back()", "if(data){commitStage();back()}else void MenuPreferences.commit({packs:clone(packDraft)},()=>{applyAppearance();commitStage();back()})");
replace("state.settings.skipWarning=v;persist()", "void MenuPreferences.setFlag('skipWarning',v)");
replace("ch.setAttribute('aria-label',tr('multiplayerWarning.check'))", "ch.dataset.preferenceFlag='skipWarning';ch.setAttribute('aria-label',tr('multiplayerWarning.check'))");
replace("state.settings.telemetryOptInExtra=v;persist();rememberScroll();render()", "void MenuPreferences.setFlag('telemetryOptInExtra',v).then(()=>{if(current.name==='telemetry'){rememberScroll();render()}}).catch(()=>{})");
replace("consent.setAttribute('aria-label',tr('telemetry_info.opt_in.description'))", "consent.dataset.preferenceFlag='telemetryOptInExtra';consent.setAttribute('aria-label',tr('telemetry_info.opt_in.description'))");
replace("button(tr('gui.done'),C+4,H-26,150,()=>{void applyLanguage(chosenLanguage,true)})", "button(tr('gui.done'),C+4,H-26,150,()=>{void applyLanguage(chosenLanguage,true)}).dataset.preferenceCommit='language'");
replace("else void MenuPreferences.commit({packs:clone(packDraft)},()=>{applyAppearance();commitStage();back()})})", "else void MenuPreferences.commit({packs:clone(packDraft)},()=>{applyAppearance();commitStage();back()})}).dataset.preferenceCommit=data?'':'packs'");
replace("text(tr('options.languageAccuracyWarning'),", "text('Language choice is remembered; translations are planned.',");
replace("text(tr('pack.dropInfo'),", "text(data?tr('pack.dropInfo'):'Pack order is remembered; appearance changes are planned.',");
const runtime = 'const PREFERENCE_SCHEMA=' + JSON.stringify(preferences) + ';\n' +
  ['MenuBridgeRuntime.js', 'MenuMockRuntime.js', 'MenuDialogRuntime.js', 'MenuVideoRuntime.js', 'MenuInputRuntime.js', 'MenuPreferencesRuntime.js', 'MenuWorldsRuntime.js', 'MenuApplicationRuntime.js', 'FoundationMenuRuntime.js'].map(file => fs.readFileSync(path.join(root, 'Scripts', file), 'utf8')).join('\n');
const credits = JSON.parse(fs.readFileSync(path.join(reference, 'assets/minecraft/texts/credits.json'), 'utf8'));
const panorama = Array.from({length:6},(_,i)=>'data:image/png;base64,'+fs.readFileSync(path.join(reference,`assets/minecraft/textures/gui/title/background/panorama_${i}.png`)).toString('base64'));
change('window.MenuPrototype=', `const BUNDLED_CREDITS=${JSON.stringify(credits)};\nconst PANORAMA_FACES=${JSON.stringify(panorama)};\n${runtime}`);
change('resize();loadPanorama();', 'resize();loadPanorama();requestAnimationFrame(animate);');
const output = lines.join('\n');
for (const pattern of [/^realms\(\)\{/m, /^realmjoin\(\)\{/m, /go\('realms'\)/, /go\('realmjoin'\)/, /realm-join/, /\.realm-feature/, /setting\('realmsNotifications'/])
  if (pattern.test(output)) throw new Error(`Retired Realms interface remains: ${pattern}`);
new vm.Script(output.match(/<script>([\s\S]*?)<\/script>/)[1], {filename:'adapted-menu.js'});
for (const pattern of [/localStorage\./,/speechSynthesis\./,/requestFullscreen\(/,/exitFullscreen\(/,/window\.open\(/,/navigator\.clipboard\./,/fetch\(/,/new AudioContext/,/\.play\(/])
  if(pattern.test(output)) throw new Error(`Forbidden runtime effect remains: ${pattern}`);
fs.mkdirSync(path.join(root,'Content/Menu/Web'), {recursive:true});
// Raw runtime data, not an Unreal asset. A .json file here triggers DataTable auto-import.
fs.writeFileSync(path.join(root,'Content/Menu/Web/preferences-schema.menuprefs'), JSON.stringify(preferences,null,2)+'\n');
fs.writeFileSync(path.join(root,'Content/Menu/Web/index.html'), output);
fs.writeFileSync(path.join(root,'Reference/OriginalHashes.json'), JSON.stringify({algorithm:'sha256',index:hash,handoff:crypto.createHash('sha256').update(fs.readFileSync(path.join(root,'Docs/Unnamed_Project_AI_Handoff/Unreal_Menu_Interface_Prompt.txt'))).digest('hex')},null,2)+'\n');
console.log(`Adapted ${output.length} characters; reference unchanged; effect audit passed.`);
