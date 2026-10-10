# Items

The Playground's items: every block's item (600, from [Blocks](Blocks.md)), plus the 63 Tier A items of Minecraft Java 26.3 that are not a block's item: the 35 tools (pickaxe, axe, shovel, hoe and sword in wooden, stone, copper, iron, golden, diamond and netherite), the 7 spears, the 14 ore and tier materials, stick, bucket, water bucket, lava bucket, apple, bread and cooked beef. They carry the 26.3 numbers: stack size, durability, attack totals, tool rules and wear. The research is [scout F's record](Records/2026-10-09-Items-Scout-Research.md); the plan and its decisions are [the items phase plan](Records/2026-10-09-Items-Phase-Plan.md).

## Data flow

| Step | What | Where |
|---|---|---|
| Reference | `items.json` (the 63 items' components, tags, creative positions, model definitions), `models.json`, `textures.json` (sprites and opacity masks), `tags.json`, `item_index.json` (all 1,658 ids), the break-time truth table. Generated, hash-listed, never edited. | `Reference/minecraft-items-26.3/` |
| Tags | The 43 vanilla item tags as `Item.*` gameplay tags (`enchantable/mining_loot` is `Item.Enchantable.MiningLoot`). | `Config/Tags/MinecraftItemTags.ini` (`ItemImportData.py --write-tags`) |
| Import | The 70 item sprites as GUI textures and the registry asset. | `Scripts/Editor/ImportItems.py` → `/Game/Items` |
| Runtime | `UFoundationItemAssets::LoadDefault()` loads `DA_Items` once; `UFoundationInteractionSettings::BuildRules` turns its entries into item definitions. | `Source/Unnamed/Foundation/FoundationItemAssets.*` |

`/Game/Items` (`UUnnamedProjectSettings::ItemRoot`; Python `ProjectPaths.item_asset()`) is always cooked. It holds:

- `DA_Items` (`UFoundationItemAssets`): per item its id, name, protocol id, group, material and kind, max stack, max damage, attack totals, the tool component (default speed, ordered rules, damage per block, can destroy blocks in creative), damage per hit, blocking disable time, enchantability, the repair selector and its items, `Item.*` tags, creative positions, model definition and sprite names. Also the 70 models' layers and display transforms (Mojang units, verbatim), the 70 sprites' sizes and opacity masks, and the reference's SHA-256.
- `Gui/item/<sprite>`: the 70 sprites with the creative screen's pixel settings (as `BlockRoot/Gui`).

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

**Presentation until items S3 and S4.** The in-game hotbar and the held view keep the `Icon` key: `pickaxe` or `sword` by kind (other items draw the generic token), tinted by material. The creative inventory draws each item's own sprite (`ItemRoot/Gui`), so Tools & Utilities, Combat, Food & Drinks and Ingredients show their items at the 26.3 positions.

## Loadout

`stone`, `iron_pickaxe`, `iron_sword` (`UFoundationInteractionSettings::Loadout`).

## Tests

- `Unnamed.Foundation.Items.Registry`: all 63 items against `items.json` (read in the test), their definitions in the rules, the 70 models and textures, spot values (iron pickaxe 4 and 1.2, hoes wear 2 per hit, spears have no tool component), no id shared with a block or block item, the merge order, and 26.3's only id clashes (`air`, `wheat`).
- `.Rules`: every tool rule's selector against its `Block.*` tag and the block registry; first-rule speed and drops per tier on imported blocks; the sword's cobweb rule kept and inert.
- `.Merge`: a project item reusing `iron_pickaxe` logs the error and is skipped; a clash with a block item is refused the same way.
- The interaction probe checks the loadout (stone, iron pickaxe, iron sword) and the HUD icons.
