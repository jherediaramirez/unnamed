// Native key bindings (MenuInput): Controls > Key Binds renderer, key/pointer capture,
// and the labels shown before the native snapshot arrives.
export const name = 'Input';

const categories = "['movement','misc','multiplayer','gameplay','inventory','creative','spectator','debug']";

export function apply(patch) {
  patch.data(data => {
    // The native assets own the live defaults; these are the matching pre-bridge labels.
    data.bindings.find(row => row.id === 'key.sprint').default = 'Left Shift';
    data.bindings.find(row => row.id === 'key.sneak').default = 'C';
    // Stable ID key.sneak is saved in players' bindings; only its label says Crouch.
    data.bindings.find(row => row.id === 'key.sneak').label = 'Crouch';
    data.labels['key.sneak'] = 'Crouch';
    data.definitions.toggleCrouch.label = 'Crouch';
  });

  patch.line('keybinds(){', [
    "keybinds(){title(tr('controls.keybinds.title'));",
    "const inputStatus=text(MenuInput.statusLabel(),0,24,W,'center muted');inputStatus.dataset.inputStatus='true';",
    "const{c}=scroll(33,33,340);let y=0,cat='';",
    `for(const b of [...DATA.bindings].sort((a,b)=>${categories}.indexOf(a.category)-${categories}.indexOf(b.category)||(a.order||0)-(b.order||0)||a.label.localeCompare(b.label))){`,
    "if(b.category!==cat){cat=b.category;text(tr('key.category.minecraft.'+cat),0,y+11,340,'center',c).style.textDecoration='underline';y+=20}",
    "const supported=MenuInput.supported(b.id),value=state.bindings[b.id];",
    "text((b.id==='key.sneak'?'Crouch':tr(b.id))+(supported?'':' (Planned)'),2,y+6,201,'',c);",
    "const bind=button(bindingCapture===b.id?'> '+value+' <':value,208,y,75,()=>MenuInput.startCapture(b.id),!MenuInput.enabled(b.id),c);",
    "bind.dataset.binding=b.id;",
    "bind.dataset.tooltip=supported?'Default: '+b.default+'. Press a keyboard key or mouse button. Backspace unbinds. Escape cancels.':'This action will be wired when its gameplay system exists.';",
    "const reset=button(tr('controls.reset'),288,y,50,()=>void MenuInput.reset(b.id),!MenuInput.enabled(b.id)||value===b.default,c);",
    "reset.dataset.bindingReset=b.id;y+=20}",
    "finishList(c,y);footerPair(tr('controls.resetAll'),()=>void MenuInput.resetAll().catch(()=>{}),tr('gui.done'),back);",
    "screen.querySelector(':scope > button:nth-last-child(2)').disabled=!MenuInput.enabled('key.forward')||DATA.bindings.filter(b=>MenuInput.supported(b.id)).every(b=>state.bindings[b.id]===b.default);},",
  ].join(''));

  patch.line("document.addEventListener('pointerdown'", "document.addEventListener('pointerdown',e=>{void initAudio();startMenuMusic();if(bindingCapture)MenuInput.capturePointer(e)},true);");
  patch.text("if(bindingCapture){e.preventDefault();state.bindings[bindingCapture]=e.key==='Escape'?'Not Bound':({Shift:'Left Shift',Control:'Left Control',Alt:'Left Alt',' ':'Space',ArrowUp:'Up',ArrowDown:'Down',ArrowLeft:'Left',ArrowRight:'Right'}[e.key]||e.key.toUpperCase());bindingCapture=null;persist();render();return}",
    'if(bindingCapture){MenuInput.captureKey(e);return}');
  patch.text("document.addEventListener('keyup'",
    "window.addEventListener('blur',()=>{if(bindingCapture){bindingCapture=null;render()}});document.addEventListener('contextmenu',e=>{if(current.name==='keybinds')e.preventDefault()});\ndocument.addEventListener('keyup'");
}
