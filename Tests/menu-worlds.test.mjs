import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const read = file => fs.readFileSync(new URL('../Scripts/' + file, import.meta.url), 'utf8');
function fixture({canQuit = true, saveFailure = false} = {}) {
  const calls = [], notices = [], intervals = [];
  let requested = false;
  const reply = () => ({ok: !(requested && saveFailure), canQuit, pendingQuit: false, quitIssued: false,
    canOpenWorld: true, pendingWorld: false, travelIssued: requested && !saveFailure, error: requested && saveFailure ? 'Save failed.' : ''});
  const context = vm.createContext({
    window: {ue: {menuapp: {getstate: async () => reply(), requestopenworld: async id => {calls.push('open:' + id); requested = true; return reply();}}}},
    screen: {querySelectorAll: () => []}, clone: structuredClone, console, Date, clearTimeout,
    setTimeout: (fn, ms) => setTimeout(fn, ms >= 20000 ? 60 : ms >= 5000 ? 40 : ms), setInterval: fn => intervals.push(fn),
    message: (...args) => notices.push(args),
    MenuVideo: {prepareForExit: async () => {calls.push('video'); await new Promise(resolve => setTimeout(resolve, 5)); calls.push('video-saved');}, cancelExit: () => calls.push('video-unlock')},
  });
  for (const name of ['MenuInput', 'MenuPreferences']) context.window[name] = {
    prepareForExit: async () => calls.push(name + '-saved'), cancelExit: () => calls.push(name + '-unlock')};
  vm.runInContext(read('MenuBridgeRuntime.js'), context);
  vm.runInContext(read('MenuApplicationRuntime.js'), context);
  return {api: context.window.MenuApplication, context, calls, notices, intervals};
}

test('only Super Flat travels, after all three settings services drain, once', async () => {
  const f = fixture(); await f.api.initialize();
  for (const id of ['fictional-forest', 'fictional-build', '/Game/Maps/L_SuperFlat', 'super-flat?listen']) await f.api.openWorld(id);
  assert.deepEqual(f.calls, []);
  const request = f.api.openWorld('super-flat');
  await f.api.openWorld('super-flat'); await f.api.quit(); await request;
  assert.equal(f.calls.at(-1), 'open:super-flat');
  assert.equal(f.calls.filter(c => c.startsWith('open:')).length, 1);
  assert.ok(f.calls.includes('MenuInput-saved') && f.calls.includes('MenuPreferences-saved') && f.calls.includes('video-saved'));
  assert.equal(f.api.enabled(), false);
  assert.equal(f.api.worldEnabled('super-flat'), false);
  assert.deepEqual(f.notices, []);
});

test('PIE can open the test level while editor Quit remains disabled', async () => {
  const f = fixture({canQuit: false}); await f.api.initialize();
  assert.equal(f.api.enabled(), false); assert.equal(f.api.worldEnabled('super-flat'), true);
  await f.api.openWorld('super-flat'); assert.equal(f.calls.at(-1), 'open:super-flat');
});

test('failed save leaves the menu and all setting stores usable', async () => {
  const f = fixture({saveFailure: true}); await f.api.initialize(); await f.api.openWorld('super-flat');
  assert.deepEqual(f.calls.slice(-3), ['video-unlock', 'MenuInput-unlock', 'MenuPreferences-unlock']);
  assert.equal(f.api.isLoading(), false); assert.equal(f.api.worldEnabled('super-flat'), true);
  assert.equal(f.notices.at(-1)[1], 'Save failed.');
});

test('unanswered settings drain never launches late after its deadline', async () => {
  const f = fixture(); await f.api.initialize(); let finish;
  f.context.window.MenuPreferences.prepareForExit = () => new Promise(resolve => finish = resolve);
  await f.api.openWorld('super-flat'); finish(); await Promise.resolve(); await Promise.resolve();
  assert.equal(f.calls.some(c => c.startsWith('open:')), false);
  assert.equal(f.api.worldEnabled('super-flat'), true);
  assert.match(f.notices.at(-1)[1], /Settings did not finish/);
});

test('late pre-travel poll cannot overwrite the accepted travel', async () => {
  const f = fixture(); await f.api.initialize(); const before = f.api.getState(); let finish;
  f.context.window.ue.menuapp.getstate = () => new Promise(resolve => finish = resolve);
  const poll = f.intervals[0](); await Promise.resolve();
  await f.api.openWorld('super-flat'); finish(before); await poll;
  assert.equal(f.api.getState().travelIssued, true);
});
