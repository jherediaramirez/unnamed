# Voxel world, phase 6, S9d part 2: fallen trees and huge mushrooms - October 11, 2026

The main lane's S9d part 2 ([the handoff](2026-10-10-Voxel-Handoff-Phase6-S9d2.md)) built the last two of S9b's selector stubs: the six fallen trees, with the `attached_to_logs` decorator, and the two huge mushrooms. It also changed two S9c decorator rules on clean new measurements (the manager's calls Q1 and Q2). Every probe passes. The re-records are below.

## 1. Fallen trees

`Decoration/Features/VoxelBigVegetation.h/.cpp` (`FVoxelFallenTreeConfig`, `CompileFallenTree`, `PlaceFallenTree`). A fallen tree is built on an `FVoxelTreeBuilder`, so the tree decorators run on it unchanged. The builder's new `DecoratedBegin` and `DecoratedEnd` make `GetCells` read the stump alone for `stump_decorators` and the log alone for `log_decorators`.

| Part | Rule (ours, fitted to the six probes, 600 trees on flat ground, unless the wiki says) |
|---|---|
| Stump | One log (axis y) at the origin, where the cell is free |
| Log | One of four directions uniformly (the probes: 166, 134, 152, 148); `log_length` - 2 cells (oak's 4..7 gives 2..5; every probe's farthest log is its largest `log_length`); starting 2 or 3 from the stump with even odds (306 and 294; the wiki's "1-2 blocks from the stump"). Logs along X or Y take the trunk's axis states |
| Terrain (ours, a known issue) | The log goes down only where every cell is free and the block under each is not; otherwise the stump stands alone (the wiki: the log "does not always generate"). The probes cannot show this |
| Decorators | Only those that read the logs they are given (trunk_vine, cocoa, shelf_mushroom, attached_to_logs). Others would be left out and counted; none is in the data |
| Draws | NextInt(4) direction, `log_length`'s draws, NextInt(2) gap, then the stump's decorators, then the log's |

The reach is the largest `log_length`, plus 1 for a decoration beside a log, plus the tree's margin of 1. Fallen super birch's 15 + 1 = 16 fits the 3 x 3 window's 16.

## 2. Huge mushrooms

`FVoxelHugeMushroomConfig`, `CompileHugeMushroom`, `PlaceHugeMushroom`.

| Part | Rule |
|---|---|
| Height | h = 4 + NextInt(3), doubled when NextInt(12) is 0 (the wiki: 5 to 7 blocks, 1 in 12 twice as high; the probes: the cap's top at dy 4/5/6, about 30% each, and 8/10/12, about 3% each) |
| Stem | h blocks at dy 0 to h - 1, `stem_provider`'s state |
| Cap | Brown: one layer of (2r + 1)^2 without corners at dy h (r 3). Red: a (2r - 1)^2 top at dy h over four walls 2r - 1 wide and 3 high at distance r (r 2). 45 blocks each. A face is true where the neighbour holds no cap block, except the bottom and a red wall's inner face (the examples' every cell, matched exactly by the test) |
| Checks (ours after the wiki's growth rules; a known issue) | The block under the origin passes `can_place_on`. Red: the stem's column up to the cap's top is free ("only the blocks directly above"). Brown: its first three cells, then the cap's square from dy 3 up ("a 7 x (height - 3) x 7 region"). Free means air or `replaceable_by_trees`; the reference has no `replaceable_by_mushrooms` tag. Stem and cap are written `IfReplaceable` |

## 3. Decorators

| Decorator | Before | Now | Why (the manager's call) |
|---|---|---|---|
| attached_to_logs | not built | Per log, at the probability, one of the data's directions at random; the provider's state there where the cell is air, with no survival check | The wiki; the probes: 0.1 x log cells per tree (0.35 to 0.61) |
| trunk_vine | 75% per side (the wiki; S9c's call kept it) | **2/3** per side | Q1 (a). Fallen oak and jungle stumps have all four sides open: 533 vines on 200 stumps is 0.666 per side, where 75% gives 3.0 per stump (z -5.5) and fails fallen jungle's mean (+16%). This changes S9c's call |
| shelf_mushroom | Gate at the probability, then 9% per open side of the 4 lowest log layers | Gate at the probability, then per log of the 4 lowest layers at 3/8, one random bark side (4 upright, the 2 across a lying log) | Q2 (a). S9c's rule gave fallen poplar 0.65 per tree against the probe's 1.05. The one rule fits both: standing poplars 0.52 on 32% of trees (the probes 0.51 pooled, 29-33%), fallen poplar 1.06 on 65% (1.05, 65%) |

**S9c's trees before and after** (ours/probe per tree, z; the S9d record's run against `s9d2-fallen3-trees-20261011-004058`):

| Probe, block | S9c | S9d part 2 |
|---|---|---|
| mega_jungle_tree, vine | 243.08/225.80 (+7.7%, z +2.8) | 231.22/225.80 (+2.4%, z +0.9) |
| jungle_tree, vine | 60.02/58.71 (+0.7) | 58.39/58.71 (-0.2) |
| swamp_oak, vine (leave_vine only) | 66.53/65.07 | 66.53/65.07 (unchanged) |
| poplars, shelf_mushroom | 0.49 against 0.54, 0.45, 0.54 | 0.51 against the same (z -0.4, +0.5, -0.4) |

## 4. Tests

| Rule | Where | Mutation that must fail |
|---|---|---|
| Fallen trees through the tree rules (logs, cells, decorations, pooled, block set, exact log reach, every-cell guard, parts) | `RunTreeProbes`, `FTreeTestKit::Compile` (fallen: the builder's tree with both decorator lists), `SimulateTrees` (`PlaceFallenTree`) | 8, the log one longer: fallen super birch reach 16/15 (and every fallen probe's logs +17 to +22%) |
| Leaf occupancy skipped where the probe has no leaf layers (shown on the line) | `CompareTrees` | |
| Fallen log directions: each probe's chi-square (ours against its four bins, read from its per-placement boxes) summed over the six, on 18 degrees of freedom; each probe's p shown on its line | `RunTreeProbes` | 9, every log measured pointing east: chi-square 9681.6, p 0.000 |
| Huge mushrooms: placed rate, caps exactly 45, stems (cells - 45) by the means rule, cap-top histogram (chi-square homogeneity), reach and blocks exactly, each example cell for cell against ours of its height | `CompareMushrooms` | |

- Probes run: 41 (S9c's 33, six fallen, two mushrooms).
- The six fallen probes (2,000 ours each): logs -2.3% to +5.1% (|z| at most 2.0); stump vines 2.69/2.74 and 2.69/2.59; mushrooms within |z| 1.7; fallen poplar shelf 1.01/1.05; reach exact on all six (birch 8, jungle 11, oak 7, poplar 7, spruce 10, super birch 15). Directions: chi-square 28.33 on 18 (p 0.057); each probe's p from 0.01 (spruce) to 0.98.
- Mushrooms: brown stems 5.41/5.64 (-4.2%, z -1.2), cap top p 0.52, reach 3/3; red stems 5.41/5.37 (+0.7%), cap top p 0.92, reach 2/2. Both examples exact on both probes.
- Mutations 8 and 9 ran through `C:\dev\sandbox\phase6s9\s9d2\mut.ps1`, which restores the file byte for byte (SHA-256), touches it and rebuilds (`runs\s9d2-mut8-20261011-004138`, `s9d2-mut9-20261011-004210`).

## 5. Re-records (trees on; every trees-off control unchanged)

| Golden | Old (S9c) | New | Source |
|---|---|---|---|
| `Decoration.Golden` case 3, overworld | `0x079658c484f2c46f`, 822,126 writes | `0x95645bbf53f73c12`, 822,150 | `runs\s9d2-gold1-20261011-005103` |
| case 3, amplified | `0x0896d92106e14e18`, 857,228 | `0x98ae73a412fa50ca`, 857,244 | same |
| case 3, large_biomes | `0x489b7d9f32c4567c`, 774,879 | unchanged | |
| `Mesher.Digest` overworld models on / off | `0x9c60a0ec50ad7985` / `0x27dd91e77ccd164b` | `0xdc44febc46d1c73a` / `0xa0ea785555d107d2` | same |
| amplified on / off | `0x32e2efa3bd031917` / `0xa6e6109c9bf13ae3` | `0x61c8745ba36e2004` / `0x6fd8255fd190e1ca` | same |
| large_biomes on / off | `0x112989521b6dc60c` / `0x762f1bf60ca6a7e5` | unchanged | |
| Radius 8 overworld / large_biomes / amplified | `0xf427b0a0ec0e6eaa` / `0xcac6968a2e00c046` / `0x0a9cb42b08a0f10a` | `0xb70a80232f0f1389` / `0xe2a8bc32dc58d73c` / `0xe40204b09dd1016f` (both load orders equal) | `C:\dev\sandbox\phase6s2\runs\r8-s9d2-005330` |

The new values passed on the next run (`runs\s9d2-gold2-20261011-005245`). The lane's expected-hash file is `C:\dev\sandbox\phase6s9\s9d2\expected-hashes-s9d2.txt`.

## 6. Cost

`voxel.Bench Decorate` prints the fallen trees and huge mushrooms after the trees' stages ("fallen trees X in N tries, huge mushrooms Y in M tries"); their decorators count with the others.

- **Indicative** (outside a window; java x2 from another lane's probe, CPU 17 to 31%; `C:\dev\sandbox\phase6s2\runs\s9d2-brk1-ow-*`, `s9d2-brk2-lb-*`, `s9d2-brk2-amp-*`): the bench areas barely meet the new features. Overworld's jungle-heavy area tried no fallen tree or mushroom; large_biomes 0.02 fallen trees per Decorate (under 0.0005 ms; attached_to_logs 1.1 us a call); amplified none. trunk_vine at 2/3 costs 0.044 ms on overworld (S9d's 0.049).
- **Per placement** (the bench prints microseconds per try since the review round; `runs\s9d2-brk3-ow24-011234`, `s9d2-brk4-ow32-011325`; `voxel.Bench Decorate`'s radius is clamped, so neither reaches a dark forest): a fallen tree costs 1.5 to 2.1 us before its decorators (attached_to_logs 1.5 us a call, the stump's trunk_vine about 1 us). **Estimates** where they are common: a forest's about 10 tree tries a column x 1.25% = 0.13 fallen trees, under 0.001 ms a column; a dark forest's 16 vegetation tries x 7.4% (brown 2.5%, then red 5% of the rest) = 1.2 huge mushrooms, at about 20 us each (45 cap and h stem writes at about 0.3 us a written cell, as the fallen trees, plus up to about 150 free checks), about 0.024 ms a dark forest column.
- **The S9 bench gate** (quiet window 01:46 to 01:57, no intruders; `gate9.ps1 -Round 1`, `-Round 2`, `-Round 3`, each launch behind `Scripts\QuietWindow.ps1`; 0 java, editor, UBT or compiler processes before every launch, CPU 3 to 49%; `C:\dev\sandbox\phase6s9\s9d2\gate9-r1.txt` to `-r3.txt`, `s9b\gatesum9.py`). Medians of 3, ms per column:

| Preset | Decorate, trees on - off (rounds) | Finalize with trees (off) | Whole column with trees (off) |
|---|---|---|---|
| overworld | **+0.387** (+0.387, +0.370, +0.648): met, narrowly (S9d +0.332) | 0.577 (0.522) | 5.558 (5.786) |
| large_biomes | -0.049 (-0.049, -0.022, -0.356) | 0.279 (0.393) | 4.605 (4.415) |
| amplified | -0.135 (-0.135, -0.229, -0.049) | 0.283 (0.257) | 4.864 (5.642) |

  The bench areas barely contain the new features (above), so the difference from S9d's +0.332 is the machine's level tonight (overworld's whole column ran 25 to 37% above S9d's window, trees off included; large_biomes and amplified ran faster with trees on than off), not the features.
- **The flight** (in the window, final code; `Tests/Evidence/2026-10-11/VoxelFlight-20261011-015429`): holes in 275 frames, at most 32 columns (circles 172, 40, 63; S9d 212, at most 16), apply throughput as before (S9b's levers, a streaming session); quads 2,170,371 (S9d 2,182,747: fewer trunk vines); chunk work over 2 ms in 3 of 9,379 frames (worst 2.50 ms); memory over laps 2 and 3: process 5,031 -> 5,077 MB, columns 18.93 -> 19.61 MB (S9d's).

## 7. The manager's calls

- The plan approved with Q1 (a) trunk_vine 2/3 everywhere, Q2 (a) one shelf_mushroom rule, Q3 the four statistics, Q4 the terrain rules as ours, flagged; "the probes are black-box vanilla, so they win over the wiki".
- The 300k case: the mushrooms stay in this session (one re-record round for the lanes). Wrap-up by 500k, hard stop 650k.
- The direction test pools by summing the probes' chi-square statistics (df 6 x 3), not by merging counts; mutation 9 added.

## 8. Commits and suites

- **Review:** one Sonnet diff reviewer (107k peak), no definite bug. Fixed: an unbuilt fallen trunk provider skips the feature (OutSkip) instead of failing the compile; a probe's stump-only placement counts no direction, as ours; `TryGetNumberField` for the probes' cell counts; a comment that mutations 1 to 7 aim at the standing trees. Kept, with reasons: the reach's margin of 1 is the standing trees' convention; `Directions` only holds bits 0 to 5; `GetName` drops the namespace.
- **Suites on final code** (after the review fixes): `Scripts\RunChecks.ps1 -Profile final -ExpectedHashes C:\dev\sandbox\phase6s9\s9d2\expected-hashes-s9d2.txt`, green in 30 min (`Tests/Evidence/2026-10-11/Checks-final-20261011-011548/summary.txt`): native 250 of 250 on the first attempt, 0 crashes (`Native-20261011-011549`); TestVoxel 161 (`VoxelAutomation-20261011-011734`); radius 8 and Mesher.Digest 9 of 9 equal; the Playground runtime (all 42.8 fps, interaction, tour); create-world; npm 57; source-check; the Reference verifiers. Before the review round: quick green (`Checks-quick-20261011-005719`).
- **Commits:** `0c39c50` (the code, tests and re-records), then this record, VoxelWorld.md, the S9e handoff and the records index.

## 9. Known issues

| Issue | Due |
|---|---|
| The fallen log's terrain rule and the huge mushrooms' space checks are ours (flat probes); a black-box probe over gaps and slopes would settle them | Optional, on Jesus's list |
| The huge mushrooms write over air and `replaceable_by_trees` (no `replaceable_by_mushrooms` tag in the reference) | With the probe above |
| **Brown huge mushroom caps render pale cream** (the shared block render path, not the decoration): our cap states are vanilla's (both probes' examples match cell for cell), and the reference gives the top state 9561 (down false, up true, sides false) brown_mushroom_block on up and mushroom_block_inside on the rest (the open state 9532: brown on all but down). In `S9d2-BigVegetation/darkforest-a/view.png` the brown top renders (243, 228, 211) and its side (225, 211, 193). Mapped through the red top's own curve (texture 200 -> 230, 46 -> 84), brown_mushroom_block (149, 112, 81) should give about (182, 147, 117), mushroom_block_inside (232, 201, 154), and mushroom_stem (203, 197, 186) gives (233, 227, 217), which is what shows. Red caps draw right. Suspect: brown_mushroom_block's texture slot or array mapping (DA_Blocks, the block materials). The test: a Playground state-gallery shot of brown_mushroom_block 9500, mushroom_stem and red_mushroom_block side by side | Jesus's small-fixes list (the manager) |
| No fallen tree is photographed: six views missed one, and nothing prints their positions | The S9e handoff's first shot |
| The gate's narrow margin: overworld +0.387 of 0.4, on a machine running slower than in S9d's window | S9e's first cost risk (the manager's call) |
| Flight holes rose: 275 frames, at most 32 columns (S9d 212, at most 16) | The streaming session after S9e |
| The intermittent allocator crash (the S9d record, section 1) | A test session |
| Mangroves (`attached_to_leaves`), azalea and `root_system`, bamboo, the `vines` feature, forest rock | S9e |
