const MenuApplication = (() => {
  let bridge = null, snapshot = null, quitting = false, polling = false, retryAllowed = false, revision = 0;
  const enabled = () => !quitting && !!snapshot?.canQuit && !snapshot.quitIssued && (!snapshot.pendingQuit || retryAllowed);
  function refresh() {
    for (const button of screen.querySelectorAll('[data-application-quit]')) button.disabled = !enabled();
  }
  async function call(name, deadline = Infinity) {
    const method = bridge[name.toLowerCase()] || bridge[name];
    if (typeof method !== 'function') throw Error('Game shutdown is unavailable.');
    const remaining = deadline - Date.now();
    if (remaining <= 0) throw Error('The game did not finish closing. Please try again.');
    const result = await MenuBridge.wait(Promise.resolve().then(() => method.call(bridge)),
      Math.min(MenuBridge.requestTimeout, remaining), 'The game shutdown connection did not respond. Please try again.');
    const reply = typeof result === 'string' ? JSON.parse(result) : result;
    if (!reply || typeof reply.ok !== 'boolean' || typeof reply.canQuit !== 'boolean' ||
        typeof reply.pendingQuit !== 'boolean' || typeof reply.quitIssued !== 'boolean')
      throw Error('Invalid response from the game shutdown service.');
    return reply;
  }
  function absorb(reply) { snapshot = reply; retryAllowed = false; refresh(); }
  async function quit() {
    if (!enabled()) return;
    quitting = true;
    retryAllowed = false;
    revision++;
    refresh();
    const deadline = Date.now() + MenuBridge.exitTimeout;
    try {
      await MenuBridge.wait(MenuVideo.prepareForExit(), deadline - Date.now(),
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
    if (!bridge || quitting || polling) return;
    polling = true;
    const ticket = revision;
    try { const reply = await call('GetState'); if (!quitting && ticket === revision) absorb(reply); }
    catch (error) { console.warn('Game shutdown synchronization: ' + error.message); }
    finally { polling = false; }
  }, 500);
  return {enabled, quit, initialize, getState: () => snapshot ? clone(snapshot) : null};
})();
window.MenuApplication = MenuApplication;
void MenuApplication.initialize();
