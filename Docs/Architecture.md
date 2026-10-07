# Architecture

## Chosen Route

An empty menu map uses `AMenuGameMode` and `AMenuPlayerController`. The controller adds one full-viewport Slate `SWebBrowser`, sets UI-only input and a visible cursor, and disables movement/look input. There is no pawn or HUD. The browser has no address bar, controls or thumb-button navigation. Restricted `UMenuVideoSettings` and `UMenuApplication` bindings own video preferences and normal game shutdown respectively. Neither exposes arbitrary commands or files. World generation and account/server services do not exist. See VideoSettings.md and QuitGame.md for the contracts.

This route reused the supplied HTML/CSS/JS, fonts, sprites and structured data. The original 36 renderers are preserved in the immutable reference; the working interface retains 33 after removing the mock Game closed route, Realms and Join Realm. Native UMG reconstruction was not needed: both editor-hosted and packaged Chromium rendered the interface correctly. The host and packaging use the installed 5.8 headers (`SWebBrowser.h`, `SWebBrowserView.h`, `IWebBrowserWindow.h`), delegates and BuildSettingsVersion.V7 template conventions.

## Data And Presentation

`Reference/minecraft-menu` is unchanged. `Scripts/AdaptMenu.mjs` checks its hash and regenerates the working `Content/Menu/Web/index.html`. Adaptation is fail-fast on missing source anchors. The source structured JSON definitions are retained rather than recreated from memory, except the deliberately removed Realms news/invites preference. `MenuMockRuntime.js` supplies transaction snapshots, stable control IDs and diagnostics, embedded into the working file. Edit the adapter/runtime modules, not the generated HTML. `Tests/registry.mjs` regenerates the registry, inventory and deferred backlog from a current rendered capture.

Realms removal is implemented in the adapter: no main entry, Realms renderer, Join Realm renderer, invite/news/purchase endpoint, join form, feature CSS, Online Settings section or associated preference remains in the active interface. The main menu closes the removed row. Unused translations and source asset copies remain for immutable provenance and historical candidate verification; they do not make Realms reachable. `Tests/realms-removal.test.mjs` and current rendered smoke checks enforce the boundary.

The five implemented video values persist through the native settings service. Other settings, language, bindings and sample lists exist only in document memory. Staged rules, experiments, packs, generation customization, forms and world drafts commit through their source Done/Save actions; Cancel/Escape rolls back the affected domain. No localStorage, cookies or SaveGame is used. Reload rehydrates the five implemented values and resets all other values to supplied defaults.

The reference presentation baseline remains English (US), Auto integer scaling, ordinary bitmap font/resources, baseline blur and panorama speed. GUI scale and blur now affect the live presentation; engine FPS/VSync/fullscreen affect their native systems. Font/contrast/language/audio remain mocked. Graphics presets update mock dependencies only and cannot overwrite the functional blur. Audio playback and speech synthesis are removed. External links, clipboard, folders, purchase, worlds, servers and accounts terminate at reversible mock screens. Quit Game drains pending preference changes, locks further edits, flushes the dedicated file, and calls Unreal's normal QuitGame API on a later controller tick. Editor Play requests are rejected; no forced termination is exposed.

## Browser Recovery

The review-fix checkpoint separates browser deadlines into `MenuBridgeRuntime.js` (five seconds per request; twenty seconds for the entire Quit workflow). `MenuVideoRuntime.js` retains later queued edits after failure and revision-checks polls. `MenuApplicationRuntime.js` starts its deadline before draining preferences and ignores late/stale replies. An unanswered completion allows explicit retry even when the last snapshot says pending; native duplicate-request protection remains intact, and fresh readback replaces that allowance. Browser timeouts do not cancel native operations already dispatched.

`MenuDialogRuntime.js` owns raw rule text and shared validation helpers. World-name and preset text live in their owning dialog's `current.data`, surviving resize/fullscreen redraw and child returns. Invalid raw rule drafts stay invalid even when filtered out; Done validates the whole staged rule set. Server Save requires both a trimmed name and valid address.

`Tests/capture-evidence.mjs` loads only explicitly selected, passing smoke/video capture folders with matching HTML hashes and viewport profiles. Contextual controls belong to the smoke run; reset confirmation observations belong to the selected video run. Historical root supplemental files are preserved but not consumed by current registry generation. [Documented Actions](<Documented Actions/README.md>) indexes the dated work and known gaps.

## Resource Origin And Packaging

The host builds an encoded `file:///` URL from ProjectContentDir, independent of current working directory. Browser navigation is limited to that document and initial about:blank; popup requests are rejected. A content-security policy rejects network connections, media, frames and forms. All actual images/fonts/styles are local; credit data is embedded, so no file-origin fetch is required.

Chromium's ordinary file-origin rules prevented WebGL texture upload of local panorama images. The working HTML now embeds the six unchanged PNG byte streams as data URLs. This avoids relaxing web security and was verified with a real cubemap rather than the fallback. Physical asset copies and HTML are staged NonUFS by the module's RuntimeDependencies. No developer HTTP server is required.

CSS alone applies integer scaling. No UMG DPI wrapper applies a second scale. Verified actual browser viewports are 1920x1080 -> 480x270 at scale 4 and 1280x720 -> 427x240 at scale 3. The developer-only freezeFrame fixes panorama angle and splash phase for captures; ordinary launch animates normally. It adds no visible menu decoration.

## Diagnostics

The dedicated `LogUnnamedMenu` category records browser console source, line and severity plus document load failures. MenuPrototype exposes screen state, dimensions, assets, errors, controls and definitions. MenuVideo exposes the native five-setting snapshot and synchronization completion; observed values distinguish actual window mode/client dimensions, engine FPS cap, measured cadence and VSync from labels. Playwright connects to Unreal's CEF over the optional `-cefdebug` port, not a separate substitute browser. Screenshot pixel checks verify nonblank frames. Slate tools can capture/inspect the outer native window, not HTML buttons.
