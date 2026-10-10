# Water and sky handoff: W2a done, into W2b (waterlogging, washed away, fluid tags)

October 10, 2026. W2a (fluid wiring and streaming) is done on `feature/water-sky`; its record is [2026-10-10-WaterSky-W2a.md](2026-10-10-WaterSky-W2a.md). Fluids now flow in voxel worlds behind `voxel.Fluids` (default 0). **W2b** is next in the lane's water order (plan section 4), unless the manager picks DN-P or W3 first. W2b's plan entry says it waits for the breaking lane's authority changes to merge: ask the manager whether they have.

**The pending merge (if W2a's session didn't do it):** the manager plans one merge of `feature/voxel-world` into `feature/water-sky` after W2a, on its "merge now". Steps:
1. Start from a clean tree with W2a's commits in, then `git merge feature/voxel-world` (never rebase, never force).
2. Expected conflicts:
   - `Scripts/TestVoxel.ps1`'s `$expected`: main's count plus this lane's 9 (W2a's `Unnamed.Voxel.Fluid.World.*` and `Fluid.Parking`);
   - `Docs/Records/README.md`: keep every entry from both sides.
3. Build with `-DisableAdaptiveUnity` (or touch the merged sources).
4. Run every suite once: the gate chain `C:\dev\sandbox\water-w2a\gate.ps1 -Label m1`. Its expected hashes (line 53) are the pre-merge values: take the new tree hashes from main's record and update `` first. Also check the digest and `Unnamed.Voxel.Fluid` 19 of 19.
5. Send the manager one combined report; stop on red.

## Reading list (W2b)

1. This file.
2. [The W2a record](2026-10-10-WaterSky-W2a.md): all of it (about 150 lines).
3. [The plan](2026-10-10-WaterSky-Plan.md): section 4 ("Against the work in flight", "Lane rules"), section 5 W2b and W3 (W3's switch flip, below), and section 6 Q7 and Q11.
4. [Scout M's facts](2026-10-10-WaterSky-Scout-M-Fluids.md): section 3 (the data inventory: where `washed_away_by_fluids` and the fluid tags live), 4.4 (walls, plants, waterlogging) and section 5's oracles `w_slab`, `w_slab_wall`, `w_waterlogged_bottom`.
5. [The W1 record](2026-10-10-WaterSky-W1.md): "Known gaps" only.
6. `Source/Unnamed/Voxel/VoxelFluidSystem.h` (whole, ~165 lines), and in `VoxelFluidSystem.cpp` `MakeCell` (the state-to-cell table W2b changes) and `GetState`.
7. `Source/UnnamedVoxel/Fluids/VoxelFluidRules.h`: `FVoxelFluidCell`'s comments (lines 25-68) and `CanEnter`/`CanHold` (about 185-212).
8. `Scripts/Editor/BlockImportData.py` 210-224 (why waterlogged slabs read dry) and the blocks reference's `tools/` README (how a reference is rebuilt: only through its tools, after moving the accepted folder away).
9. Not to read: `Reference/minecraft-blocks-26.3/research/water_research.md` and `Docs/MinecraftBlockReference.md` section 4 (bytecode-derived, banned: plan Q18).

## Working agreement

- The lane rules (plan section 4): never re-record goldens or hashes; one editor build at a time; timed gates in the manager's quiet window; commits stay on `feature/water-sky` and the manager merges; no `CLAUDE.md` edits.
- Behaviour comes from scout M's probes or the wiki, never from code. A case no probe covers is a known gap.
- `Unnamed.Voxel.Fluid.*` (19 tests) stays green. A rule change that moves an oracle needs a new probe.
- Subagent briefs: one deliverable, sized well under 100k. Both of W2a's Opus subagents passed the 150k stop (191k, 250k) while each estimated about half of that; their own estimates can't be trusted, so cut the brief instead.

## Settled decisions

- Everything listed in the W2a handoff, plus:
- **`voxel.Fluids` defaults to 0** until partial-height fluid meshing lands (the manager, October 10). **W3**, or whichever session lands the meshing, flips it to 1 with a capture pair; until then tests and evidence turn it on explicitly, and 0 is the control.
- W2a's calls, approved by the manager:
  - the fluid tick is its own counter, equal to the game tick while it runs, paused under `voxel.Fluids 0` (edits are ignored then), and behind under the budget (it catches up; the same world, later);
  - parked acts keep their remaining delay (a column leaving the ring, or an act reading a column that isn't final) and re-queue together in their first order;
  - the W2a stub: models and snow layers that hold no fluid stop fluid like walls. **W2b replaces this with the washed-away tag** (those pop, Q7: no drop) and the vanilla list of blocks fluid can't enter;
  - fluid writes don't run the authority's plant settling (W2b, with the pops).
- Q11 (a): the tags go into the blocks reference through its tools. Q7 (a): pops with no drop.

## Code map

| Piece | Where | Notes for W2b |
|---|---|---|
| State to rules cell | `VoxelFluidSystem.cpp` `MakeCell` | Set `bWashedAway` from the new table flag; drop the wall stub for washed-away blocks; waterlogged slabs need `Fluid`/`Level` from the table (open question 2 of the W1 handoff). |
| Rules cell to state | `VoxelFluidSystem.cpp` `GetState` | Writes are fluid levels, the four reaction blocks, or air. A washed-away pop is a write of fluid into the plant's cell: the system must raise `Popped` for it. |
| Wake-up hook | `FFoundationBlockWorld::OnSet` (`Foundation/FoundationBlocks.h/.cpp`) | Called after every committed `Set`/`SetMany`; the fluid system ignores its own flushes (`bFlushing`). |
| Batched writes | `FFoundationBlockWorld::SetMany` -> `FVoxelBlockStorage::SetMany` -> `FVoxelColumnPipeline::SetCells` | Pops from fluid writes go here too; the authority's `Popped` event lives in `FoundationAuthority`. |
| Scheduler | `Fluids/VoxelFluidScheduler.h/.cpp` | Park/Unpark per column with the remaining delay. |
| Tests | `Tests/VoxelFluidWorldTests.cpp` (`Unnamed.Voxel.Fluid.World.*`) | `FFluidWorldRun` runs the system on a flat pipeline against W1's oracle world: reuse it for `w_slab`, `w_slab_wall`, `w_waterlogged_bottom` in game. |
| Evidence | `Voxel/VoxelFluidShots.h/.cpp`, `CaptureVoxel.ps1 -Fluids breach\|flood` | Breach and flood captures; add a washed-away shot if useful. |

## Known issues

| Issue | When |
|---|---|
| Models and layers holding no fluid are walls (the W2a stub). | W2b |
| Fluid writes don't settle plants (a lily pad over drained water stays). | W2b |
| Waterlogged non-model blocks (slabs) read dry in the table. | W2b |
| Flowing water meshes as stepped boxes; `voxel.Fluids` stays 0 until it doesn't. | W3 |
| Drained cells stay as edits (air over air): the edit list only grows with the cells fluid ever touched. | phase 10 (saving) may prune edits equal to the generated cell |
| An act waiting on a column that never comes back stays parked (vanilla keeps such ticks with the chunk). | by design; count shown in `voxel.Fluids.Info` |
| A frame can pass the 0.5 ms budget by about one act (9 to 18 frames in the final flood runs, worst 0.61 ms). | if it matters, W5's flood work |
| The act costs about 16 µs in game, writes included (Flood-6: 55 ms over 3,423 acts): the drop search and the per-cell reads dominate. | W5, if floods get big |
| `Apply`/`Wake`/`React` recurse once per lava conversion (W1's issue). | W5's edge scan |
| Generated edges don't wake. | W5 |

## What worked and what failed

- **Worked:**
  - Testing the game system against W1's oracle world (`FVoxelFluidTestWorld`) cell for cell at every tick: the wiring (hook, overlay, flush, `SetCells`) was right the first time it ran.
  - Manual pipeline jobs (`bManualJobs` with `GetJobColumn`/`IsJobPending`) to hold one column back and pin the wait on it.
  - Three diff-only Sonnet reviewers split by path: no high findings, and the medium ones were real (budget estimate decay, settings read at `Start`, unparked acts entering an open tick).
- **Failed, then fixed:**
  - Unparking column by column broke FIFO across columns for acts due in one tick (the tests agent spotted it; now pinned).
  - The flood capture first missed every frame where the fluid acted: writes remesh columns, so the view wasn't "ready" on those frames.
  - The budget first ignored the flush (frames to 0.71 ms); a running mean then lagged costly acts; a per-frame decay forgot the cost between waves. The fix that held: the costliest act and flushed cell of recent frames that acted, falling a tenth each.
  - The first flood (3 x 3 sources) wet only 531-622 cells on the terraced desert near the spawn; 5 x 5 sources 10 apart wet 1,554.
- **Process:** a run chain that throws stops every later step (the e2e chain stopped at a flaky probe); run steps separately when one can flake.

## Evidence

- `Tests/Evidence/2026-10-10/WaterSky-W2a-e3-breach`, `-q1-flood` (the quiet window's trace), `-q1-Flight` and `-q2-Flight` (the certification), `-g2-*` (the gate after the last code change).
- `C:\dev\sandbox\water-w2a\`: `gate.ps1` and `e2e.ps1` (the lane's gate and end-to-end chains, with the CEF slot), `measure.ps1` (MeasureAgentUsage for a session started in `C:\dev`), and the summaries.

## Open questions (with options)

1. **Waterlogged slabs in the table** (from W1): (a) the block import fills `state_water` for waterlogged non-model states, which changes the generated `DA_Blocks`, so verify the goldens; (b) `MakeTable` reads the `waterlogged` property. Recommended: (a), W2b's call with a golden check.
2. **Which blocks fluid can't enter** beyond blocks that block motion (vanilla's door, sign, ladder, sugar cane, bubble column, portal list): (a) a reference tag if the data has one; (b) a short list in the table built from the reference's block ids. Recommended: (a) when the data has it; else (b), recorded as a known divergence.

## Tokens

- W2a's lead reached about 470k: about 200k for the reading list and the wiring code, the rest for subagent results, review fixes, three budget rounds, the gates and the records. Subagents: Opus 191k and 250k (both over the stop), Sonnet reviewers 66-98k.
- Expect about 120k for W2b's reading list.

## Short prompt (W2b)

> Water and sky track, session W2b (a parallel lane): waterlogging, washed away, fluid tags. Work in `C:\dev\projects\Unnamed-water` on `feature/water-sky`; merge `feature/voxel-world` only on the manager's word (never rebase). Never touch other worktrees, never push, and make no `CLAUDE.md` edits. Read `Docs/Records/2026-10-10-WaterSky-Handoff-W2b.md` and follow its reading list. Build W2b as in the plan's section 5: the washed-away tag and fluid tags through the blocks reference's tools (Q11), pops with no drop (Q7), waterlogged sources, and breaking one leaves a source. Keep `Unnamed.Voxel.Fluid.*` green, with goldens and hashes identical. Send the manager a short plan before building.
