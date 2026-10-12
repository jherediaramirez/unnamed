// In the Foundation lab the same menu is the pause menu: no second Controls screen or store.
// The native "foundationmenu" bridge exists only there; on the menu map this module stays idle.
// A world's #pause page (foundation.Pause.Menu 1 and 2) starts on the pause screen, invisible and transparent, and
// reports MenuReady once that screen and the panorama have painted; native answers whether the menu is shown, so a
// pre-warmed page stays hidden until reveal() and a shown one shows itself. Without #pause nothing here changes.
const FoundationMenu = (() => {
  const client = MenuBridge.client('foundationmenu', {service: 'Foundation', unavailable: 'The Foundation lab is not running.'});
  let inLab = false, connecting = null, ready = false, revealPending = false;
  // Fire-and-forget notifications must never surface as uncaught errors.
  const send = (method, ...args) => client.connected
    ? client.call(method, args).catch(error => console.warn('Foundation menu: ' + error.message))
    : Promise.resolve();
  const connect = () => connecting ??= client.connect();
  const pausePage = (window.location?.hash || '').slice(1).split('&').includes('pause');

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
    if (!await connect()) return;
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

  // Shows the page; its next frame reports MenuShown, with what it shows, once the stores have re-read native state.
  function show(reloaded) {
    revealPending = false;
    document.documentElement.classList.remove('pause-hidden');
    const shown = () => JSON.stringify({guiScale: state.settings.guiScale, jump: MenuInput.getState?.()?.nativeKeys?.['key.jump'] ?? null});
    requestAnimationFrame(() => void reloaded.then(() => send('MenuShown', shown())));
  }
  async function markReady() {
    if (!await connect()) return;
    const reply = await send('MenuReady');
    ready = true;
    if (reply?.shown || revealPending) show(MenuStores.reload());
  }
  // The pause screen and the panorama (or its fallback) have painted, then two more frames.
  function waitForPaint() {
    let painted = 0;
    const step = () => {
      if (current.name === 'foundationPause' && (heldPanoramaDrawn || panoramaFallback) && ++painted >= 3) return void markReady();
      requestAnimationFrame(step);
    };
    requestAnimationFrame(step);
  }
  // Native Show on a kept-alive page: the pause screen again, with settings changed while it was hidden.
  function reveal() {
    if (!pausePage) return;
    if (current.name !== 'foundationPause') {
      current = {name: 'foundationPause', data: {}};
      stack = [];
      render();
    }
    revealPending = true;
    const reloaded = MenuStores.reload();
    if (ready) show(reloaded);
  }
  if (pausePage) {
    // The first render (resize() at the end of the page script) is the pause screen, never the title screen.
    current = {name: 'foundationPause', data: {}};
    stack = [];
    const style = document.createElement('style');
    style.textContent = 'html.pause-hidden,html.pause-hidden *{visibility:hidden!important}' +
      'html.pause-hidden,html.pause-hidden body{background:transparent!important}';
    document.head.appendChild(style);
    document.documentElement.classList.add('pause-hidden');
    waitForPaint();
  }

  document.addEventListener('keydown', event => {
    if (inLab && current.name === 'foundationPause' && event.key === 'Escape' && !bindingCapture) {
      event.preventDefault();
      event.stopImmediatePropagation();
      void resume();
    }
  }, true);
  return {initialize, reveal};
})();
void FoundationMenu.initialize();
