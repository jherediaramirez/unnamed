# Handoff: items fixes (survival T2, one small chat)

Lane `feature/survival-entities` (worktree `C:\dev\projects\Unnamed-ent`), after step 12 (`04f3079`, [the XP record](2026-10-14-Items-XP.md)). This step was set by sub-manager SM-Entities on October 14. It is small: three fixes, each with a test, then the usual wrap-up. Check `git log` first; the lane may already have merged `feature/survival` (at `38399ad` when this was written, possibly later).

## 1. Reading list (in order)

1. This file, all of it.
2. [The S8b record](2026-10-14-Items-S8b.md): sections 1 (throw, merge, despawn), 4 (S8a-b) and 6 (the code map).
3. [The XP record](2026-10-14-Items-XP.md): section 3's lessons (the probe hold case, the event-name table) and the provisional table.
4. `C:\dev\tools\shift-managers\survival\codex\reviews\4-T2-4\report.md` (18 lines): findings #2 to #5.
5. `C:\dev\tools\shift-managers\survival\PROGRAM.md`, sections 4 to 6.
6. The code, by function (section 4 below). Never read `FoundationAuthority.cpp` (about 1,800 lines) or `FoundationInteractionProbe.cpp` whole: grep, then read the function.

## 2. The work

1. **S8a-b: a block placed over a drop pushes it out** (vanilla). Today the drop falls through the block. Vanilla pushes an entity inside a solid block toward the nearest free face. Check the value report first (grep `push`, `closest`, `inside`; this handoff's grep found nothing). If the report lacks it, use minecraft.wiki ("Entity", "Item (entity)") and mark the speed and rule provisional. Do the same for the experience orbs (`StepExperienceOrbs`), since they share `MoveBox`. A native test is needed: place a block on a resting drop; it ends outside the block, on the free side.
2. **Codex review 4, findings #2 to #5**, each with a test:
   - #2: merge only when the sum fits the max stack (vanilla leaves 40 and 40 apart). Today's `.Merge` "64 and 16" case changes to "40 and 40 stay".
   - #3: on a tie, the seeker moves into the other (`B.Count >= A.Count ? B : A`). Today's tie case changes: the survivor is the other stack, and the test checks its position. Recheck the "longer delay, younger age" asserts.
   - #4: with `foundation.Items.Entities 0`, a stack draws 1 copy (`FoundationItemEntityView.cpp`, the `GetCopies` call in `Update`).
   - #5: `Drop` calls `CheckHand` first (a dead player's Q is refused with `Dead`).
3. **T1's two container throw paths** (the PM and SM-Containers agreed T2 owns this). `FFoundationAuthority::Execute`'s `ContainerClick` (a click outside the window) and `ContainerClose` (no room for the cursor stack) are refused today with `DropDeferred` (at `04f3079`, about lines 883 and 900; T1's 5b may have moved them). Wire both to the S8b throw: the same spawn as `Drop` (from 0.3 below the eye along the look, delay 40), committing the trial inventory. With `foundation.Items.Entities 0` they keep refusing with `DropDeferred`. Update T1's tests: `FoundationInventoryTests.cpp`, `.Authority` (about lines 382 and 399: "throw ... DropDeferred", "close with no room is deferred"), with a switch-0 control. Factor the throw's spawn out of `Drop` into one helper and don't copy it.

Each fix gets a mutation check (`C:\dev\sandbox\items-xp\mutate.ps1`, fresh `-Name`). Tests pin literals, never the constants under test.

## 3. Working agreement

- Report to SM-Entities (`session_01HHSREoLkWuRSPoQFWbsqfX`) with `send_message`: first a plan (files against the hot files, after diffing them against the latest `feature/survival`), then wait for the go.
- Never push. Never use bare `git stash`. No CLAUDE.md edits.
- Never re-record goldens or world hashes: if one would move, stop and report.
- Values come from the value report (grep by evidence id, never read it whole, never open its Appendix H paths), the wiki or black-box runs. Never from decompiled code.
- Before heavy launches run `Scripts\QuietWindow.ps1 -Name <what>` (`-Game` for windowed runs). Wait out `C:\dev\sandbox\locks\cef-e2e.lock` before a windowed `-game` run.
- Wrap-up:
  - `Scripts\RunChecks.ps1 -Profile final -Flight smoke` after the last code change.
  - A Sonnet 5.5 review of the diff only; over about 1,500 lines, split it across two reviewers by path list.
  - Report A (staged name-status, suite counts against their base, a provisional table).
  - Commit on the go, then a handoff, then report B.
- Measure the lead's context after every build, test run and subagent with `node C:\dev\sandbox\items-s8b\measure.mjs --session <transcript id>`. The transcript id is the newest `.jsonl` under `C:\Users\jherediaramirez\.claude\projects\C--dev\`.

## 4. Code map

| What | Where |
|---|---|
| Throw | `FoundationAuthority.cpp`, `Drop` |
| Item merge | `FoundationAuthority.cpp`, `MergeItemEntities` (the S8b-a cadence and the `Into`/`From` choice) |
| Item step, pickups | `FoundationAuthority.cpp`, `StepItemEntities` |
| Shared motion | `FoundationItemEntities.cpp`, `Move` and `MoveBox` (items and orbs) |
| Orbs | `FoundationExperienceOrbs.h/.cpp`; `FoundationAuthority.cpp`, `StepExperienceOrbs` and `SpawnExperience` |
| Hand checks | `FoundationAuthority.cpp`, `CheckHand` |
| Container commands | `FoundationAuthority.cpp`, `Execute`, the `ContainerClick` and `ContainerClose` cases; `FoundationContainer.h/.cpp` (the menu's `Thrown`) |
| Copies | `FoundationItemEntityView.cpp`, `Update`'s `GetCopies` call |
| Event names | `FoundationController.cpp`, `NoteInteraction` (a new event kind needs a name there) |
| Tests | `Tests/FoundationItemEntityTests.cpp` (`.Merge`, `.Throw`), `Tests/FoundationInventoryTests.cpp` (`.Authority`), `Tests/FoundationExperienceOrbTests.cpp` |
| Scratch | `C:\dev\sandbox\items-xp\`: `runtests.ps1 -Tests <filter> -Name <fresh>`, `mutate.ps1`, `logs\`, `review\` |

## 5. Settled decisions

- Vanilla first. The value report beats the wiki where they differ (Codex #1 was settled that way in step 12).
- The S8a swap rows (friction 0.588, bounce -0.5, the scatter, the item size) and the orb pickup sound and level-up rule are a later "items swap" step, not this one.
- First person only, no hands. DA_Items, DA_Blocks and DA_Sounds are not this step's.

## 6. Known issues (carried)

| # | Issue | When |
|---|---|---|
| S8a-a | Drops and orbs sink through water. | Fluids for entities |
| S8b-c, XP-c | Provisional throw offsets, copy offsets, merge direction details, orb values (the XP record's table). | Value-holder requests |
| XP-e | The entities reference's verifier crashes without a FAIL line when `textures.json` is missing. | T6's first change to that reference |

## 7. What worked and what failed (step 12)

- **Worked:**
  - A background mutation batch (17 mutations, about 4 minutes each) while docs were written.
  - Splitting the review across two reviewers by path list.
- **Failed, then fixed:**
  - A new `EFoundationEventKind` overran `NoteInteraction`'s name table. Only runtime-all crashed; the interaction suite alone did not.
  - A probe shot followed directly by a lab reset came out empty while its counts passed. Always add a hold case after a capture that a reset follows.
  - A test lab without a block registry built from the assets crashed on `FindBlock`.

## 8. Tokens and evidence

- **Step 12:** lead 414.5k at report A. Two Sonnet reviewers, 68.1k and 67.0k peak. This step should fit well under 300k.
- **Evidence:** `C:\dev\projects\Unnamed-ent\Tests\Evidence\2026-10-14\Checks-final-20261014-164516\summary.txt`; `C:\dev\sandbox\items-xp\logs\`.

## 9. Open questions

1. **For Jesus:** add `Reference/minecraft-entities-26.3/` to CLAUDE.md's hash-checked list. Options: (a) add it (recommended); (b) leave it out.
2. **For Jesus:** pushing `feature/survival-entities`. Options: (a) after the program's landing (recommended); (b) keep it local.

## Short prompt

> Survival T2 "items fixes" (lane `C:\dev\projects\Unnamed-ent`, branch `feature/survival-entities`; check `git log` first). Read `Docs/Records/2026-10-14-Items-Handoff-Fixes.md` all of it, then its reading list. Three fixes with tests: S8a-b push-out, Codex review 4 #2-#5, T1's two container throw paths wired to the S8b throw. Send your plan to SM-Entities (`session_01HHSREoLkWuRSPoQFWbsqfX`) with `send_message` and wait for the go.
