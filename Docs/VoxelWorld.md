# Voxel world

The infinite, seeded block world: streaming, terrain, caves, biomes, decoration and structures. It is built in phases from the [worldgen prompt](worldgen-implementation-prompt.md), with the [26.3 world generation research](Minecraft%20Java%2026.3%20world%20generation%20research.md) and the ore and feature tables in [Minecraft block reference](MinecraftBlockReference.md#appendix-a--terrain-feature-heights-from-263-data) as reference. Generated worlds place [the 26.3 blocks](Blocks.md) and are played with the Foundation pawn, authority and HUD.

## Status

| Phase | Scope | Status |
|---|---|---|
| 0 | Scaffolding: core module, log, stat group, settings, console, tests, RealtimeMeshComponent | Done |
| 1 | Deterministic core: seeds, hashing, Xoroshiro128++, noise, splines | Next |
| 2 | Data and meshing on a fixed 9 x 9-column area | |
| 2W | Create New World: saved worlds, seed, world type, game mode, difficulty | |
| 3 | Streaming, plus mining and placing on generated terrain | |
| 4 | Terrain (needs lava for aquifers) | |
| 5 | Biomes and surface (needs snow layers) | |
| 6 | Decoration, after a block-shapes step (plants, cross models) | |
| 7 | Structures that fit the land (needs stairs, doors, fences, paths) | |
| 8 | Mountains | |
| 9 | Far LOD | |
| 10 | Persistence and polish | |

## Layout

| Where | What |
|---|---|
| `Source/UnnamedVoxel/` | The core module: RNG, noise, density, biomes, storage and meshing in plain C++. It depends on `Core` only, so UObjects, actors and render state cannot be included, and it compiles with precise floating point (below). Headers are included by their path from this folder (`"VoxelConstants.h"`). |
| `Source/Unnamed/Voxel/` | The Unreal side: `UVoxelWorldSettings`, the console (`VoxelConsole`), and later the storage adapter, world actor, chunk component and streaming subsystem. |
| `Source/Unnamed/Tests/Voxel*Tests.cpp` | Native tests, `Unnamed.Voxel.*`; `Scripts/TestVoxel.ps1` runs them. |
| `Plugins/RealtimeMeshComponent/` | The chunk mesh component, vendored (below). |

## Coordinates

Cells use the project's Unreal axes, as the block world does: X east, Y south, Z up, 100 cm per block. Z plays the reference's Y.

- **Height:** the world spans Z -64 to 319, which is 24 sections of 16 (`VoxelConstants`).
- **Water:** sea level is 63, so the top ocean water block is at Z 62.
- **Columns:** a column (the reference's chunk) is 16 x 16 blocks across X and Y and the full height.
- **Sections:** a section is 16 x 16 x 16 and matches `FoundationGrid::SectionCells`.

## Design decisions

These differ from the prompt because of what the project already had.

- **Block registry:** reused. The mesher reads a flat per-state table compiled from `FFoundationBlockRegistry`: shape, render layer and the seven face codes the block materials already decode.
- **Collision:** none is cooked for chunks. The Foundation pawn collides against block data through `AFoundationLab::GatherCollision`, so voxel storage answers that query directly.
- **Storage:** paletted sections behind `IFoundationBlockStorage`, so targeting, collision, mining and placing work on generated terrain through the existing authority.
- **Noise:** our own scalar noise behind a batch API, not FastNoise2. Runtime SIMD dispatch can round differently on different CPUs, and golden-value tests lock the output.
- **Seeds:** text seeds use our own 64-bit hash, pinned by test vectors, so an engine upgrade cannot change existing worlds.
- **Floating point:** `UnnamedVoxel` compiles with `FPSemanticsMode.Precise`. By default Unreal builds the editor with `/fp:precise` and game targets with `/fp:fast` (`VCToolChain.cs`), which would let a packaged build generate different blocks from the editor for the same seed.
- **Worlds:** generated worlds are player-made worlds from Create New World, all opening one procedural map. The Playground stays the only built-in world.
- **Worldgen numbers:** biome parameter boxes, splines and noise settings will come from the 26.3 data on this machine into a new hash-checked `Reference/minecraft-worldgen-26.3/`, as numbers only, never code.

## Settings and console variables

`UVoxelWorldSettings` (Project Settings > Game > Unnamed Voxel) holds the authored defaults. A deliberate change belongs in `[/Script/Unnamed.VoxelWorldSettings]` in `Config/DefaultGame.ini`.

Runtime code reads the console variables, never the settings object. The settings push their values into the variables at project-setting priority, at startup and on every edit. A console, command-line or ini override wins for the session, for example `voxel.ViewRadius 16`.

| Setting | Console variable | Default | Used from |
|---|---|---|---|
| `DevSeed` | (`voxel.Seed` command, phase 1) | `Unnamed` | 1: the seed of a world opened without one (the editor, the test area) |
| `ViewRadius` | `voxel.ViewRadius` | 12 columns | 3 |
| `UnloadMargin` | `voxel.UnloadMargin` | 2 columns | 3: unload beyond radius 14 |
| `TestAreaRadius` | `voxel.TestAreaRadius` | 4 (9 x 9 columns) | 2 |
| `ApplyBudgetMs` | `voxel.ApplyBudgetMs` | 2 ms | 2 |
| `MaxTasksInFlight` | `voxel.MaxTasksInFlight` | 0 = twice the task workers | 3 |
| `bAmbientOcclusion` | `voxel.AmbientOcclusion` | on | 2 |
| `bGreedyMeshing` | `voxel.GreedyMeshing` | on | 2 |

## Commands and diagnostics

- `voxel.Info` prints the following, to the console and to `LogUnnamedVoxel`:
  - the world range and the dev seed;
  - every `voxel.*` variable, with its value and what set it;
  - the load and unload limits, the task workers, and the mesh backend.
- `stat Voxel` shows the `STATGROUP_Voxel` timings. Each stage adds its cycle stats there, next to a matching `TRACE_CPUPROFILER_EVENT_SCOPE` for Unreal Insights.
- `LogUnnamedVoxel` is the one log category, shared by both modules.

## RealtimeMeshComponent

| | |
|---|---|
| Source | [TriAxis-Games/RealtimeMeshComponent](https://github.com/TriAxis-Games/RealtimeMeshComponent), the free Core edition |
| Commit | `b8669a0891b728d8fa8a31f43f76d3853c10f743` (`master`, 2026-08-23; plugin version 5.4; includes tag v5.3.2) |
| License | MIT, `Plugins/RealtimeMeshComponent/LICENSE.txt` |
| Copied | Everything except `.git` and `.github`. Never edited here. |
| Modules | `RealtimeMeshComponent` (runtime), `RealtimeMeshEditor` (editor), `RealtimeMeshExamples` (runtime example actors, kept so the copy stays pristine) |

To update it, replace the folder from a newer upstream commit, record the commit here, rebuild and run `Scripts\TestVoxel.ps1`.

Core has no Lumen card or distance-field support; that is in the Pro edition. Phase 2 checks whether Lumen's hardware ray tracing sees chunk meshes. If it does not, caves get their darkness from a per-vertex sky light instead.

## Tests

`& Scripts\TestVoxel.ps1` runs `voxel.Info` and then the `Unnamed.Voxel` suites. It writes `Tests/Evidence/<date>/VoxelAutomation-<time>/`, including `voxel-info.txt`.

| Test | Checks |
|---|---|
| `Unnamed.Voxel.Constants` | The world range, 24 sections, sea level, and that voxel sections match the block world's sections. |
| `Unnamed.Voxel.Settings` | Every variable mirrors its setting unless overridden; `voxel.Info` lists every variable, the seed and the range; the command is registered. |
| `Unnamed.Voxel.MeshBackend` | A worker task builds a one-face stream set; the game thread hands it to a `URealtimeMeshSimple`, which then holds 4 vertices and 6 indices. |

## Known gaps

The registry has full cubes, slabs and still water only. The generator also needs:

- lava, for aquifers;
- snow layers;
- grass, flowers and ferns, cactus, sugar cane, kelp, seagrass, vines and pointed dripstone;
- dirt paths and farmland;
- stairs, fences, doors and panes, for villages.

Each is listed in Status before the phase that needs it.
