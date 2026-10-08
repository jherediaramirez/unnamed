# Voxel world

The infinite, seeded block world: streaming, terrain, caves, biomes, decoration and structures. It is built in phases from the [worldgen prompt](worldgen-implementation-prompt.md), with the [26.3 world generation research](Minecraft%20Java%2026.3%20world%20generation%20research.md) and the ore and feature tables in [Minecraft block reference](MinecraftBlockReference.md#appendix-a--terrain-feature-heights-from-263-data) as reference. Generated worlds place [the 26.3 blocks](Blocks.md) and are played with the Foundation pawn, authority and HUD.

## Status

| Phase | Scope | Status |
|---|---|---|
| 0 | Scaffolding: core module, log, stat group, settings, console, tests, RealtimeMeshComponent | Done |
| 1 | Deterministic core: seeds, hashing, Xoroshiro128++, noise, splines | Done |
| 2 | Data and meshing on a fixed 9 x 9-column area | Done |
| 2L | Lighting: Lumen cards for chunks and a brighter sky light, so shaded slopes are no longer black | Done |
| 2W | Create New World: saved worlds, seed, world type, game mode, difficulty | Next |
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
| `Source/UnnamedVoxel/` | The core module: RNG, noise, density, biomes, storage and meshing in plain C++. It depends on `Core` only, so UObjects, actors and render state cannot be included, and it compiles with precise floating point (below). Headers are included by their path from this folder (`"Random/VoxelSeed.h"`). It holds:<br>- `Random/`: seeds, hashing, streams;<br>- `Noise/`: gradient and fractal noise;<br>- `Spline/`: nested splines;<br>- `Blocks/`: the per-state table;<br>- `Storage/`: sections and columns;<br>- `Terrain/`: the phase 2 heightmap terrain;<br>- `Meshing/`: the mesher;<br>- `Generation/`: the area builder;<br>- `VoxelBench`: timings;<br>- the module-wide `VoxelConstants`, `VoxelLog` and `VoxelStats`. |
| `Source/Unnamed/Voxel/` | The Unreal side:<br>- `UVoxelWorldSettings` and the console (`VoxelConsole`);<br>- the registry-to-table bridge (`VoxelBlocks`);<br>- the column mesh (`UVoxelChunkComponent`);<br>- the level actor (`AVoxelWorld`). |
| `/Game/Maps/L_Generated` | The generated world's map (`UUnnamedProjectSettings::GeneratedMap`), made by `Scripts/Editor/CreateGeneratedMap.py`. It is not a catalog world. |
| `/Game/Blocks/M_Chunk*` | The chunk materials, made by `Scripts/Editor/CreateBlockMaterials.py`. |
| `Source/Unnamed/Tests/Voxel*Tests.cpp` | Native tests, `Unnamed.Voxel.*`; `Scripts/TestVoxel.ps1` runs them. `VoxelTestBlocks.h` is a hand-built block table for tests. |
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
- **Storage:** paletted sections. Phase 3 puts them behind `IFoundationBlockStorage`, so targeting, mining and placing work on generated terrain through the existing authority. Collision already reads them in phase 2.
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

## Storage and meshing

| Piece | Where | How it works |
|---|---|---|
| Block table | `Blocks/VoxelBlockTable.h`, `Voxel/VoxelBlocks.cpp` | One `FVoxelStateInfo` per state id, compiled from the registry on the game thread and shared read-only. It holds the shape (full, bottom or top slab, fluid), the layer (opaque, cutout, translucent, water), the seven face codes, the tint, the light, and three flags: solid, leaves and shades corners. `OpaqueFaces` marks the faces that hide a neighbour: all six for an opaque cube, only the bottom or top for a slab, none for glass or leaves. Water's faces come from its still and flow textures, as the Playground draws it. `VoxelBlocks.cpp` checks that the core's face order and cull bit match the registry's. |
| Sections | `Storage/VoxelSection.h` | 16^3 states, X fastest, then Y, then Z. One state needs no index array; up to 16 states use 4-bit indices; up to 256 use 8-bit; more store 16-bit state ids directly. Widths divide 64, so no index straddles two words. `Set` widens as the palette fills; `Compact` narrows. Allocations are exact. |
| Columns | `Storage/VoxelColumn.h` | 24 sections plus the four heightmaps (world surface, ocean floor, motion blocking, motion blocking without leaves), stored as Z - MinZ + 1 with 0 for none. Built from a dense column buffer on a worker, then read-only. A generated column takes about 10 KB. |
| Terrain (phase 2) | `Terrain/VoxelHeightTerrain.h` | A stand-in until phase 4. A 2D height comes from continent fBm (1/220), damped hills (1/90) and detail (1/24), giving sea level 63 plus up to about 100 blocks. Layers: bedrock thinning from Z -64 to -60; deepslate below 0, blending into stone up to 8; stone; 3 to 5 blocks of soil. The top is grass, sand at the shore, gravel deep under water, bare stone from 110 and snow from 128. Water fills to Z 62. |
| Meshing | `Meshing/VoxelMesher.h` | Each section meshes alone from an 18^3 padded copy: itself plus a one-cell border from its 26 neighbours, which may sit in other columns. A missing column reads as air. Rules: see below. |
| Area builder | `Generation/VoxelArea.h` | Generates a square of columns, then meshes every section with its neighbours, one task each, timing every stage. The test area and `voxel.Bench Columns` use it. |

Mesher rules:

- **Culling:** a face is hidden when its code has the cull bit and the neighbour's facing side is opaque, or when the neighbour is the same see-through block (glass beside glass). Leaves keep the faces between leaves. Water never draws against water.
- **Merging:** full-cube faces, water surfaces and water floors merge greedily in each 16 x 16 slice. The key is the state and the four corner occlusion levels, so a merged quad never blurs shading.
- **Partial faces:** slab faces and water sides stay one quad per cell.
- **Ambient occlusion:** per vertex, from the two sides and the diagonal in front of each corner. Each quad is split along its brighter diagonal.
- **Fast exits:** sections that are all air, or solid with every side covered, return at once.
- **Output:** positions are in blocks from the column corner. UV 0 carries the block UV, continuous across a merged quad; UV 1 the face and overlay codes; UV 2 the tint and face. Vertex colour R holds occlusion and G holds light.

Rendering:

- **Chunk component:** `UVoxelChunkComponent` is one RealtimeMesh per column with a single buffer set: every section's geometry, and a poly group per layer mapped to that layer's material slot.
- **Lumen cards:** each column also carries a Lumen card representation, built on the workers with its stream set (`UVoxelChunkComponent::BuildCards`). For each face direction there is a card per section, fitted to that section's opaque and cutout faces in that direction. When a column would exceed Lumen's 32 cards per mesh, the vertically closest cards of its busiest direction merge first. Lumen captures the cards into its surface cache, which is how the terrain bounces light ([Lighting](#lighting)).
- **Chunk materials:** `M_ChunkOpaque`, `M_ChunkCutout`, `M_ChunkTranslucent` and `M_ChunkWater` reuse the block shader. They transform the continuous UV first, take each block's fraction, and sample with the continuous UV's gradients, so mips never jump at block edges. Texture animation uses the same `UFoundationBlockTextureTable`.
- **Collision:** none on the meshes. `AVoxelWorld::GatherCollision` returns block boxes (full or half for slabs) from the columns, as the Playground does from its block world.

Measured on one thread, `voxel.Bench Columns 4` (81 columns, 1,944 sections, seed `Unnamed`):

| Stage | Mean | Worst |
|---|---|---|
| Generate a column | 0.42 ms | 0.58 ms |
| Gather a section's neighbours | 0.014 ms | 0.107 ms |
| Mesh a section (all, including quick exits) | 0.048 ms | 0.320 ms |
| Mesh a section with geometry (407 of them) | 0.202 ms | 0.320 ms |

A whole column (generation plus 24 sections) costs about 1.9 ms on one worker. On all 30 workers the 81 columns generate in 3.5 ms and mesh in 6.8 ms of wall time, though per-item times rise by a third from cache contention. The area holds 24,477 quads, about 60 per meshed section, and columns take 10 KB each. A column's Lumen cards take 0.010 ms to build (0.023 ms worst): 832 cards for the area, 10.3 per column and at most 26.

## The test area

`AVoxelWorld` is the level actor of `/Game/Maps/L_Generated` and the map's `AFoundationLab`.

- **Start:** at BeginPlay it builds a square of `2 * voxel.TestAreaRadius + 1` columns around the origin with the dev seed (`-VoxelSeed=<text>` overrides it).
- **Build:** a background task generates, meshes and builds every column's stream set; the game thread then applies whole columns within `voxel.ApplyBudgetMs` per frame.
- **Player:** once the columns exist, the player returns to spawn: the dry surface nearest the origin, found from the terrain function so it is known before any column is built. Collision comes from block data, and falling below Z -128 returns the player to spawn.

To try it in the editor:

1. Open `Content/Maps/L_Generated` and press Play. You stand on the test area and can walk, jump and look around; the edges show the cut through the world down to bedrock.
2. `voxel.Seed hello` rebuilds the area with another seed. `voxel.Regenerate` rebuilds it after you change `voxel.TestAreaRadius`, `voxel.GreedyMeshing` or `voxel.AmbientOcclusion`.
3. `stat Voxel` shows the generate, gather, mesh and apply timings. The build's report is in the Output Log under `LogUnnamedVoxel`.

`& Scripts\CaptureVoxel.ps1 [-Seed <text>] [-ExecCmds '<console commands>']` renders the area offscreen and saves 5 screenshots plus `build.txt` to `Tests/Evidence/<date>/VoxelShots-<time>/`. `build.txt` holds the build report, the lighting settings in effect, each shot's GPU frame time (the mean of its last 60 settled frames) and where the player stands. `-ExecCmds` compares settings without changing anything, for example `'r.Lumen.HardwareRayTracing.LightingMode 1'`. The shots:

- `overview`;
- `closeup` of the highest point;
- `ground` from the player's view;
- `lumen-scene` (what Lumen sees);
- `section` (a cut through the west edge).

## Settings and console variables

`UVoxelWorldSettings` (Project Settings > Game > Unnamed Voxel) holds the authored defaults. A deliberate change belongs in `[/Script/Unnamed.VoxelWorldSettings]` in `Config/DefaultGame.ini`.

Runtime code reads the console variables, never the settings object. The settings push their values into the variables at project-setting priority, at startup and on every edit. A console, command-line or ini override wins for the session, for example `voxel.ViewRadius 16`.

| Setting | Console variable | Default | Used from |
|---|---|---|---|
| `DevSeed` | (`voxel.Seed` command) | `Unnamed` | 2: the test area's seed when the map opens without one (`-VoxelSeed=` overrides it) |
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
- `voxel.Seed [text]` shows the world seed a seed text makes, for example `Seed "Unnamed" -> -3224043261468865476 (text)`. In a voxel world it also rebuilds the world with that seed for the session. Without text it shows the current seed.
- `voxel.Regenerate` rebuilds the voxel world with its seed, picking up changed console variables.
- `voxel.Bench Noise [millions]` times each primitive on the calling thread (1 million samples each by default), then 3D fBm across every task worker.
- `voxel.Bench Columns [radius]` builds `(2 radius + 1)^2` columns off screen (default radius 4), first on one thread and then on every worker, and reports each stage.
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

What phase 2 found at this commit, and how the project works around it (all in `UVoxelChunkComponent`):

- **Several buffer sets in one mesh draw wrongly.** Only the most recently created one renders, with stray triangles from the others. The upstream examples use one buffer set per mesh. Workaround: each column is a single buffer set, and a changed section rebuilds its column's set.
- **Ray tracing needs aligned index offsets.** Each poly group gets a ray-tracing segment at its offset in the shared index buffer, and D3D12 asserts unless that offset is a multiple of 16 bytes (`D3D12RayTracing.cpp`). Workaround: every layer is padded to a multiple of four triangles with zero-area ones.
- **Cached bounds can go stale.** The component caches the mesh's bounds when render data changes, and a stale box frustum-culled near columns. Workaround: the component computes its own bounds, the column's footprint between its lowest and highest meshed sections.
- **Lumen needs cards from us.** The mesh accepts a Lumen card representation and a mesh distance field (`FRealtimeMesh::SetCardRepresentation` and `SetDistanceField`), and its scene proxy hands both to Lumen, but it generates neither. With hardware ray tracing the chunks are in Lumen's scene anyway, through their ray-tracing geometry, so without cards every surface-cache lookup on terrain came back black. Workaround: the column builds its own cards ([Lighting](#lighting)). No distance fields yet, so software Lumen does not see the chunks at all.

## Lighting

Generated terrain is lit by Unreal's lighting: the sun with virtual shadow maps, and Lumen global illumination with hardware ray tracing (the project default). Phase 2 showed slopes facing away from the sun almost black. Two causes, fixed in step 2L ([record](Records/2026-10-08-Voxel-Lighting.md)):

1. **No surface cache for the terrain.** Lumen's hardware rays hit the chunks' ray-tracing geometry, then read the hit's lighting from Lumen's surface cache. Chunks had no cards, so every hit read black and no light bounced off the land. Fix: every column carries Lumen cards (`UVoxelChunkComponent::BuildCards`, under Rendering above). This was measured with the GPU cost below. Hit lighting (`r.Lumen.HardwareRayTracing.LightingMode 1`) gave the same lift at a higher cost, and leaves Lumen's scene black for reflections and further bounces. In 5.8, mode 2 lights hits for reflections only, which is why phase 2's try of mode 2 changed nothing.
2. **A dim sky against the sun.** With bounce fixed, a wall that sees only sky still measured 28 of 255 against 101 for a sunlit wall. That is the physical ratio of the atmosphere's sky to a 75,000 lux sun at EV100 14.5. Fix: the generated map's SkyLight intensity is 3 (`SKY_LIGHT_INTENSITY` in `Scripts/Editor/CreateGeneratedMap.py`), three times the Playground's. This is art direction rather than physics. It lifts every sky-lit surface and leaves surfaces that cannot see the sky dark, because Lumen still traces the occlusion.

Mean sRGB levels from the `ground` and `overview` shots, before and after (`Tests/Evidence/2026-10-08/VoxelShots-20261008-152702` and `-160917`):

| Region | Before | After |
|---|---|---|
| Hill facing away from the sun | 24 | 47 |
| Shaded dirt side next to sunlit ground | 27 | 64 |
| Wall that sees only sky | 28 | 59 |
| Sunlit wall | 101 | 113 |
| Sunlit sand | 170 | 185 |

GPU frame at 1600 x 900 on this machine: 2.7 ms before, 2.9 to 3.0 ms after. The cards cost the difference, because Lumen now keeps a surface cache for the terrain. The brighter sky costs nothing. Comparisons: `Tests/Evidence/2026-10-08/VoxelLighting-*`.

Limits:

- **Hidden faces in a card.** A card captures the front face along its direction, so a face behind another facing the same way in the same section (a dune behind a dune, a valley narrower than 16 blocks) stays black in Lumen's scene. Rays that hit it get no bounce. It shows as black strips on the seabed in the `lumen-scene` shot. Caves and overhangs in phase 4 will make it more common. Options then: cards sliced along their direction, or hit lighting.
- **Software Lumen.** On a GPU without hardware ray tracing, Lumen traces distance fields. The chunks have none, so it cannot see them: no bounce and no sky occlusion, and caves would be lit by the sky. Mesh distance fields for columns (`SetDistanceField`) are the fix when it matters.
- **The Playground** keeps its own rig (sky light 1).

## Tests

`& Scripts\TestVoxel.ps1` runs `voxel.Info`, `voxel.Seed` and `voxel.Bench Noise`, then the 20 `Unnamed.Voxel` tests. It writes `Tests/Evidence/<date>/VoxelAutomation-<time>/`, with the commands' output in `voxel-console.txt`. The editor's throwaway user folder goes to a temp folder, not into the evidence.

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
| `Unnamed.Voxel.Blocks.Table` | The table from the real registry: one entry per state id; stone, glass, leaves, slab, water, grass overlay and glowstone light as expected. |
| `Unnamed.Voxel.Storage.Section` | Palette widths at 1, 2, 16, 17, 256, 257 and 4096 states, with exact byte counts; round trips; 3000 random edits widening 0 to 4 to 8 to 16 bits; non-air counts; `Compact` back to one state. |
| `Unnamed.Voxel.Storage.Column` | All four heightmaps over water, floating leaves and glass; out-of-world reads; single-state sky sections; an empty column. |
| `Unnamed.Voxel.Mesher.Shapes` | Known quad counts (a block, a merged cube, a bar, glass beside glass, leaves beside leaves, a floor across column borders, slab, water cell, pool), the slab top at half height, the water surface at 8/9, darkened corners. |
| `Unnamed.Voxel.Mesher.Borders` | Random blocks of every kind over 3 x 3 columns and three sections, meshed and split back into faces. Compared with an independent cell-by-cell reading of the rules, every face matches: none missing, none hidden drawn, none drawn twice, and every face's four corner levels agree. Run with greedy merging on and off (26,896 faces). |
| `Unnamed.Voxel.Mesher.Cards` | Lumen cards from meshed columns: one block gives one thin card per direction, looking along that face's normal; glass and water give none, leaves six. A pillar through all 24 sections, 98 cards before merging, stays within Lumen's 32. In both, and among random blocks, every opaque and cutout face lies inside a card facing its way. |
| `Unnamed.Voxel.Terrain.Generate` | Five columns, including ones millions of blocks out, generated in order on this thread and in reverse on the workers: identical. Heightmaps follow the terrain function; bedrock floor and deepslate; every column under 64 KB; another seed changes the land. |

## Known gaps

- **Lighting limits:** faces hidden behind a same-facing face inside one card stay black in Lumen's scene, and software Lumen cannot see the chunks ([Lighting](#lighting)).
- **Pale water:** seen from high above, water over sand looks pale; tuning comes with biomes and water colour in phase 5.
- **Water physics:** `AFoundationLab::IsInWater` reads the lab's water boxes and is not virtual, so generated water does not yet count for air, swimming or drowning. Phase 3 makes it ask the block data.
- **Apply budget:** one column is applied whole, so a frame can overshoot the 2 ms budget by one column (worst 2.15 ms in the latest run; handing over the cards adds no measurable time).
- **Meshing under load:** with all 30 workers busy, a meshed section averages 0.26 ms against 0.20 ms on one thread. A bitmask visibility pass is the next optimization if phase 3's streaming needs it.
- **No mining or placing yet** on generated terrain: that is phase 3.

The registry has full cubes, slabs and still water only. The generator also needs:

- lava, for aquifers;
- snow layers;
- grass, flowers and ferns, cactus, sugar cane, kelp, seagrass, vines and pointed dripstone;
- dirt paths and farmland;
- stairs, fences, doors and panes, for villages.

Each is listed in Status before the phase that needs it.
