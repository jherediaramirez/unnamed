const MenuApplication = (() => {
  let bridge = null, snapshot = null, quitting = false, loading = false, polling = false, retryAllowed = false, revision = 0;
  let catalog = []; // Native playable worlds, in list order.
  const enabled = () => !quitting && !loading && !snapshot?.pendingWorld && !snapshot?.travelIssued && !!snapshot?.canQuit && !snapshot.quitIssued && (!snapshot.pendingQuit || retryAllowed);
  const isPlayable = id => catalog.some(world => world.id === id);
  const worldEnabled = id => isPlayable(id) && !loading && !quitting && !!snapshot?.canOpenWorld &&
    !snapshot.travelIssued && !snapshot.pendingQuit && (!snapshot.pendingWorld || retryAllowed);
  function refresh() {
    for (const button of screen.querySelectorAll('[data-application-quit]')) button.disabled = !enabled();
    if (typeof refreshWorldActions === 'function') refreshWorldActions();
  }
  async function call(name, deadline = Infinity, ...args) {
    const method = bridge[name.toLowerCase()] || bridge[name];
    if (typeof method !== 'function') throw Error('Game shutdown is unavailable.');
    const remaining = deadline - Date.now();
    if (remaining <= 0) throw Error('The game did not finish closing. Please try again.');
    const result = await MenuBridge.wait(Promise.resolve().then(() => method.call(bridge, ...args)),
      Math.min(MenuBridge.requestTimeout, remaining), 'The game shutdown connection did not respond. Please try again.');
    const reply = typeof result === 'string' ? JSON.parse(result) : result;
    if (!reply || typeof reply.ok !== 'boolean' || typeof reply.canQuit !== 'boolean' ||
        typeof reply.pendingQuit !== 'boolean' || typeof reply.quitIssued !== 'boolean')
      throw Error('Invalid response from the game shutdown service.');
    return reply;
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
  async function openWorld(id) {
    if (!worldEnabled(id)) return;
    loading = true;
    retryAllowed = false;
    revision++;
    refresh();
    const deadline = Date.now() + MenuBridge.exitTimeout;
    try {
      await MenuBridge.wait(Promise.all([MenuVideo.prepareForExit(), window.MenuInput?.prepareForExit(), window.MenuPreferences?.prepareForExit()]), deadline - Date.now(),
        'Settings did not finish changing. Please try opening the level again.');
      let reply = await call('RequestOpenWorld', deadline, id);
      absorb(reply);
      if (!reply.ok) throw Error(reply.error || 'The selected level could not open.');
      while (!reply.travelIssued) {
        if (Date.now() > deadline) throw Error('The selected level did not finish opening. Please try again.');
        await new Promise(resolve => setTimeout(resolve, 100));
        reply = await call('GetState', deadline);
        absorb(reply);
        if (!reply.ok || !reply.pendingWorld && !reply.travelIssued) throw Error(reply.error || 'The selected level could not open.');
      }
      // The outgoing document is destroyed when Unreal changes levels.
    } catch (error) {
      loading = false;
      retryAllowed = true;
      revision++;
      MenuVideo.cancelExit();
      window.MenuInput?.cancelExit();
      window.MenuPreferences?.cancelExit();
      refresh();
      message('Open Level', error.message);
    }
  }
  async function quit() {
    if (!enabled()) return;
    quitting = true;
    retryAllowed = false;
    revision++;
    refresh();
    const deadline = Date.now() + MenuBridge.exitTimeout;
    try {
      await MenuBridge.wait(Promise.all([MenuVideo.prepareForExit(), window.MenuInput?.prepareForExit(), window.MenuPreferences?.prepareForExit()]), deadline - Date.now(),
        'Settings did not finish changing. Please try quitting again.');
      let reply = await call('RequestQuit', deadline);
      absorb(reply);
      if (!reply.ok) throw Error(reply.error || 'The game could not close.');
      while (!reply.quitIssued) {
        if (Date.now() > deadline) throw Error('The game did not finish closing. Please try again.');
        await new Promise(resolve => setTimeout(resolve, 100));
        reply = await call('GetState', deadline);
        absorb(reply);
        if (!reply.ok) throw Error(reply.error || 'The game could not close.');
      }
      // Keep changes locked until Unreal completes its normal shutdown.
    } catch (error) {
      quitting = false;
      // An unanswered completion must not leave a cached pending flag locking Retry.
      // Unreal still owns duplicate-request protection; fresh readback replaces this allowance.
      retryAllowed = true;
      revision++;
      MenuVideo.cancelExit();
      window.MenuInput?.cancelExit();
      window.MenuPreferences?.cancelExit();
      refresh();
      message('Quit Game', error.message);
    }
  }
  async function initialize() {
    const ticket = revision;
    for (let attempt = 0; attempt < 100; attempt++) {
      bridge = window.ue?.menuapp;
      if (bridge) break;
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    if (!bridge) return;
    try { const reply = await call('GetState'); if (ticket === revision) absorb(reply); }
    catch (error) { console.warn('Game shutdown: ' + error.message); }
  }
  setInterval(async () => {
    if (!bridge || quitting || loading || polling) return;
    polling = true;
    const ticket = revision;
    try { const reply = await call('GetState'); if (!quitting && !loading && ticket === revision) absorb(reply); }
    catch (error) { console.warn('Game shutdown synchronization: ' + error.message); }
    finally { polling = false; }
  }, 500);
  return {enabled, quit, isPlayable, worldEnabled, openWorld, isLoading: () => loading, initialize,
    defaultWorld: () => catalog[0]?.id ?? null, getState: () => snapshot ? clone(snapshot) : null};
})();
window.MenuApplication = MenuApplication;
void MenuApplication.initialize();
