const liveInputPolicies = new Set(['toggleSprint', 'toggleCrouch']);
// Native key bindings and Hold/Toggle policies through the menuinput service. A world that must
// know about key capture (Foundation's input router) registers setCaptureHooks.
const MenuInput = (() => {
  const client = MenuBridge.client('menuinput', {service: 'input settings', unavailable: 'The input settings connection is unavailable.',
    validate: reply => !!reply && typeof reply.ok === 'boolean' && typeof reply.ready === 'boolean' && !!reply.bindings &&
      !!reply.defaults && !!reply.nativeKeys && !!reply.policies && Array.isArray(reply.supported)});
  let snapshot = null, running = null, locked = false, ready = false;
  let hooks = {beforeStart: null, afterEnd: null};
  function absorb(reply) {
    snapshot = reply; ready = reply.ready;
    Object.assign(state.bindings, reply.bindings);
    Object.assign(state.settings, reply.policies);
    for (const row of DATA.bindings) if (Object.hasOwn(reply.defaults, row.id)) row.default = reply.defaults[row.id];
    if (current.name === 'keybinds') render(); else refreshSettings();
  }
  const call = (name, ...args) => client.call(name, args);
  // Every way a capture can end (key, button, wheel, Escape, Backspace, blur) reports here.
  const endCapture = () => hooks.afterEnd?.();
  function enabled(id) { return ready && !locked && !running && (liveInputPolicies.has(id) || snapshot.supported.includes(id)); }
  function supported(id) { return !!snapshot?.supported.includes(id); }
  async function mutate(name, ...args) {
    if (locked || running || !ready) return;
    const work = (async () => {
      try {
        const reply = await call(name, ...args);
        absorb(reply);
        if (!reply.ok || reply.error) throw Error(reply.error || 'Input settings could not be changed.');
      } catch (error) {
        try { absorb(await call('GetState')); } catch {}
        message('Key Bindings', error.message);
        throw error;
      }
    })();
    running = work;
    refreshSettings(); if (current.name === 'keybinds') render();
    try { await work; }
    finally { running = null; refreshSettings(); if (current.name === 'keybinds') render(); }
  }
  function submit(name, ...args) { return mutate(name, ...args).catch(() => {}); }
  function keyFromEvent(event) {
    const code = event.code;
    if (/^Key[A-Z]$/.test(code)) return code.slice(3);
    const digits = ['Zero','One','Two','Three','Four','Five','Six','Seven','Eight','Nine'];
    if (/^Digit[0-9]$/.test(code)) return digits[Number(code.slice(5))];
    if (/^F([1-9]|1[0-9]|2[0-4])$/.test(code)) return code;
    const physical = ({Space:'SpaceBar',ShiftLeft:'LeftShift',ShiftRight:'RightShift',ControlLeft:'LeftControl',ControlRight:'RightControl',
      AltLeft:'LeftAlt',AltRight:'RightAlt',ArrowUp:'Up',ArrowDown:'Down',ArrowLeft:'Left',ArrowRight:'Right',Enter:'Enter',Tab:'Tab',
      Delete:'Delete',Insert:'Insert',Home:'Home',End:'End',PageUp:'PageUp',PageDown:'PageDown',CapsLock:'CapsLock',Minus:'Hyphen',
      Equal:'Equals',BracketLeft:'LeftBracket',BracketRight:'RightBracket',Backslash:'Backslash',Semicolon:'Semicolon',
      Quote:'Apostrophe',Comma:'Comma',Period:'Period',Slash:'Slash',Backquote:'Tilde'})[code];
    if (physical) return physical;
    // UE's Windows CEF adapter sends the virtual key but omits the native scan code.
    // Consequently real Slate key events have an empty DOM code. Use the same
    // Windows virtual-key identities Unreal sends, rather than localized characters.
    const vk = event.keyCode || event.which;
    if (vk >= 65 && vk <= 90) return String.fromCharCode(vk);
    if (vk >= 48 && vk <= 57) return digits[vk - 48];
    if (vk >= 112 && vk <= 135) return 'F' + (vk - 111);
    if (vk >= 96 && vk <= 105) return ['NumPadZero','NumPadOne','NumPadTwo','NumPadThree','NumPadFour','NumPadFive','NumPadSix','NumPadSeven','NumPadEight','NumPadNine'][vk - 96];
    const side = event.location === 2 ? 'Right' : 'Left';
    if (vk === 16) return side + 'Shift';
    if (vk === 17) return side + 'Control';
    if (vk === 18) return side + 'Alt';
    return ({13:'Enter',19:'Pause',20:'CapsLock',32:'SpaceBar',33:'PageUp',34:'PageDown',35:'End',36:'Home',
      37:'Left',38:'Up',39:'Right',40:'Down',45:'Insert',46:'Delete',9:'Tab',
      106:'Multiply',107:'Add',109:'Subtract',110:'Decimal',111:'Divide',144:'NumLock',145:'ScrollLock',
      186:'Semicolon',187:'Equals',188:'Comma',189:'Hyphen',190:'Period',191:'Slash',192:'Tilde',
      219:'LeftBracket',220:'Backslash',221:'RightBracket',222:'Apostrophe'})[vk] || null;
  }
  function captureKey(event) {
    event.preventDefault(); event.stopImmediatePropagation();
    if (event.repeat) return;
    if (event.key === 'Escape') { bindingCapture = null; render(); return; }
    const id = bindingCapture, key = event.key === 'Backspace' ? 'None' : keyFromEvent(event);
    bindingCapture = null;
    if (!key) { render(); message('Key Bindings', 'That key is not supported yet. Choose another keyboard key or mouse button.'); return; }
    void submit(!event.code && key !== 'None' ? 'ApplyCapturedKey' : 'ApplyBinding', id, key);
  }
  function captureKeyAndReport(event) { captureKey(event); if (!bindingCapture) endCapture(); }
  function capturePointer(event) {
    event.preventDefault(); event.stopImmediatePropagation();
    const key = ['LeftMouseButton','MiddleMouseButton','RightMouseButton','ThumbMouseButton','ThumbMouseButton2'][event.button];
    const id = bindingCapture; bindingCapture = null;
    if (key) void submit('ApplyBinding', id, key); else render();
    endCapture();
  }
  function captureWheel(event) {
    event.preventDefault(); event.stopImmediatePropagation();
    const id = bindingCapture; bindingCapture = null;
    if (enabled(id)) void submit('ApplyBinding', id, event.deltaY < 0 ? 'MouseScrollUp' : 'MouseScrollDown');
    endCapture();
  }
  document.addEventListener('wheel', event => { if (bindingCapture) captureWheel(event); }, {capture: true, passive: false});
  window.addEventListener('blur', endCapture);
  async function initialize() {
    if (!await client.connect()) return;
    try { absorb(await call('GetState')); }
    catch (error) { console.warn('Input settings: ' + error.message); }
  }
  return {initialize, enabled, supported, keyFromEvent, captureKey: captureKeyAndReport, capturePointer,
    setPolicy: (id, value) => enabled(id) ? submit('ApplyPolicy', id, value) : Promise.resolve(),
    reset: id => enabled(id) ? submit('ResetBinding', id) : Promise.resolve(),
    unbind: id => enabled(id) ? submit('ApplyBinding', id, 'None') : Promise.resolve(),
    resetAll: async (resetPolicies = false) => {
      if (running) await running;
      if (!ready || locked) throw Error('Input settings are unavailable. Please try again.');
      return mutate('ResetBindings', resetPolicies);
    },
    // Capture begins once beforeStart (if any) has settled, so the native side knows first.
    startCapture: id => {
      if (!enabled(id)) return;
      const begin = () => { if (enabled(id)) { bindingCapture = id; render(); } };
      if (!hooks.beforeStart) return void begin();
      return Promise.resolve(hooks.beforeStart(id)).then(begin);
    },
    setCaptureHooks: next => { hooks = {beforeStart: null, afterEnd: null, ...next}; },
    getState: () => snapshot ? clone(snapshot) : null,
    statusLabel: () => !ready ? 'Input unavailable - restart Unreal after rebuilding' :
      (snapshot.build || 'Input') + ' - ' + (snapshot.persistent ? 'Changes save automatically' : 'Session only; changes do not save'),
    settled: async () => { if (running) await running; return snapshot ? clone(snapshot) : null; },
    prepareForExit: async () => { locked = true; bindingCapture = null; refreshSettings(); if (running) await running; },
    cancelExit: () => { locked = false; refreshSettings(); }
  };
})();
window.MenuInput = MenuInput;
MenuStores.register(MenuInput);
void MenuInput.initialize();
