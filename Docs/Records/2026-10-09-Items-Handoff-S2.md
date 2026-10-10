# Items handoff: into S2 (item registry and rules)

Read this first if you are picking up the items phase in a new chat. Items S1 is done and committed (`318c48f` on `feature/items-1`, a parallel lane next to the voxel chain). It produced the hash-checked [item reference](../../Reference/minecraft-items-26.3/README.md), [the items phase plan](2026-10-09-Items-Phase-Plan.md) and the two approved `CLAUDE.md` edits. **S2 is next:** `DA_Items`, its importer, `ItemRoot`, the `BuildRules` merge, the loadout switch and retiring the practice tools. This was true at `318c48f`; check `git status`, `git log -5` and newer records before trusting it.

## 1. Read these, and only these

Budget: about 80k of reading.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md).
2. **This handoff.**
3. **[The items phase plan](2026-10-09-Items-Phase-Plan.md):** sections 1 (decisions), 3 (sessions, the voxel dependencies, shared-resource rules) and 4 ("S2").
4. **[Scout F's research](2026-10-09-Items-Scout-Research.md):** section 4, "Fit with this project": the component-to-field table, the fields to add later, the two sword rules a full-block world cannot express, the creative inventory and saves.
5. **[The item reference's README](../../Reference/minecraft-items-26.3/README.md):** the file formats you import.
6. **[Docs/Blocks.md](../Blocks.md):** "Data flow" and "Ids", the pattern `DA_Items` copies.
7. **Code, headers first:**
   - `Source/Unnamed/Foundation/FoundationItems.h` (`FFoundationItemDefinition`, the mining rules);
   - `FoundationInteractionSettings.h/.cpp` (`BuildRules`, `AddBlockItems`, the practice items at lines 59 to 77, `Loadout`);
   - `Source/Unnamed/UnnamedProjectSettings.h` (`BlockRoot`, the pattern for `ItemRoot`);
   - `Scripts/Editor/ImportBlocks.py`, `BlockImportData.py` (`--write-tags`) and `ProjectPaths.py` (`block_asset`).
   Open `FoundationInteractionProbe.cpp`, `HudCreativeScreen.cpp` and the tests only where the practice ids are (below).

## 2. Working agreement

- **The manager:** Nyx at night, Hemera by day. Send a short plan first and report before and after committing; the manager confirms plans within Jesus's decisions and approves the end-of-session local commit after a clean Sonnet 5.5 pre-commit review (diff only, excluding copied reference data) and passing checks. Never push; the manager merges.
- **The lane:** S2 runs where the manager says, likely a fresh worktree branched from `feature/voxel-world` after the S1 merge. Never touch another lane's folder.
- **Builds:** S2 is the first items session that builds and runs the editor, and the main lane builds and benchmarks too.
  - One editor build at a time (`-WaitMutex`).
  - No native suites while the main lane's suites or benches run.
  - Ask the manager before a build or editor run if the main lane is benchmarking.
  - A fresh worktree has no `Binaries/` (S1's had none), so its first build is a full one.
- **Context:** checkpoints at about 300k and every 200k after, measured with `Scripts/MeasureAgentUsage.mjs`. A session started in `C:\dev` copies its transcript into a temp `projects/C--dev-projects-Unnamed-items/` folder and runs with `CLAUDE_CONFIG_DIR`; S1's helper is `C:\dev\sandbox\items-s1\measure.sh`, so change its session id.
- **Subagents:** give each exact paths and a stop near 150k.
- **Game code:** data, reports, published docs and black-box runs only. `javap -public` for signatures is fine. No decompiled method bodies, no reflection into private internals.

## 3. Where things stand

| Session | Area | Status |
|---|---|---|
| S1 | Item reference, phase plan, `CLAUDE.md` id rule | **Done** (`318c48f`) |
| S2 | Item registry and rules | **Next** |
| S3 to S9+ | Icons, held items, mining, loot, durability, combat, item entities, behaviours | Planned ([plan](2026-10-09-Items-Phase-Plan.md), section 3) |

**S1 built** `Reference/minecraft-items-26.3/` (281 hashed files, 1.9 MB):
- `items.json`: the 63 Tier A items in registry order:
  - **Identity:** `id`, `protocol_id`, `group`, `material`, `kind`, `name`.
  - **Numbers:** `max_stack`, `max_damage`, `enchantability`, `repair` (selector and resolved ids), `attack` (totals and raw modifiers).
  - **Behaviour data:** `tool` (rules plus the documented defaults), `weapon` (with defaults), `food`, `consumable`, `components`.
  - **Presentation:** `item_tags`, `creative`, `model`, `textures`.
- `models.json` (70 models with display transforms), `textures.json` (70 sprites with opacity masks), `tags.json` (43 item tags, 16 block tags), `item_index.json` (all 1,658 items: `places_block`, `block_with_same_id`, `definition`), `manifest.json`.
- `facts/break_times.json`: the hand and the 35 tools on all 1,286 blocks, with ticks and drops, in exact arithmetic.
- The verifier re-derives every generated file from the folder's copies and the jar. Four mutations failed it, as did a stray tool script.

**`CLAUDE.md` now says:** "Block and item IDs and the world ID `playground` are saved IDs: item IDs are the 26.3 item names; a block's item has the block's id." The item reference is in the hash-checked list.

## 4. S2: what to do

Send this scope in your plan; the manager confirms it.

1. **`ItemRoot` and the asset.**
   - Add `UUnnamedProjectSettings::ItemRoot` (`/Game/Items`) next to `BlockRoot`, plus `ProjectPaths.item_asset()`. Add `/Game/Items` to the always-cooked directories as `/Game/Blocks` is.
   - New `UFoundationItemAssets` (`DA_Items`): one entry per reference item with the fields below, the reference's `items.json` SHA-256 (or the whole `SHA256SUMS.txt` digest), and each item's opacity masks and texture names for S3 and S4.
   - Fields: id, name, max stack, max damage, attack damage and speed totals, default mining speed, rules, damage per block and per hit, can destroy in creative, enchantability, the repair selector, item tags, creative positions and model data.
   - Store numbers, not presentation: the held view keeps today's `Icon` key until S4.
2. **`Scripts/Editor/ImportItems.py`.**
   - It runs the reference's verifier (or checks its hashes), then writes `DA_Items`.
   - Like `ImportBlocks.py`, it verifies an existing asset and refuses to replace it when the reference changed.
   - **Tags:** a pure data module (`ItemImportData.py`, like `BlockImportData.py`) with `--write-tags` writes `Config/Tags/MinecraftItemTags.ini` (`Item.*` from `tags.json`'s item tags, the block tags' naming). Restart the editor after writing tags.
3. **Rules.**
   - `BuildRules` merges block items (`AddBlockItems`, unchanged), then `DA_Items`, then project items (`UFoundationInteractionSettings::Items`). A duplicate id is an error, never a silent override.
   - **Tool rules** map to the existing `MiningRules` (scout F's section 4 table: `#mineable/pickaxe` → `Block.Mineable.Pickaxe`, `#incorrect_for_iron_tool` → `Block.IncorrectForTool.Iron`, and so on; check the exact tag names in `Config/Tags/MinecraftBlockTags.ini`).
   - **Single-block selectors** (the swords' `minecraft:cobweb`) and the empty `#sword_instantly_mines` stay in the data and match nothing until those blocks exist. Choose a representation that keeps them, and say which in your plan.
   - **Attack totals:** the reference's `attack.damage` and `attack.speed` (for example, the iron pickaxe is 4 and 1.2), which are the `AttackDamage` and `AttacksPerSecond` totals.
4. **Loadout and retiring the practice tools (Q3).**
   - The loadout becomes `stone, iron_pickaxe, iron_sword`.
   - Remove `practice_pickaxe` and `practice_sword` from the defaults (`FoundationInteractionSettings.cpp` lines 59 to 77). Move their uses to vanilla ids:
     - `FoundationInteractionProbe.cpp`: 3 uses, including the hotbar check at line 148 and the tool at line 1009;
     - `FoundationInteractionTests.cpp`: 4;
     - `HudCreativeScreen.cpp`: 4, the sprite mapping at lines 29 to 30 and 178 to 179. Map the vanilla ids to their own `item/<id>` sprites instead.
   - `VoxelEditTests.cpp` calls `BuildRules` and rewrites `Loadout[0]`. It is a voxel test file: run it, and do not edit it unless a merge-order change forces it (ask the manager first).
   - `Source/Unnamed/Voxel/VoxelWorld.cpp` calls `BuildRules`. It needs no edit.
5. **`CLAUDE.md` (S2's edits; each needs the manager's approval within Jesus's decisions):**
   - add `practice_pickaxe` and `practice_sword` to the retired ids;
   - put `/Game/Items` (`ItemRoot`, `DA_Items`) beside the `DA_Blocks` sentence;
   - add a Commands line for `uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-items-26.3/tools/verify_reference.py` and the `ImportItems.py` run.
   Also update `Docs/Blocks.md` "Ids" ("item id = block id" becomes the new rule) and add `Docs/Items.md` (data flow, ids, the merge).
6. **Tests:**
   - `Unnamed.Foundation.Items.Registry`: all 63 items' numbers against `items.json` (read the file in the test, as block tests read their reference). No id clash between block items and `DA_Items`. `item_index.json`'s clashes are exactly `air` and `wheat`.
   - `Unnamed.Foundation.Items.Rules`: selectors resolve to `Block.*` tags; first-rule semantics (speed from the first matching rule that states one; drops from the first that states `correct_for_drops`); a wrong-tier pickaxe keeps its speed but loses the drop.
   - Interaction probe and scenarios with the new loadout.
7. **Gate:**
   - every native suite (`Automation RunTests Unnamed`), the interaction probe with 0 failures, and `TestVoxel.ps1`, with the radius-8 hashes and goldens unchanged (the authority is shared);
   - the three reference verifiers, `npm test` and `source-check.mjs`;
   - `ImportItems.py` run twice: the second run verifies and writes nothing.

**Not in S2:** icons and the creative tabs (S3), held meshes (S4), break timing by tier against the truth table (S5), loot (S5b), durability (S6) and combat (S7). S2 makes the data available and the ids right.

## 5. Notes and known issues

- **Break-time truth table:** exact arithmetic. Where 1 / d is a whole number (35,393 of 46,296 entries), the game's float steps could land one tick either side. Scout F's table is one tick long in such cases (wooden pickaxe on deepslate: 46 against an exact 45). S5 runs probe P5 before pinning ticks. S2 does not test break times.
- **Hoe wear:** the 26.3 data gives hoes `item_damage_per_attack` 2; the wiki says 1. Follow the data (it is in `items.json`). Probe P5 can confirm.
- **Spears:** they are in the reference (data and both sprites) but are Tier C for behaviour. Import them as items with their numbers (no `tool` component, so they mine like the hand); their attacks wait.
- **Items that place another block** (`item_index.json`'s `places_block`): 14 vanilla items place a block with another id (`wheat_seeds`, `redstone`, `carrot`, `powder_snow_bucket` and others). None is in Tier A. `Docs/Blocks.md` already lists the model blocks whose vanilla item has another id.
- **Ids are saved:** inventories are not saved yet (voxel phase 10), so switching the loadout loses nothing. Once retired, `practice_*` ids are never reused.

## 6. Code map: S1

| What | Where |
|---|---|
| The reference, builder and verifier | `Reference/minecraft-items-26.3/`, `tools/build_reference.py` (`derive()`), `tools/verify_reference.py` |
| Inputs | the client jar `%APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar` (SHA-1 `e877b6a0…`); `C:\dev\sandbox\mc-26.3-blocks\reports\reports` (`minecraft/components/item`, `registries.json`; server jar `33680f5f…`); the block reference's `facts/block_facts.csv`, `facts/creative_tabs.json` and `block_manifest.json` |
| The plan | `Docs/Records/2026-10-09-Items-Phase-Plan.md` |
| Scout F's scripts and raw output | `C:\dev\sandbox\scout-items-tools\` |
| S1 scratch (measure helper) | `C:\dev\sandbox\items-s1\` |

## 7. What worked and what failed in S1

- **Worked:**
  - One pure `derive()` shared by builder and verifier. The verifier re-derives all 8 generated files byte for byte, so a hand edit, even with the sums rewritten, fails.
  - Reading tags from the client jar's data pack: no server jar extraction needed.
  - A Sonnet 5.5 diff reviewer at 87.5k: its findings were small and useful.
- **Changed course:**
  - A per-entry "boundary" flag for whole-number break times flagged 76% of entries, so it was replaced by one count and a README note.
  - Unbreakable blocks first showed `drops` 1; they now show 0.
- **Python:** it is not on `PATH`. Use `uv run --no-project --python 3.13 [--with pillow] python -I`.

## 8. Commands

```powershell
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-items-26.3/tools/verify_reference.py   # item reference
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-blocks-26.3/tools/verify_reference.py  # block reference
uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/verify_reference.py              # worldgen reference
Set-Location Tests; npm ci; npm test; node source-check.mjs
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' UnnamedEditor Win64 Development -Project="$PWD\Unnamed.uproject" -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -ExecCmds="Automation RunTests Unnamed" -TestExit="Automation Test Queue Empty" -unattended -NullRHI
& Scripts\TestFoundationRuntime.ps1 -Editor -FrameRates 60
& Scripts\TestVoxel.ps1
```

## 9. Token use in S1

| Agent | Peak context | Notes |
|---|---|---|
| Lead (Opus 5.5) | about 230k at the end (217.8k at the S1 commit) | About 147k before any building: the reading list plus a large system prompt. The plan record, tools and README added about 70k. |
| Pre-commit reviewer (Sonnet 5.5) | 87.5k | Diff only, excluding copied data |

## 10. Short prompt for S2

> Items phase, session 2: the item registry and rules (`DA_Items`, `ImportItems.py`, `ItemRoot`, the `BuildRules` merge, the loadout switch to `stone, iron_pickaxe, iron_sword` and retiring the practice tools). Read `Docs/Records/2026-10-09-Items-Handoff-S2.md` first and follow it; it holds the reading list, the decisions, the scope, the tests and the gate. Work only in the folder and branch the manager names; builds and native suites take turns with the main lane. Send the manager a short plan before building.
