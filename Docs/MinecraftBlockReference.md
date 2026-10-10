# Minecraft 26.3 block reference: full cubes, slabs, and water

As of 2026-10-08. Source of truth: vanilla Java **26.3** (`%APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar`, plus the official server jar SHA-1 `33680f5f…05f4c` for Mojang's data-generator reports). This sheet is the input for the block/texture import and the research base for the chunk loader. Only water gets behavior; every other block here just needs the correct ID, textures and render flags.

**In scope:** 616 blocks (429 full cubes, powder snow among them, + 101 slabs + snow layers + 85 model blocks, the torch and wall torch among them since torch session T2) out of 1286 registered blocks, plus water and lava; powder snow and snow layers were added in phase 5 for the world generator. A block counts as a full cube when every model it can show is made only of `[0,0,0]→[16,16,16]` boxes. Stairs, fences, chests, tools and items are excluded. Render layers: opaque 490, cutout 104, translucent 20.

**Model blocks:** 83 blocks are drawn from their own baked models rather than as cubes or slabs: plants, flowers, crosses, seagrass, flowerbeds, carpets, multiface and custom models. See `Reference/minecraft-blocks-26.3/README.md` ("Baked models", "Shapes and offsets") for details.

## Files

The research outputs are preserved, hash-listed, in [`Reference/minecraft-blocks-26.3/`](../Reference/minecraft-blocks-26.3/README.md). That folder adds the merged import manifest (hardness, tints, names, items), every state's faces, the texture layer plan and the creative tabs. Paths below are relative to it. The research generators are in its `tools/` (`build_manifest.py` regenerates this doc from a research work folder).

| File | What it is |
|---|---|
| `block_manifest.json` | **Import input.** One entry per block in registry order. Each entry has: string id, protocol id, state-id range, properties, per-face textures, render layer, tint/animation/rotation flags, tags, and where it is found. Water is listed under `fluids`. The reference version adds hardness, tints, names and material layers; the research original is `research\block_manifest.research.json`. |
| `assets\minecraft\textures\block\*.png` (+ `.png.mcmeta`) | 690 block textures copied byte-for-byte from the jar, including water still/flow/overlay. |
| `assets\minecraft\textures\colormap\grass.png`, `foliage.png` | Biome tint lookup maps for tinted blocks. |
| `research\blocks_registry.csv` | All 1,286 blocks with protocol id, default/min/max state id, and properties (from `blocks.json`). |
| `research\block_tags.csv` | Every block's tags, with nested `#tag` references resolved. |
| `research\worldgen_heights.md` | Every terrain feature's block, vein size and Y distribution (embedded below as Appendix A). |
| `research\water_research.md` | Full water notes with bytecode evidence markers. |
| `research\edge_cases.txt` | Cube-like blocks that were rejected, and why. |

Vanilla textures are Mojang's copyrighted assets. They are fine for local development builds but must be replaced before anything is distributed.

---

## 1. IDs: what Mojang does, and what we copy

Minecraft uses **two kinds of ID**, for two different jobs:

| Layer | ID | Example | Stable? |
|---|---|---|---|
| Saves (chunk NBT) | **String id** + state properties | `minecraft:oak_slab` with `{type:"bottom", waterlogged:"false"}` | Yes. This is what makes old worlds load. |
| Runtime, memory and network | **Protocol id** (one per block, `0..1285`) | `stone` = 1, `water` = 37 | **No.** It is rebuilt at startup from registration order. |
| Runtime, memory and network | **Block-state id** (one per property combination, `0..35722`) | `oak_slab` = 15177–15182 (3 types × waterlogged) | **No.** It shifts whenever a block is added. |

Proof that the numbers move: 26.3 registered `poplar_planks` at protocol id **23**, right after the other planks. That shifted every block after it, compared with 26.2.

**What we copy:**
- Runtime numeric ids are taken from 26.3, so our numbers match Mojang's for this version.
- Chunk sections store small indices into a **per-section palette** of state ids.
- Save files write that palette as **string ids + properties**, never raw numbers.

The existing project convention (`dummy_block`, with a separate `dummy_block_item`) already matches the string-id form. Use the same ids without the `minecraft:` prefix.

**State ids for this sheet:**
- Most cubes have exactly **one** state.
- Logs, wood and basalt-likes have 3 (`axis=x|y|z`).
- Furnace-likes use `facing` (+ `lit`).
- **Slabs have 6:** `type=top|bottom|double` × `waterlogged`. A double slab is a state of the *slab* block, not the base block.
- Water has 16 (`level=0..15`).

## 2. How Minecraft sorts and organizes blocks

1. **Registry order = protocol id.** Blocks are registered in hand-written family order in `net.minecraft.world.level.block.Blocks`. The order is `air`, then stone and its variants, then grass/dirt, cobblestone, planks, saplings, bedrock, water, lava, sand, and so on. New blocks are inserted next to their family rather than appended. The table in §5 is in this order. (The `registries.json` report lists blocks alphabetically; the real order is the `protocol_id` field.)
2. **Block states.** Each block declares properties. Every combination gets a consecutive state id, starting at the block's `min_state_id`. 26.3 has 35,723 states in total, so the network's global palette needs 16 bits per entry.
3. **Tags** are the real "categories" the game logic uses: which tool mines a block, what ores may replace, what counts as dirt, and so on. They are data files in `data/minecraft/tags/block/`. Coverage among the blocks in this sheet:

| Tag | Blocks in this sheet |
|---|---:|
| `#minecraft:mineable/pickaxe` | 310 |
| `#minecraft:mineable/axe` | 104 |
| `#minecraft:mineable/shovel` | 34 |
| `#minecraft:mineable/hoe` | 29 |
| `#minecraft:needs_stone_tool` | 59 |
| `#minecraft:needs_iron_tool` | 12 |
| `#minecraft:needs_diamond_tool` | 5 |
| `#minecraft:base_stone_overworld` | 6 |
| `#minecraft:base_stone_nether` | 3 |
| `#minecraft:stone_ore_replaceables` | 4 |
| `#minecraft:deepslate_ore_replaceables` | 1 |
| `#minecraft:dirt` | 3 |
| `#minecraft:sand` | 3 |
| `#minecraft:logs` | 48 |
| `#minecraft:planks` | 13 |
| `#minecraft:leaves` | 14 |
| `#minecraft:slabs` | 101 |
| `#minecraft:wooden_slabs` | 13 |
| `#minecraft:wool` | 16 |
| `#minecraft:terracotta` | 17 |
| `#minecraft:ice` | 4 |
| `#minecraft:infiniburn_overworld` | 2 |
| `#minecraft:dragon_immune` | 13 |
| `#minecraft:wither_immune` | 9 |

4. **Creative tabs** are a separate, hand-ordered presentation list (`CreativeModeTabs`). Tabs: Building Blocks, Colored Blocks, Natural Blocks, Functional Blocks, Redstone Blocks, Tools & Utilities, Combat, Food & Drinks, Ingredients, Spawn Eggs, Operator Utilities.
   - Nearly every block in this sheet lives in **Building**, **Colored** or **Natural**.
   - Colored holds wool, terracotta, concrete, glass and their 26.3 slabs/stairs.
   - Natural holds terrain: stone, ores, dirt, logs, leaves, nether/end terrain.
   - Building Blocks is grouped by **material family**. Each family runs base block → stairs → slab → wall/fence → button/plate, then its variants (polished, bricks, cut, chiseled…). The order is woods first, then stone families, then mineral storage blocks, then copper last. The 26.2 sulfur and cinnabar families are placed among the stone families (per the wiki; unverified in code).
5. **Block families** in code: `BlockFamily` (a base block plus its stair, slab, wall, button…) drives Mojang's data generators for recipes, stonecutting and models. The 26.3 class names are not verified.

## 3. Using the table (import notes)

- **Textures** are the default state's faces, given as file names in `assets\minecraft\textures\block\`. `ends:` / `side:` means a column block (top and bottom share a texture).
- **Axis blocks** (`axis` flag: logs, wood, basalt, bone, hay, purpur pillar, quartz pillar…): the table shows `axis=y`. For `x`/`z`, rotate the cube so the `ends` texture faces east-west or north-south.
- **Facing blocks** (`facing`/`lit`: furnace, smoker, blast furnace, dispenser, observer, carved pumpkin…): the front texture is on the `facing` side. The manifest lists every state's textures in `all_textures` (e.g. `furnace_front_on`).
- **`rand-rot`:** the blockstate lists rotated variants (stone, dirt, sand, grass top…). The game picks one with a hash of the block position, so the same spot always looks the same. This is optional polish.
- **`tint`:** the texture is grayscale and gets multiplied by a color.
  - `grass_block` tints its top face and the `grass_block_side_overlay` texture drawn over the dirt side. Its color comes from `colormap\grass.png` indexed by biome temperature/downfall.
  - Leaves use `colormap\foliage.png`. Birch and spruce use fixed colors.
  - Without biomes, use the plains color. Verify the constant colors against `BlockColors` in 26.3 before relying on them.
- **`anim`:** the PNG is a vertical strip of frames. Its `.png.mcmeta` gives `frametime` (ticks per frame, default 1) and an optional frame order. 16 blocks are animated (prismarine, sea lantern, magma, sculk, crimson/warped stems, lit furnace fronts…).
- **Layer:** `opaque` / `cutout` (alpha 0 or 255: grass_block, mangrove_propagule, mangrove_roots, oak_leaves, spruce_leaves, birch_leaves, jungle_leaves, acacia_leaves, cherry_leaves, dark_oak_leaves, pale_oak_leaves, mangrove_leaves, red_poplar_leaves, orange_poplar_leaves, yellow_poplar_leaves, azalea_leaves, flowering_azalea_leaves, short_grass, fern, dead_bush, bush, red_shrub, short_dry_grass, tall_dry_grass, seagrass, tall_seagrass, dandelion, poppy, blue_orchid, allium, azure_bluet, red_tulip, orange_tulip, white_tulip, pink_tulip, oxeye_daisy, cornflower, lily_of_the_valley, brown_mushroom, red_mushroom, cactus, cactus_flower, sugar_cane, vine, glow_lichen, lily_pad, cocoa, sunflower, lilac, rose_bush, peony, tall_grass, large_fern, kelp, kelp_plant, tube_coral, brain_coral, bubble_coral, fire_coral, horn_coral, tube_coral_fan, brain_coral_fan, bubble_coral_fan, fire_coral_fan, horn_coral_fan, tube_coral_wall_fan, brain_coral_wall_fan, bubble_coral_wall_fan, fire_coral_wall_fan, horn_coral_wall_fan, sea_pickle, bamboo, sweet_berry_bush, amethyst_cluster, large_amethyst_bud, medium_amethyst_bud, small_amethyst_bud, sculk_vein, copper_grate, exposed_copper_grate, weathered_copper_grate, oxidized_copper_grate, waxed_copper_grate, waxed_exposed_copper_grate, waxed_weathered_copper_grate, waxed_oxidized_copper_grate, pointed_dripstone, sulfur_spike, cave_vines, cave_vines_plant, spore_blossom, azalea, flowering_azalea, pink_petals, wildflowers, leaf_litter, big_dripleaf, big_dripleaf_stem, small_dripleaf, hanging_roots, pale_moss_carpet, pale_hanging_moss, closed_eyeblossom, firefly_bush) / `translucent` (partial alpha or forced: glass, ice, frosted_ice, tinted_glass, 16 stained glasses). In 26.x glass models declare `force_translucent`, so all glass is translucent. Non-opaque blocks must not cull the faces of their neighbors. Exception: `grass_block` is listed as cutout only because of its side overlay; it is a solid occluding block. Leaves render opaque on "fast" graphics.
- **Slabs** use their base block's textures, half height (bottom: Y 0–8, top: Y 8–16). `smooth_stone_slab` is the one with its own side texture.
- **Where found:** blank (—) means the block does not generate as terrain. It is crafted, or appears only as building material in some structures. Ore Y-ranges come from the 26.3 `placed_feature` data, not the wiki.

## 4. Water

Code names are Mojang's real names: the 26.x jar ships unobfuscated. **[B]** = verified in 26.3 bytecode. **[W]** = wiki. **[old]** = known pre-26 behavior, not re-verified.

### 4.1 Source vs flowing: data model

- Water is **one block** (`water`, protocol id 37) with `level=0..15` → **16 block states (ids 89–104)**. The fluid view is derived from the block state. There is no separate fluid layer [B].

| Block `level` | Fluid | `amount` | Falling | Surface height (`amount/9`) |
|---|---|---|---|---|
| 0 | **source** (`Fluids.WATER`) | 8 | no | 0.889 |
| 1–7 | flowing (`Fluids.FLOWING_WATER`) | 8 − level (7…1) | no | 0.778 … 0.111 |
| 8–15 | flowing | 8 | **yes** | 0.889 (1.0 if water is above) |

- Inverse mapping: `level = source ? 0 : (8 − amount) + (falling ? 8 : 0)` [B].
- **Waterlogged** blocks (`waterlogged=true`, e.g. slabs) report a **source** fluid state, so a waterlogged slab behaves exactly like a source occupying that cell [B]. `bubble_column` also reports a water source [B].
- For the renderer: a fluid's top height is `amount/9`. A source tops out at 8/9 of a block unless water is directly above, in which case the cell renders full height [B]. Corner heights are averaged with the neighboring fluid cells (`LiquidBlockRenderer`) [old].

### 4.2 Flow rules (`FlowingFluid` / `WaterFluid`)

Water constants [B]: **tick delay 5**, **slope-find distance 4**, **drop-off 1** per block. Flat-ground reach is **7 blocks**. Downward flow is unlimited.

Per scheduled tick (`FlowingFluid.tick`):
1. A non-source cell recomputes itself with `getNewLiquid`:
   - Take the max amount among the 4 horizontal same-fluid neighbors, and count the adjacent sources.
   - **Infinite source:** ≥ 2 adjacent sources AND (solid below OR source below) AND gamerule `water_source_conversion` → becomes a source. (There is *no* "source above + one beside" rule in 26.3 code, despite the wiki [B].)
   - Water directly above → falling, amount 8.
   - Else `max − 1`. 0 → empty (air).
   - If the state changed, set it and schedule another tick in 5.
2. Every cell (sources too) then **spreads**:
   - Down first if possible. A non-source that can fall stops there.
   - Otherwise spread sideways at `amount − 1`. Falling water restarts at 7.
   - Only the directions with the **shortest path to a drop**, searched up to 4 blocks, receive water. The cost is 1000 when no drop is found, so ties split evenly.
3. **Walls:** a full collision block stops flow. Partial shapes use face occlusion.
   - Water replaces blocks in tag `washed_away_by_fluids` (flowers, torches, redstone, crops…) and drops their items.
   - `LiquidBlockContainer` blocks (slabs, stairs…) take water as `waterlogged`.
4. **Lava interaction:** lava source + water → obsidian; flowing lava + water → cobblestone; lava flowing onto water → stone; soul soil + blue ice → basalt [W]. The block references are confirmed in code; the exact branches are not traced.
5. **Scheduling:** `onPlace` / `neighborChanged` schedule a fluid tick 5 ticks out. Ticks live in `ServerLevel.fluidTicks` and run **only in block-ticking chunks**. Water never random-ticks (only lava does) [B].
6. **Nether:** water evaporates when placed, through dimension attribute `minecraft:gameplay/water_evaporates`. This is not a tick behavior [B].

### 4.3 How water affects the player

| Mechanic | 26.3 behavior |
|---|---|
| Detection | `Entity.updateFluidInteraction()` → `EntityFluidInteraction` keeps per-fluid `height` and `eyesInside` [B]. Older code: the box is shrunk by 0.001; fluid height = highest `cellY + amount/9` minus the box's min Y; eyes count as underwater when `eyeY − 1/9` is below the surface [old]. |
| Current | `applyCurrentTo(WATER, entity, 0.014)` each tick, along the flow vector from `FlowingFluid.getFlow` [B]. |
| Fall damage | Touching water resets fall distance [B]. |
| Movement (`LivingEntity.travelInWater`) | Horizontal drag **0.8** (sprint **0.9**), vertical drag 0.8, acceleration **0.02** [B]. Depth Strider: `water_movement_efficiency` +⅓/level, which blends drag toward 0.546 and acceleration toward walk speed (halved off-ground). Dolphin's Grace sets drag to 0.96 [B]. |
| Vertical | Space (`jumpInLiquid`) +0.04/tick; sneak (`goDownInWater`) −0.04 [B]. Gravity 0.08 with a reduced fluid-falling adjustment. Walking into a ledge from water gives vy 0.3 (`jumpOutOfFluid`) [B]. |
| Swimming state | **Start:** sprinting + eyes underwater + water at the feet block. **Keep:** sprinting + touching water [B]. Pose becomes `SWIMMING`: hitbox 0.6 × 0.6, eye height 0.4 [old]. Swim direction follows look pitch. |
| Air | Max **300** ticks. −1/tick with eyes underwater (Respiration skips some ticks); +4/tick regain. At −20 air: 2 damage (1 heart), and air resets to 0, so the player takes a heart every 20 ticks [B]. Bubble columns refill air. |
| Bubble columns | Inside: down `vy=max(−0.3, vy−0.03)`, up `vy=min(0.7, vy+0.06)` [B]. |
| Mining | Eyes underwater → ×**0.2** (`submerged_mining_speed`, Aqua Affinity → 1.0). Not on ground → another ÷5 [B]. |
| Other | Extinguishes burning entities. Underwater fog is client-side [B / not traced]. |

## 5. Block table (registry order)

`#` = protocol id. State IDs are 26.3 runtime ids, for reference only; never save them. Flags: `tint`, `anim`, `rand-rot`, and rotation properties (`axis`, `facing`, `lit`…).

| # | Block ID | Shape | Textures (`textures/block/*.png`) | Layer | Flags | State IDs | Where found |
|---:|---|---|---|---|---|---|---|
| 1 | `stone` | full cube | stone | opaque | rand-rot | 1 | Main solid rock of the overworld: surface mountains, caves, and underground in all biomes — Y 8..320 (solid stone above Y=8; blends to deepslate between Y=8 and Y=0) |
| 2 | `granite` | full cube | granite | opaque |  | 2 | Ore blobs in stone in all overworld biomes; also the filler of the copper ore vein — Y 0..60 and 64..128 (two blob batches) |
| 3 | `polished_granite` | full cube | polished_granite | opaque |  | 3 | Many ocean ruin variants |
| 4 | `diorite` | full cube | diorite | opaque |  | 4 | Ore blobs in stone in all overworld biomes — Y 0..60 and 64..128 (two blob batches) |
| 5 | `polished_diorite` | full cube | polished_diorite | opaque |  | 5 | Warm ocean ruins |
| 6 | `andesite` | full cube | andesite | opaque |  | 6 | Ore blobs in stone in all overworld biomes; also coral reef crusts — Y 0..60 and 64..128 (two blob batches) |
| 7 | `polished_andesite` | full cube | polished_andesite | opaque |  | 7 | Igloo basements, woodland mansions, trial chamber supply chests |
| 8 | `grass_block` | full cube | top: grass_block_top · bottom: dirt · side: grass_block_side | cutout | tint, rand-rot, snowy | 8–9 | Surface of most overworld biomes; disks between mud in mangrove swamps; villages and ancient cities |
| 9 | `dirt` | full cube | dirt | opaque | rand-rot | 10 | Upper terrain layers in most overworld biomes; dirt blobs inside stone; shallow-water disks — Y 0..160 (blobs in stone); surface layers elsewhere |
| 10 | `coarse_dirt` | full cube | coarse_dirt | opaque |  | 11 | Patches in windswept savanna, wooded badlands, old growth taiga and dappled forest; ice patches; trail ruins; abandoned camps |
| 11 | `podzol` | full cube | top: podzol_top · bottom: dirt · side: podzol_side | opaque | rand-rot, snowy | 12–13 | Old growth taiga, bamboo jungle, trial chamber spawner areas, abandoned camps |
| 12 | `cobblestone` | full cube | cobblestone | opaque |  | 14 | Monster rooms, jungle pyramids, pillager outposts, ocean ruins, strongholds, villages, woodland mansions, trail ruins |
| 13 | `oak_planks` | full cube | oak_planks | opaque |  | 15 | Plains villages and mineshafts (oak beams) |
| 14 | `spruce_planks` | full cube | spruce_planks | opaque |  | 16 | Taiga and snowy taiga villages |
| 15 | `birch_planks` | full cube | birch_planks | opaque |  | 17 | — |
| 16 | `jungle_planks` | full cube | jungle_planks | opaque |  | 18 | — |
| 17 | `acacia_planks` | full cube | acacia_planks | opaque |  | 19 | Savanna villages |
| 18 | `cherry_planks` | full cube | cherry_planks | opaque |  | 20 | — |
| 19 | `dark_oak_planks` | full cube | dark_oak_planks | opaque |  | 21 | — |
| 20 | `pale_oak_wood` | full cube | pale_oak_log | opaque | axis | 22–24 | — |
| 21 | `pale_oak_planks` | full cube | pale_oak_planks | opaque |  | 25 | — |
| 22 | `mangrove_planks` | full cube | mangrove_planks | opaque |  | 26 | — |
| 23 | `poplar_planks` | full cube | poplar_planks | opaque |  | 27 | — |
| 24 | `bamboo_planks` | full cube | bamboo_planks | opaque |  | 28 | — |
| 25 | `bamboo_mosaic` | full cube | bamboo_mosaic | opaque |  | 29 | — |
| 34 | `mangrove_propagule` | model | mangrove_propagule · mangrove_propagule_hanging | cutout |  | 46–85 | — |
| 36 | `bedrock` | full cube | bedrock | opaque | rand-rot | 88 | Indestructible layer at the bottom of the overworld and Nether, the Nether roof, End spikes and End gateways — Y -64..-60 (overworld floor, gradient to -59); Nether floor 0..4 and roof near 122..127; End spikes and gateways |
| 39 | `sand` | full cube | sand | opaque | rand-rot | 121 | Disks in shallow water; deserts and beaches (layers over sandstone) |
| 40 | `suspicious_sand` | full cube | suspicious_sand_0 | opaque | dusted | 122–125 | Buried rooms under desert pyramids, desert wells, warm ocean ruins |
| 41 | `red_sand` | full cube | red_sand | opaque | rand-rot | 126 | Single layer in badlands below about Y=74 — Y below about 74 (badlands) |
| 42 | `gravel` | full cube | gravel | opaque |  | 127 | Blobs in stone and disks in shallow water across overworld biomes; beds of deep water — Y -64..319 (gravel blobs in stone; ore_gravel) |
| 43 | `suspicious_gravel` | full cube | suspicious_gravel_0 | opaque | dusted | 128–131 | Cold ocean ruins and trail ruins |
| 44 | `gold_ore` | full cube | gold_ore | opaque |  | 132 | Ore blobs across the overworld; extra batch in badlands, eroded badlands and wooded badlands — Y -64..32 (peak -16) + -64..-48; badlands +32..256 |
| 45 | `deepslate_gold_ore` | full cube | deepslate_gold_ore | opaque |  | 133 | Gold ore in deepslate and tuff below Y=0 — Y -64..32 (peak -16) + -64..-48; badlands +32..256 (where it replaces deepslate, ~Y<8) |
| 46 | `iron_ore` | full cube | iron_ore | opaque |  | 134 | Ore blobs in stone across all overworld biomes; main iron source — Y -24..56 (peak 16) + 80..top (peak 232) + small -64..72 |
| 47 | `deepslate_iron_ore` | full cube | deepslate_iron_ore | opaque |  | 135 | Iron ore in the deepslate layer and in the iron ore vein (tuff filler) — Y -24..56 (peak 16) + 80..top (peak 232) + small -64..72 (where it replaces deepslate, ~Y<8) |
| 48 | `coal_ore` | full cube | coal_ore | opaque |  | 136 | Ore blobs in stone, tuff and deepslate across all overworld biomes — Y 0..192 (peak 96) + 136..top |
| 49 | `deepslate_coal_ore` | full cube | deepslate_coal_ore | opaque |  | 137 | Coal ore that replaces deepslate or tuff below Y=0 — Y 0..192 (peak 96) + 136..top (where it replaces deepslate, ~Y<8) |
| 50 | `nether_gold_ore` | full cube | nether_gold_ore | opaque |  | 138 | Netherrack blobs in all Nether biomes; more attempts in basalt deltas — Y 10..246 |
| 51 | `oak_log` | full cube | ends: oak_log_top · side: oak_log | opaque | axis | 139–141 | Oak trees across temperate overworld biomes; azalea trees; fallen oak trees |
| 52 | `spruce_log` | full cube | ends: spruce_log_top · side: spruce_log | opaque | axis | 142–144 | Spruce trees in taiga and snowy taiga; mega spruce and pine trees; fallen spruce trees |
| 53 | `birch_log` | full cube | ends: birch_log_top · side: birch_log | opaque | axis | 145–147 | Birch trees in birch forests and other forests; fallen birch trees |
| 54 | `jungle_log` | full cube | ends: jungle_log_top · side: jungle_log | opaque | axis | 148–150 | Jungle trees and jungle bushes in jungle biomes; fallen jungle trees |
| 55 | `acacia_log` | full cube | ends: acacia_log_top · side: acacia_log | opaque | axis | 151–153 | Acacia trees in savannas |
| 56 | `cherry_log` | full cube | ends: cherry_log_top · side: cherry_log | opaque | axis | 154–156 | Cherry trees in cherry groves |
| 57 | `dark_oak_log` | full cube | ends: dark_oak_log_top · side: dark_oak_log | opaque | axis | 157–159 | Dark oak trees in dark forests |
| 58 | `pale_oak_log` | full cube | ends: pale_oak_log_top · side: pale_oak_log | opaque | axis | 160–162 | Pale oak trees in pale gardens (creaking hearts inside trunks) |
| 59 | `mangrove_log` | full cube | ends: mangrove_log_top · side: mangrove_log | opaque | axis | 163–165 | Mangrove trees in mangrove swamps |
| 60 | `poplar_log` | full cube | ends: poplar_log_top · side: poplar_log | opaque | axis | 166–168 | Poplar trees in dappled forests (26.3); fallen poplar trees |
| 61 | `mangrove_roots` | model | mangrove_roots_side · mangrove_roots_top | cutout |  | 169–170 | Roots of mangrove trees in mangrove swamps; some trial chamber entrances |
| 62 | `muddy_mangrove_roots` | full cube | ends: muddy_mangrove_roots_top · side: muddy_mangrove_roots_side | opaque | axis | 171–173 | Where mud meets mangrove roots in mangrove swamps; some trial chamber entrances |
| 63 | `bamboo_block` | full cube | ends: bamboo_block_top · side: bamboo_block | opaque | axis | 174–176 | — |
| 64 | `stripped_spruce_log` | full cube | ends: stripped_spruce_log_top · side: stripped_spruce_log | opaque | axis | 177–179 | — |
| 65 | `stripped_birch_log` | full cube | ends: stripped_birch_log_top · side: stripped_birch_log | opaque | axis | 180–182 | — |
| 66 | `stripped_jungle_log` | full cube | ends: stripped_jungle_log_top · side: stripped_jungle_log | opaque | axis | 183–185 | — |
| 67 | `stripped_acacia_log` | full cube | ends: stripped_acacia_log_top · side: stripped_acacia_log | opaque | axis | 186–188 | — |
| 68 | `stripped_cherry_log` | full cube | ends: stripped_cherry_log_top · side: stripped_cherry_log | opaque | axis | 189–191 | — |
| 69 | `stripped_dark_oak_log` | full cube | ends: stripped_dark_oak_log_top · side: stripped_dark_oak_log | opaque | axis | 192–194 | — |
| 70 | `stripped_pale_oak_log` | full cube | ends: stripped_pale_oak_log_top · side: stripped_pale_oak_log | opaque | axis | 195–197 | — |
| 71 | `stripped_oak_log` | full cube | ends: stripped_oak_log_top · side: stripped_oak_log | opaque | axis | 198–200 | — |
| 72 | `stripped_mangrove_log` | full cube | ends: stripped_mangrove_log_top · side: stripped_mangrove_log | opaque | axis | 201–203 | — |
| 73 | `stripped_poplar_log` | full cube | ends: stripped_poplar_log_top · side: stripped_poplar_log | opaque | axis | 204–206 | — |
| 74 | `stripped_bamboo_block` | full cube | ends: stripped_bamboo_block_top · side: stripped_bamboo_block | opaque | axis | 207–209 | — |
| 75 | `oak_wood` | full cube | oak_log | opaque | axis | 210–212 | — |
| 76 | `spruce_wood` | full cube | spruce_log | opaque | axis | 213–215 | Snowy taiga and taiga villages |
| 77 | `birch_wood` | full cube | birch_log | opaque | axis | 216–218 | — |
| 78 | `jungle_wood` | full cube | jungle_log | opaque | axis | 219–221 | — |
| 79 | `acacia_wood` | full cube | acacia_log | opaque | axis | 222–224 | Savanna villages |
| 80 | `cherry_wood` | full cube | cherry_log | opaque | axis | 225–227 | — |
| 81 | `dark_oak_wood` | full cube | dark_oak_log | opaque | axis | 228–230 | — |
| 82 | `mangrove_wood` | full cube | mangrove_log | opaque | axis | 231–233 | Some trial chamber entrance rooms |
| 83 | `poplar_wood` | full cube | poplar_log | opaque | axis | 234–236 | — |
| 84 | `stripped_oak_wood` | full cube | stripped_oak_log | opaque | axis | 237–239 | — |
| 85 | `stripped_spruce_wood` | full cube | stripped_spruce_log | opaque | axis | 240–242 | — |
| 86 | `stripped_birch_wood` | full cube | stripped_birch_log | opaque | axis | 243–245 | — |
| 87 | `stripped_jungle_wood` | full cube | stripped_jungle_log | opaque | axis | 246–248 | — |
| 88 | `stripped_acacia_wood` | full cube | stripped_acacia_log | opaque | axis | 249–251 | — |
| 89 | `stripped_cherry_wood` | full cube | stripped_cherry_log | opaque | axis | 252–254 | — |
| 90 | `stripped_dark_oak_wood` | full cube | stripped_dark_oak_log | opaque | axis | 255–257 | — |
| 91 | `stripped_pale_oak_wood` | full cube | stripped_pale_oak_log | opaque | axis | 258–260 | — |
| 92 | `stripped_mangrove_wood` | full cube | stripped_mangrove_log | opaque | axis | 261–263 | — |
| 93 | `stripped_poplar_wood` | full cube | stripped_poplar_log | opaque | axis | 264–266 | — |
| 94 | `oak_leaves` | full cube | oak_leaves | cutout | tint | 267–294 | Canopy of oak trees; also in plains villages, trial chambers, abandoned camps |
| 95 | `spruce_leaves` | full cube | spruce_leaves | cutout | tint | 295–322 | Canopy of spruce trees; abandoned camps |
| 96 | `birch_leaves` | full cube | birch_leaves | cutout | tint | 323–350 | Canopy of birch trees; abandoned camps in birch and flower forests |
| 97 | `jungle_leaves` | full cube | jungle_leaves | cutout | tint | 351–378 | Canopy of jungle trees; abandoned camps in bamboo jungles |
| 98 | `acacia_leaves` | full cube | acacia_leaves | cutout | tint | 379–406 | Canopy of acacia trees in savannas; abandoned camps |
| 99 | `cherry_leaves` | full cube | cherry_leaves | cutout | tint | 407–434 | Canopy of cherry trees in cherry groves |
| 100 | `dark_oak_leaves` | full cube | dark_oak_leaves | cutout | tint | 435–462 | Canopy of dark oak trees in dark forests; woodland mansions |
| 101 | `pale_oak_leaves` | full cube | pale_oak_leaves | cutout | tint | 463–490 | Canopy of pale oak trees in pale gardens |
| 102 | `mangrove_leaves` | full cube | mangrove_leaves | cutout | tint | 491–518 | Canopy of mangrove trees; some trial chamber entrances |
| 103 | `red_poplar_leaves` | full cube | red_poplar_leaves | cutout |  | 519–546 | Poplar canopy in dappled forests (26.3) |
| 104 | `orange_poplar_leaves` | full cube | orange_poplar_leaves | cutout |  | 547–574 | Poplar canopy in dappled forests (26.3) |
| 105 | `yellow_poplar_leaves` | full cube | yellow_poplar_leaves | cutout |  | 575–602 | Poplar canopy in dappled forests (26.3) |
| 106 | `azalea_leaves` | full cube | azalea_leaves | cutout |  | 603–630 | — |
| 107 | `flowering_azalea_leaves` | full cube | flowering_azalea_leaves | cutout |  | 631–658 | — |
| 108 | `sponge` | full cube | sponge | opaque |  | 659 | — |
| 109 | `wet_sponge` | full cube | wet_sponge | opaque |  | 660 | Sponge rooms of ocean monuments (about 30 per room) |
| 110 | `glass` | full cube | glass | translucent |  | 661 | — |
| 111 | `lapis_ore` | full cube | lapis_ore | opaque |  | 662 | Ore blobs across the overworld, plus buried blobs — Y -32..32 (peak 0) + buried -64..64 |
| 112 | `deepslate_lapis_ore` | full cube | deepslate_lapis_ore | opaque |  | 663 | Lapis ore in the deepslate layer — Y -32..32 (peak 0) + buried -64..64 (where it replaces deepslate, ~Y<8) |
| 113 | `lapis_block` | full cube | lapis_block | opaque |  | 664 | — |
| 114 | `dispenser` | full cube | down: furnace_top · up: furnace_top · north: dispenser_front · south: furnace_side · east: furnace_side · west: furnace_side | opaque | facing | 665–676 | — |
| 115 | `sandstone` | full cube | top: sandstone_top · bottom: sandstone_bottom · side: sandstone | opaque |  | 677 | Under sand in deserts and beaches; desert wells, villages, pyramids, warm ocean ruins |
| 116 | `chiseled_sandstone` | full cube | ends: sandstone_top · side: chiseled_sandstone | opaque |  | 678 | Desert pyramids, warm ocean ruins, trial chambers |
| 117 | `cut_sandstone` | full cube | ends: sandstone_top · side: cut_sandstone | opaque |  | 679 | Desert villages, desert pyramids, warm ocean ruins |
| 118 | `note_block` | full cube | note_block | opaque |  | 680–2029 | — |
| 140 | `short_grass` | model | short_grass | cutout | tint | 2363 | — |
| 141 | `fern` | model | fern | cutout | tint | 2364 | — |
| 142 | `dead_bush` | model | dead_bush | cutout |  | 2365 | — |
| 143 | `bush` | model | bush | cutout | tint | 2366 | — |
| 144 | `red_shrub` | model | red_shrub | cutout |  | 2367 | — |
| 145 | `short_dry_grass` | model | short_dry_grass | cutout |  | 2368 | — |
| 146 | `tall_dry_grass` | model | tall_dry_grass | cutout |  | 2369 | — |
| 147 | `seagrass` | model | seagrass | cutout | anim | 2370 | — |
| 148 | `tall_seagrass` | model | tall_seagrass_bottom · tall_seagrass_top | cutout | anim | 2371–2372 | — |
| 151 | `white_wool` | full cube | white_wool | opaque |  | 2409 | — |
| 152 | `orange_wool` | full cube | orange_wool | opaque |  | 2410 | — |
| 153 | `magenta_wool` | full cube | magenta_wool | opaque |  | 2411 | — |
| 154 | `light_blue_wool` | full cube | light_blue_wool | opaque |  | 2412 | — |
| 155 | `yellow_wool` | full cube | yellow_wool | opaque |  | 2413 | — |
| 156 | `lime_wool` | full cube | lime_wool | opaque |  | 2414 | — |
| 157 | `pink_wool` | full cube | pink_wool | opaque |  | 2415 | — |
| 158 | `gray_wool` | full cube | gray_wool | opaque |  | 2416 | — |
| 159 | `light_gray_wool` | full cube | light_gray_wool | opaque |  | 2417 | — |
| 160 | `cyan_wool` | full cube | cyan_wool | opaque |  | 2418 | — |
| 161 | `purple_wool` | full cube | purple_wool | opaque |  | 2419 | — |
| 162 | `blue_wool` | full cube | blue_wool | opaque |  | 2420 | — |
| 163 | `brown_wool` | full cube | brown_wool | opaque |  | 2421 | — |
| 164 | `green_wool` | full cube | green_wool | opaque |  | 2422 | — |
| 165 | `red_wool` | full cube | red_wool | opaque |  | 2423 | — |
| 166 | `black_wool` | full cube | black_wool | opaque |  | 2424 | — |
| 183 | `white_wool_slab` | slab | white_wool | opaque |  | 3705–3710 | — |
| 184 | `orange_wool_slab` | slab | orange_wool | opaque |  | 3711–3716 | — |
| 185 | `magenta_wool_slab` | slab | magenta_wool | opaque |  | 3717–3722 | — |
| 186 | `light_blue_wool_slab` | slab | light_blue_wool | opaque |  | 3723–3728 | — |
| 187 | `yellow_wool_slab` | slab | yellow_wool | opaque |  | 3729–3734 | — |
| 188 | `lime_wool_slab` | slab | lime_wool | opaque |  | 3735–3740 | — |
| 189 | `pink_wool_slab` | slab | pink_wool | opaque |  | 3741–3746 | — |
| 190 | `gray_wool_slab` | slab | gray_wool | opaque |  | 3747–3752 | — |
| 191 | `light_gray_wool_slab` | slab | light_gray_wool | opaque |  | 3753–3758 | — |
| 192 | `cyan_wool_slab` | slab | cyan_wool | opaque |  | 3759–3764 | — |
| 193 | `purple_wool_slab` | slab | purple_wool | opaque |  | 3765–3770 | — |
| 194 | `blue_wool_slab` | slab | blue_wool | opaque |  | 3771–3776 | — |
| 195 | `brown_wool_slab` | slab | brown_wool | opaque |  | 3777–3782 | — |
| 196 | `green_wool_slab` | slab | green_wool | opaque |  | 3783–3788 | — |
| 197 | `red_wool_slab` | slab | red_wool | opaque |  | 3789–3794 | — |
| 198 | `black_wool_slab` | slab | black_wool | opaque |  | 3795–3800 | — |
| 200 | `dandelion` | model | dandelion | cutout |  | 3813 | — |
| 203 | `poppy` | model | poppy | cutout |  | 3816 | — |
| 204 | `blue_orchid` | model | blue_orchid | cutout |  | 3817 | — |
| 205 | `allium` | model | allium | cutout |  | 3818 | — |
| 206 | `azure_bluet` | model | azure_bluet | cutout |  | 3819 | — |
| 207 | `red_tulip` | model | red_tulip | cutout |  | 3820 | — |
| 208 | `orange_tulip` | model | orange_tulip | cutout |  | 3821 | — |
| 209 | `white_tulip` | model | white_tulip | cutout |  | 3822 | — |
| 210 | `pink_tulip` | model | pink_tulip | cutout |  | 3823 | — |
| 211 | `oxeye_daisy` | model | oxeye_daisy | cutout |  | 3824 | — |
| 212 | `cornflower` | model | cornflower | cutout |  | 3825 | — |
| 214 | `lily_of_the_valley` | model | lily_of_the_valley | cutout |  | 3827 | — |
| 215 | `brown_mushroom` | model | brown_mushroom | cutout |  | 3828 | — |
| 216 | `red_mushroom` | model | red_mushroom | cutout |  | 3829 | — |
| 217 | `gold_block` | full cube | gold_block | opaque |  | 3830 | — |
| 218 | `iron_block` | full cube | iron_block | opaque |  | 3831 | — |
| 219 | `bricks` | full cube | bricks | opaque |  | 3832 | Ocean ruins, plains village armorer houses, trail ruins |
| 220 | `tnt` | full cube | top: tnt_top · bottom: tnt_bottom · side: tnt_side | opaque |  | 3833–3834 | — |
| 221 | `bookshelf` | full cube | ends: oak_planks · side: bookshelf | opaque |  | 3835 | Village libraries and some village houses; stronghold libraries |
| 236 | `mossy_cobblestone` | full cube | mossy_cobblestone | opaque |  | 4924 | Monster rooms, jungle pyramids, pillager outposts, cold ocean ruins, plains and taiga villages; forest rocks; trial chambers |
| 237 | `obsidian` | full cube | obsidian | opaque |  | 4925 | Ruined portals (Overworld and Nether); woodland mansion obsidian rooms; cold ocean ruins; End spikes on the main End island and End ship (End city) — Y null (End spikes reach Y=0) |
| 238 | `torch` | model | torch | cutout |  | 4926 | — |
| 239 | `wall_torch` | model | torch | cutout |  | 4927–4930 | — |
| 243 | `creaking_heart` | full cube | ends: creaking_heart_top_awake · side: creaking_heart_awake | opaque | axis,creaking_heart_state | 5445–5462 | About 10% of pale oak trunks in pale gardens (fully enclosed by logs) |
| 247 | `diamond_ore` | full cube | diamond_ore | opaque |  | 6863 | Ore features in stone across the overworld; more common deeper down — Y -64..16 (peak -64) + -64..-4 |
| 248 | `deepslate_diamond_ore` | full cube | deepslate_diamond_ore | opaque |  | 6864 | Diamond ore in the deepslate layer — Y -64..16 (peak -64) + -64..-4 (where it replaces deepslate, ~Y<8) |
| 249 | `diamond_block` | full cube | diamond_block | opaque |  | 6865 | — |
| 250 | `crafting_table` | full cube | down: oak_planks · up: crafting_table_top · north: crafting_table_front · south: crafting_table_side · east: crafting_table_side · west: crafting_table_front | opaque |  | 6866 | — |
| 253 | `furnace` | full cube | down: furnace_top · up: furnace_top · north: furnace_front · south: furnace_side · east: furnace_side · west: furnace_side | opaque | facing,lit | 6883–6890 | — |
| 320 | `redstone_ore` | full cube | redstone_ore | opaque |  | 8551–8552 | Ore blobs across the overworld — Y -64..15 + -64..-32 (peak -64) |
| 321 | `deepslate_redstone_ore` | full cube | deepslate_redstone_ore | opaque |  | 8553–8554 | Redstone ore in the deepslate layer — Y -64..15 + -64..-32 (peak -64) (where it replaces deepslate, ~Y<8) |
| 325 | `snow` | layers | snow | opaque |  | 8589–8596 | — |
| 326 | `ice` | full cube | ice | translucent |  | 8597 | Freeze-top-layer ice on frozen oceans, frozen rivers, snowy beaches, lakes and exposed aquifers in snowy biomes |
| 327 | `snow_block` | full cube | snow | opaque |  | 8598 | Ground in ice spikes, groves, snowy slopes and peaks; igloo walls; some abandoned camps |
| 328 | `cactus` | model | cactus_bottom · cactus_top · cactus_side | cutout |  | 8599–8614 | — |
| 329 | `cactus_flower` | model | cactus_flower | cutout |  | 8615 | — |
| 330 | `clay` | full cube | clay | opaque |  | 8616 | Blobs in lush caves and underwater disks in shallow rivers, lakes and oceans; mason houses in villages — Y -64..256 (lush caves blobs, any height); underwater disks at sea level |
| 331 | `sugar_cane` | model | sugar_cane | cutout | tint | 8617–8632 | — |
| 332 | `jukebox` | full cube | top: jukebox_top · bottom: jukebox_side · side: jukebox_side | opaque |  | 8633–8634 | — |
| 334 | `netherrack` | full cube | netherrack | opaque | rand-rot | 8667 | Most of the Nether terrain; ruined portals; bastion remnants — Y 0..127 (nether terrain) |
| 335 | `soul_sand` | full cube | soul_sand | opaque |  | 8668 | Soul sand valleys surface; Nether ore blobs; fortress nether wart rooms; ancient cities (Overworld) — Y 0..31 (ore blobs in Nether) |
| 336 | `soul_soil` | full cube | soul_soil | opaque |  | 8669 | Soul sand valleys surface, with soul sand |
| 337 | `basalt` | full cube | ends: basalt_top · side: basalt_side | opaque | axis | 8670–8672 | Basalt deltas (terrain, blobs and columns) and soul sand valley pillars — Y 0..127 (nether terrain) |
| 338 | `polished_basalt` | full cube | ends: polished_basalt_top · side: polished_basalt_side | opaque | axis | 8673–8675 | — |
| 343 | `glowstone` | full cube | glowstone | opaque |  | 8686 | Ceilings of netherrack in all Nether biomes (rarer in soul sand valleys); bastion hoglin stables — Y 0..127 (ceiling blobs) |
| 345 | `carved_pumpkin` | full cube | down: pumpkin_top · up: pumpkin_top · north: carved_pumpkin · south: pumpkin_side · east: pumpkin_side · west: pumpkin_side | opaque | facing | 8689–8692 | — |
| 346 | `jack_o_lantern` | full cube | down: pumpkin_top · up: pumpkin_top · north: jack_o_lantern · south: pumpkin_side · east: pumpkin_side · west: pumpkin_side | opaque | facing | 8693–8696 | — |
| 349 | `white_stained_glass` | full cube | white_stained_glass | translucent |  | 8768 | — |
| 350 | `orange_stained_glass` | full cube | orange_stained_glass | translucent |  | 8769 | — |
| 351 | `magenta_stained_glass` | full cube | magenta_stained_glass | translucent |  | 8770 | — |
| 352 | `light_blue_stained_glass` | full cube | light_blue_stained_glass | translucent |  | 8771 | — |
| 353 | `yellow_stained_glass` | full cube | yellow_stained_glass | translucent |  | 8772 | — |
| 354 | `lime_stained_glass` | full cube | lime_stained_glass | translucent |  | 8773 | — |
| 355 | `pink_stained_glass` | full cube | pink_stained_glass | translucent |  | 8774 | — |
| 356 | `gray_stained_glass` | full cube | gray_stained_glass | translucent |  | 8775 | — |
| 357 | `light_gray_stained_glass` | full cube | light_gray_stained_glass | translucent |  | 8776 | — |
| 358 | `cyan_stained_glass` | full cube | cyan_stained_glass | translucent |  | 8777 | — |
| 359 | `purple_stained_glass` | full cube | purple_stained_glass | translucent |  | 8778 | — |
| 360 | `blue_stained_glass` | full cube | blue_stained_glass | translucent |  | 8779 | — |
| 361 | `brown_stained_glass` | full cube | brown_stained_glass | translucent |  | 8780 | — |
| 362 | `green_stained_glass` | full cube | green_stained_glass | translucent |  | 8781 | — |
| 363 | `red_stained_glass` | full cube | red_stained_glass | translucent |  | 8782 | — |
| 364 | `black_stained_glass` | full cube | black_stained_glass | translucent |  | 8783 | — |
| 376 | `stone_bricks` | full cube | stone_bricks | opaque |  | 9488 | Strongholds (walls, floors, ceilings); ruined portals; cold ocean ruins; igloo basements; trail ruins; trial chambers |
| 377 | `mossy_stone_bricks` | full cube | mossy_stone_bricks | opaque |  | 9489 | Strongholds and igloo basements; ocean ruins; ruined portals |
| 378 | `cracked_stone_bricks` | full cube | cracked_stone_bricks | opaque |  | 9490 | Strongholds; ocean ruins, igloo basements, ruined portals, trail ruins |
| 379 | `chiseled_stone_bricks` | full cube | chiseled_stone_bricks | opaque |  | 9491 | Ocean ruins, ruined portals, jungle pyramids (three per pyramid), igloo basements |
| 380 | `packed_mud` | full cube | packed_mud | opaque |  | 9492 | Trail ruins (archaeology) |
| 381 | `mud_bricks` | full cube | mud_bricks | opaque |  | 9493 | Trail ruins |
| 382 | `infested_stone` | full cube | stone | opaque | rand-rot | 9494 | Silverfish ore feature in mountain biomes (meadow, cherry grove, grove, snowy slopes, peaks, windswept hills and forest) — Y -64..63 (ore_infested placement) |
| 383 | `infested_cobblestone` | full cube | cobblestone | opaque |  | 9495 | Rare fake End portal room in woodland mansions |
| 384 | `infested_stone_bricks` | full cube | stone_bricks | opaque |  | 9496 | Strongholds and igloo basements, in place of some stone bricks |
| 385 | `infested_mossy_stone_bricks` | full cube | mossy_stone_bricks | opaque |  | 9497 | — |
| 386 | `infested_cracked_stone_bricks` | full cube | cracked_stone_bricks | opaque |  | 9498 | — |
| 387 | `infested_chiseled_stone_bricks` | full cube | chiseled_stone_bricks | opaque |  | 9499 | — |
| 388 | `brown_mushroom_block` | full cube | brown_mushroom_block | opaque |  | 9500–9563 | Huge brown mushrooms (mushroom fields, taiga, swamp); swamp abandoned camps |
| 389 | `red_mushroom_block` | full cube | red_mushroom_block | opaque |  | 9564–9627 | Huge red mushrooms (mushroom fields, taiga, swamp); swamp abandoned camps |
| 390 | `mushroom_stem` | full cube | mushroom_stem | opaque |  | 9628–9691 | Trunks of huge brown and red mushrooms; dark forest vegetation |
| 410 | `pumpkin` | full cube | ends: pumpkin_top · side: pumpkin_side | opaque |  | 10066 | Pumpkin patches on clear grass in most overworld biomes; woodland mansions, taiga villages, pillager outposts, abandoned camps |
| 411 | `melon` | full cube | ends: melon_top · side: melon_side | opaque |  | 10067 | Melon patches in jungle biomes; savanna villages; woodland mansion tillage rooms; abandoned camps |
| 416 | `vine` | model | vine | cutout | tint | 10092–10123 | — |
| 417 | `glow_lichen` | model | glow_lichen | cutout |  | 10124–10251 | — |
| 423 | `mycelium` | full cube | top: mycelium_top · bottom: dirt · side: mycelium_side | opaque | rand-rot, snowy | 10652–10653 | Mushroom fields in abundance; single blocks in swamp abandoned camps |
| 424 | `lily_pad` | model | lily_pad | cutout | tint, rand-rot | 10654 | — |
| 425 | `resin_block` | full cube | resin_block | opaque |  | 10655 | Pale garden abandoned camps (per wiki) |
| 426 | `resin_bricks` | full cube | resin_bricks | opaque |  | 10656 | — |
| 428 | `resin_brick_slab` | slab | resin_bricks | opaque |  | 10737–10742 | — |
| 430 | `chiseled_resin_bricks` | full cube | chiseled_resin_bricks | opaque |  | 11067 | — |
| 431 | `nether_bricks` | full cube | nether_bricks | opaque |  | 11068 | Walls and pillars of Nether fortresses |
| 443 | `end_stone` | full cube | end_stone | opaque |  | 11211 | All End islands, main and outer |
| 445 | `redstone_lamp` | full cube | redstone_lamp | opaque | lit | 11213–11214 | — |
| 446 | `cocoa` | model | cocoa_stage0 · cocoa_stage1 · cocoa_stage2 | cutout |  | 11215–11226 | — |
| 447 | `shelf_mushroom` | model | shelf_mushroom_stage0 · shelf_mushroom_stage1 | opaque |  | 11227–11234 | — |
| 449 | `emerald_ore` | full cube | emerald_ore | opaque |  | 11315 | Single scattered blocks in mountains, windswept hills and windswept forest, meadows, cherry groves, groves, snowy slopes and peaks — Y -16..top (peak 232), mountain biomes |
| 450 | `deepslate_emerald_ore` | full cube | deepslate_emerald_ore | opaque |  | 11316 | Emerald ore in the deepslate layer of mountain biomes — Y -16..top (peak 232), mountain biomes (where it replaces deepslate, ~Y<8) |
| 454 | `emerald_block` | full cube | emerald_block | opaque |  | 11469 | — |
| 458 | `command_block` | full cube | down: command_block_side · up: command_block_side · north: command_block_front · south: command_block_back · east: command_block_side · west: command_block_side | opaque | anim, conditional,facing | 11710–11721 | — |
| 528 | `redstone_block` | full cube | redstone_block | opaque |  | 13078 | — |
| 529 | `nether_quartz_ore` | full cube | nether_quartz_ore | opaque |  | 13079 | Netherrack blobs in all Nether biomes; more attempts in basalt deltas — Y 10..246 |
| 531 | `quartz_block` | full cube | ends: quartz_block_top · side: quartz_block_side | opaque |  | 13090 | — |
| 532 | `chiseled_quartz_block` | full cube | ends: chiseled_quartz_block_top · side: chiseled_quartz_block | opaque |  | 13091 | — |
| 533 | `quartz_pillar` | full cube | ends: quartz_pillar_top · side: quartz_pillar_side | opaque | axis | 13092–13094 | — |
| 536 | `dropper` | full cube | down: furnace_top · up: furnace_top · north: dropper_front · south: furnace_side · east: furnace_side · west: furnace_side | opaque | facing | 13199–13210 | — |
| 537 | `white_terracotta` | full cube | white_terracotta | opaque |  | 13211 | Badlands layers — Y above about 74 (badlands layers); also under shallow water |
| 538 | `orange_terracotta` | full cube | orange_terracotta | opaque |  | 13212 | Badlands layers — Y above about 74 (layers); lower elevations below red sand |
| 539 | `magenta_terracotta` | full cube | magenta_terracotta | opaque |  | 13213 | — |
| 540 | `light_blue_terracotta` | full cube | light_blue_terracotta | opaque |  | 13214 | — |
| 541 | `yellow_terracotta` | full cube | yellow_terracotta | opaque |  | 13215 | Badlands layers — Y above about 74 (badlands layers) |
| 542 | `lime_terracotta` | full cube | lime_terracotta | opaque |  | 13216 | — |
| 543 | `pink_terracotta` | full cube | pink_terracotta | opaque |  | 13217 | — |
| 544 | `gray_terracotta` | full cube | gray_terracotta | opaque |  | 13218 | — |
| 545 | `light_gray_terracotta` | full cube | light_gray_terracotta | opaque |  | 13219 | Badlands layers — Y above about 74 (badlands layers) |
| 546 | `cyan_terracotta` | full cube | cyan_terracotta | opaque |  | 13220 | — |
| 547 | `purple_terracotta` | full cube | purple_terracotta | opaque |  | 13221 | — |
| 548 | `blue_terracotta` | full cube | blue_terracotta | opaque |  | 13222 | — |
| 549 | `brown_terracotta` | full cube | brown_terracotta | opaque |  | 13223 | Badlands layers — Y above about 74 (badlands layers) |
| 550 | `green_terracotta` | full cube | green_terracotta | opaque |  | 13224 | — |
| 551 | `red_terracotta` | full cube | red_terracotta | opaque |  | 13225 | Badlands layers — Y above about 74 (badlands layers) |
| 552 | `black_terracotta` | full cube | black_terracotta | opaque |  | 13226 | — |
| 581 | `prismarine` | full cube | prismarine | opaque | anim | 14478 | Ocean monuments (main structure blocks); cold ocean ruins |
| 582 | `prismarine_bricks` | full cube | prismarine_bricks | opaque |  | 14479 | Ocean monuments (main structure blocks and pillars) |
| 583 | `dark_prismarine` | full cube | dark_prismarine | opaque |  | 14480 | Ocean monuments (treasure room and decoration) |
| 587 | `prismarine_slab` | slab | prismarine | opaque | anim | 14721–14726 | — |
| 588 | `prismarine_brick_slab` | slab | prismarine_bricks | opaque |  | 14727–14732 | — |
| 589 | `dark_prismarine_slab` | slab | dark_prismarine | opaque |  | 14733–14738 | — |
| 590 | `sea_lantern` | full cube | sea_lantern | opaque | anim | 14739 | Ocean monuments and some ocean ruins |
| 591 | `hay_block` | full cube | ends: hay_block_top · side: hay_block_side | opaque | axis | 14740–14742 | Hay piles in plains villages (less often savanna and desert villages); pillager outposts; abandoned camps |
| 608 | `terracotta` | full cube | terracotta | opaque |  | 14759 | Badlands layers; desert and plains village houses and lamps — Y above about 74 (badlands layers) |
| 609 | `coal_block` | full cube | coal_block | opaque |  | 14760 | — |
| 610 | `packed_ice` | full cube | packed_ice | opaque |  | 14761 | Ice spikes, frozen peaks, ice caves, icebergs, igloo windows, some snowy village houses |
| 611 | `sunflower` | model | sunflower_bottom · sunflower_top · sunflower_back · sunflower_front | cutout |  | 14762–14763 | — |
| 612 | `lilac` | model | lilac_bottom · lilac_top | cutout |  | 14764–14765 | — |
| 613 | `rose_bush` | model | rose_bush_bottom · rose_bush_top | cutout |  | 14766–14767 | — |
| 614 | `peony` | model | peony_bottom · peony_top | cutout |  | 14768–14769 | — |
| 615 | `tall_grass` | model | tall_grass_bottom · tall_grass_top | cutout | tint | 14770–14771 | — |
| 616 | `large_fern` | model | large_fern_bottom · large_fern_top | cutout | tint | 14772–14773 | — |
| 649 | `red_sandstone` | full cube | top: red_sandstone_top · bottom: red_sandstone_bottom · side: red_sandstone | opaque |  | 15094 | Floating badlands cave entrances, replacing red sand |
| 650 | `chiseled_red_sandstone` | full cube | ends: red_sandstone_top · side: chiseled_red_sandstone | opaque |  | 15095 | — |
| 651 | `cut_red_sandstone` | full cube | ends: red_sandstone_top · side: cut_red_sandstone | opaque |  | 15096 | — |
| 653 | `oak_slab` | slab | oak_planks | opaque |  | 15177–15182 | — |
| 654 | `spruce_slab` | slab | spruce_planks | opaque |  | 15183–15188 | — |
| 655 | `birch_slab` | slab | birch_planks | opaque |  | 15189–15194 | — |
| 656 | `jungle_slab` | slab | jungle_planks | opaque |  | 15195–15200 | — |
| 657 | `acacia_slab` | slab | acacia_planks | opaque |  | 15201–15206 | — |
| 658 | `cherry_slab` | slab | cherry_planks | opaque |  | 15207–15212 | — |
| 659 | `dark_oak_slab` | slab | dark_oak_planks | opaque |  | 15213–15218 | — |
| 660 | `pale_oak_slab` | slab | pale_oak_planks | opaque |  | 15219–15224 | — |
| 661 | `mangrove_slab` | slab | mangrove_planks | opaque |  | 15225–15230 | — |
| 662 | `poplar_slab` | slab | poplar_planks | opaque |  | 15231–15236 | — |
| 663 | `bamboo_slab` | slab | bamboo_planks | opaque |  | 15237–15242 | — |
| 664 | `bamboo_mosaic_slab` | slab | bamboo_mosaic | opaque |  | 15243–15248 | — |
| 665 | `stone_slab` | slab | stone | opaque |  | 15249–15254 | — |
| 666 | `sandstone_slab` | slab | top: sandstone_top · bottom: sandstone_bottom · side: sandstone | opaque |  | 15255–15260 | Desert pyramids, desert villages, desert wells |
| 667 | `cut_sandstone_slab` | slab | ends: sandstone_top · side: cut_sandstone | opaque |  | 15261–15266 | — |
| 668 | `petrified_oak_slab` | slab | oak_planks | opaque |  | 15267–15272 | — |
| 669 | `cobblestone_slab` | slab | cobblestone | opaque |  | 15273–15278 | Plains villages, pillager outposts, woodland mansions |
| 670 | `brick_slab` | slab | bricks | opaque |  | 15279–15284 | — |
| 671 | `stone_brick_slab` | slab | stone_bricks | opaque |  | 15285–15290 | Overworld ruined portals and strongholds |
| 672 | `mud_brick_slab` | slab | mud_bricks | opaque |  | 15291–15296 | Trail ruins |
| 673 | `nether_brick_slab` | slab | nether_bricks | opaque |  | 15297–15302 | — |
| 674 | `quartz_slab` | slab | ends: quartz_block_top · side: quartz_block_side | opaque |  | 15303–15308 | — |
| 675 | `red_sandstone_slab` | slab | top: red_sandstone_top · bottom: red_sandstone_bottom · side: red_sandstone | opaque |  | 15309–15314 | — |
| 676 | `cut_red_sandstone_slab` | slab | ends: red_sandstone_top · side: cut_red_sandstone | opaque |  | 15315–15320 | — |
| 677 | `smooth_stone` | full cube | smooth_stone | opaque |  | 15321 | — |
| 678 | `smooth_stone_slab` | slab | ends: smooth_stone · side: smooth_stone_slab_side | opaque |  | 15322–15327 | — |
| 679 | `smooth_sandstone` | full cube | sandstone_top | opaque |  | 15328 | Desert villages |
| 680 | `smooth_quartz` | full cube | quartz_block_bottom | opaque |  | 15329 | — |
| 681 | `smooth_red_sandstone` | full cube | red_sandstone_top | opaque |  | 15330 | — |
| 715 | `purpur_block` | full cube | purpur_block | opaque |  | 16687 | End cities and End ships |
| 716 | `purpur_slab` | slab | purpur_block | opaque |  | 16688–16693 | — |
| 717 | `purpur_pillar` | full cube | ends: purpur_pillar_top · side: purpur_pillar_side | opaque | axis | 16694–16696 | End cities and End ships |
| 719 | `end_stone_bricks` | full cube | end_stone_bricks | opaque |  | 16777 | End cities and End ships |
| 726 | `repeating_command_block` | full cube | down: repeating_command_block_side · up: repeating_command_block_side · north: repeating_command_block_front · south: repeating_command_block_back · east: repeating_command_block_side · west: repeating_command_block_side | opaque | anim, conditional,facing | 16798–16809 | — |
| 727 | `chain_command_block` | full cube | down: chain_command_block_side · up: chain_command_block_side · north: chain_command_block_front · south: chain_command_block_back · east: chain_command_block_side · west: chain_command_block_side | opaque | anim, conditional,facing | 16810–16821 | — |
| 728 | `frosted_ice` | full cube | frosted_ice_0 | translucent | age | 16822–16825 | — |
| 729 | `magma_block` | full cube | magma | opaque | anim | 16826 | Overworld: underwater clusters at aquifer bottoms, sulfur springs, ocean ruins, ruined portals outside cold biomes, trial chamber eruption rooms. Nether: magma ore features on netherrack — Y 27..36 (Nether magma feature) |
| 730 | `nether_wart_block` | full cube | nether_wart_block | opaque |  | 16827 | Huge crimson fungi in crimson forests |
| 731 | `red_nether_bricks` | full cube | red_nether_bricks | opaque |  | 16828 | — |
| 732 | `bone_block` | full cube | ends: bone_block_top · side: bone_block_side | opaque | axis | 16829–16831 | Fossils in desert, swamp and mangrove swamp biomes; nether fossils; trial chamber spawners — Y -64..-8 (lower fossils) and 0..319 (upper fossils); nether fossils in soul sand valleys |
| 734 | `observer` | full cube | down: observer_top · up: observer_top · north: observer_front · south: observer_back · east: observer_side · west: observer_side | opaque | facing,powered | 16833–16844 | — |
| 752 | `white_glazed_terracotta` | full cube | white_glazed_terracotta | opaque | facing | 16947–16950 | — |
| 753 | `orange_glazed_terracotta` | full cube | orange_glazed_terracotta | opaque | facing | 16951–16954 | — |
| 754 | `magenta_glazed_terracotta` | full cube | magenta_glazed_terracotta | opaque | facing | 16955–16958 | — |
| 755 | `light_blue_glazed_terracotta` | full cube | light_blue_glazed_terracotta | opaque | facing | 16959–16962 | — |
| 756 | `yellow_glazed_terracotta` | full cube | yellow_glazed_terracotta | opaque | facing | 16963–16966 | — |
| 757 | `lime_glazed_terracotta` | full cube | lime_glazed_terracotta | opaque | facing | 16967–16970 | — |
| 758 | `pink_glazed_terracotta` | full cube | pink_glazed_terracotta | opaque | facing | 16971–16974 | — |
| 759 | `gray_glazed_terracotta` | full cube | gray_glazed_terracotta | opaque | facing | 16975–16978 | — |
| 760 | `light_gray_glazed_terracotta` | full cube | light_gray_glazed_terracotta | opaque | facing | 16979–16982 | — |
| 761 | `cyan_glazed_terracotta` | full cube | cyan_glazed_terracotta | opaque | facing | 16983–16986 | — |
| 762 | `purple_glazed_terracotta` | full cube | purple_glazed_terracotta | opaque | facing | 16987–16990 | — |
| 763 | `blue_glazed_terracotta` | full cube | blue_glazed_terracotta | opaque | facing | 16991–16994 | — |
| 764 | `brown_glazed_terracotta` | full cube | brown_glazed_terracotta | opaque | facing | 16995–16998 | — |
| 765 | `green_glazed_terracotta` | full cube | green_glazed_terracotta | opaque | facing | 16999–17002 | — |
| 766 | `red_glazed_terracotta` | full cube | red_glazed_terracotta | opaque | facing | 17003–17006 | — |
| 767 | `black_glazed_terracotta` | full cube | black_glazed_terracotta | opaque | facing | 17007–17010 | — |
| 768 | `white_concrete` | full cube | white_concrete | opaque |  | 17011 | — |
| 769 | `orange_concrete` | full cube | orange_concrete | opaque |  | 17012 | — |
| 770 | `magenta_concrete` | full cube | magenta_concrete | opaque |  | 17013 | — |
| 771 | `light_blue_concrete` | full cube | light_blue_concrete | opaque |  | 17014 | — |
| 772 | `yellow_concrete` | full cube | yellow_concrete | opaque |  | 17015 | — |
| 773 | `lime_concrete` | full cube | lime_concrete | opaque |  | 17016 | — |
| 774 | `pink_concrete` | full cube | pink_concrete | opaque |  | 17017 | — |
| 775 | `gray_concrete` | full cube | gray_concrete | opaque |  | 17018 | — |
| 776 | `light_gray_concrete` | full cube | light_gray_concrete | opaque |  | 17019 | — |
| 777 | `cyan_concrete` | full cube | cyan_concrete | opaque |  | 17020 | — |
| 778 | `purple_concrete` | full cube | purple_concrete | opaque |  | 17021 | — |
| 779 | `blue_concrete` | full cube | blue_concrete | opaque |  | 17022 | — |
| 780 | `brown_concrete` | full cube | brown_concrete | opaque |  | 17023 | — |
| 781 | `green_concrete` | full cube | green_concrete | opaque |  | 17024 | — |
| 782 | `red_concrete` | full cube | red_concrete | opaque |  | 17025 | — |
| 783 | `black_concrete` | full cube | black_concrete | opaque |  | 17026 | — |
| 800 | `white_concrete_slab` | slab | white_concrete | opaque |  | 18307–18312 | — |
| 801 | `orange_concrete_slab` | slab | orange_concrete | opaque |  | 18313–18318 | — |
| 802 | `magenta_concrete_slab` | slab | magenta_concrete | opaque |  | 18319–18324 | — |
| 803 | `light_blue_concrete_slab` | slab | light_blue_concrete | opaque |  | 18325–18330 | — |
| 804 | `yellow_concrete_slab` | slab | yellow_concrete | opaque |  | 18331–18336 | — |
| 805 | `lime_concrete_slab` | slab | lime_concrete | opaque |  | 18337–18342 | — |
| 806 | `pink_concrete_slab` | slab | pink_concrete | opaque |  | 18343–18348 | — |
| 807 | `gray_concrete_slab` | slab | gray_concrete | opaque |  | 18349–18354 | — |
| 808 | `light_gray_concrete_slab` | slab | light_gray_concrete | opaque |  | 18355–18360 | — |
| 809 | `cyan_concrete_slab` | slab | cyan_concrete | opaque |  | 18361–18366 | — |
| 810 | `purple_concrete_slab` | slab | purple_concrete | opaque |  | 18367–18372 | — |
| 811 | `blue_concrete_slab` | slab | blue_concrete | opaque |  | 18373–18378 | — |
| 812 | `brown_concrete_slab` | slab | brown_concrete | opaque |  | 18379–18384 | — |
| 813 | `green_concrete_slab` | slab | green_concrete | opaque |  | 18385–18390 | — |
| 814 | `red_concrete_slab` | slab | red_concrete | opaque |  | 18391–18396 | — |
| 815 | `black_concrete_slab` | slab | black_concrete | opaque |  | 18397–18402 | — |
| 816 | `white_concrete_powder` | full cube | white_concrete_powder | opaque | rand-rot | 18403 | — |
| 817 | `orange_concrete_powder` | full cube | orange_concrete_powder | opaque | rand-rot | 18404 | — |
| 818 | `magenta_concrete_powder` | full cube | magenta_concrete_powder | opaque | rand-rot | 18405 | — |
| 819 | `light_blue_concrete_powder` | full cube | light_blue_concrete_powder | opaque | rand-rot | 18406 | — |
| 820 | `yellow_concrete_powder` | full cube | yellow_concrete_powder | opaque | rand-rot | 18407 | — |
| 821 | `lime_concrete_powder` | full cube | lime_concrete_powder | opaque | rand-rot | 18408 | — |
| 822 | `pink_concrete_powder` | full cube | pink_concrete_powder | opaque | rand-rot | 18409 | — |
| 823 | `gray_concrete_powder` | full cube | gray_concrete_powder | opaque | rand-rot | 18410 | — |
| 824 | `light_gray_concrete_powder` | full cube | light_gray_concrete_powder | opaque | rand-rot | 18411 | — |
| 825 | `cyan_concrete_powder` | full cube | cyan_concrete_powder | opaque | rand-rot | 18412 | — |
| 826 | `purple_concrete_powder` | full cube | purple_concrete_powder | opaque | rand-rot | 18413 | — |
| 827 | `blue_concrete_powder` | full cube | blue_concrete_powder | opaque | rand-rot | 18414 | — |
| 828 | `brown_concrete_powder` | full cube | brown_concrete_powder | opaque | rand-rot | 18415 | — |
| 829 | `green_concrete_powder` | full cube | green_concrete_powder | opaque | rand-rot | 18416 | — |
| 830 | `red_concrete_powder` | full cube | red_concrete_powder | opaque | rand-rot | 18417 | — |
| 831 | `black_concrete_powder` | full cube | black_concrete_powder | opaque | rand-rot | 18418 | — |
| 832 | `kelp` | model | kelp | cutout | anim | 18419–18444 | — |
| 833 | `kelp_plant` | model | kelp_plant | cutout | anim | 18445 | — |
| 834 | `dried_kelp_block` | full cube | top: dried_kelp_top · bottom: dried_kelp_bottom · side: dried_kelp_side | opaque |  | 18446 | — |
| 838 | `dead_tube_coral_block` | full cube | dead_tube_coral_block | opaque |  | 18494 | — |
| 839 | `dead_brain_coral_block` | full cube | dead_brain_coral_block | opaque |  | 18495 | — |
| 840 | `dead_bubble_coral_block` | full cube | dead_bubble_coral_block | opaque |  | 18496 | — |
| 841 | `dead_fire_coral_block` | full cube | dead_fire_coral_block | opaque |  | 18497 | — |
| 842 | `dead_horn_coral_block` | full cube | dead_horn_coral_block | opaque |  | 18498 | — |
| 843 | `tube_coral_block` | full cube | tube_coral_block | opaque |  | 18499 | — |
| 844 | `brain_coral_block` | full cube | brain_coral_block | opaque |  | 18500 | — |
| 845 | `bubble_coral_block` | full cube | bubble_coral_block | opaque |  | 18501 | — |
| 846 | `fire_coral_block` | full cube | fire_coral_block | opaque |  | 18502 | — |
| 847 | `horn_coral_block` | full cube | horn_coral_block | opaque |  | 18503 | — |
| 853 | `tube_coral` | model | tube_coral | cutout |  | 18514–18515 | — |
| 854 | `brain_coral` | model | brain_coral | cutout |  | 18516–18517 | — |
| 855 | `bubble_coral` | model | bubble_coral | cutout |  | 18518–18519 | — |
| 856 | `fire_coral` | model | fire_coral | cutout |  | 18520–18521 | — |
| 857 | `horn_coral` | model | horn_coral | cutout |  | 18522–18523 | — |
| 863 | `tube_coral_fan` | model | tube_coral_fan | cutout |  | 18534–18535 | — |
| 864 | `brain_coral_fan` | model | brain_coral_fan | cutout |  | 18536–18537 | — |
| 865 | `bubble_coral_fan` | model | bubble_coral_fan | cutout |  | 18538–18539 | — |
| 866 | `fire_coral_fan` | model | fire_coral_fan | cutout |  | 18540–18541 | — |
| 867 | `horn_coral_fan` | model | horn_coral_fan | cutout |  | 18542–18543 | — |
| 873 | `tube_coral_wall_fan` | model | tube_coral_fan | cutout |  | 18584–18591 | — |
| 874 | `brain_coral_wall_fan` | model | brain_coral_fan | cutout |  | 18592–18599 | — |
| 875 | `bubble_coral_wall_fan` | model | bubble_coral_fan | cutout |  | 18600–18607 | — |
| 876 | `fire_coral_wall_fan` | model | fire_coral_fan | cutout |  | 18608–18615 | — |
| 877 | `horn_coral_wall_fan` | model | horn_coral_fan | cutout |  | 18616–18623 | — |
| 878 | `sea_pickle` | model | sea_pickle | cutout | rand-rot | 18624–18631 | — |
| 879 | `blue_ice` | full cube | blue_ice | opaque |  | 18632 | Bottoms of icebergs and arches in frozen oceans; piles in snowy village houses; ancient cities — Y 30..61 (blue ice placement in frozen oceans) |
| 882 | `bamboo` | model | bamboo_stalk · bamboo_small_leaves · bamboo_large_leaves | cutout | tint, rand-rot | 18636–18647 | — |
| 901 | `polished_granite_slab` | slab | polished_granite | opaque |  | 19773–19778 | — |
| 902 | `smooth_red_sandstone_slab` | slab | red_sandstone_top | opaque |  | 19779–19784 | — |
| 903 | `mossy_stone_brick_slab` | slab | mossy_stone_bricks | opaque |  | 19785–19790 | — |
| 904 | `polished_diorite_slab` | slab | polished_diorite | opaque |  | 19791–19796 | — |
| 905 | `mossy_cobblestone_slab` | slab | mossy_cobblestone | opaque |  | 19797–19802 | — |
| 906 | `end_stone_brick_slab` | slab | end_stone_bricks | opaque |  | 19803–19808 | — |
| 907 | `smooth_sandstone_slab` | slab | sandstone_top | opaque |  | 19809–19814 | — |
| 908 | `smooth_quartz_slab` | slab | quartz_block_bottom | opaque |  | 19815–19820 | — |
| 909 | `granite_slab` | slab | granite | opaque |  | 19821–19826 | — |
| 910 | `andesite_slab` | slab | andesite | opaque |  | 19827–19832 | — |
| 911 | `red_nether_brick_slab` | slab | red_nether_bricks | opaque |  | 19833–19838 | — |
| 912 | `polished_andesite_slab` | slab | polished_andesite | opaque |  | 19839–19844 | — |
| 913 | `diorite_slab` | slab | diorite | opaque |  | 19845–19850 | — |
| 928 | `loom` | full cube | down: loom_bottom · up: loom_top · north: loom_front · south: loom_side · east: loom_side · west: loom_side | opaque | facing | 24095–24098 | — |
| 929 | `barrel` | full cube | top: barrel_top · bottom: barrel_bottom · side: barrel_side | opaque | facing,open | 24099–24110 | — |
| 930 | `smoker` | full cube | down: smoker_bottom · up: smoker_top · north: smoker_front · south: smoker_side · east: smoker_side · west: smoker_side | opaque | anim, facing,lit | 24111–24118 | — |
| 931 | `blast_furnace` | full cube | down: blast_furnace_top · up: blast_furnace_top · north: blast_furnace_front · south: blast_furnace_side · east: blast_furnace_side · west: blast_furnace_side | opaque | anim, facing,lit | 24119–24126 | — |
| 932 | `cartography_table` | full cube | down: dark_oak_planks · up: cartography_table_top · north: cartography_table_side3 · south: cartography_table_side1 · east: cartography_table_side3 · west: cartography_table_side2 | opaque |  | 24127 | — |
| 933 | `fletching_table` | full cube | down: birch_planks · up: fletching_table_top · north: fletching_table_front · south: fletching_table_front · east: fletching_table_side · west: fletching_table_side | opaque |  | 24128 | — |
| 936 | `smithing_table` | full cube | down: smithing_table_bottom · up: smithing_table_top · north: smithing_table_front · south: smithing_table_front · east: smithing_table_side · west: smithing_table_side | opaque |  | 24157 | — |
| 951 | `sweet_berry_bush` | model | sweet_berry_bush_stage0 · sweet_berry_bush_stage1 · sweet_berry_bush_stage2 · sweet_berry_bush_stage3 | cutout |  | 24298–24301 | — |
| 952 | `warped_stem` | full cube | ends: warped_stem_top · side: warped_stem | opaque | anim, axis | 24302–24304 | Trunks of huge warped fungi in warped forests |
| 953 | `stripped_warped_stem` | full cube | ends: stripped_warped_stem_top · side: stripped_warped_stem | opaque | axis | 24305–24307 | — |
| 954 | `warped_hyphae` | full cube | warped_stem | opaque | anim, axis | 24308–24310 | — |
| 955 | `stripped_warped_hyphae` | full cube | stripped_warped_stem | opaque | axis | 24311–24313 | — |
| 956 | `warped_nylium` | full cube | top: warped_nylium · bottom: netherrack · side: warped_nylium_side | opaque |  | 24314 | Warped forest surface |
| 958 | `warped_wart_block` | full cube | warped_wart_block | opaque |  | 24316 | Huge warped fungi in warped forests |
| 961 | `crimson_stem` | full cube | ends: crimson_stem_top · side: crimson_stem | opaque | anim, axis | 24319–24321 | Trunks of huge crimson fungi in crimson forests |
| 962 | `stripped_crimson_stem` | full cube | ends: stripped_crimson_stem_top · side: stripped_crimson_stem | opaque | axis | 24322–24324 | — |
| 963 | `crimson_hyphae` | full cube | crimson_stem | opaque | anim, axis | 24325–24327 | — |
| 964 | `stripped_crimson_hyphae` | full cube | stripped_crimson_stem | opaque | axis | 24328–24330 | — |
| 965 | `crimson_nylium` | full cube | top: crimson_nylium · bottom: netherrack · side: crimson_nylium_side | opaque |  | 24331 | Crimson forest surface |
| 967 | `shroomlight` | full cube | shroomlight | opaque |  | 24333 | Upper parts of huge crimson and warped fungi |
| 973 | `crimson_planks` | full cube | crimson_planks | opaque |  | 24389 | — |
| 974 | `warped_planks` | full cube | warped_planks | opaque |  | 24390 | — |
| 975 | `crimson_slab` | slab | crimson_planks | opaque |  | 24391–24396 | — |
| 976 | `warped_slab` | slab | warped_planks | opaque |  | 24397–24402 | — |
| 995 | `structure_block` | full cube | structure_block_corner | opaque | mode | 25079–25082 | — |
| 996 | `jigsaw` | full cube | down: jigsaw_side · up: jigsaw_lock · north: jigsaw_top · south: jigsaw_bottom · east: jigsaw_side · west: jigsaw_side | opaque | orientation | 25083–25094 | — |
| 997 | `test_block` | full cube | test_block_accept | opaque | mode | 25095–25098 | — |
| 998 | `test_instance_block` | full cube | test_instance_block | opaque |  | 25099 | — |
| 1000 | `target` | full cube | ends: target_top · side: target_side | opaque |  | 25109–25124 | — |
| 1001 | `bee_nest` | full cube | down: bee_nest_bottom · up: bee_nest_top · north: bee_nest_front · south: bee_nest_side · east: bee_nest_side · west: bee_nest_side | opaque | facing,honey_level | 25125–25148 | Bee trees in forests, flower forests and plains |
| 1002 | `beehive` | full cube | down: beehive_end · up: beehive_end · north: beehive_front · south: beehive_side · east: beehive_side · west: beehive_side | opaque | facing,honey_level | 25149–25172 | — |
| 1004 | `honeycomb_block` | full cube | honeycomb_block | opaque |  | 25174 | — |
| 1005 | `netherite_block` | full cube | netherite_block | opaque |  | 25175 | — |
| 1006 | `ancient_debris` | full cube | ends: ancient_debris_top · side: ancient_debris_side | opaque |  | 25176 | Scattered in netherrack, basalt and blackstone, never exposed to air — Y 8..24 (peak 16) + 8..248 |
| 1007 | `crying_obsidian` | full cube | crying_obsidian | opaque |  | 25177 | Some ruined portals (about 15% of their obsidian) |
| 1008 | `respawn_anchor` | full cube | top: respawn_anchor_top_off · bottom: respawn_anchor_bottom · side: respawn_anchor_side0 | opaque | anim, charges | 25178–25182 | — |
| 1013 | `lodestone` | full cube | ends: lodestone_top · side: lodestone_side | opaque |  | 25187 | — |
| 1014 | `blackstone` | full cube | ends: blackstone_top · side: blackstone | opaque |  | 25188 | Basalt deltas, below lava seas, underground blobs, bastion remnants — Y 5..36 (ore blobs); nether terrain |
| 1017 | `blackstone_slab` | slab | ends: blackstone_top · side: blackstone | opaque |  | 25593–25598 | — |
| 1018 | `polished_blackstone` | full cube | polished_blackstone | opaque |  | 25599 | Nether ruined portals |
| 1019 | `polished_blackstone_bricks` | full cube | polished_blackstone_bricks | opaque |  | 25600 | Bastion remnants and Nether ruined portals |
| 1020 | `cracked_polished_blackstone_bricks` | full cube | cracked_polished_blackstone_bricks | opaque |  | 25601 | Bastion remnants and Nether ruined portals |
| 1021 | `chiseled_polished_blackstone` | full cube | chiseled_polished_blackstone | opaque |  | 25602 | Bastion remnants and Nether ruined portals |
| 1022 | `polished_blackstone_brick_slab` | slab | polished_blackstone_bricks | opaque |  | 25603–25608 | Nether ruined portals |
| 1025 | `gilded_blackstone` | full cube | gilded_blackstone | opaque |  | 26013 | Bastion remnants, replacing some blackstone |
| 1027 | `polished_blackstone_slab` | slab | polished_blackstone | opaque |  | 26094–26099 | — |
| 1031 | `chiseled_nether_bricks` | full cube | chiseled_nether_bricks | opaque |  | 26450 | — |
| 1032 | `cracked_nether_bricks` | full cube | cracked_nether_bricks | opaque |  | 26451 | — |
| 1033 | `quartz_bricks` | full cube | quartz_bricks | opaque |  | 26452 | — |
| 1068 | `amethyst_block` | full cube | amethyst_block | opaque |  | 26759 | Inner layer of amethyst geodes — Y -58..30 (amethyst geodes) |
| 1069 | `budding_amethyst` | full cube | budding_amethyst | opaque |  | 26760 | Replaces about 8% of the inner amethyst layer of geodes — Y -58..30 (amethyst geodes) |
| 1070 | `amethyst_cluster` | model | amethyst_cluster | cutout |  | 26761–26772 | — |
| 1071 | `large_amethyst_bud` | model | large_amethyst_bud | cutout |  | 26773–26784 | — |
| 1072 | `medium_amethyst_bud` | model | medium_amethyst_bud | cutout |  | 26785–26796 | — |
| 1073 | `small_amethyst_bud` | model | small_amethyst_bud | cutout |  | 26797–26808 | — |
| 1074 | `tuff` | full cube | tuff | opaque |  | 26809 | Ore blobs below Y=0 in all overworld biomes; filler of the iron ore vein; sulfur springs — Y -64..0 (ore blobs) |
| 1075 | `tuff_slab` | slab | tuff | opaque |  | 26810–26815 | — |
| 1078 | `polished_tuff` | full cube | polished_tuff | opaque |  | 27220 | Trial chambers (primary ceiling block of many rooms) |
| 1079 | `polished_tuff_slab` | slab | polished_tuff | opaque |  | 27221–27226 | — |
| 1082 | `chiseled_tuff` | full cube | ends: chiseled_tuff_top · side: chiseled_tuff | opaque |  | 27631 | Trial chambers |
| 1083 | `tuff_bricks` | full cube | tuff_bricks | opaque |  | 27632 | Trial chambers (ceiling and wall block) |
| 1084 | `tuff_brick_slab` | slab | tuff_bricks | opaque |  | 27633–27638 | — |
| 1087 | `chiseled_tuff_bricks` | full cube | ends: chiseled_tuff_bricks_top · side: chiseled_tuff_bricks | opaque |  | 28043 | Trial chambers |
| 1088 | `sulfur` | full cube | sulfur | opaque |  | 28044 | — |
| 1089 | `potent_sulfur` | full cube | potent_sulfur | opaque |  | 28045–28049 | — |
| 1090 | `sulfur_slab` | slab | sulfur | opaque |  | 28050–28055 | — |
| 1093 | `polished_sulfur` | full cube | polished_sulfur | opaque |  | 28460 | — |
| 1094 | `polished_sulfur_slab` | slab | polished_sulfur | opaque |  | 28461–28466 | — |
| 1097 | `sulfur_bricks` | full cube | sulfur_bricks | opaque |  | 28871 | — |
| 1098 | `sulfur_brick_slab` | slab | sulfur_bricks | opaque |  | 28872–28877 | — |
| 1101 | `chiseled_sulfur` | full cube | chiseled_sulfur | opaque |  | 29282 | — |
| 1102 | `cinnabar` | full cube | cinnabar | opaque |  | 29283 | — |
| 1103 | `cinnabar_slab` | slab | cinnabar | opaque |  | 29284–29289 | — |
| 1106 | `polished_cinnabar` | full cube | polished_cinnabar | opaque |  | 29694 | — |
| 1107 | `polished_cinnabar_slab` | slab | polished_cinnabar | opaque |  | 29695–29700 | — |
| 1110 | `cinnabar_bricks` | full cube | cinnabar_bricks | opaque |  | 30105 | — |
| 1111 | `cinnabar_brick_slab` | slab | cinnabar_bricks | opaque |  | 30106–30111 | — |
| 1114 | `chiseled_cinnabar` | full cube | chiseled_cinnabar | opaque |  | 30516 | — |
| 1115 | `calcite` | full cube | calcite | opaque |  | 30517 | Middle layer of amethyst geodes; striped in stony peaks — Y -58..30 (amethyst geodes); stony peaks surface strips |
| 1116 | `tinted_glass` | full cube | tinted_glass | translucent |  | 30518 | — |
| 1117 | `powder_snow` | full cube | powder_snow | opaque |  | 30519 | Strips in groves and snowy slopes; trial chamber rooms |
| 1120 | `sculk` | full cube | sculk | opaque | anim, rand-rot | 31000 | Sculk patches in the deep dark biome, plus sculk veins — Y -64..256 (deep dark sculk patch placement) |
| 1121 | `sculk_vein` | model | sculk_vein | cutout | anim | 31001–31128 | — |
| 1122 | `sculk_catalyst` | full cube | top: sculk_catalyst_top · bottom: sculk_catalyst_bottom · side: sculk_catalyst_side | opaque | anim, bloom | 31129–31130 | Deep dark biome and ancient cities — Y -64..256 (deep dark sculk patch placement) |
| 1124 | `copper_block` | full cube | copper_block | opaque |  | 31139 | Trial chambers |
| 1125 | `exposed_copper` | full cube | exposed_copper | opaque |  | 31140 | — |
| 1126 | `weathered_copper` | full cube | weathered_copper | opaque |  | 31141 | — |
| 1127 | `oxidized_copper` | full cube | oxidized_copper | opaque |  | 31142 | — |
| 1128 | `waxed_copper_block` | full cube | copper_block | opaque |  | 31143 | Trial chambers |
| 1129 | `waxed_exposed_copper` | full cube | exposed_copper | opaque |  | 31144 | — |
| 1130 | `waxed_weathered_copper` | full cube | weathered_copper | opaque |  | 31145 | — |
| 1131 | `waxed_oxidized_copper` | full cube | oxidized_copper | opaque |  | 31146 | Trial chambers |
| 1132 | `copper_ore` | full cube | copper_ore | opaque |  | 31147 | Ore blobs across the overworld except dripstone caves (larger blobs there); copper ore vein with granite filler — Y -16..112 (peak 48) |
| 1133 | `deepslate_copper_ore` | full cube | deepslate_copper_ore | opaque |  | 31148 | Copper ore in the deepslate layer below Y=0 — Y -16..112 (peak 48) (where it replaces deepslate, ~Y<8) |
| 1134 | `cut_copper` | full cube | cut_copper | opaque |  | 31149 | — |
| 1135 | `exposed_cut_copper` | full cube | exposed_cut_copper | opaque |  | 31150 | — |
| 1136 | `weathered_cut_copper` | full cube | weathered_cut_copper | opaque |  | 31151 | — |
| 1137 | `oxidized_cut_copper` | full cube | oxidized_cut_copper | opaque |  | 31152 | Trial chambers |
| 1138 | `waxed_cut_copper` | full cube | cut_copper | opaque |  | 31153 | Trial chambers |
| 1139 | `waxed_exposed_cut_copper` | full cube | exposed_cut_copper | opaque |  | 31154 | — |
| 1140 | `waxed_weathered_cut_copper` | full cube | weathered_cut_copper | opaque |  | 31155 | — |
| 1141 | `waxed_oxidized_cut_copper` | full cube | oxidized_cut_copper | opaque |  | 31156 | Trial chambers |
| 1142 | `chiseled_copper` | full cube | chiseled_copper | opaque |  | 31157 | — |
| 1143 | `exposed_chiseled_copper` | full cube | exposed_chiseled_copper | opaque |  | 31158 | — |
| 1144 | `weathered_chiseled_copper` | full cube | weathered_chiseled_copper | opaque |  | 31159 | — |
| 1145 | `oxidized_chiseled_copper` | full cube | oxidized_chiseled_copper | opaque |  | 31160 | — |
| 1146 | `waxed_chiseled_copper` | full cube | chiseled_copper | opaque |  | 31161 | — |
| 1147 | `waxed_exposed_chiseled_copper` | full cube | exposed_chiseled_copper | opaque |  | 31162 | — |
| 1148 | `waxed_weathered_chiseled_copper` | full cube | weathered_chiseled_copper | opaque |  | 31163 | — |
| 1149 | `waxed_oxidized_chiseled_copper` | full cube | oxidized_chiseled_copper | opaque |  | 31164 | — |
| 1158 | `cut_copper_slab` | slab | cut_copper | opaque |  | 31805–31810 | — |
| 1159 | `exposed_cut_copper_slab` | slab | exposed_cut_copper | opaque |  | 31811–31816 | — |
| 1160 | `weathered_cut_copper_slab` | slab | weathered_cut_copper | opaque |  | 31817–31822 | — |
| 1161 | `oxidized_cut_copper_slab` | slab | oxidized_cut_copper | opaque |  | 31823–31828 | — |
| 1162 | `waxed_cut_copper_slab` | slab | cut_copper | opaque |  | 31829–31834 | — |
| 1163 | `waxed_exposed_cut_copper_slab` | slab | exposed_cut_copper | opaque |  | 31835–31840 | — |
| 1164 | `waxed_weathered_cut_copper_slab` | slab | weathered_cut_copper | opaque |  | 31841–31846 | — |
| 1165 | `waxed_oxidized_cut_copper_slab` | slab | oxidized_cut_copper | opaque |  | 31847–31852 | — |
| 1182 | `copper_grate` | full cube | copper_grate | cutout |  | 32877–32878 | — |
| 1183 | `exposed_copper_grate` | full cube | exposed_copper_grate | cutout |  | 32879–32880 | — |
| 1184 | `weathered_copper_grate` | full cube | weathered_copper_grate | cutout |  | 32881–32882 | — |
| 1185 | `oxidized_copper_grate` | full cube | oxidized_copper_grate | cutout |  | 32883–32884 | — |
| 1186 | `waxed_copper_grate` | full cube | copper_grate | cutout |  | 32885–32886 | — |
| 1187 | `waxed_exposed_copper_grate` | full cube | exposed_copper_grate | cutout |  | 32887–32888 | — |
| 1188 | `waxed_weathered_copper_grate` | full cube | weathered_copper_grate | cutout |  | 32889–32890 | — |
| 1189 | `waxed_oxidized_copper_grate` | full cube | oxidized_copper_grate | cutout |  | 32891–32892 | — |
| 1190 | `copper_bulb` | full cube | copper_bulb | opaque | lit,powered | 32893–32896 | — |
| 1191 | `exposed_copper_bulb` | full cube | exposed_copper_bulb | opaque | lit,powered | 32897–32900 | — |
| 1192 | `weathered_copper_bulb` | full cube | weathered_copper_bulb | opaque | lit,powered | 32901–32904 | — |
| 1193 | `oxidized_copper_bulb` | full cube | oxidized_copper_bulb | opaque | lit,powered | 32905–32908 | — |
| 1194 | `waxed_copper_bulb` | full cube | copper_bulb | opaque | lit,powered | 32909–32912 | — |
| 1195 | `waxed_exposed_copper_bulb` | full cube | exposed_copper_bulb | opaque | lit,powered | 32913–32916 | — |
| 1196 | `waxed_weathered_copper_bulb` | full cube | weathered_copper_bulb | opaque | lit,powered | 32917–32920 | — |
| 1197 | `waxed_oxidized_copper_bulb` | full cube | oxidized_copper_bulb | opaque | lit,powered | 32921–32924 | — |
| 1222 | `dripstone_block` | full cube | dripstone_block | opaque |  | 33565 | Dripstone caves (stalactites, stalagmites, stalagnates) — Y -64..256 (cluster placement range; cave-limited) |
| 1223 | `pointed_dripstone` | model | pointed_dripstone_down_base · pointed_dripstone_up_base · pointed_dripstone_down_frustum · pointed_dripstone_up_frustum · pointed_dripstone_down_middle · pointed_dripstone_up_middle · pointed_dripstone_down_tip · pointed_dripstone_up_tip · pointed_dripstone_down_tip_merge · pointed_dripstone_up_tip_merge | cutout |  | 33566–33585 | — |
| 1224 | `sulfur_spike` | model | sulfur_spike_down_base · sulfur_spike_up_base · sulfur_spike_down_frustum · sulfur_spike_up_frustum · sulfur_spike_down_middle · sulfur_spike_up_middle · sulfur_spike_down_tip · sulfur_spike_up_tip · sulfur_spike_down_tip_merge · sulfur_spike_up_tip_merge | cutout |  | 33586–33605 | — |
| 1225 | `cave_vines` | model | cave_vines · cave_vines_lit | cutout |  | 33606–33657 | — |
| 1226 | `cave_vines_plant` | model | cave_vines_plant · cave_vines_plant_lit | cutout |  | 33658–33659 | — |
| 1227 | `spore_blossom` | model | spore_blossom_base · spore_blossom | cutout |  | 33660 | — |
| 1228 | `azalea` | model | azalea_top · azalea_side · azalea_plant | cutout |  | 33661 | — |
| 1229 | `flowering_azalea` | model | flowering_azalea_top · flowering_azalea_side · azalea_plant | cutout |  | 33662 | — |
| 1230 | `moss_carpet` | model | moss_block | opaque |  | 33663 | — |
| 1231 | `pink_petals` | model | pink_petals · pink_petals_stem | cutout | tint | 33664–33679 | — |
| 1232 | `wildflowers` | model | wildflowers · wildflowers_stem | cutout | tint | 33680–33695 | — |
| 1233 | `leaf_litter` | model | leaf_litter | cutout | tint | 33696–33711 | — |
| 1234 | `moss_block` | full cube | moss_block | opaque |  | 33712 | Floor and ceiling of lush caves; trial chambers — Y -64..256 (lush cave vegetation placement) |
| 1235 | `big_dripleaf` | model | big_dripleaf_top · big_dripleaf_tip · big_dripleaf_side · big_dripleaf_stem | cutout |  | 33713–33744 | — |
| 1236 | `big_dripleaf_stem` | model | big_dripleaf_stem | cutout |  | 33745–33752 | — |
| 1237 | `small_dripleaf` | model | small_dripleaf_stem_bottom · small_dripleaf_top · small_dripleaf_side · small_dripleaf_stem_top | cutout |  | 33753–33768 | — |
| 1238 | `hanging_roots` | model | hanging_roots | cutout |  | 33769–33770 | — |
| 1239 | `rooted_dirt` | full cube | rooted_dirt | opaque | rand-rot | 33771 | Roots under azalea trees in lush caves — Y -64..256 (rooted azalea tree placement; extends into lush caves) |
| 1240 | `mud` | full cube | mud | opaque |  | 33772 | Mangrove swamp terrain; trail ruins; some trial chamber entrance rooms |
| 1241 | `deepslate` | full cube | ends: deepslate_top · side: deepslate | opaque | rand-rot, axis | 33773–33775 | Main solid rock below Y=0; ancient cities — Y -64..0 (gradient from Y=8 down to Y=0; fully deepslate at Y<=0) |
| 1242 | `cobbled_deepslate` | full cube | cobbled_deepslate | opaque |  | 33776 | Ancient cities and trial chambers |
| 1244 | `cobbled_deepslate_slab` | slab | cobbled_deepslate | opaque |  | 33857–33862 | — |
| 1246 | `polished_deepslate` | full cube | polished_deepslate | opaque |  | 34187 | Ancient cities |
| 1248 | `polished_deepslate_slab` | slab | polished_deepslate | opaque |  | 34268–34273 | — |
| 1250 | `deepslate_tiles` | full cube | deepslate_tiles | opaque |  | 34598 | Ancient cities |
| 1252 | `deepslate_tile_slab` | slab | deepslate_tiles | opaque |  | 34679–34684 | Ancient cities |
| 1254 | `deepslate_bricks` | full cube | deepslate_bricks | opaque |  | 35009 | Ancient cities |
| 1256 | `deepslate_brick_slab` | slab | deepslate_bricks | opaque |  | 35090–35095 | Ancient cities |
| 1258 | `chiseled_deepslate` | full cube | chiseled_deepslate | opaque |  | 35420 | Ancient cities |
| 1259 | `cracked_deepslate_bricks` | full cube | cracked_deepslate_bricks | opaque |  | 35421 | Ancient cities |
| 1260 | `cracked_deepslate_tiles` | full cube | cracked_deepslate_tiles | opaque |  | 35422 | Ancient cities |
| 1261 | `infested_deepslate` | full cube | ends: deepslate_top · side: deepslate | opaque | rand-rot, axis | 35423–35425 | Deepslate variant of the silverfish ore feature in the same mountain biomes — Y -64..63 (ore_infested placement) |
| 1262 | `smooth_basalt` | full cube | smooth_basalt | opaque |  | 35426 | Outer layer of amethyst geodes; also in ancient cities — Y -58..30 (amethyst geodes) |
| 1263 | `raw_iron_block` | full cube | raw_iron_block | opaque |  | 35427 | Replaces about 2% of iron ore inside iron ore veins — Y -60..-9 (iron ore vein) |
| 1264 | `raw_copper_block` | full cube | raw_copper_block | opaque |  | 35428 | Replaces about 2% of copper ore inside copper ore veins — Y 0..49 (copper ore vein) |
| 1265 | `raw_gold_block` | full cube | raw_gold_block | opaque |  | 35429 | — |
| 1268 | `ochre_froglight` | full cube | ends: ochre_froglight_top · side: ochre_froglight_side | opaque | axis | 35432–35434 | — |
| 1269 | `verdant_froglight` | full cube | ends: verdant_froglight_top · side: verdant_froglight_side | opaque | axis | 35435–35437 | — |
| 1270 | `pearlescent_froglight` | full cube | ends: pearlescent_froglight_top · side: pearlescent_froglight_side | opaque | axis | 35438–35440 | — |
| 1272 | `reinforced_deepslate` | full cube | top: reinforced_deepslate_top · bottom: reinforced_deepslate_bottom · side: reinforced_deepslate_side | opaque |  | 35442 | — |
| 1274 | `crafter` | full cube | down: crafter_bottom · up: crafter_top · north: crafter_north · south: crafter_south · east: crafter_east · west: crafter_west | opaque | crafting,orientation,triggered | 35459–35506 | — |
| 1278 | `pale_moss_block` | full cube | pale_moss_block | opaque |  | 35553 | Moss patches in pale gardens |
| 1279 | `pale_moss_carpet` | model | pale_moss_carpet · pale_moss_carpet_side_tall · pale_moss_carpet_side_small | cutout |  | 35554–35715 | — |
| 1280 | `pale_hanging_moss` | model | pale_hanging_moss · pale_hanging_moss_tip | cutout |  | 35716–35717 | — |
| 1282 | `closed_eyeblossom` | model | closed_eyeblossom | cutout |  | 35719 | — |
| 1285 | `firefly_bush` | model | firefly_bush · firefly_bush_emissive | cutout | anim | 35722 | — |

### Cube-like blocks left out

- **Inner geometry:** slime_block, honey_block, beacon, cactus, spawner, trial_spawner and vault (a full box plus extra elements).
- **Mixed shapes:** snow layers, dirt_path and farmland (15/16 tall).
- **Functional cubes with state-dependent shapes:** piston, sticky_piston, chiseled_bookshelf.
- **Entity-rendered:** chests, shulker boxes.

See `research\edge_cases.txt` for the full list. The three mushroom blocks *are* included: they are cubes whose six faces switch between the cap/stem texture and `mushroom_block_inside` depending on the `up/down/north/south/east/west` properties.

## 6. Chunk storage and loading (research base for the chunk loader)

What vanilla does, in the order the chunk loader will meet it.

**Layout**
- A chunk is a 16×16 column. Overworld Y −64…319 = **24 sections** of 16×16×16 (4,096 cells). Nether and End are Y 0…255.
- A `LevelChunkSection` holds a `PalettedContainer<BlockState>`, a biome container, and counters: `nonEmptyBlockCount`, `fluidCount`, `tickingBlockCount`, `tickingFluidCount` [B]. Sections that are all air or have no ticking fluids are skipped cheaply.
- **Fluid is not stored separately.** `BlockState.getFluidState()` derives it, which is why water levels and `waterlogged` are just block states [B].

**Palette** (`Strategy.createForBlockStates`) [B]:

| Distinct states in the section | Bits/entry | Palette |
|---|---|---|
| 1 | 0 | single value (e.g. a full-air or full-stone section) |
| ≤ 16 | 4 (min) | linear list |
| 17–256 | 5–8 | hash map |
| > 256 | 16 (global: ceil(log2 35,723)) | none; stores global state ids directly |

- Entries are packed into 64-bit longs and never span two longs, so a long holds `floor(64/bits)` entries.
- Cell index = `(y*16 + z)*16 + x` [old, 1.16+ format].

**On disk** (`SerializableChunkData`, region files `r.<x>.<z>.mca` with 32×32 chunks each):
- `sections[]`, each with `Y`, `block_states{palette:[{Name, Properties}], data:long[]}` and `biomes{…}`.
- Chunk-level keys: `block_ticks`, **`fluid_ticks`** (pending scheduled ticks: id, x, y, z, delay, priority), `PostProcessing`, `Status`, `InhabitedTime`, `isLightOn` [B for the keys, old for the palette sub-keys].
- Pending water ticks are **saved and restored**. Water is never "re-simulated" on load; it continues from its saved queue.

**Generation pipeline** (chunk statuses):
- `structure_starts → structure_references → biomes → noise → surface → carvers → features → initialize_light → light → spawn → full`.
- **Aquifers** (`Aquifer$NoiseBasedAquifer`) place still water and lava during `noise`. This is the sea, plus underground lakes at their own local levels.
- Lakes and springs come in `features`.
- Positions whose fluid may need to settle are recorded with `ProtoChunk.markPosForPostProcessing`. When the chunk is promoted to a full `LevelChunk`, `postProcessGeneration` **immediately ticks** each marked fluid, then updates the block and its neighbors, then clears the list [B]. That is why generated springs start flowing but oceans stay still.

**Load levels** (tickets; lower = more active):
- 31 entity-ticking → 32 **block-ticking (fluids tick here)** → 33 border (loaded and visible, frozen) → 34+ partially generated / not accessible.
- Player tickets put chunks within simulation distance at ticking levels. Render distance can exceed simulation distance, so far water is visible but frozen [B for the range check; levels old].

**Checklist for our loader**
1. Per section, store a palette of runtime state ids plus packed indices, with single-value sections stored as one id. Keep the counters, and a `tickingFluidCount` to skip still sections.
2. Water = block id + `level` (0–15) via state ids. Waterlogged = a state property. Derive fluid amount and falling from those; store nothing else.
3. Keep a scheduled-tick queue per chunk (pos, fluid, due tick, priority). Water delay is 5. Save it with the chunk, restore it on load, and process it only in block-ticking chunks.
4. Worldgen marks fluid post-processing positions; tick them once when the chunk becomes full.
5. Save palettes as string ids + properties. Build the runtime id table from the manifest at startup.
6. Meshing: cubes by face texture, with an opaque pass, a cutout pass, and a translucent pass (water and glass). The water surface height is `amount/9`, with averaged corners.

## Appendix A — Terrain feature heights (from 26.3 data)

Source: `data/minecraft/worldgen/feature/*.json` (configured features; this version's folder is `feature/`, not `configured_feature/`), `placed_feature/*.json`, `biome/*.json`, from the 26.3 client jar. Biome dimension membership from the generator's `biome_parameters` reports.

Y conversion: overworld bottom -64 / top 320; nether and end 0 / 256. `above_bottom: N` = bottom+N, `below_top: N` = top-N. Heightmap placements have no fixed Y and are listed as surface.

Note: `top` here is the exclusive end of the height range (min_y + height). The highest placeable block Y is 319 in the overworld and 255 in the nether and end, so a `below_top: 0` bound maps to 319/255 in-game.

Columns: count = `count` placement modifier (tries per chunk); chance = `rarity_filter` 1-in-N per chunk. Vein size is the configured `size` (ore/scattered_ore). Discard = `discard_chance_on_air_exposure`.

Items marked `code-defined` have no block/target data in the JSON; values are from the feature type and are not checked against source code.

#### Overworld

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

#### Nether

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

#### End

| Placed feature | Configured type | Block(s) placed | Replaces (rule test target) | Vein size | Air-exposure discard | Count / chance per chunk | Height (Y) | Biomes using it |
|---|---|---|---|---|---|---|---|---|
| `end_island_decorated` | end_island | end_stone (code-defined island) | air (code-defined) | — | — | weighted 1/2/chunk x chance 1/14 | uniform 55..70 | end: small_end_islands |
| `end_platform` | end_platform | obsidian (fixed platform) | air (code-defined) | — | — | 1 try | fixed [100, 49, 0] | end: the_end |
| `end_spike` | end_spike | obsidian (pillars; spikes list empty in JSON, generated at runtime) | air (code-defined) | — | — | 1 try | no Y modifier (surface-relative) | end: the_end |

#### Not in the table

- Trees, fallen trees, bamboo, flowers, grass/fern/patches, mushrooms (small), melons/pumpkins, cactus, sugar cane, kelp, seagrass, vines, berry bushes, sea pickles, chorus, multiface growth, root systems and bonus/structure features (monster room, end gateway/podium, desert well).
- Fluid-only springs (`spring_water`, `spring_lava`, `spring_closed`, `spring_closed_double`, `spring_open`, `spring_delta`, `spring_lava_frozen`): they place water/lava source blocks into rock; not terrain blocks.
- Dark forest vegetation (`dark_forest_vegetation`) mixes trees with huge brown/red mushrooms; the mushrooms are listed only via `mushroom_island_vegetation`.

#### Dimension types (`data/minecraft/dimension_type`)

In 26.x the dimension-type JSON has no `ultrawarm` key; that behaviour is an environment attribute (`minecraft:gameplay/water_evaporates`, `minecraft:gameplay/fast_lava`).

| Dimension type | min_y | height | logical_height | coordinate_scale | has_skylight | has_ceiling | water_evaporates attr |
|---|---|---|---|---|---|---|---|
| overworld | -64 | 384 | 384 | 1.0 | True | False | None |
| overworld_caves | -64 | 384 | 384 | 1.0 | True | True | None |
| the_end | 0 | 256 | 256 | 1.0 | True | False | None |
| the_nether | 0 | 256 | 128 | 8.0 | False | True | True |

## Appendix B — What changed in 26.1–26.3

Block-id diffs come from the 26.2 jar on this machine plus misode/mcmeta summaries. Release details are from the minecraft.wiki 26.1–26.3 pages.

**Releases:**
- **26.1 "Tiny Takeover"** (2026-03-24): +2 ids (`golden_dandelion`, `potted_golden_dandelion`). The `#dirt` tag was split into `#mud`, `#moss_blocks` and `#grass_blocks`. This is the first Java release shipped **unobfuscated** (wiki), which is why this doc can cite real class names.
- **26.2 "Chaos Cubed"** (2026-06-16): +28 ids, the **sulfur** and **cinnabar** families (`sulfur`, `polished_sulfur`, `sulfur_bricks`, `chiseled_sulfur`, the same set for cinnabar, plus slabs, stairs and walls), `potent_sulfur` and `sulfur_spike`.
  - Worldgen: new `sulfur_caves` biome, sulfur springs, and potent-sulfur geysers.
  - The wiki mentions a dripstone→"speleothem" rename, but the 26.3 **block ids are still `dripstone_block` / `pointed_dripstone`**.
- **26.3 "Wilderness Bound"** (2026-09-15): +90 ids.
  - Blocks: the **poplar** wood set (log, wood, stripped, planks, slab…) with **red/orange/yellow poplar leaves**, `red_shrub`, `shelf_mushroom`, `straw_bed`, and **stairs and slabs for all 16 concrete and 16 wool colors**.
  - Worldgen: `dappled_forest` biome, an abandoned camp structure, and a new `ores` block tag. Data folder rename: `worldgen/configured_feature/` → `worldgen/feature/`.

**Totals and what stayed the same:**
- Registry growth: 1,166 ids (1.21.11) → 1,286 (26.3), with none removed.
- **Ore heights are identical to 1.21.11.** The ore layout has not changed since the 1.18 overhaul.

**Still relevant from 1.21.x:**
- Trial chambers (tuff and copper variants) from 1.21.
- The pale garden (pale oak, pale moss, resin, creaking heart) from 1.21.4.

A third-party claim that 26.3 changed cave generation and structure spacing is **unconfirmed** by the wiki or the data.
