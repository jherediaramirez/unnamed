# Handoff: 8b2b, the furnace particles (track T1 Containers)

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. The step before, 18-chest, is done: see [its record](2026-10-15-Inventory-18-chest.md). The planner's approved plan for this step is `C:\dev\tools\shift-managers\survival\tracks\T1-plan-8b2b.md`. Particles only: bug fixes are deferred to the QC pass (known bugs and new Codex findings go on SM-Codex's QC-KNOWN-BUGS.md list).

## Working agreement

As in steps 9 and 18-chest:
- **Plan first:** every file, diffed against the latest feature/survival (the hot files: `FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`). Put any DA_Blocks, DA_Sounds, DA_Items or reference need at the top: those are lock-held through the PM. Send only what differs from the plan file; wait for the go.
- **Patching:** small Python patch scripts with `C:\dev\sandbox\inv9\patchlib.py` (exact-once replacement, LF kept). Write the script to a file with the Write tool: a bash heredoc holding C++ broke once in 18-chest (an unmatched quote error; nothing applied). Every source file here is LF.
- **Mutation checks:** copy `C:\dev\sandbox\inv18\mutate.ps1` (a table of file, old text, new text, test; it builds, runs, restores and rebuilds). Use exact post-clang-format text. Don't edit sources while it runs (docs are fine).
- **Review:** a Sonnet 5.5 pre-commit review on the diff only (`git add -N` new files first). Over about 1,500 diff lines, split it by path list across two reviewers (18-chest: core Foundation files / tests, HUD, tour, docs).
- **Final checks:** `Scripts\RunChecks.ps1 -Profile final -Flight smoke` after the last code change (about 25-30 min). Don't edit sources while it runs: its later steps rebuild, and its time chain flags sources newer than the DLL.
- **Context:** `node Scripts/MeasureAgentUsage.mjs` after copying this session's `.jsonl` (by its session id, not the newest file: another session's file can be newer) into `C:\dev\sandbox\inv5b\measure-cfg\projects\C--dev-projects-Unnamed\` with `CLAUDE_CONFIG_DIR=C:\dev\sandbox\inv5b\measure-cfg`.
- **Test labs:** set `Inventory.bFull` yourself. A lab that places through `Use` must set `Command.DestinationRevision` from the destination cell, or a cell the lab wrote is refused as `StaleRevision` (`FChestLab::Use` does; `FBarrelLab::Use` doesn't need to).
- **Commit messages:** `git commit -F <file>`.

## Reading list

1. `C:\dev\tools\shift-managers\survival\tracks\T1-plan-8b2b.md`, all of it (48 lines): the settled design and files.
2. `Docs/Records/2026-10-15-Inventory-Handoff-Step9.md` section B: the particle design (a pool in `FFoundationBreakEffects`, `M_FurnaceParticle` and `T_FurnaceParticles`, R15's spawn rows, the shared `StepAmbienceClock`).
3. `Docs/Records/2026-10-15-Inventory-8b2a.md` ("The ambience clock") and `Docs/Items.md` "The furnace, smoker and blast furnace".
4. `C:\dev\projects\value-holder\SURVIVAL-RUN2-FINDINGS-26.3.md`, grep only: R15 (`survival2.r15.*`: the front = centre + facing x 0.52, the lifetimes, the rates).
5. `Docs/Records/2026-10-15-Inventory-18-chest.md`, "Known issues", for what is still open on the lane.

## Settled decisions (carried)

- Feature freeze (Jesus, via PM 4, 2026-10-15; the program manager is now PM 5, session_01T56ySS741nM1kpyxmvShGm; report to SM-Containers): line 1 ends with 18-chest and 8b2b, then a full QC on a final build. Nothing beyond the plan; bug fixes are deferred to the QC pass (QC-KNOWN-BUGS.md). The chest lid is deferred until after QC.
- The particles are presentation only (the view), driven by the lit state the authority already writes; no authority change.
- The ambience clock is shared (`FoundationFurnace::StepAmbienceClock`); paused, nothing spawns.
- No saved data, golden or world hash change.

## Code map (from 18-chest, for orientation)

- `Foundation/FoundationChests.h/.cpp`: chest pairing (`GetPartner`, `GetHalves`, `GetStateForPlacement`, `UpdateShape`, `IsBlockedAbove`), `foundation.Chest`.
- `FoundationAuthority.cpp`: `UseChest`, `GetMenuCells`, `UpdateContainerOpen` (barrel and chest), `UpdateViewers`, `UpdateChestOpen`.
- `FoundationContainer.h`: `IsChestMenu`, `HasViewers`, `UsesBlockEntity`; `EFoundationMenuKind::Chest`, `LargeChest`.
- `Hud/HudContainer.h/.cpp`, `HudContainerScreen.cpp`: the generic_54 screens (3 and 6 rows).

## Known issues (with "when")

- The chest lid (both chests draw B1's stand-in box; a double chest two boxes). When: deferred until after QC (feature freeze: line 1 ends with 18-chest and 8b2b, then a full QC on a final build); then T5 B4's render-shape change and R16's lid curve, R17's model layers.
- A double chest's menu checks reach to the opened half only (vanilla checks both). When: a container pass, if a measurement shows it matters.
- Vanilla's 5-tick opener recheck (barrel and chest); spectators never view; the ender chest; cats on chests; the trapped chest's redstone; the chest both-sides tie (provisional). When: their own steps.
- Carried: the furnace ambience scheduling, the creative screen pausing, the focus loss under a survival screen, the missing recipe and fuel items, Codex 18 #2 (multiplayer).

## What worked and what failed (18-chest)

- Worked: copying step 9's lab and mutation script; splitting the review in two by path list; testing pairing both as a pure function and end to end through the place command.
- Failed once: the planned hooks at the top of `FoundationBlockFamilies::GetStateForPlacement` and `::UpdateShape` are not reached for a block with no family; two more one-liners (`UsesFamilyPlacement`, the neighbour ring's family filter) were needed. Check the call path before trusting a hook site.
- Failed once: RunChecks' voxel suite crashed in `operator new` under parallel worldgen JSON parsing (`VoxelAutomation-20261015-145649`), unrelated code; the next run is the evidence.

## Token figures

See the 18-chest record.

## Open questions (for the SM)

1. **Push:** track branches stay local; pushing is Jesus's call.

## Short prompt for 8b2b

> Survival program, track T1 Containers, step 8b2b: the furnace, smoker and blast furnace particles. Worktree C:\dev\projects\Unnamed-inv, branch feature/survival-containers. Start with the approved plan C:\dev\tools\shift-managers\survival\tracks\T1-plan-8b2b.md, then Docs/Records/2026-10-15-Inventory-Handoff-Step8b2b.md (working agreement, reading list), then the 18-chest record. Diff the hot files against feature/survival, send a SHORT final plan (only what differs from the plan file; particles only) to SM-Containers (session_01EuH6RLbTU1zzFVHK75nWYJ) and wait for the go.
