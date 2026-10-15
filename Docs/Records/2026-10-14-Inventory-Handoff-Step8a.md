# Handoff: survival step 8a, the furnace (track T1 Containers)

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. Step 7 (the crafting table) is committed (d4df4b67) and merged with `feature/survival` 229528f (896f2d37, landed). Report to SM-Containers (session_01RxPoRNFdorXHA4ai92EwyD).

## 1. Reading list (exact sections)

1. `Docs/Records/2026-10-14-Inventory-7.md`, all of it: what step 7 built (block use, the menu layout table, the open menu), its decisions, provisional values and known issues.
2. `Docs/Items.md`: "The crafting table and block use (survival step 7)" (the `RegisterBlockUse` contract, the menu, close order and throws) and "Crafting (survival step 6)".
3. `C:\dev\sandbox\scout-player\plan.md`, lines 164-184 (step 8a).
4. `C:\dev\tools\shift-managers\survival\value-swaps.md`:
   - section 2, rows 43-45 and 49-50: `FurnaceScreen` 176 x 166 at (232, 97); input / fuel / result at 56,17 / 56,53 / 116,35 (menu 0/1/2); player rows menu 3-29 main and 30-38 hotbar at y 84/102/120 and 142; the furnace block's defaults (hardness 3.5, correct tool yes, 8 states).
   - section 4.2, "T1 Containers", rows 203-210: fuel burn ticks (347 fuels x 3 blocks), the speed multiplier, cook times and XP (116 recipes), burn boundaries (lit at tick 1; first output at the cook total; first unlit tick = fuel + 1), cooling (unfinished progress -2 per tick, clamped 0), fractional XP (floor(items x xp) + Bernoulli(fraction)), shift-click routing (a valid input recipe for the block wins over fuel; else fuel; else main and hotbar trade), progress sprites (arrow local 79,34, width 0/6/12/18/24; flame x 56, width 14, lower edge y 50). Their findings sources are `C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md` (grep "Shift-click routing", "Progress sprites"; cite the evidence ids, e.g. `survival.r3.fuels`, `survival.r3.recipes`). Grep, never read it whole.
5. `C:\dev\tools\shift-managers\survival\PROGRAM.md` section 6 (settled decisions) and section 5 (asset locks).
6. Code, by symbol (no whole-file reads of the big files):
   - `FoundationContainer.h/.cpp`: `FFoundationMenuLayout` (`Get`, one row per `EFoundationMenuKind`; add `Furnace`: 0 input, 1 fuel, 2 result, main 3-29, hotbar 30-38, no armor or offhand, `CraftWidth` 0), `FFoundationContainerMenu` (`GetSlot`, `MayPlace`, `QuickMove`'s per-layout branches, `ClickResult`: the result slot rules are crafting's today; the furnace's output is take-only with no craft).
   - `FoundationAuthority.h/.cpp`: `RegisterBlockUse`/`FindBlockUse` (add `furnace` to the built-ins, or register it), `OpenContainer`, `FFoundationOpenMenu` (today it holds the crafting grid; a furnace's slots live in the block-entity store instead), the `ContainerClick` and `ContainerClose` cases (the open-menu id check and stillValid), `Throw` (T2's, for close leftovers), `SpawnExperience(LocalCm, Total, RandomSeed)` (T2 step 12's orb API: furnace XP is paid when the output is taken), `Step` (where a furnace tick independent of the screen goes).
   - `FoundationPanels.cpp`: `OpenContainerScreen(Lab, Kind, ContainerId)`; `FoundationPawn.cpp`: `StepInteraction` (the `OpenedContainer` hand-off).
   - `Hud/HudContainer.h/.cpp` (`GetSlotPosition(Kind, Slot)`, `IsShown`, `CanDragTo`), `Hud/HudContainerScreen.cpp` (`GetBackground`, `OnPaint`: add the flame and arrow sprites).
   - `Scripts/Editor/ImportContainerGui.py` (`SOURCES`: add `gui/container/furnace`, `gui/sprites/container/furnace/lit_progress`, `burn_progress`; new assets of our own, no lock; a `Docs/StandIns.md` row each).
   - Sounds: `block.furnace.fire_crackle` is already in `Reference/minecraft-sounds-26.3/sound_events.json` and `DA_Sounds` (T5's; never edit DA_Sounds). Check `sounds-batch.md` for its play rule.
7. `Docs/VoxelWorld.md` line ~56 (per-column edits): the block-entity store keyed by cell must outlive column unloads (session-only until phase 10's saves).

## 2. Working agreement

As in the worker template: plan first (every file against the hot files: `FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`; diff them against the latest `feature/survival`), then wait for the go. Sonnet 5.5 pre-commit review on the diff, split by path list over about 1,500 lines. `Scripts\RunChecks.ps1 -Profile quick|final|merge`; one `-DisableAdaptiveUnity` build before the final run. Measure context with `node Scripts/MeasureAgentUsage.mjs --session <id>` (copy the session's `.jsonl` into `C:\dev\sandbox\inv5b\measure-cfg\projects\C--dev-projects-Unnamed\`, `CLAUDE_CONFIG_DIR=C:\dev\sandbox\inv5b\measure-cfg`). Quiet windows: `& Scripts\QuietWindow.ps1 -Name <what>` before heavy launches outside RunChecks.

## 3. Settled decisions

- **Menus**: one `FFoundationContainerMenu` with a layout per kind (SM, step 7 Q1); block uses by id through `RegisterBlockUse` (SM, step 7 Q2). Container ids 1-100 wrap; container 0 is the player's menu.
- **Switches**: `foundation.Blocks.Use` (opens), plus plan.md's `foundation.Furnace` (0 = the furnace opens but does not tick).
- **Throws** are T2's path now (`Throw`, as Q): close leftovers and clicks outside throw; with `foundation.Items.Entities 0` they are refused `DropDeferred`.
- **XP** through T2's `SpawnExperience` when the output is taken (fractional XP per value-swaps row 208).
- **Values**: the value report rows above are allowed sources (cite evidence ids); anything else minecraft.wiki, marked provisional, listed in the record for `provisional-values.md`.
- **New GUI textures** are new assets of our own under `/Game/Gui/Container` (no lock), each with a `Docs/StandIns.md` row. DA_Items, DA_Blocks, DA_Sounds stay untouched (locks).

## 4. Code map (what step 8a touches)

| Area | Where | Today |
| --- | --- | --- |
| Menu layouts | `FFoundationMenuLayout::Get` | Inventory, Crafting |
| Block use | `FindBlockUse` built-ins | `crafting_table` |
| Open menu | `FFoundationPlayer::OpenMenu` (`Kind`, `Cell`, `Block`, `Grid`) | crafting grid only; furnaces need slots in a store keyed by cell |
| Clicks | `Execute`'s `ContainerClick` | layout menu over a trial inventory and trial grid |
| Screen | `SHudContainerScreen` (`Kind`), `FHudContainerModel::GetSlotPosition(Kind, Slot)` | inventory and crafting table |
| Events | `EFoundationEventKind::ContainerOpened`; `AFoundationController::NoteInteraction`'s name table (its `static_assert` names the last kind: extend both when adding a kind) | |
| Tests | `Tests/FoundationCraftingTableTests.cpp` (fixture `FTableLab`: a cell, facts aimed at it, `Use`, `Click`, `Close`) | the table |

## 5. Known issues carried in

- **No auto-close** (step 7): a container screen does not close when its block breaks or the player walks away; clicks are refused instead (stillValid: the block at the cell must still be the menu's block). The furnace's stillValid matters more (its contents live with the block): decide whether breaking a furnace while its screen is open closes it (vanilla closes; the player's simulation is paused while a screen is open, so only another actor or a test can break it).
- After a lab reset an open container screen shows an empty menu until closed (cosmetic).
- A key or thumb button bound to two screen actions fires both (kept, vanilla).
- From step 6: special recipe types not crafted; no remainders. Death drops walk only the hotbar and offhand (T4 step 11 must include both grids, the open menu and the cursor).

## 6. What worked and what failed (step 7)

- **Worked:** a generic layout table instead of per-menu copies (every range bounded both ends); scripted mutations, one build per mutant (`C:\dev\sandbox\inv7\mutate.ps1`, 11 of 11 caught); input tests through the real widget handlers; replacement files applied by an exact-once script (`C:\dev\sandbox\inv7\rep.py <file> <pairs>`, blocks `<<<<` old `====` new `>>>>`), written with the Write tool (bash heredocs containing backticks broke the Bash tool twice).
- **Failed:**
  - A new event kind crashed the tour: the controller's event-name array was indexed by kind and its `static_assert` pinned the old last kind. Native tests did not catch it; the tour did.
  - Local variables named `Controller` or `Owner` in a pawn method hide `AActor` members (C4458 is an error).
  - `UnrealEditor-Cmd` writes its log to `Saved/Logs`, not stdout: pass `-abslog=<path>` for a targeted test run.
  - A test laid planks next to the table, then expected a later placement in the same cell; use a fresh fixture per case.

## 7. Token figures (step 7)

Lead peak 353.3k at the merge report (334.2k at report A). Subagents: reviewer A 82.6k, reviewer B 105.0k (over the 100k brief size, under the 150k stop).

## 8. Evidence (step 7)

`Tests/Evidence/2026-10-14/Checks-final-20261014-201259/summary.txt` (GREEN, native 378), `Tests/Evidence/2026-10-14/Checks-merge-20261014-214657/summary.txt` (merge GREEN, native 379), the crafting shot `Playground-tour/Runtime-20261014-200248/runtime-60-tour-24d-crafting-table.png`, `C:\dev\sandbox\inv7\` (mutations, review diffs, logs).

## 9. Open questions (for the manager or Jesus)

1. The block-entity store's home: a new `Foundation/FoundationContainers.*` owned by the authority (recommended, plan.md) vs inside `FFoundationBlockWorld`'s per-column edits (couples the voxel save format early).
2. Whether a furnace screen closes when the furnace breaks while it is open (recommend: yes, as vanilla, by checking stillValid each step and sending the close).
3. The push of `feature/survival-containers` (Jesus).

## 10. Short prompt for step 8a

> Survival program, track T1 Containers, step 8a: the furnace (smelting). Worktree C:\dev\projects\Unnamed-inv, branch feature/survival-containers. Start with Docs/Records/2026-10-14-Inventory-Handoff-Step8a.md (reading list, settled decisions, code map), then plan.md lines 164-184. A furnace opens through step 7's block use (RegisterBlockUse) into a Furnace menu layout (0 input, 1 fuel, 2 result); its slots live in a block-entity store keyed by cell that outlives column unloads; it ticks independently of the screen (foundation.Furnace, 0 = opens but doesn't tick), with the value report's fuel, cook-time, cooling, fractional-XP (T2's SpawnExperience) and shift-click rows. Diff the hot files against feature/survival, send the plan to your SM (session_01RxPoRNFdorXHA4ai92EwyD) and wait for the go.
