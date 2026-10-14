# Handoff: survival step 7, the crafting table (track T1 Containers)

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. Step 6 (2x2 crafting) is committed: eb48e679, then 2839daaa (a record note). The lane is waiting in the token queue to merge `feature/survival` (9f32468 or newer: T5 B0, the sound batch; `DA_Sounds` is T5's, so take the incoming one and never hand-merge it). **Do not build until your SM confirms that merge is verified.** Read-only planning may start before it.

## 1. Reading list (exact sections)

1. `Docs/Records/2026-10-14-Inventory-6.md`, all of it: what step 6 built, the decisions, the known issues, the `CanDragTo` bug the reviews missed.
2. `Docs/Items.md`: "Crafting (survival step 6)" and "The survival inventory screen (survival step 5b)" (the gestures, now with bound keys).
3. `C:\dev\sandbox\scout-player\plan.md`, lines 151-163 (step 7).
4. `C:\dev\tools\shift-managers\survival\codex\reviews\6-T1-5b\report.md`, finding 4 (section 4, line 17 on: `foundation.Inventory.Full` changed while the screen is open). It is step 7's.
5. `C:\dev\tools\shift-managers\survival\value-swaps.md`: section 2, "T1 Containers" (line 27 on), the crafting-table rows (`CraftingScreen` 176 x 166 at (232, 97); result 124,35; the 3x3 at x 30/48/66, y 17/35/53; player rows menu 10-36 main and 37-45 hotbar at y 84/102/120 and 142; the block's defaults: hardness 2.5, no correct tool); and section 4.2, "Upcoming steps, per track" (line 197 on), its T1 crafting-table rows.
6. `C:\dev\tools\shift-managers\survival\PROGRAM.md` section 6 (settled decisions).
7. Code, by symbol (no whole-file reads of the big files):
   - `FoundationRecipes.h/.cpp` (whole, short): `FFoundationRecipeBook::Match`/`Craft` take any Width x Height; the 3x3 needs no change.
   - `FoundationContainer.h` (whole) and `FoundationContainer.cpp`: `GetSlot`, `UpdateResult`, `TakeResult`, `ClickResult`, `QuickMove`. They are written for the player's menu (container 0: `ResultSlot` 0, `FirstCraft` 1, `CraftWidth`/`CraftHeight` 2, the player's slots from 5). The crafting table is a second menu layout with its own numbering (0 result, 1-9 grid, 10-36 main, 37-45 hotbar, no armor or offhand): the step's main design call.
   - `FoundationAuthority.cpp`: `UseOn` (~834; branch 1, "the target's own use interaction wins, unless a crouching player holds something", ~854-875, the crouch bypass is `bBypass`), `MakeContainerRules` (~162), the `ContainerClick` and `ContainerClose` cases in `Execute` (`ContainerId` other than 0 is refused today), `GetPlayerMenu`.
   - `FoundationInteraction.cpp`: the use order (~420, "the target's interaction (inside UseOn), then the item's place capability"), the preview mirror (`UpdatePreview` ~573; plan.md's 485-507 has moved).
   - `Hud/HudContainer.h/.cpp` (`GetSlotPosition`, `HitTest`, `CanDragTo`: the armor branch now starts at `FirstArmor`; keep every per-range branch bounded on both ends), `Hud/HudContainerScreen.cpp` (`OnKeyDown`, the bound keys).
8. The known issue to carry or fix: **a key bound to two of the screen's actions fires both** (`HudContainerScreen.cpp`, `OnKeyDown`: the hotbar, offhand and drop checks are separate `if`s; the close returns first). Vanilla checks each too, so it may stay; say so in the plan. Mouse-button bindings do nothing over the screen.

## 2. Working agreement

As in the worker template: plan first (every file against the hot files: `FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`; diff them against the latest `feature/survival`), then wait for the go. Report to SM-Containers (session_01RxPoRNFdorXHA4ai92EwyD). Sonnet pre-commit review, split by path list above about 1,500 diff lines. `Scripts\RunChecks.ps1 -Profile quick|final|merge` for suites. Measure context with `node Scripts/MeasureAgentUsage.mjs --session <id>` (copy the session's `.jsonl` into `C:\dev\sandbox\inv5b\measure-cfg\projects\C--dev-projects-Unnamed\` and set `CLAUDE_CONFIG_DIR` to `C:\dev\sandbox\inv5b\measure-cfg`).

## 3. Settled decisions

- **Recipes** live in `Reference/minecraft-items-26.3/recipes.json` and `DA_Recipes`; the 3x3 uses the same `FFoundationRules::Recipes`. Nothing to import for recipes.
- **Switch** `foundation.Blocks.Use` (plan.md): 1 = blocks open screens; 0 = right-click always places, as today. Sneaking with an item still places (vanilla), through `UseOn`'s existing bypass.
- **The vanilla container** is `textures/gui/container/crafting_table.png` (in `Reference/minecraft-menu`), imported the way `ImportContainerGui.py` does (a new standalone texture; record it in `Docs/StandIns.md`).
- **Throws stay T2's** (`DropDeferred`): a close whose 3x3 doesn't fit, Q on the result.
- **Crafting plays no sound** (vanilla).
- **The crafting table's grid belongs to the screen**, as in vanilla's CraftingMenu: closing returns it (step 6's close order: the cursor, then the grid).
- No DA_Items change is planned; `DA_Items` is lock-gated. The next DA_Items owner (T2 19a) re-imports it onto the reference's new digest (step 6 record).

## 4. Code map (what step 7 touches)

| Area | Where | Today |
| --- | --- | --- |
| Block use | `FFoundationAuthority::UseOn` branch 1 (statics' `Interaction` only) | no block opens anything |
| Client use order | `FFoundationInteraction` use and preview | placement wins over a block's use |
| Second menu | `FFoundationContainerMenu` | container 0 only; `ContainerId` != 0 refused `InvalidSlot` |
| Screen | `FHudContainerModel` / `SHudContainerScreen` | the inventory layout only |
| Panels | `UFoundationPanels::OpenSurvivalInventory` | one screen |
| Tests | `Tests/FoundationRecipeTests.cpp`, `Tests/HudContainerTests.cpp` | the 2x2 and the inventory screen |

## 5. Known issues carried in

- From the step 6 record: special recipe types not crafted; no remainders; a key bound to two screen actions fires both; mouse-button bindings inert over the screen; the creative middle drag empties the cursor even when nothing was placed.
- From 5b: keys repeat when held; the hover waits for the first mouse move; the sharp backdrop.
- Codex 5b finding 4 (the switch flipped while the screen is open): step 7's.
- Death drops walk only the hotbar and offhand (T4 step 11 must include the grids and the cursor).

## 6. What worked and what failed (step 6)

- **Worked:**
  - The reference extension built in a sandbox copy by a Sonnet subagent, with a proof script (`C:\dev\sandbox\inv6\prove.py`: old files byte-identical, manifest minus additions equal, README lines kept).
  - Scripted mutations with a build per mutant (`C:\dev\sandbox\inv6\mutate.ps1`): 8 of 8 caught.
  - An input-path test on the real widget (`Unnamed.Hud.Container.Input`): Slate events through `OnMouseButtonDown`/`Up`/`OnMouseCaptureLost`/`OnKeyDown`, the root geometry placed so the live cursor sits on the slot (the real pointer never moves).
- **Failed:**
  - A test fixture filled main slots before the authority set `Inventory.bFull` (`SetSlot` refused them silently). Set `bFull` first.
  - `CanDragTo`'s armor branch (`Slot < FirstMain`) took the new grid slots and read out of bounds; the normal build passed, RunChecks' `-DisableAdaptiveUnity` build failed twice. Run a `-DisableAdaptiveUnity` build before the final run when ranges change.
  - `UnrealEditor-Cmd -script=` needs an absolute script path (a relative one resolves under the engine's binaries).
  - A PowerShell function named `R` shadows the `r` alias (Invoke-History).

## 7. Token figures

- Step 6 lead peak 396.7k at report A.
- Subagents: the reference extension 82.3k; reviewers A 106.0k and B 85.1k.

## 8. Evidence (step 6)

`Tests/Evidence/2026-10-14/Checks-final-20261014-175614/summary.txt` (GREEN), the tour `Playground-tour/Runtime-20261014-181016` (`24c-survival-crafting`), `C:\dev\sandbox\inv6\` (proof, mutations, logs, review diffs).

## 9. Open questions (for the manager or Jesus)

1. The crafting table's menu: a second layout inside `FFoundationContainerMenu` (a layout table per container kind; recommended, as furnaces and the barrel follow) or a separate menu class.
2. Whether a key bound to two screen actions should fire only one (recommend keeping vanilla's per-action checks).
3. The push of `feature/survival-containers` (Jesus).

## 10. Short prompt for step 7

> Survival program, track T1 Containers, step 7: the crafting table (3x3). Worktree C:\dev\projects\Unnamed-inv, branch feature/survival-containers. Start with Docs/Records/2026-10-14-Inventory-Handoff-Step7.md (reading list, settled decisions, code map), then plan.md lines 151-163. Do not build until your SM confirms the lane's merge of feature/survival is verified. Right-click a crafting table opens Java's crafting screen through UseOn's block-use branch (sneaking with an item still places); switch foundation.Blocks.Use (0 = right-click always places); the 3x3 uses step 6's recipe book; also Codex's 5b finding 4. Diff the hot files against feature/survival, send the plan to your SM and wait for the go.
