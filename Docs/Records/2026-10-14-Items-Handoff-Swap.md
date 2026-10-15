# Handoff: items swap (survival T2, one small chat: item and orb values)

Lane `feature/survival-entities` (worktree `C:\dev\projects\Unnamed-ent`), after the items fixes (`24135e3`, [the fixes record](2026-10-14-Items-Fixes.md)). SM-Entities set this step on October 14. It covers one area, item and orb values: three items with tests, then the usual wrap-up. Check `git log` first. `feature/survival` was at `045169b` (T3 10b eating polish, which touches `FoundationAuthority`) when this was written; it is merged only on the manager's merge go, never mid-step.

## 1. Reading list (in order)

1. This file, all of it.
2. [The fixes record](2026-10-14-Items-Fixes.md): sections 1 and 3 (the push-out, FX-a and FX-b).
3. [The XP record](2026-10-14-Items-XP.md): section 3's provisional table and its lessons (the probe hold case, the event-name table).
4. `C:\dev\tools\shift-managers\survival\codex\reviews\8-T2-12\report.md` (about 30 lines): findings #1 to #5.
5. `C:\dev\tools\shift-managers\survival\value-swaps.md`: lines 57-58 only (the S8a rows).
6. Value sources, by grep only, never read whole:
   - `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md`: `items.physics`, `items.free_flight`, `items.ground_friction`, `items.drop.random_scatter`; for the item size, grep `items.` with `0.25`.
   - `C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md`: `survival.r5.rules`, about line 3637, and the event table in the same section.
   - Never open Appendix H's paths.
7. `C:\dev\tools\shift-managers\survival\PROGRAM.md`, sections 4 to 6.
8. The code, by function (section 4). Never read `FoundationAuthority.cpp` (about 1,850 lines) or `FoundationInteractionProbe.cpp` whole: grep, then read the function.

## 2. The work

1. **Codex review 8, #1 to #5:**
   - **#1 (medium):** an orb merges with another only when their entity-id difference is a multiple of 40, or when the seeker spawned this tick and a 2.5% roll passes. The roll comes from a deterministic stream (seeded from the orb id and the step, never `FMath::Rand`). Orbs need an id (`FFoundationExperienceOrb`, beside the items' `NextItemEntityId`). `.Xp.Merge`'s cases change. Keep the 0.5 growth on every axis (vanilla grows orbs vertically too).
   - **#2:** orbs bounce on landing. Use an orb-only Z response in `FoundationExperienceOrbs::Move`, after `MoveBox`, and leave the items' `MoveBox` alone. The factor is provisional: neither the wiki nor the reports give it. Add a row to the table and a test.
   - **#3:** the level-up sound plays at volume 0.75 x min(level / 30, 1), pitch 1, only on a positive gain that reaches a multiple of 5, and only more than 100 ticks after the last one (`survival.r5.rules`).
     - The code is `FFoundationExperienceOrbView::Update`.
     - Tests: level 5 plays at 0.125; two level-ups 10 ticks apart play one sound.
     - The view has no clock of its own. Use the authority's step, and keep the rule in a plain helper so a native test reaches it.
   - **#4:** the pickup pitch is `0.9 + 0.35 * (U - V)` (`survival.r5.rules`). Check the 0.1 volume against the event table in the same section; keep 0.1 provisional if it is absent.
   - **#5:** state the S8a-style exception (orbs already in the world stay, fly and pay) in `foundation.Items.Xp`'s help text and in the record, as `foundation.Items.Entities` does.
2. **S8a's provisional rows** (value-swaps lines 57-58), swapped to the value report where it has them, citing evidence ids:
   - ground friction: stone retains about 0.588 per tick, against today's 0.6 x 0.98;
   - ground bounce: -0.5;
   - the random scatter: horizontal radius 0.5 x U, vertical speed 0.2;
   - the item size, if the report gives one.

   `FoundationItemEntities.h` holds them (`GroundFriction`, `SpawnSpreadCm`, `SpawnSidewaysCm`, `SpawnUpCm`, `BoxCm`). These change pinned literals in `.Fall`, `.Spawn` and `.Pickup`. Name the source of every literal you move.
   - Watch for: the push-out tests (`.PushOut`) let a drop slide after it leaves. A friction change can move it, but each check only needs the target cell.
   - Watch for: the merge tests' 74/75 cm edge depends on `BoxCm`.
3. **FX-a, only if small** (about 20 lines): a neighbour cell blocks the push-out only when a full-cell box covers it. A slab or fence neighbour then lets the item out by that side. Add a test with a slab. If it grows, leave it in the known issues. FX-b stays a note for T6.

Each fix gets a mutation check (`C:\dev\sandbox\items-xp\mutate.ps1`, a fresh `-Name`). Probe-only behaviour needs a manual runtime mutation; see `logs\mf-copies-run` for the pattern. Tests pin literals, never the constants under test.

## 3. Working agreement

- Report to SM-Entities (`session_01HHSREoLkWuRSPoQFWbsqfX`) with `send_message`. First send a plan: the files against the hot files, after diffing them against the latest `feature/survival`. Then wait for the go.
- Never push. Never use bare `git stash`. No CLAUDE.md edits. DA_Items, DA_Blocks and DA_Sounds are not this step's.
- Never re-record goldens or world hashes. If one would move, stop and report.
- Values come from the value report and the survival findings (grep by evidence id), the wiki, or black-box runs. Never from decompiled code.
- Before a heavy launch outside RunChecks, run `Scripts\QuietWindow.ps1 -Name <what>` (add `-Game` for `-game` runs). Before a `-game` run, wait out `C:\dev\sandbox\locks\cef-e2e.lock`.
- Wrap-up:
  - `Scripts\RunChecks.ps1 -Profile final -Flight smoke` after the last code change. To stop a run cleanly, write `C:\dev\sandbox\locks\cancel-Unnamed-ent.flag` and remove it afterwards.
  - A Sonnet 5.5 review of the diff only.
  - Report A: staged name-status, suite counts against their base, the changed pinned values with their sources, and a provisional table.
  - Commit on the go, then write the handoff, then send report B.
- Measure the lead after every build, test run and subagent: `node C:\dev\sandbox\items-s8b\measure.mjs --session <newest .jsonl id under C:\Users\jherediaramirez\.claude\projects\C--dev\>`.

## 4. Code map

| What | Where |
|---|---|
| Item constants | `FoundationItemEntities.h` (sourced and provisional blocks, `PushOutCm`) |
| Shared motion, push-out | `FoundationItemEntities.cpp`: `MoveBox`, `IsInside`, the anonymous `PushOut` |
| Orb rules and motion | `FoundationExperienceOrbs.h/.cpp`: `Move`, `GetStepRegion`, `GetSplitValue` |
| Orb step, merges, pickups | `FoundationAuthority.cpp`: `StepExperienceOrbs` (the merge loop after the motion), `SpawnExperience` |
| Collision gather | `FoundationAuthority.cpp`: `GatherEntityCollision` |
| Orb sounds | `FoundationExperienceOrbView.cpp`: `Update` (level-up), `OnPickedUp` (pickup) |
| Block drops (scatter) | `FoundationAuthority.cpp`: `SpawnDrops` |
| Tests | `Tests/FoundationExperienceOrbTests.cpp` (`.Xp.*`), `Tests/FoundationItemEntityTests.cpp` (`.Fall`, `.Spawn`, `.Pickup`, `.Merge`, `.PushOut`); probe 13d in `FoundationInteractionProbe.cpp` (grep `xp fly in`) |
| Scratch | `C:\dev\sandbox\items-xp\`: `runtests.ps1 -Tests <filter> -Name <fresh>`, `mutate.ps1`, `mutations-fixes.ps1` (a batch to copy), `logs\`, `review\` |

## 5. Settled decisions

- Vanilla first. The value report and survival findings beat the wiki where they differ.
- First person only, no hands.
- The push-out rule (wiki: sides, then the top) stays; its speed and order are provisional (the fixes record's table).
- FX-b (the origin comes from bottom-centred bodies) is T6's note, not this step's.

## 6. Known issues (carried)

| # | Issue | When |
|---|---|---|
| S8a-a, XP-b | Drops and orbs sink through water. | Fluids for entities |
| FX-a | A slab or fence neighbour counts as obstructed for the push-out. | This step if small, else later |
| FX-b | The push-out assumes bottom-centred bodies. | T6's first entity on `MoveBox` |
| S8b-c, XP-c | Throw offsets, copy offsets, the remaining orb values. | Value-holder requests |
| XP-a, XP-d | No Silk Touch, no Mending. | Enchantments |
| XP-e | The entities reference's verifier crashes without a FAIL line when `textures.json` is missing. | T6's first change to that reference |

## 7. What worked and what failed (items fixes)

- **Worked:**
  - A 9-mutation native batch in the background (about 1.5 minutes each) while the docs were written.
  - RunChecks' cancel flag stopped a final run cleanly when a review fix arrived.
  - A runtime mutation for a probe-only check, with `TestFoundationRuntime.ps1 -OutputDirectory` into the sandbox.
- **Failed, then fixed:**
  - The first push-out kept any velocity while the centre was clear. The reviewer showed that a moving item with a block placed on its edge would cross the block. Every centre-clear overlap now leaves by the shallowest way.
  - `git commit -F -` with a here-string failed in PowerShell. Write the message to a file and use `-F <file>`.

## 8. Tokens and evidence

- **Items fixes:** lead 215.8k at report A. One Sonnet reviewer, 77.1k peak. This step should fit under 300k.
- **Evidence:**
  - `C:\dev\projects\Unnamed-ent\Tests\Evidence\2026-10-14\Checks-final-20261014-185747\summary.txt`
  - `C:\dev\sandbox\items-xp\logs\` (`mutations-fixes.txt`, `mf-copies-run\`, `fixes-t1`, `fixes-t2`)

## 9. Open questions

1. **For Jesus:** add `Reference/minecraft-entities-26.3/` to CLAUDE.md's hash-checked list. Options: (a) add it (recommended); (b) leave it out.
2. **For Jesus:** push `feature/survival-entities`. Options: (a) after the program's landing (recommended); (b) keep it local.

## Short prompt

> Survival T2 "items swap" (lane `C:\dev\projects\Unnamed-ent`, branch `feature/survival-entities`; check `git log` first). Read `Docs/Records/2026-10-14-Items-Handoff-Swap.md` all of it, then its reading list. One area, item and orb values: Codex review 8 #1-#5 (orb merge id rule, bounce, level-up and pickup sounds, the switch-0 note), S8a's provisional rows swapped to the value report, and FX-a only if small. Send your plan to SM-Entities (`session_01HHSREoLkWuRSPoQFWbsqfX`) with `send_message` and wait for the go.
