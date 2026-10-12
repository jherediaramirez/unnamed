# Voxel world handoff: far LOD step 1b done, into step 2 (the fog look)

Branch `feature/voxel-world`. Step 1b's record: [2026-10-11-Voxel-Phase9-S1b-Planning.md](2026-10-11-Voxel-Phase9-S1b-Planning.md). Read its sections 1 and 4 first. Step 1's record ([2026-10-11-Voxel-Phase9-S1-Far-Ring.md](2026-10-11-Voxel-Phase9-S1-Far-Ring.md), section 5) has the look shots. Scout R's facts (`C:\dev\sandbox\scout-lod\facts.md`, section 3, lines 82-122) cover the fog.

## Where things stand

- `voxel.FarRadius` defaults to 24: the far ring draws out to 384 blocks as surface meshes, and the edge fog sits at D = max(`ViewRadius`, `FarRadius`).
- `voxel.FarRadius 0` restores today's world exactly. It is every A/B's control.
- **Step 1b fixed the planning cost.**
  - Center-move frames at FarRadius 24 no longer pass the 2 ms budget.
  - The plan shifts a cached order, the walk keeps its progress, visibility visits only the columns that crossed D, and unloaded chunks are released in the time later frames leave.
- **The near holes at the 80 blocks/s flight came from apply throughput, not planning** (the 1b record's section 4).
  - `voxel.Far.InnerFirst` (on by default) puts far meshes of columns at the view's edge that show nothing before refining full meshes.
  - In the A/B it took near-hole frames from 376 to 0 (FarRadius 0: 96.5).
  - Far holes and coarse columns remain (about 2,400 and 4,000 frames). Those are step 3.

## Step 2: the fog look

- **Jesus's pick (tapped 2026-10-11):** "clear air plus light haze: the world's edge fades into the real sky, with light haze over distance and in valleys; feels like weather, not a wall."
- **Look calls:**
  - The fog follows his pick, and the manager checks that the captures match it.
  - Send captures with a valid control pair: today's fog against the new one, same spot and time. The control frame must not be black, saturated or empty.
  - Include your recommendation, and keep the old fog one switch away.

### Today's code

- **`AVoxelWorld::UpdateEdgeFog`** (`Source/Unnamed/Voxel/VoxelWorld.cpp:681-717`):
  - The phase 3 wall, at D: clear to D × 16 - 24 blocks, 99% by D × 16.
  - Height falloff 0.001, max opacity 1, cutoff 1e9.
  - Sky ambient contribution black, and a fixed in-scattering colour `EdgeHazeLuminance` (`:61`).
  - Aerial perspective at `EdgeAerialScale` (50, `:64`) × 12 / D.
- **The fog floor:** a 1,000 km plane under the world's bottom follows the camera (`:216-232`, `:684-691`).
- **`SetEdgeFogVisible`** (`:719-728`) turns all three off for shots from outside.
- **Far meshes use the chunk materials.**
  - `UVoxelChunkComponent::SetFarLook` (`VoxelChunkComponent.cpp:84-96`) sets a per-layer `SetMaterial` override: null for the slot material, or the debug tint's MID.
  - The subsystem calls it at every apply (`VoxelWorldSubsystem.cpp`, `ApplyReady`), and `UpdateFarLooks` refreshes it on screen.
- **Materials come from `Scripts/Editor/CreateBlockMaterials.py`:**
  - `MATERIALS` table at `:182-191`: `M_Chunk*` opaque, cutout, translucent, water.
  - `build()` at `:216`, with the masked and translucent handling at `:224-226` and `:328-330`.
  - `Content/` .uassets are a protected path (LFS, editor-locked). New materials are made only through this script (or a sibling run the same way), never by hand, and every new asset is named in the plan.

### The plan's shape (from the brief; confirm details with the manager)

- **Edge fade, far meshes only:** a dithered opacity fade over the last 1-2 columns before D × 16, so the sky shows through. Near chunk materials stay opaque.
  - A masked far variant per layer the far mesher writes: opaque (ground and walls), cutout (canopy) and water (translucent, opacity × fade).
  - Proposed names: `M_ChunkFarOpaque`, `M_ChunkFarCutout`, `M_ChunkFarWater`.
  - The fade uses the horizontal distance to the camera (a cylinder, as vanilla's fog), through `DitherTemporalAA` into the opacity mask.
  - The fade's start and end are scalar parameters on one MID per far material, owned by the subsystem and set from D. They are handed to `SetFarLook` as the per-layer override (the debug tint still wins).
  - UE 5.8 clips masked materials only in the depth prepass (`r.EarlyZPassOnlyMaterialMasking` 1): check the dither in captures.
- **Haze:** a long, light height fog over the whole distance.
  - Start near 0, low density, a real height falloff (valleys hazier than peaks).
  - Its colour from the sky: sky ambient contribution, or in-scattering matched to the horizon.
  - Aerial perspective eased toward physical, so far ridges turn blue-grey and darker than the sky, not white.
- **The fog floor:** with the wall gone, the floor and the atmosphere's planet show below the horizon past the edge. Decide in the look check whether the floor stays, fogged, or goes.
- **Switches:**
  - The old fog stays one switch away (a `voxel.Fog.*` cvar).
  - `FarRadius 0` keeps today's wall exactly.
  - Shots from outside (`SetEdgeFogVisible`) keep working.

### Look checks

- **The 02:04 spot** in `new-world-2`, around blocks (-480, 280): the white mountain.
  - Use a copy of the world. Step 1's copy is `C:\dev\sandbox\far-lod\worlds\new-world-2`. Never point captures at `Unnamed-play`, and never write there.
  - `CaptureVoxel.ps1` takes `-World`, `-WorldsDir`, `-SpawnAt` and `-View`.
  - `-SpawnAt -472,280` snaps to (-492.5, 245.5) in a mesa pocket. Step 1 raised the camera 16 blocks. Its two views and their luminance table are in `C:\dev\sandbox\far-lod\look2\notes.md`, with the montages next to it.
- **Auto exposure re-meters:** fix it (`r.EyeAdaptation.MethodOverride 3`) for brightness comparisons.

## Rules carried over

- `FarRadius 0` must reproduce today's world exactly. Never re-record goldens or the world hashes.
- The FarMesher digest (`0x21cb3f5108dbe83b`) moves only if the far mesh changes on purpose. If it does, say why in the record.
- Runtime changes need RunChecks final with `-Flight smoke` after the last code change.
- `Plugins/RealtimeMeshComponent` is never edited.

## For step 3 (tiles and coarser cells, 512+ blocks; apply throughput)

- **Apply throughput is the near-hole cause at 80 blocks/s.**
  - At FarRadius 24 during the hole bursts, 1,000-1,750 finished meshes wait, against about 50 at 0, at about 1 apply per frame.
  - Far tiles (one apply per 16 columns) are the planned fix. The 1b record's A/B is the baseline.
- **Tried and failed: deferring only the emptying of pooled components** (keeping the old mesh until reuse; Scout U's "skip the empty SetMesh" idea).
  - Its first flight asserted in D3D12 ray tracing: `ComputedPrimitiveCountForValidation == Initializer.TotalPrimitiveCount`, from RealtimeMesh's BLAS build (`C:\dev\sandbox\far-lod-1b\runs\diag2-b24-1`).
  - The assert appears in no other log.
  - Pooled components must come back empty. 1b defers whole column releases instead.
- **Still open from step 1:**
  - Hysteresis at the `ViewRadius` border.
  - Hidden meshes of columns that left D stay applied until they unload.
  - Huge-mushroom caps count as ground.
  - Far BLAS are built though unused.

## Short prompt

> Far LOD step 2, the fog look, for Unnamed (`feature/voxel-world`). Read `Docs/Records/2026-10-11-Voxel-Handoff-Phase9-S2.md` first, all of it.
> - The fog as clear air plus a light haze, with the world's edge fading into the real sky on the far meshes only (Jesus's pick). New far materials only through `CreateBlockMaterials.py`, each named in the plan; the old fog one switch away; `FarRadius 0` unchanged.
> - Look checks at the 02:04 spot in a copy of `new-world-2`, with a control pair (today's fog against the new one).
> - Plan to the manager first. At the end, write the handoff into step 3 (tiles and coarser cells, 512+ blocks; apply throughput) from this file's step 3 section.
