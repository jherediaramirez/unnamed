// Native video settings (MenuVideo): fullscreen, frame limit, VSync, GUI scale, blur,
// the read-only Graphics API row and the temporary Hard Reset debugging control.
export const name = 'Video';

export function apply(patch) {
  patch.line('async function toggleFullscreen(', 'function toggleFullscreen(value){setSetting("fullscreen",value)}');
  patch.line("document.addEventListener('fullscreenchange'", '// Window mode belongs to Unreal; never request DOM fullscreen.');
  patch.text("setSetting('fullscreen',!document.fullscreenElement)", "setSetting('fullscreen',!state.settings.fullscreen)");
  patch.text("if(e.key==='F11'){e.preventDefault();", "if(e.key==='F11'||(e.altKey&&e.key==='Enter')){e.preventDefault();if(e.repeat)return;");
  patch.edit('function refreshSettings()', "const r=$('#restart-warning');",
    "const reset=$('#hard-reset');if(reset)reset.disabled=!MenuVideo.enabled('fullscreen');const r=$('#restart-warning');");

  // GUI scale: integer CSS scaling, at most 32, never below a 416x240 virtual screen.
  patch.line('function resize()', `function resize(){const focus=document.activeElement?.dataset.controlId;rememberScroll();heldPanoramaDrawn=false;S=Math.max(1,Math.min(state.settings.guiScale||maximumScale(),maximumScale()));W=Math.ceil(innerWidth/S);H=Math.ceil(innerHeight/S);C=Math.floor(W/2);Object.assign(gui.style,{width:W+'px',height:H+'px',transform:'scale('+S+')'});surface.style.width=W+'px';surface.style.height=H+'px';render();if(focus)[...screen.querySelectorAll('[data-control-id]')].find(n=>n.dataset.controlId===focus)?.focus({preventScroll:true})}`);
  patch.line('function maximumScale()', `function maximumScale(){let n=1;while(n<32&&innerWidth/(n+1)>=416&&innerHeight/(n+1)>=240)n++;return n}`);

  // Graphics API is informational: it reports the configured default and the running RHI.
  patch.data(data => {
    const graphicsAPI = data.definitions.preferredGraphicsBackend;
    graphicsAPI.values = ['Default'];
    graphicsAPI.displayValues = ['DirectX 12'];
    graphicsAPI.default = 'Default';
    graphicsAPI.tip = 'Default is DirectX 12. Graphics API switching will be added later.';
    delete graphicsAPI.valueKeys;
  });
  patch.edit('function activeSetting(', "if(id==='maxAnisotropyBit')", "if(id==='preferredGraphicsBackend')return false;if(id==='maxAnisotropyBit')");
  patch.edit('function settingTooltip(', 'const d=D[id],v=state.settings[id];', "if(id==='preferredGraphicsBackend')return 'Default is '+(window.MenuVideo?.getState()?.observed.defaultGraphicsAPI||'DirectX 12')+'.\\nCurrently running: '+(window.MenuVideo?.getState()?.observed.graphicsAPI||'Checking...')+'.\\nGraphics API switching will be added later.';const d=D[id],v=state.settings[id];");
  patch.text('function settingLabel(id){',"function settingLabel(id){if(id==='preferredGraphicsBackend')return 'Graphics API: '+(window.MenuVideo?.getState()?.observed.defaultGraphicsAPI||'DirectX 12');");
  patch.text('tip(b,settingTooltip(id));', "if(id==='preferredGraphicsBackend'){const info=pos(el('div','graphics-api-info',parent),x,y,w,20);info.style.position='absolute';b.style.left='0px';b.style.top='0px';b.style.pointerEvents='none';info.append(b);tip(info,settingTooltip(id));}else tip(b,settingTooltip(id));");
  patch.text('e.dataset.tooltip=settingTooltip(id);', "e.dataset.tooltip=settingTooltip(id);if(id==='preferredGraphicsBackend'&&e.parentElement.classList.contains('graphics-api-info'))e.parentElement.dataset.tooltip=e.dataset.tooltip;");

  // Temporary debugging UI; excluded as one unit by --without-debug-tools.
  patch.text("y=listSettings(['showAutosaveIndicator','vignette','attackIndicator','chunkSectionFadeInTime'],c,y);finishList(c,y);done()", "y=listSettings(['showAutosaveIndicator','vignette','attackIndicator','chunkSectionFadeInTime'],c,y);if(debugMenuToolsEnabled){y+=10;const reset=button('Hard Reset',55,y,200,()=>confirm('Hard Reset','Restore all settings and menu preferences to their first-run defaults? World saves and player progress will not be deleted.','Hard Reset',()=>void MenuVideo.hardReset()),false,c);reset.id='hard-reset';reset.dataset.debugControl='hard-reset';y+=25}finishList(c,y);done()");
}
