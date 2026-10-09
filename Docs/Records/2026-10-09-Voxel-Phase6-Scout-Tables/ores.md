| ore_dirt | x7; uniform 0..160 | 33 | 0.0 | base_stone_overworld>dirt |
| ore_gravel | x14; uniform bot+0..top-0 | 33 | 0.0 | base_stone_overworld>gravel |
| ore_granite_upper | 1/6; uniform 64..128 | 64 | 0.0 | base_stone_overworld>granite |
| ore_granite_lower | x2; uniform 0..60 | 64 | 0.0 | base_stone_overworld>granite |
| ore_diorite_upper | 1/6; uniform 64..128 | 64 | 0.0 | base_stone_overworld>diorite |
| ore_diorite_lower | x2; uniform 0..60 | 64 | 0.0 | base_stone_overworld>diorite |
| ore_andesite_upper | 1/6; uniform 64..128 | 64 | 0.0 | base_stone_overworld>andesite |
| ore_andesite_lower | x2; uniform 0..60 | 64 | 0.0 | base_stone_overworld>andesite |
| ore_tuff | x2; uniform bot+0..0 | 64 | 0.0 | base_stone_overworld>tuff |
| ore_coal_upper | x30; uniform 136..top-0 | 17 | 0.0 | ?>coal_ore, ?>deepslate_coal_ore |
| ore_coal_lower | x20; trapezoid 0..192 | 17 | 0.5 | ?>coal_ore, ?>deepslate_coal_ore |
| ore_iron_upper | x90; trapezoid 80..384 | 9 | 0.0 | ?>iron_ore, ?>deepslate_iron_ore |
| ore_iron_middle | x10; trapezoid -24..56 | 9 | 0.0 | ?>iron_ore, ?>deepslate_iron_ore |
| ore_iron_small | x10; uniform bot+0..72 | 4 | 0.0 | ?>iron_ore, ?>deepslate_iron_ore |
| ore_gold | x4; trapezoid -64..32 | 9 | 0.5 | ?>gold_ore, ?>deepslate_gold_ore |
| ore_gold_lower | x{type: uniform, max_inclusive: 1, min_inclusive: 0}; uniform -64..-48 | 9 | 0.5 | ?>gold_ore, ?>deepslate_gold_ore |
| ore_redstone | x4; uniform bot+0..15 | 8 | 0.0 | ?>redstone_ore, ?>deepslate_redstone_ore |
| ore_redstone_lower | x8; trapezoid bot+-32..bot+32 | 8 | 0.0 | ?>redstone_ore, ?>deepslate_redstone_ore |
| ore_diamond | x7; trapezoid bot+-80..bot+80 | 4 | 0.5 | ?>diamond_ore, ?>deepslate_diamond_ore |
| ore_diamond_medium | x2; uniform -64..-4 | 8 | 0.5 | ?>diamond_ore, ?>deepslate_diamond_ore |
| ore_diamond_large | 1/9; trapezoid bot+-80..bot+80 | 12 | 0.7 | ?>diamond_ore, ?>deepslate_diamond_ore |
| ore_diamond_buried | x4; trapezoid bot+-80..bot+80 | 8 | 1.0 | ?>diamond_ore, ?>deepslate_diamond_ore |
| ore_lapis | x2; trapezoid -32..32 | 7 | 0.0 | ?>lapis_ore, ?>deepslate_lapis_ore |
| ore_lapis_buried | x4; uniform bot+0..64 | 7 | 1.0 | ?>lapis_ore, ?>deepslate_lapis_ore |
| ore_copper | x16; trapezoid -16..112 | 10 | 0.0 | ?>copper_ore, ?>deepslate_copper_ore |
| underwater_magma | x{type: uniform, max_inclusive: 52, min_inclusive: 44}; uniform bot+0..256; surface_relative_threshold_filter | underwater_magma | - | {"floor_search_range": 5, "placement_probability_per_valid_position": 0.5, "placement_radius_around_floor": 1} |
| ore_gold_extra | x50; uniform 32..256 | 9 | 0.0 | ?>gold_ore, ?>deepslate_gold_ore |
| disk_sand | x3; heightmap; block_predicate_filter | disk | - | {"half_height": 2, "radius": {"type": "minecraft:uniform", "max_inclusive": 6, "min_inclusive": 2}, "state_provider": {" |
| disk_clay | heightmap; block_predicate_filter | disk | - | {"half_height": 1, "radius": {"type": "minecraft:uniform", "max_inclusive": 3, "min_inclusive": 2}, "state_provider": {" |
| disk_gravel | heightmap; block_predicate_filter | disk | - | {"half_height": 2, "radius": {"type": "minecraft:uniform", "max_inclusive": 5, "min_inclusive": 2}, "state_provider": {" |
| ore_emerald | x100; trapezoid -16..480 | 3 | 0.0 | ?>emerald_ore, ?>deepslate_emerald_ore |
| ore_copper_large | x16; trapezoid -16..112 | 20 | 0.0 | ?>copper_ore, ?>deepslate_copper_ore |
| ore_clay | x46; uniform bot+0..256 | 33 | 0.0 | base_stone_overworld>clay |
| disk_grass | x1; heightmap; offset; block_predicate_filter | disk | - | {"half_height": 2, "radius": {"type": "minecraft:uniform", "max_inclusive": 6, "min_inclusive": 2}, "state_provider": {" |
| ore_infested | x14; uniform bot+0..63 | 9 | 0.0 | ?>infested_stone, ?>infested_deepslate |
