# Voxel world handoff: phase 6, into S3 (streaming stages and Finalize) - October 9, 2026

Read this first if you are picking up the voxel world in a new chat. Phase 6's S2 is done and committed (`a325d4b`): the decoration core (Decorate, the write log in 9 buckets, Finalize), the ore feature matched to S1b's probes, `voxel.Bench Decorate`, `voxel.OreHistogram` and a capture preview. **S3 is next:** the streaming stages, with scout C's recompute-on-miss. It ends with a handoff into S4. This handoff replaces [the S2 handoff](2026-10-09-Voxel-Handoff-Phase6-S2.md) as the entry point. Facts were true at `a325d4b`; check `git status`, `git log -5` and newer records before trusting them.

## 1. Read these, and only these

Budget: about 120k of reading. Everything S2 settled is summarized here.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md) (its handoff pointer is stale; this handoff wins).
2. **This handoff.**
3. **[The phase 6 plan](2026-10-09-Voxel-Phase6-Plan.md):** lines 96 to 130 (3.1 cross-column writes, 3.2 randomness and order), 145 to 173 (3.5 the 5 ms limit and the gates), 284 to 314 (S3) and 643 to 667 (Jesus's answers).
4. **[Scout C, the pipeline](2026-10-09-Voxel-Phase6-Scout-Pipeline.md):** lines 10 to 51 (summary; recommendation 1, recompute-on-miss) and 200 to 272 (section 4: rings, cancellation, memory caps, holes, pitfalls, tests). Its P2 numbers (vanilla's rings per status) are in [their own record](2026-10-09-Voxel-Phase6-Scout-Pipeline-Probes.md).
5. **Docs:** [VoxelWorld.md](../VoxelWorld.md), "Decoration" (lines 444 to 483) and "Streaming" (its first 40 lines).
6. **Code, headers first:** `Source/UnnamedVoxel/Decoration/VoxelDecorator.h`, `VoxelWriteLog.h`, `VoxelDecorationPreview.h`; `Generation/VoxelArea.h` (`FVoxelDecoratedArea`, the stage order to copy); `Streaming/VoxelStreamingPlanner.h`; `Source/Unnamed/Voxel/VoxelColumnPipeline.h`. Open `VoxelColumnPipeline.cpp` and `VoxelWorldSubsystem.cpp` only where S3 hooks in.

## 2. Working agreement with this user

Unchanged from [the S1 handoff](2026-10-09-Voxel-Handoff-Phase6-S1.md), section 2: the manager (Nyx at night, Hemera by day: a short plan first, report before and after committing, the manager pushes), look calls with a control pair, context checkpoints at about 300k and every 200k measured with `MeasureAgentUsage`, subagent rules, commits on `feature/voxel-world`, the machine (log its state with every timing; old and new builds alternated in one window; gates on a quiet machine). Jesus's ore direction (vanilla 26.3's data is the default; better-looking options come later as look calls) stands; see [the S2 handoff](2026-10-09-Voxel-Handoff-Phase6-S2.md), section 2.

## 3. Where things stand

| Phase | Scope | Status |
|---|---|---|
| 0 to 5 | Scaffolding to biomes and surface | Done |
| 6 plan, S0, S1, S1b | Plan; speed-up; worldgen data and reader; facts and probes | Done |
| 6 S2 | The decoration core, with ores | **Done** (`a325d4b`) |
| 6 S3 | Streaming stages and Finalize | **Next** |
| 6 S4 to S12 | Ore veins, block models, the rest | |

**S2 built** ([VoxelWorld.md](../VoxelWorld.md), "Decoration"):
- **The core** (`Source/UnnamedVoxel/Decoration/`): `FVoxelDecorator` compiles the graph for a terrain's biomes (30 placed features: 29 ore placements and `ore_infested`; 141 of other types skipped and counted). `Decorate(column, 3x3 snapshots)` gives a `FVoxelWriteLog` of 9 buckets, one per target column, each already in (step, order index, sequence) order as runs; `Finalize(snapshot, 9 incoming buckets)` replays them as a 9-way merge keyed (step, order index, source offset dz then dx). Both are pure functions of (seed, data, columns). `DecorateAlone` is the oracle (5x5 generated, 3x3 decorated, centre finalized).
- **Writes:** 8 bytes (`FVoxelWrite`): cell, mode (`Force`, `IfReplaceable`, `IfTargets`), an empty support-condition slot (`EVoxelWriteSupport`; S8 fills it), state, argument. `IfTargets` re-checks the ore's targets against the merged column.
- **Randomness:** each feature's stream from `VoxelFeatureSeeds::GetStreamSeed`; each `count` try its own stream from (stream, try index), so scout E's early-out (a try whose blob lies above every block it could replace is skipped, before the biome lookup) is exact: final-column hashes are equal with it on and off.
- **The ore feature:** our blob (spheres along a segment), with discard on air exposure, matched to all 24 probes as distributions (`Decoration.OreProbes`). **Provenance:** the shape is the commonly published ore-blob form; its constants were fitted to and confirmed by the probes (a stdlib Python prototype, `C:\dev\sandbox\phase6s2\blobsim.py`, matched with the first values; the C++ port gives the probes' mean cells per blob: 6.7, 22.0, 112.3 and 681.9 at sizes 9, 17, 33 and 64, and their fine structure). The jar's code was not read. The night manager put to Jesus whether he wants the shape derived from the probe statistics alone (a contained change).
- **Rule tests, providers, modifiers:** the seven rule tests as Z bands over block bit sets; int and height providers (constant, uniform, trapezoid) matching `provider_histograms.json`; `count`, `in_square`, `height_range`, `rarity_filter`, `biome`.
- **Area and switches:** `FVoxelDecoratedArea` (Generate R+2, Decorate R+1, Finalize R). `voxel.Decoration` (default 1) decorates `voxel.Bench Columns` and the gates; 0 is the control. `voxel.Decoration.Preview` (default 0, captures only) builds streamed columns through the oracle with a snapshot cache; changing it rebuilds the world.
- **An exact storage lever:** `FVoxelSection::FromStates` stamps palette indices in a per-thread table and `Expand` decodes whole words (same palettes and words); Finalize fell from 0.43 to 0.15 ms with it, and Generate gains a little.
- **Ore-probe bounds still bite** (the night manager's check before the commit): the blob radius scaled by 1.15 fails `OreProbes` with 201 errors (means, histograms, extents), and the discard switched off fails it with 25 (7 exposure ratios); both reverted byte for byte, then `TestVoxel.ps1` 79 of 79.
- **Tests:** 13 in `Source/Unnamed/Tests/VoxelDecorationTests.cpp` (`Decoration.Merge`, `RuleTests`, `OreProbes`, `Oracle`, `Preview`, `LoadOrder`, `Reach`, `Golden`, `SaltStability`, `OreHistogram`, `HeightmapTags`, `Order`, and `Providers.Distribution`), written by a Sonnet 5.5 subagent from an exact spec; `Unnamed.Voxel` 79 of 79, every native suite 127 of 127. Scout C's four mutations (the switch `DecorationTestMutation`) each fail their named checks: arrival-order merge (`LoadOrder`, `Oracle`), a neighbour's decorated state (`LoadOrder`, `Oracle`), one shared stream (`SaltStability`, `Golden`), swapped order indices (`Golden`, `Order`). Bounds the subagent changed from the spec, each justified: the probe offset chi-squares divided by the blobs' clustering factor (blocks come in blobs); a failed placement is one that sets no cell; the exposure tolerance at least 3 of the probe's own standard errors; the ore histogram's variance from the blob profile.

**S2's gate** (quiet machine: no other UnrealEditor, UnrealBuildTool or java; background apps 0 to 25% CPU; medians of 3 runs of `voxel.Bench Decorate 4`, one thread, alternated with the old build):

| Preset | Decorate mean (0.6) | Finalize mean (0.2) | Generate (Decorate bench) | Whole column | Generate old / new (Density bench) |
|---|---|---|---|---|---|
| overworld | 0.600 | 0.162 | 3.113 | 3.875 | 3.191 / 2.867 |
| large_biomes | **0.760** | 0.211 | 3.242 | 4.213 | 3.806 / 3.121 |
| amplified | **0.686** | 0.155 | 3.504 | 4.345 | 3.820 / 4.072 |

- **Decorate is not met on large_biomes and amplified.** Two exact levers ran in the night manager's 45-minute timebox (a per-cell written bitmap; the early-out ahead of the biome lookup); no third. The rock blobs (granite, diorite, andesite and tuff at size 64; dirt and gravel at 33) are about 85% of the 6,850 writes per column: their geometry and per-cell work are where the time goes.
- **Per column** (dev seed, overworld, radius 4): 287 tries reach the early-out, 178 skipped; 6,850 blocks placed and logged; log 63.5 KB (most 81 KB); working copy 204 KB; Finalize replays 7,004 writes and repacks 8.1 sections.
- **Whole column:** every preset under Jesus's 5 ms limit with the Decorate bench's Generate (3.9 / 4.2 / 4.3 ms); amplified is 4.91 ms with the Density bench's noisier Generate median (4.072).
- **Controls:** with `voxel.Decoration 0`, the old radius-8 hashes reproduce in both orders (`0xeb1c3d76838e2481`, `0xcb42685280f8b57c`, `0xc4403ff66465fcf0`) and every old golden passes; with 1, one decorated hash per preset in both orders (`0x0015e9b54e7fe0eb`, `0x24bcb9ca548e0fcf`, `0x502dbd3b9d554557`), and `Decoration.LoadOrder`'s 22 schedules give one hash. `Decoration.Golden` (R 4, seed 263) was recorded once under the decided change "ores on": `0xa28113883ba6e7ca`, `0xf87e27cd1455c61a`, `0xfc7c6279d46e6b76`. The flight with the preview off is as S1's (`VoxelFlight-S2-222315`: no holes, 1 of 10,016 frames over the 2 ms budget, memory flat at 11.84 MB of columns; first frame to view 1,893 ms). `create-world.mjs`, the Playground probe (0 failures), the `-DisableAdaptiveUnity` build and `verify_reference.py` pass; the reference is unchanged.
- **Captures** (Preview on, dev seed): the edge-walls section (`Tests/Evidence/2026-10-09/PreviewOres-S2-214201/section.png`) with its control (`PreviewOff-S2-214313`: 10.0% of pixels differ; granite-orange 1.71% on, 0.05% off); the ore histogram sheet (`OreHistogram-S2-213939/`). The night manager accepted the look (Jesus's delegation): the section reads like a vanilla cross-section and the histogram shapes follow 26.3's height providers.

## 4. S3: what to do

Scope as [the plan's S3](2026-10-09-Voxel-Phase6-Plan.md) (lines 284 to 314), with these from scout C and S2. Send them in your plan; the manager confirms.

1. **The stages in the pipeline:** records gain generated (snapshot), decorated (log buckets), final and meshed; jobs gated as `FVoxelDecoratedArea` orders them (Decorate needs the 3x3 generated, Finalize the 3x3 decorated, Mesh the 3x3 final); the rings from `FVoxelStreamingRings` (scout C's figures at `ViewRadius` 12: 545 in view, 657 final, 777 decorated, **905 generated**, +38%).
2. **Recompute-on-miss** (scout C, recommendation 1): snapshots and buckets are caches of pure functions. A Decorate without a neighbour's snapshot regenerates it; a Finalize without a source's bucket re-runs that source's Decorate (at worst 9 Decorates and 25 Generates); **a bucket aimed at a column that is already final is dropped, never applied** (re-applying would undo the player's edits). Cheap default: keep the snapshots of final columns that still have a non-final neighbour (about 112 columns, 2 MB at R 12). Tests `Pipeline.EvictionFuzz` and `Pipeline.ReturnTrip`, and a regenerations-per-lap counter in `voxel.Info`.
3. **Buckets freed when their target finalizes** (S2's log already splits them); report held log and snapshot memory at R 12 (scout C: 5 to 22 MB of logs split, 4.7 MB of snapshots).
4. **The freeze pass moves to Finalize** (plan 3.4), so snow lands on decoration; MC-119599 stays fixed by construction.
5. **Retire the preview** once streaming decorates (keep `DecorateAlone` as the oracle); keep `voxel.Decoration 0` as the control. The new test `Decoration.Preview` then compares streaming's columns with the area's.
6. **A cave-wall camera in `CaptureVoxel`** (night manager, a follow-up from S2): `-Caves` looks out of a cave mouth toward the sky, so no shot shows a rock wall with ores. Aim one camera at a wall inside a cave; scout E's ore look calls (L1 host rock, L2 glint) need the same shot.
7. **Gate** (the plan): Finalize at most +0.1 ms net (the freeze pass, 0.07 ms, leaves Generate); the flight within the 2 ms budget on 99.9% of frames, no holes, memory flat; first view at most 4.0 s; generated columns at radius 12 reported. **Decorate's S2 overrun stays recorded;** S3 adds no Decorate time. Report each preset's whole column (Generate + Decorate + Finalize) against Jesus's 5 ms limit (answer 11; plan 3.5 flags).

## 5. Notes from S2

- **The early-out must not depend on the biome:** it runs where the biome modifier would look up the biome, before the lookup, and only for ores none of whose targets can match air; `GetTopZ` is raised by writes, so it stays exact as earlier features add blocks.
- **Timings are noisy on this machine:** background apps (NZXT CAM, Creative Cloud, the Modrinth App, which must be left alone) take 0 to 25% CPU; single runs of one bench varied by 30%. Alternate builds (`C:\dev\sandbox\phase6s2\ab.ps1`, `gate2.ps1`: DLL swaps of `UnrealEditor-Unnamed.dll`, `UnrealEditor-UnnamedVoxel.dll` and `UnrealEditor.modules`) and take medians. The bench's first pass runs about 30% slower (warm-up), so `voxel.Bench Decorate` runs the early-out-off pass first.
- **A HEAD build for A/B:** `git stash push --include-untracked`, build, copy the three files, `git stash pop`; then touch or rebuild (UBT skips files whose copied timestamps are old).
- **`-UserDir` moves `ProjectSavedDir()`:** headless runs with a temporary user folder lose files written there; `voxel.OreHistogram` writes under the project's own `Saved/`.
- **Captures set console variables with `-ExecCmds` after the world has started;** a switch read only at world start needs to rebuild the world when it changes (as the preview now does).
- **Shell:** the Bash tool's here-documents turn `\\n` into a newline inside Python strings; write patch scripts with the Write tool.

## 6. Known issues, each with when it is due

| Issue | Due |
|---|---|
| Decorate over its 0.6 ms allowance on large_biomes (0.760) and amplified (0.686) | Recorded; a later speed-up (the rock blobs' geometry) |
| Streaming decorates only through the capture preview | S3 |
| No cave-wall shot | S3 (the camera) |
| The blob's provenance: the commonly published shape, constants fitted to and confirmed by the probes | Jesus's question (project rule) |
| `CLAUDE.md` lines no worker or manager may edit (entry handoff, test count, facts path) | Questions for Jesus, below |
| The decoration read near its 50 ms gate (S1) | Jesus's morning list |
| 85 decoration blocks are not in the block table | S5 |
| Ore veins give nothing; the freeze pass runs before decoration | S4; S3 |

**Questions for Jesus** (each a `CLAUDE.md` line a worker may not edit; recommendation first):
1. **Entry handoff:** the project notes still name [the phase 6 handoff](2026-10-09-Voxel-Handoff-Phase6.md). Recommend pointing at the records index (`Docs/Records/README.md`), so the line stops going stale.
2. **Test count:** the Commands block says `TestVoxel.ps1` runs 61 `Unnamed.Voxel` tests; that was stale before S2 (66), and S2 makes it 79. Recommend dropping the number.
3. **Facts path:** the reference rule names only `tools/WorldgenFacts.java`; S1b added `tools/feature_probe.py`. Recommend "only through `tools/WorldgenFacts.java` and `tools/feature_probe.py`, then `build_reference.py`".
4. **The blob's provenance** (from the night manager): keep the commonly published shape with constants confirmed by the probes, or derive one from the probe statistics alone? Recommend keeping it: it matches all 24 probes, and the jar's code was not read.
5. **Subagent budgets** (from the night manager): the test subagent ran to 251.7k against its 150k brief (the reviewers to 130.7k and 173.9k). Recommend briefing test writers with a smaller first slice (the scheduler and three tests), then a fresh agent for the rest.

## 7. Code map: S2

| What | Where |
|---|---|
| Decorate, Finalize, the oracle, compile | `Source/UnnamedVoxel/Decoration/VoxelDecorator.*` |
| Working copy | `Decoration/VoxelDecorationContext.*` |
| Writes, buckets, the merge | `Decoration/VoxelWriteLog.*` |
| Rule tests and block tags | `Decoration/VoxelRuleTests.*` |
| Providers | `Decoration/VoxelNumberProviders.*` |
| Ore blob, compile, place | `Decoration/Features/VoxelOreFeature.*` |
| Preview | `Decoration/VoxelDecorationPreview.*`; `Source/Unnamed/Voxel/VoxelColumnPipeline.*`, `VoxelWorldSubsystem.*` |
| Decorated area | `Generation/VoxelArea.*` (`FVoxelDecoratedArea`) |
| Benches and hashes | `VoxelBench.*` (`RunDecorate`, `HashLoadOrders` with a decorator, `HashColumnBlocks`) |
| Console | `Source/Unnamed/Voxel/VoxelConsole.*` (`voxel.Decoration`, `voxel.Decoration.Preview`, `voxel.Bench Decorate`, `voxel.OreHistogram`) |
| Section encode lever | `Storage/VoxelSection.*` |
| Tests | `Source/Unnamed/Tests/VoxelDecorationTests.cpp` |
| Figure | `Scripts/OreHistogram.py` |

## 8. What worked and what failed in S2

**Worked; repeat it:**
- **A stdlib Python prototype against the probes first:** ten minutes settled the blob before any C++, and the port matched it number for number.
- **A temporary profiler, then exact levers one at a time:** cycle counters showed section copies and repacks were half the time; each lever kept the hashes.
- **A/B by DLL swap within one window:** single runs varied by 30%, alternated medians did not mislead.
- **Exactness checks built into the bench:** the early-out's on/off hashes print every run.

**Failed first; avoid it:**
- **Writes applied with `FVoxelSection::Set` in place** were slower than expanding, writing and repacking (once repacking was fast).
- **A switch read only at world start, set with `-ExecCmds`:** the first preview capture had no ores.
- **CaptureVoxel's cave view as a "cave wall":** it looks out of the cave.

## 9. Commands and tokens

```powershell
& C:\dev\sandbox\phase5s2\build.ps1 [-NoUnity]
& C:\dev\sandbox\phase6s2\exec.ps1 -Cmds '<console commands>' -Name x [-Tests Unnamed.Voxel.Decoration]   # headless editor, voxel log lines, test summary
& C:\dev\sandbox\phase6s2\gate2.ps1                         # old/new alternated: Density and Decorate benches, 3 rounds
& Scripts\CaptureVoxel.ps1 -Caves -ExecCmds 'voxel.Decoration.Preview 1'   # decorated captures (until S3)
uv run --no-project --python 3.13 --with matplotlib python -I Scripts/OreHistogram.py <csv> [--each]
& C:\dev\sandbox\phase5s6\measure.ps1 -Session <id>         # MeasureAgentUsage for a session started in C:\dev
```

**S2's cost** (session `bc109f45-594a-5ff9-b71d-f3ae6dc5ab77`, measured at the handoff):

| Agent | Peak context | Processed | Output |
|---|---|---|---|
| Lead (Opus 5.5) | 633k (425.0k at the 300k checkpoint, with the night manager's go-ahead to continue; 493k at the 500k status; wrap-up from about 510k) | 94.1M (93.2M cache reads) | 350k |
| The 13 tests (Sonnet 5.5) | 251.7k (briefed to stop near 150k) | 6.6M | 0.4k |
| Pre-commit review, voxel core (Sonnet 5.5) | 130.7k | 1.1M | 9.7k |
| Pre-commit review, game, tests and docs (Sonnet 5.5) | 173.9k | 3.6M | 1.0k |

## 10. The short prompt for S3

> Voxel world phase 6, session S3: streaming stages and Finalize. S2 (the decoration core, with ores) ran before it.
>
> Read `C:\dev\projects\Unnamed\Docs\Records\2026-10-09-Voxel-Handoff-Phase6-S3.md` first; its section 1 is the whole reading list (with line ranges), so keep to it.
>
> Build S3 only: the plan's S3 (records with the generated, decorated, final and meshed stages, jobs gated by the 3x3 rule and the new rings, snapshot and bucket lifetimes, cancellation, edits after Finalize, the freeze pass moved to Finalize, stats in `voxel.Info`), with the handoff's section 4: scout C's recompute-on-miss (a bucket aimed at a final column is dropped, never applied; `Pipeline.EvictionFuzz`, `Pipeline.ReturnTrip`), buckets freed at their target's Finalize, the preview retired, and a CaptureVoxel camera aimed at a cave wall. Keep `voxel.Decoration 0` as the control that reproduces the old goldens and hashes.
>
> Send the manager a short plan (files and why) before building. Report as the handoff's section 2 says, and measure your context at each checkpoint. End with a handoff into S4 (ore veins) and do not start S4.
