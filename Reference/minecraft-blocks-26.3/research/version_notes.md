# Java Edition 26.1 to 26.3: block and world-generation changes

Sources: minecraft.wiki version pages (26.1, 26.2, 26.3); block-ID diffs from misode/mcmeta per-version summaries.

## Block IDs added
- **26.1, Tiny Takeover (24 Mar 2026):** +2: `golden_dandelion`, `potted_golden_dandelion`. Stonecutter cuts stone and deepslate directly into variants; note blocks on copper play a trumpet; `#dirt` split into `#mud`, `#moss_blocks`, `#grass_blocks`. Wiki says it is the first Java release shipped fully unobfuscated.
- **26.2, Chaos Cubed (16 Jun 2026):** +28: cinnabar and sulfur families (base, polished, bricks, chiseled, with slab/stairs/wall), `potent_sulfur`, `sulfur_spike`.
- **26.3, Wilderness Bound (15 Sep 2026):** +90: poplar wood set with red, orange and yellow poplar leaves; `red_shrub`; `shelf_mushroom`; `straw_bed`; 16 concrete and 16 wool stair/slab sets.
- Total 1166 (1.21.11) to 1286 (26.3); no IDs removed.

## World generation
- 26.2: `sulfur_caves` underground biome (sulfur cubes, cave spiders); sulfur springs; potent-sulfur geysers over magma or lava; `pointed_dripstone` renamed `speleothem`, `dripstone_cluster` renamed `speleothem_cluster`.
- 26.3: `dappled_forest` biome beside cold biomes, with poplar trees; abandoned camp structure in 18 biomes; no cave air in Nether caves; new `ores` block tag.
- Every `ore_*` placed feature has identical height ranges in 1.21.11 and 26.3 data.
- No `raw_gold_block` appears in any ore vein (only copper and iron), so raw gold blocks do not generate naturally.
- 26.3 mob change: Endermen and Shulkers can no longer teleport onto bedrock.

## Relevant 1.21.x
- 1.21 Tricky Trials (Jun 2024): trial chambers, tuff and copper variants, trial spawners.
- 1.21.4 The Garden Awakens (Dec 2024): pale garden, pale oak wood, pale moss, creaking heart, resin blocks.

## Unconfirmed
- A third-party blog says 26.3 changed cave, biome-transition and structure-spacing generation. Neither the wiki nor the data confirms this.
