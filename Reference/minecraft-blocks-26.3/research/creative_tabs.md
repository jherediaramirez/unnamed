# Java creative inventory: tabs and block ordering

Source: https://minecraft.wiki/w/Creative_inventory; tags from misode/mcmeta 26.3-data.

## Tabs (11)
Building Blocks, Colored Blocks, Natural Blocks, Functional Blocks, Redstone Blocks, Tools & Utilities, Combat, Food & Drinks, Ingredients, Spawn Eggs, Operator Utilities (cheats and an option required). Blocks are grouped by material.

## Building Blocks order
Roughly by material family:
1. Wood: logs, stripped logs, wood, planks, then wooden stairs, slabs, fences, gates, doors, trapdoors, plates, buttons.
2. Stone: stairs, slabs, walls of all stone types first, then stone, cobblestone, smooth stone, stone bricks.
3. Granite, diorite, andesite (base then polished); deepslate (cobbled, chiseled, polished, bricks, tiles); tuff; bricks; mud; resin bricks; sandstone; cinnabar; sulfur; prismarine; Nether; end stone; purpur; mineral blocks.
4. Copper blocks last.

A base block is usually followed by its variants (polished, cut, chiseled, bricks). "All types" entries bundle stairs, slabs and walls.

## Families in data
Families are mostly block tags under `data/minecraft/tags/block/`: `logs.json`, `planks.json`, `stairs.json`, `slabs.json`, `ores.json` (new in 26.3), `mineable/pickaxe.json`, and mining-level tags `needs_stone_tool.json`, `needs_iron_tool.json`, `needs_diamond_tool.json`. Tags nest through `#` references.

In Java source, a set such as a wood or stone family is a `BlockFamily`: a base block plus its stairs, slab, wall and button. Data generators use it for recipes and stonecutter rules. I did not verify 26.3 class names (unconfirmed).
