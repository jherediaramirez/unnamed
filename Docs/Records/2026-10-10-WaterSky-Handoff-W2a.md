# Water and sky handoff: W1 done, into W2a (fluid wiring)

October 10, 2026. W1 (the fluid core) is done on `feature/water-sky`; its record is [2026-10-10-WaterSky-W1.md](2026-10-10-WaterSky-W1.md). The lane's order (plan section 4) runs **DN1 next**. Its scope and reading list are in the plan (section 5, DN1), so this handoff doesn't repeat them. **W2a** follows after T3 merges and DN1's clock exists. This handoff carries W1's results and what W2a needs.

**Before the lane's next session builds:**
- `feature/voxel-world` is now `c90be1f` (trees and the torch); the branch is still on `41190e9`.
- The next session merges it once first (merge, never rebase; the lane rules), then builds with touched sources or `-DisableAdaptiveUnity`.
- Expected conflicts:
  - `Scripts/TestVoxel.ps1`'s `$expected`: take main's count plus W1's 10;
  - `Docs/Records/README.md`: keep every entry from both sides.

## Reading list (W2a)

1. This file.
2. [The W1 record](2026-10-10-WaterSky-W1.md): all of it (about 90 lines).
3. [The plan](2026-10-10-WaterSky-Plan.md): section 1, section 4 ("Against the work in flight", "Lane rules"), section 5 W2a and W2b, and the decided questions in section 6 (Q1, Q4, Q6, Q10, Q13).
4. [Scout M's facts](2026-10-10-WaterSky-Scout-M-Fluids.md): section 6, "The game side" and "Switches", and section 2, items 9-15 (the code it names for wiring). Re-check those line numbers on your tip.
5. `Source/UnnamedVoxel/Fluids/VoxelFluidRules.h`. Read the comments of `FVoxelFluidCell`, `Act`, `React`, `Apply`, `Wake` and `RunTick`; the bodies only where you change them.
6. `Source/UnnamedVoxel/Fluids/VoxelFluidTestWorld.cpp` (29 lines): the pattern W2a's world adapter follows.
7. Not to read: `Reference/minecraft-blocks-26.3/research/water_research.md` and `Docs/MinecraftBlockReference.md` section 4. Both are bytecode-derived and banned (plan Q18).

## Working agreement

- Same as the plan's lane rules (section 4):
  - Never re-record goldens or hashes.
  - One editor build at a time.
  - Timed gates go in the manager's quiet window.
  - Commits stay on `feature/water-sky`, and the manager merges.
  - No `CLAUDE.md` edits.
- Behaviour comes from scout M's probes or the wiki, never from code. A case no probe covers is a known gap, and is never guessed from code (the manager, on W1's question 3).
- `Unnamed.Voxel.Fluid.*` must stay green. A rule change that moves an oracle needs a new probe, not a re-recorded oracle.

## Settled decisions (from the plan, and W1)

- Q1 (a): one 20 Hz world clock (DN1).
- Q4 (b): generated edges wake near the player (W5).
- Q6 (a): the simulation ring is the view minus one.
- Q9 (a): transients are reported, not pinned. W1's drain exemption covers up to 10 ticks.
- Q10 (a): the fluid budget is its own 0.5 ms.
- Q13 (a): no fluids in the Playground.
- **Jesus's answers** (October 10, recorded in the plan's section 6):
  - Q2 (a): world.json's optional `clocks` key now, at version 1 (DN1);
  - Q3 (a): a new hash-checked `Reference/minecraft-sky-26.3/` and its two `CLAUDE.md` lines, which he approved, so DN0 is unblocked;
  - Q14: sky family B, hybrid (the mock-up and DN2);
  - Q15 (a): SP1 when he's home (a screen hold, with his monitor on);
  - Q18: both documents stay, with the warning line W1 added.
- W1:
  - the core keeps the wiki's neighbour update order (west, east, down, up, north, south);
  - a spreading cell writes its sides in face order (west, east, north, south);
  - targets only enter empty cells. Fluid cells re-derive themselves when woken.

## Code map

| Piece | Where | Notes for W2a |
|---|---|---|
| State fields | `Blocks/VoxelBlockTable.h` (`EVoxelFluid`, `Fluid`, `FluidLevel`, `IsFluidSource()`) | Filled by `VoxelBlocks::MakeTable`. The struct stays 68 bytes. |
| Rules | `Fluids/VoxelFluidRules.h` | Templates over a reader or world with `Get` and `Set`. `Act` returns false on an unknown cell: park the act per column. Writes are compare-and-set. |
| Scheduler | `Fluids/VoxelFluidScheduler.h/.cpp` | Due tick plus FIFO, one act per cell and fluid. W2a adds parking per column and the remaining delay. |
| Test world | `Fluids/VoxelFluidTestWorld.h/.cpp` | The adapter pattern: `Get`, `Set`, `Place` (an edit), `Step`. |
| Tests | `Source/Unnamed/Tests/VoxelFluidTests.cpp` | Oracle data: `VoxelFluidOracles.json`, from `Scripts/MakeFluidOracles.py` (never edit the JSON by hand). |

**W2a's adapter:**
- Map each block state to `FVoxelFluidCell`:
  - `Fluid` and `FluidLevel` from the table;
  - `bSolid` from the table;
  - `Walls`: a full block 0x3F, a bottom slab Down, a top slab Up, others 0;
  - `bUnknown` for columns that aren't known.
- Map a write's `After` back to a state: a water or lava level, an `EVoxelFluidBlock` by name, or air.
- Then run `VoxelFluidRules::RunTick` with an overlay world, and batch the writes per column.

## Known issues

| Issue | When |
|---|---|
| Waterlogged non-model blocks (slabs) read as dry in the game table: `BlockImportData.py` 210-224 fills a state's water for model blocks only. The rules handle them; the table doesn't. | W2b |
| `Apply`, `Wake` and `React` recurse once per lava conversion. A long generated lava-water boundary could chain deeply on a worker thread. Make it a work list if W5's edge scan finds such chains (keep the same order). | W2a or W5 |
| The drain checkerboard differs by one wave in one quadrant of `w_drain` (5 cells, reported). | open question 1 |
| Cases no probe covers: flowing water over a still source; a dry slab between sources; sources around a cell over a drop; slab halves side by side; water going down into lava (unreachable). | a later probe, if it matters |
| Nether lava (`fast_lava`) isn't modelled. | when a Nether exists |
| Test-world cost is 10-18 µs per act, because it reads through a `TMap`. The game's array reads should be faster. | W2a measures |

## What worked and what failed

- **Worked:**
  - Replaying the probe's own commands and checkpoints, and counting "last change" exactly as `make_oracles.py` does. 50 of 51 oracles matched on the first run.
  - Automating the mutation check in a test: no rebuild per mutation, and each one's failure is logged.
- **Failed:**
  - A first mutation (flowing water waterlogs slabs) never fired, because slabs weren't passable in the drop search. The automated check caught it.
  - A trial knob declared `inline` in a core header had one copy per DLL, so the test's setting never reached the core. Export trial knobs from the core module, or put them in options.
  - Order-trial results that "don't change anything" are suspect until the knob is proven to reach the code.
- **Useful:** a 100-line Python model of the rules (`C:\dev\sandbox\water-w1\drainsim.py`, `ordersearch.py`) searched 3,456 orders in 15 s.

## Open questions (with options)

1. **The same-tick order behind vanilla's drain checkerboard.**
   - Options:
     - (a) keep the wiki's order and report the transient (as now);
     - (b) probe it: a drain whose first wake isn't symmetric, or `/tick step 1` snapshots of one drain wave;
     - (c) adopt a fitted perpendicular order (16 of 24 replay `w_drain`) with no source behind it.
   - Recommended: (a) until a probe session has a slot, then (b).
2. **Waterlogged slabs in the table** (W2b).
   - Options:
     - (a) the import fills `state_water` for waterlogged non-model states. This changes the generated `DA_Blocks`, so verify the goldens;
     - (b) `MakeTable` reads the `waterlogged` property.
   - Recommended: (a), the registry stays the one source. It is W2b's call, with a golden check.

## Evidence

- `Tests/Evidence/2026-10-10/WaterSky-W1-*` (on disk only): the gate's TestVoxel, hashes and native reports.
- `C:\dev\sandbox\water-w1\`: the gate summaries, the test runs and the order search.

## Tokens

- **W1's lead peaked at about 460k** (measured with `Scripts/MeasureAgentUsage.mjs`):
  - about 200k went on the system prompt, the plan, scout M's facts and the probe scripts;
  - the rest on code, two oracle-fix rounds, the drain-order search and the gates.
- **Subagents:** the two Sonnet 5.5 pre-commit reviewers, the diff split by path at 1,592 lines, peaked at 72k and 74k.
- **For W2a:** expect about 150k for the reading list.

## Short prompt (W2a)

> Water and sky track, session W2a (a parallel lane): fluid wiring and streaming. Work in `C:\dev\projects\Unnamed-water` on `feature/water-sky`, after T3 has merged and DN1's world clock exists (rebase or merge as the manager says). Never touch other worktrees, never push, and make no `CLAUDE.md` edits. Read `Docs/Records/2026-10-10-WaterSky-Handoff-W2a.md` and follow its reading list. Build W2a as in the plan's section 5: the system on `AVoxelWorld`, the `Set` hook, stamped and batched writes, the simulation ring with parking, the stale-mesh fix, the 0.5 ms budget, the game rules and the switches. Keep `Unnamed.Voxel.Fluid.*` green, with goldens and hashes identical. Send the manager a short plan before building.
