# Survival step 8b2a: Codex 22 #2/#3, R23's shift-craft fit, the menu reach, Codex 25 #1 (track T1 Containers)

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. Base: 4ee7f171 (8b1 merged with feature/survival bf7a93e7, which feature/survival then took). SM-Containers approved the plan. It split 8b2: the particles became 8b2b, which runs after step 9 (the barrel) and 18-chest. PM 4 added Codex review 25 #1.

## What 8b2a changed

- **Codex review 22, finding 2:** a double-click on a furnace's, smoker's or blast furnace's output now collects, as anywhere else.
  - `FHudContainerModel::MouseDown` excludes only the crafting result (`Layout.HasCraftResult()` and slot 0), where each click crafts.
  - The authority's PICKUP_ALL already took from a furnace's result.
- **Codex review 22, finding 3:** the drag preview holds the fuel slot to one empty bucket.
  - `FFoundationMenuLayout::GetSlotLimit(Slot, bBucket)` returns 1 for an armor slot and for a bucket in the fuel slot, else 64. Two callers share it:
    - the authority's `GetSlotMax`;
    - the screen's new `FHudContainerModel::GetSlotMax` (the slot's limit, then the item's stack size).
  - `GetDragPreview` and `CanDragTo` use it, so a fuel slot already holding a bucket takes no drag.
  - All three menus share the layout.
- **R23 shift-craft partial fit** (`survival2.r23.crafting_rules`). A crafting-result shift-click scans the hotbar 8..0, then the main grid 35..9, merging before filling. That order was already right.
  - **The change:** with room for only part of a result, that part goes in, the rest is thrown (`Result.Thrown`, Q's throw) and the crafting stops. Before, it crafted nothing.
  - With no room at all, nothing is crafted, as before.
  - With `foundation.Items.Entities 0` the partial click is refused `DropDeferred` and nothing changes, as for every other throw.
  - The crafting table and the 2x2 share `ClickResult`.
- **The menu reach** (8a1's `ContainerReachExtraCm`, 450 + 400 cm, eye to the block's box): verified by `survival2.r23.container_rule`. The provisional mark is dropped in the code comments (`FoundationAuthority.h/.cpp`) and in Items.md. No behaviour change.
- **Codex review 25, finding 1** (PM 4): the furnace ambience no longer plays in the pause menu.
  - `FoundationFurnace::StepAmbienceClock(Clock, DeltaTime, bRunning)` is the 20 Hz clock, moved out of `PlayFurnaceSounds`. With `bRunning` false it rolls nothing and resets.
  - `PlayFurnaceSounds` passes `AFoundationPawn::IsSimulationEnabled()` (new getter):
    - the pause menu and the creative screen turn the simulation off;
    - a container screen keeps it on (Codex review 12 #2), so the sounds go on under a container screen.
  - 8b2b's particles will share this clock.

## Tests

- **`Unnamed.Foundation.Recipes.ShiftCraftFit`** (2x2) and **`Unnamed.Foundation.CraftingTable.ShiftCraftFit`** (table) run R23's five cases. The table is pinned once, in `Tests/FoundationShiftCraftCases.h`, from `survival2.r23.crafting_rules`. Each case shift-clicks 2 oak logs' result:

  | Case | Before | Logs left | Result shown | Hotbar 8 | Hotbar 0 | Thrown |
  | --- | --- | --- | --- | --- | --- | --- |
  | empty | an empty inventory | 0 | none | 8 planks | empty | 0 |
  | merge_then_empty | hotbar 8: 63 planks; the rest dirt, hotbar 0 empty | 0 | none | 64 | 7 planks | 0 |
  | one_free_for_four | hotbar 8: 63 planks; the rest dirt | 1 | 4 planks | 64 | dirt | 3 |
  | three_free_for_four | hotbar 8: 61 planks; the rest dirt | 1 | 4 planks | 64 | dirt | 1 |
  | full | hotbar 8: 64 planks; the rest dirt | 2 | 4 planks | 64 | dirt | 0 |

  - The R23 table's "Changed inventory slots" give hotbar 8 and hotbar 0. Its "Dropped overflow" gives the thrown count.
  - Item entities are on, so the overflow is a thrown entity.
  - The 2x2 lab sets `Inventory.bFull` itself, because the authority applies `foundation.Inventory.Full` only on the first command. Without it the main grid ignored the dirt fill, and the first run's 2x2 failures were that, not the menu.
- **`Unnamed.Foundation.Recipes.ShiftCraft`:** the old "3 of room for 4: no craft" check is now the refusal path. With item entities off, the click is refused `DropDeferred` and nothing changes.
- **`Unnamed.Hud.Container.Furnace`:**
  - the output and input double-clicks collect in all three kinds;
  - 8 buckets dragged over the fuel slot and an empty slot preview 1 / 4 with 3 left on the cursor (Codex's scenario);
  - a fuel slot already holding a bucket takes no drag.
- **`Unnamed.Foundation.Furnace.AmbiencePause`:**
  - the clock: paused, no roll and a reset; running, 1 roll after 0.06 s and at most 4 a frame;
  - a spawned `AFoundationPawn` under `SetScreenInput(true)` keeps its simulation and rolls;
  - after `SetSimulationEnabled(false)` (the pause menu) it rolls nothing.

## Verification

- **Mutation checks** (`C:\dev\sandbox\inv8b2\mutate.ps1`, results in `mutants.txt`). Each mutant was built and its test run, then the source was restored and rebuilt:
  - fitthrow (the overflow not thrown): ShiftCraftFit 2x2 and table, Fail (4 checks);
  - fitroom (the old all-or-nothing room check): ShiftCraftFit 2x2 and table, Fail (4);
  - preview (the item's stack size, not the slot's limit): Hud Furnace, Fail (3);
  - slotlimit (no fuel-bucket cap in GetSlotLimit): Hud Furnace, Fail (6);
  - double (the old GetResultSlot exclusion): Hud Furnace, Fail (3);
  - pause (the clock ignores the pause): Furnace AmbiencePause, Fail (4);
  - craftdouble (after the review, `mutate-2.ps1`: no crafting-result exclusion at all): Hud Furnace, Fail (1).
- **Suites:** `RunChecks -Profile final -Flight smoke` GREEN after the review fixes, in 26 min (`Tests/Evidence/2026-10-15/Checks-final-20261015-093433/summary.txt`):
  - native 448 of 448 (2 with warnings);
  - voxel PASS;
  - hashes 9 of 9 equal;
  - the probe, interaction and tour PASS;
  - create-world 38 of 38;
  - the flight smoke PASS;
  - npm, sourcecheck and verifiers PASS.
- **The affected groups** before the final run, after the review fixes: 36 of 36 (`C:\dev\sandbox\inv8b2\tests-4.log`).

## Pre-commit review

Sonnet, diff only (525 lines, `C:\dev\sandbox\inv8b2\review-diff.patch`), 74.7k. No bugs found.

Fixed:
- **#2:** the AmbiencePause test roots its bare world while it holds it.
- **#5:** the authority's fuel bucket is looked up by `FFoundationMenuLayout::FuelBucketItem`, the name the screen uses, so the two can't drift.
- **#6:** the HUD test also checks that the crafting table's result still never starts a collect (mutant craftdouble).

Kept:
- **#1:** `Holds(..., 0)` already checks `IsEmpty`.
- **#4:** `CanDragTo` already calls `FFoundationMenuLayout::Get(Kind)` for every kind.
- **#3, #7, #8:** nits.

## Provisional values (for SM-Containers to update in `provisional-values.md`)

| Row | Real value column |
| --- | --- |
| T1 6, shift-craft fill order | matches: hotbar 8..0, then main 35..9, merge before fill (`survival2.r23.crafting_rules`); verified 8b2a |
| T1 6, shift-craft is all-or-nothing per craft | **differs, fixed in 8b2a:** a partial fit inserts what fits, throws the rest and consumes one craft; no room, nothing (`survival2.r23.crafting_rules`) |
| T1 8a1, container reach | done in 8b2a: the provisional mark dropped in code and Items.md (`survival2.r23.container_rule`) |

No new provisional rows.

## Known issues (with "when")

- **Particles:** 8b2b, after step 9 and 18-chest (SM-Containers' order). The design is settled in the next handoff.
- **Light 13:** lit states give light 0. When: T5 B3a (SM-Blocks).
- **The scheduling** of the sounds is provisional: 1 tick in 50 per lit block, a 16-block reach, the bottom centre. When: a scheduling model or capture.
- **The creative screen** still pauses, so the ambience stops under it. This is the carried known issue.
- **Recipe tie-breaks:** R23 says the first applicable recipe in the ordered set wins, and a cached or hinted recipe can win first. The project's "first by recipe id" row (T1 6) is unchanged and out of this step's scope.
- **Carried over:** the focus loss under a survival screen, the missing recipe and fuel items, and Codex 18 #2 (multiplayer).
