# Scout K: phase 6 S10 (carvers) and S11 (underground features)

Prep research, written 2026-10-10 against `015bb42` (C:\dev\projects\Unnamed-scout, read-only). Sources: the 26.3 data
under `Reference/minecraft-worldgen-26.3/`, minecraft.wiki, `javap -public` signatures, and black-box runs of the 26.3
server jar (public API calls and `/place feature` in a probe world). No game code was read. Probe scripts, raw outputs
and reproduction commands are in `C:\dev\sandbox\scout-underground\probes\`.

Layout: section 1 covers both sessions. Then **Part S10** and **Part S11** each have sections 2 to 9 in the order
asked for. A worker reads section 1 and then only its own part.

---

## 1. Summary and recommendations

### S10, carvers

- **Data:** three carvers. All 56 overworld biomes list all three, so the biome rule cannot change the result.
  - `cave`: p 0.15, start y −56..180.
  - `cave_extra_underground`: p 0.07, start y −56..47.
  - `canyon`: p 0.01, start y 10..67.
- **26.3 changed the carver format.** It removed `replaceable`, `lava_level` and `debug_settings` (wiki, Configured carver). The public API is now `carve(context, random, chunk, startChunk, CarverOutput)`, and `CarverOutput` is just `carve(x, y, z)` plus `minY()`/`maxY()`.
  - A carver only marks positions. Whoever writes the blocks decides what a marked cell becomes.
  - That matches the plan's pull pass: build a carve mask per column, then apply it with the aquifer.
- **Reach (black-box, 1,000 starts per carver):** the farthest chunk a start ever carved was 6 for both caves and 7 for canyons.
  - No start reached past 8 in 3,000 starts.
  - `WorldCarver.getRange()` returns 4, so 2 x 4 = 8 matches the plan's "up to 8 columns away". **Keep 8.**
- **Expected carving per column:** about **1,230 marked positions** (cave 784, extra 336, canyon 106).
  - In a real same-seed world diff, **~870 solid cells per column became open**: 94.3% air, 5.2% lava (all at or below y −49, so the aquifer's global lava), 0.6% water (local aquifer levels).
  - Carving never turned water into air and never removed bedrock.
- **How it meets our pipeline at 015bb42:**
  - **Hook:** inside `FillWithRules`, after the surface rules (`VoxelDensityTerrain.cpp` 442-468) and before `Freeze.Apply` (470-471). `Out.Build` (307) then rebuilds the heightmaps.
  - **Fluids:** the aquifer is column-batched and has no per-position query. Its prepared centres and statuses stay alive in `Scratch` until `ReturnScratch` (309). S10 adds one per-position query over them, so a carved cell gets air, water or lava exactly as an open noise-cave cell at that spot would.
- **Cost:** vanilla-style carving with no cache, where every target chunk replays the paths of all 867 neighbour starts, measured **0.27 ms per chunk** in warmed Java.
  - With a path cache, C++ should land around **0.05-0.15 ms**. The per-cell aquifer query for about 870 cells is the main unknown, at about 0.1 ms.
  - Expected total is 0.15-0.3 ms against the **0.5 ms** gate.
- **Size M, one session.**

### S11, underground features

- **22 placed features in scope.** Most are biome-local:
  - lush caves: 6;
  - dripstone caves: 3;
  - sulfur caves: 4;
  - deep dark: 2;
  - everywhere: lava lakes (2), geode, underwater magma, springs (3).
- **Everything measured fits the 3x3 write window** (±16 blocks past the column edge, so an origin offset of −16..31). Measured with `/place feature`:
  - geode: −10..+16 per axis (the data's ±16 generation cutoff bounds it exactly);
  - lava lake: ±6 (barrier ±7);
  - sculk patch: ±12;
  - moss, clay and dripstone clusters: ±8;
  - pointed dripstone and sulfur spike scatter: ±10 by data;
  - large dripstone: radius up to 16 by data, which is exactly the edge (a risk; see 9B).
- **Deferred by Q2 (templates):** `rooted_sulfur_spring`, because it places `spring/sulfur_spring_*` templates.
- **Deferred by Q3 (flowing fluids):** the three `spring_*` features (falling fluid states).
- **Sculk catalysts and sensors** are block-entity blocks. They come out of `sculk_patch_deep_dark`; open question S11-Q1.
- **Every other S11 feature is placeable now.** Lakes, pools, clay pools and dripstone puddles place only still sources.
- **Lake.Contained (black-box):**
  - Of 245 placed lava lakes, 0 have lava next to air at the sides or below.
  - All 147 lakes whose lava rows met an air gap refused to place.
  - In 26.3 the lake is centred on its origin: lava rows y−3..y−1, `cave_air` rows y..y+2, x/z ±6.
- **Geode.Shape (black-box, 177 geodes):**
  - all 1,151 buds sit on budding amethyst;
  - budding share of the inner layer 0.084-0.087 (data 0.083);
  - 94% cracked (data 0.95);
  - the mean distance from the hollow orders the layers: air 3.9 < amethyst 5.7 < calcite 6.8 < basalt 7.8.
- **S11 starts mostly from empty.** At 015bb42 the decorator compiles only `ore`, and it has five modifiers and three int providers. S11 depends on what S8 (plants: modifiers, predicates, `block_column`, patches) and S9 (`tree`, needed by `rooted_azalea_tree`) add first.
- **Cost against 0.3 ms:** lakes, geodes and magma together are about 0.05-0.1 ms per column. Cave-biome features are heavy per column (sculk patches, dripstone counts of 192-256 x 1-5), but they only run where those biomes are. Their mean depends on cave-biome coverage, which nobody has measured yet (risk).
- **Size L. Recommend splitting** into **S11a** (lakes, sulfur pool, geode, underwater magma, springs data-off, dripstone and sulfur speleothems, large dripstone) and **S11b** (lush caves and sculk, after S8 and S9).

### Questions for Jesus (details in 9A and 9B)

1. **S10-Q1:** the fluid in carved cells. (a) The exact aquifer per position, the same as noise caves (recommended). (b) Global fluid only.
2. **S11-Q1:** sculk catalysts and sensors. (a) Place the plain blocks now, without block entities (recommended). (b) Leave them out until phase 7.
3. **S11-Q2:** `rooted_sulfur_spring`. (a) Defer all of it to phase 7 with templates (recommended). (b) Place its roots and tuff now, without the spring.
4. **S11-Q3:** split S11 into S11a and S11b. (a) Yes (recommended). (b) No.

---

# Part S10: carvers

## 2A. Reading list (015bb42, in order)

1. `Docs/Records/2026-10-09-Voxel-Phase6-Plan.md` 145-179 (budget table and 3.6), 452-459 (S10), 643-667 (answers).
2. `Docs/VoxelWorld.md`:
   - 239-256: column density, noise caves, `bCaves`/`RemoveCaves`, stats;
   - 346-360: aquifers;
   - 444-459: decoration stages and snapshot;
   - 661-691: settings and console;
   - 718: `CaptureVoxel -Caves`.
3. `Source/UnnamedVoxel/Terrain/VoxelDensityTerrain.h` 22-70 (settings, overrides, timings), ~145 (`FScratch`), ~165 (`Blocks`).
4. `Source/UnnamedVoxel/Terrain/VoxelDensityTerrain.cpp`:
   - 227-326: `Generate`, `GenerateSnapshot`, `FinishColumn`;
   - 388-471: `FillWithRules`, which writes Dense from density and fluid, then the surface rules, then the freeze.
5. `Source/UnnamedVoxel/Terrain/VoxelAquifer.h` 9-26 and 70-83. `VoxelAquifer.cpp` 82-86 (`GetGlobal`), 103-246 (`Prepare`, centres, statuses), 249-300 (`Fill`, sky rule, barriers).
6. `Source/UnnamedVoxel/Storage/VoxelColumn.h` 29-61; `VoxelConstants.h` 11-15; `Random/VoxelRandom.h` 53-126; `Decoration/VoxelFeatureOrder.h` 24-41 (the seed-salt precedent).
7. `Source/Unnamed/Voxel/VoxelFeatureReader.cpp` 56, 290-293 and 685-710: carvers are already parsed into the decoration data's `Carver` registry and `Biome.Carvers`, but nothing in `UnnamedVoxel` uses them.
8. `Source/UnnamedVoxel/VoxelBench.cpp` 526-650 (`RunDensity`; the timed `Generate` is at 597-599). `Source/Unnamed/Voxel/VoxelConsole.cpp` 27-49, 204-210 and 257 (the `nocaves` argument).
9. Tests:
   - `Source/Unnamed/Tests/VoxelAquiferTests.cpp`: 23-57 (golden constants, hash), 138 (`Aquifer.Fluids`), 251-321 (`Aquifer.Golden` with its record mode);
   - `VoxelDensityTests.cpp` 481-544 (`Density.Golden`, caves off);
   - `VoxelFeatureTests.cpp` 106 and 126-145 (carver count and lists).
10. The data: `Reference/minecraft-worldgen-26.3/data/minecraft/worldgen/carver/*.json` (3 small files), plus the carver entries in `facts/provider_histograms.json` (search for `"carver/`). Then this part's sections 3-7.

## 3A. Data inventory

| What | Path | Count |
|---|---|---|
| Carvers | `data/minecraft/worldgen/carver/{cave,cave_extra_underground,canyon}.json` | 3 |
| Biome carver lists | `data/minecraft/worldgen/biome/*.json` → `carvers` | 56 biomes, each `[cave, cave_extra_underground, canyon]` |
| Provider histograms | `facts/provider_histograms.json`, uses `carver/...` | 10 providers (cave count, floor_level, the 3 radius multipliers, thickness, cave y, extra y; canyon distance/horizontal factor (shared), thickness, vertical_rotation, y) |
| Carver tags | none. 26.3 has no `overworld_carver_replaceables`, and the reference has 0 tags with "carver" in the name | 0 |
| Reader | `Source/Unnamed/Voxel/VoxelFeatureReader.cpp` 290-293 (schema), 685-710 (biome lists) | already strict-read |

The decoded records, with defaults filled in by the jar's codec (`probes/run2/out/records.txt`):

- `cave`: probability 0.15. y = uniform from 8 above the bottom to absolute 180. `count` = very_biased_to_bottom 0..14. thickness = trapezoid 0..3, plateau 1. `weird_thickness_bias` true. room_vertical_radius_multiplier U[0.1, 0.9). horizontal_radius_multiplier U[0.7, 1.4). vertical_radius_multiplier U[0.8, 1.3). **start_vertical_radius_multiplier 1.0 (codec default, not in the JSON).** floor_level U[−1.0, −0.4).
- `cave_extra_underground`: as `cave`, but probability 0.07 and y up to absolute 47.
- `canyon`: probability 0.01. y = uniform absolute 10..67. vertical_rotation U[−0.125, 0.125). shape: distance_factor U[0.75, 1.0), thickness trapezoid 0..6 plateau 2, width_smoothness 3, horizontal_radius_factor U[0.75, 1.0), vertical_radius_default_factor 1.0, vertical_radius_center_factor 0.0, y_scale 3.0.
- `getRange()` = 4 for all three.

## 4A. Behaviour in our own words

Each point is followed by its source in brackets.

- **What a carver is:** a carver marks positions. It never chooses blocks. [`javap -public` of `WorldCarver` and `CarverOutput`: `carve(int,int,int)`, `minY()`, `maxY()`]
- **Per call:** the carver is called once per (start chunk, target chunk) with the start's random stream. It marks cells of the target chunk only. Positions come back **chunk-local** (x, z 0..15, absolute y). The same (seed, start, target) always gives the same cells (50 of 50). [probe: `UndergroundProbe.java` part 3/4, `calls_outside_target_chunk` 0, `determinism.txt`]
- **Start chance:** a chunk starts a carver with that carver's probability. Measured rates: 0.1528, 0.0688 and 0.0096 over 6.5k, 14.5k and 104k seeds. [data `probability`; probe]
- **Cave shape** (wiki https://minecraft.wiki/w/Cave, "Carver caves"):
  - A system is an optional room (1 in 4) joined to 1-4 trunks; each trunk has 0 or 2 branches.
  - Tunnels run 85-112 blocks from root to branch end. They are thin at the ends and thick in the middle, with round ceilings and flat floors.
  - The data names the parts: `count` is tunnels per start (wiki, https://minecraft.wiki/w/Configured_carver); `room_vertical_radius_multiplier` stretches rooms; `floor_level` cuts the floor flat (1.0 means the upper half-ellipsoid only); `start_vertical_radius_multiplier` scales the first segment.
  - `weird_thickness_bias` multiplies thickness by about U(0,1)·U(0,3)+1 about 10% of the time (same page).
- **Canyon shape** (https://minecraft.wiki/w/Ravine, Configured_carver):
  - Length about 85-127 blocks, width usually under 15, depth up to 62.
  - `y_scale` 3 makes it tall and narrow. `distance_factor` scales length; `horizontal_radius_factor` scales width; `vertical_radius_*` set depth, with the centre factor deepening the middle; `width_smoothness` > 0.
- **Measured per start** (1,000 starts each; `probes/run2/out/carver_stats.json`):

| | cave | cave_extra_underground | canyon |
|---|---|---|---|
| starts that carve nothing | 41.8% | 46.0% | 0% |
| unique cells per start, mean (p50, p95, max) | 5,134 (2,020, 20,945, 43,795) | 4,885 (1,363, 20,616, 48,648) | 11,038 (10,075, 21,604, 30,831) |
| chunks touched per start, mean (max) | 13.9 (75) | 12.9 (69) | 10.4 (16) |
| farthest chunk (Chebyshev), mean / max | 2.27 / 6 | 2.11 / 6 | 5.06 / 7 |
| cells at chunk distance 0..7 | 454k, 2.08M, 1.74M, 692k, 148k, 14.9k, 525, 0 | 421k, 1.98M, 1.67M, 648k, 149k, 17.4k, 991, 0 | 242k, 1.62M, 3.06M, 3.24M, 2.03M, 715k, 119k, 4.8k |
| y of carved cells | −63..224 | −63..104 | −13..89 |
| x/z span of a start's cells, mean (max) | 86 (180) | 84 (180) | 61 (109) |
| y span, mean (max) | 115 (282) | 63 (156) | 29 (47) |
| max cells in one column, mean (max) | 17 (50) | 17 (50) | 28 (47) |
| marks per unique cell (overlap) | 2.7 | 2.8 | 3.1 |

- **The ellipsoid rule** (`WorldCarver.carveEllipsoid`, public static, called black-box; `probes/run1/out/ellipsoid_cases.json`, fit by `ellipsoid_fit.py`/`ell_dump.py`):
  - **Horizontal:** a column is a candidate when (dx² + dz²) < 1, with dx = (x + 0.5 − cx)/hr and dz = (z + 0.5 − cz)/hr, using chunk-local x and z clipped to 0..15.
  - **Vertical:** y runs from floor(cy − vr) to floor(cy + vr) + 1, never below minY + 1. The relative coordinate passed to the skip test is dy = (y − 0.5 − cy)/vr. Note the **−0.5**: it is offset by one block from the horizontal convention.
  - **Skip test:** the carver's own skip test then rejects cells. Caves reject outside the unit sphere and below `floor_level` (wiki). Canyons use their own width profile.
- **What the world does with marked cells** (same-seed diff of two 10x10-chunk normal worlds, seed 263, carvers at p 0 by datapack; `probes/carvediff/carve_diff.json`):
  - 87,035 solid cells became open: air 82,058, lava 4,487, water 489, `cave_air` 1.
  - Lava appears only in the −64..−49 bin (4,472), with 15 elsewhere from features. Water is spread over y −32..111, which means local aquifer levels.
  - Blocks removed (the terrain-time ones): stone, deepslate, dirt (1,730), gravel (1,275), grass_block (471), tuff, and ore-vein blocks.
  - Water → air happened in 5 cells (feature noise). Bedrock was never carved. No sand or snow was cut in this area (unknown elsewhere).
- **Order:** carvers run after the surface (wiki World generation; Cave: carver caves are "added later" than noise caves). Canyons cut through grass and dirt, exposing them.
- **Underwater:** canyons may appear in oceans water-filled (wiki, Ravine).

## 5A. Test oracles

Reproduce: `probes\UndergroundProbe.java`. Compile with `javac -cp <server jar;libraries> -d classes`, then from an empty folder outside the project run `java -cp classes;<cp> UndergroundProbe <out> 1000 10`. It takes 34 s. The classpath is in `probes\classpath.txt`.

1. **`Carvers.Config`, exact:** the decoded values in 3A, including `start_vertical_radius_multiplier` 1.0 and the reach of 8 (2 x `getRange()`). Mutation: dropping the default, or reading `room_vertical_radius_multiplier` as the tunnel multiplier.
2. **`Carvers.Ellipsoid`, exact:** our voxelizer, run with the skip test off, must give these cells (chunk (0,0), minY −64):

| centre (x, y, z) | hr | vr | cells | y range |
|---|---|---|---|---|
| 8.0, 40.0, 8.0 | 3.0 | 2.0 | 192 (32 columns x 6) | 38..43 |
| 8.3, 40.7, 5.2 | 2.6 | 1.7 | 110 | 39..43 |
| 1.0, 10.0, 15.5 | 4.5 | 4.5 | 242 | 5..15 |
| 7.5, 0.5, 7.5 | 1.5 | 6.0 | 126 | −6..7 |
| 8.0, −61.0, 8.0 | 3.0 | 3.0 | 224 | −63..−57 (−64 never) |

   - The full cell lists are in `ellipsoid_cases.json`.
   - Mutations that must fail: +0.5 instead of −0.5 on y; ≤ instead of < (case 1 has no ties, so add a tie case if needed); not clipping at minY + 1; swapping our Y and Z axes.
3. **`Carvers.Distribution`, statistical:** our walker, with its own streams, over 1,000 starts per carver at a fixed seed:
   - start rate within 3σ of p;
   - farthest chunk ≤ 7 (canyon) and ≤ 6 (caves), and never > 8;
   - zero-cell share and mean cells per start within ±15% of the table in 4A;
   - y range inside the table's;
   - canyon y span 14..47.
   - Mutations: the wrong `count` provider (uniform 0..14 shifts the zero share far from 42%); y_scale 1 (canyon spans shrink); a missed `thickness` (means change).
4. **`Carvers.PerColumn`, statistical:** over 1,000 columns, marked cells per column ≈ 1,226 ± 15%. In a real preset, solid → open is about 870 per column and must stay below the marked count.
5. **`Carvers.Fluids`, invariants (as in `Aquifer.Fluids`):**
   - no carved cell below y −54 is air (it is lava);
   - no carved cell becomes water above the local aquifer level;
   - a carved cell that held water stays water;
   - bedrock is never carved.
   - Statistical: of carved cells, about 94% air, about 5% lava, under 1% water (preset overworld; the shares depend on the area).
6. **`Carvers.CacheOff`:** path cache on vs off gives **0 differing blocks** over the bench area (the plan's gate).
7. **`Carvers.Reach`:** reach 8 vs reach 9 gives 0 differing blocks (nothing reaches 9). Reach 6 vs 8 must differ in some area, or the test is too weak.
8. **`Carvers.Order`:** a column generated alone equals the same column generated inside an area in any order. The existing `LoadOrder` and `HashLoadOrders` pattern applies.
9. **`Carvers.Golden`:** hashes for the 3 presets × 4 columns (the Aquifer.Golden pattern, with record mode). `Aquifer.Golden` is re-recorded as a decided change. `Density.Golden` (caves off) must stay unchanged, because carvers follow `bCaves`.

## 6A. Draft design

- **Data flow:**
  - At load, the reader's carver entries become plain configs in `UnnamedVoxel` (`FVoxelCarverConfig` x 3) and are passed in with the terrain preset, as the density graph is.
  - Per column in `Generate`, after the surface rules:
    1. **Starts.** For each carver and each start column within ±8 (289 x 3 = 867 tests), `IsStart = ToUnitDouble(Hash64(seed, Salt("carver/<id>"), sx, sz)) < p`. That is about 66 starts per column on average.
    2. **Paths.** `GetPath(carver, sx, sz)` returns the start's steps. Each step is {centre, hr, vr, kind, skip parameters} plus a bounding box. The steps come from our own walker, seeded from `Hash64(seed, Salt("carver/<id>/path"), sx, sz)`, and they are a pure function. The cache is keyed (carver, sx, sz).
    3. **Mark.** For each step whose box meets this column, voxelize it with the 4A ellipsoid rule into a 16x16x384 bit mask (12 KB on the stack or in the scratch).
    4. **Apply.** For each marked cell whose Dense state is carvable, set air, water or lava from the aquifer's per-position answer. Dense states are noise-generated terrain, surface blocks and vein blocks; not bedrock and not fluid.
  - `Out.Build` then makes the sections and heightmaps.
- **Hook:** inside `FillWithRules`, between the end of the surface rules (`VoxelDensityTerrain.cpp` ~468) and `Freeze.Apply` (470).
  - That keeps carving before the freeze on both paths. `GenerateSnapshot` already has `bFreeze` false, so `FinishColumn` freezes later; `voxel.Decoration 0` freezes inside.
  - `Heights[]` local to `FillWithRules` needs no update, because `Out.Build` recomputes the heightmaps. Check that `FillWithRules` does not use `Heights` after this point.
- **The aquifer query:** add `EVoxelAquiferFluid FVoxelAquifer::GetFluidAt(const FVoxelAquiferScratch&, int32 X, int32 Y, int32 Z) const`.
  - It uses the statuses and centres `Prepare` built, as if the cell had density 0 (open), and applies the global rule and the barrier walls as `Fill` does.
  - It needs `Prepare` to have run (always true with caves on).
  - Barrier → stay solid. Air above the local level, or not under any status → air.
- **Carvable set:** there is no tag in 26.3, so use a fixed set from the block table: everything but air, fluids and bedrock.
  - The diff shows stone, deepslate, dirt, gravel, grass, tuff and vein blocks carved, and fluids and bedrock never.
  - Unknown: sand, sandstone, snow, ice and terracotta near cave mouths. Probe a desert or badlands seed if it matters; S10 can add that probe.
- **Switches:**
  - `FVoxelDensityTerrainSettings::bCarvers` (default true, and only active with `bCaves`);
  - an override in `FVoxelDensityTerrainOverrides`;
  - bench arguments `nocarvers` and `carvecache 0|1`;
  - a `CarversMs` field in `FVoxelDensityTerrainTimings`, with its report line in `RunDensity` and in the Columns bench.
- **Cache:**
  - **What it holds:** per (carver, start column), the step list, behind a sharded lock or one cache per worker thread.
  - **Size:** about 0.23 starts per column. A ±8 margin around the R+3 ring at ViewRadius 12 covers about 1,700 columns, so about 400 paths. A cave start has up to 15 x ~112 steps x 32 bytes ≈ 54 KB worst case and a few KB on average, so about 2-5 MB.
  - **Eviction:** by distance from the player ring. Results never depend on it (gate 6).
- **Gate:** 0.5 ms per column added to Generate.
  - **Estimate:** 867 start hashes (~3 µs). Path building of about 0.23 new paths per column at ~10-30 µs each, amortized by the cache. Marking about 1,230 cells x 2.8 overlaps at ~2-4 ns each (~10-15 µs). The aquifer query on about 870 cells at ~50-150 ns (40-130 µs). Total **≈ 0.06-0.2 ms**.
  - **Without the cache,** each column replays every start's path in full, like vanilla's 0.27 ms Java figure, likely 0.15-0.3 ms in C++. That still fits, which is why a cache bug cannot break the gate silently. Gate 6 catches the results.

## 7A. Code-shape sketches (against 015bb42)

Assumes `Terrain/VoxelDensityTerrain.h/.cpp`, `Terrain/VoxelAquifer.h`, `Random/VoxelRandom.h` and `Storage/VoxelColumn.h` as they are at 015bb42.

```cpp
// Terrain/VoxelCarvers.h (new)
enum class EVoxelCarverKind : uint8 { Cave, Canyon };
struct FVoxelCarverConfig {            // filled from the reader's K::Carver entries
    FName Id; EVoxelCarverKind Kind; float Probability;
    FVoxelHeightProvider Y;            // uniform anchors, as Decoration/VoxelNumberProviders
    FVoxelIntProvider Count;           // very_biased_to_bottom 0..14 (new int provider type)
    FVoxelFloatProvider Thickness, RoomVerticalRadius, HorizontalRadius, VerticalRadius, FloorLevel;
    float StartVerticalRadius = 1.f; bool bWeirdThicknessBias;
    // canyon
    FVoxelFloatProvider VerticalRotation, DistanceFactor, HorizontalRadiusFactor, YScaleProvider;
    int32 WidthSmoothness; float VerticalRadiusDefault, VerticalRadiusCenter;
};
struct FVoxelCarveStep { FVector3d Centre; double Hr, Vr; uint8 Kind; float Skip[2]; FIntRect ColumnBounds; };
struct FVoxelCarvePath { TArray<FVoxelCarveStep> Steps; };

class FVoxelCarvers {
public:
    bool Initialize(TConstArrayView<FVoxelCarverConfig>, uint64 Seed, FString* OutError);
    static constexpr int32 Reach = 8;                           // 2 x getRange(); measured max 7
    // Marks cells of column (CX, CZ) into Mask (16*16*384 bits, index as GetDenseIndex).
    void Mark(FIntPoint Column, TBitArray<>& Mask, FVoxelCarvePathCache* Cache /* null = off */) const;
private:
    bool IsStart(int32 Carver, FIntPoint Start) const;          // Hash64(Seed, Salt, sx, sz) < p
    void BuildPath(int32 Carver, FIntPoint Start, FVoxelCarvePath& Out) const;  // our own walker
    static void Voxelize(const FVoxelCarveStep&, FIntPoint Column, TBitArray<>& Mask); // 4A rule
};

// Terrain/VoxelAquifer.h (add)
EVoxelAquiferFluid GetFluidAt(const FVoxelAquiferScratch&, int32 X, int32 Y, int32 Z) const; // as if open

// VoxelDensityTerrain.cpp, FillWithRules, after the surface rules, before Freeze.Apply:
//   if (Settings.bCaves && bCarvers) { Carvers.Mark(Position, Scratch.CarveMask, Cache);
//     for each set bit i: if (Carvable(Dense[i])) Dense[i] = ToState(Aquifer.GetFluidAt(Scratch.Aquifer, x, y, z)); }
```

## 8A. Re-orientation list (diff these first)

- **S4 (ore veins: surface rules, density terrain, `VoxelConsole`):**
  - `VoxelDensityTerrain.cpp/.h`: `FillWithRules` gains the ore-vein rule, so line numbers and the hook point shift.
  - `Surface/VoxelSurfaceEvaluator.*`.
  - `VoxelConsole.cpp`: new bench arguments.
  - `VoxelBench.cpp`: Generate timings.
  - The goldens `Aquifer.Golden`/`Density.Golden`, re-recorded by S4. Re-read their constants before re-recording again.
  - Vein blocks become carvable cells; vanilla carves them (`raw_iron_block`, `copper_ore` in the diff).
- **S6 (block table, mesher, chunk component, VoxelBlocks, pipeline switches):** `Blocks/VoxelBlockTable.h` (state info, `IsAir`), `Source/Unnamed/Voxel/VoxelBlocks.cpp`, and the pipeline switches that sit beside a new `bCarvers`.
- **S8/S9:** `VoxelFeatureReader.cpp` (the carver schema lives in the same file), `VoxelNumberProviders.*`. S10 needs float providers and `very_biased_to_bottom`; S8 may add them first.
- **Torch probe (VoxelWorld shot code):** `CaptureVoxel -Caves` is S10's evidence. Re-check its views before relying on them.

## 9A. Risks, size, questions

- **Size M, one session.** Risks:
  1. The aquifer per-position query must agree with `Fill`, or walls and fluids differ between noise caves and carved caves. Mitigation: a test where carving a cell that `Fill` already left open changes nothing.
  2. Path shape is our own walker, so the oracles are statistical. Tune to the 4A table; do not chase vanilla's exact tunnels, since its random differs.
  3. Re-recording the cave-on goldens again after S4.
  4. Cache thread safety in the streaming pool.
- **S10-Q1, the fluid in carved cells.**
  - (a) Recommended: the exact aquifer per position, the same as a noise-cave cell there. Carved tunnels then flood where noise caves flood, as vanilla's diff shows (water at local levels, lava below −54).
  - (b) Global fluid only: lava below −54 and water below sea level when open to... This is cheaper but gives dry tunnels inside flooded aquifers, and water-filled tunnels where vanilla has air.
- **Not questions, decided by the data or the plan:**
  - reach 8;
  - carve before the freeze;
  - the biome rule, which cannot matter while every biome lists the same three carvers. Assert that at load and fail loudly if it ever changes.

---

# Part S11: underground features

## 2B. Reading list (015bb42, in order)

1. `Docs/Records/2026-10-09-Voxel-Phase6-Plan.md`:
   - 98-129: stages, the write window, write modes, reach, streams;
   - 145-172: gates;
   - 181-188: deferrals;
   - 461-479: S11;
   - 643-667: answers.
2. `Docs/VoxelWorld.md` 444-484 (Decoration).
3. `Source/UnnamedVoxel/Decoration/VoxelDecorator.h` 115-169. `VoxelDecorator.cpp`:
   - 39-114: `Initialize`;
   - 119-182: `CompileFeature`, where line 127 skips every type but `ore`;
   - 201-243: `RunModifiers`;
   - 245-333: `Decorate`;
   - 335-382: `Finalize`, whose write-mode switch is at 356-375.
4. `Decoration/VoxelDecorationContext.h` 16-61, `VoxelWriteLog.h` 7-109, `Features/VoxelOreFeature.h` 37-95 (the feature shape to copy).
5. `Decoration/VoxelFeatureGraph.h` 55-142, `VoxelNumberProviders.h` 14-59, `VoxelRuleTests.h` 26-58.
6. `Blocks/VoxelBlockTable.h` 78-139; `Source/Unnamed/Voxel/VoxelBlocks.cpp` 45-141; `Source/Unnamed/Foundation/FoundationBlockRegistry.h` 196-223 (states with properties).
7. `Source/Unnamed/Voxel/VoxelFeatureReader.cpp`: the schema rows for S11's types (81 block_column, 96 geode, 104 lake, 107 large_dripstone, 113 multiface_growth, 121 root_system, 127 sculk_patch, 129-133 sequence, selectors and speleothems, 141 spring_feature, 142 template, 147 underwater_magma, 150-152 the vegetation patches).
8. Tests: `Source/Unnamed/Tests/VoxelDecorationTests.cpp` 1136 (`OreProbes`, the probe-backed pattern), 1446 (`Reach`), 1470-1480 (`Golden`).
9. `Docs/Records/2026-10-09-Voxel-Phase6-Scout-Tables/inventory.md` 7-14, 20, 23, 93-98 and 166-170, and `tags.txt`.
10. Whatever S8 and S9 changed in the files above (see 8B), then sections 3B-7B here.

## 3B. Data inventory

The placed features, step, biomes and placement come from `probes/inventory.py`, whose output is `notes/inventory.txt`. The feature configs are under `data/minecraft/worldgen/{placed_feature,feature}/<id>.json`.

| Placed feature | Step | Biomes | Placement (per column) | Feature (type), key config |
|---|---|---|---|---|
| lake_lava_underground | lakes | 55 (all but deep_dark) | rarity 1/9, uniform y 0..top, scan down ≤32 to non-air and inside the world 5 below, `OCEAN_FLOOR_WG` − y ≥ 5 | `lake_lava` (lake): fluid lava level 0, barrier stone; replace-with-air/fluid unless `features_cannot_replace`; replace-with-barrier unless `lava_pool_stone_cannot_replace` (that tag plus leaves and logs) |
| lake_lava_surface | lakes | 55 | rarity 1/200, `WORLD_SURFACE_WG` | same `lake_lava` |
| rooted_sulfur_spring | lakes | sulfur_caves | count 1-2, y −64..256, scan up ≤12 through air to solid, −1 | root_system (sulfur roots, root column ≤184) → weighted selector of sequence {tuff x 64-128 at trapezoid ±7..±10, **template** `spring/sulfur_spring_{small 1-4, medium 1-3, large 1-2, extra_large_1}` at y −7}, weights 200/90/20/5 → **Q2** |
| sulfur_pool | lakes | sulfur_caves | count 256, y −64..256, solid, scan up ≤32 to air, −1, must be sulfur | sequence {lake (water, barrier sulfur, unless on `sulfur_spike`), potent_sulfur[wet] on a solid floor under water} |
| amethyst_geode | local_modifications | 56 | rarity 1/24, y −58..30 | geode; full record in 4B |
| large_dripstone | local_modifications | dripstone_caves | count 10-48, y −64..256 | large_dripstone: column_radius clamp(U 3..19, 3..16), height_scale U[0.4, 2), radius/height ratio 0.33, stalactite bluntness U[0.3, 0.9), stalagmite bluntness U[0.4, 1), wind U[0, 0.3), wind from radius 4 and bluntness 0.6, **floor_to_ceiling_search_range 30 (codec default)**, replaceable `dripstone_replaceable_blocks` |
| underwater_magma | **underground_ores** | 56 | count 44-52, y −64..256, `OCEAN_FLOOR_WG` − y ≥ 2 | underwater_magma: floor search 5, radius 1, p 0.5 |
| sculk_vein | underground_decoration | deep_dark | count 204-250, y −64..256 | multiface_growth sculk_vein on 8 base blocks, floor, wall and ceiling, spread 1.0, search_range 20 |
| sculk_patch_deep_dark | underground_decoration | deep_dark | count 256, y −64..256 | sequence {sculk_patch (charges 10 x 32, spread attempts 64, rounds 1, growth 0), 50% sculk_catalyst[bloom=false] on a sturdy floor} |
| sulfur_spike_cluster | underground_decoration | sulfur_caves | count 48-96, y −64..256 | speleothem_cluster: sulfur / sulfur_spike, radius 2-8, height 1-4, density 0.3-0.7, wetness 0, layer 2-4, search 12 |
| sulfur_spike | underground_decoration | sulfur_caves | count 192-256, y −64..256, count 1-5, offset clamped_normal x/z ±10 (σ 3), y ±2 (σ 0.6) | simple_random_selector {speleothem up from the floor, down from the ceiling}, each found by a ≤12 scan through air or water |
| dripstone_cluster | underground_decoration | dripstone_caves | count 48-96, y −64..256 | speleothem_cluster: dripstone_block / pointed_dripstone, radius 2-8, height 3-6, wetness clamped_normal(0.1, 0.3) in [0.1, 0.9] |
| pointed_dripstone | underground_decoration | dripstone_caves | as sulfur_spike | speleothem (dripstone) up or down |
| spring_water | fluid_springs | 55 | count 25, y −64..192 | spring_feature water[falling], 11 valid blocks → **Q3, off** |
| spring_lava | fluid_springs | 55 | count 20, very_biased_to_bottom (inner 8) bottom..top−8 | spring_feature lava[falling], 8 valid blocks; record defaults requires_block_below true, rock_count 4, hole_count 1 → **Q3, off** |
| spring_lava_frozen | fluid_springs | frozen_peaks, grove, jagged_peaks, snowy_slopes | as spring_lava | valid: snow_block, powder_snow, packed_ice → **Q3, off** |
| lush_caves_ceiling_vegetation | vegetal_decoration | lush_caves | count 125, scan up ≤12 to solid, −1 | vegetation_patch ceiling: moss_block, depth 1-2, radius 4-7, vertical 5, vegetation 0.08 → `cave_vine_in_moss` (block_column down: body 0-3 (w5) or 1-7 (w1), then head) |
| cave_vines | vegetal_decoration | lush_caves | count 188, scan up ≤12 to a sturdy-down face, −1 | block_column down: body 0-19 (w2), 0-2 (w3), 0-6 (w10), then head; providers `cave_vines_body` and `cave_vines_head` |
| lush_caves_clay | vegetal_decoration | lush_caves | count 62, scan down ≤12 to solid, +1 | random_boolean_selector {clay_with_dripleaves (vegetation_patch floor, clay depth 3, radius 4-7, vertical 2, vegetation 0.05); clay_pool_with_dripleaves (waterlogged_vegetation_patch, vertical 5, vegetation 0.1)} → dripleaf selector (small_dripleaf x 4 facings; big_dripleaf column of stems 0-4 (w2) or 0 (w1) + head, 4 facings) |
| lush_caves_vegetation | vegetal_decoration | lush_caves | count 125, scan down ≤12, +1 | vegetation_patch floor: moss_block depth 1, radius 4-7, vegetation 0.8 → weighted: short_grass 50, moss_carpet 25, tall_grass 10, azalea 7, flowering_azalea 4 |
| rooted_azalea_tree | vegetal_decoration | lush_caves | count 1-2, scan up ≤12 to solid, −1 | root_system: rooted_dirt roots radius 3, 20 tries, column ≤100, hanging_roots radius 3 x 20, vertical space 3 → `azalea_tree` (`tree`: bending trunk base 4 + 0-2, bend 1-2, oak_log; random spread foliage radius 3, height 2, 50 tries; leaves 3:1 azalea:flowering) |
| spore_blossom | vegetal_decoration | lush_caves | count 25, scan up ≤12 to solid, −1 | simple_block spore_blossom |

- **Block tags used** (`data/minecraft/tags/block/`): azalea_grows_on, azalea_root_replaceable, cave_vines, dripstone_replaceable_blocks (stone, granite, diorite, andesite, tuff, deepslate), features_cannot_replace (7 blocks), geode_invalid_blocks (bedrock, water, lava, ice, packed_ice, blue_ice), lava_pool_stone_cannot_replace, lush_ground_replaceable, moss_blocks, moss_replaceable, sulfur_spike_replaceable_blocks, supports_azalea.
- **Block state providers:** `cave_vines_body`, `cave_vines_head` (`data/minecraft/worldgen/block_state_provider/`).
- **Templates (deferred):** 10 `structure/spring/sulfur_spring_*.nbt` in the jar, not in the reference.
- **Blocks:** every S11 block exists in `Reference/minecraft-blocks-26.3/facts/block_facts.csv`, including cave_air, sulfur, potent_sulfur, sulfur_spike, cinnabar, pointed_dripstone, sculk_catalyst and hanging_roots.

## 4B. Behaviour in our own words

- **Lava lake (26.3 `lake`), measured** (`probes/server1-out`, `server2-out`; `underground_probe.py`). For 49 placements per case:
  - **Shape:** lava fills rows y−3..y−1 and `cave_air` fills rows y..y+2, inside ±6 in x and z. The bottom row is rare: only 2% of lava cells are at y−3.
  - **Barrier:** solid boundary cells become stone (barrier), which is visible in tuff: ±7, y−4..y+3, about 186 per lake.
  - **Writes:** lava cells 91-107 mean (32-188); cave_air cells about 175.
  - **Refusal:** the lake refuses to place when any of its lava cells would touch air at the side or below. All 147 placements with the air gap at a lava row failed. With the gap just under the lowest row, 18/49 failed and the rest placed shallower.
  - **No leaks:** 0 placed lakes have lava touching air.
  - **Open air:** it accepts air above, in the cave_air rows. A surface lake, with air above the stone top, writes only lava.
  - **Placement data:** the underground lake scans down for a floor and must be 5 or more blocks under `OCEAN_FLOOR_WG`. Wiki: underground lakes only above y 0 (Lava Lake page). The reason is the 1/9 rarity, the uniform y 0..top and the scan, as the data shows.
- **sulfur_pool:** the same lake with water and a sulfur barrier. Water rows y−3..−1, cave_air y..y+2, sulfur ±7, potent_sulfur[wet] on about 1 floor cell. 42/49 placed, 0 leaks.
- **Geode** (record from the codec, `records.txt`):
  - **Layer thresholds:** filling 1.7, inner 2.2, middle 3.2, outer 4.2.
  - **Crack:** chance 0.95, base size 2.0, point offset 2.
  - **Placements:** use_potential_placements_chance 0.35; alternate layer-0 chance 0.083; placements require the alternate (buds only on budding amethyst).
  - **Shape inputs:** outer_wall_distance 4-6, distribution_points 3-4, point_offset 1-2.
  - **Limits:** generation offsets −16..16, noise multiplier 0.05, invalid threshold 1.
  - **Measured over 177 geodes:**
    - 650-4,380 cells, mean 2,050;
    - axis extents 10-25;
    - offsets −10..+16 from the command position (the anchor is the low corner; wiki: bottom north-west);
    - by block: about 28% basalt, 23% calcite, 18% amethyst, 1.7% budding, 27% air, 0.6% buds.
  - **Refusal:** 19/49 refused when the 1-thick air layer crossed them. Air counts against `invalid_blocks_threshold` (the wiki says air always counts). Next to bedrock at y −56 with the floor at −64 untouched, 49/49 placed.
- **Large dripstone:**
  - Columns grow between floor and ceiling found within 30. Radius is capped by 0.33 x the cave height, so a 12-high room gave radius ≤2 (±2, 14-85 cells).
  - The data's radius cap of 16 is the worst-case reach.
  - Wind shifts the column by wind_speed per block of height when radius ≥4 and bluntness ≥0.6 (fields per the wiki's Dripstone (feature) page). The shift is unmeasured, so the reach may exceed 16 in very tall caves (see 9B).
- **Speleothem cluster (dripstone):** in a 12-high room, ±8, y ±10, 52-1,160 cells. It writes some water (wetness puddles: 238 cells in 25 clusters).
- **Speleothem cluster (sulfur):** it placed but wrote nothing in stone, because its replaceable tag excludes stone. It needs sulfur-cave terrain.
- **Pointed speleothems:** each try is found by a ≤12 scan, so the reach is the offset (≤10) plus the tip.
- **Sculk:**
  - `sculk_patch_deep_dark`: ±12, y −1..0, 221-330 cells (sculk 68%, sculk_vein 31%, a catalyst in each placed patch, sensors 3 in 10 patches). 15/25 refused in a bare stone room (cause unknown; perhaps the floor rule).
  - `sculk_vein`: from a floor spot it wrote 2 cells within ±1. `search_range` 20 is the data's worst case and is unmeasured with nearby walls (9B).
- **Lush:**
  - moss_patch floor: ±8, y −1..+1, mean 292 cells;
  - ceiling: ±8, y −7..+2 with vines;
  - clay pool: ±8, y −4..+3, mean 717 cells, including about 166 water sources per pool (waterlogged patch);
  - cave_vine: one column, up to 11 long.
  - rooted_azalea_tree wrote nothing in the probe world, because the stone surface has no `azalea_grows_on` block. Data bound: roots and hanging roots radius 3, foliage radius 3 + bend 1-2 → about ±6 horizontally, up to 100 vertically.
- **Underwater magma:** not probed, because it needs water. Data: floor search 5, radius 1 → ±1. Wiki: 1-8 blocks per cluster at the bottom of water aquifers (Magma Block).
- **Springs** (wiki Spring, Data values):
  - a spring needs `rock_count` 4 neighbours in `valid_blocks`, `hole_count` 1 air neighbour, and a valid block below;
  - the block above is excluded from both counts but must be valid;
  - water springs only below y 192.
  - Placing one needs a flowing fluid, so Q3 keeps them off by default.
- **cave_air:** 26.3 lakes and pools write `cave_air` above the fluid. Carvers write plain air (the diff: 82,058 air vs 1 cave_air). Our engine treats air as state 0; check whether `cave_air` maps to an air state in `VoxelBlocks` (S6) before writing it.

## 5B. Test oracles

Reproduce:

```
uv run --no-project --python 3.13 python -I probes\underground_probe.py run --server-dir <fresh> --out <dir>
```

Then the same with `read`. `--set2` gives the lake containment and sulfur pool cases, on another port and folder. `extents_probe.py run|read --server-dir <fresh>` covers the cave features. Each run takes 50-280 s with a headless server (`tick freeze`), using `feature_probe.py` copied from S1b.

1. **`Lake.Contained`, invariant:**
   - Run our lake feature at the S1b layered world's positions: y 40 (stone), 0 (tuff), 83 (air layer in the cave_air rows), 80, 112 (surface).
   - Expected: 0 lava cells with a non-solid, non-lava neighbour at the side or below.
   - With the gap at a lava row (y 85, 86, 87), **every** placement refuses: 0/147 placed.
   - Mutations: skipping the border check (leaks appear at y 85-87); checking only the sides (the y 88 case leaks below).
2. **`Lake.Shape`, statistical:**
   - lava only in rows y−3..y−1 and air only in y..y+2, within ±6;
   - barrier only within ±7 and y−4..y+3;
   - mean lava cells 90-107 (±20%);
   - the y−3 row holds about 2% of the lava cells.
   - Mutation: the old 16x8x16 north-west-corner box (wiki for 1.18-1.21). The extents fail.
3. **`Geode.Shape`, invariants and statistics:**
   - every bud has budding_amethyst behind it, opposite its facing (1,151/1,151);
   - budding share of the inner layer is 0.083 ± 0.02 per geode mean (measured 0.084-0.087);
   - the mean distance from the hollow's centroid orders the layers air < amethyst ≈ budding < calcite < basalt (3.9 / 5.7 / 6.8 / 7.8 ± 0.5);
   - every write is within −16..+16 of the origin (measured −10..+16) and so inside the 3x3;
   - cracked share 0.95 ± 0.05 (measured 0.94-0.98);
   - mean cells 2,050 ± 20%;
   - with an air sheet through the geode, 35-45% refuse (19/49).
   - Mutations: buds not requiring budding (`buds_elsewhere` > 0); swapped layer providers (order breaks); no invalid-block check (refusals 0).
4. **`Decoration.Reach`:** 0 drops across 1,000 columns per preset with S11 on, as the plan says. Watch sculk_vein (search 20) and large_dripstone (radius 16 plus wind); see 9B.
5. **Extents per feature, statistical, from `probes/server3/extents.json`:**
   - moss patches and clay pools ±8;
   - dripstone cluster ±8 and y ±10;
   - sculk patch ±12;
   - cave_vine one column with length ≤ 20 (data max 19 + head).
6. **Goldens:** `Decoration.Golden` (hashes and write counts) and the radius-8 bench hashes are re-recorded as "underground features on", as the plan says.

## 6B. Draft design

- **Shape:** one feature implementation per type, in the style of `VoxelOreFeature`: a namespace with `Compile` and a templated `Place(..., Context, Emit)`. A dispatch branch goes in `CompileFeature` (cpp 127) and in the `Decorate` visit lambda (303-321). Files as in the plan: `Decoration/Features/VoxelLakeFeature`, `VoxelGeodeFeature`, `VoxelDripstoneFeatures` (large, speleothem, cluster) and `VoxelCaveFeatures` (underwater magma, sculk patch, multiface, vegetation patch, root system, spring).
- **Needs from S8/S9, or S11 builds them:**
  - modifiers: `environment_scan`, `offset` (with clamped_normal), `surface_relative_threshold_filter`, `heightmap` (`WORLD_SURFACE_WG`, `OCEAN_FLOOR_WG`), `block_predicate_filter`, `random_chance`;
  - block predicates: solid, matching_blocks/tag/fluids, has_sturdy_face, inside_world_bounds, not, all_of/any_of, true;
  - providers: clamped, clamped_normal, weighted_list, biased and very_biased_to_bottom, float uniform and trapezoid;
  - composite features: sequence, simple_random_selector, random_boolean_selector, weighted_random_selector;
  - `block_column`, `simple_block` with weighted states, `tree` (for azalea);
  - the IfReplaceable write mode and states with properties.
  - S11's first step is a gap list against what S8 and S9 landed.
- **Lakes and geodes** write `Force` within the window, using the context's working copy for their checks (air, liquid, invalid blocks), as decided in plan 3.1.
- **Fluids:** sources only (lava level 0, water). There are no ticks; contained by construction (5B-1).
- **Springs:** compile and place behind `voxel.Decoration.Springs` (default 0), counted as skipped when off.
- **Biome-local early-out:** the `biome` modifier is the last modifier in every S11 placement. Each try already has its own stream (`RunModifiers` takes a stream per count try), so checking the try's biome before the scans gives the same result and skips the scans in most columns. Verify with the `Oracle` test (`DecorateAlone`).
- **Cost against 0.3 ms per column:**
  - lake underground: 1/9 x (scan ≤32 + shape ~1.5k cells) ≈ 5 µs;
  - geode: 1/24 x (box ≤25³ ≈ 15k cells x distance to 3-4 points + noise ≈ 50-100 ns) ≈ 30-60 µs. This is the largest everywhere-cost; bound the box by the generation offsets;
  - underwater magma: 48 tries, mostly rejected by the threshold or by no water, ≈ 5 µs;
  - cave biomes per column where present: dripstone (10-48 large + 48-96 clusters + up to 1,280 scans) and lush (125 + 125 patches of ~225 columns x 5 + 188 vines + 62 pools) about 0.3-1 ms; deep dark (256 sculk patches with 10 charges) maybe 1 ms or more.
  - **Mean = 0.05-0.1 ms + Σ (cave-biome share x per-column cost).** At a 10% combined share that is about 0.15-0.25 ms, so it fits only if the share is modest. Measure the share first, with the biome source: the fraction of columns whose 3x3 holds each cave biome at any depth.

## 7B. Code-shape sketches (against 015bb42)

Assumes `Decoration/VoxelDecorator.h/.cpp`, `VoxelDecorationContext.h`, `VoxelWriteLog.h` and `Features/VoxelOreFeature.h` at 015bb42. The names S8 and S9 add are guesses; re-orient first.

```cpp
// Decoration/Features/VoxelLakeFeature.h
namespace VoxelLakeFeature {
  struct FConfig { uint16 Fluid, Barrier, CaveAir; FVoxelBlockPredicate CanPlace, CanReplaceWithAirOrFluid, CanReplaceWithBarrier; };
  bool Compile(const FVoxelFeatureGraph&, int32 Node, FVoxelBlockTags&, const FVoxelBlockTable&, FConfig&, FString& OutSkip, FString* OutError);
  // Shape: our own blob mask in a 13x7x13 box around Origin (rows -4..+3); refuse when any fluid cell touches
  // air or a non-solid cell at its sides or below, or when an air-row cell touches fluid; then fluid rows,
  // cave-air rows, barrier on the solid boundary.
  template <typename FEmit> bool Place(const FConfig&, FIntVector Origin, FVoxelXoroshiro&, FVoxelDecorationContext&, FEmit&&);
}
// Decoration/Features/VoxelGeodeFeature.h
namespace VoxelGeodeFeature {
  struct FConfig { uint16 Filling, Inner, AltInner, Middle, Outer; TArray<uint16, TInlineAllocator<4>> Buds;
    double Layers[4] /*1.7 2.2 3.2 4.2*/, CrackChance, CrackSize; int32 CrackOffset; double PotentialChance, AltChance;
    bool bBudsNeedAlt; FVoxelIntProvider WallDistance, Points, PointOffset; int32 MinOffset, MaxOffset; double NoiseScale; int32 InvalidThreshold;
    FVoxelBlockSet CannotReplace, Invalid; };
  template <typename FEmit> bool Place(const FConfig&, FIntVector Origin, FVoxelXoroshiro&, const FVoxelNoise& ShellNoise, FVoxelDecorationContext&, FEmit&&);
}
// VoxelDecorator.h FFeature: add  int32 Lake = INDEX_NONE, Geode = INDEX_NONE, ...;  or one  {EVoxelFeatureKind Kind; int32 Index;}
// CompileFeature: if (Type == "minecraft:lake") ... else if (Type == "minecraft:geode") ... else OutSkip = Type;
```

## 8B. Re-orientation list (diff these first)

- **S8 (plants and patches) and S9 (trees), which land before S11:**
  - `Decoration/VoxelDecorator.h/.cpp`: dispatch, new kinds, modifiers;
  - `VoxelFeatureGraph.*`, `VoxelNumberProviders.*`, `VoxelRuleTests.*` (or new block predicates);
  - `VoxelDecorationContext.h`: heightmaps and helpers;
  - `VoxelWriteLog.h`: IfReplaceable and support;
  - `Decoration.Golden` constants;
  - `Docs/VoxelWorld.md` 444-484.
  - S11 then reuses `block_column`, `simple_block`, the selectors, `tree` and the vegetation pieces from them.
- **S6 (block table, mesher, chunk component, VoxelBlocks, pipeline switches):**
  - `Blocks/VoxelBlockTable.h` and `Source/Unnamed/Voxel/VoxelBlocks.cpp`: state lookup with properties, and `IsAir` for cave_air;
  - the mesher: pointed dripstone, buds, dripleaf, vines and sculk vein models;
  - pipeline switches: where a `Springs` switch goes.
- **S6b (Playground meshes and materials):** no S11 code, but the block gallery shots of cave blocks change.
- **S4:** `VoxelBench.cpp` and `VoxelConsole.cpp` (bench arguments), which S11 extends for `voxel.Bench Decorate`.
- **Torch probe:** the VoxelWorld shot code behind `CaptureVoxel -Caves`, which is S11's evidence (geode, lush, dripstone, deep dark, sulfur, lava lake).

## 9B. Risks, size, questions

- **Size L. Split recommended:**
  - **S11a (M):** lakes, sulfur_pool, geode, underwater magma, springs (data, off), large dripstone, speleothem and speleothem_cluster (dripstone and sulfur). Mostly new shapes, with few composite pieces needed.
  - **S11b (M):** lush caves (vegetation_patch and its waterlogged form, cave vines, spore blossom, clay and dripleaf, root_system + azalea tree) and sculk (sculk_patch with charges, multiface_growth). This leans on S8/S9 pieces and has the highest cost risk.
- **Risks:**
  1. **Cave-biome coverage drives the mean cost.** It is unmeasured; measure it first.
  2. **Reach:**
     - large_dripstone at radius 16 is exactly the window's edge, and wind may push past it. Clamp the wind offset so writes stay inside the window, as a recorded difference, or count drops.
     - sculk_vein's `search_range` 20 can step past 16 when the origin is far from a face. Clamp it to the window, also a recorded difference.
     - `Decoration.Reach` must stay 0.
  3. **States with properties** (dripleaf halves and facings, bud facings, cave_vines berries, pointed_dripstone thickness and direction, sculk_vein faces) need the property-aware state lookup that `UnnamedVoxel` lacks at 015bb42 (it lives in `FoundationBlockRegistry`). S6 may change this.
  4. **Speleothem types are 26.x-era** (wiki Dripstone (feature): `pointed_dripstone` → `speleothem` in 26.2, `large_dripstone` → `large_speleothem` coming in 26.4). The 26.3 data is authoritative; the wiki's field ranges help with validation.
  5. Waterlogged patches and dripstone puddles place water sources. Their containment is unmeasured, so a Lake.Contained-style check on them is worth adding.
- **S11-Q1, sculk catalysts and sensors** (block-entity blocks placed by sculk patches; 10 catalysts and 3 sensors in 10 probe patches):
  - (a) Recommended: place the plain block states now, without block entities. They look right, and their behaviour waits for phase 7's block entities.
  - (b) Leave both out until phase 7.
- **S11-Q2, rooted_sulfur_spring** (its spring is a template):
  - (a) Recommended: defer the whole feature to phase 7, counted as skipped.
  - (b) Place its sulfur roots and tuff scatter now, without the spring structure. That looks unfinished.
- **S11-Q3, split S11:**
  - (a) Recommended: S11a and S11b as above, each with its own 0.3 ms share. S11a about 0.1 ms; S11b the rest.
  - (b) One session, at L size and with a high chance of a mid-session handoff.

---

## Token use

The figures for this scout are in the report to Nyx.
