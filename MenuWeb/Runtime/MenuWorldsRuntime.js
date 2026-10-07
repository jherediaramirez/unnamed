// Built-in worlds come from the native catalog (MenuApplication); the fictional entries keep
// their design workflows. Only a catalog ID reaches native travel: no URL, path or console text.
function refreshWorldActions() {
  const world = state.worlds.find(w => w.id === chosenWorld);
  const loading = !!window.MenuApplication?.isLoading();
  for (const button of screen.querySelectorAll('[data-world-action]')) {
    const play = button.dataset.worldAction === 'play';
    button.disabled = loading || (play ? !window.MenuApplication?.worldEnabled(world?.id) : !world || !!world.builtIn);
    if (play) button.textContent = loading ? 'Opening level...' : tr('selectWorld.select');
  }
}

function loadWorld() {
  const world = state.worlds.find(w => w.id === chosenWorld);
  if (!world || window.MenuApplication?.isLoading()) return;
  if (world.builtIn) { void MenuApplication.openWorld(world.id); return; }
  message(world.name, 'Design reference only. Choose a world marked Playable.', tr('gui.back'));
}
