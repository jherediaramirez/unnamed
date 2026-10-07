// Adapter to the same native menu/settings; no second Controls screen or store.
const FoundationMenu = (() => {
  let bridge = null, inLab = false;
  const invoke = (name, ...args) => Promise.resolve(bridge?.[name.toLowerCase()]?.(...args));
  async function resume(returnToMenu = false) {
    await Promise.all([MenuInput.settled(), MenuPreferences.settled?.(), MenuVideo.settled?.()]);
    await invoke(returnToMenu ? 'ReturnToMenu' : 'Resume');
  }
  renderers.foundationPause = () => {
    title('Game Paused', 30);
    button('Back to Game', C-100, 75, 200, () => void resume());
    button(tr('options.title'), C-100, 100, 200, () => go('options'));
    button('Return to Main Menu', C-100, 125, 200, () => void resume(true));
  };
  async function initialize() {
    for(let i=0;i<100;i++){bridge=window.ue?.foundation;if(bridge)break;await new Promise(r=>setTimeout(r,100));}
    if(!bridge)return;
    const result=await invoke('GetState');const state=typeof result==='string'?JSON.parse(result):result;inLab=!!state?.inLab;
    if(inLab){current={name:'foundationPause',data:{}};stack=[];render();}
  }
  document.addEventListener('keydown', e => {
    if(inLab&&current.name==='foundationPause'&&e.key==='Escape'&&!bindingCapture){e.preventDefault();e.stopImmediatePropagation();void resume();}
  },true);
  const oldCapture = MenuInput.startCapture;
  MenuInput.startCapture = async id => {if(!MenuInput.enabled(id))return;await invoke('SetCapture',true);oldCapture(id);};
  const oldKey = MenuInput.captureKey, oldPointer = MenuInput.capturePointer;
  MenuInput.captureKey = e => {oldKey(e);if(!bindingCapture)void invoke('SetCapture',false);};
  MenuInput.capturePointer = e => {oldPointer(e);if(!bindingCapture)void invoke('SetCapture',false);};
  window.addEventListener('blur',()=>void invoke('SetCapture',false));
  document.addEventListener('wheel',e=>{if(bindingCapture){e.preventDefault();e.stopImmediatePropagation();const id=bindingCapture;bindingCapture=null;void MenuInput.bindWheel(id,e.deltaY<0?'MouseScrollUp':'MouseScrollDown');void invoke('SetCapture',false);}},{capture:true,passive:false});
  return {initialize};
})();
void FoundationMenu.initialize();
