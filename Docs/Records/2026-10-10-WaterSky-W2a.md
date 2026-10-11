# Water and sky W2a: fluid wiring and streaming

October 10, 2026, water and sky track, session W2a (lane `feature/water-sky`, worktree `C:\dev\projects\Unnamed-water`, on `b802ad3`). It runs W1's fluid rules in voxel worlds: edits wake water and lava, the flow streams with the columns, and its writes are stamped, batched edits (plan section 5, W2a). **It ships behind `voxel.Fluids`, default 0**: the manager changed the plan's call, because without W3's meshing a spread draws as full blocks. W3, or whichever session lands partial-height meshing, flips the default. Generated worlds are unchanged.

## What changed

**`FVoxelFluidSystem`** (`Voxel/VoxelFluidSystem.h/.cpp`, new), owned by `AVoxelWorld` (built in `SetupAuthority`, reset in `Regenerate`, stopped in `EndPlay`):
- **Wake-up:** `FFoundationBlockWorld::OnSet`, a new hook called after every change `Set` and `SetMany` commit. An edit or a plant pop wakes the cell and its six neighbours through `VoxelFluidRules::WakeAround`, so placed lava meets water at once. The Playground installs no hook and stays still (Q13).
- **The rules' world:** a table maps every state to an `FVoxelFluidCell`:
  - fluid and level from the block table;
  - full cubes are walls on all faces;
  - slabs wall their half and are waterloggable (never entered);
  - kelp and other water-holding models are water sources;
  - **the W2a stub:** models and snow layers holding no fluid are walls, so fluid never removes a block unseen until W2b's washed-away tag;
  - soul soil, blue ice and the reaction blocks are named.
  
  Writes map back to the water or lava level state, obsidian, cobblestone, stone, basalt or air.
- **Writes:** an act's writes go to an overlay that later reads see. At the end of the frame they flush through `FFoundationBlockWorld::SetMany`: stamped and logged as `Set` does, then `FVoxelBlockStorage::SetMany` makes one `FVoxelColumnPipeline::SetCells` call per column. The IsKnown guard drops a write to a cell that isn't known (none happened); unchanged cells are skipped. The hook ignores the system's own flushes.
- **The fluid tick:** its own counter, equal to the game tick while it runs (`AVoxelWorld::Tick` passes the frame's clock ticks after `Clock.Step`):
  - under `voxel.Fluids 0` it stands still and edits wake nothing;
  - past the budget it falls behind and catches up, so the same world arrives later;
  - ticks with nothing due are skipped.
- **The simulation ring** (Q6 (a)): columns whose 3 x 3 is in view, narrowed by `voxel.Fluids.SimRadius`. When the ring moves, pending acts whose column left it are parked with the delay they had left. An act that reads a column not final yet is parked on that column. Both re-queue together, in their first order, when their column is back (in the ring and final, or final), and never into a tick already started.
- **The budget** (Q10 (a)): `voxel.Fluids.BudgetMs` 0.5. A frame stops acting when the next act plus the flush of what it wrote would pass it. Both costs are the costliest of recent frames that acted, falling a tenth each, so the quiet frames between waves keep them.
- **Game rules:** world.json's `water_source_conversion` (default true) and `lava_source_conversion` (default false). The dev seed's world uses the defaults.
- **Switches:** `voxel.Fluids` (0), `voxel.Fluids.BudgetMs` (0.5), `voxel.Fluids.SimRadius` (-1: the whole ring). `voxel.Fluids.Info` logs the tick, queued and parked acts, waits, sleeps, writes and frame cost.

**Block world and storage** (`Foundation/FoundationBlocks.h/.cpp`, `Voxel/VoxelBlockStorage.h/.cpp`):
- `OnSet` and `SetMany` are new. `Set` now returns its own revision even when the hook writes more.
- `IFoundationBlockStorage::SetMany` defaults to one `Set` each.

**Pipeline** (`Voxel/VoxelColumnPipeline.h/.cpp`):
- **`SetCells(column, cells[])`:** one column copy, one heightmap pass, the union of the dirty sections, one `MeshVersion` bump per column. It gives the same states, stamps, heights and dirty sections as `SetCell` per cell (pinned).
- **The stale-mesh fix** (:375): a mesh whose column changed while it was meshing is shown, and the column stays dirty and meshes again, so a column edited every few frames always shows a mesh. `GetStats().Stale` counts them; `Dropped` now counts only unloads.
- Edit replay (after Generate or Finalize) goes through the new `FVoxelColumn::SetStates`: the same column, with one heightmap pass for many edits.
- Test hooks `GetJobColumn` and `IsJobPending` were added.

**Core** (`UnnamedVoxel`):
- `FVoxelFluidScheduler`: `Park`, `ParkAsleep`, `Unpark` (one column, or every parked column at once with a minimum due tick), `PeekDue` and parked counts. A parked act stays pending for its cell.
- `VoxelFluidRules::WakeAround`, split out of `Apply` with the same order.
- `FVoxelColumn::SetStates`.
- No rule changed; the oracles are untouched.

**Evidence tooling:** `FVoxelFluidShots` (`Voxel/VoxelFluidShots.h/.cpp`), run by `CaptureVoxel.ps1 -Fluids breach|flood` (`-VoxelShotFluids`). It turns `voxel.Fluids` on for the capture and back afterwards. A failed site search fails the script, and the switch refuses `-Flight`.

**Tests:** 9 new; `TestVoxel.ps1` expects 144.

| Test | What it pins |
|---|---|
| `Fluid.World.Matches` | The game system on a flat pipeline equals W1's oracle world at every tick: water at a column corner (60 ticks), lava (200), and lava placed beside water (obsidian before any tick). |
| `Fluid.World.Border` | An act reading a column that isn't final waits, parked on it (nothing written there). Once the column is final, the flow settles to the oracle's world. |
| `Fluid.World.ReturnTrip` | The flow's columns unload mid-flow: acts sleep with their delays, edits survive, and after the return, tick 62 + k equals the oracle's 12 + k. |
| `Fluid.World.SetCells` | `SetCells` equals `SetCell` one at a time: 252 cells, 2,048 heightmap values and 12 dirty masks. |
| `Fluid.World.Budget` | Under a 0.0001 ms budget the flood falls 899 ticks behind and settles to the unlimited run's world (545 fluid cells). |
| `Fluid.World.FrameRates` | 144, 60, 30, 10 and 4 fps (up to 5 ticks a frame) hold the same cells at every tick: 328 snapshots against 144 fps, 0 differ, and the fluid spread from 2 to 138 cells. |
| `Fluid.World.Freeze` | `voxel.Fluids 0` changes no cell and stops the tick; an edit then wakes nothing. Thawed, the flow settles to the oracle's world. |
| `Fluid.World.StaleMesh` | A stale mesh is shown (Stale + 1), the column stays dirty, and the next mesh leaves it clean. |
| `Fluid.Parking` | The scheduler's parking: remaining delays, FIFO across columns, pending while parked, the asleep and waiting kinds, and `Reset`. |

## Results

**Breach** (`WaterSky-W2a-e3-breach`, final code; the plan's "dig into a sea from a cave; the cave floods"): a sealed 7 x 7 x 4 room under a seabed of sand (Z 60, two sources deep, near the spawn), with a one-cell shaft. Opening the seabed block wakes the sea: water falls down the shaft and spreads over the floor as flowing water, as vanilla's does.
- dry: 0 fluid cells in the room;
- 3 s: 36 flowing and 4 falling;
- 8 s and 20 s: 48 flowing and 4 falling (the whole floor wet; no sources form under a falling column, as in vanilla).
- The queue was empty by about 4 s: 100 acts, 56 writes, worst frame 0.39 ms.

**Flood** (the plan's 1,000-cell flood trace): a 5 x 5 grid of sources 10 apart on terraced desert near the spawn. In `WaterSky-W2a-e3-flood` (final code; not a timed run, other lanes' work was running):
- 1,554 wet cells, settled 170 game ticks (8.5 s) after the sources;
- fluid ms per frame: mean 0.017, p95 0.002, max 0.52;
- 9 frames passed 0.5 ms (worst 0.52), and 94 stopped at the budget;
- frame ms: mean 9.5, p95 10.6.
- **In the manager's quiet window** (`WaterSky-W2a-q1-flood`; no other editor, java or build): 1,554 cells, settled after 170 game ticks; fluid ms per frame mean 0.018, **p95 0.0017**, max 0.61; 18 frames passed 0.5 ms (by one act at most) and 99 stopped at the budget; frame ms mean 9.66, **p95 10.86**, max 19.4.
- Earlier runs, before the budget counted the flush and the next act, reached 0.61-0.71 ms (`Flood-2` to `-7`, `e1-flood`).
- In game an act costs about 16 µs, writes included (55 ms over 3,423 acts): W1's test world measured 10-18 µs.

**Gate** (run g2, after the last code change; `C:\dev\sandbox\water-w2a\gate-g2.txt`, evidence `Tests/Evidence/2026-10-10/WaterSky-W2a-g2-*`; g1 before the last budget change gave the same counts). Other lanes' editors were running; nothing here is timed.
- The four reference verifiers (blocks, worldgen, items, sky) report REFERENCE OK.
- `TestVoxel.ps1` passes 144 of 144. That includes the goldens: `Mesher.Digest` (12 digests, unchanged), and `Aquifer`, `Density`, `Decoration` and `Biome.Golden`.
- Every native suite (`Automation RunTests Unnamed`): 223 passed, 0 failed; one passed with a warning (the editor's LogHttp `generate_204` timeout).
- `npm test`: 57 passed. `source-check` passes.
- **The six radius-8 hashes are identical** to main's (6 of 6); both load orders agree and 0 columns differ.

| Preset | `voxel.Decoration 0` | Decorated |
|---|---|---|
| overworld | `0xe3e4e339022a4ec0` | `0x194a623a86b97dd6` |
| large_biomes | `0x92d83639acdee88f` | `0x1b665f4703a79406` |
| amplified | `0x615321e7b313fed6` | `0xbde96a3deaa9d130` |

**End to end** (after the last code change; `C:\dev\sandbox\water-w2a\e2e-e2.txt`, `e2e-e3.txt`):
- `node create-world.mjs --editor`: 32 PASS, 0 FAIL.
- `TestFoundationRuntime.ps1 -Editor -FrameRates 60`: 0 failures, 44.2 fps mean (run e3). Run e2 failed one Playground combat check (`a weak hit inside the target's cooldown adds nothing`: strength 0.520, damage 2.498) under load. The Playground has no fluid system; e1 and e3 passed it, so it is recorded as a flake.
- `CaptureVoxel.ps1 -Flight` (loaded, not a certification): holes in 53 frames (at most 13 columns), chunk work worst 4.47 ms with 4 of 9,454 frames over the 2 ms budget, memory flat over laps 2 and 3. Fluids are off in the flight; W2a reaches it only through the stale-mesh fix and the edit replay, and the flight makes no edits.
- **The flight certification, in the manager's quiet window** (19:33-19:39; no other editor, java or build):
  - flight 1 (`q1-Flight`): holes in 86 frames (at most 12 columns, all on the circle laps), chunk work worst 2.12 ms, 1 of 9,393 frames over 2 ms, memory flat (4,894 to 4,892 MB);
  - flight 2 (`q2-Flight`): holes in 32 frames (at most 6), worst 2.23 ms, 1 of 9,472 over, memory flat (4,834 to 4,829 MB).
  - Both sit in main's current range. S9b's own flight with trees had holes in 58 frames (at most 6) and 6 over budget. W1's 2-frame figure predates trees on.

## Known gaps

- **The W2a stub:** models and snow layers holding no fluid (plants, torches, doors) are walls. Fluid writes don't settle plants. Both are W2b's.
- **Drained cells stay as edits:** the edit list grows with the cells fluid ever touched. Phase 10 may prune edits equal to the generated cell.
- **Never-returning columns:** an act waiting on a column that never comes back stays parked, as vanilla keeps ticks with an unloaded chunk.
- **Budget overruns:** a frame can pass the budget by about one act.
- **Edits made under `voxel.Fluids 0`** don't wake fluid once it is back on.
- **Unchanged from W1:** the `Apply`/`Wake`/`React` recursion; generated edges don't wake (W5).

## Subagents and tokens

- **Lead:** about 470k at the records commit (MeasureAgentUsage); about 200k went on the reading list and the wiring code, the rest on subagent results, review fixes, three budget rounds, the gates and the records.
- **Opus 5.5, tests:** 191k peak (it estimated 75k). It found the cross-column unpark order, which is now fixed and pinned.
- **Opus 5.5, evidence captures:** 250k peak (it estimated 105k). It found the budget overrun from the flush and the mislabelled counter, both fixed.
- **Sonnet 5.5 pre-commit reviewers:** the diff (2,745 lines) was split three ways by path. Their peaks were 98k (core), 68k (tests) and 66k (captures). The fixes taken:
  - per-frame decay of the cost estimates;
  - settings read at `Start`;
  - un-parked acts never run in an open tick;
  - FrameRates gained 10 and 4 fps and a flow check;
  - ReturnTrip asserts pending acts;
  - the capture restores `voxel.Fluids` correctly, fails on no site, and checks settling with nothing parked.

## Evidence

- `Tests/Evidence/2026-10-10/WaterSky-W2a-e3-breach` (final code), `-e1-breach`, `-Breach-2`, `-Breach-1`.
- `Tests/Evidence/2026-10-10/WaterSky-W2a-e3-flood` (final code), `-q1-flood` (the quiet window's trace), `-Flood-2` to `-7` and `-e1-flood` (earlier budgets and grids), `-Flood-1` (its per-frame numbers missed the frames that acted).
- `Tests/Evidence/2026-10-10/WaterSky-W2a-q1-Flight` and `-q2-Flight` (the certification), `-e3-Flight`, `-e3-Probe`, `-e2-Probe` (the flake), `-e1-*`.
- `Tests/Evidence/2026-10-10/WaterSky-W2a-g2-*` (after the last code change) and `-g1-*`: the gate's TestVoxel, hashes and native reports.
- `C:\dev\sandbox\water-w2a\`: build logs, test runs, the gate summaries `gate-g1.txt` and `gate-g2.txt`, the end-to-end summaries `e2e-e1/e2/e3.txt`.
