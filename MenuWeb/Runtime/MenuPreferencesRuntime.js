const preferenceSettings = new Set(Object.keys(PREFERENCE_SCHEMA.settings));
// The 101 saved menu preferences, language, pack order and two flags through the
// menupreferences service. Edits queue in order; Done-style commits are transactions.
const MenuPreferences = (() => {
  const client = MenuBridge.client('menupreferences', {service: 'preferences',
    unavailable: 'Preferences are unavailable. Restart Unreal after rebuilding.',
    validate: reply => !!reply && typeof reply.ok === 'boolean' && typeof reply.ready === 'boolean' &&
      typeof reply.editable === 'boolean' && typeof reply.persistent === 'boolean' &&
      (!reply.ready || (!!reply.settings && Array.isArray(reply.packs) && typeof reply.language === 'string' &&
        Object.keys(reply.settings).length === preferenceSettings.size &&
        [...preferenceSettings].every(id => Object.hasOwn(reply.settings, id))))});
  let snapshot = null, running = null, locked = false, committing = false;
  let revision = 0, polling = false, initializing = null;
  const queue = [];
  // Done is a transaction: keep the selected draft stable until native saving
  // either accepts it or returns an error.
  for (const type of ['click','pointerdown','keydown'])
    document.addEventListener(type, event => {
      if (committing) { event.preventDefault(); event.stopImmediatePropagation(); }
    }, true);
  const available = () => !!snapshot?.editable && !locked && !committing;
  const copy = value => JSON.parse(JSON.stringify(value));
  const supported = id => preferenceSettings.has(id);

  function preview(target, patch) {
    if (patch.settings) {
      Object.assign(target.settings, patch.settings);
      if (Object.hasOwn(patch.settings,'graphicsPreset')) {
        Object.assign(target.settings, PREFERENCE_SCHEMA.presets[patch.settings.graphicsPreset] || {});
      } else if (PREFERENCE_SCHEMA.quality.some(id => Object.hasOwn(patch.settings,id))) {
        target.settings.graphicsPreset = 'Custom';
      }
      if (Object.hasOwn(patch.settings,'highContrast') && !patch.packs) {
        target.packs = target.packs.filter(id => id !== 'high_contrast');
        if (patch.settings.highContrast) target.packs.push('high_contrast');
      }
    }
    for (const key of ['language','packs','skipWarning','telemetryOptInExtra'])
      if (Object.hasOwn(patch,key)) target[key] = copy(patch[key]);
    if (patch.packs) target.settings.highContrast = patch.packs.includes('high_contrast');
  }
  function project() {
    if (!snapshot?.ready) return;
    const displayed = copy(snapshot);
    for (const job of queue) preview(displayed, job.patch);
    Object.assign(state.settings, displayed.settings);
    state.settings.skipWarning = displayed.skipWarning;
    state.settings.telemetryOptInExtra = displayed.telemetryOptInExtra;
    const previousLanguage = state.language;
    state.language = displayed.language;
    if (chosenLanguage === previousLanguage) chosenLanguage = displayed.language;
    state.packs = copy(displayed.packs);
    refreshSettings();
  }
  function absorb(reply) { snapshot = copy(reply); project(); }
  const call = (name, ...args) => client.call(name, args);
  function report(error) {
    console.warn('Menu preferences: ' + error.message);
    message('Menu Preferences', error.message);
  }
  async function drain() {
    while (queue.length) {
      const job = queue[0];
      job.dispatched = true;
      let failure;
      try {
        const reply = await call('ApplyChanges', JSON.stringify(job.patch));
        snapshot = copy(reply);
        if (!reply.ok || reply.error) throw Error(reply.error || 'Preferences could not be saved.');
      } catch (error) {
        failure = error;
        try { snapshot = copy(await call('GetState')); } catch {}
      }
      queue.shift();
      project();
      if (failure) { job.reject(failure); if (!committing) report(failure); }
      else job.resolve(copy(snapshot));
    }
  }
  function start() {
    if (running) return running;
    running = drain().finally(() => { running = null; if (queue.length) start(); });
    return running;
  }
  function enqueue(patch, key = '') {
    revision++;
    const last = queue.at(-1);
    if (key && last && !last.dispatched && last.key === key) {
      last.patch = copy(patch); project(); return last.promise;
    }
    const job = {patch:copy(patch),key,dispatched:false};
    job.promise = new Promise((resolve,reject) => { job.resolve = resolve; job.reject = reject; });
    // Event handlers may intentionally ignore the returned completion.
    job.promise.catch(() => {});
    queue.push(job); project(); start();
    return job.promise;
  }
  function set(id,value) {
    if (!supported(id)) throw Error('Preference is not editable: ' + id);
    const d = PREFERENCE_SCHEMA.settings[id];
    const valid = d.values ? d.values.includes(value) : Number.isFinite(value) &&
      value >= d.min && value <= d.max && (value === d.default ||
        Math.abs((value-d.min)/d.step-Math.round((value-d.min)/d.step)) < 0.00001);
    if (!valid) throw Error('Invalid preference value: ' + id);
    if (!available()) return Promise.resolve();
    return enqueue({settings:{[id]:value}},id);
  }
  function setFlag(id,value) {
    if (!['skipWarning','telemetryOptInExtra'].includes(id) || typeof value !== 'boolean')
      throw Error('Invalid preference flag');
    if (!available()) return Promise.resolve();
    return enqueue({[id]:value},id);
  }
  async function commit(patch,onSuccess) {
    if (!available()) return;
    committing = true;
    refreshSettings();
    try {
      await enqueue(patch);
      committing = false;
      onSuccess();
    } catch (error) { committing = false; report(error); }
    finally { committing = false; refreshSettings(); }
  }
  async function settled() {
    while (running || queue.length) await (running || start());
    return snapshot ? copy(snapshot) : null;
  }
  function initialize() {
    if (initializing) return initializing;
    initializing = (async () => {
      const ticket = revision;
      if (!await client.connect()) { console.warn('Menu preferences unavailable; restart Unreal after rebuilding.'); return; }
      try {
        const reply = await call('GetState');
        if (ticket === revision) absorb(reply);
        if (reply.error) report(Error(reply.error));
      } catch (error) { report(error); }
    })();
    return initializing;
  }
  setInterval(async () => {
    if (!client.connected || !snapshot || locked || committing || queue.length || running || polling) return;
    polling = true;
    const ticket = revision;
    try {
      const reply = await call('GetState');
      if (ticket === revision && !locked && !committing && !queue.length) absorb(reply);
    } catch {} // A later edit/read can retry; polling does not stack error dialogs.
    finally { polling = false; }
  },1000);
  return {
    initialize, supported, set, setFlag, commit, settled,
    enabled: id => supported(id) && available(),
    refreshAdditional: () => {
      for (const node of screen.querySelectorAll('[data-preference-flag]')) {
        node.disabled = !available();
        node.setAttribute('aria-checked', !!state.settings[node.dataset.preferenceFlag]);
      }
      for (const node of screen.querySelectorAll('[data-preference-commit]'))
        if (node.dataset.preferenceCommit) node.disabled = !available();
    },
    available, isCommitting: () => committing,
    getState: () => snapshot ? copy(snapshot) : null,
    status: () => !snapshot ? 'Preferences unavailable; restart after rebuilding' :
      !snapshot.editable ? 'Saved preferences are protected; see Menu Preferences' :
      queue.length ? 'Saving preferences...' : snapshot.persistent ? 'Preferences save automatically' : 'Session only; preferences do not save',
    tooltip: () => 'Saved for later; this option does not affect the game yet.\n' +
      (!snapshot?.persistent ? 'Editor/session-only mode: changes are temporary.' : 'Changes are remembered between launches.'),
    prepareForExit: async () => { locked = true; revision++; refreshSettings(); await settled(); },
    cancelExit: () => { locked = false; revision++; refreshSettings(); },
    reset: async () => {
      locked = true; revision++; refreshSettings(); await settled();
      const reply = await call('ResetPreferences');
      absorb(reply);
      if (!reply.ok || reply.error) throw Error(reply.error || 'Preferences could not be reset.');
    }
  };
})();
window.MenuPreferences = MenuPreferences;
MenuStores.register(MenuPreferences);
void MenuPreferences.initialize();
