# Blocks B2b handoff: placement shapes and the neighbour pass (survival track T5)

Lane `feature/survival-blocks` (worktree `C:\dev\projects\Unnamed-blocks`). B2a is commit `971d1352` on this branch, on top of `feature/survival` 4b826e4: [its record](2026-10-15-Blocks-B2a.md). B2b is the rest of [the blocks plan](2026-10-14-Blocks-Plan.md)'s B2 row: stairs (facing, half, shape), fence, wall, pane and bars connections, gate facing and `in_wall`, placed and kept up to date by the neighbour pass. No reference change is planned.

**Manager:** SM-Blocks run 2 (session `session_01DUpWqKA5aBjmdjZtn1enW6`); report there, not to Jesus. Session `session_01AmANvUc5PUpNmYL3yAvm4F` is retired. SM-Blocks sends "B2a MERGE GO" before `feature/survival` is merged into this lane: until then, do not merge. After that merge: rebuild with `-DisableAdaptiveUnity`, run `Scripts\RunChecks.ps1 -Profile quick`, then plan.

## 1. Reading list (in order)

1. [The B2a record](2026-10-15-Blocks-B2a.md), the whole file (about 60 lines): what exists, the checks, and known issues 1, 2 and 4.
2. [The blocks plan](2026-10-14-Blocks-Plan.md) section 1: the B2 row, the hot-file column, and the paragraph under the table (logic in a new `FoundationBlockFamilies.h/.cpp`, single call lines in the authority).
3. `Docs/Blocks.md` "Shapes and collision (B2a)" and "Placement" (grep the headings).
4. `Reference/minecraft-blocks-26.3/README.md` "Shape updates" (grep the heading), and the `format` key at the top of `facts/shape_updates.json`. Do not read that file whole (2.5 MB): load it in a script.
5. `C:\dev\sandbox\scout-blocks\facts.md` sections 4.1-4.3 (stairs, fences/walls/panes/bars, gates) and 7 (code sketches: `GetConnectedState`, `GetStairShape`, `ConnectsTo`, `UpdateNeighbourShapes`).
6. Values:
   - `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md` "Stair corners" (lines ~366-372; evidence `blocks.stairs`, `blocks.stair_corners`). Grep only; never open the Appendix H paths.
   - `C:\dev\tools\shift-managers\survival\value-swaps.md` "T5 Block families" rows B2 (around lines 118-120).
7. `Source/Unnamed/Foundation/FoundationBlockPlacement.h` lines 15-75 (`ResolveDestination`, `GetStateForPlacement`), and `FoundationAuthority.cpp` `SettlePlants` (line ~1849, the one-ring pass B2b sits beside).

## 2. Working agreement

- **Plan first** to SM-Blocks:
  - every file against the hot files (`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`);
  - whether hashes, goldens or saved worlds move.

  Wait for the go.
- **Diff before editing** (other lanes' dirty trees, SM-Blocks' verify_lane FLAGs on B2a):
  - `Unnamed-use` is dirty on `FoundationBlockRegistry.cpp`, `Docs/Blocks.md` and `VoxelMesherTests.cpp`;
  - `Unnamed-inv`, `Unnamed-mobs` and `Unnamed-use` on `FoundationAuthority.cpp`, the Records README and `FoundationInteractionSettings.cpp`.

  Keep B2b's edits to those files small and local.
- **Switch:** `foundation.Blocks.Shapes` already exists (B2a, default 1). B2b's placement and neighbour pass go under it; 0 stays B1's default-state placement and B1's gather.
- **Checks:**
  - `Scripts\RunChecks.ps1 -Profile quick|final` (`-Flight smoke` for the final).
  - `Scripts\QuietWindow.ps1 -Name <what>` before every heavy launch outside RunChecks.
  - A timed probe needs your own window: `pwsh -NoProfile -File C:\dev\tools\shift-managers\scripts\quiet_window.ps1 -Open Unnamed-blocks -Reason "..."`. Start it with one quoted argument string (Start-Process splits a multi-word reason otherwise). Close it with `-Close -Lane Unnamed-blocks`.
- **Pre-commit review:** Sonnet 5.5 on the diff only; re-run every suite the fixes touch. Then report A, the SM's approval, the commit, the handoff, report B.
- **Never:** push, use `git stash`, edit CLAUDE.md, or re-record goldens or world hashes.
- **Subagents:** no deletes, including their own scratch files.
- **Measure lead context** with `node C:\dev\sandbox\b0-sounds\MeasureAgentUsage.mjs --session <id>`. The id is the transcript (`%USERPROFILE%\.claude\projects\C--dev\<id>.jsonl`) that contains this chat's own words: grep for a phrase you wrote. The newest file is often another lane's (B2a's first figure was wrong that way).

## 3. Settled decisions

- Placement (`GetStateForPlacement` in `FoundationBlockPlacement.cpp`, or the new `FoundationBlockFamilies.cpp` it calls):
  - stairs: facing from the player, half by the slab rule, shape from the neighbours;
  - fences, walls, panes and bars: their connections;
  - gates: facing, and `in_wall`.
- The neighbour pass `UpdateNeighbourShapes`, one ring, no cascade:
  - recomputes the 4 horizontal neighbours, plus the cell below for walls (their `up` and tall sides read the block above);
  - B2a's plan put one call line in `SettlePlants`, which every edit path already goes through (`UseOn`, `BreakBlock`, buckets, fluids).
- The oracle is `facts/shape_updates.json`. Tests compare placement and neighbour results against it: sampled rows for every family, all 32 stair corner cases, the wall up/tall rows and the gate rows.
- The family comes from the block tags (`minecraft:stairs`, `fences`, `walls`, `fence_gates`) plus the pane/bars property set, so no new asset enum and no DA_Blocks change. The oracle's `tag_mismatches` is empty: tags and classes agree for every imported block.
- Uvlock stays as is (Blocks.md known gaps): B3 checks the pressed buttons.
- B3 (interactive blocks) starts from B2b's commit. Its scout doc is `C:\dev\sandbox\scout-b3\facts.md`.

## 4. Code map (B2a)

- **Switch:** `FoundationBlockRegistry.cpp` (`CVarBlocksShapes`, `FoundationBlockFamilies::AreShapesEnabled`); `FoundationBlockRegistry.h` (the families namespace, `bDropsPlacingItem`).
- **Gather:**
  - `FoundationBlocks.cpp` `FFoundationBlockWorld::GatherCollision` and `FoundationCollisionGather::KeepLeaving` (declared in `FoundationBlocks.h`);
  - `VoxelWorld.cpp` `AVoxelWorld::GatherCollision`: the outer layers after the main Z loop, plus the early-return guard.
- **Self-drop:**
  - `FFoundationBlockRegistry::Build(..., Items)`, where `GetDefault` passes DA_Items;
  - `UFoundationInteractionSettings::AddBlockItems` skips `bDropsPlacingItem` blocks;
  - `FFoundationAuthority::BreakBlock`'s fallback (`DropItem`) does the drop.
- **Panes:** `VoxelBlocks.cpp` (`Info.Layer` for model states); `VoxelMesher.cpp` `Model()`.
- **Reference:**
  - `build_reference.py`: `APPENDED_BATCH_STARTS`, `FROZEN_MODELS`, `model_digest`;
  - `FactsDump.java` `dumpShapeUpdates`: `settle`, `ranges`, `groups`;
  - `verify_reference.py`: the shape checks and the frozen prefix.
- **Tests:**
  - `FoundationPlantTests.cpp` (`Blocks.ShapesGather`, the PlantShapes column check);
  - `FoundationFamilyTests.cpp` (`Families.Offhand`);
  - `FoundationMiningTests.cpp` (`Mining.SaplingDrop`);
  - `VoxelMesherTests.cpp` (the pane check in `ModelData`).

## 5. Known issues (when)

1. **Now, B2b:** nothing connects; stairs are straight; family blocks place their default state.
2. **B2b (recommended):** the voxel world's outer-layer gather (`AVoxelWorld::GatherCollision`) has no native test of its own. It shares `KeepLeaving` with the tested block-world path; the flight and probe run it. Add a test with a generated column holding a fence and a pitcher crop: switch 0 against 1, regions above and below, as in `Blocks.ShapesGather`.
3. **Any reader of `shape_updates.json`:** the `format` string says a case "needed more than one pass". It means "needed a second changing pass" (`m`, `one_pass`). Changing the wording changes the file and needs a reference rebuild; leave it unless B2b rebuilds the reference anyway.
4. **When carpets are imported:** `wall_up` gains rows (FactsDump adds `white_carpet` above walls only if imported). The verifier pins 32 rows and will fail until moved.
5. **B3:** uvlock on fence-bar and open-gate undersides and pressed wall buttons (measured, `C:\dev\sandbox\b2-blocks\proof\uvlock_orders.py`).
6. **B6:** stems draw their age-0 tint at every age (B1 known issue 2, Codex 15 #5). Add a per-age tint test.
7. **B8:** saplings drop through the registry's self-drop; their loot tables (`loot_later.txt`) replace it.
8. **Any verifier run without the jar:** the blocks verifier skips the stand-in re-derivation and re-bake silently (B1).

## 6. What worked, what failed (B2a)

- **Worked:**
  - Two subagents for the reference: the dump table, then the rebuild with proofs. Each had exact files, a no-delete rule and a 150k stop; both finished under it (100.7k, 125.3k).
  - Test-first on Codex #4 (it failed for all 9 saplings before the fix).
  - Combined mutations in one build.
  - The T3D proof for the DA_Blocks re-import (`C:\dev\sandbox\b1-blocks\export_t3d.py`, `b1-blocks\toolfix\scripts\t3d_cmp.py`).
- **Failed or slow:**
  - **ExecCmds:** `-ExecCmds` separates commands with commas, not semicolons. A `;` hung the editor waiting for tests that never queued.
  - **Output folder:** `TestFoundationRuntime.ps1 -OutputDirectory` must not exist (keep logs outside it).
  - **clang-format:** run on whole files, it reformatted an unrelated line (revert it, or format only your hunks).
  - **Re-sums:** each README or tool edit after the import changes `SHA256SUMS.txt` and so DA_Blocks' digest. Finish every reference edit, including review fixes, before importing (B2a imported twice).
  - **Registry DropItem:** setting `DropItem` in the registry also made `AddBlockItems` create a duplicate block item. That is why `bDropsPlacingItem` exists.
- **Rebuild inputs:** the research folder for the blocks reference is `C:\dev\sandbox\b1-blocks\research`; the facts input is `C:\dev\sandbox\b2-blocks\ref\buildin\facts`. `mc-26.3-blocks` is stale.

## 7. Token figures (B2a)

- **Lead:** about 380k at this handoff. It crossed 300k at the reference rebuild; SM-Blocks approved the wrap-up here.
- **Subagents:**
  - FactsDump oracle (Opus 5.5): 100.7k;
  - reference rebuild (Opus 5.5): 125.3k;
  - code reviewer (Sonnet 5.5): 100.0k;
  - tools reviewer (Sonnet 5.5): 88.6k.

## 8. Evidence

- `C:\dev\sandbox\b2-blocks\`, all from B2a:
  - `ref\`: the append, regen, verify and mutation outputs;
  - `facts-dump\`: the dump, `proof.txt`, `spot.txt`;
  - `import\`: the T3D proofs, the import logs, `assets-pre-b2a\`, `assets-b2a-first-import\`;
  - `logs\`: the native runs, mutations and bench;
  - `probe\`: Shapes 0 against 1;
  - `review\`: the diffs the reviewers read;
  - `accepted-pre-b2a\`: the moved reference.
- RunChecks final: `Tests\Evidence\2026-10-15\Checks-final-20261015-024454\summary.txt`.

## 9. Open questions

1. **Push of `feature/survival-blocks`:** Jesus's call.
2. **CLAUDE.md's importer commands** use relative `-script=` paths that do not resolve (B1 known issue 7): fix the doc? That is Jesus's file.

## Short prompt for the next chat

> B2b: placement shapes and the neighbour pass (survival track T5), lane `C:\dev\projects\Unnamed-blocks`, branch `feature/survival-blocks` at the B2a handoff commit. Start with `Docs/Records/2026-10-15-Blocks-Handoff-B2b.md`: read it whole, then its reading list. SM-Blocks run 2 (`session_01DUpWqKA5aBjmdjZtn1enW6`) manages T5: report there. Merge `feature/survival` only after SM-Blocks' "B2a MERGE GO" (if it has come: merge once, rebuild with `-DisableAdaptiveUnity`, `Scripts\RunChecks.ps1 -Profile quick`). Then report your plan: every file against the hot files and the dirty trees in section 2, whether any hash, golden or saved-world number moves, and the test plan against `facts/shape_updates.json`. Wait for the go.
