# Voxel world phase 3: streaming and editing - October 8, 2026

The voxel world now streams. Columns load and unload around the player's camera, nearest first, and all chunk work on the game thread stays inside a 2 ms budget. Mining and placing work on generated terrain through the Foundation authority, and the edge of the view fades into fog instead of showing a cut. `voxel.Fly` is a free camera, and `CaptureVoxel.ps1 -Flight` flies a scripted path that checks for holes, the budget and memory. At view radius 12 that flight ends with no holes, 0 of 16,736 frames over the budget (worst 1.81 ms) and flat memory. Phase 2W and the handoff were committed first as `4987cdb` and `9dce565`. The design is in [Voxel world, Streaming](../VoxelWorld.md#streaming).

## What changed

- **Core (`Source/UnnamedVoxel/`):**
  - `Streaming/VoxelStreamingPlanner`, new, pure functions:
    - `FVoxelStreamingRings` measures the gap between the center column's square and another column's square, in columns;
    - in view: gap <= `ViewRadius`, meshed and drawn;
    - needed: in view or beside a column in view, and generated, because a section meshes from its 3 x 3;
    - unloaded beyond `ViewRadius + 1 + UnloadMargin`, the prompt's "unload radius = load radius + 2". `MinUnloadMargin` is 2, which covers a diagonal move;
    - at radius 12: 545 columns in view, 657 generated, unload beyond 15;
    - `VoxelStreaming::GetNeededColumns` orders by distance, then ahead of the view sector (8 sectors of yaw), then Y, then X. `GetUnloads`, `Plan` and `GetViewSector` complete it.
  - `Storage/VoxelColumn`: `SetState` sets a cell and its heightmaps, through a helper shared with `Build`.
  - `Meshing/VoxelMesher`: `FVoxelSectionKey` and `GetSectionsTouching`, the remesh rule.
  - `Terrain/VoxelHeightTerrain` and `VoxelFlatTerrain`: `Dense.Init` instead of `SetNumZeroed` (see Problems).
- **Unreal side (`Source/Unnamed/Voxel/`):**
  - `VoxelColumnPipeline` (`FVoxelColumnPipeline`), new, plain C++ with no UObjects and tested without a world:
    - one record per column;
    - `UE::Tasks` jobs at `BackgroundHigh`, up to `voxel.MaxTasksInFlight` (0 = twice the workers, 60 here). Walking the needed columns nearest first, a column gets a mesh job when it is in view and its 3 x 3 is generated, and a generation job otherwise;
    - a mesh job meshes the dirty sections (all of them when it has no cache), skips all-air sections, then builds the stream set and the Lumen cards;
    - stale results are dropped, by a serial per record (unload) and a mesh version (re-dirtied while meshing);
    - `SetCell` copies the column (copy on write, about 10 KB), sets the cell and its heightmaps, and marks dirty exactly the sections whose padded 18^3 box holds the cell: up to 8 sections in 4 columns;
    - edits are kept per column with a stamp (the block world's revision) and replayed after generation, so they survive unloading. They last for the session until phase 10;
    - the section mesh cache is kept within 2 columns of the center: 1.3 MB at the first view, and empty while flying, because new columns are meshed far away. An edit there remeshes only the touched sections;
    - `voxel.Debug.EdgeWalls` meshes the view's edge against air (captures).
  - `VoxelWorldSubsystem` (`UVoxelWorldSubsystem`), new, a `UTickableWorldSubsystem` for game and PIE worlds, started by `AVoxelWorld`:
    - focus: the player's camera, so `voxel.Fly` works; captures pin it to the spawn;
    - budget: plan, jobs and apply together stay within `voxel.ApplyBudgetMs` (2 ms) in a frame. A column's apply cost is estimated from its quads (a line fitted to measured applies, weighted to recent ones, times 1.5), and applying stops when the estimate would pass 85% of the budget. A frame's first column always goes, and each measured apply counts as at most 4 times its estimate plus 1 ms;
    - chunk components are pooled; columns out of view stay loaded but hidden;
    - `stat Voxel` counters, a log line every second with the worst frame's plan, jobs and apply split, and a first-view report in the captures' `build.txt`.
  - `VoxelBlockStorage` (`FVoxelBlockStorage`), new: an `IFoundationBlockStorage` over the pipeline, reached through a getter so tests pass their own pipeline. A cell is loaded when its column is generated. `Empty` drops the edits.
  - `VoxelFlyCamera` (`AVoxelFlyCamera`), new: `voxel.Fly [blocks per second]`, default speed 40. W, A, S, D fly along the view, Space and Left Ctrl rise and sink, and Left Shift is x4. The keys are physical keys, not the player's bindings. The player is frozen (actor tick off, gameplay input off).
  - `VoxelFlight` (`FVoxelFlight`), new, the acceptance flight (`-VoxelFlight=<folder>`, or `CaptureVoxel.ps1 -Flight`):
    - path: hover 2 s; east at 20, 40 and 80 blocks per second for 8 s each and at 160 for 6 s; then 3 laps of a circle of radius 288 blocks (1.5 view radii) at 80. Altitude 180 blocks, pitch -20;
    - it writes `flight.csv` (every frame), `flight.txt` and 4 screenshots. One frame moves the camera at most 50 ms of flight, because the flight's own screenshots cause hitches of about 400 ms;
    - a hole is a column closer than the fog's start (`ViewRadius - 1.5` as a gap), in the frustum, and never on screen. Columns still arriving inside the fade are counted apart as late columns.
  - `VoxelChunkComponent`: `SetMesh` makes one RealtimeMesh update (`FRealtimeMeshUpdateBuilder`) for the buffer set, the sections and the cards, instead of up to six. The layer sections come from a poly-group section handler set when the set is created.
  - `VoxelWorld` (`AVoxelWorld`), the lab:
    - streams through the subsystem; the fixed area is gone;
    - owns an `FFoundationAuthority` over `FVoxelBlockStorage`, with bounds Z -64 to 319 and X and Y ±30,000,000;
    - creates the interaction view with block drawing and lab fixtures off; `TraceTarget` goes through the authority;
    - `IsInWater` reads the blocks: a fluid cell counts up to `FluidAmount / 9` of a block, and fully when more fluid lies above it;
    - rules: a Creative saved world gets infinite supply, no tool wear and the creative inventory. Survival gets finite supply and tool wear, and starts with the loadout's tools and no blocks. The bare map uses the Playground's lab defaults;
    - `GatherCollision` treats columns not generated yet as solid, and the player is put on the spawn once its 3 x 3 is generated;
    - the edge fog, fog floor and aerial perspective (below);
    - `-VoxelShotEdits` carves a 4 x 4 x 10 pit across the nearest dry column corner, builds a stone, glass and glowstone pillar on a column border, and adds an `edits` shot.
  - Console and settings (`VoxelConsole`, `VoxelWorldSettings`):
    - `voxel.TestAreaRadius` and the `TestAreaRadius` setting are gone, and the `Unnamed.Voxel.Settings` test changed with them;
    - new: `voxel.Debug.EdgeWalls` (a debug variable, not a setting) and `voxel.Fly`;
    - `voxel.Info` prints `View radius 12: 545 columns in view, 657 generated; unload beyond 15 columns`;
    - the `UnloadMargin` clamp now starts at 2.
- **Foundation** (shared with the Playground, whose behaviour is unchanged):
  - `IFoundationBlockStorage::IsLoaded` (default true) and `FFoundationBlockWorld::SetStorage`; `IsKnown` also requires loaded;
  - both `AFoundationLab::IsInWater` overloads are virtual, and the new static `AFoundationLab::TraceAuthority` is what the Playground's `TraceTarget` now calls;
  - `UFoundationInteractionView` accepts any `AFoundationLab` with an authority and has two switches, `bDrawBlocks` and `bLabFixtures`. It drains the change log even with drawing off;
  - the creative screen opens for any lab whose authority has `Options.bCreativeInventory`;
  - the view's anonymous-namespace constant `Block` is now `InteractionViewBlockCm` (see Problems).
- **Edge fog** (`AVoxelWorld`; [Voxel world, Lighting](../VoxelWorld.md#lighting)):
  - the map's `ExponentialHeightFog` is refitted at runtime from the view radius: clear until 1.5 columns before `ViewRadius` and 99% opaque at it (density 1000 * ln(100) / 2400), height falloff 0.001, and cutoff 1e9 cm so the sky stays clear;
  - its light is matched to the sky just above the horizon: in-scattering (5900, 6500, 6000) cd/m2 and sky ambient contribution 0, from screenshots measured at (184, 189, 184) sRGB. Directional in-scattering keeps a glow on the sun side;
  - a fog floor sits under the world's bottom and follows the camera: the engine Plane scaled to 1,000 km, with no shadows, Lumen or ray tracing. Below the horizon past the edge it takes the fog's color instead of the sky atmosphere's dark ground;
  - the sky's aerial perspective is scaled x50 at radius 12 (the scale follows 12 / R at radius R), so land pales with distance;
  - shots from outside the world (`overview`, `lumen-scene`, `section`) turn the fog, the floor and the aerial perspective off.
- **Scripts:**
  - `CaptureVoxel.ps1`: edge walls on by default for the shots, and new `-Flight`, `-FlightSeconds` and `-Edits`. The "of 4" message is fixed;
  - `TestVoxel.ps1` expects 26.
- **Tests**, five new, written by a subagent from the plan's test spec, in files separate from the implementation (`Source/Unnamed/Tests/`):
  - `VoxelStreamingTests.cpp`: `Unnamed.Voxel.Streaming.Planner`, 274 checks: counts, coverage, order, sectors, hysteresis, steady-state rows, radius change, far coordinates;
  - `VoxelPipelineTests.cpp`: `Unnamed.Voxel.Streaming.Pipeline`: order independence across three runs, unload and reload, stale results dropped, bounded work, neighbours before meshing, done means done;
  - `VoxelEditTests.cpp`:
    - `Unnamed.Voxel.Edit.Sections`: the remesh rule against a brute force;
    - `Unnamed.Voxel.Edit.Remesh`: 10 edits at borders and corners. Exactly the touched sections remesh, every section equals a full rebuild, and heightmaps, stamps, survival of an unload and `DropEdits` are checked;
    - `Unnamed.Voxel.Edit.Authority`: `IsKnown`, `UseOn` placing, mining, and refusals in unloaded columns.

## Decisions confirmed with the user

The [plan](2026-10-08-Voxel-Handoff-Phase3-Plan.md#assumptions-to-confirm-with-the-user) set out its assumptions before any code. This is what the user confirmed or changed, and what was built:

| Question | Plan | Built |
|---|---|---|
| Load shape | A square: 25 x 25 columns at radius 12, with a circle noted as 441 | A circle. 545 columns in view and 657 generated at radius 12 |
| Edits | Copy on write per column; edited columns stay in memory when they unload | Copy on write per column, but edits are kept as a list per column, with stamps, and replayed after generation. No columns are kept |
| Mesh cache | Measure it; keep it near the player if it costs more than the block data | Kept within 2 columns of the center: 1.3 MB at the first view |
| Unloaded columns | Collide as solid; the player spawns once its 3 x 3 exists | As planned |
| Interaction view | Stays the presentation, with block drawing off | As planned, with lab fixtures off too |
| World edge | Fog until phase 9's far LOD (option a), or the outer ring meshed against air (option b) | Option (a). The user chose to "fade the last ~1.5 columns into the sky", then said they did not want a dark void but "looking off into the distance, but you can't because it's far and foggy". The fog floor and the haze followed |

The plan's 441 counted a circle between column centers. The planner measures the gap between column squares instead, so every block within the view radius is in a column in view, which gives 545. A square with the same clear distance is 25 x 25 = 625 columns, so the circle loads 13% fewer.

## Problems found and fixed

- **Stone walls in the first capture.** The terrains reset their scratch buffer with `SetNumZeroed`, which zeroes only the elements it adds, so a reused buffer kept the previous column's blocks. The first phase 3 capture showed them as stone walls. Both terrains now use `Dense.Init`.
- **A flight that proved nothing.** The first flights paused while streaming was busy, so "no holes" was vacuous. The flight now moves on every frame, and every frame is recorded.
- **Screenshot hitches.** The flight's own screenshots cause hitches of about 400 ms. One frame now moves the camera at most 50 ms of flight.
- **Apply estimate overshoot.** The first estimate let frames run over: worst chunk work 2.14 ms, with 8 of 17,186 frames over the 2 ms budget (`Tests/Evidence/2026-10-08/VoxelFlight-20261008-194508/`). Applying now stops when the estimate would pass 85% of the budget.
- **A gate that could starve.** The pre-commit review found the case. The 85% gate let a frame's first column through only when its estimate was above the whole budget, so a first column estimated between 85% and 100% of it was refused in every frame. Nothing queued behind it was applied either, because the estimates learn only from applies. Today's columns average 305 quads, far below the 1,601 to 1,900 quads that fall in that band at the starting estimate, but caves will make such columns likely. A frame's first column now always goes. Each measured apply also counts as at most 4 times its estimate plus 1 ms, so one stall cannot raise the estimates for the dozens of applies after it.
- **Hysteresis thrash.** The planner test caught it: columns unloaded beyond `ViewRadius + UnloadMargin`, but a needed column lies up to `ViewRadius + sqrt(2)` from the center, and a diagonal move adds up to `sqrt(2)` more. So moving back and forth across a border unloaded and reloaded columns at the edge. Columns now unload beyond the generated ring plus the margin, `ViewRadius + 1 + UnloadMargin` (15 at the defaults), which is the prompt's "unload radius = load radius + 2". `MinUnloadMargin` (2, also the setting's `ClampMin`) keeps that clear of a diagonal move.
- **Holes inside the fog's fade.** Under the plan's rule (a hole is a column within `ViewRadius - 1`), the flights after the fixes above still showed holes: 8 frames per lap, at most 8 columns, one frame each (`Tests/Evidence/2026-10-08/VoxelFlight-20261008-194508/` and `-194950/`). The flight's log showed why. Crossing a column corner moves the rings diagonally, a Y step and an X step a few frames apart, so a column can go from outside the view to within `ViewRadius - 1` before its three-frame pipeline (neighbour generation, mesh, apply) finishes. Those columns lie inside the fog's fade (10.5 to 12 columns), 33 to 60% fogged. Holes now count only where the fog is still clear (closer than `ViewRadius - 1.5`), and the flight reports columns arriving inside the fade separately, as late columns.
- **A unity-build clash.** The interaction view's anonymous-namespace `Block` constant clashed with another name once new files reshuffled the unity blobs. It is now `InteractionViewBlockCm`, and the `-DisableAdaptiveUnity` build is clean.
- **The game had lost its shaders.** The user found it. The Playground probe runs with low-cost scalability groups (`sg.GlobalIlluminationQuality`, `sg.ReflectionQuality`, `sg.ShadowQuality` and `sg.AntiAliasingQuality` at 0), and probe runs between `e09e326` (October 7) and `ac3a6b1` (October 8) saved them into the user's own `Saved/Config/WindowsEditor/GameUserSettings.ini`. `ac3a6b1` isolated the probe with `-UserDir`, so it cannot do this again. The four groups were restored to 3.

## Verification

- **Editor build:** no warnings in project code. A `-DisableAdaptiveUnity` build, which compiles the unity blobs the commit will produce, is clean with no warnings.
- **`Scripts\TestVoxel.ps1`:** 26/26 (`Tests/Evidence/2026-10-08/VoxelAutomation-20261008-210609/`).
- **Mutation check:** an off-by-one in `GetSectionsTouching` fails `Unnamed.Voxel.Edit.Sections` (13 errors) and `Unnamed.Voxel.Edit.Remesh` (11) (`VoxelAutomation-20261008-210535/`). The run after it, with the code restored, passes 26/26.
- **Every native suite:** 73/73, 68 before (`AllNative-20261008-211040/`).
- **After the review's apply-gate fix** (only `UVoxelWorldSubsystem` changed):
  - TestVoxel 26/26 (`VoxelAutomation-20261008-225350/`);
  - the acceptance flight again (`VoxelFlight-20261008-225439/`, 13,106 frames): no holes, worst chunk work 1.88 ms, 0 frames over budget, flat memory;
  - one first-view frame reached 3.31 ms, because one column took 2.01 ms (estimate 0.13 ms) to get a new component; either gate applies a column with that estimate;
  - a repeat capture's first view stayed within budget, worst 1.86 ms (`VoxelShots-20261008-225725/`).
- **Measurements** (this machine, Development editor, 1600 x 900). Runs before 20:23 are clean; after it another GPU-heavy app was running, so the edits run's GPU times are not comparable.
  - **First view at radius 12** (`Tests/Evidence/2026-10-08/VoxelShots-20261008-193131/`):
    - generate 657 columns: mean 1.02 ms, worst 2.6 ms;
    - mesh 545 columns: mean 1.5 ms (1.19 ms in the flight run);
    - stream sets and cards: mean 0.20 ms;
    - quads: 166,376 (305 per column) with edge walls, 154,167 without; columns 10.3 KB each (6.6 MB); mesh cache 1.3 MB;
    - Lumen cards: 8.8 per column (most 29) with edge walls, 7.2 (most 13) without;
    - apply: 545 columns in 44 ms over 26 frames;
    - from the first frame to the whole view on screen: 0.5 to 0.6 s over about 30 frames. Engine startup before the first frame adds about 1.3 s in captures.
  - **GPU per shot** (mean of 60 settled frames, radius 12, edge walls, no fog, `VoxelShots-20261008-192828/`): overview 3.32 ms, closeup 3.37, ground 3.16, lumen-scene 3.95, section 3.07. Phase 2W's 9 x 9 area (`VoxelShots-20261008-171008/`): 2.86, 2.77, 3.01, 3.47, 2.76.
  - **Acceptance flight** (`Tests/Evidence/2026-10-08/VoxelFlight-20261008-200725/`, 16,736 frames over 99.9 s):
    - holes: none;
    - late columns in the fog's fade: 1,155 frames, at most 33 columns, one frame each, when crossing a column corner moves the rings diagonally;
    - chunk work on the game thread: worst 1.81 ms, 0 of 16,736 frames over the 2 ms budget; the mean per leg is 0.01 to 0.14 ms (0.14 at 160 blocks per second);
    - memory over laps 2 and 3: process 3230 -> 3238 MB, column data 7.11 MB at the end of each lap; each lap generates about 3,360 columns and applies about 3,070;
    - GPU mean 3.25 ms, frame mean 6.0 ms. Before the fog floor it was 3.03 ms (`VoxelFlight-20261008-195430/`).
  - **Edits** (`VoxelShots-20261008-210841/` and `-210934/`): 166 cells in 4 columns around the corner (-16, -16) are back on screen 22 to 30 ms (2 frames) after the edit.
- **Playground runtime probe** (`TestFoundationRuntime.ps1 -Editor -FrameRates 60`): 103 checks, 0 failures (`Tests/Evidence/2026-10-08/Playground-all/Runtime-20261008-202349/`). Playground behaviour is unchanged.
- **`Tests/create-world.mjs --editor`:** 32/32 (`Tests/Evidence/2026-10-08/Worlds/Create-1791508280714/`).
- **`Tests/foundation-menu.mjs --editor`:** 36/36 (`Tests/Evidence/2026-10-08/Foundation/Menu-1791508711248/`).
- **`npm test`:** 57/57; `source-check.mjs` passes (938 assets unchanged).
- **Screenshots** (under `Tests/Evidence/2026-10-08/`): `VoxelShots-20261008-192828/montage-2w-vs-streamed.png` (phase 2W's area against the streamed world, same seed and spawn); `VoxelFlight-20261008-195929/montage-fog.png` (before the fog floor, and with it); `VoxelFlight-20261008-200725/montage-fog.png` (the dark void against the distant haze); `VoxelShots-20261008-210934/edits.png`.

## Known issues

- **Late columns in the fog's fade.** 1,155 frames of the flight had up to 33 columns still arriving inside the fade, one frame each, when crossing a column corner moves the rings diagonally.
- **The fog color is fixed to the map's sun** (there is no day cycle yet), and a faint seam remains on the sun side.
- **The first view takes 0.5 to 0.6 s** after the first frame.
- **Survival has no inventory screen.** Creative has the creative inventory.
- **Edits are session only** until phase 10.
- **No fluid flow.** Carving into water leaves air pockets.
- **The `section` shot** shows the circle's stepped edge.
- **`voxel.Fly` reads physical keys,** not the player's bindings.
- **The pawn's Creative flight** was deferred to a follow-up: it changes the shared 20 Hz movement step, and `voxel.Fly` covers fast travel.
- **Deferred from the pre-commit review** ([Known gaps](../VoxelWorld.md#known-gaps) has the fixes):
  - unloads are released in one frame outside the budget, so a teleport, a respawn or turning `voxel.Fly` off far away hitches;
  - `FFoundationBlockWorld::Clear` would list every generated cell if a populated voxel world were ever reset;
  - `AVoxelFlyCamera::EndPlay` restores the pawn's tick and input unconditionally.
- **Still open from before:** Lumen card occlusion (phase 4), software Lumen, Hardcore permadeath, world types, block shapes, the stale generated docs and the old evidence decision ([handoff](2026-10-08-Voxel-Handoff-Phase3.md#6-known-issues-and-gaps-with-suggested-fixes)).

## Next

Phase 4, terrain, which needs lava for aquifers. Its caves also need a decision on software Lumen and a second look at the step 2L card limits.

Phase 3 is not committed yet. The user commits when they ask, after a pre-commit review of the diff by a Sonnet 5.5 subagent.
