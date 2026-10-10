# Items

The Playground's items: every block's item (601, from [Blocks](Blocks.md)), plus the 63 Tier A items of Minecraft Java 26.3 that are not a block's item: the 35 tools (pickaxe, axe, shovel, hoe and sword in wooden, stone, copper, iron, golden, diamond and netherite), the 7 spears, the 14 ore and tier materials, stick, bucket, water bucket, lava bucket, apple, bread and cooked beef. They carry the 26.3 numbers: stack size, durability, attack totals, tool rules and wear. The research is [scout F's record](Records/2026-10-09-Items-Scout-Research.md); the plan and its decisions are [the items phase plan](Records/2026-10-09-Items-Phase-Plan.md).

## Data flow

| Step | What | Where |
|---|---|---|
| Reference | `items.json` (the 63 items' components, tags, creative positions, model definitions), `models.json`, `textures.json` (sprites and opacity masks), `tags.json`, `item_index.json` (all 1,658 ids), `lang.json` (the tooltip text), the break-time truth table, and two black-box facts files from the server jar's own `ItemStack` (`tools/ItemFacts.java`): the durability bar at every damage value and every item's tooltip lines. Generated, hash-listed, never edited. | `Reference/minecraft-items-26.3/` |
| Tags | The 43 vanilla item tags as `Item.*` gameplay tags (`enchantable/mining_loot` is `Item.Enchantable.MiningLoot`). | `Config/Tags/MinecraftItemTags.ini` (`ItemImportData.py --write-tags`) |
| Import | The 70 item sprites as GUI textures, the same sprites as texture array layers, and the registry asset. | `Scripts/Editor/ImportItems.py` → `/Game/Items` |
| Runtime | `UFoundationItemAssets::LoadDefault()` loads `DA_Items` once; `UFoundationInteractionSettings::BuildRules` turns its entries into item definitions. | `Source/Unnamed/Foundation/FoundationItemAssets.*` |

`/Game/Items` (`UUnnamedProjectSettings::ItemRoot`; Python `ProjectPaths.item_asset()`) is always cooked. It holds:

- `DA_Items` (`UFoundationItemAssets`): per item its id, name, protocol id, group, material and kind, max stack, max damage, attack totals and the attribute modifiers verbatim, the tool component (default speed, ordered rules, damage per block, can destroy blocks in creative), damage per hit, blocking disable time, enchantability, the repair selector and its items, `Item.*` tags, creative positions, model definition and sprite names. Also the 70 models' layers and display transforms (Mojang units, verbatim), the 70 sprites' sizes, opacity masks and array layers, the tooltip's lang keys, and the reference's SHA-256.
- `Gui/item/<sprite>`: the 70 sprites with the creative screen's pixel settings (as `BlockRoot/Gui`). The hotbar and the creative screen draw these.
- `T_ItemTextures` (the 63 sprites of 16 x 16) and `T_ItemTextures32` (the spears' 7 in-hand sprites of 32 x 32): texture arrays for the held items (items S4), mip 0 only, nearest filtering. Each sprite's layer is in `DA_Items` (`FFoundationItemTextureAsset::Layer`). Vanilla's item mips are not measured; the project's mip generator is a port for block sprites.

`ImportItems.py` checks every reference file against `SHA256SUMS.txt` (the full verifier needs Pillow, which editor Python lacks). It verifies an existing `DA_Items` (the reference digest, the item ids, the model and texture counts) and refuses to replace it when the reference changed; a second run writes nothing. To reimport, move the old assets away deliberately, then restart the editor (the runtime loads `DA_Items` once per process).

```powershell
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-items-26.3/tools/verify_reference.py
uv run --no-project --python 3.13 python -I Scripts/Editor/ItemImportData.py --write-tags   # then restart the editor
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -run=pythonscript "-script=Scripts\Editor\ImportItems.py" -unattended -nop4 -nosplash
```

## Ids

- **Item ids** are saved ids: the 26.3 item names (`iron_pickaxe`, `stick`, `water_bucket`). A block's item has the block's id. Only `air` and `wheat` share a block's id in 26.3 without being that block's item (`item_index.json`); neither is an item here.
- **Retired, never reused:** `dummy_block_item` (blocks replaced it), `practice_pickaxe` and `practice_sword` (the 26.3 iron tools replaced them).

## The merge

`BuildRules` builds the authority's item list in this order:

1. Block items (`AddBlockItems`): one per block that has an item.
2. `DA_Items`, in registry order.
3. Project items (`UFoundationInteractionSettings::Items`, empty by default).

An id that is already defined logs an error naming its source, and the later item is skipped; nothing is overridden silently. Inventory slots hold indices into this list for the session only.

Each `DA_Items` entry becomes an `FFoundationItemDefinition`:

| 26.3 data | Definition |
|---|---|
| `max_stack_size` | `MaxStack` |
| `max_damage` | `MaxDurability` |
| attack totals (player base 1 and 4 plus the modifiers; no modifiers: 1 and 4) | `AttackDamage`, `AttacksPerSecond` |
| `tool.default_mining_speed` | `DefaultMiningSpeed` |
| `tool.rules` | `MiningRules`, in order |
| `tool.damage_per_block` (no tool component: 0) | `DurabilityPerBlock` |
| `weapon.item_damage_per_attack` (no weapon component: 0) | `DurabilityPerHit` |
| item tags | `Tags` (`Item.*`) |

**Tool rules.** The first matching rule that states a speed sets the speed; the first matching rule that states `correct_for_drops` decides the drop. A wrong-tier pickaxe keeps its speed and loses the drop (iron on obsidian: speed 6, no drop). `DA_Items` keeps each rule's selector verbatim and the `Block.*` tag a tag selector names (`#minecraft:mineable/pickaxe` is `Block.Mineable.Pickaxe`, `#minecraft:incorrect_for_iron_tool` is `Block.IncorrectForTool.Iron`). A tag is registered only when an imported block carries it. A rule whose tag is not registered (`incorrect_for_diamond_tool` and `incorrect_for_netherite_tool` are empty in 26.3) or that names a single block (the swords' `minecraft:cobweb`, which is not imported) has no tags and matches nothing. `.Rules` fails if such a rule names an imported block, so importing cobweb later means adding block matching. The swords' `#sword_instantly_mines` matches bamboo.

## Presentation

- **Icons (items S3):** as vanilla's GUI, the in-game hotbar and the creative inventory draw a block item's 3D icon and every other item's own 16 x 16 sprite (`FFoundationItemDefinition::Sprite`, `ItemRoot/Gui`), untinted. An item with neither draws the generic token. Tools & Utilities, Combat, Food & Drinks and Ingredients show their items at the 26.3 positions.
- **Durability bar:** once an item is damaged, a black 13 x 2 rectangle at the item's (2, 13) with a 1 px fill (`HudModel::GetDurabilityBar`). The width shrinks from 13 to 0, and the colour runs from green to red by the share left, in single-precision steps. It matches the server jar's answers at every damage value of all 7 max damage values (`facts/durability_bar.json`). Both the hotbar and the creative screen's hotbar row draw it.
- **Tooltips:** the creative screen shows the name (white), then for an item with attribute modifiers a blank line, "When in Main Hand:" (gray) and one dark green line per base modifier with the player's base value added (iron pickaxe: " 4 Attack Damage", " 1.2 Attack Speed"). The text comes from `lang.json`, the numbers are printed with at most two decimals, and the lines are 10 px apart, with 2 more under the name (`HudTooltip`). Without a player the same code gives the black-box `facts/tooltips.json` lines exactly: "+3 Attack Damage" in blue, "-2.8 Attack Speed" in red, no line for a zero amount.
- **Known issues:** the black-box run cannot build a player (`Player` needs a live level), so the base-value form, its leading space and colour, and the zero-amount base lines (the diamond hoe's " 1 Attack Damage") are not probed. The tooltip's 10 px pitch and the 2 px gap under the name are not confirmed by a vanilla capture either. Creative clicks drop a stack's damage: the `SetCreativeSlot` command carries only the item and count.
- **Held view:** the first-person view still uses the `Icon` key (`pickaxe` or `sword` by kind) and the material tint until the extruded meshes (items S4).

## Loadout

`stone`, `iron_pickaxe`, `iron_sword` (`UFoundationInteractionSettings::Loadout`).

## Tests

- `Unnamed.Foundation.Items.Registry`: all 63 items against `items.json` (read in the test), their definitions in the rules, the 70 models and textures, spot values (iron pickaxe 4 and 1.2, hoes wear 2 per hit, spears have no tool component), no id shared with a block or block item, the merge order, and 26.3's only id clashes (`air`, `wheat`).
- `.Rules`: every tool rule's selector against its `Block.*` tag and the block registry; first-rule speed and drops per tier on imported blocks; the sword's cobweb rule kept and inert.
- `.Merge`: a project item reusing `iron_pickaxe` logs the error and is skipped; a clash with a block item is refused the same way.
- The registry test also checks each item's modifiers against `items.json`, its sprite, every texture's array layer and pixels, and the lang keys.
- `Unnamed.Hud.Items.DurabilityBar`: `GetDurabilityBar` against all 4,261 rows of `facts/durability_bar.json`, and the hotbar's bar rectangles and colours at iron damage values from 0 to 250.
- `Unnamed.Hud.Items.Icons`: the hotbar's icon per kind (block, sprite, token), damage and max damage reaching the view, the sprite's 16 x 16 draw, and a GUI texture for every sprite.
- `Unnamed.Hud.Items.Tooltips`: the no-player lines of all 63 items against `facts/tooltips.json`; the player form for the iron pickaxe, the netherite sword, the diamond hoe and the iron spear; the number format; the layout.
- The interaction probe checks the loadout (stone, iron pickaxe, iron sword) and the HUD's sprites. The HUD probe counts the 63 item sprites. The tour's `19h-hotbar-worn` shows the worn tools' bars; `22e` and `22f` show the iron pickaxe's and the netherite sword's tooltips.
