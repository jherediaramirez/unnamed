# Voxel world handoff: phase 6, into S9d (the ReadFeatures crash fix, the decorators' cost, then fallen trees and huge mushrooms) - October 10, 2026

Read this first if you are picking up the voxel world's main lane in a new chat. It replaces [the S9c handoff](2026-10-10-Voxel-Handoff-Phase6-S9c.md) as the main lane's entry point. Facts were true at this handoff's parent commit; check `git status -uno`, `git log -5` and newer records before trusting them.

**The last session** was S9c ([the S9c record](2026-10-10-Voxel-Phase6-S9c.md), code commit `8ea0414`). It built nine of the eleven tree decorators, refit and switched on mega spruce and mega pine, and added calibrated decoration checks to the tree test. Fallen trees, huge mushrooms and `attached_to_leaves` moved here. Its bench gate went over: Decorate with trees is +0.687 ms on overworld against the 0.4 allowance, and the flight's holes grew about 4x. Cutting that cost comes before any new decorator.

## 1. Read these, and only these

Budget: about 100k of reading. The lead reads no file over about 300 lines whole; give subagents the big ones.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md` and the project's [CLAUDE.md](../../CLAUDE.md).
2. **This handoff**, whole.
3. **[The S9c record](2026-10-10-Voxel-Phase6-S9c.md):** sections 1 (the decorators, lines 5 to 24), 3 (test rules, lines 39 to 53), 6 (the gate and the flight, lines 81 to 98) and 7 (known issues, from line 99).
4. **[Docs/VoxelWorld.md](../VoxelWorld.md):** lines 619 to 678 ("Trees (phase 6, S9a)", "Big trees and the streaming cost (S9b)", "Tree decorators and the giant spruces (S9c)"), the `Unnamed.Voxel.Trees.*` row (Grep it, near line 1139) and the "Trees (S9a)" known gaps (Grep `- **Trees (S9a):**`).
5. **The crash (step 1):** [the S10 record](2026-10-10-Voxel-Phase6-S10.md) section 8, lines 160 to 170 (Grep `ReadFeatures`: the dump, `VoxelFeatureReader.cpp:1121`), and [the items S5 handoff](2026-10-10-Items-Handoff-S5.md) row S3-e (line 124).
6. **[Scout J's doc](2026-10-10-Voxel-Phase6-Scout-J-Trees.md):** lines 265 to 295 (decorators and other big vegetation) and 296 to 340 (oracles and mutations).
7. **[The phase 6 plan](2026-10-09-Voxel-Phase6-Plan.md):** lines 145 to 172 (3.5, the gate rules) and 430 to 451 (S9).
8. **Code, by range:** `Source/UnnamedVoxel/Decoration/Features/VoxelTreeFeature.h` lines 86 to 300 (`EVoxelTreeDecorator`, `FVoxelTreeDecoratorConfig`, `FVoxelTreeBuilder`'s decoration API, the `VoxelTreeDecorators` namespace); `VoxelTreeDecorators.cpp` by its function list (section 5); the test kit by function (section 6).

## 2. Working agreement with this user

Unchanged from [the S9c handoff](2026-10-10-Voxel-Handoff-Phase6-S9c.md) section 2, with these updates:
- **The manager:** report to whichever manager last messaged you (session_01PuWLpgQipHqcox6dnoviYH at this handoff; S9c had two). Plan with every file first; report A before a commit (`git diff --cached --name-status`, suite counts from runs after the review fixes with the newest staged file older than every run, the source of every changed golden or count); report B after.
- **Flights go inside quiet windows** (Jesus's approval of the Efficiency Manager's audit, October 10). Gates and flights run in a window the manager grants; the Playground probe and create-world stay outside it.
- **Fast-forwards under you:** on "FF incoming", let runs in flight end, close every editor started from the main folder, reply "clear", and relay a pause to your agents (SendMessage). After "landed", the next build is `-NoUnity`. S9c had four (water W1/DN1, sky DN0, carvers S10, items S4).
- **Holds:** "QUIET WINDOW HOLD" means finish the step, reply "held" and start nothing heavy; relay it to your agents.
- **Subagents:** S9c's eight Opus agents ran 105k to 209k against stops of 100k to 120k; give each one deliverable, read lists by range and notes early. Sonnet worked well for mechanical work (re-records 54k, photos 101k, two reviewers 95k and 99k). Subagents cannot write report files (Write refuses them): ask for findings as text and save them yourself. C++ agents run one at a time (they build the same tree); Python-only fits can run beside them.
- **Builds:** after restoring a file, set its LastWriteTime to now and build `-NoUnity` (an old time makes UBT skip it). A unity build merges anonymous namespaces across a module: give file-local names unique prefixes (S9c's `DecoratorFail` clashed with VoxelDecorator.cpp's).
- **`exec.ps1`** separates console commands with commas, not semicolons (a semicolon drops the cvar silently).
- **Context:** measure with `C:\dev\sandbox\phase5s6\measure.ps1 -Session <id>`. S9c's lead: 88k after reading, 237k at the framework, 302k at D3, 424k at report A, and 454k at this handoff.

## 3. Where things stand

| Phase | Scope | Status |
|---|---|---|
| 6 S9a, S9b | Tree core; streaming cost with trees; dark oak, pale oak, mega jungle | Done |
| 6 S9c | Nine tree decorators; mega spruce and mega pine; the decoration test kit | **Done** (`8ea0414`), gate over (below) |
| 6 S9d | The crash fix; the decorators' cost; fallen trees and huge mushrooms; then mangroves (with `attached_to_leaves`), azalea, bamboo, vines, forest rock | **Next (main lane)** |
| 6 S10 | Carvers | Done in a side lane (`voxel.Carvers` default 0); S11 turns them on |
| 6 S11, S12 | Underground features, ocean and cold | |

## 4. What to do, in order

1. **The ReadFeatures crash fix: S9d's first commit** (the manager's call, out of the small-fixes bundle). An intermittent access violation hits `VoxelWorldgen::ReadFeatures`' `ParallelFor` file preload (`LoadFileToString` / `ParseWorldgenJson`, `VoxelFeatureReader.cpp:1121`) in about 1 of 4 or 5 full native runs. The dumps are in the S10 record and the items S5 handoff. It moves no hashes: prove it with the radius-8 hashes and the goldens unchanged, and with several full native runs.
2. **The decorators' cost, before any new decorator** (the manager's call G). Add a per-decorator breakdown to `voxel.Bench` (`Source/UnnamedVoxel/VoxelBench.cpp`; time each decorator type inside `VoxelTreeDecorators::Place`, or count calls and cells). Then make targeted cuts. Likely suspects: leaf litter's 246 tries per tree on common forest trees, leave_vine's per-leaf-side draws on jungle trees, and the pale moss patch. Keep every draw and output identical, so the probe lines and goldens do not move; prove it with the guard and the goldens. Gate: Decorate with trees at most 0.4 ms per column on every preset (`gate9.ps1` in a quiet window), then the flight in a window, against S9c's figures.
3. **Fallen trees and huge mushrooms** (D4, designed in section 7), with `attached_to_logs`.
4. **Then, if the session's budget allows** (or S9e, open question 1): mangroves (the root placer, upwards_branching) with `attached_to_leaves`, azalea (bending) and `root_system`, bamboo, the `vines` feature, forest rock (`block_blob`).
5. **Re-records** (`Decoration.Golden`'s third case, `Mesher.Digest` trees on, radius 8; the trees-off controls must reproduce S8 part B's), **photos** (S9d's list below), **the gate**, the Sonnet review, e2e, report A.
- **Photo list carried from S9c:** a low podzol shot on flat old growth ground with a trees-off pair; a close trunk shot for cocoa and shelf mushrooms; fallen trees and huge mushrooms in a dark forest and a forest.

## 5. Code map

| Piece | Where |
|---|---|
| Decorator data, builder API | `Decoration/Features/VoxelTreeFeature.h` (`EVoxelTreeDecorator`, `FVoxelTreeDecoratorConfig`, `FVoxelTreeBuilder::SetDecoration`, `ReplaceWithDecoration`, `Drop`, `GetCells`, `IsAir`, `GetKind`), `VoxelTreeFeature.cpp` (`Compile`: decorators and reach; `Place`: far leaves dropped, decorators, emission) |
| Decorators | `Decoration/Features/VoxelTreeDecorators.cpp`: `TreeDecoratorTypes` (31), `PlaceAlterGround` (71), `PlaceBeehive` (127), `PlaceCocoa` (153), `PlaceTrunkVine` (176), `PlaceLeaveVine` (194), `PlaceOnGround` (240), `PlaceShelfMushroom` (291), `PlacePaleMoss` (407, with `PlacePaleHangingMoss` and `PlacePaleMossPlant`), `PlaceCreakingHeart` (476), `Compile` (515), `GetReach` (653), `Place` (710) |
| Giant crown | `VoxelFoliagePlacers.cpp` (`MegaPineRadii`, `PlaceMegaPine`); `VoxelTrunkPlacers::FindType` (giant on) |
| Selector stubs | `VoxelDecorator.cpp` (`TakeStub`'s `LaterTypes`: fallen_tree, huge_brown_mushroom, huge_red_mushroom) |
| Tests | `Source/Unnamed/Tests/VoxelTreeTests.cpp` (the test kit, section 6); goldens in `VoxelDecorationTests.cpp` (`TreeHashes`, `TreeWrites`) and `VoxelMeshDigestTests.cpp` (`PlantGolden`) |
| Fits and refits | `C:\dev\sandbox\phase6s9\s9c\`: `alter\` (fit2.py), `megarefit\` (REFIT.md), `d2\`, `d3\`, `d3b\` (each agent's NOTES.md and fit scripts), `review\findings.md`, `decisions.md` (every call), `cmplines.py` (the probe-line guard) |

## 6. The test kit

**S9d, S11 and S12 reuse these; ask the manager before inventing new statistics.** They are file-local today, in `VoxelTreeTests.cpp`'s anonymous namespace. S9d moves them to a shared header (for example `Source/Unnamed/Tests/VoxelProbeStats.h`) when it first needs them outside the tree test, as a pure move that changes no probe line.

| Rule | Where (VoxelTreeTests.cpp) | How to call | Calibrated false-fail rate | Mutations that must fail |
|---|---|---|---|---|
| Exact two-sided binomial | `BinomialTwoSided(K, N, P)` (322) | the probability of an outcome no more likely than K | exact | |
| Leaf occupancy, Bonferroni | `GetOccupancyP` (636) | per cell, the probe's share against both samples pooled; fails below 0.01 | S9a: about 1 probe in 100 | 2 (radius + 1) |
| Means (logs, leaves, cells) | `CheckMeans` (Grep) | fails when over 5% off and \|z\| > 3 | S9a | 1 (base_height + 1) |
| Decoration means, per probe | `CheckDecorations` (729) | per block, ours against the probe's `mean_per_tree`. At 1 or more per tree: fails when over 5% off and \|z\| > 3 (our deviation; a Poisson stand-in when ours has none). Under 1 per tree, both means: exact conditional binomial on the totals, a ~ Bin(a + b, n1 / (n1 + n2)), with both counts divided by D = max(1, var / mean) (quasi-Poisson), fails when p < 0.01 and over 5% off; D is shown on the line; `GetDecorationPZ` (699) turns p into a signed z for the line | at most 21 of 1,000 for one probe (birch_bees_0002); some probe in 161 of 1,000 runs | 6 (cocoa zeroed: -100%, z -16.2); litter tries halved (-38 to -41%); pale moss ground zeroed; the heart's enclosure dropped |
| Pooled decoration check | `PoolDecorationZ` (827), `IsPooledFailure` (823) | Stouffer: sum of the per-probe z over sqrt(k), per block, after every probe; a probe joins only where ours expects the block at least once in its trees; k 1 never fails; the bound is 3.5 | 8 of 1,000 runs | 5 (pooled +18.78); not caught: 3 (beehive x2, pooled +2.91: 100-tree probes cannot resolve it) |
| Block-set exemption | `GetDecoratorBlocks` (794), in `CompareTrees` (845) | a block the probe never saw passes only when the tree's built decorators can write it, and is then z-tested against 0 | | 5 (beehive writes oak_planks: fails on 13 probes) |
| Probe-none guard | `CompareTrees` ("placed none against some", 863) | a probe that placed none fails ours only when (1 - q)^n < 0.01 | by construction: 0.01 | 4 (giant base_height - 5: ceil15 at 33%, (1 - q)^n 0.0018) |
| Every-cell reach guard | `CompareTrees` ("reach of every cell", 952) | fails when over 5% of our trees reach beyond the probe's `max_reach_h_any_cell`; the logs-and-leaves reach stays exact against the probe's | 0.95^100 = 0.006 per probe | 7 (litter radius + 2: 100% of trees beyond 4) |
| Small probes | `IsSmallProbe` (85) | under 50 tries: the means, decorations and occupancy are skipped (ceilings) | | |
| Calibration harness | `TreeTestCalibration` (43), `MakeSampleProbe` (980), the block after the probes in `RunTreeProbes` (1067) | set the constant to 1000, build, run `& C:\dev\sandbox\phase6s9\runtests-long.ps1 -Filter 'Unnamed.Voxel.Trees.Shapes' -Name cal`, read the "calibration ..." Info lines, set it back to 0 | | |
| Mutations | `TreeTestMutation` (40), `ApplyTreeTestMutation` (1003) | set the constant, build, run, see it fail, set it back to 0 | | 1 to 7 above |
| Probe-line guard | `C:\dev\sandbox\phase6s9\s9c\cmplines.py <old run> <new run>` | compares every probe line with the decoration fields stripped; "diff 0" means no tree's shape moved | | |

- **Calibration's limit (a known issue):** it scores 1,000 self-samples against one fixed 2,000-tree run (one seed), which inflates the counts; a second seed is untested.
- **The review's unfixed lows:** small-count rounding after D, and the "left out" excuse, which covers any block when any decorator is skipped (no tested tree skips one after S9c; mangroves will).

## 7. D4: fallen trees and huge mushrooms (design notes)

- **The data:** `fallen_tree` (e.g. `data/minecraft/worldgen/feature/fallen_oak_tree.json`) has `log_length` (uniform), `trunk_provider` (axis y), `stump_decorators` ([trunk_vine] on oak and jungle) and `log_decorators` ([attached_to_logs: a weighted mushroom provider, directions up, probability 0.1]; fallen poplar has shelf_mushroom at 0.8). `huge_red_mushroom` and `huge_brown_mushroom` have `cap_provider`, `stem_provider` and `can_place_on` (a matching_block_tag predicate).
- **The probes** (`facts/feature_probes/tree_fallen_*.json`, `tree_huge_*.json`; scout J lines 284 to 288):
  - Fallen trees: the stump is at the origin and the log lies along x or z at dy 0; decorations sit at dy +1. The reach equals the largest log_length: 7, 7, 8, 10, 11 and 15. Per tree: stump vines about 2.7 (oak, jungle), mushrooms 0.1 to 0.44, fallen poplar's shelf mushrooms 1.05.
  - Huge mushrooms: always 45 cap blocks; stems 5.37 (red) and 5.64 (brown); red reach 2, brown reach 3; up to dy 12. The wiki gives heights 5 to 7, with a 1 in 12 chance of 9, 11 or 13. The examples give every cap face state.
- **Where:** a new `Decoration/Features/VoxelBigVegetation.h/.cpp` (the plan's name). Compile them in VoxelDecorator.cpp's feature dispatch, take them out of `TakeStub`'s `LaterTypes`, and keep `Describe`'s counts.
- **Reuse the decorators:** build a fallen tree on an `FVoxelTreeBuilder` (logs along the axis, `GetCells(Log)` sorted), so trunk_vine and shelf_mushroom run unchanged; add `attached_to_logs` to `VoxelTreeDecorators.cpp`. `FVoxelTreeConfig` may need a stump/log decorator split, or a small fallen-tree config with two decorator lists.
- **The new test path:** `Trees.Shapes` reads every `tree_*.json`, but `FTreeTestKit::Compile` compiles only tree features (others come back as a skip). Add `CompileBig` and `SimulateBig`, measured like `AddTree`:
  - fallen trees: logs per tree, the log's axis share, the stump, decorations via `CheckDecorations`, reach via the every-cell guard;
  - huge mushrooms: cap blocks exactly 45, stems against the probe's mean, reach;
  - the "probes run" count rises by 8 (6 fallen, 2 mushrooms).
  - Scout J's mutation "fallen log one longer" must fail the reach for fallen super birch: 16 against 15; with the 5% guard it fails as long as more than 5% of trees take the longest log.
- **Gate:** fallen trees and huge mushrooms add Decorate time, so step 2's cuts must leave room.

## 8. Settled decisions

- **Jesus (October 10):** trees on by default; vanilla's grand trees first, with no shape or weight changes; no AMPLIFIED speed-up session for now. Flights inside quiet windows (the Efficiency Manager's audit).
- **The managers (S9c), every call with its numbers in `C:\dev\sandbox\phase6s9\s9c\decisions.md` and the record's section 5:**
  - The ceil15 guard.
  - The block-set exemption, tighter.
  - The pooled check with T's conditions, at a bound of 3.5.
  - The exact rare-block test with D, D logged.
  - "Beehive x2" not catchable.
  - Mega jungle's 75% trunk_vine kept; if a later probe shows about 0.70 per side, independent of which sides count as open, switch to the probe.
  - Pale moss's 0.75 kept.
  - attached_to_leaves, fallen trees and huge mushrooms to S9d.
  - The photo set accepted.
  - G: commit over the gate, with the cost cuts as S9d's step 2.
  - The crash fix as S9d's first commit.
  - The test kit reused, with no new statistics without the manager.
- **Earlier:** S9a's and S9b's (the earlier handoffs' section 6).

## 9. Known issues, each with when it is due

| Issue | Due |
|---|---|
| ReadFeatures ParallelFor access violation (about 1 in 4 or 5 full runs) | S9d, step 1 |
| Decorate with trees +0.687 ms on overworld against the 0.4 allowance; Finalize with trees 0.453 | S9d, step 2 |
| Flight holes in 228 frames (at most 16 columns) against S9b's 40 and 42 (quads +9.7%) | S9d step 2, then a streaming session (S9b's levers: RealtimeMesh's commit off the game thread or split applies; stream trimming) |
| Fallen trees, huge mushrooms, mangroves with attached_to_leaves, azalea, bamboo, vines, forest rock | S9d (or S9e) |
| Mega jungle vines +7.7% (z +2.8); pale moss ground +5% (pooled z up to +2.75) | When a probe settles them |
| Calibration on one seed; the review's unfixed lows (section 6) | A test session, or whoever touches them |
| Ring spreads (litter, beehive radius) only checked in Python; alter_ground's vertical search and the litter's rules fitted on flat ground | When probed on slopes |
| Pine crown rows C 4, 5 and 6 odd each have an unseparated alternative | A wider pine probe |
| Dark forest leaves bright green, not the dark forest tint (also in S9b) | Small fixes |
| Finalize over 0.3 ms with trees off too; amplified's whole column 5.29 ms | A streaming or small-fixes session; FYI for Jesus |
| The earlier handoffs' issues (glow lichen near-white, saplings' survival stand-in, review R2/R3/R5/R6) | As listed there |

## 10. What worked, what did not

- **Worked:**
  - The lead writing the framework itself (builder API, emission, the decoration check) before any agent, then one decorator group per agent on that API. Every agent's guard was "every earlier probe line byte-identical", and every one held.
  - Fitting in Python from `profile.decorators` (totals by dy and Chebyshev radius) and the examples' full states, then porting: alter_ground's 168-family search and the mega pine table both ported first try.
  - Calibrating every new test rule on 1,000 self-samples before trusting it. It caught three miscalibrated rules: the plain pooled sum (70/1000), the normal z on rare blocks (587/1000) and the clumped blocks without D (96/1000).
  - A temporary switch-on of an off placer, to test a decorator that only it carries (giant on for alter_ground), restored by hash.
  - Two Sonnet reviewers split by path list: they found a real pale moss bug and two test-statistics flaws.
- **Did not:**
  - Agents' token stops: most ran 40 to 90% over.
  - Rules that compare our max of 2,000 trees with the probe's max of 100 (the every-cell reach), or treat tiny counts as normal: both needed calibrated guards.
  - Heredoc and Python string escapes for Windows paths: `\r` in a path became a carriage return (fixed with PowerShell).
  - The decorators' cost was not measured until the gate; measure Decorate per decorator group as each lands.

## 11. Tokens (session `4f5975c6-698b-587e-8972-9a595cf95808`, measured)

| Agent | Peak context | Processed | Output |
|---|---|---|---|
| Lead (Opus 5.5) | 454k at this handoff (88k after reading, 237k at the framework, 302k at D3, 424k at report A) | 62.1M | 216k |
| R, mega spruce/pine refit (Opus, Python) | 208.6k | 5.52M | 0.6k |
| D2, bee/cocoa/vines (Opus) | 160.2k | 6.68M | 4.5k |
| P, giant port (Opus) | 116.7k | 4.36M | 7.1k |
| T, block-set and pooled rules (Opus) | 139.6k | 3.92M | 6.8k |
| D3, litter and shelf mushroom (Opus) | 152.2k | 5.22M | 2.0k |
| T2, exact rare-block rule (Opus) | 105.2k | 2.83M | 11.2k |
| D3b, pale moss and creaking heart (Opus) | 183.7k | 8.63M | 1.5k |
| Re-records and suites (Sonnet) | 53.7k | 0.48M | 0.6k |
| Photos (Sonnet) | 99.9k | 1.77M | 1.5k |
| Review part 1 (Sonnet) | 99.1k | 0.44M | 14.2k |
| Review part 2 (Sonnet) | 95.2k | 0.76M | 7.6k |
| Review fixes (Opus) | 138.5k | 5.92M | 6.0k |

## 12. Evidence (best first)

- The decorators' probe lines: `C:\dev\sandbox\phase6s9\runs\s9c-final-20261010-182449`, then `fix-1-20261010-185440` (after the review). The calibration: `fix-calib-20261010-185841`. Mutations: `s9c-mutalter-*`, `p-mut4-*`, `p-mutcrown-*`, `t-mut3-*`, `f-mut5-*`, `t2-mut6-*`, `fix-mut7-*`, `d3-mut-*`, `d3b-mutground-*`, `d3b-mutheart-*`.
- The gate: `C:\dev\sandbox\phase6s9\s9c\gate9-r1.txt` to `-r3.txt` (`..\s9b\gatesum9.py`). The flight: `Tests/Evidence/2026-10-10/VoxelFlight-20261010-192809`.
- Photos: `Tests/Evidence/2026-10-10/S9c-Decorators/contact-sheet.png` (best jungle-r3, palegarden-r2, dappled-r1, oldgrowth-r3; `notes.md`).
- Suites: `Tests/Evidence/2026-10-10/VoxelAutomation-20261010-190748` (147/147), `C:\dev\sandbox\phase6s9\runs\native-full-fix-20261010-191015` (227/227); e2e `Worlds/Create-1791674054177`, `Playground-all/Runtime-20261010-193042`.
- The refit: `C:\dev\sandbox\phase6s9\s9c\megarefit\REFIT.md`. The review: `C:\dev\sandbox\phase6s9\s9c\review\findings.md`. Every call: `C:\dev\sandbox\phase6s9\s9c\decisions.md`.

## 13. Open questions, with options

1. **S9d's scope:** (a) **recommended**: S9d = the crash fix, the cost cuts and fallen trees with huge mushrooms; S9e = mangroves with attached_to_leaves, azalea, bamboo, vines and forest rock. (b) Everything in S9d.
2. **If the cuts cannot reach 0.4 ms without changing outputs:** (a) **recommended**: report to the manager with the per-decorator figures before changing any draw (a changed draw moves goldens and needs the fits re-checked); (b) accept a documented overage, Jesus's call through the manager.
3. **The flight's holes:** (a) **recommended**: re-fly after the cuts, in a window; if still about 4x S9b's, propose the streaming session (S9b's levers); (b) leave them until the streaming session.

## 14. Commands

```powershell
& C:\dev\sandbox\phase5s2\build.ps1 [-NoUnity]                                   # headless build (-NoUnity after a restore or a fast-forward)
& C:\dev\sandbox\phase6s9\runtests-long.ps1 -Filter 'Unnamed.Voxel.Trees' -Name trees   # tree tests (capture the output; print the first line, Errors, the lines you need)
uv run --no-project --python 3.13 python -I C:\dev\sandbox\phase6s9\s9c\cmplines.py <old run> <new run>   # probe-line guard
& Scripts\TestVoxel.ps1                                                          # 147 voxel suites
& C:\dev\sandbox\phase6s9\runtests-long.ps1 -Filter 'Unnamed' -Name native-full   # every native suite (227)
& C:\dev\sandbox\phase6s2\exec.ps1 -Cmds 'voxel.Decoration.Trees 0,voxel.Bench Columns 8 overworld' -Name r8 -Grep 'Block hashes'   # radius 8 (commands comma-separated)
& C:\dev\sandbox\phase6s9\s9b\e2e1.ps1 -Step create|probe|flight                # e2e under the CEF slot rule (the flight only in a window)
& C:\dev\sandbox\phase6s9\s9b\gate9.ps1 -Round 1                                 # one bench gate round (quiet window); s9b\gatesum9.py to summarize
& C:\dev\sandbox\phase5s6\measure.ps1 -Session <id>
```

## 15. The short prompt for the next session

> Voxel world, main lane: phase 6's S9d. First the ReadFeatures ParallelFor crash fix (its own first commit, no hashes move). Then the tree decorators' cost: S9c went over the gate, with Decorate with trees at +0.687 ms on overworld against 0.4. Add a per-decorator breakdown to voxel.Bench and make targeted cuts that keep every draw and output. Then fallen trees and huge mushrooms. S9c is committed (`8ea0414`): nine decorators, mega spruce and mega pine on, and a calibrated decoration test kit.
>
> Read `C:\dev\projects\Unnamed\Docs\Records\2026-10-10-Voxel-Handoff-Phase6-S9d.md` first; its section 1 is the whole reading list (with line ranges), so keep to it.
>
> Do section 4 in order. Reuse section 6's test kit and ask the manager before inventing new statistics. Send the manager that starts you a short plan (files and why) before building. Report as the handoff's section 2 says, run gates and flights only in quiet windows, and measure your context after every build, test run and subagent result. End with a handoff into the next session and do not start it.
