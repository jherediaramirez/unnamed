# Voxel world, phase 6, S9e part 2: the cost step measured (the cut dropped), the mangrove re-probe, bamboo, forest rock and vines - October 11, 2026

The main lane's S9e part 2 ([the handoff](2026-10-11-Voxel-Handoff-Phase6-S9e2.md)) measured S9e part 1's cost against S9d part 2's build, built an exact early-out for the trees' reads and dropped it (no measured gain), re-probed the mangroves (the accepted tall_mangrove@mudwater probe is an artefact), and put bamboo, forest rock (`block_blob`) and the `vines` feature in the world. The mangroves stay built but off: switching them on needs the re-probe's data in the worldgen reference, part 3's work (section 3).

## 1. The cost step

- **The A/B** (one quiet window, 04:25:38 to 04:43:34; `C:\dev\sandbox\phase6s9\s9e2\gateab.ps1 -Round 1..3`): `voxel.Bench Decorate 4 overworld`, trees off and on, three builds rotated per round: the control (`C:\dev\projects\Unnamed-items` at `e84ef6d`, S9d part 2's voxel code, its DLLs checked as that commit's build and launched read-only), S9e part 1 (`3434261`) and the cut, both rebuilt with `-DisableAdaptiveUnity` and swapped in by their saved, SHA-256-checked binaries (`s9e2\arm-s9e1`, `s9e2\arm-cut`). Background load: mean 3.29 cores (SearchIndexer 1.05, Defender 0.64); the guard's six "intruders" were the control arm's own launches.

| Arm | Decorate trees on - off per round (median) | Trees' stages, ms per Decorate (medians): total / feature size / foliage / decorators / emission |
|---|---|---|
| control (S9d part 2) | +0.162, +0.668, +0.076 (+0.162) | 0.361 / 0.039 / 0.055 / 0.076 / 0.127 |
| S9e part 1 | +0.031, +0.263, +0.744 (+0.263) | 0.387 / 0.043 / 0.057 / 0.080 / 0.132 |
| the cut | +0.476, +0.728, +0.756 (+0.728) | 0.406 / 0.038 / 0.060 / 0.081 / 0.144 |

- **What it says:** the on - off measure swings by up to 0.7 ms between rounds today, so it cannot resolve 0.1 ms. The trees' own stage times are steadier (the leaves' distance, which no arm changes, still spans 0.050 to 0.057). S9e part 1's own increment over S9d part 2 is +0.026 ms in the trees' stages, as its record estimated (0.03 to 0.04); projected on S9d part 2's last measured +0.387, that is about +0.41 against the gate's 0.4, inside the noise. **This window could not resolve the S9 gate** (the manager's call), for Jesus at the phase 6 record; the final window measured it (section 8). Part 1's +0.548 was the machine's level, not S9e.
- **The cut** (S9e part 1's proposal (b)): the tree builder's reads skipped the working copy above a column's live top (air without a read). Proved before measuring: an instrumented build read back every early-out over the three presets' Decorate benches, `Decoration.Golden` and the tree tests, 430,738 of them, 0 not air (`C:\dev\sandbox\phase6s2\runs\s9e2-topz-proof-041744`); the tree lines byte-identical (cmpfull same 75, diff 0) and the hashes unchanged. It measured no gain (0.406 against 0.387 ms): reads above the top land in uniform air sections, already cheap; the trees' time is in the emission (each write into the working copy and its four heightmaps) and the decorators. **Dropped** (the manager's Q1 (a): exact or nothing); `FVoxelDecorationContext::IsAboveTop` stays, for the vines (section 4). Its review (Sonnet, 56.5k): no bug, two nits.
- **The flight** (in the window, on the cut's binaries): 0 errors; holes in 95 frames, at most 11 columns (S9e part 1: 453, at most 34); chunk work worst 1.97 ms, none of 9,621 frames over budget (`Tests/Evidence/2026-10-11/VoxelFlight-20261011-043526`).

## 2. The mangrove re-probe (F4's question)

A Sonnet agent re-ran scout J's tree probe (a copy reading the grid start and the level seed from the environment, `C:\dev\sandbox\phase6s9\s9e2\reprobe\tools\tree_probe_rp.py`; the original untouched) on the probe's own sandbox server: 3 runs of the four mangrove probes, 100 placements each, at grid starts 9216, 25600 and 41997 with seeds 263, 7001 and 90210 (the accepted: 1024, 263). 272 s of java, each run behind the quiet-window gate. Report: `C:\dev\sandbox\phase6s9\s9e2\reprobe\reprobe-report.md`.

| Probe | Placed, accepted / new runs | Leaves per tree, accepted / new | Branch logs per trunk log, accepted / new |
|---|---|---|---|
| mangrove | 100 / 100, 100, 100 | 117 / 128 | 0.444 / 0.524 |
| tall_mangrove | 91 / 92, 91, 91 | 287 / 322 | 0.964 / 1.064 |
| mangrove@mudwater | 89 / 82, 89, 83 | 132 / 129 | 0.550 / 0.552 |
| tall_mangrove@mudwater | 28 / 29, 31, 26 | 375 / 307 | 1.086 / 1.010 |

- tall_mangrove@mudwater's height selection does not reproduce: survivors with a trunk of 6 or less, 10 of 29, 7 of 31 and 9 of 26, against the accepted 0 of 28 (Fisher p 4e-4). Nor does the mud's extra branching (mud against grass, pooled new: 0.552 against 0.524, 1.010 against 1.064; p 0.44 and 0.43). The accepted grass probes ran low. The accepted tall@mud probe is a sampling or grid-seed artefact; the feature has no such rule. Ours gave 283 leaves on it against the accepted 375 (-24%); the new runs' 307 puts it near -8%.
- **The manager's call (a), for part 3:** the 300-tree re-probes join `Reference/minecraft-worldgen-26.3` as new probe files through its own tool path (the accepted files byte-identical, aggregates by additions only, no index shift, `verify_reference.py` passing); the accepted tall@mud probe stays, tested for placement only, citing this report; the fit targets the new or pooled data with no mud rule; `IsMangroveBuilt()` goes on when all four pass.

## 3. What landed

| Piece | Where | Rule (ours, fitted to scout J's probes by F3b, `C:\dev\sandbox\phase6s9\s9e\fit\f3b-part2\part2-report.md`, unless the data or the wiki says) |
|---|---|---|
| bamboo | `VoxelBigVegetation.h/.cpp` `FVoxelBambooConfig`, `CompileBamboo`, `PlaceBamboo` | The wiki: a stalk of 6 to 17 blocks cut short by an obstacle, a podzol disk of radius 1 to 4 at the data's `probability`. Fails where the origin is not air or the block under it is not in `#supports_bamboo`; the stalk grows while the cell is air; each cell's state by its distance from the top (stage 1 and large leaves at the top, large, small, then none: the probes' examples); the disk's cells `dx^2 + dy^2 <= r^2` one below, where the block is in `#beneath_tree_podzol_replaceable`. Draws: NextInt(12), NextFloat, NextInt(4) |
| block_blob (forest rock) | `FVoxelBlockBlobConfig`, `CompileBlockBlob`, `PlaceBlockBlob` | Walks down from the origin to the first cell over `can_place_on` (fails below MinZ + 3, nothing drawn); three stamps of radii NextInt(2) each (the reference's x, y, z: ours X, Z, Y), cells within (0.5 + sum / 3) (1, 5 or 19 cells); the centre moves -1 or 0 per axis (NextFloat < 0.5) after the first two; every cell takes mossy cobblestone (a Force write), but bedrock |
| vines | `FVoxelVinesConfig`, `CompileVines`, `PlaceVine`, `HasNoVineSupport` | No algorithm in the data or the wiki: where the position is air, the first face of up, north, east, south, west whose neighbour fills its cube and has a collision box (the wiki's support, which takes leaves: the manager's Q3) gets the vine, that face alone; else it fails. Draws nothing |
| The vines' early-out | `VoxelDecorator.cpp` (`Prune`), `FVoxelDecorationContext::IsAboveTop` | A vines try whose five support cells are all above their columns' live tops ends before its biome lookup; exact (the bench's early-out on and off hashes equal in all three benches) |
| Dispatch, timing | `EVoxelPlantFeature::Bamboo`, `BlockBlob`, `Vines` under `voxel.Decoration.Trees`; `FVoxelTreeTimes::Bamboo`, `BlockBlob`, `Vines`; `VoxelBench.cpp` | `voxel.Bench Decorate` prints "Big vegetation per Decorate: bamboo ..., forest rock ..., vines ..." with microseconds per try (the "Trees per Decorate" total includes them) |

## 4. Tests

| Rule | Where | Mutation that must fail |
|---|---|---|
| Bamboo against both probes: placed rate, the top's histogram, the share of stalks with podzol (exact binomial), podzol per stalk (CheckMeans' rule), reach and blocks exactly, each example's stalk cell for cell | `CompareBamboo` (`VoxelTreeTests.cpp`) | 12, every stalk one taller: both fail the top histogram (`C:\dev\sandbox\phase6s9\runs\s9e2-mut12-20261011-045054`) |
| Forest rock: placed rate, cells per rock and their histogram (pooled in threes, the review), the box's extremes on every axis, reach and blocks exactly | `CompareBlob` | 13, the centre never moves: cells histogram, box extremes and reach fail (`runs\s9e2-mut13-20261011-045142`) |
| Vines: F3b's nine wall cases on an empty column, and whether the early-out skips each | `Unnamed.Voxel.Trees.Vines` | 14, the faces in reverse order: cases c and i fail (`runs\s9e2-mut14-20261011-045247`) |
| Probes run | `FVoxelTreeShapesTest` | 46 (43 + the two bamboos + forest_rock; the mangroves skip at their root placer) |

- bamboo_some_podzol: placed 100%, top p 0.13, podzol stalks 19.1%/15.0% (p 0.37), podzol per stalk 4.63/2.75 (+68%, z +2.1: F3b's flagged line, within the rule), examples exact 2/2. bamboo_no_podzol: top p 0.32, examples 2/2. forest_rock: cells 12.46/11.90 (z +0.7), the box (-3, -3, -3) to (1, 1, 1) exactly, reach 3/3.
- Every earlier probe line unchanged (cmpfull against S9e part 1's `s9e-off1`: same 75, the 3 differing lines the new probes; the huge mushrooms' example check moved into `MatchExample`).

## 5. Re-records (trees on; every trees-off control unchanged)

| Golden | Old (S9e part 1) | New | Source |
|---|---|---|---|
| `Decoration.Golden` case 3, large_biomes | `0xff1a7eb180110302`, 793,370 writes | `0x5e72b754b1f20fa6`, 793,454 (bamboo or vines: model blocks) | `C:\dev\sandbox\phase6s9\runs\s9e2-gold1-20261011-050606` |
| case 3, overworld / amplified | `0x95645bbf53f73c12` / `0x98ae73a412fa50ca` | unchanged | |
| `Mesher.Digest` large_biomes models on / off | `0x0ea42a420b0a2f83` / `0x8b29db68a9ad618d` | `0x95edc2ccd204faaa` / unchanged | same |
| Radius 8 overworld / large_biomes / amplified | `0x1681a6e69daab32e` / `0xe2a8bc32dc58d73c` / `0xfbb824583b2844c8` | `0xa96b931bfbc4c39b` / unchanged / `0x5cae8edc58ba77d5` (both load orders equal, 0 columns differ) | `C:\dev\sandbox\phase6s2\runs\r8-s9e2-050825` |

The new values passed on the same run (Golden and Digest after the edit). The lane's hash file: `C:\dev\sandbox\phase6s9\s9e2\expected-hashes-s9e2.txt`.

## 6. Cost (indicative, outside a window; `C:\dev\sandbox\phase6s9\s9e2\bench.ps1`)

- The overworld gate area is a bamboo jungle: bamboo 0.053 ms a Decorate (33.7 tries, 1.6 us each), vines 0.011 ms (132.6 tries reaching the feature, 0.09 us each); the trees fall to 0.241 ms (2.37 placed of 6.90 tries), as stalks now stand where trees would root (`C:\dev\sandbox\phase6s2\runs\s9e2-cost-gate-050401`, CPU 93%).
- A jungle (-88, -24): bamboo 0.014 ms (7.7 tries of `bamboo_light`), vines 0.011 ms (`s9e2-cost-jungle-050429`). An old-growth spruce taiga (-2392, 2216): forest rock 0.002 ms (0.78 tries, 3.0 us each) (`s9e2-cost-taiga-050455`).
- Lush caves (`classic_vines_cave_feature`, 256 tries a column): `voxel.FindBiome lush_caves` found none within 6,000 blocks (it searches the surface); unmeasured.

## 7. The look call

Photos by a Sonnet agent, the manager's call: both accepted. Bamboo, `Tests/Evidence/2026-10-11/S9e2-Bamboo/bamboo-on` against `bamboo-off` (`voxel.Decoration.Trees 0`, the same camera): a dense lit grove with leaf tufts at the tops and podzol and dirt patches, against bare grass. Forest rock, `S9e2-ForestRock/rock-on4` against `rock-off4` (an old-growth pine taiga at 1992, 1992): a plus of mossy cobblestone half set into a coarse-dirt slope among ferns, against the same slope bare. The manager measured the rock's brightness: the scene's sunlit exposure, not the block (a global brightness note for Jesus's list). Vines: no shot (the unit test covers them).

## 8. The final window and the suites

- **Window** 05:50:34 to 06:01:49 (no intruders; background load mean 2.04 cores, SearchIndexer 1.04), final code on `0c8b458` (W3b's base): `C:\dev\sandbox\phase6s9\s9e2\gatefinal.ps1 -Round 1..3`, `gatefinal-summary.txt`.
- **The S9 gate as measured** (gate9's presets, Decorate trees on - off, medians of 3): overworld +0.249 (+0.130, +0.249, +0.540), large_biomes +0.117 (+0.297, +0.117, +0.036), amplified +0.194 (+0.186, +0.194, +0.489): all under 0.4, with the overworld swinging 0.41 between rounds, at a background load of 2.04 cores. S9e part 1's +0.548 was the machine's level that night.
- **The trees' stage-time A/B** (overworld, final against the S9d part 2 control, every stage including bamboo, rock and vines): final 0.152 ms against 0.340 (rounds -0.367, -0.156, -0.154; median -0.156). Not a speed-up: the gate area is a bamboo jungle and the stalks take the trees' spots (6.90 tree tries, about 2.4 placed, against 4.40 and 3.19). Bamboo 0.030 to 0.042 ms, vines 0.008 to 0.009.
- **The flight** (final code): 0 errors; holes in 159 frames, at most 12 columns; chunk work worst 4.91 ms, 3 of 9,426 frames over budget (the earlier flight, before W3b's fluids default: 0); quads 2,185,115 (`Tests/Evidence/2026-10-11/VoxelFlight-20261011-055934`).
- **Suites** (after the review fix, on the W3b base): `Scripts\RunChecks.ps1 -Profile final -ExpectedHashes C:\dev\sandbox\phase6s9\s9e2\expected-hashes-s9e2.txt`, green in 20 min (`Tests/Evidence/2026-10-11/Checks-final-20261011-053043/summary.txt`): native 260 of 260, 0 crashes; TestVoxel 167; hashes 9 of 9 equal (the re-records hold under `voxel.Fluids` 1); the Playground runtime (119, 81, 27 checks); create-world 32; npm 61; source-check; the verifiers. An earlier run was stopped when W3b landed under it and is not used.

## 9. The manager's calls

- The plan approved (Q1 (a) overworld only for the A/B, Q2 (a) the re-probe data in the sandbox first, Q3 (a) its scope, Q4 (a) F3b's vines rule); the conditions: both main-folder arms differing only by the cut, the window for gate launches and the flight alone.
- The cut dropped, `IsAboveTop` kept for the vines (Q1 (a)); the gate recorded as not resolved (Q2); leaves support a vine, as the wiki says (Q3 (a)).
- The 300k case accepted: the three features here, the mangrove fit and switch-on to part 3; lines at 450k, 500k and 700k.
- The mangroves (a): the re-probes into the reference as new files, the accepted tall@mud probe tested for placement only.
- The look calls accepted (bamboo, forest rock).

## 10. Commits

- **Review:** a Sonnet reviewer over 1,063 diff lines (peak 116.9k, over its brief): no definite bug. Fixed: the forest rock's cell histogram pooled in threes (the helper does not pool; 100 probe rocks leave bins of one), with mutation 13 re-run. Kept, with reasons: the podzol line's z +2.1 (F3b's flagged margin, within the rule); `TestTrue`'s message reading `Error` before the call (the file's pattern); the box test's need of 2,000 rocks (`TreeTestCount` pinned); the bench's trees total including bamboo, rock and vines (what the A/B compares).
- **Commits:** `fd23e85` (the code, tests and re-records), then this record, VoxelWorld.md, the S9e part 3 handoff and the records index.

## 11. Known issues

| Issue | Due |
|---|---|
| The mangroves off; the accepted tall_mangrove@mudwater probe an artefact | S9e part 3 (the re-probes into the reference, the fit, the switch-on) |
| The S9 gate not resolved at today's noise (S9e part 1's +0.026 ms projects to about +0.41) | Jesus, at the phase 6 record |
| bamboo_some_podzol's podzol per stalk +68% (z +2.1, within the rule) | Watch on every tree run |
| The vines' face order and one face per try are ours, unprobed | Optional: a wall probe |
| The lush caves' vines (256 tries a column) unmeasured | S11 |
| 3 of 9,426 flight frames over the chunk budget (4.91 ms worst), first seen after `voxel.Fluids` went on by default | S11's flight: watch |
