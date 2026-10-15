# Blocks

The Playground's block vocabulary: every full-cube block and slab of Minecraft Java 26.3, plus powder snow and snow layers, plus the 83 blocks decoration places that are drawn from their own baked models, plus the torch and wall torch, plus the 310 survival family blocks of survival B1 (stairs, fences, doors, beds and the rest; see "Survival family blocks (B1)") (926 blocks: 428 cubes, 101 slabs, powder snow, snow and 395 model blocks), plus air, static water and static lava. The registry has 929 blocks with air, 28,552 states and 965 textures. These blocks use the 26.3 protocol and state ids, faces, textures, tints, animation, mining facts and placement rules. Saves store `{id, properties}`, never numbers. The research is in [Minecraft block reference](MinecraftBlockReference.md). Verification, decisions and gaps are in the [blocks record](Records/2026-10-08-Blocks.md).

## Data flow

| Step | What | Where |
|---|---|---|
| Reference | Jar files byte for byte, `block_manifest.json`, `block_states.json`, `block_models.json` (baked models), `textures.json`, `items.json`, server facts (hardness, tools, light, creative tabs, per-state shapes and offsets). Generated, hash-listed, never edited. | `Reference/minecraft-blocks-26.3/` |
| Tags | 202 vanilla block tags as `Block.*` gameplay tags. | `Config/Tags/MinecraftBlockTags.ini` (`BlockImportData.py --write-tags`) |
| Import | Texture arrays, the GUI icon atlas, the creative-screen textures and the registry asset. | `Scripts/Editor/ImportBlocks.py` → `/Game/Blocks` |
| Materials | `M_BlockOpaque`, `M_BlockCutout`, `M_BlockTranslucent`, `M_BlockWater`. | `Scripts/Editor/CreateBlockMaterials.py` |
| Runtime | `FFoundationBlockRegistry::GetDefault()` loads `DA_Blocks` once and builds the lookup tables. | `Source/Unnamed/Foundation/FoundationBlockRegistry.*` |

`/Game/Blocks` (`UUnnamedProjectSettings::BlockRoot`; Python `ProjectPaths.block_asset()`) is always cooked. It holds:

- `DA_Blocks` (`UFoundationBlockAssets`): blocks, states, faces, baked models, textures, creative tabs and the reference SHA-256.
- `T_BlockTextures`: 1,364 layers at 16 x 16, the 1,363 block frames plus one white layer, with vanilla mipmaps (each texture's `mipmap_strategy` and `alpha_cutoff_bias`).
- `T_WaterFlowTextures`: 48 layers at 32 x 32, lava 16 and then water 32 (alphabetical; the array keeps its name).
- `T_BlockIcons`: the GUI atlas: every block layer, then the item sprites (`textures/item`) model blocks' icons use (117 sprite icons and 241 3D icons in all since B1).
- The materials, and `Gui/` for the creative inventory art.

`ImportBlocks.py` verifies an existing `DA_Blocks` and refuses to replace it when the reference changed. To reimport, move the old assets away deliberately.

```powershell
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-blocks-26.3/tools/verify_reference.py
uv run --no-project --python 3.13 python -I Scripts/Editor/BlockImportData.py --write-tags   # then restart the editor
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -run=pythonscript "-script=Scripts\Editor\ImportBlocks.py" -unattended -nop4 -nosplash
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -run=pythonscript "-script=Scripts\Editor\CreateBlockMaterials.py" -unattended -nop4 -nosplash
```

## Ids

- **Block ids:** the vanilla names (`stone`, `oak_slab`, `water`). Protocol ids and state ids match 26.3 exactly. A state's id is the block's first state id plus its properties in mixed radix, last property fastest; air is 0.
- **Saved form:** `FFoundationSavedState {Id, Properties}`, with `Save`/`Load` and `Parse`/`ToString` (`oak_log[axis=x]`). Numbers are never stored. An unknown block id fails to load. A missing property takes the default value. An unknown property or value keeps the default and adds a warning.
- **Item ids:** item ids are the 26.3 item names, and a block's item has the block's id (the user's choice; the other items are in [Items](Items.md)). Each block whose vanilla item has its own id gets one: 887 block items with the vanilla name (601 before survival B1, which added 286), stack size and GUI tint. `AddBlockItems` adds them to the rules. Powder snow has none, because its vanilla item is `powder_snow_bucket`; nor do the 13 model blocks whose vanilla item has another id (cocoa, sweet berry bush, cave vines, big dripleaf stem, the wall fans, the wall torch; kelp plant, cave vines plant and tall seagrass have none). The reference records each model block's `vanilla_item` for the inventory pass that adds those items. The `snow` item sits in Natural Blocks at index 19.
- **Project blocks:** `UFoundationInteractionSettings::Blocks` (empty by default) appends blocks after the imported ones, from protocol id 1286 and state id 35723. A project block whose id is already imported is skipped with a warning.
- **Retired, never reused:** `dummy_block` and `dummy_block_item`. Blocks replaced them, and the loadout now starts with `stone`.

## States and shapes

- **Shapes:** full cubes have one shape. Slabs are bottom, top or double. Snow layers (`snow`, layers 1-8) use `layers_1` to `layers_7` (`EFoundationBlockShape::Layers1..Layers7`), boxes k/8 high, and 8 layers is a full cube. Their collision is one layer lower than the outline, as in 26.3, so 1 layer has none; 8 layers collide as a full cube (26.3 collides at 7/8). Placement gives 1 layer, with no stacking yet. Powder snow is a solid full cube for now, with no sinking. Water has no selection or collision shape. Its surface height is `amount / 9`, so a source is 8/9 tall, and falling water (level 8 and up) is full.
- **Model blocks:** `EFoundationBlockShape::Model` states are drawn from baked quads (`FFoundationBlockRegistry::GetModelParts`, `GetModelQuads`): per state a list of parts, each with its alternatives (random variant lists keep every entry: lily pad, sea pickle, bamboo). Quads carry positions, UVs, texture, `tintindex`, cull face, facing, shade direction (`shade_direction_override`) and light; models carry their ambient occlusion flag. Outline and collision boxes, per-state light, water and the position offset (kind, range, and whether the shapes move with it) come from the server facts (`facts/block_state_facts.json`), never from the models. Since phase 6 S7 a model state's outline and collision are its own boxes (`FFoundationBlockWorld::GetOutlineBoxes`, `GetCollisionBoxes`), moved by the block's offset where the facts move them, with the mapping the renderers use (`FoundationBlockOffsets`). Targeting tests each box, the outline draws each, and collision gathers each, in the Playground and in generated worlds. The single-box getters (`GetSelectionBox`, `GetCollisionBox`) give their bounds, which the preview, the mining overlay and the placement obstruction check use. The voxel world draws model states since phase 6 S6, in their own chunk section ([Block models](VoxelWorld.md#block-models)), and the Playground since S6b (Rendering, below); placement follows the plant rules (Placement, below).
- **Waterlogging:** `waterlogged` is a real property. Placement sets it to false, except that a model block placed into a water source is waterlogged (S7). Waterlogged and always-water model states (kelp, seagrass, corals) report water through `GetFluid`. Breaking or popping a block that holds a water source (waterlogged, kelp, seagrass) leaves `water[level=0]` (`FFoundationBlockRegistry::GetRemainder`, water and sky W2b; minecraft.wiki "Waterlogging"), in the Playground too, where it stays still (fluids flow only in voxel worlds).
- **Still lava:** a fluid like water, now in the block reference, `DA_Blocks` and the registry (states 105-120). It is in the opaque layer with light 15, and has no item, no damage and no flow. It is drawn with its animated `lava_still` on every face, untinted; `lava_flow` is in the flow array: flowing lava's sides and sloped tops draw it, and every lava side under `voxel.Fluids.SlopedTops 2` (water and sky W3; `Docs/VoxelWorld.md`, "Fluid surfaces"). It still counts as water for the player's movement, as the stand-in did. The stand-in in `VoxelBlocks::MakeTable` is gone.
- **Light:** each state's own light from the facts (`GetLightEmission`), full cubes too since B3a: a lit furnace, smoker or blast furnace 13, lit redstone ore 9, a lit redstone lamp 15, the copper bulbs and the respawn anchor by state (`StateLight` in `DA_Blocks` for the 15 full-cube blocks whose states differ).
- **Mining:** the 26.3 hardness and correct-tool facts plus `Block.Mineable.*` and `Block.IncorrectForTool.*` tags drive mining.
- **Drops:** a broken block drops itself, and a double slab drops 2. This is a provisional rule, not loot tables.

## Survival family blocks (B1)

Survival B1 (track T5) brought in 310 blocks as data. They are in the manifest under `added: "survival-B1"`, which `DA_Blocks` keeps as the per-block `bAppendedFamily`. The registry is appended: no earlier id moves.

| Group | Blocks |
|---|---|
| Stairs | 97 |
| Fences, fence gates | 14, 13 |
| Walls | 32 |
| Panes and bars | 26 |
| Doors, trapdoors | 22, 22 |
| Buttons, pressure plates | 15, 17 |
| Lever, ladder | 1, 1 |
| Nether vines | 4 |
| Beds | 16 |
| Chests | 3 (`chest`, `trapped_chest`, `ender_chest`; stand-in box) |
| Farmland, dirt path | 2 |
| Crops, saplings | 8, 9 |
| Cauldrons | 4 |
| Fire, soul fire, campfire, soul campfire | 4 |

- **Totals:** 926 blocks, 28,552 states, 4,852 baked models (most from stair shapes), 965 textures, 1,364 of 2,048 array layers (plus 48 flow layers). Texture codes: 966 of 1,024 used, 58 left. A later phase must widen the code field before it adds more blocks.
- **Items:** 887 block items (286 new). The 9 sapling items (oak, spruce, birch, jungle, acacia, cherry, dark oak, pale oak, poplar) place their sapling block ([Items](Items.md)).
- **Placement:** each B1 block places its default state only. Shapes, connections, the doors' two halves and use come in B2 to B7.
- **Switch:** `foundation.Blocks.Families` (int, default 1; listed in [Breaking](Breaking.md)). 0 hides the B1 blocks' own items from the creative inventory and the B1 blocks from the Playground gallery tails, and refuses to place one (`UnknownBlock`); the 9 sapling items stay in the inventory, as before B1, and placing them is refused; recipes whose result is a B1 block's item craft nothing (`FFoundationAuthority::MakeContainerRules`). The cvar is in `FoundationBlockRegistry.cpp`, the placement guard in `FoundationAuthority.cpp` (`CheckPlacement`), the creative filter in `FoundationPanels.cpp`. The creative screen is cached, so flipping the cvar mid-session does not rebuild it: set it at launch.
- **Chest stand-in:** the three chests draw a 14/16 box, [1,0,1] to [15,14,15], turned by `facing`. Chests and trapped chests pair into double chests (T1 step 18-chest, see [Items](Items.md) "The chest"); the two halves still draw two single boxes until the lid work (T5 B4's render-shape change, then R16 and R17), deferred until after QC. The faces are 16 x 16 tiles cropped (and flipped) from `textures/entity/chest/normal.png`, `trapped.png` and `ender.png` in the 26.3 client jar, stored in `Reference/minecraft-blocks-26.3/stand_ins/chest_<kind>_<face>.png` (`textures.json` `file` and `derived_from`). The item icon is the baked box. See [Stand-ins](StandIns.md).
- **Plants:** `poplar_sapling` joined `VegetationBlocks` in `Source/UnnamedVoxel/Blocks/VoxelPlantSurvival.cpp`, so the voxel core's plant survival (see "Plants" above) covers it.
- **Facts:** `facts/family_survival.json` holds the rule-bearing states of every imported block against air, and every state. `facts/plant_survival.json` stays frozen on `tools/survival_base_ids.txt`. `facts/block_use_sounds.csv` (90 rows) maps doors, trapdoors, gates, buttons, plates and the lever to their open, close and click events, for B3 and B5.
- **Sounds:** the sound reference now has 332 events (3 new step and 3 new fall events appended: `cherry_sapling`, `ladder`, `weeping_vines`). `DA_Sounds` is appended: `ImportSounds.py` accepts the old block entries as an ordered subsequence. `DA_BreakAssets` was rebuilt from the extended reference. It has no append path, is self-contained and is looked up by id.
- **Loot:** B1 blocks' loot tables are held back (`Reference/minecraft-items-26.3/tools/loot_later.txt`). Mining a B1 block drops itself, and since B2a a sapling drops its DA_Items sapling. That is wrong for panes without silk touch, crops, farmland and dirt path until their sessions.

Known gaps:

- Nothing connects and every B1 block places its default state (B2b).
- Doors saved before B3a as one lower half stay one half (no migration); a use opens that half alone.
- Stems use their age-0 tint for every state (per-state tint in B6).
- The water cauldron imports as one translucent block, because `water_still` has partial alpha: a known look issue. Lava and powder snow cauldrons are untinted.
- Campfires draw no cooking items.
- Fire and campfires are data only: no burning or damage until T3 and B6.
- **uvlock for partial faces** (`bake_models.py`): when a face's UV is no transform of its canonical UV, the lock's
  turn is applied after the face's own UV map; whole faces apply it the other way round. Measured over the 26.3 jar
  (`C:\dev\sandbox\b2-blocks\proof\uvlock_orders.py`): the two orders agree on every stair and wall face, and differ
  only on the undersides of fence side bars (y 90/180/270), the undersides of open fence gates, and pressed buttons
  turned x=90. Not changed: it would rewrite accepted baked models, and there is no vanilla ground truth yet. B3 (the
  buttons) checks it against a vanilla probe or screenshot if it matters.

## Shapes and collision (B2a)

Survival B2a (track T5) prepares B2b's connections and stair shapes and widens collision.

- **Switch:** `foundation.Blocks.Shapes` (int, default 1; `FoundationBlockRegistry.cpp`,
  `FoundationBlockFamilies::AreShapesEnabled`). 0 is B1's behaviour. B2b adds placement and the neighbour pass under
  the same switch.
- **Collision gather:** both gatherers (`FFoundationBlockWorld::GatherCollision`, `AVoxelWorld::GatherCollision` for
  model states) also read the cells just below and above a region and keep only the boxes that leave their cell
  vertically and overlap the region with volume (`FoundationCollisionGather::KeepLeaving`). That gives fences, walls and
  closed gates their 1.5-block collision and the pitcher crop's lower half its 1/16 below the cell. Every box stays in
  its cell's column (`Blocks.PlantShapes`), so the region's own cells and these two layers cover every box. With the
  switch at 0 the gather is exactly B1's (`Blocks.ShapesGather`).
- **Oracle:** `facts/shape_updates.json` in the blocks reference: `BlockState.updateShape` answers for the 182 stairs,
  fences, walls, fence gates, panes and bars, measured by `FactsDump.java` through its public `LevelReader` proxy (see
  the reference README, "Shape updates"). B2b's placement and neighbour results are tested against it.
- **Model order frozen:** `tools/frozen_models.txt` lists the 4,867 accepted baked models; the build and the verifier
  require them unchanged at their indices, and later appended blocks bake in batches (`APPENDED_BATCH_STARTS`), each
  batch's item icons before the next batch, so no item-icon index moves.
- **Panes:** a translucent block's model (the glass panes; the water cauldron) draws in the voxel world's translucent
  section instead of the cutout model section, so stained panes keep their half-transparent pixels.
- **Saplings drop themselves:** a block without an item of its own that a DA_Items item places (the 9 saplings) drops
  that item (`FFoundationBlockRegistry::Build`'s `Items`, `bDropsPlacingItem`). B8's loot tables replace this.
- **Hidden saplings pass:** with `foundation.Blocks.Families 0`, a sapling in the main hand passes the use on to the
  offhand, as before B1.

## Shapes and connections (B2b)

Survival B2b (track T5): stairs, fences, walls, panes, bars and fence gates take their shapes from their neighbours,
when placed and when a neighbour changes. Logic in `FoundationBlockFamilies.h/.cpp`, under `foundation.Blocks.Shapes`
(0: B1's default-state placement and no neighbour pass).

- **Families** (`FoundationBlockFamilies::GetFamily`, `EFoundationBlockFamily`): `#minecraft:stairs`, `fences`,
  `walls`, `fence_gates`, and panes and bars (exactly `north`, `east`, `south`, `west`, `waterlogged`, without the fence
  tag). Exactly the oracle's 182 subjects by class (`Shapes.Families`). No asset enum and no DA_Blocks change.
- **Sturdy faces** (`GetSturdyFaces`, public for B3's buttons and doors): a face whose collision boxes, clipped to the
  cell, cover it whole. Leaves and powder snow have none.
- **Connections:** fences join sturdy faces, fences of their kind (wooden with wooden, nether brick with its own) and
  gates whose bar points at them; walls join sturdy faces, walls, panes, bars and those gates; panes and bars join
  sturdy faces, each other and walls. The pumpkins, the jack o'lantern and the melon never join. A wall side is tall
  when the underside of the block above covers a 2-pixel strip from that edge past the centre, else low; the post shows
  under a post-override block or a wall with its post, at an end, a corner or alone, and otherwise only when the block
  above covers the centre while no two opposite sides are tall (minecraft.wiki, Wall).
- **Stairs:** a perpendicular same-half stair in front turns the stair into an outer corner, one behind into an inner
  corner, unless the side it turns toward holds a stair of the same facing and half (VALUES "Stair corners").
- **Gates:** `in_wall` when a wall stands on either side across the facing (VALUES "Fence gates").
- **Placement** (`FoundationBlockFamilies::GetStateForPlacement`, from `FoundationBlockPlacement::GetStateForPlacement`):
  stairs face the player's horizontal look, the half by the slab rule; gates face the look (VALUES
  `blocks.fence_gate_placement`); `waterlogged` in a water source; then the shape settles from the six neighbours as
  the oracle settles a case.
- **Neighbour pass** (`UpdateNeighbourShapes`, one line in `FFoundationAuthority::SettlePlants`, which every edit path
  goes through): each family block among the 4 horizontal neighbours of the edited cell, and a wall below it, takes one
  `updateShape` toward it. One ring, no cascade, unknown neighbours skipped (cells a rule reads beyond them, such as a
  wall's above, read as air); it runs with `Options.bPlantRules` off too. It rings the edited cell only: cells
  `SettlePlants` pops or writes afterwards (a popped plant, an upper half) get no ring of their own. No family block
  reads those today; B3's door halves will.
  World generation and the Playground gallery write states directly and never run it.
- **Oracle:** the tests compare every row of `facts/shape_updates.json`: 1,199,226 single-neighbour rows, 141,960
  cross-family rows, the 32 wall-up rows, the stair singles (16 outer and 16 inner bottom corners) and 512 pairs, and the
  72 gates-in-wall rows.
- **Voxel gather:** `AVoxelWorld::GatherColumns` is `GatherCollision`'s part over the streamed columns, callable with any
  pipeline (`Unnamed.Voxel.World.ShapesGather`).
## Doors, trapdoors and gates (B3a)

Survival B3a (track T5): doors, trapdoors and fence gates place by their own rules and open by hand. Logic in
`FoundationBlockFamilies.h/.cpp`, under `foundation.Blocks.Interact` (default 1; 0: no hand use, no door survival,
and doors and trapdoors place as before B3a). `foundation.Blocks.Shapes` 0 also turns door and trapdoor placement off, as it does B2b's families'.

- **Families:** `Door` (`#minecraft:doors`, 22) and `Trapdoor` (`#minecraft:trapdoors`, 22) join `GetFamily`. They take
  no `updateShape` of their own.
- **Door placement:** the lower half faces the player's horizontal look, and the authority writes the upper half (the
  plant rules' pairing: an occupied upper cell refuses with `DestinationOccupied`, one beyond the world with
  `OutsideBounds`). The hinge, as measured by the headless probe `Tests/Probes/door-hinge-26.3.json` (11,100
  `useOn` placements; VALUES "Door hinges"), counting the full-block-collision cells beside the door per side (its
  lower and its upper cell each count; stone, glass and leaves count, a bottom slab and a fence do not): right with a
  door's lower half on the left only, or with more full cells on the right; else left with a door's lower half on the
  right only, or more full cells on the left; else the half of the cell the click hit, left on the centre line. The
  rule is not symmetric: a door on the left wins over the counts, a door on the right does not (probe part D).
- **Door survival** (`FoundationBlockFamilies::GetSurvival`, in `CheckPlantPlacement` and `CanPlantSurvive`): the
  lower half needs a sturdy top below it (`IsFaceSturdy`), the upper half the same door below. Checked against every
  candidate state of `facts/family_survival.json`'s down probe. Losing the support pops both halves; mining either
  half removes both and yields one door (the mined half drops it, the popped half nothing).
- **Trapdoor placement:** on a side face it faces that face, the half from the click height (the slab rule); on a top
  or bottom face, or a click that replaces the clicked block, its facing is opposite the player's look (minecraft.wiki
  "Trapdoor"), the top half unless the click was on a top face.
- **Use** (`FoundationBlockFamilies::Use`, the fallback of `FFoundationAuthority::FindBlockUse`): flips `open`, both door
  halves in the one command; a gate that faces the player turns to the player's look as it opens (survival.r9.switches,
  F 5071-5135, all 57 door, trapdoor and gate rows tested). `iron_door` and `iron_trapdoor` have no use: a click passes
  to the held item, and the preview shows the placement. `FFoundationAuthority::CommitBlockUse` writes the states,
  settles each cell as an edit and adds a `BlockUsed` event.
- **Sounds:** `BlockUsed` carries the set's event (`GetUseSound`: wooden, cherry wood, bamboo wood, nether wood,
  copper; checked against `facts/block_use_sounds.csv`), played at volume 1 and pitch 0.9 + 0.1U (R5,
  survival.r5.play_calls).
- **Neighbour pass:** since B3a `SettlePlants` also rings every other cell it changed (a written upper half, a pop), so
  a fence beside a door's upper half joins it. A double-plant half that still pairs with its partner (a door opened,
  tall grass replaced by tall grass) keeps it: no `Popped` event, the same world.
- **Collision:** from the states' facts (B1): an open gate has none, an open door's slab is on its hinge side.
## Placement

`FoundationBlockPlacement` ports `BlockPlaceContext` and each family's `getStateForPlacement`: clicked face, `Direction.orderedByNearest` (with Minecraft's sine table) and `fromYRot`. Every placement goes through `FFoundationAuthority`.

| Rule | Blocks | State |
|---|---|---|
| `Axis` | logs, stems, pillars, basalt, hay block, bone block, … | `axis` from the clicked face |
| `FacingPlayer` | furnace, smoker, blast furnace, carved pumpkin, … | horizontal facing toward the player |
| `FacingAwayNearest` | dispenser, dropper, barrel, command blocks | opposite the nearest look direction |
| `FacingNearest` | observer | the nearest look direction |
| `CrafterOrientation`, `JigsawOrientation` | crafter, jigsaw | front and top as vanilla |
| `Slab` | 101 slabs | bottom or top from the click height; a matching slab merges to double |
| `Leaves` | leaves | `persistent=true`, `distance` from neighbouring logs and leaves |
| `Snowy` | grass block, podzol, mycelium | `snowy` when a `#snow` block is above |
| `WallAttached` | wall torch | `facing` away from its wall, which the torch item picks by look (below) |
| families (B2b) | stairs, fences, walls, panes, bars, fence gates | facing, half, connections and shape (above, "Shapes and connections (B2b)") |
| families (B3a) | doors, trapdoors | a door's two halves and hinge, a trapdoor's facing and half (above, "Doors, trapdoors and gates (B3a)") |

**The torch** (torch sessions T2 and T3). The `torch` item (`PlacesWallBlock`, from the reference's standing-and-wall items) places `torch` or `wall_torch`, where a support holds it: `FFoundationBlockRegistry::GetTorchSupport` gives each state's faces from `facts/block_support.json`, which `FactsDump.java` measures with the torch's own `canSurvive` (leaves and powder snow hold none, every other full cube all five, an upside-down slab a floor torch, a bottom slab nothing).
- **Placement by look** (`FoundationBlockPlacement::GetTorchState`), as measured black-box in 26.3 by probe P3 (`Tests/Probes/torch-placement-26.3.json`, 64,896 clicks): the directions nearest the look (`GetNearestLooks`), with the direction into the clicked block first unless the click replaces it (short grass). Up is skipped, down gives the floor torch if the cell below holds one, and a side gives the wall torch against the first side, in that order, that holds one; the first hit wins, and nothing holding places nothing (`CannotSurvive`). So a side or top click on a block that holds a torch always places on it, an underside click places by look, and a click on short grass chooses by look, not by the clicked face. The preview and the placement share the rule.
- **Survival:** a torch pops through S7's one-level cascade when its support stops holding it (an unknown support cell keeps it). It shares the plants' switch, `Options.bPlantRules`.
- **Drops:** a broken wall torch drops a `torch`, and pick block on it selects the torch. Its light is in "Light sources" below. The coral fans' items place their wall fans the same way (below).

**Plants and the other model blocks** (phase 6, S7; `FoundationPlantRules`, `FFoundationAuthority::CheckPlantPlacement` and `SettlePlants`). Behaviour comes from the reference's properties and tags, scout H's black-box support table ([the prep](Records/2026-10-10-Voxel-Phase6-Scout-H-Plant-Interaction.md), T1) and minecraft.wiki.
- **State from the click**, by property, for model blocks without an asset rule:
  - `half`: the lower half, and the authority writes the upper half above it. The upper cell must be replaceable (else `DestinationOccupied`, or `OutsideBounds` at the build limit);
  - `facing`: amethyst, shelf mushroom and coral wall fans point away from the clicked block; cocoa faces it; the others face the player;
  - vine, glow lichen and sculk vein: the one face toward the clicked block. A vine under a block takes its up face (S7b, Jesus's answer to Q8), and a state without a face is refused;
  - `vertical_direction`: up on a top face, down under a block, and on a side against the vertical look;
  - `waterlogged`: in a water source.
- **Coral fans** (S7b, Jesus's answer to Q7): the fan's item places its wall fan by look, as the torch's item does (`FoundationBlockPlacement::GetStandingOrWallState`), where the survival table holds it. The five pairs are the reference's standing-and-wall items (`FoundationPlantRules::GetWallBlock`; the torch's pair comes from `DA_Blocks`). A wall fan has no item, drops its fan's, and pick block gives the fan.
- **Survival:** the placed state must survive where it goes (`CannotSurvive`), by the voxel core's one table, `FVoxelPlantSurvival` (S8's ground plants plus S7's attached, hanging, multiface and pointed kinds; `VoxelBlocks::GetPlantSurvival`). A rule that reads an unknown cell refuses, so nothing is placed on the Playground's floor, which is not a block. Sturdy faces are S8's `GetSturdyFaces` until Q2's follow-up measures them. Corals need no water (Q6). A vine stays while any of its faces holds (glow lichen and sculk vein need every face), and its up face needs the upper face of the block above, as measured: a top slab holds it, a bottom slab does not (S7b, Q8).
- **After an edit** (a placement or a break), one level deep (Jesus's answer 6):
  - a double plant is one unit, so mining or replacing either half removes the other;
  - each neighbour of the changed cells that no longer survives pops, with its other half;
  - a vine, glow lichen or sculk vein loses the faces no longer held while one is left;
  - a pop's neighbours are not checked, except along a stack, which settles to its end (S7b, Jesus's answer to Q1): from each changed segment, the cells of the same stack above and below, while they change. The stacks (`FoundationPlantRules::GetStack`): sugar cane, cactus with its flower, bamboo, kelp, cave vines, pointed dripstone, sulfur spike, pale hanging moss, vine (a vine hanging under a vine) and big dripleaf with its stem;
  - a rule that reads an unknown cell keeps the plant;
  - pops are `Popped` events and drop nothing (Q4).
- **Replaceable:** every block takes the facts' flag (Jesus's answer 4), except snow: one layer is replaced by a placed block, more layers are not (S7b, Jesus's answer to Q5; `FoundationBlockPlacement::IsReplaceable`, which the destination of a click also uses). Snow does not replace a snow layer: stacking layers is not built.
- **Mining:** hardness 0 breaks at once (68 of the 85 model blocks, the torches included); vines, lichen, cocoa and sculk vein take 0.2.
- `Options.bPlantRules` switches all of this off, for A/B runs.

## Rendering

- **Meshes:** each `(section, mesh, layer)` is one instanced static mesh in `UFoundationInteractionView`. The runtime meshes (`FoundationBlockMeshes`) are a cube, bottom and top slabs, eight water heights and seven snow layer heights, then one per baked model (303), built on first use, then one per sloped fluid surface (water and sky W3b): a fluid cell whose surface slopes under `voxel.Fluids.SlopedTops` takes the voxel mesher's geometry (`MakeFluidSurface`, `GetFluidFaces`), on a mesh shared by every surface with the same four corners, with the flow texture on a top that runs downhill and on flowing lava's sides set in its custom data (`UFoundationInteractionView::MakeFluidSurfaceData`; per instance, so no code above 2^15 goes into a vertex channel). A change redraws the fluid cells around it and below them (3 x 3 x 2). Flat fluid cells keep their height boxes, and every cell is still one instance.
- **Model meshes** (phase 6 S6b): a model's quads in order, without the 16 that repeat an earlier quad exactly (as the voxel world drops them). Normals and tangents follow each quad's shade face, so crosses are lit as up, as in the voxel world. UV 0 is the quad's texture UV. UV 1 is (shade face, the quad's own code), in full-precision UVs: texture and tint bit as face codes hold them, no UV transform, the element light in bits 16-19, and bit 20 marking it (under 2^21, so interpolation cannot move it past the materials' rounding). The `M_Block*` materials use that code instead of the instance's face code, and emit the brighter of the state's light and the element light (the firefly bush's overlay). The cube meshes keep UV 1's second channel at 0. A quad lying on an earlier quad's four corners (only the firefly bush's emissive overlay, 4 quads) is lifted 1/512 block along its facing. UE 5.8 clips masked materials only in the depth prepass (`r.EarlyZPassOnlyMaterialMasking` defaults to 1), so a coplanar overlay passed the base pass's depth test across the whole bush and drew its transparent black texels: the bush drew black with lit fireflies. Both worlds draw a model's quads through `FoundationBlockMeshes::GetDrawnModelQuads`. It returns the baked quads, except that each quad textured `torch` is cut at UV v = 8/16, the line between torch.png's flame (rows 6-7) and its stick (rows 8-15). The flame piece and the top face carry element light 1; the stick and the bottom keep 0 (torch session T3b; the registry keeps the baked quads).
- **Model instances:** one per part of a state. Each part draws one alternative (`VoxelModels::PickAlternative`), and the whole state moves by `VoxelModels::GetOffset`, so the Playground and the voxel world agree cell for cell. The layer is the block's own (moss carpet is opaque). A state that holds water also draws a water source in its cell (full height under water), as the voxel world draws held water. `GetDrawnCellCount` counts cells, and `GetInstanceCount` counts instances; a vine with no face has none.
- **Per-instance data:** nine floats per instance: six face codes, an overlay code, the packed RGB tint and the light the instance glows at (`FoundationBlockMeshes::GetDrawnLight`: the state's light emission, `GetLightEmission`, where sea pickles and cave vines vary by state; torch-rule states at `voxel.TorchGlow`).
- **Face codes:** texture layer (bits 0-9), UV transform (bits 10-12), tint (bit 13) and cull face (bit 14). The overlay code adds a present bit (15) and a face mask (bits 16-21) for the grass side overlay.
- **UVs:** `FoundationBlockFaces::GetCanonicalUV` follows vanilla `FaceBakery`.
- **Layers:**
  - Blocks use the 26.3 render layer: 489 opaque, 22 cutout (leaves, grates) and 20 translucent (glass, stained glass, ice).
  - Water uses its own material with the flow array (48 layers: lava 16, then water 32).
- **Tints:** vanilla `BlockColors` at plains biome values:
  - grass `#91BD59`;
  - foliage `#77AB2F`;
  - spruce `#619961`;
  - birch `#80A755`;
  - water `#3F76E4`.

  GUI items use `items/*.json` tints, such as grass `#7CBD6B`. Model blocks' sources come from minecraft.wiki: grass for the tinted crosses, sugar cane and bush (pink petals and wildflowers on their stems only), foliage for vine, the dry foliage colormap for leaf litter (plains `#A37546`), and lily pad `#208030`.
- **Animation:** `UFoundationBlockTextureTable` holds each animated texture's current layer, next layer and blend, updated at 20 ticks per second from the `.mcmeta` frame list. Interpolated textures blend.

## Light sources (voxel world, torch session T3)

- **Which blocks:** every state whose block light is `voxel.LightMinLevel` (10) or more gets a point light, except lava (`VoxelLightSources`):
  - the torch and wall torch (14);
  - glowing cubes: glowstone, sea lantern, shroomlight, the froglights, jack o'lantern, crying obsidian;
  - emitting models: cave vines with berries, sea pickles of three or more.
- **Where the light sits:** a torch's light hangs just past its flame. A cube's light sits just outside its open faces; a buried cube gets none.
- **The pool:** `FVoxelLights`, on `AVoxelWorld`.
  - It lights the nearest `voxel.LightCount` (64, at most 100) sources within `voxel.LightRadius` (48 blocks). Sources under 2 blocks apart share one light.
  - The lights are movable `UPointLightComponent`s that cast shadows and allow MegaLights. Their radius is (level - 1) blocks, and their intensity is `voxel.LightIntensity` cd (600) times level / 14. `voxel.LightTemperature` (2,200 K) sets the colour. These defaults are warm A, Jesus's pick in the torch look round (T3b).
  - With `voxel.LightFalloff` above 0, the falloff is that exponent instead of inverse square, and intensity comes from `voxel.LightBrightness`.
- **When it is live:** only while `r.MegaLights.EnableForProject` is 1 and `voxel.Lights` is on. Otherwise no light component exists. MegaLights is on for the whole project (`Config/DefaultEngine.ini`, since T3b). With no lights it costs about 0.13 ms of GPU and 1.4 ms of game thread a frame (T3).
- **When it updates:** when a column's sources change (they are gathered by the column's mesh job), when the camera moves a block, or when a setting changes. A torch edit costs about 0.01 ms of game thread. `voxel.LightsInfo` reports the pool.
- **The torch's own glow:** `voxel.TorchGlow` (default 1, warm A) is the level the whole torch model glows at: in the voxel world at world build and at `voxel.Regenerate` (or with `-VoxelTorchGlow=N`), and in the Playground as a torch instance is made. 14 is the old white torch. The pool still lights at the real level, 14.
  - The flame quads carry element light 1 on their own (see "Model meshes" above), so `voxel.TorchGlow 0` leaves only the flame glowing. Jesus asked for that look (the stick in its real brown, no glow). It is not the default yet: in a cave the stick reads black (mean sRGB 9/3/1 against the texture's 110/87/55), because the torch's top face shadows it from its own light. The brown stick needs a glow under level 1 (T3c; see [the T3b record](Records/2026-10-10-Torch-T3b.md)).
- **The Playground** has no torch lights.
- **Records:** T3's gates and look captures are in [Records/2026-10-10-Torch-T3.md](Records/2026-10-10-Torch-T3.md).

## Playground gallery

Built by `AFoundationPlayground::AddBlockGallery` and restored by every reset, like the starter blocks. Signs label each part.

| Part | Where (cells) | Contents |
|---|---|---|
| Showcase | from (16, 14, 0), rows three cells apart, reading east | One row per acceptance shot. **Grass:** tint and overlay, snowy under a snow block. **Leaves:** cutout and tints. **Translucent:** glass, stained glass, tinted glass, ice. **Axis and facing:** oak log x/y/z, furnace each way, observer up. **Slabs:** bottom, top and double. **Water:** levels 0-8. **Animated:** sea lantern, magma, prismarine, crimson stem, command block, lit smoker. **Plants** (row 7, `PlantRow`): short grass, poppy, tall grass (both halves), pink petals, leaf litter, moss carpet, firefly bush, waterlogged sea pickles, seagrass, lily pad, bamboo, amethyst cluster. |
| Registry gallery | from (-70, -22, 0), 24 columns, every other cell | Every block's default state in registry order, model blocks included (26 rows). With `foundation.Blocks.Families` 1 a tail follows on a fresh row: one default-state cell per B1 block, plus a sign. |
| State gallery | from (-200, -58, 0), 59 columns, every other cell | Every state in state-id order, as Minecraft's debug world, model states included (4,419 states, 75 rows). With the switch at 1 a tail follows on a fresh row: one default-state cell per B1 block (only the default state), plus a sign. |

## Creative inventory

In the Playground, the inventory key (E) opens a reproduction of the 26.3 creative inventory, `SHudCreativeScreen` in `Source/Unnamed/Hud/`. It replaces the routing fixture, which other maps still use.

- **Layout:** the vanilla screen at the GUI scale, using vanilla art. Tabs, grid, scroller and hotbar use the 26.3 coordinates.
- **Tabs:** the vanilla tab order and contents, from `CreativeModeTab.buildContents()`.
- **Search:** filters by name or id, and `#` filters by block tag. `T` opens search.
- **Interaction:** tooltips and the carried stack. Clicks, shift-clicks, number keys, picking up and deleting follow vanilla `slotClicked`.
- **Rendering:** `FHudCreativeModel` holds the pure tab, scroll and click rules. 3D item icons come from `HudBlockIcon`: top, east and north faces at shades 1.0, 0.68 and 0.44. Model blocks' items draw their item model: 62 flat sprites (an icon atlas tile, tinted like their layer) and 10 block models (cactus, azalea, flowering azalea, the moss carpets, mangrove roots, big and small dripleaf, shelf mushroom, spore blossom) projected from their baked quads under the model's gui transform, back faces culled, far quads first.
- **Hotbar:** the in-game hotbar draws block items with the same 3D icons, and other items with their sprites ([Items](Items.md), "Presentation").
- **Commits:** every inventory change is the `SetCreativeSlot` authority command (Inventory numbers: 0-8 the hotbar, 9-35 the main grid, 36-39 the armor, 40 the offhand; see Items.md, survival step 5a), gated by `FFoundationLabOptions::bCreativeInventory`.

## Budgets and known issues

- **Texture codes:** face and quad codes hold a texture index in 10 bits: 966 of 1,024 are used (white included; 58 left). The block array holds 1,364 of D3D12's 2,048 slices (plus 48 flow layers). A later phase must widen the code field before more blocks come in. Both are asserted by the reference verifier and `.Textures`. Phase 7's blocks must stay inside both, or widen the field.
- **Water against solids (Playground):** water instances draw every face, so a water face coplanar with a solid neighbour's face z-fights (the starter stone at (12, 6, 0) in the water station; S6b's re-save flipped which wins, about 70-260 pixels per shot). Held water around water-holding model states meets the same faces where it touches solids. Options for later: face-culled water meshes or a small water inset.
- **Plants (S7):**
  - project blocks (`UFoundationInteractionSettings`) hold no plant: the survival table knows only the imported blocks;
  - snow placed on a snow layer goes into the cell above instead of adding a layer (stacking is not built);
  - the survival table against the game's matrix (`Blocks.Survival`, counted per cause; S7's kinds agree everywhere):
    - sturdy faces, until Q2's follow-up measures them: 20,931 answers (leaves, mud, soul sand, snow, powder snow and the model blocks as supports);
    - the mushrooms' light: 10,360 (the table reads no light; the dark probe agrees);
    - snow layers have no survival rule: 205,680 (main lane, with snow: generation places snow layers);
    - S8's rules (main lane, small fixes; they move generation): pale moss carpet with `bottom=false` takes any floor instead of a `bottom=true` carpet (1,055,349); a lily pad popped on a water source held in a block, such as a waterlogged slab or leaves (1,521, now 0: since use step 16a, Codex review 13 #1, every waterlogged state holds water; matrix run `Tests/Evidence/2026-10-15/Native-20261015-020856`); an upper half stands on its own upper half (213); a mushroom stands on powder snow or a test instance block in the dark (4);
  - the mushrooms ignore light (S8);
  - a thick bamboo stalk's outline reaches 1/16 block into the next cell at its largest offsets, so a ray through that sliver alone misses it (targeting tests one cell at a time).
- **Shelf mushroom:** its two textures are 32 x 32; the 16 x 16 block array holds their own vanilla mip 1 (S6 decides whether they need more).

## Tests

- **Native:**
  - `Unnamed.Foundation.Blocks.Registry`: counts, every protocol id and state range, tags and facts.
  - `.Codec`: round trip of every state.
  - `.Faces`: every face resolves to a valid layer, plus the UV transforms.
  - `.Textures`: imported pixels against the jar hashes, plus mipmaps (the strict cutout and biased textures regenerated and compared), and the budgets.
  - `.Placement`: truth tables.
  - `.Support` and `.Torch`: every state's torch support against the reference; the torch item, the wall torch's facing and survival on stone, glass, leaves and slabs.
  - `.TorchProbe`: probe P3's matrix, every look order and all 64,896 entries through the authority's `CheckPlacement`. `.TorchSurvival`: the pop cascade, the wall torch's drop and pick block.
  - `.Shapes`: slab, water, snow layer, powder snow and lava shapes; every model state's boxes, light, water and offset against the facts.
  - `.PlantShapes`: every model state's moved outline and collision boxes at four cells against the voxel table's offset (collision never leaves its cell; a thick bamboo outline reaches at most 1/16 block out, checked at 256 cells), the gathered boxes, and a ray through a dandelion's empty corner.
  - `.PlantPlacement`: the plant states from the click, and the halves of double plants.
  - `.Replaceable`: every block's flag against the facts; snow by its layers (one is replaceable), and what replaces a layer.
  - `.Survival`: about 90 support rows from scout H's table; the whole matrix of `facts/plant_survival.json` (every imported state, eight probes, 4,425 candidates: 156 million answers, each difference counted by its cause and pinned); then the pop pass through the authority (a flower, both halves, stacks to their end up and down, trimmed vine and lichen faces, a cactus beside a placed block, unknown cells).
  - `Unnamed.Foundation.Interaction.Plants`: both halves through the authority, replacing grass, refusals, a vine under stone and slabs, coral fans on a side and a top face, one snow layer as the destination, pops dropping nothing, instant mining, and the switch.
  - `Unnamed.Foundation.Interaction.WaterRemains` (W2b): a waterlogged slab and seagrass broken through the command path leave `water[level=0]`, as does seagrass that pops when the stone under it is broken; a dry slab leaves air.
  - `.Models`: every baked quad and every model state's parts against the reference, with spot checks.
  - `.ModelMeshes`: the Playground's model meshes: 2,440 baked quads, 2,444 drawn (two triangles each: 16 exact repeats dropped, 20 added by the torch's flame cut), full-precision UVs, and every vertex's position, texture UV, shade face and own code.
  - `Unnamed.Hud.Creative.Tabs`, `.Clicks` and `.Authority`.
  - `Unnamed.Foundation.Shapes.*` (B2b): `.Families`, `.SingleNeighbour`, `.CrossFamily`, `.WallUp`, `.Stairs`, `.GatesInWall` against every row of `shape_updates.json`; `.Placement` (facing, half, corners, connections, `in_wall`, switch 0); `.Neighbours` (placing and mining through the authority, one ring, a wall under a new block, switch 0). `Unnamed.Voxel.World.ShapesGather`: the voxel gather's outer layers on a streamed column.
  - `Unnamed.Foundation.Interact.*` (B3a): `.Families` (tags, the iron exceptions, every csv sound), `.Switches` (the 57 rows of F 5071-5135, both halves in one command), `.Gates` (re-facing, collision), `.Placement` (facing, hinge samples, refusals, trapdoor half and facing, switch 0), `.Hand` (sneak, off hand, iron passes, the preview), `.Survival` (the rows and every candidate of `family_survival.json`), `.Hinge` (every probe case), `.Edits` (one item from either half, the ring, tall grass replaced).
- **Runtime:** `TestFoundationRuntime.ps1 -Suite tour` shots 09-23 cover the gallery and the creative screen (17p and 17q: the plant row). The interaction suite covers stone placement and mining and the hotbar block icon, and checks the drawn cells and the exact instance count (one per part, plus held water) against the world. Its plants group (S7) places a dandelion on grass and mines the grass (the dandelion pops), refuses a dandelion on stone, and places and mines tall grass (both halves go).
