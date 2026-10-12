import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const read = file => fs.readFileSync(new URL('../MenuWeb/Runtime/' + file, import.meta.url), 'utf8');
function fixture({inLab = true, bound = true} = {}) {
  const calls = [], listeners = {};
  let hooks = null;
  const native = {
    getstate: async () => JSON.stringify({inLab, capture: false}),
    setcapture: async active => { calls.push('capture:' + active); },
    resume: async () => { calls.push('resume'); },
    returntomenu: async () => { calls.push('menu'); }
  };
  const context = vm.createContext({
    window: {ue: bound ? {foundationmenu: native} : {}}, console, clearTimeout,
    setTimeout: (fn, ms) => setTimeout(fn, ms === 100 ? 0 : ms),
    document: {addEventListener: (type, fn) => listeners[type] = fn},
    renderers: {}, current: {name: 'main', data: {}}, stack: [{name: 'main'}], bindingCapture: null, C: 200,
    title: () => {}, button: (label, x, y, w, onClick) => calls.push(['button', label, onClick]), go: () => {},
    tr: key => key, render: () => calls.push('render'),
    MenuInput: {setCaptureHooks: next => { hooks = next; }},
    testStore: {prepareForExit: async () => {}, cancelExit() {}, settled: async () => calls.push('settled')}
  });
  vm.runInContext(read('MenuBridgeRuntime.js'), context);
  vm.runInContext('MenuStores.register(testStore)', context);
  return {context, calls, listeners, hooks: () => hooks, load: () => vm.runInContext(read('FoundationMenuRuntime.js'), context)};
}
const flush = () => new Promise(resolve => setTimeout(resolve, 10));

test('in the lab the menu opens as the pause menu and capture reaches the native router', async () => {
  const f = fixture(); f.load(); await flush(); await flush();
  assert.equal(f.context.current.name, 'foundationPause');
  assert.equal(f.context.stack.length, 0);
  await f.hooks().beforeStart('key.forward');
  f.hooks().afterEnd(); await flush();
  assert.deepEqual(f.calls.filter(c => typeof c === 'string' && c.startsWith('capture')), ['capture:true', 'capture:false']);
});

test('Escape and Back to Game settle every store before resuming', async () => {
  const f = fixture(); f.load(); await flush(); await flush();
  f.context.renderers.foundationPause();
  const back = f.calls.find(c => Array.isArray(c) && c[1] === 'Back to Game')[2];
  back(); await flush();
  assert.deepEqual(f.calls.filter(c => c === 'settled' || c === 'resume'), ['settled', 'resume']);
  let stopped = false;
  f.listeners.keydown({key: 'Escape', preventDefault() {}, stopImmediatePropagation() { stopped = true; }}); await flush();
  assert.equal(stopped, true);
  assert.equal(f.calls.filter(c => c === 'resume').length, 2);
});

test('without the native bridge (menu map) the module stays idle and never throws', async () => {
  const f = fixture({bound: false});
  f.context.setTimeout = (fn) => setTimeout(fn, 0);
  f.load(); await new Promise(resolve => setTimeout(resolve, 50));
  assert.equal(f.hooks(), null);
  assert.equal(f.context.current.name, 'main');
});

// A world's #pause page (foundation.Pause.Menu 1 and 2). requestAnimationFrame is queued; frame() runs one frame.
function pauseFixture({hash = '#pause', shown = false} = {}) {
  const calls = [], frames = [], classes = new Set(), styles = [];
  const native = {
    getstate: async () => JSON.stringify({inLab: true, capture: false}),
    setcapture: async () => {}, resume: async () => { calls.push('resume'); }, returntomenu: async () => {},
    menuready: async () => { calls.push('ready'); return JSON.stringify({shown}); },
    menushown: async state => { calls.push('shown:' + state); }
  };
  const context = vm.createContext({
    window: {ue: {foundationmenu: native}, location: {hash}}, console, clearTimeout,
    setTimeout: (fn, ms) => setTimeout(fn, ms === 100 ? 0 : ms),
    requestAnimationFrame: fn => frames.push(fn),
    document: {addEventListener: () => {}, createElement: () => ({}), head: {appendChild: node => styles.push(node.textContent)},
      documentElement: {classList: {add: name => classes.add(name), remove: name => classes.delete(name)}}},
    renderers: {}, current: {name: 'main', data: {}}, stack: [{name: 'main'}], bindingCapture: null, C: 200,
    title: () => {}, button: () => {}, go: () => {}, tr: key => key,
    render: () => calls.push('render:' + context.current.name),
    heldPanoramaDrawn: false, panoramaFallback: false, state: {settings: {guiScale: 3}},
    MenuInput: {setCaptureHooks: () => {}, getState: () => ({nativeKeys: {'key.jump': 'SpaceBar'}})},
    testStore: {prepareForExit: async () => {}, cancelExit() {}, settled: async () => {}, reload: async () => calls.push('reload')}
  });
  vm.runInContext(read('MenuBridgeRuntime.js'), context);
  vm.runInContext('MenuStores.register(testStore)', context);
  vm.runInContext(read('FoundationMenuRuntime.js'), context);
  const frame = async () => { for (const fn of frames.splice(0)) fn(0); await flush(); };
  return {context, calls, classes, styles, frame, reveal: () => vm.runInContext('FoundationMenu.reveal()', context)};
}

test('with #pause the first render is the pause screen, hidden until it has painted', async () => {
  const f = pauseFixture();
  f.context.render();  // the page's resize() at the end of its script
  assert.deepEqual(f.calls.filter(c => c.startsWith('render:')), ['render:foundationPause']);
  assert.equal(f.context.stack.length, 0);
  assert.ok(f.classes.has('pause-hidden'));
  assert.ok(f.styles.some(css => css.includes('html.pause-hidden') && css.includes('transparent')));
});

test('MenuReady is sent once, after the panorama has drawn and two more frames; a pre-warmed page stays hidden', async () => {
  const f = pauseFixture();
  for (let i = 0; i < 5; i++) await f.frame();
  assert.equal(f.calls.filter(c => c === 'ready').length, 0);
  f.context.heldPanoramaDrawn = true;
  await f.frame(); await f.frame();
  assert.equal(f.calls.filter(c => c === 'ready').length, 0);
  await f.frame(); await flush();
  for (let i = 0; i < 5; i++) await f.frame();
  assert.equal(f.calls.filter(c => c === 'ready').length, 1);
  assert.ok(f.classes.has('pause-hidden'));
  assert.equal(f.calls.filter(c => c.startsWith('shown:')).length, 0);
});

test('reveal() returns to the pause screen, reloads the stores, shows the page and then reports MenuShown', async () => {
  const f = pauseFixture();
  f.context.panoramaFallback = true;
  for (let i = 0; i < 4; i++) await f.frame();
  f.context.current = {name: 'options', data: {}};
  f.reveal();
  assert.equal(f.context.current.name, 'foundationPause');
  assert.ok(f.calls.includes('render:foundationPause'));
  assert.ok(f.calls.includes('reload'));
  assert.ok(!f.classes.has('pause-hidden'));
  assert.equal(f.calls.filter(c => c.startsWith('shown:')).length, 0);
  await f.frame();
  assert.deepEqual(f.calls.filter(c => c.startsWith('shown:')), ['shown:{"guiScale":3,"jump":"SpaceBar"}']);
  assert.ok(f.calls.indexOf('reload') < f.calls.indexOf('shown:{"guiScale":3,"jump":"SpaceBar"}'));
});

test('a shown page (cold open) or one revealed while painting shows itself once ready', async () => {
  for (const [shown, revealEarly] of [[true, false], [false, true]]) {
    const f = pauseFixture({shown});
    if (revealEarly) f.reveal();
    assert.ok(f.classes.has('pause-hidden'));
    f.context.heldPanoramaDrawn = true;
    for (let i = 0; i < 4; i++) await f.frame();
    assert.ok(!f.classes.has('pause-hidden'), `shown=${shown} revealEarly=${revealEarly}`);
    await f.frame();
    assert.equal(f.calls.filter(c => c.startsWith('shown:')).length, 1);
  }
});

test('with &bg=world the page drops the panorama for the in-world background and is ready without it', async () => {
  const f = pauseFixture({hash: '#pause&bg=world'});
  assert.ok(f.classes.has('pause-world') && f.classes.has('pause-hidden'));
  const css = f.styles.join('');
  assert.ok(css.includes('html.pause-world #panorama') && css.includes('display:none'));
  assert.ok(css.includes('textures/gui/inworld_menu_background.png') && css.includes('high_contrast/assets/minecraft/textures/gui/inworld_menu_background.png'));
  for (let i = 0; i < 3; i++) await f.frame();
  assert.equal(f.calls.filter(c => c === 'ready').length, 1);
  assert.ok(!pauseFixture().classes.has('pause-world'));
});

test('without #pause a bound page never hides or reports ready (today\'s page)', async () => {
  const f = pauseFixture({hash: ''});
  f.context.heldPanoramaDrawn = true;
  for (let i = 0; i < 4; i++) await f.frame();
  f.reveal(); await flush();
  assert.equal(f.classes.size, 0);
  assert.equal(f.styles.length, 0);
  assert.ok(!f.calls.includes('ready') && !f.calls.includes('reload'));
});