// The kept title page (menu.Title.Keep 1 and 2): one browser for the title screen for the whole game. Native binds
// "menutitle" only on that browser; elsewhere this module stays idle. The page reports TitleReady once its title screen
// and the panorama (or its fallback) have painted and two more frames have passed: after loading (token -1), and after
// native asked it back to the title (reset(token)), which native does, hidden, while each world starts.
const MenuTitle = (() => {
  const client = MenuBridge.client('menutitle', {service: 'title screen', unavailable: 'The title screen is not kept.'});
  let token = -1;
  const send = (method, ...args) => client.connected
    ? client.call(method, args).catch(error => console.warn('Title screen: ' + error.message))
    : Promise.resolve();
  function waitForPaint(ticket, since) {
    let painted = 0;
    const step = () => {
      if (ticket !== token) return;
      const panorama = panoramaFallback || panoramaLoaded && lastDraw >= since;
      if (current.name === 'main' && panorama && ++painted >= 3) return void send('TitleReady', ticket);
      requestAnimationFrame(step);
    };
    requestAnimationFrame(step);
  }
  // Back to the title as a freshly loaded page shows it: no screen history, nothing leaving, the stores and the saved
  // worlds read again.
  async function reset(ticket) {
    token = ticket;
    const since = performance.now();
    current = {name: 'main', data: {}};
    stack = [];
    chosenWorld = null;
    bindingCapture = null;
    scrolls = {};
    messageAction = null;
    searches = {worlds: '', language: '', rules: '', biomes: ''};
    render();
    await Promise.all([MenuApplication.returned(), MenuStores.reload()]);
    if (ticket !== token) return;
    render();
    waitForPaint(ticket, since);
  }
  // Native has revealed the page on a new menu map: its application state now allows world travel again.
  const shown = () => void MenuApplication.initialize();
  async function initialize() {
    if (!await client.connect() || token !== -1) return;
    waitForPaint(-1, 0);
  }
  return {initialize, reset, shown};
})();
window.MenuTitle = MenuTitle;
void MenuTitle.initialize();
