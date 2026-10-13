import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const read = file => fs.readFileSync(new URL('../MenuWeb/Runtime/' + file, import.meta.url), 'utf8');
const playground = {id: 'playground', name: 'Playground', summary: '2 km dev grid: movement course, building, mining and combat',
  status: 'Playable - Java 26.3 foundation, session only'};
const helloDraft = {name: 'Hello', seed: 'hello', gameMode: 'Survival', difficulty: 'Normal', type: 'Default'};
function fixture({canQuit = true, saveFailure = false, createError = '', waitForPaint} = {}) {
  const calls = [], notices = [], intervals = [], screens = [], frames = [];
  let requested = false;
  // Native's saves, most recently played first.
  let saved = [{id: 'hello', name: 'Hello', summary: 'hello (10/8/26, 4:30 PM)', status: 'Survival Mode', draft: helloDraft}];
  const reply = (error = '') => ({ok: !(requested && saveFailure) && !error, canQuit, pendingQuit: false, quitIssued: false,
    canOpenWorld: true, pendingWorld: false, travelIssued: requested && !saveFailure && !error,
    error: error || (requested && saveFailure ? 'Save failed.' : ''), worlds: [playground], savedWorlds: structuredClone(saved),
    ...(waitForPaint === undefined ? {} : {waitForPaint})});
  const native = {
    getstate: async () => reply(),
    requestopenworld: async id => { calls.push('open:' + id); requested = true; return reply(); },
    createworld: async json => {
      calls.push('create:' + JSON.parse(json).name);
      if (createError) return reply(createError);
      saved = [{id: 'new-world', name: JSON.parse(json).name, summary: 'new-world', status: 'Survival Mode', draft: JSON.parse(json)}, ...saved];
      requested = true;
      return reply();
    },
    deleteworld: async id => { calls.push('delete:' + id); saved = saved.filter(w => w.id !== id); return reply(); },
    renameworld: async (id, name) => {
      calls.push('rename:' + id + ':' + name);
      saved = saved.map(w => w.id === id ? {...w, name} : w);
      return reply();
    },
  };
  const context = vm.createContext({
    window: {ue: {menuapplication: native}},
    screen: {querySelectorAll: () => []}, clone: structuredClone, console, Date, clearTimeout,
    state: {worlds: []}, current: {name: 'main'}, drawWorlds: () => calls.push('draw'),
    setTimeout: (fn, ms) => setTimeout(fn, ms >= 20000 ? 60 : ms >= 5000 ? 40 : ms), setInterval: fn => intervals.push(fn),
    message: (...args) => notices.push(args), tr: key => key,
    go: (name, data) => { screens.push([name, data.title]); context.current = {name, data}; }, back: () => screens.push(['back']),
    requestAnimationFrame: fn => frames.push(fn),
    MenuVideo: {prepareForExit: async () => {calls.push('video'); await new Promise(resolve => setTimeout(resolve, 5)); calls.push('video-saved');}, cancelExit: () => calls.push('video-unlock')},
  });
  for (const name of ['MenuInput', 'MenuPreferences']) context.window[name] = {
    prepareForExit: async () => calls.push(name + '-saved'), cancelExit: () => calls.push(name + '-unlock')};
  vm.runInContext(read('MenuBridgeRuntime.js'), context);
  // Script order registers video, input, then preferences.
  vm.runInContext('for (const store of [MenuVideo, window.MenuInput, window.MenuPreferences]) MenuStores.register(store)', context);
  vm.runInContext(read('MenuApplicationRuntime.js'), context);
  // Runs one animation frame (every callback queued before it).
  const frame = () => { for (const fn of frames.splice(0)) fn(); };
  return {api: context.window.MenuApplication, context, calls, notices, intervals, native, screens, frame};
}

test('only listed worlds travel, after all three settings services drain, once', async () => {
  const f = fixture(); await f.api.initialize();
  for (const id of ['fictional-forest', 'fictional-build', '/Game/Maps/L_Playground', 'playground?listen', '../hello']) await f.api.openWorld(id);
  assert.deepEqual(f.calls.filter(c => c !== 'draw'), []);
  const request = f.api.openWorld('playground');
  await f.api.openWorld('playground'); await f.api.quit(); await request;
  assert.equal(f.calls.at(-1), 'open:playground');
  assert.equal(f.calls.filter(c => c.startsWith('open:')).length, 1);
  assert.deepEqual(f.screens, [['generic', 'selectWorld.data_read']], "26.3's message screen from the click");
  assert.ok(f.calls.includes('MenuInput-saved') && f.calls.includes('MenuPreferences-saved') && f.calls.includes('video-saved'));
  assert.equal(f.api.enabled(), false);
  assert.equal(f.api.worldEnabled('playground'), false);
  assert.deepEqual(f.notices, []);
});

test('PIE can open the test level while editor Quit remains disabled', async () => {
  const f = fixture({canQuit: false}); await f.api.initialize();
  assert.equal(f.api.enabled(), false); assert.equal(f.api.worldEnabled('playground'), true);
  await f.api.openWorld('playground'); assert.equal(f.calls.at(-1), 'open:playground');
});

test('failed save leaves the menu and all setting stores usable', async () => {
  const f = fixture({saveFailure: true}); await f.api.initialize(); await f.api.openWorld('playground');
  assert.deepEqual(f.calls.slice(-3), ['video-unlock', 'MenuInput-unlock', 'MenuPreferences-unlock']);
  assert.equal(f.api.isLoading(), false); assert.equal(f.api.worldEnabled('playground'), true);
  assert.equal(f.notices.at(-1)[1], 'Save failed.');
  assert.deepEqual(f.screens, [['generic', 'selectWorld.data_read'], ['back']]);
});

test('unanswered settings drain never launches late after its deadline', async () => {
  const f = fixture(); await f.api.initialize(); let finish;
  f.context.window.MenuPreferences.prepareForExit = () => new Promise(resolve => finish = resolve);
  await f.api.openWorld('playground'); finish(); await Promise.resolve(); await Promise.resolve();
  assert.equal(f.calls.some(c => c.startsWith('open:')), false);
  assert.equal(f.api.worldEnabled('playground'), true);
  assert.match(f.notices.at(-1)[1], /Settings did not finish/);
});

test('late pre-travel poll cannot overwrite the accepted travel', async () => {
  const f = fixture(); await f.api.initialize(); const before = f.api.getState(); let finish;
  f.context.window.ue.menuapplication.getstate = () => new Promise(resolve => finish = resolve);
  const poll = f.intervals[0](); await Promise.resolve();
  await f.api.openWorld('playground'); finish(before); await poll;
  assert.equal(f.api.getState().travelIssued, true);
});

test('native worlds are the whole list: the catalog, then the saves; no sample entries', async () => {
  const f = fixture(); await f.api.initialize();
  assert.deepEqual([...f.context.state.worlds.map(w => w.id)], ['playground', 'hello']);
  assert.deepEqual([...f.context.state.worlds.map(w => !!w.builtIn)], [true, false]);
  assert.deepEqual([...f.context.state.worlds.map(w => !!w.saved)], [false, true]);
  assert.deepEqual({...f.context.state.worlds[1].draft}, helloDraft, 'a save carries its settings for Re-Create');
  assert.equal(f.api.defaultWorld(), 'hello', 'the last played save is selected first');
  assert.equal(f.api.isPlayable('playground'), true);
  assert.equal(f.api.isPlayable('hello'), true);
  for (const id of ['fictional-forest', 'Playground', '/Game/Maps/L_Playground', '', 'foundation', 'super-flat', 'interaction-lab', 'hello?x'])
    assert.equal(f.api.isPlayable(id), false);
  await f.api.openWorld('hello');
  assert.equal(f.calls.at(-1), 'open:hello');
});

test('a rolled-back world list is repaired on the next reply', async () => {
  const f = fixture(); await f.api.initialize();
  f.context.state.worlds = f.context.state.worlds.filter(w => !w.builtIn);
  f.context.current.name = 'worlds';
  await f.intervals[0]();
  assert.deepEqual([...f.context.state.worlds.map(w => w.id)], ['playground', 'hello']);
  assert.equal(f.calls.at(-1), 'draw');
});

test('Create sends the draft once, after the settings drain, then follows travel', async () => {
  const f = fixture(); await f.api.initialize();
  const creating = f.api.createWorld({name: 'New World', seed: '', gameMode: 'Creative'});
  await f.api.createWorld({name: 'Twice'}); await f.api.openWorld('playground'); await creating;
  assert.deepEqual(f.calls.filter(c => c.startsWith('create:') || c.startsWith('open:')), ['create:New World']);
  assert.ok(f.calls.indexOf('create:New World') > f.calls.indexOf('MenuPreferences-saved'));
  assert.equal(f.api.getState().travelIssued, true);
  assert.equal(f.api.isLoading(), true, 'the page stays locked while Unreal travels');
  assert.deepEqual(f.screens, [['generic', 'createWorld.preparing']]);
  assert.equal(f.context.state.worlds.find(w => w.id === 'new-world').name, 'New World');
  assert.deepEqual(f.notices, []);
});

test('a world that stops opening after its message screen goes back to the list', async () => {
  const f = fixture(); await f.api.initialize();
  const native = f.context.window.ue.menuapplication, accepted = await native.getstate();
  native.requestopenworld = async () => ({...accepted, pendingWorld: true});
  native.getstate = async () => ({...accepted, ok: false, error: 'The game is no longer available for map travel.'});
  await f.api.openWorld('playground');
  assert.deepEqual(f.screens, [['generic', 'selectWorld.data_read'], ['back']]);
  assert.equal(f.notices.at(-1)[1], 'The game is no longer available for map travel.');
  assert.equal(f.api.isLoading(), false);
});

test("a refused draft shows native's reason and leaves the menu usable", async () => {
  const f = fixture({createError: 'Enter a name for the world.'}); await f.api.initialize();
  await f.api.createWorld({name: ' '});
  assert.deepEqual([...f.notices.at(-1)], ['Create New World', 'Enter a name for the world.']);
  assert.deepEqual(f.screens, [['generic', 'createWorld.preparing'], ['back']], 'the message screen goes back before the reason shows');
  assert.equal(f.api.isLoading(), false);
  assert.equal(f.api.worldEnabled('playground'), true);
  assert.deepEqual(f.calls.slice(-3), ['video-unlock', 'MenuInput-unlock', 'MenuPreferences-unlock']);
});

test('Create without the game explains instead of failing silently', async () => {
  const f = fixture(); // never initialized: no native state
  await f.api.createWorld({name: 'Offline'});
  assert.equal(f.calls.some(c => c.startsWith('create:')), false);
  assert.equal(f.notices.at(-1)[0], 'Create New World');
});

test('delete and rename reach native for saved worlds only, and the reply updates the list', async () => {
  const f = fixture(); await f.api.initialize();
  await f.api.deleteWorld('playground'); await f.api.renameWorld('playground', 'Mine'); await f.api.deleteWorld('../hello');
  assert.equal(f.calls.some(c => c.startsWith('delete:') || c.startsWith('rename:')), false);
  await f.api.renameWorld('hello', 'Hi there');
  assert.equal(f.calls.at(-1), 'rename:hello:Hi there');
  assert.equal(f.context.state.worlds.find(w => w.id === 'hello').name, 'Hi there');
  await f.api.deleteWorld('hello');
  assert.equal(f.calls.at(-1), 'delete:hello');
  assert.deepEqual([...f.context.state.worlds.map(w => w.id)], ['playground']);
  assert.equal(f.api.isPlayable('hello'), false);
  assert.deepEqual(f.notices, []);
});

test('a kept title page back from a world unlocks and can travel again (MenuTitle.reset)', async () => {
  const f = fixture(); await f.api.initialize();
  await f.api.openWorld('playground');
  assert.equal(f.api.isLoading(), true);
  assert.equal(f.api.worldEnabled('playground'), false);
  // The next menu map: native's application starts clean (UMenuApplication::Initialize).
  f.native.getstate = async () => ({ok: true, canQuit: true, pendingQuit: false, quitIssued: false, canOpenWorld: true,
    pendingWorld: false, travelIssued: false, error: '', worlds: [playground], savedWorlds: []});
  await f.api.returned();
  assert.equal(f.api.isLoading(), false);
  assert.ok(['video-unlock', 'MenuInput-unlock', 'MenuPreferences-unlock'].every(c => f.calls.includes(c)), f.calls.join());
  assert.equal(f.api.worldEnabled('playground'), true);
  await f.api.openWorld('playground');
  assert.equal(f.calls.filter(c => c.startsWith('open:')).length, 2);
});

// menu.World.WaitForPaint: the native request waits two frames after the preparing screen (at most 250 ms); at 0 (or
// from a native without the field) it goes at once, as before.
const settle = () => new Promise(resolve => setTimeout(resolve, 10));
const requests = f => f.calls.filter(c => c.startsWith('open:') || c.startsWith('create:'));
test('WaitForPaint 1: Play Selected World asks native two frames after Reading world data', async () => {
  const f = fixture({waitForPaint: true}); await f.api.initialize();
  const opening = f.api.openWorld('playground'); await settle();
  assert.deepEqual(f.screens.at(-1), ['generic', 'selectWorld.data_read']);
  assert.deepEqual(requests(f), []);
  assert.ok(!f.calls.includes('video'), 'the stores drain after the screen has painted');
  f.frame(); await settle();
  assert.deepEqual(requests(f), [], 'one frame is not enough');
  f.frame(); await opening;
  assert.deepEqual(requests(f), ['open:playground']);
});
test('WaitForPaint 1: Create New World takes the same path', async () => {
  const f = fixture({waitForPaint: true}); await f.api.initialize();
  const creating = f.api.createWorld(helloDraft); await settle();
  assert.deepEqual(requests(f), []);
  f.frame(); f.frame(); await creating;
  assert.deepEqual(requests(f), ['create:Hello']);
});
test('WaitForPaint 0 and a native without it: the request goes at once', async () => {
  for (const waitForPaint of [false, undefined]) {
    const f = fixture({waitForPaint}); await f.api.initialize();
    await f.api.openWorld('playground');
    assert.deepEqual(requests(f), ['open:playground'], String(waitForPaint));
  }
});
test('WaitForPaint 1: a page that never draws asks after 250 ms', async () => {
  const f = fixture({waitForPaint: true}); await f.api.initialize();
  const started = Date.now();
  await f.api.openWorld('playground');
  assert.deepEqual(requests(f), ['open:playground']);
  assert.ok(Date.now() - started >= 240, String(Date.now() - started));
});
