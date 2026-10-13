# Items loot step 2a: every imported block's 26.3 loot table (record and handoff)

Lane `feature/voxel-loot-all` (worktree `Unnamed-qol`), from main `42f6606`. Scout AC's plan step 1 (`C:\dev\sandbox\scout-player\plan.md`); the items handoff's "loot step 2" (`2026-10-11-Items-Handoff-P5.md`, section 11, option c). Next: scout AC's plan step 2, the missing drop items (section 8).

## 1. What was built

- **Behaviour** (`Docs/Items.md`, "Loot"): `foundation.Loot.AllBlocks 1` (the default) rolls the 26.3 loot table of every imported block whose table is attached: 602 of the 616 have one, 571 roll it and 31 are skipped (below) (the 14 without one: bedrock, the wall torch, the three command blocks, the structure block, the jigsaw, the two test blocks and the five coral wall fans). Stone with a pickaxe gives cobblestone, by hand nothing; deepslate gives cobbled deepslate; the grass block, podzol and mycelium give dirt; glass, stained glass and ice give nothing; a double slab gives 2 through its table; budding amethyst gives nothing. `0` is items S5b's rule: the 18 ores roll, everything else drops itself. Breaking, wear and the inventory path are otherwise unchanged; a roll that yields nothing still breaks the block.
- **Skipped tables (31)**, which keep "drops itself" until their items exist (step 2b): the 11 leaves (oak, spruce, birch, jungle, acacia, cherry, dark oak, pale oak, the three poplars), gravel, short grass, tall grass, fern, large fern, snow, the snow block, clay, glowstone, melon, sea lantern, cocoa, the sweet berry bush, cave vines and cave vines plant, the amethyst cluster, the bookshelf, the creaking heart, gilded blackstone and nether gold ore. **Short grass and fern keep "drops itself" until step 2** (the plan's payoff line expected nothing). Tall grass and large fern also need `location_check`.
- **Reference** (`Reference/minecraft-items-26.3`, through `tools/build_reference.py` only, after moving the accepted folder to `C:\dev\sandbox\loot2a\ref-old\`): the loot block list is the 18 ores (`LOOT_BLOCKS`, unchanged) then every other imported block with a table, in `block_manifest.json`'s order. 584 new table copies and `predicate/tool/can_shear.json`; `loot_tables.json` 12,682 to 341,892 bytes (602 tables, 2 predicates, 696 candidates), `manifest.json` 17,407 to 65,017, `SHA256SUMS.txt` 305 to 890 lines. Predicates are in first-use order (identical for the old data; `tool/can_shear` last). `ItemFacts.java` re-run: its two files byte-identical. `verify_reference.py` passes.
- **Proof** (`C:\dev\sandbox\loot2a\ref-proof.txt`, `scripts\prove_additive.py`): 300 of the 306 old files (the 305 that SHA256SUMS.txt lists, plus itself) byte-identical, the other 6 being the changed ones; `loot_tables.json` minus `tables[18:]`, `predicates[1:]` and `extensions` equals the old file, parsed and byte for byte; the manifest minus the appended names equals the old one except the three loot counts (tables 18 to 602, predicates 1 to 2, candidates 36 to 696); every old SUMS line unchanged except the 6 changed files'; the 585 added lines are exactly the new files.
- **Import:** two new structs in `FoundationItemAssets.h` (`FFoundationLootConditionAsset`, one node of a table's conditions, since a struct cannot hold itself; `FFoundationLootModifierAsset`), and new fields on the candidate, pool and table structs; the old fields are unchanged. `ItemImportData.py` (`loot_table`) and `ImportItems.py`'s **`loot2` migration** (`PRE_LOOT2_SHA256` = `8d33db55...`, `UNNAMED_ITEMS_MIGRATE=loot2`): DA_Items is verified as before, its 18 tables and predicate against the new reference's first ones, then only the loot tables, predicates and digest are written. `DA_Items.uasset` 298,970 to 1,177,094 bytes; sprites and texture arrays untouched. A fresh process then verified it (`import-verify1.log`).
- **Runtime:** `FoundationLoot` evaluates condition nodes per table (`FFoundationLootCondition`: Predicate, SurvivesExplosion, MatchBlock, AnyOf, AllOf, Inverted, TableBonus, RandomChance, MatchTool, EntityProperties) and ordered modifiers (`FFoundationLootModifier`: SetCount with add, ApplyBonus, LimitCount, ExplosionDecay, Copy) on candidates, pools and tables, with pool conditions. `Roll` takes an `FFoundationLootContext` (tool, block, state, entity). `AddLoot` turns S5b's ore form into the same modifiers (the count range always drawn first, as S5b drew it), so the ores roll exactly as before. `BreakBlock` passes the cell's block and state, and reads `foundation.Loot.AllBlocks` (`FoundationLoot::LegacyTables` = 18).
- **Probe:** `CaptureVoxel.ps1 -MineProbe` (opt-in; `-VoxelMineProbe`, `AVoxelWorld::RunMineProbe`): a probe player mines a stone block beside the spawn with a stone pickaxe through the authority, under the world's game mode, and build.txt gets its hotbar. The default capture path is unchanged.

## 2. Decisions

- **The format** (the manager's go, October 13): extend the flattened candidates, not the jar's tree. The tree would rewrite the 18 ore entries (the aggregate rule) or need a parallel structure. Flattening stays exact because every condition on a nested `alternatives` is deterministic (`survives_explosion`, `inverted(can_silk_touch)`); the builder stops if a random condition ever sits on one. A candidate keeps S5b's ore form when its modifiers fit it, else carries an ordered `modifiers` list; a condition is a predicate name or an object; pools gain `conditions` and `modifiers`, tables `modifiers`; a new last key `extensions` documents it.
- **Q1-Q3 (the manager):** predicates in first-use order; the path-sorted `copied` list and SUMS interleave new paths (not indexed); the three manifest loot counts change value.
- **Q5:** the probe is opt-in behind its own switch. The dev seed's world has the infinite supply (no drops; probe run 1), so the probe uses a saved survival world in a sandbox folder (`C:\dev\sandbox\loot2a\worlds\loot2a-survival`, passed with `-WorldsDir`).
- **`location_check`** (tall grass, large fern), found by the rebuild: kept as data (`Offset`, the block and state); the runtime does not evaluate it, so `AddLoot` skips those tables, which are skipped for wheat seeds anyway.
- **`copy_components`** appears on 8 tables (the bee nests' bees, and `custom_name` on the barrel, the furnaces, the dispenser and the dropper), not 2: all no-ops.

## 3. Tests

`Source/Unnamed/Tests/FoundationLootTests.cpp` (written by a Sonnet 5.5 subagent; the lead restored one strict check); `Unnamed.Foundation.Loot` 6 tests:

- **`.Tables`:** all 602 tables of `loot_tables.json` against DA_Items field by field (pools, candidates, condition nodes rebuilt in `loot_table`'s order, modifiers, table modifiers; 12,223 field comparisons) and the 2 predicates; the runtime holds 571 tables, each `Index` its JSON position; the 31 skipped tables derived at runtime and pinned both ways, each with its cause; the first 18 keep S5b's ore checks (17 attached).
- **`.Drops`** (the 18 ores x 36 tools: 648 entries, 82 eligible, the same tallies) and **`.InventoryFull`**: unchanged pins, restricted to the legacy tables.
- **`.Counts`:** the ore checks (legacy tables); chance checks over 3 cells x 20,000 break counts within 5 sigma (by hand: azalea leaves' azalea 0.04948 against 0.05 and sticks 0.00993 and 0.01030 against 0.01 each; mangrove leaves' sticks 0.00940 and 0.00985; the brown mushroom block's 1 and 2 at 0.11103 and 0.11182 against 1/9, nothing 0.77715 against 7/9; the dead bush's 0, 1 and 2 sticks at a third each); synthetic tables for every condition kind (17 truth-table rows), `set_count` add, `limit_count` bounds, the modifier order across entry, pool and table, rolls and determinism (`random_chance` 0.125: 0.12707; `table_bonus` level 0 at 0.25: 0.25245).
- **`.Blocks`** (new; 35 cases through the authority, survival, finite inventory): stone with each of the 7 pickaxes gives 1 cobblestone, by hand it breaks and gives nothing; deepslate gives cobbled deepslate; the grass block, podzol and mycelium give dirt by hand and with a shovel; glass, white stained glass and ice give nothing by hand and with a pickaxe; stone and oak slabs give 1 bottom or top and 2 double; budding amethyst gives nothing; bedrock never breaks; gravel, oak leaves, clay and the snow block drop themselves.
- **`.Switch`** (new): with `foundation.Loot.AllBlocks 0` (restored on exit), 602 blocks x the hand and 35 tools (25,308 entries, plus the double slabs): 75 ore rolls and 13,615 "drops itself" entries equal items S5b's rule; 828 entries (23 blocks the lab's aim cannot stand or hit: kelp, seagrass, corals, ...) are counted and capped at 5%. Back at 1, stone gives cobblestone.
- **`Mining.Creative`:** its survival control now expects cobblestone from stone (the only other test the change moved).
- `tool/can_shear` never passes (no shears item); the test pins that.

## 4. Known issues

| # | Issue | When |
|---|---|---|
| L2a-a | 31 tables skipped for undefined items (section 1); their blocks drop themselves, including short grass and fern. | Step 2 (the missing drop items) |
| L2a-b | `location_check` is not evaluated (tall grass, large fern). | Step 2, with wheat seeds |
| L2a-c | Silk Touch, Fortune, shears and explosions are data only; `copy_*` are no-ops. | Enchantments, shears, explosions, block entities |
| L2a-d | The reference README's `data/minecraft/...` row still says "the jar's 18 ore loot tables and the one predicate" (review B). A README edit changes SHA256SUMS.txt and so DA_Items' digest, so it waits for step 2's rebuild. | Step 2 |
| S5b-c | `LootSeed` is 0 in every world. | World saves (voxel phase 10) |
| S5b-d | Drop counts are checked against the data and the wiki, not black-box. | Optional probe P8 |
| — | Tripwires for the next rebuild: 602 tables (571 attached, 31 skipped), 2 predicates, 696 candidates, 18 legacy tables first. | Standing |

## 5. Next: scout AC's plan step 2, the missing drop items (loot step 2b)

- Append about 25 items to the items reference (flint, wheat_seeds, the 10 saplings, gold_nugget, snowball, glow_berries, clay_ball, glowstone_dust, melon_slice, cocoa_beans, sweet_berries, prismarine_crystals, amethyst_shard, resin_clump, book): the items handoff's open question 2 (an "append last" ordering in `build_reference.py`, items after the existing ones and their textures after the existing layers) needs the manager's call first.
- Evaluate `location_check` (the context gains a block lookup at an offset) for tall grass and large fern.
- The tables switch on by themselves when their items exist; re-pin `.Blocks`' skipped list and `.Tables`' attached count.
- "Apply P5" (the P5 handoff, section 7) is still pending; it edits `FoundationAuthority.cpp` and the mining tests too, so run it before or after step 2, never beside it.

## 6. Code map

| What | Where |
|---|---|
| Evaluator | `Source/Unnamed/Foundation/FoundationLoot.h/.cpp`: `FFoundationLootCondition`, `FFoundationLootModifier`, `FFoundationLootContext`, `FoundationLoot::Test`, `Roll`, `LegacyTables` |
| Assets and import | `FoundationItemAssets.h/.cpp` (`FFoundationLootConditionAsset`, `FFoundationLootModifierAsset`, `AddLoot`); `Scripts/Editor/ItemImportData.py` (`loot_table`); `Scripts/Editor/ImportItems.py` (`PRE_LOOT2_SHA256`, `loot2`) |
| Break | `FoundationAuthority.cpp`: `CVarLootAllBlocks`, `BreakBlock` |
| Reference | `Reference/minecraft-items-26.3/tools/build_reference.py` (`loot_blocks`, `derive_loot`, the condition and modifier forms), `verify_reference.py`, `README.md` ("Loot tables") |
| Probe | `Source/Unnamed/Voxel/VoxelWorld.h/.cpp` (`RunMineProbe`), `Scripts/CaptureVoxel.ps1 -MineProbe` |
| Tests | `Source/Unnamed/Tests/FoundationLootTests.cpp` |
| Scratch | `C:\dev\sandbox\loot2a\`: `survey.txt`, `scripts\` (`survey.py`, `show.py`, `prove_additive.py`), `ref-old\`, `ref-proof.txt`, `runtests.ps1`, `logs\`, `worlds\`, the import and build logs |

```powershell
& C:\dev\sandbox\loot2a\runtests.ps1 -Tests 'Unnamed.Foundation.Loot' -Name 'loot-x'      # one native group (headless; fresh name)
& Scripts\CaptureVoxel.ps1 -MineProbe -World loot2a-survival -WorldsDir C:\dev\sandbox\loot2a\worlds -OutputDirectory <fresh>
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-items-26.3/tools/verify_reference.py
```

## 7. Evidence and token use

`Tests/Evidence/` is git-ignored; the runs stay in the worktree.

- **Final checks after the last code change:** `Tests\Evidence\2026-10-13\Checks-final-20261013-165212\summary.txt` (GREEN in 20 min: build, native 303 of 303 with no warnings, TestVoxel 197, hashes 9 of 9, runtime all 128/0, interaction 83/0, tour 27/0, create-world 38/0, the smoke flight PASS, npm, source-check, the verifiers).
- **The first final run** (RED): `Tests\Evidence\2026-10-13\Checks-final-20261013-164140\summary.txt`. Native was 303 of 303, but one Playground runtime check failed: "break returns the block once and the last durability point breaks the pickaxe" expected stone back from stone. It now expects the block's loot (`FoundationInteractionProbe.cpp`: the stone stack stays at 63 and one cobblestone arrives).
- **Mine probe:** `Tests\Evidence\2026-10-13\Loot2a-MineProbe-2\build.txt` (the saved survival world: "stone ... broken after 12 steps; hotbar: [1] stone_pickaxe x1, [2] cobblestone x1"); `Loot2a-MineProbe-1` (the dev seed's infinite supply: broken, no drop).
- **Tests:** `C:\dev\sandbox\loot2a\logs\loot-3`, `foundation-1` (the subagent's runs), `lead-loot-mining-1` (Loot and Mining, 10 of 10, after the lead's fixes).
- **Reference and import:** `C:\dev\sandbox\loot2a\ref-proof.txt`; `import-migrate.log` (the `loot2` migration), `import-verify1.log` (the verify after it).
- **Review:** `C:\dev\sandbox\loot2a\review-A.diff` (code) and `review-B.diff` (tests and docs), two Sonnet 5.5 reviewers. Review A found no correctness defects. The migration check was tightened (18 tables and 1 predicate required). Not taken: a cycle guard for hand-edited condition terms (the builder writes only forward indices), the builder's `isinstance` order for a malformed `location_check` (it still stops), and dropping the extra `RandRange(1, 1)` draw on ore-form candidates (it keeps the ores' draws). Review B: `.Switch` now pins its exact counts, the mangrove check fails when the table is missing, `Mining.Creative` checks the switch is 1, and the doc counts and wording were fixed. The README row is L2a-d.

| Agent | Peak context | Notes |
|---|---|---|
| Lead (Opus 5.5) | 333.4k | Past the 300k mark during the second final run; the manager got a one-line status. |
| Reference rebuild (Sonnet 5.5) | 97.5k | Builder, verifier, README, proof, the dumper re-run; found `location_check`. |
| Tests (Sonnet 5.5) | 160.2k | Over its ~100k brief: four suites, five builds, two full runs. |
| Review A, B (Sonnet 5.5) | 80.2k, 97.5k | Diff only (1,351 and 1,539 lines). |
