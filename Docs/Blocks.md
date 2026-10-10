# Blocks

The Playground's block vocabulary: every full-cube block and slab of Minecraft Java 26.3, plus powder snow and snow layers, plus the 83 blocks decoration places that are drawn from their own baked models, plus the torch and wall torch (616 blocks: 428 cubes, 101 slabs, powder snow, snow and 85 model blocks), plus air, static water and static lava. The registry has 619 blocks with air, 4,424 states and 690 textures. These blocks use the 26.3 protocol and state ids, faces, textures, tints, animation, mining facts and placement rules. Saves store `{id, properties}`, never numbers. The research is in [Minecraft block reference](MinecraftBlockReference.md). Verification, decisions and gaps are in the [blocks record](Records/2026-10-08-Blocks.md).

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
- `T_BlockTextures`: 946 layers at 16 x 16, the 945 block frames plus one white layer, with vanilla mipmaps (each texture's `mipmap_strategy` and `alpha_cutoff_bias`).
- `T_WaterFlowTextures`: 48 layers at 32 x 32, lava 16 and then water 32 (alphabetical; the array keeps its name).
- `T_BlockIcons`: the GUI atlas: every block layer, then the 12 item sprites (`textures/item`) model blocks' icons use.
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
- **Item ids:** item ids are the 26.3 item names, and a block's item has the block's id (the user's choice; the other items are in [Items](Items.md)). Each block whose vanilla item has its own id gets one: 601 block items with the vanilla name, stack size and GUI tint. `AddBlockItems` adds them to the rules. Powder snow has none, because its vanilla item is `powder_snow_bucket`; nor do the 13 model blocks whose vanilla item has another id (cocoa, sweet berry bush, cave vines, big dripleaf stem, the wall fans, the wall torch; kelp plant, cave vines plant and tall seagrass have none). The reference records each model block's `vanilla_item` for the inventory pass that adds those items. The `snow` item sits in Natural Blocks at index 19.
- **Project blocks:** `UFoundationInteractionSettings::Blocks` (empty by default) appends blocks after the imported ones, from protocol id 1286 and state id 35723. A project block whose id is already imported is skipped with a warning.
- **Retired, never reused:** `dummy_block` and `dummy_block_item`. Blocks replaced them, and the loadout now starts with `stone`.

## States and shapes

- **Shapes:** full cubes have one shape. Slabs are bottom, top or double. Snow layers (`snow`, layers 1-8) use `layers_1` to `layers_7` (`EFoundationBlockShape::Layers1..Layers7`), boxes k/8 high, and 8 layers is a full cube. Their collision is one layer lower than the outline, as in 26.3, so 1 layer has none; 8 layers collide as a full cube (26.3 collides at 7/8). Placement gives 1 layer, with no stacking yet. Powder snow is a solid full cube for now, with no sinking. Water has no selection or collision shape. Its surface height is `amount / 9`, so a source is 8/9 tall, and falling water (level 8 and up) is full.
- **Model blocks:** `EFoundationBlockShape::Model` states are drawn from baked quads (`FFoundationBlockRegistry::GetModelParts`, `GetModelQuads`): per state a list of parts, each with its alternatives (random variant lists keep every entry: lily pad, sea pickle, bamboo). Quads carry positions, UVs, texture, `tintindex`, cull face, facing, shade direction (`shade_direction_override`) and light; models carry their ambient occlusion flag. Outline and collision boxes, per-state light, water and the position offset (kind, range, and whether the shapes move with it) come from the server facts (`facts/block_state_facts.json`), never from the models. Since phase 6 S7 a model state's outline and collision are its own boxes (`FFoundationBlockWorld::GetOutlineBoxes`, `GetCollisionBoxes`), moved by the block's offset where the facts move them, with the mapping the renderers use (`FoundationBlockOffsets`). Targeting tests each box, the outline draws each, and collision gathers each, in the Playground and in generated worlds. The single-box getters (`GetSelectionBox`, `GetCollisionBox`) give their bounds, which the preview, the mining overlay and the placement obstruction check use. The voxel world draws model states since phase 6 S6, in their own chunk section ([Block models](VoxelWorld.md#block-models)), and the Playground since S6b (Rendering, below); placement follows the plant rules (Placement, below).
- **Waterlogging:** `waterlogged` is a real property. Placement sets it to false, except that a model block placed into a water source is waterlogged (S7). Waterlogged and always-water model states (kelp, seagrass, corals) report water through `GetFluid`.
- **Still lava:** a fluid like water, now in the block reference, `DA_Blocks` and the registry (states 105-120). It is in the opaque layer with light 15, and has no item, no damage and no flow. It is drawn with its animated `lava_still` on every face, untinted; `lava_flow` is in the flow array for later flowing lava. It still counts as water for the player's movement, as the stand-in did. The stand-in in `VoxelBlocks::MakeTable` is gone.
- **Mining:** the 26.3 hardness and correct-tool facts plus `Block.Mineable.*` and `Block.IncorrectForTool.*` tags drive mining.
- **Drops:** a broken block drops itself, and a double slab drops 2. This is a provisional rule, not loot tables.

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
| `WallAttached` | wall torch | `facing` the clicked side face, away from its wall |

**The torch** (torch session T2). The `torch` item (`PlacesWallBlock`, from the reference's standing-and-wall items) places `torch` on a top face and `wall_torch` on a side face. Either needs a support that holds it: `FFoundationBlockRegistry::GetTorchSupport` gives each state's faces from `facts/block_support.json`, which `FactsDump.java` measures with the torch's own `canSurvive` (leaves and powder snow hold none, every other full cube all five, an upside-down slab a floor torch, a bottom slab nothing). An underside click places nothing until T3 brings vanilla's choice among the other faces. Survival after placement (popping), light and the coral fans' wall fans are later work.

**Plants and the other model blocks** (phase 6, S7; `FoundationPlantRules`, `FFoundationAuthority::CheckPlantPlacement` and `SettlePlants`). Behaviour comes from the reference's properties and tags, scout H's black-box support table ([the prep](Records/2026-10-10-Voxel-Phase6-Scout-H-Plant-Interaction.md), T1) and minecraft.wiki.
- **State from the click**, by property, for model blocks without an asset rule:
  - `half`: the lower half, and the authority writes the upper half above it. The upper cell must be replaceable (else `DestinationOccupied`, or `OutsideBounds` at the build limit);
  - `facing`: amethyst, shelf mushroom and coral wall fans point away from the clicked block; cocoa faces it; the others face the player;
  - vine, glow lichen and sculk vein: the one face toward the clicked block. A vine is never placed on an underside until Jesus decides its up face (Q8), and a state without a face is refused;
  - `vertical_direction`: up on a top face, down under a block, and on a side against the vertical look;
  - `waterlogged`: in a water source.
- **Survival:** the placed state must survive where it goes (`CannotSurvive`), by the voxel core's one table, `FVoxelPlantSurvival` (S8's ground plants plus S7's attached, hanging, multiface and pointed kinds; `VoxelBlocks::GetPlantSurvival`). A rule that reads an unknown cell refuses, so nothing is placed on the Playground's floor, which is not a block. Sturdy faces are S8's `GetSturdyFaces` until Jesus decides Q2. Corals need no water (Q6), and coral fans place only on floors (Q7).
- **After an edit** (a placement or a break), one level deep (Jesus's answer 6):
  - a double plant is one unit, so mining or replacing either half removes the other;
  - each neighbour of the changed cells that no longer survives pops, with its other half;
  - a vine, glow lichen or sculk vein loses only the faces no longer held while one is left;
  - a pop's neighbours are not checked, so a stack (sugar cane, cactus, kelp, dripstone) loses only the segment next to the edit (Q1 open);
  - a rule that reads an unknown cell keeps the plant;
  - pops are `Popped` events and drop nothing (Q4).
- **Replaceable:** every block takes the facts' flag (Jesus's answer 4), except snow, which stays not replaceable until Jesus decides its layers (Q5).
- **Mining:** hardness 0 breaks at once (68 of the 85 model blocks, the torches included); vines, lichen, cocoa and sculk vein take 0.2.
- `Options.bPlantRules` switches all of this off, for A/B runs.

## Rendering

- **Meshes:** each `(section, mesh, layer)` is one instanced static mesh in `UFoundationInteractionView`. The runtime meshes (`FoundationBlockMeshes`) are a cube, bottom and top slabs, eight water heights and seven snow layer heights, then one per baked model (303), built on first use.
- **Model meshes** (phase 6 S6b): a model's quads in order, without the 16 that repeat an earlier quad exactly (as the voxel world drops them). Normals and tangents follow each quad's shade face, so crosses are lit as up, as in the voxel world. UV 0 is the quad's texture UV. UV 1 is (shade face, the quad's own code), in full-precision UVs: texture and tint bit as face codes hold them, no UV transform, the element light in bits 16-19, and bit 20 marking it (under 2^21, so interpolation cannot move it past the materials' rounding). The `M_Block*` materials use that code instead of the instance's face code, and emit the brighter of the state's light and the element light (the firefly bush's overlay). The cube meshes keep UV 1's second channel at 0. A quad lying on an earlier quad's four corners (only the firefly bush's emissive overlay, 4 quads) is lifted 1/512 block along its facing. UE 5.8 clips masked materials only in the depth prepass (`r.EarlyZPassOnlyMaterialMasking` defaults to 1), so a coplanar overlay passed the base pass's depth test across the whole bush and drew its transparent black texels: the bush drew black with lit fireflies.
- **Model instances:** one per part of a state. Each part draws one alternative (`VoxelModels::PickAlternative`), and the whole state moves by `VoxelModels::GetOffset`, so the Playground and the voxel world agree cell for cell. The layer is the block's own (moss carpet is opaque). A state that holds water also draws a water source in its cell (full height under water), as the voxel world draws held water. `GetDrawnCellCount` counts cells, and `GetInstanceCount` counts instances; a vine with no face has none.
- **Per-instance data:** nine floats per instance: six face codes, an overlay code, the packed RGB tint and the state's light emission (`GetLightEmission`; sea pickles and cave vines vary by state).
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

## Playground gallery

Built by `AFoundationPlayground::AddBlockGallery` and restored by every reset, like the starter blocks. Signs label each part.

| Part | Where (cells) | Contents |
|---|---|---|
| Showcase | from (16, 14, 0), rows three cells apart, reading east | One row per acceptance shot. **Grass:** tint and overlay, snowy under a snow block. **Leaves:** cutout and tints. **Translucent:** glass, stained glass, tinted glass, ice. **Axis and facing:** oak log x/y/z, furnace each way, observer up. **Slabs:** bottom, top and double. **Water:** levels 0-8. **Animated:** sea lantern, magma, prismarine, crimson stem, command block, lit smoker. **Plants** (row 7, `PlantRow`): short grass, poppy, tall grass (both halves), pink petals, leaf litter, moss carpet, firefly bush, waterlogged sea pickles, seagrass, lily pad, bamboo, amethyst cluster. |
| Registry gallery | from (-70, -22, 0), 24 columns, every other cell | Every block's default state in registry order, model blocks included (26 rows). |
| State gallery | from (-200, -58, 0), 59 columns, every other cell | Every state in state-id order, as Minecraft's debug world, model states included (4,419 states, 75 rows). |

## Creative inventory

In the Playground, the inventory key (E) opens a reproduction of the 26.3 creative inventory, `SHudCreativeScreen` in `Source/Unnamed/Hud/`. It replaces the routing fixture, which other maps still use.

- **Layout:** the vanilla screen at the GUI scale, using vanilla art. Tabs, grid, scroller and hotbar use the 26.3 coordinates.
- **Tabs:** the vanilla tab order and contents, from `CreativeModeTab.buildContents()`.
- **Search:** filters by name or id, and `#` filters by block tag. `T` opens search.
- **Interaction:** tooltips and the carried stack. Clicks, shift-clicks, number keys, picking up and deleting follow vanilla `slotClicked`.
- **Rendering:** `FHudCreativeModel` holds the pure tab, scroll and click rules. 3D item icons come from `HudBlockIcon`: top, east and north faces at shades 1.0, 0.68 and 0.44. Model blocks' items draw their item model: 62 flat sprites (an icon atlas tile, tinted like their layer) and 10 block models (cactus, azalea, flowering azalea, the moss carpets, mangrove roots, big and small dripleaf, shelf mushroom, spore blossom) projected from their baked quads under the model's gui transform, back faces culled, far quads first.
- **Hotbar:** the in-game hotbar draws block items with the same 3D icons.
- **Commits:** every inventory change is the `SetCreativeSlot` authority command (slots 0-8 and 9 for the offhand), gated by `FFoundationLabOptions::bCreativeInventory`.

## Budgets and known issues

- **Texture codes:** face and quad codes hold a texture index in 10 bits: 691 of 1,024 are used (white included). The block array holds 946 of D3D12's 2,048 slices. Both are asserted by the reference verifier and `.Textures`. Phase 7's blocks must stay inside both, or widen the field.
- **Water against solids (Playground):** water instances draw every face, so a water face coplanar with a solid neighbour's face z-fights (the starter stone at (12, 6, 0) in the water station; S6b's re-save flipped which wins, about 70-260 pixels per shot). Held water around water-holding model states meets the same faces where it touches solids. Options for later: face-culled water meshes or a small water inset.
- **Plants (S7):**
  - breaking or popping a water-holding plant leaves air, not its water (vanilla leaves the source);
  - project blocks (`UFoundationInteractionSettings`) hold no plant: the survival table knows only the imported blocks;
  - a stack pops one segment per edit (Q1);
  - the vine's up face is never checked (Q8);
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
  - `.Support` and `.Torch`: every state's torch support against the reference; the torch item, the block by clicked face, the wall torch's facing and survival on stone, glass, leaves and slabs.
  - `.Shapes`: slab, water, snow layer, powder snow and lava shapes; every model state's boxes, light, water and offset against the facts.
  - `.PlantShapes`: every model state's moved outline and collision boxes at four cells against the voxel table's offset (collision never leaves its cell; a thick bamboo outline reaches at most 1/16 block out, checked at 256 cells), the gathered boxes, and a ray through a dandelion's empty corner.
  - `.PlantPlacement`: the plant states from the click, and the halves of double plants.
  - `.Replaceable`: every block's flag against the facts (snow pinned as today).
  - `.Survival`: about 80 support rows from scout H's table, then the pop pass through the authority (a flower, both halves, one segment of a stack, trimmed vine and lichen faces, a cactus beside a placed block, unknown cells).
  - `Unnamed.Foundation.Interaction.Plants`: both halves through the authority, replacing grass, refusals, pops dropping nothing, instant mining, and the switch.
  - `.Models`: every baked quad and every model state's parts against the reference, with spot checks.
  - `.ModelMeshes`: the Playground's model meshes: 2,440 baked quads, 2,424 drawn (two triangles each), full-precision UVs, and every vertex's position, texture UV, shade face and own code.
  - `Unnamed.Hud.Creative.Tabs`, `.Clicks` and `.Authority`.
- **Runtime:** `TestFoundationRuntime.ps1 -Suite tour` shots 09-23 cover the gallery and the creative screen (17p and 17q: the plant row). The interaction suite covers stone placement and mining and the hotbar block icon, and checks the drawn cells and the exact instance count (one per part, plus held water) against the world. Its plants group (S7) places a dandelion on grass and mines the grass (the dandelion pops), refuses a dandelion on stone, and places and mines tall grass (both halves go).
