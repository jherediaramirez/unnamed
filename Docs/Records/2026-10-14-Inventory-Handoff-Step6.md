# Handoff: survival step 6, 2x2 crafting (track T1 Containers)

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. Step 5b is committed (f0fe595). At the time of writing the merge of `feature/survival` (5fd40df: T3 chat commands, T2 step 4's Q throw) is still waiting for the manager's go. **Do not build until that merge is verified** (`RunChecks -Profile merge` GREEN). Read-only planning may start before it.

## 1. Reading list (exact sections)

1. `Docs/Records/2026-10-14-Inventory-5b.md`, all of it (short): what the screen is, the decisions, the known issues.
2. `Docs/Records/2026-10-14-Inventory-5a.md`, sections 1-2 (the inventory, the menu, vanilla numbering on both levels).
3. `Docs/Items.md`: "Inventory and container clicks (survival step 5a)" and "The survival inventory screen (survival step 5b)".
4. `C:\dev\sandbox\scout-player\plan.md`, lines 134-150 (step 6) and 151-163 (step 7, so the grid code serves 3x3 next).
5. `C:\dev\tools\shift-managers\survival\PROGRAM.md` section 6 (settled decisions; Q3 (a): recipes extend `Reference/minecraft-items-26.3`).
6. The manager's brief adds two more: SM-Codex's 5a findings 2, 4 and 5 (`C:\dev\tools\shift-managers\survival\codex\reviews\2-T1-5a\report.md`) and the T1 rows of `C:\dev\tools\shift-managers\survival\value-swaps.md`. Both are in step 6's scope.
7. Code, by symbol (no whole-file reads of the big files):
   - `FoundationContainer.h` (whole, short), `FoundationContainer.cpp`: `ToInventorySlot` (~40-49; menu 0-4 return INDEX_NONE today, which is how crafting clicks are refused), `MayPlace` (~67), `Pickup` (~129), `QuickMove` (~176), `Click` (~315-385, the snapshot and revision logic).
   - `FoundationItems.h`: `FFoundationInventory` (~190-260).
   - `FoundationAuthority.cpp`: the `ContainerClick` and `ContainerClose` cases in `Execute` (~646-690).
   - `HudContainer.h/.cpp`: `GetSlotPosition` (0 result 154,28; 1-4 the grid), `IsClickable` (`Slot >= 5` makes 0-4 inert), `CanDragTo` (`Slot < FirstArmor` refused).
   - `HudContainerScreen.cpp`: `OnPaint`'s slot loop.
   - `FoundationPanels.cpp`: `OpenSurvivalInventory` (`GetView` leaves 0-4 empty).

## 2. Working agreement

As in the worker template: plan first (every file against the hot files: `FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`; diff them against the latest `feature/survival`), then wait for the go. Report to SM-Containers (session_01RxPoRNFdorXHA4ai92EwyD). Sonnet pre-commit review, split by path list above about 1,500 diff lines. `Scripts\RunChecks.ps1 -Profile quick|final|merge` for suites. Measure context with `node Scripts/MeasureAgentUsage.mjs --session <id>`. It resolves the transcript folder from the script's project (`C--dev-projects-Unnamed`), so copy the session's `.jsonl` from `C:\Users\jherediaramirez\.claude\projects\C--dev\` into a temp `CLAUDE_CONFIG_DIR\projects\C--dev-projects-Unnamed\` and point `CLAUDE_CONFIG_DIR` there (`C:\dev\sandbox\inv5b\measure-cfg` is set up).

## 3. Settled decisions

- Vanilla numbering: menu 0 is the craft result, 1-4 the 2x2 grid (row-major: 1 2 / 3 4), all in container 0. No renumbering is needed: 5a reserved them.
- The grid's stacks live in the player's container. Vanilla's InventoryMenu crafting slots belong to the menu, not the Inventory. Closing returns them (as the cursor's `Return`), and what doesn't fit is thrown. Where they live (a `Craft[4]` on `FFoundationInventory` or on the menu state) is step 6's design call; the revision must cover them.
- Recipes extend `Reference/minecraft-items-26.3` through its own tool (PROGRAM.md Q3 (a)); the reference is hash-checked and regenerated only by its tool.
- Switch `foundation.Crafting` (1 on; 0 the grid draws but yields nothing).
- **Throws belong to T2** (manager's note): T2 wires T1's two container throw paths to its throw: a click outside the window with a stack, and a close without room (both refused `DropDeferred` today). That includes the update to T1's `.Authority` tests. Step 6 does not do it. If step 6 adds a third throw path (the grid on close without room), route it through the same `DropDeferred` branch so T2's wiring covers it.
- The player-model frame stays empty (Jesus: first person only).

## 4. Code map (what step 6 touches)

| Area | Where | Today |
| --- | --- | --- |
| Menu slots 0-4 | `FFoundationContainerMenu::ToInventorySlot`, `GetSlot`, `MayPlace` | INDEX_NONE: refused (`InvalidSlot`) |
| Result slot clicks | `FFoundationContainerMenu::Pickup`/`QuickMove` | none: vanilla's result slot takes only (left takes the result, shift crafts as many as fit, no placing) |
| Close | `ContainerClose` in `FFoundationAuthority::Execute` | returns the cursor only |
| Screen | `FHudContainerModel::IsClickable`, `CanDragTo`, `HitTest`; `SHudContainerScreen::OnPaint` | 0-4 highlight, no clicks, never drag targets |
| View | `UFoundationPanels::OpenSurvivalInventory` `GetView` | 0-4 empty |
| Tests | `Tests/HudContainerTests.cpp` (`.Clicks`: "crafting slot", "result slot" expect no clicks; flip these), `Tests/FoundationInventoryTests.cpp` (`.Pickup`: menu slot 0 refused; flip it) | |

## 5. Known issues carried in

The 5b record, section 5: key repeat; other keys swallowed; the hover waits for the first mouse move; the tour's hover depends on the real pointer; the sharp backdrop (vanilla blurs); shift read at the press. Item drops on death walk only the hotbar and offhand (T4 step 11; it must include the grid and the cursor too).

## 6. What worked and what failed (5b)

- **Worked:**
  - One UObject-free model for the gestures, tested without Slate. The same `Press`/`Move`/`Release` path serves probes.
  - Scripted mutations with a build line per mutant (`C:\dev\sandbox\inv5b\mutate.ps1`).
  - The pixel check of a refactor against a control pair of two old runs (`C:\dev\sandbox\inv5b\cmp.py`, crop the panel: the world behind changes run to run).
- **Failed:**
  - `-DisableAdaptiveUnity` (RunChecks final) put two files' anonymous-namespace names in one blob. Give every new file's anonymous-namespace symbols unique names.
  - A `python -` in a PowerShell chain hung waiting on stdin.
  - The Edit tool rewrote `Config/DefaultGame.ini`'s mixed line endings. Edit that file byte-exactly.
  - A tour run lost its hover shots to a real pointer move.

## 7. Token figures

- 5b lead peak 336.2k at report A.
- Two Sonnet reviewers, 93.4k and 82.3k.
- No other subagents.

## 8. Evidence (5b)

`Tests/Evidence/2026-10-14/Checks-final-20261014-144605/summary.txt` (GREEN), the tour `Playground-tour/Runtime-20261014-150454`, `C:\dev\sandbox\inv5b\` (tests, mutations, imports, review diffs).

## 9. Open questions (for the manager or Jesus)

1. Where the recipes live: extend the items reference (Q3 (a), recommended) or a new `Reference/minecraft-recipes-26.3`. Q3 (a) settled this; the question is listed only in case the extension proves too large.
2. The grid's storage: on `FFoundationInventory` (simple, survives a screen close, like the cursor) or on a per-screen menu state (vanilla-like). Recommend the inventory: the authority already snapshots and revisions it.
3. Probe P3 (a vanilla client run) still owes the fill order and the creative middle drag (from 5a).
4. The push of `feature/survival-containers` (Jesus).

## 10. Short prompt for step 6

> Survival program, track T1 Containers, step 6: 2x2 crafting in the survival inventory screen. Worktree C:\dev\projects\Unnamed-inv, branch feature/survival-containers. Start with Docs/Records/2026-10-14-Inventory-Handoff-Step6.md (reading list, settled decisions, code map), then plan.md lines 134-163. Do not build until your SM confirms the lane's merge of feature/survival is verified. Recipes extend Reference/minecraft-items-26.3 through its tool; the grid is menu slots 1-4 and the result 0 in container 0; switch foundation.Crafting (0 = the grid draws but yields nothing); throws stay T2's (DropDeferred). Diff the hot files against feature/survival, send the plan to your SM and wait for the go.
