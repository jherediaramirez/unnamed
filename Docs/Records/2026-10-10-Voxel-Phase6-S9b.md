# Voxel world phase 6, S9b: the streaming cost with trees, and the big trees - October 10, 2026

The main lane's S9b. It profiled the flight's holes under trees and fixed most of them with two levers (the gate passed in part, the manager's call). It then ported the big trees: dark oak, pale oak and mega jungle ship after refits of their fits, and mega spruce and mega pine stay off until S9c. [Docs/VoxelWorld.md](../VoxelWorld.md) has the rules ("Big trees and the streaming cost (phase 6, S9b)"); [the S9c handoff](2026-10-10-Voxel-Handoff-Phase6-S9c.md) has what comes next.

## 1. The streaming cost with trees

**Profile** (before any lever; `C:\dev\sandbox\phase6s9\s9b\streaming-notes.md`):
- **Quads:** the headless radius-8 overworld bench gives 1,812 quads per column with trees off and 2,857 with trees on. Leaves are 1,008 of them, all in the cutout layer.
  - Leaves never hide faces against each other (vanilla's fancy leaves).
  - The greedy merge keyed on the full state, so leaves of different `distance` never merged.
- **The holes come from apply throughput, not the workers.** In S9a's hole frames, 27 to 57 meshes were ready but one column applied per frame.
  - An apply costs about 0.33 ms fixed plus 0.09 ms per 1,000 quads, and a tree column (5,000 to 7,000 quads) about 0.9 ms.
  - The ready list sorted nearest first, regardless of where the camera looks.
  - Every frame over 2 ms was a column-crossing frame (plan and jobs about 1 ms) plus the frame's forced first apply.
- **Memory** (LLM, a Sonnet agent):
  - +275 MB tracked. 206 MB of it is `LoadMapMisc`, an artifact of the trees-off run rebuilding after map load.
  - The rest scales with quads, at about 0.2 to 0.4 KB per on-screen quad (RealtimeMesh's CPU stream copies: about 220 B per quad from the stream layout).
  - Laps 2 and 3 are flat. The manager accepted this as explained.

**Lever 1, merge by look** (no change to the look):
- `FVoxelBlockTable::GetLookState`: the lowest state of a block equal in every field the mesher reads. It lives in separate arrays, because the water lane adds fields to `FVoxelStateInfo`.
- The mesher keys full-cube faces on it (`FVoxelMeshOptions::bMergeLooks`).
- Leaf quads -22%, all quads -7.8% with trees.
- Trees off, every digest and hash is unchanged. `Mesher.Digest` gets a trees-off case again, pinned to S8 part B's values.

**Lever 2, apply scheduling:**
- (a) The frame's first column waits one frame, once, when it fits the budget alone and the frame's planning and jobs leave it no room.
- (b) Ready columns behind the view sector apply as if twice as far.
- Both landed on T3b's fast-forward (`c90be1f`) after the manager's hold.

**The gate** (the manager's quiet window, 14:29 to 14:42). One flight per process, old (`c90be1f`) and new (S9b) DLL pairs alternated; every run started with no other editor or java, CPU 2 to 28%, MegaLights on:

| Run | Hole frames (most columns) | Over 2 ms (worst) | First view | Laps 2-3 | Evidence |
|---|---|---|---|---|---|
| new, trees on | 40 (5) | 3 (5.07) | 3.49 s | 4855 -> 4864 MB | `VoxelFlight-20261010-142956` |
| old, trees on | 91 (12) | 19 (2.94) | 3.44 s | 4683 -> 4743 MB | `-143204` |
| new, trees off | 0 | 2 (4.82) | 2.21 s | 4286 -> 4288 MB | `-143411` |
| new, trees on | 42 (8) | 4 (2.43) | 3.39 s | 4723 -> 4731 MB | `-143618` |
| old, trees on | 107 (12) | 22 (3.20) | 3.49 s | 4887 -> 4905 MB | `-143826` |
| new, trees off | 0 | 1 (2.39) | 2.10 s | 4183 -> 4184 MB | `-144033` |

- **Read (the manager's call, the gate partial):**
  - frames over 2 ms are met;
  - holes are down 58% but not at trees-off's none;
  - memory is explained.
- **What is left:** apply throughput in the circles (known issue). Next levers for a streaming session:
  - RealtimeMesh's commit off the game thread, or a column's apply split across frames;
  - trimming the stream's bytes per vertex.
- **Not done:** lever 3, leaves culling same-leaf faces. It is a look change, and vanilla's default is fancy leaves.
- **The review fix:** a column larger than the whole budget no longer waits, and the wait is tied to the column. It changed lever 2a after the window. The e2e flight below runs on the final code.

## 2. The big trees

| Group | Result | How |
|---|---|---|
| Mega spruce, mega pine (`giant` + `mega_pine`) | Written, off in `FindType` | Ported as fitted. Leaves +6.5% and +21%, occupancy p 0.000; the crown radius by height and parity is unsolved (a linear rule search found nothing better). S9c, a refit. |
| Mega jungle (`mega_jungle` + `jungle`) | Ships | The fit failed one occupancy cell (p 0.001), so a Python refit (ours, fitted). Four-step branches from the (1,1) column at `(int)(1.5 + t cos a)`, rising one per two steps; disc canopies r1 and r2, plus r3 one below on a coin drawn in the trunk's stream (`FVoxelFoliageAttachment::bBottomLayer`). The six example trees rebuild with 0 mismatches. |
| Dark oak, pale oak, pale oak creaking (`dark_oak`) | Ship | The fit failed occupancy (p 0.000). It was not a transpose: the probe has a fixed world-frame asymmetry. The refit: branches on the ring around the 2x2 at 0.31 each, length 2 to 4, each with its own leaf blob; the top-log layer a fixed 55-cell shape and the layer below a 36-cell square; the lean starts at h - 1 - M. The example trees match leaf for leaf. |

- **Final probe lines** (`C:\dev\sandbox\phase6s9\runs\`):
  - mega_jungle_tree: logs 87.20/84.24 (+3.5%, z 1.1), leaves 273.60/270.05 (+1.3%), occupancy p 1.000, reach 8/8 (`mj-20261010-150449`).
  - dark_oak_leaf_litter: logs 40.87/40.30 (z +0.9), occupancy p 1.000; pale_oak and pale_oak_creaking alike; ceilings 5 to 11 pass (`dko-20261010-144621`).
  - All 46 of S9a's probe lines are byte-identical through every step.
- **Mutations** (each restored byte for byte by SHA-256, rebuilt `-NoUnity`, re-run identical):
  - dark oak branch blobs removed: all three fail (leaves -12.4%, z -22.8, occupancy p 6e-41; `mutdko-20261010-145035`);
  - mega jungle r3 layer removed: leaves -10.4%, occupancy p 0.000 (`mjmut-20261010-150622`);
  - giant soil removed: "soil under the trunk 0.00 (one per trunk column, alter_ground left out: 4.00)" (`bigamut-20261010-135306`).
- **A changed test rule** (the manager's call):

| Rule | Old | New | Why |
|---|---|---|---|
| Soil under the trunk, block set | dirt mean against the probe's; every written block in the probe's blocks | for a tree whose alter_ground decorator is left out, soil under the trunk exactly one per trunk column, and the soil left out of the block set; every other tree as before | the giant probes show podzol (alter_ground turned the dirt), so no tree could pass while the decorator is S9c's. Removed in S9c when alter_ground is built. |

- **Test counts:** `Trees.Shapes` 27 -> 31 probes, `Trees.FeatureSize` 19 -> 26 ceilings; `TestVoxel.ps1` 111 -> 112 (`Blocks.LookStates`).
- **Recorded:** mega jungle's branch logs stand upright (axis Z), because the probe's `log_axis` is all "y". `mega_spruce_ceil15` would fail "placed none against some" (ours 2.2%, probe 0 of 16, binomial p 0.70) once giant ships: a test-rule question for S9c.

## 3. Re-records

- **`Mesher.Digest`, trees on** (lever 1 only; the blocks are unchanged), models on / off:
  - overworld `0x2d204b0b4cf1abd8` / `0x6ee399181d5fccd7`;
  - large_biomes `0xca80f4d03a73a6f9` / `0x762f1bf60ca6a7e5`;
  - amplified `0x49802047907506c6` / `0x627b48a897480211`.
  - The new trees-off case equals S8 part B's six.
- **`Decoration.Golden`'s third case** is unchanged: no dark oak or mega jungle lands in seed 263's radius-4 area.
- **Radius 8, trees on** (both load orders equal; `C:\dev\sandbox\phase6s2\runs\r8on-151008`):
  - overworld `0x194a623a86b97dd6` (was `0x52c29bae6e786bfc`) and amplified `0xbde96a3deaa9d130` (was `0xcc437793fbd65281`), because dark oak and mega jungle place;
  - large_biomes `0x1b665f4703a79406` is unchanged.
  - Trees off reproduces S8 part B's `0xddb7e949cd5e191a`, `0xcbac9722096ef7c5`, `0x61c63a3490017e2e` (`r8off-151135`).

## 4. Results and evidence

- **Suites** (after the review fixes, `-NoUnity` build on `0ecd426`):
  - `TestVoxel.ps1`: 112 of 112, one HTTP-timeout warning (`Tests/Evidence/2026-10-10/VoxelAutomation-20261010-152509`), and again with the committed script's count of 112 (`VoxelAutomation-20261010-160552`);
  - every native suite: 191 of 191, 2 warnings (`C:\dev\sandbox\phase6s9\runs\native-full-20261010-152723`).
- **The review** (Sonnet, diff only): 1 bug, 6 risks, 5 nits (`C:\dev\sandbox\phase6s9\s9b\review\findings.md`).
  - Fixed: the bug (a column larger than the budget lost a frame), R1 (the wait tied to its column), R4 (a test for a state moved to another block) and the nits.
  - Recorded:
    - R2: a field the mesher starts reading must join `IsSameLook`; a size assert cannot see padding fields.
    - R3: out-of-order `SetState` is O(n^2) per call.
    - R5: the jungle's truncation relies on libm near integers.
    - R6: a column behind the view waits behind those ahead, bounded.
- **Photos for Jesus:** `Tests/Evidence/2026-10-10/S9b-BigTrees/` (`contact-sheet.png`; best `palegarden-r1`, `jungle-r2`).
  - The dark forest close-up and wide view, and pale garden.
  - Old growth taiga as the gap shot: no mega spruce yet.
  - A trees-off control pair: grayscale mean / near-black % / near-white % 100.0 / 3.0 / 1.1 on, 99.9 / 0.9 / 0.0 off.
  - `notes.md` lists what each shot still lacks: podzol rings, vines, cocoa, leaf litter, pale moss, creaking hearts, huge mushrooms, fallen trees.
- **End to end and the bench gate:** section 6.

## 5. The manager's calls (October 10)

- Lever 1 and the profile; lever 2 after T3b's fast-forward; the memory explained.
- The alter_ground soil rule, with its conditions.
- Mega jungle's upright branch logs.
- The refits, run in order: dark oak first, then mega jungle; mega spruce and pine's radius table goes to S9c.
- The gate partial; no lever 3.
- The split: S9c is the decorators (alter_ground removes the soil rule), fallen trees and huge mushrooms, with the mega spruce/pine refit agent first; S9d is the rest.
- The S9 bench gate in this session, on the final code.

## 6. End to end and the bench gate

- **End to end** (final code, `-NoUnity` on `0ecd426`):
  - Create New World passes (`Tests/Evidence/2026-10-10/Worlds/Create-1791661367338`, under the CEF lock);
  - the Playground probe has 0 failures, mean 44.66 fps at the 60 cap (`Playground-all/Runtime-20261010-154939`; 1 other editor running, CPU 27%);
  - the flight (`VoxelFlight-20261010-155134`, not a quiet window): holes in 58 frames (most 6), 6 over budget, laps 2 and 3 4,909 to 4,926 MB, in line with the gate's new-build runs.
- **The S9 bench gate** (the manager's quiet window, 15:55 to 16:03, every process started with no other editor or compiler, CPU 0 to 28%): `voxel.Bench Decorate 4`, one bench per process, trees off and on alternating, medians of 3 of the second pass (early-out on), ms per column (`C:\dev\sandbox\phase6s9\s9b\gate9-r1..r3.txt`, `gatesum9.py`):

| Preset | Trees | Generate | Decorate | Finalize | Whole |
|---|---|---|---|---|---|
| overworld | off | 3.058 | 0.904 | 0.268 | 4.216 |
| overworld | on | 3.090 | 1.299 | 0.363 | 4.752 |
| large_biomes | off | 2.965 | 0.722 | 0.328 | 4.015 |
| large_biomes | on | 3.324 | 0.772 | 0.273 | 4.369 |
| amplified | off | 3.974 | 0.924 | 0.370 | 5.038 |
| amplified | on | 3.813 | 1.080 | 0.380 | 5.362 |

- **Trees on minus off, paired per round:**
  - Decorate: overworld +0.334, +0.395, +0.555 (median +0.395), large_biomes median +0.037, amplified median +0.156;
  - Finalize: overworld median +0.095, large_biomes -0.026, amplified +0.010.
- **Against the S9 gate:**
  - Decorate's allowance (0.4) is met on every median; overworld's is at the edge, and one of its rounds was over.
  - "Finalize at most 0.3 with trees" misses on overworld (0.363) and amplified (0.380). Trees off is already at 0.268 to 0.370 there, against about 0.18 in S8b's gate, so most of it predates the trees. It is likely the light work merged from T3/T3b (unverified), and it goes in as a known issue for a streaming or small-fixes session (the manager's call).
  - Amplified's whole column, already over 5 ms, rises further (+0.32 ms): a flag under the plan's section 3.5, an FYI for Jesus, since he has ruled out an AMPLIFIED speed-up session for now.
