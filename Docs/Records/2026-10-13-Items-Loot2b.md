# Items loot step 2b: the missing drop items (record and handoff)

Lane `feature/voxel-loot-2b` (worktree `Unnamed-qol`), from `737a456` (loot 2a merged with the atmosphere work). Scout AC's plan step 2 (`C:\dev\sandbox\scout-player\plan.md`); [loot step 2a](2026-10-13-Items-Loot2a.md), sections 4 and 5. Next: scout AC's plan step 3, item entities (section 5).

## 1. What was built

- **Behaviour** (`Docs/Items.md`, "Loot"): every one of the 602 attached tables rolls; none is skipped. The leaves drop saplings, sticks and (oak, dark oak) apples, gravel drops flint 1 time in 10 (else gravel), short grass, fern, tall grass and large fern drop wheat seeds 1 time in 8, snow and the snow block drop snowballs, clay 4 clay balls, glowstone dust, melon slices, prismarine crystals, cocoa beans, sweet and glow berries, amethyst shards, books, resin clumps, and nether gold ore and gilded blackstone gold nuggets. `foundation.Loot.AllBlocks 0` still restores the S5b rule; nether gold ore (legacy table 17) now rolls under it too.
- **23 items, not about 25:** the 31 skipped tables name exactly 23 undefined items (`C:\dev\sandbox\loot2b\survey.txt`): 9 saplings (oak, spruce, birch, jungle, acacia, cherry, dark oak, pale oak, poplar; the three poplar leaves share the poplar sapling), resin clump, amethyst shard, wheat seeds, flint, snowball, clay ball, book, glowstone dust, cocoa beans, melon slice, gold nugget, prismarine crystals, sweet berries and glow berries. Azalea and mangrove leaves were already attached. The ones that place a block in vanilla (the saplings, wheat seeds, cocoa beans, sweet berries, glow berries) are items only (placing them is a later block step).
- **Reference** (`Reference/minecraft-items-26.3`, through `tools/build_reference.py` only, after moving the accepted folder to `C:\dev\sandbox\loot2b\ref-old\`): a new last `TIER_A` group `drop` and `APPEND_LAST`. Items are sorted by (appended group, protocol id), and models and textures by (first used by an appended group, name), so items 0-62 and models and textures 0-69 keep their positions. 92 new files (per item its definition, model, components report and sprite; the saplings' sprites are the jar's `textures/block/<id>_sapling.png`). `items.json` 63 to 86 items, `models.json` and `textures.json` 70 to 93, `tags.json` 43 to 53 item tags (`bookshelf_books`, `chicken_food`, `duplicates_allays`, `fox_food`, `happy_ghast_food`, `happy_ghast_tempt_items`, `metal_nuggets`, `parrot_food`, `saplings`, `villager_plantable_seeds`), `facts/tooltips.json` 63 to 86 entries (`ItemFacts.java` re-run; `durability_bar.json` byte-identical), `SHA256SUMS.txt` 890 to 982 lines. `loot_tables.json`, `item_index.json`, `lang.json` and the break times are byte-identical. README: the group, the counts, and the stale loot row (L2a-d, now "the jar's 602 loot tables ... and the two predicates"). `verify_reference.py` passes: 86 items with 23 `drop` last, models and textures 70 onward only theirs, the saplings' `block/` sprites, every loot item an imported block or an item.
- **Proof** (`C:\dev\sandbox\loot2b\ref-proof.txt`, `scripts\prove_additive.py`): 881 of the 891 old files byte-identical, the other 10 the changed ones. `items.json`, `models.json` and `textures.json` minus their appended 23 equal the old files, parsed and byte for byte, and the old entries keep their positions. `tags.json` minus the 10 new tags and `tooltips.json` minus its 23 appended lines equal the old files byte for byte. The manifest minus `tier_a.drop`, the 92 new `copied` paths and four counts (items 63 to 86, models and textures 70 to 93, item tags 43 to 53) equals the old one. Every old SUMS line is unchanged except those of the 9 changed files, and the 92 added lines are exactly the new files.
- **Import:** `ItemImportData.py` also lists the block textures an item's model uses as GUI sprites (93), and accepts `block/<id>` as a sapling's GUI sprite (the GUI model stays `item/<id>`). `ImportItems.py` imports sprites as `Gui/<folder>/<name>`. `Config/Tags/MinecraftItemTags.ini` regenerated (+10 tags). Full reimport: `DA_Items`, `T_ItemTextures` and `T_ItemTextures32` moved to `C:\dev\sandbox\loot2b\items-old\`, then `ImportItems.py` (86 items, 93 models, 93 textures, 86 + 7 layers, 23 sprites created), then a fresh verifying run (`import-verify1.log`). The review's README fix (the items that place another block: `redstone` and four `drop` items) changed `SHA256SUMS.txt` and so the digest, so the import ran once more the same way (the first result kept in `C:\dev\sandbox\loot2b\items-2b-first\`; `import2.log`, `import-verify2.log`). **DA_Items proof** (`C:\dev\sandbox\loot2b\da-proof.txt`, `scripts\dump_items.py` and `compare_dumps.py`, `da-before.json` against the final `da-after2.json`): items 0-62, models 0-69 and textures 0-69 are struct-identical (`export_text`), the layer pixels of 16 x 16 layers 0-62 and 32 x 32 layers 0-6 are identical, the new sprites are layers 63-85, and the lang keys, loot tables and predicates are identical.
- **`location_check`:** `EFoundationLootCondition::LocationCheck` with a cell `Offset` (the jar's x, y, z is the cell's X, Z, Y: west -X, north -Y, up +Z); `FFoundationLootContext::GetBlockAt` returns the block and state at the broken cell plus an offset, and `BreakBlock` sets it. The roll happens before the block is removed, so breaking either half of tall grass or large fern sees the other half and rolls once (1 in 8 wheat seeds); the other half then pops without drops, as before. `AddLoot` no longer skips the kind.
- **Sprites in the HUD:** the hotbar already drew `Gui/<Textures[0]>`. The creative screen now uses the item's first texture too (`FHudCreativeItem::Sprite`, filled in `FoundationPanels.cpp`), and looks `block/` sprites up under `ItemRoot/Gui` as well, so the saplings show in Natural Blocks.

## 2. Decisions

- **The manager's go (October 13):** Q1, exactly the 23 needed items; Q2, import the saplings' block-folder sprites and change the creative screen; "Apply P5" was not running.
- **One group, `drop`,** appended last by `APPEND_LAST` rather than a second tier list (no parallel structure; `tier_a`'s new key is last in the manifest).
- **The `.Blocks` double-plant case** writes both halves directly (the default state is the lower half, `FoundationPlantRules::GetOtherHalf` gives the upper) instead of placing through `UseOn`: the lab's `UseOn` was refused with a target mismatch, and placement is the plant tests' subject. The break goes through the authority.

## 3. Tests

Native: `native-1` 311 of 313 (the last two rows below failed, then were fixed: `fix-1`, their groups 4 of 4); the full set again in RunChecks final smoke (section 7). Changed pins and their sources:

| Pin | Old | New | Source |
|---|---|---|---|
| `Loot.Tables` skipped / attached / runtime tables | 31 / 571 / 571 | 0 / 602 / 602 | every loot item now defined (the verifier's coverage check) |
| `Loot.Tables` attached ore tables; `gold_nugget` an item | 17; no | 18; yes | nether gold ore's table (`gold_nugget` 2-6, ore_drops x0, decay) |
| `Loot.Drops` tally | nether_gold_ore 7 | gold_nugget 7 | the 7 pickaxes are its eligible entries (`break_times.json`); its table drops gold nuggets |
| `Loot.Counts` ore tables | 17 | 18 | as above; its 2-6 range checked like the others |
| `Loot.Switch` ore rolls / drops itself | 75 / 13,615 | 82 / 13,608 | nether gold ore's 7 pickaxe entries move from "drops itself" to rolls (82 = `.Drops`' eligible) |
| `Loot.Blocks` skipped pins | 31 | 0 | as above |
| `Items.Registry` items; models, textures; 16 x 16 layers | 63; 70; 63 | 86; 93; 86 | `items.json`, `models.json`, `textures.json` (32 x 32 stays 7) |
| `Items.Mesh` sprites; first-person models | 70; 63 | 93; 86 | `textures.json`; `items.json` |
| `Combat.Damage` rows; full-charge ticks | 63; 64 | 86; 87 | `items.json` (+ the hand); 1,103 attacks = 942 + 23 x 7 (speed 4: ticks 0-6) |
| `Combat.ShieldDisable` rows | 63 | 86 | `items.json` |
| `Mining.Creative` flag copied | 63 | 86 | `items.json` |
| `Hud.Items.Tooltips` | 63 | 86 | `facts/tooltips.json` |
| HUD probe item sprites | 63 | 86 | one sprite per item (`Textures[0]`) |
| `Hud.Creative.Tabs` combat | 22, TNT last | 23, TNT then snowball | `creative_tabs.json`: snowball at combat index 68, after TNT (66) |
| `Interaction.Plants` tall grass upper half | the item back (64) | no grass back (63), at most one wheat seed | tall grass's table by hand |

New checks: `.Tables`, tall grass's and large fern's two `LocationCheck` nodes (offsets (0, 0, 1) and (0, 0, -1), state half upper and lower); `.Counts`, gravel by hand (flint 0.10012, gravel 0.89988 against 0.1 and 0.9), oak leaves by hand (sapling 0.04948, apple 0.00502, sticks x1 0.00993 and x2 0.01030 against 0.05, 0.005, 0.01, 0.01), short grass (wheat seeds 0.12448 against 0.125), over 3 cells x 20,000 break counts within 5 sigma, and 8 synthetic `location_check` rows (no lookup, unknown cell, wrong block, wrong state, no state, a match with the offset handed over, extra state values, an empty wanted state); `.Blocks` (167 cases), clay by hand and with a shovel 4 clay balls, the snow block with a shovel 4 snowballs (by hand nothing), gravel 40 rounds by hand and with a shovel (one stack, gravel or flint), oak leaves and short grass 60 rounds each, and tall grass and large fern 64 breaks each alternating halves (each: the other half gone, only wheat seeds; 8 of 64 for both). The test file's helper `LootTests_SameCondition` gained the `location_check` mapping and offset check. Written by a Sonnet 5.5 subagent; the lead replaced its double-plant placement (section 2).

## 4. Known issues

| # | Issue | When |
|---|---|---|
| L2b-a | Saplings, wheat seeds, cocoa beans, sweet berries and glow berries are items only; they do not place their blocks. | A later block step |
| L2a-c | Silk Touch, Fortune, shears and explosions are data only; `copy_*` are no-ops. | Enchantments, shears, explosions, block entities |
| S5b-c | `LootSeed` is 0 in every world. | World saves (voxel phase 10) |
| S5b-d | Drop counts are checked against the data and the wiki, not black-box. | Optional probe P8 |
| — | Tripwires for the next rebuild: 86 items (23 `drop` last), 93 models and textures (86 + 7 layers), 53 item tags, 602 tables (all attached), 2 predicates, 696 candidates. A new group goes in `APPEND_LAST`. | Standing |

L2a-a (31 skipped tables), L2a-b (`location_check`) and L2a-d (the README row) are closed.

## 5. Next: scout AC's plan step 3, item entities (items S8a)

- Drops land in the world as spinning item entities and are picked up (`C:\dev\sandbox\scout-player\plan.md`, step 3; `foundation.Items.Entities`). Run probe P3 first or mark the physics provisional.
- "Apply P5" (the P5 handoff, section 7) is still pending; it edits `FoundationAuthority.cpp` and the mining tests, so run it before or after step 3, never beside it.

## 6. Code map

| What | Where |
|---|---|
| Reference | `Reference/minecraft-items-26.3/tools/build_reference.py` (`TIER_A["drop"]`, `APPEND_LAST`, the model and texture order in `derive`), `verify_reference.py` (section 5 and the loot coverage check), `README.md` |
| Import | `Scripts/Editor/ItemImportData.py` (`sprite_pngs`, the GUI sprite assertion), `Scripts/Editor/ImportItems.py` (`Gui/<folder>/<name>`) |
| `location_check` | `Source/Unnamed/Foundation/FoundationLoot.h/.cpp` (`LocationCheck`, `Offset`, `GetBlockAt`), `FoundationItemAssets.cpp` (`AddLoot`), `FoundationAuthority.cpp` (`BreakBlock`) |
| Creative sprites | `Source/Unnamed/Hud/HudCreative.h` (`Sprite`), `HudCreativeScreen.cpp` (`ItemSprite`), `Foundation/FoundationPanels.cpp` |
| Tests | `Source/Unnamed/Tests/FoundationLootTests.cpp` and the pins in section 3 |
| Scratch | `C:\dev\sandbox\loot2b\`: `survey.txt`, `scripts\` (`survey.py`, `prove_additive.py`, `dump_items.py`, `compare_dumps.py`), `ref-old\`, `items-old\`, `ref-proof.txt`, `da-proof.txt`, `da-before.json`, `da-after.json`, `facts.ps1`, `runtests.ps1`, `runpy.ps1`, `logs\` |

```powershell
& C:\dev\sandbox\loot2b\runtests.ps1 -Tests 'Unnamed.Foundation.Loot' -Name 'loot-x'      # one native group (headless; fresh name)
uv run --no-project --python 3.13 --with pillow python -I C:\dev\sandbox\loot2b\scripts\prove_additive.py C:\dev\sandbox\loot2b\ref-old Reference\minecraft-items-26.3
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-items-26.3/tools/verify_reference.py
```

## 7. Evidence and token use

- Logs: `C:\dev\sandbox\loot2b\logs\` (`build1-4.log`, `import1.log`, `import-verify1.log`, `dump-before.log`, `dump-after.log`, test runs `loot-1` to `loot-3`, `native-1`, `fix-1`).
- RunChecks final smoke, GREEN in 21 min (every step PASS: build, native 313 of 313 with one engine warning (an HTTP connectivity check timing out), voxel 207 suites, hashes 9 of 9, the three Playground runtimes, create-world, flight, npm, sourcecheck, the five reference verifiers): `Tests/Evidence/2026-10-13/Checks-final-20261013-181103/summary.txt`. The tour's `runtime-60-tour-22b-creative-natural-plants-a.png` shows the saplings in Natural Blocks.
- Token use (`MeasureAgentUsage`, at the report): lead peak 323.8k context (past the ~300k wrap-up line, reached during the second import and the final run), 30.4M processed; the test subagent (Sonnet 5.5) peak 158.1k, the review subagent (Sonnet 5.5) peak 109.7k, both over the 100k guideline.
