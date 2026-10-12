# Voxel world phase 9, S1b: cheap planning at drawn radius 24 (far LOD step 1b), and far meshes at the view's edge first

Branch `feature/voxel-world`, from `37d8c71`. Brief: [the step 1b handoff](2026-10-11-Voxel-Handoff-Phase9-S1b.md). Next: [the step 2 handoff](2026-10-11-Voxel-Handoff-Phase9-S2.md) (the fog look; it also carries step 3's notes). Sandbox: `C:\dev\sandbox\far-lod-1b`.

## 1. What changed

- **Measured first.** The flight CSV now splits the planning and the jobs: `focus_ms`, `vis_ms`, `walk_ms`, `walked`, `release_ms`, plus `pending_ms` and `pending_releases` (Docs/VoxelWorld.md, the flight).
  - At FarRadius 24 on step 1's code (`runs\diag-b24-1`), a center-move frame cost SetFocus 0.90 ms, visibility 0.31 ms, a walk over about 2,100 columns 0.45 ms, and the chunk releases 0.47 ms.
- **Needed order** (`Streaming/VoxelStreamingPlanner`):
  - `FVoxelNeededOrder` holds GetNeededColumns' order as offsets from the center, with each offset's ring and an inverse index.
  - It is built once per (ViewRadius, FarRadius, Stages, sector), from GetNeededColumns at center 0, and shifted.
  - `VoxelStreaming::ForEachLeft` visits the columns a move leaves behind, row by row, by Y then X.
- **Plan** (`FVoxelColumnPipeline::SetFocus`): a move that keeps the rings' shape takes the shift path, whatever the jump.
  - Records are added only for newly needed columns.
  - Only the band left beyond the unload distance unloads, in GetUnloads' order.
  - Caches are dropped only within MeshCacheRadius + 1 of the old center.
  - A shape change takes the full path. `FSettings::bFullReplan` (tests) forces it.
- **Walk** (`CarryProgress`):
  - The walk starts after the longest head of the new order whose columns were before the old FirstIncomplete and keep their ring and wanted kind.
  - Complete columns launch nothing, so the jobs are the full path's.
  - With edge walls on (captures), it regresses as before. Mutation M4 shows that carrying there is wrong.
- **Visibility** (`UVoxelWorldSubsystem::UpdateVisibility`): only the columns that crossed D, and every chunk on a radius change.
- **Deferred releases** (`TVoxelPendingReleases`, approved item 7):
  - An unloaded column's component, already hidden beyond D, stays in `Chunks` and is released after the applies while the tick is under 85% of `voxel.ApplyBudgetMs`. At least one is released a frame.
  - A column that gets a mesh again first keeps its component, re-shown. A waiting chunk stays hidden.
  - With the pool empty, AcquireChunk releases the oldest waiting column before making a component.
  - Lights are cleared at unload, as before.
  - This changes runtime timing at every radius, FarRadius 0 included, but no world data.
- **Item 8, `voxel.Far.InnerFirst`** (default: see section 4):
  - A far mesh for a column within ViewRadius + 2 that shows nothing yet goes before a full mesh whose column already shows something. Between two fills, the nearer goes first.
  - It has no effect without a far ring.
- **Counters:** `FVoxelPipelineStats::ShiftedPlans` and `CarriedColumns`.
- **Not changed:** `voxel.ApplyBudgetMs`, the far mesher (the digest is `0x21cb3f5108dbe83b` as before) and the world data.

## 2. What didn't work

- **Deferring only the emptying of pooled components**, which keeps the old mesh until reuse, as in Scout U's "skip the empty SetMesh":
  - Its first flight asserted in D3D12 ray tracing: `ComputedPrimitiveCountForValidation == Initializer.TotalPrimitiveCount`, from RealtimeMesh's BLAS build (`runs\diag2-b24-1`).
  - The assert appears in no other log under `C:\dev\sandbox` or `Tests\Evidence`.
  - Pooled components must come back empty, so 1b defers the whole release instead.
- **The PendingReleases model test caught a real bug in the first column-level version.**
  - A column whose last mesh was empty, then unloaded and came back with a mesh, had its live chunk released.
  - Fix: AcquireChunk always dequeues, and only columns with a chunk are queued.

## 3. Checks

- **Unnamed.Voxel.Streaming.Planner, block 14:**
  - FVoxelNeededOrder shifted equals GetNeededColumns, order and rings included: 8 ring cases, 8 sectors, centers out to ±1e9.
  - ForEachLeft equals a brute force over 6 radii and moves out to 100,000 columns.
- **Unnamed.Voxel.Pipeline.ShiftPlan (new):** the full path and the shift path side by side.
  - 8 configs: decoration on and off, with and without the far ring, edge walls on or toggled, one near (1e6, -1e6).
  - Random walks of 200-500 steps, with moves, teleports of ±40-60, sector, view and far radius changes, and jobs run in step.
  - Compared after every step: every job, every status in the box, the counts, the screen flags, PopReady and TakeUnloaded.
  - It asserts that the shift path ran: 78-342 shifted plans and 9-1,625 carried columns per config. About 60 s.
- **Unnamed.Voxel.Pipeline.InnerFirst (new):** the ready order against a model of the rule.
  - Flag off is today's order.
  - Fills go nearest first.
  - An urgent far mesh goes before a nearer refining full mesh.
  - The band edge is exact (ViewRadius+2 is urgent, +3 is not).
  - FarRadius 0 takes are identical with the flag on and off.
- **Unnamed.Voxel.PendingReleases (new):**
  - The queue: FIFO, removal, duplicates, peak with removal.
  - The reload-before-release path: the column keeps its chunk, live; a later unload releases it once; a column back without a mesh and gone again leaves no chunk.
  - A 20,000-step model of the subsystem's map, pool and queue.
- **Mutations**, each failing its test and restored byte-exact (`mutations.txt`):

| Mutation | Fails |
|---|---|
| M1: carry without the ring and kind check | ShiftPlan |
| M2: unload band off by one | ShiftPlan |
| M3: urgent branch off | InnerFirst |
| M4: carry with edge walls on (real plan mismatch at step 102) | ShiftPlan |

- **Review:** two Sonnet reviewers, split by path. Production: no bugs, 4 risks fixed (section 1). Tests: gaps closed (edge walls, combined moves, proof the shift ran, TakeUnloaded, band-edge literals, overflow coordinates) and 2 tautological asserts removed.
- **RunChecks final with the flight smoke, on the final code: GREEN** (`Tests/Evidence/2026-10-11/Checks-final-20261011-222458`).
  - Native 273 of 273 (step 1's 270 plus the 3 new tests), no crash; 179 voxel suites.
  - **9 of 9 hashes equal.** Mesher.Digest, the FarMesher digest `0x21cb3f5108dbe83b` and Planner blocks 1-13 are unchanged, so FarRadius 0's world is today's.
  - Playground 119 + 81 + 27 checks; create-world 38 checks; npm 62; source-check and the verifiers pass.
  - The smoke flight at FarRadius 24: no holes, 4,080 coarse frames, 2,393 far-hole frames, 18 of 8,034 frames over budget, release queue peak 73 and 0 at the end.
- **The ray tracing crash set, on the final build** (`C:\dev\sandbox\far-lod-1b\crash1\verdict.txt`): ViewRadius 24 ×3, 32 ×3 and 12 with a 32 MB resident pool ×2 all pass, with neither assert (`ComputedPrimitiveCountForValidation`, `RayTracingGeometry->GetRHI`) in any log.

## 4. The A/B

Quiet window 22:04-22:24, no intruders, background load mean 1.5 cores (Modrinth App, msedgewebview2, cam_helper). Flights alternated a, b, c, a, b, c (`C:\dev\sandbox\far-lod-1b\ab2`: `summary.md`, `split.txt`, `runs.txt`). The cases:
- a: `voxel.FarRadius 0`;
- b: 24 with `voxel.Far.InnerFirst 0`;
- c: 24 with `voxel.Far.InnerFirst 1`.

Values are means of 2.

| | a | b | c |
|---|---|---|---|
| Near hole frames: in view, nothing drawn (most columns) | 96.5 (15.5) | 376 (44.5) | **0 (0)** |
| Coarse frames: in view, far mesh only | 0 | 3,760 | 4,049 |
| Far-hole frames | 0 | 2,319 | 2,453 |
| Frames over 2 ms (of about 8,000-9,500) | 4 | 29 | 20.5 |
| Mean GPU ms per frame | 4.01 | 4.59 | 4.60 |
| Center-move frames: tick ms; over 2 ms | 0.88; 1 | 1.57; 8 | 1.57; 4 |
| Center-move split, ms: focus, visibility, walk, releases | 0.12, 0.08, 0.19, 0.30 | 0.33, 0.16, 0.42, 0.30 | 0.33, 0.16, 0.41, 0.28 |
| Release queue: peak; at the end | 40; 0 | 73; 0-1 | 73-74; 0 |

- **Planning:** step 1's b took 2.32 ms on center-move frames, and 420 of 554 of them were over budget. Here b takes 1.57 ms with 8 over. Frames over budget for the whole flight fell from about 435 to 29.
- **Near holes:** b barely moved (step 1: 406). The planning cost was not their main cause.
  - In the hole bursts at 24, 1,000-1,750 finished meshes wait (about 50 at 0), at about one apply a frame (`runs\diag3-b24-1`).
  - Far meshes only get the room near applies leave, so columns entered the view before their far mesh was on screen.
  - InnerFirst fixes exactly that: c has no hole frame at all, fewer than a, with fewer frames over budget than b. **Pass.**
- **Default:** `voxel.Far.InnerFirst` 1, because c beats b on holes and on budget.
- **Still apply throughput (step 3):** far holes and coarse columns stay at about 2,400 and 4,000 frames.
- **First view:** a's first run took 19.3 s from start, but 3.3 s from the first frame (a cold start). Its second run took 6.05 s, as in step 1. At 24 the near view takes 5.9 s and the whole ring 8.6-8.7 s.

## 5. Open items

- **Apply throughput is the near-hole cause at 80 blocks/s** (section 4). This is step 3, far tiles. The step 2 handoff carries the numbers.
- **Sector orders are built lazily.** The first turn through each sector at a new shape costs one GetNeededColumns, about 1 ms at D 24.
- **The two subagent-built test files are long** (VoxelPipelineShiftTests.cpp is about 1,300 lines).
