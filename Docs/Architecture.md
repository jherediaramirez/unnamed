# Architecture

## Chosen Route

An empty menu map uses `AMenuGameMode` and `AMenuPlayerController`. The controller adds one full-viewport Slate `SWebBrowser`, sets UI-only input and a visible cursor, and disables movement/look input. There is no pawn or HUD. The browser has no address bar, controls or thumb-button navigation. No browser object binding, user-settings adapter, world generation or account/server service exists.

This route reused the complete supplied HTML/CSS/JS, fonts, sprites, data and all 36 renderers. Native UMG reconstruction was not needed: both editor-hosted and packaged Chromium rendered the interface correctly. The host and packaging use the installed 5.8 headers (`SWebBrowser.h`, `SWebBrowserView.h`, `IWebBrowserWindow.h`), delegates and BuildSettingsVersion.V7 template conventions.

## Data And Presentation

`Reference/minecraft-menu` is unchanged. `Scripts/AdaptMenu.mjs` checks its hash and regenerates the working `Content/Menu/Web/index.html`. Adaptation is fail-fast on missing source anchors. The source structured JSON definitions are retained rather than recreated from memory. `MenuMockRuntime.js` supplies transaction snapshots, stable control IDs and diagnostics, embedded into the working file.

Shared settings, language, bindings and sample lists exist only in document memory. Staged rules, experiments, packs, generation customization, forms and world drafts commit through their source Done/Save actions; Cancel/Escape rolls back the affected domain. No localStorage, cookies, SaveGame or config storage is used for menu values. Reload starts from supplied defaults. The engine may write its ordinary startup logs/cache/config; the menu does not persist settings.

The frozen presentation baseline is English (US), Auto integer scaling, ordinary bitmap font/resources, baseline blur and panorama speed. Mock GUI/font/contrast/language/blur/FPS/audio changes do not affect this baseline. Graphics presets and Hardcore constraints update mock dependencies only. Audio playback and speech synthesis are removed. External links, clipboard, folders, purchase, worlds, servers and accounts terminate at reversible mock screens. Quit never calls an exit API.

## Resource Origin And Packaging

The host builds an encoded `file:///` URL from ProjectContentDir, independent of current working directory. Browser navigation is limited to that document and initial about:blank; popup requests are rejected. A content-security policy rejects network connections, media, frames and forms. All actual images/fonts/styles are local; credit data is embedded, so no file-origin fetch is required.

Chromium's ordinary file-origin rules prevented WebGL texture upload of local panorama images. The working HTML now embeds the six unchanged PNG byte streams as data URLs. This avoids relaxing web security and was verified with a real cubemap rather than the fallback. Physical asset copies and HTML are staged NonUFS by the module's RuntimeDependencies. No developer HTTP server is required.

CSS alone applies integer scaling. No UMG DPI wrapper applies a second scale. Verified actual browser viewports are 1920x1080 -> 480x270 at scale 4 and 1280x720 -> 427x240 at scale 3. The developer-only freezeFrame fixes panorama angle and splash phase for captures; ordinary launch animates normally. It adds no visible menu decoration.

## Diagnostics

The dedicated `LogUnnamedMenu` category records browser console source, line and severity plus document load failures. The Development browser's MenuPrototype diagnostic API exposes screen, mock values, dimensions, assets, errors, controls and definitions. It has no engine bridge. Playwright connects to Unreal's CEF over the optional `-cefdebug` port, not a separate substitute browser. Screenshot pixel checks verify nonblank frames. Slate tools can capture/inspect the outer native window, not HTML buttons.
