# Water and sky W3b: Playground parity, the look shots beside vanilla, open corners and the flip

October 11, 2026, lane `feature/water-sky` (worktree `C:\dev\projects\Unnamed-water`). W3b finishes W3 (plan section 5): the Playground draws sloped fluid cells as the voxel mesher does, a capture rebuilds the vanilla client probe's water scenes beside its frames, the corner rule gained open corners from those shots, the mesh cost is measured, and `voxel.Fluids` defaults to 1. It began with the step-0 merge of main (`5367b18`). After W3b the water lane stops: Jesus moved to one building agent at a time, and the next water session ([its handoff](2026-10-11-WaterSky-Handoff-W3c.md)) starts only on his word.

## Decisions

- **The plan** (manager, under Jesus's away rule): as proposed. Handoff Q3 went to **(b), per-instance custom data**, not (a): (a) put the flow code with `ModelCodeBit` (2^20 and up) into UV 1, which breaks the rule that integer codes in float vertex channels stay under 2^15. With (b) the sloped meshes carry geometry and UVs only, and every code stays in the instance's custom data as today's face codes do.
- **The scenes camera's field of view is 77 degrees vertical, measured.** The probe set `fov 70`, but its spectator camera flies, which widens the view: its frames showed the ledge 1/1.138 as wide as a 70 degree camera does (s2_fall_front45, 290 against 330 px; the same in s2_fall_lip), which is 2 atan(1.138 tan 35) = 77. After the change the ledge measures 204 and 276 px in both renders (under 1%). Only the capture camera uses it; the game's field of view is unchanged.
- **Look call (manager, Jesus's away rule): open corners on by default** (`voxel.Fluids.OpenCorners 1`, `FVoxelMeshOptions::bOpenCorners`); 0 stays one switch away. Handoff Q1 (the diagonal) keeps (a): no pose can show it (tops are lit as up, and the silhouettes differ by under a pixel). Q2 keeps the sources' plateau: vanilla's low view shows a flat top about one block wide (65 px at 62 px per block), with no sag.
- **The direction check (Jesus's priority, "how the water looks when it goes off in different directions"): no rule change.** Our corner rule's flow axis matches vanilla's measured streak axis (below). Our render's streaks are faint, so the next session raises their contrast.
- **Jesus, after seeing the comparison pictures:** it acts more like vanilla. Water colour is fine for now (the colour, the sun glint and the blue-tinted shadows are dropped). He wants less see-through water and Minecraft's water fog: the next session's first item.
- **The flip:** `voxel.Fluids` defaults to 1 (flowing water and lava run in every voxel world), with the 0/1 capture pair below.
- **Builds pass `-NoHotReloadFromIDE`** (as `BuildFoundation.ps1` does): Jesus's play copy holds the engine-wide Live Coding lock, and the flag skips only UBT's check; it writes this worktree's binaries. `RunChecks` ran with `-Skip build` after such a build.

## What changed

- **`Foundation/FoundationBlockMeshes.h/.cpp`:**
  - `HoldsFluid`, `PackFluid` (a state packed as the mesher packs it), `MakeFluidSurface` (the ring from the block world's states, then `VoxelFluidSurface::Make`);
  - `GetFluidFaces`: the six faces in `FluidFace`'s corner order, so the top splits along the same diagonal;
  - `GetFluidSurfaceKey`: the four corners in 108ths, exact for every corner the rule makes;
  - `BuildFluidSurface`: explicit triangles, the winding fixed by swapping corners 1 and 3, which keeps the 0-2 diagonal.
- **`UFoundationInteractionView`:** a sloped cell takes a cached mesh after the kinds and the models (`GetFluidSurfaceMesh`) and `MakeFluidSurfaceData` (the flow code on a top that runs downhill, `lava_flow` on flowing lava's sides). `RefreshFluidsAround` replaces `RefreshWaterBelow`: a change redraws the drawn fluid cells 3 x 3 x 2 around it, and `ApplyChanges` removes whatever is drawn at a changed cell before adding its state. Flat cells keep their kinds, and every cell is still one instance.
- **Open corners** (`VoxelFluidSurface::OpenCell`, `Make(..., bOpenCorners)`): an air cell among a corner's four counts as height 0 in the mean of the flowing cells. A corner with a source or a full cell is unchanged, so generated fluid (all sources) meshes the same either way. The mesher packs air as `OpenCell` when the option is on; the full-bit loop now tests the fluid bits. Switch: `voxel.Fluids.OpenCorners` (`FVoxelFluidSystem::GetOpenCorners`), through `FVoxelColumnPipeline::SetSlopedTops(SlopedTops, bOpenCorners)` and `UVoxelWorldSubsystem`, read by the Playground when it draws.
- **`voxel.Fluids` defaults to 1** (`VoxelFluidSystem.cpp`).
- **`FVoxelFluidShots` scenario `scenes`** (`CaptureVoxel.ps1 -Fluids scenes`): it checks for VP1's Superflat ground, builds facts.md's five scenes by its commands in order, and shoots its 20 poses at its times (water from 8 s, lava from 35 s). It runs at `voxel.Fluids` as set. `scenes.txt` holds the probe's eight RCON reach checks, each scene's ground as levels, and the mesh jobs since the build.
- **Tests** (`VoxelFluidMeshTests.cpp`, now 5 declared):
  - `Mesher.FluidPlayground`: the parity, under modes 1 and 2 and open corners;
  - `Mesher.FluidCost`: logs the cost; asserts only that water meshed;
  - `Fluid.Surface`: gains the open-corner cases.
- **Docs:** `VoxelWorld.md` (Fluid surfaces: open corners, the Playground, the flip; the test rows), `Blocks.md` (the meshes paragraph).

## Results

- **Parity:** `Mesher.FluidPlayground` compares 374 faces per variant (a 113-cell spread, a waterfall's foot, seagrass's water, lava), with 0 mismatches in corners, UVs and codes under mode 1, mode 2 and open corners.
- **Defaults:** at open corners on, the mesher and fluid tests pass 35 of 35, and `Mesher.Digest` equals every golden (overworld plants off `0xa8ba8d0d0a39174f`, large_biomes `0xe49601da349c7c0d`, amplified `0x7f9491318204f4b2`). `Mesher.FluidGenerated` gives mode 1 = mode 0 (`0x195088fb4d3615e3`, 1,003 water quads, on main's new generation).
- **Mesh cost per touched section** (`Mesher.FluidCost`, median of 41; indicative, with other lanes' editors and Jesus's game running):

  | Section | SlopedTops 0 | 1 | 2 |
  |---|---|---|---|
  | 113-cell spread | 0.165 ms | 0.199 ms | 0.197 ms |
  | 256 mixed levels | 0.190 ms | 0.246 ms | 0.247 ms |

  An earlier run under different load gave 0.100 -> 0.161 ms for the spread. In the scenes runs, the remesh jobs while water flowed averaged 0.25-0.30 ms per column job. All of this is within scout M's 0.2 ms estimate per section, give or take load; it is not a gate.
- **The scenes against vanilla** (water and lava mask IoU below the horizon; widths in px at fixed rows; `WaterSky-W3b-look`):
  - Reach: 8 of 8 as the probe in every run with fluids on.
  - Sloped against flat (SlopedTops 1 against 0): closer to vanilla on 17 of 18 measurable poses (fall front 0.711 -> 0.876, lava fall side 0.413 -> 0.593).
  - Open corners against sloped: the stream and lip widths go from 10-30% wide to vanilla's within 4%:

    | Place | Vanilla | Sloped | Open corners |
    |---|---|---|---|
    | Terrace, close-down | 182, 238, 248 | 200, 256, 274 | 182, 236, 248 |
    | Terrace, 45 deg | 98, 214, 310 | 108, 236, 352 | 96, 214, 310 |
    | Lip | 284, 308 | 358, 402 | 295, 312 |

    Lava IoU: spread45 0.945 -> 0.990, spread_low 0.882 -> 0.953, fall_front 0.914 -> 0.951. Water IoU is the same or better on 14 of 16 water poses.
- **Direction** (a comparer's per-cell structure-tensor streak axis on rectified patches, 111 cells, plus a port of the rule, `ruleflow.py`):
  - Vanilla's axis against our rule's flow axis, cells where vanilla's streaks are clear (coherence 0.2 and up):
    - spread close-down: 14 cells, median 3.6 deg, max 12.7;
    - spread at 45 deg: 38 cells, median 7.8; 8 over 15 deg, all rim cells or the oblique view's diagonal pair;
    - terraces at 45 deg: 13 cells, median 1.2.
  - Our render's streaks read at coherence 0.25-0.4 where vanilla's read at 0.5-0.7. Next to the terrace steps ours reads across the flow (0-2 deg against 90), most likely because the stone edges seen through our paler water outweigh the streaks. The ground stream reads right (93-95 against 90).
- **The flip pair** (`WaterSky-W3b-flip-1`, `-0`):
  - at 1, reach 8 of 8, and the frames match the sloped look above;
  - at 0, reach 2 of 8 (only the "none" checks), and the frames show the placed sources and the pool standing still (pool IoU 0.56-0.86, spread 0.006).
- **The step-0 merge:** `RunChecks -Profile merge -Skip build`, GREEN in 21 min (`Checks-merge-20261011-022726`):
  - native 257 of 257, crashes 0; TestVoxel 164;
  - radius-8 and `Mesher.Digest` 9 of 9 equal to main's expected hashes;
  - the Playground probe 119, interaction 81 and tour 27, each with 0 failures; create-world 32;
  - npm 57, source-check, the verifiers.
- **The final checks** (after the review fixes, on the last code; `RunChecks -Profile final -Flight smoke -Skip build` after a `-DisableAdaptiveUnity -NoHotReloadFromIDE` build): GREEN in 46 min (`Checks-final-20261011-025637`):
  - native 259 of 259 (257 + the 2 new tests), crashes 0; one LogHttp warning in `Decoration.Preview`, the engine's known timeout;
  - TestVoxel 166 (164 + 2);
  - radius-8 and `Mesher.Digest` 9 of 9 equal to main's expected hashes;
  - the Playground probe 119, interaction 81 and tour 27, each with 0 failures; create-world 32;
  - the smoke flight passed in 144 s with no errors besides LogPython (it waited out the main lane's quiet window first);
  - npm 57, source-check, the verifiers.

## Known gaps

- **Faint flow streaks and see-through water:** the next session's item 1, Jesus's decision, measured with the flowdir scripts.
- **The Playground's flat flowing lava** keeps `lava_still` sides, where the mesher draws `lava_flow` (Q2 (a): parity covers sloped cells). When: with the next Playground water change.
- **One component per surface shape per section:** each distinct set of corners is its own instanced mesh per section, so a large varied spread in the Playground adds draw calls. Its water areas are small. When: cap it if a Playground pool grows.
- **Open corners at a loading edge:** air in an unloaded neighbour's padding counts as open, so flowing water thins there until the column loads. Generated fluid is unaffected. When: W5, with generated edges.
- **No sloped cell in the Playground's own scenes yet:** the showcase's water levels stand apart (tour shot 15 is unchanged), so the sloped meshes are proven by `Mesher.FluidPlayground`'s geometry, not yet by a render. When: W3c, with a placed spread in a Playground shot.
- **Carried:**
  - waterlogged slabs draw dry;
  - water under lava draws no surface;
  - mode 1's see-through step between equal-level sources;
  - the Playground's z-fight;
  - W2b's list.

## Subagents and tokens

- **Lead** (MeasureAgentUsage):
  - 157k after the reading list;
  - 308k at the first checkpoint (the manager accepted the case to finish here);
  - 416k at the merge report;
  - about 440k at the final checks.
- **Subagents** (Sonnet 5.5):
  - crop comparer, 75.5k peak;
  - flow-direction comparer, 58.8k;
  - pre-commit reviewer, 125.4k (diff of 1,468 lines).
- **What cost the most:** reading images in the lead (about 1-6k each) before the comparers took over.

## Evidence

- `Tests/Evidence/2026-10-11/WaterSky-W3b-look/`:
  - `look-open-corners.png` (vanilla | sloped | open corners: terrace, lip, lava spread);
  - `cmp-oc_sheet.png` and `cmp-oc_iou.csv` (all 20 poses);
  - `cmp-b_iou.csv` (sloped against flat), `cmp-flip_*`, `widths-oc.csv`, `agent-compare.md`;
  - `flowdir/` (`findings.md`, `cells.csv`, `flow.py`, `ruleflow-output.txt`);
  - the runs `scenes-m1` (70 degrees), `scenes-m1b`, `scenes-oc1` and `scenes-s0b`, each with its `scenes.txt`;
  - the scripts `compare.py`, `widths.py`, `crops.py`, `ruleflow.py`.
- `Tests/Evidence/2026-10-11/WaterSky-W3b-flip-1/`, `-flip-0/`.
- `Tests/Evidence/2026-10-11/Checks-merge-20261011-022726/summary.txt`.
- `C:\dev\sandbox\water-w3b\`:
  - `scenes.ps1` (a scenes run on a fresh copy of the Superflat world), `worlds-template\vp1-flat`;
  - `build.ps1`, `runtests.ps1`, `measure.ps1`;
  - `patches\`, `review\w3b-review.md`;
  - `wip-before-merge.patch`.
