const liveVideoSettings = new Set(['fullscreen', 'framerateLimit', 'enableVsync', 'guiScale', 'menuBackgroundBlurriness']);
// The five native video settings through the menuvideo service, applied one at a time in order.
const MenuVideo = (() => {
  const client = MenuBridge.client('menuvideo', {service: 'video settings', unavailable: 'Video settings connection is unavailable.',
    validate: reply => !!reply && typeof reply.ok === 'boolean' && !!reply.settings && !!reply.capabilities && !!reply.observed});
  let snapshot = null, running = null, polling = false, resetting = false, exitLocked = false, revision = 0;
  const pending = new Map();
  const cloneState = () => snapshot ? clone(snapshot) : null;
  const call = (name, ...args) => client.call(name, args);
  function absorb(value) {
    snapshot = value;
    const oldScale = state.settings.guiScale;
    for (const id of liveVideoSettings) {
      if (pending.has(id) || (id === 'fullscreen' && value.pendingFullscreen)) continue;
      state.settings[id] = value.settings[id];
    }
    applyAppearance();
    if (oldScale !== state.settings.guiScale) resize();
    refreshSettings();
  }
  function report(error) {
    console.warn('Video settings: ' + error);
    message('Video Settings', String(error));
  }
  async function waitForWindow(value) {
    const deadline = Date.now() + 7000;
    while (value.pendingFullscreen) {
      if (Date.now() > deadline) throw Error('The game window did not finish changing mode.');
      await new Promise(resolve => setTimeout(resolve, 100));
      value = await call('GetState');
      absorb(value);
    }
    return value;
  }
  async function drain() {
    while (pending.size) {
      const [id, value] = pending.entries().next().value;
      pending.delete(id);
      try {
        let reply = await call('ApplySetting', id, JSON.stringify({value}));
        // A later edit remains visible while this asynchronous response is in flight.
        if (!pending.has(id)) state.settings[id] = value;
        absorb(reply);
        reply = await waitForWindow(reply);
        absorb(reply);
        if (!reply.ok || reply.error) report(reply.error || 'The setting could not be applied.');
      } catch (error) {
        try { absorb(await call('GetState')); } catch {}
        report(error.message);
      }
    }
  }
  function startDrain() {
    if (running) return running;
    running = drain().finally(() => { running = null; if (pending.size) startDrain(); });
    return running;
  }
  function set(id, value) {
    const d = D[id];
    const valid = !d ? false : id === 'guiScale' ? Number.isInteger(value) && value >= 0 && value <= maximumScale() : d.values ? d.values.includes(value) : Number.isFinite(value) &&
      value >= d.min && value <= (id === 'guiScale' ? maximumScale() : d.max) &&
      Number.isInteger((value - d.min) / d.step);
    if (!liveVideoSettings.has(id) || !valid) throw Error('Invalid video setting ' + id);
    if (resetting || exitLocked || !snapshot?.capabilities[id]) return Promise.resolve();
    revision++;
    state.settings[id] = value;
    if (quality.has(id)) state.settings.graphicsPreset = 'Custom';
    if (id === 'guiScale') resize();
    else { applyAppearance(); refreshSettings(); }
    pending.set(id, value);
    return startDrain();
  }
  async function initialize() {
    const ticket = revision;
    if (!await client.connect()) {
      console.warn('Native video settings unavailable; live controls remain disabled.');
      return;
    }
    try { const reply = await call('GetState'); if (ticket === revision) absorb(await waitForWindow(reply)); }
    catch (error) { report(error.message); }
  }
  async function hardReset() {
    if (resetting || exitLocked || !snapshot?.capabilities.fullscreen) return;
    resetting = true;
    revision++;
    refreshSettings();
    try {
      while (running || pending.size) await (running || startDrain());
      if (window.MenuPreferences) await window.MenuPreferences.reset();
      if (window.MenuInput) await window.MenuInput.resetAll(true);
      let reply = await call('HardReset');
      reply = await waitForWindow(reply);
      absorb(reply);
      if (!reply.ok || reply.error) throw Error(reply.error || 'Settings could not be reset.');
      // Native preferences were reset explicitly; reload recreates transient drafts.
      window.location.reload();
    } catch (error) { resetting = false; window.MenuPreferences?.cancelExit(); refreshSettings(); report(error.message + '\nReset did not finish. Some preference groups may already have reset; retry to finish.'); }
  }
  async function reload() {
    if (!client.connected || running || resetting) return;
    const ticket = revision;
    try { const reply = await call('GetState'); if (!running && !resetting && ticket === revision) absorb(reply); }
    catch (error) { console.warn('Video settings synchronization: ' + error.message); }
  }
  setInterval(async () => {
    if (!client.connected || running || polling || resetting) return;
    polling = true;
    try { await reload(); }
    finally { polling = false; }
  }, 500);
  return {
    set, initialize, hardReset, reload, getState: cloneState,
    enabled: id => !resetting && !exitLocked && !!snapshot?.capabilities[id],
    prepareForExit: async () => {
      exitLocked = true;
      revision++;
      refreshSettings();
      while (running || pending.size) await (running || startDrain());
    },
    cancelExit: () => { revision++; exitLocked = false; refreshSettings(); },
    settled: async () => { while (running || pending.size) await (running || startDrain()); return cloneState(); }
  };
})();
window.MenuVideo = MenuVideo;
MenuStores.register(MenuVideo);
void MenuVideo.initialize();
