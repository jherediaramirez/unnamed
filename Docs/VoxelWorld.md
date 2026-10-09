# Voxel world

The infinite, seeded block world: streaming, terrain, caves, biomes, decoration and structures. It is built in phases from the [worldgen prompt](worldgen-implementation-prompt.md), with the [26.3 world generation research](Minecraft%20Java%2026.3%20world%20generation%20research.md) and the ore and feature tables in [Minecraft block reference](MinecraftBlockReference.md#appendix-a--terrain-feature-heights-from-263-data) as reference. Generated worlds place [the 26.3 blocks](Blocks.md) and are played with the Foundation pawn, authority and HUD.

## Status

| Phase | Scope | Status |
|---|---|---|
| 0 | Scaffolding: core module, log, stat group, settings, console, tests, RealtimeMeshComponent | Done |
| 1 | Deterministic core: seeds, hashing, Xoroshiro128++, noise, splines | Done |
| 2 | Data and meshing on a fixed 9 x 9-column area | Done |
| 2L | Lighting: Lumen cards for chunks and a brighter sky light, so shaded slopes are no longer black | Done |
| 2W | Create New World: saved worlds, seed, world type, game mode, difficulty | Done |
| 3 | Streaming, plus mining and placing on generated terrain | Done |
| 4 | Terrain: 26.3's density graph, climate, caves, aquifers and lava | Done (5 sessions: worldgen data and reader; noise, climate, splines and debug views; 3D density and generator 2; caves on, aquifers, still lava, cave-lighting decision; performance). Caves-on columns are slightly over 5 ms on two presets ([Known gaps](#known-gaps)) |
| 5 | Biomes and surface | In progress (session 2 of 5 done: reference data, blocks and readers) |
| 6 | Decoration, after a block-shapes step (plants, cross models) | |
| 7 | Structures that fit the land (needs stairs, doors, fences, paths) | |
| 8 | Mountains | |
| 9 | Far LOD | |
| 10 | Persistence and polish | |

## Layout

| Where | What |
|---|---|
| `Source/UnnamedVoxel/` | The core module: RNG, noise, density, biomes, storage, meshing and the streaming plan in plain C++. It depends on `Core` only, so UObjects, actors and render state cannot be included, and it compiles with precise floating point (below). Headers are included by their path from this folder (`"Random/VoxelSeed.h"`). It holds:<br>- `Random/`: seeds, hashing, streams;<br>- `Noise/`: gradient and fractal noise, and the 26.3-style normal noise;<br>- `Spline/`: nested splines;<br>- `Density/`: the density graph, the reference's density functions as plain data, and the evaluator;<br>- `Blocks/`: the per-state table;<br>- `Storage/`: sections and columns;<br>- `Terrain/`: the terrain interface, the phase 2 height terrain, Superflat's flat terrain and the climate sampler;<br>- `Meshing/`: the mesher and its remesh rule;<br>- `Generation/`: the area builder;<br>- `Streaming/`: the planner (which columns are in view, which exist, which unload);<br>- `VoxelBench`: timings;<br>- the module-wide `VoxelConstants`, `VoxelLog` and `VoxelStats`. |
| `Source/Unnamed/Voxel/` | The Unreal side:<br>- `UVoxelWorldSettings` and the console (`VoxelConsole`);<br>- the registry-to-table bridge (`VoxelBlocks`);<br>- the worldgen readers (`VoxelWorldgenReader`, `VoxelSurfaceReader`, shared helpers in `VoxelWorldgenJson.h`): the reference's JSON into the density graph, the biome table and the surface rules;<br>- the terrain factory (`VoxelTerrainFactory`): every world's terrain, used by the voxel world and by the menu's check before travel;
- the climate debug tools (`VoxelClimateDebug`): a cached sampler per (preset, seed), the map writer and the values at the player;<br>- the column mesh (`UVoxelChunkComponent`);<br>- the column pipeline (`FVoxelColumnPipeline`, plain C++ with no UObjects);<br>- the streaming subsystem (`UVoxelWorldSubsystem`);<br>- the storage adapter (`FVoxelBlockStorage`);<br>- the level actor (`AVoxelWorld`);<br>- the free camera (`AVoxelFlyCamera`) and the acceptance flight (`FVoxelFlight`). |
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

These differ from the prompt because of what the project already had, or settle what it left open.

- **Block registry:** reused. The mesher reads a flat per-state table compiled from `FFoundationBlockRegistry`: shape, render layer and the seven face codes the block materials already decode.
- **Collision:** none is cooked for chunks. The Foundation pawn collides against block data through `AFoundationLab::GatherCollision`, so voxel storage answers that query directly. Columns not generated yet count as solid, so the pawn can never fall into a hole.
- **Storage:** paletted sections, read-only once built. `FVoxelBlockStorage` puts them behind `IFoundationBlockStorage`, so targeting, mining and placing work on generated terrain through the existing authority. Columns are shared read-only with workers, so an edit copies its column (about 10 KB) instead of changing it in place.
- **Noise:** our own scalar noise behind a batch API, not FastNoise2. Runtime SIMD dispatch can round differently on different CPUs, and golden-value tests lock the output.
- **Seeds:** text seeds use our own 64-bit hash, pinned by test vectors, so an engine upgrade cannot change existing worlds.
- **Floating point:** `UnnamedVoxel` compiles with `FPSemanticsMode.Precise`. By default Unreal builds the editor with `/fp:precise` and game targets with `/fp:fast` (`VCToolChain.cs`), which would let a packaged build generate different blocks from the editor for the same seed.
- **Worlds:** generated worlds are player-made worlds from Create New World, all opening one procedural map ([Saved worlds](#saved-worlds)). The Playground stays the only built-in world.
- **View shape:** a circle of columns measured edge to edge, not a square, so a fog ending at the view radius never shows a missing column ([Streaming](#streaming)).
- **Edits:** kept as a list per column, each with a stamp, and replayed when the column generates again. Unloading frees the column, and the edits survive. They last for the session until phase 10's saves.
- **Mesh cache:** kept only within 2 columns of the center, where edits happen. Elsewhere an edit remeshes the whole column.
- **Unloaded columns:** solid for collision and unknown to the authority, so nothing is placed in them.
- **Interaction view:** it stays the presentation in generated worlds (outline, cracks, held item, effects), with block drawing and lab fixtures off. The chunks draw the blocks.
- **World edge:** fog, a floor and aerial perspective hide it until phase 9's far LOD ([Lighting](#lighting)). Meshing the edge against air, the alternative, is kept for captures (`voxel.Debug.EdgeWalls`).
- **Worldgen data:** the terrain's noise settings, density functions and noises come byte for byte from the 26.3 jar into the hash-checked `Reference/minecraft-worldgen-26.3/`: data and numbers only, never code. Biome parameter boxes and surface rules have joined it (phase 5, session 2); the parameter list comes from a facts file, because the jar builds it in code. The game module reads the JSON (`VoxelWorldgen::ReadPreset`), because the core depends on `Core` alone and has no JSON reader ([Worldgen data](#worldgen-data)).
- **Terrain (phase 4, decided on October 8):**
  - our own code and noise evaluate 26.3's density-function graph;
  - Large Biomes and AMPLIFIED use their own presets;
  - the new terrain is generator 2, and saved worlds made before it keep the stand-in terrain;
  - aquifers get still lava, built like water;
  - for now caves are lit correctly only with hardware ray tracing;
  - caves on through the full final_density (session 4);
  - cave lighting: Lumen cards stay (hit lighting is one switch away: r.Lumen.HardwareRayTracing.LightingMode 1), decided by the manager overnight on October 9 from same-cave captures; for Jesus's review.

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
| Normal noise | `Noise/VoxelNormalNoise.h` | `FVoxelNormalNoise` is the 26.3-style noise, built from a noise file (`FVoxelDensityNoise`). Octave i runs at 2^(base_octave + i) cycles per unit. Weights halve going up, are normalized to sum 1, then multiplied by the amplitude modifiers. Two stacks of our 3D gradient noise are summed, the second read at scale 1.0183. Every octave has its own seed and lattice offset, all from one Xoroshiro stream seeded by `Hash64(world seed, VoxelRandom::Salt(noise name))`. Zero-modifier octaves still draw from the stream but are not sampled. Spread: standard deviation = base_amplitude / 3 x summed weights x `SpreadScale` (1.06), with the sum rescaled by the linear/quadrature sum, because independent octaves add in quadrature. The rule was fitted from the jar: vanilla's standard deviation follows base_amplitude x (1/3) x summed normalized weights within 2% across all climate noises (1/3 is the jar's `NormalNoise.TARGET_DEVIATION`). `SpreadScale` decides saved worlds like the golden checksums: never change it to pass a test. This is not seed-for-seed vanilla. Noises also report a bound (their summed octave amplitudes). |
| Blended noise | `Noise/VoxelBlendedNoise.h` | The base 3D noise (`old_blended_noise`): two limit fBm noises blended by a coarser selector fBm, our own design on our gradient noise, with 26.3's numbers (684.412 units per block times the node's scale, selector divided by the xz/y factors, octaves starting at -15 and -7). Limits use 8 octaves (they stop below a block's period), the selector 8. `OutputScale` 1.89 was calibrated once against the jar's spread (std 0.198 for the overworld's numbers; ours 0.1980, p1/p99 -0.456/0.459 against -0.461/0.4635) with the tolerance fixed first (std 10%, p1/p99 15%, half-sill lags within 2x: vanilla 34 blocks along x, 60 along y). It decides saved worlds like the goldens. |
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

## Worldgen data

Phase 4 builds the terrain from 26.3's own worldgen data, and phase 5 adds the biomes and the surface rules. `Reference/minecraft-worldgen-26.3/` holds it byte for byte, with a verifier and notes on how the format changed after 1.21 ([README](../Reference/minecraft-worldgen-26.3/README.md)):

- **Noise settings:** `overworld`, `large_biomes` and `amplified`.
- **Density functions:** the 51 those presets use.
- **Noises:** the 62 top-level noises.
- **Spline facts:** Mojang's own values for the 9 terrain splines, 512 samples each, dumped by running the jar's `CubicSpline` (`tools/WorldgenFacts.java`).
- **Surface rules:** the overworld tree, 37 files in `material_rule/` (`overworld`, `bedrock_floor` and 35 under `overworld/`), and the 8 named conditions in `material_condition/` (the tree leaves `under_ceiling` unused).
- **Biomes:** the 56 overworld biomes (the jar's `#minecraft:is_overworld` tag decides which), that tag, and `multi_noise_biome_source_parameter_list/overworld.json` (`{"preset": "minecraft:overworld"}`).
- **Biome facts:** the jar builds the parameter list in code, so `facts/biome_parameters.json` holds it: 7,594 points over 56 biomes, quantized x10,000, in list order. `facts/biome_lookup_samples.json` has 5,120 seeded target points with the jar's brute-force and indexed answers; they differ on 95, all ties, checked with an independent Python scan.

| Piece | Where | How it works |
|---|---|---|
| Density graph | `Density/VoxelDensityGraph.h` | `FVoxelDensityGraph` holds the reference's 30 node types (`EVoxelDensityOp`) as one flat array of nodes with children by index, plus splines, interval lists and noises. A named function is one node however often it is referenced, so evaluation can compute it once per point. Every node comes after its inputs, and every spline after its nested splines, so evaluating in index order needs no recursion. `Validate` checks indices, each op's inputs, that order, thresholds and names. `BuildSpline` bakes a spline into an `FVoxelSpline`, with its coordinates in first use. |
| Preset | `Density/VoxelDensityGraph.h` | `FVoxelTerrainPreset`: a world type's sea level and height, its router entry points, its aquifer inputs and the 9 debug views (N, T, V, C, E, D, W, PV, PS), all nodes of one graph. The router entries are continents, erosion, ridges, temperature, vegetation, depth, chunk surface level and final density. The aquifer inputs are barrier, exclusion, floodedness, spread, lava and surface level. |
| Reader | `Voxel/VoxelWorldgenReader.h` (game module) | `VoxelWorldgen::ReadPreset` and `ReadReferences` read files into the graph. `WriteValue` and `WriteNoise` write nodes back as the reference's JSON. Reading is strict: an unknown type, a missing or unexpected field, a bad id, a missing file or a cycle fails with the file and the field. Noise files leave out `octave_count` when it is 1 and `amplitude_modifiers` when all are 1; the reader fills both in. |
| Biome table | `Biome/VoxelBiomeTable.h` | `FVoxelBiomeTable`: the biomes (`FVoxelBiome`) and the parameter points (`FVoxelBiomeParameter`) of one list, as plain data. `Quantize` turns a float into the table's x10,000 integer and `Validate` checks the table. No biome lookup yet (phase 5, session 4). |
| Surface rules | `Surface/VoxelSurfaceRules.h` | `FVoxelSurfaceRules`: a rule tree as flat arrays of rules and conditions, every node after its inputs, as the density graph keeps its nodes. 5 rule ops (`sequence`, `condition`, `block`, `ore_vein`, `bandlands`) and 10 condition ops (`biome`, `noise_threshold`, `vertical_gradient`, `y_above`, `water`, `stone_depth`, `not`, `steep`, `hole`, `above_preliminary_surface`). Ids (blocks, biomes, noises) stay strings for the evaluator. `Validate` checks it. No evaluator yet (phase 5, session 5). |
| Biome and surface readers | `Voxel/VoxelSurfaceReader.cpp` (declared in `VoxelWorldgenReader.h`) | `ReadBiomes`, `ReadBiomeParameters` and `ReadSurfaceRules` read the files; `WriteSurfaceRule` and `WriteSurfaceCondition` write nodes back as the reference's JSON. Strict like `ReadPreset`, with the file and field in every error; a biome file's `attributes`, `features` and `carvers` are skipped by name. The overworld tree reads as 231 rules and 107 conditions, with 37 named rules and 7 named conditions. The JSON helpers shared with `ReadPreset` are in `Voxel/VoxelWorldgenJson.h`. |

- **Size:** each preset reads as 24 functions, 279 nodes, 31 noises and 118 splines with 430 points, in about 8 ms in the editor. All 51 functions and 62 noises together make 530 nodes.
- **Splines:** our `FVoxelSpline` matches Mojang's `CubicSpline` on all 4,608 samples, within 9.5e-7 (float rounding).
- **Noise facts:** `facts/noise_samples.json` has 62 noises with range, mean, standard deviation and 11 percentiles over xz and xyz, from 4 seeds x 16,384 positions, dumped by `tools/WorldgenFacts.java`.
- **Verifier:** it now reports `REFERENCE OK: 228 files hashed, 219 compared with the jar, 580 references resolved, 9 splines with 4608 samples, 44 rule and condition files with 182 references, 7594 biome points over 56 biomes, 5120 lookup samples`. The 123 files of phase 4 are byte-identical, except `manifest.json` and the two edited tools.
- **In use:** the terrain uses the graph since session 3 (generator 2). Packages stage the reference's `data/` folder (219 files) and `facts/biome_parameters.json` as runtime dependencies at the same paths under the project (`Unnamed.Build.cs`), so `GetReferenceRoot` works unchanged. Checked from the target receipt only; a full package check is for session 5.

### Density evaluator

`Density/VoxelDensityEvaluator.h`: `FVoxelDensityEvaluator` compiles the nodes the requested roots read, for one seed. Noises are created and splines baked with `BuildSpline`. It then evaluates node by node over a batch of points, in graph order, with float values and the reference's axes (x, y up, z). Each thread needs its own `FVoxelDensityScratch`.

- **Runs:** every op.
- **Fixed values:** `blend_alpha` is 1, `blend_offset` is 0, `beardifier` is 0, and `blend_density` passes its input.
- **Confirmed against the 26.3 jar** by black-box sampling (`DensityFunction.CODEC` and our own `CompileContext`, numbers only):
  - `noise` = N(x * xz_scale + shift_x, y * y_scale, z * xz_scale + shift_z);
  - `shift_a` = 4 * N(x/4, 0, z/4); `shift_b` = 4 * N(z/4, x/4, 0);
  - `lerp` = first + alpha * (second - first);
  - `squeeze(2)` = 11/24 (clamp to [-1, 1], then x/2 - x^3/24);
  - `half_negative` and `quarter_negative` scale negatives by 1/2 and 1/4;
  - `div`; `gradient` is linear between its coordinates and clamps beyond its ends;
  - `range_choice` max is exclusive;
  - `interval_select` picks function K for thresholds[K-1] <= input < thresholds[K] (thresholds inclusive from below);
  - `interpolated` is trilinear between corners on multiples of the cell size (4 x 8 x 4), x then y then z;
  - `find_top_surface` starts at floor(upper_bound / cell_height) * cell_height, steps down a cell at a time and returns the first y whose density is > 0 (strictly), else lower_bound; it ignores the sample's own y;
  - `old_blended_noise` has a range of ±2.167 in the jar.

  The session 3 values come from `C:\dev\sandbox\phase4s3\OpProbe3.java` (numbers only).
- **`cache`** cannot run outside the jar's deduplicated graph ("Cannot compile cache before it has been deduplicated"), so it is taken as identity: it only shares work.
- **Pieces added in session 3:**
  - per-node y dependence (flat nodes come before varying ones in the order);
  - `EvaluateNodes` on a chosen subset;
  - a conservative `EvaluateBounds`: noise by amplitude sums, splines and divisions across 0 unbounded, a small widening for rounding;
  - `interpolated` and `find_top_surface` use a nested evaluator of their inner function; `find_top_surface` runs its flat part once and only the y-dependent part per step.
- **Lazy `interval_select`:** functions whose whole subtree nothing else reads are not compiled in the parent. The select evaluates each only at the points that pick it, through a nested evaluator, and bounds it by a bound over all y found at compile. Exact (goldens unchanged); about 10% off a caves-on column.
- **Lazy `range_choice`:** a branch's private nodes (read only by that branch; never interpolated or `find_top_surface` nodes, and roots are never private) run only at the points that pick that branch, in a nested scratch with the branch's frontier values gathered in.
  - Only branches holding costly work (noise, shift, blended noise, spline, `interval_select`) go lazy. Nested choices take their own private nodes: outer choices first, ownership goes to the innermost.
  - `Evaluate` and `EvaluateNodes` skip deferred nodes (`IsDeferred`); `EvaluateBounds` still bounds them.
  - Exact, because every op is per point. Corners: overworld 2.0 -> 1.7 ms, amplified 3.68 -> 2.81 ms.

### Climate

`Terrain/VoxelClimate.h`: `FVoxelClimateSampler` evaluates continentalness (C), erosion (E), weirdness (W), peaks and valleys (PV), temperature (T), vegetation (V), and the depth function's offset (O), factor (F) and jaggedness (J). Offset, factor and jaggedness are found as functions in the router depth's folder. `SampleColumn` takes the 5 x 5 cell corners (4 blocks apart) and a time; `SampleDepth` takes one block. The cycle stat is "Climate column" (`STAT_VoxelClimateColumn`) with the trace scope `Voxel_ClimateColumn`. The density terrain does not use it; it serves the debug views and benches.

| Measured (Development editor, this machine, seed "Unnamed") | Time |
|---|---|
| Climate column, one thread | 46 µs, after one warm-up column (45 to 51 µs across runs and presets) |
| Climate column across 30 workers, one scratch per worker | 6 to 7 µs per column over 289 columns (too few to scale fully) |
| Maps | 0.12 µs per point (1024 x 1024 in about 125 ms on all workers) |
| Reading a preset | about 8 ms |

This is under the 2 ms target.

On the maps (seed "Unnamed"): continents span thousands of blocks; Large Biomes are about 4x larger; AMPLIFIED raises more peaks; PV valleys form river-like channels. About 43 to 48 % of the area estimates below sea level, which includes rivers and beach-level land the estimate puts just under 63. Evidence: `Tests/Evidence/2026-10-09/ClimateMaps-20261009-001155/` (PNGs and the montages `climate-overworld-8192.png` and `climate-continental-32768.png`).

### Column density and terrain (generator 2)

`Density/VoxelColumnDensity.h`: `FVoxelColumnDensity` evaluates final_density over a 16 x 16 column.

- **Corners:** nodes under `interpolated` run at the 5 x 5 x 49 corners of 4 x 8 x 4 cells (flat ones once per corner column, 5 x 5). Inside a cell they are trilinear. Nodes above it run per block.
- **Height-bound skipping:** each cell is bounded first. A certainly positive cell is solid and a certainly non-positive one is empty; neither is evaluated. Each cell column is bounded from the top in stacks of 8 cells (`BoundStack`) while a whole stack is certain, then one cell at a time down to the first mixed cell. Cells below it stay mixed (with caves their bounds never proved anything). Only the corners of mixed cells are evaluated. Overworld caves on: bounds 1.51 -> 0.27 ms.
- **Cells from corners (stage 3b):** once a mixed cell's corners are evaluated, each interpolated node is bounded by its 8 corner values (trilinear weights are convex; a margin of 1e-5 x max |corner| + 1e-30 covers float rounding). `EvaluateBounds` then runs over the per-block nodes for the cell's y range, so per-block terms outside `interpolated` get their own bounds. Certainly positive means solid; certainly <= 0 and not KeepEmpty means empty; neither needs per-block work. Overworld caves on: 34.2% more cells settled, cells to blocks 2.38 -> 0.75 ms. Stats: `RefinedCells`, `RefineMs`.
- **Weights table (stage 4):** trilinear weights by offset are precomputed as float(double(D) / Cell), the expression direct evaluation uses, so results are bit-identical. It removes 3 double divisions per block per interpolated node.
- **Exact:** results equal direct evaluation bit for bit (test `Density.Column`).

`Terrain/VoxelDensityTerrain.h`: `FVoxelDensityTerrain` (`IVoxelTerrain`).

- **Axes:** reference x = our X, z = our Y, y = our Z.
- **Blocks:** density > 0 is solid. Water fills open space up to Z 62 (sea level 63 - 1). Bedrock thins from Z -64 to -60 and deepslate mixes into stone from Z 0 to 8, as in the stand-in. The top layer is grass on 3 to 5 dirt, sand where the surface is within Z 56..64 and gravel below Z 56, until phase 5's surface rules.
- **Caves:** on by default (`FVoxelDensityTerrainSettings::bCaves`). Off (`voxel.Bench Density ... nocaves`) gives session 3's terrain exactly (`RemoveCaves`, water to Z 62), and its goldens stay. `RemoveCaves` makes range choices on `*/sloped_cheese` read it alone, and `*/caves/noodle` a neutral constant 64. With caves only the first surface from the sky is dressed (grass/sand/gravel); cave floors stay stone or deepslate.
- **Spawn:** `IsDry` is cheap (the preset's PS view at or above sea level). `ConfirmDry` and `GetSurfaceHeight` generate the real column (about a millisecond). The spawn search confirms at most 64 candidates. PS rounds down to 8-block cells and ignores the 3D noise, so on maps 74% of the overworld area estimates below sea level, though the real terrain has much more land.
- **Stats:** "Density column" (`STAT_VoxelDensityColumn`, trace `Voxel_DensityColumn`) and "Fill column blocks" (`STAT_VoxelFillColumn`, `Voxel_FillColumn`), both inside "Generate column".

Measured (Development editor, this machine, seed "Unnamed"), `voxel.Bench Density 8 <preset>`: 289 columns on one thread, ms per column. Skipping off is in brackets.

| Preset | Mean | p50 | p95 | Cells skipped |
|---|---|---|---|---|
| overworld | 2.05 (4.21) | 1.70 (3.33) | 3.58 (7.55) | 81% |
| large_biomes | 2.49 (3.87) | 1.99 (3.24) | 4.63 (6.30) | 76% |
| amplified | 3.57 (4.10) | 3.23 (3.37) | 6.54 (7.91) | 28% |

- **Split of a column:** density about 1.15 ms (flat 0.08, bounds 0.33, corners 0.11, cells to blocks 0.37 in the radius-4 run) and block fill about 0.8 ms.
- **Across 30 workers:** 0.13 ms per column (amplified 0.21).
- **Skipping is exact:** blocks differing with skipping on and off: 0 of 28,409,856 per preset.
- **Climate column** this run: 74 µs on one thread (46 µs in session 2; the machine was busier).
- **Targets:** under the 5 ms limit; the overworld is near the 2 ms target.

Caves off now (session 5) measures 2.03 / 2.26 / 2.34 ms mean (overworld / large_biomes / amplified; amplified was 4.06). The table above is session 3, kept as history.

Caves on (session 5), `voxel.Bench Density 8 <preset>`, 289 columns, one thread, ms per column; skipping off in brackets:

| Preset | Mean | p50 | p95 | Cells skipped (bounds + corners) |
|---|---|---|---|---|
| overworld | 4.53 (8.09) | 3.84 (7.12) | 7.71 (14.01) | 53.8% + 34.2% = 88.0% |
| large_biomes | 5.13 (7.84) | 4.44 (6.68) | 8.62 (13.30) | 41.6% + 44.6% = 86.2% |
| amplified | 5.74 (7.74) | 4.94 (6.47) | 10.40 (13.37) | 15.1% + 72.3% = 87.4% |

- **Session 4** (mean/p50/p95): overworld 7.83/6.57/13.78, large_biomes 8.65/7.44/14.04, amplified 11.04/10.16/18.09.
- **Split of an overworld column:** flat 0.08-0.09, bounds 0.27-0.28, corners 1.7-2.1, cells from corners 0.09, cells to blocks 0.6, aquifer 0.65-0.70, block fill 0.97-1.08.
- **Across 30 workers:** 0.260 ms per column (large_biomes 0.281, amplified 0.285).
- **Skipping is exact:** 0 of 28,409,856 blocks differ with skipping on and off, every preset, caves on and off. Goldens are unchanged.
- **Targets:** the overworld mean is under 5 ms (p95 is over). large_biomes (5.13) and amplified (5.74) are slightly over. p50 is under 5 ms for all three. None reaches the 2 ms target with caves.
- **Streaming flight with caves** (`Tests/Evidence/2026-10-09/VoxelFlight-20261009-035142`): no holes, 3 of 10,107 frames over the 2 ms chunk budget (worst 2.12 ms, one per circle), start to view 3.34 s (session 4: 3.64 s), late columns in the fog fade in 2,392 frames (session 4: 2,890; at most 35), memory over laps 2 and 3 3,831 -> 3,841 MB. Columns generate in 7.26 ms mean while workers mesh (13.8 ms in session 4). Apply worst frame 1.76 ms.

Streaming (`CaptureVoxel`, dev seed):

- columns generate in 3.7 to 3.8 ms mean (9.3 ms worst) while 30 workers also mesh;
- start to view on screen: 2.5 to 2.6 s (the stand-in took 0.5 to 0.8 s);
- the flight (`Tests/Evidence/2026-10-09/VoxelFlight-20261009-012136`): 0 of 10,675 frames over the 2 ms chunk budget, worst 1.74 ms, no holes, memory flat; late columns in the fog's fade in 1,527 frames (at most 35);
- the first view's apply peaked at 2.04 ms once.

Evidence: `Tests/Evidence/2026-10-09/VoxelShots-20261009-005643` (first generator-2 shots) and `DensityMaps-*` (PS and N maps).

### Aquifers

`Terrain/VoxelAquifer.h`: `FVoxelAquifer`. Our own design on the preset's `aquifers` inputs (barrier, exclusion, fluid_level_floodedness, fluid_level_spread, lava, surface_level) and 26.3's numbers.

- **Cells:** 16 x 12 x 16. Each has a centre jittered inside it by `Hash64(seed, "aquifer/center", cell)`.
- **Near the surface:** a centre within 12 blocks under the preliminary surface (PS, sampled at the cell column's middle), or above it, takes the global fluid: water below sea level 63, lava below y -54.
- **Deeper:** floodedness (clamped to ±1) is compared with thresholds that move with closeness under a sea floor.
  - Deep: global fluid above 0.8, own level above 0.4. Right under a sea floor: -0.3 and -0.8. Closeness falls to 0 at 64 blocks under the floor.
  - Exclusion > 0 keeps a cell dry.
  - An own level is the 40-block band's middle + spread * 10, quantized to 3, capped at the surface.
  - Lava when the level is <= -10 and |lava noise| > 0.3 (sampled per 64 x 40 x 64).
- **Per block:** a non-solid block takes the nearest of the 27 centres around it. Where the 2nd or 3rd nearest is almost as near (squared distance within 25) and differs, a wall term can make it stone. The term is strongest inside the band between the two levels, plus the barrier noise; lava against water always.
- **Global rules:** open space below y -54 is lava. Open space with no solid block above it takes the global fluid (no aquifer pockets in oceans or under open sky).
- **Skipping:** layers whose 27 centres all agree skip the per-block work. `GetKeepEmpty` flags the other y levels so `FVoxelColumnDensity` does not skip those cells as empty (barriers read exact density), which keeps skip on/off exact.
- **Cost:** 0.5 to 0.7 ms per column. Stat "Aquifer fill" (`STAT_VoxelAquiferColumn`).

## Storage and meshing

| Piece | Where | How it works |
|---|---|---|
| Block table | `Blocks/VoxelBlockTable.h`, `Voxel/VoxelBlocks.cpp` | One `FVoxelStateInfo` per state id, compiled from the registry on the game thread and shared read-only. It holds the shape (full, bottom or top slab, fluid), the layer (opaque, cutout, translucent, water), the seven face codes, the tint, the light, and three flags: solid, leaves and shades corners. `OpaqueFaces` marks the faces that hide a neighbour: all six for an opaque cube, only the bottom or top for a slab, none for glass or leaves. Water's faces come from its still and flow textures, as the Playground draws it. `VoxelBlocks.cpp` checks that the core's face order and cull bit match the registry's. |
| Sections | `Storage/VoxelSection.h` | 16^3 states, X fastest, then Y, then Z. One state needs no index array; up to 16 states use 4-bit indices; up to 256 use 8-bit; more store 16-bit state ids directly. Widths divide 64, so no index straddles two words. `Set` widens as the palette fills; `Compact` narrows. Allocations are exact. |
| Columns | `Storage/VoxelColumn.h` | 24 sections plus the four heightmaps (world surface, ocean floor, motion blocking, motion blocking without leaves), stored as Z - MinZ + 1 with 0 for none. Built from a dense column buffer on a worker, then read-only. An edit copies the column, and `SetState` sets one cell and its heightmaps through the helper `Build` shares. A generated column takes about 10 KB. |
| Terrains | `Terrain/VoxelTerrain.h` | `IVoxelTerrain`: a column generator the area builder, the pipeline, the spawn search and the bench all take. Every column is a pure function of the terrain's settings and its position. The dense scratch buffer is reused between columns, so a terrain must set every cell (`Dense.Init`), not rely on zeroes. |
| Flat terrain | `Terrain/VoxelFlatTerrain.h` | Superflat: the same stack of layers in every column, laid from Z -64 up and cut at the world's top. The surface is the top solid layer; a fluid above it makes the land not dry. |
| Height terrain (phase 2) | `Terrain/VoxelHeightTerrain.h` | A stand-in until phase 4. A 2D height comes from continent fBm (1/220), damped hills (1/90) and detail (1/24), giving sea level 63 plus up to about 100 blocks. Layers: bedrock thinning from Z -64 to -60; deepslate below 0, blending into stone up to 8; stone; 3 to 5 blocks of soil. The top is grass, sand at the shore, gravel deep under water, bare stone from 110 and snow from 128. Water fills to Z 62. |
| Density terrain (phase 4) | `Terrain/VoxelDensityTerrain.h` | Generator 2: 26.3's density graph over a column. See [Column density and terrain](#column-density-and-terrain-generator-2). |
| Meshing | `Meshing/VoxelMesher.h` | Each section meshes alone from an 18^3 padded copy: itself plus a one-cell border from its 26 neighbours, which may sit in other columns. A missing column reads as air. `GetSectionsTouching` is the remesh rule for an edit: the sections whose padded box holds the cell, up to 8 in 4 columns (`FVoxelSectionKey` names a section). Rules: see below. |
| Area builder | `Generation/VoxelArea.h` | Generates a square of columns, then meshes every section with its neighbours, one task each, timing every stage. `voxel.Bench Columns` uses it. The streamed world runs the same stages per column in the pipeline ([Streaming](#streaming)). |

Mesher rules:

- **Culling:** a face is hidden when its code has the cull bit and the neighbour's facing side is opaque, or when the neighbour is the same see-through block (glass beside glass). Leaves keep the faces between leaves. Water never draws against water.
- **Merging:** full-cube faces, water surfaces and water floors merge greedily in each 16 x 16 slice. The key is the state and the four corner occlusion levels, so a merged quad never blurs shading.
- **Partial faces:** slab faces and water sides stay one quad per cell.
- **Ambient occlusion:** per vertex, from the two sides and the diagonal in front of each corner. Each quad is split along its brighter diagonal.
- **Fast exits:** sections that are all air, or solid with every side covered, return at once.
- **Output:** positions are in blocks from the column corner. UV 0 carries the block UV, continuous across a merged quad; UV 1 the face and overlay codes; UV 2 the tint and face. Vertex colour R holds occlusion and G holds light.

Rendering:

- **Chunk component:** `UVoxelChunkComponent` is one RealtimeMesh per column with a single buffer set: every section's geometry, and a poly group per layer mapped to that layer's material slot. `SetMesh` hands the buffer set, the layer sections and the Lumen cards to RealtimeMesh in one update. The subsystem pools the components.
- **Lumen cards:** each column also carries a Lumen card representation, built on the workers with its stream set (`UVoxelChunkComponent::BuildCards`). For each face direction there is a card per section, fitted to that section's opaque and cutout faces in that direction. When a column would exceed Lumen's 32 cards per mesh, the vertically closest cards of its busiest direction merge first. Lumen captures the cards into its surface cache, which is how the terrain bounces light ([Lighting](#lighting)).
- **Chunk materials:** `M_ChunkOpaque`, `M_ChunkCutout`, `M_ChunkTranslucent` and `M_ChunkWater` reuse the block shader. They transform the continuous UV first, take each block's fraction, and sample with the continuous UV's gradients, so mips never jump at block edges. Texture animation uses the same `UFoundationBlockTextureTable`.
- **Collision:** none on the meshes. `AVoxelWorld::GatherCollision` returns block boxes (full or half for slabs) from the columns, as the Playground does from its block world. Columns not generated yet count as solid.

Measured on one thread, `voxel.Bench Columns 4` (81 columns, 1,944 sections, seed `Unnamed`):

| Stage | Mean | Worst |
|---|---|---|
| Generate a column | 0.42 ms | 0.58 ms |
| Gather a section's neighbours | 0.014 ms | 0.107 ms |
| Mesh a section (all, including quick exits) | 0.048 ms | 0.320 ms |
| Mesh a section with geometry (407 of them) | 0.202 ms | 0.320 ms |

A whole column (generation plus 24 sections) costs about 1.9 ms on one worker. On all 30 workers the 81 columns generate in 3.5 ms and mesh in 6.8 ms of wall time, though per-item times rise by a third from cache contention. The area holds 24,477 quads, about 60 per meshed section, and columns take 10 KB each. A column's Lumen cards take 0.010 ms to build (0.023 ms worst): 832 cards for the area, 10.3 per column and at most 26.

## Streaming

`AVoxelWorld` is the level actor of `/Game/Maps/L_Generated` and the map's `AFoundationLab`. It starts `UVoxelWorldSubsystem`, which streams columns around the player's camera, and owns the `FFoundationAuthority` that mines and places ([phase 3 record](Records/2026-10-08-Voxel-Phase3.md)). The world comes from its saved world's seed and terrain (`?VoxelWorld=<id>`, [Saved worlds](#saved-worlds)) or, when the map opens without one, the dev seed. `-VoxelSeed=<text>` overrides either seed.

Rings around the center column (`FVoxelStreamingRings`, in the core's `Streaming/VoxelStreamingPlanner.h`):

| Set | Rule | At radius 12 |
|---|---|---|
| In view | gap <= `ViewRadius`; meshed and drawn | 545 columns |
| Needed | in view or beside a column in view; generated, because a section meshes from its 3 x 3 | 657 columns |
| Unloaded | beyond `ViewRadius + 1 + UnloadMargin` | beyond 15 columns |

- **Gap:** the distance between the center column's square and another column's square, in columns. Every block within `ViewRadius` columns of the camera, anywhere in its column, lies in a column in view, so a fog ending at `ViewRadius * 16` blocks never shows a missing column.
- **Shape:** a circle. A square with the same clear distance is 25 x 25 = 625 columns, so the circle loads 13% fewer.
- **Hysteresis:** the unload ring is the prompt's "unload radius = load radius + 2", with the generated ring as the load radius. The margin has a minimum of 2 (`MinUnloadMargin`), which covers a diagonal move.
- **Order:** `VoxelStreaming::GetNeededColumns` sorts by distance, then ahead of the view (the yaw in 8 sectors), then Y, then X. `GetUnloads`, `Plan` and `GetViewSector` complete the planner. All are pure functions.

The pipeline (`FVoxelColumnPipeline`, `Voxel/VoxelColumnPipeline.h`):

- **Plain C++** with no UObjects, so tests drive it without a world. It keeps one record per column.
- **Jobs:** `UE::Tasks` at `BackgroundHigh`, up to `voxel.MaxTasksInFlight` (0 = twice the workers, 60 here). Walking the needed columns nearest first, a column gets a mesh job when it is in view and its 3 x 3 is generated, and a generation job otherwise.
- **A mesh job** meshes the dirty sections (all of them when it has no cache), skips all-air sections, then builds the stream set and the Lumen cards.
- **Stale results** are dropped: by a serial per record when the column unloads, and by a mesh version when it was edited while meshing.
- **Mesh cache:** section meshes are kept for columns within 2 columns of the center (1.3 MB at the first view; empty while flying, because new columns are meshed far away), so an edit remeshes only the touched sections.
- **`voxel.Debug.EdgeWalls`** meshes the view's edge against air (captures).

Editing:

- **`SetCell`** copies the column (copy on write, about 10 KB), sets the cell and its heightmaps (`FVoxelColumn::SetState`), and marks dirty exactly the sections whose padded 18^3 box holds the cell (`VoxelMesher::GetSectionsTouching`: up to 8 sections in 4 columns).
- **Edits** are kept per column with a stamp (the block world's revision) and replayed after the column generates, so they survive unloading. They last for the session until phase 10.
- **`FVoxelBlockStorage`** (`Voxel/VoxelBlockStorage.h`) is an `IFoundationBlockStorage` over the pipeline, reached through a getter so tests pass their own pipeline. A cell is loaded when its column is generated. `Empty` drops the edits.

The subsystem (`UVoxelWorldSubsystem`, a `UTickableWorldSubsystem` for game and PIE worlds, started by `AVoxelWorld`):

- **Focus:** the player's camera, so `voxel.Fly` works. Captures pin it to the spawn.
- **Budget:** all chunk work on the game thread in a frame (plan, jobs, apply) stays within `voxel.ApplyBudgetMs`. A column's apply cost is estimated from its quads (a line fitted to measured applies, weighted to recent ones, times 1.5), and applying stops when the estimate would pass 85% of the budget. A frame's first column always goes, so no column waits forever, however big. Each measured apply counts as at most 4 times its estimate plus 1 ms, so one stall does not raise the estimates after it.
- **Components** are pooled. Columns out of view stay loaded but hidden.
- **Instruments:** `stat Voxel` counters, a log line every second with the worst frame's plan, jobs and apply split, and a first-view report in the captures' `build.txt`.

The lab (`AVoxelWorld`):

- **Authority:** it owns an `FFoundationAuthority` over `FVoxelBlockStorage`, with bounds Z -64 to 319 and X and Y ±30,000,000. `TraceTarget` goes through it.
- **Presentation:** it creates the interaction view with block drawing and lab fixtures off.
- **Rules:** a Creative saved world gets infinite supply, no tool wear and the creative inventory. A Survival world gets finite supply and tool wear, and starts with the loadout's tools and no blocks. The bare map uses the Playground's lab defaults.
- **Collision and spawn:** columns not generated yet count as solid. The spawn is the dry surface nearest the origin, found from the terrain function. The player is put there once its 3 x 3 is generated, and falling below Z -128 returns the player to it.
- **Water:** `IsInWater` reads the blocks. A fluid cell counts up to `FluidAmount / 9` of a block, and fully when more fluid lies above it.

Fast travel and the acceptance flight:

- **`voxel.Fly [blocks per second]`** (`AVoxelFlyCamera`) is a free camera, default speed 40. W, A, S, D fly along the view, Space and Left Ctrl rise and sink, and Left Shift is x4. The keys are physical keys, not the player's bindings. The player is frozen: actor tick off, gameplay input off.
- **The flight** (`FVoxelFlight`; `-VoxelFlight=<folder>`, or `CaptureVoxel.ps1 -Flight`) flies at altitude 180 blocks with pitch -20: hover 2 s; east at 20, 40 and 80 blocks per second for 8 s each and at 160 for 6 s; then 3 laps of a circle of radius 288 blocks (1.5 view radii) at 80. It writes `flight.csv`, `flight.txt` and 4 screenshots. `flight.csv` has a row per frame: time, position, speed, leg, center column, columns generated, needed, in view and in view on screen, jobs, ready, applied, `tick_ms` (all chunk work), `plan_ms`, `jobs_ms`, `apply_ms`, holes, late, column and cache MB, process MB, and frame and GPU ms. One frame moves the camera at most 50 ms of flight, because the flight's own screenshots cause hitches of about 400 ms.
- **A hole** is a column closer than the fog's start (`ViewRadius - 1.5` as a gap), in the frustum, and never on screen. Columns still arriving inside the fade are counted apart as late columns.

At the edge the view ends in fog, a fog floor and aerial perspective rather than a wall ([Lighting](#lighting)).

Measured on this machine, Development editor, 1600 x 900. Runs before 20:23 are clean; after it another GPU-heavy app was running, so the edits run's GPU times are not comparable.

- **First view at radius 12** (`Tests/Evidence/2026-10-08/VoxelShots-20261008-193131/`):
  - generate 657 columns: mean 1.02 ms, worst 2.6 ms;
  - mesh 545 columns: mean 1.5 ms (1.19 ms in the flight run);
  - stream sets and cards: mean 0.20 ms;
  - quads: 166,376 (305 per column) with edge walls, 154,167 without; columns 10.3 KB each (6.6 MB); mesh cache 1.3 MB;
  - Lumen cards: 8.8 per column (most 29) with edge walls, 7.2 (most 13) without;
  - apply: 545 columns in 44 ms over 26 frames;
  - from the first frame to the whole view on screen: 0.5 to 0.6 s over about 30 frames. Engine startup before the first frame adds about 1.3 s in captures.
- **Acceptance flight** at radius 12 (`Tests/Evidence/2026-10-08/VoxelFlight-20261008-200725/`, 16,736 frames over 99.9 s):
  - holes: none;
  - late columns in the fog's fade: 1,155 frames, at most 33 columns, one frame each, when crossing a column corner moves the rings diagonally;
  - chunk work on the game thread: worst 1.81 ms, 0 of 16,736 frames over the 2 ms budget; the mean per leg is 0.01 to 0.14 ms (0.14 at 160 blocks per second);
  - memory over laps 2 and 3: process 3230 -> 3238 MB, column data 7.11 MB at the end of each lap; each lap generates about 3,360 columns and applies about 3,070;
  - GPU mean 3.25 ms, frame mean 6.0 ms.
- **Edits** (`Tests/Evidence/2026-10-08/VoxelShots-20261008-210841/` and `-210934/`): 166 cells in 4 columns around the corner (-16, -16) are back on screen 22 to 30 ms (2 frames) after the edit.

GPU frame per shot, mean of the last 60 settled frames (radius 12 with edge walls and no fog, `VoxelShots-20261008-192828/`, against phase 2W's 9 x 9 area, `VoxelShots-20261008-171008/`):

| Shot | Radius 12 | Phase 2W |
|---|---|---|
| `overview` | 3.32 ms | 2.86 ms |
| `closeup` | 3.37 ms | 2.77 ms |
| `ground` | 3.16 ms | 3.01 ms |
| `lumen-scene` | 3.95 ms | 3.47 ms |
| `section` | 3.07 ms | 2.76 ms |

To try it in the editor:

1. Open `Content/Maps/L_Generated` and press Play. You stand on the generated land and can walk, jump, look around, mine and place, and the land streams in as you move. Or start from the menu: Singleplayer, Create New World.
2. `voxel.Fly` detaches a free camera for crossing the world fast.
3. `voxel.Seed hello` rebuilds the world with another seed. `voxel.Regenerate` rebuilds it after you change `voxel.GreedyMeshing` or `voxel.AmbientOcclusion`.
4. `stat Voxel` shows the streaming counters and the generate, gather, mesh and apply timings. The reports are in the Output Log under `LogUnnamedVoxel`.

`& Scripts\CaptureVoxel.ps1 [-Seed <text>] [-World <id> [-WorldsDir <folder>]] [-ExecCmds '<console commands>'] [-Edits] [-Flight [-FlightSeconds <s>]]` renders the world offscreen (`-World` opens a saved world, from `Saved/Worlds` by default) and saves 5 screenshots plus `build.txt` to `Tests/Evidence/<date>/VoxelShots-<time>/`. `build.txt` holds the first-view report, the lighting settings in effect, each shot's GPU frame time and where the player stands. `-ExecCmds` compares settings without changing anything, for example `'r.Lumen.HardwareRayTracing.LightingMode 1'`. Edge walls are on by default for the shots. `-Edits` carves a 4 x 4 x 10 pit across the nearest dry column corner, builds a stone, glass and glowstone pillar on a column border, and adds an `edits` shot (the game's `-VoxelShotEdits`). `-Flight` flies the acceptance path instead and writes `Tests/Evidence/<date>/VoxelFlight-<time>/`; `-FlightSeconds` ends the flight after that many seconds. The shots:

- `overview`;
- `closeup` of the highest point;
- `ground` from the player's view;
- `lumen-scene` (what Lumen sees);
- `section` (a cut through the west edge).

## Saved worlds

Create New World makes a saved world, and Play opens one. Both go through `UMenuApplication` (`CreateWorld`, `RequestOpenWorld`, `RenameWorld`, `DeleteWorld`) to the store `UnnamedSavedWorlds` in `Source/Unnamed/Worlds/`. The page's list is native: the catalog's built-in worlds, then the saved worlds, most recently played first. The reference's two sample saves are gone.

- **Where:** `Saved/Worlds/<id>/world.json`, written crash-safely (`UnnamedFiles::SaveTextAtomically`). `-WorldsDir=<folder>` moves the store for tests and captures. Worlds save in the editor too: they are the player's content, not settings.
- **Ids:** made once from the name, as lowercase ASCII letters and digits joined by dashes. "My World!" becomes `my-world`, and a name with nothing usable becomes `world`. A taken id gets `-2`, `-3` and so on; an id is never a catalog id, a retired id or a Windows device name. A world id is a saved id: it never changes, so renaming changes only the name. Every id from the page is checked before it becomes a path.
- **world.json:**
  - `version` 1 and `generator` (2 for new worlds; generator 1 worlds keep the stand-in terrain, and Superflat stays flat under both);
  - the Create screen's own keys and values: `name`, `seed`, `gameMode`, `difficulty`, `commands`, `type`, `structures`, `bonus`, `biome`, `flatBiome`, `flat`, `experiments` and `rules`;
  - `seedValue`, the 64-bit seed written as text;
  - `created` and `lastPlayed` (UTC, ISO 8601).

  A file from a newer version or generator is refused, never rewritten. Re-Create refills the Create screen from these keys; a random seed comes back as its number.
- **Travel:** `L_Generated?VoxelWorld=<id>`. `AVoxelWorld` reads its world on first use and builds the terrain with `VoxelTerrainFactory`. The pawn asks its lab for the player's rules when it arrives (`AFoundationLab::ConfigurePlayer`).

| Choice | What it does now |
|---|---|
| Seed | Parsed at creation (`FVoxelSeed::Parse`); a blank seed draws a random one once, and the world keeps it. |
| World type | Superflat builds the flat terrain from its layers: listed top first on screen, laid from the floor up. Every layer must be a block this game has. Default and Single Biome build the overworld preset (Single Biome until phase 5); Large Biomes and AMPLIFIED build their own presets. Plain `L_Generated` (the dev seed) uses generator 2, or `-VoxelGenerator=1` for the stand-in. A saved generator-2 world whose preset cannot be read or compiled gets no stand-in: Create and Play refuse it before travelling, with the menu's error line ("This world cannot be played: ..."), and opened directly the map builds an empty terrain, logs the error and returns to the title screen (the title screen does not show the reason yet: the page shows errors only for its own requests). Only the dev seed falls back to the stand-in, with an error in the log. |
| Game mode | Survival and Creative set the player's mode and the lab's block rules: Creative gets infinite supply, no tool wear and the creative inventory; Survival gets finite supply and tool wear, and starts with the loadout's tools and no blocks. Hardcore plays as Survival on Hard: the screen and the store force Hard, no commands and no bonus chest. Permadeath needs a game-over screen first. |
| Difficulty | Peaceful, Easy, Normal or Hard in the player's vitals. Peaceful means no mob damage and, with natural regeneration on, a point of health every second and of food every half second. |
| Game rules | `natural_health_regeneration` drives the player's regeneration; the others are saved for their systems. |
| Commands, structures, bonus chest, experiments, Single Biome's biome | Saved; each takes effect when its system exists. Data Packs stay a mock. |

## Settings and console variables

`UVoxelWorldSettings` (Project Settings > Game > Unnamed Voxel) holds the authored defaults. A deliberate change belongs in `[/Script/Unnamed.VoxelWorldSettings]` in `Config/DefaultGame.ini`.

Runtime code reads the console variables, never the settings object. The settings push their values into the variables at project-setting priority, at startup and on every edit. A console, command-line or ini override wins for the session, for example `voxel.ViewRadius 16`.

| Setting | Console variable | Default | Used from |
|---|---|---|---|
| `DevSeed` | (`voxel.Seed` command) | `Unnamed` | 2: the world's seed when the map opens without a saved world (`-VoxelSeed=` overrides it) |
| `ViewRadius` | `voxel.ViewRadius` | 12 columns | 3: the circle of columns in view; the fog ends here |
| `UnloadMargin` | `voxel.UnloadMargin` | 2 columns (at least 2) | 3: unload beyond `ViewRadius + 1 + UnloadMargin`, which is 15 at radius 12 |
| `ApplyBudgetMs` | `voxel.ApplyBudgetMs` | 2 ms | 2; from 3 it bounds all chunk work on the game thread in a frame |
| `MaxTasksInFlight` | `voxel.MaxTasksInFlight` | 0 = twice the task workers (60 here) | 3 |
| `bAmbientOcclusion` | `voxel.AmbientOcclusion` | on | 2 |
| `bGreedyMeshing` | `voxel.GreedyMeshing` | on | 2 |

`voxel.Debug.Climate <C|E|W|PV|T|V|O|F|J|D|off>` (default off) shows all climate values at the player on screen (overworld preset) and highlights the one named. It also shows N at the player and PS (still not confirmed on screen: offscreen captures do not show on-screen debug messages). It is debug-only, like the next variable, so it has no row. The in-game minimap is not built yet; it comes with phase 5's biome debug map.

`voxel.Debug.EdgeWalls` (default off) is a debug-only variable, not a setting, so it has no row. It meshes the view's edge against air, so the outer columns draw their sides: the cut through the world that the `overview` and `section` shots show. `CaptureVoxel.ps1` turns it on for its shots.

## Commands and diagnostics

- `voxel.Info` prints the following, to the console and to `LogUnnamedVoxel`:
  - the world range and the dev seed;
  - every `voxel.*` variable, with its value and what set it;
  - the view radius with its column counts, for example `View radius 12: 545 columns in view, 657 generated; unload beyond 15 columns`;
  - the task workers, the tasks in flight and the mesh backend.
- `voxel.Seed [text]` shows the world seed a seed text makes, for example `Seed "Unnamed" -> -3224043261468865476 (text)`. In a voxel world it also rebuilds the world with that seed for the session. Without text it shows the current seed.
- `voxel.Regenerate` rebuilds the voxel world with its seed, picking up changed console variables.
- `voxel.Fly [blocks per second]` is the free camera ([Streaming](#streaming)).
- `voxel.Bench Noise [millions]` times each primitive on the calling thread (1 million samples each by default), then 3D fBm across every task worker.
- `voxel.Bench Columns [radius 4] [preset|standin] [nocaves]` builds `(2 radius + 1)^2` columns off screen (default: overworld with caves; `standin` is generator 1's height terrain). It reports the area stages (generate, gather, mesh) on one thread and then on every worker. For a density terrain it adds the generation stages (density, aquifer, blocks: p50, p95, mean), then block hashes in two load orders: row by row on one thread against backwards across every worker (`VoxelBench::HashLoadOrders`, CityHash64 of each column's dense blocks, combined in position order). Radius 8 hashes: overworld `0x7bbf1f42c5434443`, large_biomes `0xc70b24ba700501f2`, amplified `0x27c2d49086a4614a`; both orders agree, 0 columns differ.
- `voxel.Bench Climate [radius] [preset]` times the climate sampler over `(2 radius + 1)^2` columns (default radius 8 = 289 columns, overworld preset).
- `voxel.Bench Density [radius 4] [preset] [nocaves]` times the density terrain with skipping on and off, with its stages and the number of differing blocks, then on all workers. `nocaves` turns caves off.
- `Scripts\CaptureVoxel.ps1 -Caves` adds a cave view and a lava view to the capture.
- `voxel.ClimateMap <C|E|W|PV|T|V|O|F|J|N|PS|all> [size px 1024] [blocks per px 8] [preset]` writes PNGs to `Saved/VoxelClimate`, centered on the player (or 0,0), and logs the time and percentiles. `all` includes N and PS. PS is the terrain height. N is the final density at sea level (red solid, blue open), about 3.3 µs per point. O is drawn as the surface height the offset implies (y = -64 + (O + 1.5) x 128, before factor, jaggedness and 3D noise).
- `stat Voxel` shows the `STATGROUP_Voxel` timings and the streaming counters. Each stage adds its cycle stats there, next to a matching `TRACE_CPUPROFILER_EVENT_SCOPE` for Unreal Insights.
- `LogUnnamedVoxel` is the one log category, shared by both modules. While a voxel world runs it gets a line every second with the worst frame's plan, jobs and apply split.

## RealtimeMeshComponent

| | |
|---|---|
| Source | [TriAxis-Games/RealtimeMeshComponent](https://github.com/TriAxis-Games/RealtimeMeshComponent), the free Core edition |
| Commit | `b8669a0891b728d8fa8a31f43f76d3853c10f743` (`master`, 2026-08-23; plugin version 5.4; includes tag v5.3.2) |
| License | MIT, `Plugins/RealtimeMeshComponent/LICENSE.txt` |
| Copied | Everything except `.git` and `.github`. Never edited here. |
| Modules | `RealtimeMeshComponent` (runtime), `RealtimeMeshEditor` (editor), `RealtimeMeshExamples` (runtime example actors, kept so the copy stays pristine) |

To update it, replace the folder from a newer upstream commit, record the commit here, rebuild and run `Scripts\TestVoxel.ps1`.

What phases 2 and 3 found at this commit, and how the project works around it (all in `UVoxelChunkComponent`):

- **Several buffer sets in one mesh draw wrongly.** Only the most recently created one renders, with stray triangles from the others. The upstream examples use one buffer set per mesh. Workaround: each column is a single buffer set, and a changed section rebuilds its column's set.
- **Ray tracing needs aligned index offsets.** Each poly group gets a ray-tracing segment at its offset in the shared index buffer, and D3D12 asserts unless that offset is a multiple of 16 bytes (`D3D12RayTracing.cpp`). Workaround: every layer is padded to a multiple of four triangles with zero-area ones.
- **Cached bounds can go stale.** The component caches the mesh's bounds when render data changes, and a stale box frustum-culled near columns. Workaround: the component computes its own bounds, the column's footprint between its lowest and highest meshed sections.
- **Lumen needs cards from us.** The mesh accepts a Lumen card representation and a mesh distance field (`FRealtimeMesh::SetCardRepresentation` and `SetDistanceField`), and its scene proxy hands both to Lumen, but it generates neither. With hardware ray tracing the chunks are in Lumen's scene anyway, through their ray-tracing geometry, so without cards every surface-cache lookup on terrain came back black. Workaround: the column builds its own cards ([Lighting](#lighting)). No distance fields yet, so software Lumen does not see the chunks at all.
- **Updates can be batched.** `SetMesh` used to make up to six RealtimeMesh updates: the buffer set, up to four section configs, then the cards. It now makes one (`FRealtimeMeshUpdateBuilder`) for the buffer set, the sections and the cards. The layer sections come from a poly-group section handler set when the set is created.

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

**Edge fog (phase 3).** The view ends at `voxel.ViewRadius`. Instead of a wall or a dark void, the last 1.5 columns fade into the sky, so the land seems to run off into a distance that is too far and foggy to see. `AVoxelWorld` refits the map's `ExponentialHeightFog` from the view radius at runtime:

- **Fog:** clear until 1.5 columns before the radius and 99% opaque at it: density 1000 * ln(100) / 2400, about 1.92, where 2,400 cm is the fade. Height falloff 0.001. Cutoff distance 1e9 cm, so the sky stays clear.
- **Color:** matched to the sky just above the horizon, which measures (184, 189, 184) sRGB in screenshots: in-scattering (5900, 6500, 6000) cd/m2 and sky ambient contribution 0. Directional in-scattering keeps a glow on the sun side.
- **Floor:** a plane under the world's bottom follows the camera: the engine Plane scaled to 1,000 km, with no shadows, Lumen or ray tracing. Below the horizon past the edge it takes the fog's color instead of the sky atmosphere's dark ground.
- **Aerial perspective:** the sky's aerial perspective is scaled x50 at radius 12 (the scale follows 12 / R at radius R), so land pales with distance.
- **Shots from outside the world** (`overview`, `lumen-scene`, `section`) turn the fog, the floor and the aerial perspective off.
- **Cost:** the flight's mean GPU time went from 3.03 ms before the fog floor to 3.25 ms with the floor and the haze (`Tests/Evidence/2026-10-08/VoxelFlight-20261008-195430/` and `-200725/`).

Limits:

- **Hidden faces in a card.** A card captures the front face along its direction, so a face behind another facing the same way in the same section (a dune behind a dune, a valley narrower than 16 blocks) stays black in Lumen's scene. Rays that hit it get no bounce. It shows as black strips on the seabed in the `lumen-scene` shot. Caves and overhangs in phase 4 will make it more common. Options then: cards sliced along their direction, or hit lighting.
  - Session 4 compared the same cave mouth (`CaptureVoxel -Caves`): cards 3.29 ms against hit lighting 3.71 ms GPU at 1600x900. The images are identical (mean sRGB 63.1/70.2/59.0 against 63.2/70.2/59.0, also at +3 EV), so cards stay.
  - Caveat: the fixed daylight exposure (EV100 14.5) keeps cave interiors near black and lava's emission nearly invisible, which could hide card artifacts. Re-run the comparison once exposure is decided.
  - Evidence: `C:\dev\sandbox\phase4s4\montage\cavecmp2.png`, `Tests/Evidence/2026-10-09/VoxelShots-20261009-025021` (cards), `-025054` (hit), `-025126` and `-025159` (+3 EV).
- **Software Lumen.** On a GPU without hardware ray tracing, Lumen traces distance fields. The chunks have none, so it cannot see them: no bounce and no sky occlusion, and caves would be lit by the sky. Mesh distance fields for columns (`SetDistanceField`) are the fix when it matters.
- **Fog color.** It is fitted to the map's sun, so a day cycle would need it refitted. A faint seam remains on the sun side.
- **The Playground** keeps its own rig (sky light 1).

## Tests

`& Scripts\TestVoxel.ps1` runs `voxel.Info`, `voxel.Seed` and `voxel.Bench Noise`, then the 46 `Unnamed.Voxel` tests (every native suite: 94). Saved worlds are tested by `Unnamed.Worlds.Saved` (native) and, through the menu end to end, by `Tests/create-world.mjs`. It writes `Tests/Evidence/<date>/VoxelAutomation-<time>/`, with the commands' output in `voxel-console.txt`. The editor's throwaway user folder goes to a temp folder, not into the evidence.

| Test | Checks |
|---|---|
| `Unnamed.Voxel.Constants` | The world range, 24 sections, sea level, and that voxel sections match the block world's sections. |
| `Unnamed.Voxel.Settings` | Every setting's variable mirrors it unless overridden; `voxel.Info` lists every variable, the seed and the range; the command is registered. |
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
| `Unnamed.Voxel.Terrain.Flat` | The classic Superflat stack (bedrock, two dirt, grass) in every column with its surface and heightmap; water on top is not dry land; a stack taller than the world fills it; trailing air is dropped; no layers stands nowhere. |
| `Unnamed.Voxel.Terrain.Generate` | Five columns, including ones millions of blocks out, generated in order on this thread and in reverse on the workers: identical. Heightmaps follow the terrain function; bedrock floor and deepslate; every column under 64 KB; another seed changes the land. |
| `Unnamed.Voxel.Streaming.Planner` | 274 checks on the pure planner: the in-view and generated counts, coverage of the view, the order of loads, view sectors, hysteresis, the rows a one-column move adds and removes in steady state, radius changes, and far coordinates. |
| `Unnamed.Voxel.Streaming.Pipeline` | The pipeline without a world: results do not depend on load order (three runs); unload and reload; stale results are dropped; work in flight stays bounded; no column meshes before its 3 x 3 exists; done means done. |
| `Unnamed.Voxel.Edit.Sections` | The remesh rule (`GetSectionsTouching`) against a brute-force search. |
| `Unnamed.Voxel.Edit.Remesh` | Ten edits at section borders, column borders and corners. Exactly the touched sections remesh; every section equals a full rebuild; heightmaps; stamps; the edits survive an unload; `DropEdits`. |
| `Unnamed.Voxel.Edit.Authority` | `IsKnown`; `UseOn` placing; mining; refusals in unloaded columns. |
| `Unnamed.Voxel.Worldgen.Presets` | The three presets read and validate: sea level, height, blocks, every router and aquifer entry, and the debug views. Each world type's entry points come from its own files. Overworld nodes are checked by hand against their JSON: `y`, `shift_x`, the final density, the aquifer exclusion and barrier, an interval select, continentalness and shared references. Files no preset reaches also read: `zero`, `small_patch` and `gravel`. |
| `Unnamed.Voxel.Worldgen.RoundTrip` | All 51 function files and 62 noise files, read into one graph and written back, equal their files; so do each preset's router, aquifers and debug views. Fifteen malformed files are refused with a reason, including wrong-case types, fields and axes. Nodes found by their values pin each op's field order, which a shared field table could otherwise swap unseen. `Validate` refuses eight kinds of bad graph and `BuildSpline` two, each with its reason. |
| `Unnamed.Voxel.Worldgen.Splines` | The 9 terrain splines, baked into `FVoxelSpline`, against Mojang's values on 4,608 samples. |
| `Unnamed.Voxel.Worldgen.NoiseSpread` | `Tests/VoxelNoiseSpreadTests.cpp`: every climate noise of the three presets has a standard deviation within 10% and p1/p99 within 15% of vanilla. The tolerance was fixed before calibrating. All 62 noises are logged, and in fact all are within it. |
| `Unnamed.Voxel.Climate.NormalNoise` | `Tests/VoxelClimateTests.cpp`: golden checksums of the normal noise. |
| `Unnamed.Voxel.Climate.Ops` | Op values measured from the jar (the list under Density evaluator), plus our own cases for the rest: squeeze inside [-1, 1], positive half and quarter negative, clamp, min, max, abs, square, cube, gradients along x and z, batch independence, and that `interpolated` compiles and returns a constant. |
| `Unnamed.Voxel.Density.BlendedSpread` | `Tests/VoxelBlendedNoiseTests.cpp`: the blended noise's spread against the jar (the calibration under Deterministic core). |
| `Unnamed.Voxel.Density.Ops` | The jar-confirmed op values of session 3 (`interval_select`, `gradient`, `interpolated`, `find_top_surface`). |
| `Unnamed.Voxel.Density.Column` | The column equals direct evaluation bit for bit on 3 presets x 3 columns x 300 blocks; skipped cells keep the sign. |
| `Unnamed.Voxel.Density.Terrain` | Runs caves off and on. Skipping on and off are equal on 25 columns per preset; row, reverse and parallel orders are equal; the bedrock, sea and air rules; `GetSurfaceHeight` and `ConfirmDry` agree with the column. |
| `Unnamed.Voxel.Density.Golden` | Block hashes per preset, seed 263 (caves off, session 3's). |
| `Unnamed.Voxel.Aquifer.Determinism` | Two aquifers with one seed fill a column identically (and flag the same KeepEmpty rows); a scratch's earlier columns do not change a column; another seed differs. Load order and threads are checked by `Density.Terrain` with caves on. |
| `Unnamed.Voxel.Aquifer.Fluids` | Over 98 overworld columns: no water below y -54 and open space there is lava, no lava at y -10 or above, water above the sea only under solid ground, and lava and walls exist. |
| `Unnamed.Voxel.Density.LoadOrder` | In `VoxelAquiferTests.cpp`: 5 x 5 columns per preset with caves, generated in two load orders (row by row; backwards across the workers). Mismatches 0 and equal combined hashes. |
| `Unnamed.Voxel.Aquifer.Golden` | Caves-on block hashes at the `Density.Golden` positions (seed 263), recorded once: overworld `0xe9376e319a221c4c`, large_biomes `0x9e26565ae0dc0258`, amplified `0x3e5329bd4ce985c4`. |
| `Unnamed.Voxel.Climate.Presets` | Climate values of the three presets: goldens (each parameter weighted by its index, so swapped parameters fail), shared column edges, the PV formula. |
| `Unnamed.Voxel.Climate.Threads` | 8 threads in shuffled order match the single-threaded result. |
| `Unnamed.Voxel.Biome.Read` | `Tests/VoxelBiomeTests.cpp`: the overworld list reads as 56 biomes and 7,594 points and validates; every biome has a point; every point equals its row of `facts/biome_parameters.json`, read without the reader; spot values of six biomes (colours, temperature, downfall, modifiers), and only the two frozen oceans are frozen; `ReadBiomes` reuses the ids a table holds. Seven malformed small roots are refused, each error naming the file and the field. |
| `Unnamed.Voxel.Surface.Read` | The overworld tree reads as 231 rules and 107 conditions, with the pinned count of each of the 5 rule ops and 10 condition ops, validates, and has 37 named rules and 7 named conditions (`under_ceiling` is not read). What it names resolves: its `noise_threshold` noises exist, its two ore veins' density functions resolve, its 29 biomes are among the list's 56, and its 32 block ids are in the `minecraft:` namespace. Eight malformed small roots are refused with the file and the field. |
| `Unnamed.Voxel.Surface.RoundTrip` | All 44 named rule and condition files (37 and 7), written back by `WriteSurfaceRule` and `WriteSurfaceCondition`, equal their files in the reference. |
| `Unnamed.Worlds.TerrainCheck` | `Tests/WorldsTests.cpp`, outside `TestVoxel.ps1`'s 46: `VoxelTerrainFactory::Check` builds all three generator-2 world types from the reference; with an empty folder instead, a generator-2 world fails with an error naming its preset, while generator 1 and Superflat need no reference. A saved world whose preset cannot be read gets no terrain, but the dev seed falls back to the stand-in and says why. Each world type reads its own preset (Single Biome reads `overworld`). |

Aquifer mutation check: turning global lava into water, and disabling the walls, fail `Aquifer.Fluids` (3 checks) and `Aquifer.Golden` (3).

A deliberate off-by-one in `GetSectionsTouching` fails `Edit.Sections` (13 errors) and `Edit.Remesh` (11). Zeroing spline derivatives in `BuildSpline` and negating the reader's constants fail `Worldgen.Splines` (6 errors), `Worldgen.RoundTrip` (34) and `Worldgen.Presets` (1). Swapping `lerp`'s order and `shift_b`'s axes fails `Climate.Ops` (4 errors) and `Climate.Presets` (3). Making `interval_select`'s thresholds exclusive and swapping the column's x/z interpolation weights fail `Density.Ops` (3 errors), `Density.Column` (3) and `Density.Golden` (3).

## Known gaps

- **Lighting limits:** faces hidden behind a same-facing face inside one card stay black in Lumen's scene, and software Lumen cannot see the chunks ([Lighting](#lighting)).
- **Edge fog:** its color is fixed to the map's sun (there is no day cycle yet), and a faint seam remains on the sun side. Columns still arriving inside the fade show up late: in the session 5 flight (2,392 frames, at most 35 at a time), one frame each, when crossing a column corner moves the rings diagonally.
- **Pale water:** seen from high above, water over sand looks pale; tuning comes with biomes and water colour in phase 5.
- **Fluids:** generated water counts as water for the player, but it does not flow: carving into water leaves air pockets.
- **First view:** the view is on screen 3.3 to 3.6 s after the first frame with generator 2 and caves (2.5 to 2.6 s before caves; 0.5 to 0.6 s with the stand-in).
- **AMPLIFIED skipping:** it skips only 28% of cells (loose bounds where the factor is small).
- **Block fill:** it costs about 0.8 ms per column (the palette build and the per-block loop).
- **Caves near the limit:** a caves-on column costs 4.53 ms mean (overworld, under the 5 ms limit); large_biomes (5.13) and amplified (5.74) are slightly over. Remaining levers are listed in the phase 5 handoff: cross-column corner sharing, `base_3d_noise` speed, conditional noodle corners, an aquifer status cache, block fill by runs, the flight's 3 frames over 2 ms, and inner rings first.
- **Climate minimap:** not built; build it with phase 5's biome debug map.
- **`voxel.Debug.Climate`** on screen is unconfirmed since session 3.
- **Still lava:** it has no damage and no flow, and it counts as water for the player's movement.
- **Snow layers:** placement gives one layer (no stacking); eight layers collide as a full cube (26.3 collides at 7/8); the creative item icon draws as a cube.
- **Cave exposure:** the fixed daylight exposure keeps cave interiors near black and lava's emission nearly invisible (the lava view is still black). Still open; the brightness fix is proposed for phase 5's session 3.
- **PS biased low:** the spawn estimate rounds to 8-block cells and ignores the 3D noise.
- **Beaches:** sand-wide beaches until phase 5's surface rules.
- **Horizon seam:** the sky/fog seam at the horizon is unchanged from phase 3 (the same in the stand-in's flight).
- **Edits:** they last for the session until phase 10.
- **Survival** has no inventory screen yet. Creative has the creative inventory.
- **`voxel.Fly`** reads physical keys, not the player's bindings. The pawn's own Creative flight is deferred: it changes the shared 20 Hz movement step, and `voxel.Fly` covers fast travel.
- **The `section` shot** shows the circle's stepped edge.
- **Meshing under load:** with all 30 workers busy, a meshed section averages 0.26 ms against 0.20 ms on one thread. Streaming did not need the optimization; a bitmask visibility pass is the next one if a later phase does.
- **Deferred from the phase 3 review:**
  - **Unloads in one frame.** Columns past the unload distance are released in one frame, outside the apply budget. A teleport, a respawn or turning `voxel.Fly` off far away can release about 500 chunks at once, which causes a hitch. Fix: queue the releases and drain them under the apply estimate.
  - **Clearing a populated world.** `FFoundationBlockWorld::Clear` (from `FFoundationAuthority::Reset`) lists every solid cell, which over the streamed storage means every non-air cell generated. `AVoxelWorld::Regenerate` resets only while the pipeline is still empty. A reset of a populated world would allocate hundreds of MB and stall. Fix: let the storage say it streams, and have `Clear` call `Empty` only.
  - **`voxel.Fly` restore.** `AVoxelFlyCamera::EndPlay` turns the pawn's tick and gameplay input back on unconditionally, even on quit or a level change. Fix: record the state when freezing and restore only that.
- **World choices still to land:** Hardcore has no permadeath or hardcore hearts; Single Biome generates the overworld terrain until phase 5; Superflat refuses layers of blocks the game lacks.

The registry has full cubes, slabs, powder snow, snow layers, still water and still lava. The generator also needs:

- grass, flowers and ferns, cactus, sugar cane, kelp, seagrass, vines and pointed dripstone;
- dirt paths and farmland;
- stairs, fences, doors and panes, for villages.

Each is listed in Status before the phase that needs it.
