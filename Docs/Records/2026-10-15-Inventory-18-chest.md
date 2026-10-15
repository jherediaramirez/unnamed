# Survival step 18-chest: the chest (track T1 Containers)

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. Base: 3e7c5493 (step 9 merged with feature/survival d6c26416). Plan: `C:\dev\tools\shift-managers\survival\tracks\T1-plan-18-chest.md`, approved by SM-Containers (with-hooks variant per PM 4 and SM-Blocks; the lid split out; attenuation 12). Mid-step, the feature freeze (Jesus, via PM 4): line 1 ends with 18-chest and 8b2b, then a full QC; bug fixes go to the QC pass; the chest lid is deferred until after QC.

## What 18-chest built

- **`FoundationChests`** (new): pairing as measured (type=left's partner on its facing's clockwise side; `C:\dev\sandbox\inv18\chest_probe.txt`), `GetPartner`, `GetHalves` (menu order: type=right, then type=left), `GetStateForPlacement` (the plant rules' facing and waterlogging, then the type; sneaking stays single or joins a sneak-clicked chest's side, taking its facing), `UpdateShape` (a single chest a half points at takes the other type; a half whose partner side no longer points back turns single), `IsBlockedAbove` (a full opaque cube, provisional), `foundation.Chest` (default 1; 0: no use and no pairing).
- **The hooks** (SM-Blocks' file, one-liners gated on the chest ids): the top of `FoundationBlockFamilies::GetStateForPlacement` and `::UpdateShape`, as planned, plus two the plan lacked: `UsesFamilyPlacement` (else a chest never reaches the family placement) and the family filter in `UpdateNeighbourShapes`' ring (else a chest neighbour never gets `UpdateShape`). `FFoundationPlacementContext::bSecondaryUse` is set in `FFoundationAuthority::MakePlacementContext` (the build site; `FoundationInteraction.cpp` untouched).
- **The store and menus:** `EFoundationBlockEntityKind::Chest` (27 slots, `Viewers`); `EFoundationMenuKind::Chest` (the barrel's layout) and `LargeChest` (90: 0-53, main 54-80, hotbar 81-89); `IsChestMenu`, `HasViewers`; `UsesBlockEntity` covers both. `GetMenuCells` gives a menu's entity cells; `GetMenuGrid`, `IsMenuValid` and the click commit use it (the barrel goes through the same path, one cell).
- **The use:** `UseChest` / `WouldUseChest` for `chest` and `trapped_chest`; blocked above either half, the click is used and nothing opens; the entities are made before the open.
- **The sounds:** `UpdateBarrelOpen` became `UpdateContainerOpen` (barrel unchanged; a chest goes to `UpdateChestOpen`): the pair's viewer count, kept on the halves' entities; one `BlockUsed` per chest (`block.chest.open` / `.close`, `Amount` 0.5, the centre or the halves' midpoint). `FFoundationOpenMenu::Partner` and `UpdateViewers` let a half left behind count its own viewers (found while writing `.BreakHalf`: without it the survivor's next open was silent).
- **The screen:** heights 168 / 222, the 6-row positions, the top 125 of generic_54, "Chest" / "Large Chest", "Inventory" at 8,128; the panels' log names.
- **Tour:** case n3, shot `24k-double-chest` ("tour double chest open", "tour double chest: slot order").

## Values

value-swaps.md l.47, l.48, l.50, l.261; R16 (`survival2.r16.animation_and_viewers`); VALUES "Double chests" (`blocks.chests`, `blocks.chest_neighbor_pairing`); the black-box run `C:\dev\sandbox\inv18\chest_probe.txt` (partner side, slot order); minecraft.wiki "Chest" (blocked above). Attenuation 12 stays in DA_Sounds. Provisional rows added to `provisional-values.md`: the both-sides tie (clockwise first), the double chest's sound at the midpoint, "conductive" as a full opaque cube.

## Tests

`Unnamed.Foundation.Chest.Menu`, `.LargeMenu`, `.Pairing`, `.BreakHalf`, `.OpenState`, `.Blocked`, `.Switch0` (new, `Tests/FoundationChestTests.cpp`); `Unnamed.Hud.Container.Chest`, `.LargeChest` (new). Docs/Items.md "Tests" lists what each checks. The native suite now runs 486 tests.

## Verification

- **Affected groups** (Chest, Hud.Container, Barrel, Interact, Blocks, CraftingTable, Furnace): 82 of 82 before the review (`C:\dev\sandbox\inv18\tests-2.log`) and after the review fixes (`tests-3.log`).
- **Mutation checks** (`C:\dev\sandbox\inv18\mutate.ps1`, `mutants.txt`; each built, run, restored; restore build exit 0): partnerside (left's partner anticlockwise) Pairing Fail; slotorder (halves swapped) LargeMenu Fail (14); bothsounds (no pair count) OpenState Fail (4); blockedoff Blocked Fail (6); sneakignored Pairing Fail (1); sneakwire (`bSecondaryUse` not set) Pairing Fail; partnerviewers (no survivor update) BreakHalf Fail (2); height (222 to 220) Hud.Container.LargeChest Fail (3). Two Pairing mutants count 0 matched lines because TestEqual on an enum prints "not equal"; the result is Fail.
- **RunChecks final -Flight smoke** after the review fixes: GREEN in 81 min (createworld waited for the CEF lock) (`Tests/Evidence/2026-10-15/Checks-final-20261015-150033/summary.txt`): native 486 of 486 (`Native-20261015-150035`, 1 warning: the skeleton loot table, not ours), voxel, hashes 9 of 9, probe, interaction, tour 46 checks 0 failures (`Playground-tour/Runtime-20261015-151408`), create-world, flight smoke, npm, sourcecheck, verifiers. The run before the review (`Checks-final-20261015-145325`) was RED: native 486 of 486, then the voxel suite crashed in `operator new` under `VoxelWorldgen::ReadFeatures`' parallel JSON parse (`VoxelAutomation-20261015-145649`), code this step does not touch, while sources were being edited and other lanes were building; the rerun passed it.
- **The sapling warning** (ACTION-PLAN Line 1 #5): absent from `Native-20261015-150035` too (no warning line names a sapling). Dropped.
- **The vanilla frame** (`Tests/Evidence/2026-10-15/Inventory-18-chest-compare/`: `double-chest-vanilla-vs-ours.png`, `compare.py`, `compare.txt`): the jar's generic_54.png composed for 6 rows against shot `24k-double-chest` at GUI scale 3: empty chest slots 100.0 %, empty main grid 100.0 %, the seam band 95.7 % (an item count crosses it), the whole window 96.7 % (items and labels are not in the reference), within 8/255.
- **Review** (Sonnet 5.5, diff only, split in two: core 89.4k, tests/HUD/tour/docs 92.3k): no bugs. Fixed: the sneak-join looks beside the destination, not at the clicked cell (a replaced grass block beside a chest; core #1, with a test); a reopen of the same cell refreshes the old partner (#3); comments (#5-#7); the blocked double case checks both halves' entities, a y 167 hit test, a trapped double chest in `.LargeMenu`, the Items.md wording (tests #3, #5-#7). Kept: reach to the opened half only (core #2: vanilla's both-halves rule is code-only; the plan's rule; a known issue), either half blocks (#4, the plan's rule), no viewer reset on a world reset (#8, entities are cleared), the tour's close order (as the barrel's).

## Known issues (with "when")

- The lid: both chests draw B1's stand-in box (a double chest as two boxes). When: deferred until after QC (then T5 B4's render-shape change, R16's lid curve, R17's model layers).
- A double chest's menu checks reach to the opened half only. When: the QC pass (QC-KNOWN-BUGS.md).
- Vanilla's 5-tick opener recheck is not modelled (a last viewer leaving in an unloaded column keeps the count); the ender chest; cats; the trapped chest's redstone; the both-sides tie (provisional). Carried from step 9: the barrel saved while open; spectators never view.
- Carried: the furnace ambience scheduling, the creative screen pausing, the focus loss under a survival screen, the missing recipe and fuel items, Codex 18 #2 (multiplayer).

## Token figures

- Lead: 73.9k at the plan; 92.8k after the first tests; 316.2k after the review fixes' tests (104 requests); measured with `MeasureAgentUsage.mjs`.
- Subagents: two Sonnet reviewers, 89.4k and 92.3k total (their notifications; no peak in the meter).

## Evidence

- `Tests/Evidence/2026-10-15/Checks-final-20261015-150033/summary.txt` (GREEN, final, after the review fixes); `Checks-final-20261015-145325` (RED, the voxel crash, before the review).
- `Tests/Evidence/2026-10-15/Inventory-18-chest-compare/`.
- `C:\dev\sandbox\inv18\` (`chest_probe.txt`, `mutate.ps1`, `mutants.txt`, `tests-2.log`, `tests-3.log`, `review.diff`, `runchecks-final-1.txt`, `runchecks-final-2.txt`, the patch scripts `p2.py`-`p8.py`).

## Next

[The 8b2b handoff](2026-10-15-Inventory-Handoff-Step8b2b.md) (the furnace particles, particles only).
