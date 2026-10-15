# Minecraft 26.3 block reference: full cubes, slabs, and water

As of 2026-10-08. Source of truth: vanilla Java **26.3** (`%APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar`, plus the official server jar SHA-1 `33680f5f…05f4c` for Mojang's data-generator reports). This sheet is the input for the block/texture import and the research base for the chunk loader. Only water gets behavior; every other block here just needs the correct ID, textures and render flags.

**In scope:** {{N_TOTAL}} blocks ({{N_CUBE}} full cubes, powder snow among them, + {{N_SLAB}} slabs + snow layers + {{N_MODEL}} model blocks) out of {{N_REGISTRY}} registered blocks, plus water and lava; powder snow and snow layers were added in phase 5 for the world generator. A block counts as a full cube when every model it can show is made only of `[0,0,0]→[16,16,16]` boxes. Tools and items are excluded. Render layers: {{LAYERS}}.

**Model blocks:** {{N_MODEL}} blocks are drawn from their own baked models rather than as cubes or slabs: plants, flowers, crosses, seagrass, flowerbeds, carpets, multiface and custom models, the torch, and the survival B1 families (stairs, fences, fence gates, walls, panes, doors, trapdoors, buttons, the lever, pressure plates, the ladder, nether vines, beds, chests, farmland and dirt path, crops, saplings, cauldrons and fires). The chests are drawn by a block entity in vanilla, so they get a stand-in box. See `Reference/minecraft-blocks-26.3/README.md` ("Baked models", "Family blocks (B1)", "Chest stand-in", "Shapes and offsets") for details.

## Files

The research outputs are preserved, hash-listed, in [`Reference/minecraft-blocks-26.3/`](../Reference/minecraft-blocks-26.3/README.md). That folder adds the merged import manifest (hardness, tints, names, items), every state's faces, the texture layer plan and the creative tabs. Paths below are relative to it. The research generators are in its `tools/` (`build_manifest.py` regenerates this doc from a research work folder).

| File | What it is |
|---|---|
| `block_manifest.json` | **Import input.** One entry per block in registry order. Each entry has: string id, protocol id, state-id range, properties, per-face textures, render layer, tint/animation/rotation flags, tags, and where it is found. Water is listed under `fluids`. The reference version adds hardness, tints, names and material layers; the research original is `research\block_manifest.research.json`. |
| `assets\minecraft\textures\block\*.png` (+ `.png.mcmeta`) | {{N_TEX}} block textures copied byte-for-byte from the jar, including water still/flow/overlay. |
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

{{TAG_TABLE}}

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
- **Layer:** `opaque` / `cutout` (alpha 0 or 255: {{CUTOUT}}) / `translucent` (partial alpha or forced: {{TRANSLUCENT}}). In 26.x glass models declare `force_translucent`, so all glass is translucent. Non-opaque blocks must not cull the faces of their neighbors. Exception: `grass_block` is listed as cutout only because of its side overlay; it is a solid occluding block. Leaves render opaque on "fast" graphics.
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

{{BLOCK_TABLE}}

### Cube-like blocks left out

- **Inner geometry:** slime_block, honey_block, beacon, cactus, spawner, trial_spawner and vault (a full box plus extra elements).
- **Mixed shapes:** snow layers, dirt_path and farmland (15/16 tall; both later joined as model blocks in survival B1).
- **Functional cubes with state-dependent shapes:** piston, sticky_piston, chiseled_bookshelf.
- **Entity-rendered:** chests (later joined as model blocks with a stand-in box in survival B1), shulker boxes.

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

{{WORLDGEN}}

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
