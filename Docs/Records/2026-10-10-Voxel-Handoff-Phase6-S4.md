# Voxel world handoff: phase 6, into S4 (ore veins) - October 10, 2026

Read this first if you are picking up the voxel world in a new chat. Phase 6's S3 is done and committed (`9b54611`): streaming decorates, with Generate, Decorate, Finalize and Mesh as stages, scout C's recompute-on-miss, the freeze pass at Finalize, the preview retired and a cave-wall camera. **S4 is next:** the `ore_vein` rule in the surface evaluator. This handoff replaces [the S3 handoff](2026-10-09-Voxel-Handoff-Phase6-S3.md) as the entry point. Facts were true at `9b54611`; check `git status`, `git log -5` and newer records before trusting them.

## 1. Read these, and only these

Budget: about 100k of reading.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md) (its handoff pointer is stale; this handoff wins).
2. **This handoff.**
3. **[The phase 6 plan](2026-10-09-Voxel-Phase6-Plan.md):** lines 131 to 137 (3.3 ore veins), 145 to 173 (3.5 the 5 ms limit and the gates), 315 to 323 (S4) and 643 to 667 (Jesus's answers).
4. **The vein facts:** [the S2 handoff](2026-10-09-Voxel-Handoff-Phase6-S2.md), line 46 (`facts/ore_vein_samples.json`: what was dumped, the six `overworld/ore_vein/*` densities, 5,575 positions); [the S1b handoff](2026-10-09-Voxel-Handoff-Phase6-S1b.md), lines 78 to 80 (the probe's design). Open `Reference/minecraft-worldgen-26.3/facts/ore_vein_samples.json` only to learn its layout (its first entries), and the six density functions in `Reference/minecraft-worldgen-26.3/data/minecraft/worldgen/density_function/overworld/ore_vein/` (`copper_density`, `gap`, `iron_density`, `mask`, `richness`, `toggle`).
5. **Docs:** [VoxelWorld.md](../VoxelWorld.md), "Surface rules, temperature and snow" (lines 362 to 415) and "Column density and terrain (generator 2)" (its first 30 lines, line 239 on).
6. **Code, headers first:** `Source/UnnamedVoxel/Surface/VoxelSurfaceEvaluator.h` and `VoxelSurfaceRules.h` (where `ore_vein` compiles to nothing today), `Terrain/VoxelDensityTerrain.h` (`FVoxelDensityTerrainOverrides`, `FillWithRules`); in `Source/Unnamed/Tests/VoxelSurfaceTests.cpp`, only the test you extend.

## 2. Working agreement with this user

Unchanged from [the S1 handoff](2026-10-09-Voxel-Handoff-Phase6-S1.md), section 2: the manager (Nyx at night, Hemera by day: a short plan first, report before and after committing, the manager pushes), look calls with a control pair, context checkpoints at about 300k and every 200k measured with `MeasureAgentUsage`, subagent rules (exact paths and line ranges, stop near 150k; a loosened bound gets its own mutation), commits on `feature/voxel-world`, the machine (log its state with every timing; old and new builds alternated in one window; gates on a quiet machine, asked of the manager). Jesus's ore direction (vanilla 26.3's data is the default; better-looking options come later as look calls) stands.

## 3. Where things stand

| Phase | Scope | Status |
|---|---|---|
| 0 to 5 | Scaffolding to biomes and surface | Done |
| 6 plan, S0, S1, S1b, S2 | Plan; speed-up; data and reader; facts and probes; the decoration core with ores | Done |
| 6 S5 | Block models: data (a parallel chain) | Done, merged into `feature/voxel-world` |
| 6 S3 | Streaming stages and Finalize | **Done** (`9b54611`) |
| 6 S4 | Ore veins | **Next** |
| 6 S6 to S12 | Block models (rendering), plants, trees, carvers, the rest | |

**S3 built** ([VoxelWorld.md](../VoxelWorld.md), "Streaming" and "Decoration"):
- **The staged pipeline** (`Source/Unnamed/Voxel/VoxelColumnPipeline.*`): a record holds the snapshot (Generate without the freeze), the 9 buckets its Decorate aimed at its 3x3, and the final column (Finalize: the merge, the freeze pass, the player's edits). A column wants its ring's stage (`FVoxelStreamingRings::GetRing`: 0 in view and meshed, 1 final, 2 decorated, 3 generated; 545 / 657 / 777 / 905 columns at radius 12). Walking nearest first, `RequireFinal` / `RequireBucket` / `RequireDecorated` / `RequireSnapshot` launch whatever a column misses as soon as its inputs exist. Without a decorator (`voxel.Decoration 0`) it is the old path exactly (Stages 1).
- **Recompute-on-miss** (scout C, recommendation 1): a Decorate without a neighbour's snapshot regenerates it; a Finalize without a source's bucket clears that source's decorated flag and runs its Decorate again; **a bucket aimed at a final column is dropped, never applied** (`BucketsDropped`).
- **Lifetimes:** a bucket is freed when its target's Finalize is accepted; a final column's snapshot once it and its 8 neighbours have decorated. Its neighbours are its sources, so at rest no final column keeps one: the snapshots held are rings 2 and 3 (248, 4.3 MB at radius 12); buckets with writes 913 (6.6 MB).
- **Cancellation and stale results:** a per-record flag lets an unloaded column's unstarted jobs skip their work; results are dropped by record serial as before.
- **Hysteresis:** the unload margin rises to 3 with decoration (`GetMinUnloadMargin`; unload beyond 18 at radius 12), because ring 3's diagonal corners reach R + 3 sqrt(2). Found by the pre-commit review; `Pipeline.Stages` checks it (margin 2 regenerates 6 corner columns on a diagonal step back and forth).
- **The freeze at Finalize** (plan 3.4): `IVoxelTerrain::GenerateSnapshot` / `FinishColumn`, `FVoxelFreezePass::ApplyToColumn` on the packed column (sections it changes expanded once, heightmaps updated); `FinishColumn(GenerateSnapshot)` equals `Generate` (`Freeze.Moved`, 3 presets). `voxel.Decoration.FreezeLast` (default 1; 0 = S2's order). **It re-records nothing:** the dev seed's radius-8 areas hold no snow, so S2's three decorated hashes (`0x0015e9b54e7fe0eb`, `0x24bcb9ca548e0fcf`, `0x502dbd3b9d554557`) and `Decoration.Golden` are unchanged under both settings. `voxel.Bench FreezeDiff` explains every differing cell in the snowiest area per preset (radius 4): overworld 9,374 snow layers, 0 cells differ; large_biomes 7 (5 ore or host cells beside a snow layer, 2 in the same blob, whose discard draw shifts); amplified 5 (beside snow); unexplained 0.
- **The preview is retired** (`Decoration/VoxelDecorationPreview.*` deleted, `voxel.Decoration.Preview` gone); `DecorateAlone(Terrain, Column, Out)` stays the oracle. `Decoration.Preview` now compares streaming with the area in three load orders.
- **`voxel.Info`** takes the world and adds the stages (rings, counts, per-stage times, what is held, regenerations, re-decorates, buckets dropped, cancelled jobs); the 1-second log line ends with the same counters.
- **The cave-wall camera** (`AVoxelWorld::FindCaveShots`, `CaptureVoxel -Caves` adds `cave-wall`): a covered camera facing a wall 3 to 10 blocks away, scored by ore blocks on its 9 x 5 face, doubled when daylit. Jesus has seen it (coal and iron in daylit stone).
- **Tests:** `Pipeline.Stages`, `Pipeline.EvictionFuzz`, `Pipeline.ReturnTrip` (`Source/Unnamed/Tests/VoxelPipelineStageTests.cpp`), `Freeze.Moved`, `Freeze.AfterDecoration` (`VoxelFreezeStageTests.cpp`), `Decoration.Preview` rewritten; `Unnamed.Voxel` 84 of 84, every native suite 133 of 133. Mutations (the switch `StageTestMutation`; `EVoxelPipelineMutation`), each run by the lead and reverted: `ApplyToFinal` fails ReturnTrip (24 edits undone, 19 columns off the edited state), `MissingBucketAsEmpty` fails EvictionFuzz (29 to 46 columns per seed off the oracle), `KeepSnapshots` fails Stages (69 final columns keep snapshots). Two bounds changed from the lead's spec, each with a mutation that bites: the "band of final columns keeping snapshots" (a spec error: none at rest; now an exact count), and ReturnTrip comparing the first visit's columns only.

**S3's gate** (quiet machine: no other UnrealEditor, UnrealBuildTool or java; CPU 1 to 12% at the bench; the manager held the items lane; old build `8f91b1f` and new alternated by DLL swap, October 10 00:50 to 01:06; three runs that overlapped another editor discarded and re-run):

| Check | Result |
|---|---|
| Flight, decoration on | No holes in any lap; 0 of 10,004 frames over 2 ms (worst 1.89); memory over laps 2 and 3: process 4,066 -> 4,066 MB, columns 18.55 -> 19.22 MB each lap; 3,913 to 3,916 columns per lap (old 3,350, +17%) |
| Flight, `voxel.Decoration 0` | The old build's: no holes, 3,350 / 3,355 / 3,350 columns per lap, columns 11.36 -> 11.84 MB |
| First view (start to view; first frame) | New 3,628 and 3,591 ms (2,028, 2,070); old 3,438 and 3,269 (1,838, 1,820); gate 4.0 s met. Worker time to view 7.6 to 8.1 s |
| Generated at radius 12 | 905 (+38% over 657), 777 decorated, 657 final |
| Finalize, medians of 3 (old / new) | overworld 0.172 / 0.181, large_biomes 0.159 / 0.276, amplified 0.204 / 0.329 ms; the freeze inside 0.048 / 0.069 / 0.076 ms against 0.050 to 0.065 in the old Generate: **net 0.00 to +0.02 ms** |
| Whole column (old / new) | overworld 3.947 / 3.814, large_biomes 4.126 / 4.257, **amplified 4.79 (old) / 5.04 (new) ms in one window; at the 5 ms limit within noise; flagged for Jesus** |
| Controls | `voxel.Decoration 0`: the old radius-8 hashes in both orders (`0xeb1c3d76838e2481`, `0xcb42685280f8b57c`, `0xc4403ff66465fcf0`) and every old golden; FreezeLast 0 and 1: S2's decorated hashes, from the bench and (`Decoration.Preview`) from streaming |

End to end (after the review fixes): `verify_reference.py` REFERENCE OK (unchanged), `create-world.mjs` PASS, the Playground probe 0 failures, the `-DisableAdaptiveUnity` build and the normal build succeed. Captures: `Tests/Evidence/2026-10-10/VoxelShots-20261010-011549/` (the standard shots, cave-wall, cave, lava) and the quiet flight `VoxelFlight-20261010-005601`; the shots Jesus saw are `VoxelShots-20261010-002019/cave-wall.png`, `VoxelShots-20261010-001828/section.png` and `VoxelFlight-20261010-002127/flight-circle.png`.

## 4. S4: what to do

Scope as [the plan's S4](2026-10-09-Voxel-Phase6-Plan.md) (lines 315 to 323, with 3.3): our own evaluator for the `ore_vein` rule, matched to `facts/ore_vein_samples.json`.

1. **The evaluator:** the six `overworld/ore_vein/*` density functions are already in the reference; build the rule where the surface evaluator compiles it to nothing today. The constants are in facts G7 (`VEININESS_THRESHOLD` 0.4, `VEIN_SOLIDNESS` 0.7, richness 0.1 to 0.3, gap skip below -0.3, `raw_ore_chance` 0.02); S1b found no vein block where its type's density is 0 or below and 67 to 69% filled above 0. Behaviour from the data, published docs and black-box runs only (`javap -public` for signatures; never method bodies or bytecode constants).
2. **The test** `Surface.OreVein` against the 5,575 samples (every recorded position: the block and the densities), and the shortcut (vein noises only inside the vein types' Y ranges) on and off giving 0 differing blocks.
3. **A switch** (veins on by default in phase 6, Jesus's answer 4) whose off setting reproduces today's goldens and radius-8 hashes; goldens re-recorded under the decided change "veins on", named in the commit.
4. **Evidence:** an edge-walls cut through a copper vein and an iron vein (the cave-wall camera may find a vein wall too), and the ore histogram with veins counted on their own.
5. **Gate:** at most 0.3 ms per column added to Generate (medians of 3, old and new alternated, quiet machine); the flight and first view unchanged within noise; report each preset's whole column against 5 ms (amplified is already at the limit, so any rise is a flag).

Before or beside S4, as the manager decides: **scout E's look calls L1 (ores wear their host rock) and L2 level 1 (ore-aware mips and glint, no glow)**, Jesus's answer of October 9; vanilla stays the default, one switch away; the cave-wall shot (`CaptureVoxel -Caves`) is the control pair's frame.

## 5. Notes from S3

- **The walk is gated:** `LaunchJobs` returns at once unless a job finished, the plan changed or something regressed (`bWalk`); every new transition that makes a stage lost must call `Regress()` (the review found `SetTintBlend` missing it).
- **Snapshot lifetime:** a final column's 8 neighbours are its sources, so the neighbour rule only matters after reloads; the at-rest count (rings 2 and 3) is the strong check.
- **The flight's laps do not exercise the return trip** (the 18-column circle unloads every column once a lap: 0 regenerations); `Pipeline.ReturnTrip`'s partial return (k = 5 at radius 2) does.
- **Busy-machine flights mislead:** with a test run in parallel, Generate rose to 5.15 ms and lap 1 showed holes; the quiet run had none. Ask for the quiet window before certifying.
- **Unity builds merge test files:** helpers in each test file's anonymous namespace carry a prefix (`Stage`, `Freeze`).
- **large_biomes and snow:** seed 263 has no snow within 512 columns (the climate is stretched 4 times); `Freeze.Moved` uses the dev seed for that preset, whose snow lies near (80, 0).
- **`-ExecCmds 'voxel.Decoration 0'` rebuilds an already-loaded world,** so that capture's first-view time (about 1.6 s) is not comparable with a cold start.
- **Shell:** `[IO.File]` calls resolve relative paths against the process folder, not PowerShell's: use full paths (a stray empty file was made and removed once).

## 6. Known issues, each with when it is due

| Issue | Due |
|---|---|
| Amplified whole column at the 5 ms limit within noise (4.79 / 5.04 in S3's window) and Decorate over its 0.6 ms allowance on large_biomes and amplified (S2) | One item for a future speed-up session (like S0); on Jesus's morning list |
| Ore veins give nothing | S4 |
| Scout E's look calls L1 and L2 | After S3 (now), as the manager schedules |
| 85 decoration blocks are not in the block table | S5 merged the data; S6 renders |
| Buckets aimed at rings 2 and 3 are held until their source unloads (bounded) | Recorded |
| `CLAUDE.md` lines no worker or manager may edit (entry handoff, facts path) | Questions for Jesus, below |

**Questions for Jesus** (each a `CLAUDE.md` line a worker may not edit; recommendation first):
1. **Entry handoff:** the project notes still name [the phase 6 handoff](2026-10-09-Voxel-Handoff-Phase6.md). Recommend pointing at the records index (`Docs/Records/README.md`), so the line stops going stale.
2. **Facts path:** the reference rule names only `tools/WorldgenFacts.java`; S1b added `tools/feature_probe.py`. Recommend "only through `tools/WorldgenFacts.java` and `tools/feature_probe.py`, then `build_reference.py`".
3. **Subagent budgets:** S3's test writers, briefed in two slices, peaked at 105k and 87k; the reviewers at 119k and 129k. Recommend keeping the two-slice brief.

## 7. Code map: S3

| What | Where |
|---|---|
| Staged pipeline, recompute-on-miss, lifetimes, test hooks, mutations | `Source/Unnamed/Voxel/VoxelColumnPipeline.*` |
| Rings per stage, the stage-aware margin | `Source/UnnamedVoxel/Streaming/VoxelStreamingPlanner.*` |
| Snapshot and finish | `Terrain/VoxelTerrain.h`, `Terrain/VoxelDensityTerrain.*`, `Surface/VoxelFreezePass.*` (`ApplyToColumn`) |
| FreezeLast, the oracle | `Decoration/VoxelDecorator.*` (`GenerateSnapshot`, `FinishColumn`, `DecorateAlone(Terrain, ...)`); `Generation/VoxelArea.*` (`FinishMs`) |
| FreezeDiff, the Decorate bench's freeze line | `Source/UnnamedVoxel/VoxelBench.*` |
| Decorator at world start, stage stats, the log line | `Source/Unnamed/Voxel/VoxelWorldSubsystem.*` (`DescribeStages`) |
| Switches, `voxel.Info`, `voxel.Bench FreezeDiff` | `Source/Unnamed/Voxel/VoxelConsole.*` |
| Cave-wall camera | `Source/Unnamed/Voxel/VoxelWorld.cpp` (`FindCaveShots`); `Scripts/CaptureVoxel.ps1` |
| Tests | `Source/Unnamed/Tests/VoxelPipelineStageTests.cpp`, `VoxelFreezeStageTests.cpp`, `VoxelDecorationTests.cpp` (`Decoration.Preview`) |

## 8. What worked and what failed in S3

**Worked; repeat it:**
- **Manual jobs:** queuing jobs and running any one on the test thread made random completion orders and evictions reproducible from a seed; the fuzz found the recompute paths working on the first run, and its mutation bites hard.
- **An accounting bench for a decided change:** `FreezeDiff` explained every differing cell before anyone asked whether the hashes would move.
- **Two test slices, then two reviewers split by path:** each stayed near budget, and the reviewers found the one real defect (the margin).
- **Mutations by the lead, after the subagent:** a mutation that did not bite (an eager snapshot free) exposed a spec error rather than a test gap.

**Failed first; avoid it:**
- **The spec's "band of final columns that keep snapshots":** it cannot exist at rest; read the design before writing a bound.
- **Timing runs beside another lane's editor:** contaminated rounds had to be discarded; the manager's window and a `Get-Process` check per run caught it.
- **Printing whole bench outputs:** a 3-round Density bench printed 8k tokens; summarize with a script (`C:\dev\sandbox\phase6s3\gatesum.py`).

## 9. Commands and tokens

```powershell
& C:\dev\sandbox\phase5s2\build.ps1 [-NoUnity]
& C:\dev\sandbox\phase6s2\exec.ps1 -Cmds '<console commands>' -Name x [-Tests Unnamed.Voxel.Pipeline]   # headless editor, voxel log lines, test summary
& C:\dev\sandbox\phase6s3\gate3.ps1 -Bench [-Captures] [-Flights]   # old/new alternated (phase6s3\oldbuild, newbuild): Decorate bench, first views, flights
uv run --no-project --python 3.13 python -I C:\dev\sandbox\phase6s3\gatesum.py <gate3 output>   # medians per build and preset
& Scripts\CaptureVoxel.ps1 -Caves     # standard shots plus cave, cave-wall and lava
& Scripts\CaptureVoxel.ps1 -Flight    # the acceptance flight
& C:\dev\sandbox\phase5s6\measure.ps1 -Session <id>   # MeasureAgentUsage for a session started in C:\dev
```

**S3's cost** (session `ff2dd84d-b9f8-5eab-8d43-39893c00ec0b`, measured at the handoff):

| Agent | Peak context | Processed | Output |
|---|---|---|---|
| Lead (Opus 5.5) | 569.8k (393.7k at the 300k checkpoint, with the night manager's go-ahead; 501.0k at the 500k status; wrap-up from about 540k) | 82.8M (82.0M cache reads) | 267k |
| Tests, slice 1: Stages, EvictionFuzz, ReturnTrip (Sonnet 5.5) | 105.1k (briefed to stop near 150k) | 1.5M | 22.2k |
| Tests, slice 2: Freeze.Moved, Freeze.AfterDecoration, Decoration.Preview (Sonnet 5.5) | 87.1k (briefed near 120k) | 0.7M | 2.5k |
| Pre-commit review, core and pipeline (Sonnet 5.5) | 119.0k | 0.8M | 16.0k |
| Pre-commit review, game, tests, docs and scripts (Sonnet 5.5) | 129.3k | 1.6M | 0.5k |

## 10. The short prompt for S4

> Voxel world phase 6, session S4: ore veins. S3 (the streaming stages and Finalize) ran before it.
>
> Read `C:\dev\projects\Unnamed\Docs\Records\2026-10-10-Voxel-Handoff-Phase6-S4.md` first; its section 1 is the whole reading list (with line ranges), so keep to it.
>
> Build S4 only: the plan's S4 (the `ore_vein` rule in the surface evaluator, matched to `facts/ore_vein_samples.json`; `Surface.OreVein`; the vein shortcut exact; a switch whose off setting reproduces today's goldens and hashes; goldens re-recorded under "veins on"; the edge-walls cut through a copper and an iron vein; the ore histogram with veins), with the handoff's section 4. Gate: at most 0.3 ms per column added to Generate, measured old and new alternated on a quiet machine; amplified's whole column is already at 5 ms, so report any rise as a flag.
>
> Send the manager a short plan (files and why) before building. Report as the handoff's section 2 says, and measure your context at each checkpoint. End with a handoff into the next session and do not start it.
