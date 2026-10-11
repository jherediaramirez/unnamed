# Voxel world, phase 6, S9e part 1: azalea trees and their root systems; the mangroves built but off - October 11, 2026

The main lane's S9e ([the handoff](2026-10-11-Voxel-Handoff-Phase6-S9e.md)) was split by the manager at the plan (Shift manager 5, Q2): part 1 is the azalea tree with `root_system` and the mangroves; bamboo, forest rock and the `vines` feature go to part 2 with a fit already done. The azalea and its root systems are on and pass their probes. The mangroves are built but switched off (`VoxelRootPlacers::IsMangroveBuilt()` false): their four probes do not all pass yet (section 3), and the manager's rule was to commit what is green by about 450k and hand the rest to part 2.

## 1. The fallen tree's photo (the handoff's step 0)

`voxel.FindBlock` now takes a state filter (`birch_log[axis=x]`: only fallen birches lie that way; `Source/Unnamed/Voxel/VoxelBiomeDebug.cpp`). It found fallen birches at -644,-62,64 and -661,-136,64 near the birch forest at -632,-152. `Tests/Evidence/2026-10-11/S9e-FallenTree/birch-fallen-b/view.png` shows the stump (end grain up), a one-block gap, then the lying log with a red mushroom on top (the manager's look call: right). `birch-fallen-c` shows a Y-axis log and its stump with the camera partly in leaves; `birch-fallen-a` is inside a canopy (unusable).

## 2. What landed and is on

| Piece | Where | Rule (ours, fitted to scout J's probes, unless the data or the wiki says) |
|---|---|---|
| `bending_trunk_placer` | `VoxelTrunkPlacers.cpp` `PlaceBending` | The wiki's outline. Logs up the origin's column to Height - 3; the cell at Height - 2 moves one along a random direction at 0.4 (the probe's three-part trunks, 39%), the one at Height - 1 one further, always (0 of 100 unmoved); a log above it and `bend_length` more along the direction at Height (the top log at Height). Attachments: the trunk's cells from `min_height_for_leaves` up, then the top row from the inside out. Draws: NextInt(4), `bend_length`'s, NextFloat |
| `random_spread_foliage_placer` | `VoxelFoliagePlacers.cpp` `PlaceRandomSpread` | Per attachment, `leaf_placement_attempts` tries at (NextInt(r) - NextInt(r), NextInt(h) - NextInt(h) up, NextInt(r) - NextInt(r)), each pair drawn in order: triangular, -2..2 across and -1..1 up at the data's 3 and 2. A new leaf draws its state (azalea's weighted provider: 3 to 1 per leaf, as the probe) |
| `root_system` | `VoxelBigVegetation.h/.cpp` `FVoxelRootSystemConfig`, `CompileRootSystem`, `PlaceRootSystem` | The wiki: rooted dirt "in a 5x5 cross-section ... until it reaches the lush caves". Ours: the tree's spot is the first cell from the origin up to `root_column_max_height` passing `allowed_tree_position` with `required_vertical_space_for_tree` free cells (at most `allowed_vertical_water_for_tree` water); none: it fails. Rooted dirt from two below the spot down while the column's cell is in `root_replaceable`, `root_placement_attempts` triangular tries per layer; then `hanging_root_placement_attempts` hanging roots at NextInt(span) under the lowest layer where the cell is air under a block that is not air, hanging roots or water; then the tree at the spot. The tree's failure keeps the roots |
| Dispatch | `VoxelDecorator.cpp/.h`, `VoxelPatchFeatures.h` | `EVoxelPlantFeature::RootSystem`, `RootSystems`, under `voxel.Decoration.Trees` (the gate covers it; the manager's Q3) |
| Timing | `FVoxelTreeTimes::Roots`, `RootSystem`; `VoxelBench.cpp` | `voxel.Bench Decorate` prints "Roots per Decorate: mangrove roots ..., root systems ..." with microseconds per call; `voxel.Bench Decorate <r> <preset> at=X:Y` centres the area on a chosen column (a colon: `-ExecCmds` splits at commas) |

The fits: `C:\dev\sandbox\phase6s9\s9e\fit\f1-crown\crown-report.md` (azalea's and the mangroves' crowns, agent F1) and `f3a-rootsystem\rootsystem-report.md` (agent F3a).

## 3. The mangroves: built, off

Built for part 2 and covered by the review, but no world places them: `PlaceMangrove` (F2's root walk: an outward band of 0.36 times the ring's share, wasted steps count, the tree fails past `max_root_length`; strands, muddy and waterlogged roots, moss at 0.5; `f2-roots\roots-report.md`), `PlaceUpwardsBranching` (F1: a branch per trunk log at 0.5 x 0.85, S - 1 diagonal logs), `PlaceAttachedToLeaves` (propagules: two air cells under the leaf, no earlier one within 1 across and 0 + 1 up or down), and the tree config's `LogFreeSet` (the free set plus the trunk's `can_grow_through`; only the mangroves have one, so every other tree's path is unchanged: its log free set is its free set).

With all four on (run `C:\dev\sandbox\phase6s9\runs\s9e-crown1-20261011-024401`): mangrove and tall_mangrove on grass pass all but mangrove's leaf occupancy (p 0.001: dy -5 (-4, 0) probe 0.08, ours 0.004); mangrove@mudwater fails its 6-connected parts (p 0.00); tall_mangrove@mudwater fails leaves (283/373, -24.0%, z -3.2), cells and muddy roots (-7.2%). Agent F4 joined the two fits in Python and found no rule (`f4-joint\joint-report.md`): the mud probe's 28 survivors have no trunk of 6 or less (p 3e-4 under the height draw) and about 25% more branch logs per trunk log than the grass probes; no rule on descent and trunk size fits both mangroves, and the grass probes match the fitted branch chance 0.425 while the mud ones match the data's 0.5. Its options: a failure decided after the trunk (favouring branchy trees at deep descents), or a re-probe with other grid offsets (the probes' trees may be correlated through their grid seeds). Part 2's call.

## 4. Tests

| Rule | Where | Mutation that must fail |
|---|---|---|
| The azalea tree through the tree rules | `RunTreeProbes` (it compiles now) | 10: `leaf_placement_attempts` halved: leaves -32.9% (azalea_tree), -35.6% (rooted_azalea_tree), occupancy (`runs\s9e-mut10-20261011-030605`) |
| The rooted azalea: its root system simulated (`SimulateTrees`' `RootSystem`), its tree by the tree rules, rooted dirt and hanging roots as decorations | `FTreeTestKit::Compile(..., OutRootSystem)`, `SimulateTrees` | 11: `root_placement_attempts` halved: rooted dirt -37.0%, pooled rooted dirt z -90.49 (`runs\s9e-mut11-20261011-030652`) |
| Probes run | `FVoxelTreeShapesTest` | 43 (41 + azalea_tree + rooted_azalea_tree; the mangroves skip at their root placer) |

- azalea_tree: placed 100%, logs 7.50/7.38 (+1.7%, z +1.3), leaves 87.45/85.95 (+1.7%), top p 0.27, occupancy p 1.000, reach 6/6, 6-connected parts p 0.73.
- rooted_azalea_tree: placed 100%, logs 7.51/7.60, leaves 87.50/89.47 (-2.2%), rooted dirt 115.57/114.43 (z +2.4), hanging roots 7.87/8.07, top p 0.91, reach 6/6, 6-connected parts p 0.01 (passes; borderline, a known issue: under mutation 11's redrawn trees it fails).
- Every earlier probe line is unchanged in full (the scaffold run `runs\s9e-scaffold-20261011-022559` against S9d part 2's `s9d2-mush1-trees-20261011-004921`: cmpfull same 73, diff 0).
- Run on final code: `runs\s9e-off1-20261011-030454` (7 of 7).

## 5. Re-records (trees on; every trees-off control unchanged)

| Golden | Old (S9d part 2) | New | Source |
|---|---|---|---|
| `Decoration.Golden` case 3, large_biomes | `0x489b7d9f32c4567c`, 774,879 writes | `0xff1a7eb180110302`, 793,370 | `runs\s9e-gold1-20261011-030753` |
| case 3, overworld / amplified | `0x95645bbf53f73c12` / `0x98ae73a412fa50ca` | unchanged | |
| `Mesher.Digest` large_biomes models on / off | `0x112989521b6dc60c` / `0x762f1bf60ca6a7e5` | `0x0ea42a420b0a2f83` / `0x8b29db68a9ad618d` | same |
| overworld, amplified pairs | | unchanged | |
| Radius 8 overworld / large_biomes / amplified | `0xb70a80232f0f1389` / `0xe2a8bc32dc58d73c` / `0xe40204b09dd1016f` | `0x1681a6e69daab32e` / unchanged / `0xfbb824583b2844c8` (both load orders equal, 0 columns differ) | `C:\dev\sandbox\phase6s2\runs\r8-s9e-031053` |

The new values passed on the next run (`runs\s9e-gold2-20261011-030959`). The lane's hash file: `C:\dev\sandbox\phase6s9\s9e\expected-hashes-s9e.txt`.

## 6. Cost

- **Root systems** (indicative, outside a window): 121.4 us a try in the overworld gate area before S9e's exact cuts (the space's first cell checked before the predicate; the roots' own cells in a flat grid instead of a TMap), 63.9 us after (`C:\dev\sandbox\phase6s2\runs\s9e-ow1-025121`, CPU 92%, 5 java/UnrealEditor processes): 0.24 tries a Decorate, 0.015 ms. Most of a try is writing the rooted dirt column through the rock (about 13 cells a layer).
- **A mangrove swamp column** with the mangroves on (they are off now; for part 2): `voxel.Bench Decorate 2 overworld at=520:-1112` (`runs\s9e-mangrove2-024731`, CPU 2%): Decorate 1.568 ms with trees against 1.114 without (+0.45 ms); trees 0.475 ms in 6.88 tries (2.61 placed), mangrove roots 7.8 us a tree, attached_to_leaves 7.0 us a call (scout J's estimate: 0.5 to 1.1 ms a column).
- **The S9 bench gate: MISSED** (the manager's call: committed with the miss recorded, the cut is S9e part 2's first step). Quiet window 03:16:59 to 03:40:28 (the guard saw no other lane); `C:\dev\sandbox\phase6s9\s9e\gate9.ps1 -Round 1..3`, each launch behind `Scripts\QuietWindow.ps1`, final code, 0 java, editor, UBT or compiler processes before every launch, CPU 12 to 90%; `C:\dev\sandbox\phase6s9\s9e\gate9-r1.txt` to `-r3.txt`, `s9b\gatesum9.py`. Medians of 3, ms per column:

| Preset | Decorate, trees on - off (rounds) | Finalize with trees (off) | Whole column with trees (off) |
|---|---|---|---|
| overworld | **+0.548** (+0.548, +0.577, +0.533): missed (S9d part 2 +0.387) | 0.677 (0.532) | 7.838 (7.186) |
| large_biomes | +0.001 (+0.001, +0.028, -0.043) | 0.522 (0.521) | 7.135 (7.254) |
| amplified | +0.185 (+0.171, +0.185, +0.189) | 0.570 (0.510) | 8.587 (8.317) |

  The breakdown (overworld, per Decorate, the same in every round): trees 0.52 to 0.53 ms in 4.53 tries (3.29 placed): feature size 0.051, trunk 0.019, foliage 0.076, leaves' distance 0.069, decorators 0.105 (leave_vine 0.061, trunk_vine 0.043), emission 0.19; root systems 0.015 (0.24 tries, 64 us each), S9e's only new stage. The rest is S9d part 2's tree code, unchanged (its 73 probe lines byte-identical), on a machine whose overworld column ran 7.186 ms with trees off against S9d part 2's window's 5.786 (+24%). S9e's own increment is estimated at 0.03 to 0.04 (root systems plus the few azalea trees in the tree stages): about 0.42 to 0.43 at S9d part 2's level, a small miss either way.
  A first set of three rounds is discarded (`gate9-pre-r1.txt` to `-r3.txt`, +0.613): my own photo agent's captures (UnrealEditor.exe from this folder) ran beside them, as the window lets its own lane through, and a build collided with the review fixes, so they ran on the previous binaries.
- **The flight** (in the window, final code; `Tests/Evidence/2026-10-11/VoxelFlight-20261011-033653`): PASS in 134 s; holes in 453 frames, at most 34 columns (S9d part 2 275, at most 32; apply throughput, the streaming session); quads 2,158,399 (S9d part 2 2,170,371); chunk work worst 3.91 ms, over budget in 13 frames.

## 7. The manager's calls

- Shift manager 5: the plan approved with the split (Q2: part 1 = mangroves, azalea, root_system; bamboo, forest rock, vines to part 2 with F3b's fit, `C:\dev\sandbox\phase6s9\s9e\fit\f3b-part2\part2-report.md`); everything under `voxel.Decoration.Trees` (Q3); the 300k case accepted with "wrap-up by 500k; if mangroves are not green by ~450k, commit azalea + root_system and hand mangroves to part 2".
- The manager's handover: from then on "Shift manager handover" (session_0183qLtfHxnQtUiZ3iuqUHwh); Jesus approved the main lane chaining through phase 6 (S9e part 2, S11, S12) one chat at a time.
- The mangroves (the new manager, under Jesus's away rule): (b), a black-box re-probe of mangrove and tall_mangrove on mud and water before any new rule ("closer to vanilla, easy to change later"); they stay built but off in part 1.
- The gate (S9c's precedent, its overage going to the next step): commit with the miss recorded; S9e part 2 starts with the cost step: in one window, S9d part 2's build against this commit (overworld, 3 rounds) for S9e's increment, then the exact early-out cut, the gate re-measured against the same control, committed on its own.
- The look call: azalea `S9e-Azalea/a5` against `c1` accepted (the flowering crown over the shore; the control bare); the roots view `b1` is invalid (an unlit cave): the tests cover the roots, and a lit lush-cave roots shot goes to S11.

## 8. Commits and suites

- **Review:** one Sonnet diff reviewer (108.4k peak, 1,310 diff lines), no definite bug. Fixed: the bench's preset skips `at=` tokens; `voxel.FindBlock` requires the closing `]`; attached_to_leaves' reach covers its required empty cells; the mangrove root placer's draws comment names the providers' draws. Kept, with reasons: bending attaches whether or not a log went down (the other trunks' convention, now in its comment); `LogFreeSet` only for trunks with `can_grow_through` (only the mangroves: the 73 earlier probe lines are byte-identical, cmpfull diff 0, and the overworld and amplified goldens unchanged).
- **Suites on final code** (after the review fixes): `Scripts\RunChecks.ps1 -Profile final -ExpectedHashes C:\dev\sandbox\phase6s9\s9e\expected-hashes-s9e.txt -Skip build`, green in 22 min (`Tests/Evidence/2026-10-11/Checks-final-20261011-034057/summary.txt`): native 257 of 257 on the first attempt, 0 crashes (`Native-20261011-034059`); TestVoxel 164 (`VoxelAutomation-20261011-034530`); radius 8 and Mesher.Digest 9 of 9 equal; the Playground runtime (all 119, interaction 81, tour 27 checks, 0 failures); create-world 32 checks; npm 57; source-check; the Reference verifiers. Then clang-format reflowed one comment (`VoxelTreeDecorators.cpp`): rebuilt, tree tests 7 of 7 (`C:\dev\sandbox\phase6s9\runs\s9e-final-20261011-040357`).
- **Commits:** `aa67ad2` (the code, tests and re-records), then this record, VoxelWorld.md, the S9e part 2 handoff and the records index.

## 9. Known issues

| Issue | Due |
|---|---|
| The mangroves are off: tall_mangrove@mudwater's taller, branchier survivors; mangrove's far low leaves; mangrove@mudwater's 6-connected parts (section 3) | S9e part 2 (a joint rule, or a re-probe: the owner's call) |
| rooted_azalea_tree's 6-connected parts at p 0.01 (passes; fails under mutation 11's redrawn trees). azalea_tree, the same tree, gives p 0.73 | Watch; part 2's runs |
| root_system's failure paths (no surface within 100, water, low ceilings) and its level tests (`level_test_distance`, `max_level_deviation`, 0 in the data) are ours and unprobed | Optional, with the terrain probe on Jesus's list |
| **The S9 gate missed**: overworld +0.548 against 0.4 (S9e's own increment about 0.03 to 0.04; the rest the trees on a slower machine) | S9e part 2's first step (the cost step) |
| Root systems cost about 64 us a try (the rooted dirt column) | With the cost step |
| A lit lush-cave view of the hanging roots and rooted dirt | S11 (lush caves) |
| The fallen log's terrain rule and the huge mushrooms' space checks (S9d part 2) | Optional, on Jesus's list |
| Brown huge mushroom caps render pale cream (S9d part 2) | Jesus's small-fixes list |
| Flight holes (apply throughput) | The streaming session |
