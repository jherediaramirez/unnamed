// Quit Game runs through the native MenuApplication service instead of the mock "Game closed" screen.
export const name = 'Application';

export function apply(patch) {
  patch.line('quit(){', '');
  patch.edit('main(){', "button(tr('menu.quit'),", "const quit=button(tr('menu.quit'),");
  patch.edit('main(){', "()=>go('quit'));", "()=>void MenuApplication.quit(),!MenuApplication.enabled());quit.dataset.applicationQuit='';");
}
