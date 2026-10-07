// In the Foundation lab the same menu is the pause menu: no second Controls screen or store.
// The native "foundationmenu" bridge exists only there; on the menu map this module stays idle.
const FoundationMenu = (() => {
  const client = MenuBridge.client('foundationmenu', {service: 'Foundation', unavailable: 'The Foundation lab is not running.'});
  let inLab = false;
  // Fire-and-forget notifications must never surface as uncaught errors.
  const send = (method, ...args) => client.connected
    ? client.call(method, args).catch(error => console.warn('Foundation menu: ' + error.message))
    : Promise.resolve();

  async function resume(returnToMenu = false) {
    await MenuStores.settled();
    await send(returnToMenu ? 'ReturnToMenu' : 'Resume');
  }

  renderers.foundationPause = () => {
    title('Game Paused', 30);
    button('Back to Game', C - 100, 75, 200, () => void resume());
    button(tr('options.title'), C - 100, 100, 200, () => go('options'));
    button('Return to Main Menu', C - 100, 125, 200, () => void resume(true));
  };

  async function initialize() {
    if (!await client.connect()) return;
    // While a key binding is captured, the native router must let raw input through.
    MenuInput.setCaptureHooks({beforeStart: () => send('SetCapture', true), afterEnd: () => void send('SetCapture', false)});
    const state = await send('GetState');
    inLab = !!state?.inLab;
    if (inLab) {
      current = {name: 'foundationPause', data: {}};
      stack = [];
      render();
    }
  }

  document.addEventListener('keydown', event => {
    if (inLab && current.name === 'foundationPause' && event.key === 'Escape' && !bindingCapture) {
      event.preventDefault();
      event.stopImmediatePropagation();
      void resume();
    }
  }, true);
  return {initialize};
})();
void FoundationMenu.initialize();
