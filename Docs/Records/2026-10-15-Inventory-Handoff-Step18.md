# Handoff: 18-chest, then 8b2b (the furnace particles) (track T1 Containers)

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. Step 9 (the barrel) is committed: f862db54 (record `2026-10-15-Inventory-9.md`). Report to SM-Containers (session_01EuH6RLbTU1zzFVHK75nWYJ); the program manager is PM 4 (session_01AARfM5mjEz5npqB79VSTLC). The lane is queued for its merge after T6 20b2b and T2 19a2, so your base will be a new feature/survival merge: re-diff the hot files on it.

SM-Containers' order (ACTION-PLAN.md, Line 1): 18-chest next, then 8b2b. Each is its own chat. Section A is 18-chest. 8b2b's settled design is unchanged: `2026-10-15-Inventory-Handoff-Step9.md` section B.

## Working agreement (both)

As in step 9:
- **Plan first:** every file, diffed against the latest feature/survival. Put any DA_Blocks, DA_Sounds or block-reference need at the top: those are SM-Blocks' through the PM. Wait for the go.
- **Mutation checks:** copy `C:\dev\sandbox\inv9\mutate.ps1`. It takes a table of file, old text, new text and test; it calls `Scripts/QuietWindow.ps1`, builds, runs, restores and rebuilds. Use exact post-clang-format text. Don't edit sources while it runs (docs are fine).
- **Patching:** small Python patch scripts (`C:\dev\sandbox\inv9\patchlib.py`: exact-once replacement, LF kept). Gotcha: a `\` line continuation inside a heredoc'd Python string got lost twice in step 9; write it as `chr(92)` or check the file afterwards.
- **Review:** a Sonnet 5.5 pre-commit review on the diff only (`git add -N` new files first so they appear in the diff).
- **Final checks:** `Scripts\RunChecks.ps1 -Profile final -Flight smoke` after the last code change; about 25-29 min. Run it in parallel with the review, then once more after the review fixes.
- **Context:** `node Scripts/MeasureAgentUsage.mjs`. Copy the session `.jsonl` (`C:\Users\jherediaramirez\.claude\projects\C--dev\<session>.jsonl`) into `C:\dev\sandbox\inv5b\measure-cfg\projects\C--dev-projects-Unnamed\` (overwrite), then set `CLAUDE_CONFIG_DIR=C:\dev\sandbox\inv5b\measure-cfg`.
- **Editor scripts:** an absolute `-script=` path and `Start-Process -Wait`.
- **Commit messages:** `git commit -F <file>`.
- **Test labs:** set `Inventory.bFull` yourself (the authority applies `foundation.Inventory.Full` only on the first command). `FBarrelLab` (`Tests/FoundationBarrelTests.cpp`) is a two-player lab with per-player sequences; copy it for the chest.

## A. 18-chest

### Reading list

1. `Docs/Records/2026-10-15-Inventory-9.md`, all of it: the store kind, `UsesBlockEntity`, the ChestMenu shift-click branch, `UpdateBarrelOpen` and its call sites, `BlockUsed` with `Amount`/`LocationCm`, the screen's per-kind height.
2. `Docs/Items.md`, "The barrel (T1 step 9)".
3. `C:\dev\projects\value-holder\SURVIVAL-RUN2-FINDINGS-26.3.md` (grep only):
   - R16, lines 158-185 (`survival2.r16.animation_and_viewers`, `survival2.r16.lid_curve`, `survival2.r16.openers`):
     - the lid's raw openness moves ±0.1 per client tick, clamped to [0,1];
     - eased `1 − (1 − openness)^3`; the lid and latch X rotation is −eased × π/2;
     - the open sound plays on the first viewer, the close sound on the last leaving;
     - opener rechecks run every 5 world ticks;
     - a double chest uses the maximum of its two halves' openness, and the duplicate half's sound is suppressed;
     - the chest's sound plays from the block centre (R16's chest rows: x.5, y.5, z.5).
   - R17 (`survival2.r17.model_layers`, line 193): the chest halves' model layers (`<model>#<layer>`; pivots, cubes, UVs) in the companion JSON. Grep the layer index (Appendix B); never open Appendix H's source paths.
4. value-swaps.md:
   - line 48: `generic_54` 6-row chest, 176 x 222, container y 18..108, player main y 139/157/175, hotbar 197;
   - line 50: the chest's hardness 2.5, 24 states;
   - line 261: chest open/close BLOCKS volume 0.5, pitch 0.9 + 0.1U, attenuation 12 (F 3551-3555).
5. `Docs/Blocks.md` line 75: B1's chest stand-in, a 14/16 box [1,0,1]-[15,14,15] turned by `facing`, with tiles from `entity/chest/normal.png` (`Docs/StandIns.md` rows 22-24). Double-chest halves draw two single boxes until this step.
6. ACTION-PLAN.md Line 1 #5. It also lists the native `Unnamed.Hud.Container.Input` sapling-sprite warning. That warning did not appear in step 9's two final runs (`Native-20261015-120456`: 7 tests with warnings; `Native-20261015-123511`: 2). Grep the native logs for it and say in the plan whether it still shows.

### Starting points (to settle in your plan)

- **Store:** `EFoundationBlockEntityKind::Chest`, 27 slots per half. The menu is `EFoundationMenuKind::Chest` (3 rows, the barrel's layout) and a 6-row `LargeChest` (54 + 36 = 90 slots: container 0-53, main 54-80, hotbar 81-89). The ChestMenu shift-click branch already handles any container size: it keys on container slots with no fuel slot.
- **Open state:** reuse `UpdateBarrelOpen`'s viewer count. Generalise it into a container viewer helper keyed by cell; for a double chest, count both halves. The chest has no `open` block state, so its viewer count drives the lid instead. Play its sound from the centre, once per double chest, with attenuation 12. The `BlockUsed` view path takes `Amount` and `LocationCm` already.
- **Screen:** `FHudContainerModel::GetHeight(Kind)` (222 for the large chest), `HasRecipeButton`, the positions per value-swaps l.47/48. `generic_54` is already imported, and its StandIns row says 18-chest reuses it.
- **Lid model and animation** (R16 curve, R17 layers): this is presentation work on B1's stand-in box. Check with SM-Containers whether the lid model is in this step or split out. It may touch DA_Blocks or the block mesh, which belong to SM-Blocks.
- **Vanilla rules to confirm from the data/wiki:** whether a chest opens with a solid block or a cat above it, the double-chest pairing on placement (the `type` property: B1 placement may already set it; check `FoundationBlockFamilies`), and the trapped chest's redstone (out of scope unless the plan says so).

### Known issues carried

- From step 9: a barrel saved while open, or whose last viewer leaves in an unloaded column, stays `open=true` until it is next used. Spectators never view.
- The furnace ambience scheduling, the creative screen pausing, the focus loss under a survival screen, the missing recipe and fuel items, Codex 18 #2 (multiplayer).

## Token figures (step 9)

- Lead: 142.6k after the reading; 272.1k mid-mutation; 302.1k at report A (123 requests).
- Subagent: the Sonnet reviewer, 120.5k total (the meter found no subagent transcripts, so no peak figure).
- Cost drivers: two RunChecks final runs (pre- and post-review), and the eight-mutant run.

## Evidence (step 9)

- `Tests/Evidence/2026-10-15/Checks-final-20261015-123510/summary.txt`: GREEN, final, after the review fixes.
- `Tests/Evidence/2026-10-15/Inventory-9-compare/` (`barrel-vanilla-vs-ours.png`, `compare.py`, `compare.txt`).
- `C:\dev\sandbox\inv9\` (`mutants.txt`, `mutate.ps1`, `tests-2.log`, `tests-3.log`, `review-diff.patch`, `runchecks-final.log`, `runchecks-final-2.log`).

## Open questions (for the SM)

1. **Push:** track branches stay local; pushing is Jesus's call.
2. **The chest lid:** animate it in 18-chest, or keep B1's static box and split the lid into its own step? Recommended: split it if it needs a DA_Blocks or mesh change, because that work is SM-Blocks'.

## Short prompt for 18-chest

> Survival program, track T1 Containers, step 18-chest: the chest on the 8a store (single and double, 27/54 slots, generic_54). Worktree C:\dev\projects\Unnamed-inv, branch feature/survival-containers. Start with Docs/Records/2026-10-15-Inventory-Handoff-Step18.md section A (reading list: the step 9 record, R16's lid/double-chest rules, R17's chest layers, B1's stand-in box), then the step 9 record. Diff the hot files against feature/survival, send the plan to SM-Containers (session_01EuH6RLbTU1zzFVHK75nWYJ) and wait for the go.

## Short prompt for 8b2b (after 18-chest, carried forward)

> Survival program, track T1 Containers, step 8b2b: the furnace, smoker and blast furnace particles. Worktree C:\dev\projects\Unnamed-inv, branch feature/survival-containers. Start with Docs/Records/2026-10-15-Inventory-Handoff-Step9.md section B (settled design: a pool in FFoundationBreakEffects, M_FurnaceParticle and T_FurnaceParticles, R15's spawn rows, the shared StepAmbienceClock), then the 8b2a record and the latest T1 record. Diff the hot files against feature/survival, send the plan to SM-Containers (session_01EuH6RLbTU1zzFVHK75nWYJ) and wait for the go.
