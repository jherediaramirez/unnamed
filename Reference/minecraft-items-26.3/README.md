# Java 26.3 item reference

The import input for the items phase: vanilla Java 26.3's Tier A items (the items plan, [Docs/Records/2026-10-09-Items-Phase-Plan.md](../../Docs/Records/2026-10-09-Items-Phase-Plan.md)): the 35 tools (pickaxe, axe, shovel, hoe and sword in wooden, stone, copper, iron, golden, diamond and netherite), the 7 spears, the 14 ore and tier materials, stick, the 3 buckets and 3 foods (63 items). Per item: its id, components, item model definition, models and sprites. Plus an index of all 1,658 items and the break-time truth table. The research behind it is [scout F's record](../../Docs/Records/2026-10-09-Items-Scout-Research.md). This folder is generated, hash-listed and never edited by hand.

| Source | SHA-1 |
|---|---|
| Client `%APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar` (the block reference's client) | `e877b6a07acd633fb3bb475002175cec036e7b87` |
| Official server jar: the data generator's `--reports` (`minecraft/components/item`, the "Default Components" report, and `registries.json`), in `C:\dev\sandbox\mc-26.3-blocks\reports\reports` | `33680f5f2ac32864d6d7cf5e56a705fdb3e05f4c` |

Also read, pinned by SHA-256 in `manifest.json` and not copied: `Reference/minecraft-blocks-26.3/facts/block_facts.csv` (hardness, correct tool, each block's item), `facts/creative_tabs.json` (creative positions) and `block_manifest.json` (which blocks are imported).

```powershell
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-items-26.3/tools/verify_reference.py
```

The verifier checks every file against `SHA256SUMS.txt` and the block reference inputs against their pins. It re-derives every generated file byte for byte from this folder's own copies and the jar, through the same `derive()` the builder uses. It compares every `assets/` copy with its jar entry and every report copy with the data generator's output when that folder is present. Last, it checks counts and spot values: scout F's appendix A per material, attack totals, and worked break times. It also checks the black-box facts against the data: one durability bar entry per max damage with its items, every damage value from 0 to the max, the ramp's ends, and every item's tooltip lines rebuilt from `items.json` and `lang.json`.

## Files

| Path | What it is |
|---|---|
| `items.json` | The 63 items in registry order. Each has `id`, `protocol_id`, `group` (tool, spear, material, stick, bucket, food), `material` and `kind`, the English `name`, `max_stack`, `max_damage`, `enchantability`, `repair` (the `repairable` selector and its resolved item ids), `attack` (totals and the raw `attribute_modifiers`), `tool`, `weapon`, `food`, `consumable`, `components` (every component name the item carries), `item_tags`, `creative` (tab and index in every tab's display list), `model` (the item model definition) and `textures`. |
| `models.json` | Every model an item definition selects (70): `parent`, `chain` (child first, ending at `builtin/generated`), resolved `textures` and `layers`, `generator`, `gui_light`, and `display`: each context's transform, taken per context from the nearest model in the chain that has it (`display_from`). |
| `textures.json` | The 70 sprites: `size` (16 x 16, or 32 x 32 for the spears' in-hand textures), `png_sha256`, `rgba_sha256` (decoded RGBA), `alpha` (all 70 are `cutout`: alpha only 0 or 255), `opaque_pixels`, and `mask`, the opacity mask: one hex string per row, top row first, leftmost pixel in the highest bit, 1 where alpha is not 0. |
| `tags.json` | Item tags (43): every tag that holds a Tier A item, and the repair tags. Block tags (16): the tool rules' tags (`incorrect_for_*_tool`, `mineable/*`, `sword_efficient`, `sword_instantly_mines`) and `needs_stone_tool`, `needs_iron_tool` and `needs_diamond_tool`. Each has its jar `values` verbatim and its `resolved` ids. |
| `item_index.json` | All 1,658 items in protocol order: `id`, `protocol_id`, `places_block` (the block whose item it is, from `block_facts.csv`: its own id when one matches, else the first block in protocol order), `block_with_same_id`, and `definition` (the first model the definition selects: `block_model` 834, `sprite` 761, `special` 62, `other` 1). Only `air` and `wheat` share a block's id without being that block's item. 14 items place a block with another id (`redstone`, `string`, `wheat_seeds`, `carrot`, `potato`, `powder_snow_bucket`, ...); none is in Tier A. |
| `facts/break_times.json` | The truth table: the hand and the 35 tools on all 1,286 blocks. Per block: `hardness`, `requires_correct_tool`, `imported` (in the block reference), `mineable` kinds, then `ticks` and `drops` in `tools` order. Ticks 0 is instant and -1 unbreakable. |
| `facts/break_times.md` | Scout F's 21 sample blocks, rendered from the JSON. |
| `lang.json` | The tooltip text the items' attribute modifiers need, verbatim from the jar's `en_us.json` (6 keys): the slot header `item.modifiers.mainhand` ("When in Main Hand:"), the attribute names, and the `attribute.modifier.equals`, `plus` and `take` forms of `add_value` (`.0`). |
| `facts/durability_bar.json` | Black box (`tools/ItemFacts.java`): for each max damage among the items (7: 32, 59, 131, 190, 250, 1561, 2031) and every damage value from 0 to it, the server jar's `ItemStack.isBarVisible()`, `getBarWidth()` (0-13) and `getBarColor()` (RGB hex), with the items that share the max (ItemFacts checks they all answer the same). |
| `facts/tooltips.json` | Black box (`tools/ItemFacts.java`): every item's `ItemStack.getTooltipLines` with no player and the creative flag (`TooltipFlag.NORMAL.asCreative()`), each line as its text runs `[text, colour]`. With no player the base modifiers show their own amounts ("+3 Attack Damage" in blue, "-2.8 Attack Speed" in red), and a zero amount shows no line (the diamond hoe shows only the header). In game a player is present and adds its base values; see "Where each value comes from". |
| `loot_tables.json` | The block loot tables of every imported block that has one (602 of the 616; `loot_blocks()`: the 18 ores of `LOOT_BLOCKS` first, then the other blocks of `block_manifest.json` in its order), flattened. Per table: `block`, `file` and `pools` (`[]` when the jar table has none), plus `modifiers` only when the jar table has a `modifier`. Per pool: `rolls`, its ordered `candidates`, then `conditions` and `modifiers` only when present. A candidate is either the ore form (`item`, `conditions`, `count` = `set_count`'s inclusive `[min, max]` (default `[1, 1]`), `bonus` = the Fortune `apply_bonus` (`enchantment`, `formula` `ore_drops` or `uniform_bonus_count`, `bonus_multiplier`) or null, `explosion_decay`) or the general form (`item`, `conditions`, `modifiers`). `predicates` lists each predicate file the tables reference, in order of first use (`tool/can_silk_touch`, `tool/can_shear`). The last key `extensions` documents the vocabulary beyond the ores. |
| `data/minecraft/loot_table/blocks/<id>.json`, `data/minecraft/predicate/tool/can_silk_touch.json` | The jar's 18 ore loot tables and the one predicate they reference, byte for byte. |
| `manifest.json` | Versions and jar SHA-1s, the block reference inputs' SHA-256, the Tier A list, counts, and the generated and copied file lists, and `loot` (the loot blocks, table, predicate and candidate counts). |
| `reports/components/item/<id>.json` | The data generator's Default Components report for the 63 items, byte for byte. |
| `reports/registries-item.json` | The `minecraft:item` registry from `registries.json` (all 1,658 ids and protocol ids), re-serialized. |
| `assets/minecraft/` | Jar files, byte for byte: `items/<id>.json` (63), the `models/item` chains (73: 70 item models plus `generated`, `handheld` and `spear_in_hand`) and `textures/item/*.png` (70). |
| `tools/` | `build_reference.py`, `verify_reference.py` and `ItemFacts.java` (the black-box facts). |

## Where each value comes from

- **Ids, protocol ids, components:** the data generator's reports, as copied. The `tool` and `weapon` entries fill omitted fields with their defaults from minecraft.wiki "Data component format": `default_mining_speed` 1, `damage_per_block` 1, `can_destroy_blocks_in_creative` true, `item_damage_per_attack` 1, `disable_blocking_for_seconds` 0.
- **Attack totals:** the player's base attack damage 1 and attack speed 4, plus the item's `base_attack_damage` and `base_attack_speed` modifiers, rounded to 6 decimals (the modifiers are float values stored as doubles: -2.799999952316284 is -2.8). The raw modifiers are kept.
- **Names:** `en_us.json` in the client jar, by the `item_name` key.
- **Tags:** the client jar's data pack (`data/minecraft/tags`), resolved through nested tags. Optional entries (`required: false`) resolve like the others.
- **Model definitions:** each `items/<id>.json` is a `model` or a `select` on `display_context`. The spears select their 16 x 16 icon for `gui`, `ground`, `fixed` and `on_shelf`, and the 32 x 32 in-hand model otherwise, with `swap_animation_scale` 1.95. Root fields not given use their defaults (`swap_animation_scale` 1, `hand_animation_on_swap` true). The builder stops if a Tier A item's definition uses another type or a tint.
- **Display transforms:** verbatim per context (minecraft.wiki "Model": translation, then rotation, then scale; a missing left-hand entry falls back to the right hand, which the data leaves to the renderer).
- **Creative positions:** the index of the plain entry (`minecraft:<id>`) in each tab's `display` list, from `FactsDump`'s `creative_tabs.json`.
- **Durability bar and tooltip lines:** the server jar's own `ItemStack`, called through its public API only (`tools/ItemFacts.java`, after loading the vanilla data pack as a dedicated server does). The tooltip run has no player: `Player` needs a live level, which a black-box run does not have. So the in-game form of the base modifiers, with the player's base values added (iron pickaxe: 4 Attack Damage, 1.2 Attack Speed), is not in these facts. Its text is the `attribute.modifier.equals.0` form in `lang.json`.

## Break times

The formula is minecraft.wiki "Breaking", for a player standing on the ground, out of water, with no enchantments or effects:

- **Speed:** the first tool rule that matches the block and states a speed, else `default_mining_speed` (the hand: 1).
- **Drops:** the block does not need the correct tool, or the first matching rule that states `correct_for_drops` says true.
- **Progress:** d = speed / hardness / (30 if it drops, else 100). The block breaks at once when d ≥ 1, otherwise after ceil(1 / d) ticks. Hardness 0 is instant; -1 is unbreakable.

The arithmetic is exact (fractions of the data's decimal values). In 35,393 of the 46,296 entries 1 / d is a whole number (`manifest.json`, `break_time_whole_inverse`). In those, the game's single-precision steps could land one tick either side; inexact doubles already do. For example, scout F's `break_times.py` (doubles: 1 / d = 45.00000000000001) gave 46 ticks (2.30 s) for a wooden pickaxe on deepslate, where the exact value is 45 (2.25 s). Probe P5 (scout F's appendix C) settles the in-game count. A tool without a `tool` component (the spears, the buckets, every non-tool item) mines like the hand.

## Rebuild

`build_reference.py` needs the pinned client jar, the report folder (`--reports`, default above) and the block reference. It refuses to run unless this folder holds only `tools/` and `README.md`, so to rebuild, move the accepted folder away first (keeping `tools/` and this README). After a README or tool edit, `build_reference.py --sums-only` rewrites only `SHA256SUMS.txt`. More tiers are added by extending `TIER_A`'s groups (or a new tier list) and rebuilding.

The black-box facts come after the build:

1. Use JDK 25 and a classpath of the unbundled server jar (`C:\dev\sandbox\mc-26.3-blocks\datagen\versions\26.3\server-26.3.jar`) plus the 39 jars of its `libraries` folder.
2. Compile with `javac -cp <classpath> -d <temp> tools/ItemFacts.java`.
3. Run `java -cp <temp>;<classpath> ItemFacts <this folder>` outside the project, since the game leaves a `logs/` folder in the working directory. It reads `items.json` and writes the two `facts/` files.
4. Run `build_reference.py --sums-only`.

## Loot tables

`loot_tables.json` flattens each table the way the game reads it. A table is `minecraft:block` (`random_sequence` is ignored; a table-level `modifier` is kept as `modifiers`); each pool has `rolls` (an integer), no bonus rolls and exactly one entry; a pool's `condition` and `modifier` are kept as `conditions` and `modifiers`. An `alternatives` entry's children become the ordered candidates (recursively, with the alternatives' own `condition` put first in each child's conditions); the first candidate whose conditions all pass is the one taken. The builder refuses a random condition (`random_chance`, `table_bonus`) on an `alternatives` entry's own condition, because flattening would re-roll it per child. A `condition` or `modifier` value may be one object or a list; both are normalized to lists.

Vocabulary. A condition is a `minecraft:<path>` reference to a predicate file (`data/minecraft/predicate/<path>.json`, kept as the bare path string) or an object: `survives_explosion`; `match_block` (`block`, `state`); `any_of` / `all_of` (`terms`); `inverted` (`term`); `table_bonus` (`enchantment` fortune, `chances`); `random_chance` (`chance`); `match_tool` (`tag` or null, `items` resolved through the item tags as `tags.json` does); `entity_properties` (`entity` this, empty predicate); `location_check` (`offset` `[x, y, z]`, `block`, `state`; added because `large_fern` and `tall_grass` need it). A candidate keeps the ore form (`item`, `conditions`, `count`, `bonus`, `explosion_decay`, byte-identical to the ores' first version) when its modifiers are, in order, at most one each of `set_count` (no `add`, no condition), `apply_bonus` and `explosion_decay` (no condition); otherwise it is `item`, `conditions`, `modifiers`, an ordered list of `set_count` (`count`, `add`), `apply_bonus`, `limit_count` (`min`, `max`), `explosion_decay`, `copy_components` (`source`, `include`) and `copy_state` (`block`, `properties`), each with a last key `conditions` when it has a condition. An ore's first candidate is the block itself under `tool/can_silk_touch`, the second its drop. The Fortune bonus and explosion decay are kept for later: they change nothing without the enchantment or an explosion. `predicates` follows the first use of a predicate while walking tables, pools, candidates and conditions (not sorted). The `extensions` key summarizes this. The builder stops, naming the block, on anything outside this vocabulary. Sources: minecraft.wiki "Loot table", "Predicate", "Item modifier" and the ore pages (the wiki notes that the 26.3 Snapshot 4 renamed the `conditions` key to `condition` and a modifier's `function` to `type`); the data is the client jar's `data/minecraft/loot_table/blocks` and `data/minecraft/predicate`.

Minecraft art and data remain Mojang/Microsoft material. Local development builds may use them; anything distributed must replace them.
