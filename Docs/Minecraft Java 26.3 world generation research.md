# Minecraft Java 26.3 world generation research

Oct 8, 2026 · @Jesus Heredia-Ramirez

Minecraft Java 26.3 "Wilderness Bound" (released 15 Sep 2026) still uses the world generator Mojang introduced in 1.18. Climate noise picks biomes, a 3D density field shapes terrain, and fixed stages add surface blocks, caves, ores, plants and structures. The 2026 drops reworked the data format around that core, not the core itself.

Every random choice is computed from the seed, a salt and coordinates, so a seed builds the same world in any load order. Speed comes from staging work by need, palette-compressed 16x16x16 sections, coarse noise cells, worker-thread meshing and cave culling.

Villages split on cliffs because nothing checks slope across the whole footprint before building. Our Unreal Engine 5 version should plan structures first and bend terrain around them; the last sections show how, plus a faster pipeline.

## What 26.3 is and what 2026 changed

Java 26.3 is the third 2026 "game drop", released 15 Sep 2026. Mojang switched to year.drop numbering after 1.21.11 (Dec 2025), and every 2026 version needs Java 25.

| Version | Released | Changes that matter for world generation |
| --- | --- | --- |
| [26.3 Wilderness Bound](https://minecraft.wiki/w/Java_Edition_26.3) | 15 Sep 2026 | Dappled forest biome near cold biomes (poplar trees with orange, red or yellow leaves); abandoned camp structure in 18 biomes; shelf mushrooms and red shrubs; a large rework of the worldgen data format (below) |
| [26.2 Chaos Cubed](https://minecraft.wiki/w/Java_Edition_26.2) | 16 Jun 2026 | Sulfur caves biome inside hills and mountains; sulfur springs; `interval_select` replaces `weird_scaled_sampler`; large dripstone capped at radius 16 so it stays within neighbor chunks; experimental Vulkan renderer |
| [26.1 Tiny Takeover](https://minecraft.wiki/w/Java_Edition_26.1) | 24 Mar 2026 | First fully unobfuscated release; each dimension's files moved under `world/dimensions/minecraft/<dimension>/` ([PaperMC](https://papermc.io/news/26-1)); dirt block tag split |

The 26.3 rework, confirmed in the [density function history](https://minecraft.wiki/w/Density_function) and the [26.3 Overworld settings file](https://raw.githubusercontent.com/misode/mcmeta/26.3-data/data/minecraft/worldgen/noise_settings/overworld.json):

- Density functions now run in single precision (32-bit floats), not double.
- Cell size moved out of the noise settings and into the `interpolated` function (`cell_size_xz`, `cell_size_y`).
- `cache_2d`, `flat_cache`, `cache_all_in_cell` and `shifted_noise` were removed; `cache_once` became `cache`, and `noise` gained shift fields.
- `y_clamped_gradient` became `gradient`, `invert` became `reciprocal`, and math nodes such as `lerp`, `pow`, `sqrt` and `distance_to_point` arrived.
- The Overworld router now has eight inputs: temperature, vegetation, continents, erosion, ridges, depth, `chunk_surface_level` and `final_density`.
- Aquifer settings moved into their own `aquifers` object, ore veins became an `ore_vein` function, and surface rules became a separate `material_rule`.
- Noise definitions moved to an fBm-style config (base octave, octave count, amplitude modifiers), per the [Noise page](https://minecraft.wiki/w/Noise).

Unchanged: sea level 63, the Y -64 to 319 range, the climate parameters, the chunk stages and the region-file format. Mojang keeps renaming worldgen nodes, so we should design our own node graph rather than mirror theirs.

## How a chunk gets built

A chunk is a 16x16 column of the full world height that climbs through 12 fixed stages. Each stage runs only after its neighbors reach the stages it reads. Mid-way it is a "proto chunk" (data only, nothing ticks); at `full` it becomes a live chunk.

| Stage | What it does | Neighbors it needs |
| --- | --- | --- |
| empty | Creates a blank chunk | none |
| structure\_starts | Decides whether a structure starts here and lays out its pieces; places no blocks | none |
| structure\_references | Notes structures from nearby chunks whose pieces reach in | starts, 8 chunks out |
| biomes | Fills the 4x4x4 biome grid from climate noise | starts, 8 out |
| noise | Writes stone, water, lava and air from the density field, including noise caves, aquifers and structure terrain shaping | biomes, 1 out |
| surface | Swaps the top layers by surface rules (grass, sand, bedrock, deepslate) | biomes, 1 out |
| carvers | Cuts classic caves and canyons | starts, 8 out |
| features | Ores, trees, lakes, plants and structure pieces in 11 steps; may write 1 chunk into neighbors | carvers, 1 out |
| initialize\_light, light | Seeds and spreads sky and block light across borders | 1 out |
| spawn | Adds the first animals | biomes, 1 out |
| full | Converts to a live chunk | none |

Since 1.20.5-1.21, each stage is a declared step with direct and accumulated neighbor needs plus a block-write radius. Only `features` may write outside its own chunk, and only one chunk out; 26.2 shrank large dripstone so it fits that limit. The older `liquid_carvers` and `heightmaps` stages are gone.

Load order never changes the result: a stage reads neighbors only at known earlier stages, and all randomness comes from seed and coordinates. The stage list is confirmed from the [1.21.11 ChunkStatus mappings](https://maven.fabricmc.net/docs/yarn-1.21.11+build.4/net/minecraft/world/chunk/ChunkStatus.html) and [ChunkGenerationStep](https://maven.fabricmc.net/docs/yarn-1.21.11+build.4/net/minecraft/world/chunk/ChunkGenerationStep.html); the radii come from the game's code structure and should be checked in the 26.3 jar.

## Seeds and randomness

The seed is one signed 64-bit number, and every random choice is computed from it plus a salt and coordinates, never drawn from a shared stream.

- **Making the seed:** a whole number that fits in 64 bits is used as typed. Other text goes through Java's 32-bit `String.hashCode`, so text seeds reach only about 4.3 billion worlds. Blank means random ([Seed](<https://minecraft.wiki/w/Seed_(level_generation)>)).
- **Noise streams:** climate and terrain noise use Xoroshiro128++. The seed is expanded to 128 bits, then each named noise (temperature, erosion, aquifer and so on) mixes in its own constants for an independent stream ([Cubiomes](https://raw.githubusercontent.com/Cubitect/cubiomes/master/biomenoise.c)).
- **Positional randomness:** for any block, x, y and z are hashed into the base state to make a fresh generator. One position never depends on another being generated first.
- **Structures:** placement still uses the old Java linear congruential generator for compatibility. Region (rx, rz) is seeded with rx × 341873128712 + rz × 132897987541 + seed + salt.
- **Decoration:** each chunk seeds from the world seed, draws two odd numbers a and b, and uses (cornerX × a + cornerZ × b) XOR seed. Each feature then adds its index plus 10000 × its step.

Worked example ([Slime](https://minecraft.wiki/w/Slime)): slime chunks seed a Java Random with the world seed plus fixed multiples of cx², cx, cz² and cz, XOR 0x3AD8025F. The chunk spawns slimes when `nextInt(10)` returns 0, so 1 chunk in 10.

1.18 replaced the random generator, which is why pre-1.18 seeds now give different worlds. The structure and decoration formulas come from code knowledge, not a page we read. For our engine, the cleaner form of the same idea is a counter-based hash: value = hash(seed, subsystem, x, y, z).

## Terrain shape: density, climate and splines

Each block gets one number, the final density. Above 0 is solid stone; 0 or below is open space that aquifers fill with water, lava or air.

&#91;embedded content: Overworld terrain density pipeline · 1.18+ design, 26.3 node names\]

Three climate noises set height and roughness through splines; 3D noise adds overhangs, caves are cut in with a minimum, and aquifers fill what is open.

- **Settings (26.3 file):** sea level 63, min\_y −64, height 384, default block stone, default fluid water.
- **Peaks and valleys:** folded out of weirdness w as PV = 1 − |3·|w| − 2|. Weirdness 0 gives a valley (−1) and |w| = 2/3 a peak (+1).
- **Splines:** curves with control points and slopes, which can nest. They turn continentalness, erosion and PV into offset (base height), factor (how fast density falls with height; low means tall terrain) and jaggedness (sharp peak detail).
- **Shape (1.18-1.21 data; 26.3 renamed the nodes):** depth = offset + a gradient from +1.5 at Y −64 to −1.5 at Y 320. Terrain ≈ 4 × quarter\_negative((depth + jaggedness × half\_negative(jagged noise)) × factor) + base 3D noise.
- **Caves:** cheese caves (big caverns), spaghetti caves (long tunnels), noodle caves (1-5 blocks wide) and pillars, combined with min() so open space always wins ([Cave](https://minecraft.wiki/w/Cave)).
- **Aquifers:** cells of about 16x12x16 blocks with random fluid levels, so caves hold their own lakes. Lava appears below about Y −54, and stone barriers separate different levels.
- **Ore veins:** large copper and iron veins come from the density graph (`ore_vein` in 26.3), not from features.
- **Chunk surface level:** a cheap ground-height estimate available before full terrain, replacing the older preliminary-surface input. Surface rules use it to keep grass out of caves.

The speed trick: density is sampled only at cell corners (historically 4 blocks wide by 8 tall) and blended trilinearly, with 2D inputs cached per column. That is one noise evaluation per 128 blocks. Each noise is "normal noise", two stacks of improved Perlin octaves, and continent-scale features span thousands of blocks ([Noise router](https://minecraft.wiki/w/Noise_router), [Density function](https://minecraft.wiki/w/Density_function)).

## Biomes and climate

A biome is a box in six climate dimensions plus an offset, and each spot takes the nearest box. Temperature and humidity steer biomes only, never terrain height.

- **The six values:** temperature, humidity (vegetation), continentalness, erosion, weirdness and depth.
- **Matching:** distance is zero inside a range, otherwise the squared gap to its edge, summed over all six; the offset makes some biomes lose ties. Something always matches.
- **Storage:** values are fixed-point (×10,000) and searched with a tree plus a cache.

Overworld bands, from the 1.18 biome builder layout (confirm against the 26.3 parameter list):

| Parameter | Bands, low to high |
| --- | --- |
| Temperature | frozen −1.0 to −0.45 · cool to −0.15 · temperate to 0.2 · warm to 0.55 · hot to 1.0 |
| Humidity | arid −1.0 to −0.35 · dry to −0.1 · neutral to 0.1 · damp to 0.3 · wet to 1.0 |
| Continentalness | mushroom fields −1.2 to −1.05 · deep ocean to −0.455 · ocean to −0.19 · coast to −0.11 · near inland to 0.03 · mid inland to 0.3 · far inland to 1.0 |
| Erosion | 7 levels split at −0.78, −0.375, −0.2225, 0.05, 0.45, 0.55; low is mountainous, high is flat |
| Peaks and valleys | valley below −0.85 · low to −0.2 · mid to 0.2 · high to 0.7 · peak above 0.7 |

- **Sea level vs continentalness:** water fills open space below Y 63, but continentalness decides ocean or land through terrain height. Deep oceans are over 30 blocks deep ([World generation](https://minecraft.wiki/w/World_generation)).
- **Depth and cave biomes:** depth is about 0 at the surface and grows about 1/128 per block downward. Dripstone caves need depth 0.2-0.9 and continentalness 0.8+; lush caves need humidity 0.7+; the deep dark needs depth about 1.1 and low erosion. 26.2's sulfur caves use the same depths with set weirdness, continentalness and erosion ranges.
- **Altitude and snow:** above about Y 80, temperature falls about 0.00125 per block (0.05 per 40, with noise). Below 0.15, rain becomes snow and still water freezes, which creates snow lines. These numbers are code knowledge; the pages we read didn't show them.
- **Oceans:** warm, lukewarm, cold and frozen variants come from the temperature band; all but warm have deep versions ([Biome](https://minecraft.wiki/w/Biome)).
- **Resolution:** biomes sit in 4x4x4-block cells, 64 per section. Lookups soften borders by picking among 8 nearby cells with a jitter seeded from a SHA-256 hash of the seed. The wiki lists per-block biome storage (4,096 per section) as upcoming in 26.4, so hide storage behind an interface.
- **New in 26.3:** the [dappled forest](https://minecraft.wiki/w/Java_Edition_26.3), placed near cold biomes. Its exact climate box wasn't in our sources.

## What blocks go where

After the noise stage a chunk holds only stone, water, lava and air. Surface rules, carvers and features then dress it, in that order.

**Surface rules** (a `material_rule` file in 26.3) are a small decision tree run top-down per column ([Surface rule](https://minecraft.wiki/w/Surface_rule)):

- **Rule types:** `sequence` (first match wins), `condition` (if a test passes, run a nested rule), `block` (place it), `bandlands` (badlands stripes).
- **Tests:** biome, noise threshold (3D option since 26.2), vertical gradient, Y above, water, stone depth, above the preliminary surface (keeps grass out of caves), hole, steep, temperature, not.
- **Results:** bedrock is solid at Y −64 and thins out by Y −59. Deepslate fills Y 0 and below, mixing with stone up to Y 7. Grass tops dirt, sand and sandstone cover deserts and beaches, and badlands get terracotta bands.

**Carvers** run after surface rules: the cave carver cuts worm tunnels and rooms, mostly Y −56 to 47 with some up to 180, and canyons cut ravines ([Carver](https://minecraft.wiki/w/Carver)).

**Features** pair a configured feature (what: a tree, an ore blob, a lake) with placement modifiers applied in order: count, in\_square, height\_range, heightmap, biome, rarity\_filter, surface\_relative\_threshold\_filter and environment\_scan ([Placed feature](https://minecraft.wiki/w/Placed_feature)). Each biome lists its features in 11 steps: raw\_generation → lakes → local\_modifications → underground\_structures → surface\_structures → strongholds → underground\_ores → underground\_decoration → fluid\_springs → vegetal\_decoration → top\_layer\_modification.

**Ores** ([Ore (feature)](<https://minecraft.wiki/w/Ore_(feature)>)): "triangular" is most common mid-range, and ranges that run past the world edge peak at the edge; ×N is attempts per chunk.

| Ore | Main placements (Y) | Notes |
| --- | --- | --- |
| Coal | 0 to 192 triangular ×20; 136 to 320 uniform ×30 | Lower band discards 50% of air-exposed blocks |
| Iron | −24 to 56 triangular ×10; −64 to 72 uniform ×10; 80 to 384 triangular ×90 | The high band fills mountains |
| Copper | −16 to 112 triangular ×16 | Bigger blobs in dripstone caves |
| Gold | −64 to 32 triangular ×4; badlands 32 to 256 uniform ×50 | 50% air-exposure discard outside badlands |
| Redstone | −64 to 15 uniform ×4; −64 to −32 triangular ×8 | Most common at the bottom |
| Lapis | −32 to 32 triangular ×2; −64 to 64 uniform ×4 | The second band is buried and vanishes if it touches air |
| Diamond | peaks at −64, gone by 16, ×7; −64 to −4 uniform ×2 | 50 to 100% air-exposure discard |
| Emerald | −16 to 480 triangular ×100 | Mountain and windswept biomes only |

**Large ore veins** (copper with granite, iron with tuff) are not features; they come from the density graph's vein noise during the noise stage.

All of this is seeded from the per-chunk decoration seed, so ores and trees land in the same spots for everyone on that seed.

## World height, bedrock and the void

The Overworld spans Y −64 to 319, which is 384 blocks or 24 sections. Nothing exists below −64, and void damage starts at Y −128 ([Altitude](https://minecraft.wiki/w/Altitude)).

| Dimension | Range (Y) | Bedrock | Void damage below |
| --- | --- | --- | --- |
| Overworld | −64 to 319 | Solid at −64, thinning out by −59 | −128 |
| Nether | 0 to 255 (logical 0 to 127) | Floor 0 to 4, roof 123 to 127 | −64 |
| End | 0 to 255 | None | −64 |

- Sea level 63 means the top ocean water block is Y 62. Deepslate starts at Y 0 and mixes with stone up to Y 7.
- Vanilla peaks stay well below 319; [Tectonic](https://modrinth.com/project/tectonic) pushes mountain ranges close to the limit.
- Custom dimensions allow min\_y from −2032 to 2016 and height 16 to 4064, both multiples of 16, with min\_y + height at most 2032 ([Dimension type](https://minecraft.wiki/w/Dimension_type)).

**Heightmaps** store, per column, the top block that matches a rule, so nothing scans a column to find the surface ([Heightmap](https://minecraft.wiki/w/Heightmap)).

| Heightmap | Tracks | Used for |
| --- | --- | --- |
| WORLD\_SURFACE\_WG, OCEAN\_FLOOR\_WG | Same rules as their twins, during generation only | Placing structures and features |
| WORLD\_SURFACE | Highest non-air block | Debug, commands, lightning rods |
| OCEAN\_FLOOR | Highest block that blocks movement (water ignored) | Server-side placement |
| MOTION\_BLOCKING | Highest block that blocks movement or holds fluid | Rain and snow, debug, commands |
| MOTION\_BLOCKING\_NO\_LEAVES | Same, ignoring leaves | Spawning, such as patrols |

Each heightmap holds 256 values at 9 bits in the Overworld, packed 7 per 64-bit word and stored as Y − min\_y + 1, with 0 meaning none. Removing a block forces a downward rescan, since the surface can drop.

## Structures and villages

Villages are placed on a seeded grid, assembled from jigsaw pieces, and blended in by a density term. Nothing scores slope across the whole village first, which is why they break on cliffs.

**Two layers of data:** a structure says what to build (type, biomes, decoration step, terrain adaptation). A [structure set](https://minecraft.wiki/w/Structure_set) says where to try (weighted structures plus a placement rule).

**Placement:**

- `random_spread` cuts the world into regions of `spacing` chunks and picks one candidate per region from region coordinates, seed and `salt`, at least `separation` chunks from the region edge. Spread is linear or triangular; `frequency` thins attempts; `exclusion_zone` keeps distance from another set.
- Villages use spacing 34, separation 8, salt 10387312.
- `concentric_rings` (strongholds only) places candidates on rings around the world origin.

**The 26.3 plains village file** ([village\_plains](https://raw.githubusercontent.com/misode/mcmeta/26.3-data/data/minecraft/worldgen/structure/village_plains.json)): type jigsaw, start pool `village/plains/town_centers`, size 6, max 80 blocks from center, start height projected to WORLD\_SURFACE\_WG, step surface\_structures, terrain adaptation beard\_thin.

**Jigsaw assembly** ([Template pool](https://minecraft.wiki/w/Template_pool), [Jigsaw Block](https://minecraft.wiki/w/Jigsaw_Block)):

- Each piece is a saved template with jigsaw connectors. A connector names itself, the name it targets, the pool for the next piece, and a joint (rollable or aligned).
- From the town center, the game picks weighted pieces whose connectors match, rotates them to fit, and rejects any whose box overlaps an existing piece. When nothing fits, the pool's fallback caps the branch; growth stops at depth 6 or 80 blocks out.
- Pieces are rigid (houses keep their shape) or terrain\_matching (paths and fields follow the ground). Processors age or swap blocks as they are placed.

**Terrain adaptation** is a density term, the "beardifier", applied in the noise stage, so ground is shaped before any block lands:

| Mode | Effect | Used by |
| --- | --- | --- |
| none | No change | Most other structures |
| beard\_thin | Builds ground up under pieces and clears space above, fading over a few blocks | Villages, pillager outposts |
| beard\_box | Like beard\_thin, applied over the whole bounding box | Ancient cities |
| bury | Surrounds the structure with terrain | Strongholds, trail ruins |
| encapsulate | Fully encases it | Trial chambers |

Only the village entry is verified from the 26.3 file; the other users and the exact effects are code knowledge.

**Why villages split:** the start height is sampled once at the center. Houses attached to a path take the ground height where they meet it, while houses attached to houses keep a fixed offset. With no footprint-wide slope check and only a few blocks of beard, a cliff site leaves houses hanging over drops or cut into hills (code knowledge).

**When it happens:** layout is decided at `structure_starts` and stored in the start chunk, chunks within 8 store references, the beardifier shapes terrain during `noise`, and blocks are written during `features`, one chunk at a time.

26.3's abandoned camp (tent, straw beds, optional shelter, pond, crops, tree) is new across 18 biomes; its placement values weren't in our sources.

## Why Minecraft loads chunks so well

Minecraft does only the work each chunk needs, stores it compactly, and draws only what the camera could see.

**Work by need** ([Chunk](https://minecraft.wiki/w/Chunk)):

- Tickets give each chunk a level: 31 or below ticks entities, 32 ticks blocks only, 33 is accessible but frozen, and 34+ may sit half-generated just for its neighbors. Above 44 it unloads.
- View distance and simulation distance are separate, so you can see far without simulating far. Entities tick in a square of 2s+1 chunks per side, block ticks one ring wider.
- Spawn chunks shrank in 1.20.5 and were removed in 1.21.9.

**Compact storage** ([Chunk format](https://minecraft.wiki/w/Chunk_format), [Region file format](https://minecraft.wiki/w/Region_file_format)):

- Each 16x16x16 section stores a palette of the block states it uses plus packed indices of at least 4 bits. An all-stone or all-air section is one palette entry with no index array.
- Biomes use a separate palette over 64 cells per section. Light takes 2,048 bytes per section per channel and is omitted when empty.
- A region file holds 32x32 chunks in 4 KiB sectors behind an 8 KiB header, so one chunk loads with one seek and one read. Compression is zlib by default, with LZ4 or none as options.

**Cheap generation:** coarse noise cells, per-column caches, 4x4x4 biomes and neighbor-limited stages running on worker threads (sections above).

**Cheap rendering:**

- Each section meshes into its own buffer on a worker thread and rebuilds only when it changes.
- Frustum culling skips sections outside the view.
- Cave culling: meshing also records which of a section's 6 faces connect through open blocks. Rendering walks out from the camera only through connected faces, so sealed caves are never drawn (Mojang's 2014 design, from memory).
- Light updates spread from the change only, dropping 1 per block; sky light stays 15 straight down through clear blocks ([Light](https://minecraft.wiki/w/Light)).

26.2's experimental Vulkan renderer won't carry over to Unreal, but the section-plus-dirty-flag model will.

## Building it in Unreal Engine 5 with C++

Generate and mesh on worker threads, touch Unreal objects only on the game thread, and apply finished chunks under a per-frame budget.

&#91;embedded content: Proposed UE5 chunk architecture · game thread vs worker threads\]

The subsystem decides what to build, workers return plain arrays, and the game thread applies a few per frame.

- **Engine:** UE 5.8 appears to be the current release (Oct 2026); confirm against the project.
- **Mesh component:** [RealtimeMeshComponent](https://github.com/TriAxis-Games/RealtimeMeshComponent) (free MIT core, UE 5.5-5.8) is built for frequent runtime updates and supports collision; its Pro tier adds runtime Nanite and Lumen support. ProceduralMeshComponent works for a prototype (async collision via `bUseAsyncCooking`) but updates slower. DynamicMeshComponent targets editing, not streaming.
- **Endgame renderer:** a custom primitive scene proxy that uploads 8-byte quads and expands them in the vertex shader, built only if profiling shows the component is the bottleneck.
- **Nanite:** [supported](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine) on static and instanced static meshes, not on runtime chunk meshes in stock UE. Use it for authored props through instanced meshes.
- **Threads:** [UE::Tasks](https://dev.epicgames.com/documentation/en-us/unreal-engine/tasks-systems-in-unreal-engine) `Launch` with prerequisites mirrors the stage graph, and `FPipe` serializes work on one resource. Workers output plain arrays; UObjects stay on the game thread.
- **Streaming:** a `UTickableWorldSubsystem` with separate load and unload radii (hysteresis), view-biased priority, cancellation and pooled components. World Partition targets authored levels, so a procedural world wants its own manager.
- **Content:** block faces in one texture array so a single material covers every block; grass and props as instanced static meshes.

Check in the editor: RealtimeMesh's async API names, its collision cooking mode, and whether Lumen sees runtime meshes.

## Beating vanilla

We can beat vanilla on speed with a 2D column pass, SIMD noise, bitmask meshing and far LOD. We can beat it on looks by planning villages before the terrain around them.

**Faster:**

- **Column pass once:** compute climate, biome, estimated surface height and structure influence once per column, then reuse them for terrain, site checks and far LOD.
- **Skip what can't change:** use each column's height bounds to skip 3D cells that must be solid rock or open sky; caves keep their own pass.
- **SIMD noise:** [FastNoise2](https://github.com/Auburn/FastNoise2) (MIT) reports roughly 5-8x the throughput of FastNoiseLite with AVX2, choosing SSE2 through AVX-512 or NEON at runtime.
- **Bitmask meshing:** [binary greedy meshing](https://github.com/cgerikj/binary-greedy-meshing) culls and merges 64 faces per instruction and emits 8-byte quads, meshing a 64³ chunk in about 50-200 µs. Add per-vertex ambient occlusion: 3 neighbors per corner, flipping the quad diagonal to avoid seams.
- **Lessons from mods:** [C2ME](https://modrinth.com/mod/c2me-fabric) parallelizes generation and disk IO, Sodium meshes off-thread with compact vertices, and [Distant Horizons](https://gitlab.com/jeseibel/distant-horizons) draws simplified terrain past render distance. Distant Horizons is GPLv3, so we take ideas, not code.
- **Far LOD:** build distant terrain from the column pass, height plus surface material in a quadtree of doubling cell sizes. Far away, caves and overhangs don't matter.
- **Save deltas:** store only chunks the player changed and regenerate the rest from the seed.
- **Stateless randomness:** use hash(seed, subsystem, x, y, z) everywhere, so any thread can build any chunk in any order and the LOD matches the real terrain.

**Better mountains:**

- **Scale:** [Tectonic](https://modrinth.com/project/tectonic) builds continents thousands to tens of thousands of blocks wide, with ranges that climb near the build limit. Use the full height budget.
- **Shape:** ridged noise along domain-warped ridge lines draws the ranges; low erosion and high peaks-and-valleys select mountain biomes.
- **Erosion without simulation:** Inigo Quilez's derivative-damped fBm divides each octave by 1 + the squared accumulated slope, so steep areas lose detail and valleys stay smooth. [runevision's 2026 erosion filter](https://blog.runevision.com/2026/03/) adds branching gullies aligned to the slope, statelessly per point, at about gully weight 0.5 and strength 2.0.
- **Order:** shape the 2D height first, then let 3D density add overhangs only on steep faces, so cliffs can overhang without stray floating islands.

**Villages that never break in half:**

1. **Plan first:** in each placement region, try several seeded candidate sites and estimate each footprint.
2. **Score the land:** sample the estimated height every few blocks across the footprint plus a margin. Reject sites over limits on height range, mean and max slope, or water; keep the best, or skip the region.
3. **Flatten zone:** store a settlement mask (center, radius, falloff) in the column data and scale jaggedness and erosion strength by (1 − mask), so mountains fade out around towns.
4. **Pads:** fit a plane under each building (least squares, tilt capped at a few percent, or flat), then blend density toward it with a smoothstep ring about 6-10 blocks wide. Big towns on slopes get terraces joined by stairs.
5. **Protect the ground:** suppress caves and aquifers under footprints.
6. **Build:** rigid houses sit on their pads, and paths route by A\* over the coarse height grid with a grade penalty and a hard cap.
7. **Foundations:** fill gaps of 1-3 blocks under building edges; use pillars or stilts for deeper drops.
8. **Order:** pads, then roads, then a final grade check, since easing pads after roads can break road grades again ([example](https://github.com/hoffspot/conquest/pull/110)).

This turns vanilla's few-block beard into a whole-site plan, and it stays deterministic because every step depends only on the seed and the region.

## Open questions

- [ ] Spline control points and the full 26.3 `final_density` graph: read them from the 26.3 data files if we want to tune toward vanilla.
- [ ] Neighbor radii for each chunk stage in 26.3; the stage list is confirmed, the radii are code knowledge.
- [ ] Climate band edges, checked against the 26.3 biome parameter list.
- [ ] Altitude temperature numbers (Y 80, 0.00125 per block, snow below 0.15).
- [ ] What the new `aquifers` object and `ore_vein` function contain.
- [ ] Climate boxes for the dappled forest and sulfur caves, and the abandoned camp's placement values.
- [ ] Whether 26.4 moves biomes to per-block storage.
- [ ] RealtimeMesh async and collision APIs, and whether Lumen sees runtime meshes.

## Sources

Pages opened during this research:

- **Minecraft Wiki, versions:** [Java Edition 26.1](https://minecraft.wiki/w/Java_Edition_26.1), [26.2](https://minecraft.wiki/w/Java_Edition_26.2), [26.3](https://minecraft.wiki/w/Java_Edition_26.3), [Wilderness Bound](https://minecraft.wiki/w/Wilderness_Bound)
- **Minecraft Wiki, generation:** [World generation](https://minecraft.wiki/w/World_generation), [Noise settings](https://minecraft.wiki/w/Noise_settings), [Noise router](https://minecraft.wiki/w/Noise_router), [Density function](https://minecraft.wiki/w/Density_function), [Noise](https://minecraft.wiki/w/Noise), [Cave](https://minecraft.wiki/w/Cave), [Biome](https://minecraft.wiki/w/Biome), [Surface rule](https://minecraft.wiki/w/Surface_rule), [Carver](https://minecraft.wiki/w/Carver), [Placed feature](https://minecraft.wiki/w/Placed_feature), [Ore](https://minecraft.wiki/w/Ore), [Ore (feature)](<https://minecraft.wiki/w/Ore_(feature)>), [Seed](<https://minecraft.wiki/w/Seed_(level_generation)>), [Slime](https://minecraft.wiki/w/Slime)
- **Minecraft Wiki, structures and world limits:** [Structure](https://minecraft.wiki/w/Structure), [Structure set](https://minecraft.wiki/w/Structure_set), [Template pool](https://minecraft.wiki/w/Template_pool), [Jigsaw Block](https://minecraft.wiki/w/Jigsaw_Block), [Altitude](https://minecraft.wiki/w/Altitude), [Heightmap](https://minecraft.wiki/w/Heightmap), [Dimension type](https://minecraft.wiki/w/Dimension_type), [Chunk](https://minecraft.wiki/w/Chunk), [Chunk format](https://minecraft.wiki/w/Chunk_format), [Region file format](https://minecraft.wiki/w/Region_file_format), [Light](https://minecraft.wiki/w/Light)
- **26.3 game data (misode/mcmeta mirror):** [overworld noise settings](https://raw.githubusercontent.com/misode/mcmeta/26.3-data/data/minecraft/worldgen/noise_settings/overworld.json), [village\_plains](https://raw.githubusercontent.com/misode/mcmeta/26.3-data/data/minecraft/worldgen/structure/village_plains.json)
- **Code mappings and seed tools:** [Yarn 1.21.11 ChunkStatus](https://maven.fabricmc.net/docs/yarn-1.21.11+build.4/net/minecraft/world/chunk/ChunkStatus.html), [Yarn 1.21.11 ChunkGenerationStep](https://maven.fabricmc.net/docs/yarn-1.21.11+build.4/net/minecraft/world/chunk/ChunkGenerationStep.html), [Cubiomes biomenoise.c](https://raw.githubusercontent.com/Cubitect/cubiomes/master/biomenoise.c)
- **Release coverage:** [PaperMC 26.1 notes](https://papermc.io/news/26-1), [Minecraft 26.1 Pre-release 1](https://feedback.minecraft.net/hc/en-us/articles/44167381353869-Minecraft-Java-Edition-26-1-Pre-release-1), [GamingOnLinux on 26.3](https://www.gamingonlinux.com/2026/09/minecraft-java-edition-26-3-wilderness-bound-update-released/)
- **Unreal Engine:** [RealtimeMeshComponent](https://github.com/TriAxis-Games/RealtimeMeshComponent), [Tasks System](https://dev.epicgames.com/documentation/en-us/unreal-engine/tasks-systems-in-unreal-engine), [Nanite](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine)
- **Performance:** [FastNoise2](https://github.com/Auburn/FastNoise2), [binary greedy meshing](https://github.com/cgerikj/binary-greedy-meshing), [Distant Horizons](https://gitlab.com/jeseibel/distant-horizons)
- **Terrain and settlements:** [Tectonic](https://api.modrinth.com/v2/project/tectonic), [Terralith](https://api.modrinth.com/v2/project/terralith), [runevision, March 2026](https://blog.runevision.com/2026/03/), [settlement pads PR](https://github.com/hoffspot/conquest/pull/110)
