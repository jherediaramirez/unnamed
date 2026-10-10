# Voxel world phase 6, S6: block models, rendering - October 10, 2026

Phase 6's S6, run by the night manager's chain in the parallel lane `C:\dev\projects\Unnamed-s5` (`feature/voxel-s6`, from `adee7a6`). It draws S5's 83 model blocks in the generated world. The Playground half of the plan's S6 (one runtime mesh per baked model, the `M_Block*` material change, galleries, tour) moved to **S6b** at the 300k checkpoint, by Nyx's decision. The design is in [Docs/VoxelWorld.md](../VoxelWorld.md#block-models); this record keeps the decisions, numbers and evidence.

## 1. What was built

| Area | Files |
|---|---|
| Core table | `Source/UnnamedVoxel/Blocks/VoxelBlockTable.*`: `EVoxelShape::Model`, `EVoxelLayer::Model`, `EVoxelTintSource::DryFoliage`, `EVoxelOffset`, `FVoxelModelQuad` (with `OcclusionFront`), the model side table (quads, parts and alternatives), `HeldFluid` / `HoldsFluid`, offsets; `VoxelModels::GetOffset` and `PickAlternative` |
| Mesher | `Meshing/VoxelMesher.*`: `FEmitter::Model` (cull faces, AO rule, tint once per cell, emission, offsets, alternatives); water around water-holding states, never against them; `FVoxelMeshOptions::bModels`, `bModelShadeUnturned` |
| Tints, storage | `Biome/VoxelBiomeTable.*`, `VoxelBiomeTints.*` (dry foliage: `dry_foliage_color` or the colormap); `Storage/VoxelColumn.cpp` (motion blocking counts held water) |
| Game | `Voxel/VoxelBlocks.cpp` (copies the 303 models, drops 16 exact repeats, model states), `VoxelChunkComponent.*` (fifth slot), `VoxelWorld.*` (fifth material, swimming in held water, `-VoxelShotGarden`), `VoxelWorldSubsystem.*` and `VoxelColumnPipeline.*` (`voxel.Models`, `voxel.Debug.ModelShadeUnturned`), `VoxelTerrainFactory.*` and `Unnamed.Build.cs` (`dry_foliage.png`), new `VoxelModelBench.cpp` (`voxel.BenchModels`) |
| Scripts | `Scripts/CaptureVoxel.ps1 -Garden` |
| Tests | `VoxelMesherTests.cpp` (`Mesher.Models`, `Mesher.ModelData`), new `VoxelMeshDigestTests.cpp` (`Mesher.Digest`), `VoxelTintTests.cpp` (`Tints.DryFoliage`) |
| Docs | `Docs/VoxelWorld.md` ("Block models", biome tints), `Docs/Blocks.md` (model blocks bullet) |

Not changed: generation (no model state is placed yet), `VoxelConsole.*` (S4's file), the RealtimeMesh plugin, the block reference, `DA_Blocks` and every material.

## 2. Decisions

- **Scope (Nyx, 300k checkpoint):** the Playground moves to S6b with the material re-save and its before/after cube capture (Nyx's condition 2).
- **Look call 1, ray tracing (Nyx, delegated, 02:19):** plants stay in the ray-tracing scene, one component per column. Jesus's rule was to drop them only if G3 shows the ground darkening.
  - G3, with exposure fixed (`r.EyeAdaptation.MethodOverride 3`): the bare clearing's mean sRGB was (169.8, 235.4, 140.0) with plants, (169.8, 235.4, 139.9) on a repeat, and (170.6, 235.8, 141.3) with `voxel.Models 0`. That is -0.8 / -0.4 / -1.4, under the 3/255 bar.
  - Control (pixels over 2/255, repeat against `voxel.Models 0`): field 14.6% / 58.7%, field-clearing 17.6% / 76.7%, `ground` (no plants) 0.21% / 0.05%.
  - Auto exposure re-meters when plants fill the frame: in auto mode the clearing read +22/255 brighter with plants, so G3 is valid only with exposure fixed.
  - G4: `lumen-scene` differs at noise (9.97% against 9.56%), means within 0.2. G5: 15,244 cards (28.0 per column) in every run; `Mesher.Models` proves the model quads add no card.
  - Evidence: `Tests/Evidence/2026-10-10/S6-G3-clearing-on-off.png`; runs `VoxelShots-20261010-020835` (on), `-020928` (off), `-021136` (repeat).
- **Look call 2, shade override (Nyx, delegated, 02:19):** kept turned with the blockstate rotation (the baked data; no re-bake). The other option is one switch away: `voxel.Debug.ModelShadeUnturned 1`.
  - Data correction: in 26.3, glow lichen and sculk vein have no `shade_direction_override` (their quads' shade equals their facing). Of the 70 blocks with an override, 7 have one that is not up after the rotation: the vine (its ceiling part, shaded south), the amethyst cluster and its three buds (a cross shaded up, turned to their facing), and the stems of pink petals and wildflowers (north, turned by their facing).
  - Wall-mounted amethyst clusters, sunlit, facing south (band mean sRGB): turned (190.8, 193.2, 208.6), unturned (193.6, 195.0, 208.8), turned repeat (190.8, 193.3, 208.6). Pixels over 2/255: 50.2% unturned against 3.8% repeat; over 8/255: 18.6% against 0.2%.
  - Ceiling strip of the overhang: (114.8, 140.0, 108.8) turned, (114.6, 139.6, 108.6) unturned; 11.8% of pixels against 6.9% noise.
  - Evidence: `Tests/Evidence/2026-10-10/S6-LookCall-shade-override.png`; runs `-021429` (turned), `-021535` (unturned), `-021635` (repeat).
- **Plant shadows:** Jesus answered "cast shadows near the camera". The switch built for it (`voxel.ModelShadows`) could not work and was removed (Nyx agreed).
  - Cause: the pinned RealtimeMesh draws one mesh batch per buffer set, with `MeshBatch.CastShadow = DrawMask.ShouldRenderShadow()` (`Plugins/RealtimeMeshComponent/Source/RealtimeMeshComponent/Private/RenderProxy/RealtimeMeshBufferSetProxy.cpp:290`), the OR of its sections' flags. A section's own `bCastsShadow` is therefore ignored while the column's terrain casts.
  - G2 control: `voxel.ModelShadows -1` against the default differed at noise (garden 0.24% against 0.06%, over 8/255), and both showed plant shadows.
  - So plants cast shadows at every distance, and that cost is inside G2's +0.30 ms. The choice is open for Jesus (S6b handoff).
- **Duplicate quads:** 16 baked quads repeat an earlier quad of their model exactly (flowerbed segments that list a plane twice). The core copy drops them; 2,394 quads remain.
- **The firefly overlay:** the firefly bush's emissive overlay (model 302, quads 4-7, light 15) lies on its cross's four planes (quads 0-3), facing the same way. `Mesher.ModelData` allows exactly those four pairs, and its built-in mutation, a thin plane wound backwards, still fails the check.
- **Tint per cell:** a cell's tinted quads share one blend lookup (a review finding). It took the model-quad cost from 0.229 to 0.176 and 0.136 ms per planted column.

## 3. Gates (quiet window, 02:36-02:55; every launch logged a quiet machine at start and end)

| Gate | Result |
|---|---|
| Generated-world mesh, `voxel.Bench Columns 4`, old/new alternated, ms per column (2 runs per launch) | old 2.419, 2.279; new 2.417, 2.679; old 2.411, 2.537; new 2.405, 2.583. The first run of each launch is equal; the second runs scatter by about 0.2 ms in both builds: within noise. Quads and bytes identical: 125,184, 203 per meshed section, 23.1 KB per column |
| Flight, old/new alternated | No holes in any lap; chunk work 0.38-0.39 ms per circle lap in both; frames over 2 ms: old 0 and 2, new 3 and 2 (single spikes); process memory flat, columns 18.55 -> 19.22 MB per lap in both |
| Plant-dense mesh, `voxel.BenchModels 4 3` (seed 263, 227.1 plants and 981.1 model quads per column) | Model quads 0.176 / 0.136 ms per planted column (two runs), under the 0.2 ms cross fast-path gate, so no fast path. Mesh per meshed section 0.28 ms (generated 0.26). Mesh written: 398.4 KB per planted column against 222.1 KB |
| G2 GPU, 1600 x 900 frame ms | Field (4,843 plants in view): models on 3.75 / 3.78, `voxel.Models 0` 3.46, so +0.30 ms (bar 0.5). Garden shots +0.05 to 0.16 ms |
| G1 distance plot (indicative, a lower bound) | Changed pixels x d^2 relative to 8 blocks: 1.00, 0.88, 0.81, 0.72, 0.30 at 8, 16, 32, 64, 128 blocks. Green on green undercounts at distance. Vanilla-parity mips stand (Jesus's answer 3). The 30-frame jitter shimmer test is open |

## 4. Report A and end to end

On the final build (machine: other lanes' `java` and an editor running; none of these are timings):
- **Radius-8 hashes, the three presets, identical to `adee7a6`:** decorated (the default) `0x0015e9b54e7fe0eb` / `0x24bcb9ca548e0fcf` / `0x502dbd3b9d554557`; `voxel.Decoration 0` `0xeb1c3d76838e2481` / `0xcb42685280f8b57c` / `0xc4403ff66465fcf0`, both load orders, 0 columns differ. Biome hashes, bench quads and KB per column identical too.
- **Mesh digest** (`Mesher.Digest`, decorated radius 4, seed 263), with models on and with `voxel.Models 0`, equal to `adee7a6`'s: overworld `0xa8ba8d0d0a39174f` (100,113 quads), large_biomes `0xe49601da349c7c0d` (111,767), amplified `0x7f9491318204f4b2` (139,339).
- **Goldens and suites:** `TestVoxel.ps1` PASS, 88 voxel tests (84 before plus `Mesher.Models`, `Mesher.ModelData`, `Mesher.Digest`, `Tints.DryFoliage`; the script now counts tests passing with warnings too, since an editor HTTP timeout lands on whichever test is running). `Unnamed.Foundation`, `Unnamed.Hud`, `Unnamed.Worlds`, `Unnamed.Project`: 41 of 41.
- **End to end:** `create-world.mjs --editor` PASS; the Playground probe 0 failures (mean 44.4 FPS at cap 60, busy machine; S6 changes nothing in the Playground).
- **Run folders:** `C:\dev\sandbox\s6work\gate-final2` (hashes, digests, TestVoxel), `tests-final-foundation`, `Tests/Evidence/2026-10-10/Playground-all/Runtime-20261010-033151`; quiet-window runs `qw-*`; captures `Tests/Evidence/2026-10-10/VoxelShots-20261010-0208*` to `-0252*`.

## 5. Known issues

| Issue | Due |
|---|---|
| Plants cast sun shadows at every distance (RealtimeMesh's one batch per buffer set) | Jesus's choice, S6b handoff |
| The Playground draws no model state; galleries skip them; the held block hides them | S6b |
| The 30-frame jitter shimmer test (G1) | Open |
| Sea pickles and glow lichen glow near white in daylight: chunk emission is scaled for lava (`CHUNK_LIGHT_EMISSIVE`) | Later look pass |
| Generation places no model block (no plants in generated worlds yet) | The decoration features that place them |
| Model blocks place their default state; no support, facing or stacking rules | S7 |
