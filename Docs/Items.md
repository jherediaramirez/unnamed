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
- `M_HeldItem`: the held items' cutout material (`Scripts/Editor/CreateItemMaterials.py`, items S4; see "Presentation").
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

## Mining (items S5)

The authority mines by the 26.3 rules (minecraft.wiki "Breaking"), in the Playground and the generated worlds alike:

- **Speed and drops:** the held item's first matching rule that states a speed, else its default (the hand's 1); efficiency, haste, fatigue and break speed are neutral extension points; x0.2 with the eyes in water and x0.2 off the ground. The block drops when it needs no correct tool, or the first matching rule that states `correct_for_drops` says true (`FFoundationAuthority::CanHarvest`). d = speed / hardness / (30 if it drops, else 100).
- **Steps:** hardness -1 never breaks. Hardness 0, or a first step that completes the break, takes the instant path, with no delay after it in survival. Otherwise each step's `MineContinue` adds d until the progress reaches 1 within `CompletionTolerance` (1e-6, `FFoundationRules::CompletesBreak`), and a 5-step delay (`FFoundationInteraction::MiningDelaySteps`) passes before the next break accumulates.
- **The exact table:** the step counts equal `facts/break_times.json`'s exact ticks on all 22,248 entries of the imported blocks (`Unnamed.Foundation.Mining.Tiers`). 17,305 of the 19,288 progressive entries are whole-inverse (1 / d is a whole number). In double precision their progress lands within 4e-8 of 1, so the tolerance completes them on the exact tick, and every other entry is at least 4e-4 short of 1 one step before its last, so the tolerance never shortens one. 27 entries have d = 1 exactly (an iron hoe on leaves, an iron shovel on snow, a golden pickaxe on netherrack, an iron axe on mushroom blocks and vines): the block's single-precision hardness (0.2) leaves d at 0.99999998, and the tolerance puts them on the instant path, as the table and the game's single-precision 6 / 0.2 = 30 have them.
- **Creative** (wiki "Creative"): every block breaks at once, bedrock included, without drops or wear. Holding attack breaks one every 6 steps (the 5-step delay, then the next start); a fresh click breaks at once. An item whose `can_destroy_blocks_in_creative` is false (the swords) breaks nothing (reason `CreativeCannotBreak`), and neither do the spears: `FFoundationRules::CreativeNoBreakTags` holds `Item.Spears`, from the wiki's "Breaking" and "Creative" pages, because 26.3 gives spears no tool component. That rule is not yet black-box verified; clearing the container (one line in `BuildRules`) follows the data alone.
- **Wear (items S6):** with tool wear on (the finite fixture, survival worlds), breaking a block of hardness above 0 costs the tool's `damage_per_block` (1, the swords 2, the spears none: no tool component) and an accepted hit the weapon's `item_damage_per_attack` (2 for pickaxes, axes, shovels and hoes, 1 for swords and spears); an instant block (hardness 0) and an ignored hit cost nothing, and nothing wears in creative. The last point breaks the item once (`ToolBroke`). A creative write keeps the stack's damage (`FFoundationCommand::CreativeDamage`, from the creative screen's stack), and the authority refuses damage outside 0 to the max damage (reason `InvalidDamage`; an item that never wears takes only 0).
- **Known issues:** the post-break delay is 5 steps, then the next start; the wiki says 6 ticks. Whether the game's single-precision steps keep the exact ticks is open: summed in single precision, the bare hand on oak planks or a log takes 61 ticks against the exact 60, and stone 151 against 150, while a wooden pickaxe on deepslate stays at 45. Probe P5 settles both. Drops follow the provisional "drops itself" rule until S5b's loot tables.

## Presentation

- **Icons (items S3):** as vanilla's GUI, the in-game hotbar and the creative inventory draw a block item's 3D icon and every other item's own 16 x 16 sprite (`FFoundationItemDefinition::Sprite`, `ItemRoot/Gui`), untinted. An item with neither draws the generic token. Tools & Utilities, Combat, Food & Drinks and Ingredients show their items at the 26.3 positions.
- **Durability bar:** once an item is damaged, a black 13 x 2 rectangle at the item's (2, 13) with a 1 px fill (`HudModel::GetDurabilityBar`). The width shrinks from 13 to 0, and the colour runs from green to red by the share left, in single-precision steps. It matches the server jar's answers at every damage value of all 7 max damage values (`facts/durability_bar.json`). Both the hotbar and the creative screen's hotbar row draw it.
- **Tooltips:** the creative screen shows the name (white), then for an item with attribute modifiers a blank line, "When in Main Hand:" (gray) and one dark green line per base modifier with the player's base value added (iron pickaxe: " 4 Attack Damage", " 1.2 Attack Speed"). The text comes from `lang.json`, the numbers are printed with at most two decimals, and the lines are 10 px apart, with 2 more under the name (`HudTooltip`). Without a player the same code gives the black-box `facts/tooltips.json` lines exactly: "+3 Attack Damage" in blue, "-2.8 Attack Speed" in red, no line for a zero amount.
- **Known issues:** the black-box run cannot build a player (`Player` needs a live level), so the base-value form, its leading space and colour, and the zero-amount base lines (the diamond hoe's " 1 Attack Damage") are not probed. The tooltip's 10 px pitch and the 2 px gap under the name are not confirmed by a vanilla capture either.
- **Held view (items S4):** the first-person view draws an item as its sprite extruded, the way 26.3's `builtin/generated` does (`FoundationItemMesh`):
  - **Mesh:** a front and a back quad of the whole sprite one model pixel apart (Z 8.5 and 7.5, whatever the sprite's size), plus one side face per opaque pixel edge that borders a transparent pixel or the sprite's border. A side samples its pixel's centre, so it takes exactly that pixel's colour. Built from the sprite's opacity mask in `DA_Items` on first use and cached per sprite (a transient `UStaticMesh`, UV 1's X holding the array layer).
  - **Material:** `M_HeldItem` (`Scripts/Editor/CreateItemMaterials.py`), masked at 0.5 like 26.3's cutout, samples `T_ItemTextures` or, through a dynamic instance's `Sprites` parameter, `T_ItemTextures32` (the spears' in-hand sprites). The arrays are loaded the first time an item is held. A `Glow` term adds emissive = sprite colour x `foundation.Held.Glow` (16,000 nits by default): the display rotation turns a sprite's face away from the Playground's sun, and lit by the scene alone the tour's items read at about a third of their sprites' luma (0.27 to 0.43), where vanilla lights held items in view space. 16,000 brings them to 0.84 to 1.2 under the 75,000 lux sun at EV100 14.5; `foundation.Held.Glow 0` is the scene-lit look.
  - **Model and transform:** the item's model for `firstperson_righthand` (`UFoundationItemAssets::FindModelView`: a select's case naming the context, else its fallback, so spears hold `item/<mat>_spear_in_hand`), placed by that context's display transform verbatim: scale, then rotation, then translation, about the model's centre. Mojang (x, y, z) is Unreal (-z, x, y); the mirror turns Mojang's Rx · Ry · Rz into `FQuat(Y, -a) * FQuat(Z, -b) * FQuat(X, c)`.
  - **Blocks** keep their old pose; vanilla's `block/block` first-person pose (rotation [0, 45, 0], scale 0.4) is a later item. The `Icon` key now only says `block`; the tools' tints are gone.
- **Held view known issues (until probe P1):** the hand's anchor (the pivot's rest at (42, 24, -24) cm in camera space), the hand space's scale (`UFoundationInteractionView::HeldModelBlockCm`, 50 cm per model block so the items keep the old cube tools' size) and the rotation order are provisional, and so is the look call on the tour's `19i-held-*` shots. P1 (scout F's appendix C) fits the anchor and scale from vanilla shots at FOV 70 and 1920 x 1080 and confirms the order; then the first-person FOV is set to match (the Playground camera uses Unreal's default 90 degrees horizontal). The offhand is not drawn, and the swing and equip curves are the project's own. The glow ignores the light level: held items read daylight-bright in dark places until it is scaled by the light at the eye (no such query exists yet).

## Loadout

`stone`, `iron_pickaxe`, `iron_sword` (`UFoundationInteractionSettings::Loadout`).

## Tests

- `Unnamed.Foundation.Items.Registry`: all 63 items against `items.json` (read in the test), their definitions in the rules, the 70 models and textures, spot values (iron pickaxe 4 and 1.2, hoes wear 2 per hit, spears have no tool component), no id shared with a block or block item, the merge order, and 26.3's only id clashes (`air`, `wheat`).
- `.Rules`: every tool rule's selector against its `Block.*` tag and the block registry; first-rule speed and drops per tier on imported blocks; the sword's cobweb rule kept and inert.
- `.Merge`: a project item reusing `iron_pickaxe` logs the error and is skipped; a clash with a block item is refused the same way.
- `Unnamed.Foundation.Mining.Tiers`: every entry of `facts/break_times.json` for the 618 imported blocks (the hand and the 35 tools, 22,248 entries): the authority's steps (`GetMiningDelta` until `CompletesBreak`, as `MineStart` and `MineContinue` decide) and drop eligibility, the blocks' hardness and correct-tool flag, and the table against exact arithmetic on the data's decimals. It pins the table's tallies (288 unbreakable, 2,672 instant, 19,288 progressive, 3,073,507 ticks), the 17,305 whole-inverse entries and the 27 at d = 1, asserts the tolerance's margins (whole-inverse within a tenth of it, every other entry ten times beyond it), and reports how many entries single-precision steps would move, for probe P5.
- `.Scenario`: iron ore in the finite lab through the command path: a wooden pickaxe takes 150 steps and returns nothing, a stone one 23 and returns the block's drop; both wear 1. Then 10 full cubes (dirt, oak leaves, oak log, stone, deepslate, iron and diamond ore, obsidian, bedrock, TNT) with the hand and all 35 tools through `MineStart` and `MineContinue`, against the table.
- `.Wear`: every item that wears (the 35 tools and the 7 spears) against `items.json`: dirt costs `damage_per_block`, TNT nothing, an accepted hit `item_damage_per_attack`; in creative nothing (swords and spears break no block there, and their hit still lands); the last point breaks the item once. Creative writes keep the damage from 0 to the max damage (iron pickaxe 100 and 250, stick 0) and refuse 251, -1 and a stick at 1; the interaction's write (the creative screen's) keeps a diamond sword's 1,561.
- `.Creative`: the data's flag is false for the 7 swords only and is copied for all 63 items; the swords and the spears (the tag rule) break nothing in creative. In creative, stone, bedrock and obsidian break at once with a tool or the bare hand, with no return and no wear, against a survival control (8 steps, returned, worn). The sword and the spear are refused; without the tag rule the spear breaks. Through the interaction, holding attack in creative breaks at steps 0, 6, 12 and 18 of 20, clicking breaks every step, and in survival an instant block (TNT) breaks every step while held.
- The registry test also checks each item's modifiers against `items.json`, its sprite, every texture's array layer and pixels, and the lang keys.
- `Unnamed.Hud.Items.DurabilityBar`: `GetDurabilityBar` against all 4,261 rows of `facts/durability_bar.json`, and the hotbar's bar rectangles and colours at iron damage values from 0 to 250.
- `Unnamed.Hud.Items.Icons`: the hotbar's icon per kind (block, sprite, token), damage and max damage reaching the view, the sprite's 16 x 16 draw, and a GUI texture for every sprite.
- `Unnamed.Hud.Items.Tooltips`: the no-player lines of all 63 items against `facts/tooltips.json`; the player form for the iron pickaxe, the netherite sword, the diamond hoe and the iron spear; the number format; the layout.
- `Unnamed.Foundation.Items.Mesh`: quad counts for known masks (one pixel 6, a full 16 x 16 66, a 2 x 1 bar 8, a 3 x 3 ring 18, empty 2, a full 32 x 32 130); every mesh closed and consistently wound (each directed edge matched by its reverse; two pixels touching only at a corner share a depth edge between four faces), front and back mirrored, every side sampling its own opaque texel's centre, the winding Unreal draws; the mask format; the axis map; display transforms against Mojang's matrices written out; all 70 `DA_Items` sprites; every item's first-person model (the iron pickaxe's and the iron spear's values); and that `M_HeldItem` exists, without loading it (it holds the arrays, which have no platform data under `-NullRHI`).
- The interaction probe checks the loadout (stone, iron pickaxe, iron sword), the HUD's sprites, and the held item at each selection: the pickaxe's and the sword's extruded meshes (sprite, triangle count from the mask, display transform, sprite array), and none for the empty hand. The HUD probe counts the 63 item sprites. The tour's `19h-hotbar-worn` shows the worn tools' bars; `19i-held-*` shows the empty hand (the control) and ten held items (pickaxes of four materials, the iron sword, a stick, an apple, a water bucket, the iron spear's 32 x 32 sprite, and stone to compare), and `19i-held-*-glow0` four of them again with the glow off; `22e` and `22f` show the iron pickaxe's and the netherite sword's tooltips.
