# Scout H: plant interaction and collision (prep for voxel phase 6 S7)

Scout H, October 10, 2026. Read-only research against the pushed tip **015bb42** (`C:\dev\projects\Unnamed-scout`). Nothing in the repo was edited or built.

**Sources and rules followed:**
- Behaviour comes from the blocks reference data (tags, per-state facts) and from black-box runs of the 26.3 server jar's public API: `BlockState.canSurvive`, `updateShape`, `isFaceSturdy` and `Block.isFaceFull`. The level is a `java.lang.reflect.Proxy` of the public `LevelReader` interface.
- Where the data and the probes are silent, minecraft.wiki fills in.
- No bytecode or game code was read, decompiled or reflected into.

**Where the evidence is (`C:\dev\sandbox\scout-plant-interaction\`):**
- `notes/`: the full truth table (`survival_table.md`, `truth_table.csv`), `oracles.md`, `predicate_check.md`, `code-map.md` (Sonnet subagent) and `wiki.md` (Sonnet subagent).
- `probes/`: the Java probes and the Python analysis.
- `probes/out2/`: the raw probe output.

---

## 1. Summary and recommendations

1. **The data decides most plants.** 26.3 ships 41 support tags (`supports_*`, `cannot_support_*`, `support_override_*`), and our import already turns every vanilla tag into a `Block.*` gameplay tag (`Config/Tags/MinecraftBlockTags.ini`).
   - **Tag alone (29 blocks):** a tag reproduces `canSurvive` exactly over all 4,432 candidate states. For cocoa, the tag applies on its `facing` side.
   - **Tag plus a measured extra (14 blocks):** the double plants' upper halves, small dripleaf, cactus's side blockers (`#blocks_motion` or lava), sugar cane's water, lily pad's water source, big dripleaf and its stem, and the mushrooms' light.
   - **Face rules (the other 40):** "sturdy" (full or centre) or "full collision face", mostly the underwater and hanging blocks. Each was matched exactly by a black-box set (section 4.1, table T1).
2. **"Sturdy face" is the one input we lack.** The facts' collision boxes reproduce `isFaceSturdy(FULL)` exactly except for three cases:
   - leaves: full collision, but not sturdy;
   - mud and soul sand: sturdy, but their collision is 14/16 tall;
   - 8-layer snow.

   **R1:** add `support` bits (sturdy FULL and CENTER per face) to `block_state_facts.json` through `FactsDump.java`. This is the same public call the probe used, and the fallback is a derived rule plus those three exceptions (Q2).
3. **Pops come in two mechanisms, both measured.**
   - Most plants turn to air in `updateShape` the moment a neighbour changes.
   - Stacks schedule a tick and break on it: sugar cane, cactus, bamboo, kelp, cave vines, pointed dripstone and sulfur spike, pale hanging moss, big dripleaf stem. In vanilla each break updates the next, so a stack breaks one segment per tick.
   - Jesus's "one level deep" therefore truncates vanilla for stacks. **Q1** asks whether same-block vertical chains should pop fully (recommended: yes, bounded).
4. **Double plants are one unit.** Removing either half makes the other half air in `updateShape`, measured for the 6 tall plants, tall seagrass and small dripleaf. Recommend treating the pair as one plant inside the one-level rule.
5. **Multiface blocks differ.**
   - **Vine:** keeps any supported face and drops unsupported ones. A side face may also hang from a vine above that has the same face.
   - **Vine's top face is a quirk:** it needs the block above to have a full **upper** face. A top slab holds it; a bottom slab does not.
   - **Glow lichen and sculk vein:** use the face touching them, as you would expect. Every face must be held to survive, but losing a support only trims that face.
6. **Collision and targeting have real gaps at 015bb42.**
   - **L_Generated, `blocks_motion` blocks:** `AVoxelWorld::GatherCollision` turns these into full cubes: cactus, bamboo, amethyst, dripstone, sulfur spike and even sculk vein, which has no collision.
   - **L_Generated, other colliders:** lily pads, carpets, cocoa, sea pickles, azaleas and big dripleaves don't collide at all.
   - **Playground:** gameplay uses one bounding box per model state, so a vine with two faces is a full-cell selection box.

   S7 needs the per-box lists (`GetCollisionBoxes` and `GetOutlineBoxes` already exist) and the S6 offset hash.
7. **Already done at 015bb42:**
   - instant mining: hardness 0 gives a delta of 1 (`FoundationAuthority.cpp:292`), and 66 of the 83 model blocks have hardness 0;
   - no item for the 12 model blocks whose vanilla item has another id, or none (`FoundationInteractionSettings.cpp:106`);
   - imported tags.
8. **Size L. Recommend a split:**
   - **S7a, rules:** survival table, placement checks, double plants, one-level pops, the replaceable switch (answer 4), `Blocks.Placement` and `Blocks.Survival`, and the interaction scenarios.
   - **S7b, shapes:** collision and outline boxes in both worlds, multi-box targeting and outlines, offsets, and the walk-through evidence and gates. S7b needs S6's offset hash and S6/S6b's `FoundationInteractionView` changes.

---

## 2. Reading list for the S7 worker (at 015bb42, in order)

1. This doc: sections 1, 4.1 (T1), 5 and 6.
2. `Docs/Records/2026-10-09-Voxel-Phase6-Plan.md`: 380-404 (S7) and 643-667 (answers 6 and 7).
3. `Docs/Records/2026-10-09-Voxel-Handoff-Phase6-S6.md`: 27-34 (what S5 imported) and 66-84 (known issues; answer 4, "replaceable for every block").
4. `Docs/Blocks.md`: 32-48 (Ids, States and shapes), 49-62 (Placement rule table) and 110-121 (Tests).
5. `Source/Unnamed/Foundation/FoundationAuthority.cpp`:
   - 181-240: `GetDestination`, `MakePlacementContext`;
   - 242-287: `CheckPlacement`, with the support check at 263-272;
   - 289-302: `GetMiningDelta`;
   - 465-566: `UseOn`, which commits at 549;
   - 583-636: `MineStart`;
   - 689-735: `BreakBlock`, which removes at 714.

   In `FoundationAuthority.h`: 40 (`EFoundationReason`), 213-240 (events) and 290-330 (declarations).
6. `Source/Unnamed/Foundation/FoundationBlockPlacement.h` (all 46 lines) and `FoundationBlockPlacement.cpp` 99-220 (`CanBeReplaced`, `ResolveDestination`, `GetStateForPlacement`).
7. `Source/Unnamed/Foundation/FoundationBlocks.cpp` 113-180: `IsKnown`, `Get`, `Set`, `GetSelectionBox`, `GetCollisionBox`, `GatherCollision`.
8. `Source/Unnamed/Foundation/FoundationBlockRegistry.h` 47-104 (`FFoundationBlockType`, including the offset fields at 88-92) and 211-256 (API). In the `.cpp`: 398-430 (box lists), 463-540 (properties) and 544-590 (single-box shapes).
9. `Source/Unnamed/Foundation/FoundationTargeting.cpp` 76-101 (one selection box per cell).
10. Voxel world:
    - `Source/Unnamed/Voxel/VoxelWorld.cpp`: 280-300 (`SetupAuthority`) and 476-519 (`GatherCollision`);
    - `VoxelBlocks.cpp` 95-125 (model states as air; `bSolid` from `blocks_motion`);
    - `VoxelBlockStorage.h` and `.cpp` (89 lines).
11. `Scripts/Editor/BlockImportData.py`: 87-100 (`placement_rule`) and 200-257 (model fields; `replaceable` at 227 and 246; `placement` at 247; tags at 249).
12. Tests:
    - `FoundationInteractionTests.cpp`: 11-160 (fixtures `FLab`, `MineSteps`), 281-445 (`.Placement`; replaceable at 386-398) and 445-575 (`.Mining`; instant at 498-513);
    - `FoundationBlocksTests.cpp`: 88-132 (`FBlocks`), 487-614 (`.Placement`) and 614-750 (`.Shapes` against the facts).
13. `Source/Unnamed/Foundation/FoundationInteractionProbe.cpp`: 27-110 (helpers, `Prepare`, `AddCase`, `Record`), 461-524 (the mining group, a template) and 654-690 (the finite group).
14. Only after S6 and S6b land: `FoundationInteractionView.cpp`, the outline drawing near 521, 547 and 734 at 015bb42.

Reading cost: about 1,400 lines.

---

## 3. Data inventory

| What | Path | Count |
|---|---|---|
| Model blocks (`shape: model`), with `properties`, `tags`, `hardness`, `vanilla_item` | `Reference/minecraft-blocks-26.3/block_manifest.json` | 83 blocks, 949 states |
| Per-state facts: `outline`, `collision`, `light`, `replaceable`, `occludes` (6 bits), `fluid`; per-block `offset` | `Reference/minecraft-blocks-26.3/facts/block_state_facts.json` | 616 blocks (614 + water, lava) |
| Resolved block tags, all registry blocks | `Reference/minecraft-blocks-26.3/research/block_tags.csv` | 1,285 blocks, 296 tags, 41 support-family tags |
| Gameplay tags as imported | `Config/Tags/MinecraftBlockTags.ini` (e.g. `Block.SupportsVegetation` :197, `Block.Replaceable` :146) | every vanilla block tag |
| Support tag members | `notes/support_tags.txt` (this folder) | see T2 |
| Black-box survival signatures | `probes/out2/signatures.jsonl` (73 distinct sets), `subjects.jsonl` (949 states), `candidates.tsv` (4,432 states: 614 imported blocks + air, water, lava + 4 tag-only extras: farmland, honey_block, barrier, bamboo_sapling) | 949 x 6 directions x 4,432 |
| Truth table | `notes/survival_table.md`, `notes/truth_table.csv` | 240 rows (state groups x direction) |
| Multi-neighbour cases (lichen and vine faces, dripleaf stem, cactus sides, sugar cane water, mushroom light, double-plant replacement) | `probes/out2/multi.txt` (`MultiProbe.java`) | 7 cases |

**Counts S7 tests can pin:**
- **Hardness:** 66 model blocks are 0 (instant). 17 are above 0: vine, glow lichen, cocoa and sculk vein 0.2; moss carpet, pale moss carpet, big dripleaf and big dripleaf stem 0.1; cactus 0.4; mangrove roots 0.7; bamboo 1.0; the four amethyst blocks, pointed dripstone and sulfur spike 1.5.
- **`replaceable` (facts):** 15 model blocks, plus air, water, lava and snow. No block's flag varies by state.
  - The 15: short grass, fern, dead bush, bush, red shrub, short dry grass, tall dry grass, seagrass, tall seagrass, vine, glow lichen, tall grass, large fern, leaf litter, hanging roots.
- **Several outline boxes:** vine (26 states), glow lichen (114), sculk vein (114), shelf mushroom (8), azalea and flowering azalea (1 each), big dripleaf (24), pale moss carpet (152).
- **Several collision boxes:** shelf mushroom (8 states), azalea, flowering azalea.
- **No box leaves its cell**, including the offset ones.
- **Offsets:**
  - collision moves with the offset: bamboo, pointed dripstone, sulfur spike;
  - outline moves: bamboo, the flowers, dripstone, sulfur spike, propagule and others (facts `outline_moves`).
- **`blocks_motion` model blocks:** mangrove roots, cactus, bamboo, the 4 amethyst blocks, sculk vein, pointed dripstone, sulfur spike.
  - Sculk vein has no collision.
  - These have collision but are not `blocks_motion`: lily pad, cocoa, shelf mushroom, sea pickle, azalea, flowering azalea, moss carpet, big dripleaf, pale moss carpet.
- **No item (12):**
  - another id (9): cocoa (`cocoa_beans`), the 5 coral wall fans (their fans), sweet berry bush (`sweet_berries`), cave vines (`glow_berries`), big dripleaf stem (`big_dripleaf`);
  - none (3): kelp plant, cave vines plant, tall seagrass.

---

## 4. Behaviour, in our own words

### 4.1 Survival (T1): what each model state needs

Every row was measured: one neighbour at a time in the given direction, all other cells air, light 15.

**"Exact"** means the probe's set equals the rule's set over all 4,432 candidate states. Ties between tags with identical members are named.

**Decided by** says what the rule rests on:
- **tag**: the data alone;
- **tag + probe**: the data names the set, and the probe confirms the direction or the extras;
- **facts**: the facts' collision boxes;
- **probe**: only the black-box run shows it.

| Block(s) | Support cell | Rule (exact) | Decided by |
|---|---|---|---|
| short grass, fern, bush, red shrub, dandelion, poppy, blue orchid, allium, azure bluet, 4 tulips, oxeye daisy, cornflower, lily of the valley, closed eyeblossom, firefly bush, sweet berry bush, pink petals, wildflowers; **lower half** of tall grass, large fern, sunflower, lilac, rose bush, peony | below | `#supports_vegetation`: 10 imported blocks (coarse dirt, dirt, grass block, moss block, mud, muddy mangrove roots, mycelium, pale moss block, podzol, rooted dirt), + farmland. Same members: `#supports_stem_fruit`, `#supports_melon_stem_fruit`, `#supports_pumpkin_stem_fruit`. Name decides. | tag |
| dead bush, short dry grass, tall dry grass | below | `#supports_dry_vegetation` (30 imported: sand family, terracottas, dirt family, moss, mud) | tag |
| azalea, flowering azalea | below | `#supports_azalea` (11 imported + farmland: dirt family, clay, moss, mud) | tag |
| mangrove propagule, `hanging=false` (all ages, waterlogged or not) | below | `#supports_mangrove_propagule` (same members as `#supports_azalea`) | tag |
| mangrove propagule, `hanging=true` | above | `#supports_hanging_mangrove_propagule` = mangrove leaves | tag |
| bamboo (all 12 states) | below | `#supports_bamboo` (16 imported, including bamboo itself and gravel, sand) | tag |
| cactus (all ages) | below | `#supports_cactus` (sand, red sand, suspicious sand), or cactus. **Plus blockers:** a side neighbour in `#blocks_motion` or holding lava, or water or lava above. Side rule: breaks exactly when `isSolid()` or lava, for all 1,286 default states, and `isSolid()` equals `#blocks_motion` membership for every imported state. | tag + probe |
| cactus flower | below | Sturdy CENTER top face, or `#support_override_cactus_flower` (cactus, farmland). Stone works. | probe (sturdy) + tag |
| sugar cane (all ages) | below | Sugar cane; or `#supports_sugar_cane` (13 imported) **with water beside the support**. Water beside means one horizontal neighbour of the support with water fluid (a source, flowing water level 1, a waterlogged slab) or `#supports_sugar_cane_adjacently` (frosted ice). Ice does not count. | tag + probe |
| lily pad | below | Any block whose fluid is a water **source** (water level 0, waterlogged blocks, kelp, seagrass ...), or `#supports_lily_pad` (ice, frosted ice). 161 blocks. Water level 1 fails. | tag + probe |
| seagrass; tall seagrass, lower half | below | Sturdy FULL top face, minus `#cannot_support_seagrass` (magma). For tall seagrass, `#cannot_support_kelp` has the same members. | probe (sturdy) + tag |
| upper half of tall grass, large fern, sunflower, lilac, rose bush, peony, tall seagrass; small dripleaf `half=upper` | below | The same block's `half=lower` state (any facing or waterlogged for small dripleaf), nothing else | probe |
| kelp (26 ages), kelp plant | below | Sturdy FULL top face minus `#cannot_support_kelp` (magma), or kelp or kelp plant | probe (sturdy) + tag |
| 5 corals, 5 coral fans | below | Sturdy FULL top face (waterlogged or not: survival is the same) | probe (sturdy) |
| 5 coral wall fans | opposite `facing` (`facing=north` needs a block to the south) | Sturdy FULL face toward the fan | probe (sturdy) |
| amethyst cluster, large, medium and small bud | opposite `facing` (`facing=up` needs a block below) | Sturdy FULL face toward it. Leaves fail. | probe (sturdy) |
| shelf mushroom | opposite `facing` | Sturdy FULL face toward it | probe (sturdy) |
| cocoa | **toward** `facing` (`facing=north` needs the log at north) | `#supports_cocoa` (jungle log, jungle wood, both stripped) | tag + probe |
| leaf litter | below | Sturdy FULL top face (stone, mud: yes; leaves: no) | probe (sturdy) |
| moss carpet; pale moss carpet `bottom=true` | below | Any non-air state, including water and leaves | probe |
| pale moss carpet `bottom=false` | below | Pale moss carpet `bottom=true` (all 81 such states) | probe |
| sea pickle (all) | below | The support's collision touches its top plane (facts), or sturdy FULL top face (adds mud, soul sand, snow 8, barrier). A top slab or leaves hold it; a bottom slab does not. | facts + probe |
| spore blossom | above | Sturdy CENTER bottom face. Leaves fail. | probe (sturdy) |
| hanging roots | above | Sturdy FULL bottom face | probe (sturdy) |
| cave vines (52), cave vines plant (2) | above | Sturdy FULL bottom face, or `#cave_vines` (cave vines, cave vines plant) | probe + tag |
| pale hanging moss | above | Sturdy FULL bottom face, or `#leaves`, or pale hanging moss | probe + tag |
| pointed dripstone, sulfur spike (`vertical_direction=up`) | below | Sturdy FULL top face, or the same block with `vertical_direction=up` (checked for dripstone; sulfur spike shows the same 10/20 pattern) | probe (sturdy) |
| pointed dripstone, sulfur spike (`vertical_direction=down`) | above | Sturdy FULL bottom face, or the same block with `vertical_direction=down` | probe (sturdy) |
| brown and red mushroom | below | At light 15: `#overrides_mushroom_light_requirement` (mycelium, podzol, both nylium) only. At light 0, also any support whose facts `occludes` is 63 (all six faces). On stone it survives at light 0-12 and pops at 13-15; on a bottom slab it never survives. The probe answers every brightness query with one value, so which light is read is not separable. | tag + facts + probe |
| big dripleaf (32) | below | `#supports_big_dripleaf` (10 imported + farmland), big dripleaf, big dripleaf stem | tag + probe |
| big dripleaf stem | below **and** above | Needs both: above, a big dripleaf or a stem; below, `#supports_big_dripleaf` (10 imported + farmland) or a stem (12 default states). Air or stone above: never. | tag + probe |
| small dripleaf `half=lower`, `waterlogged=false` | below | `#supports_small_dripleaf` (clay, moss block) | tag |
| small dripleaf `half=lower`, `waterlogged=true` | below | 12 blocks: `#supports_small_dripleaf` + `#supports_vegetation` (= the members of `#supports_azalea`) | tag + probe |
| vine, a side face F | the neighbour at F | Its face toward the vine is full collision **or** sturdy FULL: stone, leaves, mud, soul sand. **Or** the vine above has face F. | facts + probe |
| vine, `up=true` | above | **Quirk:** the block above has a full collision or sturdy **upper** face. Top slab yes, bottom slab no, azalea yes, leaves yes. | probe |
| glow lichen, sculk vein, one face F | the neighbour at F | Full collision or sturdy face toward it. For the up face this is the bottom face of the block above: bottom slab yes, top slab no. | facts + probe |
| glow lichen, sculk vein, several faces | each face's neighbour | **Every** face must hold (0 of 372 tries with one face supported survive). A vine needs **any** one (75 of 75). | probe |
| mangrove roots | - | Survives anywhere (no rule) | probe |

**Derived predicates (`notes/predicate_check.md`):**
- **Collision full face (facts) and `Block.isFaceFull(getCollisionShape)` (probe)** are identical for every imported state and face.
- **Sturdy FULL** is that set minus the 14 leaves, plus mud, soul sand and `snow[layers=8]`.
  - Mud, soul sand and snow 8 are sturdy on their top and sides, and snow 8 also underneath.
  - Probe-only extras (not in our world): honey block and barrier.
- **Sturdy CENTER** differs from a facts "covers the centre" test in the same leaves, mud, soul sand and snow cases, plus some amethyst, shelf mushroom and dripstone side states. This is why R1 recommends dumping the sturdy bits.

### 4.2 What happens when a neighbour changes (measured with `updateShape`)

| Family | Result when its support goes | Source |
|---|---|---|
| Plants, flowers, grasses, corals and fans, amethyst, cocoa, shelf mushroom, leaf litter, carpets, sea pickle, spore blossom, hanging roots, propagule, big dripleaf, lily pad | `updateShape` returns **air** at once. Waterlogged states also schedule a water tick (`fluid:water@5`), which is not a pop. | probe |
| Both halves of double plants (6 tall plants, tall seagrass, small dripleaf) | Removing either half returns **air** for the other | probe |
| Sugar cane, cactus, bamboo, kelp, kelp plant, cave vines and plant, pointed dripstone and sulfur spike (pointing up) | **Unchanged**, plus a scheduled tick of their own block, 1 game tick later. The tick (not runnable black-box) breaks it. | probe; wiki (Kelp: "Breaking one part of a kelp stalk destroys all kelp blocks above it"; Bamboo the same; Block_update#1.2: cactus uses a scheduled tick) |
| Pointed dripstone and sulfur spike (pointing down, stalactite) | Unchanged plus a tick **2** game ticks later | probe; wiki (Pointed_Dripstone: unsupported dripstone below a broken block drops) |
| Pale hanging moss `tip=false` | Becomes `tip=true` plus a tick; the shape changes before the pop | probe |
| Vine with several faces | Loses the unsupported face (`vine[north=false,...]`), keeps the rest; air when none is left | probe |
| Vine side face with a vine above holding the same face | Unchanged | probe |
| Glow lichen and sculk vein | `updateShape` removes only the unsupported face, for 2 to 6 faces; air when the last face goes. Placement needs every face held, but a neighbour change only trims. | probe |
| Big dripleaf stem (either neighbour removed) | Unchanged plus a tick, 1 game tick later (a stack) | probe |

**What this means for "one level deep" (answer 6):**
- Vanilla has no depth limit. Each pop is a block change that updates its neighbours, so stacks break segment by segment, one tick apart.
- Every other family settles in one pass, apart from the double-plant partner.
- So "one level deep" only changes the outcome for stacks (Q1). They are: sugar cane, cactus (and a cactus flower on top), bamboo, kelp, cave vines, pointed dripstone and sulfur spike, pale hanging moss, vines hanging under vines, and the big dripleaf column.

### 4.3 Placement (where the click decides)

None of these can be black-boxed without a real `Level`: `BlockPlaceContext` reads the level in its constructor. The wiki fills in, and survival (T1) checks the result. Rows marked (inferred) follow from T1 and the state properties, not from a stated rule.

- **Ground plants** (T1 "below" rows) place their default state into the destination (wiki: Flower, Short_Grass).
- **Double plants** write `half=lower` at the destination and `half=upper` above it. The upper cell must be free (replaceable). (inferred; the wiki pages say nothing about the space check.)
- **Coral fan floor and wall:** on top of a block the floor fan; on a side the wall fan, with `facing` = the clicked face, never the bottom (wiki: Coral_Fan). The wall fans have no item of their own (Q7).
- **Amethyst:** `facing` = the clicked face (wiki: Amethyst_Bud, "facing equals the anchoring face").
- **Cocoa:** only on jungle log or wood sides, with `facing` pointing back at the log. T1 shows that only that facing survives. Cocoa has no item in our game (answer 7), so S7 can only seed it.
- **Vine, glow lichen, sculk vein:** the face toward the clicked block. Clicking a bottom face gives the up face. A vine there also needs the quirk rule. (wiki: Vines, Glow_Lichen say faces stack; the click mechanics are (inferred).)
- **Hanging blocks** (spore blossom, hanging roots, pale hanging moss) on the underside of the clicked block; mangrove propagule hanging under mangrove leaves (wiki: Spore_Blossom, Hanging_Roots).
- **Pointed dripstone:** `vertical_direction` from the clicked face (top gives up, bottom gives down). `thickness` depends on neighbours; place `tip` (wiki: Pointed_Dripstone; the merge rules are out of scope).
- **Kelp and seagrass:** only into a water source, or downward-flowing water for kelp (wiki: Kelp, Seagrass).
- **Waterlogging:** today `GetStateForPlacement` always writes `waterlogged=false` (`FoundationBlockPlacement.h:42-43`). S7 should set it when the destination holds a water source, since water states are now replaceable.
- **Stacking** is out of S7's listed scope and stays for later: sea pickle 1-4, petals 1-4, leaf litter 1-4, a second face on a multiface block (wiki: Sea_Pickle, Pink_Petals, Leaf_Litter).
- **Small dripleaf:** two halves like a double plant, with `facing` opposite the player. It needs water to sit on dirt (T1).

### 4.4 Replaceable, mining, items, drops

- **Replaceable:** the facts' flag is the no-argument `canBeReplaced()`. It is constant per block.
  - Answer 4 switches every block to it, which adds lava and snow to air and water.
  - **Snow:** the facts mark all 8 layers replaceable. The wiki's history says multi-layer replacement was removed in 17w47a, but its current text doesn't state the single-layer rule (Q5).
  - **Placing into the lower half of tall grass:** the replaced half's partner gets an `updateShape` from a different block, and returns air. Measured for tall grass and sunflower, both halves, with stone as the new block.
- **Mining:** hardness 0 is instant today (`GetMiningDelta`, `FoundationAuthority.cpp:289-302`, test `FoundationInteractionTests.cpp:498-510`). S7 only adds rows for real plants: dandelion instant; vine 0.2 not instant.
  - Tool speed-ups (shears on vines, swords on bamboo) are wiki facts (`notes/wiki.md`) and stay out of S7.
- **Items:** a block with no `Item` has no block item, and breaking it drops nothing (`FoundationAuthority.cpp:695, 701`). This is answer 7 and needs no change.
- **Drops:** vanilla pops drop item entities (wiki: grass seeds 12.5%, flowers drop themselves, cocoa 1-3 beans). We have no item entities (Q4).

### 4.5 Collision and targeting at 015bb42

- **Playground collision:** `FFoundationBlockWorld::GatherCollision` (`FoundationBlocks.cpp:167-180`) adds one box per solid cell, from `GetCollisionShape`, which is the bounding box of a model state's boxes. Plants have none, which is correct. Shelf mushroom and azalea get one merged box.
- **L_Generated collision:** `AVoxelWorld::GatherCollision` (`VoxelWorld.cpp:476-519`) uses only the voxel table's `bSolid`:
  - model states in `blocks_motion` collide as full cells (wrong for cactus, bamboo, amethyst, dripstone, sulfur spike, and sculk vein, which has no collision);
  - the colliding non-`blocks_motion` states (lily pad, carpets, cocoa, sea pickle, azalea, big dripleaf, shelf mushroom) don't collide at all.
- **Targeting:** `FoundationTargeting.cpp:83` intersects one selection box per cell, the bounding box. That is wrong for vine, lichen and vein with several faces, big dripleaf, pale moss carpet and shelf mushroom.
  - The comment at :87 ("shapes stay inside their cells, so the first hit in traversal order is the nearest") still holds with offsets (checked: no box leaves its cell).
- **Offsets:** no code applies them yet. S6 implements the hash (R6: our hash of x and z, no Y), and S7 must reuse it for the outline (flowers, bamboo, dripstone ...) and collision (bamboo, dripstone, sulfur spike).

---

## 5. Test oracles

All rows below are from the black-box probe. `notes/oracles.md` has every row (about 85 survival rows and 24 pop rows); here is the core set for `Blocks.Survival`, `Blocks.Placement` and `Interaction`.

**Survival (support state in the given cell, all else air, light 15):**

| Subject | Support | Expected |
|---|---|---|
| dandelion | grass_block / dirt / podzol / moss_block / mud / farmland below | survives |
| dandelion | sand / stone / clay / gravel / snow_block below | pops |
| tall_grass[half=upper] | tall_grass[half=lower] below | survives |
| tall_grass[half=upper] | grass_block, or tall_grass[half=upper], below | pops |
| sunflower[half=upper] | tall_grass[half=lower] below | pops |
| dead_bush | sand, red_terracotta, grass_block below | survives |
| dead_bush | stone below | pops |
| sugar_cane[age=0] | sand below, no water | pops |
| sugar_cane[age=0] | sand below, water sources beside the sand | survives |
| sugar_cane[age=0] | stone below, water beside | pops |
| sugar_cane[age=0] | sugar_cane[age=15] below | survives |
| cactus[age=0] | sand / red_sand / cactus below | survives |
| cactus[age=0] | dirt below | pops |
| cactus_flower | cactus or stone below | survives |
| brown_mushroom | stone below, light 15 | pops |
| brown_mushroom | stone below, light 0 | survives |
| brown_mushroom | mycelium below, light 15 | survives |
| brown_mushroom | bottom oak slab below, light 0 | pops |
| lily_pad | water[level=0] or ice below | survives |
| lily_pad | water[level=1] or stone below | pops |
| kelp[age=0] | stone or kelp_plant below | survives |
| kelp[age=0] | magma_block below | pops |
| seagrass | sand below | survives |
| seagrass | magma_block, or bottom slab (waterlogged), below | pops |
| tube_coral_wall_fan[facing=north] | stone south | survives |
| tube_coral_wall_fan[facing=north] | stone north | pops |
| cocoa[facing=north] | jungle_log north | survives |
| cocoa[facing=north] | jungle_log south, or oak_log north | pops |
| amethyst_cluster[facing=up] | stone below | survives |
| amethyst_cluster[facing=up] | oak_leaves below | pops |
| leaf_litter | stone or mud below | survives |
| leaf_litter | oak_leaves below | pops |
| sea_pickle | top slab or oak_leaves below | survives |
| sea_pickle | bottom slab below | pops |
| moss_carpet | water[level=0] or oak_leaves below | survives |
| vine[up only] | top slab or oak_leaves above | survives |
| vine[up only] | bottom slab above | **pops (quirk)** |
| vine[north only] | stone / oak_leaves / mud north | survives |
| vine[north only] | vine[north only] above | survives |
| vine[north only] | vine[east only] above | pops |
| glow_lichen[up only] | bottom slab above | survives |
| glow_lichen[up only] | top slab above | pops |
| cave_vines | stone or cave_vines_plant above | survives |
| spore_blossom | stone above | survives |
| spore_blossom | oak_leaves above | pops |
| pointed_dripstone[tip, up] | dripstone[frustum, up] below | survives |
| pointed_dripstone[tip, up] | dripstone[frustum, down] below | pops |
| small_dripleaf[lower, waterlogged=false] | clay below | survives |
| small_dripleaf[lower, waterlogged=false] | dirt below | pops |
| small_dripleaf[lower, waterlogged=true] | dirt below | survives |
| pale_moss_carpet[bottom=false] | pale_moss_carpet[bottom=true] below | survives |
| cactus on sand | stone north, or lava north | pops |
| cactus on sand | short_grass north (not `#blocks_motion`) | survives |
| sugar_cane on sand | one water source, flowing water level 1, frosted ice, or waterlogged slab east of the sand | survives |
| sugar_cane on sand | ice east of the sand | pops |
| brown_mushroom on stone | light 12 | survives |
| brown_mushroom on stone | light 13 | pops |
| big_dripleaf_stem | clay below + big_dripleaf above | survives |
| big_dripleaf_stem | clay below, air above | pops |
| glow_lichen[north, east] | stone north only | pops (every face needed) |
| vine[north, east] | stone north only | survives (any face) |

**Neighbour change (updateShape after the named neighbour becomes air):**
- **Air at once:** dandelion (down); tall_grass lower (down or up); tall_grass upper (down); small_dripleaf lower (down or up); tall_seagrass lower (down or up); cocoa (log); big_dripleaf (down); hanging propagule (up).
- **Air with a water tick:** glow_lichen with one face (`fluid:water@5`).
- **Same state plus a tick of the block:**
  - one tick later: sugar_cane, cactus, bamboo, kelp, kelp_plant, cave_vines_plant, dripstone pointing up;
  - two ticks later: dripstone pointing down.
- **Air when the other half is replaced by stone (not removed):** tall_grass and sunflower, both halves.
- **Face trimmed:** glow_lichen with 2-6 faces loses only the face whose support went.
- **Changed state:**
  - pale_hanging_moss[tip=false] becomes tip=true, plus a tick;
  - vine[east, north, up] with its north support removed becomes vine[east, up], the north face removed.

**Counts:**
- `#supports_vegetation` has 10 imported members;
- `#supports_dry_vegetation` 30;
- `#supports_bamboo` 16;
- `#supports_sugar_cane` 13;
- `#supports_cocoa` 4;
- lily pad supports: 161 blocks with at least one supporting state;
- for blocks with at least one passing state, see the per-row counts in `notes/survival_table.md` (corals: 520 candidate blocks; seagrass: 519, magma being the one excluded).

**Mutations that must fail these tests:**
- **Using `#dirt` instead of `#supports_vegetation`:** `#dirt` is coarse dirt, dirt and rooted dirt, so dandelion on grass_block pops.
- **Sturdy taken as full collision, no leaves exception:** leaf litter, amethyst and spore blossom on leaves survive.
- **The vine's up face using the bottom face:** a vine under a bottom slab survives and under a top slab pops.
- **Cocoa facing turned around:** cocoa[facing=north] with the log south survives.
- **No double-plant partner rule:** mining a tall grass top leaves the lower half.
- **Cascade not limited to the agreed depth (Q1):** a three-high sugar cane on sand, with the sand mined, gives a different count of popped canes.
- **Lily pad accepting flowing water:** water[level=1] passes.
- **Sea pickle using sturdy only:** oak_leaves fail. Using "touches the top" only: mud fails.
- **The magma exclusion dropped:** seagrass on magma survives.

**Reproduce (light run, about 16 s plus 15 s load; JDK 25 at `C:\Users\jherediaramirez\.gradle\jdks\eclipse_adoptium-25-amd64-windows.2\bin`):**

```powershell
$sb = 'C:\dev\sandbox\scout-plant-interaction\probes'
$cp = (Get-Content "$sb\classpath.txt")   # server-26.3.jar + the 39 datagen libraries (C:\dev\sandbox\mc-26.3-blocks\datagen)
javac -cp $cp -d "$sb\classes" "$sb\SurvivalProbe.java"; javac -cp "$cp;$sb\classes" -d "$sb\classes" "$sb\MultiProbe.java"
Push-Location $sb; java -Xmx2g -cp "$cp;$sb\classes" SurvivalProbe "$sb\out2" "$sb\imported_plus.txt" "$sb\models.txt"
java -Xmx2g -cp "$cp;$sb\classes" MultiProbe "$sb\out2"; Pop-Location
uv run --no-project --python 3.13 python -I "$sb\analyse2.py" Reference\minecraft-blocks-26.3 "$sb\out2" C:\dev\sandbox\scout-plant-interaction\notes
uv run --no-project --python 3.13 python -I "$sb\checks.py"  Reference\minecraft-blocks-26.3 "$sb\out2"
uv run --no-project --python 3.13 python -I "$sb\oracles.py" "$sb\out2"
```

---

## 6. Draft design

**Data flow.**
- **Today:** registry tags (imported) and per-state facts boxes (imported) feed a new pure-function module, `FoundationBlockSurvival`, which mirrors `FoundationBlockPlacement`. It owns a rule table keyed by block id, built once from the registry, with tag handles resolved once.
- **Missing input, sturdy faces:** with R1, two bitmasks per state in the asset (`StateSupportFull`, `StateSupportCenter`), imported like `StateLight`. The fallback is derived from the collision boxes, plus `Block.Leaves` excluded and an explicit list (mud, soul sand, snow 8).
- **Oracle (Q3):** the probe's signatures as a reference fact file, which `Blocks.Survival` compares against every model state x direction x imported state. That is about 25M rule evaluations, a few seconds in C++, or a fixed sample.

**Rule shapes** (enough for T1):
- **Support cell:** below, above, toward `facing`, opposite `facing`, or per multiface face.
- **Predicate:** `Tag`, `SturdyFull`, `SturdyCenter`, `CollisionOrSturdy`, `CollisionOrSturdyUpperFace` (vine top), `TouchesTopOrSturdy` (sea pickle), `NotAir`, `WaterSourceOrTag` (lily pad), `SameBlockLowerHalf`, or `SameBlockWithValue` (dripstone direction, carpet bottom).
- **Modifiers:**
  - `ExceptTag` (magma);
  - `OrSelf` / `OrTag` (cave vines, kelp);
  - extra conditions: water beside the support (sugar cane); no solid or lava beside, no fluid above (cactus); occluding support or a light limit (mushrooms); and the two-neighbour stem.

**Hooks (015bb42):**
1. **`FFoundationAuthority::CheckPlacement`** (`FoundationAuthority.cpp:263-272`): keep the `bNeedsSupportBelow` path for project blocks, and add `if (!FoundationBlockSurvival::CanSurvive(World, OutDestination, Placed)) return EFoundationReason::CannotSurvive;`.
   - Double plants: the cell above must be known and replaceable, else `DestinationOccupied`.
2. **`GetStateForPlacement`** (`FoundationBlockPlacement.cpp:137-218`): new `EFoundationPlacementRule` families, assigned in `BlockImportData.py:247` (today `"DEFAULT" if water or model`):
   - `DoublePlant`;
   - `FacingClickedFace` (amethyst, coral wall fan, shelf mushroom);
   - `FacingIntoClicked` (cocoa);
   - `MultifaceClicked`;
   - `VerticalFromClicked` (dripstone, sulfur spike);
   - `HangingFromClicked` (propagule);
   - `WaterloggedFromDestination`.
3. **Commit:**
   - `UseOn` writes at :549 and `BreakBlock` removes at :714.
   - Both then call one new helper, `SettleNeighbours(Cell)`: for each of the 6 neighbours, run `FoundationBlockSurvival::OnNeighbourChanged`, `World.Set` the result, and add a pop event.
   - A double plant's partner is removed in the same pass. There is no further recursion beyond Q1's choice.
   - This also covers placing into the lower half of tall grass (the upper pops) and placing a block beside a cactus (the cactus pops).
4. **Replaceable (answer 4):**
   - `BlockImportData.py:246` and `:227` take the facts flag for every block.
   - `CanBeReplaced` (`FoundationBlockPlacement.cpp:103`) is unchanged, plus the snow rule (Q5).
   - Watch `GetDestination` (`FoundationAuthority.cpp:188`): it must stay in sync.
5. **Collision:**
   - **Playground:** `FFoundationBlockWorld::GatherCollision` (`FoundationBlocks.cpp:174-179`) loops over `GetCollisionBoxes`, adding the offset when `bCollisionMovesWithOffset`.
   - **L_Generated:** `AVoxelWorld::GatherCollision` (`VoxelWorld.cpp:502-517`) branches on model states before `bSolid`. Use S6's `EVoxelShape::Model`, or a per-state "model collision" flag in the voxel table, so the hot loop stays a table lookup. Copy the registry boxes and keep `bSolid` (`blocks_motion`) for the heightmap only.
6. **Targeting and outlines:** `FoundationTargeting.cpp:83` intersects each outline box (with offset) and keeps the nearest. The outline drawing in `FoundationInteractionView.cpp` (after S6 and S6b) draws the boxes.
7. **Events:** reuse `EFoundationEventKind::Broken`, or add `Popped`, for pops (`FoundationAuthority.h:213`). `FFoundationResult` carries one cell, so pops are reported as events.

**Switches and gates.**
- A switch for A/B, for example `Options.bPlantRules` (on by default), so the probe's FPS gate and the collision-gather timing can be compared with the old path.
- **Cost:** survival runs at most 7 rule evaluations per edit, which is negligible.
- **Collision gather:** one table lookup per cell, plus box copies for model cells. Against the plan's at-most-+10% gate, this should land well inside, because most cells near the player are not model states. Report the gather time per tick over the plant garden, old against new, with the DLL-swap A/B.

---

## 7. Code-shape sketches (signatures only)

**Against 015bb42: a new `Source/Unnamed/Foundation/FoundationBlockSurvival.h`.**

```cpp
enum class EFoundationSupportCell : uint8 { Below, Above, TowardFacing, AgainstFacing, PerFace, None };
enum class EFoundationSupportTest : uint8 { Tag, SturdyFull, SturdyCenter, CollisionOrSturdy, CollisionOrSturdyUpperFace,
    TouchesTopOrSturdy, NotAir, WaterSourceOrTag, SameBlockLowerHalf, SameBlockWithValue, Always };
struct FFoundationSurvivalRule
{
    EFoundationSupportCell Cell = EFoundationSupportCell::None;
    EFoundationSupportTest Test = EFoundationSupportTest::Always;
    FGameplayTag Tag, ExceptTag, OrTag;   // Block.SupportsVegetation, Block.CannotSupportKelp, Block.CaveVines ...
    bool bOrSelf = false;                 // stacks: sugar cane, cactus, kelp, pale hanging moss
    uint8 Extra = 0;                      // flags: WaterBesideSupport, NoSolidBeside, NoFluidAbove, DarkOrOccluding, StemColumn
};
namespace FoundationBlockSurvival
{
    UNNAMED_API const FFoundationSurvivalRule* FindRule(const FFoundationBlockType& Block);
    UNNAMED_API bool IsSturdy(const FFoundationBlockRegistry&, FFoundationBlockState, EFoundationFace Face, bool bCenter);
    UNNAMED_API bool CanSurvive(const FFoundationBlockWorld&, const FIntVector& Cell, FFoundationBlockState State);
    // updateShape: air (pop), a changed state (vine face removed, pale hanging moss tip), or State unchanged.
    UNNAMED_API FFoundationBlockState OnNeighbourChanged(const FFoundationBlockWorld&, const FIntVector& Cell,
        FFoundationBlockState State, EFoundationFace Changed);
    UNNAMED_API bool GetPartner(const FFoundationBlockWorld&, const FIntVector& Cell, FFoundationBlockState, FIntVector& OutCell);
}
```

**Against 015bb42: `FoundationAuthority.h` / `.cpp`.**

```cpp
// after World.Set(Destination, Placed) (:549) and after World.Set(Cell, {}) (:714)
void FFoundationAuthority::SettleNeighbours(const FIntVector& Cell, FFoundationPlayer& Player);   // one level (Q1)
// loop outline:
// for Face in 0..5: N = Cell + FaceOffset(Face); S = World.Get(N).State; R = OnNeighbourChanged(World, N, S, Opposite(Face));
//   if R != S: World.Set(N, {R}); if R.IsAir(): { if GetPartner(...) World.Set(Partner, {}); AddEvent(Popped/Broken at N); }
```

**Against 015bb42: `FoundationBlocks.h` / `.cpp`, plus `FoundationTargeting.cpp:83`.**

```cpp
bool GetOutlineBoxes(const FIntVector& Cell, FFoundationBlockState, TArray<FBox>& Out) const;   // world cm, offset applied
bool GetCollisionBoxes(const FIntVector& Cell, FFoundationBlockState, TArray<FBox>& Out) const; // used by GatherCollision
// FoundationTargeting: for (const FBox& B : Boxes) if (IntersectBox(B, Eye, Dir, Limit, D, Face) && D < Nearest) {...}
```

**Against 015bb42: `VoxelWorld.cpp:502-517`. Assumes S6's `EVoxelShape::Model`; otherwise use a registry-shape lookup table built in `SetupAuthority`.**

```cpp
if (Info.Shape == EVoxelShape::Model) { AddModelCollision(StateId, Cell, Out); continue; }   // registry boxes + offset
if (!Info.bSolid) continue;   // unchanged for cubes, slabs, layers
```

**Against 015bb42: `FoundationAssets` / `BlockImportData.py`, if R1 is chosen.**

```cpp
UPROPERTY() TArray<uint8> StateSupportFull;    // bit per EFoundationFace: isFaceSturdy(FULL)
UPROPERTY() TArray<uint8> StateSupportCenter;  // isFaceSturdy(CENTER)
```

---

## 8. Re-orientation: what lands before S7, and what to diff first

S7 base = whatever follows S4, S6, S6b and the torch probe. First command:

```powershell
git diff 015bb42..HEAD --stat -- Source/Unnamed/Foundation Source/Unnamed/Voxel/VoxelWorld.cpp Source/Unnamed/Voxel/VoxelBlocks.cpp Source/UnnamedVoxel/Blocks Scripts/Editor/BlockImportData.py Docs/Blocks.md Reference/minecraft-blocks-26.3
```

| Session | Changes files S7 touches | What to check |
|---|---|---|
| **S6** (model rendering) | `Voxel/VoxelBlocks.cpp` (`MakeTable`: `EVoxelShape::Model`, quads; `bSolid` from `blocks_motion` must stay), `UnnamedVoxel/Blocks/VoxelBlockTable.*`, the mesher, `VoxelChunkComponent.*`, `VoxelColumnPipeline.cpp` (cards), `Foundation/FoundationBlockMeshes.*`, `FoundationInteractionView.cpp` (`AddInstance`, held block, galleries), `FoundationTourProbe.cpp`; the **offset hash** (R6), its home and signature | Reuse the offset function for boxes. Check that the `FVoxelStateInfo` shape is usable for the collision branch. |
| **S6b** (Playground plant meshes and materials) | `FoundationInteractionView.cpp`, `FoundationPlayground.cpp`, `FoundationBlockMeshes.*` | The outline drawing (near 521, 547, 734 at 015bb42) moved or changed |
| **S4** (surface rules, density terrain, VoxelConsole) | `Source/UnnamedVoxel/Surface/*`, `Terrain/VoxelDensityTerrain.*`, `Surface/VoxelFreezePass.*`, `Voxel/VoxelWorld.cpp` (`FindCaveShots`), the VoxelConsole, `Scripts/CaptureVoxel.ps1` | `VoxelWorld.cpp` line shifts around `GatherCollision` (476-519) and `SetupAuthority` (280-300) |
| **Torch light probe** | `Voxel/VoxelWorld.cpp` (shot code), maybe the light paths | The same `VoxelWorld.cpp` regions |
| Nobody else | `FoundationAuthority.*`, `FoundationBlockPlacement.*`, `FoundationBlocks.*`, `FoundationTargeting.*`, `FoundationBlockRegistry.*` (unless S6 adds quads API), `BlockImportData.py` (unless S6 changes model fields), the two test files, `FoundationInteractionProbe.cpp` | Expected unchanged; confirm with the diff |

---

## 9. Risks, size, split, and questions for Jesus

**Size: L.** Split into:
- **S7a, rules (M):** survival module, placement families, double plants, settle-neighbours, the replaceable switch, `Blocks.Placement` / `Blocks.Survival`, interaction scenarios 1-5, and `Docs/Blocks.md`.
- **S7b, shapes (M):** collision boxes in both worlds, multi-box targeting and outlines, offsets, the walk-through clip, and the FPS and collision-gather gates.

S7a can start right after S6. S7b needs S6's offset hash and S6b's view changes.

**Risks:**
- **R-a: the reference rebuild for R1 and Q3.** S5 did one; follow its move-aside-and-compare method. Budget about a quarter of the session.
- **R-b: one level deep against stacks** (Q1): floating sugar cane or kelp looks like a bug to players.
- **R-c: the double-plant replace case.** Placing into a tall grass half must clear the partner, or a half-plant is left.
- **R-d: the L_Generated collision change.** It touches a hot loop. Keep the branch a table lookup, and measure with the DLL-swap A/B.
- **R-e: provenance.** At 015bb42, `FoundationBlockPlacement.h:22-23` cites "26.3 bytecode". This phase's rules are data, wiki and black-box only, so S7's new placement families must cite the wiki and T1 instead.
- **R-f: answer 4's switch reaches cubes.** Snow (8 layers) and lava become replaceable, and existing `.Placement` truth tables may change (Q5).

**Questions for Jesus** (recommendation first in each):

1. **Q1, stacks under "one level deep":**
   - **(a) recommended:** one level, with double plants as one unit, but same-block vertical chains pop fully, bounded by the column. Chains: sugar cane, cactus (+ flower), bamboo, kelp, cave vines, dripstone, pale hanging moss, vine under vine, dripleaf column.
   - (b) strictly one level: the upper segments float until touched.
   - (c) the full vanilla cascade, any depth.
2. **Q2, sturdy faces:**
   - **(a) recommended:** dump `support` bits (FULL, CENTER per face) with `FactsDump.java`. This is a reference rebuild.
   - (b) derive them from the collision boxes, with leaves, mud, soul sand and snow 8 as listed exceptions.
3. **Q3, survival oracle:**
   - **(a) recommended:** add the black-box probe to the blocks reference tools, with its signatures as a facts file. `Blocks.Survival` then checks the rules against the whole matrix.
   - (b) pin about 100 rows in the test only.
4. **Q4, drops when a plant pops:**
   - **(a) recommended:** pops drop nothing until item entities exist. This avoids inventory-full refusals mid-cascade.
   - (b) pops return their drop to the breaking player, like a break.
5. **Q5, snow layers under answer 4:**
   - **(a) recommended:** only `layers=1` is replaceable by other blocks, following the wiki history. The facts' flag stays the default for everything else.
   - (b) the facts flag literally: all 8 layers replaceable.
   - (c) keep snow as today.
6. **Q6, underwater plants:**
   - **(a) recommended:** kelp and seagrass place only into water; corals place anywhere and stay alive, since we have no dead coral blocks.
   - (b) corals need water too.
7. **Q7, coral wall fans:**
   - **(a) recommended:** the coral fan item (same id as the fan) places the wall fan when a side is clicked. This is a placement rule, not a new item, so answer 7 holds.
   - (b) floor fans only until the inventory pass.
8. **Q8, the vine's top-face quirk:**
   - **(a) recommended:** reproduce it as measured: the support's upper face.
   - (b) use the face touching the vine, like glow lichen.

---

## Appendix A. Interaction-suite scenarios (expected outcomes)

Cells are in blocks; Unreal +Z is up; Minecraft north is -Y. Each scenario is a `Prepare(...)` plus `AddCase(...)` group in `FoundationInteractionProbe.cpp`, seeded through `Authority.World.Set`.

1. **Flower on grass, mine the grass** (plan's scenario):
   - grass_block at G, holding dandelion: right-click G's top gives `dandelion` at G+Z.
   - Mine G (hardness 0.6, several steps): G is air, and in the same step G+Z is air, with one pop event.
   - With Q4 (a), the inventory gains the grass block only (finite fixture).
2. **Flower refused on stone:** right-click the top of stone with dandelion gives `Rejected` with `CannotSurvive`; the count is unchanged.
3. **Double plant:**
   - Right-click grass at G with tall_grass gives `tall_grass[half=lower]` at G+Z and `[half=upper]` at G+2Z.
   - Mine G+2Z (instant): both cells air.
   - Re-place, then mine G: G+Z and G+2Z are both air (the partner rule).
   - Re-place with stone at G+2Z: the placement is refused.
4. **Place into replaceable plants:**
   - Short grass at G+Z: right-click it with stone puts stone at G+Z, not G+2Z.
   - Tall grass at G+Z/G+2Z: right-click the lower half with stone puts stone at G+Z, and G+2Z pops.
5. **Vine:**
   - A stone column at S. Holding vine, right-click S's south face (+Y): `vine[north=true]` at S+Y.
   - Mine S: the vine pops.
   - Under a top slab at T: right-click T's bottom face gives `vine[up=true]` at T-Z.
   - Under a bottom slab: refused with `CannotSurvive` (the quirk, Q8).
6. **Cocoa** (seeded; no item): jungle log at L, `cocoa[facing=north]` at L+Y. Mine L (hardness 2): the cocoa pops, and nothing is dropped (no item).
7. **Walk through grass** (S7b): walking 5 blocks through a short and tall grass field takes the same time as on bare grass, within a frame. The player never stands on a plant.
8. **Collide with plants** (S7b):
   - walk into a cactus: the player stops 1/16 block (6.25 cm) closer than against a full block;
   - stand on a lily pad: feet at the water surface + 0.09375 blocks (9.375 cm);
   - stand on a moss carpet: + 0.0625 blocks.
9. **Target through a cross** (S7b):
   - aim at a dandelion's empty corner (outside 0.3125-0.6875, shifted by its offset): the ray hits the block behind;
   - aim at its centre: the dandelion.
