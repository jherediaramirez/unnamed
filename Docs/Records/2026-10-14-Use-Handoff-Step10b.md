# Use lane handoff: step 10b, eating polish

From step 14 (axe stripping, commit `6743dfe` on `feature/survival-use`, worktree `C:\dev\projects\Unnamed-use`). Survival program track T3. After 10b: step 17 (buckets), then 16 (survival fixes). The sub-manager is "SM-Player" (session `session_01AQoPHG1guSaFSFHz6Pqd8N`); reports go there.

## Short prompt

> Survival program, track T3, step 10b: eating polish. Lane `C:\dev\projects\Unnamed-use`, branch `feature/survival-use`. Read `Docs/Records/2026-10-14-Use-Handoff-Step10b.md` first and follow its reading list. Payoff: (a) the held item's eating motion from the value report (`eating.hand`, `eating.hand_transform`: an early-use jiggle, then the exponent-27 approach; item only, no arm); (b) crumb particles (`eating.crumbs`: 5 per periodic effect, 16 at completion, the report's velocities); (c) Codex review 1's two fixes (switch 0 ends a running meal, with a test; the probe's release check waits past the deadline). All behind `foundation.Items.Use`. Start with a plan: every file, checked against the hot files after diffing them with the latest `feature/survival`. Report it to the manager session and wait for the go.

## 1. Reading list, in order

1. This file.
2. [2026-10-14-Use-Eating.md](2026-10-14-Use-Eating.md): sections 1, 2, 4 and 6 (the use path, decisions, known issues, code map).
3. `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md`, "## Eating" (lines 127-152 at the time of writing): "Hand motion" and "Crumb particles and duration". Cite the evidence ids (`eating.hand`, `eating.hand_transform`, `eating.crumbs`). **Never open Appendix H (from line 8503) or the source paths it indexes**, nor anything under `value-holder\build\` or `src\` (game code). The 66 hand matrices are raw data in `C:\dev\projects\value-holder\reports\reference\data\client.json` (search for `hand_transform`; read only that record).
4. Codex review 1: `C:\dev\tools\shift-managers\survival\codex\reviews\1-T3-10\report.md`, findings 1 and 2 (3 was rejected).
5. `C:\dev\tools\shift-managers\survival\value-swaps.md` (rows "T3 10", lines 11-16, and the notes at 170-176) and `provisional-values.md` (rows "T3 10", lines 11-12): the rows 10b updates.
6. [2026-10-14-Use-Stripping.md](2026-10-14-Use-Stripping.md): section 1 (step 14, the latest change on this lane).
7. Code:
   - `FoundationInteractionView.cpp`: `UpdateHeldItem` (the eating pose, `EatBlend`, `GetEatBlend`) and `HandleEvents` (`Eating`/`Ate`).
   - `FoundationAuthority.cpp`: `UseItemContinue` (where the `Eating` and `Ate` events are made and the effect ticks are chosen), `CancelUse`, `Step()` (the missed-step cancel).
   - `FoundationBreakEffects.h/.cpp`: the break particles (block textures only). Crumbs need item-sprite particles; see section 5.
   - `FoundationInteractionProbe.cpp`: scenario 13c (eat hold, eat release, the capture pair).
   - `Tests/FoundationEatingTests.cpp`: `FEatingLab`.

## 2. Scope

- **(a) Held-item eating motion.** Replace the project's ease (`EatBlend` toward the mouth, a bob every 4 ticks) with the report's: an early-use jiggle, then the exponent-27 approach to the mouth. Item only, no arm (Jesus's first-person rule: arms are not drawn). Fit to the matrices for the main hand at each remaining tick; record the fit error. Keep the old pose one switch value away if the look changes visibly (a look call: send the manager a capture pair plus a control).
- **(b) Crumb particles.** 5 item particles per periodic effect, 16 at completion. Velocity starts ((U-0.5)x0.1, Ux0.1+0.1, 0), is rotated with the view, then gains +0.05 Y; spawn offsets are randomized and view-relative (the report gives no exact offset: provisional, say so). The report notes particle events were not live-counted.
- **Check the effect ticks.** The report: periodic effects need elapsedTicks > floor(totalTicks x 0.21875) (7 for 32) and remainingTicks divisible by 4. Today's code emits `Eating` every 4th tick from the 8th (six for an apple: 8, 12, ... 28) and `Ate` at 32. Confirm that tick 32 is the completion effect only, not an extra periodic one (remaining 0 is divisible by 4, so check which effect it gets and that it gets one, not both).
- **(c) Codex review 1.** #1: with `foundation.Items.Use 0` a running meal ends (in `UseItemContinue` or `Step()`: cancel, outcome `Canceled`), with a native test (start, switch off, continue: canceled, nothing eaten). #2: the probe's "letting go mid-way eats nothing" waits past the original deadline (more than 1.0 s after the release), then checks `Use.bActive` false, hunger still 14, 3 apples, no `Ate`.
- **Rows:** update the T3 10 rows in `value-swaps.md` and `provisional-values.md` (pose/motion, crumbs, cadence) with what shipped and its evidence id.

## 3. Working agreement (unchanged)

- Gameplay only through `FFoundationAuthority` commands. Each step a switch; 10b stays behind `foundation.Items.Use` (presentation-only parts may get their own `foundation.*` switch if a look call needs one).
- A plan first, listing every file against the hot files (`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`), diffed against the latest `feature/survival`.
- A Sonnet 5.5 pre-commit review of the diff only (keep the brief to one file, the diff; the step 14 review peaked at 65.5k).
- `Scripts\RunChecks.ps1 -Profile final -Flight smoke` after the last code change, including any clang-format pass (formatting rebuilds the DLL, and the time chain goes red otherwise).
- DA_Items, DA_Blocks and DA_Sounds are never changed or hand-merged by this lane (asset locks). No CLAUDE.md edits. Never push. No bare `git stash`.
- Game code: values from the value report's text and its raw data only; never its Appendix H source paths, decompiled code or bytecode.

## 4. Settled decisions

- Hold-to-use has its own commands (`UseItemStart/Continue/Release`); instant uses on a block go through `UseOn` (step 14 did).
- The food and strip tables are native, pinned against the references; no asset migration this stretch.
- Vanilla first when a call is easy to change later.
- Item only for the eating motion: no arm.

## 5. Known issues, with when

- Crumbs need item-sprite particles: the break chips draw block textures only (`FoundationBreakEffects`). Either a sprite path for the chip material (the held view's `T_ItemTextures` array) or a separate small emitter. When: 10b, part (b).
- No motion for an offhand meal (the offhand is not drawn). When: when the offhand is drawn.
- Dropping one item from the stack being eaten keeps the meal going. When: step 16.
- Food table not in `DA_Items`. When: an `ImportItems.py` migration once no other lane touches item assets.
- Step 14's: no copper scrape or wax-off, no shield-offhand exception, no arm swing on a strip, strip sound volume and pitch provisional.

## 6. Code map (step 14's additions)

- `FoundationStripping.h/.cpp`: the strip table, `GetAxesTag()`, `WearPerStrip`.
- `FoundationAuthority.cpp`: `UseOn` (takes `bCreative`; step 2 calls `StripOn`), `StripOn`; `FFoundationResult::bStripped`, `Strips`; event `Stripped` (after `Ate`; the controller's name table pins it).
- `FoundationInteractionView.cpp`, `HandleEvents`: `Stripped` plays `item.axe.strip`.
- `FoundationInteractionProbe.cpp`: scenario 13d, with a hold case after each capture.
- `Tests/FoundationStrippingTests.cpp`: `FStripLab` (aims at a cell's centre from a fixed eye).

## 7. What worked and what failed

- Worked:
  - Python patch scripts with exact-match asserts (`C:\dev\sandbox\use-lane\patch_*.py`, `newline=''`).
  - `C:\dev\sandbox\use-lane\run_items.ps1 -Tag <t> -Filter <f>`: build plus one native filter.
  - `mutate.ps1 -Name -File -From -To`: one mutation, built, run, restored. `-To ''` is refused; use `' '` to delete a line.
  - `C:\dev\sandbox\use-lane\MeasureAgentUsageCdev.mjs --session <id>`: MeasureAgentUsage for a session started in C:\dev.
  - Looking at the capture pair before reporting: it caught a pair shot after the next phase's reset.
- Failed:
  - A probe capture lands a frame later: a capture case followed directly by a `Prepare` shoots the reset lab. Add a short hold case after each capture.
  - Editing a source file during a RunChecks run turns its time chain red; finish edits first.
  - clang-format after the green run forced a fourth final run; format before the final run.
  - `FFoundationBlockRegistry::GetDefault()` returns a `TSharedRef`; hold it before taking a reference.
  - One run lost 3 voxel tests to engine HTTP `socket_send_failure` under other lanes' load; a rerun was green.

## 8. Token figures

Lead 222.5k at report A (MeasureAgentUsage, session 03d57632; the capture images cost about 10k). The Sonnet review subagent peaked at 65.5k.

## 9. Evidence (step 14)

- `Tests/Evidence/2026-10-14/Checks-final-20261014-143844/summary.txt`: green; native 334, interaction 98 checks, runtime-all 143.
- Capture pair: `Tests/Evidence/2026-10-14/Playground-interaction/Runtime-20261014-145021/runtime-60-interaction-stripping-on.png` and `-stripping-off.png`.
- Mutation runs: `C:\dev\sandbox\use-lane\runs\mut-strip-*`.

## 10. Open questions (with options)

- The merge of `feature/survival` at `8cfbb86` (T1 5a and T4 chat commands) is pending the manager's go; expect a textual merge in `FoundationController.cpp` and the records README. T1 5a moved the offhand slot index from 9 to 40: check 13c/13d and the eating and strip tests after the merge.
- The eating motion: report matrices exactly (recommended, vanilla first) or the project's ease kept as switch value 0. A look call for the manager.
- Crumb rendering: a sprite path in the chip material (recommended, one particle system) or a separate emitter.
- The push of `feature/survival-use`: Jesus's call.
