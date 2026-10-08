# Voxel world

The infinite, seeded block world: streaming, terrain, caves, biomes, decoration and structures. It is built in phases from the [worldgen prompt](worldgen-implementation-prompt.md), with the [26.3 world generation research](Minecraft%20Java%2026.3%20world%20generation%20research.md) and the ore and feature tables in [Minecraft block reference](MinecraftBlockReference.md#appendix-a--terrain-feature-heights-from-263-data) as reference. Generated worlds place [the 26.3 blocks](Blocks.md) and are played with the Foundation pawn, authority and HUD.

## Status

| Phase | Scope | Status |
|---|---|---|
| 0 | Scaffolding: core module, log, stat group, settings, console, tests, RealtimeMeshComponent | Done |
| 1 | Deterministic core: seeds, hashing, Xoroshiro128++, noise, splines | Done |
| 2 | Data and meshing on a fixed 9 x 9-column area | Next |
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
| `Source/UnnamedVoxel/` | The core module: RNG, noise, density, biomes, storage and meshing in plain C++. It depends on `Core` only, so UObjects, actors and render state cannot be included, and it compiles with precise floating point (below). Headers are included by their path from this folder (`"Random/VoxelSeed.h"`). It holds `Random/` (seeds, hashing, streams), `Noise/` (gradient and fractal noise), `Spline/` (nested splines), `VoxelBench` (timings) and the module-wide `VoxelConstants`, `VoxelLog` and `VoxelStats`. |
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

## Deterministic core

Every random value in generation is a pure function of (world seed, subsystem salt, coordinates). No stream is shared between columns, no time-based seed is used after world creation, and nothing depends on hash-map order.

The constants below decide every saved world. Tests pin them, either against an independent Python implementation of the published algorithms or against recorded golden checksums. Never change one to make a test pass; a deliberate change needs a note here and new golden values.

| Piece | Where | Rules |
|---|---|---|
| Seeds | `Random/VoxelSeed.h` | `FVoxelSeed::Parse` trims surrounding whitespace. Blank picks a random seed, once, at world creation. An optionally signed decimal integer that fits in 64 bits is used as typed (`+7`, `-0042`); anything else, including out-of-range numbers, is text. Text becomes FNV-1a 64 over its UTF-8 bytes, finished with SplitMix64's mixer: `Unnamed` is -3224043261468865476. Unlike the reference's 32-bit `String.hashCode`, text seeds reach every world. |
| Positional hash | `Random/VoxelRandom.h` | `VoxelRandom::Hash64(seed, salt, x, y, z)` folds seed, salt and coordinates in through three SplitMix64 mixing rounds. Neighbouring cells differ in 32 of 64 bits on average. |
| Salts | `Random/VoxelRandom.h` | `VoxelRandom::Salt("terrain")` is a compile-time hash of the subsystem's name, so a new subsystem never collides with an old one. |
| Streams | `Random/VoxelRandom.h` | `FVoxelXoroshiro` is Xoroshiro128++, seeded through SplitMix64. Use it inside one computation that needs several values, for example one feature at one position seeded from `Hash64`. |
| Values | `Random/VoxelRandom.h` | `ToUnitFloat` and `ToUnitDouble` give [0, 1); `ToIndex` and `ToRange` give integers by multiply-high. With no rejection loop they stay pure functions, with bias below Bound / 2^32. `FVoxelXoroshiro::NextInt` is exactly uniform. |
| Gradient noise | `Noise/VoxelGradientNoise.h` | One octave in 2D and 3D, with analytic gradients and a quintic fade. Lattice points pick a gradient by an integer hash of (seed, cell), so there is no permutation table and nothing repeats within the int32 lattice. Gradient directions are literal tables, with no runtime trigonometry. The cell and fraction split happens in double precision; the fraction is evaluated in float. Values are 0 on lattice points. 2D peaks near ±1; 3D (the 12 edge directions) reaches about ±0.78 in practice. |
| Fractal noise | `Noise/VoxelNoise.h` | `FVoxelNoiseSettings` (frequency in cycles per block, octaves, lacunarity, gain, warp) compiles to an `FVoxelNoise`. Each octave is seeded and offset from (seed, salt) alone. `Fbm` sums octaves; `Ridged` is Musgrave's ridged multifractal; `Damped` fades octaves on steep accumulated slopes (Quilez). Domain warp moves positions by up to `WarpAmplitude` blocks. `SampleGrid` fills grids with X fastest, then Y, then Z. |
| Splines | `Spline/VoxelSpline.h` | `FVoxelSplineBuilder` assembles nodes, each over one coordinate. A point's value is a constant or a nested node, and `Build` rejects empty nodes, unsorted points, cycles and bad coordinates. `FVoxelSpline` holds flat arrays with the root first: cubic Hermite between points, straight lines beyond the ends. |

Measured on this machine, Development editor build, one thread, `voxel.Bench Noise` with 1 million samples each (`Tests/Evidence/2026-10-08/VoxelAutomation-20261008-144031/voxel-console.txt`):

| Primitive | ns per sample |
|---|---|
| `Hash64` (x, y, z) | 5.6 |
| Xoroshiro128++ next | 1.7 |
| Gradient noise 2D / 3D, one octave | 15.0 / 34.7 |
| fBm 2D, 6 octaves | 84.6 |
| fBm 3D, 4 octaves | 130.5 |
| Ridged 2D, 6 octaves | 97.5 |
| Damped 2D, 6 octaves | 159.5 |
| Warped fBm 2D, 6 octaves | 155.0 |
| Spline, 3 nested levels | 61.3 |
| fBm 3D across 30 task workers | 117 million samples per second in total |

A column's density corners at 4 x 8 x 4 cells (5 x 5 x 49 points) cost about 160 µs per 4-octave 3D noise on one thread.

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
- `voxel.Seed [text]` shows the world seed a seed text makes, for example `Seed "Unnamed" -> -3224043261468865476 (text)`. Without text it shows the dev seed.
- `voxel.Bench Noise [millions]` times each primitive on the calling thread (1 million samples each by default), then 3D fBm across every task worker.
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

`& Scripts\TestVoxel.ps1` runs `voxel.Info`, `voxel.Seed` and `voxel.Bench Noise`, then the 13 `Unnamed.Voxel` tests. It writes `Tests/Evidence/<date>/VoxelAutomation-<time>/`, with the commands' output in `voxel-console.txt`. The editor's throwaway user folder goes to a temp folder, not into the evidence.

| Test | Checks |
|---|---|
| `Unnamed.Voxel.Constants` | The world range, 24 sections, sea level, and that voxel sections match the block world's sections. |
| `Unnamed.Voxel.Settings` | Every variable mirrors its setting unless overridden; `voxel.Info` lists every variable, the seed and the range; the command is registered. |
| `Unnamed.Voxel.MeshBackend` | A worker task builds a one-face stream set; the game thread hands it to a `URealtimeMeshSimple`, which then holds 4 vertices and 6 indices. |
| `Unnamed.Voxel.Random.Vectors` | SplitMix64 from 0 and 1234567, Xoroshiro128++ from a raw state and from seeds 0 and 42, the FNV-1a published vectors, salts and `Hash64`. Expected values come from an independent Python implementation; the salts and hashes are also `static_assert`s, so the build checks them. |
| `Unnamed.Voxel.Random.Seed` | Numbers (signs, leading zeros, both 64-bit limits), text (pinned hashes, including non-ASCII), out-of-range numbers as text, trimming, case sensitivity, random seeds for blank text. |
| `Unnamed.Voxel.Random.Distribution` | Every `Hash64` bit is set half the time over 2^18 cells; neighbours differ in about 32 bits; `NextInt` and `ToIndex` buckets are uniform; values stay in range. |
| `Unnamed.Voxel.Noise.Golden` | Eleven pinned checksums of fixed grids: gradient 2D and 3D, fBm, ridged, damped and warped in 2D and 3D, and fBm 30 million blocks out. A rebuilt noise matches bit for bit. |
| `Unnamed.Voxel.Noise.Properties` | Zero on lattice points; [-1, 1]; mean and spread; analytic gradients against finite differences; no gradient jump at lattice planes; full spread and smoothness 30 million blocks out; salts and seeds uncorrelated; every fractal kind in [-1, 1]. |
| `Unnamed.Voxel.Noise.Grid` | `SampleGrid` equals point samples bit for bit, including a column's 5 x 5 x 49 density corners. |
| `Unnamed.Voxel.Noise.Throughput` | Runs the bench at 200 000 samples and logs every line. |
| `Unnamed.Voxel.Determinism.Threads` | 24 000 jobs of 11 kinds (hash, stream, noises, spline) on 8 system threads, each in its own shuffled order, once sharing the noises and once building its own. Every result matches the single-threaded reference bit for bit. |
| `Unnamed.Voxel.Spline.Hermite` | Values and slopes at points, the segment midpoint, extrapolation, a one-point spline, an empty spline. |
| `Unnamed.Voxel.Spline.Nested` | A nested spline equals the hand-built composition; a shared child builds; repeated locations, empty nodes, cycles, bad coordinates and unknown children are refused with a reason. |

## Known gaps

The registry has full cubes, slabs and still water only. The generator also needs:

- lava, for aquifers;
- snow layers;
- grass, flowers and ferns, cactus, sugar cane, kelp, seagrass, vines and pointed dripstone;
- dirt paths and farmland;
- stairs, fences, doors and panes, for villages.

Each is listed in Status before the phase that needs it.
