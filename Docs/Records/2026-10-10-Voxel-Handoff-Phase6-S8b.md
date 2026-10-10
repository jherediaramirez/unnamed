# Voxel world handoff: phase 6, into S8b (the plants' gate, evidence and docs) - October 10, 2026

Read this first if you are picking up the voxel world's main lane in a new chat. S8 part A (plants and patches: the vegetation feature types, 74 placed features on by default) is built, tested and committed (`fa3a5c1`); [its record](2026-10-10-Voxel-Phase6-S8.md) has the details. By the night manager's 500k rule, **S8b** takes what part A could not: the timed gate and the flight first, in a quiet window, then the evidence shots and `Docs/VoxelWorld.md`, and part B once Jesus has answered scout I's questions 1 to 3. This handoff replaces [the S8 handoff](2026-10-10-Voxel-Handoff-Phase6-S8.md) as the main lane's entry point. Facts were true at `fa3a5c1`; check `git status`, `git log -5` and newer records before trusting them.

## 1. Read these, and only these

Budget: about 100k of reading.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, and the project's [CLAUDE.md](../../CLAUDE.md).
2. **This handoff.**
3. **[The S8 record](2026-10-10-Voxel-Phase6-S8.md):** all of it (short).
4. **[The phase 6 plan](2026-10-09-Voxel-Phase6-Plan.md):** lines 145 to 172 (3.5, the gate rules and flags), 405 to 428 (S8), 500 to 512 (acceptance captures).
5. **[Scout I's doc](2026-10-10-Voxel-Phase6-Scout-I-Plants-Patches.md):** section 5.3 (cost and levers) for the gate; sections 4.4 and 9 only for part B.
6. **[VoxelWorld.md](../VoxelWorld.md):** "Decoration" (find it by heading); you add S8 to it.
7. **Code, headers only unless a step needs more:** `Source/UnnamedVoxel/Decoration/VoxelDecorator.h`, `Decoration/Features/VoxelPatchFeatures.h`, `Blocks/VoxelPlantSurvival.h`; for the shots, `Scripts/CaptureVoxel.ps1`'s parameters and `Source/Unnamed/Voxel/VoxelWorld.h`.
8. **Jesus's morning answers** to questions 1 to 3 (section 6), if the manager has passed them on.

## 2. Working agreement with this user

Unchanged from [the S1 handoff](2026-10-09-Voxel-Handoff-Phase6-S1.md), section 2.
- **The manager:** Nyx at night, Hemera by day. Send a short plan first, listing every file you will change (other lanes share the PC and some files); report before and after committing; the manager pushes.
- **Look calls:** each comes with a control pair, two runs of the control for noise, and the numbers.
- **Context checkpoints:** at about 300k and every 200k after, measured with `MeasureAgentUsage` after every build, test run and subagent result.
- **Subagents:** give exact paths and line ranges, and tell them to stop near 150k.
- **Commits** go on `feature/voxel-world`.
- **The machine:** log its state with every timing; alternate old and new builds in one window; run gates in a quiet window asked of the manager, and hold (nothing heavy) when it sends "QUIET WINDOW HOLD".

## 3. Where things stand

| Phase | Scope | Status |
|---|---|---|
| 0 to 5 | Scaffolding to biomes and surface | Done |
| 6 plan, S0 to S6 | Plan to block models; ore looks | Done |
| 6 S6b, S7 | Block models in the Playground; plant interaction | S6b merged; S7 in its lane |
| 6 S8 part A | Plants and patches: types, tests, goldens | **Done** (`fa3a5c1`) |
| 6 S8b | The gate, flight, evidence, docs; part B after Jesus's answers | **Next (main lane)** |
| 6 S9 to S12 | Trees, carvers, the rest | |

**What part A built** (record, section 1): block properties in the core table; the plant survival table (`Blocks/VoxelPlantSurvival.*`, shared with S7's lane); `biased_to_bottom`, `weighted_list`, `clamped`; live heightmaps in the working copy; the write mode `IfUnchanged` and the supports `Plant` / `PlantPair`, re-checked by Finalize; 12 block predicates; 7 state-provider types; simple_block, block_column, disk, vegetation_patch (floor), the four selectors, sequence, no_op; the switch `voxel.Decoration.Plants` (default 1, every bench and streamed world follows it; 0 is the control).

**Verified:** voxel suite 101 of 101 (after the review fixes and T2's merge, see the record); plants off reproduces every golden, the mesh digest and the six radius-8 hashes; plants-on values recorded; scout I's oracles within bounds; the replay test; mutations 5 and 6 each fail a test; Playground probe and create-world pass.

## 4. S8b: what to do, in order

1. **The gate, first, in a quiet window** (ask Nyx or Hemera: "ready for quiet window", with its length and steps; a queued chain checks a cancel flag before each launch):
   - Build both sides with `build.ps1 -NoUnity`: old = the commit before `fa3a5c1` (or `voxel.Decoration.Plants 0` on the new build as a second control), new = `fa3a5c1`. Restore files with fresh timestamps (the S8 handoff, section 5).
   - `voxel.Bench Decorate 4 <preset>` for overworld, large_biomes, amplified, three runs each, alternated; report Decorate with plants on minus plants off (the "patches" figure; allowance 0.4 ms per column), Finalize, Generate, whole column, with the machine state.
   - A first look on a busy machine (not a gate figure) gave Decorate 1.19 to 1.25 ms per column with plants on, against S2's gated 0.600 with ores only: the patches may be over 0.4 ms. If so, the plan's flags apply (tell the manager before committing anything), and Nyx's rule says S8b fixes it or flips the default. Levers (scout I, 5.3): biome lookups dominate (seagrass and kelp check the biome on every try because `biome` comes last in their chains); a per-column cache of the jittered biome at the heightmap cell; skipping `#air`-filtered tries whose cells lie above every column top.
   - Mesh per column in plant-dense biomes against S6's figure: `voxel.BenchModels 4 3` (now follows `voxel.Decoration.Plants`), plants on and off; S6: 0.28 ms per meshed section, model quads 0.176 / 0.136 ms per planted column.
   - The flight (`CaptureVoxel -Flight`), old and new alternated: holes, the 2 ms apply budget, memory.
2. **Evidence:** the nine biome shots (plains, flower forest, meadow, desert, badlands, jungle floor, a swamp with lily pads, a lukewarm ocean floor with kelp and seagrass plus a warm ocean with pickles, a taiga with berries and ferns) and a column-grid overlay shot of patches crossing borders. Build a `voxel.FindBiome <id>` helper and `voxel.Debug.ColumnGrid` (debug lines on column borders); keep off the shot code the torch probe and S4 added in `VoxelWorld.cpp` (Nyx's condition). A Sonnet subagent with exact specs can build them. Look at every sheet before sending it.
3. **`Docs/VoxelWorld.md`**, "Decoration": S8's pieces (record, section 1), the switch, the plants-on hashes, and "ours" for each open algorithm (dual_noise's mapping, the noise providers' mapping, vegetation_patch's footprint, block_column's trim, the selectors' draws); the status table; the known issues below.
4. **Part B, after Jesus's answers:** glow lichen's `multiface_growth` (Q3: clamp the search to the 3x3 window); the five modifiers with no S8 user, the unused predicates and rotated / random_block (Q2); the seven S11 / S12 placed features (Q1: `FVoxelDecorationOptions::bLaterPlants`, one switch). Each moves the plants-on goldens under its decided change.

## 5. Notes from part A

- **Plants off is the old path:** if a plants-off value ever differs, something leaked into the ore path (the context's table, `Finalize`'s new branches, `RunModifiers`' refactor).
- **`IfUnchanged` over `IfReplaceable` for plants** (record, section 2): it keeps two columns' patches from overwriting each other's halves. Finalize's pair logic expects a two-cell plant's halves as consecutive writes of one bucket run; PlacePlant emits them so.
- **The replay re-checks vertical support only;** sugar cane's water and cactus's sides are Decorate-time checks.
- **The golden area does not exercise the live-heightmap raise** (mutation 6 leaves `Decoration.Golden` passing); `Plants.LiveHeightmaps` is what catches it.
- **Test values for patches come from scout I's analytic counts,** with bounds of about 4.5 standard errors at 2,000 origins; a loosened bound needs its own mutation (Nyx's condition 2).
- **Merges under you:** T2 (torch data) fast-forwarded into the main folder during part A's wrap-up; rebuild after any merge before a run.

## 6. Questions for Jesus (from scout I's section 9; recommendations as sent)

1. **The seven S11 / S12 placed features S8's types complete:** recommend (b), an owner list keeps them off until S11 and S12 (built as `bLaterPlants`, default off).
2. **The five modifiers with no S8 user** (surface_water_depth_filter, environment_scan, random_chance, cuboid, randomly_selected), plus the unused predicates and the coral providers: recommend (b), each to its first user.
3. **Multiface reach:** recommend (a), clamp the search to the 3x3 window.
4. **Session size:** done as recommended (a): one S8 with a cut line, the rest in S8b.
5. **Sturdy faces** (S7's question): part A counts full cubes as sturdy (glass included, leaves not, per scout H), slabs on their flat side and snow of two layers or more on its bottom only. Recommend deciding with S7's answer; no test pins leaves, mud, soul sand or eight-layer snow.
6. **Plant rules taken from the wiki, not measured:** leaf litter and sea pickles need a sturdy top; carpets need anything but air below; red shrub follows supports_vegetation; mushrooms ignore light during generation. Recommend a probe only if a shot shows one wrong.

## 7. Known issues, each with when it is due

| Issue | Due |
|---|---|
| Decorate with plants may exceed its 0.4 ms allowance (busy-machine look: about 1.2 ms per column, plants on) | S8b's gate |
| Replay checks only the vertical support (a neighbour's write beside a cactus or cane is not re-checked) | Recorded; revisit if a shot shows it |
| `Finalize`'s refused-write count is approximate when two features place the identical two-cell plant in one cell | Recorded (stats only) |
| Kelp trimming: scout I's POOL4 probe gave a free run one cell shorter than our reading | Re-measure at depths 3 to 6 if kelp looks wrong |
| Snow layers of two or more are sturdy only on their bottom face | Jesus's sturdy-face answer (question 5) |
| The previous handoff's known issues (L1 contrast, cave-wall shot, amplified at 5 ms, CLAUDE.md's facts-path line) | As listed there |

## 8. Code map: plants

| What | Where |
|---|---|
| Block properties | `Source/UnnamedVoxel/Blocks/VoxelBlockTable.*`; filled in `Source/Unnamed/Voxel/VoxelBlocks.cpp` (`MakeTable`) |
| Survival | `Blocks/VoxelPlantSurvival.*` |
| Predicates, providers | `Decoration/VoxelBlockPredicates.*`, `Decoration/VoxelStateProviders.*`, `Decoration/VoxelNumberProviders.*` |
| Features | `Decoration/Features/VoxelPatchFeatures.*`, `VoxelSelectorFeatures.h` |
| Compile, run, replay | `Decoration/VoxelDecorator.*` (`CompilePlant`, `CompilePlaced`, `CompileModifiers`, `RunModifiers`, `PlacePlant`, `Finalize`) |
| Live heightmaps | `Decoration/VoxelDecorationContext.*` |
| Switch | `Source/Unnamed/Voxel/VoxelConsole.*` (`voxel.Decoration.Plants`), `VoxelWorldSubsystem.*`, `VoxelModelBench.cpp` |
| Tests | `Source/Unnamed/Tests/VoxelPlantTests.cpp` (Properties, Survival, LiveHeightmaps, PatchCounts, Columns, Replay); `VoxelDecorationTests.cpp` (Golden both sides, Distribution, mutations 5 and 6); `VoxelMeshDigestTests.cpp` (both sides) |

## 9. What worked and what failed

**Worked; repeat it:**
- **Plants off as an option, asserted in tests from the start:** every golden kept its old value on one side, so the new side could be recorded with confidence.
- **Replaying real writes against a changed snapshot** (`Plants.Replay`): a black-box test of the merge rules with no hand-made logs.
- **Re-deriving a footprint from the probe's numbers:** the review's "radius + 1" was confirmed from scout I's measured 69 blocks and 3.6 to 6.4 reach, not from memory of vanilla code.

**Failed first; avoid it:**
- **Rejecting a weighted_list entry that is not a constant:** S11's data nests uniforms in it, and `Providers.Distribution` caught it.
- **A namespace-scoped forward declaration** (`struct FVoxelFeatureValue` inside a namespace) made a second type; declare it at file scope.

## 10. Commands and tokens

```powershell
& C:\dev\sandbox\phase5s2\build.ps1 -NoUnity                         # both sides of a gate alike
& C:\dev\sandbox\phase6s2\exec.ps1 -Cmds 'voxel.Decoration.Plants 0, voxel.Bench Decorate 4 overworld, voxel.Decoration.Plants 1, voxel.Bench Decorate 4 overworld' -Name gate
& C:\dev\sandbox\phase6s2\exec.ps1 -Cmds 'voxel.Info' -Name x -Tests 'Unnamed.Voxel.Plants+Unnamed.Voxel.Decoration.Golden+Unnamed.Voxel.Mesher.Digest' -Grep ZZZ
& C:\dev\sandbox\phase6s2\exec.ps1 -Cmds 'voxel.Decoration.Plants 1, voxel.Bench Columns 8, voxel.Bench Columns 8 large_biomes, voxel.Bench Columns 8 amplified' -Name r8 -Grep 'Block hashes'
& Scripts\TestVoxel.ps1                                              # 101 suites
& C:\dev\sandbox\phase5s6\measure.ps1 -Session <id>
```

**Part A's cost** (session `910f2698-f49c-5138-a024-d52e8bbe612a`):

| Agent | Peak context | Processed | Output |
|---|---|---|---|
| Lead (Opus 5.5) | about 500k at the handoff (171k after reading; 317k at the 300k case, which Nyx accepted; wrap-up from about 440k) | 46.0M (45.3M cache reads) | 246k |
| Pre-commit review A, decoration (Sonnet 5.5) | 124.1k (briefed to stop near 150k) | 0.78M | 5.1k |
| Pre-commit review B, blocks, game, tests (Sonnet 5.5) | 96.0k | 0.63M | 1.3k |

## 11. The short prompt for S8b

> Voxel world, main lane: phase 6's S8b, the plants' gate, evidence and docs (S8 part A, plants and patches, is committed).
>
> Read `C:\dev\projects\Unnamed\Docs\Records\2026-10-10-Voxel-Handoff-Phase6-S8b.md` first; its section 1 is the whole reading list (with line ranges), so keep to it.
>
> Do section 4 in order: the timed gate and the flight first, in a quiet window asked of the manager, then the evidence shots and `Docs/VoxelWorld.md`; part B only for the questions Jesus has answered. Send the manager a short plan (files and why) before building. Report as the handoff's section 2 says, and measure your context after every build, test run and subagent result. End with a handoff into the next session and do not start it.
