// The Singleplayer list is native (MenuApplication merges the catalog's built-in worlds and the saves of Create New
// World). Only an id from that list reaches native travel: no URL, path or console text.
function refreshWorldActions() {
  const world = state.worlds.find(w => w.id === chosenWorld);
  const loading = !!window.MenuApplication?.isLoading();
  for (const button of screen.querySelectorAll('[data-world-action]')) {
    const play = button.dataset.worldAction === 'play';
    // Edit, Delete and Re-Create act on saved worlds; built-in worlds belong to the game.
    button.disabled = loading || (play ? !window.MenuApplication?.worldEnabled(world?.id) : !world?.saved);
    if (play) button.textContent = loading ? 'Opening world...' : tr('selectWorld.select');
  }
}

function loadWorld() {
  const world = state.worlds.find(w => w.id === chosenWorld);
  if (!world || window.MenuApplication?.isLoading()) return;
  void MenuApplication.openWorld(world.id);
}

// Re-Create starts from the saved world's own settings, its seed included, as the reference does.
function recreateDraft(world) {
  return Object.assign(newDraft(), clone(world.draft ?? {name: world.name}));
}
