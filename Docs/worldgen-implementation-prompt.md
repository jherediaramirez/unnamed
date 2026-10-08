# Prompt: build our Minecraft-style world generator in Unreal Engine 5 (C++)

How to use: start a new chat on the project and paste everything below the line as the first message, or attach this file and say "follow this". If you can, also attach a Markdown or PDF export of the research doc "Minecraft Java 26.3 world generation research" as the detailed reference.

---

## Who you are and what we're building

You are a senior C++ engine programmer pairing with me on our Unreal Engine 5 game, "Unnamed" (first-person; the main menu is done and first-person movement is in progress). Build an infinite, seeded voxel world: chunk streaming, terrain, caves, biomes, ores, vegetation and pre-built structures. Use Minecraft Java Edition 26.3 as the reference for how this kind of generator works, then do better: faster loading, more beautiful mountains, and settlements that never get split in half by cliffs.

Work in small phases. At the end of each phase the result must compile, run in the editor, and be measurable. Do not start the next phase until I confirm.

## First, before any code

1. Inspect the project: engine version (expected UE 5.x, possibly 5.8), the game module and its Build.cs, enabled plugins (look for RealtimeMeshComponent), the existing character and controller setup, and source control.
2. Tell me what you found, then propose the module or plugin layout, folder structure, and class list for Phases 0 to 2.
3. Ask only questions that block Phase 0. For anything else, state your assumption and continue.

## Ground rules

- Original code only. Treat Minecraft as a reference for mechanics and numbers. Never paste or translate decompiled Minecraft code, and use no Minecraft assets, textures or names in the game.
- Deterministic: the same seed must produce the same blocks regardless of chunk load order, thread timing, view distance or frame rate. Every random value is a pure function of (seed, subsystem salt, coordinates). No shared random stream across chunks, no time-based seeds in generation, no dependence on hash-map iteration order.
- We do not need to match vanilla Minecraft worlds seed for seed; we need Minecraft-like behavior. Our own design wins over vanilla parity.
- Threading: worker threads produce plain C++ data only. UObjects, components, actors and render state are created or changed only on the game thread. Use UE::Tasks for the job graph.
- Data-driven: blocks, biomes, features, ores, structures and tuning live in data assets or data tables, not hard-coded enums. Mojang renamed or replaced many worldgen node types between 26.1 and 26.3, so design for change.
- Keep the core (RNG, noise, density, biomes, meshing, storage) in plain C++ with no UObject dependencies, so it runs on any thread and in automation tests.
- Instrument from day one: a stat group, Unreal Insights trace scopes (TRACE_CPUPROFILER_EVENT_SCOPE), and console variables for radii and budgets. Every phase reports timings.
- Keep files focused and small; comment the why, not the what.

## Reference: how Minecraft Java 26.3 does it (condensed)

### World and storage
- A chunk is a 16x16 column from Y -64 to 319 (384 blocks), stored as 24 sections of 16x16x16. Sea level is 63 (the top water block is Y 62).
- Bedrock is solid at Y -64 and thins out by Y -59. Deepslate replaces stone at Y 0 and below, mixed with stone up to Y 7. Below Y -64 is void; void damage starts at Y -128.
- A section stores a palette of the block types it uses plus packed indices (at least 4 bits each). A single-type section (all air, all stone) has no index array.
- Biomes are stored per 4x4x4-block cell (64 per section).
- Heightmaps per column: WORLD_SURFACE (highest non-air), OCEAN_FLOOR (highest movement-blocking block, ignoring fluid), MOTION_BLOCKING (movement-blocking or fluid), MOTION_BLOCKING_NO_LEAVES, plus worldgen-only variants used to place structures and features. Stored as y - minY + 1, with 0 meaning none.
- Region files hold 32x32 chunks in 4 KiB sectors behind an offset table, so one chunk is one seek and one read. Compression is zlib, LZ4 or none.

### Generation stages
Each stage runs only when its neighbors have reached the stage it reads:
empty -> structure_starts (lay out structure pieces, no blocks) -> structure_references (needs starts 8 chunks out) -> biomes -> noise (needs biomes 1 out) -> surface -> carvers -> features (needs carvers 1 out; may write one chunk into each neighbor) -> initialize_light -> light (1 out) -> spawn -> full.

### Randomness
- The seed is a signed 64-bit integer. Numeric text parses directly; other text is hashed (vanilla uses Java's 32-bit String.hashCode; we will use a 64-bit hash).
- Noise uses Xoroshiro128++. The seed is expanded to 128 bits with a SplitMix64-style mixer, and each named noise mixes in its own constants. Per-position randomness hashes (x, y, z) into the base state.
- Vanilla structure regions (rx, rz) are seeded from rx*341873128712 + rz*132897987541 + seed + salt. Features use a per-chunk decoration seed, then add featureIndex + 10000*step.

### Terrain
- final_density(x, y, z) > 0 means solid; otherwise aquifers decide water, lava or air.
- Climate noises: continentalness (ocean vs inland), erosion (rugged vs flat), weirdness, temperature and humidity. Peaks-and-valleys PV = 1 - abs(3*abs(weirdness) - 2). Depth grows about 1/128 per block below the surface.
- Splines map (continentalness, erosion, PV) to offset (base height), factor (vertical stretch; low means tall terrain) and jaggedness (sharp peaks). Roughly: depth = offset + a gradient from +1.5 at Y -64 to -1.5 at Y 320; terrain = 4 * quarterNegative((depth + jaggedness * halfNegative(jaggedNoise)) * factor) + base3DNoise.
- Caves: cheese (big caverns), spaghetti (long tunnels), noodle (1-5 blocks wide) and pillars, combined with min() so open space wins. Classic carvers (worm caves, canyons) run later.
- Aquifers: cells of about 16x12x16 with random fluid levels; lava below about Y -54; stone barriers between different levels.
- Speed: density is evaluated only at cell corners (vanilla historically 4 wide by 8 tall) and trilinearly interpolated; 2D inputs are cached per column. 26.3 evaluates density in 32-bit float.

### Biomes
- Each biome is a box in six dimensions (temperature, humidity, continentalness, erosion, weirdness, depth) plus an offset. The nearest box wins: the sum of squared distances outside each range, plus the offset.
- Band edges: temperature -1 / -0.45 / -0.15 / 0.2 / 0.55 / 1. Humidity -1 / -0.35 / -0.1 / 0.1 / 0.3 / 1. Continentalness: mushroom -1.2 to -1.05, deep ocean to -0.455, ocean to -0.19, coast to -0.11, near inland to 0.03, mid inland to 0.3, far inland to 1. Erosion splits at -0.78, -0.375, -0.2225, 0.05, 0.45, 0.55. PV: valley below -0.85, low to -0.2, mid to 0.2, high to 0.7, peak above.
- Cave biomes live at depth 0.2 to 0.9 (about 25 to 115 blocks down); a deep-dark-style biome sits at depth about 1.1 with low erosion.
- Weather temperature: base biome temperature minus about 0.00125 per block above Y 80. Below 0.15 it snows and still water freezes, which creates snow lines.

### Surface and decoration
- Surface rules are a per-column decision tree (sequence, condition, block) with tests such as biome, noise threshold, vertical gradient, Y above, water, stone depth, above preliminary surface, hole, steep, temperature and not.
- Features are placed in 11 ordered steps: raw generation, lakes, local modifications, underground structures, surface structures, strongholds, underground ores, underground decoration, fluid springs, vegetal decoration, top layer modification. Placement modifiers: count, in square, height range, heightmap, biome filter, rarity, surface-relative threshold, environment scan.
- Ore placements (Y range, shape, attempts per chunk): coal 0..192 triangular x20 and 136..320 uniform x30; iron -24..56 triangular x10, -64..72 uniform x10, 80..384 triangular x90; copper -16..112 triangular x16; gold -64..32 triangular x4, plus 32..256 uniform x50 in badlands; redstone -64..15 uniform x4 and -64..-32 triangular x8; lapis -32..32 triangular x2 and -64..64 uniform x4 (buried); diamond peaking at -64 and gone by 16, x7; emerald -16..480 triangular x100, mountains only. Several ores discard 50 to 100% of blocks exposed to air. Large copper and iron veins come from a vein noise in the density stage, not from features.

### Structures
- A structure set is weighted structures plus a placement. random_spread uses regions of `spacing` chunks with one candidate per region at least `separation` chunks from the edge, picked from region coordinates, seed and salt. Villages use spacing 34 and separation 8. Strongholds use concentric rings.
- Jigsaw assembly: a start pool, a max depth (villages 6), a max distance from center (villages 80 blocks), and a start height projected to the worldgen surface heightmap. Pieces have named connectors (name, target, pool, joint); bounding boxes may not overlap; a fallback pool caps branches. Pieces are rigid (houses) or terrain-following (paths).
- Terrain adaptation is a density term (the "beardifier") applied during the noise stage. Villages use beard_thin: build up under pieces, clear space above, a few blocks of falloff.
- The weakness to fix: there is no footprint-wide slope check, and houses take the ground height only where they attach to a path, so cliff sites split villages.

### Why vanilla loads fast
Work is staged by need (tickets and load levels; view distance separate from simulation distance). Sections are palette-compressed and noise is coarse-sampled. Worker threads mesh sections, which rebuild only when dirty. Rendering uses frustum culling plus cave culling (a per-section face-connectivity graph walked out from the camera), and lighting updates incrementally.

## Target architecture

Module: a runtime module or plugin named `VoxelWorld` (adjust to the project's conventions).

### Plain C++ core (no UObject, thread-safe, unit tested)
- `FVoxelSeed`: parse text to int64 (numeric text as-is; other text through a stable 64-bit hash such as xxHash64 or FNV-1a 64).
- `VoxelRandom`: SplitMix64, Xoroshiro128++, `Hash64(seed, salt, x, y, z)`, and helpers for unit floats and int ranges. Subsystem salts are compile-time hashes of names ("terrain", "caves", "ores", "structures" and so on).
- `FVoxelNoise`: a wrapper over FastNoise2 (preferred: MIT, SIMD) or a hand-written Perlin or OpenSimplex with derivatives; fBm, ridged and domain-warp variants; every noise seeded by salt.
- `FSpline1D`: cubic Hermite spline with nested-spline support, baked from data assets into plain arrays at load. Never read a UCurveFloat from worker threads.
- `FClimateSample` {temperature, humidity, continentalness, erosion, weirdness, pv, depth} and `FClimateSampler`.
- `FColumnData` per chunk column (16x16): climate, surface-height estimate, min and max possible solid height, biome hint, settlement mask, pad heights and flags. Computed once and reused by terrain, structures, spawning and far LOD.
- `FDensityField`: evaluates density at coarse cell corners (start with 4x8x4) with trilinear interpolation. Terrain term, plus caves via min(), plus the settlement term; skips cells outside the column's height bounds.
- `FAquiferSampler`; `FBiomeTable` (data-driven 6D boxes plus offset, nearest match, cached per 4x4x4 cell); `FSurfaceRules` (compiled tree); `FFeatureSystem` (placed features, modifiers, 11 steps); `FStructurePlanner` (below).
- `FChunkSection` (16x16x16, palette plus packed indices, single-value fast path) and `FChunkColumn` (sections, heightmaps, biomes, current stage, dirty flags).
- `FSectionMesher`: binary greedy meshing with a 1-voxel border copied from neighbors and per-vertex ambient occlusion; outputs packed quads or vertex and index arrays per material.
- `FRegionFile`: 32x32 columns per file, sector allocation, compression via FCompression (LZ4 or Oodle), a versioned header. It saves only player-edited columns as deltas; everything else regenerates from the seed.

### Unreal side
- `UVoxelWorldSettings` (UDeveloperSettings or a data asset): seed, radii, budgets and toggles.
- `UVoxelWorldSubsystem` (UTickableWorldSubsystem): owns the column map, streaming rings around the player, a priority queue (distance with a view-direction bias), hysteresis (unload radius = load radius + 2), cancellation of stale work, the game-thread apply budget, pooling, stats and console commands.
- One actor or component per chunk column using RealtimeMeshComponent (preferred), with one updatable section group per 16-block section; ProceduralMeshComponent is the fallback. Collision only within a small radius (3 to 4 columns), cooked asynchronously.
- One opaque material that indexes a Texture2DArray per face; a second material for translucent blocks (water, leaves) later.
- Props and foliage as Hierarchical Instanced Static Meshes (Nanite-enabled static meshes are fine there). Do not plan on Nanite for chunk geometry.
- Debug console commands such as `voxel.Seed <text>`, `voxel.Regenerate`, `voxel.Debug.Climate <param>`, `voxel.Debug.Stages` and `voxel.Debug.Biomes`, plus an on-screen readout of climate, biome and stage at the player.

### Job graph (UE::Tasks; prerequisites mirror the stages)
1. StructurePlan: per region, cached and deterministic; required before Column2D for any column in its range.
2. Column2D: climate, biome hints, height estimate, settlement mask and pads.
3. Noise: density to blocks, aquifers and caves.
4. Surface: surface rules, bedrock and deepslate.
5. Carvers.
6. Features: requires this column and its 8 neighbors at Carvers. Writes into neighbors go to per-neighbor outgoing buffers, merged into each neighbor in a fixed order (step, source column x, z, feature index), so results never depend on timing.
7. Finalize: a column's blocks are final once it and its 8 neighbors have finished Features and its incoming buffers are merged.
8. Mesh: per section, requires final blocks for the surrounding 3x3 columns (border faces and ambient occlusion).
9. Apply: game thread, within the per-frame budget.

Bound in-flight tasks (about 2x the worker count), and drop work for columns that left the unload radius unless a neighbor still needs them.

## Our improvements over vanilla (must-haves)

1. **Two-tier generation:** the 2D column pass is computed once and reused everywhere (terrain, biomes, structures, LOD).
2. **Settlement-first terrain:** plan structures before terrain density and bend the terrain around them.
   - For each placement region, try several seeded candidate sites (for example 8). Estimate each footprint and sample the 2D height estimate every 4 blocks over the footprint plus a 16-block margin.
   - Score on height range, mean slope, max slope, water fraction and biome. Reject sites over thresholds, keep the best, and if none pass, skip the region deterministically.
   - Write a settlement mask into the column data. Inside it, multiply jaggedness and erosion-filter strength by (1 - mask) so mountains fade out around settlements.
   - Fit a pad under each building (a least-squares plane with a small tilt cap, or flat) and blend density toward the pad with a smoothstep falloff ring about 6 to 10 blocks wide. Larger settlements on gentle slopes use terraces joined by stairs or ramps.
   - Suppress cave and aquifer densities under footprints.
   - Rigid buildings sit on their pads. Paths route with A* over the coarse height grid using a grade penalty and a hard maximum grade, and follow the terrain within that grade.
   - Foundations fill gaps of 1 to 3 blocks under building edges; deeper gaps get pillars or stilts.
   - Order: pads, then roads, then a final grade check after all terrain edits.
   - Validate after generation: each building's footprint must be at least about 90% supported; log and fix any violation.
3. **Better mountains:** a larger continental scale that uses the full height range; ranges from ridged noise along domain-warped ridge lines; erosion detail from derivative-damped fBm (each octave divided by 1 + the squared accumulated slope) and/or a stateless gully erosion filter applied to the 2D height before the 3D density; 3D overhang noise only on steep faces.
4. **Stateless counter-based randomness** everywhere.
5. **Binary greedy meshing** with compact output and per-vertex ambient occlusion; remesh only dirty sections and the neighbors whose border changed.
6. **Far LOD:** a separate heightfield LOD (quadtree, doubling cell size) built from the 2D column pass and drawn beyond the voxel radius, with skirts to hide seams.
7. **Delta saves:** only edited columns are written to region files.

## Phases and acceptance criteria

- **Phase 0, scaffolding:** module or plugin, log category, stat group, settings, console commands, automation test skeleton, RealtimeMeshComponent integration (or the fallback), and optionally a FastNoise2 third-party module. Done when the project builds and `voxel.Info` prints the settings.
- **Phase 1, deterministic core:** seed parsing, SplitMix64, Xoroshiro128++, salted coordinate hashing, the noise wrapper and the spline evaluator. Done when automation tests show identical outputs across runs and across 8 threads sampling in shuffled order, and noise throughput is logged.
- **Phase 2, data and meshing on a fixed area:** block registry (data assets with name, texture-array indices per face, opacity, collision, emissive), palette sections, columns with heightmaps, a simple 2D heightmap terrain, the binary greedy mesher with neighbor borders, the texture-array material, and a 9x9-column test area. Done when there are no missing or hidden faces at section and chunk borders and per-section mesh time is logged.
- **Phase 3, streaming:** the subsystem with rings, hysteresis, priorities, the UE::Tasks pipeline, bounded in-flight work, budgeted apply, pooling, near-only async collision and cancellation. Done when flying fast across the world shows no holes, the game thread never exceeds the apply budget on chunk work, and memory stays flat while circling.
- **Phase 4, terrain:** climate sampler, splines for offset, factor and jaggedness, coarse-cell 3D density with interpolation and height-bound skipping, sea level 63, bedrock and deepslate gradients, noise caves, basic aquifers, and debug color views per climate parameter. Done when oceans, plains, hills and mountains appear at sensible scales, caves exist, a seed reproduces exactly, and per-column generation time is logged.
- **Phase 5, biomes and surface:** the data-driven biome table (6D boxes plus offset), 4x4x4 storage with seeded border jitter, altitude temperature and the snow line, surface rule trees per biome, and ocean temperature variants. Done when the biome debug map shows plausible regions and borders and peaks above the snow line are snowy.
- **Phase 6, decoration:** placed features with modifiers and the 11 steps, ores per the table, trees and plants, lakes and springs, carver caves and canyons, deferred cross-column writes with a deterministic merge, and meshing gated on 3x3 finalization. Done when trees across column borders are whole, an ore histogram by Y matches the configured shapes, and regenerating the same area in a different order produces identical block hashes.
- **Phase 7, structures that fit the land:** structure sets with random spread, region-level cached planning before Column2D, site scoring, the settlement mask, pads or terraces, density blending, cave suppression, a jigsaw-style assembler for authored pieces (pieces with connectors stored as data assets), rigid vs terrain-following pieces, A* roads with grade caps, foundations and stilts, and post-validation. Done when, across 100 settlements on varied seeds, no building is less than 90% supported, none intersects a cliff face, and road grades stay within the cap.
- **Phase 8, mountains:** continental scale, warped ridged ranges, erosion detail, steep-face overhangs and settlement-mask attenuation. Done when before and after screenshots show clearly better ranges, there are no unintended floating islands, and generation time stays within budget.
- **Phase 9, far LOD:** a quadtree heightfield LOD from the column pass beyond the voxel radius, matching real terrain, with seam skirts. Done when visible distance is at least 4x the voxel radius at a stable frame time.
- **Phase 10, persistence and polish:** region files with delta saves and versioning, block editing that marks sections dirty and remeshes neighbors, and optional voxel light levels for gameplay (such as spawning) while rendering uses Unreal's lighting. Done when edits persist across restarts and save size scales with edits, not with explored area.

## Initial performance targets (tune with measurements)

- Game thread: at most about 2 ms per frame applying chunk results.
- Column generation (stages 2 to 6) on one worker: under 5 ms at first, aiming for 2 ms.
- Section meshing: under 0.2 ms per 16x16x16 section.
- Voxel view radius of 12 to 16 columns; collision radius of 3 to 4 columns.
- Memory: a typical column under about 64 KB with palettes.

## Testing

- Automation tests for RNG and hashing determinism, noise determinism across threads, palette packing and unpacking, mesher face counts on known shapes, biome lookup on known climate points, and full-area block hashes for a fixed seed generated in two different orders.
- A `voxel.Bench` command that generates N columns off-screen and reports per-stage timings.

## How to work with me

- Start each phase with a short plan: the files to add or change and why.
- Deliver complete files with paths (no fragments), and note any Build.cs or .uplugin changes.
- Tell me exactly how to test in the editor: the console commands and what I should see.
- End each phase with measured timings, known issues and what's next, then wait for my go-ahead.
- If something in the reference above conflicts with what you find in the engine, or you see a better design, say so and propose the change.
