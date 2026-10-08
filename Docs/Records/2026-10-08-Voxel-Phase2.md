# Voxel world phase 2 - October 8, 2026

Data and meshing on a fixed area: the first generated chunks on screen. The block table comes from the 26.3 registry. Sections are palette-compressed and columns carry heightmaps. A stand-in heightmap terrain is meshed greedily with neighbour borders and ambient occlusion, into one RealtimeMesh per column with texture-array chunk materials, as a 9 x 9-column test area in `/Game/Maps/L_Generated` that the Foundation pawn can walk on ([Voxel world](../VoxelWorld.md#storage-and-meshing)). Phase 1 was committed first as `c3dc645`.

## What changed

- **Core (`Source/UnnamedVoxel/`):**
  - `Blocks/VoxelBlockTable`: per-state shape, layer, opaque faces, flags, tint, light and face codes.
  - `Storage/VoxelSection`: palette with 0, 4, 8 or 16-bit indices.
  - `Storage/VoxelColumn`: 24 sections and four heightmaps.
  - `Terrain/VoxelHeightTerrain`: the stand-in terrain.
  - `Meshing/VoxelMesher` and `VoxelMeshData`: padded gather and greedy meshing.
  - `Generation/VoxelArea`: square builds with per-stage timings.
  - New cycle stats: generate column, gather section, mesh section.
- **Unreal side (`Source/Unnamed/Voxel/`):**
  - `VoxelBlocks`: registry to table.
  - `UVoxelChunkComponent`: one RealtimeMesh per column.
  - `AVoxelWorld`: the test area actor, with collision, spawn and screenshots.
  - Console: `voxel.Seed <text>` rebuilds the world, `voxel.Regenerate`, `voxel.Bench Columns`.
- **Assets and scripts:**
  - `CreateBlockMaterials.py` makes `M_ChunkOpaque`, `M_ChunkCutout`, `M_ChunkTranslucent` and `M_ChunkWater`; `-Materials=` builds only some.
  - `CreateGeneratedMap.py` makes `/Game/Maps/L_Generated`.
  - `UUnnamedProjectSettings::GeneratedMap`, `ProjectPaths.generated_map()` and `MapsToCook`.
  - `Scripts/CaptureVoxel.ps1` captures offscreen screenshots.
- **Foundation:** `AFoundationLab::GetFallOutZ` replaces the pawn's fixed -1000 cm respawn height, so a voxel world can go to Z -64 (it returns the player below Z -128).
- **Tests:** six new, 19 voxel tests in all: `Unnamed.Voxel.Blocks.Table`, `Storage.Section`, `Storage.Column`, `Mesher.Shapes`, `Mesher.Borders` and `Terrain.Generate`. `VoxelTestBlocks.h` is a hand-built table.

## Problems found and fixed

- **Only the top section of each column showed.** The first screenshots had hills floating above the void, and a cut through the edge showed only a 16-block band. The 81 components held all 407 sections, but RealtimeMesh at the pinned commit draws only the most recently created buffer set of a mesh correctly, with stray triangles from the others. Each column is now a single buffer set with a poly group per layer.
- **D3D12 ray-tracing assert.** `(Initializer.IndexBufferOffset % 16) == 0` fired on the first render: RealtimeMesh builds a ray-tracing segment per poly group at its index offset. Each layer is now padded to a multiple of four triangles with zero-area ones.
- **Stale bounds.** The component's cached bounds could predate a new section. The column now reports its own bounds from its meshed sections.
- **Unity build collisions.** Generic helper names in anonymous namespaces (`Size`, `FindTexture`, `LayerMaterialNames`, `Print`) clashed with engine code and other files once compiled together. They were renamed, and the rule is in [Naming conventions](../NamingConventions.md).
- **A wrong test expectation.** Water sides are per-cell faces, so a 4 x 4 pool has 17 water quads, not 5.

## Verification

- **Editor build:** succeeded; no warnings in project code.
- **`Scripts\TestVoxel.ps1`:** 19/19. Evidence: `Tests/Evidence/2026-10-08/VoxelAutomation-20261008-152734/`.
- **Every native suite:** 65/65 in 22 s.
- **`Unnamed.Voxel.Mesher.Borders`:** random blocks of every kind over 3 x 3 columns and three sections, 26,896 faces. Every face is drawn exactly once with the expected four corner levels, with greedy merging on and off. No face is missing or hidden at any section or column border.
- **Chunk materials:** compiled with `-AllowCommandletRendering`: opaque and cutout 248 pixel-shader instructions, translucent 891, water 1,887.
- **Screenshots** (`Scripts\CaptureVoxel.ps1`, `Tests/Evidence/2026-10-08/VoxelShots-20261008-152540/`): overview, close-up, ground view, the Lumen scene and a section from grass down through the deepslate blend.
- **Player:** grounded on the generated surface at spawn, feet exactly at Z 6,500 cm (top of the block at 64), after about 8 s of simulation.
- **Timings, one thread** (`voxel.Bench Columns 4`, 81 columns, 1,944 sections):
  - Generate a column: 0.42 ms mean, 0.58 ms worst.
  - Gather a section: 0.014 ms.
  - Mesh a section: 0.048 ms mean over all sections; 0.202 ms mean and 0.320 ms worst over the 407 with geometry.
  - About 1.9 ms per whole column.
- **Timings, 30 workers:** generation 3.5 ms and meshing 6.8 ms of wall time for the 81 columns; stream sets 1.4 ms; apply 81 columns over 3 to 4 frames, worst frame 2.10 ms against the 2 ms budget.
- **Memory:** 10 KB per column, 24,477 quads in the area.

## Known issues

- **Dark shaded slopes.** RealtimeMesh Core gives Lumen no surface cache: the Lumen scene view shows the chunks black, so no light bounces off terrain. Hit lighting made no difference. Phase 3 needs a decision: a per-vertex sky light (also the way to dark caves), RealtimeMesh Pro, or our own scene proxy.
- **Water look and physics.** Water seen from above looks pale. Generated water does not yet count for air or drowning, because `AFoundationLab::IsInWater` is not virtual.
- **Meshing under load.** With all workers busy, a meshed section averages 0.26 ms against 0.20 ms alone.
- **Apply budget.** A frame can overshoot the 2 ms budget by one column.

## Next

Phase 2W, Create New World: saved world folders with metadata; the seed from the menu (blank random, numbers as typed, text hashed); world type, game mode and difficulty; the Singleplayer list showing saved worlds; Play opening `L_Generated` with that world's seed. Then phase 3, streaming.
