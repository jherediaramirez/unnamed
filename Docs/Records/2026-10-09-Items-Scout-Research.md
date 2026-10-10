# Items, tools and weapons: scout facts and draft plan (Unnamed, 26.3)

Research scout F, night of 2026-10-09. Read-only: nothing in `C:\dev\projects\Unnamed` was changed. No Java and no Unreal runs.

**Sources.** Every number comes from the game's own files:
- Mojang's data-generator "Default Components" report, already in `C:\dev\sandbox\mc-26.3-blocks\reports\reports\minecraft\components\item\` (1,658 files, generated 2026-10-08 from server jar SHA-1 `33680f5f…`).
- The data-pack tags inside the unpacked server jar.
- The pinned client jar `%APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar` (SHA-1 `e877b6a0…`, checked tonight; it matches the block reference).
- `FactsDump`'s `block_facts.csv` for hardness.

Rules that are code-side (formulas, defaults) come from minecraft.wiki and Mojang changelogs (see the appendix). No decompiled code was used.

**Scripts and raw output** (rerunnable, Python only):
- `tools/extract_items.py` and `tools/break_times.py`.
- Output goes to `out/`: `item_components.md` (95 items), `tags.md`, `item_models.md`, `break_times.md`, `detail_components.json` (verbatim components of 23 key items) and `item_kinds.json` (all 1,658 item definitions, classified).

---

## 1. Priority list

"Needs" lists what each item requires:
- **sprite:** a 16x16 icon plus a flat extruded mesh (the `item/generated` look).
- **handheld:** the same sprite with the tool pose in hand (`item/handheld`).
- **special:** a model drawn by code in vanilla (an entity model).
- **behaviour:** new gameplay.

### Tier A: the first playable set (mining loop and its outputs)

| # | Items | Why first | Needs |
|---|---|---|---|
| A1 | **35 tools:** pickaxe, axe, shovel, hoe and sword in wooden, stone, copper, iron, golden, diamond and netherite | They are what makes mining tiers matter. All 35 share one model type and one data shape. | **Model:** handheld. **Behaviour:** none new. The existing `FFoundationItemDefinition` (mining rules, durability, damage and speed) already holds every field; only the data import is needed (§4). |
| A2 | **Ore and tier materials:** `coal`, `raw_copper`, `raw_iron`, `raw_gold`, `copper_ingot`, `iron_ingot`, `gold_ingot`, `diamond`, `emerald`, `lapis_lazuli`, `redstone`, `quartz`, `netherite_ingot`, `netherite_scrap` | An ore that "drops itself" hides the whole point of tiers. These are also the tools' repair materials. | **Model:** sprite. **Behaviour:** none (stack 64). They become drops once ore loot is data-driven (session S5b). |
| A3 | `stick` | The ingredient in every tool. | **Model:** sprite. Inert. |
| A4 | `bucket`, `water_bucket`, `lava_bucket` | Still water and lava already exist. Taking and placing a source is the iconic use action. | **Model:** sprite. **Behaviour:** use on a block places or picks up a source. Stack sizes are 16 empty and 1 full. |
| A5 | **Food:** `bread`, `apple`, `cooked_beef` (and `golden_apple` later) | `FoundationVitals` already tracks food, so eating closes the loop. | **Model:** sprite. **Behaviour:** the `consumable` component (eating time and animation) and `food` (bread: nutrition 5, saturation 6.0). |

### Tier B: cheap once Tier A's pipeline exists

| Items | Why | Needs |
|---|---|---|
| `shears` | Pure tool rules: `#shears_*_breaking_speed` and cobweb. | **Model:** sprite (generated, not handheld). |
| `torch` (item only) | The most-used utility item. **Placement is blocked:** the torch is not a full block, and Jesus's direction is full blocks only (question Q4). | **Model:** sprite, from `block/torch`. |
| `flint_and_steel`, `arrow`, `ender_pearl`, `snowball`, `compass` | Recognisable, small items. | **Model:** sprite. **Behaviour:** each needs its own system (fire, projectiles, the compass needle has 66 model variants), so they are inert at first. |

### Tier C: separate systems; plan later

| Items | Why later | Needs |
|---|---|---|
| **7 spears** (new weapon, 1.21.11) | Jab and charge ("kinetic") attacks, plus reach (2-4.5 blocks). | Two sprites per spear: a 16x16 icon and a 32x32 in-hand texture with its own transform. A new combat mode. |
| `mace`, `trident` | Smash attack from fall damage; throwing. | `handheld_mace`. The trident in hand is a special entity model. |
| `shield` | Blocking and axe shield-disable (5 s). | A special entity model with banner patterns, drawn in the offhand. |
| `bow`, `crossbow` | Projectiles and charging. | Pull-frame model switching (`condition`/`select`). |
| **Armor:** 7 materials × 4 pieces | Vitals already tracks armor points. | Equipment slots, the armor layer on a player body (none exists yet), and trim layers. |
| `clock`, `map`, `elytra`, `totem_of_undying` | Each one is a system of its own. | Animated or dynamic models. |

---

## 2. Decisions for Jesus

Each decision has options and a recommendation. Q1 and Q2 touch the CLAUDE.md rule "Block/item IDs (item id = block id) … are saved IDs."

**Q1. What id does a non-block item get?**
- **Data:** 26.3 has 1,658 items and 1,286 blocks. 1,129 names are in both registries, and each of those is the block's own item, with two exceptions:
  - `air`.
  - `wheat`: the crop block `wheat` gives `wheat_seeds`, while the `wheat` item is the harvest.

  The other 529 items have no block of the same name.
- **(a) Recommended:** item id = the 26.3 item registry name (`iron_pickaxe`, `stick`, `water_bucket`). For every block item this is still the block id, so nothing is renamed. Reword the rule as: *"Item IDs are the 26.3 item names; a block's item has the block's id (item id = block id)."* Note that `wheat` would clash only if crop blocks are ever imported, and they are not full blocks.
- **(b)** Prefix non-block items (`item_iron_pickaxe`). This avoids any theoretical clash but breaks vanilla parity (creative search, future `/give`, saves readable as vanilla).
- **(c)** Project-named items (like today's `practice_*`). No.

**Q2. Where does non-block item data live?**
- **(a) Recommended:** a new generated asset, `DA_Items` (`UFoundationItemAssets`):
  - **Location:** `/Game/Items`, through a new `UUnnamedProjectSettings::ItemRoot`.
  - **Source:** a new hash-checked `Reference/minecraft-items-26.3/`.
  - **Effect:** `DA_Blocks` and its reference are not touched or reimported. `BuildRules` merges three sources: block items (`AddBlockItems`, unchanged), then vanilla non-block items from `DA_Items`, then project items (`UFoundationInteractionSettings::Items`).
- **(b)** Grow `DA_Blocks` to hold every item. This needs the accepted block reference moved away and rebuilt, `DA_Blocks` reimported, and the "Blocks" name would no longer describe the asset. More churn for no gain.
- **(c)** Hand-copy the 35 tools into `DefaultGame.ini` `Items`. Fast, but the numbers can drift from the jar, and CLAUDE.md reserves those settings for *project* items.

**Q3. What happens to `practice_pickaxe`, `practice_sword` and the loadout?**
- **Today:** the loadout is `stone, practice_pickaxe, practice_sword`:
  - The practice items carry iron stats.
  - They are drawn as coloured cubes in hand.
  - The creative screen maps them to the `diamond_pickaxe` and `netherite_sword` sprites.
  - Tests and the interaction probe use them.
- **Recommended:** keep them until the vanilla tools are in (S2). Then switch the loadout to `stone, iron_pickaxe, iron_sword` (or Jesus's pick), move the tests to vanilla ids, and add both ids to the retired list.
- **Alternative:** keep them for good as lab fixtures alongside the vanilla tools.

**Q4. Torch, given "blocks stay simple"?**
- **Recommended:** the torch is an item only for now: icon, held, in the creative tab, with no placement.
- **Alternative:** a single exception for torches (standing and wall variants plus a light source of 14). That is a block project, not an item project.

**Q5. Drops: in-world item entities, or straight to the inventory?**
- **Today:** the finite fixture puts drops straight into the inventory. Infinite supply gives nothing.
- **Recommended:** keep that through the tool sessions. Item entities get their own later session (S8): physics, a pickup delay, merging, ground rendering, bobbing and spinning.

**Q6. Ore drops.**
- **Recommended:** a small data-driven loot subset (S5b) so that ores drop their resources (diamond ore gives a diamond, iron ore gives raw iron, and so on). It reads `data/minecraft/loot_table/blocks/*.json` from the jar.
- **Scope:** Fortune, Silk Touch and explosion decay wait for enchantments.
- **Without it:** a pickaxe tier shows only as "drops or not".

**Q7. Held-item mesh approach** (details in §3). **Recommended:** C. A pure C++ builder makes an exact extruded mesh from each sprite's opacity mask, cached per item, with item textures in a texture array. This matches the block pipeline and gives vanilla's look.

---

## 3. Models and rendering

### What 26.3's data says

**The 1,658 item definitions** (`assets/minecraft/items/*.json`):

| Kind | Count | Notes |
|---|---|---|
| Block models | 834 | The project's `HudBlockIcon` already draws these. |
| Flat sprites (`item/generated` → `builtin/generated`) | 761 | |
| `special` (code-drawn) | 62 | Shield, trident in hand, chests, heads, banners, beds and similar. |
| Other | 1 | |

**Item textures:** 824 are 16x16 and 7 are 32x32. The 32x32 ones are the spear in-hand textures.

**The tools** (all 35) use one plain chain:
- `items/<id>.json` is `{"type":"model","model":"item/<id>"}`.
- `item/<id>` → `item/handheld` → `item/generated`, with `layer0 = item/<id>`.
- There are no tints, no variants, and one layer.

**Display transforms** are verbatim from the jar. Units are pixels (1/16 block) and degrees:

| Model | firstperson_righthand | thirdperson_righthand | ground | fixed |
|---|---|---|---|---|
| `item/generated` (sticks, ingots, food, buckets) | rot [0,-90,25], tr [1.13,3.2,1.13], scale 0.68 | rot 0, tr [0,3,1], scale 0.55 | tr [0,2,0], scale 0.5 | rot [0,180,0] |
| `item/handheld` (all 35 tools) | same as generated | rot [0,-90,55], tr [0,4,0.5], scale 0.85 | inherits generated | inherits |
| `item/handheld_mace` | rot [0,-90,25], tr [0,3,0.8], scale 0.9 | rot [0,-90,55], tr [0,4,1], scale 1.0 | inherits | inherits |
| `item/handheld_rod` (fishing rod) | rot [0,90,25], tr [0,1.6,0.8], scale 0.68 | rot [0,90,55], tr [0,4,2.5], scale 0.85 | inherits | inherits |
| `item/spear_in_hand` (32x32) | rot [-20,90,-35], tr [3.13,2,0.13], scale [1.36,1.36,0.68] | rot [5,270,-40], tr [0,2,2], scale [1.7,1.7,0.85] | (the gui, ground and fixed contexts use the 16x16 `item/<mat>_spear` instead) | |
| `block/block` (held blocks, for comparison) | rot [0,45,0], scale 0.4 | rot [75,45,0], tr [0,2.5,0], scale 0.375 | tr [0,3,0], scale 0.25 | scale 0.5 |

Left-hand entries mirror the Y and Z rotations. The `head` context exists too.

The non-uniform spear scale keeps a 32x32 sprite one sixteenth-pixel thick. Spears also set `swap_animation_scale` 1.95.

**What the data does not give:** where the hand sits on screen, and the swing and equip curves. Those live in the client's code. The display transforms place the item *relative to* the hand. To match vanilla 1:1, measure it (probe P1 in the appendix) rather than reading code.

### `builtin/generated`, as observed in game

The wiki only says it is "a model created out of the specified icon". It gives no thickness. As seen in game, each layer becomes:
- a front quad and a back quad of the whole 16x16 texture, one pixel (1/16 block) apart;
- a one-pixel-deep side face wherever an opaque pixel borders a transparent one or the edge, coloured by that edge pixel.

The result is a sprite with exact pixel-step sides, centred in the 16³ model box before the display transform. Confirm the thickness and edge colouring from the probe P1 shots.

**Display transform semantics** (wiki "Model"):
- **Fields:** `translation`, `rotation` and `scale`, each [x, y, z]. Translation is applied first, then rotation.
- **Clamps:** translation is clamped to ±80, scale to at most 4.
- **Fallback:** a missing left-hand entry falls back to the right-hand one.
- **Contexts (9):** thirdperson right and left, firstperson right and left, `gui`, `head`, `ground`, `fixed` and `on_shelf`.

**Model definition root fields:** `hand_animation_on_swap` (default true) and `swap_animation_scale` (default 1.0) drive the equip dip.

**Model types:** `model`, `select`, `condition`, `range_dispatch`, `special`, `composite`, `empty` and `bundle/selected_item`.

### Bringing it into Unreal 5.8

**Coordinates.** Minecraft model space is 16 px per block, Y up, X east and Z south. Unreal is Z up, X forward, in cm.
- `UFoundationProfile::BlockCm` per block, so 1 px = BlockCm/16.
- Convert a transform as translate → rotate (X, Y, Z in degrees) → scale, about the model centre (8,8,8). Mirror one axis for the handedness change.
- The project already renders the held item with UE's First Person Rendering (`SetFirstPersonPrimitiveType(FirstPerson)`, camera `FirstPersonScale` 0.5). Keep it: it stops clipping into walls and gives the hand its own FOV.

**Icons (inventory, hotbar, tooltips).** Vanilla's GUI draws the flat 16x16 sprite at the GUI scale.
- **Precedent:** the creative screen already loads single sprites as `/Game/Blocks/Gui/item/<name>` for tab icons (`ItemSprite()` in `HudCreativeScreen.cpp`).
- **For hundreds of items:** an atlas (`T_ItemIcons`) with a UV region per item on `FSlateBrush`, like `T_BlockIcons`, keeps the screen to one texture.
- **Filtering:** nearest, with no mips.
- **Tinted layers** (leather armor, potions, spawn eggs) draw each layer with its `tints` colour.
- **Durability bar:** 13 px wide and 2 px high under the icon, green to red as the item wears. Measure the exact colour ramp with probe P2.

**Held and dropped meshes: three approaches.**

| | A. Shared alpha-tested slab (Luanti style) | B. Exact mesh per item, baked at import | **C. Exact mesh per item, built at runtime from a stored mask (recommended)** |
|---|---|---|---|
| **How** | One mesh per texture size: front and back quads, plus a thin strip per pixel row and column, each sampling its own pixel. A masked material (alpha 0.5) discards the transparent texels, which also hides the strips beside them. The geometry never reads the pixels. | Editor Python reads each PNG's alpha and emits front and back quads plus edge sides only where needed (Geometry Script `CreateNewStaticMeshAssetFromMesh`). One StaticMesh asset per item. | A 16x16 opacity mask (32 bytes) per item lives in `DA_Items`. A pure C++ function turns the mask into quads (the same output as B) and caches one mesh per item on first use. Textures come from a `T_ItemTextures` array, like `T_BlockTextures`. |
| **Look** | Near-vanilla. Sides show at every opaque edge. Masked shading, with possible shimmer at a distance unless mips are off. | Exact vanilla. Opaque material. | Exact vanilla. Opaque or cutout material picked per item, like the block layers. |
| **Cost** | One mesh for all items and no per-item import. About 70 quads per 16x16 item, mostly discarded pixels. Masked overdraw is negligible for a handful of items. | One asset per item (35 to 761) and a slower import. Reimporting means moving assets away (the house rule). | No mesh assets, about 2 KB of data per item, and a few microseconds per first build. Unit-testable like `UnnamedVoxel`. Can use `RealtimeMeshComponent` (already vendored) or a transient StaticMesh. |
| **Who does it** | Luanti `src/client/wieldmesh.cpp` (LGPL-2.1+; ideas only). | Godot VoxelFactory and Voxelizer; Blockbench "Extrude Texture". | Builds the same mesh as B, at runtime. Bedrock's default held item is also an "extruded texture mesh". |

Plain flat quads (approach D) are the cheapest, but they look paper-thin from the side and are not vanilla. They are only for far LODs, if ever needed.

**Special models** (the shield, the trident in hand, and later chests and heads) are entity models drawn by code. Rebuild them later from the jar's entity textures as hand-made `.glb` files in `foundry/`, following the project rule for art sources. None of them are in Tier A.

**Dropped items (later, S8).**
- **Pose:** the `ground` transform (scale 0.5 for sprites, 0.25 for blocks).
- **Motion:** spin and bob, more copies drawn for bigger stacks.
- **Rules:** the wiki-documented pickup delay, merging and despawn (see the appendix).

---

## 4. Fit with this project

**What already exists.**
- `FFoundationItemDefinition` (`Foundation/FoundationItems.h`) has these fields: `Id`, `DisplayName`, `Tags`, `MaxStack`, `PlacesBlock`, `AttackDamage` and `AttacksPerSecond` (totals), `DefaultMiningSpeed`, ordered `MiningRules` (first rule with a speed sets the speed; first rule stating drops sets correctness, the same as 26.3's `tool` component), `MaxDurability`, `DurabilityPerBlock`, `DurabilityPerHit`, `Icon` (`block`, `pickaxe` or `sword`) and `IconColor`.
- `GetMiningDelta` in `FoundationAuthority.cpp` already applies the 26.3 order: tool speed, efficiency above 1, haste and fatigue, in water ×0.2, airborne ×0.2, ÷30 or ÷100.
- Melee already has the charge scaling (0.2 + 0.8·s²) and criticals.
- Block tags already exist as `Block.Mineable.*`, `Block.IncorrectForTool.*`, `Block.NeedsTool.*`, `Block.SwordEfficient` and `Block.Shears*`.
- `Block.SwordInstantlyMines` is absent, because none of its blocks (bamboo and the like) are imported.

**Mapping from 26.3 components to existing fields.** Tier A tools need **no new fields**:

| 26.3 component | Project field |
|---|---|
| `tool.rules[]` (block tag → `Block.*` tag, `speed`, `correct_for_drops`) | `MiningRules` |
| `tool.default_mining_speed` (default 1) | `DefaultMiningSpeed` |
| `tool.damage_per_block` (default 1; swords, mace and trident 2) | `DurabilityPerBlock` |
| `weapon.item_damage_per_attack` (default 1; pickaxes, axes, shovels and hoes 2; see the note below) | `DurabilityPerHit` |
| `max_damage` | `MaxDurability` |
| `attribute_modifiers` (`base_attack_damage` + 1; `base_attack_speed` + 4) | `AttackDamage`, `AttacksPerSecond` |
| `max_stack_size` | `MaxStack` |
| item tags (`#pickaxes`, `#swords` …) | new `Item.*` gameplay tags (`Config/Tags/MinecraftItemTags.ini`, like the block tags) |

The existing `practice_pickaxe` already uses the iron pickaxe's data and costs (250, 1 per block, 2 per hit), so the model is proven.

**Data versus the wiki on hoes.** The wiki's Durability page says a hoe loses 1 per hit in Java. The 26.3 data gives every hoe `item_damage_per_attack` 2. Follow the data; probe P5 can confirm it.

**Fields to add later, when their sessions need them:**
- `Enchantability`.
- `RepairWith` (an item tag).
- `bCanDestroyBlocksInCreative`: false for swords, the mace, the trident and spears.
- `DisableBlockingSeconds`: 5 for axes.
- `bDamageResistant`: netherite items do not burn as dropped items.
- `ItemModel` / `Display`: the transform set name.
- The 16x16 opacity mask and texture layer.

Replace the `Icon` presentation key (`pickaxe`, `sword`) with the item model data in S4.

**The two sword rules a full-block world cannot express:**
- `cobweb`: the rule names a single block, not a tag, and cobweb is not imported.
- `#sword_instantly_mines`: the tag is empty here.

Keep both rules in the data. They match nothing until those blocks exist.

**The creative inventory.**
- Tabs keep only known items, so Tools & Utilities (158 entries), Combat (126), Food & Drinks (195) and Ingredients (194) are hidden today.
- Their full vanilla order is already in `C:\dev\sandbox\mc-26.3-creative\facts\creative_tabs.json` (`FactsDump`). Move it into the new item reference.
- Once `DA_Items` exists, the tabs fill themselves, but non-block entries need the sprite icon path (S3).
- **Entries with components:** some tab entries are the same id with different components (potions, enchanted books, goat horns). Show only plain items at first.
- **Hotbar:** the in-game hotbar has the same gap: it draws block icons only.

**Saves.** Inventories are not saved yet (worlds get saves in voxel phase 10). The time to settle Q1 is before phase 10 writes the first inventory to disk.

---

## 5. Draft plan (one area per session)

**Size:** S is about half a session, M one session, L may need a handoff.

**"Parallel-safe"** means the session touches none of `Source/UnnamedVoxel/`, `Source/Unnamed/Voxel/`, `Reference/minecraft-worldgen-26.3/` or the voxel docs. Builds and native suites still have to take turns with the voxel worker:
- One editor build at a time (`-WaitMutex`).
- No suites while another worker's suites run.
- Commits to the shared branch are coordinated.

| # | Session | Size | Depends on | What it pins (tests) | Parallel-safe |
|---|---|---|---|---|---|
| S1 | **Item reference.** `Reference/minecraft-items-26.3/` (generated, hash-listed, never edited). It holds `items.json`: every item's id, English name, stack, durability, tool rules, weapon, attack totals, enchantability, repair, model chain, display transform name, layers, tints and creative positions. It also holds the item and tool tags, the jar's `textures/item/*.png`, `items/*.json` and `models/item/*.json` byte for byte, plus `verify_reference.py`. Built from the datagen reports and jars by a Python tool (this scout's `extract_items.py` is a start). Item tags go to `Config/Tags/MinecraftItemTags.ini`. | M | none | The verifier hashes everything. Counts: 1,658 items, 35 tools, 7 spears. Spot values: `diamond_pickaxe` 1561 durability and speed 8, `golden_*` 32 and 12, `netherite_sword` 8 damage at 1.6. | **Yes** (new folder and a script) |
| S2 | **Item registry and rules.** `UFoundationItemAssets` `DA_Items` (`ImportItems.py`, verifies and never replaces), `UUnnamedProjectSettings::ItemRoot`, and the `BuildRules` merge. Settle Q1-Q3, reword the CLAUDE.md rule, and switch the loadout. | M | S1, Q1-Q3 | `Unnamed.Foundation.Items.Registry`: every tool's numbers against the reference, and no id clash between block items and non-block items. `.Rules`: tags resolve to `Block.*` tags. Interaction probe with the new loadout. | **Yes** (Foundation and settings). The authority is shared with the voxel world, but nothing voxel is edited. |
| S3 | **Icons.** `T_ItemIcons` atlas and the `T_ItemTextures` array (`ImportItems.py`). The creative tabs show Tools, Combat, Food and Ingredients. The hotbar draws sprites. Durability bar. Tooltip lines ("When in Main Hand: 6 Attack Damage, 1.6 Attack Speed" from the lang keys). | M | S2 | `Unnamed.Hud.Creative.Tabs` with the new counts. Tour shots of each tab. A pixel check of the durability bar against probe P2. | **Yes** (Hud) |
| S4 | **Held-item rendering.** The pure mask-to-mesh builder (Q7 C), the item material, the display transforms from data (`generated`, `handheld`, `handheld_mace`, `handheld_rod`, `spear_in_hand`), and the hand anchor from probe P1. Replace the cube pickaxe and sword. Third person waits for a body. | M-L | S1, S3 (textures) | `Unnamed.Foundation.Items.Mesh`: quad counts for known masks (an iron pickaxe's mask gives an exact count), closed silhouette, front and back mirror. Screenshot shots per material against the P1 vanilla shots. | **Yes** (`FoundationInteractionView`) |
| S5 | **Mining by tool and tier.** All 35 tools on the real tags, sword and shears rules, and creative "swords, spears, tridents and maces can't break" (`can_destroy_blocks_in_creative`). The delay between breaks: the wiki says 6 ticks (0.3 s), skipped for breaks of one tick or less. I did not find where the project sets it; pin it with probe P5. | S | S2 | `Unnamed.Foundation.Mining.Tiers`: the break-tick and drop truth table in Appendix B (tool × block), plus the harvest table per tier. Interaction scenario: iron ore with wooden then stone pickaxes. One `CaptureVoxel -Edits` run, because generated terrain mines through the same authority. | **Yes** (Foundation) |
| S5b | **Ore loot subset.** Data-driven drops for ores (and gravel to flint, leaves to saplings and sticks), from `loot_table/blocks/*.json`. No Fortune yet. | S-M | S2, Q6 | A drop table per block and tool, and "silk touch absent" behaviour. | **Yes** |
| S6 | **Durability.** Per-tool max, 1 or 2 per block or hit, breaking, the break sound and event, and the hotbar bar (if not done in S3). Repair, Unbreaking and Mending wait for crafting and enchanting. | S | S2 (merge into S5 if small) | Wear sequences per kind: a pickaxe loses 59 points over 59 blocks, a wooden pickaxe loses 2 per attack, and a sword loses 2 per block mined. Instant blocks (hardness 0) cost nothing. | **Yes** |
| S7 | **Basic combat by item.** Damage and speed come from each item as totals. Charge and criticals already exist; this session adds:<br>• **Cooldown:** T = 20 / attack speed ticks, damage × (0.2 + 0.8·s²).<br>• **Sword sweep:** needs ≥ 84.8% charge, no crit, no sprint knockback, on the ground, a sword in the main hand, and a small movement delta. Sweep damage is 1 + ratio·base. Range: entities within 3 blocks, using the target box grown by 1 horizontally and 0.25 vertically.<br>• **Knockback.**<br>• **Axe shield-disable** (5 s, held as data until shields exist). | S-M | S2 | A damage table per tool at full and half charge, crits, and the sweep truth table. | **Yes** |
| S8 | **Item entities** (later). Dropped items in the world, with the wiki's rules:<br>• **Body:** a 0.25 × 0.25 box with physics against blocks.<br>• **Pickup:** delay 10 ticks, or 40 when a player throws it.<br>• **Despawn:** 6,000 ticks.<br>• **Merging:** same item within a 0.5 × 0.25 × 0.5 box, checked about every 40 ticks.<br>• **Rendering:** the ground transform; spin 1 rad/s; bob on a period of π s; copies drawn 1 / 2 / 3 / 4 / 5 at stack sizes 1 / 2-16 / 17-32 / 33-48 / 49+. | L | S4, Q5 | Pickup delay, merge box, stack copies and despawn. A tour shot against probe P3. | Mostly. Collision reads the authority's block storage, not voxel files. |
| S9+ | **Tier A4 and A5 behaviours** (buckets, eating), then Tier B and C systems. | S each | S2-S4 | Per system | Yes |

**Order:** S1 → S2 → (S3 ∥ S5 ∥ S6 ∥ S7) → S4 → S5b → S8.
- S1 can start tonight, alongside the voxel chain, because it only adds new files.
- S2 is the only session that edits CLAUDE.md, so do it at a quiet moment for the voxel worker.

**Questions for Jesus.** Q1-Q7 in §2, plus:
- **Q8.** Should the first playable set include copper tools from the start? Recommended: yes. 26.3 has them (1.21.9), with stone-level harvest, speed 5, durability 190 and enchantability 13.
- **Q9.** Should the hotbar draw items exactly as vanilla does: flat sprites for items, 3D icons for blocks? Recommended: yes. It is the 1:1 rule.

---

## Appendix A: tool and weapon data (26.3 components)

**Attack totals:** player base 1 damage and 4 attacks/s, plus the item's modifiers. Durability costs use the component defaults.

**Per material** (the same for all five kinds):

| Material | Durability | Mining speed | Cannot harvest (first rule: `#incorrect_for_<m>_tool`) | Enchantability | Repair (`#<m>_tool_materials`) | Other |
|---|---|---|---|---|---|---|
| wooden | 59 | 2 | `needs_stone`, `needs_iron` and `needs_diamond` (108 blocks) | 15 | `#planks` | furnace fuel |
| stone | 131 | 4 | `needs_iron` and `needs_diamond` (17) | 5 | cobblestone, blackstone, cobbled_deepslate | |
| copper | 190 | 5 | `needs_iron` and `needs_diamond` (17), the same as stone | 13 | copper_ingot | |
| iron | 250 | 6 | `needs_diamond` (5) | 14 | iron_ingot | |
| golden | 32 | 12 | the same as wooden (108) | 22 | gold_ingot | |
| diamond | 1561 | 8 | none (the tag is empty) | 10 | diamond | |
| netherite | 2031 | 9 | none | 15 | netherite_ingot | `damage_resistant` |

**Tier membership:**
- `needs_stone_tool` (91 blocks): copper and iron ores and blocks, lapis, and so on.
- `needs_iron_tool` (12): diamond, emerald, gold and redstone ores; diamond, emerald, gold and raw-gold blocks.
- `needs_diamond_tool` (5): obsidian, crying obsidian, netherite block, respawn anchor, ancient debris.
- There is no `needs_copper_tool`.

**Rules by kind:**
- **Pickaxe, axe, shovel, hoe:** rules `[#incorrect_for_<m>_tool → drops no] [#mineable/<kind> → speed s, drops yes]`. Durability costs 1 per block and 2 per hit.
- **Axes, shovels and hoes** also carry `block_transformer` (`axe`, `shovel` or `hoe`: stripping, paths and tilling).
- **Axes** carry `weapon.disable_blocking_for_seconds` 5.
- **Sword:** rules `[cobweb → 15, drops yes] [#sword_instantly_mines → 3.4e38] [#sword_efficient → 1.5]`, with `damage_per_block` 2, `can_destroy_blocks_in_creative` false, and 1 per hit.

**Attack damage / attacks per second:**

| Kind | wooden | stone | copper | iron | golden | diamond | netherite |
|---|---|---|---|---|---|---|---|
| sword | 4 / 1.6 | 5 / 1.6 | 5 / 1.6 | 6 / 1.6 | 4 / 1.6 | 7 / 1.6 | 8 / 1.6 |
| axe | 7 / 0.8 | 9 / 0.8 | 9 / 0.8 | 9 / 0.9 | 7 / 1.0 | 9 / 1.0 | 10 / 1.0 |
| pickaxe | 2 / 1.2 | 3 / 1.2 | 3 / 1.2 | 4 / 1.2 | 2 / 1.2 | 5 / 1.2 | 6 / 1.2 |
| shovel | 2.5 / 1.0 | 3.5 / 1.0 | 3.5 / 1.0 | 4.5 / 1.0 | 2.5 / 1.0 | 5.5 / 1.0 | 6.5 / 1.0 |
| hoe | 1 / 1.0 | 1 / 2.0 | 1 / 2.0 | 1 / 3.0 | 1 / 1.0 | 1 / 4.0 | 1 / 4.0 |
| spear | 1 / 1.54 | 2 / 1.33 | 2 / 1.18 | 3 / 1.05 | 1 / 1.05 | 4 / 0.95 | 5 / 0.87 |

**Other weapons and tools:**

| Item | Durability | Damage / speed | Notes |
|---|---|---|---|
| mace | 500 | 6 / 0.6 | Repair with `breeze_rod`. Tool with no rules: 2 per block, cannot break in creative. |
| trident | 250 | 9 / 1.1 | Enchantability 1. Same tool data as the mace. No repair material. |
| shears | 238 | n/a | cobweb 15 (drops), `#shears_extreme_breaking_speed` 15, `#shears_major_breaking_speed` 5, `#shears_minor_breaking_speed` 2 |
| shield | 336 | n/a | Repair with `#wooden_tool_materials`. `blocks_attacks`: 0.25 s delay, item damage threshold 3 (base 1, factor 1). Offhand equippable. |
| bow, crossbow, fishing rod, flint and steel, brush | 384, 465, 64, 64, 64 | n/a | |

**Spears** add `attack_range` (reach 2-4.5, creative 2-6.5), `kinetic_weapon`, `piercing_weapon`, `minimum_attack_charge` 1 and `damage_type` spear. For copper: `kinetic_weapon` damage multiplier 0.82, 13 ticks delay. Full values are in `out/detail_components.json`.

**Key non-tool items:**

| Items | Stack | Components |
|---|---|---|
| bucket | 16 | |
| water_bucket, lava_bucket | 1 | Lava is fuel. |
| torch, stick, coal, ingots, diamond | 64 | Ingots and diamond carry `provides_trim_material`. |
| bread | | `food` 5 / 6.0, `consumable` |
| ender_pearl, snowball, egg | 16 | The pearl has `use_cooldown`. |
| Armor (iron helmet, chestplate, leggings, boots) | | Durability 165 / 240 / 225 / 195, enchantability 9, `#repairs_iron_armor` |

## Appendix B: break times, computed from the data

The formula (wiki "Breaking"; the project's `GetMiningDelta` follows the same order):

```
S = speed of the first tool rule that matches and states one (else default_mining_speed, 1)
if S > 1: S += mining_efficiency            (Efficiency: level² + 1)
S *= 1 + 0.2 × Haste level
S *= Mining Fatigue: 0.3, 0.09, 0.0027, 0.00081 for I-IV (hardcoded; the wiki notes 0.3^n is wrong for III and IV)
S *= block_break_speed                       (default 1)
if eyes are in water: S *= submerged_mining_speed   (0.2; 1 with Aqua Affinity)
if not on the ground: S /= 5
d = S / hardness / (30 if the block drops with this tool else 100)
instant if d ≥ 1, otherwise ticks = ceil(1 / d), then a 6-tick delay before the next block (wiki)
```

Hardness -1 cannot be broken, and hardness 0 breaks at once. In creative every block breaks at once, unless the held item cannot break blocks in creative.

All times assume standing on ground, out of water, with no enchantments or effects. ✗ means the block breaks without dropping. The tool column is the block's `mineable/*` kind.

| Block | Hardness | Needs tool | Tool | Hand | wooden | stone | copper | iron | golden | diamond | netherite | iron sword |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| dirt / sand | 0.5 | no | shovel | 0.75s | 0.40s | 0.20s | 0.15s | 0.15s | 0.10s | 0.10s | 0.10s | 0.75s |
| oak_log / planks | 2.0 | no | axe | 3.00s | 1.50s | 0.75s | 0.60s | 0.50s | 0.25s | 0.40s | 0.35s | 3.00s |
| oak_leaves | 0.2 | no | hoe | 0.30s | 0.15s | 0.10s | 0.10s | instant | instant | instant | instant | 0.20s (shears instant) |
| stone | 1.5 | yes | pickaxe | 7.50s ✗ | 1.15s | 0.60s | 0.45s | 0.40s | 0.20s | 0.30s | 0.25s | 7.50s ✗ |
| deepslate / coal_ore | 3.0 | yes | pickaxe | 15.00s ✗ | 2.30s | 1.15s | 0.90s | 0.75s | 0.40s | 0.60s | 0.50s | 15.00s ✗ |
| iron_ore / copper_ore | 3.0 | yes | pickaxe | 15.00s ✗ | 7.50s ✗ | 1.15s | 0.90s | 0.75s | 1.25s ✗ | 0.60s | 0.50s | 15.00s ✗ |
| gold / diamond ore | 3.0 | yes | pickaxe | 15.00s ✗ | 7.50s ✗ | 3.75s ✗ | 3.00s ✗ | 0.75s | 1.25s ✗ | 0.60s | 0.50s | 15.00s ✗ |
| deepslate_diamond_ore | 4.5 | yes | pickaxe | 22.50s ✗ | 11.25s ✗ | 5.65s ✗ | 4.50s ✗ | 1.15s | 1.90s ✗ | 0.85s | 0.75s | 22.50s ✗ |
| iron_block | 5.0 | yes | pickaxe | 25.00s ✗ | 12.50s ✗ | 1.90s | 1.50s | 1.25s | 2.10s ✗ | 0.95s | 0.85s | 25.00s ✗ |
| obsidian | 50 | yes | pickaxe | 250s ✗ | 125s ✗ | 62.5s ✗ | 50s ✗ | 41.7s ✗ | 20.85s ✗ | 9.40s | 8.35s | 250s ✗ |
| ancient_debris | 30 | yes | pickaxe | 150s ✗ | 75s ✗ | 37.5s ✗ | 30s ✗ | 25s ✗ | 12.5s ✗ | 5.65s | 5.00s | 150s ✗ |

A wrong-tier tool still mines at its full speed: the `incorrect_for` rule states no speed, so the `mineable` rule's speed applies. It only loses the drop and pays the ÷100.

## Appendix C: probes for a later worker (vanilla client, black box)

**P1. Hand pose.**
- **Setup:** in the 26.3 client (Modrinth instance), creative superflat, FOV 70, view bobbing off, 1920x1080, GUI scale 2.
- **Shots:** with F1 (HUD hidden), screenshot standing still holding, in turn: `iron_pickaxe`, `stick`, `stone`, `mace`, `iron_spear` and `fishing_rod`, main hand then offhand.
- **Animations:** 10 frames of one swing (a 60 fps capture), and an equip after a slot change.
- **Third person:** repeat in F5 back view.
- **Use:** match in UE with the same FOV and aspect. Minecraft's FOV is vertical; UE's camera FOV is horizontal.

**P2. Durability bar colours.**
- **Damage:** `/give @s iron_pickaxe[damage=N]` for N = 0, 25, 62, 125, 187, 249.
- **Capture:** screenshot the hotbar at GUI scale 1, then sample the bar's pixels.

**P3. Dropped item.**
- **Drops:** drop 1, 2, 17, 33 and 64 `iron_ingot`, and 1 `stone`.
- **Capture:** record the spin period, bob height and copy count from video.

**P5. Mining and wear.**
- **Delay between breaks:** with advanced tooltips (F3+H) on, mine a row of 10 dirt with an iron shovel (3 ticks each). Count the 60 fps frames between consecutive breaks to confirm the delay of 6 ticks (wiki), against the 5 the scout first assumed.
- **Hoe wear:** hit a mob with an iron hoe once and read the durability, to settle data 2 against wiki 1.
- **Sword wear:** break 1 dirt with an iron sword. Expect 2.

**P4. Command to refresh the reports**, only if the sandbox copy is lost. It needs JDK 25, and the item components are the `--reports` "Default Components" output:

```powershell
cd C:\dev\sandbox\mc-26.3-blocks\datagen
& '<JDK25>\bin\java.exe' -DbundlerMainClass=net.minecraft.data.Main -jar ..\downloads\server.jar --reports --output ..\reports
```

## Appendix D: links

The wiki pages were read on 2026-10-09 and describe current Java (about 26.3). A subagent summarised them; numbers that matter are cross-checked against the jar data above.

**Vanilla rules**
- Breaking: https://minecraft.wiki/w/Breaking
- Data components (tool, weapon, attack_range, kinetic_weapon and their defaults): https://minecraft.wiki/w/Data_component_format
- Durability, wear and repair (crafting combine +5%, anvil combine +12%, each material unit 25%, Unbreaking 1/(1+level)): https://minecraft.wiki/w/Durability and https://minecraft.wiki/w/Anvil_mechanics
- Melee, cooldown and crits: https://minecraft.wiki/w/Damage
- Sweep: https://minecraft.wiki/w/Sweep_attack
- Dropped items: https://minecraft.wiki/w/Item_(entity)
- Models: https://minecraft.wiki/w/Model and https://minecraft.wiki/w/Items_model_definition

**Versions**
- Copper tools: Java 1.21.9, released 2025-09-30. https://minecraft.wiki/w/Java_Edition_1.21.9
- Spears: 1.21.11 "Mounts of Mayhem", released 2025-12-09. https://minecraft.wiki/w/Java_Edition_1.21.11 and https://minecraft.wiki/w/Spear
- The 26.x numbering starts at 26.1 (released 2026-03-24). 26.2 was released 2026-06-16 and 26.3 on 2026-09-15. https://minecraft.wiki/w/Java_Edition_version_history
- 26.3 added the `block_transformer` and `attack_animation` components that the axes and spears carry above. No numeric tool stat changes were seen across 26.1-26.3, though the wiki's change lists were truncated in places; the jar data above is what counts.

**Rendering approaches**
- Luanti `wieldmesh.cpp` (LGPL-2.1+, ideas only): https://github.com/luanti-org/luanti/blob/master/src/client/wieldmesh.cpp. It uses one shared mesh per power-of-two size (16 up to 512) with alpha test 0.5, no mipmaps ("thin black line" artifacts), and nearest filtering in the inventory.
- Godot per-texture voxelizers: https://github.com/cenullum/Voxelizer-For-Godot-Engine and https://github.com/antopilo/VoxelFactory
- Bedrock's held items as an extruded texture mesh: https://wiki.bedrock.dev/items/attachables
- Blockbench "Extrude Texture", for hand-made `foundry/` models: https://docs.faithfulpack.net/pages/dungeons/items-tutorial, plus the plugin https://github.com/MentonLiu/texture2model. The glTF export of the result is unverified.
- UE First Person Rendering (5.5+; FirstPersonFieldOfView, FirstPersonScale, `EFirstPersonPrimitiveType` FirstPerson / WorldSpaceRepresentation): https://dev.epicgames.com/documentation/en-us/unreal-engine/first-person-rendering. It is camera-only geometry and should not cast world shadows. Translucency limits are unverified.
- UE editor mesh baking (approach B): https://dev.epicgames.com/documentation/unreal-engine/BlueprintAPI/GeometryScript/AssetManagement/CreateNewStaticMeshAssetfromMesh. The Python name `create_new_static_mesh_asset_from_mesh` is unverified.
- `FSlateBrush` UV region, for an icon atlas: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/FSlateBrush. The UE4-era reports say a Nearest texture can still blur in UMG; the workaround is a UI-domain material, NoMipmaps, the UI texture group and integer scale. The project's creative screen already draws crisp sprites, so follow its brushes.
