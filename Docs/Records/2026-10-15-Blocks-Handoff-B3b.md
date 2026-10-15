# Blocks B3b handoff: buttons, levers and the button timer (survival track T5)

Lane `feature/survival-blocks` (worktree `C:\dev\projects\Unnamed-blocks`). B3a is commit `4b2d1b4b` ([its record](2026-10-15-Blocks-B3a.md)); `feature/survival` 4625b579 (T6 20a/20b1, T4 11a, T1 8a2) is merged on top (`d2cb426b`), then `72a96f1f` (a grazing test). This handoff is the commit after that. B3b is the rest of [the blocks plan](2026-10-14-Blocks-Plan.md)'s B3 row: buttons (`powered`, unpowered after 20 or 30 ticks on an authority timer list, cancelled on break), levers (toggle `powered`), their wall, floor and ceiling placement and survival. Its scout doc is `C:\dev\sandbox\scout-b3\facts.md`.

**Manager:** SM-Blocks run 2 (session `session_01DUpWqKA5aBjmdjZtn1enW6`); report there, not to Jesus.

## 1. Reading list (in order)

1. [The B3a record](2026-10-15-Blocks-B3a.md), whole (about 50 lines).
2. `C:\dev\sandbox\scout-b3\facts.md` sections 4.4 (buttons), 4.5 (lever), 4.6 (common rules), 5 (oracles 1 lever row, 3-6, 8, 14, 15), 7 (the `BlockTicks` sketch) and 9 (gaps h and i). Grep the headings.
3. `Source/Unnamed/Foundation/FoundationBlockFamilies.h` (whole, about 110 lines) and `FoundationBlockFamilies.cpp` `Use`, `GetSurvival`, `GetStateForPlacement` (grep).
4. `FoundationAuthority.h` `CommitBlockUse` and `FoundationAuthority.cpp` `FindBlockUse`, `CommitBlockUse`, `Step` and `Reset` (grep).
5. `Source/Unnamed/Tests/FoundationInteractTests.cpp` `FInteractLab` and `SwitchRows` (the lever row was left out: add it).
6. Values (grep, never read whole; cite the evidence id):
   - `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md` "Buttons, lever" rows.
   - `C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md` F 5053-5069 (button durations), the lever row of F 5071-5135, F 3561-3567 and 3639-3729 (button and lever sounds).
   - `C:\dev\projects\value-holder\SURVIVAL-RUN2-FINDINGS-26.3.md` (new, 59k lines, the same rule as the other two; cite the R id): R13 "Every requested block state" and Appendix A (`survival2.r13.shapes`, `survival2.r13.connections`).
   - `C:\dev\tools\shift-managers\survival\value-swaps.md` the T5 B3 rows (about lines 252-260).

## 2. Working agreement

- **Plan first** to SM-Blocks: every file against the hot files (`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`); whether hashes, goldens or saved worlds move; the test plan. Wait for the go.
- **Diff before editing** a file other lanes have dirty (SM-Blocks' `verify_lane` flagged `FoundationAuthority.h/.cpp` and `FoundationInteractionView.cpp` for the ent, inv and mobs lanes). Keep hot-file edits to call lines.
- **Switch:** `foundation.Blocks.Interact` covers buttons and levers too (B3a's; 0: no use, default-state placement).
- **Checks:** `Scripts\RunChecks.ps1 -Profile quick|final` (`-Flight smoke` for the final); `Scripts\QuietWindow.ps1 -Name <what>` before every heavy launch outside RunChecks.
- **Pre-commit review:** Sonnet 5.5 on the diff only, split by path list over about 1,500 lines; re-run every suite the fixes touch. Then report A, the SM's approval, the commit, the handoff, report B.
- **Never:** push, use `git stash`, edit CLAUDE.md, or re-record goldens or world hashes. Subagents delete nothing.
- **Measure lead context** with `node C:\dev\sandbox\b0-sounds\MeasureAgentUsage.mjs --session <id>`; find the id by grepping `%USERPROFILE%\.claude\projects\C--dev\*.jsonl` for a phrase you wrote.

## 3. Settled decisions

- **First task: the R13 cross-checks.** Before B3b's own work, compare `SURVIVAL-RUN2-FINDINGS-26.3.md` R13 / Appendix A (outline and collision per state, `survival2.r13.shapes`; connections) against the registry for doors, trapdoors, gates, buttons and the lever (a test, like `Blocks.Models`' facts loop). Report any mismatch before building on it.
- **Family logic** lives in `FoundationBlockFamilies.h/.cpp`; the authority gets call lines. Add `Button` (`Block.Buttons`) and `Lever` (id `lever`) to `EFoundationBlockFamily`; `IsUsable`, `Use` and `GetSurvival` extend; `GetUseSound` gains `click_on`/`click_off` (`facts/block_use_sounds.csv`).
- **Uses** go through `FFoundationAuthority::CommitBlockUse` and the `BlockUsed` event (B3a, gap g). The lever's call is volume 0.3, pitch 0.6 on and 0.5 off (F 3561-3567); buttons 0.3 asset volume with on/off pitch (F 3639-3729). The view's `BlockUsed` case plays 0.9 + 0.1U today: buttons and levers need their own pitch, so pass the pitch in the event (an `Amount`-style field) rather than a second rule in the view.
- **The timer** (gaps h, i): `BlockTicks` in the authority (`Cell`, the exact pressed state, steps left), counted in `Step()`, cleared in `Reset()`. A due timer releases only if the cell still holds that state, so a broken or replaced button drops its timer. Stone and polished blackstone 20 steps, the 13 wooden buttons 30 (F 5053-5069, `survival.r9.buttons`). Pressing a pressed button does nothing (its release stays).
- **Arrow-held buttons:** deferred until T2 19c (arrows) lands (`survival.r9.arrow_button`, F 5139-5256).
- **Uvlock for pressed buttons (x=90):** B2a known issue 3; check it against `C:\dev\sandbox\b2-blocks\proof\uvlock_orders.py` when the pressed state is drawn.
- **Redstone:** none. Buttons and levers flip only their own `powered`.
- First person only, no hands.

## 4. Code map (B3a)

- **Families:** `FoundationBlockFamilies.h/.cpp`: `GetFamily` (B2b's five plus `Door`, `Trapdoor`), `IsInteractOn` (`foundation.Blocks.Interact`), `UsesFamilyPlacement`, `IsUsable`, `Use`, `GetUseSound`, `GetSurvival` (`EFoundationFamilySurvival`), the hinge (`GetDoorHingeSide`, anonymous namespace), `GetStateForPlacement` (doors, trapdoors). Anonymous helpers must have names no other `.cpp` uses (unity builds).
- **Authority:** `FindBlockUse`'s family fallback; `CommitBlockUse` (public); `GetSurvival` in `CheckPlantPlacement` and `CanPlantSurvive`; `SettlePlants`' `PopOtherHalf` guard and the ring over `Changed`.
- **Events:** `EFoundationEventKind::BlockUsed` (last); `FoundationController.cpp` names it `used`; `FoundationInteractionView.cpp` plays it.
- **Light:** `BlockImportData.py` `cube_light_fields`, `ImportBlocks.py` (`state_light` for non-model blocks), `VoxelBlocks.cpp` (`GetLightEmission(State)` for every state).
- **Probe:** `Tests/Probes/DoorHingeProbe.java`, `run-door-hinge-probe.ps1`, `door-hinge-26.3.json` (11,100 cases; SHA-256 in `Tests/Probes/README.md`). A button and lever placement probe can copy it (public `useOn` only).
- **Tests:** `Tests/FoundationInteractTests.cpp` (`Unnamed.Foundation.Interact.*`: Families, Switches, Gates, Placement, Hand, Survival, Hinge, Edits, Grazing).

## 5. Known issues (when)

1. **Any RunChecks run (flake):** the voxel step crashed once in `VoxelWorldgen::ReadFeatures` (`VoxelFeatureReader.cpp:1121`, an access violation in the parallel decoration read, `Tests\Evidence\2026-10-15\Checks-final-20261015-073418`); it passed on the re-run. Re-run once before suspecting code; report it with the dump path if it repeats. B2b's audio-device flake in `Unnamed.Voxel.Spawn.Reader` and `.Standing` may also show.
2. **B3b:** the view plays every `BlockUsed` at pitch 0.9 + 0.1U (doors, trapdoors, gates). Buttons and levers need their own (decision above).
3. **Old saved worlds:** a door saved before B3a as one lower half stays one half; family blocks keep their stored states until a neighbour changes (B2b issue 3).
4. **The streamed world's edge:** rules read unknown cells as air (B2b issue 2); a door use whose other half is unknown is refused.
5. **B6, B8:** stems' age-0 tint; sapling and family loot tables (door, trapdoor, gate, button and lever loot stays in B8; B3a only made a broken door yield one item).
6. **B3b:** uvlock on pressed buttons (decision above), fence-bar and open-gate undersides (B2a issue 3).

## 6. What worked, what failed (B3a)

- **Worked:**
  - A headless public-API probe settled the hinge in one subagent run (11,100 cases, about 30 s per run); appending parts D and E later kept A-C byte-identical.
  - Mutations selected by an environment variable (`B3A_MUTATION`) in one build: 6 runs of the suite, no rebuilds between (`C:\dev\sandbox\b3a-blocks\logs\mutation-summary.txt`). Restore from the sandbox copy and touch the files afterwards.
  - Comparing every candidate state of `family_survival.json` (1.25 million cases, seconds) instead of samples.
  - The `DA_Blocks` T3D diff, checked line by line with the new field removed, proves an import change is only what it claims.
- **Failed or slow:**
  - A reviewer suggested the hinge rule from recalled vanilla code: not an allowed source. Measuring the two cases with the probe was quick; do that instead of arguing from memory.
  - Test fixtures that block the aim ray (a support block under a fence, a slab's top below the aimed point) fail quietly as "not placed": aim at a box point (`FInteractLab::BoxPoint`) or a point on the real surface.
  - The lab breaks as creative until both `Vitals.Mode = Survival` and `P().Inventory.bInfiniteSupply = false` (no drops otherwise).
  - Eating starts with `UseItemStart`, not `UseOn`; `Options.bItemUse` and `bBlockUse` default off in a test authority.
  - Git Bash `grep -c $'\r'` miscounted line endings; `git ls-files --eol` is the reliable check (the repo is LF).

## 7. Token figures (B3a)

- **Lead:** 345.9k at the checkpoint (all features in), 426.8k at report A (MeasureAgentUsage, session `4774b293-6ee4-5ad4-b10f-9da363c47cf4`).
- **Subagents (Sonnet 5.5):** hinge probe 85.0k; probe parts D and E 58.8k; code reviewer 74.7k; tests and docs reviewer 108.2k (over the 100k budget).

## 8. Evidence

- `C:\dev\sandbox\b3a-blocks\logs\`: `interact-1..5.log`, `review-fixes.log`, `mutation-0..6.log`, `mutation-summary.txt`, `merge-interact.log`.
- `C:\dev\sandbox\b3a-blocks\import\`: `da-blocks-before.t3d`, `da-blocks-after.t3d`, `blocks1.log` (the import), `blocks2-verify.log`, `blocks3-verify-after-review.log`, `assets-pre-b3a\`.
- `C:\dev\sandbox\b3a-blocks\review\`: `b3a-diff-code.patch`, `b3a-diff-tests-docs.patch`.
- RunChecks: final before the merge (voxel crash) `Tests\Evidence\2026-10-15\Checks-final-20261015-073418\summary.txt`; its re-run (GREEN) `Tests\Evidence\2026-10-15\Checks-final-20261015-074531\summary.txt`; final after merging 4625b579: see report B.

## 9. Open questions

1. **Push of `feature/survival-blocks`:** Jesus's call.
2. **CLAUDE.md's importer commands** use relative `-script=` paths that do not resolve (B1 known issue 7): Jesus's file.
3. **The button and lever pitch in `BlockUsed`:** a pitch field in the event (recommended) or per-family rules in the view; SM-Blocks' call (hot header).

## Short prompt for the next chat

> B3b: buttons, levers and the button timer (survival track T5), lane `C:\dev\projects\Unnamed-blocks`, branch `feature/survival-blocks` at the B3b handoff commit. Start with `Docs/Records/2026-10-15-Blocks-Handoff-B3b.md`: read it whole, then its reading list. Its first task is the R13 cross-check (`SURVIVAL-RUN2-FINDINGS-26.3.md`). SM-Blocks run 2 (`session_01DUpWqKA5aBjmdjZtn1enW6`) manages T5: report there. Report your plan first: every file against the hot files, whether any hash, golden or saved-world number moves, and the test plan. Wait for the go.
