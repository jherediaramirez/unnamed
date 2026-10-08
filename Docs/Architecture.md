# Architecture

How the project fits together. Foundation-specific movement, input and diagnostics details are in [Foundation architecture](Foundation/Architecture.md); naming rules are in [Naming conventions](NamingConventions.md).

## Layout

| Folder | Contents |
|---|---|
| `Source/Unnamed/` | The one game module. Project-wide pieces at the root: `UnnamedProjectSettings` (maps, world catalog, input assets, HUD and block asset roots), `UnnamedSession` (may this run save settings?), `UnnamedFiles` (crash-safe saves), `UnnamedLog` (log categories). |
| `Source/Unnamed/Menu/` | The offline menu: `UMenuBrowserHost`, the menu services it binds, and the menu map's game mode/controller. |
| `Source/Unnamed/Input/` | Enhanced Input layer: `UUnnamedInputSubsystem`, the saved `UUnnamedInputSettings`, `FUnnamedInputIntent`. |
| `Source/Unnamed/Foundation/` | The first-person pawn, controller parts, pure simulation, player vitals (health, food, air, XP, attack recovery), lab geometry (`AFoundationLab`, and the Playground in `AFoundationPlayground`), runtime probe; the Playground's block world, items, targeting, authority, interaction state and presentation; the 26.3 block registry, placement rules, block meshes and texture animation ([Blocks](Blocks.md)). |
| `Source/Unnamed/Hud/` | Native Slate HUD, pure layout/selection, cooked sprite metadata and the Foundation adapter (player vitals by default, an opt-in preview fixture); the creative inventory screen and 3D block item icons. |
| `Source/Unnamed/Tests/` | Native automation tests, one file per area (`Unnamed.<Area>.*`). |
| `MenuWeb/` | Menu generator (`AdaptMenu.mjs` + `Patches/`) and the JavaScript shipped inside the page (`Runtime/`). |
| `Content/Menu/Web/` | Generated `index.html`, preference schema and mirrored reference assets, staged as loose files. |
| `Reference/minecraft-menu/` | The unchanged, hash-checked reference menu. |
| `Reference/minecraft-blocks-26.3/` | The generated, hash-checked Java 26.3 block reference that `Scripts/Editor/ImportBlocks.py` reads. |
| `Scripts/` | PowerShell build/launch/test entry points; `Scripts/Editor/` holds Unreal editor Python. |
| `Tests/` | Node unit tests (`*.test.mjs`) and CEF integration suites; recorded evidence in `Tests/Evidence/<date>/`. |
| `Docs/` | Living docs; dated work records in `Docs/Records/`; preserved inputs in `Docs/Archive/`. |

## Maps and controllers

`/Game/Menu/L_Menu` uses `AMenuGameMode` and `AMenuPlayerController`: no pawn or HUD, just the menu. `/Game/Maps/L_Playground` uses `AFoundationGameMode` and `AFoundationController` with one placed `AFoundationPlayground` (the 2 km dev-grid ground, the starter-block stations, the block gallery and the movement course; see [Foundation architecture](Foundation/Architecture.md#playground-block-interaction) and [Playground](Playground.md)). It is the only world in the catalog.

Neither gameplay controller inherits from the menu controller. Any controller that needs the menu adds a `UMenuBrowserHost` component: the menu controller shows it on BeginPlay; the Foundation controller shows it as the pause menu and adds its own `foundationmenu` bridge. Only the Foundation controller adds diagnostics (input router, HUD, panels), so the plain menu map has no F3 overlay or input preprocessor.

## Menu host and native services

`UMenuBrowserHost` adds one full-viewport Slate `SWebBrowser` with no address bar, controls or thumb-button navigation, gives its owner UI-only input with a cursor, and binds the native services below. Each binding name is its JavaScript global lowercased, and each service exposes no arbitrary commands, paths or files.

| JS global | Binding | Native owner | Owns |
|---|---|---|---|
| `MenuVideo` | `menuvideo` | `UMenuVideoSettings` | Fullscreen, frame limit, VSync, GUI scale, menu blur; saved to `Saved/Config/MenuVideoSettings.ini`. |
| `MenuInput` | `menuinput` | `UMenuInputBridge` -> `UUnnamedInputSubsystem` | Player-mappable bindings and Hold/Toggle sprint/crouch; saved in the Enhanced Input SaveGame slot. |
| `MenuPreferences` | `menupreferences` | `UMenuPreferences` (GameInstance subsystem) | 101 further options, language, pack order, two flags; saved to `Saved/Config/MenuPreferences.json`. |
| `MenuApplication` | `menuapplication` | `UMenuApplication` | Quit Game and world travel. |
| `FoundationMenu` | `foundationmenu` | `UFoundationMenuBridge` | Foundation pause menu and binding-capture handoff (Foundation map only). |

Quit and world travel drain every settings store, lock further edits, flush all three native stores, then call Unreal's normal QuitGame or OpenLevel on a later tick. Editor Play cannot quit. Saving is crash-safe: `UnnamedFiles::SaveTextAtomically` writes a temporary file, backs up the previous file and moves the new one into place; loads fall back to the backup. `UnnamedSession::CanSaveSettings()` is the single rule that keeps editor and `-MenuSettingsSessionOnly` runs from saving.

## Project settings and world catalog

`UUnnamedProjectSettings` (Project Settings > Game > Unnamed; C++ defaults, overridable in `DefaultGame.ini`) is the one place for the menu map, the dev grid material, the input mapping contexts and actions, the HUD and block asset roots (`HudRoot`, `BlockRoot`), and the Singleplayer world catalog (ID, map, name, summary, status). The catalog holds one world, `playground`; the retired IDs `foundation`, `super-flat` and `interaction-lab` are never reused. `UMenuApplication::RequestOpenWorld` accepts only an exact, case-sensitive catalog ID and sends the catalog to the page in `GetState`; the page lists those worlds first, marks them built-in and keeps no list of its own. Editor Python reads the same paths through `Scripts/Editor/ProjectPaths.py`. Cooked maps come from `MapsToCook` in `DefaultGame.ini`.

## Menu web layer

`MenuWeb/AdaptMenu.mjs` checks the reference hash (from `Reference/OriginalHashes.json`) and regenerates `Content/Menu/Web/index.html` and `preferences-schema.menuprefs`. Each file in `MenuWeb/Patches/` adapts one feature (Core, Realms, Application, Video, Input, Preferences, SettingsRouting, Worlds, Dialogs) by editing original reference text only; `ReferencePatcher` rejects overlapping edits, so no patch depends on another patch's output, and every edit fails fast if its anchor moves. The structured reference data is kept, except the removed Realms preference. Edit the patches and `MenuWeb/Runtime/`, never the generated HTML.

The runtime modules are concatenated into the page in this order: `MenuBridgeRuntime` (shared `MenuBridge` client and `MenuStores` registry), `MenuPrototypeRuntime` (transaction snapshots, stable control IDs, diagnostics as `window.MenuPrototype`), `MenuDialogRuntime`, `MenuVideoRuntime`, `MenuInputRuntime`, `MenuPreferencesRuntime`, `MenuWorldsRuntime`, `MenuApplicationRuntime`, `FoundationMenuRuntime`. `MenuBridge.client` waits for a binding, looks up CEF's lowercased method names, bounds each request (five seconds; twenty for a whole quit or travel), parses JSON and validates replies. Late replies settle only their abandoned wait. The three settings stores register with `MenuStores`, so quit, travel and the pause menu drain or release them together. `MenuInput.setCaptureHooks` lets the Foundation pause menu tell the native input router when a binding capture starts and ends.

Realms is removed: no main entry, renderer, invite/news/purchase endpoint, join form, styles, Online Settings section or preference; `Patches/Realms.mjs` verifies it. Dialog drafts (world name, preset, rules, server) live in their dialog's `current.data`, survive redraws and child screens, and Done validates the whole draft.

The presentation baseline is the reference: English (US), Auto integer scaling, ordinary font and resources, baseline blur and panorama speed. GUI scale and blur change the live page; frame limit, VSync and fullscreen change the engine. Font, contrast, language and audio choices are saved while their effects remain pending. External links, clipboard, folders, purchases, servers and accounts end at reversible mock screens.

## Resource origin and packaging

The host builds an encoded `file:///` URL from ProjectContentDir. Navigation is limited to that document and `about:blank`; popups are rejected. A content-security policy rejects network connections, media, frames and forms. Credits are embedded, and the six panorama PNGs are embedded as data URLs because Chromium's file-origin rules block WebGL uploads of local images. HTML and assets are staged as loose NonUFS files by the module's RuntimeDependencies; no HTTP server is involved.

CSS alone applies integer scaling; no UMG DPI wrapper scales a second time. Verified browser viewports: 1920x1080 -> 480x270 at scale 4, 1280x720 -> 427x240 at scale 3.

## Diagnostics and logging

`UnnamedLog.h` defines `LogUnnamedMenu` (host, application, video, preferences, browser console), `LogUnnamedWorlds` (world catalog and travel; not yet used) and `LogUnnamedFoundation` (lab and probe). `window.MenuPrototype` exposes screen state, dimensions, controls and definitions for CEF tests. Playwright connects to Unreal's own CEF through the optional `-cefdebug` port. Slate tools can inspect the native window, not HTML buttons. The Foundation runtime probe (`-FoundationProbe=report.json`) exists only in builds with dev automation tests.

## Verification

`Tests/` holds the Node unit tests (`npm test`), CEF integration suites and source checks; `Scripts/Test*.ps1` run the native automation and the Foundation probe. Results and their evidence are recorded in [Verification](Verification.md) and `Tests/Evidence/<date>/`.
