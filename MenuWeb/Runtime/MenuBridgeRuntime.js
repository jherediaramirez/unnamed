// Shared plumbing for the native services Unreal binds into window.ue (menuvideo, menuinput,
// menupreferences, menuapp, foundation): waiting for a binding, method lookup (CEF lowercases
// method names), bounded waits and JSON replies.
const MenuBridge = Object.freeze({
  requestTimeout: 5000,
  exitTimeout: 20000,
  wait(promise, milliseconds, description) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(description)), Math.max(1, milliseconds));
      // Late replies settle only this abandoned wait, never its caller's state.
      Promise.resolve(promise).then(
        value => { clearTimeout(timer); resolve(value); },
        error => { clearTimeout(timer); reject(error); }
      );
    });
  },
  // Resolves the binding once Unreal attaches it, or null after ten seconds.
  async connect(name) {
    for (let attempt = 0; attempt < 100; attempt++) {
      const binding = window.ue?.[name];
      if (binding) return binding;
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    return null;
  },
  // One native service. validate(reply) rejects malformed replies; messages name the service.
  client(name, {service, unavailable, validate = () => true}) {
    let binding = null;
    return {
      async connect() { binding = await MenuBridge.connect(name); return !!binding; },
      get connected() { return !!binding; },
      async call(method, args = [], timeout = MenuBridge.requestTimeout) {
        const fn = binding?.[method.toLowerCase()] || binding?.[method];
        if (typeof fn !== 'function') throw Error(unavailable);
        const raw = await MenuBridge.wait(Promise.resolve().then(() => fn.apply(binding, args)), timeout,
          `The ${service} connection did not respond. Please try again.`);
        const reply = typeof raw === 'string' ? JSON.parse(raw) : raw;
        if (!validate(reply)) throw Error(`Invalid response from the ${service} service.`);
        return reply;
      }
    };
  }
});

// The settings stores (MenuVideo, MenuInput, MenuPreferences) register here in script order.
// Quit, world travel and the pause menu drain or release all of them together.
const MenuStores = (() => {
  const stores = [];
  return Object.freeze({
    register(store) { stores.push(store); },
    // Locks every store and waits for its in-flight saves.
    prepareForExit: () => Promise.all(stores.map(store => store.prepareForExit())),
    cancelExit() { for (const store of stores) store.cancelExit(); },
    settled: () => Promise.all(stores.map(store => store.settled()))
  });
})();
window.MenuBridge = MenuBridge;
window.MenuStores = MenuStores;
