# Blocks B4 handoff: climbing, ladders and vines (survival track T5)

Lane `feature/survival-blocks` (worktree `C:\dev\projects\Unnamed-blocks`). B3b is [its record](2026-10-15-Blocks-B3b.md); this handoff is the commit after it. B4 is [the blocks plan](2026-10-14-Blocks-Plan.md)'s B4 row: ladder placement (wall-attached, facing) and survival; climbing for `#climbable` (ladder, vine, cave vines, weeping and twisting vines, an open trapdoor over a same-facing ladder): the fall speed clamp, climbing on push or jump, holding on sneak, fall distance zeroed. Switch `foundation.Blocks.Climb`. First person only, no hands (no arm, no third-person climbing pose).

**Manager:** SM-Blocks run 2 (session `session_01DUpWqKA5aBjmdjZtn1enW6`); report there, not to Jesus.

## 1. Reading list (in order)

1. [The B3b record](2026-10-15-Blocks-B3b.md), whole (about 60 lines), and [the B3a record](2026-10-15-Blocks-B3a.md) section 3.
2. [The blocks plan](2026-10-14-Blocks-Plan.md): the B4 row (line 17) and line 23 (the call-line rule).
3. `Docs/Blocks.md` "Buttons and levers (B3b)" (the placement-by-looks pattern a ladder can reuse) and "Placement" (the torch's `GetStandingOrWallState`).
4. `Source/Unnamed/Foundation/FoundationBlockFamilies.h` (whole, about 140 lines); `.cpp` `GetStateForPlacement`, `GetSurvival`, `IsFaceSturdy` (grep).
5. `Source/Unnamed/Foundation/FoundationSimulation.cpp`: T4's movement step (grep for the gravity and fall-distance lines; diff against `feature/survival` first, the plan says T4 owns it). Hot file: call lines only, ask SM-Blocks before editing.
6. Values (grep, never read whole; cite the evidence or R id):
   - `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md` lines 351-353 (`movement.ladder`, `movement.ladder_clamp`): horizontal clamp ±0.15, down −0.15 blocks/tick, sneak holds 0.
   - `C:\dev\tools\shift-managers\survival\value-swaps.md` lines 125-126 (ladder 0.4, vine 0.2; the climbing row; ascent 0.2 "source-inspected": treat as provisional unless a black-box run confirms it).
   - `SURVIVAL-RUN2-FINDINGS-26.3.md` R13 (ladders 8 states, vines 32: their shapes are in `Source/Unnamed/Tests/BlockR13Oracles.json`'s generator; extend `FAMILIES` in `Scripts/MakeR13Oracles.py` if B4 wants them).
   - `Reference/minecraft-blocks-26.3/facts/family_survival.json` (the ladder's and vines' rows).

## 2. Working agreement

- **Plan first** to SM-Blocks: every file against the hot files (`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`, and `FoundationSimulation.cpp` for B4); whether hashes, goldens or saved worlds move; the test plan. Wait for the go.
- **Checks:** `Scripts\RunChecks.ps1 -Profile quick|final` (`-Flight smoke` for the final). Don't edit sources while a RunChecks run is going: its time chain turns red (B3b's first quick run). Run clang-format (`CLAUDE.md`) BEFORE the final run, not after (B3b had to cancel one).
- **Pre-commit review:** Sonnet 5.5 on the diff only, split by path list over about 1,500 lines; re-run every suite the fixes touch.
- **Never:** push, use `git stash`, edit CLAUDE.md, or re-record goldens or world hashes.
- **Measure lead context** with `node C:\dev\sandbox\b0-sounds\MeasureAgentUsage.mjs --session <id>`; find the id by grepping `%USERPROFILE%\.claude\projects\C--dev\*.jsonl` for a phrase you wrote.

## 3. Settled decisions

- Family logic in `FoundationBlockFamilies.h/.cpp`; the authority and the simulation get call lines.
- A ladder places like a wall button (B3b): the first of `GetClickLooks` where it survives, sides only (measure the ladder with a copy of `Tests/Probes/SwitchPlacementProbe.java`'s parts A and B; public `useOn` only).
- Survival from `family_survival.json` (test every state and candidate, as `.SwitchSurvival` does).
- Movement values from VALUES 351-353; anything not there is provisional (a row in `provisional-values.md`) until a black-box movement probe, which needs Jesus's go (the plan).
- `ScheduleBlockTick` (B3b) exists if a climbing block needs a delayed update; vines' random-tick spread is B6's, not B4's.
- **Also in B4: chest no-mesh (a T1 ask via PM 4).** `chest`, `trapped_chest` and `ender_chest` stop drawing in the voxel mesh (vanilla's ENTITYBLOCK_ANIMATED render shape). Keep their 14/16 collision and outline box, their break-particle tile and their item icon. It is a `DA_Blocks` re-import; any reference change is additions-only, through the reference's own tool (stop and ask SM-Blocks before touching a hash-checked reference). B4's landing report says "chest no-mesh landed".
- **Expect in a later merge:** T1's step 18 (chests) adds one-line hooks in `FoundationBlockFamilies.cpp` (`GetStateForPlacement`, `UpdateShape`) and a `bSecondaryUse` field in `FoundationBlockPlacement.h` (PM-approved). Take them as they come; don't pre-empt them.

## 4. Code map (B3b)

- **Families:** `GetFamily` (stairs, fence, wall, pane, gate, door, trapdoor, button, lever), `UsesFamilyPlacement`, `IsUsable`, `Use`, `ReleaseButton`, `GetButtonTicks`, `GetUseSound`, `GetSurvival`, `IsFaceSturdy` (the snow underside exception).
- **Authority:** `CommitBlockUse(Player, Cell, Writes, Sound, Result, Volume, Pitch)`; `ScheduleBlockTick` / `StepBlockTicks` (first in `Step`) / `BlockTicks.Empty()` in `Reset`; `SettlePlants(..., bWriteUpperHalf)`.
- **Event:** `FFoundationEvent::Pitch`; `BlockUsed` volume in `Amount`; the view plays them.
- **Tests:** `Tests/FoundationInteractTests.cpp` (`Unnamed.Foundation.Interact.*`: Families, Switches, Gates, Placement, Hand, Survival, Hinge, Edits, Grazing, Buttons, SwitchSurvival, SwitchPlacement, R13, LoneHalf).
- **Probes:** `Tests/Probes/SwitchPlacementProbe.java` (parts A-C; chunks must report entity ticking before scheduled ticks run).

## 5. Known issues (when)

1. **A reload while a button is pressed:** block ticks are not saved (edits are not saved yet).
2. **Pressed wall buttons, up close:** 1 px edge strips sample about 1 px off vanilla's uvlock order (B2a issue 3); left as is.
3. **Wooden buttons and arrows:** arrow-held buttons wait for T2 19c.
4. **`foundation.Blocks.Interact 0`:** buttons and levers place in their default state and never pop.
5. **Any RunChecks run (flake):** the voxel step's `ReadFeatures` crash (B3a); re-run once before suspecting code.
6. **Later B steps:** pressure plates, sapling and family loot tables (B8), stems' age-0 tint (B6).

## 6. What worked, what failed (B3b)

- **Worked:**
  - A headless probe settled placement, fallback order and press timing in one Sonnet run (7,920 placements, 15 press runs, about 30 s). The existing `GetNearestLooks` table reproduced every look tie, so no tie row needed skipping.
  - The R13 cross-check offline first (a Python compare in seconds), then the C++ test from a generated fixture.
  - The full `family_survival.json` compare found a real gap at once (a ceiling switch under one snow layer).
  - Eight mutations selected by `B3B_MUTATION` in one build, nine runs, no rebuilds between.
- **Failed or slow:**
  - Appending C++ with a PowerShell here-string left `}#endif` twice (no trailing newline): end the here-string with a blank line.
  - A double-quoted PowerShell string ate the Markdown backticks (`` `f `` became a form feed): write docs with single-quoted here-strings or the Write tool.
  - Editing sources while RunChecks quick ran made its time chain red.
  - clang-format after starting the final run meant cancelling it.

## 7. Token figures (B3b)

- **Lead:** 126.5k at the plan, 269.4k at the checkpoint, see report A for the figure at report A (MeasureAgentUsage, session `c96681e8-0a74-516b-b640-e93e2c86502d`).
- **Subagents (Sonnet 5.5):** switch probe 108.7k (over the 100k brief); code reviewer 81.8k; tests and docs reviewer 86.1k.

## 8. Evidence

- `C:\dev\sandbox\b3b-blocks\r13\r13-shapes-summary.txt` (the offline R13 compare), `C:\dev\sandbox\b3b-blocks\scripts\`.
- `C:\dev\sandbox\b3b-blocks\probe\` (the probe's work folder).
- `C:\dev\sandbox\b3b-blocks\logs\`: `interact-1.log`, `interact-2.log`, `mutation-0..8.log`, `mutation-summary.txt`, `build*.log`.
- `C:\dev\sandbox\b3b-blocks\review\`: `b3b-diff-code.patch`, `b3b-diff-tests-docs.patch`.
- `C:\dev\sandbox\b3b-blocks\uvlock\uvlock_buttons.py` (the pressed-button uvlock measurement).
- RunChecks: quick before the work `Tests\Evidence\2026-10-15\Checks-quick-20261015-111029\summary.txt` (all steps pass; time chain red from edits during the run); final: see report B.

## 9. Open questions

1. **Push of `feature/survival-blocks`:** Jesus's call.
2. **CLAUDE.md's importer commands** use relative `-script=` paths that do not resolve (B1 known issue 7): Jesus's file.
3. **A black-box movement probe for climbing** (ascent speed, the trapdoor-over-ladder case): needs Jesus's go (the plan). Options: run it in B4 (exact values), or ship the VALUES numbers and mark the ascent provisional (cheaper; recommended unless Jesus has said go).

## Short prompt for the next chat

> B4: climbing, ladders and vines (survival track T5), lane `C:\dev\projects\Unnamed-blocks`, branch `feature/survival-blocks` at the B4 handoff commit. Start with `Docs/Records/2026-10-15-Blocks-Handoff-B4.md`: read it whole, then its reading list. SM-Blocks run 2 (`session_01DUpWqKA5aBjmdjZtn1enW6`) manages T5: report there. Report your plan first: every file against the hot files (including `FoundationSimulation.cpp`), whether any hash, golden or saved-world number moves, and the test plan. Wait for the go. First person only, no hands.
