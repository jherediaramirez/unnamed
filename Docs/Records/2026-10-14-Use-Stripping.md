# Use step 14: axe stripping (record)

Lane `feature/survival-use` (worktree `Unnamed-use`), from `5efbf6c`. Survival program track T3, scout AC's plan step 14 (`C:\dev\sandbox\scout-player\plan.md`, line 226). Next on this lane: step 10b (eating polish), then 17 (buckets) and 16 (survival fixes); the handoff is [2026-10-14-Use-Handoff-Step10b.md](2026-10-14-Use-Handoff-Step10b.md).

## 1. What was built

- **Behaviour** (`Docs/Items.md`, "Stripping"): right-click a log, wood, stem, hyphae or bamboo block with any of the 7 axes and it becomes its stripped block, axis kept, for 1 point of wear (none in creative or with tool wear off; the last point breaks the axe). `item.axe.strip` plays at the block. Either hand strips; the offhand only after the main hand passes. Switch: `foundation.Items.Use` (step 10's), 0 lets the axe pass as before.
- **Data** (`FoundationStripping.h/.cpp`, new): `GetTable()` / `FindStripped()`: 25 pairs `x` to `stripped_x`, following the wiki "Axe" page; `GetAxesTag()` looks up `Item.Axes` (the `#minecraft:axes` item tag data); `WearPerStrip` 1. Kept out of the hot `FoundationItems` files. No `DA_Items`, `DA_Blocks` or `DA_Sounds` change.
- **Authority** (`FoundationAuthority.h/.cpp`): `UseOn` takes `bCreative` and, after the target's own interaction, calls `StripOn` before the place capability. `StripOn` swaps the state through the saved form (`Save`, id swapped, `Load`), wears the hand, and adds `Stripped` (and `ToolBroke`). New: event kind `Stripped` (appended after `Ate`), `FFoundationResult::bStripped`, the `Strips` counter.
- **Presentation:** `FoundationInteractionView.cpp` plays `item.axe.strip` (volume 1, pitch 1, provisional); `FoundationController.cpp`'s event-name table gained "stripped" (its `static_assert` now pins `Stripped`).
- **Probe:** `FoundationInteractionProbe.cpp` scenario 13d: an oak log (axis x) stripped with an iron axe in the finite lab, then the capture pair `stripping-on` / `stripping-off` (switch 0).
- **Tests** (`FoundationStrippingTests.cpp`, new): `Unnamed.Foundation.Items.StripReference` and `.Strip` (see `Docs/Items.md`, "Tests").

## 2. Decisions

- Stripping goes through `UseOn` (settled at step 10): an instant use on a block, whose result carries the cell and the state.
- The shared switch `foundation.Items.Use`, no new one (settled; the manager's go).
- The strip table is native, pinned against the block reference's `stripped_*` blocks (vanilla's map is in game code only). The pairs follow the wiki; the reference adds poplar, so its log and wood are in.
- Vanilla first: 1 wear (wiki "Axe"); stripping works in either hand and while crouching (it is the item's use, not the block's).
- Volume 1 and pitch 1 for `item.axe.strip`: provisional until a vanilla capture.

## 3. Verified

- Native: `.StripReference` and `.Strip` pass (`C:\dev\sandbox\use-lane\runs\strip-132236`).
- Mutations (each fails its test, then restored): one pair dropped (cherry wood) fails `.StripReference` (`runs\mut-strip-pair-132408`); `WearPerStrip` 0 fails `.Strip` (`runs\mut-strip-wear-132314`).
- Final checks green on the final code: native 334 of 334, voxel 207, 9 of 9 expected hashes, runtime all 143 checks, interaction 98 (94 before; the 4 are 13d's), tour 27, create-world 38, the smoke flight, npm 85, source-check, verifiers.
- The pre-commit review (Sonnet, diff only) found one weak assertion (the offhand meal checked after the release, which ends a client use by itself); it is checked before the release now.
- The first capture pair was shot after the next phase's reset (the screenshot lands a frame later); 13d now has hold cases after each capture, as 13c does.

## 4. Known issues, with when

- The axe's other uses on a block (scraping oxidised copper, removing wax). When: once copper oxidation and waxing exist.
- Vanilla's shield exception (a main-hand axe does not strip while a shield is in the offhand and the player is not crouching). When: when shields exist (items S7 follow-up).
- No arm swing on a strip (the client's swing follows placements only). When: step 16 or the hand-animation lane.
- The strip sound's volume and pitch are provisional. When: a vanilla capture (Jesus's go).

## 5. Code map

- `FoundationStripping.h/.cpp`: the table, the tag lookup, the wear constant.
- `FoundationAuthority.cpp`: `UseOn` (step 2 calls `StripOn`), `StripOn` (after `UseOn`).
- `FoundationInteractionView.cpp`, `HandleEvents`: case `Stripped`.
- `FoundationController.cpp`, `NoteInteraction`: the name table.
- `FoundationInteractionProbe.cpp`: scenario 13d (after 13c).
- `Tests/FoundationStrippingTests.cpp`: `FStripLab` (aims at a cell's centre from a fixed eye; direct commands use the client's sequence counter).

## 6. Evidence

- `Tests/Evidence/2026-10-14/Checks-final-20261014-143844/summary.txt`: green.
- Capture pair: `Tests/Evidence/2026-10-14/Playground-interaction/Runtime-20261014-145021/runtime-60-interaction-stripping-on.png` (the stripped log, axis x, the axe's durability bar) and `-stripping-off.png` (switch 0: the bark log, no wear).
- Native strip run: `C:\dev\sandbox\use-lane\runs\strip-132236`; mutations: `runs\mut-strip-pair-132408`, `runs\mut-strip-wear-132314`.
