# Water and sky handoff: W3b done, into W3c (readable flow, less see-through water, water fog, a probe step)

October 11, 2026. W3b (Playground parity, the look shots, open corners, the flip) is done on `feature/water-sky`; its record is [2026-10-11-WaterSky-W3b.md](2026-10-11-WaterSky-W3b.md). The water lane then stopped: Jesus moved to one building agent at a time (the main lane). **W3c starts only on Jesus's word.**

## Short prompt (W3c)

> Water and sky track, session W3c: readable flow, less see-through water and Minecraft's water fog (Jesus's decision), then a vanilla probe step. Work in the worktree and branch the manager names (the water lane was `C:\dev\projects\Unnamed-water`, `feature/water-sky`). If main has moved, merge it first, once, on the manager's go (never rebase or force). Never push, and make no `CLAUDE.md` edits. Read `Docs/Records/2026-10-11-WaterSky-Handoff-W3c.md` and follow its reading list. Item 1: the flow streaks' contrast and the water's opacity, measured with the flowdir scripts against vanilla, plus the water fog, with control pairs. Item 2: the probe step (five new vanilla scenes), on Jesus's go for the client. Send the manager a short plan before building.

## Reading list

1. This file.
2. [The W3b record](2026-10-11-WaterSky-W3b.md): Decisions, Results (the scenes against vanilla, Direction), Known gaps.
3. `Scripts/Editor/CreateBlockMaterials.py`:
   - `FLOW_SAMPLE` and `WATER_HLSL` (about 85-100);
   - `CHUNK_FLOW_SAMPLE` and `CHUNK_WATER_HLSL` (about 137-150, 175-180);
   - `MATERIALS` (about 182).

   The W3 part 1 record's "Materials" result covers the regeneration and its control pair.
4. `Tests/Evidence/2026-10-11/WaterSky-W3b-look/flowdir/findings.md`, and `flow.py`'s header: the method. Run it with `ruleflow.py` on new captures.
5. `C:\dev\sandbox\vanilla-probes\facts.md`: "W3 water and lava scenes", and the harness lines at the top, for the probe step.
6. Plan section 5, W4 and DN2 (`2026-10-10-WaterSky-Plan.md`, about 309-335), to keep the fog work apart from theirs (ownership below).
7. Not to read: `Reference/minecraft-blocks-26.3/research/water_research.md` and `Docs/MinecraftBlockReference.md` section 4 (bytecode-derived, banned: plan Q18).

## Item 1 (Jesus's decision, October 11): readable flow, less see-through water, water fog

- **What he said:** after the comparison pictures, it acts more like vanilla. He wants less see-through water and Minecraft's water fog. Colour, sun glint and the blue-tinted shadows are fine as they are; don't change them.
- **Readable flow (direction, not colour):**
  - Today our streaks read at coherence 0.25-0.4 where vanilla's read at 0.5-0.7 on the same cells.
  - Next to the terrace steps, the stone edges seen through our water outweigh the streaks.
  - Raise the streaks' contrast or the water's opacity in `M_ChunkWater` and `M_BlockWater` (`CreateBlockMaterials.py`, then the regeneration with compiled shaders).
  - Re-run `flow.py` and `ruleflow.py` until our cells read the rule's axis at vanilla's coherence.
  - Control pair: the old materials swapped in, as W3 part 1 did.
- **Less see-through:** match vanilla's opacity by numbers. Compare how much of the floor shows through 1, 2 and 4 blocks of water; the pool scene (s4) gives one depth, and the probe step adds more.
- **Water fog, vanilla's:**
  - the biome's water fog colour from the reference data (find where 26.3 keeps it: the worldgen biome files' effects, or the sky reference's environment attributes; check before assuming);
  - fog when the camera is under water;
  - how deep water reads from above.

  Distances come from the wiki ("Fog", "Water") or a probe measurement, never from game code.
- **Who owns what:**
  - W4 (player in fluids) is movement and damage only: swimming, drag, the current's push, lava.
  - DN2 owns the sky and the edge fog (its colour per frame, replacing `EdgeHazeLuminance`).
  - The underwater fog, the water's opacity and the streaks are this session's. Keep the water fog its own pass or volume that DN2's edge fog doesn't overwrite. Say in the plan how the two combine.
- **Evidence:**
  - the scenes pair (`CaptureVoxel.ps1 -Fluids scenes` through `C:\dev\sandbox\water-w3b\scenes.ps1`);
  - the flowdir numbers;
  - an underwater shot pair beside the probe's.

  Look calls go to the manager with vanilla twins, numbers and a control pair.

## Item 2: a vanilla probe step (on Jesus's go for the client launch)

New scenes the W3 probe didn't take, posed and timed like facts.md's, on VP1's Superflat:
1. an L-turn channel;
2. two sources meeting head-on, and at 90 degrees;
3. flow around a single block;
4. a diagonal staircase run;
5. a stream past a hole;
6. for item 1: pools 1, 2, 4 and 10 deep, seen from above; one underwater view at noon (fog distance and colour) in plains and in a biome with another water fog colour.

Then add each scene to the `scenes` scenario (or a second scenario) and compare with `compare.py`, `widths.py` and `flow.py`.

## Working agreement

- The lane rules: never re-record goldens or hashes; one editor build at a time; timed gates only in the manager's quiet window; commits stay on the branch and the manager merges; no `CLAUDE.md` edits.
- Builds pass `-NoHotReloadFromIDE` (Jesus's play copy may hold the Live Coding lock). `RunChecks` then runs with `-Skip build` until its own build step passes the flag.
- Behaviour comes from the wiki, the probes and the client shots, never from game code. Look calls (closer to vanilla wins) go to the manager, with the other option one switch away.
- Image comparisons go through scripts and a subagent that returns numbers. Images cost the lead about 1-6k each.
- Set work aside with a WIP commit or a patch, never a bare `git stash`.

## Settled decisions

- Everything in the W2a-W3b records:
  - Q5 (b): `SlopedTops 1` is the default, and 2 is for shots;
  - open corners on (`voxel.Fluids.OpenCorners 1`);
  - the diagonal stays the first one;
  - sources keep their plateau;
  - `voxel.Fluids` defaults to 1;
  - the Playground's sloped cells use per-instance codes (Q3 (b): vertex codes stay under 2^15);
  - the scenes camera uses the probe's measured 77 degrees vertical.
- Jesus: water colour, glint and shadows stay as they are for now.

## Code map

| Piece | Where |
|---|---|
| Surface rule, open corners | `UnnamedVoxel/Meshing/VoxelFluidSurface.h/.cpp` (`Pack`, `OpenCell`, `Make`, `GetFlowUVs`) |
| Mesher | `VoxelMesher.cpp`: the packed array, `MakeFluidSurface`, the fluid case; `FVoxelMeshOptions::SlopedTops`, `bOpenCorners` |
| Switches | `VoxelFluidSystem.cpp` (`voxel.Fluids`, `.SlopedTops`, `.OpenCorners`); `UVoxelWorldSubsystem`; `FVoxelColumnPipeline::SetSlopedTops` |
| Playground | `FoundationBlockMeshes` (`MakeFluidSurface`, `GetFluidFaces`, `BuildFluidSurface`); `UFoundationInteractionView` (`GetFluidSurfaceMesh`, `MakeFluidSurfaceData`, `RefreshFluidsAround`) |
| Materials | `Scripts/Editor/CreateBlockMaterials.py` (`M_ChunkWater`, `M_BlockWater`) |
| Shots | `Voxel/VoxelFluidShots.h/.cpp` (`breach`, `flood`, `scenes`); `CaptureVoxel.ps1 -Fluids` |
| Tests | `Tests/VoxelFluidMeshTests.cpp` (`Fluid.Surface`, `Mesher.FluidShapes`, `FluidGenerated`, `FluidPlayground`, `FluidCost`) |

## Known issues

| Issue | When |
|---|---|
| Faint streaks and see-through water | W3c item 1 |
| No water fog | W3c item 1 |
| The Playground's flat flowing lava keeps `lava_still` sides | with the next Playground water change |
| One instanced mesh per surface shape per section in the Playground | cap it if a Playground pool grows |
| Open corners thin flowing water at an unloaded edge | W5 (generated edges) |
| No Playground shot shows a sloped cell (the showcase levels stand apart); the sloped meshes are proven only by `Mesher.FluidPlayground` | W3c: a placed spread in a Playground shot |
| Waterlogged slabs draw dry; water under lava; mode 1's equal-level step; the Playground's z-fight; W2b's list | as their records say |

## What worked and what failed (W3b)

- **Worked:**
  - **The parity test against the mesher on the project's blocks:** it passed on its first build and caught nothing, because both sides share `Make` and the corner order.
  - **The scenes capture on a fresh copy of a Superflat world per run:** the run's edits would otherwise persist in the world.
  - **Measuring before deciding:**
    - the 77 degree field of view (a 14% scale error that every image comparison would have carried);
    - the stream widths that exposed open corners;
    - the rule port that separated rule from render in the direction check.
  - **Subagents that return numbers:** the lead stopped spending context on images.
- **Failed, then fixed:**
  - A colour-threshold water mask: our paler water fell out of it (IoU 0.1). A relative mask (blue over green and red) plus a horizon cut gave 0.9.
  - The first build hit the Live Coding lock (Jesus's game); `-NoHotReloadFromIDE` fixed it.
  - `cd` in PowerShell moved the session's primary directory; use absolute paths.

## Tokens

- **W3b lead:**
  - 157k after the reading list;
  - 308k at the checkpoint;
  - 416k at the merge report;
  - about 440-460k with the final checks and these records.
- **Subagents:** 75.5k, 58.8k and 125.4k (the reviewer).
- **Expect for W3c:** about 120k for this reading list, plus the material regeneration and captures.

## Evidence

- The W3b record's Evidence section.
- The best place to start: `Tests/Evidence/2026-10-11/WaterSky-W3b-look/look-open-corners.png` and `cmp-oc_iou.csv`.

## Open questions (with options)

1. **Readable streaks:**
   - (a) raise the flow texture's contrast in the material (keeps the see-through amount);
   - (b) raise the water's opacity (Jesus wants that anyway);
   - (c) both.

   Recommended: (b) first, measured, then (a) only if the streaks still read under vanilla's coherence.
2. **The water fog's place:**
   - (a) a post-process volume driven by the camera's cell;
   - (b) the material's own depth fade from above, plus an underwater exponential fog.

   Recommended: decide after the plan's check of DN2's fog design. The colours come from the reference data either way.
