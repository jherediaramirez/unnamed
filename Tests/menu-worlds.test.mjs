import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const read = file => fs.readFileSync(new URL('../MenuWeb/Runtime/' + file, import.meta.url), 'utf8');
function fixture({canQuit = true, saveFailure = false} = {}) {
  const calls = [], notices = [], intervals = [];
  let requested = false;
  const reply = () => ({ok: !(requested && saveFailure), canQuit, pendingQuit: false, quitIssued: false,
    canOpenWorld: true, pendingWorld: false, travelIssued: requested && !saveFailure, error: requested && saveFailure ? 'Save failed.' : '',
    worlds: [{id: 'foundation', name: 'Player Foundation', summary: 'Lab', status: 'Playable'},
      {id: 'super-flat', name: 'Super Flat', summary: 'Flat', status: 'Playable'}]});
  const context = vm.createContext({
    window: {ue: {menuapplication: {getstate: async () => reply(), requestopenworld: async id => {calls.push('open:' + id); requested = true; return reply();}}}},
    screen: {querySelectorAll: () => []}, clone: structuredClone, console, Date, clearTimeout,
    state: {worlds: [{id: 'fictional-forest', name: 'Forest'}, {id: 'fictional-build', name: 'Build'}]},
    current: {name: 'main'}, drawWorlds: () => calls.push('draw'),
    setTimeout: (fn, ms) => setTimeout(fn, ms >= 20000 ? 60 : ms >= 5000 ? 40 : ms), setInterval: fn => intervals.push(fn),
    message: (...args) => notices.push(args),
    MenuVideo: {prepareForExit: async () => {calls.push('video'); await new Promise(resolve => setTimeout(resolve, 5)); calls.push('video-saved');}, cancelExit: () => calls.push('video-unlock')},
  });
  for (const name of ['MenuInput', 'MenuPreferences']) context.window[name] = {
    prepareForExit: async () => calls.push(name + '-saved'), cancelExit: () => calls.push(name + '-unlock')};
  vm.runInContext(read('MenuBridgeRuntime.js'), context);
  // Script order registers video, input, then preferences.
  vm.runInContext('for (const store of [MenuVideo, window.MenuInput, window.MenuPreferences]) MenuStores.register(store)', context);
  vm.runInContext(read('MenuApplicationRuntime.js'), context);
  return {api: context.window.MenuApplication, context, calls, notices, intervals};
}

test('only catalog worlds travel, after all three settings services drain, once', async () => {
  const f = fixture(); await f.api.initialize();
  for (const id of ['fictional-forest', 'fictional-build', '/Game/Maps/L_SuperFlat', 'super-flat?listen']) await f.api.openWorld(id);
  assert.deepEqual(f.calls.filter(c => c !== 'draw'), []);
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
  f.context.window.ue.menuapplication.getstate = () => new Promise(resolve => finish = resolve);
  const poll = f.intervals[0](); await Promise.resolve();
  await f.api.openWorld('super-flat'); finish(before); await poll;
  assert.equal(f.api.getState().travelIssued, true);
});

test('native catalog leads the world list and is the only source of playable IDs', async () => {
  const f = fixture(); await f.api.initialize();
  assert.deepEqual([...f.context.state.worlds.map(w => w.id)], ['foundation', 'super-flat', 'fictional-forest', 'fictional-build']);
  assert.deepEqual([...f.context.state.worlds.map(w => !!w.builtIn)], [true, true, false, false]);
  assert.equal(f.api.defaultWorld(), 'foundation');
  assert.equal(f.api.isPlayable('super-flat'), true);
  for (const id of ['fictional-forest', 'Super-Flat', '/Game/Maps/L_SuperFlat', '']) assert.equal(f.api.isPlayable(id), false);
});

test('a rolled-back world list regains the built-in worlds on the next reply', async () => {
  const f = fixture(); await f.api.initialize();
  f.context.state.worlds = f.context.state.worlds.filter(w => !w.builtIn);
  f.context.current.name = 'worlds';
  await f.intervals[0]();
  assert.equal(f.context.state.worlds.filter(w => w.builtIn).length, 2);
  assert.equal(f.calls.at(-1), 'draw');
});