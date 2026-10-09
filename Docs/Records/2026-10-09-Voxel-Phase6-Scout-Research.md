# Voxel phase 6 research: decoration (scout report, October 9, 2026)

> **Copy note (October 9, 2026, phase 6 plan session):** this report was copied as is from `C:\dev\sandbox\scout-phase6\phase6-research.md` for [the phase 6 plan](2026-10-09-Voxel-Phase6-Plan.md). Its `work\*.md` and `work\*.txt` tables are in [2026-10-09-Voxel-Phase6-Scout-Tables](2026-10-09-Voxel-Phase6-Scout-Tables/). The jar extract (`jar-data\`), the JSON work files and the scripts were not kept, because game data enters only through the references' own tools. The plan's section 1.2 corrects three points: ore targets use rule tests (`predicate_type`), the heightmaps come from block tags, and feature ids nest.

Read-only scout for the phase 6 planner. Nothing in `C:\dev\projects\Unnamed` was changed. Every number below comes from the 26.3 server jar's data (extracted to `jar-data\`), the client jar's blockstate and model JSON (read in place), or the project's own docs and headers. No Mojang code was read: behaviour notes describe what the game does, not how its code does it, and the ones that need confirming are marked *(observed; confirm by dump)*.

Inputs read: `CLAUDE.md`; `Docs/worldgen-implementation-prompt.md` (ground rules, randomness, surface and decoration, job graph, phases); `Docs/VoxelWorld.md` (status, design decisions, known gaps); `Docs/Records/2026-10-09-Voxel-Phase5-Plan.md` (sections 3, 4 and 7); `Docs/Blocks.md`; `C:\dev\tools\shift-managers\IDEAS.md` (terrain slabs); the facts-dumper memory note.

## 0. Summary

1. Overworld biomes use **171 placed features** (210 counting nested ones), backed by **182 configured features of 39 types**, in 9 of the 11 steps. `raw_generation` and `strongholds` are empty. Vegetal decoration has 109 placed features and underground ores 34. Each placed feature sits in exactly one step, and every step's order across the 56 biomes is acyclic, so one global order per step exists.
2. **26.3 changed the format.** `configured_feature` is now `worldgen/feature`, and `configured_carver` is now `worldgen/carver`. `worldgen/block_state_provider` is a new folder of 8 named providers. Block states are written `{"id", "properties"}`. `random_patch` is gone: a patch is now a placement chain (count, offset, filter) around a `simple_block`. New types include `sequence`, `template`, `speleothem`, poplar placers and support tags. A reader built from 1.20-era docs would be wrong.
3. Decoration touches **183 blocks**. 97 work today (cubes, logs, leaves, ores, fluids). **83 blocks (about 950 states, 135 textures) are missing**, plus chest and spawner, which phase 6 can defer. They group into crosses, double plants, seagrass, flowerbeds, carpets, multiface (vine, lichen, sculk vein) and 26 custom-element models.
4. **Block-shapes step:** I recommend a generic baked-model path: import each model's element boxes as per-state quads, and take collision and outline boxes from dumped facts. That covers all 83 blocks, and it also covers phase 7's stairs, fences, doors and panes. Hand-coding each shape would not cover phase 7.
5. **Determinism:** I recommend push-style decoration with write logs. Each column decorates once from a frozen 3x3 snapshot of the neighbours' pre-decoration blocks. Finalize rebuilds the column from those blocks plus every write log in its 3x3, replayed in a fixed order with conditional writes. A 3x3 isolation oracle is the test. Meshing waits for 3x3 finalization. The generated ring grows from R+1 to R+3, about +33% generated columns at radius 12.
6. Ores match the prompt's table, with a few extras the prompt omits: diamond medium, large and buried; lower gold; small iron; infested stone; dripstone-cave copper; lush-cave clay. Carvers are the same three in all 56 biomes: cave 0.15, extra cave 0.07, canyon 0.01.
7. Proposed split: **2 block-shapes sessions, then 4 phase 6 sessions**. They are: shapes data and the Playground; shapes in the voxel mesher and interaction; the framework, pipeline and ores; trees; carvers and underground; water, cold, finalize polish, performance and the record. Terrain slabs would be an optional pass at Finalize.
8. There are 8 open questions for Jesus, each with a recommendation (section 4.3).

## 1. Inventory

### 1.1 Format notes for the reader (26.3)

| Topic | What the data shows |
|---|---|
| Folders | `worldgen/placed_feature` (273), `worldgen/feature` (240, formerly `configured_feature`), `worldgen/carver` (4, formerly `configured_carver`), `worldgen/block_state_provider` (8, new: `cave_vines_body/head`, `flower_flower_forest`, `flower_meadow`, `flower_plain`, `mangrove_propagule`, `podzol_beneath_tree`, `soil_beneath_tree`). Tags: `tags/block` (300), `tags/worldgen/feature/can_spawn_from_bone_meal`. |
| Shape | A feature's fields sit beside `"type"`, with no `config` wrapper. A placed feature is `{feature, placement[]}`, and either may be inline. Selectors point at placed features by id (`features`, `default`, and `data` in weighted lists). |
| Block states | `{"id": "minecraft:grass_block", "properties": {"snowy": "false"}}`. Some fields take a bare id (`base_block`, `pointed_block`, `state` in ore targets, `output_state`). |
| Patches | There is no `random_patch` type. Example: `patch_grass_plain` is `noise_threshold_count(-0.8: 5/10)`, `in_square`, `heightmap WORLD_SURFACE_WG`, `biome`, `count 32`, `offset trapezoid ±7 / ±3 / ±7`, `block_predicate_filter air`, then `simple_block grass`. Modifiers can fan one origin out into many positions after the heightmap. 66 reachable placed features carry xz offsets; the largest are ±10 (`pointed_dripstone`, `sulfur_spike`), and seagrass and grass are ±7. |
| New types | `sequence` (runs its features in order at one origin, for example the sculk patch followed by a catalyst), `template` (structure templates: desert well), `speleothem` and `speleothem_cluster` (dripstone and sulfur spikes, each with `base_block` and `pointed_block`), `overlay`, `coral_tree` and `coral_claw` (each takes a nested placed feature per coral block), `fallen_tree`, `root_system`, `pale_moss`, `creaking_heart`, `shelf_mushroom` and `place_on_ground` decorators, `poplar_*` placers, the `cuboid` and `randomly_selected` modifiers, and `rule_based` providers (`if_true`, `then`, `fallback`). |
| Survival as data | Support rules are block tags: `supports_vegetation`, `supports_sugar_cane` (+`_adjacently`), `supports_cactus`, `supports_lily_pad`, `supports_bamboo`, `supports_azalea`, `supports_big_dripleaf`, `supports_small_dripleaf`, `supports_cocoa`, `supports_dry_vegetation`, `supports_(hanging_)mangrove_propagule`, `cannot_support_kelp/seagrass/snow_layer`, `support_override_*`. `would_survive` filters (47 reachable) use them. The names are already in `Config/Tags/MinecraftBlockTags.ini`, but membership lists only imported blocks. |
| Fixed noise | The three flower providers (`noise_threshold`) and the `noise_*_count` modifiers use a fixed seed, 2345 (scale 0.005 for flowers), not the world seed, so vanilla's flower bands sit in the same place in every world. Ours should be world-seeded (Q4). |
| New biomes | `dappled_forest` (red, orange and yellow poplars with leaf litter, shelf mushrooms, red shrubs), `sulfur_caves` (sulfur, potent sulfur, sulfur spikes, a rooted sulfur spring, a sulfur pool, cinnabar). |
| Coverage | 58 configured features are unreachable from the overworld: nether, end, bone meal, piles (village), and the bare `orange/red/yellow_poplar` (only their `_leaf_litter` versions are used). |

### 1.2 Features per biome group

Groups used below: ocean (9), river_shore (river, frozen river, beaches, stony shore: 5), plains (plains, sunflower, snowy plains, ice spikes, meadow: 5), forest (8, including pale garden, dappled forest, cherry grove), taiga (5, including grove), jungle (3), savanna (3), dry (desert and the 3 badlands), swamp (2), mountain (windswept x3, the 3 peaks, snowy slopes: 7), mushroom (1), cave (lush, dripstone, deep dark, sulfur: 4).

| Group | Biomes | Features per biome, steps 1-4,7,8,10 | Ores (6) | Vegetal (9) | Total |
|---|---|---|---|---|---|
| ocean | 9 | 8-11 | 29 | 9-12 | 48-49 |
| river_shore | 5 | 8 | 29 | 8-11 | 45-48 |
| plains | 5 | 8-10 | 29-30 | 6-11 | 45-48 |
| forest | 8 | 8-9 | 29-30 | 5-12 | 42-49 |
| taiga | 5 | 8-10 | 29-30 | 3-14 | 43-52 |
| jungle | 3 | 8 | 29 | 11-12 | 48-49 |
| savanna | 3 | 8 | 29 | 9-10 | 46-47 |
| dry | 4 | 8-11 | 29-30 | 10-11 | 48-50 |
| swamp | 2 | 10 | 27-28 | 7-15 | 45-52 |
| mountain | 7 | 9-10 | 30 | 1-10 | 40-49 |
| mushroom | 1 | 8 | 29 | 6 | 43 |
| cave | 4 | 6-12 | 29-30 | 2-9 | 43-48 |

### 1.3 Placed features by step

"Biomes" gives the number of biomes using the feature and, by group, how many of each group's biomes use it.

**0. raw_generation** (0 placed features)

None in the overworld.


**1. lakes** (4 placed features)

| Placed feature | Feature type | Biomes |
|---|---|---|
| lake_lava_underground | lake | 55: all but cave 3/4 |
| lake_lava_surface | lake | 55: all but cave 3/4 |
| rooted_sulfur_spring | root_system | 1: sulfur_caves |
| sulfur_pool | sequence | 1: sulfur_caves |

**2. local_modifications** (5 placed features)

| Placed feature | Feature type | Biomes |
|---|---|---|
| amethyst_geode | geode | all 56 |
| iceberg_packed | iceberg | 2: deep_frozen_ocean, frozen_ocean |
| iceberg_blue | iceberg | 2: deep_frozen_ocean, frozen_ocean |
| large_dripstone | large_dripstone | 1: dripstone_caves |
| forest_rock | block_blob | 2: old_growth_pine_taiga, old_growth_spruce_taiga |

**3. underground_structures** (4 placed features)

| Placed feature | Feature type | Biomes |
|---|---|---|
| monster_room | monster_room | all 56 |
| monster_room_deep | monster_room | all 56 |
| fossil_upper | fossil | 3: desert, mangrove_swamp, swamp |
| fossil_lower | fossil | 3: desert, mangrove_swamp, swamp |

**4. surface_structures** (4 placed features)

| Placed feature | Feature type | Biomes |
|---|---|---|
| blue_ice | blue_ice | 2: deep_frozen_ocean, frozen_ocean |
| desert_well | overlay | 1: desert |
| ice_spike | spike | 1: ice_spikes |
| ice_patch | disk | 1: ice_spikes |

**5. strongholds** (0 placed features)

None in the overworld.


**6. underground_ores** (34 placed features)

| Placed feature | Feature type | Biomes |
|---|---|---|
| ore_dirt | ore | all 56 |
| ore_gravel | ore | all 56 |
| ore_granite_upper | ore | all 56 |
| ore_granite_lower | ore | all 56 |
| ore_diorite_upper | ore | all 56 |
| ore_diorite_lower | ore | all 56 |
| ore_andesite_upper | ore | all 56 |
| ore_andesite_lower | ore | all 56 |
| ore_tuff | ore | all 56 |
| ore_coal_upper | ore | all 56 |
| ore_coal_lower | ore | all 56 |
| ore_iron_upper | ore | all 56 |
| ore_iron_middle | ore | all 56 |
| ore_iron_small | ore | all 56 |
| ore_gold | ore | all 56 |
| ore_gold_lower | ore | all 56 |
| ore_redstone | ore | all 56 |
| ore_redstone_lower | ore | all 56 |
| ore_diamond | ore | all 56 |
| ore_diamond_medium | ore | all 56 |
| ore_diamond_large | ore | all 56 |
| ore_diamond_buried | ore | all 56 |
| ore_lapis | ore | all 56 |
| ore_lapis_buried | ore | all 56 |
| ore_copper | ore | 55: all but cave 3/4 |
| underwater_magma | underwater_magma | all 56 |
| ore_gold_extra | ore | 3: badlands, eroded_badlands, wooded_badlands |
| disk_sand | disk | 54: all but swamp |
| disk_clay | disk | all 56 |
| disk_gravel | disk | 54: all but swamp |
| ore_emerald | ore | 10: plains 1/5, forest 1/8, taiga 1/5, mountain 7/7 |
| ore_copper_large | ore | 1: dripstone_caves |
| ore_clay | ore | 1: lush_caves |
| disk_grass | disk | 1: mangrove_swamp |

**7. underground_decoration** (7 placed features)

| Placed feature | Feature type | Biomes |
|---|---|---|
| ore_infested | ore | 10: plains 1/5, forest 1/8, taiga 1/5, mountain 7/7 |
| sculk_vein | multiface_growth | 1: deep_dark |
| sculk_patch_deep_dark | sequence | 1: deep_dark |
| dripstone_cluster | speleothem_cluster | 1: dripstone_caves |
| pointed_dripstone | simple_random_selector | 1: dripstone_caves |
| sulfur_spike_cluster | speleothem_cluster | 1: sulfur_caves |
| sulfur_spike | simple_random_selector | 1: sulfur_caves |

**8. fluid_springs** (3 placed features)

| Placed feature | Feature type | Biomes |
|---|---|---|
| spring_water | spring_feature | 55: all but cave 3/4 |
| spring_lava | spring_feature | 55: all but cave 3/4 |
| spring_lava_frozen | spring_feature | 4: taiga 1/5, mountain 3/7 |

**9. vegetal_decoration** (109 placed features)

| Placed feature | Feature type | Biomes |
|---|---|---|
| glow_lichen | multiface_growth | all 56 |
| patch_grass_badlands | simple_block | 24: ocean 9/9, river_shore 5/5, plains 2/5, forest 1/8, dry 4/4, mountain 3/7 |
| patch_dry_grass_badlands | simple_block | 3: badlands, eroded_badlands, wooded_badlands |
| patch_dead_bush_badlands | simple_block | 3: badlands, eroded_badlands, wooded_badlands |
| brown_mushroom_normal | simple_block | 41: ocean 9/9, river_shore 5/5, plains 4/5, forest 5/8, taiga 2/5, jungle 3/3, savanna 3/3, dry 4/4, swamp 1/2, mountain 3/7, cave 2/4 |
| red_mushroom_normal | simple_block | 41: ocean 9/9, river_shore 5/5, plains 4/5, forest 5/8, taiga 2/5, jungle 3/3, savanna 3/3, dry 4/4, swamp 1/2, mountain 3/7, cave 2/4 |
| patch_sugar_cane_badlands | block_column | 3: badlands, eroded_badlands, wooded_badlands |
| patch_pumpkin | simple_block | 46: all but plains 4/5, forest 6/8, swamp 1/2, mountain 4/7, mushroom, cave 2/4 |
| patch_cactus_decorated | block_column | 3: badlands, eroded_badlands, wooded_badlands |
| patch_firefly_bush_near_water | simple_block | 42: ocean 9/9, river_shore 5/5, plains 4/5, forest 6/8, taiga 4/5, jungle 3/3, savanna 3/3, dry 3/4, swamp 1/2, mountain 3/7, mushroom |
| bamboo | bamboo | 1: bamboo_jungle |
| bamboo_vegetation | random_selector | 1: bamboo_jungle |
| flower_warm | simple_block | 5: jungle 3/3, savanna 2/3 |
| patch_grass_jungle | simple_block | 3: bamboo_jungle, jungle, sparse_jungle |
| patch_sugar_cane | block_column | 38: ocean 9/9, river_shore 5/5, plains 4/5, forest 6/8, taiga 4/5, jungle 3/3, savanna 3/3, mountain 3/7, mushroom |
| vines | vines | 3: bamboo_jungle, jungle, sparse_jungle |
| patch_melon | simple_block | 2: bamboo_jungle, jungle |
| flower_default | simple_block | 29: ocean 9/9, river_shore 5/5, plains 2/5, forest 4/8, taiga 4/5, savanna 1/3, dry 1/4, mountain 3/7 |
| forest_flowers | simple_random_selector | 4: forest 4/8 |
| wildflowers_birch_forest | simple_block | 2: birch_forest, old_growth_birch_forest |
| trees_birch | random_selector | 1: birch_forest |
| patch_bush | simple_block | 9: river_shore 2/5, plains 1/5, forest 3/8, mountain 3/7 |
| patch_grass_forest | simple_block | 6: forest 6/8 |
| patch_tall_grass_2 | simple_block | 8: plains 3/5, forest 1/8, cave 4/4 |
| patch_grass_plain | simple_block | 5: plains 2/5, forest 1/8, cave 2/4 |
| flower_cherry | simple_block | 1: cherry_grove |
| trees_cherry | tree | 1: cherry_grove |
| trees_water | random_selector | 11: ocean 9/9, river_shore 2/5 |
| seagrass_cold | weighted_random_selector | 1: cold_ocean |
| kelp_cold | block_column | 4: ocean 4/9 |
| trees_dappled_forest | weighted_random_selector | 1: dappled_forest |
| brown_mushroom_dappled_forest | simple_block | 1: dappled_forest |
| patch_red_shrub | simple_block | 1: dappled_forest |
| dark_forest_vegetation | random_selector | 1: dark_forest |
| patch_leaf_litter | simple_block | 1: dark_forest |
| seagrass_deep_cold | weighted_random_selector | 1: deep_cold_ocean |
| trees_plains | random_selector | 4: plains 2/5, cave 2/4 |
| flower_plains | simple_block | 4: plains 2/5, cave 2/4 |
| seagrass_deep_warm | weighted_random_selector | 1: deep_lukewarm_ocean |
| kelp_warm | block_column | 2: deep_lukewarm_ocean, lukewarm_ocean |
| seagrass_deep | weighted_random_selector | 1: deep_ocean |
| patch_dry_grass_desert | simple_block | 1: desert |
| patch_dead_bush_2 | simple_block | 1: desert |
| patch_sugar_cane_desert | block_column | 1: desert |
| patch_cactus_desert | block_column | 1: desert |
| flower_forest_flowers | simple_random_selector | 1: flower_forest |
| trees_flower_forest | random_selector | 1: flower_forest |
| flower_flower_forest | simple_block | 1: flower_forest |
| trees_birch_and_oak_leaf_litter | random_selector | 1: forest |
| trees_grove | random_selector | 1: grove |
| trees_snowy | random_selector | 2: ice_spikes, snowy_plains |
| bamboo_light | bamboo | 1: jungle |
| trees_jungle | random_selector | 1: jungle |
| seagrass_warm | weighted_random_selector | 2: lukewarm_ocean, warm_ocean |
| lush_caves_ceiling_vegetation | vegetation_patch | 1: lush_caves |
| cave_vines | block_column | 1: lush_caves |
| lush_caves_clay | random_boolean_selector | 1: lush_caves |
| lush_caves_vegetation | vegetation_patch | 1: lush_caves |
| rooted_azalea_tree | root_system | 1: lush_caves |
| spore_blossom | simple_block | 1: lush_caves |
| classic_vines_cave_feature | vines | 1: lush_caves |
| trees_mangrove | random_selector | 1: mangrove_swamp |
| patch_grass_normal | simple_block | 3: mangrove_swamp, swamp, windswept_savanna |
| patch_dead_bush | simple_block | 4: taiga 2/5, swamp 2/2 |
| patch_waterlily | simple_block | 2: mangrove_swamp, swamp |
| seagrass_swamp | weighted_random_selector | 2: mangrove_swamp, swamp |
| patch_grass_meadow | simple_block | 1: meadow |
| flower_meadow | simple_block | 1: meadow |
| trees_meadow | random_selector | 1: meadow |
| wildflowers_meadow | simple_block | 1: meadow |
| mushroom_island_vegetation | random_boolean_selector | 1: mushroom_fields |
| brown_mushroom_taiga | simple_block | 3: mushroom_fields, snowy_taiga, taiga |
| red_mushroom_taiga | simple_block | 3: mushroom_fields, snowy_taiga, taiga |
| seagrass_normal | weighted_random_selector | 1: ocean |
| birch_tall | random_selector | 1: old_growth_birch_forest |
| patch_large_fern | simple_block | 4: taiga 4/5 |
| trees_old_growth_pine_taiga | random_selector | 1: old_growth_pine_taiga |
| patch_grass_taiga | simple_block | 2: old_growth_pine_taiga, old_growth_spruce_taiga |
| brown_mushroom_old_growth | simple_block | 2: old_growth_pine_taiga, old_growth_spruce_taiga |
| red_mushroom_old_growth | simple_block | 2: old_growth_pine_taiga, old_growth_spruce_taiga |
| patch_berry_common | simple_block | 3: old_growth_pine_taiga, old_growth_spruce_taiga, taiga |
| trees_old_growth_spruce_taiga | random_selector | 1: old_growth_spruce_taiga |
| pale_garden_vegetation | random_selector | 1: pale_garden |
| pale_moss_patch | vegetation_patch | 1: pale_garden |
| pale_garden_flowers | simple_block | 1: pale_garden |
| flower_pale_garden | simple_block | 1: pale_garden |
| seagrass_river | weighted_random_selector | 1: river |
| patch_tall_grass | simple_block | 2: savanna, savanna_plateau |
| trees_savanna | random_selector | 2: savanna, savanna_plateau |
| patch_grass_savanna | simple_block | 2: savanna, savanna_plateau |
| trees_taiga | random_selector | 2: snowy_taiga, taiga |
| patch_grass_taiga_2 | simple_block | 2: snowy_taiga, taiga |
| patch_berry_rare | simple_block | 1: snowy_taiga |
| trees_sparse_jungle | random_selector | 1: sparse_jungle |
| patch_melon_sparse | simple_block | 1: sparse_jungle |
| patch_sunflower | simple_block | 1: sunflower_plains |
| trees_swamp | tree | 1: swamp |
| flower_swamp | simple_block | 1: swamp |
| brown_mushroom_swamp | simple_block | 1: swamp |
| red_mushroom_swamp | simple_block | 1: swamp |
| patch_sugar_cane_swamp | block_column | 1: swamp |
| patch_firefly_bush_swamp | simple_block | 1: swamp |
| patch_firefly_bush_near_water_swamp | simple_block | 1: swamp |
| warm_ocean_vegetation | simple_random_selector | 1: warm_ocean |
| sea_pickle | simple_block | 1: warm_ocean |
| trees_windswept_forest | random_selector | 1: windswept_forest |
| trees_windswept_hills | random_selector | 2: windswept_gravelly_hills, windswept_hills |
| trees_windswept_savanna | random_selector | 1: windswept_savanna |
| trees_badlands | random_selector | 1: wooded_badlands |

**10. top_layer_modification** (1 placed features)

| Placed feature | Feature type | Biomes |
|---|---|---|
| freeze_top_layer | freeze_top_layer | all 56 |

### 1.4 Ores (step 6 and infested stone in step 7)

Count `xN` is attempts per column, `1/N` a rarity filter, Y values are absolute (the world spans -64 to 319; a trapezoid may extend past it, so only its middle is used). Targets: `stone_ore_replaceables` gives the ore, `deepslate_ore_replaceables` its deepslate twin; the rock and dirt blobs replace `base_stone_overworld`. "Discard" is `discard_chance_on_air_exposure`.

| Placed feature | Placement | Size | Discard | Places |
|---|---|---|---|---|
| ore_dirt | x7; uniform 0..160 | 33 | 0 | dirt |
| ore_gravel | x14; uniform -64..319 | 33 | 0 | gravel |
| ore_granite_upper | 1/6; uniform 64..128 | 64 | 0 | granite |
| ore_granite_lower | x2; uniform 0..60 | 64 | 0 | granite |
| ore_diorite_upper | 1/6; uniform 64..128 | 64 | 0 | diorite |
| ore_diorite_lower | x2; uniform 0..60 | 64 | 0 | diorite |
| ore_andesite_upper | 1/6; uniform 64..128 | 64 | 0 | andesite |
| ore_andesite_lower | x2; uniform 0..60 | 64 | 0 | andesite |
| ore_tuff | x2; uniform -64..0 | 64 | 0 | tuff |
| ore_coal_upper | x30; uniform 136..319 | 17 | 0 | coal_ore / deepslate_coal_ore |
| ore_coal_lower | x20; trapezoid 0..192 | 17 | 0.5 | coal_ore / deepslate_coal_ore |
| ore_iron_upper | x90; trapezoid 80..384 | 9 | 0 | iron_ore / deepslate_iron_ore |
| ore_iron_middle | x10; trapezoid -24..56 | 9 | 0 | iron_ore / deepslate_iron_ore |
| ore_iron_small | x10; uniform -64..72 | 4 | 0 | iron_ore / deepslate_iron_ore |
| ore_gold | x4; trapezoid -64..32 | 9 | 0.5 | gold_ore / deepslate_gold_ore |
| ore_gold_lower | x0-1; uniform -64..-48 | 9 | 0.5 | gold_ore / deepslate_gold_ore |
| ore_redstone | x4; uniform -64..15 | 8 | 0 | redstone_ore / deepslate_redstone_ore |
| ore_redstone_lower | x8; trapezoid -96..-32 | 8 | 0 | redstone_ore / deepslate_redstone_ore |
| ore_diamond | x7; trapezoid -144..16 | 4 | 0.5 | diamond_ore / deepslate_diamond_ore |
| ore_diamond_medium | x2; uniform -64..-4 | 8 | 0.5 | diamond_ore / deepslate_diamond_ore |
| ore_diamond_large | 1/9; trapezoid -144..16 | 12 | 0.7 | diamond_ore / deepslate_diamond_ore |
| ore_diamond_buried | x4; trapezoid -144..16 | 8 | 1 | diamond_ore / deepslate_diamond_ore |
| ore_lapis | x2; trapezoid -32..32 | 7 | 0 | lapis_ore / deepslate_lapis_ore |
| ore_lapis_buried | x4; uniform -64..64 | 7 | 1 | lapis_ore / deepslate_lapis_ore |
| ore_copper | x16; trapezoid -16..112 | 10 | 0 | copper_ore / deepslate_copper_ore |
| underwater_magma | x44-52; uniform -64..256; surface_relative_threshold_filter | underwater_magma | - | magma_block (observed) |
| ore_gold_extra | x50; uniform 32..256 | 9 | 0 | gold_ore / deepslate_gold_ore |
| disk_sand | x3; heightmap; block_predicate_filter | disk r 2-6, half height 2 | - | sand / sandstone |
| disk_clay | heightmap; block_predicate_filter | disk r 2-3, half height 1 | - | clay |
| disk_gravel | heightmap; block_predicate_filter | disk r 2-5, half height 2 | - | gravel |
| ore_emerald | x100; trapezoid -16..480 | 3 | 0 | emerald_ore / deepslate_emerald_ore |
| ore_copper_large | x16; trapezoid -16..112 | 20 | 0 | copper_ore / deepslate_copper_ore |
| ore_clay | x46; uniform -64..256 | 33 | 0 | clay |
| disk_grass | x1; heightmap; offset; block_predicate_filter | disk r 2-6, half height 2 | - | dirt / grass_block |
| ore_infested | x14; uniform -64..63 | 9 | 0 | infested_stone / infested_deepslate |

Against the prompt's table: everything listed there matches. The prompt omits `ore_diamond_medium` (x2, -64..-4, size 8), `ore_diamond_large` (1 in 9 columns, size 12, discard 0.7), `ore_diamond_buried` (x4, discard 1.0), `ore_gold_lower` (0-1, -64..-48), `ore_iron_small` (x10, size 4), `ore_infested` (step 7, the emerald biomes), `ore_copper_large` (dripstone caves, size 20) and `ore_clay` (lush caves, x46). Emerald appears in 10 biomes: the 7 mountain biomes plus meadow, cherry grove and grove. Large ore veins (copper with granite and raw copper blocks; iron with tuff and raw iron blocks) come from the noise router. Phase 5 reads them, and Jesus's answer 4 turns them on in phase 6 behind a switch. `raw_iron_block`, `raw_copper_block`, granite and tuff are already registered.

### 1.5 Trees

36 tree configurations are reachable, with 10 trunk placers, 12 foliage placers, 1 root placer (mangrove) and 11 decorators. Taking every random term at its maximum, trunks reach 31 blocks (mega jungle) and 29 (mega pine and spruce); fancy oaks reach 14. Five `fallen_tree` configurations, `huge_brown_mushroom`, `huge_red_mushroom`, `bamboo`, `vines` and `root_system` (azalea, sulfur) round out the big vegetation.

| Tree | Trunk / foliage (+ roots) | Max trunk | Decorators |
|---|---|---|---|
| azalea_tree | bending_trunk_placer/random_spread_foliage_placer | 6 |  |
| cherry_bees_005 | cherry_trunk_placer/cherry_foliage_placer | 8 | beehive |
| pale_oak_creaking | dark_oak_trunk_placer/dark_oak_foliage_placer | 9 | creaking_heart,pale_moss |
| pale_oak | dark_oak_trunk_placer/dark_oak_foliage_placer | 9 | pale_moss |
| dark_oak_leaf_litter | dark_oak_trunk_placer/dark_oak_foliage_placer | 9 | place_on_ground |
| fancy_oak | fancy_trunk_placer/fancy_foliage_placer | 14 |  |
| fancy_oak_bees | fancy_trunk_placer/fancy_foliage_placer | 14 | beehive |
| fancy_oak_bees_002 | fancy_trunk_placer/fancy_foliage_placer | 14 | beehive |
| fancy_oak_bees_005 | fancy_trunk_placer/fancy_foliage_placer | 14 | beehive |
| fancy_oak_bees_0002_leaf_litter | fancy_trunk_placer/fancy_foliage_placer | 14 | beehive,place_on_ground |
| fancy_oak_leaf_litter | fancy_trunk_placer/fancy_foliage_placer | 14 | place_on_ground |
| acacia | forking_trunk_placer/acacia_foliage_placer | 9 |  |
| mega_pine | giant_trunk_placer/mega_pine_foliage_placer | 29 | alter_ground |
| mega_spruce | giant_trunk_placer/mega_pine_foliage_placer | 29 | alter_ground |
| mega_jungle_tree | mega_jungle_trunk_placer/jungle_foliage_placer | 31 | leave_vine,trunk_vine |
| orange_poplar_leaf_litter | poplar_trunk_placer/poplar_foliage_placer | 11 | place_on_ground,shelf_mushroom |
| red_poplar_leaf_litter | poplar_trunk_placer/poplar_foliage_placer | 11 | place_on_ground,shelf_mushroom |
| yellow_poplar_leaf_litter | poplar_trunk_placer/poplar_foliage_placer | 11 | place_on_ground,shelf_mushroom |
| jungle_tree | straight_trunk_placer/blob_foliage_placer | 12 | cocoa,leave_vine,trunk_vine |
| super_birch_bees | straight_trunk_placer/blob_foliage_placer | 13 | beehive |
| super_birch_bees_0002 | straight_trunk_placer/blob_foliage_placer | 13 | beehive |
| oak | straight_trunk_placer/blob_foliage_placer | 6 |  |
| oak_bees_002 | straight_trunk_placer/blob_foliage_placer | 6 | beehive |
| oak_bees_005 | straight_trunk_placer/blob_foliage_placer | 6 | beehive |
| oak_bees_0002_leaf_litter | straight_trunk_placer/blob_foliage_placer | 6 | beehive,place_on_ground |
| oak_leaf_litter | straight_trunk_placer/blob_foliage_placer | 6 | place_on_ground |
| birch_bees_0002 | straight_trunk_placer/blob_foliage_placer | 7 | beehive |
| birch_bees_002 | straight_trunk_placer/blob_foliage_placer | 7 | beehive |
| birch_bees_0002_leaf_litter | straight_trunk_placer/blob_foliage_placer | 7 | beehive,place_on_ground |
| birch_leaf_litter | straight_trunk_placer/blob_foliage_placer | 7 | place_on_ground |
| swamp_oak | straight_trunk_placer/blob_foliage_placer | 8 | leave_vine |
| jungle_bush | straight_trunk_placer/bush_foliage_placer | 1 |  |
| pine | straight_trunk_placer/pine_foliage_placer | 10 |  |
| spruce | straight_trunk_placer/spruce_foliage_placer | 8 |  |
| tall_mangrove | upwards_branching_trunk_placer/random_spread_foliage_placer+mangrove_root_placer | 14 | attached_to_leaves,beehive,leave_vine |
| mangrove | upwards_branching_trunk_placer/random_spread_foliage_placer+mangrove_root_placer | 7 | attached_to_leaves,beehive,leave_vine |

### 1.6 Carvers

All 56 biomes list `cave` (probability 0.15, count very-biased-to-bottom 0..14), `cave_extra_underground` (0.07) and `canyon` (0.01, y scale 3, thickness trapezoid 0..6). The data is in `jar-data\data\minecraft\worldgen\carver\`. In vanilla, carvers work as a pull: a column carves the parts of tunnels that start in columns up to 8 away. That is naturally order-independent, and it comes before decoration.

### 1.7 Feature types and modifiers

Counts are over the reachable set. "Biome x feature uses" is how many (biome, placed feature) pairs have that root type.

**Feature types**

| Feature type | Root of N placed features | Biome x feature uses | Nodes reachable (incl. nested) |
|---|---|---|---|
| simple_block | 52 | 329 | 60 |
| tree | 2 | 2 | 36 |
| ore | 30 | 1424 | 24 |
| random_selector | 23 | 40 | 21 |
| block_column | 9 | 54 | 9 |
| overlay | 1 | 1 | 7 |
| template | 0 | 0 | 7 |
| weighted_random_selector | 9 | 11 | 7 |
| fallen_tree | 0 | 0 | 6 |
| sequence | 2 | 2 | 6 |
| coral_claw | 0 | 0 | 5 |
| coral_tree | 0 | 0 | 5 |
| disk | 5 | 166 | 5 |
| simple_random_selector | 5 | 8 | 5 |
| speleothem | 0 | 0 | 4 |
| vegetation_patch | 3 | 3 | 4 |
| spring_feature | 3 | 114 | 3 |
| bamboo | 2 | 2 | 2 |
| fossil | 2 | 6 | 2 |
| iceberg | 2 | 4 | 2 |
| lake | 2 | 110 | 2 |
| multiface_growth | 2 | 57 | 2 |
| random_boolean_selector | 2 | 2 | 2 |
| root_system | 2 | 2 | 2 |
| speleothem_cluster | 2 | 2 | 2 |
| block_blob | 1 | 2 | 1 |
| blue_ice | 1 | 2 | 1 |
| freeze_top_layer | 1 | 56 | 1 |
| geode | 1 | 56 | 1 |
| huge_brown_mushroom | 0 | 0 | 1 |
| huge_red_mushroom | 0 | 0 | 1 |
| large_dripstone | 1 | 1 | 1 |
| monster_room | 2 | 112 | 1 |
| no_op | 0 | 0 | 1 |
| sculk_patch | 0 | 0 | 1 |
| spike | 1 | 1 | 1 |
| underwater_magma | 1 | 56 | 1 |
| vines | 2 | 4 | 1 |
| waterlogged_vegetation_patch | 0 | 0 | 1 |

**Placement modifiers** (reachable placed features, every occurrence):

count 174, biome 171, in_square 170, block_predicate_filter 154, heightmap 109, offset 105, height_range 59, rarity_filter 51, surface_water_depth_filter 26, environment_scan 20, random_chance 10, noise_threshold_count 6, cuboid 5, noise_based_count 4, surface_relative_threshold_filter 3, randomly_selected 2

**Every typed node** (modifiers, predicates, providers, placers, decorators, number providers), for scale:

trapezoid 231, count 174, biome 171, in_square 170, block_predicate_filter 154, matching_block_tag 148, uniform 144, offset 115, heightmap 109, matching_blocks 85, height_range 59, rarity_filter 51, would_survive 51, not 49, weighted 40, rule_based 39, all_of 38, two_layers_feature_size 33, weighted_list 32, matching_fluids 31, any_of 31, surface_water_depth_filter 26, solid 21, environment_scan 20, place_on_ground 20, straight_trunk_placer 16, beehive 15, blob_foliage_placer 13, random_chance 10, clamped_normal 7, fancy_foliage_placer 6, fancy_trunk_placer 6, attached_to_logs 6, noise_threshold_count 6, randomized_int 6, leave_vine 5, random_block 5, cuboid 5, noise_based_count 4, trunk_vine 4, has_sturdy_face 4, shelf_mushroom 4, rotated 4, surface_relative_threshold_filter 3, clamped 3, poplar_foliage_placer 3, poplar_trunk_placer 3, dark_oak_foliage_placer 3, three_layers_feature_size 3, dark_oak_trunk_placer 3, random_spread_foliage_placer 3, very_biased_to_bottom 2, biased_to_bottom 2, replaceable 2, randomly_selected 2, append_loot 2, attached_to_leaves 2, mangrove_root_placer 2, upwards_branching_trunk_placer 2, alter_ground 2, mega_pine_foliage_placer 2, giant_trunk_placer 2, pale_moss 2, inside_world_bounds 1, true 1, bush_foliage_placer 1, jungle_foliage_placer 1, mega_jungle_trunk_placer 1, cherry_foliage_placer 1, cherry_trunk_placer 1, spruce_foliage_placer 1, noise_threshold 1, volume_match 1, noise 1, pine_foliage_placer 1, cocoa 1, bending_trunk_placer 1, dual_noise 1, creaking_heart 1, acacia_foliage_placer 1, forking_trunk_placer 1

## 2. Blocks and shapes

### 2.1 What is supported today

- Registry (`Docs/Blocks.md`): 531 blocks (428 cubes, 101 slabs, powder snow, snow layers), plus air, still water and still lava. That is 534 blocks with air, 3,470 states and 709 texture layers.
- Shapes: `EFoundationBlockShape` (`Source/Unnamed/Foundation/FoundationBlockAssets.h`) has None, Full, SlabBottom, SlabTop, Fluid and Layers1-7. The voxel core's `EVoxelShape` has Air, Full, SlabBottom, SlabTop, Fluid and Layers. The render layers are opaque, cutout, translucent and water.
- Tint: one packed RGB per state today. Per-vertex grass, foliage and water tints arrive in phase 5 session 6.
- Heightmaps: `EVoxelHeightmap` has WorldSurface, OceanFloor, MotionBlocking and MotionBlockingNoLeaves. Placements use MOTION_BLOCKING (47), OCEAN_FLOOR (37), WORLD_SURFACE_WG (19), OCEAN_FLOOR_WG (8) and MOTION_BLOCKING_NO_LEAVES (3). The `_WG` variants mean "as generated, before features", which the snapshot design gives for free (section 3).
- `VoxelWorld.md` "Known gaps" already lists the generator's needs: grass, flowers and ferns, cactus, sugar cane, kelp, seagrass, vines, pointed dripstone.

### 2.2 Blocks decoration places, by the shape they need

148 blocks are named in the reachable feature JSON. 35 more are placed by feature *types* (fixed in code), by templates or by ore veins: vine (from `vines` and the vine decorators), cocoa, bee_nest, creaking_heart, pale_hanging_moss, shelf_mushroom, bamboo, magma_block, snow, ice, snow_block, cobblestone, spawner, chest, bone_block, sandstone_slab, sculk, the 15 coral plants and fans, mud, cinnabar and raw ore blocks *(observed; confirm by dump, section 5)*. Of the 183, 97 are supported.

| Group | Model | Blocks | States | Blocks |
|---|---|---|---|---|
| A0 cross | `cross`: two crossed planes | 33 | 120 | allium, azure_bluet, blue_orchid, brain_coral, brown_mushroom, bubble_coral, cactus_flower, cave_vines, cave_vines_plant, closed_eyeblossom, cornflower, dandelion, dead_bush, fire_coral, firefly_bush, hanging_roots, horn_coral, kelp, kelp_plant, lily_of_the_valley, orange_tulip, oxeye_daisy, pale_hanging_moss, pink_tulip, poppy, red_mushroom, red_shrub, red_tulip, short_dry_grass, sweet_berry_bush, tall_dry_grass, tube_coral, white_tulip |
| A0 cross (facing) | `cross` rotated to 6 facings | 4 | 48 | amethyst_cluster, large_amethyst_bud, medium_amethyst_bud, small_amethyst_bud |
| A1 cross, tinted | `tinted_cross` (tall grass and large fern are two cells) | 6 | 23 | bush, fern, large_fern, short_grass, sugar_cane, tall_grass |
| A2 double cross | `cross` in two cells (sunflower adds a head) | 4 | 8 | lilac, peony, rose_bush, sunflower |
| A3 seagrass template | `template_seagrass` | 2 | 3 | seagrass, tall_seagrass |
| B1 flowerbed / leaf litter | `flowerbed_1-4`, `template_leaf_litter_1-4`: flat segments x facing (multipart) | 3 | 48 | leaf_litter, pink_petals, wildflowers |
| B2 carpet | `carpet` (pale moss adds low/tall side walls, multipart) | 2 | 163 | moss_carpet, pale_moss_carpet |
| C multiface | thin planes on any face (multipart) | 3 | 288 | glow_lichen, sculk_vein, vine |
| D custom elements | own element boxes | 26 | 248 | azalea, bamboo, big_dripleaf, big_dripleaf_stem, brain_coral_fan, brain_coral_wall_fan, bubble_coral_fan, bubble_coral_wall_fan, cactus, cocoa, fire_coral_fan, fire_coral_wall_fan, flowering_azalea, horn_coral_fan, horn_coral_wall_fan, lily_pad, mangrove_propagule, mangrove_roots, pointed_dripstone, sea_pickle, shelf_mushroom, small_dripleaf, spore_blossom, sulfur_spike, tube_coral_fan, tube_coral_wall_fan |
| H block entity (defer) | block entity models | 2 | 25 | chest, spawner |
| **Total needed** | | **83** | **949** | plus chest and spawner deferred |

Already supported (97): every ore, deepslate ore and infested stone; all logs (including `poplar_log`, `pale_oak_log`) and all leaves (including the three poplar leaves, azalea and flowering azalea leaves); dirt, grass block, podzol, rooted dirt, mud, muddy mangrove roots, moss block, pale moss block, sand, sandstone, gravel, clay, calcite, smooth basalt, tuff, granite, diorite, andesite, amethyst block, budding amethyst, dripstone block, sculk, sculk catalyst, sulfur, potent sulfur, cinnabar, magma, packed ice, blue ice, ice, snow block, snow layers, pumpkin, melon, bee nest, creaking heart, the mushroom blocks and stem, coral blocks, bone block, cobblestone, mossy cobblestone, suspicious sand, sandstone slab, raw iron/copper blocks, and air, water and lava.

What the missing groups need beyond geometry:

| Need | Blocks | Notes |
|---|---|---|
| Two-sided cutout quads, no AO | all crosses, seagrass, flowerbeds, multiface | Cross models turn ambient occlusion off. 23 plant textures carry `"mipmap_strategy": "strict_cutout"` or an `alpha_cutoff_bias` in their `.mcmeta`. The import needs cutout-aware mips, or distant grass dissolves. |
| Random XZ (and some Y) offset per position | flowers, grass, ferns, bamboo, pointed dripstone, small dripleaf … | Our own position hash. The maximum offset and which blocks have one come from the facts dump. |
| Biome tint per vertex (`tintindex`) | grass-tinted crosses, sugar cane, vine (foliage), lily pad (fixed), pink petals and wildflowers stems, leaf litter (dry foliage) | Builds on phase 5 session 6's per-vertex tint. Leaf litter needs a dry-foliage colormap. |
| Water around the block | kelp, kelp plant, seagrass, tall seagrass, corals and fans (always water); sea pickle, hanging roots, amethyst, dripleaf, glow lichen, sculk vein, mangrove roots and propagule (`waterlogged`) | The water mesher must treat these as water sources. Today `waterlogged` exists but placement always sets it false. |
| Emission | glow lichen (7), amethyst buds and clusters (1-5), sea pickles (in water), cave vines with berries, firefly bush (emissive overlay `cross_emissive`, animated) | Light facts already exist; the overlay is new. |
| Animation | kelp, kelp plant, seagrass, tall seagrass (both halves), sculk vein, firefly bush overlay | 7 of the 135 new textures are animated. The texture table already animates. |
| No collision, outline only | crosses, flowers, seagrass, vine, lichen | Collision and outline boxes from facts (section 5). The cactus is inset by 1/16, carpets are 1/16 tall, the lily pad is thin, bamboo has a small offset box. |
| Two cells | tall grass, large fern, lilac, peony, rose bush, sunflower, tall seagrass, small dripleaf | `half=lower/upper`. Placement and mining must handle both halves. |
| Survival | every plant | Placement and "pop when the support goes" use the support tags (1.1). The 26 tags features reference are listed in `work\tags.txt`; 3 are missing from the ini (`air`, `corals`, `wall_corals`) because they hold no imported block. |

Deferred: chest and spawner (dungeons) need block entities and loot, and `sculk_shrieker` only appears in ancient cities.

### 2.3 Options for the block-shapes step

| Option | How | Pros | Cons |
|---|---|---|---|
| 1. Hand-coded shapes | New `EVoxelShape` values (Cross, DoubleCross, Flat, Carpet, Multiface, Cactus …), each with mesher and Playground code | Small first step; fast emit for crosses | 26 custom models (dripleaf, azalea, cocoa, bamboo, coral fans, propagule, sea pickle …) each need their own code. Phase 7's stairs, fences, doors and panes start over. |
| **2. Generic baked models (recommended)** | The block reference's tools bake each state's model (element boxes, per-face UV, texture, `tintindex`, `cullface`, `shade`, blockstate x/y rotation, uvlock) into a quad list in `DA_Blocks`. The mesher appends a model state's quads per cell (not greedy-merged) into the cutout layer, dropping faces whose `cullface` neighbour is opaque. The Playground builds one runtime mesh per baked model. Collision and outline come from dumped boxes, never from render models. | One path for all 83 blocks and for phase 7. The data already sits beside what `classify_blocks.py` reads. Shapes are exact. | A bigger first session (baker, asset format, mesher path). Crosses become 4 quads through a general path; measure before optimising. |
| 3. Hybrid | Option 2 plus a fast path for crosses | Best speed if crosses dominate | Two paths to test. Only worth it if benches show crosses are the cost. |

**Recommendation:** option 2 now, and option 3's fast path only if `voxel.Bench` shows the cross path costs more than about 0.2 ms per column in plains or jungles.

### 2.4 Terrain slabs (IDEAS.md) and decoration

- **Where the pass goes:** at Finalize, after the decoration merge and before the snow pass. It reads the pre-decoration surface heights of the 3x3 columns, which decoration already holds (section 3), and writes only into its own column. This answers the IDEAS note that "the rule needs neighbour heights across column borders" without new machinery.
- **Conflicts:** a slab replaces short replaceable plants (grass, flowers, ferns, dry grass) in its cell. It skips cells that hold trunks, cactus, sugar cane, bamboo or the lower half of a double plant, and cells whose ground decoration turned to dirt or podzol under a tree.
- **Plants on slabs:** no vanilla support tag includes a slab. Recommendation: slabs support nothing at first, which keeps the walkable steps clean.
- **Snow:** vanilla snow layers need a full top face, so snowy biomes would show bare slabs. Recommendation: no slabs where the snow pass would put snow, or a snowy slab variant later.
- **IDs and assets:** new non-vanilla blocks get project block ids (protocol 1286 and up) through `UFoundationInteractionSettings::Blocks` or a reference extension. The block-shapes step's registry work is the natural moment, but only after Jesus decides the IDs, textures, tint, drops and the grass-to-dirt rule.

## 3. Determinism, cross-column writes and meshing

### 3.1 Fixed rules, whichever option

- **Randomness:** one stream per (world seed, decoration salt, column x, column z, step, feature salt), each a fresh Xoroshiro128++ from a `Hash64` of those values. Nothing is shared across columns or features. The prompt's vanilla rule (decoration seed plus `featureIndex + 10000*step`) ties seeds to list positions, so adding one feature to the data reseeds every later feature in that step everywhere. I recommend the feature salt be a hash of the placed feature's id (Q4). The global order per step still decides write priority.
- **Order per step:** a global order per step, from a topological sort of the 56 biome lists (acyclic for all 11 steps) with ties broken by id. The order is computed once by the reader and stored with the data.
- **Which features run:** a column attempts a feature if any biome in its 3x3 neighbourhood lists it. The `biome` modifier, present on 171 top-level placements, then keeps a position only if that position's biome lists the feature. The rule needs 4x4x4 biome storage for the 3x3 columns (phase 5 session 4).
- **Reach:** origins come from `in_square` inside the column. Patch offsets reach ±10. Trees, icebergs, geodes, lakes (a 16x8x16 box from the origin -8) and fossils stay inside the 3x3 by the reference's own contract, which our code must enforce: a write outside the 3x3 is dropped and counted, the way vanilla logs far writes.
- **Reads:** a feature reads the 3x3 *pre-decoration* snapshot (after noise, surface, carvers and ore veins), plus its own column's writes so far in this decoration (options A and C differ only here). It never reads a neighbour's decoration output. That is what makes the result independent of timing.
- **Heightmaps during decoration:** the `_WG` heightmaps are the snapshot's. MOTION_BLOCKING and OCEAN_FLOOR read the snapshot updated by the column's own writes. Finalize recomputes all four from the final blocks.
- **Last step:** `top_layer_modification` (`freeze_top_layer`: ice on cold water, snow on cold tops) runs at Finalize on the merged column. Snow then also lands on leaves a neighbour's tree placed, which fixes a vanilla border artifact (bare leaves where a later chunk's tree overhangs). Phase 5 session 5's freeze pass moves there.
- **Meshing gate:** a column meshes once it and its 8 neighbours are final. Edits replay after Finalize, as today.
- **Ring arithmetic:** mesh radius R needs final columns to R+1, decorated to R+2 and generated (pre-decoration) to R+3. Today generation reaches R+1. At `ViewRadius` 12 that is about π·15² ≈ 707 generated columns instead of π·13² ≈ 531 (+33%). The first view needs 7x7 generated columns instead of 3x3; measure it against today's 3.3 to 3.6 s.
- **Memory:** a column's snapshot must live until all 8 neighbours have decorated. Finalize needs every neighbour decorated, so the snapshot can turn into the final column in place. Write logs live from Decorate to the target's Finalize.

### 3.2 Options

| | A. Push with write logs (recommended) | B. Pull: recompute the neighbourhood | C. Stateless snapshot features |
|---|---|---|---|
| How | Decorate(C), once its 3x3 is generated, runs every step and feature and produces one write log, each write tagged with its target column, (step, order index, sequence) and a write mode. Finalize(C), once its 3x3 is decorated, starts from C's snapshot and replays all writes aimed at C from the 9 logs, sorted by (step, order index, source offset dx,dz, sequence). | Finalize(C) reruns the decoration of all 9 columns in its 3x3 itself and keeps only the writes that land in C. No logs and no decorated stage. | As A, but features read only the snapshot, even in their own column. All writes are conditional and resolved in the merge. |
| Pros | Each column decorates once. Close to vanilla in-column interplay (vegetation sees earlier trees). Deterministic by construction. Logs are small (see below). This is the prompt's job graph. | Simplest state: Finalize is a pure function of the 5x5 generated columns. Trivially order-independent. | Simplest to reason about. Features within a column could run in parallel. |
| Cons | Two new stages and bookkeeping (logs held until the targets finalize, cancellation). The own-column live view and the replay can disagree. Deterministic either way, but the replay is the truth, so Decorate's own-column reads are only a hint. | Nine times the decoration work, trees and geodes included; too slow at runtime. | Loses feature-to-feature interplay. Flowers go under canopies and grass into trunk cells unless merge conditions catch them. More vanilla-unlike. |
| Use | Runtime | **The test oracle:** decorate a 3x3 alone and compare it with the streamed result | Not recommended |

**Write modes** (applied at replay against the merged state): `IfReplaceable(tag)` for trees (`replaceable_by_trees`), plants (`air`, or `replaceable`) and ores (their rule-test target); `IfPredicate` for anything a filter checked; and `Force` for carving lakes and geodes. Conditional replay keeps two overlapping trees from cutting each other's logs. In vanilla the second tree would usually refuse to grow. We accept rare overlaps as a known, deterministic difference. Size: ore and rock blobs dominate (about 30 ore placements with 10 to 60 blocks each), so expect about 5k to 15k writes per column, 8 to 12 bytes each, held briefly. Measure in S3.

**Leaves' `distance`** is computed by the tree from its own logs before it writes. It is exact unless trees overlap, which is acceptable. Persistent leaves stay false.

### 3.3 Tests that pin this

- `Decoration.LoadOrder`: hash a 9x9 area generated in 8 shuffled orders and with 1 and 30 workers. Every hash must be equal.
- `Decoration.Oracle`: option B on a 3x3 in isolation equals the streamed column, block for block.
- `Decoration.Reach`: no write is dropped for leaving the 3x3, across 1,000 columns of each preset.
- `Decoration.Golden`: block hashes per seed and preset, re-recorded only on a generator decision.
- `Carvers.LoadOrder`, the same check for carving.

## 4. Proposed sessions and open questions

Phase 6 depends on phase 5's biome storage (S4), surface rules and freeze pass (S5), and per-vertex tints (S6).

### 4.1 Block-shapes step (two sessions before phase 6)

| Session | Scope | Tests | Acceptance captures |
|---|---|---|---|
| **P1. Shapes: data and the Playground** | Extend `Reference/minecraft-blocks-26.3` through its own tools with the 83 blocks: blockstates, models, the 135 textures (cutout mip strategy), states and ids; extend `FactsDump.java` with per-state collision and outline boxes, fluid state, offset kind and maximum, and all 300 block tags. Bake models to quads in the reference tools. Reimport `DA_Blocks` (move the old assets deliberately). Add a `Model` shape plus a baked-model index in the registry. The Playground draws model states through `FoundationBlockMeshes` (one runtime mesh per baked model). Items: vanilla rule (Q7). | `Blocks.Registry` (617 blocks, about 4,420 states), `.Codec`, `.Faces`, `.Textures`, `.Shapes` (every new state's boxes against facts), new `.Models` (quad counts and UVs against the reference) | Tour: the registry and state galleries with every new block; a plant row in the showcase (crosses, double plants, flowerbeds, vine faces, dripleaf, coral fans) |
| **P2. Shapes: voxel mesher and interaction** | `EVoxelShape::Model` with quads, cullface culling, the AO flag, per-vertex tint by `tintindex`, the position-hash offset, the emissive overlay, waterlogged water, and Lumen cards skipped for small cutout models if they cost. Interaction: outline targeting, collision boxes in `GatherCollision`, placement with the support tags, double plants, attached faces (cocoa, vine, lichen), pop-when-unsupported after mining (Q6), instant mining from facts. | `Voxel.Mesher.Models` (culling against neighbours, quad counts, tint), `Blocks.Placement` truth tables for plants, `Blocks.Survival` (cascade), an interaction-suite scenario (flower on grass, mine the grass, the flower pops) | Hand-placed plant garden in `L_Generated` and the Playground; a section-border case; bench of a plant-dense column |

### 4.2 Phase 6 proper

| Session | Scope | Tests | Acceptance captures |
|---|---|---|---|
| **S3. Framework, pipeline and ores** | The worldgen reference gains placed features, features, providers, carvers and the referenced tags (section 5). The reader turns the JSON into core data. Core `Decoration/`: the 16 modifiers, 14 predicates, providers (simple, weighted, rule_based, noise, noise_threshold, dual_noise, randomized_int, rotated), int, float and height providers, and the features simple_block, block_column, ore, scattered_ore, disk, the four selectors, sequence, no_op and multiface_growth. Pipeline: Generate, Decorate, Finalize, Mesh, with the rings and cancellation from 3.1; freeze moves to Finalize; edits replay after it. The ore-vein switch comes on. Most grass, flower, cactus, sugar cane, kelp and lichen patches work from here. | `Decoration.LoadOrder`, `.Oracle`, `.Reach`, `.Golden`; `Decoration.OreHistogram` (blocks per Y per ore with caves and discards off, against the configured height provider; a chi-square bound); `Providers.Distribution` against dumped histograms; `Modifiers`/`Predicates` units | Ore histogram figure (CSV plus PNG, our world and vanilla region stats side by side); regeneration in a different order gives identical hashes; `voxel.Bench Decorate`; first view and flight |
| **S4. Trees and big vegetation** | `tree` with 10 trunk placers, 12 foliage placers, the mangrove root placer, 2- and 3-layer sizes and 11 decorators; `fallen_tree`, huge mushrooms, bamboo, vines, `root_system` (azalea), `block_blob` (forest rock), cocoa and bee nests. | `Trees.Shapes` (each of the 36 configs: height range, log connectivity, leaves distance at most 7), `Trees.Borders` (a tree straddling a corner is whole in every load order), `Trees.Golden` | Trees whole across column borders: forest, taiga (mega spruce), jungle (mega jungle with vines and cocoa), dark forest, mangrove, cherry, dappled forest; a column-grid overlay shot |
| **S5. Carvers and underground** | Carvers (cave, extra cave, canyon) as a pull pass after the surface, using aquifers and the lava level; lava lakes (surface and underground); springs (switch, Q3); geodes; large dripstone, speleothem and clusters (dripstone, sulfur); sculk patch and vein; lush caves (`vegetation_patch`, cave vines, spore blossom, dripleaf, clay); sulfur caves (rooted spring, pool); underwater magma. Dungeons, wells and fossils per Q2. | `Carvers.LoadOrder`, `.Golden`; `Geode.Shape`; `Lake.Contained` (no fluid leaks into air at borders) | `CaptureVoxel -Caves`: a canyon, a worm cave, a geode, lush, dripstone, deep dark and sulfur caves, a lava lake |
| **S6. Water, cold, polish, performance and the record** | Seagrass, kelp, coral tree and claw, sea pickles, icebergs, blue ice, ice spikes and ice patches; snow on leaves at Finalize; (optional) terrain slabs (Q5). Benches against the 5 ms column limit and the flight's 2 ms frame budget; Lumen cards for plants; Docs (`VoxelWorld.md`, `Blocks.md`) and the phase 6 record. | `Freeze.OnLeaves`; `Slabs.Rule` if in scope; all goldens re-recorded once | Warm reef, frozen ocean with icebergs, ice spikes, snowy taiga with snow on leaves, plus every capture above in one montage; flight and benches |

### 4.3 Open questions for Jesus

1. **Block shapes approach.** Options: (a) generic baked models; (b) hand-coded shapes; (c) generic plus a cross fast path. **Recommend (a)**, adding (c) only if benches demand it, because it also serves phase 7's stairs, fences and doors.
2. **Templates and block entities: dungeons (chest, spawner, loot), desert wells, fossils.** Options: (a) defer to phase 7 with the template loader; (b) now; (c) never. **Recommend (a).** They need a structure-template reader and block entities, which phase 7 builds anyway, and they are rare.
3. **Springs while water does not flow.** Options: (a) data supported, switch off by default; (b) a single static source in the wall; (c) a precomputed static fall to the floor. **Recommend (a)** until flowing fluids exist. (b) looks like a bug, and (c) is throwaway work.
4. **Feature seeds.** Options: (a) a stable hash of the placed feature id; (b) vanilla's `featureIndex + 10000*step`. Also, should flower and noise-count patterns use the world seed (vanilla fixes seed 2345)? **Recommend (a) and the world seed.** Data edits then stay local, and flower fields differ per world. We do not need seed parity.
5. **Terrain slabs.** Options: (a) add in S6 as a Finalize pass behind a switch, with IDs and assets decided first; (b) a separate session after phase 6; (c) park again. **Recommend (b).** The pass is ready by design (2.4), but its five decisions (IDs, textures, tint, drops, grass to dirt) should not slow phase 6.
6. **Plant interaction depth in P2.** Options: (a) survival-checked placement plus pop-when-unsupported; (b) place anywhere, nothing pops. **Recommend (a).** A floating flower after mining the grass under it reads as a bug. A one-level cascade is enough (a block above, double-plant halves, attached faces).
7. **Items for decoration blocks whose vanilla item has another id** (sweet berry bush, cave vines, cocoa, kelp plant, tall seagrass). Options: (a) keep the `Blocks.md` rule: no block item, as with powder snow; (b) add the vanilla items now. **Recommend (a).** Creative players get the rest; items come with an inventory pass.
8. **The bigger generation ring** (R+3: about +33% generated columns at radius 12; the first view needs 7x7 generated columns). Options: (a) accept and measure; (b) lower `ViewRadius`; (c) later, a cheaper "decorated but not meshed" outer ring. **Recommend (a)** with a budget check in S3. Columns cost about 0.25 ms of worker time across 30 workers.

## 5. Reference and tooling additions (through the references' own tools)

**Worldgen reference** (`Reference/minecraft-worldgen-26.3`, `tools/build_reference.py` then `verify_reference.py`):

- Add the reachable sets only, using the same reachability filter idea as today's `is_overworld` biome tag:
  - `worldgen/placed_feature` (210 reachable of 273);
  - `worldgen/feature` (182 of 240);
  - `worldgen/block_state_provider` (8);
  - `worldgen/carver` (`cave`, `cave_extra_underground`, `canyon`);
  - the 26 block tags features reference, plus the support tags and the tags those include.
- Templates (`data/minecraft/structure/desert_well/*`, `fossil/*`) only if Q2 says now.
- Verifier: every placed feature leads to a feature; every nested placed feature, provider and tag resolves; every file is reachable from a biome; every block id is in the blocks reference.
- `facts/feature_order.json`: our per-step order (the topological sort), with vanilla's own sorted step lists alongside for comparison (dumped, below).

**Blocks reference** (`Reference/minecraft-blocks-26.3`, `tools/build_manifest.py`, `classify_blocks.py`, `build_blocks.py`, `FactsDump.java`):

- The 83 blocks:
  - blockstates;
  - models, including parents such as `cross`, `tinted_cross`, `flowerbed_*`, `template_*`, `carpet`, `coral_fan`, `pointed_dripstone`;
  - the 135 textures, with their `.mcmeta` mip strategy;
  - the classification extended past cube, slab and layers (`work\shape_groups.txt` is a first cut).
- A model baker: per state, quads with UV, texture layer, tint index, cullface, shade and rotation. This is data generated from data.
- All 300 block tags over the full registry.

**Black-box facts worth dumping** (run the jar, record numbers; check `Get-Process UnrealEditor-Cmd` before each heavy run):

| Fact | How | Used by |
|---|---|---|
| Per state: collision boxes, outline boxes, fluid state (fluid, amount), offset kind and sampled maximum offset, `replaceable`, occluding faces | `FactsDump`: after `WorldLoader`, query each state against an empty getter at the origin (public API only) | P1 `Blocks.Shapes`, P2 collision and targeting |
| Distributions of int, float and height providers (uniform, trapezoid, clamped, clamped_normal, weighted_list, biased and very biased to bottom) | `WorldgenFacts`: build each from its codec, take 10⁶ samples, write histograms (the spline-dump seam already works) | S3 `Providers.Distribution` |
| Vanilla's per-step feature order | the server's biome source after `WorldLoader` | sanity check for our order |
| Vanilla world statistics | Run the 26.3 dedicated server headless on 2 or 3 fixed seeds and pre-generate about 32x32 chunks. A Python NBT reader then reads the region files and writes ore blocks by Y per ore, trees and plants per chunk per biome, and blocks written across chunk borders. | S3 ore figure (shape comparison, not seed parity), S4 and S6 density targets. This is the heaviest run: a few minutes of CPU, off-hours. |
| Single-feature exemplars | Same server, a void world, the `place feature` command at fixed origins for each of the 36 tree configs, the huge mushrooms, icebergs, geodes and spikes, then read the touched blocks | S4 visual references; confirms the type-fixed blocks in 2.2 |
| Survival matrix (optional) | `canSurvive` per plant over a small support matrix through a minimal reader | spot-check of the support-tag rules |

**Our own tooling:** `voxel.Bench Decorate` (decorate and finalize ms per column; log sizes), `voxel.OreHistogram <radius>` (CSV), `CaptureVoxel -Trees` (a shot finder for trees on a column border, like `FindCaveShots`), and a column-grid overlay cvar for border shots.

## 6. Rough size

Rough guesses from the scope above, to compare sessions with each other. They are not commitments.

| Session | New or changed files | New tests | Effort |
|---|---|---|---|
| P1 Shapes data + Playground | about 4 Python/Java tools, the regenerated reference, 3 or 4 registry and asset C++ files, `FoundationBlockMeshes`, `Blocks.md` | 1 new, 4 updated | L: the asset reimport and baker are the risk; one long session |
| P2 Shapes in the mesher + interaction | the mesher, block table, chunk component layers, `FoundationBlockPlacement`, authority survival hook, collision gather | 3 or 4 | M to L |
| S3 Framework, pipeline, ores | the reader (2 or 3 files), about 10 to 14 core files (`Decoration/`: context, log, modifiers, predicates, providers, features, order), the pipeline, settings, console, bench | 8 or 9 | L: the largest, since the pipeline change is the risky part; consider splitting into S3a (pipeline and oracle with one trivial feature) and S3b (modifiers, providers, ores, patches) |
| S4 Trees | about 6 to 8 core files (trunk, foliage, root placers, decorators, tree, fallen tree, huge mushroom, bamboo) | 3 | M to L |
| S5 Carvers + underground | about 8 to 10 core files | 4 | L |
| S6 Water, cold, polish, record | about 4 to 6 core files, captures, docs | 2 or 3 | M |

Rough totals: about 40 to 55 new or changed source files, about 20 new tests, and 6 to 7 sessions, or 7 to 8 if S3 splits. P1 and S3 carry most of the risk. Benchmarks decide whether the cross fast path (2.3) and a cheaper outer ring (Q8) are needed.

## Appendix: files in this sandbox

| Path | What |
|---|---|
| `jar-data\data\minecraft\worldgen\{placed_feature,feature,block_state_provider,carver,biome}\`, `jar-data\data\minecraft\tags\{block,worldgen}\` | Extract from `server-26.3.jar` (1,015 files), data only |
| `scripts\extract.py`, `analyze.py`, `report.py`, `classify_shapes.py`, `tally.py`, `ores.py`, `trees.py`, `tags.py`, `order.py`, `reach.py`, `textures.py` | The scripts behind every table (run with `uv run --no-project --python 3.13 python -I`) |
| `work\inventory.json` | Per placed feature: steps, biomes, root type, modifiers, nested types, every block reference with its key path |
| `work\blocks.json`, `work\shapes.json`, `work\shape_groups.txt` | Blocks placed and tested, model facts per block, the shape groups |
| `work\ores.md`, `work\trees.txt`, `work\tags.txt`, `work\groups.md`, `work\inventory.md` | Raw tables |

What I could not confirm without running Mojang's code: the blocks placed by feature types (2.2, "observed"), the exact reach of the large features, and every effort estimate.
