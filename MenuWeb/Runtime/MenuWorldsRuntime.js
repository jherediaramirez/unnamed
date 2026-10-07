// The two fictional entries retain their design workflows. Only this built-in ID
// can reach native map travel; no URL, package path, or console text crosses over.
function refreshWorldActions() {
  const world = state.worlds.find(w => w.id === chosenWorld);
  const loading = !!window.MenuApplication?.isLoading();
  for (const button of screen.querySelectorAll('[data-world-action]')) {
    const play = button.dataset.worldAction === 'play';
    button.disabled = loading || (play ? !window.MenuApplication?.worldEnabled(world?.id) : !world || ['super-flat','foundation'].includes(world.id));
    if (play) button.textContent = loading ? 'Opening level...' : tr('selectWorld.select');
  }
}

function loadWorld() {
  const world = state.worlds.find(w => w.id === chosenWorld);
  if (!world || window.MenuApplication?.isLoading()) return;
  if (['super-flat','foundation'].includes(world.id)) { void MenuApplication.openWorld(world.id); return; }
  message(world.name, 'Design reference only. Choose a world marked Playable.', tr('gui.back'));
}
