// Quit Game and world travel through the native menuapplication service. Both drain every settings
// store first, send one request, then follow native state until Unreal confirms.
const MenuApplication = (() => {
  const client = MenuBridge.client('menuapplication', {service: 'game application', unavailable: 'Game shutdown is unavailable.',
    validate: reply => !!reply && typeof reply.ok === 'boolean' && typeof reply.canQuit === 'boolean' &&
      typeof reply.pendingQuit === 'boolean' && typeof reply.quitIssued === 'boolean'});
  let snapshot = null, leaving = null, polling = false, retryAllowed = false, revision = 0; // leaving: 'quit' | 'world'
  let catalog = []; // Native playable worlds, in list order.
  const enabled = () => !leaving && !snapshot?.pendingWorld && !snapshot?.travelIssued && !!snapshot?.canQuit &&
    !snapshot.quitIssued && (!snapshot.pendingQuit || retryAllowed);
  const isPlayable = id => catalog.some(world => world.id === id);
  const worldEnabled = id => isPlayable(id) && !leaving && !!snapshot?.canOpenWorld &&
    !snapshot.travelIssued && !snapshot.pendingQuit && (!snapshot.pendingWorld || retryAllowed);
  function refresh() {
    for (const button of screen.querySelectorAll('[data-application-quit]')) button.disabled = !enabled();
    if (typeof refreshWorldActions === 'function') refreshWorldActions();
  }
  // Built-in worlds lead the list; a rolled-back screen transaction may drop them, so every
  // reply re-adds any that are missing.
  function syncWorlds(worlds) {
    if (!Array.isArray(worlds)) return;
    const next = worlds.map(({id, name, summary, status}) => ({id, name, summary, status}));
    if (JSON.stringify(next) === JSON.stringify(catalog) && catalog.every(world => state.worlds.some(w => w.id === world.id))) return;
    catalog = next;
    state.worlds = [...catalog.map(world => ({...world, folder: world.name, builtIn: true})), ...state.worlds.filter(w => !w.builtIn)];
    if (current.name === 'worlds') drawWorlds();
  }
  function absorb(reply) { snapshot = reply; retryAllowed = false; syncWorlds(reply.worlds); refresh(); }

  const exits = {
    quit: {title: 'Quit Game', request: 'RequestQuit', done: reply => reply.quitIssued,
      stillPending: () => true, failed: 'The game could not close.',
      drain: 'Settings did not finish changing. Please try quitting again.',
      timedOut: 'The game did not finish closing. Please try again.'},
    world: {title: 'Open World', request: 'RequestOpenWorld', done: reply => reply.travelIssued,
      stillPending: reply => reply.pendingWorld, failed: 'The selected world could not open.',
      drain: 'Settings did not finish changing. Please try opening the world again.',
      timedOut: 'The selected world did not finish opening. Please try again.'}
  };
  async function leave(kind, ...args) {
    const exit = exits[kind];
    leaving = kind;
    retryAllowed = false;
    revision++;
    refresh();
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
      refresh();
      message(exit.title, error.message);
    }
  }
  const quit = () => enabled() ? leave('quit') : Promise.resolve();
  const openWorld = id => worldEnabled(id) ? leave('world', id) : Promise.resolve();

  async function initialize() {
    const ticket = revision;
    if (!await client.connect()) return;
    try { const reply = await client.call('GetState'); if (ticket === revision) absorb(reply); }
    catch (error) { console.warn('Game application: ' + error.message); }
  }
  setInterval(async () => {
    if (!client.connected || leaving || polling) return;
    polling = true;
    const ticket = revision;
    try { const reply = await client.call('GetState'); if (!leaving && ticket === revision) absorb(reply); }
    catch (error) { console.warn('Game application synchronization: ' + error.message); }
    finally { polling = false; }
  }, 500);
  return {enabled, quit, isPlayable, worldEnabled, openWorld, isLoading: () => leaving === 'world', initialize,
    defaultWorld: () => catalog[0]?.id ?? null, getState: () => snapshot ? clone(snapshot) : null};
})();
window.MenuApplication = MenuApplication;
void MenuApplication.initialize();
