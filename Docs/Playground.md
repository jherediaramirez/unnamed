# Playground

The Playground is the one built-in world. Every feature is tried here: the first-person Foundation pawn, the dev grid, placing and mining the Java 26.3 blocks ([Blocks](Blocks.md)), the block gallery and the creative inventory, practice combat, the movement course and the HUD. New features go into this world, not into new worlds. Edits last for the session only; nothing is saved or networked.

| | |
|---|---|
| Catalog ID | `playground` (saved ID, never reused) |
| Name | Playground |
| Map | `/Game/Maps/L_Playground` (cooked through `MapsToCook` in `Config/DefaultGame.ini`) |
| Game mode | `AFoundationGameMode`, with `AFoundationPawn` and `AFoundationController` (the controller owns the Foundation HUD and pause menu) |
| Level actor | One `AFoundationPlayground`, which builds the ground, stations and course at BeginPlay |

The map holds only what cannot be built at runtime: that actor, a PlayerStart at the origin, a sun, sky atmosphere and skylight, a fixed-exposure post-process (EV100 12) and thin height fog (density 0.004, from 30 m) so the ground edge fades. The daylight rig is the one from the retired Super Flat map. There is no floor mesh. Kill Z is -10000 cm; falling out of the lab respawns at spawn.

## Layout

Units are Unreal centimetres: 100 cm is one block, one metre. +X is east, +Y south, +Z up. The origin is spawn: the PlayerStart and the block-world origin, feet at z = 0. Blocks built at the origin lift the feet onto the stack rather than inside it.

The ground is one flat 2 km x 2 km slab centred on the origin, top at z = 0. The block world covers all of it: cells (-1000, -1000, 0) to (999, 999, 63), so 2000 x 2000 x 64 cells. Placement outside those cells is refused.

Stations around spawn (metres from the origin):

| Zone | Where | Contents |
|---|---|---|
| Starter blocks | x 3..7, y -1..4; (12, 6); (-8, -7) | 13 stone blocks, restored by every reset. One sits raised 1 m at (4, 0). |
| Lab controls | x -4..-3, y -2..3 | Placement preset (Use cycles presets), reset lab, supply (infinite, or finite with tool wear). |
| Ledge | x -13..-10, y -3..3; steps x -10..-8, y 2..3 | 3 m ledge with 2 m and 1 m steps: crouch at the edge, build out, or jump off and place while falling. |
| Low ceiling | x 5..9, y -10..-6 | Underside at 2.8 m: exact-contact stacking. |
| Occluding wall | x -7, y -9..-5 | 2.5 m thin wall; blocks reach. |
| Water | x 10..13, y 4..7 | 2 m of still water: eyes underwater, mining x0.2. |
| Target wall | x 3..7, y 9 | 2.5 m thin wall. |
| Training target | x -0.3..0.3, y 9.7..10.3 | Damageable, 1.8 m tall. |
| Target behind wall | x 4.7..5.3, y 9.7..10.3 | Damageable; the wall blocks attacks. |
| Block showcase | from (16, 14), rows 3 m apart running east | One labelled row each: grass, leaves, translucent, axis and facing, slabs, water levels, animated. |
| Registry gallery | from (-70, -22), 24 columns, 2 m apart | Every block's default state in registry order. |
| State gallery | from (-200, -58), 59 columns, 2 m apart | Every block state in state-id order, as Minecraft's debug world. |

Movement course. Its origin `(6000, 0, 0)` is 60 m east of spawn; its elements span 42 to 82 m east and 10 m north to 9 m south.

| Element | Where (m east, m south) | Tests |
|---|---|---|
| 50, 60, 61 and 100 cm steps | x 63..74, y -2.5..-0.5 | 50 and 60 cm step up; 61 cm is blocked; 100 cm needs a jump. |
| 155 cm ceiling | x 63..68, y 3..7 | Crouch under. |
| Gaps | x 53..57, y 3..9 | 59 cm gap blocked, 61 cm gap clear, either side of a wall. |
| Raised ledge and access steps | ledge x 45..50, y -10..-5, 2 m; steps x 42..45, y -8..-6 (50, 100, 150 cm) | Ledge access. |
| Corner comparison | x 54..55, y -6..-5, 2 m | Box corner for collision comparison. |
| Jump stairs and drop | stairs x 75..79, y -10..-8 (100 to 400 cm); drop x 79..82, same y (500 cm) | Step up one block at a time; the five-block drop costs fall damage. |
| Water column | x 75..78, y 3..6, 3 m | Air use and drowning; no swimming physics. |

A sign at 25 m east, reading "MOVEMENT COURSE 42 m EAST OF SPAWN", points to it.

## Dev grid

Ground, lab solids, blocks and training targets use one material, `/Game/DevGrid/M_DevGrid`. It is world-aligned (triplanar on the dominant normal axis), so the floor, cubes and blocks share one metric grid.

- 1 m tiles with seams. Corner crosses and the "1m" label come from the mask `/Game/DevGrid/T_DevGridMarkup`.
- 10 m major lines; red X and green Y axis lines through the origin.
- Subtle per-tile variation; clear coat with a fresnel fade.
- Parameters: `Color` (set per instance by the code), `SeamColor`, `SeamOpacity`, `LabelOpacity`, `CrossOpacity`, `TileVariation`, `MajorLineOpacity`, `AxisLines`, `Roughness`, `ClearCoat`, `ClearCoatRoughness`, `ClearCoatFresnelFade`.
- Held items, particles and the mining overlay keep the engine's BasicShapeMaterial, since a world-aligned grid would swim on moving cubes.
- Path: `UUnnamedProjectSettings::DevGridMaterial` (Python: `ProjectPaths.dev_grid_material()`). `/Game/DevGrid` is always cooked.
- Art source: `Art/DevGrid/T_DevGridMarkup.png`, made by `uv run --with pillow python Scripts/Editor/CreateDevGridTexture.py`. Assets are made by `Scripts/Editor/CreateDevGridMaterial.py`.
- Inspired by the forum post [Fancy dev grid material](https://forums.unrealengine.com/t/fancy-dev-grid-material/2601911). This grid is rebuilt procedurally; that version is a Fab asset and is not used.

## Create

The map is authored by editor Python, not by hand. Run both after the editor target has compiled. `BuildFoundation.ps1` and `BuildMenu.ps1` run them too.

```powershell
$editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'C:\dev\projects\Unnamed\Unnamed.uproject'
& $editor $project -run=pythonscript "-script=C:\dev\projects\Unnamed\Scripts\Editor\CreateDevGridMaterial.py" -unattended -nop4 -nosplash
& $editor $project -run=pythonscript "-script=C:\dev\projects\Unnamed\Scripts\Editor\CreatePlaygroundMap.py" -unattended -nop4 -nosplash
```

- `CreatePlaygroundMap.py` never rewrites an existing map. It verifies the map and raises if it is not set up as expected. To rebuild from nothing, close any editor that has it open, delete `Content/Maps/L_Playground.umap` and rerun.
- `CreateDevGridMaterial.py` rebuilds the material on the same asset, so existing instances keep working. To prove the shaders compile, add `-AllowCommandletRendering`.

## Play

- Menu: `Scripts\LaunchFoundation.ps1`, then **Singleplayer → Playground → Play Selected World**. Double-click and Enter also work.
- Direct: `Scripts\LaunchFoundation.ps1 -DirectLevel` opens the map in a packaged candidate built after the Playground exists (`BuildFoundation.ps1 -Package -PackageName <FreshName>`, then `-PackageName <FreshName>`). `-Editor -DirectLevel` runs the editor executable with `-game`.
- Editor: open `Content/Maps/L_Playground` and press Play, or press Play in `L_Menu` and choose Playground. Editor Play uses temporary settings and cannot quit the editor.
- Blocks: the loadout starts with stone, the iron pickaxe and the iron sword ([Items](Items.md)). E opens the creative inventory: pick any block onto the hotbar ([Blocks](Blocks.md#creative-inventory)).
- Controls and HUD: [Foundation README](Foundation/README.md). Interaction rules: [interaction record](Records/2026-10-08-Interaction.md#play).

## Test

- `Scripts\TestFoundationRuntime.ps1 -Editor -FrameRates 60` runs both suites; `-Suite movement` or `-Suite interaction` runs one. `-Preset VanillaReference` or `-Preset LucidFluid` and `-Finite` set the interaction options. `-HudCaptures` adds HUD captures. `-Suite tour` takes the screenshot tour, including the block gallery and creative inventory shots. Omit `-Editor` and pass `-PackageName` to test a packaged candidate.
- Each run writes `Tests/Evidence/<date>/Playground-<suite>/Runtime-<time>/` and refuses to overwrite it.
- `node foundation-menu.mjs --editor` (from `Tests`): CEF and native end to end, including Singleplayer → Playground, travel and return.
- Native: `Unnamed.Worlds.RoutingAndIsolation` (retired IDs and paths rejected; the world game mode is the Foundation one) and `Unnamed.Worlds.Catalog` (exactly one world; the dev grid material loads). `Unnamed.Foundation.*` covers the rest. Commands are in [CLAUDE.md](../CLAUDE.md).

## Retired worlds

| ID | Name | Map | Removed |
|---|---|---|---|
| `foundation` | Player Foundation | `/Game/Maps/L_Foundation` | Map. |
| `super-flat` | Super Flat | `/Game/Maps/L_SuperFlat` | Map, `Source/Unnamed/SuperFlat/`, `CreateSuperFlatMap.py`, `VerifySuperFlatMap.py`, `-SuperFlatSmokeTest`, `Tests/super-flat.mjs`. |
| `interaction-lab` | Interaction Lab | `/Game/Maps/L_InteractionLab` | Map, `SetupInteractionLab.py`, `-InteractionLab`. |

- These IDs are never reused. World selection is in memory and no saved player data refers to them, so nothing migrates.
- Super Flat's lighting moved into the Playground. The Foundation code and its tests remain.
- Dated records describe the worlds as they were. Older packages (for example `Packages\SuperFlatDevelopment`) still contain their maps; they are preserved, not rebuilt.
