# Blocks

The Playground's block vocabulary: every full-cube block and slab of Minecraft Java 26.3 (529 blocks: 428 cubes and 101 slabs), plus air and static water. These blocks use the 26.3 protocol and state ids, faces, textures, tints, animation, mining facts and placement rules. Saves store `{id, properties}`, never numbers. The research is in [Minecraft block reference](MinecraftBlockReference.md). Verification, decisions and gaps are in the [blocks record](Records/2026-10-08-Blocks.md).

## Data flow

| Step | What | Where |
|---|---|---|
| Reference | Jar files byte for byte, `block_manifest.json`, `block_states.json`, `textures.json`, `items.json`, server facts (hardness, tools, light, creative tabs). Generated, hash-listed, never edited. | `Reference/minecraft-blocks-26.3/` |
| Tags | 194 vanilla block tags as `Block.*` gameplay tags. | `Config/Tags/MinecraftBlockTags.ini` (`BlockImportData.py --write-tags`) |
| Import | Texture arrays, the GUI icon atlas, the creative-screen textures and the registry asset. | `Scripts/Editor/ImportBlocks.py` → `/Game/Blocks` |
| Materials | `M_BlockOpaque`, `M_BlockCutout`, `M_BlockTranslucent`, `M_BlockWater`. | `Scripts/Editor/CreateBlockMaterials.py` |
| Runtime | `FFoundationBlockRegistry::GetDefault()` loads `DA_Blocks` once and builds the lookup tables. | `Source/Unnamed/Foundation/FoundationBlockRegistry.*` |

`/Game/Blocks` (`UUnnamedProjectSettings::BlockRoot`; Python `ProjectPaths.block_asset()`) is always cooked. It holds:

- `DA_Blocks` (`UFoundationBlockAssets`): blocks, states, faces, textures, creative tabs and the reference SHA-256.
- `T_BlockTextures`: 688 layers at 16 x 16, the 687 block frames plus one white layer, with vanilla mipmaps.
- `T_WaterFlowTextures`: 32 layers at 32 x 32.
- `T_BlockIcons`: the GUI atlas.
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
- **Item ids:** item id = block id (the user's choice). Each block with a vanilla item gets one: 528 block items with the vanilla name, stack size and GUI tint. `AddBlockItems` adds them to the rules.
- **Project blocks:** `UFoundationInteractionSettings::Blocks` (empty by default) appends blocks after the imported ones, from protocol id 1286 and state id 35723. A project block whose id is already imported is skipped with a warning.
- **Retired, never reused:** `dummy_block` and `dummy_block_item`. Blocks replaced them, and the loadout now starts with `stone`.

## States and shapes

- **Shapes:** full cubes have one shape. Slabs are bottom, top or double. Water has no selection or collision shape. Its surface height is `amount / 9`, so a source is 8/9 tall, and falling water (level 8 and up) is full.
- **Waterlogging:** `waterlogged` is a real property. Placement always sets it to false.
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

## Rendering

- **Meshes:** each `(section, mesh, layer)` is one instanced static mesh in `UFoundationInteractionView`. The runtime meshes (`FoundationBlockMeshes`) are a cube, bottom and top slabs, and eight water heights.
- **Per-instance data:** nine floats per instance: six face codes, an overlay code, the packed RGB tint and the light emission.
- **Face codes:** texture layer (bits 0-9), UV transform (bits 10-12), tint (bit 13) and cull face (bit 14). The overlay code adds a present bit (15) and a face mask (bits 16-21) for the grass side overlay.
- **UVs:** `FoundationBlockFaces::GetCanonicalUV` follows vanilla `FaceBakery`.
- **Layers:**
  - Blocks use the 26.3 render layer: 487 opaque, 22 cutout (leaves, grates) and 20 translucent (glass, stained glass, ice).
  - Water uses its own material with the 32-layer flow array.
- **Tints:** vanilla `BlockColors` at plains biome values:
  - grass `#91BD59`;
  - foliage `#77AB2F`;
  - spruce `#619961`;
  - birch `#80A755`;
  - water `#3F76E4`.

  GUI items use `items/*.json` tints, such as grass `#7CBD6B`.
- **Animation:** `UFoundationBlockTextureTable` holds each animated texture's current layer, next layer and blend, updated at 20 ticks per second from the `.mcmeta` frame list. Interpolated textures blend.

## Playground gallery

Built by `AFoundationPlayground::AddBlockGallery` and restored by every reset, like the starter blocks. Signs label each part.

| Part | Where (cells) | Contents |
|---|---|---|
| Showcase | from (16, 14, 0), rows three cells apart, reading east | One row per acceptance shot. **Grass:** tint and overlay, snowy under a snow block. **Leaves:** cutout and tints. **Translucent:** glass, stained glass, tinted glass, ice. **Axis and facing:** oak log x/y/z, furnace each way, observer up. **Slabs:** bottom, top and double. **Water:** levels 0-8. **Animated:** sea lantern, magma, prismarine, crimson stem, command block, lit smoker. |
| Registry gallery | from (-70, -22, 0), 24 columns, every other cell | Every block's default state in registry order. |
| State gallery | from (-200, -58, 0), 59 columns, every other cell | Every state in state-id order, as Minecraft's debug world. |

## Creative inventory

In the Playground, the inventory key (E) opens a reproduction of the 26.3 creative inventory, `SHudCreativeScreen` in `Source/Unnamed/Hud/`. It replaces the routing fixture, which other maps still use.

- **Layout:** the vanilla screen at the GUI scale, using vanilla art. Tabs, grid, scroller and hotbar use the 26.3 coordinates.
- **Tabs:** the vanilla tab order and contents, from `CreativeModeTab.buildContents()`.
- **Search:** filters by name or id, and `#` filters by block tag. `T` opens search.
- **Interaction:** tooltips and the carried stack. Clicks, shift-clicks, number keys, picking up and deleting follow vanilla `slotClicked`.
- **Rendering:** `FHudCreativeModel` holds the pure tab, scroll and click rules. 3D item icons come from `HudBlockIcon`: top, east and north faces at shades 1.0, 0.68 and 0.44.
- **Hotbar:** the in-game hotbar draws block items with the same 3D icons.
- **Commits:** every inventory change is the `SetCreativeSlot` authority command (slots 0-8 and 9 for the offhand), gated by `FFoundationLabOptions::bCreativeInventory`.

## Tests

- **Native:**
  - `Unnamed.Foundation.Blocks.Registry`: counts, every protocol id and state range, tags and facts.
  - `.Codec`: round trip of every state.
  - `.Faces`: every face resolves to a valid layer, plus the UV transforms.
  - `.Textures`: imported pixels against the jar hashes, plus mipmaps.
  - `.Placement`: truth tables.
  - `.Shapes`: slab and water shapes.
  - `Unnamed.Hud.Creative.Tabs`, `.Clicks` and `.Authority`.
- **Runtime:** `TestFoundationRuntime.ps1 -Suite tour` shots 09-23 cover the gallery and the creative screen. The interaction suite covers stone placement and mining and the hotbar block icon.
