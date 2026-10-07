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
    window: {ue: bound ? {foundation: native} : {}}, console, clearTimeout,
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
