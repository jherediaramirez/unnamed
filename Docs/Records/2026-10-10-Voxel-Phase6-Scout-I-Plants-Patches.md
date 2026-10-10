# Scout I: S8 plants and patches, facts (against 015bb42)

Scout for phase 6 S8 (vegetation feature types), October 10, 2026. Read-only over `C:\dev\projects\Unnamed-scout` at 015bb42. Tools and raw outputs sit beside this file:
- `tools/inventory.py`: walks the reached data from the 56 biomes and writes `notes/inventory.md` and `.json`.
- `tools/cost.py`: the per-biome work, written to `notes/cost.md`.
- `notes/wiki-behaviour.md`: the wiki's descriptions, in our words.
- `probes/`: the vanilla probe harness and its results.

Reproduce: `python -I tools/inventory.py <Unnamed-scout>/Reference/minecraft-worldgen-26.3 notes`, then `python -I tools/cost.py notes`.

## 1. Summary and recommendations

**What S8 turns on.**
- Of the 171 top-level placed features, **82 reach only S8's feature types** (every feature in them, nested ones included, is `simple_block`, `block_column`, `disk`, a selector, `sequence`, `no_op`, `multiface_growth` or a vegetation patch). Under S2's rule (a placed feature runs once all its types are built), all 82 start running in S8.
- **7 of the 82 belong to other sessions' scope:**
  - S11: `lush_caves_vegetation`, `lush_caves_ceiling_vegetation`, `lush_caves_clay`, `cave_vines`, `spore_blossom`, `sculk_vein`.
  - S12: `ice_patch` (a `disk`).
- **30 more reach S8 types inside other types:** trees behind `random_selector`, `mushroom_island_vegetation`, `warm_ocean_vegetation`, pointed dripstone, sculk patch, sulfur pool. These stay skipped until S9, S11 and S12.
- **Steps:** 76 of the 82 run in `vegetal_decoration`, 4 in `underground_ores` (the disks), 1 in `underground_decoration` (sculk vein) and 1 in `surface_structures` (ice patch).

**Which of S8's 11 new modifiers have an S8 user.** Five have none:
- `surface_water_depth_filter`: trees only (S9).
- `environment_scan`: lush caves and dripstone (S11), plus the two `*_on_snow` trees (S9).
- `random_chance`: coral (S12) and the sculk patch (S11).
- `cuboid`: warm ocean vegetation (S12).
- `randomly_selected`: the desert well, a template (phase 7).

S8 builds them as the plan says, but only unit tests can cover them in S8. `noise_based_count` has only kelp as an S8 user, and `surface_relative_threshold_filter` only glow lichen.

**Providers.**
- There is no `simple_state_provider` type in 26.3's files. A simple provider is a bare block state, either `{"id", "properties"}` (170 uses) or a bare id string (62).
- 7 named providers are used: `flower_plain` (`noise_threshold`), `flower_meadow` (`dual_noise`), `flower_flower_forest` (`noise`), and the two `cave_vines_*` (S11). `mangrove_propagule`, `podzol_beneath_tree` and `soil_beneath_tree` are reached only through trees (S9).
- All three noise providers carry `"seed": 2345` and the 26.3 noise form `{"base_octave", "base_amplitude"}`, which S1's reader already reads (`noise_parameters`).

**Cost.** S8's own features come to about 60 to 110 µs per column at the unit costs assumed in 5.3, about 15 to 30% of the 0.4 ms gate.
- The work is mostly biome lookups (136 to 240 per column) and surface patch tries: up to 800 in jungle, 720 in cherry grove and 712 in savanna.
- The risks:
  - the unit costs are assumptions, not measurements;
  - seagrass and kelp check the biome on every try, because `biome` comes last in their chains;
  - the lush caves patches, if they turn on in S8, are not counted: each found floor or ceiling scans hundreds to about 2,000 cells.

**Vanilla probes (5.2):** 17 probes, 100 to 200 origins each.
- **Confirmed:**
  - the analytic patch counts (grass 7.85 against 7.79, tall grass 22.1 against 22.1, seagrass 41.4 against 41.0 cells);
  - survival at placement;
  - double-plant upper halves;
  - the `biased_to_bottom` heights;
  - the cactus side rule;
  - the disk footprint dx² + dz² <= r², with `target` checked per cell;
  - the seagrass and pickle weights.
- **Open:**
  - kelp trimming (one cell short in a 4-deep pool);
  - the lichen search (local in practice);
  - the vegetation patch footprint.

**Recommendations.**
1. **Extend S2's machinery; don't fork it.**
   - Add modifier types to `FVoxelDecorator::FModifier` and its `RunModifiers` switch (`VoxelDecorator.cpp:202-243`). Keep the rules S2 set there: each modifier gives its position a new seed, and every count try gets its own stream from (stream, try).
   - Nested placed features (selectors, `vegetation_feature`) compile into the same modifier lists.
2. **Live heightmaps in the working copy.**
   - `heightmap` reads `MOTION_BLOCKING`, `MOTION_BLOCKING_NO_LEAVES` and `OCEAN_FLOOR` from the working copy, because earlier writes in the same column raise them (pumpkin, melon, cactus, sugar cane). The `_WG` maps come from the snapshot.
   - **Off by one:** a vanilla heightmap value is the first cell above the top block. Our `FVoxelColumn::GetHeight` returns the top block itself, so add 1.
3. **One survival table in the core,** compiled from the support tags in `CODE_TAGS`. `simple_block` and `would_survive` use it, and so does S7's placement (shared, not copied). Writes carry the S2 support slot (`EVoxelWriteSupport`: "the block below is in tag X"), so Finalize drops a plant whose support a neighbour's write removed.
4. **Plant writes:** `IfReplaceable` over air or `#replaceable`; disks `IfTargets`-like, with their target predicate re-checked at replay. A double plant is two writes with the same support.
5. **Pattern noises:**
   - `noise_threshold_count`, `noise_based_count` and the three flower providers take `VoxelFeatureSeeds::GetPatternSeed(WorldSeed, 2345)` (already in `VoxelFeatureOrder.h:46`; Q4).
   - Our noise is not bit-compatible with the jar's, so oracles are distributions, not positions.
6. **Keep S2's run rule,** so the S11 and S12 seven turn on with S8. If the gate fails, `FVoxelDecorationOptions::Filter` (it exists) holds them back. Question 1.
7. **`multiface_growth` can reach outside the 3x3:** glow lichen's `search_range` is 20 around an origin anywhere in the column. Clamp the search to the 48 x 48 window (question 3) so `Decoration.Reach` stays at 0 drops.
8. **Tests:**
   - `Providers.Distribution` gains `biased_to_bottom`, `weighted_list` and `clamped`: 2 + 16 + 3 histograms already in `facts/provider_histograms.json`.
   - Patch statistics against the analytic values (5.1) and the vanilla probes (5.2).
   - Units for every modifier and predicate, the five without S8 users included.

## 2. Reading list for the worker (at 015bb42, in order)

1. `Docs/Records/2026-10-09-Voxel-Phase6-Plan.md` 96-173 (3.1, 3.2, 3.5), 405-429 (S8), 643-667 (answers; Q4 and Q6 matter here).
2. `Docs/VoxelWorld.md` 444-484 (Decoration: stages, pieces, gate table).
3. `Source/UnnamedVoxel/Decoration/VoxelDecorator.h` 1-171 (whole: `FModifier`, `FFeature`, `RunModifiers`, `Options.Filter`).
4. `Source/UnnamedVoxel/Decoration/VoxelDecorator.cpp`:
   - 119-183, `CompileFeature`: the type check and the modifier compile loop;
   - 202-243, `RunModifiers`;
   - 280-326, the Decorate loop and the ore's write emission, the pattern for plants.
5. `Source/UnnamedVoxel/Decoration/VoxelDecorationContext.h` 1-86 (`GetState`, `SetState`, `GetBiome`, `GetTopZ`; no heightmap yet).
6. `Source/UnnamedVoxel/Decoration/VoxelWriteLog.h` 1-60 (modes, the support slot and its 3-bit / 10-bit packing).
7. `Source/UnnamedVoxel/Decoration/VoxelNumberProviders.h` 1-60 (`FVoxelIntProvider`: constant, uniform, trapezoid only).
8. `Source/UnnamedVoxel/Decoration/VoxelRuleTests.h` 1-68 (bands of Z times block bit sets; reuse for predicates).
9. `Source/UnnamedVoxel/Storage/VoxelColumn.h` 8-50 (`EVoxelHeightmap`, `GetHeight`).
10. `Source/UnnamedVoxel/Decoration/VoxelFeatureOrder.h` 25-50 (salts, `GetStreamSeed`, `GetPatternSeed`, `VanillaPatternSeed`).
11. `Source/Unnamed/Tests/VoxelDecorationTests.cpp` 802-870 (`Providers.Distribution`; the type filter at 834-836).
12. `Reference/minecraft-worldgen-26.3/tools/build_reference.py` 32-57 (`CODE_TAGS`: the support tags and who reads them).
13. This folder: `notes/inventory.md` (every S8 placed feature with its chain, biomes and counts), `probes/results.md`, `notes/wiki-behaviour.md`.

## 3. Data inventory

Sources:
- `Reference/minecraft-worldgen-26.3/data/minecraft/worldgen/`: `biome/` (56), `placed_feature/` (210), `feature/` (182, 5 under `coral/`), `block_state_provider/` (8);
- `tags/block/` (127);
- `facts/provider_histograms.json` (119), `facts/feature_order.json`.

### 3.1 Type occurrences in the data files (each file counted once; `tools/types.py`)

| Group | Type: occurrences |
|---|---|
| Modifiers (S2 has) | count 174, biome 171, in_square 170, height_range 59, rarity_filter 51 |
| Modifiers (S8) | block_predicate_filter 154, offset 115 (10 of them inside `randomly_selected`), heightmap 109, surface_water_depth_filter 26, environment_scan 20, random_chance 10, noise_threshold_count 6, cuboid 5, noise_based_count 4, surface_relative_threshold_filter 3, randomly_selected 2 |
| Predicates (13 used) | matching_block_tag 113, matching_blocks 85, would_survive 51, all_of 38, matching_fluids 31, any_of 31, solid 21, not 15 (some are other types' `not`), has_sturdy_face 4, replaceable 2, volume_match 1, inside_world_bounds 1, true 1. The 14th type is unused (see 4.2). |
| Block state providers | (simple: bare states) weighted 38, rule_based 4, randomized_int 4, rotated 4 (coral, S12), random_block 5 (coral, S12), noise_threshold 1, noise 1, dual_noise 1 |
| Features (S8) | simple_block 60, random_selector 21, block_column 9, weighted_random_selector 7, sequence 6, disk 5, simple_random_selector 5, vegetation_patch 4, multiface_growth 2, random_boolean_selector 2, waterlogged_vegetation_patch 1, no_op 1 |
| Int providers S8 adds | weighted_list 32 (in files), biased_to_bottom 2, clamped 3, very_biased_to_bottom 2 (S11), clamped_normal 7 (S11 and S9) |

### 3.2 Parameters seen (uses walked from the biomes)

- **heightmap:** MOTION_BLOCKING 45, OCEAN_FLOOR 37, WORLD_SURFACE_WG 19, OCEAN_FLOOR_WG 5, MOTION_BLOCKING_NO_LEAVES 3 (`pale_garden_flowers`, `pale_moss_patch`, `patch_firefly_bush_near_water`).
- **offset**, x / y / z, trapezoids with plateau 0:
  - ±7 / ±3 / ±7: 49 uses;
  - ±7 / 0 / ±7 (seagrass, sea pickle): 9;
  - ±6 / ±2 / ±6 (flowers 96 and 64, wildflowers, cherry): 7;
  - ±4 / 0 / ±4 (sugar cane): 4;
  - ±4 / ±3 / ±4 (firefly bush): 3;
  - ±5 / ±3 / ±5 (bush): 1.

  Constants and other providers:
  - (0, -1, 0): 11;
  - (0, 1, 0): 4;
  - (0, uniform -3..-1, 0): 5;
  - clamped_normal ±10 / ±2 / ±10 (pointed dripstone, S11): 2.

  The largest S8 reach is ±7 around an `in_square` origin.
- **count:** 96 (28), 2 (13), 20 (13), 32 (11), 64 (10), 4 (9), weighted_list {10: 9, 11: 1} (8), 10 (6), 3 (6), 16 (5), 25 (4), 7, 8; uniform 104..157 (glow lichen), 204..250 (sculk vein); clamped (uniform -3..1 into 0..1) and (uniform -1..3 into 0..3) (the two forest flower selectors).
- **rarity_filter:** 2 to 384 (S8 users: 2, 3, 4, 5, 6, 7, 8, 13, 16, 32, 64, 171, 256, 300, 384, 512).
- **noise_threshold_count** (all six are S8's): level -0.8 with below / above 5 / 10 (`patch_grass_plain`, `patch_grass_meadow`, `flower_cherry`, `wildflowers_meadow`), 0 / 7 (`patch_tall_grass_2`), 15 / 4 (`flower_plains`).
- **noise_based_count:**
  - `kelp_cold`: ratio 120, factor 80;
  - `kelp_warm`: ratio 80, factor 80;
  - `bamboo` (S9): ratio 160, factor 80, offset 0.3;
  - `warm_ocean_vegetation` (S12): ratio 20, factor 400.
- **surface_relative_threshold_filter:** OCEAN_FLOOR_WG, max -13 (glow lichen), -5 and -2 (S11).
- **environment_scan:**
  - up 12, `solid` while air (4);
  - down 12, solid while air (2);
  - up or down 12, solid while air or water (2 each);
  - up 12, sturdy(down) while air (cave vines);
  - up 8, not powder snow (2, S9);
  - three single uses in S11.
- **block_predicate_filter in S8's placed features:**
  - `#air` (most);
  - `all_of(#air, =grass_block below)`: berries, pumpkin, leaf litter;
  - `all_of(replaceable, fluid=empty, =grass_block below)`: melon;
  - `all_of(#air, would_survive(X), any_of(water or flowing_water fluid at the 4 cells beside the one below))`: sugar cane x4, firefly bush near water x2;
  - `all_of(#air, would_survive(cactus))`;
  - `all_of(#air, !=podzol below)`: jungle grass;
  - `=water`: seagrass, sea pickle;
  - kelp: `all_of(=water, =water above, sturdy(up) below, !#cannot_support_kelp below)`;
  - disks: `fluid=water` (matching_fluids at the OCEAN_FLOOR_WG origin), `=mud` (disk_grass after offset -1), `=snow_block` (ice patch).
- **Features:**
  - `block_column` (S8's):
    - sugar cane: up, height biased_to_bottom 2..4, allowed `#air`, prioritize_tip false;
    - cactus: up, cactus biased_to_bottom 1..3, then cactus_flower weighted_list {0: 3, 1: 1}, tip false;
    - kelp: up, kelp_plant uniform 0..9 then one `kelp` with `randomized_int age 20..23`, allowed water with water above, tip true.
  - `disk`, radius / half height / target / provider:
    - sand: uniform 2..6 / 2 / dirt, grass_block / `rule_based`: sandstone if air below, else sand;
    - gravel: 2..5 / 2 / dirt, grass_block / gravel;
    - clay: 2..3 / 1 / dirt, clay / clay;
    - grass: 2..6 / 2 / dirt, mud / a rule-based dirt or grass_block;
    - ice patch (S12): 2..3 / 1 / 7 soils, snow and ice / packed_ice.
  - `vegetation_patch` (S8's `pale_moss_patch`): floor, xz radius uniform 2..4, depth 1, vertical range 5, ground pale_moss_block, replaceable `#moss_replaceable`, vegetation 0.30, edge 0.75, bottom 0.
  - `multiface_growth` glow lichen: search range 20, wall and ceiling (no floor), 10 base blocks (stone, andesite, diorite, granite, dripstone, calcite, tuff, deepslate, sulfur, cinnabar).
- **Selectors:**
  - `simple_random_selector`: `forest_flowers` and `flower_forest_flowers`, 4 double plants or flowers, each 96 tries;
  - `weighted_random_selector`: the 6 seagrass features, tall (with a water-above filter) against short, 80 / 20, 60 / 40 or 30 / 70.
- **Blocks the S8 features place:**
  - grasses and shrubs: short_grass, tall_grass, fern, large_fern, bush, red_shrub, firefly_bush, dead_bush, short and tall dry grass;
  - flowers: 14 (dandelion, poppy, blue_orchid, allium, azure_bluet, four tulips, oxeye_daisy, cornflower, lily_of_the_valley, closed_eyeblossom, pink_petals);
  - ground cover: wildflowers and leaf_litter (4 facings x amounts, weighted);
  - double plants: sunflower, lilac, rose_bush, peony;
  - water and stems: sugar_cane, cactus, cactus_flower, kelp, kelp_plant, seagrass, tall_seagrass, sea_pickle (1 to 4, waterlogged), lily_pad;
  - crops and fungi: sweet_berry_bush (age 3), pumpkin, melon, brown and red mushroom;
  - glow_lichen;
  - ground blocks: pale_moss_block and pale_moss_carpet, sand, sandstone, gravel, clay, dirt, grass_block.
  - S11's set adds moss, azalea, cave vines, spore blossom, dripleaf and clay; S12's adds packed_ice.

### 3.3 The 82 placed features, condensed

Per chunk (16 x 16 column): E is the mean number of positions reaching the feature, from the modifier chain. Noise counts are taken at their midpoint here; in practice the noise is rarely below -0.8, so the "above" count is typical. Full chains and biome lists are in `notes/inventory.md`.

| Group (biomes) | Placed features (E tries / chunk) |
|---|---|
| everywhere | `glow_lichen` (131 origins, all 56), `disk_clay` (1), `disk_gravel` (1), `disk_sand` (3) (54-56), `patch_pumpkin` (0.32 x 96 tries, 46), `patch_firefly_bush_near_water` (40, 42), `brown_mushroom_normal` / `red_mushroom_normal` (0.375 / 0.19, 41), `patch_sugar_cane` (3.3 x 2.5 cells, 38), `flower_default` (2, 29), `patch_grass_badlands` (32, 24) |
| plains, meadow | `patch_grass_plain` (240), `flower_plains` (noise 15 / 4), `patch_tall_grass_2`, `patch_sunflower` (32), `flower_meadow` (96, `dual_noise`), `wildflowers_meadow` (60), `patch_grass_meadow` (120) |
| forest | `patch_grass_forest` (64), `forest_flowers` (0.03 x 96), `flower_flower_forest` (144, `noise`), `flower_forest_flowers`, `wildflowers_birch_forest` (96), `flower_cherry` (720, pink petals), `patch_leaf_litter` (64), `pale_moss_patch` (1 patch), `pale_garden_flowers`, `flower_pale_garden`, `brown_mushroom_dappled_forest` (48), `patch_red_shrub` (8), `patch_bush` (6) |
| taiga | `patch_grass_taiga` (224), `patch_grass_taiga_2` (32), `patch_large_fern` (19), `patch_berry_common` (3), `patch_berry_rare` (0.25), `brown_mushroom_taiga` / `_old_growth`, `patch_dead_bush` |
| jungle | `patch_grass_jungle` (800), `patch_melon` (10.7), `patch_melon_sparse` (1), `flower_warm` (4) |
| savanna | `patch_grass_savanna` (640), `patch_tall_grass` (19), `flower_warm` |
| dry | `patch_dead_bush_badlands` (80), `patch_dry_grass_badlands` (10.7), `patch_dry_grass_desert` (21), `patch_dead_bush_2` (8), `patch_cactus_desert` (1.7 x 1.75), `patch_cactus_decorated`, `patch_sugar_cane_desert` (20 x 2.5), `patch_sugar_cane_badlands` |
| swamp | `patch_waterlily` (40), `seagrass_swamp` (64), `flower_swamp` (2), `brown_mushroom_swamp` (192), `red_mushroom_swamp`, `patch_grass_normal` (160), `patch_dead_bush`, `patch_sugar_cane_swamp`, `patch_firefly_bush_swamp`, `patch_firefly_bush_near_water_swamp` (60), `disk_grass` (mangrove swamp, 1 disk) |
| ocean, river | `seagrass_*` (32 to 80), `kelp_cold` (4 oceans, up to 120), `kelp_warm` (lukewarm, up to 80), `sea_pickle` (warm, 1.25) |
| S11 / S12 | `lush_caves_vegetation` (125 origins), `lush_caves_ceiling_vegetation` (125), `lush_caves_clay` (62), `cave_vines` (188), `spore_blossom` (25), `sculk_vein` (227); `ice_patch` (2) |

## 4. Behaviour in our own words

Sources:
- [W] Placed feature, https://minecraft.wiki/w/Placed_feature#Placement_modifiers;
- [P] Block predicate, https://minecraft.wiki/w/Block_predicate;
- [S] Block state provider, https://minecraft.wiki/w/Block_state_provider;
- [F] Configured feature, https://minecraft.wiki/w/Configured_feature;
- [D] the reference data;
- [B] the black-box probes (5.2).

The wiki notes came through a summarizing fetch (`notes/wiki-behaviour.md`); spot-check before pinning an exact edge case.

### 4.1 Modifiers [W]

- **heightmap:** Y becomes one above the chosen map's top, which is vanilla's heightmap value. Our `GetHeight` returns the top, so add 1. The maps:
  - WORLD_SURFACE and OCEAN_FLOOR come in `_WG` forms;
  - MOTION_BLOCKING and MOTION_BLOCKING_NO_LEAVES are live, raised by earlier writes.
- **offset** (26.3's rename of `random_offset`): adds three int providers, each -16..16.
- **block_predicate_filter:** keeps the position when the predicate holds.
- **surface_water_depth_filter:** counts the water above the motion-blocking surface. It passes only when that count is strictly below `max_water_depth`, so 0 means dry land only. Trees only (S9).
- **environment_scan:**
  - steps up to `max_steps` (1..32) in `direction_of_search`;
  - returns the first position where `target_condition` holds;
  - stops, with nothing, when `allowed_search_condition` fails or the steps run out;
  - whether the start cell is tested is not stated: measure it, or test both in a unit and pick by S11's caves.
- **random_chance:** passes with probability `chance` (26.3).
- **noise_threshold_count:** 2D noise at (x / 200, z / 200). Strictly below `noise_level` emits `below_noise` copies, otherwise `above_noise`.
- **noise_based_count:** count = ceil((noise(x / factor, z / factor) + offset) x ratio). Values at or below 0 emit nothing.
- **Which noise:** the wiki does not name the one both read. Ours is a 2D pattern noise from `GetPatternSeed` (Q4). Its value range sets how often below -0.8 occurs, so measure that fraction and report it beside the patch counts.
- **surface_relative_threshold_filter:** keeps the position when (Y minus the map's height) is in [min, max]. The subtraction is implied, not written.
- **cuboid (26.3):** repeats the position over a box of `xz_size` by `y_size`; `include_edges` false rounds it and `include_interior` false hollows it. S12's coral only.
- **randomly_selected (26.3):** applies one of its modifier lists, chosen at random (assume uniform). Desert well only.
- **count:** 0..4096, and counts multiply.
- **biome** in a nested placed feature is a crash in vanilla, so the data never does it. `Validate` should refuse it too.

### 4.2 Predicates [P]

The wiki lists 16 types. The data uses 13; `unobstructed`, `height_range` (26.3) and `matching_biomes` are unused. The offset is added to the tested position.
- **Combinators:** `all_of`, `any_of`, `not`, `true`.
- **matching_blocks / matching_block_tag:** the block at the offset is in the set or tag.
- **matching_fluids:** the fluid there is in the set. Waterlogged blocks and flowing water count, which is how sugar cane's water check differs from `=water`.
- **solid:** the block at the offset is solid.
- **has_sturdy_face:** the face toward `direction` is a full support.
- **replaceable:** the block at the offset is in `#replaceable`.
- **would_survive:** the state would survive at the offset (the survival table).
- **inside_world_bounds:** Y is in the world.
- **volume_match (26.3):** every cell of the box matches `match`.
- **Open:** empty `all_of` / `any_of` (not in the data) and out-of-world reads. Treat out-of-world as air, as `GetState` does.

### 4.3 Providers [S] [D]

- **simple:** an inline state.
- **weighted:** by weight.
- **rule_based:** the first rule whose predicate holds, else `fallback`.
- **randomized_int:** sets an int property from an int provider (kelp age 20-23, pickles 1-4).
- **rotated:** sets axis or facing (coral, S12).
- **random_block:** the default state of a random member (coral, S12).
- **noise:** picks from `states` by the noise value at (x, z) x scale. The mapping from noise to index is not stated; ours is the noise mapped linearly over [-1, 1) to an index, an assumption.
- **noise_threshold:** see 5.1.
- **dual_noise:** a slow noise picks how many states (`variety` 1..3 for the meadow) and which subset; the fast noise picks among them. The details are not stated, so design our own and pin our own distribution.

### 4.4 Features [F] [D] [B]

- **simple_block:** places the provider's state; `schedule_tick` is absent in the data. The survival check and the upper half of a double plant are not on the wiki; the probes measure both (5.2: patches on stone and on grass, tall grass halves).
- **block_column:** layers in order, each its own height draw, along `direction`; every cell must pass `allowed_placement`.
  - When the free run is shorter than the total, cells are trimmed. With `prioritize_tip` the base layers lose cells first, so the tip survives (kelp keeps its top); without it, the column is cut from the top.
  - Measured in POOL4 (5.2). The wiki states only the tip rule.
- **disk:** a flat cylinder: radius 0..8, half height 0..4, cells matching `target` take the rule-based state. The shape (dx² + dz² <= r² or similar) and where `target` is checked come from the probes (5.2).
- **Selectors:**
  - `random_selector`: entries in order, each taken with its `chance`, else `default`;
  - `simple_random_selector`: uniform;
  - `random_boolean_selector`: a fair coin;
  - `weighted_random_selector` (26.2): by weight.
  
  Assumed where the wiki is silent; pin our own distributions.
- **sequence** (26.2): runs its placed features in order. Whether it stops at a failure is not stated. Its only users are S11's.
- **no_op:** nothing.
- **multiface_growth:** places `block` on a face of an air or water cell that touches an allowed base block. Floor, wall and ceiling are switchable. The search range defaults to 10, the data's is 20, and `chance_of_spreading` is used for sculk.
  - **Probe [B]:** an origin 4 cells from every face fails; an origin beside a wall places about 1.6 faces on that wall.
  - The search order is not on the wiki, so design our own, local around the origin and clamped to the window (Q3).
- **vegetation_patch:**
  - finds the floor (or ceiling) within `vertical_range` of each footprint column;
  - replaces `depth` cells (+1 with `extra_bottom_block_chance`) of `#replaceable_*` ground with `ground_state`;
  - edge columns are kept with `extra_edge_column_chance`;
  - then runs the vegetation feature on each new ground cell with `vegetation_chance`;
  - the waterlogged form also fills the footprint's surface with water.
  
  The footprint and order come from the probes (pale moss, 5.2).

## 5. Test oracles

### 5.1 Exact values from the data (no jar needed)

- **Trapezoid offsets** (plateau 0: the sum of two uniforms over 0..a, minus a; S2's `FVoxelIntProvider` already matches these histograms):
  - P(0) is 0.125 for ±7, 0.1429 for ±6, 0.2 for ±4, 0.25 for ±3 and 0.3333 for ±2;
  - nothing lands beyond ±a.
- **Patches on flat ground** (origin on the first air cell above a flat field). Only dy = 0 tries survive: below is solid, above has no support. The table gives E[distinct cells planted] = sum over cells of 1 - (1 - p)^n, as computed in this scout:

| Patch (tries, xz, y) | Tries at dy = 0 | Distinct cells |
|---|---|---|
| short grass (32, ±7, ±3) | 8.00 | 7.79 |
| tall grass, large fern, 96-try flowers, berries, mushrooms (96, ±7, ±3) | 24.0 | 22.12 |
| flower_meadow, flower_cherry, flower_flower_forest (96, ±6, ±2) | 32.0 | 27.83 |
| flower_default, flower_warm, flower_swamp... (64, ±7, ±3) | 16.0 | 15.15 |
| flower_plains, wildflowers (64, ±6, ±2) | 21.3 | 19.42 |
| seagrass_normal on a flat sea floor (48, ±7, y 0) | 48 | 41.02 |
| seagrass_warm (80, ±7, y 0) | 80 | 62.02 |
| waterlily on a flat pond (10, ±7, ±3) | 2.5 | 2.48 |

- **Int providers S8 adds** (`facts/provider_histograms.json`, 10^6 samples; indices are list positions):
  - [98] `biased_to_bottom` 2..4 (sugar cane): 2: 0.6114, 3: 0.2780, 4: 0.1107. Analytic: the value is min + U(0, U(1, max - min + 1) - 1) with nested uniforms, which gives 0.6111 / 0.2778 / 0.1111.
  - [63] `biased_to_bottom` 1..3 (cactus): 0.6110 / 0.2783 / 0.1107.
  - [64] `weighted_list` {0: 3, 1: 1} (cactus flower): 0.7509 / 0.2491.
  - [12] `clamped(uniform -3..1, 0..1)` (`forest_flowers` count): 0.8 / 0.2.
  - [11] `clamped(uniform -1..3, 0..3)` (`flower_forest_flowers`): 0: 0.399, then 0.2 each for 1, 2 and 3.
  - The other 14 `weighted_list` histograms (trees, S9) and [65], [66] (cave vines, S11) come free with the type.
- **Weighted block state providers** (exact weights):
  - `leaf_litter` and `wildflower`: 16 states, 4 facings x amounts 1-4, weight 1 each;
  - `dry_grass`: short / tall, 1 : 1;
  - `flower_default`: dandelion 2 : poppy 1;
  - `flower_cherry`: pink_petals, 16 states;
  - seagrass selectors: 80 / 20, 60 / 40, 30 / 70.

  `Providers.Distribution` can pin them by drawing 10^6 states per provider.
- **`noise_threshold` (`flower_plain`), the rule:** below the threshold (-0.8), one of the 4 tulips, uniform. At or above it, with chance 1/3 (0.33333334) one of poppy, azure_bluet, oxeye_daisy and cornflower, uniform; otherwise dandelion. Pin the rule with the noise value injected, and the fractions given a noise fraction q below -0.8: tulips q/4 each, the four high states (1 - q)/12 each, dandelion 2(1 - q)/3.

### 5.2 Vanilla probes

The setup:
- **The run:** a headless 26.3 server on a superflat plains world: bedrock -64, stone -63..59, dirt 60..62, grass 63. Tick frozen, so these are placement-time results, not survival ticks.
- **Placing:** `/place feature`, plus a `probe` data pack whose features are `simple_random_selector`s around one inline placed feature. That inline feature is the vanilla patch's tail, copied from its `count` onward and without `biome`.
- **Reading back:** from the region files. Nothing changed beyond ±12 xz of any origin.
- **Details and reproduction:** `probes/results.md` (the reproduce commands, the pads, the origin grid) and `probes/results_auto.md` (every histogram). The final data came from two server folders (stage A in `server/`, stage B in `server-b/` via `runb.sh`), and `plant_probe.py read` combines them.
- **Statistics:** vanilla's `/place` randomness is unseeded. Pin means within a confidence bound and supports exactly; never pin positions.

| Probe (origins) | Measured | Expected / exact rule to pin | Mutation it catches |
|---|---|---|---|
| short grass patch, 32 tries ±7 / ±3 (100) | 7.85 blocks per origin (2..17); dx, dz in -7..7, triangular; dy always 0 | 7.79 analytic; dy = 0 only on flat ground (survival checked) | no survival check: dy = +1..3 tries place floating grass; uniform instead of trapezoid: flat dx histogram |
| tall grass patch, 96 tries (100) | 22.09 lower halves (16..31); uppers equal lowers, 0 orphans | 22.12 analytic; `simple_block` places the upper half of a double plant | upper half missing; a later try overwrites an upper half |
| `forest_flowers` (100) | one flower type per origin, never mixed (31 / 27 / 19 / 23); about 44 blocks for the double plants, about 22 for lily of the valley | `simple_random_selector`: one uniform pick per placement | picking per try instead of per placement |
| sugar cane patch, CANE pad (100) | 5.1 columns per origin; heights 2 / 3 / 4 = 325 / 141 / 45, never 1; every base beside water | heights follow `biased_to_bottom` 2..4 (0.611 / 0.278 / 0.111); the water filter exact | water check on the cane cell, not beside its support; uniform heights |
| cactus patch, SAND pad (100; 4 placed nothing) | 2.35 columns per origin; heights 1 / 2 / 3 = 142 / 68 / 25; flower on 24.7%; **no two columns adjacent** | `biased_to_bottom` 1..3, flower {0: 3, 1: 1}; survival forbids a solid side neighbour | cactus survival without the side rule |
| `disk_sand` on grass (200) | radius 2..6; 3 layers dy -2..0 (grass and dirt); footprint exactly dx² + dz² <= r²; no sandstone (no air below) | shape: `target` is checked per cell, the origin needn't match; `rule_based` falls back to sand | square footprint; `target` checked only at the origin |
| `disk_gravel` (200) / `disk_clay` (200) | gravel r 2..5, dy -2..0; clay r 2..3, only dy -1 (grass is not a clay target) | as above; half height 1 for clay | target ignored |
| `disk_grass` on grass (100) | "Placed", 0 blocks changed | its provider gives dirt under cover, so dirt over dirt shows nothing; re-measure on a mud pad | none here |
| kelp, POOL12 (200) | total height uniform 1..10; top `kelp` age uniform 20..23 | `kelp_plant` uniform 0..9 plus one tip; `randomized_int` age | tip missing; age fixed |
| kelp, POOL4 (200) | height only 1 (21) or 2 (179) in water 4 deep (floor y59, water 60..63) | the tip survives trimming (`prioritize_tip`); **the free run is one cell shorter than our reading of the predicate** (3 cells hold water with water above). Re-measure at depths 3 to 6 before pinning the trim rule | trimming the tip instead of the base |
| `seagrass_tall` (200) | 160 tall (both halves), 40 short | weights 80 / 20 | weights swapped |
| `seagrass_normal` tail, POOL12 (100) | 54 blocks per origin: 28.9 short, 12.5 tall (30% tall); 41.4 cells | 41.02 cells analytic; 30 / 70 | `biome` or the water filter at the wrong step |
| `sea_pickle` (200) | pickles 1 / 2 / 3 / 4 = 49 / 56 / 45 / 50; all waterlogged | uniform 1..4, the source's waterlogged kept | property not set |
| lily pad tail, POOL4 (100; 1 nothing) | 2.61 pads per origin (0..6), all at y64 | 2.48 analytic; pads only on water (survival) | pads over land or under water |
| `pale_moss_patch` (100) | 69 pale moss blocks per origin, depth 1, farthest 3.6..6.4 blocks; on top 8.7 carpets, 8.9 short grass, 3.15 tall grass (6.3 blocks) | vegetation on 0.30 of the new ground (20.7 of 69); the footprint size is our design (radius 2..4 with chancy edges gives 51..76 by our candidate rules) | vegetation before ground; chance on every column |
| flower providers, lattice 17 x 9, step 128 (153 positions each) | `flower_plain`: dandelion 0.64, the four high states 0.07..0.10, no tulips; `flower_meadow`: poppy 0.27, azure bluet 0.26, dandelion 0.25, cornflower 0.11, tall grass 0.06, allium 0.05; `flower_flower_forest`: tulips 0.14..0.22 each, azure bluet 0.13, oxeye 0.08, allium 0.04 | `flower_plain` above the threshold: dandelion 2/3, each high state 1/12 (0.083); the others indicative only (small sample, fixed vanilla noise) | `high_chance` misapplied |
| glow lichen, 9 x 9 x 9 stone room (50 + 50) | origin at the centre (4 from every face): 50 of 50 fail; origin beside the east wall: 50 of 50 place, 1.6 faces each, all on that wall | in practice lichen grows on faces next to its origin, not 20 cells away; the search algorithm is not known | — |

**The survival rule, tested with gravity off:** every plant measured landed only where its support holds: grass and flowers on grass, cane beside water, cactus with free sides, pads on water. So vanilla's `simple_block` and `block_column` check survival at placement. Our survival table must too, besides the replay-time support check.

### 5.3 Cost per column against the 0.4 ms gate (`notes/cost.md`)

Assumed unit costs, to be replaced by the bench:

| Unit | Assumed cost (ns) |
|---|---|
| biome lookup (jittered, per position at the `biome` modifier) | 150 |
| surface try (6 RNG draws, a window read, survival, a write) | 50 |
| disk cell | 25 |
| underground origin (height draw, heightmap, 1-2 reads) | 60 |

| Group | Biome lookups / chunk | Surface tries / chunk | Disk cells (under water only) | Underground origins | Estimate (µs) |
|---|---|---|---|---|---|
| mountain, grove, peaks | 136-139 | 0-89 | 1,291 | 130 | 60-65 |
| plains, meadow | 139-152 | 83-351 | 1,291 | 130 | 65-80 |
| forest | 140-210 | 89-540 | 1,291 | 130 | 67-111 (cherry grove) |
| taiga | 136-147 | 0-374 | 1,291 | 130 | 60-81 |
| jungle | 160 | 880 | 1,291 | 130 | 107-108 |
| savanna | 143-158 | 211-712 | 1,291 | 130 | 72-99 |
| dry | 140-145 | 140-260 | 1,291 | 130 | 67-73 |
| swamp | 208-211 | 308-544 | 85-403 | 130 | 65-69 |
| ocean, river | 139-239 | 83-296 | 1,291 | 130 | 65-90 |
| cave (S11's set on) | up to 633 origins | | | 671 (lush) | 61-171, plus each lush patch's 0.5k-2k cells |

How to read it:
- Disk cells count only where the origin's top cell is water (the `fluid=water` filter at `OCEAN_FLOOR_WG`); on land a disk costs one lookup and one read.
- Glow lichen's 131 origins are mostly in stone: one read each, and about 36% pass its surface filter.
- At a biome border a column runs the union of its 3x3's lists, but the `biome` modifier prunes the extra tries.
- **Levers if needed:**
  - `biome` before the `count` fan-out where the chain allows it (it already does for the land patches);
  - S2's early-out idea: skip a try whose window cells above the top are all air for `#air`-filtered plants... not needed at these figures;
  - a per-column cache of the jittered biome at the heightmap cell.

## 6. Draft design

**Data flow, Decorate.** For each placed feature (global order), the modifier chain runs as in S2. Each position reaching the feature calls `Place(Feature, Position, TrySeed, Context, Emit)`:
- `Emit` writes to the bucket of the target column, as ores do, and to the working copy (`SetState`), so later features see the plant.
- The heightmap overlay updates on `SetState` for the motion-blocking, fluid and leaves classes (the `Decoration.HeightmapTags` classes already exist).

**Compile (in `FVoxelDecorator::Initialize` / `CompileFeature`):**
- `FModifier` grows the 11 types and its parameters.
- Predicates compile to a flat node array: children by index; leaves hold an offset and a state bit set from `FVoxelBlockTags`, as rule tests do.
- Providers compile to a flat array.
- Features compile to an op plus a config index. Nested placed features become `FPlaced` entries (modifiers plus a feature).
- A placed feature runs when every node it reaches compiled. Otherwise it is skipped and listed, as S2 does.

**Writes:**
- Plants: `IfReplaceable` with the `replaceable` tag (and air). Support slot "block below in tag T" for every plant that needs ground, checked at replay.
- Double plants: the lower and upper halves as two writes, both with the lower's support.
- Kelp and seagrass: support "below not in #cannot_support_*", with water required at the cell. Needs a second support kind; the slot has 3 bits, 8 kinds.
- Disks: mode `IfTargets`-like over the disk's target set, so a neighbour's change to the target cell is honoured.
- `vegetation_patch` ground: `IfReplaceable(#moss_replaceable)`.

**Survival table (core):** per state, a rule kind and its tag:
- `supports_vegetation` (grass, flowers, ferns, bushes, saplings);
- `supports_dry_vegetation`;
- sugar cane: support, plus water or `supports_sugar_cane_adjacently` beside the support;
- cactus: support, plus no solid side neighbour, plus the flower override;
- lily pad;
- kelp and seagrass: water at the cell, not on `cannot_support_*`;
- double plants: upper over lower;
- hanging: cave vines and spore blossom (S11);
- attached faces: multiface.

S7 builds placement rules from the same tags (plan S7, Q6). Put the table in the core so S7 and S8 share it (see section 8).

**Switches and gates:**
- `voxel.Decoration.Plants 1`, default on. At 0, `Options.Filter` drops the 82, and S3's goldens and hashes reproduce: the off-path A/B, as S2 and S4 did.
- A debug cvar draws the column grid over shots (plan evidence: patches crossing borders).
- Gates:
  - Decorate +0.4 ms mean per column (`voxel.Bench Decorate 4`, three presets, medians of 3);
  - `Decoration.Reach` 0 drops (needs the multiface clamp);
  - `Decoration.LoadOrder` and `.Oracle`;
  - `Decoration.Golden` re-recorded under "plants on";
  - mesh per column in plant-dense biomes against S6's figure.

## 7. Code-shape sketches

All against 015bb42 and for orientation only.

```cpp
// Decoration/VoxelDecorator.h (015bb42), FModifier::EType grows; parameters stay plain data.
enum class EType : uint8 { Count, InSquare, HeightRange, RarityFilter, Biome,
    Heightmap, Offset, Filter, WaterDepth, EnvironmentScan, RandomChance,
    NoiseThresholdCount, NoiseBasedCount, Cuboid, RandomlySelected, SurfaceRelative };
// + EVoxelHeightmap Map; FVoxelIntProvider X, Y, Z (offset, cuboid); int32 Predicate (root node);
// + float Chance01; double NoiseLevel, Ratio, Factor, NoiseOffset; int32 Below, Above;
// + int32 MaxSteps; int8 Direction; int32 Allowed (predicate); int32 MinRel, MaxRel; int32 FirstChoice, ChoiceCount.
```

```cpp
// Decoration/VoxelBlockPredicates.h (new; assumes VoxelRuleTests.h's FVoxelBlockTags at 015bb42).
enum class EVoxelPredicateOp : uint8 { True, MatchingBlocks, MatchingTag, MatchingFluids, Solid, Replaceable,
    SturdyFace, WouldSurvive, InsideWorld, Unobstructed, VolumeMatch, AllOf, AnyOf, Not };
struct FVoxelPredicateNode { EVoxelPredicateOp Op; FIntVector Offset; int32 Set; int32 First, Count; uint8 Face; uint16 State; };
class FVoxelBlockPredicates {
public:
    bool Test(int32 Root, const FVoxelDecorationContext& Context, FIntVector Position) const;
private:
    TArray<FVoxelPredicateNode> Nodes; TArray<int32> Children; TArray<TBitArray<>> StateSets;
};
```

```cpp
// Decoration/VoxelStateProviders.h (new): one flat array, noise providers own a pattern noise.
enum class EVoxelStateProviderOp : uint8 { Simple, Weighted, RuleBased, Noise, NoiseThreshold, DualNoise,
    RandomizedInt, Rotated, RandomBlock };
uint16 SampleState(int32 Provider, FVoxelXoroshiro& Random, FIntVector Position, const FVoxelDecorationContext&) const;
```

```cpp
// Decoration/Features/VoxelPatchFeatures.h (new): one entry, a switch per op, writes through Emit (as VoxelOreFeature::Place).
template <typename FEmit>
bool PlaceFeature(int32 Feature, FIntVector Position, FVoxelXoroshiro& Random, FPlaceContext& Context, FEmit&& Emit);
// simple_block: State = SampleState(...); if (!Survives(State, Position)) return false; Emit(Position, State, Support);
//               if (IsLowerHalf(State)) Emit(Position + Up, UpperOf(State), Support);
// block_column: sample each layer's height; walk Direction while Allowed holds; trim (tip kept if prioritize_tip); emit.
// disk:         r = Radius.Sample; for cells with dx*dx + dz*dz <= r*r, dy in [-HalfHeight, HalfHeight]: target test, provider.
// selectors:    pick by chance / uniform / weight / coin; run the chosen placed feature's modifiers from Position.
// vegetation_patch: ground footprint (edges by chance), then vegetation at vegetation_chance per ground cell.
// multiface:    the origin's faces first, then the search, clamped to the window.
```

```cpp
// Decoration/VoxelDecorationContext.h (015bb42): the live heightmap overlay.
int32 GetHeight(EVoxelHeightmap Map, int32 X, int32 Y) const;   // top Z (+1 at the modifier for vanilla's value)
// SetState raises the overlay when the state counts for Map (classes from the heightmap tags).
```

## 8. Re-orientation: what other sessions change before S8

| Session | Changes | What S8 diffs first |
|---|---|---|
| S4 (ore veins; running) | `Surface/VoxelSurfaceEvaluator.*`, `VoxelSurfaceRules.h`, `Terrain/VoxelDensityTerrain.*`, `Source/Unnamed/Voxel/VoxelConsole.*`; goldens and radius-8 hashes re-recorded "veins on" | The golden constants in `VoxelDecorationTests.cpp` and the decorated hashes S8 re-records next; the console switch layout |
| S6 (models: rendering; running) | `Blocks/VoxelBlockTable.*` (`EVoxelShape::Model`, quads), `Source/Unnamed/Voxel/VoxelBlocks.cpp` (`MakeTable`, `GetTintSource`, dry foliage), the mesher (cutout model section), the chunk component, `voxel.Models` | Which plant states exist in the core table, their property encoding (half, age, facing, `segment_amount` and `flower_amount`, pickles, waterlogged), `bSolid` (from `blocks_motion`), heightmap classes |
| S6b (Playground meshes and materials) | Playground meshes and materials | Little; only for shots |
| S7 (plant interaction, before S8) | `Foundation/FoundationBlockPlacement.*`, `FoundationBlocks.*`, `Voxel/VoxelBlockStorage.*`, `Docs/Blocks.md` (placement and survival); `replaceable` for every block from the facts | **The survival rules and the replaceable flag:** S8 should reuse S7's table, or move it into the core |
| Torch probe | VoxelWorld shot code (`Source/Unnamed/Voxel/VoxelWorld.cpp` shots, `Scripts/CaptureVoxel.ps1`) | S8's biome shot finder and the column-grid overlay touch the same code |
| S5 (merged) | 85 decoration blocks in the data | Already at 015bb42: the blocks exist as model states, drawn after S6 |

## 9. Risks, size, open questions

**Risks:**
1. **Unknown algorithms** (multiface search, dual_noise, the vegetation_patch order, block_column trimming): design our own where the probes do not pin them, and state each as "ours" in `VoxelWorld.md`, as S2 did for the ore blob.
2. **Reach:** glow lichen's `search_range` of 20 could leave the 3x3 on paper, though the probe shows lichen growing next to its origin; vanilla's own 26.4 snapshot 1 restricts placements to a 3x3 chunk region [W]. Clamp it anyway (Q3).
8. **Kelp trimming:** POOL4 gave a free run one cell shorter than expected. Re-measure at several depths before pinning the trim rule.
3. **Live heightmaps:** without the overlay, a second patch lands inside a pumpkin or cactus (vanilla's maps update as blocks go in).
4. **Support across columns:** a plant over a neighbour's later lake air. The support slot exists (S2); S8 fills it.
5. **Cost:** the unit costs are assumed. Biome lookups dominate on land, disk cells under water, and lush patches in caves if S11's set turns on.
6. **Block states:** S6's property encoding for plants (half, age, facing and amounts, pickles, waterlogged) must be stable before S8 writes states into the log.
7. **Golden churn:** S4 and S8 both re-record `Decoration.Golden` and the decorated hashes. S8 needs S4's values merged first.

**Size: L.** Eleven modifiers, 13 predicates, 9 providers, 12 feature types, a survival table, the heightmap overlay, nine biome shots and a column-grid overlay. One session can do it at the 1M allowance with a cut line:
- first the surface patches: modifiers, predicates, providers, `simple_block`, `block_column`, the selectors, survival, the overlay, goldens;
- then disks, `vegetation_patch`, `multiface_growth`, the noise providers and the evidence.

**Open questions for Jesus:**
1. **The seven S11 / S12 placed features that S8's types complete** (lush caves x3, cave vines, spore blossom, sculk vein, ice patch):
   - (a) **(recommended)** they turn on in S8 as the data says (S2's rule); S11 and S12 verify and shoot them;
   - (b) an owner list keeps them off until their sessions.
2. **The five modifiers with no S8 user** (`surface_water_depth_filter`, `environment_scan` (S11's), `random_chance`, `cuboid`, `randomly_selected`):
   - (a) **(recommended)** build them in S8 with unit tests, as the plan says; each is small;
   - (b) defer each to its first user (S9, S11, S12, phase 7).
3. **Multiface reach:**
   - (a) **(recommended)** clamp the search to the 3x3 window: deterministic, and it matches vanilla's own 26.4 restriction;
   - (b) count the drops and exempt multiface from `Decoration.Reach`;
   - (c) a wider window.
4. **Session size:**
   - (a) **(recommended)** one S8 with the cut line above, the second half moving to S8b only if the lead passes about 500k;
   - (b) split now into S8a (surface patches) and S8b (disks, vegetation patch, multiface, noise providers, evidence).
