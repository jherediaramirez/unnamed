import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const read = file => fs.readFileSync(new URL('../MenuWeb/Runtime/' + file, import.meta.url), 'utf8');
// The kept title page's module (MenuTitleRuntime.js) with the page globals it resets, a frame queue for
// requestAnimationFrame and stand-ins for MenuApplication and one settings store.
function fixture({bound = true} = {}) {
  const calls = [], frames = [];
  const native = {titleready: async token => { calls.push('ready:' + token); }};
  const context = vm.createContext({
    window: {ue: bound ? {menutitle: native} : {}}, console, clearTimeout,
    setTimeout: (fn, ms) => setTimeout(fn, ms === 100 ? 0 : ms),
    performance: {now: () => context.clock}, clock: 1000,
    requestAnimationFrame: fn => frames.push(fn),
    current: {name: 'generic', data: {title: 'Preparing for world creation...'}}, stack: [{name: 'main'}, {name: 'worlds'}],
    chosenWorld: 'playground', bindingCapture: 'key.jump', scrolls: {worlds: 40}, messageAction: () => {},
    searches: {worlds: 'abc', language: '', rules: '', biomes: ''},
    panoramaLoaded: true, panoramaFallback: false, lastDraw: -1000,
    render: () => calls.push('render:' + context.current.name),
    MenuApplication: {returned: async () => { calls.push('returned'); }, initialize: async () => { calls.push('initialize'); }},
    testStore: {prepareForExit: async () => {}, cancelExit() {}, settled: async () => {}, reload: async () => { calls.push('reload'); }}
  });
  vm.runInContext(read('MenuBridgeRuntime.js'), context);
  vm.runInContext('MenuStores.register(testStore)', context);
  vm.runInContext(read('MenuTitleRuntime.js'), context);
  // Runs one animation frame (every callback queued before it).
  const frame = () => { for (const fn of frames.splice(0)) fn(); };
  return {context, calls, frame, title: context.window.MenuTitle};
}
const flush = () => new Promise(resolve => setTimeout(resolve, 10));
const readies = f => f.calls.filter(c => c.startsWith('ready'));

test('a freshly loaded title reports its first paint once the title and the panorama have drawn, then two frames', async () => {
  const f = fixture(); await flush();
  f.context.current = {name: 'main', data: {}};
  f.frame(); f.frame();
  assert.deepEqual(readies(f), [], 'no panorama drawn yet');
  f.context.lastDraw = 5;
  f.frame(); f.frame(); await flush();
  assert.deepEqual(readies(f), [], 'two frames are not enough');
  f.frame(); await flush();
  assert.deepEqual(readies(f), ['ready:-1']);
});

test('reset brings back a fresh title: main, no history, nothing leaving, stores and worlds read again', async () => {
  const f = fixture(); await flush();
  const resetting = f.title.reset(4);
  assert.equal(f.context.current.name, 'main');
  await resetting;
  const c = f.context;
  assert.deepEqual(JSON.parse(JSON.stringify([c.stack, c.chosenWorld, c.bindingCapture, c.scrolls, c.messageAction])),
    [[], null, null, {}, null]);
  assert.deepEqual(JSON.parse(JSON.stringify(c.searches)), {worlds: '', language: '', rules: '', biomes: ''});
  assert.ok(f.calls.includes('returned') && f.calls.includes('reload'), f.calls.join());
  assert.equal(f.calls.at(-1), 'render:main', 'rendered again after the stores were read');
  // The panorama drawn before the reset does not count: only a draw after it.
  c.lastDraw = 999;
  f.frame(); f.frame(); f.frame(); await flush();
  assert.deepEqual(readies(f), []);
  c.clock = 1100; c.lastDraw = 1050;
  f.frame(); f.frame(); f.frame(); await flush();
  assert.deepEqual(readies(f), ['ready:4']);
});

test('only the latest reset is answered', async () => {
  const f = fixture(); await flush();
  const first = f.title.reset(1);
  const second = f.title.reset(2);
  await Promise.all([first, second]);
  f.context.lastDraw = 2000;
  for (let i = 0; i < 6; i++) f.frame();
  await flush();
  assert.deepEqual(readies(f), ['ready:2']);
});

test('shown re-reads the application state; without the menutitle binding the module stays idle', async () => {
  const f = fixture(); await flush();
  f.title.shown(); await flush();
  assert.ok(f.calls.includes('initialize'));
  const idle = fixture({bound: false});
  idle.context.current = {name: 'main', data: {}};
  idle.context.lastDraw = 5;
  for (let i = 0; i < 4; i++) { idle.frame(); await flush(); }
  await new Promise(resolve => setTimeout(resolve, 50));
  assert.deepEqual(readies(idle), []);
});
