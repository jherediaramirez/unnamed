# Water and sky W3c: the ice flicker fixed, water shine 0.25

October 12, 2026, lane `feature/water-w3c` (worktree `C:\dev\projects\Unnamed-water`, from `b89c3c0`). Jesus trimmed W3c to two items: the frozen-lake flicker he found while playing, and his water shine pick from the brightness pass. Opacity, streaks, the water fog and the probe step move to the next small session ([its handoff](2026-10-12-WaterSky-Handoff-W3d.md)).

## Decisions

- **Plan** (manager, within the settled decisions): the see-through part below, the ice capture as its control pair, and WaterSpecular 0.25 with a before/after pair.
- **Rejected fixes:**
  - the After-DOF translucency pass for the translucent layer: it would draw ice over break cracks and particles, and an aquarium's far glass over its water;
  - moving faces between layers in the mesher: it would shift the `Mesher.Digest` goldens.

## The ice flicker (item 0)

- **What Jesus saw** (2026-10-12, a frozen lake): a checkerboard of dark blue squares that changed as he moved.
- **Not a z-fight.** Under ice, the water's top lies at 8/9 of its cell (`VoxelMesher.cpp`, the fluid top's plane), 1/9 block under the ice's bottom face.
- **The cause: a tie in the translucent sort.**
  - Ice (the Translucent layer) and water (the Water layer) were two translucent sections of the same column primitive.
  - UE sorts translucent draws by primitive priority, distance to the primitive's bounds origin, then `MeshIdInPrimitive` (`BasePassRendering.cpp` `CalculateTranslucentMeshStaticSortKey`, `MeshDrawCommands.cpp` `UpdateTranslucentMeshSortKeys`).
  - RealtimeMesh never sets `MeshIdInPrimitive`, so every section has 0, and both sections share one bounds and one priority.
  - The engine's unstable sort then ordered each column's pair arbitrarily, anew as the visible set changed. A column whose water drew last read dark.
- **The fix: the see-through part** (`UVoxelChunkComponent`).
  - The column's translucent layer draws from a child component of the same class, made when the column first has translucent geometry and pooled with it.
  - `BuildStreams` writes that layer last. `SetMesh` cuts it off the streams' tail (`SplitSeeThrough`, a plain copy of each stream's tail with the triangles rebased) and hands it to the part.
  - The part has the same bounds, a `TranslucencySortDistanceOffset` of -1 cm (so within its column it draws after the water), and follows its column's visibility, far look and destruction.
  - Between columns the distance decides, as before. `voxel.SeeThroughPart 0` is the control: it applies to columns meshed after the change.
  - Known edge (by design): a camera in water looking at glass in the same column also gets the glass after the water. Water tops are one-sided, so the ice-from-below case draws no water top at all.
- **Collision, targeting, break and place** read block data only: no code in `Source/Unnamed` traces meshes, and the meshes have no collision. So the part cannot change them.

## Results

- **The ice pair** (`CaptureVoxel -Fluids ice`: a frozen 48 x 48 pool over 3 x 3 columns on VP1's Superflat, 16 poses stepping 0.75 blocks at 45 degrees, then one from above; the dark share of the pool's pixels, luma under 120):

  | Pose | Part off (`voxel.SeeThroughPart 0`) | Part on |
  |---|---|---|
  | path 00 | 25.35% | 0.66% |
  | path 04 | 15.70% | 0.79% |
  | path 08 | 9.01% | 0.90% |
  | path 12 | 1.72% | 0.84% |
  | above, per column | 89 91 78 / 0 0 2 / 0 50 78 | 1 0 4 / 0 0 2 / 0 0 3 |

  With the part off, whole columns go dark, and which ones changes with the pose. With it on, none do (the remaining under 1% is texture shade).
- **Frozen ocean flight** (dev seed, spawn at `voxel.FindBiome frozen_ocean`'s ice spot (-1464, -1528), render distance 12, the full flight):
  - first-view apply: part on 1,993 columns in 397.6 ms (0.1995 ms per column); part off 390.3 ms (0.1959). That is +1.9%, within this PC's run-to-run noise;
  - holes: none in either run; far holes 4,643 against 4,644 frames (step 2's far-ring measure, the same both ways); errors besides LogPython: 0;
  - parts: 236 of 661 columns had one at the start (all with geometry, none hidden); at the end 336 parts for 2,471 columns, 0 with geometry, 0 hidden with geometry. No part outlives its column's geometry.
- **On ice in the generated world** (the views suite with its day pad moved onto the frozen ocean, sandbox poses file):
  - stone placed on the ice by an authority command, and its outline shots (modes 1, 2, high contrast) pass;
  - the slab, plant and preview cases failed: the pad's next cells and the player's stance were open water (the ocean is only partly frozen), so the ice cell itself was not targeted.
- **Water shine 0.25** (`voxel.Look.WaterSpecular`, the scenes capture at 0.5 against 0.25): the water's mean luma drops 2-5% (spread above 76.3 -> 73.7, pool 78.7 -> 74.7, terraces 70.8 -> 69.1); no sun glint falls in these poses. `MPC_VoxelLook` was re-saved by `CreateBlockMaterials.py -Materials=` (no material rebuilt); a Content hash before and after shows only `MPC_VoxelLook.uasset` changed.
- **Tests:** `Unnamed.Voxel.Mesher.SeeThroughPart` (new): the part gets exactly the translucent layer's vertices and triangles, rebased and in order, and the column keeps the rest; no split without translucent triangles. `Unnamed.Voxel.Look` now pins 0.25. The mesher tests, `Mesher.Digest` included, pass unchanged.
- **Hashes, goldens and saved worlds:** none move (render only).

## What changed

- `Voxel/VoxelChunkComponent.h/.cpp`: the see-through part, `SplitSeeThrough`, the layer order, `voxel.SeeThroughPart`, the part counters.
- `Voxel/VoxelFluidShots.h/.cpp`, `Scripts/CaptureVoxel.ps1`: the `ice` scenario.
- `Voxel/VoxelFlight.cpp`: the flight summary's "Chunk components" line (columns and parts at the start and the end).
- `Voxel/VoxelLook.h`, `Scripts/Editor/CreateBlockMaterials.py` (`LOOK_PARAMETERS`), `Content/Blocks/MPC_VoxelLook.uasset`, `Tests/VoxelLookTests.cpp`: WaterSpecular 0.25.
- `Tests/VoxelMesherTests.cpp`: the new test. `Docs/VoxelWorld.md`: the part, and the checkerboard's known issue closed.

## Evidence

`C:\dev\sandbox\water-w3c\`:

- `item0\ice-pair-sheet.jpg`, `icedark.txt`;
- `shots\ice-part0`, `shots\ice-part1`;
- `flights\frozen-part1`, `flights\frozen-part0`;
- `views\run1` (on ice);
- `item0\specular-pair-sheet.jpg`, `specular-pair.txt`; `shots\spec050`, `shots\spec025`;
- `viewset\` (2560 x 1440: frozen river, frozen ocean edge, shore), `item0\viewset-sheet.jpg`;
- the tools: `build.ps1`, `runtests.ps1`, `fluids.ps1`, `flight.ps1`, `viewset.ps1`, `tools\icedark.py`, `tools\glint.py`.

## Tokens

The lead was at 172k for the plan and about 330k at the wrap-up. One Sonnet review subagent.
