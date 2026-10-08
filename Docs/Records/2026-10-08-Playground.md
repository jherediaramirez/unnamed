# Playground - October 8, 2026

One world now holds every feature. The Player Foundation, Super Flat and Interaction Lab worlds are retired and their maps deleted. The Playground (catalog ID `playground`, map `/Game/Maps/L_Playground`) replaces them. Layout and how-to are in [Playground](../Playground.md).

## What changed

- **Worlds:** `foundation`, `super-flat` and `interaction-lab` removed from the catalog; their maps deleted. `Scripts/Editor/CreatePlaygroundMap.py` authors the one map: `AFoundationPlayground` on `AFoundationGameMode`, a PlayerStart at the origin, the Super Flat daylight rig, fixed exposure, light height fog and kill Z -10000.
- **C++:** `AFoundationInteractionLab` renamed `AFoundationPlayground` (`Foundation/FoundationPlayground.{h,cpp}`). `AFoundationLab` builds `AddGround()` (2 km x 2 km slab, top z = 0, `GroundHalfExtentCm` 100000) and `BuildMovementCourse()` around `CourseOrigin`. Solids go through the virtual `AddSolid`.
- **Super Flat code deleted:** `ASuperFlatGameMode`, `ASuperFlatPreviewController`, `ASuperFlatPreviewHUD` and `Source/Unnamed/SuperFlat/`. The `-SuperFlatSmokeTest` flag is gone.
- **Dev grid:** `/Game/DevGrid/M_DevGrid` (clear coat, world-aligned, 1 m tiles, 10 m major lines, axis lines at the origin). Its path is `UUnnamedProjectSettings::DevGridMaterial`. Lab solids, blocks and training targets use it; held items, particles and the mining overlay keep the engine material.
- **Scripts:** `TestFoundationRuntime.ps1` always runs `L_Playground`; `-InteractionLab` became `-Suite interaction`. `LaunchFoundation.ps1 -DirectLevel` opens the Playground. `BuildFoundation.ps1` and `BuildMenu.ps1` run `CreateDevGridMaterial.py` and `CreatePlaygroundMap.py`. `SetupFoundation.py` no longer creates a map. Deleted: `CreateSuperFlatMap.py`, `VerifySuperFlatMap.py`, `SetupInteractionLab.py`, `Tests/super-flat.mjs`. The click, Enter and double-click travel coverage moved into `Tests/foundation-menu.mjs`.
- **Tests:** `Unnamed.Worlds.RoutingAndIsolation` (retired IDs and paths rejected; world game mode is the Foundation one) and `Unnamed.Worlds.Catalog` (exactly one world; dev grid material loads) replace the Super Flat checks.
- **Config:** `MapsToCook` lists `L_Menu` and `L_Playground`; `/Game/DevGrid` is always cooked.

## Decisions

- **Retire, do not rename.** Each old ID named a world with its own map and rules. One new ID, `playground`, means an old name never points at different behavior. Retired IDs are never reused. World selection is in memory, so no saved data refers to them and nothing migrates.
- **Anchor rule.** Every Playground solid is an authority static: targetable, occluding and blocking placement. Only grid-aligned solids (minimum and maximum on 100 cm multiples) anchor placement. Thin walls and the low ceiling occlude without anchoring, so they cannot become a placement base.
- **Token fixtures removed.** The block fixture (4.5 reach), entity fixture, occluder, hidden entity and ruler strips are gone from the course; the stations use real Dummy Blocks and training targets. `FFoundationActions` remains the test double for the native ActionContracts test and the plain-lab fallback the pawn spawns when a map has no lab.
- **Line grids removed.** The interaction view no longer draws line-batch cell grids. The dev grid material provides the measuring lines.
- **Bounds grew.** The block world covers the whole ground: cells (-1000, -1000, 0) to (999, 999, 63), instead of 40 x 40 x 16 cells. Section iteration over huge ranges visits only existing sections, so empty ground costs nothing.
- **Course moved east.** The movement course origin is `(6000, 0, 0)`, 60 m east of spawn; its elements span 42 to 82 m. The stations stay around the origin, and a sign at 25 m points to the course.

## Verification

- Editor build: succeeded. Native suites (`Automation RunTests Unnamed`): 37/37.
- In-game probes, all suites with HUD captures (`TestFoundationRuntime.ps1 -Editor -FrameRates 60 -HudCaptures`): 139 checks, 0 failures (`Tests/Evidence/2026-10-08/Playground-all/Runtime-20261008-114114`).
- CEF end to end (`node foundation-menu.mjs --editor`): 39/39, menu to Playground by button, double click and Enter. `npm test` 53/53; `source-check.mjs` passed.
- Screenshot tour (`-Suite tour -FullCostRendering`): `Tests/Evidence/2026-10-08/Playground-tour/Runtime-20261008-114355` (spawn, grid, building, mining, combat, course, water, F3).
- Fixes found while verifying: exposure EV100 12 to 14.5 (the 75,000 lux sun washed the grid out) with `r.EyeAdaptation.CachedLightingPreExposure=8` (default clipped Lumen cached lighting); `M_DevGrid` needs `used_with_instanced_static_meshes` or placed blocks fall back to the engine checker material; `TestFoundationRuntime.ps1` `-UserDir` trailing backslash swallowed the following arguments.

Backup of the pre-change working tree: `C:\dev\backups\Unnamed\playground-pre-20261008-101820`.
