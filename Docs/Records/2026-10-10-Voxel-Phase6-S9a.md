# Voxel world phase 6, S9a: the tree core - October 10, 2026

The main lane's S9a: the tree feature in C++ from the S9a handoff's uncompiled draft, the placers of three fitted groups ported, the tests, and two bugs that had kept every tree out of the world. Trees are on by default (Jesus's answer). The big group (dark oak and pale oak, mega spruce and mega pine, mega jungle) moved to S9b as its first job (the manager's call on the budget). [Docs/VoxelWorld.md](../VoxelWorld.md), "Trees (phase 6, S9a)", has the rules and the values; [the S9b handoff](2026-10-10-Voxel-Handoff-Phase6-S9b.md) has what comes next.

## 1. What was built

- **The framework** (the draft, compiled first time in both a unity and a per-file build): `VoxelTreeFeature.h/.cpp` (placer configs by the data's field names, the builder over the working copy, the feature size, the leaves' distance by BFS, `Place`), `VoxelTrunkPlacers.cpp`, `VoxelFoliagePlacers.cpp`, the decorator's tree kind, `surface_water_depth_filter` and `environment_scan`, and `voxel.Decoration.Trees` (default on).
- **Placers ported** (one subagent per group, each against `Trees.Shapes`):
  - **straight + blob** (the draft): oak, birch, super birch, jungle tree, swamp oak and their bee and leaf-litter variants.
  - **simple:** bush (jungle bush), spruce, pine. Passed first try.
  - **branchy:** fancy trunk and foliage (fancy oak, the one "grand" tree built), forking trunk and acacia foliage. Acacia's fork length is uniform 1 to 3: the reference probe rejects the fit report's 10:13:16 weights (p 0.005).
  - **canopy:** cherry and poplar trunks and foliage (cherry, red, orange and yellow poplar). Two needed edits: a `BranchDepth` on the foliage attachment (poplar's foliage stops at its trunk's branch layer), and an untyped `{min_inclusive, max_inclusive}` read as uniform (cherry's `branch_start_offset_from_top` had been dropped silently).
  - **Not built** (S9b first): giant, mega_jungle and dark_oak trunks with mega_pine, jungle and dark_oak foliage (the big group's fits: mega jungle solved; mega spruce and pine, dark oak and pale oak not); bending (azalea) and upwards_branching with the mangrove root placer (not fitted).
- **Fixes beyond the draft:**
  - **No tree ever placed in the world.** Every tree placement filters on `would_survive` of its sapling, saplings are not in the block reference, and S8's predicate compile skipped such a filter, so all 22 tree placed features were skipped. `Decoration.Golden` passing with trees on gave it away. A sapling the table lacks now tests the block below against `supports_vegetation`, the wiki's sapling placement (minecraft.wiki, "Sapling"), until saplings are in the block reference. The mangrove propagule is in the table (S7), so mangrove filters keep the propagule's own rule; mangroves skip for their placers.
  - **Whole selectors skipped.** An unported tree skipped its whole selector (plains lost its oaks because fancy oak, 10%, was not built). Now such an entry places nothing and is counted, and the selector's other entries place, as the draft did for S9b's fallen trees (the manager's reading of S9 question 2 (a)).
  - **environment_scan** held the target cell itself to `allowed_search_condition`; the wiki ends a scan on a failing cell only when it is not a target (it kept `rooted_azalea_tree` from ever placing). Found by `Trees.EnvironmentScan`.
  - **rule_based state providers** may omit `fallback` (the trees' `soil_beneath_tree` has none): no rule matching leaves the cell.

## 2. The tests and their bounds

New in `Source/Unnamed/Tests/VoxelTreeTests.cpp`: `Trees.Shapes` (27 probes), `Trees.FeatureSize` (19 ceilings: oak, spruce, fancy oak), `Trees.LeafDistance`, `Trees.Borders` (the real Decorate, merge and Finalize; 16 corner oaks and 20 touching pairs, five schedules identical), `Trees.WaterDepth`, `Trees.EnvironmentScan`, `Trees.Sapling`. Each configuration places 2,000 trees on its probe's layered column and is compared with the probe's 100 (16 for a ceiling).

Four bounds differ from the handoff's first statement, and the ceiling probes no longer test their top-log histogram (2 to 16 trees: the review's call; their exact top-log bound and placed rate stay). Each was calibrated with self-samples (`TreeTestCalibration`: each configuration's own 100-tree samples scored as probes against its 2,000 trees, 100 samples each), and every mutation below still fails under it (the manager's conditions).

| Bound | Old | New | Why | Calibration (failures in 100 correct samples) |
|---|---|---|---|---|
| Straight trunks' top-log histogram | ours against the probe, chi-square p >= 0.01 | ours and the probe each against the data's exact distribution (base + U(0..a) + U(0..b)), p >= 0.01 | jungle tree's probe sits at p 0.02 against the data itself; ours-vs-probe failed it | mutation (g) gives p < 0.005 on every straight probe |
| Leaf occupancy per cell | max z <= 4, the probe's share for the variance (clamped at 0.5 / n) | the probe's count against the pooled share, exact two-sided binomial p per cell, Bonferroni over the cells, fail below 0.01 | fancy oak has thousands of outer cells with leaves in under 5% of trees; a z score misreads counts that rare | fancy oak 64 (old), 31 (pooled z), 0 (new); every other configuration 0 |
| Means (logs, leaves, cells) | fail when over 5% and \|z\| > 2.5 | over 5% and \|z\| > 3 | a run tests about 80 means; 2.5 fails a correct model about once a run | pine 2, fancy oak 1, the rest 0 |
| Ceilings | top log <= ceiling - 3 | straight trunks ceiling - 3, others ceiling - 2 | fancy's top log sits at its height: the probe itself places fancy under a ceiling at 5 with the top log at 3 | (exact rule) |

- **The occupancy bound's limit (recorded, the manager's condition):** at fancy oak's scale (about 1,000 cells) it catches a cell emptying but not a thinning of about 11% in one cell. Mutation (h1), one end layer of each fancy cluster one radius smaller (a cell every probe tree fills drops to 0.886), was caught on 1 of the 6 fancy probes (adjusted p 0.002; the others 0.023 to 0.024); (h2), both end layers, fails all six (p 0.000).

| Mutation | Fails | Message |
|---|---|---|
| (a) base_height + 1 | Shapes, all straight probes | oak logs +19.2% (z +11.8); top-log histogram (the probe against the data) |
| (b) foliage radius + 1 | Shapes | oak leaves +145.4%, leaf occupancy (raw p 6e-133), reach |
| (c) blob corners always kept | Shapes, all 13 blob probes | oak leaf occupancy p 0.000, leaves +18.2% |
| (d) leaf distance from any log in the working copy | LeafDistance | "1 2 3 4 4 3 2 1" for "1 2 3 4 5 6 0 0" |
| (e) no feature-size check | FeatureSize | oak_ceil3 placed 100% against 0%; fancy_oak_ceil5 top log too close |
| (f) leaves written with Force | Borders | 38 earlier logs lost; 882 corner-tree leaves not replay-safe |
| (g) height_rand_a roll drops its top value | Shapes, 15 of the 16 straight probes (jungle bush has one height) | top-log histogram (ours against the data), p < 0.005 |
| (h1), (h2) fancy foliage end layers smaller | Shapes (see above) | 1 of 6, then 6 of 6 fancy probes |
| (i) acacia fork length 2 to 4 | Shapes | acacia logs +13.6% (z +6.2) |
| Sapling rule: the one-cell-down offset removed | Sapling | an oak sapling survives over grass, dirt, coarse dirt: not equal |

Reverts were checked byte for byte against copies (the new files are untracked), and each mutation series ended with a `-DisableAdaptiveUnity` build.

## 3. Results

- **Probes** (final `Trees.Shapes`): oak logs 5.01 against 5.04, leaves 55.01 against 55.01; fancy oak logs +3.4%, leaves +1.2%, reach 7/7; acacia logs +5.6% (z 2.6; the probe's trunk tops average 5.80 against the data's exact 6.00), leaves +0.9%; cherry logs +0.1%, leaves +0.2%; yellow poplar leaves +4.0%. Every adjusted occupancy p is above 0.9 and every reach equals the probe's.
- **Goldens:** plants on with trees off reproduces S8 part B exactly (`Decoration.Golden`'s new third case); trees on moves `Decoration.Golden`, `Mesher.Digest` and the radius-8 hashes (values in Docs/VoxelWorld.md). `Decoration.Reach` drops nothing.
- **Suites** (after the review fixes, `-DisableAdaptiveUnity` build): `TestVoxel.ps1` 109 of 109 (`Tests/Evidence/2026-10-10/VoxelAutomation-20261010-124134`; one HTTP-timeout warning from the engine's connectivity check); every native suite 173 of 173 (166 plus the 7 tree tests; 4 such warnings; `C:\dev\sandbox\phase6s9\runs\native-full-20261010-124404`).
- **End to end:** Create New World passes (`Tests/Evidence/2026-10-10/Worlds/Create-1791650776488`); the Playground probe passes with 0 failures (`Playground-all/Runtime-20261010-125239`).
- **The flight with trees (a regression, flagged):** on a quiet machine (CPU 0 to 27%, no other editor or java), trees on, off, on: holes in 96, 0 and 49 frames (at most 13, 0 and 8 columns), frames over the 2 ms chunk budget 14, 0 and 17, first frame to view 3.6, 2.1 and 3.6 s, process memory 4,915, 4,219 and 4,938 MB, chunk components at the first view 659, 545 and 659 (trees carry content into sections above the terrain). Decorate's mean in the flight 1.47 and 1.49 ms against 1.24 off; Finalize 0.62 against 0.58 (`VoxelFlight-20261010-125457`, `-125714`, `-125931`). Trees off reproduces S8b's clean flight.
- **Describe** (overworld): 128 placed features compile (94 in vegetal_decoration, up from 72); 23 tree configurations; stubs placing nothing: fallen trees 20, giant trunk 3, poplar 3 before the canopy port, mega jungle 2, dark oak 1, huge mushrooms 2; tree decorators left out: beehive 12, place_on_ground 12, leave_vine 2, cocoa 1, trunk_vine 1.

## 4. Jesus's answers and the manager's calls (October 10)

- **Jesus:** trees on by default (yes); "more grand trees": option 3 of the tree table, finish vanilla's grand trees first with no shape or weight changes; options 1 (bigger shapes) and 2 (more of the grand trees, behind a switch) are parked, his call later.
- **The manager:** the straight-trunk test basis and the three loosened bounds, each with its calibration and mutations; the stub reading of Q2 (a); the sapling rule with its test and mutation; the big port moved to S9b; the timed gate to S9b or a quiet window.

## 5. Evidence

- Test runs (`C:\dev\sandbox\phase6s9\runs\`): `calib-20261010-114424` and `calib2-20261010-114606` (calibration), `mut2-*-20261010-11*` (mutations a to i), `mut-sapling-20261010-121626`, `trees7-20261010-121553` (7 of 7).
- Notes per step: `C:\dev\sandbox\phase6s9\s9a\` (`tests-notes.md`, `mutations2-notes.md`, `port-simple-notes.md`, `port-branchy-notes.md`, `port-canopy-notes.md`, `tree-table.md`).
- The early photo set: `Tests/Evidence/2026-10-10/S9a-Trees-Early/`.
