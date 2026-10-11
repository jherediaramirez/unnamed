# Voxel world handoff: far LOD step 1 done, into step 1b (planning cost), then step 2 (the fog look)

Branch `feature/voxel-world`. Step 1's record: [2026-10-11-Voxel-Phase9-S1-Far-Ring.md](2026-10-11-Voxel-Phase9-S1-Far-Ring.md). Read its sections 1, 4 and 7 first. Scout R's facts (`C:\dev\sandbox\scout-lod\facts.md`, section 3) cover the fog.

## Where things stand

- `voxel.FarRadius` defaults to 24: the far ring draws out to 384 blocks as surface meshes, and the edge fog sits at D = max(`ViewRadius`, `FarRadius`). `voxel.FarRadius 0` restores today's world exactly; it is every A/B's control.
- **The open fail:** at the 80 blocks/s acceptance flight, near holes are worse with the ring.
  - With the ring: 406 hole frames, at most 46 columns. Without: 188 frames, at most 19 (mean of 2 runs each).
  - The cause is the game-thread planning cost at D = 24, not the far meshes. On frames where the rings move, `SetFocus` (plan) takes about 1.2 ms and `Update` (jobs: collect plus the walk) about 1.0 ms. Together they use the 2 ms apply budget, so near meshes apply one per frame.
  - Plain `voxel.ViewRadius 24` shows the same split.
- **The crash fix** (`r.RayTracing.UseReferenceBasedResidency=False`) is in. Large radii no longer assert.

## Step 1b: make planning cheap at large D (do this first)

- **Measure first.** Use the flight CSV's `plan_ms` and `jobs_ms` on frames over budget (the split script is in the step 1 record's A/B folder notes; `C:\dev\sandbox\far-lod\ab_summary.py` summarizes runs). Add Insights scopes if needed (`VoxelPipeline_Plan`, `VoxelPipeline_Launch` exist).
- **Plan** (`FVoxelColumnPipeline::SetFocus`, `VoxelStreaming::GetNeededColumns`):
  - The needed order is a pure function of (D, ViewRadius, Stages, sector) and the offsets from the center, so it is translation-invariant. Cache the sorted offsets and rings per key and shift them by the center: no candidate scan, no sort, no `GetRing` per column. The output must equal today's, order included; Streaming.Planner and Pipeline.FarRing case 5 pin that.
  - The unload pass (`GenerateKeyArray` plus `GetUnloads` over every record) and the cache loop can run only over columns that crossed a border.
- **Jobs** (`LaunchJobs`): `SetFocus` calls `Regress()`, so every center move re-walks all needed columns from 0 with a map lookup each. Keep the walk's progress across pure center moves, or walk only the columns whose ring or kind changed. Keep the far-cap walk from scanning every far column when far jobs are full.
- **Pass line:** near holes no worse than `FarRadius` 0 within the run-to-run spread, frames over budget close to `FarRadius` 0's, and `FarRadius` 0 unchanged (the 9 hashes, Mesher.Digest, Planner, FarRing).
- The A/B runs in a quiet window: a, b, a, b at least (`CaptureVoxel.ps1 -Flight -ExecCmds 'voxel.FarRadius 0'` vs `'voxel.FarRadius 24'`; the runner is `C:\dev\sandbox\far-lod\rtrun.ps1 -Flight`).
- Don't tune `voxel.ApplyBudgetMs` silently.

## Step 2: the fog look (after 1b)

- **Jesus's answer (Q4):** clear air plus a light haze, the edge fading into the real sky (the 26.4 idea), not a wall.
- Today's `AVoxelWorld::UpdateEdgeFog` (VoxelWorld.cpp) is the phase 3 wall, moved to D: clear to D x 16 - 24 blocks, 99% by D x 16, with a fixed colour, and aerial perspective at 50 x 12 / D.
- **The edge fade:** a dithered opacity fade on the far meshes only, over the last 1-2 columns, so the sky shows through; near chunk materials stay opaque. Far meshes already use the chunk materials. The fade needs a far-only parameter (a material instance per far component, or a vertex channel), and the far component is already its own component (`SetFarLook`).
- **The haze:** a long, light height fog over the whole distance, denser low (real height falloff), its colour from the sky. Aerial perspective eases toward physical, so far ridges turn blue-grey, not white.
- Scout R's §3 lists the UE 5.8 knobs. The white mountain at the 02:04 spot is the look check: `new-world-2` (copy it out of `Unnamed-play`; never point captures at the play copy), around blocks (-480, 280). Step 1's eye-level shots are in `C:\dev\sandbox\far-lod\look2`.

## Rules carried over

- `FarRadius 0` must reproduce today's world exactly. Never re-record goldens or the world hashes. The FarMesher digest (`0x21cb3f5108dbe83b`) moves only if the far mesh changes on purpose; then say why in the record.
- Runtime changes need RunChecks final with `-Flight smoke` after the last code change.
- The plugin `Plugins/RealtimeMeshComponent` is never edited.

## Short prompt

> Far LOD step 1b, then step 2, for Unnamed (`feature/voxel-world`). Read `Docs/Records/2026-10-11-Voxel-Handoff-Phase9-S1b.md` first, all of it.
> - Step 1b: make the streaming plan and job walk cheap at drawn radius 24 (cache the needed order shifted by the center; keep the walk's progress). Pass: near holes at `voxel.FarRadius 24` no worse than at 0 in a quiet-window A/B, with `FarRadius 0` unchanged.
> - Then step 2: the fog as clear air plus a light haze, with the edge fading into the real sky on the far meshes only. Look checks at the 02:04 spot in a copy of `new-world-2`.
> - Plan to the manager first.
