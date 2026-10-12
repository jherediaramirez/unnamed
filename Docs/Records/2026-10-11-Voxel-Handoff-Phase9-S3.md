# Voxel world handoff: far LOD step 2 done, into step 3 (tiles and coarser cells, 512+ blocks; apply throughput)

Branch `feature/voxel-world`. Step 2's record: [2026-10-11-Voxel-Phase9-S2-Fog.md](2026-10-11-Voxel-Phase9-S2-Fog.md).

## Reading list (in this order; exact sections)

1. This file, all of it.
2. Step 1b's record, [2026-10-11-Voxel-Phase9-S1b-Planning.md](2026-10-11-Voxel-Phase9-S1b-Planning.md): section 1 (what changed) and section 4 (the A/B: the baseline for step 3).
3. Step 2's record: section 1 (what changed) and section 6 (open items). Section 3 only if you touch the look.
4. Scout U's prior art, `C:\dev\sandbox\scout-lod-prior-art\facts.md`: the summary (lines 5-21) and section 4, "Step 3 (tiles, 512+ blocks)" (lines 171-220).
5. `Docs/VoxelWorld.md`: the far ring and planning paragraphs (search "Far ring (phase 9, S1)" and "Planning at large D"), and "The haze (phase 9, S2)" in Lighting.
6. Step 1's record, [2026-10-11-Voxel-Phase9-S1-Far-Ring.md](2026-10-11-Voxel-Phase9-S1-Far-Ring.md), section 2 (summary rules) only if you change what a far column draws.

## Where things stand

- `voxel.FarRadius 24` draws to 384 blocks as surface meshes. `voxel.FarRadius 0` reproduces the world before far LOD exactly; it is every A/B's control.
- **Step 1b:** planning is cheap at D 24; `voxel.Far.InnerFirst` (on) fills edge columns with far meshes before refining, which took near-hole frames at the 80 blocks/s flight from 376 to 0.
- **Step 2:** the fog wall at D is gone while far meshes form the edge (`voxel.Fog.Haze`, on).
  - Clear air for 48 blocks, then a light height haze from the sky's own light; aerial perspective x4.
  - Far meshes draw in `M_ChunkFarOpaque` / `M_ChunkFarCutout` / `M_ChunkFarWater`: their layer's chunk material plus a dithered fade over D's last 2 columns, by horizontal distance to the camera.
  - The fog floor lies 2,000 blocks under the world, so the haze covers it below the horizon. `voxel.Fog.Haze 0` brings step 1's wall back with the same calls and values.
- **Left for step 3 (from 1b's A/B):** far holes and coarse columns at the 80 blocks/s flight, about 2,400 and 4,000 frames. During hole bursts at 24, 1,000-1,750 finished meshes wait, against about 50 at 0, at about 1 apply per frame.

## Step 3: tiles and coarser cells (512+ blocks), and apply throughput

**Goal:** far land to 512 blocks or more without far holes at the 80 blocks/s flight, inside the 2 ms apply budget, with FarRadius 0 unchanged.

**The shape from the brief and Scout U (confirm with the manager in the plan):**
- **Far tiles:** one mesh per 4 x 4 far columns (one apply per 16 columns), replacing per-column far components past some inner radius.
- **The near/far swap:** today a column swaps from far to full by a mesh apply on its own component. With tiles, the tile's material hides the columns whose near mesh is on screen, read from a small per-column mask texture. A column changing hands then costs a texel update instead of an apply, and "far shows until near is ready" (no holes) survives.
- **Coarser cells:** double the cell size as the distance doubles (1 block to 384, 2 blocks to 768, 4 to 1,536), keeping each cell about 3-6 px wide at 1080p. Seams: skirts, only 2:1 steps between neighbours, a 10-15% hysteresis band.
- **Step 2's fade:** the far materials already mask by distance. A tile material adds the near mask, and must keep the edge fade, `FarFadeStart`/`FarFadeEnd` and the outside-shot rule (no fade while the edge fog is hidden). New materials only through `CreateBlockMaterials.py`, each named in the plan.

**Measure first.** Run the 1b A/B flight (FarRadius 0 / 24 InnerFirst 0 / 24 InnerFirst 1) on step 2's code, in a quiet window, alternating. The CSV's `ready` column and the far apply split show where the time goes. Step 2 changed no streaming code, but the far material is now masked: GPU new - old measured -0.04 ms over 7 views.

## Working agreement

- **Plans:** plan to the manager first (Shift manager chat); it confirms within settled decisions. Name every new asset.
- **Commits:** the local commit after a clean Sonnet 5.5 diff-only pre-commit review and passing suites; re-run every suite the review fixes touch. The manager pushes; never push.
- **Runtime changes:** RunChecks final with `-Flight smoke` after the last code change. A timed A/B needs a quiet window from the manager (owner Unnamed). Check `Get-Process java, javaw, UnrealEditor*` before every timed run and log the machine state with each figure.
- **Look calls:**
  - Control pairs on the same build, one switch apart, with the control frame proven valid (not black, saturated or empty).
  - Exposure fixed (`r.EyeAdaptation.MethodOverride 3`).
  - Copies of worlds only (`C:\dev\sandbox\far-lod\worlds\new-world-2`), never `Unnamed-play`.
  - Jesus wants plenty of 2560x1440 screenshots of each finished build.
- **Budget:** measure the lead with `C:\dev\sandbox\far-lod-2\measure.ps1` (it copies this session's transcript for `Scripts/MeasureAgentUsage.mjs`; point it at your own session's marker string). Case or wrap-up at 300k and every 200k after. Subagents: 5.5 models, exact paths and line ranges, one deliverable of about 100k.
- **Game numbers** come from the game's data, published docs and black-box runs; never from bytecode.

## Settled decisions

- The fog look is Jesus's pick (clear air, light haze, the edge fading into the real sky). The manager confirmed the plan (haze start 48) and, after report A, made two calls, flagged to Jesus:
  - the floor stays lowered and fogged;
  - haze density 0.004, aerial x4 and the 2-column fade stay the defaults.
- Jesus checks the dither in play.
- `FarRadius 0` must reproduce today's world exactly; never re-record goldens or world hashes. The FarMesher digest `0x21cb3f5108dbe83b` moves only on purpose, with the reason in the record.
- `Plugins/RealtimeMeshComponent` is never edited.
- **Pooled chunk components must come back empty** (1b: deferring the emptying asserted in D3D12 ray tracing, `ComputedPrimitiveCountForValidation`). Defer whole column releases instead.

## Code map

- **Far mesher:** `Source/UnnamedVoxel/Meshing/VoxelFarMesher.{h,cpp}`. It writes Opaque (ground, walls, lava), Cutout (canopy; `voxel.Far.CanopyLayer`) and Water layers only.
- **Pipeline and planning:** `Source/UnnamedVoxel/Streaming/VoxelStreamingPlanner.h` (`FVoxelStreamingRings`: ViewRadius, FarRadius, GetDrawnRadius; `FVoxelNeededOrder`) and the column pipeline (`FVoxelColumnPipeline`: SetFocus, PeekReady/PopReady, InnerFirst).
- **Applies:** `Source/Unnamed/Voxel/VoxelWorldSubsystem.cpp`.
  - `ApplyReady`: the 85% budget rule, and far applies only in the room near ones leave.
  - `AcquireChunk` / `ReleaseChunk` / `ReleasePending`.
  - `UpdateFarLooks`: the far material overrides, the fade parameters, the tint.
  - `UpdateVisibility`.
- **Components:** `Source/Unnamed/Voxel/VoxelChunkComponent.{h,cpp}`: one RealtimeMesh per column, a poly group per layer, `SetFarLook(bFar, bShadows, per-layer overrides)`.
- **The edge:** `Source/Unnamed/Voxel/VoxelEdgeFog.{h,cpp}` (UsesHaze, GetFarFade, GetNoFade, the `voxel.Fog.*` tuning variables) and `AVoxelWorld::UpdateEdgeFog` / `SetEdgeFogVisible` / `LoadMaterials` (`FarMaterialNames`) in `VoxelWorld.cpp`.
- **Materials:** `Scripts/Editor/CreateBlockMaterials.py`.
  - `MATERIALS` table; the far entries carry `"far": True`.
  - `FAR_FADE_HLSL`, plus the fade wiring in `build()`: a camera-relative World Position node, `MaterialExpressionShadowReplace`, `dither_opacity_mask`.
  - Run it with `-Materials=<names>` so the existing .uassets stay byte-identical, and hash `Content/` before and after.
- **Captures:** `Scripts/CaptureVoxel.ps1`.
  - `-View` takes one camera per run, so step 2's set used a wrapper (`C:\dev\sandbox\far-lod-2\views.ps1`, `cap.ps1`).
  - `-SpawnAt` moves the rings; the focus stays pinned on the spawn during shots.
  - The flight: `-Flight` (`flight.csv`: plan_ms, jobs_ms, ready, the far apply split).

## Known issues (with when)

- **Far holes and coarse columns at 80 blocks/s** (1b A/B, 2026-10-11): about 2,400 and 4,000 frames. Step 3's target.
- **Hysteresis at the `ViewRadius` border** (step 1, 2026-10-11): still open.
- **Hidden meshes of columns that left D stay applied until they unload** (step 1): still open.
- **Huge-mushroom caps count as ground** (step 1): still open.
- **Far BLAS are built though unused** (step 1): far meshes are out of ray tracing, but RealtimeMesh still builds their acceleration structures.
- **Sector orders are built lazily** (1b): the first turn through each sector at a new shape costs about 1 ms at D 24.
- **Step 2's edge** (2026-10-11):
  - The dither shows as a fine stipple in still frames. Check it in play.
  - The horizon match is one colour ratio (`voxel.Fog.HazeSky`), so the sun side can show a faint seam.
  - The fade centres on the camera while the rings centre on their column. These are equal in play; shots with a pinned focus can see a hard edge on the side nearer the rings' edge.
- **Brightness pass** (after the small fixes, Scout T): the haze follows the sky's own light, so it needs no extra scaling. The wall's `EdgeHazeLuminance` stays absolute, but only for `Haze 0`.

## What worked and what failed (step 2)

- **Worked:**
  - Tuning through cvars read on change: no rebuild per look tweak.
  - Top-down shots (`-View cx,cy,1000,cx,cy+1,0`) as maps for placing views.
  - The horizon band fitted in three captures by sampling the sky above and the band below.
  - The second fog layer at sea level, so the density is set at sea level whatever the falloff.
  - A script summary of each set (`C:\dev\sandbox\far-lod-2\summary.py`): frame stats, diffs, GPU ms and machine state.
- **Failed first:**
  - The raw sky-ambient haze made a blue band below the horizon.
  - The camera-centred fade cut the far ring in outside shots: compare the outside shots too.
  - Python has no `MaterialExpressionShadowPassSwitch`; it is `MaterialExpressionShadowReplace`.
  - `pwsh -File` passes a `[string[]]` parameter's second value to the next parameter; use one `;`-separated string.

## Token figures (step 2)

- Lead (MeasureAgentUsage): 164k at the plan, 308k at the first checkpoint, 418k at report A.
- Subagent peaks (Sonnet 5.5): docs 71.4k; review 80.3k (798-line diff, nothing serious); record draft 59.1k; delta review of the post-review code 48.2k (no defects).
- A fresh lead reading this handoff should expect about 150-180k before work starts.

## Evidence (best first)

- `C:\dev\sandbox\far-lod-2\set1`:
  - `A-0204-river-today-vs-new.png`, `B-plateau-valley-today-vs-new.png`, `C-ocean-flat-today-vs-new.png`, `D-floor-options-from-200-up.png`;
  - `summary.md`;
  - the per-view folders.
- `C:\dev\sandbox\far-lod-2\caves` (old, new2) and `C:\dev\sandbox\far-lod-2\look` (head-v1, t1-*, probe-east-s*, mtn150-*, far0-v1).
- Step 1b's A/B: `C:\dev\sandbox\far-lod-1b\ab2` (summary.md, split.txt).

## Open questions (recommendation first)

1. **Tile size:**
   - **4 x 4 columns** (one apply per 16; Scout U's figures favour 16-64 chunks per mesh);
   - 8 x 8 (fewer applies, but a coarser swap and a bigger rebuild per edit).
2. **The near/far swap:**
   - **a per-column mask texture read by the tile material** (no apply per column);
   - keep per-column far components inside 384 and tile only past it (simpler, but keeps today's applies where they hurt most).
3. **The first coarser band:**
   - **2-block cells from 384 to 768** (FarRadius 48);
   - stay at 1-block cells to 512 first and measure.

## Short prompt

> Far LOD step 3 for Unnamed (`feature/voxel-world`): far tiles and coarser cells to 512+ blocks, and apply throughput. Read `Docs/Records/2026-10-11-Voxel-Handoff-Phase9-S3.md` first, all of it.
> - Measure first: 1b's A/B flight on step 2's code, in a quiet window.
> - Then plan tiles (one apply per 4 x 4 far columns), the near/far swap by a mask texture, and 2-block cells past 384. Keep step 2's edge fade and `FarRadius 0` unchanged; new materials only through `CreateBlockMaterials.py`, each named in the plan.
> - Plan to the manager first.
