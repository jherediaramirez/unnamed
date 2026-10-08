# Unnamed

Unreal Engine 5.8.3 project: `C:\dev\projects\Unnamed\Unnamed.uproject`. An offline, hash-checked Minecraft-style menu (hosted in Unreal's CEF browser) with persistent video, input and preference settings, normal Quit Game, and travel to the one built-in world, the **Playground**: a first-person pawn on a 2 km dev grid with all Java 26.3 full-cube blocks and slabs (placement, mining, a block gallery and the creative inventory), practice combat, a movement course and the HUD. Minecraft Realms is removed.

- [Architecture](Docs/Architecture.md) - how the pieces fit, folder by folder.
- [Naming conventions](Docs/NamingConventions.md) - how files, classes, tests and docs are named.
- [Playground](Docs/Playground.md) - the built-in world: layout, dev grid, how to create, play and test it.
- [Player Foundation](Docs/Foundation/README.md) - controls, behavior and its own architecture.
- [Records](Docs/Records/README.md) - dated work records and known gaps; [Verification](Docs/Verification.md) - results and evidence.

## Launch

The Playground is the only built-in world. Choose **Singleplayer → Playground → Play Selected World** (double-click and Enter also work). Left Shift sprints and C crouches. The Playground has placement of every Java 26.3 full-cube block and slab (including while jumping and falling; E opens the creative inventory), mining with a practice pickaxe, a training target for a practice sword, and a movement course 42 to 82 m east of spawn; edits last for the session only. Player vitals and the Java-style HUD (falls, drowning, hunger, regeneration, XP, attack recovery) run in the same world, and F3 diagnostics and F3+F6 controls are described in the [Foundation README](Docs/Foundation/README.md). [Player vitals](Docs/Records/2026-10-07-Vitals.md) and [HUD milestone 1](Docs/Records/2026-10-07-Hud.md) record scope, evidence and gaps; the [interaction record](Docs/Records/2026-10-08-Interaction.md) has the placement and mining rules, presets and evidence. [Playground](Docs/Playground.md) has the layout.

No packaged candidate contains the Playground yet. `Packages\InteractionDevelopment20261008-r3\Windows\Unnamed.exe` is the newest package and still has the retired Interaction Lab; build a fresh candidate as described under Build.

```powershell
& 'C:\dev\projects\Unnamed\Scripts\LaunchFoundation.ps1' -Editor -DirectLevel                          # editor: straight into the Playground
& 'C:\dev\projects\Unnamed\Scripts\LaunchFoundation.ps1' -PackageName <FreshName> -DirectLevel         # a candidate built after the Playground exists
```

The previous candidates `Packages\InteractionDevelopment20261008-r3` (retired worlds), `Packages\InteractionDevelopment20261008-r2`, `Packages\InteractionDevelopment20261008`, `Packages\VitalsDevelopment20261007-r2`, `Packages\HudDevelopment20261007-r2`, `Packages\FoundationDevelopment20261007-r3` and `Packages\SuperFlatDevelopment` are kept as preserved checkpoints.

Video settings, key bindings with their Hold/Toggle policies, and 101 further preferences save when changed and restore at launch. Hard Reset (bottom of Video Settings, debug tools) restores preferences without deleting world saves. Quit Game finishes pending saves and requests normal shutdown; a failed save cancels quitting. Editor Play uses temporary settings and cannot quit the editor. Window, FPS and VSync controls need the standalone game.

## Build

Requires Visual Studio 2022 (C++ and Windows SDK) and UE 5.8. Node 24 runs only the menu generator and tests.

```powershell
& 'C:\dev\projects\Unnamed\Scripts\BuildFoundation.ps1' -Package -PackageName FoundationNextDevelopment
```

The build regenerates the menu from `Reference/minecraft-menu` (`MenuWeb/AdaptMenu.mjs`), compiles the editor, runs the editor asset scripts (which preserve existing assets, including the dev grid material and the Playground map) and packages the maps listed in `Config/DefaultGame.ini`. Restart an open Unreal Editor after rebuilding C++. Package names must be new; earlier candidates are never overwritten.

## Verify

```powershell
Set-Location 'C:\dev\projects\Unnamed\Tests'
npm ci --ignore-scripts
npm test                                   # Node unit tests for the menu runtime
node source-check.mjs                      # reference hash, mirrored assets, generated script syntax
& '..\Scripts\TestMenu.ps1'                # native Unnamed.Menu suites (also TestInput.ps1, TestVideoSettings.ps1)
& '..\Scripts\TestFoundationRuntime.ps1' -Editor -FrameRates 60
& '..\Scripts\TestFoundationRuntime.ps1' -Editor -FrameRates 60                       # Playground: movement and interaction suites
& '..\Scripts\TestFoundationRuntime.ps1' -Editor -Suite interaction -FrameRates 60    # add -Preset LucidFluid for the other preset
node foundation-menu.mjs --editor          # CEF + native bridge end to end
```

Integration suites and probes write to `Tests/Evidence/<date>/` and refuse to overwrite existing evidence. Chromium diagnostics use loopback and only the diagnostic launch option (`LaunchMenu.ps1 -Diagnostics`) enables them. The native editor MCP endpoint is `http://127.0.0.1:18777/mcp`; `Scripts\InvokeUnrealMCP.ps1` is the rerunnable bridge.

The private source backup is [jherediaramirez/unnamed](https://github.com/jherediaramirez/unnamed); see [GitHub Backup](Docs/Records/2026-10-07-GitHub-Backup.md). The unchanged reference and its provenance are under `Reference/minecraft-menu`. Keep the asset provenance and rights information with distributions; this interface does not grant rights to Minecraft branding or assets.
