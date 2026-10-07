// Offline host baseline: no audio, network, browser storage or browser-agent hooks;
// embedded credits/panorama; staged drafts; focus-preserving navigation.
export const name = 'Core';

export function apply(patch, {debugTools}) {
  patch.line('let saved=', `const debugMenuToolsEnabled=${debugTools};const saved={};`);
  patch.line('state.settings.skipWarning=', 'state.settings.skipWarning=false;');
  patch.line('function persist()', 'function persist(){} // Mock state intentionally never leaves this document.');
  patch.line('async function initAudio()', 'function initAudio(){}');
  patch.line('let creditScroll=', 'let creditScroll=0,creditKeys={space:false,up:false,control:new Set()};function startMenuMusic(){}');
  patch.line('function clickSound()', 'function clickSound(){lastInteraction=Date.now()}');
  patch.line('function applyAppearance()', `const presentationBaseline=Object.freeze({language:'en_us',guiScale:0,forceUnicodeFont:false,highContrast:false,menuBackgroundBlurriness:defaults.menuBackgroundBlurriness,panoramaSpeed:defaults.panoramaSpeed,panoramaFPS:30,hideSplashTexts:false});function applyAppearance(){gui.classList.remove('high-contrast','uniform-font');const blur=['main','quit'].includes(current.name)?0:state.settings.menuBackgroundBlurriness;$('#panorama').style.filter='blur('+blur+'px)';$('#fallback').style.filter='blur('+blur+'px)'}`);

  // Navigation remembers focus and stages dialog drafts so Back can roll them back.
  patch.edit('function go(', 'rememberScroll();stack.push({...current});current={name,data};render()}',
    'rememberScroll();current.focusID=document.activeElement?.dataset.controlId;stack.push({...current});current={name,data};beginStage(name);render()}');
  patch.edit('function back()', "rememberScroll();current=stack.pop()||{name:'main',data:{}};render()}",
    "rememberScroll();rollbackStage();current=stack.pop()||{name:'main',data:{}};render();if(current.focusID){const node=[...screen.querySelectorAll('[data-control-id]')].find(n=>n.dataset.controlId===current.focusID);node?.focus({preventScroll:true})}}");

  patch.line('linkconfirm()', `linkconfirm(){title(tr('chat.link.confirmTrusted'),Math.floor(H/2)-54);text(current.data.url,C-155,Math.floor(H/2)-29,310,'center');const y=Math.floor(H/2)+40;button(tr('chat.link.open'),C-154,y,100,()=>message(tr('chat.link.open'),'External navigation is deferred. No browser or account service was opened.'));button(tr('chat.copy'),C-50,y,100,()=>message(tr('chat.copy'),'Clipboard integration is deferred. Nothing was copied.'));button(tr('gui.cancel'),C+54,y,100,back)},`);
  patch.line('const context=document.modelContext;', '// No browser agent bindings or gameplay services.');
  patch.line('if(context?.registerTool)', '');
  patch.text("if(id==='narrator')return 'speechSynthesis'in window;", "if(id==='narrator')return true;");

  // Presentation uses the reference baseline; a capture can freeze the panorama.
  patch.text('dt*2*state.settings.panoramaSpeed', 'dt*2*presentationBaseline.panoramaSpeed');
  patch.text('Math.min(30,state.settings.framerateLimit===260?60:state.settings.framerateLimit)', 'presentationBaseline.panoramaFPS');
  patch.text("if(current.name!=='quit'&&!document.hidden)spin=", "if(frozenFrame!==null){spin=frozenFrame.angle;t=frozenFrame.phase;lastDraw=-1000}if(frozenFrame===null&&current.name!=='quit'&&!document.hidden)spin=");
  // file:// cannot fetch or upload local images to WebGL; both are embedded by AdaptMenu.
  patch.text("fetch('assets/minecraft/texts/credits.json').then(r=>{if(!r.ok)throw Error('Credits unavailable');return r.json()})", 'Promise.resolve(BUNDLED_CREDITS)');
  patch.text("im.src=A+'title/background/panorama_'+i+'.png'", 'im.src=PANORAMA_FACES[i]');
  patch.line('resize();loadPanorama();', 'resize();loadPanorama();requestAnimationFrame(animate);');

  patch.text("$('#status').textContent=(screen.querySelector('.text')?.textContent||current.name)+' screen';", "$('#status').textContent=(screen.querySelector('.text')?.textContent||current.name)+' screen';annotateControls();");
  patch.text('<audio id="menu-music" preload="none" aria-hidden="true"></audio>', '');
  patch.text('<meta charset="UTF-8">', `<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src 'self' data:; font-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'none'; media-src 'none'; form-action 'none'; frame-src 'none'; base-uri 'none'"><meta charset="UTF-8">`);
}

export function verify(output) {
  for (const pattern of [/localStorage\./, /speechSynthesis\./, /requestFullscreen\(/, /exitFullscreen\(/, /window\.open\(/,
    /navigator\.clipboard\./, /fetch\(/, /new AudioContext/, /\.play\(/])
    if (pattern.test(output)) throw new Error(`Forbidden runtime effect remains: ${pattern}`);
}
