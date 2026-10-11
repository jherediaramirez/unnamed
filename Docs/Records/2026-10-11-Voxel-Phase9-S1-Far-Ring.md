# Voxel world phase 9, S1: the far ring (far LOD step 1), and the ray tracing crash fix

Branch `feature/voxel-world`, from `0b6f2b0`. Brief: `C:\dev\sandbox\scout-lod\build-prompt-step1.md` (scout R, facts in `C:\dev\sandbox\scout-lod\facts.md`). Jesus's answers: Q1 `FarRadius` 24 (384 blocks), Q2 far trees as leaf-canopy boxes, Q3 far shadows off. Next: [the step 1b handoff](2026-10-11-Voxel-Handoff-Phase9-S1b.md) (planning cost, then step 2's fog).

## 1. What changed

- **Rings** (`Streaming/VoxelStreamingPlanner.h`):
  - `FVoxelStreamingRings::FarRadius`, with the drawn radius D = max(`ViewRadius`, `FarRadius`), `IsDrawn` and `IsFar`.
  - The stage rings, the needed extent and the unload distance grow from D. `IsInView` still means a full mesh.
  - With `FarRadius` at or below `ViewRadius`, every set is exactly today's (Streaming.Planner block 13).
- **Far mesher** (`Meshing/VoxelFarMesher.h/.cpp`, new):
  - A per-state class table.
  - `Summarize`: a pure function of a final column, plus tints when given a neighbourhood. Step 4 can fill the same `FVoxelFarSummary` from the density graph.
  - `Mesh`: the summary plus 4 edge-neighbour summaries, or null for a skirt.
  - It writes the full mesher's exact layout through `VoxelMesher::AppendQuad`, which is the old emitter body moved out (Mesher.Digest is unchanged), and its tints through `VoxelMesher::GetFaceTints`. `FVoxelBlockTable::GetDefaultStates` gives the log ids.
- **Pipeline** (`VoxelColumnPipeline`):
  - `EJob::FarMesh`, run for a drawn column beyond the view once its 3x3 is final. The center gets tints from its 3x3; the 4 edge neighbours are summarized without tints.
  - Each record keeps its `MeshedKind` and `ShownKind`. A column whose wanted kind changes keeps showing its mesh until the other kind's mesh is taken.
  - A late result, or a waiting mesh, of the kind a drawn on-screen column no longer wants is dropped (`CollectMesh`, `SetFocus`).
  - Far jobs are capped at max(1, `MaxTasksInFlight` / 3). `PeekReady`/`PopReady` take every full mesh before any far one.
  - A far mesh drops the column's section cache, because it took the dirty bits. Edits and fluid ticks re-run a far column's mesh through `Dirty`.
  - `IsNearViewOnScreen` lets the loading screen wait for the near view only. Without a far ring it is `IsViewOnScreen`.
- **Subsystem and component:**
  - Visibility is `IsDrawn`. Far meshes apply only in the room near applies leave under `voxel.ApplyBudgetMs`; they never go first anyway, and they have their own apply-cost fit.
  - `UVoxelChunkComponent::SetFarLook` turns cast shadow off (unless `voxel.Far.Shadows 1`) and ray tracing off for far meshes, and back on for full ones. Lumen cards are none.
  - `voxel.Debug.FarTint 1` is an `/Engine/BasicShapes/BasicShapeMaterial` MID, lit magenta, so it reads pink.
  - Counters: `voxel.Info` (far radius, far columns), the 1 s log line (a `; far: ...` tail, only with a far ring), and `stat Voxel` (far wanted, on screen, quads, applies and their ms). `build.txt` adds a near-view time and a far-ring line.
- **Settings:** `voxel.FarRadius` (0-64, settings default **24**), `voxel.Far.Shadows` (0), `voxel.Far.CanopyLayer` (0 cutout, 1 opaque), `voxel.Debug.FarTint` (0).
- **Fog:** `UpdateEdgeFog` and `SetEdgeFogVisible` use D, with the same formula. At 24 that is clear to 360 blocks, 99% by 384, aerial scale 50 x 12 / 24.
- **Flight:** the CSV appends `coarse` (in view and still showing the far mesh), `far_holes` (drawn far columns closer than D - 1.5, in the frustum, with nothing on screen) and `far_ms`. `flight.txt` sums them. The near holes keep their definition.

## 2. Summary rules and choices

- **Classes:**
  - Skip: air, model states, and logs and stems.
  - Leaves: the table's flag.
  - Fluid: fluids, plus model states holding a fluid (seagrass, kelp, waterlogged models), so far water has no holes. This is the subagent's choice, accepted. Revert it with one line in the constructor.
  - SnowLayer.
  - Ground: everything else.
- **Logs:** the block table carries no tags, and the 26.3 block reference has no `#minecraft:logs`, so logs are found by **id suffix**: `_log`, `_wood`, `_stem`, `_hyphae`. These cover the worldgen reference's `logs` tag expansion (logs_that_burn plus the crimson and warped stems, the stripped variants included) and `mushroom_stem`.
- **Per cell, scanning down from WorldSurface:**
  - **Ground:** the first ground cell. A snow layer directly on it makes the top face snow. A wall's top block shows the ground state's side face, and the rest shows the state beneath it (dirt under grass), so cliffs don't stack grass sides.
  - **Canopy:** the topmost run of leaves above the ground. A snow layer on it makes its top snow.
  - **Fluid:** the topmost fluid above the ground, which is its floor.
  - **Tints:** each is the full mesher's tint for that face.
- **The mesh:**
  - Tops are merged greedily by (height, state, tint). Fluid tops sit at the fluid's surface height on its own layer.
  - Walls are drawn toward every lower neighbour cell, down to it, across column borders; a missing neighbour gets a skirt to `LowestGround - 1`.
  - Canopy boxes have their sides minus what neighbour canopy or ground covers, and no bottom when ground lies right under them.
  - Ground and walls are on the opaque layer. There is no AO (R 255), and G is the state's emission, as the full mesher writes it.
- **Canopy layer: cutout** (the manager's call, closer to vanilla's fancy leaves). Cutout and opaque differ in 0.18% of the overview's pixels. Opaque is `voxel.Far.CanopyLayer 1`.
- **Quads per far column:** 254 in game at the dev spawn (`build.txt`), and a mean of 517 (max 668) in the FarMesher digest's 3x3, which is trees and hills at seed 263. Summary plus mesh takes 0.40 ms per column on a worker.
- **Neighbours:** far meshes wait for their 3x3 to be final, as near meshes do. The far ring's outer neighbours are ring 1, which is final anyway, so streaming never meshes against a missing neighbour and never remeshes for one. The neighbour mask (`MeshedNeighbors`) still drives the edge-walls debug.

## 3. The crash at `voxel.ViewRadius` 24 and 32 (in scope)

- **Cause:** scout S's audit, `C:\dev\sandbox\scout-rtcrash\facts.md`. UE 5.8 evicts ray tracing geometry the scene didn't reference once resident geometry passes a 400 MB pool. The pinned RealtimeMesh asserts on an evicted geometry (`RealtimeMeshComponentProxy.cpp:717`; upstream PR #304).
- **Fix, no plugin edit:** `r.RayTracing.UseReferenceBasedResidency=False` in `Config/DefaultEngine.ini`. The guards are `Tests/source-check.mjs` (fails without the line while ray tracing is on), and `build.txt`'s Lighting line, which now prints the residency and pool cvars.
- **Repro on the old binaries** (`C:\dev\sandbox\far-lod\rt\`):
  - radius 12 + `r.RayTracing.ResidentGeometryMemoryPoolSizeInMB 32` asserted 2 of 2 (`a2-pool32-1`, `-2`);
  - plain radius 24 asserted (`a1-r24-base`);
  - radius 24 with residency 0 ran clean 2 of 2 (`a3-*`).
- **Verified on the final build, all clean** (`v-*`): radius 24 3 of 3, radius 32 3 of 3, the 32 MB pool 2 of 2, and both radius-24 flights in the A/B.
- **Far meshes:** `SetVisibleInRayTracing(false)` keeps them out of the ray tracing instances. RealtimeMesh still builds a BLAS for each (no per-component switch).

## 4. The A/B (quiet window 18:03-18:16, no intruders, background load 1.2 cores)

The flights alternated a, b, c, a, b, c (`C:\dev\sandbox\far-lod\ab\`, `summary.md`). Cases:
- a: `voxel.FarRadius 0`;
- b: `voxel.FarRadius 24`;
- c: `voxel.FarRadius 0,voxel.ViewRadius 24`, the brute force.

Values are run 1 / run 2.

| | a | b | c |
|---|---|---|---|
| Near hole frames (most columns) | 132 / 244 (14 / 24) | 432 / 380 (46 / 46) | 8,203 / 7,885 (542) |
| Coarse frames (most) | 0 | 3,705 / 3,796 (65 / 73) | 0 |
| Far-hole frames (most) | 0 | 2,351 / 2,335 (454 / 452) | 0 |
| Frames over 2 ms (worst) | 1 / 3 (2.33 / 2.48) | 440 / 429 (4.36 / 4.40) | 744 / 741 (5.01 / 4.07) |
| Mean GPU / frame ms | 4.01 / 10.53 | 4.59 / 12.57 | 5.43 / 14.93 |
| First view: near; far ring complete | 5.3 / 5.9 s | 5.7 s; 8.6 s | 11.8 s |
| Memory over laps 2-3: process; columns | 4,934 → 4,959 MB; 19.0 → 19.7 MB | 5,171 → 5,233 MB; 59.7 → 60.9 MB | 7,877 → 7,996 MB; 59.9 MB |

- **GPU:** b adds 0.58 ms over a; c adds 1.42. **Pass.**
- **The far ring** completes 2.9 s after the near view. **Pass.**
- **Near holes: FAIL.** b has 406 hole frames against a's 188 (mean of 2), and at most 46 columns against 19, at the 80 blocks/s acceptance flight.
  - **Cause** (CSV split): on frames where the rings move, planning plus the job walk over D=24's 2,641 needed columns take about 2.2 ms. That is plan 1.22 + jobs 0.98 in b and 1.25 + 0.97 in c, against about 0 in a.
  - That uses the whole 2 ms budget, so near applies fall to the one-per-frame "first anyway".
  - Far applies are not the cause: they only get leftover room. The same leftover starvation is why the far ring lags the fast flight (far holes).
- **Budget not tuned**, per the brief. The manager's call: commit with `FarRadius` 24 as the default. Step 1b fixes the planning cost. `voxel.FarRadius 0` restores today's world and timings.
- `far_ms` read 0 in these runs, because Tick's frame copy dropped the far fields. It was fixed after the A/B, and the re-run checks below include it.

## 5. Look

- **Overview** (`C:\dev\sandbox\far-lod\look\montage-overview.png`: a | b | t | o; control valid):
  - The control is an island over the void. With the ring, matching terrain runs to the horizon: snow and desert, sea, mesas, savanna.
  - FarTint shows the outer land is far mesh. No seams, cracks or colour steps at that scale.
- **Eye-level, near the 02:04 spot** (`C:\dev\sandbox\far-lod\look2\montage-view2.png` and `montage-view1.png`, a | b | t | o; notes.md):
  - `-SpawnAt -472,280` snaps to (-492.5, 245.5). The camera sits 16 blocks above eye height to clear the spawn mesa.
  - Without the ring, the land past about 190 blocks is a white haze wall.
  - With it, the hills, savanna canopies and far slopes stand there under light haze. FarTint marks them as far mesh, with no crack or colour step at the near/far boundary.
  - Opaque canopy differs from cutout in 0.8% of the frame, none of it visible.

## 6. Checks

- **Unnamed.Voxel.FarMesher** (new, 4 cases):
  - summary rules (short grass, snow, oak tree, trunk-only cell, water over sand, held fluids, log ids, canopy-bottom skip);
  - watertight (24 random fields, every exposed wall face covered once, including borders and skirts; every top quad covers only cells of its own height, state and tint);
  - same layout as the full mesher (UVs, codes, tint, corner order, indices, colours);
  - digest pinned `0x21cb3f5108dbe83b`, from its first run on this code.
- **Unnamed.Voxel.Pipeline.FarRing** (new, 6 cases): far columns never get full meshes; kind swaps both ways with no column going blank (with a bounce mid-swap); near before far; the far job cap; `FarRadius` 0/≤ view runs exactly today's jobs; waiting meshes of the wrong kind are dropped.
- **Unnamed.Voxel.Streaming.Planner** extended:
  - 12/24: 545 in view, 1,993 drawn, 2,641 needed with decoration, unload beyond 30;
  - 12/32: 3,473 drawn;
  - D drives the rings;
  - `FarRadius` 0 and ≤ `ViewRadius` give today's sets and order.
- **Mutations** (each failed its test, reverted byte-exact; `C:\dev\sandbox\far-lod\mutations.txt`): walls only inside the column; plants as ground; far popped before near; unload distance from `ViewRadius` (FarRing crashed on `FindChecked`, Planner failed); the shown far mesh dropped before the full one arrives; top merges ignoring tint (2 variants).
- **Guards:** Mesher.Digest is unchanged, so the emitter path is byte-identical. The 9 radius-8 hashes are equal.
- **RunChecks final with the flight smoke:** GREEN on the pre-A/B code (`Tests/Evidence/2026-10-11/Checks-final-20261011-171809`). Native attempt 1 hit the known A2 allocator crash (AV at `0xffffffffffffffff` in Core during Mesher.Digest; the crash-hunt record puts it at about 1 in 9), which is unrelated; attempt 2 passed 270 of 270. Re-run on the final code, after the far_ms fix: GREEN, native 270 of 270 with no crash, 9 of 9 hashes, the flight smoke (`Tests/Evidence/2026-10-11/Checks-final-20261011-181848`). That smoke flight at `FarRadius` 24: 478 hole frames (at most 47), 3,778 coarse frames, 2,394 far-hole frames, and 30,218 far applies in 2,938 ms (about 0.1 ms each, 0.37 ms per frame).
- **Review:** two Sonnet reviewers, by path.
  - Fixed: `DropEdits` read `Ready` after `Reset`; the ini edit had re-ended an unrelated line; the near-view report line appeared at `FarRadius` 0; far applies get their own cost fit.
  - The mesher review found no implementation bugs, and its test gaps were closed.

## 7. Open items

- **Step 1b, next:** the planning cost at large D (section 4). Cache the needed order per (D, Stages, sector) shifted by the center, since it is translation-invariant, and keep the walk's progress across center moves. Pass line: near holes no worse than `FarRadius` 0.
- **Step 2, the fog look:** clear air plus a light haze; the edge fades into the real sky (a dithered fade on the far meshes only). Today's fog wall now sits at D.
- **Step 3:**
  - Apply throughput: far tiles, one apply per 16 columns. Far holes and coarse columns at 80 blocks/s show it.
  - Coarser cells out to 512.
  - Hysteresis at the `ViewRadius` border: a column bouncing across it remeshes.
  - Meshes of columns that leave D are still applied, hidden, until those columns unload (today's behaviour, now with a wider band).
- **Also open:**
  - Huge-mushroom caps count as ground (pillars).
  - Far BLAS are built though unused.
  - The far mesher's subagent ran to 199k against a 150k stop, and the FarRing writer to 173k.
