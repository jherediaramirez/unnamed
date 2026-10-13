// Quit Game, world travel and Create New World through the native menuapplication service. Leaving drains
// every settings store first, sends one request, then follows native state until Unreal confirms.
const MenuApplication = (() => {
  const client = MenuBridge.client('menuapplication', {service: 'game application', unavailable: 'Game shutdown is unavailable.',
    validate: reply => !!reply && typeof reply.ok === 'boolean' && typeof reply.canQuit === 'boolean' &&
      typeof reply.pendingQuit === 'boolean' && typeof reply.quitIssued === 'boolean'});
  let snapshot = null, leaving = null, polling = false, retryAllowed = false, revision = 0; // leaving: 'quit' | 'world' | 'create'
  let catalog = [], saved = []; // Native worlds in list order: the catalog's, then Create New World's saves.
  const enabled = () => !leaving && !snapshot?.pendingWorld && !snapshot?.travelIssued && !!snapshot?.canQuit &&
    !snapshot.quitIssued && (!snapshot.pendingQuit || retryAllowed);
  const isPlayable = id => catalog.some(world => world.id === id) || saved.some(world => world.id === id);
  const canTravel = () => !leaving && !!snapshot?.canOpenWorld && !snapshot.travelIssued && !snapshot.pendingQuit &&
    (!snapshot.pendingWorld || retryAllowed);
  const worldEnabled = id => isPlayable(id) && canTravel();
  function refresh() {
    for (const button of screen.querySelectorAll('[data-application-quit]')) button.disabled = !enabled();
    if (typeof draft !== 'undefined')
      for (const button of screen.querySelectorAll('[data-create-submit]')) button.disabled = !!leaving || !draft.name.trim();
    if (typeof refreshWorldActions === 'function') refreshWorldActions();
  }
  // The world list is native: the catalog's built-in worlds, then the saved worlds, most recently played first.
  // A rolled-back screen transaction may restore an older list, so every reply repairs it.
  function syncWorlds(worlds, savedWorlds) {
    if (!Array.isArray(worlds)) return;
    catalog = worlds.map(({id, name, summary, status}) => ({id, name, summary, status}));
    saved = (Array.isArray(savedWorlds) ? savedWorlds : []).map(({id, name, summary, status, draft}) => ({id, name, summary, status, draft}));
    const list = [...catalog.map(world => ({...world, folder: world.name, builtIn: true})),
      ...saved.map(world => ({...world, folder: world.id, saved: true}))];
    if (JSON.stringify(list) === JSON.stringify(state.worlds)) return;
    state.worlds = list;
    if (current.name === 'worlds') drawWorlds();
  }
  function absorb(reply) { snapshot = reply; retryAllowed = false; syncWorlds(reply.worlds, reply.savedWorlds); refresh(); }

  const exits = {
    quit: {title: 'Quit Game', request: 'RequestQuit', done: reply => reply.quitIssued,
      stillPending: () => true, failed: 'The game could not close.',
      drain: 'Settings did not finish changing. Please try quitting again.',
      timedOut: 'The game did not finish closing. Please try again.'},
    world: {title: 'Open World', request: 'RequestOpenWorld', done: reply => reply.travelIssued, preparing: 'selectWorld.data_read',
      stillPending: reply => reply.pendingWorld, failed: 'The selected world could not open.',
      drain: 'Settings did not finish changing. Please try opening the world again.',
      timedOut: 'The selected world did not finish opening. Please try again.'},
    // Native checks and saves the draft, then opens the new world like any other.
    create: {title: 'Create New World', request: 'CreateWorld', done: reply => reply.travelIssued, preparing: 'createWorld.preparing',
      stillPending: reply => reply.pendingWorld, failed: 'The world could not be created.',
      drain: 'Settings did not finish changing. Please try creating the world again.',
      timedOut: 'The new world did not finish opening. Please try again.'}
  };
  async function leave(kind, ...args) {
    const exit = exits[kind];
    leaving = kind;
    retryAllowed = false;
    revision++;
    refresh();
    // 26.3's message screen from the click until Unreal changes levels (the world's own loading screen follows).
    if (exit.preparing) go('generic', {title: tr(exit.preparing)});
    const deadline = Date.now() + MenuBridge.exitTimeout;
    const call = (method, ...callArgs) => {
      const remaining = deadline - Date.now();
      if (remaining <= 0) throw Error(exit.timedOut);
      return client.call(method, callArgs, Math.min(MenuBridge.requestTimeout, remaining));
    };
    try {
      await MenuBridge.wait(MenuStores.prepareForExit(), deadline - Date.now(), exit.drain);
      let reply = await call(exit.request, ...args);
      absorb(reply);
      if (!reply.ok) throw Error(reply.error || exit.failed);
      while (!exit.done(reply)) {
        if (Date.now() > deadline) throw Error(exit.timedOut);
        await new Promise(resolve => setTimeout(resolve, 100));
        reply = await call('GetState');
        absorb(reply);
        if (!reply.ok || !exit.stillPending(reply) && !exit.done(reply)) throw Error(reply.error || exit.failed);
      }
      // Stay locked: Unreal shuts down or destroys this document when it changes levels.
    } catch (error) {
      leaving = null;
      // An unanswered completion must not leave a cached pending flag locking Retry.
      // Unreal still owns duplicate-request protection; fresh readback replaces this allowance.
      retryAllowed = true;
      revision++;
      MenuStores.cancelExit();
      if (current.name === 'generic') back();
      refresh();
      message(exit.title, error.message);
    }
  }
  const quit = () => enabled() ? leave('quit') : Promise.resolve();
  const openWorld = id => worldEnabled(id) ? leave('world', id) : Promise.resolve();
  function createWorld(settings) {
    if (canTravel()) return leave('create', JSON.stringify(settings));
    if (!leaving) message('Create New World', 'A new world can be created only while the game is running.');
    return Promise.resolve();
  }
  // Edits to a saved world: one request each; the reply's list replaces the page's. Older polls are discarded.
  async function editSaved(title, method, id, ...args) {
    if (leaving || !saved.some(world => world.id === id)) return;
    revision++;
    try {
      const reply = await client.call(method, [id, ...args]);
      absorb(reply);
      if (!reply.ok) throw Error(reply.error || title + ' did not finish.');
    } catch (error) { message(title, error.message); }
  }
  const deleteWorld = id => editSaved('Delete World', 'DeleteWorld', id);
  const renameWorld = (id, name) => editSaved('Edit World', 'RenameWorld', id, name);

  async function initialize() {
    const ticket = revision;
    if (!await client.connect()) return;
    try { const reply = await client.call('GetState'); if (ticket === revision) absorb(reply); }
    catch (error) { console.warn('Game application: ' + error.message); }
  }
  // The kept title page back from a world (MenuTitle.reset): the world it left for is gone, so nothing is leaving any
  // more and the stores unlock; the saved worlds are read again.
  function returned() {
    leaving = null;
    retryAllowed = false;
    revision++;
    MenuStores.cancelExit();
    return initialize();
  }
  setInterval(async () => {
    if (!client.connected || leaving || polling) return;
    polling = true;
    const ticket = revision;
    try { const reply = await client.call('GetState'); if (!leaving && ticket === revision) absorb(reply); }
    catch (error) { console.warn('Game application synchronization: ' + error.message); }
    finally { polling = false; }
  }, 500);
  return {enabled, quit, isPlayable, worldEnabled, openWorld, createWorld, deleteWorld, renameWorld,
    isLoading: () => leaving === 'world' || leaving === 'create', initialize, returned,
    defaultWorld: () => saved[0]?.id ?? catalog[0]?.id ?? null, getState: () => snapshot ? clone(snapshot) : null};
})();
window.MenuApplication = MenuApplication;
void MenuApplication.initialize();
