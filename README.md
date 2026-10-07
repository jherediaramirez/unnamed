# Unnamed

Unreal Engine 5.8.3 project: `C:\dev\projects\Unnamed\Unnamed.uproject`. An offline, hash-checked Minecraft-style menu (hosted in Unreal's CEF browser) with persistent video, input and preference settings, normal Quit Game, and travel to two built-in worlds: the first-person **Player Foundation** lab and the 2 km **Super Flat** preview. Minecraft Realms is removed.

- [Architecture](Docs/Architecture.md) - how the pieces fit, folder by folder.
- [Naming conventions](Docs/NamingConventions.md) - how files, classes, tests and docs are named.
- [Player Foundation](Docs/Foundation/README.md) - controls, behavior and its own architecture.
- [Records](Docs/Records/README.md) - dated work records and known gaps; [Verification](Docs/Verification.md) - results and evidence.

## Launch

The current candidate is built from the code-review cleanup: `Packages\CodeReviewDevelopment20261007\Windows\Unnamed.exe` (Left Shift sprint, C crouch). Choose **Singleplayer → Player Foundation** for first-person movement, configurable controls, the primitive course and F3 diagnostics, or **Super Flat** for the flat test ground (temporary fly camera: WASD/mouse, E/Q; Escape returns to the menu).

```powershell
& 'C:\dev\projects\Unnamed\Scripts\LaunchFoundation.ps1' -PackageName CodeReviewDevelopment20261007
```

The previous candidates `Packages\FoundationDevelopment20261007-r3` and `Packages\SuperFlatDevelopment` are kept as preserved checkpoints.

Video settings, key bindings with their Hold/Toggle policies, and 101 further preferences save when changed and restore at launch. Hard Reset (bottom of Video Settings, debug tools) restores preferences without deleting world saves. Quit Game finishes pending saves and requests normal shutdown; a failed save cancels quitting. Editor Play uses temporary settings and cannot quit the editor. Window, FPS and VSync controls need the standalone game.

## Build

Requires Visual Studio 2022 (C++ and Windows SDK) and UE 5.8. Node 24 runs only the menu generator and tests.

```powershell
& 'C:\dev\projects\Unnamed\Scripts\BuildFoundation.ps1' -Package -PackageName FoundationNextDevelopment
```

The build regenerates the menu from `Reference/minecraft-menu` (`MenuWeb/AdaptMenu.mjs`), compiles the editor, runs the editor asset scripts (which preserve existing assets) and packages the maps listed in `Config/DefaultGame.ini`. Restart an open Unreal Editor after rebuilding C++. Package names must be new; earlier candidates are never overwritten.

## Verify

```powershell
Set-Location 'C:\dev\projects\Unnamed\Tests'
npm ci --ignore-scripts
npm test                                   # Node unit tests for the menu runtime
node source-check.mjs                      # reference hash, mirrored assets, generated script syntax
& '..\Scripts\TestMenu.ps1'                # native Unnamed.Menu suites (also TestInput.ps1, TestVideoSettings.ps1)
& '..\Scripts\TestFoundationRuntime.ps1' -Editor -FrameRates 60
node foundation-menu.mjs --editor          # CEF + native bridge end to end
```

Integration suites and probes write to `Tests/Evidence/<date>/` and refuse to overwrite existing evidence. Chromium diagnostics use loopback and only the diagnostic launch option (`LaunchMenu.ps1 -Diagnostics`) enables them. The native editor MCP endpoint is `http://127.0.0.1:18777/mcp`; `Scripts\InvokeUnrealMCP.ps1` is the rerunnable bridge.

The private source backup is [jherediaramirez/unnamed](https://github.com/jherediaramirez/unnamed); see [GitHub Backup](Docs/Records/2026-10-07-GitHub-Backup.md). The unchanged reference and its provenance are under `Reference/minecraft-menu`. Keep the asset provenance and rights information with distributions; this interface does not grant rights to Minecraft branding or assets.
