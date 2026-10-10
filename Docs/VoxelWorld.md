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
| 4 | Terrain: 26.3's density graph, climate, caves, aquifers and lava | Done (5 sessions: worldgen data and reader; noise, climate, splines and debug views; 3D density and generator 2; caves on, aquifers, still lava, cave-lighting decision; performance). Caves-on columns were slightly over 5 ms on two presets; phase 5 session 3's corner sharing brings them under it in the bench's best case ([Known gaps](#known-gaps)) |
| 5 | Biomes and surface | Done (6 sessions: reference data, blocks and readers; performance levers; cave brightness and the cave-lighting comparison; the biome source, storage, maps and minimap; surface rules, temperature, the snow line and Single Biome; biome tints, ocean variants, the batched normal noise, captures and the record). AMPLIFIED's column mean was over the 5 ms limit until phase 6's speed-up session; [the phase 5 record](Records/2026-10-09-Voxel-Phase5.md) |
| 6 | Decoration, after a block-shapes step (plants, cross models) | In progress ([the phase 6 plan](Records/2026-10-09-Voxel-Phase6-Plan.md)). S0, the speed-up, is done: every preset's column mean is under 5 ms ([Column density](#column-density-and-terrain-generator-2), "Speed-up session S0"). S1 (worldgen data and reader) is done: the decoration data the overworld reaches is in the worldgen reference, read into `FVoxelFeatureGraph` with its per-step order ([Worldgen data](#worldgen-data)); generation does not use it yet. S1b is done: provider histograms, vanilla's feature order and the ore vein samples dumped from the jar, and the feature probe harness with the 24 ore probes ([Worldgen data](#worldgen-data)). S2 is done: the decoration core (Decorate, the write log in 9 buckets, Finalize) with the ore feature matched to the probes, on the fixed area, the benches and a capture preview ([Decoration](#decoration)); Decorate's 0.6 ms gate is not met on large_biomes and amplified. S3 is done: streaming decorates (Generate, Decorate, Finalize and Mesh as stages on three rings past the view, recompute-on-miss, the freeze pass at Finalize; [Streaming](#streaming)), the preview is retired; the flight has no holes and the first view is under 4 s. S4 is done: 26.3's ore veins through our own evaluator, on by default, matched to the probe's samples and exact against direct evaluation ([Ore veins](#ore-veins-phase-6-s4)). S6 (block models, rendering) is merged from its lane. Scout E's ore look calls L1 and L2 level 1 are done: render-only switches whose default stays vanilla, both accepted ([Ore looks](#ore-looks-phase-6-look-calls-l1-and-l2)). S8 (plants and patches) is done in two parts, A and S8b: 74 vegetation placed features on by default (`voxel.Decoration.Plants`), within Decorate's 0.4 ms allowance, the flight with no holes ([Plants and patches](#plants-and-patches-phase-6-s8)); its part B is glow lichen's multiface_growth, clamped to the 3x3 window (Jesus's answer). S6b is merged; S7 runs in its own lane. S8 part B (glow lichen) is done. S9a (the tree core) is done: the tree feature with 5 trunk and 8 foliage placers fitted to scout J's probes, on by default (`voxel.Decoration.Trees`, Jesus's answer), 23 tree configurations in the world ([Trees](#trees-phase-6-s9a)). Next in the main lane: S9b, the big trees first (dark oak, pale oak, mega spruce, mega pine, mega jungle), then the tree decorators, fallen trees and huge mushrooms |
| 7 | Structures that fit the land (needs stairs, doors, fences, paths) | |
| 8 | Mountains | |
| 9 | Far LOD | |
| 10 | Persistence and polish | |

## Layout

| Where | What |
|---|---|
| `Source/UnnamedVoxel/` | The core module: RNG, noise, density, biomes, storage, meshing and the streaming plan in plain C++. It depends on `Core` only, so UObjects, actors and render state cannot be included, and it compiles with precise floating point (below). Headers are included by their path from this folder (`"Random/VoxelSeed.h"`). It holds:<br>- `Random/`: seeds, hashing, streams;<br>- `Noise/`: gradient and fractal noise, and the 26.3-style normal noise;<br>- `Spline/`: nested splines;<br>- `Density/`: the density graph, the reference's density functions as plain data, the evaluator, the column density and its shared corner cache;<br>- `Blocks/`: the per-state table;<br>- `Biome/`: the biome table, the biome source and each column's biome storage;<br>- `Surface/`: the surface rules;<br>- `Storage/`: sections and columns;<br>- `Terrain/`: the terrain interface, the phase 2 height terrain, Superflat's flat terrain and the climate sampler;<br>- `Meshing/`: the mesher and its remesh rule;<br>- `Generation/`: the area builder;<br>- `Streaming/`: the planner (which columns are in view, which exist, which unload);<br>- `VoxelBench`: timings;<br>- the module-wide `VoxelConstants`, `VoxelLog` and `VoxelStats`. |
| `Source/Unnamed/Voxel/` | The Unreal side:<br>- `UVoxelWorldSettings` and the console (`VoxelConsole`);<br>- the registry-to-table bridge (`VoxelBlocks`);<br>- the worldgen readers (`VoxelWorldgenReader`, `VoxelSurfaceReader`, shared helpers in `VoxelWorldgenJson.h`): the reference's JSON into the density graph, the biome table and the surface rules;<br>- the terrain factory (`VoxelTerrainFactory`): every world's terrain, used by the voxel world and by the menu's check before travel;
- the climate debug tools (`VoxelClimateDebug`): a cached sampler per (preset, seed), the map writer and the values at the player;<br>- the biome debug tools (`VoxelBiomeDebug`): a cached biome source per (preset, seed), the biome map writer, and the readout and minimap on screen;<br>- the column mesh (`UVoxelChunkComponent`);<br>- the column pipeline (`FVoxelColumnPipeline`, plain C++ with no UObjects);<br>- the streaming subsystem (`UVoxelWorldSubsystem`);<br>- the storage adapter (`FVoxelBlockStorage`);<br>- the level actor (`AVoxelWorld`);<br>- the free camera (`AVoxelFlyCamera`) and the acceptance flight (`FVoxelFlight`). |
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
- **Decoration (phase 6, S1):** what the 56 biomes' `features` and `carvers` lists reach, found by `build_reference.py` key by key (a string names a placed feature, a feature or a provider only at the keys that hold one): 210 placed features (`placed_feature/`, 171 listed by biomes and 39 nested), 182 features (`feature/`, ids nest: `coral/brain_block`), the 8 block state providers (`block_state_provider/`), 3 carvers (`carver/`), and 127 block tags (`tags/block/`): those the data names, the 24 the game's code reads by name (`CODE_TAGS`: `replaceable`, `replaceable_by_trees`, `features_cannot_replace`, `logs`, the two heightmap tags and the plants' and snow's support tags, each with what reads it), and every tag those include.
- **Biome facts:** the jar builds the parameter list in code, so `facts/biome_parameters.json` holds it: 7,594 points over 56 biomes, quantized x10,000, in list order. `facts/biome_lookup_samples.json` has 5,120 seeded target points with the jar's brute-force and indexed answers; they differ on 95, all ties, checked with an independent Python scan.
- **Decoration facts (phase 6, S1b):** `WorldgenFacts.java` loads the game's registries as a dedicated server does (`WorldLoader` over the vanilla pack) and writes three facts; the four older ones come out byte-identical.
  - `facts/provider_histograms.json`: the 119 distinct int, float and height providers in the decoration data (71, 14 and 34), each decoded with the jar's codec, with every use and 10^6 samples (counts per value, or 64 bins for floats). S2's `Providers.Distribution` compares against it.
  - `facts/feature_order.json`: vanilla's per-step order (`FeatureSorter` over the overworld's 56 biomes). `Features.Order` checks ours against it.
  - `facts/ore_vein_samples.json`: the overworld material rule run by the jar's surface system on stone chunks (3 seeds, 2 chunks each, Y -64 to 56), with the six `overworld/ore_vein/*` densities beside 5,575 recorded positions. No vein block sits where its type's density is 0 or below; where it is above 0, 67 to 69% of positions hold a vein block (`VEIN_SOLIDNESS` is 0.7). S4 built the evaluator against it (`Surface.OreVein`, [Ore veins](#ore-veins-phase-6-s4)).
- **Feature probes (phase 6, S1b):** `tools/feature_probe.py` runs a headless 26.3 server on a layered superflat world (deepslate, tuff from Y -16 to 15, stone, a 1-block air layer at Y 84), places each of the 24 ore features 100 times per band with `/place feature` (deepslate Y -40, tuff 0, stone 44, the air layer 84), and reads the region files back; a 3x3x3 gold box per band per ore proves the reader (27 found every time, nothing else changed). `facts/feature_probes/<ore>.json`: blocks per state as distributions, extents, block totals per offset and per Y, what was replaced, and exposed against solid counts. Findings:
  - Blobs sit below their origin: Y offsets -3 to 0 up to size 12, -4 to 1 at size 20 (copper_large), -5 to 2 at size 33 and -6 to 3 at size 64; X and Z offsets reach -10 to 9 at size 64.
  - In tuff, the stone ore wins where both targets match (Y 0 to 8, the stone ore listed first): every two-state ore gives its stone ore at Y 0 and its deepslate ore below 0 (the blobs of the small ores reach no higher than Y 0).
  - The air exposure ratio follows `discard_chance_on_air_exposure`: about 1 at 0, 0.5 to 0.6 at 0.5, 0.33 at 0.7 and 0 at 1.
  - Small sizes often place nothing: ore_emerald (size 3) fails 60 to 73% of placements, ore_diamond_small and ore_iron_small (size 4) 24 to 35%.

| Piece | Where | How it works |
|---|---|---|
| Density graph | `Density/VoxelDensityGraph.h` | `FVoxelDensityGraph` holds the reference's 30 node types (`EVoxelDensityOp`) as one flat array of nodes with children by index, plus splines, interval lists and noises. A named function is one node however often it is referenced, so evaluation can compute it once per point. Every node comes after its inputs, and every spline after its nested splines, so evaluating in index order needs no recursion. `Validate` checks indices, each op's inputs, that order, thresholds and names. `BuildSpline` bakes a spline into an `FVoxelSpline`, with its coordinates in first use. |
| Preset | `Density/VoxelDensityGraph.h` | `FVoxelTerrainPreset`: a world type's sea level and height, its router entry points, its aquifer inputs and the 9 debug views (N, T, V, C, E, D, W, PV, PS), all nodes of one graph. The router entries are continents, erosion, ridges, temperature, vegetation, depth, chunk surface level and final density. The aquifer inputs are barrier, exclusion, floodedness, spread, lava and surface level. |
| Reader | `Voxel/VoxelWorldgenReader.h` (game module) | `VoxelWorldgen::ReadPreset` and `ReadReferences` read files into the graph. `WriteValue` and `WriteNoise` write nodes back as the reference's JSON. Reading is strict: an unknown type, a missing or unexpected field, a bad id, a missing file or a cycle fails with the file and the field. Noise files leave out `octave_count` when it is 1 and `amplitude_modifiers` when all are 1; the reader fills both in. |
| Biome table | `Biome/VoxelBiomeTable.h` | `FVoxelBiomeTable`: the biomes (`FVoxelBiome`) and the parameter points (`FVoxelBiomeParameter`) of one list, as plain data. `Quantize` turns a float into the table's x10,000 integer and `Validate` checks the table. The lookup is `FVoxelBiomeSource` ([Biomes](#biomes)). |
| Surface rules | `Surface/VoxelSurfaceRules.h` | `FVoxelSurfaceRules`: a rule tree as flat arrays of rules and conditions, every node after its inputs, as the density graph keeps its nodes. 5 rule ops (`sequence`, `condition`, `block`, `ore_vein`, `bandlands`) and 10 condition ops (`biome`, `noise_threshold`, `vertical_gradient`, `y_above`, `water`, `stone_depth`, `not`, `steep`, `hole`, `above_preliminary_surface`). Ids (blocks, biomes, noises) stay strings for the evaluator. `Validate` checks it. The evaluator is `FVoxelSurfaceEvaluator` ([Surface rules](#surface-rules-temperature-and-snow)). |
| Feature graph | `Decoration/VoxelFeatureGraph.h` | `FVoxelFeatureGraph`: the decoration data as one flat table of typed nodes (`EVoxelFeatureKind`: placed features, features, modifiers, block predicates, rule tests, providers, block states, int, float and height providers, anchors, tree parts, carvers and template parts), each with a type id and a row of fields. A field holds a number, a bool, a string, a child node, a list, or an entry named by id (`Ref`); children come before their parents. Entries per registry (placed features, features, providers, carvers) map ids to nodes; tags keep their files' values (`ExpandTag` resolves includes); each biome keeps its 11 step lists and its carvers. Ids stay strings: later sessions compile the types they run, as the surface evaluator compiles its rules. `Validate` checks indices, kinds per registry, refs, steps and tag includes. |
| Feature order and seeds | `Decoration/VoxelFeatureOrder.h` | `FVoxelFeatureOrder`: one order per step, a topological sort of every biome's list for that step; of the features free to go next, the smallest id goes first. Contradicting lists are refused with the step named. Per step 0, 4, 5, 4, 4, 0, 34, 7, 3, 109, 1 features (171). `VoxelFeatureSeeds` (Jesus's answer 4): a placed feature's salt is FNV-1a 64 over its full id's UTF-8 bytes, finished with `Mix64`; a feature's stream in a column is `Hash64(world seed, DecorationSalt ^ salt, column x, column z, step)`; flower-band and noise-count pattern noises take `Hash64(world seed, PatternSalt, data seed)`, so they move with the world seed (vanilla's sit alike in every world, seeded 2345). |
| Feature reader | `Voxel/VoxelFeatureReader.cpp` (declared in `VoxelWorldgenReader.h`) | `ReadFeatures` reads from the biome tag through every entry and tag named, as `build_reference.py` reaches them, and `WriteFeatureNode` and `WriteBlockTag` write them back. A schema, one line per kind and type the reachable data uses, gives each field's kinds; reading is strict: an unknown type or field, a missing field, a value of the wrong kind, a bad id or a missing file fails with the file and the field path (`worldgen/placed_feature/ore_iron_upper.json: placement[0].count: unexpected a string`). The 587 files are loaded and parsed in parallel first, then read in order: 33 to 46 ms in the editor's tests (about 20 ms loading and parsing, 13 to 19 ms reading), and at world start 37.6 ms (the flight's dev seed) and 39.3 ms (a generator-2 world saved before S1), within S1's 50 ms gate; one by one, opening the files alone took 55 ms. The package `VoxelPhase6S1-20261009` stages all of it and reads it in 42.0 ms, but 174.9 ms on its first run after staging (151.8 ms loading and parsing cold files). The terrain factory reads it at world start after `ReadInputs`; a saved world whose decoration data cannot be read is refused, and the graph is held in `FVoxelDensityTerrainSettings::Features` for the decoration stages. Packages already stage it with `data/`. |
| Biome and surface readers | `Voxel/VoxelSurfaceReader.cpp` (declared in `VoxelWorldgenReader.h`) | `ReadBiomes`, `ReadBiomeParameters` and `ReadSurfaceRules` read the files; `WriteSurfaceRule` and `WriteSurfaceCondition` write nodes back as the reference's JSON. Strict like `ReadPreset`, with the file and field in every error; a biome file's `attributes`, `features` and `carvers` are skipped by name. The overworld tree reads as 231 rules and 107 conditions, with 37 named rules and 7 named conditions. The JSON helpers shared with `ReadPreset` are in `Voxel/VoxelWorldgenJson.h`. |

- **Size:** each preset reads as 24 functions, 279 nodes, 31 noises and 118 splines with 430 points, in about 8 ms in the editor. All 51 functions and 62 noises together make 530 nodes.
- **Splines:** our `FVoxelSpline` matches Mojang's `CubicSpline` on all 4,608 samples, within 9.5e-7 (float rounding).
- **Noise facts:** `facts/noise_samples.json` has 62 noises with range, mean, standard deviation and 11 percentiles over xz and xyz, from 4 seeds x 16,384 positions, dumped by `tools/WorldgenFacts.java`.
- **Verifier:** it now reports `REFERENCE OK: 786 files hashed, 749 compared with the jar, 580 references resolved, 9 splines with 4608 samples, 44 rule and condition files with 182 references, 7594 biome points over 56 biomes, 5120 lookup samples, 530 decoration files and block tags with 3557 references and 1102 blocks, vanilla's order over 56 biomes, 119 provider histograms, 5575 ore vein samples, 24 feature probes`. Since S1b it also checks that vanilla's order holds each step's listed features, that every provider histogram counts all its samples and matches the data at each use, and that every probe names a reached feature. From the biomes it resolves every placed feature, feature, provider, carver and tag reference, requires every file in those folders to be reached, and finds every block id in the blocks reference's registry list (`facts/block_facts.csv`, all 1,286 blocks; 85 decoration blocks are not imported yet, phase 6 S5). In S1 every existing data and facts file stayed byte-identical; only `manifest.json`, `README.md`, `build_reference.py` and `verify_reference.py` changed. In S1b likewise; only `README.md`, `WorldgenFacts.java` and `verify_reference.py` changed, beside the 28 new files.
- **In use:** the terrain uses the graph since session 3 (generator 2). Packages stage the reference's `data/` folder (749 files since phase 6 S1) and `facts/biome_parameters.json` as runtime dependencies at the same paths under the project (`Unnamed.Build.cs`), so `GetReferenceRoot` works unchanged. Phase 6 S1's package read the decoration data from there (`PackageShots-s1-*`).

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
- **Batch `old_blended_noise` (phase 5, session 3):** `FVoxelBlendedNoise::Sample` over a batch, one octave at a time (`FVoxelGradientNoise::AccumulateOctave`, with the gradient noise inlined). Each point adds the same octaves in the same order and skips the same limit, so its value equals the single-point `Sample` bit for bit (test `Density.BlendedBatch`). No measurable gain yet (at the noise floor, about 0.03 ms per column); it is the groundwork for the normal-noise lever.

### Climate

`Terrain/VoxelClimate.h`: `FVoxelClimateSampler` evaluates continentalness (C), erosion (E), weirdness (W), peaks and valleys (PV), temperature (T), vegetation (V), and the depth function's offset (O), factor (F) and jaggedness (J). Offset, factor and jaggedness are found as functions in the router depth's folder. `SampleColumn` takes the 5 x 5 cell corners (4 blocks apart) and a time; `SampleDepth` takes one block. The cycle stat is "Climate column" (`STAT_VoxelClimateColumn`) with the trace scope `Voxel_ClimateColumn`. The biome source reads it at each column's quart columns; it also serves the debug views and benches.

| Measured (Development editor, this machine, seed "Unnamed") | Time |
|---|---|
| Climate column, one thread | 46 µs, after one warm-up column (45 to 51 µs across runs and presets) |
| Climate column across 30 workers, one scratch per worker | 6 to 7 µs per column over 289 columns (too few to scale fully) |
| Maps | 0.12 µs per point (1024 x 1024 in about 125 ms on all workers) |
| Reading a preset | about 8 ms |

This is under the 2 ms target.

On the maps (seed "Unnamed"): continents span thousands of blocks; Large Biomes are about 4x larger; AMPLIFIED raises more peaks; PV valleys form river-like channels. About 43 to 48 % of the area estimates below sea level, which includes rivers and beach-level land the estimate puts just under 63. Evidence: `Tests/Evidence/2026-10-09/ClimateMaps-20261009-001155/` (PNGs and the montages `climate-overworld-8192.png` and `climate-continental-32768.png`).

### Biomes

`Biome/VoxelBiomeSource.h`: `FVoxelBiomeSource` picks each quart's biome (a 4 x 4 x 4 block cell) from the overworld parameter list over the preset's climate. All three world types use that list, as 26.3's presets do; Large Biomes stretches the climate instead.

- **Fitness:** the sum over the six parameters of the squared distance outside each inclusive range, plus the point's offset squared, in the jar's quantized units (x 10,000, truncated toward zero). Ties go to the first point in list order, which is the jar's brute-force answer.
- **Lookup:** the 7,594 points form four depth groups: depth 0 (3,795 points), 1.0 (3,795), 0.2 to 0.9 (3, the cave biomes) and 1.1 (1, the deep dark).
  - Within a group the depth term is the same for every point. A bounding-box tree over the other five parameters (leaves of 8, split at the median of the widest axis) finds each group's best point exactly. It is a branch and bound that prunes a node only when its bound is higher than the best, or equal with no lower list index below it.
  - The answer is the best group winner once each group's depth term is added.
  - A column searches each group once per quart column (25 of them: its own 4 x 4 plus the next column's first row and column), starting from the previous quart column's winner. It then sweeps the 96 quart depths over the four winners.
  - This is exact by construction. `FindBruteForce` scans every point, for the tests and the bench gate.
- **Climate:** a quart's biome is the one at its minimum corner (block 4q).
  - T, H (vegetation), C, E and W come from `FVoxelClimateSampler` at the 25 quart columns.
  - Depth is the router depth's y gradient plus the offset, computed as the evaluator computes it, so it is bit-identical to `SampleDepth`.
  - `Initialize` refuses a preset whose depth is not the offset plus a y gradient.
- **Fixed source:** `InitializeFixed` gives one biome everywhere: Single Biome's, from world.json's `biome` ([Surface rules](#surface-rules-temperature-and-snow)).
- **Storage:** `Biome/VoxelBiomeStorage.h`: `FVoxelBiomeStorage` holds one byte per quart over the column's 5 x 5 x 96 quart lattice (2,400 bytes: its own 4 x 4 plus the next column's first row and column).
  - Every block's lookup is answered inside its own column, whatever is loaded around it. The ring equals the neighbours' own values, because they are pure.
  - `FVoxelColumn` carries the storage (`GetBiomes`, `GetBiome(x, y, z)`). Superflat and the stand-in leave it empty, and an edit keeps it.
- **Jitter:** a block takes the biome of the nearest site among its cell's 8 corners.
  - Each corner's site moves by up to 0.45 quart per axis, from `Hash64(seed, "biome/jitter", quart)` in world quart coordinates (three 10-bit fields). The top layer has no corner above it.
  - Borders come out ragged instead of following the 4-block grid, as 26.3's biome zoom makes them, through our own hash.
- **Column job:** `FVoxelDensityTerrain`, given a biome table, fills the storage after the aquifer and before the blocks, which the surface rules will choose by biome.
  - `VoxelTerrainFactory` passes the overworld list for every generator-2 world.
  - A list that cannot be read refuses a saved world, as an unreadable preset does; the dev seed falls back.

| Measured (Development editor, seed "Unnamed", `voxel.Bench Density 4`: 81 columns per preset; Minecraft open) | Time |
|---|---|
| Biome stage per column, one thread | mean 0.19 ms (p50 0.14 to 0.17, p95 0.27 to 0.45) over the three presets |
| Brute force per column (the gate's reference) | 172 to 181 ms on a worker |
| Biome maps | 0.25 µs per point (1024 x 1024 in about 0.26 s on all workers) |

- **Gates:**
  - pruned against brute force: 0 of 21,600 quarts differ (9 columns per preset);
  - biomes on and off: 0 blocks differ;
  - radius-8 block hashes are unchanged;
  - biome hashes agree in both load orders: overworld `0x787dbeb131ce9f63`, large_biomes `0x350119336bfbc720`, amplified `0x6a5593310ccd357d`.
- **Memory:** a column takes 19.2 KB in the capture, against 16.8 KB before biomes.
- **Maps** (seed "Unnamed", at the surface, 8,192 blocks across):
  - 51 of 56 biomes in the overworld and AMPLIFIED, 28 in Large Biomes (53 and 52 at 32,768 blocks).
  - Largest overworld shares: forest 15.0%, plains 11.0%, cold ocean 7.0%, river 6.7%, desert 4.7%.
  - At depth 0, AMPLIFIED's map equals the overworld's. The two share every climate noise and differ only in the offset, which depth 0 sets aside; their columns differ underground.
  - The jar's lookup samples are uniform climate targets, so their shares (in each map's CSV) measure climate space, not area. They confirm which biomes the mapping reaches.
  - Evidence: `Tests/Evidence/2026-10-09/BiomeMaps-20261009-north/` (`montage-8192.png`, `montage-32768.png`, a CSV per map).

### Column density and terrain (generator 2)

`Density/VoxelColumnDensity.h`: `FVoxelColumnDensity` evaluates final_density over a 16 x 16 column.

- **Corners:** nodes under `interpolated` run at the 5 x 5 x 49 corners of 4 x 8 x 4 cells (flat ones once per corner column, 5 x 5). Inside a cell they are trilinear. Nodes above it run per block.
- **Height-bound skipping:** each cell is bounded first. A certainly positive cell is solid and a certainly non-positive one is empty; neither is evaluated. Each cell column is bounded from the top in stacks of 8 cells (`BoundStack`) while a whole stack is certain, then one cell at a time down to the first mixed cell. Cells below it stay mixed (with caves their bounds never proved anything). Only the corners of mixed cells are evaluated. Overworld caves on: bounds 1.51 -> 0.27 ms.
- **Cells from corners (stage 3b):** once a mixed cell's corners are evaluated, each interpolated node is bounded by its 8 corner values (trilinear weights are convex; a margin of 1e-5 x max |corner| + 1e-30 covers float rounding). `EvaluateBounds` then runs over the per-block nodes for the cell's y range, so per-block terms outside `interpolated` get their own bounds. Certainly positive means solid; certainly <= 0 and not KeepEmpty means empty; neither needs per-block work. Overworld caves on: 34.2% more cells settled, cells to blocks 2.38 -> 0.75 ms. Stats: `RefinedCells`, `RefineMs`.
- **Weights table (stage 4):** trilinear weights by offset are precomputed as float(double(D) / Cell), the expression direct evaluation uses, so results are bit-identical. It removes 3 double divisions per block per interpolated node.
- **Exact:** results equal direct evaluation bit for bit (test `Density.Column`).
- **Corner sharing (phase 5, session 3):** the corners on a column's edges are the ones its neighbours need, so `Density/VoxelCornerCache.h` (`FVoxelCornerCache`, one per terrain) keeps the edge corner columns' interpolated inputs once evaluated: per column the 4 on its west line and 3 more on its north line, direct-mapped over a 32 x 32-column window. Nothing inside the loaded area (31 columns across) collides, and a corner column a window away takes its slot over (the newest wins). Each slot has its key, a 49-bit mask of the corners present and a 1-byte `UE::FMutex`; the table takes 6.9 MB, fixed at construction (at most 16 MiB). Stage 3 copies the needed edge corners it finds, evaluates only the rest, then stores what it evaluated. Corners are pure functions of the seed, the graph and the position, so a corner found equals a fresh evaluation bit for bit and load order cannot matter. Benches and `HashLoadOrders` call `ResetCaches` before each pass, so every check starts cold. `FVoxelDensityTerrainSettings::bShareCorners` turns it off (only to compare against). Tests: `Density.SharedCorners`, `Density.LoadOrder`.

`Terrain/VoxelDensityTerrain.h`: `FVoxelDensityTerrain` (`IVoxelTerrain`).

- **Axes:** reference x = our X, z = our Y, y = our Z.
- **Blocks:** density > 0 is solid. Water fills open space up to Z 62 (sea level 63 - 1). With biomes and surface rules (every generator-2 world since phase 5 session 5) the rules choose every solid block and the freeze pass adds ice and snow ([Surface rules](#surface-rules-temperature-and-snow)). Without them (`bSurfaceRules` off, or no rules) the earlier dressing stands in: bedrock thinning from Z -64 to -60, deepslate mixing into stone from Z 0 to 8, grass on 3 to 5 dirt, sand where the surface is within Z 56..64 and gravel below Z 56.
- **Caves:** on by default (`FVoxelDensityTerrainSettings::bCaves`). Off (`voxel.Bench Density ... nocaves`) gives session 3's terrain exactly (`RemoveCaves`, water to Z 62), and its goldens stay. `RemoveCaves` makes range choices on `*/sloped_cheese` read it alone, and `*/caves/noodle` a neutral constant 64. Without surface rules, with caves only the first surface from the sky is dressed (grass/sand/gravel); cave floors stay stone or deepslate.
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

Phase 5 session 3 (corner sharing, batch blended noise), `voxel.Bench Density 8 <preset>`, caves on, 289 columns, one thread, mean ms per column over two editor runs. Sharing on and off ran in the same editor run. Baseline: the same bench at `5168c69`, two runs on the same day.

| Preset | Baseline | Sharing on: row order, warm neighbours (best case) | Sharing off | Corners shared |
|---|---|---|---|---|
| overworld | 4.42 / 4.35 | 3.79 / 3.76 | 4.37 / 4.13 | 198 of 582 per column (34%) |
| large_biomes | 4.39 / 5.40 | 3.92 / 4.64 | 4.58 / 4.56 | 247 of 728 (34%) |
| amplified | 5.30 / 5.62 | 4.28 / 4.64 | 4.96 / 5.24 | 357 of 1,049 (34%) |

- **In game, expect something between sharing on and off:** streaming loads columns ring by ring, so a new column often has one neighbour done, sometimes none.
- **Corners stage,** sharing on against off in the same run: overworld -0.52 / -0.48 ms, large_biomes -0.69 / -0.51, amplified -0.80 / -0.78.
- **Across 30 workers** (index order, sharing on): 0.247 / 0.232 ms per column (baseline 0.268), large_biomes 0.243 / 0.243 (0.282), amplified 0.250 / 0.247 (0.298).
- **Spread:** runs differ a lot on this machine (large_biomes' baseline: 4.39 and 5.40), so the comparison that counts is sharing on against off within one run. One pass broke that: large_biomes run 2's sharing-on mean (4.64) is above its sharing-off mean (4.56), from outliers (p95 9.24), though its p50 (3.74 against 4.07) and its corners stage (-0.51 ms) are lower.
- **A third run, after the review's fixes** (a busier machine): sharing on 3.93 / 4.25 / 4.55, off 4.60 / 4.99 / 5.61 (overworld / large_biomes / amplified), with every gate unchanged.
- **A fourth run, on the committed code** (a quiet machine: p95 within 1 ms of p50): sharing on 3.15 / 3.45 / 3.59, off 3.62 / 4.07 / 4.33.
- **Exact:** 0 of 28,409,856 blocks differ with skipping on and off, and with sharing on and off, every preset. `voxel.Bench Columns 8` hashes `0x7bbf1f42c5434443` in both orders, as before. Goldens are unchanged.
- **Batch blended noise:** 863 / 865 / 847 -> 781 / 848 / 810 ns per point, while the unchanged normal noise moved by up to 4%, so no gain is credited.
- **Batched normal noise (phase 5 session 6):** `noise`, `shift_a` and `shift_b` sample their whole batch octave by octave (`FVoxelNormalNoise::SampleBatch`). `FVoxelGradientNoise::AccumulateOctave`, which `old_blended_noise` uses too, fetches a lattice cell's eight gradients once while consecutive points stay in that cell, as most corners do at low frequencies. Each point keeps Sample's arithmetic and octave order, so the values are bit-identical. The corners stage fell from 1.21 / 1.43 / 1.91 to 0.90 / 1.13 / 1.38 ms (overworld / large_biomes / AMPLIFIED, quiet machine). Goldens and the radius-8 hashes are unchanged.
- **Profile before the levers** (a temporary per-op timer; ms per column): corners overworld 1.62 = normal noise 1.03 + `old_blended_noise` 0.50 + the rest 0.09; large_biomes 2.07 = 1.33 + 0.63; amplified 2.34 = 1.34 + 0.89. The costliest corner noises are `cave_cheese`, `cave_entrance`, `spaghetti_3d_rarity` and `pillar`, then the spaghetti and noodle noises.
- **Speed-up session S0 (phase 6, October 9).** Most of the column's time outside the noise went to walking dense buffers with a 256-element stride, and to TArray range checks, which Development builds keep. Three exact levers, each gated on its own:
  - **Block fill:**
    - `FillWithRules` builds the block kinds layer by layer, through raw pointers.
    - `FVoxelSection::Encode` packs a whole 64-bit word at a time, per width.
    - `FVoxelColumn::Build` starts its heightmap scans at the highest section that is not all air.
    - `Generate` no longer clears the dense buffer, which both fills overwrite.
  - **Aquifer fill:** layer by layer from the top, with a sky flag per block column, and the per-y values hoisted (global fluid, uniform layer).
  - **Cells to blocks:**
    - certain cells are written by rows, and a layer of one certain state as one run;
    - the trilinear steps are shared across a cell, as the same expressions on the same values;
    - the results go back by rows.
  - **Exact:**
    - goldens unchanged; radius-8 hashes as before in both load orders;
    - 0 blocks differ in the bench passes;
    - an old-against-new `Build` comparison: 0 of 1,944 sections (palette, words, NonAir, allocation) and 0 of 82,944 heightmap values per preset;
    - `Density.Column` checks blocks bit for bit against direct evaluation.
  - **Figures:** the old and new `UnnamedVoxel` DLLs alternated run by run (three pairs, 18:10 to 18:13, no `java`, `javaw` or `UnrealEditor`, the Modrinth launcher open; the machine ran about 15% slower than at 17:42). Column-mean medians of three, old code -> S0: overworld 5.25 -> 3.29 ms (-37%), large_biomes 5.70 -> 3.47 (-39%), AMPLIFIED 6.30 -> 4.45 (-29%). Evidence: `Tests/Evidence/2026-10-09/BenchLogs-20261009-s0/`.
  - **AMPLIFIED's stages after S0** (the median run, ms):
    - density 2.31: corners 1.67, bounds 0.13, flat 0.09, cells from corners 0.18, cells to blocks 0.25;
    - aquifer 0.43;
    - biomes 0.20;
    - blocks 1.51, of which surface 1.13.
  - **Finer split** (a temporary per-slot timer in separate runs, its own overhead included, so the figures do not add up to the stages above; AMPLIFIED, before -> after, ms):
    - kinds 0.63 -> 0.19;
    - sections 0.29 -> 0.17;
    - heightmaps 0.09 -> 0.03;
    - aquifer fill 0.62 -> 0.37;
    - cells to blocks 0.50 -> 0.30.
  - **Corner noises:** 1.09 ms of AMPLIFIED's corners: `old_blended_noise` 0.52, the entrances (`cave_entrance`, the 3D spaghetti, roughness) 0.33, the noodle noises 0.13, the underground branch 0.10.

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

### Surface rules, temperature and snow

Phase 5 session 5 replaced the stand-in dressing with 26.3's surface rules (`material_rule/overworld`), run by our own code over our own noise. What each rule and condition does was black-boxed from the jar: [the scout's facts](Records/2026-10-09-Voxel-Phase5-Scout-Facts.md) (sections 1 to 4), with the evidence beside them.

- **Inputs** (`Surface/VoxelSurfaceContext.h`), per block column:
  - the surface depth, `trunc(surface * 2.75 + 3 + r * 0.25)` with `r` from `Hash64(seed, "surface/depth", x, y)`;
  - the preliminary surface P: the preset's `preliminary_surface_level` (`find_top_surface`) at the generated column's four corners, lerped across it as the jar does. The minimum surface level is `floor(P) + depth - 8`;
  - steep: 26.3's chunk-local test (the height 1 block south minus 1 north is 4 or more, or 1 west minus 1 east). Neighbours clamp to the column, so no column reads another and load order cannot matter;
  - per block, from one top-down pass: the stone depth above (air resets it; fluid neither counts nor resets it), below (to the next air or fluid), and the water height (set at the first fluid after the last air, lava included).
- **Evaluator** (`Surface/VoxelSurfaceEvaluator.h`): the tree compiles into a linear program (overworld: 261 instructions). A failed condition jumps past its rule, the first block given ends the run, and inversions fold into the tests.
  - Ids resolve once: 32 blocks, biome sets as bit masks, 11 noises (`FVoxelNormalNoise`, seeded like the density graph's).
  - Each solid block takes the biome its own position gives (`FVoxelBiomeStorage::GetBlock`, the jittered per-block lookup).
  - Ore veins: since phase 6's S4 ([Ore veins](#ore-veins-phase-6-s4)).
- **Our own parts:**
  - the vertical gradients: `Hash64(seed, random_name, x, y, z)`; bedrock from Z -64 to -59, deepslate from 0 to 8;
  - the depth jitter;
  - the badlands bands: a 192-block pattern from a seeded stream, offset by `round(clay_bands_offset * 4)`. About half is plain terracotta, with orange singles every 4 to 8 blocks, yellow, brown and red runs, and white beside light gray. Badlands look like 26.3's but do not match them block for block.
- **The exact shortcut** (`FVoxelDensityTerrainSettings::bSurfaceShortcut`): per generated column, the tree is decided once per height where the height and the biomes of the quart layers are enough.
  - Below a column's minimum surface level, the surface branch cannot run (the deep table). Rows it cannot decide alone, the gradients, resume the tree where the table stopped, and rows that give nothing are not visited.
  - Some blocks lie deeper in their run than every stone_depth condition the column's biomes can reach. They take the interior table, which works out each instruction's outcome backwards; an undecided test whose two ways end alike counts as decided.
  - Biome tests read the layer masks first, and corner jitters are cached per column.
  - Off, every solid block runs the whole tree and looks its biome up directly. Both give the same blocks (the bench and `Surface.Exact`).
- **Temperature** (`Biome/VoxelBiomeClimate.h`):
  - the biome's temperature, with the frozen oceans' modifier (0.2 in open-water patches, from two seedless noises);
  - above sea level + 17 (Z 80), minus `(Z - 80 + wobble) * 0.05 / 40`, in float as the jar computes it. The wobble is a seedless noise of up to 8 blocks;
  - cold enough to snow: below 0.15 (strictly);
  - the climate noises take no world seed, as in 26.3.
- **The freeze pass** (`Surface/VoxelFreezePass.h`) runs after the rules. Per block column, the top is the position above the highest block that blocks movement or holds fluid. With the biome there:
  - still water just below the top turns to ice when cold;
  - a cold top takes one snow layer, if the block below can hold it (a full top face; not ice, packed ice, powder snow or barriers). Grass, podzol and mycelium below turn snowy (`FVoxelBlockTable::GetSnowyState`);
  - block light is taken as 0;
  - when trees arrive, run it after them.
- **Not built (decided):** eroded-badlands hoodoos and frozen-ocean icebergs, which 26.3 makes in code rather than data.
- **Single Biome:** world.json's `biome` becomes the fixed source (`FVoxelDensityTerrainSettings::FixedBiome`). An id outside the overworld list refuses the world and names the id; world.json is not changed. The Create screen also offers other dimensions' biomes, such as `minecraft:the_void`.
- **Off:** `bSurfaceRules = false` keeps the earlier dressing, for comparisons.

| Measured (Development editor, seed "Unnamed", Minecraft open; `voxel.Bench Density 4`, 81 columns, one thread, same run) | overworld | large_biomes | amplified |
|---|---|---|---|
| Whole column, rules on: mean / p50 ms | 5.16 / 4.38 | 5.20 / 4.54 | 6.33 / 5.39 |
| Whole column, rules off | 4.41 / 3.85 | 4.66 / 3.90 | 4.86 / 4.29 |
| Surface stage (inside it): mean / p50 / p95 | 0.76 / 0.61 / 1.70 | 0.82 / 0.63 / 1.70 | 1.04 / 0.89 / 1.97 |
| Surface stage with the shortcut off, mean | 7.35 | 9.21 | 7.77 |
| Solid blocks per column: deep table / interior table / whole tree | 14,525 / 3,730 / 3,780 | 14,503 / 4,808 / 3,736 | 14,063 / 15,020 / 3,922 |
| Blocks the rules change, of 7,962,624 | 147,926 | 106,000 | 135,887 |

- **Of the stage:** the preliminary surface takes about 0.03 ms and the freeze pass about 0.06 ms. A block the whole tree decides runs 7 to 9 of the 261 instructions.
- **Against the 5 ms limit:** with Minecraft open, every preset's mean was over it with the rules on and under it with them off (4.41 to 4.86). The passes run one after another, so the whole-column difference (0.75, 0.54 and 1.47 ms) and the stage's own mean disagree by up to 0.4 ms: run-to-run noise on a busy machine.
- **Session 6, quiet machine:** before any lever, 4.91 / 5.03 / 6.15 ms (overworld / large_biomes / AMPLIFIED). With the batched normal noise ([Column density](#column-density-and-terrain-generator-2)), the median of three runs is 4.50 / 4.88 / 5.32 ms: overworld and Large Biomes under 5 ms, AMPLIFIED over it ([the phase 5 record](Records/2026-10-09-Voxel-Phase5.md)).
- **A per-run memo, tried and reverted:** a block reused the walk of the block above it in its run when every test on that path kept its outcome. Stone depth, height and water outcomes were proven from the depth steps, and biome tests were run again. It was exact (0 differing blocks), but only about 116 of 3,780 tree blocks per column could reuse a walk: 52% of walks end at a stone_depth boundary and 39% pass more biome tests than it kept. A ceiling near 0.1 ms per column did not pay for the code.
- **Across 30 workers** (`voxel.Bench Columns 8`, 289 columns): 0.27, 0.27 and 0.29 ms per column.
- **Gates** (radius 4, all three presets): 0 of 7,962,624 blocks differ with the shortcut on and off, with biomes on and off (rules off), with sharing on and off, and with skipping on and off.
- **Radius-8 block hashes,** equal in both load orders: overworld `0xeb1c3d76838e2481`, large_biomes `0xcb42685280f8b57c`, amplified `0xc4403ff66465fcf0`. Biome hashes are unchanged from session 4.
- **Mutation checks:** snow at or below 0.15 instead of below fails `Surface.Snow`; steep's two directions swapped fail `Surface.Conditions`.

### Ore veins (phase 6, S4)

26.3's overworld tree runs two `ore_vein` rules right after the bedrock floor: copper (copper ore, raw copper blocks, granite) and iron (deepslate iron ore, raw iron blocks, tuff). Since S4 they run through our own evaluator (`Surface/VoxelOreVeins.h`, `FVoxelOreVeins`), on by default (`FVoxelDensityTerrainSettings::bOreVeins`, console `voxel.OreVeins`, `noveins` on the benches).

- **The rule** (black-boxed against `facts/ore_vein_samples.json`, test `Surface.OreVein`; the jar's code was not read): where the vein's `density` function is above 0, the rule gives a block with the density's value as the chance. The data's value there is 0.7; the code reads the value, not the facts' `VEIN_SOLIDNESS` constant, which is the same 0.7. The block is ore with the `richness` function as the chance (0.1 to 0.3), unless the `filler_gap` function is above 0, and the filler otherwise. Ore is raw ore with `raw_ore_chance` (0.02). Elsewhere the rule gives nothing and the tree runs on.
- **What the samples pinned** (5,575 positions; every vein block of the probe's chunks and one in 64 of the rest): no vein block where its density is 0 or below; no ore where the gap is above 0 (the highest gap at an ore block is -0.0013); ore makes 22.2% and 22.9% of the vein blocks with gap at or below 0 against mean richnesses of 21.9% and 22.4%; raw ore 14 of 625; 68.7% of the positions with density above 0 hold a vein block.
- **The functions** (`overworld/ore_vein/*`) run through the density evaluator like final_density's, over our own noise: `toggle` (interpolated `ore_veininess`), `mask` (interpolated `ore_vein_a` and `_b`), the two densities (y ranges, the edge roundoff), `richness` and `gap` (`ore_gap`). Fed each sample's toggle and mask, our densities and richness equal the jar's within 2.4e-7.
- **Our own part:** the three draws come from one stream seeded by `Hash64(seed, "ore_vein", x, y, z)`, the same for both veins at a block. So veins sit where our noise puts them and match the jar's in shape and rate, not block for block.
- **Rate, like for like** (`voxel.Bench VeinProbe`, the probe's chunk choice and sampling on our noise, 32 seeds): positions with density above 0 per seed's two chunks, copper median 207 (the jar's seeds: 802, 220, 199), iron median 336 (208, 2,300, 340); fill 70.3% (68.7%); `ore_gap`'s noise sd 0.31 (0.29 to 0.34). Over 1,089 columns (radius 16) the dev seed ("Unnamed") holds 46.3 vein blocks per column (`voxel.Bench Density 16`) and seed 263 34.1 (`Surface.OreVein`); near the dev seed's origin (radius 4) only 9.2. Raw ore is only loosely pinned by the facts (14 of 625 ore blocks).
- **In the evaluator:** an `ore_vein` rule compiles to a `Vein` instruction. The deep and interior tables pass over it and note it per height (`DeepVeins`, `InteriorVeins`): a block there takes the first noted vein that gives it a block, and the table's result otherwise. In the interior table, a vein past an undecided test whose two ways end alike leaves the height to the full tree. With veins off, the instructions pass and the tables are the ones from before S4.
- **The field and its shortcut** (exact): per generated column, before the rules, `FVoxelOreVeins::Generate` works out every vein block.
  - Only the heights where a vein's density bounds allow a positive value are evaluated (from the data: copper 0 to 49, iron -60 to -9).
  - The interpolated inputs are evaluated at the 4 x 8 x 4 cells' corners one node at a time (toggle first). A layer of cells, then each cell of a layer that may hold a vein, is bounded from the corners known so far, and closed when every density is certainly 0 or below.
  - The rest are evaluated per solid block, with `VoxelDensity::Trilinear` as direct evaluation does, and the gap only where the richness draw passes.
  - With `bOreVeinShortcut` off, every solid block of the column is evaluated directly at every height: the oracle. 0 of 107,053,056 blocks differ in a one-off `voxel.Bench Density 16` run (1,089 columns, overworld, dev seed); `voxel.Bench Density` checks it at any radius, and `Surface.OreVein` on its four vein-richest columns.
- **Saved worlds:** veins are a decided change of the generator, so existing saved worlds get them in the terrain they generate (edits are stored on top). `voxel.OreVeins` is a console switch, not part of a world.
- **Gate** (quiet machine, October 10, 03:13 to 03:16; old `015bb42` and new alternated; `voxel.Bench Decorate 4`, medians of 3):

  | Preset | Generate added, p50 | Generate added, mean | Whole column, old -> new (mean) |
  |---|---|---|---|
  | overworld | +0.23 ms | +0.33 ms | 3.66 -> 4.08 ms |
  | large_biomes | +0.27 ms | +0.64 ms | 3.41 -> 4.05 ms |
  | amplified | +0.41 ms | +0.86 ms | 3.80 -> 4.63 ms |

  Amplified is over the 0.3 ms allowance by p50. It is flagged and queued for the speed-up session, as decided by the night manager (03:17). Notes:
  - The means swing with outliers (maxima of 7 to 12 ms).
  - The Density bench reads lower on amplified (+0.17 ms p50), and the gap between the two benches is not explained.
  - The field's column mask (vein lookups only in block columns holding a vein block) is part of the shortcut, so the oracle checks it too.
  - First view: 3.66 and 3.73 s against 3.48 and 3.49 s old.
  - Flight: no holes, 0 frames over 2 ms, memory flat.
- **Hashes:**
  - Veins off reproduces every golden and the radius-8 hashes from before S4: the plain ones above, and the decorated `0x0015e9b54e7fe0eb`, `0x24bcb9ca548e0fcf`, `0x502dbd3b9d554557`.
  - Veins on, both load orders: plain `0xe3e4e339022a4ec0`, `0x92d83639acdee88f`, `0x615321e7b313fed6`; decorated `0x80485dafc2e934f4`, `0xf0684ae48c1cd603`, `0xc01314216b7aa817`.
- **Switches:** `voxel.OreVeins 0` rebuilds the world without veins, the control. Benches take `noveins`. `voxel.OreHistogram` counts the veins' blocks on their own ("vein" columns: generated with veins minus without).

### Ore looks (phase 6, look calls L1 and L2)

Scout E's two render-only looks for ores ([the record](Records/2026-10-10-Voxel-Phase6-OreLooks.md)). Each is a console switch whose default, 0, is vanilla 26.3; with both at 0 every vertex is the vanilla mesh (`Unnamed.Voxel.Mesher.Digest` keeps its golden). Blocks, columns, saves, goldens and hashes never change.

- **L1, ores wear their host rock** (`voxel.Look.OreHost`; accepted by Nyx, delegated, at 04:22 on October 10, default 0): 1 draws a stone or deepslate ore in granite, diorite, andesite or tuff as its ore pixels over that rock's own texture (the composite); 2 is the palette swap (the base rock's tones mapped by brightness onto the host's), kept for comparison.
  - The host (`VoxelOreLooks::PickHost`) is the rock class most common among the ore's 6 face neighbours or, when none is a rock, its 20 edge and corner neighbours, all inside the padded section. A tie, no rock, or stone or deepslate winning keeps vanilla.
  - Known issue: on light hosts (tuff, diorite) the ore pixels lose contrast; iron in tuff reads worse than vanilla's dark square.
- **L2 level 1, ore-aware mips and gloss** (`voxel.Look.OreGlint 1`; accepted by Nyx, delegated, at 04:36, default 0): the ore textures' mips keep the ore pixels' share and colour, so coal, iron and copper still read as specks far away; metal pixels get roughness 0.4 and specular 0.6, gem pixels 0.2 and 0.9, so they catch light (no light, no glint). Level 2 (a faint glow on gems) is not built. `voxel.Look.OreGlint 3` is a debug view: every ore face flat magenta.
- **Textures:** `Scripts/OreLooks.py` (Pillow, from the 26.3 block reference only) writes `Art/OreLooks/T_OreLooks.png` and `manifest.json`: 320 slots, (mode x 5 + host) x 16 + ore x 2 + base, for the 8 ores on stone and deepslate, 5 hosts (the base, granite, diorite, andesite, tuff) and 4 modes (composite, swap, each with 26.3's mips or ore-aware ones). Ore pixels are those outside the base rock's palette (stone's 4 greys, deepslate's 5). `Scripts/Editor/ImportOreLooks.py` builds `/Game/Blocks/T_OreLooks` (the block array's settings; ore-aware mips through `BuildTextureArray`'s `"explicit"` strategy) and verifies an existing one, never rebuilding it.
- **Mesher and material:** block classes `FVoxelStateInfo::Ore` and `Rock` (`VoxelOreLooks::Classify`, from `VoxelBlocks::MakeTable`). With OreHost on, the host joins the greedy-merge key above the tint. An ore face's look word takes its overlay code's place: bit 14 without the overlay's bit 15, the slot in bits 0 to 9, the level in bits 10 and 11. Words stay below 2^15, because the GPU interpolates the channel as a float, and near 2^23 it is off by one. `M_ChunkOpaque`'s `CHUNK_OPAQUE_HLSL` samples `T_OreLooks` (`OreLookTextures`) for those faces and drives roughness, specular and the debug emission from custom outputs; the other materials are unchanged.
- **Switching:** a change remeshes every loaded column (`FVoxelColumnPipeline::SetOreLooks`); captures take them in `-ExecCmds`, and `voxel.Bench Columns` reads them.
- **Cost** (quiet machine, October 10; old and new alternated): GPU frame per shot +0.00 ms at 0 and +0.00 to +0.07 ms with both looks on; quads per column +0.04% with OreHost on (merges stop at host changes); mesh per column: at 0 inside the old build's own spread (one-thread area pass, mean per section: 0.091 ms against 0.092 to 0.109; per column 2.240 ms against 1.984 to 2.314), both looks on +0.03 ms per column against old's median (+1.4%, within noise).

### Carvers (phase 6, S10)

`Terrain/VoxelCarvers.h`: `FVoxelCarvers`, 26.3's three carvers (`cave`, `cave_extra_underground`, `canyon`) as a pull pass in Generate. **Off by default** (`FVoxelDensityTerrainSettings::bCarvers`, console `voxel.Carvers`; `carvers` and `nocarvers` on the Columns and Density benches). Off gives the blocks from before S10 exactly. S11 turns them on, with the re-records.

- **Data:** the reader's carver entries compile into `FVoxelCarverConfig`. `start_vertical_radius_multiplier` is missing from the 26.3 files, so it defaults to 1, the codec's default. The carvers come from the biome lists. All 56 overworld biomes list the same three, so the per-biome rule cannot change a block, and `Initialize` fails if the lists ever differ (`Carvers.BiomeRule`).
- **Starts and reach:** a column starts a carver when `Hash64(seed, "carver/<id>", column) < probability`. A column carves the parts of every path that starts within 8 columns (2 x the jar's `getRange()`). A step that would reach farther is dropped; in the tests no step ever is.
- **Paths:** a path is a pure function of the seed, the carver and the start. It comes from our own walker, shaped after the wiki; vanilla's random is not reproduced.
  - Caves: one system per `count` draw (very_biased_to_bottom 0 to 14). A system is a room (1 in 4) with 1 to 4 trunks, or one trunk. A trunk runs 85 to 112 steps, with a radius of 1.5 + sin x thickness, times the system's multipliers. One step in four carves nothing (the flicker). A trunk thicker than 1 splits into two T branches in its middle half.
  - Canyons: one per start, 84 to 112 steps. The radius is (2 + sin x thickness) x the factor, the vertical radius is y_scale x that, and the width has ledges every few heights (width_smoothness).
  - The free numbers were fitted to scout K's black-box statistics in four rounds (`C:\dev\sandbox\phase6s10\walkfit.py` mirrors the walk). Ours against the jar's, per start:

| | cave | cave_extra_underground | canyon |
|---|---|---|---|
| zero-cell share | 0.429 / 0.418 | 0.421 / 0.460 | 0 / 0 |
| mean cells | 4,933 / 5,134 | 5,221 / 4,885 | 10,291 / 11,038 |
| farthest chunk | 6 / 6 | 6 / 6 | 7 / 7 |
| y range | -63..217 / -63..224 | -63..95 / -63..104 | -8..90 / -13..89 |

  - Marked cells per column: 1,242 against about 1,226.
  - Known miss: the canyon top (90 against the jar's sample maximum 89), and canyon y spans a little short (26.4 against 29.2). S11 tunes.
- **Cells:** scout K's ellipsoid rule (`Voxelize`). A block column is in when (dx^2 + dy^2) < 1 at its centre. Z runs from floor(z - v) to floor(z + v) + 1, never below -63, and the skip test reads (Z - 0.5 - z) / v. Caves keep the unit sphere above the floor level; canyons keep the width-scaled ellipsoid. Five jar cases plus a tie case (confirmed on the jar on October 10) are checked with the skip test off, and with it on from the jar's own recorded skip arguments.
- **Cache:** `FVoxelCarvePathCache`, 4,096 direct-mapped slots with a lock each. It holds immutable paths indexed by the columns they touch, and a newer key takes an older one's slot. Results never depend on it: `Carvers.CacheOff` compares with no cache and with a 16-slot cache, and the bench compares cache on and off.
- **The pass:** `FillWithRules`, after the surface rules (the wiki: carvers follow the surface) and before the freeze. A marked block that is not air, water, lava or bedrock takes what the aquifer gives an open block there (`FVoxelAquifer::FillCarved`; S10-Q1 (a), the manager's call under Jesus's away rule). FillCarved applies Fill's rules to single blocks on Prepare's statuses, with the sky taken from before carving; a barrier stays solid. `Carvers.AquiferAgrees` checks FillCarved against Fill on every open block of 12 columns (784,636 blocks, 0 differ). `Heights` come down past carved air for the freeze. Carving runs only with caves, the rules and the decoration data.
- **Fluids:** 64 columns spread over the world, seed 263:
  - Carved blocks are 71.8% air, 25.8% water and 2.5% lava.
  - The noise caves' open blocks in the same columns are 78.3% air, 20.6% water and 1.1% lava.
  - So carved tunnels hold what the noise caves hold. Scout K's vanilla diff (94% / 0.6% / 5%) differs because our aquifer floods more than vanilla's; that is a question for S11.
- **Cost:** see the S10 record.
- **Not built:** sand, sandstone, snow, ice and terracotta are carved, which is unverified against vanilla. Vanilla may re-dress dirt exposed under carved grass; that's an S11 open item. Fluids stay static, as in the noise caves.
### Biome tints

Phase 5 session 6 colours grass, foliage and water by biome, as 26.3's `BlockColors` and biome blend do ([the scout's facts](Records/2026-10-09-Voxel-Phase5-Scout-Facts.md), section 5).

- **Colours per biome** (`Biome/VoxelBiomeTints.h`, `FVoxelTintTable`), built from the biome table (dry foliage since phase 6 session 6):
  - water: the biome's `water_color`;
  - grass: `grass_color`, or the block reference's grass colormap at the biome's own temperature and downfall, each clamped to [0, 1] (`dd = d * t`, pixel `(trunc((1 - t) * 255), trunc((1 - dd) * 255))`). Then the modifier: dark forest blends halfway toward `#28340A`; swamp gives `#4C763C` where the info noise at `(x, y) * 0.0225` is below -0.1, else `#6A7039`;
  - foliage: `foliage_color`, or the foliage colormap;
  - dry foliage (leaf litter): `dry_foliage_color` (dark forest, swamp, mangrove swamp, pale garden and dappled forest set one), or the dry foliage colormap, read like the others; plains is `#A37546`.
  - Height, the snow line and the frozen modifier play no part. The swamp pattern uses our own seedless info noise, so it does not match the jar block for block.
- **Which blocks** (`FVoxelStateInfo::TintSource`): the registry keeps each tint as its plains colour, so `VoxelBlocks.cpp` reads grass `#91BD59` as the grass source (the grass block) and foliage `#77AB2F` as the foliage source (oak, jungle, acacia, dark oak and mangrove leaves). Other tints stay constant (spruce `#619961`, birch `#80A755`), and water takes the water colour.
- **The colormaps:** `ReadInputs` reads `grass.png`, `foliage.png` and `dry_foliage.png` from `Reference/minecraft-blocks-26.3/assets/minecraft/textures/colormap/` into the biome table (`VoxelTerrainFactory::GetColormapRoot`). `Unnamed.Build.cs` stages the three files with packages, and a world whose colormaps cannot be read is refused like one whose rules cannot.
- **In the mesher:** a face's tint is the per-channel integer mean of its source's colour over the (2r + 1)^2 blocks around its block, at the block's height. That is 26.3's biome blend; r is `voxel.TintBlend` (2, vanilla's default).
  - Each block takes its own jittered biome (`FVoxelBiomeStorage::GetBlock`, the lookup the rules use) from whichever of the 3 x 3 columns holds it, with each column's corner jitters cached per section. A face's colour does not depend on which section meshes it.
  - A block in a missing column (a finite area's edge, or the edge-walls view of the shots) takes the biome of the meshed column's nearest block, so the edge shows no stray plains colour. Blocks without biomes keep their plains tint (Superflat, the stand-in). The streamed world meshes a column only once its neighbours exist.
  - The colour is the same at a face's four vertices (UV 2's packed tint, as before), so the material is unchanged. The greedy-merge key includes the tint, so a merged quad never spans two colours.
  - When every quart corner that the blend's block lookups can reach holds one biome (and the colour does not vary with position, as swamp grass does), the face takes that biome's colour without averaging. The result is the same: the bench gate counts 0 differing vertex tints with this shortcut on and off.
- **Switches:** `voxel.TintBlend 0` gives each block its own biome's colour (hard, ragged edges); `-1` gives every block its plains tint. A change remeshes every loaded column (`FVoxelColumnPipeline::SetTintBlend`), so it works from the console and from a capture's `-ExecCmds`.

| Measured (Development editor, seed "Unnamed", quiet machine; `voxel.Bench Columns 4`, 81 columns meshed again on one thread, same run) | Quads per column | Mesh ms per column |
|---|---|---|
| Tints off (plains colours) | 1,462.2 | 2.162 / 2.024 |
| Blend 0 | 1,463.9 (+0.1%) | 2.286 (+5.7%) / 1.846 (-8.8%) |
| Blend 2 | 1,476.4 (+1.0%) | 2.236 (+3.5%) / 1.912 (-5.6%) |

- Two runs. The quads rise by 1%: merges stop where the colour changes. The mesh times lie within run-to-run noise of each other. Before the jitter cache, blend 2 cost +19%.
- **Looks** (phase 5 session 6, decided by the day manager under Jesus's delegation): blend 2, against blend 0's hard per-block edges. Evidence: `Tests/Evidence/2026-10-09/TintScenes-20261009-s6c/montage-blend2-vs-0.png`; the oceans and before/after montages in `TintScenes-20261009-s6/`. That folder's `*-blend0` and `*-blend-1` shots are invalid: the variable was not live yet, so they show blend 2.
- Gates: 0 of 474,288 (blend 0) and 0 of 478,352 (blend 2) vertex tints differ with the shortcut on and off. Tints change no block: block and biome hashes, goldens and the load-order hashes are unchanged.

## Decoration

Phase 6 builds 26.3's decoration from the data in [Worldgen data](#worldgen-data). S2 built the core, proven with ores; it runs on the fixed area (`FVoxelDecoratedArea`), in the benches and gates, and, since S3, in streaming ([Streaming](#streaming); [the phase 6 plan](Records/2026-10-09-Voxel-Phase6-Plan.md), 3.1 and 3.2; [scout C's pipeline research](Records/2026-10-09-Voxel-Phase6-Scout-Pipeline.md)).

| Stage | Reads | Produces |
|---|---|---|
| Generate | nothing (a pure function, as before) | the snapshot: blocks, biomes, heightmaps; without the freeze pass (S3) |
| Decorate | the 3x3 snapshots, plus its own writes so far | a write log in 9 buckets, one per target column of its 3x3 |
| Finalize | its snapshot, plus the 9 buckets aimed at it | the final column: the merge, then the freeze pass (S3), then the player's edits (streaming) |

| Piece | Where | How it works |
|---|---|---|
| Decorator | `Decoration/VoxelDecorator.h` | `FVoxelDecorator` compiles the graph for a terrain's biomes (matched by id): the global order per step (`FVoxelFeatureOrder`), one bit set of features per biome, the ore features with their target lists. A column runs every placed feature some biome of its 3x3 lists (its own quarts, 64-bit masks); the `biome` modifier then checks each position's own biome. Each feature's stream starts from `VoxelFeatureSeeds::GetStreamSeed` (Jesus's answer 4), and each try of a `count` takes its own stream from (that stream, try index), so a skipped or failed try never moves the next. `Decorate` and `Finalize` are pure functions of (seed, data, columns), so S3 can recompute either on a miss (scout C, recommendation 1). `DecorateAlone` is the oracle (design B): a column's 5x5 generated, its 3x3 decorated, then finalized. Placed features of types not built yet are skipped and counted (`Describe`): 30 compiled (29 ore placements and `ore_infested`), 141 skipped; since S8, 104 compile and 67 are skipped ([Plants and patches](#plants-and-patches-phase-6-s8)). |
| Working copy | `Decoration/VoxelDecorationContext.h` | The 48 x 48 window over the 3x3 snapshots with this Decorate's own writes in an open-addressing table, and a per-cell written bitmap (the worker thread's, 110 KB, cleared by the context) so other reads skip the table. A feature sees the column's earlier features wherever they landed and never a neighbour's decoration. `GetTopZ` bounds each column's highest non-air block, raised by writes. |
| Write log and merge | `Decoration/VoxelWriteLog.h` | `FVoxelWrite` is 8 bytes: the cell (17 bits), the mode (`Force`, `IfReplaceable`, `IfTargets`), a support condition slot (`EVoxelWriteSupport`, empty until S8: checked at replay against the merged column, so plants cannot float over a neighbour's lake air; scout C, recommendation 7), the state and the mode's argument. A Decorate emits each bucket already in (step, order index, sequence) order, as runs keyed step << 8 \| index; `VoxelWriteMerge::Merge` replays the 9 buckets aimed at a column as a 9-way merge keyed (step, order index, source offset dz then dx), not a sort (scout C, recommendation 2). A write that falls outside the 3x3 is dropped and counted. |
| Finalize | `FVoxelDecorator::Finalize` | Copies the snapshot, expands each section a write reaches once, replays the merge (`IfTargets`: the first target whose rule test matches the merged block decides the state; none, no write), packs the sections again and updates only the heightmap cells whose classes a write changed. |
| Rule tests | `Decoration/VoxelRuleTests.h` | The seven rule tests the ores use (always_true, block_match, tag_match, height_match, not, all_of, any_of), compiled to bands of Z, each band a bit set over blocks: one lookup per test. `FVoxelBlockTags` expands the graph's tags against the block table (`FVoxelBlockTable::FindBlock`). |
| Providers | `Decoration/VoxelNumberProviders.h` | `FVoxelIntProvider`: constant, uniform and trapezoid (two uniform parts; a triangle at plateau 0); height providers resolve their anchors (absolute, above_bottom, below_top) to Z and use the same type. All 37 uniform and 10 trapezoid int histograms and all 24 uniform and 9 trapezoid height histograms of `facts/provider_histograms.json` agree (`Providers.Distribution`). |
| Modifiers | `FVoxelDecorator::RunModifiers` | `count`, `in_square`, `height_range`, `rarity_filter`, `biome`, each passing its position a new seed. |
| Ore feature | `Decoration/Features/VoxelOreFeature.h` | Our own blob, below. `Place` takes, per candidate cell, the first target whose rule test matches the block there, and drops a candidate with an air face neighbour with the discard chance. |
| Area | `Generation/VoxelArea.h` | `FVoxelDecoratedArea::Build`: Generate on radius + 2 (the decorator's snapshot), Decorate on radius + 1, Finalize and the finishing pass on the radius, each on one thread or every worker, in row order or reversed. `FVoxelArea::Build` takes a decorator. |
| Freeze at Finalize (S3) | `IVoxelTerrain::GenerateSnapshot`, `FinishColumn`; `FVoxelFreezePass::ApplyToColumn` | The snapshot is Generate without the freeze pass; `FinishColumn` runs the pass on the merged column (sections it changes expanded once, heightmaps of changed cells updated), so snow lands on every feature, a neighbour's included, and vanilla's bare border under leaves (MC-119599) cannot happen (plan 3.4). `FinishColumn(GenerateSnapshot)` equals `Generate` (`Freeze.Moved`). `FVoxelDecorator::GenerateSnapshot` and `FinishColumn` follow `voxel.Decoration.FreezeLast` (default 1; 0 is S2's order, the freeze inside the snapshot). |

- **The ore blob.** Its shape: a horizontal segment of half-length size / 8 through the origin block's lower corner at a random angle, its ends 0 to 2 blocks below the origin independently; size spheres at t = i / size along it, of radius ((s size / 16)(sin(pi t) + 1) + 1) / 2 with s uniform; every cell whose centre lies inside a sphere is a candidate, once. Spheres inside one of the last 4 marked are skipped (exact).
  - **Provenance:** the shape is the commonly published ore-blob form (overlapping spheres along a random segment). Its constants were fitted to and confirmed by S1b's probes: a stdlib Python prototype (kept in the sandbox, outside the repository) matched all 24 with these first values, needing no tuning, and the C++ port gives the probes' mean cells per blob (6.7, 22.0, 112.3, 681.9 at sizes 9, 17, 33, 64). The fine structure matches too: the even and odd zigzag of the size 9 histogram, the dip at one block for size 4, and size 4's +x lean (0.40 / 0.60 against the probe's 0.39 / 0.61). The jar's code was not read. Jesus has the question whether the shape should instead be derived from the probe statistics alone.
  - **Against the probes** (`Decoration.OreProbes`, the probe's layered column, 2,000 blobs per band): blocks per placement, failure rates (size 3 fails 64% against 60 to 73%), Y offsets, extents, the tuff rule (the stone ore at Z 0 and up, the deepslate ore below) and the air exposure ratio at discard 0, 0.5, 0.7 and 1.0.
- **Scout E's early-out** (exact): an ore try whose blob box lies wholly above the highest block of every column it overlaps (or outside the world) is skipped, at the biome modifier before its lookup, for ores none of whose targets can match air. About 178 of 287 tries per column are skipped at the dev seed; final-column hashes are equal with it on and off (`voxel.Bench Decorate` prints both).
- **Per column** (dev seed, overworld, radius 4): 287 ore tries reach the early-out (178 skipped), 6,850 blocks placed and logged, log 63.5 KB (most 81 KB), working copy 204 KB (the table and the thread's bitmap); Finalize replays 7,004 writes and repacks 8.1 sections. At `ViewRadius` 12, scout C's 120 log-equivalents held in buckets come to about 7.6 MB.
- **Gate (S2, quiet machine: no other UnrealEditor, UnrealBuildTool or java; background apps 0 to 25% CPU; medians of 3 runs of `voxel.Bench Decorate 4`, one thread, alternated with the old build in one window):**

| Preset | Decorate mean (gate 0.6 ms) | Finalize mean (gate 0.2 ms) | Generate, old build / new (Density bench) |
|---|---|---|---|
| overworld | 0.600 | 0.162 | 3.191 / 2.867 |
| large_biomes | **0.760** | 0.211 (p50 0.15) | 3.806 / 3.121 |
| amplified | **0.686** | 0.155 | 3.820 / 4.072 |

  Decorate is not met on large_biomes and amplified; per the night manager's call, no lever beyond the two exact ones of the timebox (the written bitmap and the early-out ahead of the biome lookup). Generate's figures are noisy run to run; the exact section encode (below) speeds it up where the machine was steady.
- **An exact storage lever:** `FVoxelSection::FromStates` stamps each state's palette index in a per-thread table (16-bit generations) instead of searching the palette, and `Expand` decodes whole words; palettes and words come out the same. Finalize fell from 0.43 to 0.15 ms with it.
- **Switch:** `voxel.Decoration` governs streaming (S3) and `voxel.Bench Columns` (the radius-8 gate). At 0 it runs the old code path and the old goldens and radius-8 hashes reproduce exactly; at 1 its area and hashes are decorated: one hash per preset in both load orders (overworld `0x0015e9b54e7fe0eb`, large_biomes `0x24bcb9ca548e0fcf`, amplified `0x502dbd3b9d554557`). `voxel.Bench Decorate`, `voxel.OreHistogram` and the tests always decorate.
- **The freeze move (S3) re-records nothing:** the dev seed's radius-8 areas hold no snow, so the three decorated hashes and `Decoration.Golden` are the same with `FreezeLast` 1 and 0. `voxel.Bench FreezeDiff` builds an area both ways and explains every cell that differs. In the snowiest area found per preset (radius 4, dev seed): overworld at (-80, -96), 9,374 snow layers, 0 cells differ; large_biomes at (80, 0), 7 differ (5 ore or host cells beside a snow layer, 2 in the same blob as one); amplified at (16, -120), 5 differ (all beside a snow layer); none unexplained. The cause: Decorate now sees air where the snow will be (as in vanilla, whose `freeze_top_layer` runs after the ores), so an ore's air-exposure discard can fire, and its random draw shifts the rest of that blob.

### Plants and patches (phase 6, S8)

S8 builds the vegetation that the data's S8 placed features need ([the S8 record](Records/2026-10-10-Voxel-Phase6-S8.md), [the S8b record](Records/2026-10-10-Voxel-Phase6-S8b.md), [scout I](Records/2026-10-10-Voxel-Phase6-Scout-I-Plants-Patches.md)). 105 placed features compile: 30 ores and 75 plants (71 in vegetal_decoration, glow lichen among them since part B, and the 4 disks in underground_ores). 66 are skipped and counted (`Describe`): trees 17, fallen trees 8, the seven later plants (Jesus's question 1, `FVoxelDecorationOptions::bLaterPlants`, off), springs 3, lakes 3, and single types.

| Piece | Where | How it works |
|---|---|---|
| Block properties | `Blocks/VoxelBlockTable.h`, `Voxel/VoxelBlocks.cpp` | `MakeTable` copies each block's properties and first state in the registry's layout (a cartesian product in declaration order, the last property fastest). `WithValue`, `GetValue` and `MakeState` resolve double-plant halves, kelp age, pickles, waterlogging and petals to the registry's state ids. |
| Survival | `Blocks/VoxelPlantSurvival.h` | One rule per state from 26.3's support tags (`supports_vegetation`, `_dry_vegetation`, `_sugar_cane`, `_sugar_cane_adjacently`, `_cactus`, `_lily_pad`, `support_override_cactus_flower`, `cannot_support_kelp`, `_seagrass`, and `small_flowers`). Its kinds: Always, OnTag, SugarCane, Cactus, CactusFlower, UpperHalf, Underwater, Mushroom, SturdyBelow, LilyPad, NotAirBelow. `Survives` is the whole rule, side checks included; `SurvivesOn` is the vertical part, which Finalize re-checks. `GetSturdyFaces` counts full cubes (glass too, leaves not: scout H's measurement), slabs on their flat side, and snow of two layers or more on its bottom only. S7's lane uses the same table. |
| Modifiers | `FVoxelDecorator::RunModifiers` | Adds heightmap, offset, block_predicate_filter, noise_threshold_count, noise_based_count and surface_relative_threshold_filter. surface_water_depth_filter and environment_scan came with their first users, S9a's trees ([Trees](#trees-phase-6-s9a)); random_chance, cuboid and randomly_selected wait for theirs (question 2). |
| Predicates | `Decoration/VoxelBlockPredicates.h` | true, matching_blocks, matching_block_tag, matching_fluids, solid (read as blocks_motion), replaceable, has_sturdy_face, would_survive, inside_world_bounds, all_of, any_of and not, with offsets turned to our axes. A placed feature that reaches another type is skipped. |
| Int providers | `Decoration/VoxelNumberProviders.h` | Adds biased_to_bottom, weighted_list (entries of any compiled provider) and clamped, with exact distributions (`Providers.Distribution`: 101 providers, worst z 2.56). |
| State providers | `Decoration/VoxelStateProviders.h` | simple, weighted, rule_based, randomized_int, noise, noise_threshold and dual_noise, named or inline. rotated and random_block (coral, S12) are not built. |
| Features | `Decoration/Features/VoxelPatchFeatures.h`, `VoxelSelectorFeatures.h` | simple_block (survival checked; a two-cell plant needs its upper cell free and goes in as a pair), block_column, disk, vegetation_patch (floor), multiface_growth (glow lichen, part B), the four selectors (`EVoxelSelectorOp`), sequence and no_op. A selector's nested placed features run from the same position. |
| Live heightmaps | `Decoration/VoxelDecorationContext.h` | `EVoxelDecorationHeightmap` has 26.3's six names. The `_WG` maps are the snapshot's; WORLD_SURFACE, OCEAN_FLOOR, MOTION_BLOCKING and MOTION_BLOCKING_NO_LEAVES rise with this Decorate's writes. Only plants turn this on. |
| Writes and replay | `Decoration/VoxelWriteLog.h`, `FVoxelDecorator::Finalize` | Plants write `IfUnchanged` (the cell must still hold what this Decorate saw), with the support `Plant` (the state's vertical survival against the merged cell below) or `PlantPair` (a lower half lands only with the next write, its upper half). Disks keep `IfReplaceable` over their target set, re-checked at replay. |

- **Ours, where the wiki is silent** (the jar's code was not read):
  - **The noise providers:** the noises are ours (`FVoxelNormalNoise`), seeded by `VoxelFeatureSeeds::GetPatternSeed(world seed, data seed)`, so the flower bands differ per world (Jesus's answer 4). noise maps its value linearly over [-1, 1) to an index into its states.
  - **dual_noise:** the slow noise (at slow_scale) picks the variety, between the data's two bounds. A second slow sample, offset 4,096.5 blocks, picks where in the list the window starts. The fast noise then picks within the window.
  - **vegetation_patch's footprint:** a rectangle of the two radii plus one, corners left out, edge columns kept with extra_edge_column_chance. Each column's floor is the highest replaceable ground under air within vertical_range. The vegetation runs after the whole ground is laid. The "plus one" fits scout I's pale moss probe (69 blocks per origin; farthest 3.6 to 6.4 blocks for radii 3 to 5).
  - **block_column's trim:** every layer's height is drawn first. A shorter free run trims the base layers first with prioritize_tip, else the top.
  - **The selectors' draws:** random_selector draws a float per entry, in order, and takes the first under its chance (else the default). simple_random and random_boolean draw one int; the weighted selector draws one int over the total weight.
  - **disk:** cells with dx² + dy² <= r², column by column (x, then y), each from its top down.
  - **multiface_growth (part B; the local search is the manager's call within Jesus's answer to question 3):** fitted to scout I's room probe, where an origin 4 cells from every wall never placed and one a cell short of the east wall always placed on the cell next to the wall (31 of 50 also on one face-adjacent cell of the same wall). Ours tries the origin, then walks one cell along each of the six directions in a shuffled order, stopping at a cell that is neither air nor a water source (an origin inside stone still walks; the probe only tried air origins); the first cell with an allowed face toward a base block takes the block on that face (its faces in a shuffled order), waterlogged in water. Then, with chance_of_spreading (0.5), one of the four cells beside it in the face's plane takes the same face. The data's search_range (20) would have let the probe's centre origin place, so the walk is one cell; it never leaves the 3x3 window (`Decoration.Reach` drops 0). The writes are `IfUnchanged` with no support check: a lichen's support lies beside or above it. `Plants.Multiface` pins the room probe (second blocks 0.505 per origin against the probe's 0.62 of 50); a walk of 4 cells fails it.
- **Merging (decided in S8):** `IfUnchanged` replaces the plan's `IfReplaceable` for plants. Two columns' patches at one cell resolve in merge order (the first wins), and no plant lands on half of another's two-cell plant. The replay re-checks the vertical support only: the cell below lies in the target column, so that check is exact. Sugar cane's water and cactus's free sides are checked at Decorate only.
- **Switch:** `voxel.Decoration.Plants` (default 1). At 0 only the ore features compile, the context gets no block table, and every replay branch S8 added is inert. Today's values then reproduce exactly:
  - `Decoration.Golden`: `0xa28113883ba6e7ca`, `0xf87e27cd1455c61a`, `0xfc7c6279d46e6b76`;
  - `Mesher.Digest`: `0xa8ba8d0d0a39174f`, `0xe49601da349c7c0d`, `0x7f9491318204f4b2`;
  - the six radius-8 hashes.

  Streamed worlds rebuild on a change, and every bench follows the switch (`voxel.Bench Decorate`, `voxel.Bench Columns`, `voxel.BenchModels`).
- **Plants on (recorded under the decided change; part B's glow lichen moved them again):**
  - `Decoration.Golden`: `0xb77656a8d37bde4a`, `0xa290112228773eef`, `0xed3582d140b6c106` (writes 769,195, 774,817 and 829,385; S8 part A had `0x9cce36407bad56b5`, `0x92fd17ac831fcd42`, `0x6cdc112a2c9575f6`).
  - `Mesher.Digest`, models on / off: overworld `0xd8f3b39ed6d66f5a` / `0x6b673e48bda8a0b7`, large_biomes `0xd8c297a251e3c4b1` / `0x81cbb033a8f2c493`, amplified `0x9719bc3f651df81b` / `0xa0fbd505d2c267f9` (glow lichen is a model block, so only models on moved).
  - Radius 8: overworld `0xddb7e949cd5e191a`, large_biomes `0xcbac9722096ef7c5`, amplified `0x61c63a3490017e2e`, equal in both load orders (part A had `0x41fda26c3faa91da`, `0x652c06643974705f`, `0x5454860304d8f56c`).
  - Scout I's oracles (`Plants.PatchCounts`, 2,000 origins on flat ground): short grass 7.835 (analytic 7.79), tall grass 22.228 (22.12), flowers 28.018 (27.83), seagrass 41.029 (41.02).
- **Gate (S8b).** Quiet window on October 10, 07:10 to 07:31: no other editor, java or UnrealBuildTool; 16 idle Python MCP servers; CPU 0 to 26%. Old is `0273eb0`, new is S8 part A; both were built with `-DisableAdaptiveUnity`. Figures are medians of 3 runs of `voxel.Bench Decorate 4`, one bench per editor process (large_biomes: in one process with the others), old and new alternated. The patches figure is Decorate with plants on minus plants off, paired by round; the allowance is 0.4 ms.

| Preset | Patches, p50 / mean | Decorate p50, old to plants on | Finalize p50, old / off / on | Whole column (sum of means), old / off / on |
|---|---|---|---|---|
| overworld | 0.129 / 0.062 | 0.579 to 0.759 | 0.177 / 0.221 / 0.278 | 4.270 / 4.561 / 4.670 |
| large_biomes | 0.094 / 0.140 | 0.532 to 0.631 | 0.187 / 0.225 / 0.256 | 4.088 / 4.256 / 4.789 |
| amplified | 0.192 / 0.172 | 0.638 to 0.846 | 0.208 / 0.375 / 0.259 | **5.594 / 5.392 / 5.499** |

  - **Run-to-run noise** is about 0.2 ms in Decorate and Finalize. Generate's mean swings 3.7 to 4.9 ms on identical code, and that decides the whole column.
  - **The control:** at plants 0 the final-column hash equals the old build's on every preset; at plants 1 it differs.
  - **Amplified (flagged):** every build, old included, read over 5 ms in this window; S4's quiet window had 4.63. By stages S8 adds about 0.28 ms (Decorate mean +0.26, Finalize +0.02), about 4.91 on S4's footing. The manager accepted it with no lever; it joins the speed-up item on Jesus's list (plan 3.5, Q11).
  - **Meshing** (`voxel.BenchModels 4 3`, 2 runs): the decorated area with plants carries 1,328 quads per column (92 of them model quads, +16.6 KB of mesh) against 1,236. Its cost per meshed section is lost in the noise: 0.276 to 0.305 ms with plants, 0.321 to 0.322 without, against S6's 0.28.
  - **The flight** (5 runs: old, new, new with plants 0, old, new): no holes in any. Frames over the 2 ms budget: 0 and 3 (old), 0 and 1 (new), 1 (plants 0). Memory is flat from lap 2 to lap 3 (process within 8 MB; columns 18.85 to 19.53 MB with plants, 18.55 to 19.22 without). Chunk work per circle averages 0.43 ms against 0.37 to 0.38.
- **Evidence (S8b):** biome pairs, plants on against plants 0, each an aimed `view` shot at a `voxel.FindBiome` spot. They cover plains, flower forest, meadow, desert, badlands, jungle, swamp, the lukewarm and warm ocean floors and taiga, plus a column-grid shot of patches crossing borders. Kelp (lukewarm, under water and from above) and sea pickles (warm, from above) were shot at `voxel.FindBlock` spots. They are in `Tests/Evidence/2026-10-10/S8b-Biomes` (sheets `sheet-all-1.png`, `sheet-all-2.png`, `sheet-oceans.png`); the record's section 3 lists them. The look call is "accept, plants stay on" (Nyx, delegated).
- **Tools for the shots:**
  - `voxel.FindBiome <biome> [X Y] [MaxRadius 6000] [preset]` finds up to three spots where a biome covers at least 60% of a 3 x 3 of columns' surface cells, reading the floor under water. It checks the biome on a 32-block lattice, then generates the candidates' columns. It runs headless.
  - `voxel.FindBlock <block> [X Y] [radius in columns 12] [preset]` decorates columns outward, one at a time, until three hold the block (kelp, sea pickles).
  - `voxel.Debug.ColumnGrid 1` draws the column borders around the player as magenta lines on the ground (OCEAN_FLOOR, so under plants and water), with posts at the corners.
  - `CaptureVoxel.ps1 -View CX,CY,CZ,TX,TY,TZ` adds a `view` shot from a camera to a target, in blocks.

### Trees (phase 6, S9a)

S9a builds the tree feature's core ([the S9a record](Records/2026-10-10-Voxel-Phase6-S9a.md), [scout J](Records/2026-10-10-Voxel-Phase6-Scout-J-Trees.md)): trunk and foliage placers, feature sizes, the soil under the trunk and the leaves' distance. 23 tree configurations compile, and the placed features that reach them run in vegetal_decoration with the plants (128 placed features compile on the overworld, 94 in vegetal_decoration). Decorators (beehives, vines, cocoa, leaf litter, alter_ground's podzol), fallen trees, huge mushrooms, bamboo and the rooted azalea come with S9b; until then a tree's decorators are left out and counted (`Describe`).

| Piece | Where | How it works |
|---|---|---|
| The feature | `Decoration/Features/VoxelTreeFeature.h` | `Compile` reads a tree's placers by the data's field names (`FVoxelTreePlacerConfig`: ints and int providers, an untyped `{min_inclusive, max_inclusive}` read as uniform, numbers, a block set), its feature size and its state providers, and checks its static reach (at most 16). `Place`: the trunk height (base_height + uniform(0..height_rand_a) + uniform(0..height_rand_b), the wiki), the feature size, the trunk, the foliage at the trunk's attachments, the leaves' distance, then every cell emitted. `FVoxelTreeBuilder` holds the tree's own cells over the working copy: a log may replace the tree's own leaf, a leaf never a log. |
| Feature size | `VoxelTreeFeature::GetFreeHeight` | Ours, fitted to scout J's ceiling probes: layers 0 to height + 1 above the origin must be free (air or replaceable_by_trees), each a square of its layer's width (two- or three-layer sizes) around every trunk column. Otherwise the trunk clips to the free height minus 2 when that reaches min_clipped_height (fancy oak), else the tree fails. |
| Leaves' distance | `VoxelTreeFeature::GetLeafDistances` | A BFS from the tree's own logs through its own face-adjacent leaves. A leaf farther than 6 is dropped (Jesus's answer to scout J's question 4: nothing decays), and every leaf is persistent=false. |
| Trunk placers | `VoxelTrunkPlacers.cpp` | straight, fancy, forking (acacia), cherry and poplar. The shapes are ours where the wiki is silent, fitted in the sandbox (`C:\dev\sandbox\phase6s9\fit\`, a report per group) and checked again here; each placer's comment gives its rule and draw order. S9b adds dark_oak and mega_jungle (refit, below); giant (with mega_pine foliage) is written but off in `FindType` until its crown fit passes; bending and upwards_branching are not built. |
| Foliage placers | `VoxelFoliagePlacers.cpp` | blob, bush, spruce, pine, fancy, acacia, cherry and poplar. Poplar's radius counts the trunk's cell (the manager's call: the data's radius 8 reaches 7); its foliage stops at the trunk's branch layer (`FVoxelFoliageAttachment::BranchDepth`). |
| Writes | `VoxelDecorator::Decorate` | Logs and leaves `IfReplaceable` over air plus replaceable_by_trees, so the earlier tree in merge order keeps its logs; the soil under the trunk (below_trunk_provider) `IfUnchanged`; leaves waterlogged in water. |
| Placement | `VoxelDecorator.cpp`, `VoxelBlockPredicates.cpp`, `VoxelStateProviders.cpp` | surface_water_depth_filter passes when WORLD_SURFACE minus OCEAN_FLOOR is at most max_water_depth (the wiki's strict "less than" would refuse every tree on land). environment_scan tests the start cell first (ours); a cell walked onto that is not the target must meet allowed_search_condition. A sapling's would_survive: saplings are not in the block reference, so it tests the block below against supports_vegetation (minecraft.wiki, "Sapling"; S8's vegetation rule) until they are. The mangrove propagule is in the table (S7) and keeps its own rule. A rule_based state provider may omit its fallback (`soil_beneath_tree` has none): no rule matching leaves the cell. |
| Selectors | `VoxelDecorator.cpp` | An entry whose tree placer is not built places nothing, as do S9b's fallen trees and huge mushrooms; the selector's other entries still place (the manager's reading of S9's question 2 (a)). `Describe` counts them. |

- **Ours, fitted:** each placer's shape where the wiki is silent, fitted in Python against scout J's probes, then ported and checked again by `Trees.Shapes` against the reference's probes. Acacia's fork length is uniform 1 to 3: the reference probe rejects the fit report's 10:13:16 (p 0.005).
- **Not built yet:** mega spruce and mega pine (S9c: their crown radius rule is unsolved), mangroves (the root placer and upwards_branching) and azalea (bending). A selector entry of one of them places nothing; a placed feature that is one of them alone is skipped.
- **Switch:** `voxel.Decoration.Trees` (default 1, Jesus's answer). At 0 trees are skipped and S8 part B's values reproduce exactly: `Decoration.Golden`'s plants-on case (its second) and the radius-8 hashes `0xddb7e949cd5e191a`, `0xcbac9722096ef7c5`, `0x61c63a3490017e2e`. Streamed worlds rebuild on a change, and every bench follows it.
- **Trees on** (S9a's, re-recorded in S9b where noted):
  - `Decoration.Golden`, its third case: `0x56c797f071194ef7`, `0x3625dcba631991c6`, `0x3634ee586cb5898c` (writes 817,117, 774,870 and 854,341; no S9b tree lands in its area, so unchanged);
  - `Mesher.Digest`, models on / off (S9b: faces merge by look): overworld `0x2d204b0b4cf1abd8` / `0x6ee399181d5fccd7`, large_biomes `0xca80f4d03a73a6f9` / `0x762f1bf60ca6a7e5`, amplified `0x49802047907506c6` / `0x627b48a897480211`. Its trees-off case (plants on) is pinned again since S9b, to S8 part B's values;
  - radius 8 (S9b: dark oak and mega jungle place): overworld `0x194a623a86b97dd6`, large_biomes `0x1b665f4703a79406`, amplified `0xbde96a3deaa9d130`, equal in both load orders.
- **Evidence:** the early photo set, `Tests/Evidence/2026-10-10/S9a-Trees-Early/` (forest, taiga, savanna, cherry grove, a wide view; `contact-sheet.png`).

### Big trees and the streaming cost (phase 6, S9b)

S9b ([the record](Records/2026-10-10-Voxel-Phase6-S9b.md)) made trees cheaper to stream and added three of the five big trees.

| Piece | Where | How it works |
|---|---|---|
| Merging by look | `FVoxelBlockTable::GetLookState`, `FVoxelMeshOptions::bMergeLooks` | Each state's look state is the lowest state of its block equal to it in every field the mesher reads (kept by `SetState`, in separate arrays: `FVoxelStateInfo` keeps its layout). Full-cube faces merge by it, so leaves of every distance merge: leaf quads -22%, all quads -7.8% at radius 8 with trees. Nothing else changes: trees off keeps every digest. |
| Apply scheduling | `UVoxelWorldSubsystem::ApplyReady`, `FVoxelColumnPipeline::SortReady` | See the budget under streaming: the first column may wait one frame, and columns behind the view sector apply as if twice as far. |
| Dark oak, pale oak | `PlaceDarkOak`, `PlaceDarkOakFoliage` | Ours, refit in S9b against scout J's probes (the fit's rules were wrong): branches stand on the ring around the 2x2 at 0.31 each, length 2 to 4, each with its own leaf blob; the top-log layer is a fixed 55-cell shape and the layer below a 36-cell square; the lean starts at h - 1 - M. |
| Mega jungle | `PlaceMegaJungle`, `PlaceJungle` | Ours, refit in S9b: four-step branches from the (1,1) column at `(int)(1.5 + t cos a)`, rising one per two steps; disc canopies r1 and r2, plus r3 one below on a coin drawn in the trunk's stream (`FVoxelFoliageAttachment::bBottomLayer`). |
| Mega spruce and pine | `PlaceGiant`, `PlaceMegaPine` | Written, off in `FindType`: leaves +6.5% and +21%, the crown radius rule by height and parity unsolved (S9c, a refit). |

- **The test's alter_ground rule (the manager's call, S9b):** giant probes show podzol where vanilla's alter_ground turned the trunk's soil; while that decorator is left out, `Trees.Shapes` checks such a tree's soil under the trunk exactly (one per trunk column) and leaves it out of the block set. It goes away when S9c builds alter_ground.
- **The flight with trees** (quiet window, old and new builds alternated, MegaLights on): holes in 40 and 42 frames (at most 5 to 8 columns) against 91 and 107 before; frames over 2 ms 3 and 4 against 19 and 22; trees off none and 1 to 2. The remaining holes are apply throughput (see known gaps).
- **The S9 bench gate** (quiet window; medians of 3, ms per column, trees on against off): Decorate +0.395 overworld, +0.037 large_biomes, +0.156 amplified (the allowance is 0.4: met, overworld at the edge); Finalize with trees 0.363, 0.273 and 0.380, over 0.3 on overworld and amplified, though trees off is already 0.268, 0.328 and 0.370 (about 0.18 in S8b); amplified's whole column rises from 5.04 to 5.36 ms. [The S9b record](Records/2026-10-10-Voxel-Phase6-S9b.md), section 6.
- **Evidence:** `Tests/Evidence/2026-10-10/S9b-BigTrees/` (dark forest, pale garden, jungle, old growth taiga as the gap shot, a trees-off control).

## Storage and meshing

| Piece | Where | How it works |
|---|---|---|
| Block table | `Blocks/VoxelBlockTable.h`, `Voxel/VoxelBlocks.cpp` | One `FVoxelStateInfo` per state id, compiled from the registry on the game thread and shared read-only. It holds the shape (full, bottom or top slab, fluid, snow layers, model), the layer (opaque, cutout, translucent, water, model), the seven face codes, the tint, the light, and three flags: solid, leaves and shades corners. Model states also hold their parts (each a list of alternative baked models, copied from the registry with their quads), the water they hold and the block's offset ([Block models](#block-models)). `OpaqueFaces` marks the faces that hide a neighbour: all six for an opaque cube, only the bottom or top for a slab, none for glass or leaves. Water's faces come from its still and flow textures, as the Playground draws it. `VoxelBlocks.cpp` checks that the core's face order and cull bit match the registry's. |
| Sections | `Storage/VoxelSection.h` | 16^3 states, X fastest, then Y, then Z. One state needs no index array; up to 16 states use 4-bit indices; up to 256 use 8-bit; more store 16-bit state ids directly. Widths divide 64, so no index straddles two words. `Set` widens as the palette fills; `Compact` narrows. Allocations are exact. |
| Columns | `Storage/VoxelColumn.h` | 24 sections plus the four heightmaps (world surface, ocean floor, motion blocking, motion blocking without leaves), stored as Z - MinZ + 1 with 0 for none. Built from a dense column buffer on a worker, then read-only. An edit copies the column, and `SetState` sets one cell and its heightmaps through the helper `Build` shares. A generated column takes about 10 KB; generator 2's also carry their biomes (2.4 KB, [Biomes](#biomes)). |
| Terrains | `Terrain/VoxelTerrain.h` | `IVoxelTerrain`: a column generator the area builder, the pipeline, the spawn search and the bench all take. Every column is a pure function of the terrain's settings and its position. The dense scratch buffer is reused between columns, so a terrain must set every cell (`Dense.Init`), not rely on zeroes. |
| Flat terrain | `Terrain/VoxelFlatTerrain.h` | Superflat: the same stack of layers in every column, laid from Z -64 up and cut at the world's top. The surface is the top solid layer; a fluid above it makes the land not dry. |
| Height terrain (phase 2) | `Terrain/VoxelHeightTerrain.h` | A stand-in until phase 4. A 2D height comes from continent fBm (1/220), damped hills (1/90) and detail (1/24), giving sea level 63 plus up to about 100 blocks. Layers: bedrock thinning from Z -64 to -60; deepslate below 0, blending into stone up to 8; stone; 3 to 5 blocks of soil. The top is grass, sand at the shore, gravel deep under water, bare stone from 110 and snow from 128. Water fills to Z 62. |
| Density terrain (phase 4) | `Terrain/VoxelDensityTerrain.h` | Generator 2: 26.3's density graph over a column. See [Column density and terrain](#column-density-and-terrain-generator-2). |
| Meshing | `Meshing/VoxelMesher.h` | Each section meshes alone from an 18^3 padded copy: itself plus a one-cell border from its 26 neighbours, which may sit in other columns. A missing column reads as air. `GetSectionsTouching` is the remesh rule for an edit: the sections whose padded box holds the cell, up to 8 in 4 columns (`FVoxelSectionKey` names a section). Rules: see below. |
| Area builder | `Generation/VoxelArea.h` | Generates a square of columns, then meshes every section with its neighbours, one task each, timing every stage. `voxel.Bench Columns` uses it. The streamed world runs the same stages per column in the pipeline ([Streaming](#streaming)). |

Mesher rules:

- **Culling:** a face is hidden when its code has the cull bit and the neighbour's facing side is opaque, or when the neighbour is the same see-through block (glass beside glass). Leaves keep the faces between leaves. Water never draws against water.
- **Merging:** full-cube faces, water surfaces and water floors merge greedily in each 16 x 16 slice. The key is the state, the four corner occlusion levels and the tint, so a merged quad never blurs shading or colour.
- **Partial faces:** slab faces and water sides stay one quad per cell.
- **Ambient occlusion:** per vertex, from the two sides and the diagonal in front of each corner. Each quad is split along its brighter diagonal.
- **Fast exits:** sections that are all air, or solid with every side covered, return at once.
- **Model states:** their baked quads go to the model layer, unmerged ([Block models](#block-models)).
- **Output:** positions are in blocks from the column corner. UV 0 carries the block UV, continuous across a merged quad; UV 1 the face and overlay codes; UV 2 the tint (the biome's colour, [Biome tints](#biome-tints)) and face. Vertex colour R holds occlusion and G holds light.

Rendering:

- **Chunk component:** `UVoxelChunkComponent` is one RealtimeMesh per column with a single buffer set: every section's geometry, and a poly group per layer mapped to that layer's material slot. `SetMesh` hands the buffer set, the layer sections and the Lumen cards to RealtimeMesh in one update. The subsystem pools the components.
- **Lumen cards:** each column also carries a Lumen card representation, built on the workers with its stream set (`UVoxelChunkComponent::BuildCards`). For each face direction there is a card per section, fitted to that section's opaque and cutout faces in that direction. When a column would exceed Lumen's 32 cards per mesh, the vertically closest cards of its busiest direction merge first. Lumen captures the cards into its surface cache, which is how the terrain bounces light ([Lighting](#lighting)).
- **Chunk materials:** `M_ChunkOpaque`, `M_ChunkCutout`, `M_ChunkTranslucent` and `M_ChunkWater` reuse the block shader. They transform the continuous UV first, take each block's fraction, and sample with the continuous UV's gradients, so mips never jump at block edges. Texture animation uses the same `UFoundationBlockTextureTable`.
- **Collision:** none on the meshes. `AVoxelWorld::GatherCollision` returns block boxes (full or half for slabs) from the columns, as the Playground does from its block world. A model state collides with its own boxes from the facts, moved by its offset where the facts move them (cactus, bamboo, lily pads, carpets; phase 6 S7); `bSolid` (26.3's `blocks_motion`) is for the heightmaps. Columns not generated yet count as solid.

### Block models

Phase 6 session 6 draws the 83 model blocks (plants, multiface, custom element models) that S5 imported ([Blocks.md](Blocks.md), "States and shapes"), as scout D recommended ([record](Records/2026-10-09-Voxel-Phase6-Scout-D-Block-Models.md)).

- **The table:** `VoxelBlocks::MakeTable` copies the registry's 303 baked models in its order, dropping 16 quads that repeat an earlier one of their model exactly (flowerbed segments list a plane twice): 2,394 quads. Each model state gets `EVoxelShape::Model`, `EVoxelLayer::Model`, its parts and alternatives, its own light, the water it holds (`HeldFluid`, the water source, for the 286 water-holding states) and the block's offset. `bSolid` stays 26.3's `blocks_motion` tag.
- **The mesher** (`FEmitter::Model`): one alternative per part by `VoxelModels::PickAlternative` (a hash of the cell and the part; all weights are equal), every quad moved by `VoxelModels::GetOffset` (a hash of X and Y only, in 16 steps up to the block's horizontal range, and down to its vertical range for XYZ kinds, so two-cell plants and dripstone columns stay aligned). A quad with a cull face is hidden when that neighbour covers the side opaquely.
  - Normal: the quad's shade direction (`shade_direction_override`, turned with the blockstate rotation as S5 bakes it), through the face index, so crosses are lit as up.
  - Ambient occlusion: models with `ambientocclusion: false` stay open; a quad lying flat within 1/16 of a cell side takes the corner occlusion of the full face there, read around the cell beyond the side it faces (a cactus side) or around its own cell when the side is behind it (a carpet's top). Other quads stay open.
  - Tint: a quad with a `tintindex` takes the block's tint source with the biome blend (grass, foliage, dry foliage; the lily pad's constant `#208030`).
  - Light: the brighter of the state's light and the element's `light_emission` (the firefly bush's overlay at 15), in vertex G.
  - UVs are the baked texture UVs, with no face transform; the code carries the texture and the tint bit, no overlay.
  - Water: a water-holding state draws its water's faces as a source would, and water never draws a face against it. It also counts for the motion-blocking heightmaps (`HoldsFluid`), and for swimming.
  - Thin planes already have both faces in the data, so `M_ChunkCutout` stays one-sided.
- **Rendering:** the model layer is the chunk mesh's fifth section, drawn with `M_ChunkCutout`. It is never in Lumen's cards (cards take the opaque and cutout layers only), but it is in the ray-tracing scene, like the rest of the column (capture G3 below). Plants cast sun shadows at every distance: RealtimeMesh draws one mesh batch per buffer set, with its shadow flag the OR of its sections' (`RealtimeMeshBufferSetProxy.cpp`, `InitializeMeshBatch`), so a section's own `bCastsShadow` is ignored while the column's terrain casts. Keeping plant shadows near the camera only needs the model quads in their own component, or a shadow-pass fade in a model material (a decision for Jesus, in the S6b handoff). The Playground draws the same models since S6b, from one runtime mesh per model with the same offsets and alternatives ([Blocks.md](Blocks.md), Rendering).
- **Switches** (cvars in `VoxelWorldSubsystem.cpp`; each change remeshes every loaded column):
  - `voxel.Models 0` leaves the model quads out (their water stays), for A/B.
  - `voxel.Debug.ModelShadeUnturned 1` shades quads whose shade direction differs from their facing as up, as an override left unturned by the blockstate rotation would (look call 2 below).
- **Generated worlds** hold no model state yet (decoration places none), so their mesh is byte-identical with models on and off and to the mesh from before S6 (`Unnamed.Voxel.Mesher.Digest` pins it per preset).
- **Captures** (`CaptureVoxel -Garden`, `AVoxelWorld::ApplyShotGarden`): a plot with every model block (rows, a pool for the water-holding ones, an overhang with vine, glow lichen and sculk vein, wall-mounted amethyst clusters, a dripstone column, tall grass across a section border), a grass field of about 4,800 plants around a bare 7 x 7 clearing, and a distance plot shot from 8 to 128 blocks.
- **Look calls** (made by the night manager under Jesus's delegation, October 10, 02:19; numbers in [the S6 record](Records/2026-10-10-Voxel-Phase6-S6.md)):
  1. **Ray tracing (G3):** plants stay in the ray-tracing scene, one component per column. With exposure fixed, the clearing's ground moved -0.8 / -0.4 / -1.4 per channel with plants, under the 3/255 bar.
  2. **Shade override:** kept turned with the blockstate rotation (the baked data). Unturned is about 3/255 brighter and flatter on wall-mounted amethyst. In 26.3 only the vine's ceiling part and the amethyst cluster and buds turn an override; glow lichen and sculk vein have none.

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
| In view (ring 0) | gap <= `ViewRadius`; meshed and drawn | 545 columns |
| Needed without decoration (ring 1) | in view or beside a column in view; generated, because a section meshes from its 3 x 3 | 657 columns |
| Unloaded | beyond `ViewRadius + Stages + UnloadMargin` | beyond 15 columns (18 with decoration: the margin rises to 3, `GetMinUnloadMargin`, so a diagonal step back and forth unloads none of the rings' corners) |

With decoration (S3) each stage needs the 3 x 3 of the one before it, so `Stages` is 3 and each ring is the one inside it grown by a column on both axes (`GetRing`; scout C's figures): ring 1 final (657 columns), ring 2 decorated (777), ring 3 generated (905, +38% over 657). Without decoration `Stages` is 1 and the sets are as before.

- **Gap:** the distance between the center column's square and another column's square, in columns. Every block within `ViewRadius` columns of the camera, anywhere in its column, lies in a column in view, so a fog ending at `ViewRadius * 16` blocks never shows a missing column.
- **Shape:** a circle. A square with the same clear distance is 25 x 25 = 625 columns, so the circle loads 13% fewer.
- **Hysteresis:** the unload ring is the prompt's "unload radius = load radius + 2", with the generated ring as the load radius. The margin has a minimum of 2 (`MinUnloadMargin`), which covers a diagonal move.
- **Order:** `VoxelStreaming::GetNeededColumns` sorts by distance, then ahead of the view (the yaw in 8 sectors), then Y, then X. `GetUnloads`, `Plan` and `GetViewSector` complete the planner. All are pure functions.

The pipeline (`FVoxelColumnPipeline`, `Voxel/VoxelColumnPipeline.h`):

- **Plain C++** with no UObjects, so tests drive it without a world. It keeps one record per column.
- **Jobs:** `UE::Tasks` at `BackgroundHigh`, up to `voxel.MaxTasksInFlight` (0 = twice the workers, 60 here). Walking the needed columns nearest first, a column gets a mesh job when it is in view and its 3 x 3 is final, and otherwise the job its ring's stage needs. A walk that ended with room to spare is not repeated until a job finishes or the plan changes.
- **Stages (S3, `voxel.Decoration 1`):** a record holds the snapshot (Generate without the freeze), the 9 buckets its Decorate aimed at its 3 x 3, and the final column (Finalize: the merge, the freeze pass, the edits). Each column wants its ring's stage; what it misses starts as soon as its own inputs exist. Without a decorator, Generate gives the final column at once (the old path).
- **Recompute-on-miss** ([scout C](Records/2026-10-09-Voxel-Phase6-Scout-Pipeline.md), recommendation 1): snapshots and buckets are caches of pure functions. A Decorate without a neighbour's snapshot generates it again; a Finalize without a source's bucket runs that source's Decorate again (at worst 9 Decorates and 25 Generates). **A bucket aimed at a column that is already final is dropped, never applied:** applying it again would undo the player's edits (`IfTargets` would put an ore back where the player left stone). This is the return trip: a column reloading beside a final one.
- **Lifetimes:** a bucket is freed when its target finalizes; a final column's snapshot once it and its 8 neighbours have decorated. Its neighbours are its sources, so at rest no final column keeps one: the snapshots held are those of rings 2 and 3 (248, 4.3 MB at radius 12), and one regenerated for a neighbour that reloaded stays until that neighbour decorates. Buckets aimed at rings 2 and 3 stay until their target finalizes or their source unloads (6.6 MB at rest at radius 12).
- **Cancellation:** an unloaded column's jobs that have not started skip their work (a flag per record); results of any job whose record changed are dropped by serial.
- **`voxel.Info`** adds, in a voxel world, the rings, each stage's count and times, what is held, and the recompute counters (snapshots regenerated, Decorates run again, buckets aimed at final columns dropped, jobs cancelled); the 1-second log line ends with the same counters.
- **Test hooks:** `bManualJobs` (jobs queue; `RunJob` runs any one, so tests choose the order), `EvictSnapshot`, `EvictBuckets`, and `EVoxelPipelineMutation` for the mutation checks (each fails its test: `ApplyToFinal` fails `Pipeline.ReturnTrip` with 24 edits undone, `MissingBucketAsEmpty` fails `Pipeline.EvictionFuzz` with 29 to 46 columns per seed off the oracle, `KeepSnapshots` fails `Pipeline.Stages`).
- **Gate (S3, quiet machine: no other UnrealEditor, UnrealBuildTool or java; CPU 1 to 12% at the bench runs; old and new builds alternated by DLL swap in one window, October 10 00:50 to 01:06):**
  - **Flight with decoration:** no holes in any lap, 0 of 10,004 frames over the 2 ms budget (worst 1.89 ms); memory flat over laps 2 and 3 (process 4,066 to 4,066 MB; columns 18.55 to 19.22 MB within each lap). 3,913 to 3,916 columns generated per lap against the old 3,350 (+17%). The laps never reload a column beside a final one (the circle unloads every column once per lap: 0 regenerations); `Pipeline.ReturnTrip` covers that case. With `voxel.Decoration 0` the flight is the old build's: no holes, 3,350 / 3,355 / 3,350 columns per lap, columns 11.36 to 11.84 MB.
  - **First view:** start to view 3,628 and 3,591 ms (first frame to view 2,028 and 2,070), against the old build's 3,438 and 3,269 (1,838 and 1,820); the gate is 4.0 s. Generated at radius 12: 905 columns (+38% over 657), 777 decorated, 657 final; worker time to view 7.6 to 8.1 s.
  - **Finalize** (`voxel.Bench Decorate 4`, medians of 3 rounds, old / new): overworld 0.172 / 0.181, large_biomes 0.159 / 0.276, amplified 0.204 / 0.329 ms, of which the freeze is 0.048, 0.069 and 0.076 ms; in the old Generate the freeze cost 0.050 to 0.065 ms, so the move is net 0.00 to +0.02 ms (the rest of the raw delta is in unchanged code; Decorate, unchanged, moved by up to 0.2 ms between builds). Whole column (old / new): overworld 3.947 / 3.814, large_biomes 4.126 / 4.257, amplified 4.787 / 5.036 ms. **Amplified whole column 4.79 (old) / 5.04 (new) ms in one window; at the 5 ms limit within noise; flagged for Jesus** (Generate rose 0.13 ms on code that only lost the freeze). S3's own cost is about zero.
  - **A busy-machine flight** (a test run in parallel, Generate at 5.15 ms) had holes in 3 frames of lap 1, at most 6 columns; none on the quiet machine.
- **A mesh job** meshes the dirty sections (all of them when it has no cache), skips all-air sections, then builds the stream set and the Lumen cards.
- **Stale results** are dropped: by a serial per record when the column unloads, and by a mesh version when it was edited while meshing.
- **Mesh cache:** section meshes are kept for columns within 2 columns of the center (1.3 MB at the first view; empty while flying, because new columns are meshed far away), so an edit remeshes only the touched sections.
- **`voxel.Debug.EdgeWalls`** meshes the view's edge against air (captures).

Editing:

- **`SetCell`** copies the column (copy on write, about 10 KB), sets the cell and its heightmaps (`FVoxelColumn::SetState`), and marks dirty exactly the sections whose padded 18^3 box holds the cell (`VoxelMesher::GetSectionsTouching`: up to 8 sections in 4 columns).
- **Edits** are kept per column with a stamp (the block world's revision) and replayed after the column generates (with decoration, after Finalize; the snapshot stays pure), so they survive unloading. They last for the session until phase 10.
- **`FVoxelBlockStorage`** (`Voxel/VoxelBlockStorage.h`) is an `IFoundationBlockStorage` over the pipeline, reached through a getter so tests pass their own pipeline. A cell is loaded when its column is generated. `Empty` drops the edits.

The subsystem (`UVoxelWorldSubsystem`, a `UTickableWorldSubsystem` for game and PIE worlds, started by `AVoxelWorld`):

- **Focus:** the player's camera, so `voxel.Fly` works. Captures pin it to the spawn.
- **Budget:** all chunk work on the game thread in a frame (plan, jobs, apply) stays within `voxel.ApplyBudgetMs`. A column's apply cost is estimated from its quads (a line fitted to measured applies, weighted to recent ones, times 1.5), and applying stops when the estimate would pass 85% of the budget. A frame's first column goes anyway, so no column waits forever, however big; but when the frame's planning and jobs leave it no room (its estimate would pass the whole budget), it waits one frame and goes first in the next (S9b: it removed most frames over budget with trees). Ready columns apply nearest first, a column behind the camera's view sector counting as twice as far (S9b), so the columns the camera faces go first while it turns. Each measured apply counts as at most 4 times its estimate plus 1 ms, so one stall does not raise the estimates after it.
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
  - `created` and `lastPlayed` (UTC, ISO 8601);
  - optionally `clocks` (water and sky DN1): the world clock, saved when the player leaves the world (`AVoxelWorld::EndPlay`, which reads world.json afresh, sets the key and saves it). Its layout is vanilla's `world_clocks.dat`: `"clocks": {"minecraft:overworld": {"total_ticks": 13219, "paused": true, "rate": 3.0}}`, with each value left out at its default (0, false, 1). Other clock ids and unknown fields are ignored. A malformed value refuses the file, as any other field does. A missing key means a new world's clock, at tick 0. **The optional key at `version` 1 is Jesus's own decision (the water and sky plan's Q2 (a), 2026-10-10), chosen over a version bump.** It is not a breach of the version-bump rule: older builds ignore the key, and a world never left since DN1 writes no key, byte for byte as before (`Unnamed.Voxel.Clock.SavedWorld`). The dev seed and sessions pinned with `-VoxelTime` save no time. The game tick is not saved: it goes with phase 10, with the fluids' pending acts.

  A file from a newer version or generator is refused, never rewritten. Re-Create refills the Create screen from these keys; a random seed comes back as its number.
- **Travel:** `L_Generated?VoxelWorld=<id>`. `AVoxelWorld` reads its world on first use and builds the terrain with `VoxelTerrainFactory`. The pawn asks its lab for the player's rules when it arrives (`AFoundationLab::ConfigurePlayer`).

| Choice | What it does now |
|---|---|
| Seed | Parsed at creation (`FVoxelSeed::Parse`); a blank seed draws a random one once, and the world keeps it. |
| World type | Superflat builds the flat terrain from its layers: listed top first on screen, laid from the floor up. Every layer must be a block this game has. Default and Single Biome build the overworld preset (Single Biome until phase 5); Large Biomes and AMPLIFIED build their own presets. Plain `L_Generated` (the dev seed) uses generator 2, or `-VoxelGenerator=1` for the stand-in. A saved generator-2 world whose preset cannot be read or compiled gets no stand-in: Create and Play refuse it before travelling, with the menu's error line ("This world cannot be played: ..."), and opened directly the map builds an empty terrain, logs the error and returns to the title screen (the title screen does not show the reason yet: the page shows errors only for its own requests). Only the dev seed falls back to the stand-in, with an error in the log. |
| Game mode | Survival and Creative set the player's mode and the lab's block rules: Creative gets infinite supply, no tool wear and the creative inventory; Survival gets finite supply and tool wear, and starts with the loadout's tools and no blocks. Hardcore plays as Survival on Hard: the screen and the store force Hard, no commands and no bonus chest. Permadeath needs a game-over screen first. |
| Difficulty | Peaceful, Easy, Normal or Hard in the player's vitals. Peaceful means no mob damage and, with natural regeneration on, a point of health every second and of food every half second. |
| Game rules | `natural_health_regeneration` drives the player's regeneration, and `advance_time` the world clock's day (off stops the day, not the game tick). The others are saved for their systems. |
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

`voxel.Debug.Climate <C|E|W|PV|T|V|O|F|J|D|off>` (default off) shows all climate values at the player on screen (overworld preset) and highlights the one named. It also shows N at the player and PS (still not confirmed on screen: shots leave out on-screen debug messages). It is debug-only, like the next variables, so it has no row.

The biome views are debug-only too, all off by default:

- **`voxel.Debug.Biomes 1`** shows, at the player:
  - the biome its loaded column stores for the player's block (the jittered lookup);
  - the six climate targets there, the biome they pick and its fitness.
- **`voxel.Debug.Minimap <biomes|C|E|W|PV|T|V|O|F|J|off>`** draws a 256-pixel map in the top right corner, north up, with the player and its heading. It shows the biomes at depth 0, or one climate parameter coloured as `voxel.ClimateMap` colours it. `voxel.Debug.MinimapScale` sets its blocks per pixel (4, so 1,024 blocks across). The image is redrawn on the workers when the view changes or the player moves an eighth of its width.
- Both read the world's own terrain, so Large Biomes and AMPLIFIED show their own climate.
- They draw through `UDebugDrawService` ("Game"). The hook exists only while one is on: the variables' change callbacks register it and remove it. Off, there is no draw hook, no task and no texture. `Biome.Lookup` checks that both variables start off; the rest follows from the code: nothing else registers the hook.
- Voxel shots keep the UI while one is on, so they show it.

`BiomeMapColors` (category Debug) is the biome maps' palette: one colour per biome id, after the community maps' conventions, in `Config/DefaultGame.ini`. It has no console variable; the debug tools read it from the settings. A biome without a colour is drawn in a colour hashed from its id, and the map's summary names it.

`voxel.Decoration` (default on) decorates streamed columns (S3) and those of `voxel.Bench Columns`, the radius-8 gate; off keeps them as Generate leaves them (the control). `voxel.Decoration.FreezeLast` (default on) runs the freeze pass at Finalize; off runs it inside the snapshot (S2's order). Changing either rebuilds the world ([Decoration](#decoration)). The S2 capture preview (`voxel.Decoration.Preview`) is retired. `voxel.Decoration.Plants` (default on, S8) places the vegetation placed features. Off, only the ores are placed, and the old goldens and hashes reproduce (the control). Streamed worlds rebuild on a change, and every bench follows it ([Plants and patches](#plants-and-patches-phase-6-s8)).

`voxel.Debug.EdgeWalls` (default off) is a debug-only variable, not a setting, so it has no row. It meshes the view's edge against air, so the outer columns draw their sides: the cut through the world that the `overview` and `section` shots show. `CaptureVoxel.ps1` turns it on for its shots.

`voxel.Debug.ColumnGrid` (default off, S8b) is debug-only too. It draws the borders of the 7 x 7 columns around the player as magenta lines on the ground (OCEAN_FLOOR, so under plants and water), with a post at each corner, for the shots of patches crossing borders.

## Commands and diagnostics

- `voxel.Info` prints the following, to the console and to `LogUnnamedVoxel`:
  - the world range and the dev seed;
  - every `voxel.*` variable, with its value and what set it;
  - the view radius with its column counts, for example `View radius 12: 545 columns in view, 905 generated; unload beyond 18 columns` with decoration (657 and 15 without), then decoration's rings; in a voxel world, the streaming stages (`UVoxelWorldSubsystem::DescribeStages`);
  - the task workers, the tasks in flight and the mesh backend.
- `voxel.Seed [text]` shows the world seed a seed text makes, for example `Seed "Unnamed" -> -3224043261468865476 (text)`. In a voxel world it also rebuilds the world with that seed for the session. Without text it shows the current seed.
- `voxel.Regenerate` rebuilds the voxel world with its seed, picking up changed console variables.
- `voxel.Fly [blocks per second]` is the free camera ([Streaming](#streaming)).
- `voxel.Time set <time>|day|noon|night|midnight`, `add <time>`, `query time|day|gametime`, `pause`, `resume` and `rate <rate>` drive the world clock (water and sky DN1), as `/time` does, with 26.3's own replies (scout N's probes; for example `Set minecraft:overworld to 25002 tick(s)` and `Timeline minecraft:day is at 1004 tick(s)`).
  - A time is whole ticks, or a number with `d` (24,000 ticks), `s` (20) or `t`. A marker (day 1000, noon 6000, night 13000, midnight 18000) moves the clock forward to its next tick of the day; already there, it stays. The rate runs from above 0 to 1000.
  - The clock is `FVoxelWorldClock` (`Source/UnnamedVoxel/Clock/`), owned by `AVoxelWorld` and advanced at the top of its `Tick`. It turns each frame into whole 20 Hz game ticks, at most `UFoundationProfile::MaxCatchupSteps` (5) of them; after a hitch the excess is dropped and counted. The game tick always runs (W2a's fluids will run after each one). The day clock moves `rate` ticks per game tick, only while the world's `advance_time` rule is on and the clock is not paused. A day is 24,000 ticks, and the day timeline is the total modulo a day. The sky is still today's fixed rig (DN2 moves it).
  - `-VoxelTime=<time or marker>` pins the day clock for shots and flights: it is set and paused (the game tick still runs), and the session saves no time; `voxel.Time resume` lets it run.
  - The F3 overlay's pose section shows `Time: day 1 at 1004 (07:00) | moon waning gibbous | game tick 4521`, plus the rate, the pause or `advance_time off` when they apply. Days count from 0, the hour is 06:00 at tick 0, and the moon's phase is the day number modulo 8, starting on a full moon. `voxel.Info` adds the same line and the ticks dropped by the cap.
- `voxel.Bench Noise [millions]` times each primitive on the calling thread (1 million samples each by default), then 3D fBm across every task worker.
- `voxel.Bench Columns [radius 4] [preset|standin] [nocaves]` builds `(2 radius + 1)^2` columns off screen (default: overworld with caves; `standin` is generator 1's height terrain). It reports the area stages (generate, gather, mesh) on one thread and then on every worker. For a density terrain it adds the generation stages (density, aquifer, biomes, blocks: p50, p95, mean), then block hashes in two load orders: row by row on one thread against backwards across every worker (`VoxelBench::HashLoadOrders`, CityHash64 of each column's dense blocks, combined in position order). Radius 8 hashes: overworld `0x7bbf1f42c5434443`, large_biomes `0xc70b24ba700501f2`, amplified `0x27c2d49086a4614a`; both orders agree, 0 columns differ. The biome hashes come next, the same way ([Biomes](#biomes)).
- `voxel.Bench Decorate [radius 4] [preset] [nocaves]` decorates `(2 radius + 1)^2` final columns on one thread (Generate on radius + 2, Decorate on radius + 1, Finalize on the radius), first with scout E's early-out off as a warm-up, then on for the gate figures: each stage's p50, p95, mean and the whole column; tries, skips, writes, log and working-copy bytes per Decorate; what Finalize replayed; and the final-column hashes with the early-out on and off (equal: it is exact). Since S3 its Generate excludes the freeze pass and Finalize includes it (a line gives the freeze alone), as `voxel.Decoration.FreezeLast` says.
- `voxel.Bench FreezeDiff [radius 4] [preset] [x y]` builds the decorated area with the freeze at Finalize and inside the snapshot, around (x, y) or the snowiest column top of a coarse search near the origin, and explains every cell that differs (an ore or its host beside a snow layer, or in the same blob as one).
- `voxel.OreHistogram [radius 8] [preset] [nocaves] [nodiscard]` writes the blocks the ore features placed (final minus generated) per Z and per state to `Saved/Voxel/OreHistogram-<preset>-r<radius>[-nocaves][-nodiscard].csv`; `uv run --no-project --python 3.13 --with matplotlib python -I Scripts/OreHistogram.py <csv> [--each]` draws one sheet with a panel per ore and per rock blob (and one PNG per panel).
- `voxel.Bench Climate [radius] [preset]` times the climate sampler over `(2 radius + 1)^2` columns (default radius 8 = 289 columns, overworld preset).
- `voxel.Bench Density [radius 4] [preset] [nocaves]` times the density terrain in four passes, each starting cold (`ResetCaches`):
  - skipping, corner sharing and biomes on (row by row, so each column finds its west and north neighbours' corners: the best case);
  - biomes off;
  - sharing off;
  - skipping off without sharing.

  It prints their stages, corners evaluated and shared, the blocks that differ from the first pass (skipping on and off; biomes on and off; sharing on and off) and the cache's size. It then compares the pruned biome search with brute force on the central 3 x 3 columns (quarts that differ, always 0) and times all workers. `nocaves` turns caves off.
- `Scripts\CaptureVoxel.ps1 -Caves` adds a cave view and a lava view to the capture. `-View CX,CY,CZ,TX,TY,TZ` adds a `view` shot from a camera to a target, in blocks; with `-SpawnAt X,Y -SpawnExact` it shoots a chosen place (S8b's biome pairs).
- `voxel.FindBiome <biome> [X Y] [MaxRadius 6000] [preset]` searches outward from (X, Y) for up to three spots where the biome (`plains` or `minecraft:plains`) holds the middle column's centre and at least 60% of a 3 x 3 of columns' surface cells, the floor under water. It checks a 32-block lattice ring by ring (radius at most 20,000), then generates the candidates' columns (at most 40), with the default terrain settings. For each spot it logs the cell, its column, the distance, the surface and floor Z, the top block and the coverage. It works headless, with the world's seed or the dev seed.
- `voxel.FindBlock <block> [X Y] [radius in columns 12] [preset]` decorates columns outward from (X, Y), each alone (`DecorateAlone`, following `voxel.Decoration.Plants`). It logs the first three that hold the block: the count, the lowest cell nearest the column's centre, and the floor and surface Z there. It finds the plants only some places grow, such as kelp where its noise count is positive, and sea pickles, one chunk in 16. Each column costs 25 Generates, so the radius is at most 24 columns and the search stops at 400 columns; it blocks the game thread while it runs.
- `voxel.BiomeMap [size px 1024] [blocks per px 8] [preset] [surface|<y>]` writes a biome PNG to `Saved/VoxelClimate`, centered on the player (or 0,0), coloured from `BiomeMapColors`. Beside it goes a CSV of each biome's share of the map and of the jar's depth-0 lookup samples. It logs the time, the biome count and the largest shares. `surface` (the default) reads each point at depth 0; a number reads that height.
- `voxel.ClimateMap <C|E|W|PV|T|V|O|F|J|N|PS|all> [size px 1024] [blocks per px 8] [preset]` writes PNGs to `Saved/VoxelClimate`, centered on the player (or 0,0), and logs the time and percentiles. Maps are drawn north up (-Y, since Y points south); until phase 5 session 4 the climate maps were drawn south up. `all` includes N and PS. PS is the terrain height. N is the final density at sea level (red solid, blue open), about 3.3 µs per point. O is drawn as the surface height the offset implies (y = -64 + (O + 1.5) x 128, before factor, jaggedness and 3D noise).
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

**Cave exposure and glow (phase 5, session 3b).** At the fixed daylight exposure cave interiors were near black and lava was invisible (the `cave-lava` view measured 0/0/0). Jesus's decision: eye adaptation between two EV bounds, plus stronger emission for light-emitting blocks. Hemera took the look call on October 9 (option (a)).

- **Emission:** `CHUNK_LIGHT_EMISSIVE = 8000` in `Scripts/Editor/CreateBlockMaterials.py` sets the `LightEmissive` default of `M_ChunkOpaque`, `M_ChunkCutout` and `M_ChunkTranslucent` (color x light / 15 x LightEmissive). The block materials keep 4 (`BLOCK_LIGHT_EMISSIVE`), so the Playground is unchanged. At the daylight exposure, still lava's pixels read red 188, like sunlit sand (185): full-bright, as in vanilla. The view's mean red is 76.4, with nothing at 255; 2000 gave 38.7.
- **Exposure:** the map's unbound PostProcessVolume (`EXPOSURE_SETTINGS` in `Scripts/Editor/CreateGeneratedMap.py`, which `verify()` checks) uses the histogram method, at 2 f-stops per second towards brighter scenes and 1 towards darker ones.
  - Metering reads sunlit terrain at only EV100 12.8 to 13.5 (the block textures are dark). A cap at the old fixed 14.5 would therefore brighten every outdoor view (the overview went from 88 to 133 mean sRGB).
  - So the cap is 12.5 and the exposure compensation 2 stops lower (-1 instead of the engine default 1). Local exposure pivots on middle grey times the compensation, so its middle-grey bias is +2. Without that, daylight came out about 0.4 EV darker: the overview's mean went from 88 to 75 (`VoxelShots-20261009-105141`), and its exposure fell from 1.11e-4 to 8.43e-5 at the same metered EV100, 12.8 (the overlay crops `hdr-old.png` and `hdr-text.png` in the evidence folder below).
  - Every view metered at 12.5 or above gets exactly the old daylight exposure. The floor, EV100 6, lets caves open up to 6.5 stops brighter.
- **Shots:** `-VoxelShot`'s camera adapts at 50 f-stops per second, so each shot lands on its adapted exposure within the settle frames. `ShowFlag.VisualizeHDR 1` in `-ExecCmds` prints the metered EV100 and the exposure on each shot.
- **Result, mean sRGB:**

| View | Before | After |
|---|---|---|
| `cave-lava` | 0.0 / 0.0 / 0.0 | 130.2 / 85.1 / 27.0 |
| `cave` (reaches open sky) | 63.1 / 70.2 / 58.9 | 105.9 / 110.5 / 93.5 |
| `overview` | 88.0 / 103.2 / 108.4 | the same |
| `closeup`, `ground`, `section` | 90.8 / 111.8 / 55.7, 159.4 / 113.1 / 69.8, 104.9 / 114.7 / 125.0 | within 0.1 per channel |

- **Lava in a cave clips:** with the eye adapted to the cave, 36% of the `cave-lava` view has a channel at 255, mostly red. The lava reads yellow-orange with its texture still visible, and the walls glow. The knob is `CHUNK_LIGHT_EMISSIVE`. An estimate, not measured: 5000 would give about 0.7 stop less clipping (log2 of 8000 / 5000), with the daylight `cave-lava` view's mean red near 63. That is interpolated in stops between the measured 38.7 at 2000 and 76.4 at 8000.
- **Evidence:** `Tests/Evidence/2026-10-09/VoxelShots-20261009-104219` (before), `-104748` (emission only, at the daylight exposure), `-105556` (both); HDR overlays `-105247` (new map) and `-105415` (old map); montages and crops in `Tests/Evidence/2026-10-09/CaveBrightness-20261009-3b/` (`look-brightness.png` first).

**Local lights (torch probe T0 + T1, October 10).** Torches will light caves with real point lights. The light probe (`CaptureVoxel.ps1 -Lights`, `voxel.LightProbe <N> [megalights|vsm|none]`; [record](Records/2026-10-10-Torch-Light-Probe.md)) measured what they cost and how they look.
- **The probe:** in a dark cave of the dev seed at 1600 x 900, lights 1 m over cave floors, attenuation radius 13 blocks.
- **The look call (Nyx, delegated, 04:20):** one MegaLights point light per torch (ray-traced shadows), for T3.
- **The project setting:** `r.MegaLights.EnableForProject` stayed 0 through T3; the probe turned it on per case at runtime. Since T3b it is 1 for the whole project (Jesus; see "Torch lights" below).

| Lights | MegaLights | VSM shadows | Unshadowed |
|---|---|---|---|
| 0 | +0.12 ms (MegaLights' floor) | base 3.48 ms | base |
| 1 | +0.56 | +0.27 | +0.10 |
| 10 | +0.78 | +1.68 | +0.26 |
| 100 | +1.39 | +8.95 | +1.42 |

- **Unshadowed lights leak.** A light over three blocks of rock lifts the cave floor below by 31/255. Both shadowed modes stay within 0.15/255.
- **Edits:** a mined niche is lit correctly once its column is back on screen, with MegaLights after 5 to 11 more frames.
- **Glowing cubes** (glowstone and the other light-15 cubes, through emission and Lumen):
  - they lift a cave wall 5 blocks away by 39 to 83/255, at +0.03 to +0.06 ms each;
  - 35 to 43% of that light comes from screen traces, so it fades when the cube leaves the screen (a known limit).

**Torch lights (torch T3 and T3b, October 10).** Torches place, survive and drop as measured in 26.3, and every light source at level 10 or more lights the cave around it ([Blocks](Blocks.md) "Light sources"; records [T3](Records/2026-10-10-Torch-T3.md) and [T3b](Records/2026-10-10-Torch-T3b.md)).
- **The pool:** `FVoxelLights` on `AVoxelWorld` gives the nearest 64 sources within 48 blocks one MegaLights point light each (at most 100), merging sources under 2 blocks apart. The mesh job gathers each column's sources, so an edit costs about 0.01 ms of game thread.
- **The look (Jesus, T3b):** warm A (600 cd, 2,200 K, inverse square), with MegaLights on for the whole project.
  - The torch model glows at level 1 (`voxel.TorchGlow`), not its light level 14.
  - The flame's quads are cut from the stick, so only the flame glows at `voxel.TorchGlow 0`. That is Jesus's pick, waiting for a stick glow under level 1 (T3c), because the stick reads black without it.
- **Cost (T3b's quiet window, GPU at 1600 x 900):**
  - +0.61, +0.87 and +1.40 ms at 1, 10 and 100 torches, the same as T1's probe lights.
  - With no lights, MegaLights costs +0.13 ms GPU and about 1.5 ms of game thread.
  - The Playground probe at the 60 cap: 45.03 against 45.31 FPS without it.
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
  - Evidence: `C:\dev\sandbox\phase4s4\montage\cavecmp2.png`, `Tests/Evidence/2026-10-09/VoxelShots-20261009-025021` (cards), `-025054` (hit), `-025126` and `-025159` (+3 EV).
  - Re-run in phase 5 session 3b, after the brightness fix above, so caves and lava are visible: cards stay (the settled test: switch only if hit lighting looks clearly better in caves).
    - `cave`: the same image. 0.02% of pixels differ by more than 8/255, against 0.03% between two cards runs.
    - `cave-lava`: 21% of pixels differ by more than 24/255 (two cards runs: 0.05%; smaller differences come from lava's animation). All of it is on the lava surface: slightly more saturated orange with hit lighting, a little whiter with cards. The walls are the same.
    - GPU frame at 1600 x 900, cards / hit / cards in sequence: `cave` 4.12 / 5.53 / 3.79 ms, `cave-lava` 5.13 / 6.22 / 5.31 ms. Measured with Minecraft open on the same PC, so compare only within the sequence: hit lighting costs 1 to 1.5 ms more.
    - The switch, if a later look needs it: `r.Lumen.HardwareRayTracing.LightingMode 1`.
    - Evidence: `Tests/Evidence/2026-10-09/VoxelShots-20261009-105747` (cards), `-105826` (hit), `-105906` (cards); montage `Tests/Evidence/2026-10-09/CaveBrightness-20261009-3b/look-lighting.png`.
- **Software Lumen.** On a GPU without hardware ray tracing, Lumen traces distance fields. The chunks have none, so it cannot see them: no bounce and no sky occlusion, and caves would be lit by the sky. Mesh distance fields for columns (`SetDistanceField`) are the fix when it matters.
- **Fog color.** It is fitted to the map's sun, so a day cycle would need it refitted. A faint seam remains on the sun side.
- **The Playground** keeps its own rig (sky light 1).

## Tests

`& Scripts\TestVoxel.ps1` runs `voxel.Info`, `voxel.Seed` and `voxel.Bench Noise`, then the 84 `Unnamed.Voxel` tests (every native suite: 133). Saved worlds are tested by `Unnamed.Worlds.Saved` (native) and, through the menu end to end, by `Tests/create-world.mjs`. It writes `Tests/Evidence/<date>/VoxelAutomation-<time>/`, with the commands' output in `voxel-console.txt`. The editor's throwaway user folder goes to a temp folder, not into the evidence.

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
| `Unnamed.Voxel.Blocks.LookStates` | (S9b) Look states: a synthetic table set out of order and then changed follows; on the real table every look state is of its block, no higher, and draws alike; oak leaves of every distance and persistence share one; snowy grass does not share grass's. |
| `Unnamed.Voxel.Storage.Section` | Palette widths at 1, 2, 16, 17, 256, 257 and 4096 states, with exact byte counts; round trips; 3000 random edits widening 0 to 4 to 8 to 16 bits; non-air counts; `Compact` back to one state. |
| `Unnamed.Voxel.Storage.Column` | All four heightmaps over water, floating leaves and glass; out-of-world reads; single-state sky sections; an empty column. |
| `Unnamed.Voxel.Mesher.Shapes` | Known quad counts (a block, a merged cube, a bar, glass beside glass, leaves beside leaves, a floor across column borders, slab, water cell, pool), the slab top at half height, the water surface at 8/9, darkened corners. |
| `Unnamed.Voxel.Mesher.Borders` | Random blocks of every kind over 3 x 3 columns and three sections, meshed and split back into faces. Compared with an independent cell-by-cell reading of the rules, every face matches: none missing, none hidden drawn, none drawn twice, and every face's four corner levels agree. Run with greedy merging on and off (26,896 faces). |
| `Unnamed.Voxel.Mesher.Cards` | Lumen cards from meshed columns: one block gives one thin card per direction, looking along that face's normal; glass and water give none, leaves six. A pillar through all 24 sections, 98 cards before merging, stays within Lumen's 32. In both, and among random blocks, every opaque and cutout face lies inside a card facing its way. |
| `Unnamed.Voxel.Terrain.Flat` | The classic Superflat stack (bedrock, two dirt, grass) in every column with its surface and heightmap; water on top is not dry land; a stack taller than the world fills it; trailing air is dropped; no layers stands nowhere. |
| `Unnamed.Voxel.Terrain.Generate` | Five columns, including ones millions of blocks out, generated in order on this thread and in reverse on the workers: identical. Heightmaps follow the terrain function; bedrock floor and deepslate; every column under 64 KB; another seed changes the land. |
| `Unnamed.Voxel.Streaming.Planner` | 274 checks on the pure planner: the in-view and generated counts, coverage of the view, the order of loads, view sectors, hysteresis, the rows a one-column move adds and removes in steady state, radius changes, and far coordinates. |
| `Unnamed.Voxel.Streaming.Pipeline` | The pipeline without a world: results do not depend on load order (three runs); unload and reload; stale results are dropped; work in flight stays bounded; no column meshes before its 3 x 3 exists; done means done. |
| `Unnamed.Voxel.Pipeline.Stages` | Decoration's rings at radius 12 (545 / 112 / 120 / 128; unload 18, 15 without decoration). A decorating pipeline with jobs in a seeded random order and with threaded jobs: no mesh before its 3 x 3 is final, no Finalize before its 9 sources hold their buckets, no Decorate before its 3 x 3 snapshots; every final column equals the decorated area's; nothing regenerated, decorated again or dropped at a still focus; at rest no final column keeps its snapshot and the snapshots held are rings 2 and 3; stepping back and forth across a diagonal border generates nothing (hysteresis). |
| `Unnamed.Voxel.Pipeline.EvictionFuzz` | Scout C's check: 12 seeds of 700 random steps (run a random job, update, evict a snapshot or buckets, move the focus); afterwards every final column equals the oracle, and recompute-on-miss ran (572 regenerations, 150 Decorates again). |
| `Unnamed.Voxel.Pipeline.ReturnTrip` | Edits that applying a bucket again would undo (ores set back to their host), then a partial return (a final column stays while its neighbour unloads and reloads: its bucket aimed at the final column is dropped, never applied), a full return past the unload ring, and `DropEdits`: hashes equal the edited first visit (the unedited one after `DropEdits`) and every edit reads back with its stamp. |
| `Unnamed.Voxel.Freeze.Moved` | `FinishColumn(GenerateSnapshot)` equals `Generate` (blocks, biomes, all four heightmaps) around each preset's snowiest column and the origin, and the freeze did leave the snapshot. |
| `Unnamed.Voxel.Freeze.AfterDecoration` | A hand-made bucket from a neighbour places stone on a snowy top: with the freeze at Finalize snow lands on it; in S2's order the top stays bare (vanilla's MC-119599). |
| `Unnamed.Voxel.Decoration.Preview` | Since S3: streaming's final columns equal the decorated area's at two centers, for threaded, walked and seeded random-order loads. |
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
| `Unnamed.Voxel.Density.BlendedBatch` | `Tests/VoxelBlendedNoiseTests.cpp`: the batch `Sample` equals `Sample` point by point, bit for bit, for two sets of numbers, 4,096 positions each (block corners and fractional), in batches of uneven sizes. |
| `Unnamed.Voxel.Density.Ops` | The jar-confirmed op values of session 3 (`interval_select`, `gradient`, `interpolated`, `find_top_surface`). |
| `Unnamed.Voxel.Density.Column` | The column equals direct evaluation bit for bit on 3 presets x 3 columns x 300 blocks; skipped cells keep the sign. |
| `Unnamed.Voxel.Density.Terrain` | Runs caves off and on. Skipping on and off are equal on 25 columns per preset; row, reverse and parallel orders are equal; the bedrock, sea and air rules; `GetSurfaceHeight` and `ConfirmDry` agree with the column. |
| `Unnamed.Voxel.Density.Golden` | Block hashes per preset, seed 263, caves off, with biomes and surface rules. Re-recorded once for the rules (phase 5 session 5): overworld `0xf7ba992062080de1`, large_biomes `0x9cdb8dff3e065521`, amplified `0xab4971c1b4ee6520`. Since phase 6's S4, under the decided change "veins on": `0xa4b22a0e3536092f`, `0x648c3c2c4627e9d3`, `0xca86f4c1b74532ce`; veins off must still give the values before it. |
| `Unnamed.Voxel.Aquifer.Determinism` | Two aquifers with one seed fill a column identically (and flag the same KeepEmpty rows); a scratch's earlier columns do not change a column; another seed differs. Load order and threads are checked by `Density.Terrain` with caves on. |
| `Unnamed.Voxel.Aquifer.Fluids` | Over 98 overworld columns: no water below y -54 and open space there is lava, no lava at y -10 or above, water above the sea only under solid ground, and lava and walls exist. |
| `Unnamed.Voxel.Density.LoadOrder` | In `VoxelAquiferTests.cpp`: 5 x 5 columns per preset with caves, generated in two load orders (row by row; backwards across the workers). Each order starts cold (`ResetCaches`), so the corner cache is shared differently in each. Mismatches 0 and equal combined hashes. |
| `Unnamed.Voxel.Density.SharedCorners` | Corner sharing changes no value: 5 x 5 columns per preset, caves on, in three orders (row by row, backwards, shuffled), each starting cold, have densities bit-identical to a terrain without sharing, and corners were shared. Then the cache alone: found values are the stored ones bit for bit, a corner column a window away evicts, negative positions work, positions inside a column are never kept, and the table stays within 16 MiB. |
| `Unnamed.Voxel.Aquifer.Golden` | Caves-on block hashes at the `Density.Golden` positions (seed 263), with biomes and surface rules. Re-recorded once for the rules (phase 5 session 5): overworld `0x22523b7400c2f38e`, large_biomes `0x6b19b1927b73266a`, amplified `0x863f9b8b01468a3f` (phase 4: `0xe9376e319a221c4c`, `0x9e26565ae0dc0258`, `0x3e5329bd4ce985c4`). Since phase 6's S4, veins on: `0x06587391fe15bb98`, `0x6bef4434e7dee57c`, `0x82816e657968c3f1`; veins off must still give the values before it. |
| `Unnamed.Voxel.Climate.Presets` | Climate values of the three presets: goldens (each parameter weighted by its index, so swapped parameters fail), shared column edges, the PV formula. |
| `Unnamed.Voxel.Climate.Threads` | 8 threads in shuffled order match the single-threaded result. |
| `Unnamed.Voxel.Biome.Read` | `Tests/VoxelBiomeTests.cpp`: the overworld list reads as 56 biomes and 7,594 points and validates; every biome has a point; every point equals its row of `facts/biome_parameters.json`, read without the reader; spot values of six biomes (colours, temperature, downfall, modifiers), and only the two frozen oceans are frozen; `ReadBiomes` reuses the ids a table holds. Seven malformed small roots are refused, each error naming the file and the field. |
| `Unnamed.Voxel.Surface.Read` | The overworld tree reads as 231 rules and 107 conditions, with the pinned count of each of the 5 rule ops and 10 condition ops, validates, and has 37 named rules and 7 named conditions (`under_ceiling` is not read). What it names resolves: its `noise_threshold` noises exist, its two ore veins' density functions resolve, its 29 biomes are among the list's 56, and its 32 block ids are in the `minecraft:` namespace. Eight malformed small roots are refused with the file and the field. |
| `Unnamed.Voxel.Surface.RoundTrip` | All 44 named rule and condition files (37 and 7), written back by `WriteSurfaceRule` and `WriteSurfaceCondition`, equal their files in the reference. |
| `Unnamed.Voxel.Surface.Conditions` | `Tests/VoxelSurfaceTests.cpp`, the scout's cases (facts section 2): the column scan (C1 to C11: depth above and below, water height) through marker trees, `stone_depth` S1 to S14, `y_above` Y1 to Y11, `water` W1 to W9, `noise_threshold` N1 to N8, steep T1 to T9 (slopes rising south or west only, neighbours clamped, the 39-cell plateau), hole, the preliminary surface, the vertical gradient (frequency, repeats, biome-free) and the per-block biome, with the shortcut's layer masks on and off. |
| `Unnamed.Voxel.Surface.Rules` | The whole overworld tree on the jar's own columns (facts 2.8, its noise values injected): 16 biomes, four profiles, two columns each, 41 blocks top down, shortcut on and off. Badlands are compared down to where our own bands start. |
| `Unnamed.Voxel.Surface.Snow` | Temperature: the 30 values of facts table 3.3, 0.15 exactly at old-growth pine taiga's Y 200 (not cold), the wobble within 8, the frozen modifier's two values. The freeze pass on hand-made columns: snow and snowy grass, ice, nothing on lava, ice, packed ice or powder snow, nothing in a warm biome; the taiga snow line (no snow at Z 150, snow at 175). |
| `Unnamed.Voxel.Surface.Exact` | Three presets, six columns: the shortcut on and off give the same blocks; rules off differ; the surface has dressed blocks. |
| `Unnamed.Voxel.Surface.OreVein` | Phase 6, S4, against `facts/ore_vein_samples.json`. (a) Fed each sample's toggle and mask, our copper and iron densities and richness equal the jar's within 2.4e-7 (0 of 5,575 differ); mask is -1 at all 1,418 samples with toggle in [-0.4, 0.4). (b) No recorded vein block where its density is 0 or below, no ore where the gap is above 0, and our rule gives neither. (c) Shares within 4 sigma: the jar's fill 68.7% against 0.7, its ore share 22.7% against the mean richness, its raw ore 14 of 625 against 0.02, and the same three with our draws. (d) 1,089 columns of seed 263 (223 with veins, 37,124 vein blocks): every vein block in its vein's heights; on the four richest columns (two with a block at a vein's end height), the vein shortcut and the surface shortcut on and off give the same blocks. |
| `Unnamed.Voxel.OreLooks.Classes`, `.Host`, `.Words`, `.Mesh`, `.Merge`, `.Texture` | The ore looks (render-only, [Ore looks](#ore-looks-phase-6-look-calls-l1-and-l2)): the ore and rock classes, no real overlay code that reads as a look word; ten host neighbourhoods, ties keeping vanilla; the look words below 2^15 and the manifest's 320 slots against `GetSlot`; on the decorated radius-4 area of seed 263, OreGlint alone changing only ore faces' overlay channel, OreHost keeping every layer's face area and its words in the right slots; two ores walled in different rocks splitting their merged top into their own hosts' words; `T_OreLooks` against the manifest and the strip, and `M_ChunkOpaque` sampling it. |
| `Unnamed.Voxel.Tints.Colormap` | `Tests/VoxelTintTests.cpp`: both colormaps read (65,536 colours); every in-range grid row of the scout's `colors.json` matches `LookupColormap` for grass and foliage. |
| `Unnamed.Voxel.Tints.Biomes` | Every overworld biome's water, grass and foliage against the jar's `per_biome` (swamp grass aside), the facts' spot values, the dark forest formula, and the fallback for other sources. |
| `Unnamed.Voxel.Tints.Swamp` | Swamp and mangrove grass at 22,801 positions against the literal rule (info noise at 0.0225 below -0.1); both colours occur. A threshold of -0.05 fails it. |
| `Unnamed.Voxel.Tints.Blocks` | Grass block, oak, spruce and birch leaves, water and stone take the right tint source and colour. |
| `Unnamed.Voxel.Tints.Mesher` | Real columns at a biome border: every up-face cell's tint at blend 2 and 0 equals an independent per-block computation; blend -1 gives the plains tints; the shortcut off gives the same mesh; with two neighbours missing, blocks there take the centre column's nearest block. Blend radius 1 run as 2 fails it. |
| `Unnamed.Voxel.Biome.Lookup` | `Tests/VoxelBiomeSourceTests.cpp`: four depth groups. All 5,120 samples of `facts/biome_lookup_samples.json`: the pruned search, and our brute force, equal the jar's brute-force answer (point and fitness agree too). The 95 samples where the jar's indexed search differs are all ties of equal fitness. A fixed source answers its biome and fills every quart with it, and refuses a biome outside the table. Every biome has its own map colour, and the on-screen biome views start off. |
| `Unnamed.Voxel.Biome.Exact` | Three presets, the `Density.Golden` columns: every quart of the pruned fill equals brute force. The column's depths equal `SampleDepth` bit for bit, and a target sampled at a quart (the maps' and readout's path) finds the biome the column stored. |
| `Unnamed.Voxel.Biome.Jitter` | 5 x 5 columns filled forward on one thread and backwards on the workers are equal. Each column's ring equals its neighbours' own quarts. Every block takes one of its cell's 8 corners, and some take another than their own, which moves borders. The jitter repeats and stays within 0.45 quart. Through the terrain, the column stores the source's biomes and reads them per block; biomes off leaves it without biomes and with the same blocks. |
| `Unnamed.Voxel.Decoration.*`, `Unnamed.Voxel.Providers.Distribution` | `Tests/VoxelDecorationTests.cpp` (phase 6 S2, 13 tests, about 12 s): `Merge` (the 9-way merge against a stable sort, 300 random cases); `RuleTests` (the seven rule tests, and iron's targets by host and Z); `Providers.Distribution` (80 providers against the jar's histograms, two-sample chi-square); `OreProbes` (our blob in the probe's layered column against all 24 probes and 4 bands, 2,000 blobs each; chi-square bounds scaled by the blobs' clustering); `Oracle` and `Preview` (the oracle and the preview equal the area and a scheduled run; the preview also through a 25-column cache built in parallel); `LoadOrder` (22 schedules: row-major, reverse, spiral in and out, checkerboard, all Decorates first, Finalize as soon as ready, with 1, 2 and 30 workers and random delays, plus one with Generate jobs: one hash); `Reach` (0 cells dropped over 1,089 Decorates per preset); `Golden` (R 4, seed 263, "ores on": `0xa28113883ba6e7ca`, `0xf87e27cd1455c61a`, `0xfc7c6279d46e6b76`); `SaltStability` (removing `ore_granite_lower` leaves every other feature's writes outside its cells unchanged); `OreHistogram` (blocks per Z of five ores against their height providers, caves and discard off; totals within 1%); `HeightmapTags` (our motion rule against the heightmap tags: one known disagreement, powder snow); `Order` (the run order keeps every biome's list order). A switch at the top, `DecorationTestMutation`, makes scout C's four mutations; each fails its named checks (arrival-order merge: `LoadOrder`, `Oracle`; a neighbour's decorated state: `LoadOrder`, `Oracle`; one shared stream: `SaltStability`, `Golden`; swapped order indices: `Golden`, `Order`). |
| `Unnamed.Voxel.Plants.*` | `Tests/VoxelPlantTests.cpp` (phase 6 S8). `Properties`: block properties and the registry's state layout. `Survival`: the survival table per kind and the sturdy faces. `LiveHeightmaps`: writes raise the live maps and leave the `_WG` ones. `PatchCounts`: scout I's oracles at 2,000 origins, bounds about 4.5 standard errors. `Columns`: block_column's trim. `Replay`: seed 263's real writes, replayed alone, land on their snapshot and are refused (and counted) when changed under them (support removed, cell taken, a pair's upper cell taken, a disk's target replaced). Since S8, `Decoration.Golden` and `Mesher.Digest` check both sides of `voxel.Decoration.Plants` (off: the old values; on: [Plants and patches](#plants-and-patches-phase-6-s8)). Mutation 5 (supports_vegetation empty) fails `Golden`, `PatchCounts` and `Survival`; mutation 6 (the live-heightmap raise frozen) fails `LiveHeightmaps`. |
| `Unnamed.Voxel.Trees.*` | `Tests/VoxelTreeTests.cpp` (phase 6 S9a). `Shapes`: each built configuration, 2,000 trees on its probe's layered column against the probe's 100 (`facts/feature_probes/tree_*.json`): the leaves' distance (the BFS exactly, persistent=false), the probe's blocks only, one 26-connected trunk and a straight trunk's height range exactly; logs, leaves and cells within 5% unless |z| <= 3; the top-log histogram (a straight trunk's, ours and the probe's each against the data's exact distribution); leaf occupancy per cell by an exact binomial test, Bonferroni-adjusted (fails below 0.01); reach at most the probe's; 6-connected parts by chi-square. `FeatureSize`: the ceiling probes' placed rates and top logs (straight trunks at the ceiling minus 3 or lower, others minus 2). `LeafDistance`, `Borders` (the real Decorate, merge and Finalize in five schedules: corner trees whole, touching pairs keep each other's logs), `WaterDepth`, `EnvironmentScan`, `Sapling`. `TreeTestMutation` (1 base_height + 1, 2 radius + 1) and `TreeTestCalibration` (self-samples scored as probes) are for manual runs; the bounds, their calibration and the mutations are in [the S9a record](Records/2026-10-10-Voxel-Phase6-S9a.md). Since S9a, `Decoration.Golden` has three cases (plants off; plants on, trees off; both on). S9b: 31 probes and 26 ceilings (dark oak, pale oak, mega jungle); the alter_ground rule (soil under a giant trunk exact, out of the block set, until S9c); `Mesher.Digest` has a trees-off case again (S8 part B's). |
| `Unnamed.Voxel.Clock.*` | `Tests/VoxelClockTests.cpp` (water and sky DN1, 6 tests, from scout N 5 (c) and (f)). `Ticks`: 20 game ticks per second at 30 to 240 fps, the partial tick carried, a new world at tick 0. `DayClock`: rate 3 and 0.5, pause and `advance_time` off stop the day but not the game tick, the moon phase changing exactly at each multiple of 24,000, the F3 line. `Commands`: `voxel.Time`'s replies (`add 25000` from 2 gives 25002, `query day` the total modulo 24,000), the time units and refusals. `Markers`: forward to the next match, never back. `Cap`: a 1 s hitch runs 5 ticks and drops 15. `SavedWorld`: a file from before DN1 loads with no clock and writes back byte for byte; the `clocks` key round-trips with defaults left out; other clocks are ignored; a newer version and broken clocks are refused; rename and touch keep the clock. |
| `Unnamed.Voxel.Features.Read` | `Tests/VoxelFeatureTests.cpp`: the decoration data reads with 210 placed features, 182 features, 8 providers, 3 carvers and 127 tags (every file in those folders, every code tag), and each of the 56 biomes' step and carver lists equals its file. Logs the read time. |
| `Unnamed.Voxel.Features.Strict` | 14 broken files (an unknown field at the top and deep in a list, an unknown type, a type or a heightmap word in another case, a string or 9.5 for an int, a missing field, a bad id, a feature or a tag file missing, a bad tag value, an anchor with two fields) each fail the read, and the error names the file and the field path. The reader accepting unknown fields fails it. |
| `Unnamed.Voxel.Features.RoundTrip` | Every placed feature, feature, provider, carver and tag written back equals its file as JSON (530 files). |
| `Unnamed.Voxel.Features.Order` | Hand-made lists sort to a known order (the smallest free id first) and contradicting lists are refused with the step named. On the reference: all 11 steps sort, every list's order holds and the smallest-free rule, replayed in the test from the lists, gives the same order; 171 features as 0, 4, 5, 4, 4, 0, 34, 7, 3, 109, 1. A flipped tie-break fails it. Against vanilla's order (`facts/feature_order.json`): every step holds the same features, and all 3,479 pairs the lists constrain (directly or through other features) come in the same order; 918 unconstrained pairs differ and are reported, not failed. Swapping two constrained features in vanilla's list fails it. |
| `Unnamed.Voxel.Features.Seeds` | Feature salts (UTF-8 included), the decoration and pattern salts, stream and pattern seeds, pinned against an independent Python implementation; the world seed moves the patterns. A changed salt fails it. |
| `Unnamed.Voxel.Biome.Golden` | Biome hashes at the `Density.Golden` positions (seed 263), recorded once after the gates passed. Quarts: overworld `0xe3013e9a963d42fe`, large_biomes `0x6d37af6332445934`, amplified `0xbdc71ddbc5a89adf`. Jittered blocks on every seventh layer (these pin the jitter): `0x9aa40aa00117a819`, `0x83db5e7e69587903`, `0x6bbcb6f996e2a018`. |
| `Unnamed.Worlds.TerrainCheck` | `Tests/WorldsTests.cpp`, outside `TestVoxel.ps1`'s 79: `VoxelTerrainFactory::Check` builds all three generator-2 world types from the reference. With an empty folder instead, a generator-2 world fails with an error naming its preset, while generator 1 and Superflat need no reference. With the reference's data but no facts, every generator-2 type fails naming `biome_parameters.json`, and a saved world gets no terrain. Without `material_rule/`, every generator-2 type (Single Biome too) fails naming it. Single Biome builds with desert, frozen peaks and deep dark, and refuses `minecraft:the_void`, naming it. A saved world whose preset cannot be read gets no terrain, but the dev seed falls back to the stand-in and says why. Each world type reads its own preset (Single Biome reads `overworld`). |

Aquifer mutation check: turning global lava into water, and disabling the walls, fail `Aquifer.Fluids` (3 checks) and `Aquifer.Golden` (3).

Biome mutation check: ties going to the last point, in both searches, fail `Biome.Lookup` (173 samples). Another jitter salt fails `Biome.Golden`'s three block hashes; `Biome.Jitter`'s structural checks pass it, as they should.

A deliberate off-by-one in `GetSectionsTouching` fails `Edit.Sections` (13 errors) and `Edit.Remesh` (11). Zeroing spline derivatives in `BuildSpline` and negating the reader's constants fail `Worldgen.Splines` (6 errors), `Worldgen.RoundTrip` (34) and `Worldgen.Presets` (1). Swapping `lerp`'s order and `shift_b`'s axes fails `Climate.Ops` (4 errors) and `Climate.Presets` (3). Making `interval_select`'s thresholds exclusive and swapping the column's x/z interpolation weights fail `Density.Ops` (3 errors), `Density.Column` (3) and `Density.Golden` (3).

## Known gaps

- **Decoration (S2):** Decorate's 0.6 ms allowance is not met on large_biomes (0.760 ms) and amplified (0.686 ms) ([Decoration](#decoration), the gate); with it, amplified's whole column sits at the 5 ms limit within noise (S3's window: 4.79 old, 5.04 new), an item for a later speed-up session. Only the ore feature type is built (141 placed features skipped).
- **Plants and patches (S8):**
  - **AMPLIFIED's whole column** read over 5 ms in S8b's window for every build, old included (one bench per process: old 5.594, plants off 5.392, plants on 5.499). By stages S8 adds about 0.28 ms. Jesus has ruled out a speed-up session for now; how to read the limit (plan 3.5, Q11) stays open.
  - **The replay re-checks only the vertical support.** A neighbour's write beside a cactus or sugar cane is not re-checked.
  - **`Finalize`'s refused-write count** is approximate when two features place the identical two-cell plant in one cell (stats only).
  - **Kelp's trim:** scout I's POOL4 probe gave a free run one cell shorter than our reading. Re-measure at depths 3 to 6 if kelp looks wrong.
  - **Snow layers** of two or more are sturdy only on their bottom face, until the measured sturdy-face table.
  - **Some plant rules come from the wiki, not a probe:** leaf litter and sea pickles need a sturdy top; carpets need anything but air below; red shrub follows supports_vegetation; mushrooms ignore light during generation.
  - **Jesus's answers (October 10):**
    - glow lichen's multiface_growth, its search clamped to the 3x3 window (Q3), is S8's part B, next in the main lane;
    - the five modifiers with no S8 user, the unused predicates, and rotated / random_block come with their first users (Q2);
    - the seven later plants stay off until their own sessions (Q1, `bLaterPlants`);
    - sturdy faces will be measured from the game into one table for S8 and the torch (a follow-up);
    - plant sun shadows at every distance are accepted as they are;
    - there is no AMPLIFIED speed-up session for now.
- **Trees (S9a):**
  - **Not built:** mega spruce and mega pine (S9c, a refit of the crown radius), mangroves and azalea, and every tree decorator, fallen trees, huge mushrooms and bamboo. Since S9b dark forest, pale garden and jungle have their big trees; old growth taiga still lacks about a third (`C:\dev\sandbox\phase6s9\s9a\tree-table.md`, S9a's table). Without alter_ground, giant trunks (when built) stand on dirt, without the podzol ring.
  - **Saplings' survival** follows the wiki's rule (the block below in supports_vegetation) until saplings are in the block reference.
  - **The occupancy test's reach:** at fancy oak's scale (about 1,000 cells) the Bonferroni bound catches a cell emptying but not a thinning of about 11% in one cell (mutation h1 failed 1 of the 6 fancy probes).
  - **Acacia's logs** run 5.6% over the reference probe (z 2.6, inside the bound); that probe's trunks also run under the data's exact mean.
  - **The flight with trees still has holes (S9b, partial gate, the manager's call):** after S9b's levers, holes in 40 and 42 frames (at most 5 to 8 columns, all in the circles) against none with trees off; frames over 2 ms 3 and 4 (trees off 1 and 2); first view 3.4 s against 2.1 s. What is left is apply throughput: a tree column of 5,000 to 7,000 quads takes about 0.9 ms to apply (about 0.33 ms fixed plus 0.09 ms per 1,000 quads), so one fits per frame. Next levers, for a streaming session: RealtimeMesh's commit off the game thread or a column's apply split across frames, and trimming the stream's bytes per vertex. The extra memory (about 0.2 to 0.4 KB per on-screen quad, RealtimeMesh's stream copies) is explained and flat over laps. [The S9b record](Records/2026-10-10-Voxel-Phase6-S9b.md) has the runs.
  - **The S9 bench gate (S9b):** Decorate with trees is within 0.4 ms on every preset (overworld +0.395, at the edge). Finalize is over 0.3 ms on overworld (0.363) and amplified (0.380), mostly from before the trees: trees off is 0.268 to 0.370, against about 0.18 in S8b's gate; likely the light work merged from T3/T3b (unverified), for a streaming or small-fixes session. Amplified's whole column rises to 5.36 ms: a flag, an FYI for Jesus (he ruled out an AMPLIFIED speed-up session for now).
  - **"More grand trees"** (Jesus): vanilla's grand trees first; bigger shapes or more of them are parked, his call later.
- **Streaming stages (S3):** a flight on a busy machine had holes in 3 frames of its first lap (none on a quiet one). The flight's laps never reload a column beside a final one, so only `Pipeline.ReturnTrip` covers that case. Buckets aimed at rings 2 and 3 are held until their source unloads (bounded by the rings).
- **The 5 ms limit:** since S0 every preset's column mean is under it ([Column density](#column-density-and-terrain-generator-2), "Speed-up session S0"). Phase 6's decoration will add to it (the plan's allowances, section 3.5). Levers still untried:
  - the entrance and noodle noises at corners whose cells sloped_cheese's upper bound already proves empty;
  - conditional noodle corners;
  - the aquifer's 27-centre search;
  - the surface tree's biome tests.
- **Surface rules** ([Surface rules](#surface-rules-temperature-and-snow)): the stage adds 0.6 to 0.9 ms (p50) per column. No eroded-badlands hoodoos or frozen-ocean icebergs (code-made in 26.3; decided out of phase 5). Ore veins since S4 ([Ore veins](#ore-veins-phase-6-s4)). The freeze pass takes block light as 0; since S3 it runs at Finalize, after decoration.
- **Lighting limits:** faces hidden behind a same-facing face inside one card stay black in Lumen's scene, and software Lumen cannot see the chunks ([Lighting](#lighting)).
- **Edge fog:** its color is fixed to the map's sun (there is no day cycle yet), and a faint seam remains on the sun side. Columns still arriving inside the fade show up late: in the session 5 flight (2,392 frames, at most 35 at a time), one frame each, when crossing a column corner moves the rings diagonally.
- **Pale water:** seen from high above, water over sand looks pale even with the biomes' water colours (phase 5 session 6); the water material decides it.
- **Frozen-ocean checkerboard:** where ice covers water, merged squares under the ice read lighter or darker from run to run: translucent ice over translucent water, sorted per merged quad. It is there with tints off too, so it came with session 5's freeze pass. A translucency-sorting fix is its own task.
- **Fluids:** generated water counts as water for the player, but it does not flow: carving into water leaves air pockets.
- **First view:** the view is on screen 3.3 to 3.6 s after the first frame with generator 2 and caves (2.5 to 2.6 s before caves; 0.5 to 0.6 s with the stand-in).
- **AMPLIFIED skipping:** bounds settle only 9% of its cells (loose where the factor is small), against the overworld's 54%. Its corners settle another 76% of cells, but those corners must be evaluated first: about 757 per column, against the overworld's 390.
- **Caves near the limit:** corner sharing (phase 5, session 3) brings caves-on columns under 5 ms in the bench's best case (overworld 3.76 to 3.79, large_biomes 3.92 to 4.64, amplified 4.28 to 4.64 ms). Without sharing, overworld 4.13 to 4.37, large_biomes 4.56 to 4.58 and amplified 4.96 to 5.24 ms are near or over the limit, and the in-game figure lies between. Biomes add 0.19 ms (session 4); the surface rules will add more. The normal noise is batched since phase 5 session 6, and S0 took a further 1.9 to 2.2 ms off the whole column (see "The 5 ms limit" above). Still open: the flight's frames over 2 ms, and inner rings first.
- **Biome stage:** 0.19 ms per column. Its climate pass (about 0.05 ms) is shared neither with the density's corners nor with neighbouring columns.
- **`voxel.Debug.Climate`** on screen is unconfirmed since session 3.
- **Still lava:** it has no damage and no flow, and it counts as water for the player's movement.
- **Snow layers:** placement gives one layer (no stacking); eight layers collide as a full cube (26.3 collides at 7/8); the creative item icon draws as a cube.
- **Lava in a cave clips:** with eye adaptation, 36% of the `cave-lava` view has a channel at 255; the lava reads yellow-orange with its texture still visible ([Lighting](#lighting), "Cave exposure and glow").
- **PS biased low:** the spawn estimate rounds to 8-block cells and ignores the 3D noise.
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
- **World choices still to land:** Hardcore has no permadeath or hardcore hearts; Single Biome refuses a biome outside the overworld list (Jesus's call between refusing and a plains fallback is pending); Superflat refuses layers of blocks the game lacks.

The registry has full cubes, slabs, powder snow, snow layers, still water and still lava. The generator also needs:

- vines and pointed dripstone (grass, flowers, ferns, cactus, sugar cane, kelp and seagrass are placed since phase 6's S8);
- dirt paths and farmland;
- stairs, fences, doors and panes, for villages.

Each is listed in Status before the phase that needs it.
