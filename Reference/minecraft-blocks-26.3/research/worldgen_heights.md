# Worldgen terrain features - Minecraft Java 26.3 (vanilla data)

Source: `data/minecraft/worldgen/feature/*.json` (configured features; this version's folder is `feature/`, not `configured_feature/`), `placed_feature/*.json`, `biome/*.json`, from the 26.3 client jar. Biome dimension membership from the generator's `biome_parameters` reports.

Y conversion: overworld bottom -64 / top 320; nether and end 0 / 256. `above_bottom: N` = bottom+N, `below_top: N` = top-N. Heightmap placements have no fixed Y and are listed as surface.

Note: `top` here is the exclusive end of the height range (min_y + height). The highest placeable block Y is 319 in the overworld and 255 in the nether and end, so a `below_top: 0` bound maps to 319/255 in-game.

Columns: count = `count` placement modifier (tries per chunk); chance = `rarity_filter` 1-in-N per chunk. Vein size is the configured `size` (ore/scattered_ore). Discard = `discard_chance_on_air_exposure`.

Items marked `code-defined` have no block/target data in the JSON; values are from the feature type and are not checked against source code.

## Overworld

| Placed feature | Configured type | Block(s) placed | Replaces (rule test target) | Vein size | Air-exposure discard | Count / chance per chunk | Height (Y) | Biomes using it |
|---|---|---|---|---|---|---|---|---|
| `forest_rock` | block_blob | mossy_cobblestone | surface on #forest_rock_can_place_on | — | — | 2/chunk | surface (heightmap MOTION_BLOCKING) | overworld: old_growth_pine_taiga, old_growth_spruce_taiga |
| `pile_hay` | block_pile | hay_block (axis y) | air (surface) | 1 block | — | 1 try | no Y modifier (surface-relative) | not in any biome (structure/feature-only) |
| `pile_ice` | block_pile | blue_ice (1) / packed_ice (5) block pile | air (surface) | 1 block high | — | 1 try | no Y modifier (surface-relative) | not in any biome (structure/feature-only) |
| `pile_snow` | block_pile | snow (layers=1) | air (surface) | 1 layer | — | 1 try | no Y modifier (surface-relative) | not in any biome (structure/feature-only) |
| `blue_ice` | blue_ice | blue_ice | code-defined (no JSON targets) | — | — | 0..19/chunk (uniform) | uniform 30..61 | overworld: deep_frozen_ocean, frozen_ocean |
| `disk_clay` | disk | clay | — | radius 2..3, half-height 1 | — | 1 try | surface (heightmap OCEAN_FLOOR_WG) | all overworld biomes (56) |
| `disk_grass` | disk | dirt, grass_block | — | radius 2..6, half-height 2 | — | 1/chunk | surface (heightmap OCEAN_FLOOR_WG) | overworld: mangrove_swamp |
| `disk_gravel` | disk | gravel | — | radius 2..5, half-height 2 | — | 1 try | surface (heightmap OCEAN_FLOOR_WG) | most overworld biomes (54/56) |
| `disk_sand` | disk | sand, sandstone | — | radius 2..6, half-height 2 | — | 3/chunk | surface (heightmap OCEAN_FLOOR_WG) | most overworld biomes (54/56) |
| `ice_patch` | disk | packed_ice | — | radius 2..3, half-height 1 | — | 2/chunk | surface (heightmap MOTION_BLOCKING) | overworld: ice_spikes |
| `fossil_lower` | fossil | bone_block (template skeleton); coal/diamond overlay | stone-type (template processors) | template | — | chance 1/64 | uniform -64..-8 | overworld: desert, mangrove_swamp, swamp |
| `fossil_upper` | fossil | bone_block (template skeleton); coal overlay | stone-type (template processors) | template | — | chance 1/64 | uniform 0..320 | overworld: desert, mangrove_swamp, swamp |
| `freeze_top_layer` | freeze_top_layer | snow layer, ice over water | surface (code-defined) | — | — | 1 try | no Y modifier (surface-relative) | all overworld biomes (56) |
| `amethyst_geode` | geode | amethyst_block, budding_amethyst, calcite, smooth_basalt (outer), amethyst buds/clusters, air core | not #features_cannot_replace (geode shell) | outer wall radius 4..6 | — | chance 1/24 | uniform -58..30 | all overworld biomes (56) |
| `iceberg_blue` | iceberg | blue_ice | code-defined (placed on water, no JSON targets) | — | — | chance 1/200 | no Y modifier (surface-relative) | overworld: deep_frozen_ocean, frozen_ocean |
| `iceberg_packed` | iceberg | packed_ice | code-defined (placed on water, no JSON targets) | — | — | chance 1/16 | no Y modifier (surface-relative) | overworld: deep_frozen_ocean, frozen_ocean |
| `lake_lava_surface` | lake | lava (fluid), stone (barrier) | air/fluid; not #features_cannot_replace | lake basin | — | chance 1/200 | surface (heightmap WORLD_SURFACE_WG) | most overworld biomes (55/56) |
| `lake_lava_underground` | lake | lava (fluid), stone (barrier) | air/fluid; not #features_cannot_replace | lake basin | — | chance 1/9 | uniform 0..320 | most overworld biomes (55/56) |
| `large_dripstone` | large_dripstone | dripstone_block, pointed_dripstone | #dripstone_replaceable_blocks | column radius 3..16 | — | 10..48/chunk (uniform) | uniform -64..256 | overworld: dripstone_caves |
| `ore_andesite_lower` | ore | andesite | #base_stone_overworld (andesite, deepslate, diorite, granite, stone, tuff) | 64 | 0 | 2/chunk | uniform 0..60 | all overworld biomes (56) |
| `ore_andesite_upper` | ore | andesite | #base_stone_overworld (andesite, deepslate, diorite, granite, stone, tuff) | 64 | 0 | chance 1/6 | uniform 64..128 | all overworld biomes (56) |
| `ore_clay` | ore | clay | #base_stone_overworld (andesite, deepslate, diorite, granite, stone, tuff) | 33 | 0 | 46/chunk | uniform -64..256 | overworld: lush_caves |
| `ore_coal_lower` | ore | coal_ore, deepslate_coal_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 17 | 0.5 | 20/chunk | trapezoid 0..192, peak ~Y=96 (plateau 0) | all overworld biomes (56) |
| `ore_coal_upper` | ore | coal_ore, deepslate_coal_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 17 | 0 | 30/chunk | uniform 136..320 | all overworld biomes (56) |
| `ore_copper` | ore | copper_ore, deepslate_copper_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 10 | 0 | 16/chunk | trapezoid -16..112, peak ~Y=48 (plateau 0) | most overworld biomes (55/56) |
| `ore_copper_large` | ore | copper_ore, deepslate_copper_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 20 | 0 | 16/chunk | trapezoid -16..112, peak ~Y=48 (plateau 0) | overworld: dripstone_caves |
| `ore_diamond` | ore | diamond_ore, deepslate_diamond_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 4 | 0.5 | 7/chunk | trapezoid -144..16, peak ~Y=-64 (plateau 0) | all overworld biomes (56) |
| `ore_diamond_buried` | ore | diamond_ore, deepslate_diamond_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 8 | 1 | 4/chunk | trapezoid -144..16, peak ~Y=-64 (plateau 0) | all overworld biomes (56) |
| `ore_diamond_large` | ore | diamond_ore, deepslate_diamond_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 12 | 0.7 | chance 1/9 | trapezoid -144..16, peak ~Y=-64 (plateau 0) | all overworld biomes (56) |
| `ore_diamond_medium` | ore | diamond_ore, deepslate_diamond_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 8 | 0.5 | 2/chunk | uniform -64..-4 | all overworld biomes (56) |
| `ore_diorite_lower` | ore | diorite | #base_stone_overworld (andesite, deepslate, diorite, granite, stone, tuff) | 64 | 0 | 2/chunk | uniform 0..60 | all overworld biomes (56) |
| `ore_diorite_upper` | ore | diorite | #base_stone_overworld (andesite, deepslate, diorite, granite, stone, tuff) | 64 | 0 | chance 1/6 | uniform 64..128 | all overworld biomes (56) |
| `ore_dirt` | ore | dirt | #base_stone_overworld (andesite, deepslate, diorite, granite, stone, tuff) | 33 | 0 | 7/chunk | uniform 0..160 | all overworld biomes (56) |
| `ore_emerald` | ore | emerald_ore, deepslate_emerald_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 3 | 0 | 100/chunk | trapezoid -16..480, peak ~Y=232 (plateau 0) | 10/56 overworld biomes (e.g. cherry_grove, frozen_peaks, grove) |
| `ore_gold` | ore | gold_ore, deepslate_gold_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 9 | 0.5 | 4/chunk | trapezoid -64..32, peak ~Y=-16 (plateau 0) | all overworld biomes (56) |
| `ore_gold_extra` | ore | gold_ore, deepslate_gold_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 9 | 0 | 50/chunk | uniform 32..256 | overworld: badlands, eroded_badlands, wooded_badlands |
| `ore_gold_lower` | ore | gold_ore, deepslate_gold_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 9 | 0.5 | 0..1/chunk (uniform) | uniform -64..-48 | all overworld biomes (56) |
| `ore_granite_lower` | ore | granite | #base_stone_overworld (andesite, deepslate, diorite, granite, stone, tuff) | 64 | 0 | 2/chunk | uniform 0..60 | all overworld biomes (56) |
| `ore_granite_upper` | ore | granite | #base_stone_overworld (andesite, deepslate, diorite, granite, stone, tuff) | 64 | 0 | chance 1/6 | uniform 64..128 | all overworld biomes (56) |
| `ore_gravel` | ore | gravel | #base_stone_overworld (andesite, deepslate, diorite, granite, stone, tuff) | 33 | 0 | 14/chunk | uniform -64..320 | all overworld biomes (56) |
| `ore_infested` | ore | infested_stone, infested_deepslate | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 9 | 0 | 14/chunk | uniform -64..63 | 10/56 overworld biomes (e.g. cherry_grove, frozen_peaks, grove) |
| `ore_iron_middle` | ore | iron_ore, deepslate_iron_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 9 | 0 | 10/chunk | trapezoid -24..56, peak ~Y=16 (plateau 0) | all overworld biomes (56) |
| `ore_iron_small` | ore | iron_ore, deepslate_iron_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 4 | 0 | 10/chunk | uniform -64..72 | all overworld biomes (56) |
| `ore_iron_upper` | ore | iron_ore, deepslate_iron_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 9 | 0 | 90/chunk | trapezoid 80..384, peak ~Y=232 (plateau 0) | all overworld biomes (56) |
| `ore_lapis` | ore | lapis_ore, deepslate_lapis_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 7 | 0 | 2/chunk | trapezoid -32..32, peak ~Y=0 (plateau 0) | all overworld biomes (56) |
| `ore_lapis_buried` | ore | lapis_ore, deepslate_lapis_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 7 | 1 | 4/chunk | uniform -64..64 | all overworld biomes (56) |
| `ore_redstone` | ore | redstone_ore, deepslate_redstone_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 8 | 0 | 4/chunk | uniform -64..15 | all overworld biomes (56) |
| `ore_redstone_lower` | ore | redstone_ore, deepslate_redstone_ore | #height_specific_ore_replaceables (tuff); #stone_ore_replaceables (andesite, diorite, granite, stone); #deepslate_ore_replaceables (deepslate) (height_specific tags only inside their Y gate) | 8 | 0 | 8/chunk | trapezoid -96..-32, peak ~Y=-64 (plateau 0) | all overworld biomes (56) |
| `ore_tuff` | ore | tuff | #base_stone_overworld (andesite, deepslate, diorite, granite, stone, tuff) | 64 | 0 | 2/chunk | uniform -64..0 | all overworld biomes (56) |
| `lush_caves_clay` | random_boolean_selector | clay (floor); dripleaf plants on top (not terrain) | #lush_ground_replaceable | xz radius 4..7, depth 3 (waterlogged variant: vertical range 5) | — | 62/chunk | uniform -64..256 | overworld: lush_caves |
| `mushroom_island_vegetation` | random_boolean_selector | huge_red_mushroom or huge_brown_mushroom (50/50) | #huge_*_mushroom_can_place_on | foliage radius 3 | — | 1 try | surface (heightmap MOTION_BLOCKING) | overworld: mushroom_fields |
| `sculk_patch_ancient_city` | sequence | sculk, sculk_catalyst (50%) | code-defined sculk spread | charges 10, 32/charge, 64 spread attempts | — | 1 try | no Y modifier (surface-relative) | not in any biome (structure/feature-only) |
| `sculk_patch_deep_dark` | sequence | sculk, sculk_catalyst (50%) | code-defined sculk spread | charges 10, 32/charge, 64 spread attempts | — | 256/chunk | uniform -64..256 | overworld: deep_dark |
| `sulfur_pool` | sequence | water (fluid), sulfur (barrier) | air/fluid; not #features_cannot_replace | lake basin | — | 256/chunk | uniform -64..256 [ceiling-attached] | overworld: sulfur_caves |
| `dripstone_cluster` | speleothem_cluster | dripstone_block (base), pointed_dripstone | #dripstone_replaceable_blocks | radius 2..8, height 3..6 | — | 48..96/chunk (uniform) | uniform -64..256 | overworld: dripstone_caves |
| `sulfur_spike_cluster` | speleothem_cluster | sulfur (base), sulfur_spike | #sulfur_spike_replaceable_blocks | radius 2..8, height 1..4 | — | 48..96/chunk (uniform) | uniform -64..256 | overworld: sulfur_caves |
| `ice_spike` | spike | packed_ice | #ice_spike_replaceable (placed on snow_block) | spike | — | 3/chunk | surface (heightmap MOTION_BLOCKING) | overworld: ice_spikes |
| `underwater_magma` | underwater_magma | magma_block | seafloor (code-defined, search range 5, p=0.5) | — | — | 44..52/chunk (uniform) | uniform -64..256 | all overworld biomes (56) |
| `lush_caves_ceiling_vegetation` | vegetation_patch | moss_block (ceiling), cave_vine | #moss_replaceable | xz radius 4..7, depth 1..2 | — | 125/chunk | uniform -64..256 [ceiling-attached] | overworld: lush_caves |
| `lush_caves_vegetation` | vegetation_patch | moss_block (floor), moss_carpet | #moss_replaceable | xz radius 4..7, depth 1 | — | 125/chunk | uniform -64..256 | overworld: lush_caves |
| `pale_moss_patch` | vegetation_patch | pale_moss_block (floor), pale moss carpet | #moss_replaceable | xz radius 2..4, depth 1 | — | 1/chunk | surface (heightmap MOTION_BLOCKING_NO_LEAVES) | overworld: pale_garden |

## Nether

| Placed feature | Configured type | Block(s) placed | Replaces (rule test target) | Vein size | Air-exposure discard | Count / chance per chunk | Height (Y) | Biomes using it |
|---|---|---|---|---|---|---|---|---|
| `delta` | delta_feature | lava (core), magma_block (rim) | code-defined (nether base blocks) | size 3..7, rim 0..2 | — | 40/layer | every layer 0..256 (no height modifier) | nether: basalt_deltas |
| `crimson_fungi` | huge_fungus | nether_wart_block (hat), shroomlight (decor), crimson stem | replaceable_blocks list (saplings/plants) | huge fungus | — | 8/layer | every layer 0..256 (no height modifier) | nether: crimson_forest |
| `warped_fungi` | huge_fungus | warped_wart_block (hat), shroomlight (decor), warped stem | replaceable_blocks list (saplings/plants) | huge fungus | — | 8/layer | every layer 0..256 (no height modifier) | nether: warped_forest |
| `basalt_blobs` | netherrack_replace_blobs | basalt | netherrack | radius 3..7 | — | 75/chunk | uniform 0..256 | nether: basalt_deltas |
| `blackstone_blobs` | netherrack_replace_blobs | blackstone | netherrack | radius 3..7 | — | 25/chunk | uniform 0..256 | nether: basalt_deltas |
| `ore_blackstone` | ore | blackstone | netherrack | 33 | 0 | 2/chunk | uniform 5..31 | most nether biomes (4/5) |
| `ore_gold_deltas` | ore | nether_gold_ore | netherrack | 10 | 0 | 20/chunk | uniform 10..246 | nether: basalt_deltas |
| `ore_gold_nether` | ore | nether_gold_ore | netherrack | 10 | 0 | 10/chunk | uniform 10..246 | most nether biomes (4/5) |
| `ore_gravel_nether` | ore | gravel | netherrack | 33 | 0 | 2/chunk | uniform 5..41 | most nether biomes (4/5) |
| `ore_magma` | ore | magma_block | netherrack | 33 | 0 | 4/chunk | uniform 27..36 | all nether biomes (5) |
| `ore_quartz_deltas` | ore | nether_quartz_ore | netherrack | 14 | 0 | 32/chunk | uniform 10..246 | nether: basalt_deltas |
| `ore_quartz_nether` | ore | nether_quartz_ore | netherrack | 14 | 0 | 16/chunk | uniform 10..246 | most nether biomes (4/5) |
| `ore_soul_sand` | ore | soul_sand | netherrack | 12 | 0 | 12/chunk | uniform 0..31 | nether: soul_sand_valley |
| `basalt_pillar` | overlay | basalt (column) | air | single-block pillars | — | 10/chunk | uniform 0..256 | nether: soul_sand_valley |
| `glowstone` | random_neighbor_spread | glowstone | air on ceilings (neighbour spread) | attempts 1500, xz +-7, y -11..0 | — | 10/chunk | uniform 0..256 | all nether biomes (5) |
| `glowstone_extra` | random_neighbor_spread | glowstone | air on ceilings (neighbour spread) | neighbour spread | — | 0..9/chunk (biased low) | uniform 4..252 | all nether biomes (5) |
| `ore_ancient_debris_large` | scattered_ore | ancient_debris | #base_stone_nether (basalt, blackstone, netherrack) | 3 | 1 | 1 try | trapezoid 8..24, peak ~Y=16 (plateau 0) | all nether biomes (5) |
| `ore_debris_small` | scattered_ore | ancient_debris | #base_stone_nether (basalt, blackstone, netherrack) | 2 | 1 | 1 try | uniform 8..248 | all nether biomes (5) |
| `large_basalt_columns` | weighted_random_selector | basalt (stepped columns) | air or lava (above sea level) | 50 columns, height 5..10 | — | 2/layer | every layer 0..256 (no height modifier) | nether: basalt_deltas |
| `small_basalt_columns` | weighted_random_selector | basalt (stepped columns) | air or lava (above sea level) | small clusters | — | 4/layer | every layer 0..256 (no height modifier) | nether: basalt_deltas |

## End

| Placed feature | Configured type | Block(s) placed | Replaces (rule test target) | Vein size | Air-exposure discard | Count / chance per chunk | Height (Y) | Biomes using it |
|---|---|---|---|---|---|---|---|---|
| `end_island_decorated` | end_island | end_stone (code-defined island) | air (code-defined) | — | — | weighted 1/2/chunk x chance 1/14 | uniform 55..70 | end: small_end_islands |
| `end_platform` | end_platform | obsidian (fixed platform) | air (code-defined) | — | — | 1 try | fixed [100, 49, 0] | end: the_end |
| `end_spike` | end_spike | obsidian (pillars; spikes list empty in JSON, generated at runtime) | air (code-defined) | — | — | 1 try | no Y modifier (surface-relative) | end: the_end |

## Not in the table

- Trees, fallen trees, bamboo, flowers, grass/fern/patches, mushrooms (small), melons/pumpkins, cactus, sugar cane, kelp, seagrass, vines, berry bushes, sea pickles, chorus, multiface growth, root systems and bonus/structure features (monster room, end gateway/podium, desert well).
- Fluid-only springs (`spring_water`, `spring_lava`, `spring_closed`, `spring_closed_double`, `spring_open`, `spring_delta`, `spring_lava_frozen`): they place water/lava source blocks into rock; not terrain blocks.
- Dark forest vegetation (`dark_forest_vegetation`) mixes trees with huge brown/red mushrooms; the mushrooms are listed only via `mushroom_island_vegetation`.

## Dimension types (`data/minecraft/dimension_type`)

In 26.x the dimension-type JSON has no `ultrawarm` key; that behaviour is an environment attribute (`minecraft:gameplay/water_evaporates`, `minecraft:gameplay/fast_lava`).

| Dimension type | min_y | height | logical_height | coordinate_scale | has_skylight | has_ceiling | water_evaporates attr |
|---|---|---|---|---|---|---|---|
| overworld | -64 | 384 | 384 | 1.0 | True | False | None |
| overworld_caves | -64 | 384 | 384 | 1.0 | True | True | None |
| the_end | 0 | 256 | 256 | 1.0 | True | False | None |
| the_nether | 0 | 256 | 128 | 8.0 | False | True | True |

