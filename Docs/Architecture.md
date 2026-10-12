# Architecture

How the project fits together. Foundation-specific movement, input and diagnostics details are in [Foundation architecture](Foundation/Architecture.md); naming rules are in [Naming conventions](NamingConventions.md).

## Layout

| Folder | Contents |
|---|---|
| `Source/Unnamed/` | The game module. Project-wide pieces at the root: `UnnamedProjectSettings` (maps, world catalog, input assets, HUD and block asset roots), `UnnamedSession` (may this run save settings?), `UnnamedFiles` (crash-safe saves), `UnnamedLog` (log categories). |
| `Source/Unnamed/Menu/` | The offline menu: `UMenuBrowserHost`, the menu services it binds, and the menu map's game mode/controller. |
| `Source/Unnamed/Input/` | Enhanced Input layer: `UUnnamedInputSubsystem`, the saved `UUnnamedInputSettings`, `FUnnamedInputIntent`. |
| `Source/Unnamed/Foundation/` | The first-person pawn, controller parts, pure simulation, player vitals (health, food, air, XP, attack recovery), lab geometry (`AFoundationLab`, and the Playground in `AFoundationPlayground`), runtime probe; the Playground's block world, items, targeting, authority, interaction state and presentation; the 26.3 block registry, placement rules, block meshes and texture animation ([Blocks](Blocks.md)). |
| `Source/Unnamed/Hud/` | Native Slate HUD, pure layout/selection, cooked sprite metadata and the Foundation adapter (player vitals by default, an opt-in preview fixture); the creative inventory screen and 3D block item icons. |
| `Source/Unnamed/Worlds/` | Saved worlds from Create New World: the `UnnamedSavedWorlds` store (`Saved/Worlds/<id>/world.json`, ids, draft checks) ([Voxel world](VoxelWorld.md#saved-worlds)). The world catalog stays in `UnnamedProjectSettings`. |
| `Source/Unnamed/Voxel/` | The voxel world's Unreal side ([Voxel world](VoxelWorld.md)): `UVoxelWorldSettings`, the `voxel.*` console, the registry-to-table bridge, the worldgen reader (`VoxelWorldgen`, JSON to the density graph), the column mesh `UVoxelChunkComponent`, the streaming subsystem `UVoxelWorldSubsystem` with its column pipeline `FVoxelColumnPipeline`, the storage adapter `FVoxelBlockStorage`, the `voxel.Fly` camera and the acceptance flight, and `AVoxelWorld`, the level actor of `/Game/Maps/L_Generated`. |
| `Source/UnnamedVoxel/` | The second module: the voxel world's plain C++ core (generation, the density graph, storage, meshing, the streaming planner). It depends on `Core` only and compiles with precise floating point. |
| `Source/Unnamed/Tests/` | Native automation tests, one file per area (`Unnamed.<Area>.*`). |
| `Plugins/RealtimeMeshComponent/` | Vendored MIT RealtimeMeshComponent Core at a pinned commit, for chunk meshes; never edited ([Voxel world](VoxelWorld.md#realtimemeshcomponent)). |
| `MenuWeb/` | Menu generator (`AdaptMenu.mjs` + `Patches/`) and the JavaScript shipped inside the page (`Runtime/`). |
| `Content/Menu/Web/` | Generated `index.html`, preference schema and mirrored reference assets, staged as loose files. |
| `Reference/minecraft-menu/` | The unchanged, hash-checked reference menu. |
| `Reference/minecraft-blocks-26.3/` | The generated, hash-checked Java 26.3 block reference that `Scripts/Editor/ImportBlocks.py` reads. |
| `Reference/minecraft-worldgen-26.3/` | The hash-checked Java 26.3 worldgen data (noise settings, density functions, noises) and Mojang's own spline values, which the voxel terrain's reader (`VoxelWorldgen`) turns into the core's density graph ([Voxel world](VoxelWorld.md#worldgen-data)). |
| `Scripts/` | PowerShell build/launch/test entry points; `Scripts/Editor/` holds Unreal editor Python. |
| `Tests/` | Node unit tests (`*.test.mjs`) and CEF integration suites; recorded evidence in `Tests/Evidence/<date>/`. |
| `Docs/` | Living docs; dated work records in `Docs/Records/`; preserved inputs in `Docs/Archive/`. |

## Maps and controllers

`/Game/Menu/L_Menu` uses `AMenuGameMode` and `AMenuPlayerController`: no pawn or HUD, just the menu. `/Game/Maps/L_Playground` uses `AFoundationGameMode` and `AFoundationController` with one placed `AFoundationPlayground` (the 2 km dev-grid ground, the starter-block stations, the block gallery and the movement course; see [Foundation architecture](Foundation/Architecture.md#playground-block-interaction) and [Playground](Playground.md)). It is the only world in the catalog.

`/Game/Maps/L_Generated` (`UUnnamedProjectSettings::GeneratedMap`, not in the catalog) uses the same Foundation game mode with one placed `AVoxelWorld`. Every saved world opens it as `L_Generated?VoxelWorld=<id>`. That actor is the map's `AFoundationLab`: it streams the world's terrain, built from the saved seed and type, around the player, gives the pawn its collision and spawn from block data, owns the authority for mining and placing, and gives the pawn the world's game mode and difficulty through `AFoundationLab::ConfigurePlayer` ([below](#voxel-world-streaming)).

Neither gameplay controller inherits from the menu controller. Any controller that needs the menu adds a `UMenuBrowserHost` component: the menu controller shows it on BeginPlay; the Foundation controller shows it as the pause menu and adds its own `foundationmenu` bridge. Only the Foundation controller adds diagnostics (input router, HUD, panels), so the plain menu map has no F3 overlay or input preprocessor.

## Voxel world streaming

`AVoxelWorld` starts `UVoxelWorldSubsystem`, and the two split the work ([Voxel world](VoxelWorld.md#streaming)):

- **`UVoxelWorldSubsystem`** is the game-thread driver. Each tick it takes the player's camera as the focus and does three things within `voxel.ApplyBudgetMs`: plan (the core's planner, `Source/UnnamedVoxel/Streaming/`, pure functions, decides which columns to load and unload), start jobs, and apply finished meshes to pooled `UVoxelChunkComponent`s.
- **`FVoxelColumnPipeline`** is plain C++ with no UObjects, so tests drive it without a world. It owns one record per column: the data, the `UE::Tasks` jobs (generate, then mesh once the 3 x 3 neighbourhood exists), the section mesh cache near the center, and the column's edit list.
- **`AVoxelWorld`**, the lab, holds the saved world and its terrain, reads the pipeline's columns for collision, spawn and water, and owns the `FFoundationAuthority`.
- **`FVoxelBlockStorage`** is an `IFoundationBlockStorage` over the pipeline, which the authority's block world takes through `SetStorage`. A cell is loaded when its column is generated. A write goes to the pipeline's `SetCell`, which copies the column and marks the touched sections dirty; the pipeline then remeshes them.

Targeting, mining, placing, the HUD and the creative inventory stay the Foundation's own code ([Foundation architecture](Foundation/Architecture.md#playground-block-interaction)). The interaction view presents the outline, cracks, held item and effects, while the chunk meshes draw the blocks.

## Menu host and native services

`UMenuBrowserHost` adds one full-viewport Slate `SWebBrowser` with no address bar, controls or thumb-button navigation, gives its owner UI-only input with a cursor, and binds the native services below. Each binding name is its JavaScript global lowercased, and each service exposes no arbitrary commands, paths or files.

| JS global | Binding | Native owner | Owns |
|---|---|---|---|
| `MenuVideo` | `menuvideo` | `UMenuVideoSettings` | Fullscreen, frame limit, VSync, GUI scale, menu blur; saved to `Saved/Config/MenuVideoSettings.ini`. |
| `MenuInput` | `menuinput` | `UMenuInputBridge` -> `UUnnamedInputSubsystem` | Player-mappable bindings and Hold/Toggle sprint/crouch; saved in the Enhanced Input SaveGame slot. |
| `MenuPreferences` | `menupreferences` | `UMenuPreferences` (GameInstance subsystem) | 101 further options, language, pack order, two flags; saved to `Saved/Config/MenuPreferences.json`. |
| `MenuApplication` | `menuapplication` | `UMenuApplication` | Quit Game, world travel and saved worlds (create, list, rename, delete); worlds are saved to `Saved/Worlds/<id>/world.json`. |
| `FoundationMenu` | `foundationmenu` | `UFoundationMenuBridge` | Foundation pause menu and binding-capture handoff (Foundation map only). |

Quit and world travel drain every settings store, lock further edits, flush all three native stores, then call Unreal's normal QuitGame or OpenLevel on a later tick. Editor Play cannot quit. Saving is crash-safe: `UnnamedFiles::SaveTextAtomically` writes a temporary file, backs up the previous file and moves the new one into place; loads fall back to the backup. `UnnamedSession::CanSaveSettings()` is the single rule that keeps editor and `-MenuSettingsSessionOnly` runs from saving settings. Saved worlds are the player's content, not settings, and always save; `-WorldsDir=` moves them for tests.

### The pause menu in a world

The Foundation controller reads two switches when a world starts (`AFoundationController::BeginPlay`; change one, then reload the world). Each keeps the behaviour until 2026-10-12 one value away ([record](Records/2026-10-12-Pause-Menu-Flash.md)).

| Switch | Values | Default |
|---|---|---|
| `foundation.Pause.Menu` | 0 a fresh opaque browser per ESC that starts on the title screen and is destroyed on resume: about 0.2-0.4 s of black, then a title frame (until 2026-10-12). 1 one browser per world, made at world start on `index.html#pause` (transparent, invisible until its pause screen has painted, then collapsed); ESC shows it and the page reports `MenuShown` within a frame or two; resume collapses it. 2 (diagnostic) a fresh browser per ESC, transparent until painted: no black, the old delay. `IsOpen()` means shown; `HasBrowser()` means it exists. | 1 |
| `foundation.Pause.Background` | Modes 1 and 2 only. 0 the title screen's blurred panorama (until 2026-10-12). 1 the world behind the menu (`#pause&bg=world`): a transparent page with vanilla's `inworld_menu_background` instead of the panorama, and a Slate `SBackgroundBlur` under the browser (Z 99) at Menu Background Blur x viewport height / 1080 (a chosen mapping; vanilla's strength is unmeasured). | 1 |

## Project settings and world catalog

`UUnnamedProjectSettings` (Project Settings > Game > Unnamed; C++ defaults, overridable in `DefaultGame.ini`) is the one place for the menu map, the dev grid material, the input mapping contexts and actions, the HUD and block asset roots (`HudRoot`, `BlockRoot`), and the Singleplayer world catalog (ID, map, name, summary, status). The catalog holds one world, `playground`; the retired IDs `foundation`, `super-flat` and `interaction-lab` are never reused. `UMenuApplication::RequestOpenWorld` accepts an exact, case-sensitive catalog ID or a saved world's id. `GetState` sends the catalog (`worlds`) and the saved worlds (`savedWorlds`, most recently played first, each with its settings for Re-Create). The page lists the catalog first, marked built-in, then the saves, and keeps no list of its own. Editor Python reads the same paths through `Scripts/Editor/ProjectPaths.py`. Cooked maps come from `MapsToCook` in `DefaultGame.ini`.

## Menu web layer

`MenuWeb/AdaptMenu.mjs` checks the reference hash (from `Reference/OriginalHashes.json`) and regenerates `Content/Menu/Web/index.html` and `preferences-schema.menuprefs`. Each file in `MenuWeb/Patches/` adapts one feature (Core, Realms, Application, Video, Input, Preferences, SettingsRouting, Worlds, Dialogs) by editing original reference text only; `ReferencePatcher` rejects overlapping edits, so no patch depends on another patch's output, and every edit fails fast if its anchor moves. The structured reference data is kept, except the removed Realms preference. Edit the patches and `MenuWeb/Runtime/`, never the generated HTML.

The runtime modules are concatenated into the page in this order: `MenuBridgeRuntime` (shared `MenuBridge` client and `MenuStores` registry), `MenuPrototypeRuntime` (transaction snapshots, stable control IDs, diagnostics as `window.MenuPrototype`), `MenuDialogRuntime`, `MenuVideoRuntime`, `MenuInputRuntime`, `MenuPreferencesRuntime`, `MenuWorldsRuntime`, `MenuApplicationRuntime`, `FoundationMenuRuntime`. `MenuBridge.client` waits for a binding, looks up CEF's lowercased method names, bounds each request (five seconds; twenty for a whole quit or travel), parses JSON and validates replies. Late replies settle only their abandoned wait. The three settings stores register with `MenuStores`, so quit, travel and the pause menu drain or release them together. `MenuInput.setCaptureHooks` lets the Foundation pause menu tell the native input router when a binding capture starts and ends.

Realms is removed: no main entry, renderer, invite/news/purchase endpoint, join form, styles, Online Settings section or preference; `Patches/Realms.mjs` verifies it. Dialog drafts (world name, preset, rules, server) live in their dialog's `current.data`, survive redraws and child screens, and Done validates the whole draft.

The presentation baseline is the reference: English (US), Auto integer scaling, ordinary font and resources, baseline blur and panorama speed. GUI scale and blur change the live page; frame limit, VSync and fullscreen change the engine. Font, contrast, language and audio choices are saved while their effects remain pending. External links, clipboard, folders, purchases, servers and accounts end at reversible mock screens.

## Resource origin and packaging

The host builds an encoded `file:///` URL from ProjectContentDir. Navigation is limited to that document and `about:blank`; popups are rejected. A content-security policy rejects network connections, media, frames and forms. Credits are embedded, and the six panorama PNGs are embedded as data URLs because Chromium's file-origin rules block WebGL uploads of local images. HTML and assets are staged as loose NonUFS files by the module's RuntimeDependencies; no HTTP server is involved.

CSS alone applies integer scaling; no UMG DPI wrapper scales a second time. Verified browser viewports: 1920x1080 -> 480x270 at scale 4, 1280x720 -> 427x240 at scale 3.

## Diagnostics and logging

`UnnamedLog.h` defines `LogUnnamedMenu` (host, application, video, preferences, browser console), `LogUnnamedWorlds` (saved worlds: created, opened, deleted) and `LogUnnamedFoundation` (lab and probe). `window.MenuPrototype` exposes screen state, dimensions, controls and definitions for CEF tests. Playwright connects to Unreal's own CEF through the optional `-cefdebug` port. Slate tools can inspect the native window, not HTML buttons. The Foundation runtime probe (`-FoundationProbe=report.json`) exists only in builds with dev automation tests.

## Verification

`Tests/` holds the Node unit tests (`npm test`), CEF integration suites and source checks; `Scripts/Test*.ps1` run the native automation and the Foundation probe. Results and their evidence are recorded in [Verification](Verification.md) and `Tests/Evidence/<date>/`.
