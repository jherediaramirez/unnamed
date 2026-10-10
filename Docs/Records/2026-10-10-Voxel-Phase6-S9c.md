# Voxel world, phase 6 S9c: the tree decorators and the giant spruces - October 10, 2026

S9c built nine of the eleven tree decorators, refit and switched on mega spruce and mega pine, and changed four test rules (each calibrated, each with a mutation). Fallen trees, huge mushrooms and `attached_to_leaves` moved to S9d (the manager's call, for the session's budget and because `attached_to_leaves` appears only on mangroves). Every probe line that existed before each step stayed byte-identical; the only planned change is pale_oak_creaking's log count, since the creaking heart now replaces a log.

## 1. The decorators

They run in `VoxelTreeDecorators.cpp` after the trunk, the foliage and the leaves' distance, in the data's order, and draw from the tree's stream. Leaves too far from the logs are dropped first, so a vine never hangs from a leaf that is not emitted. Decorations are emitted after the tree's logs and leaves, only where the cell still holds what this Decorate saw (`IfUnchanged`). A decoration over the tree's own soil replaces it and keeps the soil's kind (podzol over the trunk's dirt). The creaking heart turns its log into a decoration (`FVoxelTreeBuilder::ReplaceWithDecoration`).

| Decorator | Rule (wiki, or ours, fitted) | Probe lines (ours/probe per tree, z) |
|---|---|---|
| alter_ground | Ours, fitted (`C:\dev\sandbox\phase6s9\s9c\alter\fit2.py`, the best of 168 rule families, ring counts within 1.5 sigma). Around each bottom log there are 4 circles (a 5 x 5 square without its corners) at (-1, -1), (2, -1), (-1, 2) and (2, 2). Then 5 tries each draw an offset in -3..4 (X, then Y), and an offset on that square's edge adds a circle there. In each column, the first cell from 2 above the log to 3 below that is not air, not replaceable and not the tree's own takes `podzol_beneath_tree` (the vertical search is ours: the probes stand on flat ground). | podzol: mega_spruce 118.12/118.71 (z -0.6), mega_pine 118.12/119.07 (-0.9); every cell's reach 7/7 |
| beehive | Wiki: probability per tree, the nest faces south. Ours, fitted: a random air west, east or south side of a log one layer under the lowest leaf, with air south of it. All 7 of fancy oak's misses are trees whose canopy reaches the ground. | fancy_oak_bees 0.92/0.93 (-0.4), super_birch_bees 1.00/1.00, the 0.02 to 0.05 probes within z 0.9 |
| cocoa | Wiki: at the probability the tree gets any, then 25% per face. Ours, fitted: the two lowest log layers (the probe's pods sit at dy 0 and 1 only); facing the log, age 0 to 2. | jungle_tree 0.35/0.44 (-1.0) |
| trunk_vine | Wiki: 75% per free side of every log (branches included), air only. | see leave_vine |
| leave_vine | Wiki: the probability per leaf side. Ours: a vine facing the leaf, then up to 4 more below while air (the probe's runs are 5 long). | jungle_tree vine 60.02/58.71 (+0.7), swamp_oak 66.53/65.07 (+1.0), mega_jungle_tree 243.08/225.80 (+7.7%, z +2.8) |
| place_on_ground | Wiki: tries, radius and height default to 128, 2 and 1. Ours, fitted: each try picks a column in the trunk's footprint plus the radius and a height within origin Z +- height. Litter lands where the cell is air, the block below is the column's top (motion_blocking_no_leaves), no log of this tree stands above (the examples have no litter in 60 columns under branches), and it survives. | leaf_litter oak 30.26/30.11, birch 30.26/30.48, fancy oak 27.42/27.22, dark oak 28.85/28.65, poplars 27.08 against 26.49 to 27.67 (all within z 1.5) |
| shelf_mushroom | Wiki: on the trunk's sides. Ours, fitted: the tree gets any at the data's probability, then 9% per open side of the 4 lowest log layers; it faces away from the log, age 0 to 1. | 0.49 against 0.54, 0.45 and 0.54 (z -0.5, +0.5, -0.5) |
| pale_moss | Ours, fitted. The ground part (at its probability) runs the data's configured `pale_moss_patch` (S8's vegetation patch) centred on a random bottom log; carpets beside a log grow a side toward it. Then hanging moss goes under each open log bottom (trunk probability) and leaf bottom (leaves probability), growing one more cell at 0.5 while air, tip at the end. | pale_oak / pale_oak_creaking: hanging moss 18.14 against 19.15/17.81; moss block 54.05 against 49.51/46.11 (+2.5); carpet 7.19 against 6.21/6.59; short grass 6.80 against 6.32/5.84; tall grass 5.36 against 5.74/4.40 |
| creaking_heart | Wiki: a trunk cell fully enclosed by logs. Ours: one such log at random, dormant and natural, keeping the log's axis. | 0.29/0.31 (-0.5) |

- **Draws:** each decorator's comment lists its draws in order. They all come after the foliage's, so no tree's shape changed.
- **Reach:** `VoxelTreeDecorators::GetReach` per decorator; a tree's reach is the largest.
- **Left out and counted:** `attached_to_leaves` (S9d, with mangroves). Fallen trees and huge mushrooms remain selector entries that place nothing (S9d).

## 2. Mega spruce and mega pine

- **The refit** (Python, `C:\dev\sandbox\phase6s9\s9c\megarefit\REFIT.md`): the crown radius per layer is a table by crown height C and top parity, 20 rows (C 3 to 7 and 13 to 17). It was measured from the probes' per-layer occupancy (times 100 these are exact tree counts) and their leaf totals. No closed form fits (the best misses 27 of 197 layers). The draws are unchanged.
- **The scorer** (the C++ test's rules, 10 seeds of 2,000 trees): occupancy p 1.000 on every seed for both, where the old rule had p 0.000. 24 example trees rebuild with 0 mismatches; 9 rows were read off examples, and 3 rows are confirmed independently. Pine's C 4, 5 and 6 odd rows each have an equally valid alternative no probe separates.
- **In C++** (`MegaPineRadii` in `VoxelFoliagePlacers.cpp`; giant back on in `VoxelTrunkPlacers::FindType`), ours/probe:

| Probe | Logs | Leaves | Occupancy p | Reach |
|---|---|---|---|---|
| mega_spruce | 81.35/83.00 (z -0.9) | 316.10/315.88 (+0.1) | 1.000 | 5/5 |
| mega_pine | 81.35/82.88 (-0.8) | 108.44/105.76 (+0.7) | 1.000 | 5/5 |

- **Ceilings**, ours/probe placed: 12 0/0, 15 2%/0, 18 20/19, 22 46/50, 26 73/88 (p 0.26), 30 98/100.
- **Counts:** `Trees.Shapes` 31 -> 33 probes; `Trees.FeatureSize` 26 -> 32 ceilings.

## 3. Test rules changed (the manager's calls)

| Rule | Old | New | Why | Mutation that must fail |
|---|---|---|---|---|
| Soil under a giant trunk | S9b's alter_ground rule: soil exactly one per trunk column, left out of the block set | Deleted; the dirt under the trunk against the probe's, every block in the set | alter_ground is built | alter_ground's random tries removed: podzol 77/119, z -37 |
| Decoration means (new) | none | Each decoration block's mean per tree against the probe's (scout J's distribution 4). At 1 or more per tree: fail beyond 5% and z 3. Under 1 per tree: an exact conditional binomial on the totals, with both counts divided by D = max(1, our variance / our mean); D is shown on the line. A block never written is only reported while the tree leaves decorators out. | the decorators' oracle; the normal z false-failed 587/1000 self-samples on rare blocks, and without D clumped blocks false-failed (cocoa 96/1000) | mutation 6, cocoa's probability zeroed: -100%, z -16.2 |
| Pooled decoration check (new) | none | Stouffer's combined z per block across probes, failing beyond 3.5. A probe joins only where ours expects the block at least once in its trees, and k 1 never fails. | one probe's 100 trees cannot see small rate changes. The plain sum false-failed 70/1000; with the conditions 6/1000; with the pale garden and leaf litter in the pool, 21/1000 at a bound of 3, then 8/1000 at 3.5 | mutation 5: pooled z +18.78 |
| Block set | every written block in the probe's | an unseen block is allowed only when one of this tree's built decorators can write it, then tested against the probe's 0 | the p 0.02 and 0.002 bee probes saw no nest in 100 trees | mutation 5 (beehive writes oak_planks): fails on 13 probes |
| Placed none against some (probe none) | any of ours fails | fails only when (1 - q)^n < 0.01 (q ours, n the probe's tries) | ours 1/45 = 2.2% at mega_spruce_ceil15 against the probe's 0 of 16 is p 0.70 | mutation 4 (giant base_height -5, ceil15 at 33%): (1 - q)^n 0.0018 |
| Reach | every cell against the logs' and leaves' reach | the logs and leaves as before; every cell against `max_reach_h_any_cell` | the podzol reaches 7, the leaves 5 | |

- **Calibration** (1,000 self-samples per configuration, `TreeTestCalibration`): the pooled check false-fails 8 of 1,000 runs (`runs\f-cal1000-20261010-181920`). The per-probe decoration rule false-fails at most 21 of 1,000 for any one probe (birch_bees_0002; `t2-cal2-20261010-174745`). It is conditioned on our one 2,000-tree run (one seed), which inflates the counts; untested with a second seed (a known issue).
- **Not catchable:** scout J's "beehive probability x2". Per probe z +0.1, +1.8, -0.4, +0.7, +2.2, +1.7, +2.1, 0.0; pooled +2.91 (`t-mut3-20261010-172221`). The two probability-1 probes cannot move, and 100-tree probes at 2 to 5% cannot resolve a doubling at 3 sigma (the manager's call: an honest limit).
- **Other mutations:** the litter's tries halved (every litter probe -38 to -41%); pale moss's ground zeroed (four ground blocks -100%); the creaking heart's enclosure dropped (+223%, z +6.7); the old crown rule (occupancy p 0.000 on both giants). Each file was restored byte for byte (SHA-256), touched, and rebuilt `-NoUnity`.

## 4. Re-records

Trees on changed, as the decorators and the giants write; every trees-off control reproduces S8 part B's values.

| Golden | Old (S9a/S9b) | S9c |
|---|---|---|
| `Decoration.Golden` third case (overworld, large_biomes, amplified) | `0x56c797f071194ef7`, `0x3625dcba631991c6`, `0x3634ee586cb5898c`; writes 817,117, 774,870, 854,341 | `0x079658c484f2c46f`, `0x489b7d9f32c4567c`, `0x0896d92106e14e18`; writes 822,126, 774,879, 857,228 |
| `Mesher.Digest` trees on, models on / off: overworld | `0x2d204b0b4cf1abd8` / `0x6ee399181d5fccd7` | `0x9c60a0ec50ad7985` / `0x27dd91e77ccd164b` |
| large_biomes | `0xca80f4d03a73a6f9` / `0x762f1bf60ca6a7e5` | `0x112989521b6dc60c` / unchanged (no full-cube decoration lands in its area) |
| amplified | `0x49802047907506c6` / `0x627b48a897480211` | `0x32e2efa3bd031917` / `0xa6e6109c9bf13ae3` |
| Radius 8 trees on (both load orders equal) | `0x194a623a86b97dd6`, `0x1b665f4703a79406`, `0xbde96a3deaa9d130` | `0xf427b0a0ec0e6eaa`, `0xcac6968a2e00c046`, `0x0a9cb42b08a0f10a` |

- **Controls:** `Decoration.Golden`'s second case and `Mesher.Digest`'s trees-off case pass unchanged; radius 8 with trees off is `0xddb7e949cd5e191a`, `0xcbac9722096ef7c5`, `0x61c63a3490017e2e` (S8 part B's).
- **Runs:** `C:\dev\sandbox\phase6s9\runs\rerec2-s9c-20261010-182707`; radius 8 `C:\dev\sandbox\phase6s2\runs\radius8-s9c-*` (on) and `radius8b-s9c-*` (off; exec.ps1 separates commands with commas).
- **Suites:** `TestVoxel.ps1` 147/147 (`Tests/Evidence/2026-10-10/VoxelAutomation-20261010-183350`); every native suite 227/227 (`C:\dev\sandbox\phase6s9\runs
ative-full-s9c-20261010-183613`; one warning, an HTTP timeout in the environment). No test name was added, so TestVoxel's count stays 147.

## 5. The manager's calls (October 10)

- The plan in the handoff's order; the ceil15 guard (a) with its own mutation.
- The block-set exemption, tighter (1a); the pooled check (2a) with T's two conditions (A); the exact rare-block test (B) with the D scale (E), D logged; the pooled bound 3.5 after recalibration (F), at most 10/1000 and mutation 5 failing.
- "Beehive x2" recorded as not catchable (C).
- Mega jungle's vines: keep the wiki's 75% trunk_vine. If a later probe shows the per-side rate really is about 0.70, independent of which sides count as open, switch to the probe.
- Pale moss: keep the data's 0.75 edge chance; record the ground's +5%.
- `attached_to_leaves`, fallen trees and huge mushrooms move to S9d.
- Two managers: the first (session_01Rm6t4pNqopFBUje9vnNw8Q) until the D3 step, then session_01PuWLpgQipHqcox6dnoviYH.

## 6. End to end, photos and the gate

- **Photos** (`Tests/Evidence/2026-10-10/S9c-Decorators/`, `contact-sheet.png`, `notes.md`; the manager's look call (a), accepted): old growth taiga now shows the giant 2 x 2 spruces; jungle trunks and leaves carry vines; the pale garden's ground is moss blocks and carpets, with hanging moss under the canopy; leaf litter shows in the dark, plain and dappled forests. The control pair (oldgrowth-r3's camera): trees on mean 91.6, dark 1.0%, white 0.5%; trees off mean 95.5, dark 4.3%, white 0.0%, with no trees at all. Not visible: cocoa and bee nests (rare), the creaking heart (inside a trunk), podzol rings (old growth taiga's own surface podzol looks the same; the trees-off frame shows it too), and shelf mushrooms on trunks. Taken before the review's fix to pale moss's upper carpets.
- **For S9d's photo list:** a low podzol shot on flat old growth ground with a trees-off pair, and a close trunk shot for cocoa and shelf mushrooms.

- **The S9 bench gate** (quiet window 19:20 to 19:28; `C:\dev\sandbox\phase6s9\s9b\gate9.ps1`, one bench per process, trees off and on alternating; outputs `C:\dev\sandbox\phase6s9\s9c\gate9-r1.txt` to `-r3.txt`, summarized by `gatesum9.py`; 0 java or editor processes before each launch, CPU 3 to 26%). Medians of 3, ms per column:

| Preset | Decorate, trees on - off | Finalize with trees (off) | Whole column with trees (off) |
|---|---|---|---|
| overworld | +0.687 (rounds +0.792, +0.687, +0.612): over the 0.4 allowance (S9b +0.395) | 0.453 (0.262) | 4.745 (3.848) |
| large_biomes | -0.074 (noise: few trees in its area) | 0.283 (0.298) | 4.069 (4.419) |
| amplified | +0.250 | 0.299 (0.252) | 5.294 (4.769); S9b 5.36 |

- **The flight** (in the quiet window, final code; `Tests/Evidence/2026-10-10/VoxelFlight-20261010-192809`): holes in 228 frames, at most 16 columns (circles 106, 64, 58), against S9b's 40 and 42 in its quiet window and 58 (at most 6) in its final e2e flight. Quads 2.18 M against 1.99 M (+9.7%, the decorations). Chunk work over 2 ms in 2 of 9,442 frames (worst 2.10). Memory is flat over laps 2 and 3 (process 5,032 -> 5,065 MB, columns 18.93 -> 19.61 MB).
- **The manager's call G (a):** commit as is with both recorded. S9d's second step (after the ReadFeatures crash fix) is a per-decorator cost breakdown in `voxel.Bench` and targeted cuts, gated on the 0.4 allowance, before any new decorator.
- **End to end** after the last code change: create-world PASS (`Tests/Evidence/2026-10-10/Worlds/Create-1791674054177`); the Playground probe 0 failures, 44.87 FPS (`Playground-all/Runtime-20261010-193042`); the flight above.
- **Calibration note:** the per-probe decoration rule's comment cites 138 runs in 1,000 with some probe failing (T2's count); with the pale garden's blocks the final calibration measured 161 (`fix-calib-20261010-185841`), the most for one probe still 21.

## 7. Known issues

| Issue | Due |
|---|---|
| Fallen trees, huge mushrooms and `attached_to_leaves` (with mangroves) not built | S9d |
| Mega jungle vines +7.7% (z +2.8): the probe's open trunk sides carry vines at 0.64 (dy 0) to 0.70 (examples, 244 of 348) against the wiki's 75% | When a probe settles the per-side rate |
| Pale moss ground about 5% high (pooled z: moss block +2.75, carpet +2.23, short grass +2.14); an edge chance of 0.65 would fit, the data says 0.75 | When the pale garden is probed again |
| Calibration conditioned on one seed of our 2,000 trees | A test session |
| Beehive x2 not catchable at 100-tree probes | Needs larger probes |
| Ring spreads (leaf litter, beehive radius) checked only in Python, not by the test | A test session |
| alter_ground's vertical search, place_on_ground's top-of-column rule, and the litter's log shadow fitted on flat ground only | When probed on slopes |
| Pine crown rows C 4, 5 and 6 odd each have an unseparated alternative | A wider pine probe |
| Dark forest leaves bright green, not the dark forest tint (also in S9b) | Small fixes |
| Decorate with trees +0.687 ms on overworld against the 0.4 allowance; Finalize with trees 0.453 | S9d, step 2 (a per-decorator breakdown and cuts) |
| Flight holes in 228 frames (at most 16 columns) against S9b's 40 and 42 | S9d step 2, then a streaming session (S9b's levers) |
| The review's unfixed lows: small-count rounding after D; the broad "left out" excuse; alter_ground's per-tree allocations | Whoever touches those |
