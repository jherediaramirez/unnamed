# Water and sky W3 part 1: fluid surfaces (core, mesher, materials)

October 11, 2026, lane `feature/water-sky`. W3 (plan section 5) was cut in two at its 300k checkpoint (the manager's call): this part builds the surface rule, the mesher, the switch and the materials, with `voxel.Fluids` still 0. W3b ([its handoff](2026-10-11-WaterSky-Handoff-W3b.md)) does the Playground parity, the look shots beside the vanilla probe, the mesh cost and the `voxel.Fluids` flip.

## Decisions

- **The plan** (manager, under Jesus's away rule, October 11), as recommended:
  - Q1: the materials change, one script and four regenerated assets, proven by a control pair;
  - Q2 (a): Playground parity covers sloped cells only; flat cells keep today's boxes and instance counts, and the z-fight stays on the small-fixes list;
  - Q3: waterlogged slabs drawn wet are deferred (Known gaps);
  - Q4: the look calls go beside a vanilla client probe's shots (run tonight; for W3b).
- **The corner rule is ours.** The wiki gives the heights (amount / 9, or 1.0 under the same fluid), says the fill "depends on the neighboring fluid blocks above each upper corner", and describes the current as a vector sum over the four neighbours. No game code was read. At each corner:
  - one of its four cells drawn full makes it full;
  - else a source among them puts it at a source's 8/9, so a source keeps its plateau;
  - else it's the mean of the flowing cells.
  
  Other fluids and non-fluids don't count. The flow runs down the steepest descent of the four corners. Both are to be tuned against vanilla in W3b.
- **"Strips above the neighbour's height"** became shared corners. Neighbours compute each corner from the same four cells, so their tops meet exactly, and the sides between two cells of one fluid stay hidden with no gap. A side facing out is a trapezoid under its two top corners.
- **Mode 1 (Q5 (b))** applies the rule only at corners whose cells' levels differ (amount and falling). Where they are equal, the corner takes the cell's own height, so generated fluid (all sources) meshes byte for byte as mode 0. "Fluid above" keeps today's meaning (any fluid), for the same reason.
- **Lava's sides:** `lava_flow` where lava flows (levels 1-15) under 1, and on every lava side under 2. Lava sources keep `lava_still` under 1 so the digest holds.
- **Materials pick the texture array from the texture table's fourth channel** (1 for the flow array), not from the face. A sloped top is an up face drawing the flow texture. Chunk materials sample the flow array at `frac(raw * 0.5)`, so a turned top's UVs, which cross block lines inside one texture, never wrap.

## What changed

- **`Meshing/VoxelFluidSurface.h/.cpp`** (core, new; shared with the Playground in W3b):
  - `Pack`: one byte per cell (amount, falling, fluid, full);
  - `Make`: a cell's four corner heights and flow from its 3 x 3 ring, under modes 0-2;
  - `GetCorner`;
  - `GetFlowUVs`: the flow texture turned so its v runs downhill, centred, at half scale, inside the 32 x 32 texture.
- **`VoxelMesher`:**
  - `FVoxelMeshOptions::SlopedTops` (default 1);
  - a packed fluid array per padded section, built only when the section holds fluid and the mode isn't 0;
  - `FEmitter::FluidFace`: Quad's corner order, with the vertices at the cell's top moved to the surface corners;
  - in the fluid case, a sloped cell draws its top (the flow code and UVs when it runs downhill) and its outward sides through `FluidFace`;
  - flowing lava's flat sides draw the flow code;
  - everything else takes the old path unchanged.
- **Table:** `FVoxelBlockTable::SetFlowCode`/`GetFlowCode`, one per fluid. `FVoxelStateInfo` is unchanged. `VoxelBlocks::MakeTable` sets `water_flow` with the tint bit, and `lava_flow`.
- **Switch:** `voxel.Fluids.SlopedTops` 0/1/2 (default 1), in `VoxelFluidSystem.cpp` (`FVoxelFluidSystem::GetSlopedTops`). `UVoxelWorldSubsystem` reads it at start and every tick. A change remeshes every loaded column through the new `FVoxelColumnPipeline::SetSlopedTops`.
- **Texture table:** `UFoundationBlockTextureTable` writes 1 into a flow-array texture's fourth channel. `SetTick` rewrites only channels 0-2.
- **Materials:** `CreateBlockMaterials.py`.
  - `FLOW_SAMPLE` (water, `M_BlockOpaque`) and `CHUNK_FLOW_SAMPLE` (chunk water, `M_ChunkOpaque`) choose the array by `entry.w`.
  - `M_BlockOpaque` and `M_ChunkOpaque` gained the flow texture input.
  - Regenerated with compiled shaders: `M_ChunkWater`, `M_ChunkOpaque`, `M_BlockWater`, `M_BlockOpaque`. The other four are untouched.
- **Tests** (`VoxelFluidMeshTests.cpp`, new; 3 declared tests, which TestVoxel counts from the source):

| Test | What it pins |
|---|---|
| `Fluid.Surface` | `Pack`; modes 0-2 on a pool, a full neighbour, a 5 -> 3 step, a source's plateau, a full falling neighbour, another fluid; every corner shared by 2-4 tops agreeing over 40 random fields per mode (2,956 and 2,969 corners); the flow UVs' turn, handedness and range. |
| `Mesher.FluidShapes` | The step's tops (5/9, 4/9, 3/9), trapezoid side, flow code and direction, and mode 0's stepped boxes; a level-2 pool still one merged top (modes 0-2); a 113-cell spread whose tops meet at every point (largest step 0) and all run downhill; a waterfall's ring (full at the column, 7/9 outside), whole column sides and a flat 8/9 head; kelp's water as a source; lava's side codes per mode. |
| `Mesher.FluidGenerated` | A generated area (overworld, seed 263, radius 2, decorated): mode 1's mesh digest equals mode 0's (`0x52cc4a888f61b0dc`, 1,003 water quads); mode 2 differs (`0xbf00f24d031e1ab5`). |

## Results

- `Mesher.Digest` matches every golden at the default (verified, not re-recorded). All 18 cases pass, among them overworld plants off `0xa8ba8d0d0a39174f`, large_biomes `0xe49601da349c7c0d` and amplified `0x7f9491318204f4b2`.
- **Mutations** (`C:\dev\sandbox\water-w3\mutate.ps1`, `mutations.txt`):
  - the flow's sign flipped fails `Fluid.Surface` and `Mesher.FluidShapes`;
  - mode 1 sloping at every corner fails `Fluid.Surface`, `Mesher.FluidGenerated` and `Mesher.Digest`.
- **Materials** compile (`-AllowCommandletRendering`, `BLOCK_MATERIALS PASS`):

  | Material | PS instructions | Samplers | Size before -> after (bytes) |
  |---|---|---|---|
  | `M_BlockOpaque` | 313 | 3 | 18,105 -> 19,093 |
  | `M_BlockWater` | 1,932 | 7 | 16,429 -> 16,432 |
  | `M_ChunkOpaque` | 293 | 4 | 16,793 -> 18,043 |
  | `M_ChunkWater` | 1,887 | 7 | 13,191 -> 13,552 |

- **Control pair** (AA off, 100% screen; the old assets swapped in for "before"):
  - The dev seed's five voxel shots (still water tops, flow-textured water sides on the cut, opaque terrain) differ by no more than two runs with the same materials do. Pixels off by more than 8, before/after vs after/after: overview 3.18% vs 2.65%; closeup 4.06% vs 4.09%; section 1.19% vs 1.76%; lumen-scene 0.17% vs 0.20%; ground 0% vs 0%.
  - The Playground tour's 47 shots: the water pool, the showcase water row, the water station and every opaque scene are 0.000% off. Only the moving shots differ (combat 3.7%, animated-b 4.6%).
- **Positive control** (W2a's breach scene, fluids on, so sloped tops; new against old materials): the frames with flowing water differ by 24%, 33% and 96% of pixels (3 s, 8 s, 20 s). The dry frame differs by 0.001%. The new 20 s frame shows a full-height column from the breach and a sloped pool with flow streaks running away from it.
- **RunChecks final with the smoke flight** (after the last code change): green in 38 min (`Tests/Evidence/2026-10-11/Checks-final-20261011-010120/summary.txt`): native 245 of 245 declared, crashes 0; TestVoxel 164; the radius-8 hashes and `Mesher.Digest` 9 of 9 equal to main's expected hashes; the Playground probe 119, interaction 81 and tour 27 checks with 0 failures; create-world 32 checks; the smoke flight 124 s with no errors besides LogPython; npm 57; source-check; the five reference verifiers.

## Known gaps

- **Waterlogged slabs draw dry.** When: a later water session, with the mesher reading the property or a table change, and a digest check (handoff W2b open question 1).
- **Two fluids touching:** water under lava draws no surface between them. Today's "any fluid above" rule is kept for the digest. When: W4/W5, if a probe shows it matters.
- **Mode 1's accepted gap:** a surface source beside a source under more water (equal levels) keeps today's see-through step between 8/9 and 1. Mode 2 closes it. When: the main lane's flip to (a).
- **Untuned:**
  - each sloped top splits along its first diagonal;
  - the corner rule and the flow direction are our design.
  
  When: W3b, beside the vanilla shots.
- **The Playground draws sloped cells as stepped boxes** until W3b's parity. Its z-fight stays on the small-fixes list.
- **`voxel.Fluids` stays 0,** so flowing water appears only in captures that switch it on. When: W3b's flip with a 0/1 pair.
- **Sloped tops draw per cell** (no merging). Their mesh cost per touched section isn't measured yet. When: W3b.

## Subagents and tokens

- Lead: about 213k after the reading list, 329k at the checkpoint, 407k at the final checks and 431k with the records (MeasureAgentUsage).
- One Sonnet diff-only reviewer, 76.1k peak, on a 1,200-line diff. It found no confirmed defects. Applied:
  - a stronger flow-direction check in the step test;
  - packing held fluid whenever a state holds one;
  - qualified `Make` calls in the test.
  
  Checked and dismissed: the tick rewriting the flow channel (it writes 0-2 only); `lava_flow` outside the flow array (`textures.json` puts it there); Quad's colours differing (open corners give the same 255).

## Evidence

- `Tests/Evidence/2026-10-11/`:
  - `WaterSky-W3-ctl-before-voxel`, `-ctl-after-voxel` and `-ctl-after2-voxel`: the null pair and the noise run;
  - `WaterSky-W3-ctl-before-tour` and `-ctl-after-tour`;
  - `WaterSky-W3-ctl-before-breach` and `-ctl-after-breach`: the positive control and the first look at slopes.
- RunChecks: `Tests/Evidence/2026-10-11/Checks-final-20261011-010120/summary.txt` and the folders it lists.
- `C:\dev\sandbox\water-w3\`:
  - `build.ps1`, `runtests.ps1`, `mutate.ps1`, `materials.ps1`, `measure.ps1`, `imgdiff.py`, `aaoff\TestFoundationRuntime.ps1`;
  - logs `build-*.log` and `materials-m1.log`, runs `runs\t1`-`t3`, `mutations.txt`;
  - `ctl-voxel-diff.txt`, `ctl-voxel-noise.txt`, `ctl-tour-diff.txt`, `ctl-breach-diff.txt`;
  - the review in `review\` (`diff-r1.patch`, `findings-r1.md`);
  - the old materials in `materials-before\`.
