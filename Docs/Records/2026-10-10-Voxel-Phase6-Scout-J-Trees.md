# Scout J: trees and big vegetation, prep for voxel phase 6 S9

Read-only research for S9, October 10, 2026, against `015bb42` (`C:\dev\projects\Unnamed-scout`). Sources:
- The 26.3 worldgen reference's data, read by `probes\inventory.py`.
- The minecraft.wiki pages in `notes\wiki_trees.md`, collected by a Sonnet helper.
- Black-box runs of the 26.3 server through S1b's harness, copied and adapted as `probes\tree_probe.py`: 82 probes with 5,512 placements of `/place feature`, plus a 784-placement reach re-check.

No game code was read. Paths below are relative to `C:\dev\sandbox\scout-trees\` unless they start with a repo folder.

## 1. Summary and recommendations

- **Scope as the plan says.** The data has 36 tree configurations with 10 trunk placers, 12 foliage placers and one root placer (mangrove). 33 use two-layer feature sizes and 3 use three-layer ones (dark oak, pale oak, pale oak creaking). 11 decorators. Big vegetation: 6 fallen trees, 2 huge mushrooms, 2 bamboo, `vines`, `rooted_azalea_tree` (`root_system`) and `forest_rock` (`block_blob`). `rooted_sulfur_spring` is also a `root_system`, but it lives in sulfur caves: leave it to S11.
- **Reach: every configuration fits the 3x3 from any origin.**
  - No tree chain moves its origin sideways. The only offset in any tree chain is azalea's y -1, so origins stay inside the column and the margin is 16 blocks on each side.
  - Largest measured horizontal reach (Chebyshev distance from the origin, 100 placements each):
    - fallen super birch 15, which is exactly its largest `log_length`;
    - fallen jungle 11, fallen spruce 10, mega jungle 9 (its 2x2 trunk sits at +0..+1, so its extent is -6..+9);
    - cherry 8, tall mangrove 8, fallen birch 8;
    - every other tree 7 or less; huge mushrooms 2 and 3; bamboo 4; forest rock 3.
  - Fallen super birch is the only one that uses nearly the whole margin (15 of 16). Recommendation: compile a static reach bound per feature (a fallen tree's bound is its largest `log_length`; for trees, the probe maxima plus 1) and assert it is at most 16 at `Initialize`. `Decoration.Reach` then guards the rest.
- **Leaf distance.** After the game's scheduled ticks run, a leaf's `distance` equals min(7, steps from the nearest `#logs` block through face-adjacent leaves) on 97.7% to 100% of leaves. Our rule can be that BFS exactly.
  - The random-spread placers (azalea, mangrove, tall mangrove) leave 1.5 to 2.7 leaves per tree farther than 6 from any log. Vanilla places them at distance 7 and they decay later. We have no decay, so this is question 4.
  - Unsettled (ticks frozen), the saved values are mid-update. This is why the probe settles ticks first.
- **Feature sizes, measured with stone ceilings.** Oak, spruce, dark oak and mega spruce never shrink: a tree places only if its top log is at least 3 below the ceiling, and otherwise the whole tree fails. Fancy oak (`min_clipped_height` 4) shrinks to fit instead: 16 of 16 placed with the ceiling at dy 6.
- **Block entities.** Only two blocks need them:
  - `bee_nest` from the beehive decorator. Its block entity id is `minecraft:beehive`; it is always `facing=south,honey_level=0`, with 2 to 3 bees per the wiki.
  - `creaking_heart` from its decorator.
  - Under Q2 (a), S9 places the states only. Phase 7 can derive the bees from a position hash, so S9 saves nothing.
- **Cost against the 0.4 ms Decorate gate.** An upper bound, counting every try as a success, in single-biome worlds:

  | Biome | Cells written per column |
  |---|---|
  | mangrove swamp | about 12,500 |
  | bamboo jungle | about 10,500 |
  | jungle | about 8,000 |
  | pale garden | 4,100 |
  | cherry | 2,700 |
  | dark forest | 2,500 |
  | every other | 2,200 or fewer |

  - At 40 to 90 ns per written cell, mangrove (0.5 to 1.1 ms) and the jungles (0.3 to 0.95 ms) can exceed 0.4 ms. Everything else stays under about 0.25 ms.
  - Finalize's replay of those writes (about 23 ns each in S2) adds about 0.2 to 0.3 ms in mangrove and jungle, which threatens the 0.3 ms Finalize gate there.
  - Recommendation: gate on the presets' means as before; report single-biome figures per group (`FixedBiome` exists); flag mangrove and jungle.
- **Size L; split it in two** (question 2): S9a is the tree core (placers, sizes, roots, leaf distance, borders); S9b is the decorators, big vegetation, shots and gates. Both need S8 first: modifiers, predicates, providers, selectors, and states with properties.

## 2. Reading list for the S9 worker (at 015bb42, in order)

1. `Docs/Records/2026-10-09-Voxel-Phase6-Plan.md` lines 96-173 (the decoration design, write modes, reach, the gates table), 405-451 (S8, then S9) and 643-667 (Jesus's answers; Q2 defers block entities).
2. `Docs/VoxelWorld.md` lines 444-484: what S2 and S3 built.
3. This doc: sections 1, 4 (only your placer's bullets), 6 and 7.
4. `Source/UnnamedVoxel/Decoration/VoxelWriteLog.h` lines 6-57 (modes `Force`, `IfReplaceable`, `IfTargets`; the support slot; the 8-byte write) and 59-132 (buckets, the 9-way merge).
5. `Source/UnnamedVoxel/Decoration/VoxelDecorationContext.h` lines 8-86 (the working copy: `GetState`, `SetState`, `GetTopZ`).
6. `Source/UnnamedVoxel/Decoration/VoxelDecorator.h` lines 59-171.
7. `Source/UnnamedVoxel/Decoration/VoxelDecorator.cpp`:
   - 119-184, `CompileFeature`: only `minecraft:ore` today; S8 will have widened it, so diff first;
   - 201-243, `RunModifiers`;
   - 245-333, `Decorate`: the ore write lambda at 305-321 is the pattern for tree writes;
   - 335-401, `Finalize`.
8. `Source/UnnamedVoxel/Decoration/Features/VoxelOreFeature.h` lines 1-96: the shape of a feature (config, reach, `Place` with an emit callback).
9. `Source/UnnamedVoxel/Decoration/VoxelNumberProviders.h` lines 1-60. Tree data needs `weighted_list` int providers (cherry `branch_count`, poplar foliage radius) and S8 should have added them; check.
10. `Source/UnnamedVoxel/Blocks/VoxelBlockTable.h` lines 76-140: state ids are the registry's; at 015bb42 only `FindState(Block)` (the default state) exists, with no lookup by properties.
11. `Source/Unnamed/Tests/VoxelDecorationTests.cpp` lines 1136-1246 (`Decoration.OreProbes`: how a test reads probe facts) and 1446-1503 (`Reach`, `Golden`).
12. `Source/UnnamedVoxel/VoxelBench.cpp` lines 682-720 (`RunDecorate`), and `Source/UnnamedVoxel/Terrain/VoxelDensityTerrain.h` lines 38-39 (`FixedBiome`, for the per-biome benches).
13. `Scripts/CaptureVoxel.ps1` lines 2 and 30 (switches), and `Source/Unnamed/Voxel/VoxelWorld.cpp` line 139 with `VoxelWorld.h` line 150 (shot flags): where `-Trees` goes.
14. `Reference/minecraft-worldgen-26.3/README.md` (the feature probes paragraph and "Rebuilding" 3) and `tools/feature_probe.py` lines 1-48 and 82-205 (the harness this scout's tool extends).
15. Sandbox, as needed: `notes\doc_trees.md`, `probes\out\<probe>.json`, `probes\out\profiles\<probe>.txt` (per-layer leaf occupancy), `notes\wiki_trees.md`.

## 3. Data inventory

All under `Reference/minecraft-worldgen-26.3/data/minecraft/`:

- **`worldgen/feature/`**:
  - 36 `minecraft:tree` files: acacia, azalea_tree, the birch family (4), cherry_bees_005, dark_oak_leaf_litter, the fancy oaks (6), jungle_bush, jungle_tree, mangrove, mega_jungle_tree, mega_pine, mega_spruce, the oaks (5), the poplars (3), pale_oak, pale_oak_creaking, pine, spruce, super_birch_bees(_0002), swamp_oak, tall_mangrove.
  - Big vegetation: 6 `fallen_*_tree`, `huge_brown_mushroom`, `huge_red_mushroom`, `bamboo_no_podzol`, `bamboo_some_podzol`, `vines`, `rooted_azalea_tree`, `rooted_sulfur_spring`, `forest_rock`.
  - The selectors that reach them: 19 `trees_*` (`random_selector`; `trees_dappled_forest` is a `weighted_random_selector`), plus `dark_forest_vegetation`, `mangrove_vegetation`, `mushroom_island_vegetation`, `bamboo_vegetation`, `meadow_trees`, `birch_tall` and `pale_garden_vegetation`.
- **Placer counts.**
  - Trunk: straight 16, fancy 6, dark_oak 3, poplar 3, giant 2, upwards_branching 2, forking 1, bending 1, mega_jungle 1, cherry 1.
  - Foliage: blob 13, fancy 6, dark_oak 3, random_spread 3, poplar 3, mega_pine 2, and 1 each of acacia, bush, cherry, jungle, pine and spruce.
- **Decorator uses.**
  - On trees: beehive 15, place_on_ground 10 (two each), leave_vine 5, trunk_vine 2, cocoa 1, alter_ground 2, attached_to_leaves 2, pale_moss 2, creaking_heart 1, shelf_mushroom 3.
  - On fallen trees: attached_to_logs 6, trunk_vine on the stump 2, shelf_mushroom 1.
- **`worldgen/placed_feature/`:** 22 placements in the tree chains.
  - Each `trees_*` placement is count, `in_square`, `surface_water_depth_filter` (0; mangrove 5), the `OCEAN_FLOOR` heightmap, `would_survive` (a sapling) and `biome`.
  - The `*_checked` placements carry only `would_survive`.
  - `pine_on_snow` and `spruce_on_snow` use `environment_scan` (up 8, not powder snow) with a snow or powder snow block below.
  - `rooted_azalea_tree` is count 1-2, `in_square`, `height_range` (bottom to 256), `environment_scan` up 12 through air to a solid block, `offset` y -1, then `biome`.
- **`worldgen/block_state_provider/`:**
  - `soil_beneath_tree`: dirt, unless the block is in `cannot_replace_below_tree_trunk` (#dirt, #mud, #moss_blocks, podzol).
  - `podzol_beneath_tree`: podzol over `beneath_tree_podzol_replaceable` (= `#substrate_overworld`).
  - `mangrove_propagule`: hanging, age uniform 0-4.
- **`tags/block/`:**
  - `replaceable_by_trees` (28 entries: #leaves, #small_flowers, water, grasses, vines, leaf litter, shelf mushroom...);
  - `logs`, which goes through `logs_that_burn` to 10 wood families including poplar (a leaf's distance counts from these; `creaking_heart` is not in it);
  - `leaves`, `cannot_replace_below_tree_trunk`, `beneath_tree_podzol_replaceable`;
  - `mangrove_logs_can_grow_through`, `mangrove_roots_can_grow_through`;
  - `azalea_root_replaceable`, `azalea_grows_on`;
  - `supports_cocoa` (#jungle_logs), `supports_hanging_mangrove_propagule`;
  - `huge_red_mushroom_can_place_on`, `huge_brown_mushroom_can_place_on`, `forest_rock_can_place_on`, `features_cannot_replace`.
- **Blocks: `Reference/minecraft-blocks-26.3/block_manifest.json`.** It holds every block S9 places, with its `properties` and `state_id_range`.
  - A state's id is the range start plus a mixed-radix index over the properties in their listed order, the last varying fastest. For example, `oak_leaves` is 267-294 and its default 294 is distance 7, persistent false, waterlogged false.
  - States S9 writes: log `axis`; leaves `distance`/`persistent`/`waterlogged`; vine faces; cocoa `age`/`facing`; bee nest `facing`/`honey_level`; mushroom-block faces (64 states); propagule `age`/`hanging`/`stage`/`waterlogged`; leaf litter `facing`/`segment_amount`; pale hanging moss `tip`; shelf mushroom `age`/`facing`; bamboo `age`/`leaves`/`stage`; creaking heart `axis`/`creaking_heart_state`/`natural`.
- **Sandbox:**
  - `notes\tree_inventory.json` and `.md`: placers, sizes, decorators, and tries per column for every biome.
  - `probes\out\*.json`: per probe, the statistics, 2 example placements with every cell, and a per-placement summary.
  - `probes\out\overview.json`, `notes\probe_tables.md`, `notes\cost.md`.

### Tree configurations: data joined with the probes

"Tries per column" is the expected number of tries per 16x16 column before terrain filters: count expectation x rarity x selector chance. In the probe columns:
- "Top log dy" is the highest log above the origin.
- "Cells" is every changed cell, including the dirt under the trunk and the decorators.
- "Reach" is the largest Chebyshev distance from the origin of any changed cell.
- "Extent" is the bounding box of the changed cells.

The world is superflat with grass at Y 59 over 3 dirt and stone; origins are at Y 60; each placement sits in its own 32x32 cell; ticks settle for 3 s with random ticks off. `@mudwater` puts the cell on mud (Y 56-59) under 2 water.

| Tree | Trunk: placer, height range | Foliage: placer, radius | Size | Decorators (p) | Biomes: tries per column | Placed / 100 | Top log dy | Cells | Logs | Leaves | Reach | Extent |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| acacia | forking 5-9 | acacia r2 | two_layers u2 | - | windswept_savanna 1.7, savanna 0.88, savanna_plateau 0.88 | 100 | 4-8 | 63-99 (85.5) | 5-12 (8.67) | 57-86 (75.8) | 6 | x -6..6, z -6..6, y -1..9 |
| azalea_tree | bending 4-6 | random_spread r3 | two_layers | - | lush_caves 1.5 | 100 | 4-6 | 64-134 (98.1) | 6-9 (7.5) | 57-124 (89.6) | 6 | x -6..6, z -6..6, y -1..7 |
| birch_bees_0002 | straight 5-7 | blob r2 | two_layers | beehive 0.002 | birch_forest 10, old_growth_birch_forest 5 | 100 | 4-6 | 57-65 (61.9) | 5-7 (6.1) | 51-58 (54.8) | 2 | x -2..2, z -2..2, y -1..7 |
| birch_bees_0002_leaf_litter | straight 5-7 | blob r2 | two_layers | beehive 0.002, place_on_ground x2 | forest 2 | 100 | 4-6 | 81-100 (91.9) | 5-7 (5.92) | 50-61 (55) | 4 | x -4..4, z -4..4, y -1..7 |
| birch_bees_002 | straight 5-7 | blob r2 | two_layers | beehive 0.02 | flower_forest 1.2 | 100 | 4-6 | 58-67 (62.2) | 5-7 (6.03) | 52-59 (55.1) | 2 | x -2..2, z -2..2, y -1..7 |
| birch_leaf_litter | straight 5-7 | blob r2 | two_layers | place_on_ground x2 | dark_forest 0.99 | 100 | 4-6 | 82-106 (92.6) | 5-7 (5.95) | 51-59 (55.1) | 4 | x -4..4, z -4..4, y -1..7 |
| cherry_bees_005 | cherry 7-8 | cherry r4 | two_layers u2 | beehive 0.05 | cherry_grove 10 | 100 | 5-7 | 137-423 (262) | 8-24 (15.7) | 128-397 (246) | 8 | x -8..8, z -8..8, y -1..10 |
| dark_oak_leaf_litter | dark_oak 6-9 | dark_oak r0 | three_layers u2 | place_on_ground x2 | dark_forest 9.9 | 100 | 5-8 | 170-219 (195) | 28-57 (41.5) | 105-140 (120) | 6 | x -5..6, z -5..6, y -1..10 |
| fancy_oak | fancy 3-14 | fancy r2 | two_layers l0 m4 u0 | - | jungle 5, bamboo_jungle 1.5, sparse_jungle 0.21 (+14) | 100 | 3-11 | 74-487 (189) | 4-33 (12.4) | 69-456 (176) | 7 | x -7..7, z -6..7, y -1..15 |
| fancy_oak_bees | fancy 3-14 | fancy r2 | two_layers l0 m4 u0 | beehive 1 | meadow 0.005 | 100 | 3-11 | 74-449 (188) | 4-31 (12.1) | 69-417 (174) | 7 | x -7..6, z -7..7, y -1..15 |
| fancy_oak_bees_0002_leaf_litter | fancy 3-14 | fancy r2 | two_layers l0 m4 u0 | beehive 0.002, place_on_ground x2 | forest 0.81 | 100 | 3-11 | 94-442 (214) | 4-28 (12.1) | 69-395 (174) | 7 | x -6..7, z -7..7, y -1..15 |
| fancy_oak_bees_002 | fancy 3-14 | fancy r2 | two_layers l0 m4 u0 | beehive 0.02 | flower_forest 0.49 | 100 | 3-11 | 74-470 (183) | 4-35 (12.1) | 69-439 (170) | 7 | x -7..7, z -7..7, y -1..15 |
| fancy_oak_bees_005 | fancy 3-14 | fancy r2 | two_layers l0 m4 u0 | beehive 0.05 | deep_dark 0.017, dripstone_caves 0.017, plains 0.017 (+1) | 100 | 3-11 | 74-477 (198) | 4-31 (12.8) | 69-448 (184) | 7 | x -7..7, z -6..7, y -1..15 |
| fancy_oak_leaf_litter | fancy 3-14 | fancy r2 | two_layers l0 m4 u0 | place_on_ground x2 | dark_forest 0.39 | 100 | 3-11 | 97-469 (200) | 4-30 (11) | 69-422 (160) | 7 | x -7..6, z -6..7, y -1..15 |
| jungle_bush | straight 1-1 | bush r2 | two_layers l0 u0 | - | jungle 23, bamboo_jungle 4.3, sparse_jungle 0.95 | 100 | 0-0 | 28-34 (31.3) | 1-1 (1) | 26-32 (29.3) | 2 | x -2..2, z -2..2, y -1..2 |
| jungle_tree | straight 4-12 | blob r2 | two_layers | cocoa 0.2, trunk_vine, leave_vine 0.25 | jungle 15, sparse_jungle 0.93 | 100 | 3-11 | 74-170 (124) | 4-12 (8.25) | 52-59 (55) | 3 | x -3..3, z -3..3, y -1..12 |
| mangrove | upwards_branching 2-7 | random_spread r3 | two_layers l2 u2 | leave_vine 0.125, attached_to_leaves 0.14, beehive 0.01 + mangrove roots | mangrove_swamp 3.8 | 100 | 2-10 | 78-448 (220) | 2-16 (7.05) | 34-310 (128) | 7 | x -6..6, z -6..7, y 0..12 |
| mangrove@mudwater | upwards_branching 2-7 | random_spread r3 | two_layers l2 u2 | leave_vine 0.125, attached_to_leaves 0.14, beehive 0.01 + mangrove roots | mangrove_swamp 3.8 | 81 | 2-11 | 83-523 (225) | 2-18 (6.93) | 33-340 (127) | 6 | x -6..6, z -6..6, y -4..13 |
| mega_jungle_tree | mega_jungle 10-31 | jungle r2 | two_layers l1 u2 | trunk_vine, leave_vine 0.25 | bamboo_jungle 17, jungle 7.5 | 100 | 9-30 | 350-886 (593) | 37-133 (86.6) | 203-373 (272) | 9 | x -6..9, z -6..9, y -1..31 |
| mega_pine | giant 13-29 | mega_pine r0 | two_layers l1 u2 | alter_ground | old_growth_pine_taiga 3 | 100 | 12-27 | 230-432 (309) | 49-109 (79.1) | 75-199 (112) | 7 | x -5..7, z -5..7, y -1..28 |
| mega_spruce | giant 13-29 | mega_pine r0 | two_layers l1 u2 | alter_ground | old_growth_spruce_taiga 3.4, old_growth_pine_taiga 0.26 | 100 | 12-28 | 376-611 (516) | 49-113 (83.1) | 207-371 (315) | 7 | x -5..7, z -5..7, y -1..29 |
| oak | straight 4-6 | blob r2 | two_layers | - | windswept_forest 0.91, windswept_savanna 0.41, savanna 0.22 (+14) | 100 | 3-5 | 56-65 (60.8) | 4-6 (5.05) | 49-60 (54.8) | 2 | x -2..2, z -2..2, y -1..6 |
| oak_bees_0002_leaf_litter | straight 4-6 | blob r2 | two_layers | beehive 0.002, place_on_ground x2 | forest 7.2 | 100 | 3-5 | 79-100 (90.8) | 4-6 (5.01) | 50-59 (54.9) | 4 | x -4..4, z -4..4, y -1..6 |
| oak_bees_002 | straight 4-6 | blob r2 | two_layers | beehive 0.02 | flower_forest 4.4 | 100 | 3-5 | 57-66 (61.1) | 4-6 (5.03) | 51-59 (55.1) | 2 | x -2..2, z -2..2, y -1..6 |
| oak_bees_005 | straight 4-6 | blob r2 | two_layers | beehive 0.05 | deep_dark 0.033, dripstone_caves 0.033, plains 0.033 (+1) | 100 | 3-5 | 56-68 (61.1) | 4-6 (5.07) | 51-61 (55) | 2 | x -2..2, z -2..2, y -1..6 |
| oak_leaf_litter | straight 4-6 | blob r2 | two_layers | place_on_ground x2 | wooded_badlands 5, dark_forest 3.5 | 100 | 3-5 | 80-102 (90.9) | 4-6 (4.98) | 52-59 (55.2) | 4 | x -4..4, z -4..4, y -1..6 |
| orange_poplar_leaf_litter | poplar 7-11 | poplar r5/6/7/8 (5:5:1:1) | two_layers u2 | place_on_ground x2, shelf_mushroom 0.4 | dappled_forest 2.1 | 100 | 6-10 | 163-607 (278) | 8-27 (14.7) | 130-560 (235) | 7 | x -7..7, z -7..7, y -1..12 |
| pale_oak | dark_oak 6-9 | dark_oak r0 | three_layers u2 | pale_moss | pale_garden 14 | 100 | 5-8 | 159-347 (258) | 28-56 (40.6) | 107-140 (120) | 6 | x -5..6, z -5..6, y -1..10 |
| pale_oak_creaking | dark_oak 6-9 | dark_oak r0 | three_layers u2 | pale_moss, creaking_heart 1 | pale_garden 1.6 | 100 | 5-8 | 170-365 (249) | 26-57 (41.5) | 109-141 (122) | 6 | x -5..6, z -5..6, y -1..10 |
| pine | straight 6-10 | pine r1 | two_layers l2 u2 | - | grove 3.4, snowy_taiga 3.4, taiga 3.4 (+2) | 100 | 5-9 | 17-101 (50.3) | 6-10 (7.92) | 10-90 (41.4) | 3 | x -3..3, z -3..3, y -1..11 |
| red_poplar_leaf_litter | poplar 7-11 | poplar r5/6/7/8 (5:5:1:1) | two_layers u2 | place_on_ground x2, shelf_mushroom 0.4 | dappled_forest 1.8 | 100 | 6-10 | 166-609 (274) | 8-27 (14.4) | 129-559 (231) | 7 | x -7..7, z -7..7, y -1..12 |
| spruce | straight 5-8 | spruce r2-3 | two_layers l2 u2 | - | grove 6.7, snowy_taiga 6.6, taiga 6.6 (+8) | 100 | 4-7 | 35-132 (70.4) | 5-8 (6.55) | 29-123 (62.8) | 3 | x -3..3, z -3..3, y -1..10 |
| super_birch_bees | straight 5-13 | blob r2 | two_layers | beehive 1 | meadow 0.005 | 100 | 4-12 | 59-72 (66) | 5-13 (9.11) | 51-60 (54.9) | 2 | x -2..2, z -2..2, y -1..13 |
| super_birch_bees_0002 | straight 5-13 | blob r2 | two_layers | beehive 0.002 | old_growth_birch_forest 5 | 100 | 4-12 | 59-71 (64.8) | 5-13 (9) | 51-59 (54.8) | 2 | x -2..2, z -2..2, y -1..13 |
| swamp_oak | straight 5-8 | blob r3 | two_layers | leave_vine 0.25 | swamp 2.1 | 100 | 4-7 | 181-252 (209) | 5-8 (6.41) | 131-140 (135) | 4 | x -4..4, z -4..4, y -1..8 |
| tall_mangrove | upwards_branching 4-14 | random_spread r3 | two_layers l3 u2 | leave_vine 0.125, attached_to_leaves 0.14, beehive 0.01 + mangrove roots | mangrove_swamp 21 | 91 | 7-21 | 110-1074 (545) | 5-42 (18.9) | 37-759 (335) | 8 | x -8..8, z -8..8, y 0..23 |
| tall_mangrove@mudwater | upwards_branching 4-14 | random_spread r3 | two_layers l3 u2 | leave_vine 0.125, attached_to_leaves 0.14, beehive 0.01 + mangrove roots | mangrove_swamp 21 | 30 | 7-19 | 237-983 (547) | 7-34 (18.6) | 124-633 (328) | 8 | x -8..8, z -8..8, y -4..21 |
| yellow_poplar_leaf_litter | poplar 7-11 | poplar r5/6/7/8 (5:5:1:1) | two_layers u2 | place_on_ground x2, shelf_mushroom 0.4 | dappled_forest 0.8 | 100 | 6-10 | 170-599 (266) | 9-25 (14.2) | 131-554 (224) | 7 | x -7..7, z -7..7, y -1..12 |

### Big vegetation

| Feature | Type | Biomes: tries per column | Placed / 100 | Cells | Reach | Extent | Blocks (mean per placement) |
|---|---|---|---|---|---|---|---|
| bamboo_no_podzol | bamboo | jungle 0.25 | 100 | 6-17 (12.3) | 0 | x 0..0, z 0..0, y 0..16 | bamboo 12.28 |
| bamboo_some_podzol | bamboo | bamboo_jungle 1 | 100 | 6-64 (17) | 4 | x -4..4, z -4..4, y -1..16 | bamboo 11.58, podzol 5.38 |
| fallen_birch_tree | fallen_tree | birch_forest 0.13, old_growth_birch_forest 0.063, forest 0.025 (+2) | 100 | 4-9 (5.88) | 8 | x -8..8, z -8..8, y 0..1 | birch_log 5.57, red_mushroom 0.21, brown_mushroom 0.1 |
| fallen_jungle_tree | fallen_tree | jungle 0.19, sparse_jungle 0.012 | 100 | 4-15 (9.32) | 11 | x -11..10, z -11..11, y 0..1 | jungle_log 6.2, vine 2.64, red_mushroom 0.33, brown_mushroom 0.15 |
| fallen_oak_tree | fallen_tree | forest 0.091, wooded_badlands 0.064, dark_forest 0.049 (+10) | 100 | 5-11 (7.58) | 7 | x -7..7, z -7..7, y 0..1 | oak_log 4.45, vine 2.72, red_mushroom 0.25, brown_mushroom 0.16 |
| fallen_poplar_tree | fallen_tree | dappled_forest 1.1 | 100 | 3-12 (5.87) | 7 | x -7..7, z -7..7, y 0..1 | poplar_log 4.39, shelf_mushroom 1.17, brown_mushroom 0.31 |
| fallen_spruce_tree | fallen_tree | snowy_taiga 0.084, taiga 0.084, old_growth_pine_taiga 0.057 (+6) | 100 | 5-11 (7.33) | 10 | x -10..10, z -10..9, y 0..1 | spruce_log 6.83, red_mushroom 0.34, brown_mushroom 0.16 |
| fallen_super_birch_tree | fallen_tree | old_growth_birch_forest 0.063 | 100 | 4-16 (10.3) | 15 | x -15..14, z -15..14, y 0..1 | birch_log 9.39, red_mushroom 0.44, brown_mushroom 0.43 |
| forest_rock | block_blob | old_growth_pine_taiga 2, old_growth_spruce_taiga 2 | 100 | 2-38 (12.4) | 3 | x -3..1, z -3..1, y -3..1 | mossy_cobblestone 12.39 |
| huge_brown_mushroom | huge_brown_mushroom | mushroom_fields 0.5, dark_forest 0.4 | 100 | 49-57 (50.3) | 3 | x -3..3, z -3..3, y 0..12 | brown_mushroom_block 45, mushroom_stem 5.33 |
| huge_red_mushroom | huge_red_mushroom | dark_forest 0.78, mushroom_fields 0.5 | 100 | 49-57 (50.7) | 2 | x -2..2, z -2..2, y 0..12 | red_mushroom_block 45, mushroom_stem 5.66 |
| rooted_azalea_tree | root_system | lush_caves 1.5 | 100 | 182-260 (221) | 6 | x -6..6, z -6..6, y 0..18 | rooted_dirt 115.32, azalea_leaves 67.73, flowering_azalea_leaves 22.66, hanging_roots 8.01, oak_log 7.55 |
| rooted_sulfur_spring | root_system | sulfur_caves 1.5 | - | - | - | - | not probed |
| vines | vines | lush_caves 2.6e+02, bamboo_jungle 1.3e+02, jungle 1.3e+02 (+1) | - | - | - | - | not probed |

## 4. Behaviour in our own words

Sources are marked [W] (`notes\wiki_trees.md`, chiefly https://minecraft.wiki/w/Tree_definition), [D] (the reference data) and [P] (a probe in `probes\out\`; per-layer shapes are in `probes\out\profiles\`).

### Tree feature

- **Trunk height** is `base_height` + uniform(0..`height_rand_a`) + uniform(0..`height_rand_b`) [W]. The probes' top logs stay inside those ranges [P].
- **The soil under the trunk:** `below_trunk_provider` writes dirt under every bottom trunk cell (1 cell for one-wide trunks, 4 for 2x2) unless that block is in `cannot_replace_below_tree_trunk` [D][P: dirt 1.0 or 4.0 per tree].
- **What a tree may overwrite:** logs and leaves go only into air or `replaceable_by_trees` [D: the tag's code-read note in `build_reference.py`].
- **Free space (`minimum_size`):** the tree checks a column of layers above its origin. Two-layer sizes take `limit`, `lower_size` and `upper_size`; three-layer sizes take `limit`, `upper_limit`, `lower_size`, `middle_size` and `upper_size`. `min_clipped_height`, when set, lets the trunk shrink to the free height instead of failing [W].
  - Measured with a 17x17 stone ceiling at dy H, 16 placements per row [P `*_ceil<H>.json`]:

  | Feature | Ceiling at dy | Placed | Top log dy | Highest change dy |
  |---|---|---|---|---|
  | dark_oak_leaf_litter | 5 | 0/16 | -..- | - |
  | dark_oak_leaf_litter | 6 | 0/16 | -..- | - |
  | dark_oak_leaf_litter | 7 | 0/16 | -..- | - |
  | dark_oak_leaf_litter | 8 | 1/16 | 5..5 | 7 |
  | dark_oak_leaf_litter | 9 | 5/16 | 5..6 | 8 |
  | dark_oak_leaf_litter | 10 | 12/16 | 5..7 | 9 |
  | dark_oak_leaf_litter | 11 | 16/16 | 5..8 | 10 |
  | fancy_oak | 4 | 0/16 | -..- | - |
  | fancy_oak | 5 | 2/16 | 3..3 | 4 |
  | fancy_oak | 6 | 16/16 | 3..3 | 5 |
  | fancy_oak | 8 | 16/16 | 3..4 | 7 |
  | fancy_oak | 10 | 16/16 | 3..6 | 9 |
  | fancy_oak | 13 | 16/16 | 3..8 | 12 |
  | mega_spruce | 12 | 0/16 | -..- | - |
  | mega_spruce | 15 | 0/16 | -..- | - |
  | mega_spruce | 18 | 2/16 | 14..15 | 16 |
  | mega_spruce | 22 | 5/16 | 16..19 | 20 |
  | mega_spruce | 26 | 14/16 | 14..22 | 23 |
  | mega_spruce | 30 | 16/16 | 13..27 | 28 |
  | oak | 3 | 0/16 | -..- | - |
  | oak | 4 | 0/16 | -..- | - |
  | oak | 5 | 0/16 | -..- | - |
  | oak | 6 | 9/16 | 3..3 | 4 |
  | oak | 7 | 9/16 | 3..4 | 5 |
  | oak | 8 | 16/16 | 3..5 | 6 |
  | spruce | 4 | 0/16 | -..- | - |
  | spruce | 5 | 0/16 | -..- | - |
  | spruce | 6 | 0/16 | -..- | - |
  | spruce | 7 | 4/16 | 4..4 | 6 |
  | spruce | 8 | 9/16 | 4..5 | 7 |
  | spruce | 9 | 14/16 | 4..6 | 8 |
  | spruce | 10 | 16/16 | 4..7 | 9 |

  - Without `min_clipped_height`, a placed tree always had its top log at dy H-3 or lower, and taller rolls failed rather than shrank (oak, spruce, dark oak, mega spruce).
  - Fancy oak shrank: at H 6, all 16 placed with their top log at 3. It needs H 6 (2 of 16 placed at H 5).
- **Leaf distance:** see section 1. After the 3 s settle, a leaf's `distance` equals min(7, BFS from `#logs` through face-adjacent leaves) on every leaf of oak, birch, jungle bush and the bee oaks, and on 97.7% or more of the leaves of every other tree [P: `leaf_mismatch`].
  - The residue is fancy 1.9, mega jungle 6.2 and red poplar 5.4 per tree. After settling, the only pending leaf ticks left were 65 in one birch probe, on leaves already matching. It looks like cells whose neighbours changed without an update; this is a guess. [P, `probes\mismatch.py`]
  - All leaves are `persistent=false` [P]. Leaves beyond 6 steps (distance 7) occur only for random spread: azalea 1.7, mangrove 1.5, tall mangrove 2.5 per tree [P `leaf_bfs7`].
- **Log connectivity:** every tree's logs form one 26-connected part [P, all 38 tree probes].
  - Face-connected (6-connected) parts are always 1 for straight, giant, mega pine and spruce, poplar, cherry, pine and spruce trees, and for the bush.
  - They are 1 to 13 for fancy (branches step diagonally), 2 to 6 for acacia, 2 to 3 for azalea, 1 to 4 for dark oak, 1 to 7 for mangrove, 1 to 19 for tall mangrove and 1 to 10 for mega jungle [P].

### Trunk placers

- **straight:** one column at the origin [P oak: logs only at (0,0)]. Jungle bush is one log (base 1) [P].
- **giant (mega pine and spruce):** a 2x2 trunk at x and z in 0..+1 (extent -5..+7) [P]. alter_ground then writes about 118 podzol per tree within reach 7 [P].
- **mega_jungle:** a 2x2 at 0..+1 with side branches carrying small canopies [W]. Extent -6..+9, top log 9 to 30, 37 to 133 logs [P].
- **dark_oak (dark oak, pale oak):** a 2x2 with stubby branches; 28 to 57 logs [P]. Top log 5 to 8.
- **fancy:** a trunk of 3 to 14 with diagonal branches, each ending in a leaf blob [W]. 4 to 35 logs; highest change dy 15; extent ±7 [P].
- **forking (acacia):** a trunk that bends sideways once and may fork [W]. Logs 5 to 12, top log 4 to 8, extent ±6 [P].
- **bending (azalea):** `bend_length` 1-2 and `min_height_for_leaves` 3 [D]. 6 to 9 logs [P].
- **upwards_branching (mangrove):** a trunk with upward side branches (`place_branch_per_log_probability` 0.5, extra steps and lengths) [D][W]. Logs 2 to 16 (mangrove) and 5 to 42 (tall) [P].
- **cherry:** 1 to 3 branches of horizontal length 2-4, starting 3-4 below the top [D]. Extent ±8, leaves 128 to 397 [P].
- **poplar:** a trunk of 7 to 11 with 1-4 short branches and 4 trunk blocks above them [D]. Extent ±7, leaves 129 to 560 [P].

### Foliage placers

- **blob (oak, birch, jungle tree, super birch, swamp oak).** Seen relative to the top log [P `profiles\oak.txt`]:
  - layers -2 and -1 are radius-2 squares whose 4 corners are each kept about half the time;
  - layer 0 is a radius-1 square with half-kept corners;
  - layer +1 is a plus (corners never).
  - swamp oak is radius 3 (reach 4 with vines).
- **The other placers:** shapes are in `probes\out\profiles\<probe>.txt`, read after the quiet window; section 10 notes what it found.
  - spruce and pine reach 3; mega pine and spruce reach 7 including podzol; jungle bush reach 2 [P].
  - random_spread makes `leaf_placement_attempts` (50 or 70) tries within radius 3 and foliage height 2 [D].
  - cherry hangs leaves below its layers (`hanging_leaves_chance` 1/6, extension 1/3) [D].
  - poplar's radius is 5, 6, 7 or 8 with weights 5:5:1:1 and `side_hole_chance` 0.15 [D]. Its leaf reach was only 7 in 100 trees: question 5.

### Root placer (mangrove)

- `trunk_offset_y` 1-3 raises the trunk; roots grow down and outwards, up to `max_root_width` 8 and `max_root_length` 15, through `mangrove_roots_can_grow_through` [D][W]. They turn into muddy roots in mud [D], and moss carpets go on top at chance 0.5 [D].
- Measured: roots about 18 and muddy roots 21 per tree; moss carpet 4 to 6; vines 57 (mangrove) and 143 (tall); propagules 1.6 to 6.7 [P]. Root reach is 5 to 7, under `max_root_width` 8 [P].
- On mud under 2 water, 81 of 100 mangroves and only 30 of 100 tall mangroves placed, against 100 and 91 on grass [P]. S9 should reproduce the failure condition. Pin it with an `@mudwater` row in `Trees.Shapes`.

### Decorators

- **beehive:** writes `bee_nest[facing=south,honey_level=0]` against the trunk, under the canopy [P][W].
  - Probability p per tree, but it can fail when no side is free: fancy oak p1 gives 0.93 per tree; super birch p1 gives 1.0 [P]. Reach 1 to 3.
  - The wiki gives 2 to 3 bees [W]. Our probe saw 0 to 3 after the 3 s tick, because bees leave [P].
- **leave_vine:** with probability p per leaf side, a vine hangs down up to 4 blocks [W]. Vines per tree: jungle tree 59, swamp oak 67, mangrove 57, mega jungle 230 [P].
- **trunk_vine:** vines on each free trunk side at 75% [W].
- **cocoa:** on the bottom trunk blocks, 25% per face [W]. Jungle tree p0.2 gave 0.36 cocoa per tree on 19% of trees [P].
- **alter_ground:** podzol rings around the 2x2 base through `podzol_beneath_tree` [D][W]: 118 per tree, reach 7 [P].
- **attached_to_leaves (mangrove):** a propagule under a leaf, with `required_empty_blocks` 2 and exclusion radius 1 [D].
- **attached_to_logs (fallen trees):** a mushroom on top of a log at p0.1 [D]: 0.1 to 0.44 per tree [P].
- **place_on_ground:** `tries`, `radius` and `height` default to 128, 2 and 1 [W]. The data's pair is (96 tries, radius 4, height 2) and (150 tries, default radius, height 2) [D]. Together they give 27 to 31 leaf litter per tree within reach 4-5 [P].
- **pale_moss:** probabilities for ground, trunk and leaves [D].
  - It writes pale moss blocks plus short and tall grass on the ground (about 70% of trees), carpets, and about 17-19 hanging moss per tree under leaves and logs [P].
- **creaking_heart:** p1.0, yet only 0.31 to 0.42 per tree [P]. It needs a trunk cell fully enclosed by logs [W]. Read its state with the profile step.
- **shelf_mushroom:** on the trunk's sides, p0.4 on poplars (0.46 to 0.57 per tree, reach 1-2) and p0.8 on fallen poplars (1.17 per log) [P].

### Other big vegetation

- **fallen_tree:** the stump is at the origin; the log lies along x or z at dy 0, and decorations sit at dy +1 [P].
  - The reach equals the largest `log_length` in all six: 7, 7, 8, 10, 11 and 15 [P][D].
  - Oak and jungle stumps take trunk vines (about 2.7 per tree) [P][W].
- **huge mushrooms:** always 45 cap blocks; stems of 5 to 6 on average; red reach 2, brown reach 3; up to dy 12 [P].
  - The wiki gives heights of 5 to 7, with a 1 in 12 chance of 9, 11 or 13 [W].
- **bamboo:** a column of 6 to 17 (mean 12) [P]. `bamboo_some_podzol` (probability 0.2) wrote podzol for 22% of placements, 5.4 cells on average, within radius 4 [P].
- **vines (feature):** a single cell at its try position, against a solid side [W]. Tries per column: jungle 127, lush caves 256 [D]. Not probed: superflat air has no walls.
- **root_system (azalea):** from the air cell under a cave ceiling, it finds a surface with free space above and places `azalea_tree` there [D].
  - It writes rooted dirt along the way (115 per placement, reach 2) and hanging roots under the ceiling (8, reach 2) [P].
  - The probe's tree stood 11 above the origin. Its top log was 15 to 17 [P].
- **block_blob (forest rock):** 2 to 38 mossy cobblestone; extent -3..+1 on every axis, so the blob walks down and towards -x/-z [P].

## 5. Test oracles

Our random differs from vanilla's, so per-seed equality is impossible. The probes give two kinds of oracle: exact rules, and distributions. Run each configuration N = 400 times (or 100, against the probe's N = 100) on the probe's layered column, as `Decoration.OreProbes` does.

**Exact (must hold for every placement):**
1. **Height:** the origin column's log count is in [base, base + a + b] for straight trunks, from the data. The top log matches the probe ranges in the table (oak 3-5, mega spruce 12-28, mega jungle 9-30, fancy 3-11).
2. **Leaf distance:** every leaf's `distance` is min(7, BFS through the tree's own face-adjacent leaves from its `#logs` blocks), and `persistent=false`.
3. **Connectivity:** logs form one 26-connected part. 6-connected parts follow section 4's list; a straight, giant or poplar trunk always has 1.
4. **Reach:** at most the probe's maximum per configuration. Every value is at most 16; fallen trees are at most their largest `log_length`.
5. **Block sets:** each configuration writes only the ids its probe saw (`blocks` in its JSON). For example, oak writes only `dirt`, `oak_leaves` and `oak_log`.
6. **Counts:** huge mushrooms have exactly 45 cap blocks; jungle bush exactly 1 log; dirt under the trunk is 1 (4 for 2x2); the bee nest is `facing=south,honey_level=0`.
7. **Feature size:** under a ceiling at dy H, a placed oak, spruce, dark oak or mega spruce has its top log at H-3 or lower, and no oak places at H 5 or less. Every fancy oak places at H 6 or more, with its top log at H-3 or lower.

**Distributions** (N = 400 against the probe's 100 and its p5/p50/p95):
1. Mean cells, logs and leaves per tree within ±5%.
2. Top-log histograms within a chi-square test at p = 0.01.
3. Per-layer leaf occupancy within ±0.1 of `profiles\<probe>.txt`.
4. Decorator means per tree within 3 binomial sigma: beehive 0.93 to 1.0 at p1; cocoa 0.36; leaf litter 27 to 31; podzol 118; vines per the table; creaking heart 0.31 to 0.42.
5. Placement rate on mud under water: mangrove 0.81, tall mangrove 0.30 (±0.1).

**Reproduce**, from `C:\dev\sandbox\scout-trees\probes`, with JDK 25 and the 26.3 server jar at S1b's paths:

```powershell
uv run --no-project --python 3.13 python -I tree_probe.py list
uv run --no-project --python 3.13 python -I tree_probe.py all --server-dir <fresh folder> --out <out> [probe ...]
uv run --no-project --python 3.13 python -I tree_probe.py profile --server-dir <same> --out <out> [probe ...]
uv run --no-project --python 3.13 python -I table.py <out> <notes>; uv run --no-project --python 3.13 python -I cost.py <notes> <out>
```

A run of all 82 probes takes about 17 minutes at below-normal priority with 2 GB of heap. Each run draws new random numbers, so the means move within their sampling error.

**Mutations that must fail:**

| Mutation | Fails |
|---|---|
| `base_height` + 1 | Exact 1 |
| foliage radius + 1 | Distributions 1 and 3, exact 4 |
| corners always kept | Distribution 3 |
| distance from every log in the window instead of the tree's own | Exact 2, on `Trees.Borders` with two trees touching |
| skip the feature-size check | Exact 7 |
| giant trunk at -1..0 instead of 0..+1 | the extent signs |
| leaves written with `Force` | `Trees.Borders`: the earlier tree's logs survive |
| beehive probability x2 | Distribution 4 |
| fallen log one longer | Exact 4 for fallen super birch, which would then reach 16 |

## 6. Draft design

- **Compile.** `CompileFeature` gains these kinds: `tree`, `fallen_tree`, the two huge mushrooms, `bamboo`, `vines`, `root_system` (its nested placed feature compiled like a selector branch) and `block_blob`.
  - Each compiles once into a POD config: trunk, foliage and root placer as tagged structs with their providers; feature size; decorator list; block states resolved through S8's state lookup.
  - S8's selectors dispatch to them.
  - Each config gets a static reach bound, asserted at most 16 at `Initialize`.
- **Place one tree** (`VoxelTreeFeature::Place`), given the origin and the try's stream:
  1. Roll the trunk height (and the root offset).
  2. Check the feature size against the working copy: a cell is free if air or `replaceable_by_trees`. Also fail if the top would pass `MaxZ`.
  3. Mangrove roots.
  4. The soil under the trunk.
  5. The trunk: logs with their axis; it returns foliage attachment points (position, radius bonus, double-trunk flag).
  6. The foliage.
  7. Leaf distance by BFS over the tree's own leaves. Use a dense local scratch of 33 x 33 x 48 bits around the origin, or a small hash for tall trees.
  8. The decorators, reading the tree's log and leaf lists and the working copy.
  9. Emit every cell.

  Writes also go into the working copy, so the column's later features (S8's plants) see them, as with ores.
- **Write modes.**

  | Cells | Mode |
  |---|---|
  | logs, leaves, mangrove roots, bee nest, creaking heart | `IfReplaceable(replaceable_by_trees)`, the plan's mode. Air is implied: S9 must check whether S2's `IfReplaceable` includes air. |
  | dirt under the trunk | `IfReplaceable(not cannot_replace_below_tree_trunk)`, or an `IfTargets` list built from the provider's rule |
  | podzol | `IfReplaceable(beneath_tree_podzol_replaceable)` |
  | rooted dirt | `IfReplaceable(azalea_root_replaceable)` |
  | vines, cocoa, propagules, leaf litter, hanging moss, shelf mushrooms | `IfReplaceable(air)` |

  - A vine's or cocoa's support is horizontal, so it may sit in the next column, and S2's support slot is vertical only. Keep these without a support check: their support is the same tree's block, written in the same run.
  - Leaf litter and ground moss take S8's vertical support condition.
- **Overlap:** settled. The earlier tree in merge order keeps its logs. Leaf distances stay per tree, which is cosmetic because nothing decays.
- **Block entities:** states only (Q2). No saved data. Phase 7 derives bees (2-3) from Hash(seed, position).
- **Switches:**
  - `voxel.Decoration.Trees` (default 1). At 0 the tree kinds are skipped, so S8's goldens and hashes reproduce.
  - `voxel.Decoration.TreeLeavesUnsupported` (question 4).
  - `voxel.Bench Decorate 4 <preset> [biome]`, using `FixedBiome` for the per-biome-group figures.
- **Gates (plan):** Decorate (trees) at most 0.4 ms and Finalize at most 0.3 ms, on preset means; the per-group single-biome figures are reported.
- **Cost risk.** The upper bound, every try a success:

  | Biome | Tries per column | Cells written per column | of which leaves | Decorate at 40 / 90 ns per cell (ms) |
  |---|---|---|---|---|
  | jungle (jungle) | 50.4 | 7,966 | 4,404 | 0.32 / 0.72 |
  | bamboo_jungle (jungle) | 23.8 | 10,536 | 5,022 | 0.42 / 0.95 |
  | sparse_jungle (jungle) | 2.1 | 198 | 116 | 0.01 / 0.02 |
  | mangrove_swamp (mangrove) | 25.0 | 12,458 | 7,454 | 0.50 / 1.12 |
  | dark_forest (dark/pale) | 16.0 | 2,474 | 1,500 | 0.10 / 0.22 |
  | pale_garden (dark/pale) | 16.0 | 4,121 | 1,924 | 0.16 / 0.37 |
  | forest (forest) | 10.1 | 1,010 | 645 | 0.04 / 0.09 |
  | birch_forest (forest) | 10.1 | 618 | 547 | 0.02 / 0.06 |
  | old_growth_birch_forest (forest) | 10.1 | 633 | 547 | 0.03 / 0.06 |
  | flower_forest (forest) | 6.1 | 433 | 391 | 0.02 / 0.04 |
  | dappled_forest (forest) | 6.0 | 1,311 | 1,101 | 0.05 / 0.12 |
  | taiga (taiga) | 10.1 | 638 | 557 | 0.03 / 0.06 |
  | snowy_taiga (taiga) | 10.1 | 638 | 557 | 0.03 / 0.06 |
  | grove (taiga) | 10.1 | 643 | 562 | 0.03 / 0.06 |
  | old_growth_pine_taiga (taiga) | 12.1 | 1,526 | 797 | 0.06 / 0.14 |
  | old_growth_spruce_taiga (taiga) | 12.1 | 2,189 | 1,433 | 0.09 / 0.20 |
  | cherry_grove (cherry/meadow) | 10.1 | 2,651 | 2,482 | 0.11 / 0.24 |
  | meadow (cherry/meadow) | 0.0 | 1 | 1 | 0.00 / 0.00 |
  | savanna (savanna/badlands) | 1.1 | 88 | 79 | 0.00 / 0.01 |
  | savanna_plateau (savanna/badlands) | 1.1 | 88 | 79 | 0.00 / 0.01 |
  | windswept_savanna (savanna/badlands) | 2.1 | 169 | 150 | 0.01 / 0.02 |
  | wooded_badlands (savanna/badlands) | 5.1 | 458 | 278 | 0.02 / 0.04 |
  | swamp (swamp) | 2.1 | 439 | 283 | 0.02 / 0.04 |
  | windswept_forest (windswept) | 3.1 | 219 | 197 | 0.01 / 0.02 |
  | windswept_hills (windswept) | 0.1 | 7 | 6 | 0.00 / 0.00 |
  | windswept_gravelly_hills (windswept) | 0.1 | 7 | 6 | 0.00 / 0.00 |
  | plains (sparse (plains, snowy, oceans)) | 0.1 | 5 | 5 | 0.00 / 0.00 |
  | sunflower_plains (sparse (plains, snowy, oceans)) | 0.1 | 5 | 5 | 0.00 / 0.00 |
  | snowy_plains (sparse (plains, snowy, oceans)) | 0.1 | 7 | 6 | 0.00 / 0.00 |
  | ice_spikes (sparse (plains, snowy, oceans)) | 0.1 | 7 | 6 | 0.00 / 0.00 |
  | ocean (sparse (plains, snowy, oceans)) | 0.1 | 7 | 7 | 0.00 / 0.00 |
  | river (sparse (plains, snowy, oceans)) | 0.1 | 7 | 7 | 0.00 / 0.00 |
  | lush_caves (caves) | 1.5 | 358 | 136 | 0.01 / 0.03 |
  | deep_dark (caves) | 0.1 | 5 | 5 | 0.00 / 0.00 |
  | dripstone_caves (caves) | 0.1 | 5 | 5 | 0.00 / 0.00 |
  | mushroom_fields (mushroom) | 1.0 | 50 | 0 | 0.00 / 0.00 |

  - Levers if mangrove or jungle exceed the gate:
    1. A dense copy-on-write section in the working copy once a column passes a few thousand writes, instead of the hash table.
    2. Skip the BFS for blob foliage, whose distances are fixed per layer pattern.
    3. Emit leaves per layer span.
  - The write log's size: up to about 100 KB per mangrove column (12.5k writes x 8 bytes) on top of the ores' 63 KB.

## 7. Code-shape sketches (against 015bb42; names follow `VoxelOreFeature.h`)

```cpp
// Decoration/Features/VoxelTreeFeature.h  (assumes S8's FVoxelStateProvider and FVoxelIntProvider with weighted_list)
enum class EVoxelTrunkPlacer : uint8 { Straight, Forking, Giant, MegaJungle, DarkOak, Fancy, Bending, UpwardsBranching, Cherry, Poplar };
enum class EVoxelFoliagePlacer : uint8 { Blob, Spruce, Pine, Acacia, Bush, Fancy, Jungle, MegaPine, DarkOak, RandomSpread, Cherry, Poplar };
struct FVoxelTrunkPlacer { EVoxelTrunkPlacer Type; int32 Base, RandA, RandB; FVoxelIntProvider P[4]; float Chance; int32 Extra[2]; FVoxelBlockSet GrowThrough; };
struct FVoxelFoliagePlacer { EVoxelFoliagePlacer Type; FVoxelIntProvider Radius, Offset, Height; float Chances[4]; int32 Attempts; };
struct FVoxelFeatureSize { bool bThreeLayers; int32 Limit, UpperLimit, Lower, Middle, Upper; int32 MinClipped /* -1 none */; };
struct FVoxelTreeDecorator { EVoxelTreeDecorator Type; float Probability; /* per type: provider, directions, radii, tries */ };
struct FVoxelTreeConfig
{
    FVoxelStateProvider Trunk, Foliage, BelowTrunk; FVoxelTrunkPlacer TrunkPlacer; FVoxelFoliagePlacer FoliagePlacer;
    FVoxelFeatureSize Size; TOptional<FVoxelMangroveRoots> Roots; TArray<FVoxelTreeDecorator> Decorators; bool bIgnoreVines;
};
namespace VoxelTreeFeature
{
    struct FAttachment { FIntVector Pos; int32 RadiusBonus; bool bDoubleTrunk; };
    struct FScratch { TArray<FIntVector> Logs, Leaves, Roots; TArray<FAttachment> Attach; TBitArray<> Local; TArray<uint8> Distance; };
    int32 GetReach(const FVoxelTreeConfig& Config);              // static bound, <= 16 asserted at Initialize
    // Returns false (nothing emitted) when the free-space check fails.
    template <typename FEmit> bool Place(const FVoxelTreeConfig&, FIntVector Origin, FVoxelXoroshiro& Random,
        FVoxelDecorationContext& Context, const FVoxelBlockTable& Table, FScratch& Scratch, FEmit&& Emit);
}
```

The `Place` outline:
1. `H = Base + Random(0..A) + Random(0..B)`.
2. `if (!FreeHeight(Size, Origin, H, Context)) return false;`
3. Roots, then dirt.
4. `TrunkPlace(...)` into `Scratch.Logs` and `Scratch.Attach`.
5. For each attachment: `FoliagePlace(...)` into `Scratch.Leaves`.
6. `LeafDistanceBfs(Scratch)`.
7. Decorators.
8. For each cell: `Emit(X, Y, Z, State, Mode, Argument)`.

In `Decorate`, emission is the ore lambda (`VoxelDecorator.cpp` 305-321) with `EVoxelWriteMode::IfReplaceable` and the tag index, plus `Context.SetState`.

```cpp
// VoxelDecorator.h FFeature gains: int32 Tree = INDEX_NONE, Big = INDEX_NONE; TArray<FVoxelTreeConfig> Trees; TArray<FVoxelBigVegetation> Big;
// Tests (VoxelDecorationTests.cpp): Unnamed.Voxel.Trees.Shapes loads probes\out\<probe>.json (or the reference's copy,
// question 1), runs 400 placements per configuration on the probe column, and checks section 5. Unnamed.Voxel.Trees.Borders
// places a tree at each of the 4 column corners and checks every load order (as Decoration.LoadOrder does).
```

## 8. Re-orientation: what other sessions change before S9

| Session | Changes | What S9 diffs first |
|---|---|---|
| S4 (ore veins; surface rules, density terrain, VoxelConsole) | `Surface/`, `Terrain/VoxelDensityTerrain.*`, `VoxelConsole` | The `FixedBiome` path and the console's `voxel.Bench` argument parsing, before adding `[biome]`. Generate's figures move, so S9 re-takes its baseline. |
| S6 (block models: block table, mesher, chunk component, VoxelBlocks, pipeline switches) | `Blocks/VoxelBlockTable.h`, `Source/Unnamed/Voxel/VoxelBlocks.cpp` (`MakeTable`), the mesher, `VoxelChunkComponent`, switches | How model states (vines, cocoa, propagules, leaf litter, mangrove roots, bamboo, hanging moss) enter the table; any state-by-properties lookup; `bSolid` and the heightmap classes of leaves and model blocks; leaf tint. |
| S6b (Playground meshes, materials) | Playground only | Nothing for generation; the evidence shots may use its materials. |
| Torch light probe (VoxelWorld shot code) | `Source/Unnamed/Voxel/VoxelWorld.cpp` and `.h` (shot flags, shot search) | Before adding `-VoxelShotTrees` and the column-grid overlay shot. |
| S7 (block models: interaction) | placement and support rules | How support is expressed, for leaf litter and ground moss. |
| S8 (plants, before S9) | modifiers, predicates, providers, selectors, `Features/`, `VoxelDecorationTests.cpp`, goldens | Everything in `CompileFeature` and the providers. S9 builds on S8's `would_survive`, heightmap and selector code, and on its state lookup. |

## 9. Risks, size, split, questions

**Risks:**
1. **Cost in mangrove and jungle** (section 6). It is likely over 0.4 ms in single-biome worlds, and Finalize is at risk there.
2. **Shape fidelity.** The placers' exact algorithms are not on the wiki: upwards_branching, cherry and poplar are marked "needs more info" [W]. They must be fitted to the probes, as S2 did for ore blobs. The per-layer profiles make that possible for the layered placers. Branching trees (fancy, mangrove, cherry, mega jungle) can only be matched statistically.
3. **Mud and water failures for mangroves** need a rule that reproduces 81% and 30%. The cause is unknown black-box. Candidates: roots unable to reach through water, or the trunk offset.
4. **Leaf distance residue:** vanilla itself is 0 to 2.3% off BFS. Pin BFS, not vanilla's stored values.
5. **Reach margin:** 1 block for fallen super birch. Any change that adds a horizontal offset to a fallen tree breaks the 3x3.
6. **S8 dependency:** state lookup by properties, weighted int providers, selectors.

**Size and split:** L. Recommended:
- **S9a, tree core:** placers, sizes, roots, soil, leaf distance, `Trees.Shapes` (logs and leaves), `Trees.Borders`, the switch.
- **S9b:** the 11 decorators, fallen trees, huge mushrooms, bamboo, vines, azalea roots, forest rock, the shots, the per-biome gates and `Decoration.Golden`.

**Questions for Jesus** (options and a recommendation each):

1. **Tree probe facts in the reference?**
   - (a) **Recommended:** S9a's first step moves `tree_probe.py` into `Reference/minecraft-worldgen-26.3/tools/`, re-runs it into staging, and extends `build_reference.py` and `verify_reference.py` to accept tree probes and their variants (`@mudwater`, ceilings). Tests then read hash-checked facts. A new run means new random numbers; the oracles in section 5 are robust to that.
   - (b) Tests keep the numbers typed in from this doc.
   - (c) Copy this sandbox's outputs in as-is.
2. **Split S9:**
   - (a) **Recommended:** S9a and S9b as above.
   - (b) One L session.
3. **The gate in dense biomes:**
   - (a) **Recommended:** gate on the presets' means as every other session does; report single-biome jungle, mangrove and pale garden as information, and flag them if over.
   - (b) Gate every biome group at 0.4 ms. Expect mangrove to need the dense working-copy lever.
4. **Leaves farther than 6 from a log** (random spread: azalea, mangroves; 1.5 to 2.7 per tree):
   - (a) **Recommended:** drop them at placement. Vanilla's decay removes them within minutes, so this matches what a player sees, and we have no decay.
   - (b) Place them as vanilla does, so they float forever.
5. **Poplar canopy radius 8** (weight 1/12) never showed a leaf beyond 7 in 300 poplars:
   - (a) **Recommended:** fit the shape to the probes (the radius may count from a centre that excludes the trunk).
   - (b) Take the data's radius literally.

## 10. After the quiet window

Run after Nyx's "window over" (03:17).

- **Reach re-check with 48-block cells**, which measure reach up to 23 (`probes\tree_probe_wide.py`, `probes\out-wide\`). 196 placements each, so 296 with the first run:
  - fallen super birch 15, in all four directions; its log count is at most 14, so there is a gap between stump and log;
  - mega jungle 9 (extent -6..+9, y -1..31);
  - cherry 8;
  - tall mangrove 8 (174 of 196 placed on grass).

  These are the same maxima as the 32-block run. Every configuration fits the 3x3 from any origin.
- **Creaking heart state:** `creaking_heart[axis=y,creaking_heart_state=dormant,natural=true]`, 42 of 42, read after the daytime settle.
- **Foliage profiles** (`probes\out\profiles\<probe>.txt`; per layer relative to the highest log, as mean leaves per tree / largest Chebyshev radius). Each file also has occupancy grids (`#` always, `.` never, digits = tenths) and the decorators' dy and radius histograms.

  | Tree | Layers |
  |---|---|
  | blob (birch, jungle tree) | -2: 22/r2, -1: 22/r2, 0: 6/r1, +1: 5/r1. This is exact: radius-2 squares minus half the corners, then a radius-1 square minus half the corners, then a plus. |
  | swamp oak | -2: 46/r3, -1: 46/r3, 0: 22/r2, +1: 21/r2 |
  | jungle bush | 0: 21.9/r2, +1: 6.9/r1, +2: 0.5 |
  | spruce | a cone from dy -6 (r3) to +3 (r1): 1, 4.4, 11.4, 14.2, 12.2, 8.4, 5.2, 3.0, 1.9, 1.0 |
  | pine | -2: 5.2/r2, -1: 14.6/r3, 0: 15.5/r2, +1: 5/r1, +2: 1 |
  | mega spruce | 18 layers from -16 (r4, r5 widest at -15..-11) to +1 (4/r1) |
  | mega pine | -6 to +1, at most r5 |
  | dark oak | -1: 42/r5, 0: 52/r6, +1: 24/r5, +2: 2/r3 |
  | acacia | its main canopy is at 0 (46/r6) and +1 (14/r5), with the fork's small canopy below (-5..-1) |
  | cherry | -4..+3, widest at 0 and +1 (65 each, r8) |
  | poplar | -4..+2, widest -2 (62/r7). No layer ever reached r8 in 300 trees (question 5). |
  | fancy | -6..+4, r7 at most |
  | mega jungle | the crown at -1 (92/r6), 0 (67/r5), +1 (44/r4), plus branch blobs down to -16 |
  | azalea | -4..+1, r6 at most |
