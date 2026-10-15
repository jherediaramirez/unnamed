# Blocks B3a handoff: doors, trapdoors and fence gates (survival track T5)

Lane `feature/survival-blocks` (worktree `C:\dev\projects\Unnamed-blocks`). B2b is commit `695fef8b` ([its record](2026-10-15-Blocks-B2b.md)); `feature/survival` f673fa9d (T3 16a, T1 8a1 furnace store) is merged on top of it (`524b90da`), and this handoff is the commit after that merge. B3a is the first part of [the blocks plan](2026-10-14-Blocks-Plan.md)'s B3 row: doors (two-half placement, hinge, open both halves), trapdoors (half from the click), fence gates (open, re-face away), with iron doors and trapdoors refusing hand use. Its scout doc is `C:\dev\sandbox\scout-b3\facts.md`. Buttons, levers and their timers are B3b unless SM-Blocks splits it otherwise.

**Manager:** SM-Blocks run 2 (session `session_01DUpWqKA5aBjmdjZtn1enW6`); report there, not to Jesus. B3 waits on **T1 step 7** (`foundation.Blocks.Use` in `UseOn` step 1): ask SM-Blocks whether it has landed on `feature/survival`, and merge only on its go (merge once, rebuild with `-DisableAdaptiveUnity`, `Scripts\RunChecks.ps1 -Profile quick`).

## 1. Reading list (in order)

1. [The B2b record](2026-10-15-Blocks-B2b.md), the whole file (about 45 lines).
2. `C:\dev\sandbox\scout-b3\facts.md` sections 1 (summary), 4.1-4.3 and 4.6 (doors, trapdoors, gates, common rules), 5 (test oracles), 6-7 (design and sketches; they are against 4b826e4, so rebase them on B2b's `FoundationBlockFamilies`), 9 (Codex 12's block-use hook gaps) and 10 (risks, split). Grep the headings; do not read section 8 again (B2b is done).
3. [The blocks plan](2026-10-14-Blocks-Plan.md) section 1, the B3 row and the paragraph under the table.
4. `Docs/Blocks.md` "Shapes and connections (B2b)" and "Placement" (grep the headings).
5. `Source/Unnamed/Foundation/FoundationBlockFamilies.h` (whole, about 60 lines) and `FoundationBlockFamilies.cpp` `GetFamily`, `GetStateForPlacement` and `UpdateNeighbourShapes` (grep them).
6. Values: `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md` "Door hinges", "Fence gates" and any door or trapdoor rows (grep; never open the Appendix H paths), and `C:\dev\tools\shift-managers\survival\value-swaps.md` the T5 rows marked B3.
7. `FoundationAuthority.cpp` `UseOn` step 1 (T1's branch, once merged) and `SettlePlants` (the B2b line at its top).

## 2. Working agreement

- **Plan first** to SM-Blocks: every file against the hot files (`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`); whether hashes, goldens or saved worlds move; the test plan. Wait for the go.
- **Diff before editing** the files other lanes have dirty (B2a's list: `FoundationBlockRegistry.cpp`, `Docs/Blocks.md`, `VoxelMesherTests.cpp`, `FoundationAuthority.cpp`, the Records README, `FoundationInteractionSettings.cpp`). Keep hot-file edits to call lines.
- **Switch:** the plan names `foundation.Blocks.Interact` (under T1's `foundation.Blocks.Use`). Door and trapdoor placement can sit under `foundation.Blocks.Shapes` like B2b's families; ask SM-Blocks if unsure.
- **Checks:** `Scripts\RunChecks.ps1 -Profile quick|final` (`-Flight smoke` for the final); `Scripts\QuietWindow.ps1 -Name <what>` before every heavy launch outside RunChecks.
- **Pre-commit review:** Sonnet 5.5 on the diff only; re-run every suite the fixes touch. Then report A, the SM's approval, the commit, the handoff, report B.
- **Never:** push, use `git stash`, edit CLAUDE.md, or re-record goldens or world hashes. Subagents delete nothing.
- **Measure lead context** with `node C:\dev\sandbox\b0-sounds\MeasureAgentUsage.mjs --session <id>`; find the id by grepping `%USERPROFILE%\.claude\projects\C--dev\*.jsonl` for a phrase you wrote.

## 3. Settled decisions

- Family logic lives in `FoundationBlockFamilies.h/.cpp`; the authority gets call lines only. Add `Door`, `Trapdoor` (and later `Button`, `Lever`) to `EFoundationBlockFamily` from the tags (`Block.Doors`, `Block.Trapdoors`, `Block.Buttons`; check `Config/Tags/MinecraftBlockTags.ini`), keep `GetFamily` the one lookup.
- `IsFaceSturdy` / `GetSturdyFaces` are the sturdy-face helpers for door and button support (collision boxes clipped to the cell; leaves and powder snow none, as the oracle measured).
- Fence gates already place facing the look with `in_wall` (VALUES `blocks.fence_gate_placement`); B3a adds opening, re-facing away from the player, and `in_wall` stays from B2b's `UpdateShape`.
- Hinge and door-half consistency: from `shape_updates.json` if it answers them (it has no door subjects; the scout suggests a P3-style probe, which needs SM-Blocks' go and maybe Jesus's for a vanilla client), else VALUES "Door hinges".
- First person only, no hands: held items move only as items.

## 4. Code map (B2b)

- **Families:** `Source/Unnamed/Foundation/FoundationBlockFamilies.h/.cpp`: `GetFamily`, `GetSturdyFaces`, `IsFaceSturdy`, `UpdateShape`, `SettleShape`, `GetStateForPlacement`, `UpdateNeighbourShapes`; helpers in the anonymous namespace (names must not repeat another `.cpp`'s anonymous helpers: unity builds merge them; B2b's first final run failed on `FaceValue` and `IsHorizontal`).
- **Placement dispatch:** `FoundationBlockPlacement.cpp` `GetStateForPlacement`, the first branch (switch and family, the slab half).
- **Neighbour pass:** `FoundationAuthority.cpp` `SettlePlants`, its first line.
- **Voxel gather:** `VoxelWorld.h/.cpp` `AVoxelWorld::GatherColumns` (static, testable without a world).
- **Tests:** `Tests/FoundationShapeTests.cpp` (`Unnamed.Foundation.Shapes.*`: the oracle sections, placement, neighbours; `FShapeLab` drives UseOn and mining through the authority), `Tests/VoxelShapesGatherTests.cpp` (`Unnamed.Voxel.World.ShapesGather`).

## 5. Known issues (when)

1. **B3a (SM-Blocks: B3a closes it for doors):** the neighbour pass rings only the edited cell; a door's upper half written in `SettlePlants`, and any popped cell, gets no ring of its own (B2b known issue 1). Ring every changed cell, with a test.
2. **B3a (Codex 16 #1, folded in by SM-Blocks):** `Reference/minecraft-blocks-26.3/tools/verify_reference.py` line ~384 runs `set()` on the `"m"` ranges of `shape_updates.json` instead of expanding them as it expands `"r"`. Expand them the same way and add a self-test with a synthetic non-empty `m`. This changes a tool of a hash-checked reference: follow the reference rules (the tool's own rebuild path, the append and regeneration proofs, `SHA256SUMS.txt` and so DA_Blocks' digest move; finish every reference edit before importing). No data change.
3. **Any edit at the streamed world's edge:** rules read unknown cells as air (B2b known issue 2).
4. **Old saved worlds:** family blocks keep their stored states until a neighbour changes (B2b known issue 3).
5. **B3:** uvlock on fence-bar and open-gate undersides and pressed wall buttons (B2a known issue 3; `C:\dev\sandbox\b2-blocks\proof\uvlock_orders.py`).
6. **Any `shape_updates.json` reader:** "more than one pass" means "a second changing pass" (B2a issue 4). **Carpets imported:** `wall_up` gains rows and the verifier fails until its pin moves (B2a issue 5).
7. **B6:** stems' age-0 tint; **B8:** sapling loot tables (B2a issues 6, 7).
8. **Any RunChecks run (flake):** `Unnamed.Voxel.Spawn.Reader` and `.Standing` failed once in B2b's quick run with `FAudioMixerWasapi::PostDeviceSwap - null device swap result!` logged as an error during them; both passed on a re-run (`C:\dev\sandbox\b2b-blocks\logs\spawn-rerun.log`) and in the final. Re-run those two before suspecting code; report it if it repeats.

## 6. What worked, what failed (B2b)

- **Worked:**
  - Testing every oracle row (1.34 million) instead of samples: the first run named every exception (leaves, powder snow, pumpkins, melon) by block, and nothing else.
  - The mismatch tally grouped by subject, direction and neighbour block keeps the log short.
  - Combined mutations in one build, with sources copied aside to `C:\dev\sandbox\b2b-blocks\mutation-backup` (not stash) and touched after the restore.
  - One Sonnet review on a 1,315-line diff (100.2k peak).
- **Failed or slow:**
  - **Unity build:** an anonymous-namespace helper named like another file's (`FaceValue`, `IsHorizontal` in `FoundationBlockPlacement.cpp`) builds fine adaptively (edited files build alone) and fails under `-DisableAdaptiveUnity`, which RunChecks uses. Build once with `-DisableAdaptiveUnity` before the final.
  - **PowerShell:** `R` is the alias of `Invoke-History`, so a helper function named `R` never runs; and `.NET` file APIs resolve relative paths from the process directory, not `Set-Location` (an empty stray file came of it). Use absolute paths.
  - **Line endings:** the repo is LF (`* -text`); write files with LF.

## 7. Token figures (B2b)

- **Lead:** 301.6k at report A, 317.9k after the post-merge final (MeasureAgentUsage, session `e9c83adf-fe78-50ec-9b97-8d26bfe2454b`); it crossed 300k during the wrap-up.
- **Subagents:** pre-commit reviewer (Sonnet 5.5): 100.2k peak.

## 8. Evidence

- `C:\dev\sandbox\b2b-blocks\logs\`: `shapes-1..3.log` (the oracle runs: first failures, then green), `gather-1.log`, `mutation-1.log`, `review-fixes.log`, `spawn-rerun.log`.
- `C:\dev\sandbox\b2b-blocks\review\b2b-diff-1.patch` (the reviewed diff).
- RunChecks: quick `Tests\Evidence\2026-10-15\Checks-quick-20261015-035837\summary.txt` (the audio flake); final before the merge `Tests\Evidence\2026-10-15\Checks-final-20261015-040721\summary.txt`; final after merging f673fa9d (GREEN, native 415, voxel 210, hashes 9 of 9) `Tests\Evidence\2026-10-15\Checks-final-20261015-045113\summary.txt`.

## 9. Open questions

1. **Push of `feature/survival-blocks`:** Jesus's call.
2. **CLAUDE.md's importer commands** use relative `-script=` paths that do not resolve (B1 known issue 7): Jesus's file.
3. **The door hinge source:** VALUES "Door hinges" alone, or a P3-style placement probe with a vanilla client (needs Jesus's go)? Recommendation: VALUES first, and the probe only if a case is missing.

## Short prompt for the next chat

> B3a: doors, trapdoors and fence gates (survival track T5), plus Codex 16 #1 (the `m` ranges in the blocks verifier), lane `C:\dev\projects\Unnamed-blocks`, branch `feature/survival-blocks` at the B3a handoff commit. Start with `Docs/Records/2026-10-15-Blocks-Handoff-B3a.md`: read it whole, then its reading list. SM-Blocks run 2 (`session_01DUpWqKA5aBjmdjZtn1enW6`) manages T5: report there. B3 waits on T1 step 7: ask SM-Blocks whether it has landed and merge `feature/survival` only on its go (merge once, rebuild with `-DisableAdaptiveUnity`, `Scripts\RunChecks.ps1 -Profile quick`). Then report your plan: every file against the hot files, whether any hash, golden or saved-world number moves, and the test plan. Wait for the go.
