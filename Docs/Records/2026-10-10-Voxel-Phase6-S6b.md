# Voxel world phase 6, S6b: block models in the Playground - October 10, 2026

Phase 6's S6b, run by the night manager's chain in the parallel lane `C:\dev\projects\Unnamed-s5` (`feature/voxel-s6`, from `cd6e616`). The Playground now draws S5's 83 model blocks: in the galleries, in a new plant row in the showcase, and in the hand. The design came from [the S6b handoff](2026-10-10-Voxel-Handoff-Phase6-S6b.md), section 4; the rendering is described in [Docs/Blocks.md](../Blocks.md), Rendering.

## 1. What was built

| Area | Files |
|---|---|
| Model meshes | `Foundation/FoundationBlockMeshes.*`: `BuildModel` (one transient mesh per baked model, built on first use, full-precision UVs), `MakeModelCode`, `IsRepeatedQuad`, `GetOverlayLift`; the cube `Build` shares the tangent and finish helpers |
| Instances | `Foundation/FoundationInteractionView.*`: `FSectionKey::Mesh` is `uint16` (models after the cube kinds); `DrawnKeys` holds several keys per cell; section instances are a multimap; `AddPart`; one instance per part with `VoxelModels::PickAlternative` and `GetOffset`; held water; the state's light in custom data 8; the held block as a pool of part components; `GetDrawnCellCount` |
| Playground | `Foundation/FoundationPlayground.*`: the `IsModel` skips are gone, and showcase row 7 (`PlantRow`) holds 12 plants |
| Tour, probe | `FoundationTourProbe.cpp`: shots 17p (plant row) and 17q (near), and shots 18/19 aimed from the galleries' real row counts. `FoundationInteractionProbe.cpp`: drawn cells plus an exact expected instance count, computed from the world and the registry |
| Materials | `Scripts/Editor/CreateBlockMaterials.py`: `UNPACK` reads UV 1's two channels, and a vertex's own code (bit 20) replaces the instance's face code. `BLOCK_LIGHT_HLSL` lights a model vertex with the brighter of custom data 8 and its element light. Only `M_BlockOpaque`, `M_BlockCutout`, `M_BlockTranslucent` and `M_BlockWater` were re-saved (`-Materials=`); `M_Chunk*` are untouched |
| Tests | `Tests/FoundationBlocksTests.cpp`: `Unnamed.Foundation.Blocks.ModelMeshes` |
| Docs | `Docs/Blocks.md` (model blocks bullet, Rendering, gallery, known issues, tests), `Docs/Playground.md` (showcase), `Docs/VoxelWorld.md` (one line in Block models) |

Not changed: `Source/UnnamedVoxel/`, the voxel world's game code, the RealtimeMesh plugin, the block reference, `DA_Blocks`, the chunk materials.

## 2. Decisions

- **Water-holding states** (Nyx agreed, 04:00): the model plus a water-source instance in the same cell (full height when water is above), as the voxel world draws held water. Drawing only the model would leave a hole in a pool once S7 places seagrass into water.
- **The own code's marker is bit 20, not 23** (review finding): codes stay under 2^21, where a float's ulp is 0.25. Perspective interpolation of a constant attribute is not exact on every GPU; at 2^23 a one-ulp error would flip the texture's low bit, and `round()` absorbs a quarter.
- **The firefly bush's overlay is lifted 1/512 block** along its facing. UE 5.8 clips masked materials only in the depth prepass (`r.EarlyZPassOnlyMaterialMasking` defaults to 1, `RendererScene.cpp`), so the overlay's four quads, coplanar with the cross, passed the base pass's depth-equal test across the whole bush and drew their transparent texels: the bush drew black with lit fireflies. Lifted, the overlay wins only where its own prepass depth was written. Evidence: `S6b-firefly-black-before-lift.png`, `S6b-firefly-after-lift.png`.
- **Duplicate quads:** the 16 exact repeats are dropped, as in the voxel world; 2,394 quads are drawn.
- **Held block:** the default state's parts, each part's first alternative, centred on the bounds of the parts' meshes. No held water.
- **Plant shadows:** not touched. Nyx put the choice on Jesus's morning list with (a) recommended (the S6b handoff, section 5).

## 3. Checks

**Cubes render the same after the material re-save** (Nyx's condition 2; all on the old `cd6e616` DLL, the materials swapped):
- **With temporal AA**, % of pixels over 2/255: same-material pairs 0.9-7.1% and cross pairs 5.1-7.6% on the row shots 10-14. The showcase separated (0.7-1.3% within the old runs, 5.0-5.3% across), and an interleaved old run grouped with the old ones. Means were identical to 0.1/255 in all five runs. The extra pixels were high-frequency speckle, block edges, and the floor's dev-grid lines (a material not touched): a jitter or frame-phase shift. Nyx asked for a decisive control.
- **With TAA off** (`r.AntiAliasingMethod 0`, `r.ScreenPercentage 100`, a sandbox copy of the runtime script), old, new, old, new on the final assets: every cross pair is at or near 0.
  - Grass, leaves and translucent: 0.01-0.03%, all of it one patch, the water station's pool (69, 154 and 217 pixels over 8/255).
  - Showcase: 0.03-0.57% across against 0.25-0.54% within (the animated row).
  - Slabs and water: within-pair noise (0.2-2.0%).
  - Means equal (showcase 158.0 / 160.3 / 161.9).
  - Nyx passed the gate at 04:30, and the final assets repeat the same picture.
- **The pool patch:** the starter stone at (12, 6, 0) stands inside the water station, and the water faces next to it are coplanar with its faces. They z-fight, and the re-compile flipped which side wins (old: the stone shows through in dither; new: water covers it). A known issue below. Evidence: `S6b-cube-check-pool-zfight-09.png`, `-12.png`, `-12-marked.png`, `S6b-cube-check-taa-on-09-diff.png`.
- **Assets:** re-saved 04:54:56-04:55:22, after the script's final edit (04:43:13). SHA-256 (first 16): Opaque `C4DF92743C9A8002`, Cutout `DE0D9162BF2B6586`, Translucent `BC1737EC8F200C92`, Water `839854DA6B096998`. Shaders compiled (PS instructions 301 / 301 / 944 / 1932).

**Exact instance counts** (Nyx's condition 1): the interaction suite checks drawn cells against the world and instances against the count derived from the registry (one per part, plus held water).
- Clean: 5,117 cells, 6,448 of 6,448 instances, 901 section meshes.
- Mutation A, skipping each state's second part: 5,995 of 6,448; 5 count checks fail.
- Mutation B, skipping held water: 6,140 of 6,448 (308 water instances); 5 count checks fail.
- Both were reverted (the view's source is byte-identical to the saved final copy, with no `MUTATION` marker).

**Model meshes** (`Blocks.ModelMeshes`):
- 2,410 baked quads, 2,394 drawn, two triangles each.
- Full-precision UVs in every mesh.
- Every vertex's position, texture UV, shade face and own code checked.
- 4 overlay quads lifted.
- No model without a mesh.

**Suites on the final build:**
- Native `Unnamed.Foundation` and `Unnamed.Hud`: 40 of 40.
- Interaction suite: 0 failures in 65 checks. The combat "weak hit inside the target's cooldown" check gave strength 0.440, damage 2.129; it failed once at 0.520 under load during mutation A.
- Tour: 0 failures.
- `create-world.mjs --editor`: PASS.
- The probe: see section 4.

**Review:** one Sonnet 5.5 pass over the 909-line diff found nothing breaking cubes, the chunk materials or the bookkeeping.
- Applied: the bit-20 marker, the held pool reset on a pawn change, a null-mesh guard in `AddPart`, and the probe counting held water for every model-shape state.
- Kept, with reasons:
  - the test's vertex order (the engine keeps it; the test passes);
  - model cull faces, which are back to back with one-sided materials;
  - cube light in slot 8 (`GetLightEmission` gives the block's value for non-model states).

## 4. Probe FPS, old against new (quiet window, 05:18-05:25)

`TestFoundationRuntime.ps1 -Editor -FrameRates 60` (movement and interaction). The game DLL was swapped by copy (old: `cd6e616`'s; new: S6b's final), both with the final materials, in the order new, old, new, old. Every run logged a quiet machine at start and end (no `java`, `javaw`, `UnrealEditor*` or UBT). Each run had 0 failures in 103 checks. Timings are frame-weighted over 120 phases:

| Run | Mean FPS | GPU ms | Render thread ms | Game thread p50 ms | Cells / instances / section meshes |
|---|---|---|---|---|---|
| new 1 | 45.80 | 6.31 | 4.07 | 2.36 | 5,117 / 6,448 / 901 |
| old 1 | 45.78 | 6.14 | 3.93 | 2.19 | 4,072 / 4,072 / 243 |
| new 2 | 45.62 | 6.17 | 4.14 | 2.37 | 5,117 / 6,448 / 901 |
| old 2 | 45.70 | 6.19 | 3.97 | 2.25 | 4,072 / 4,072 / 243 |

- **FPS:** new 45.71 against old 45.74, which is at noise (gate passed, Nyx). Both builds are frame-pacing bound at the cap (p50 16.7 ms, p95 about 30 ms), so the cap hides small costs.
- **Costs under the cap:** the 658 extra ISM section components add about 0.15 ms of render thread and 0.15 ms of game thread, consistently in both pairs. GPU +0.08 ms is within its spread.
- **Run folders:** `C:\dev\sandbox\s6bwork\ab-*`.

## 5. Known issues

| Issue | Due |
|---|---|
| Playground water draws every face: a water face coplanar with a solid neighbour z-fights (the pool stone at (12, 6, 0); 70-260 pixels per shot). Held water meets the same faces wherever water-holding states touch solids | Later: face-culled water meshes or a small water inset |
| **The voxel world's firefly bush draws black** with lit fireflies: the same coplanar overlay in `M_ChunkCutout`, masked in the prepass only. Confirmed with `CaptureVoxel -Garden` (`VoxelShots-20261010-052915/garden.png`; crop `S6b/S6b-voxel-firefly-bush-black-crop.png`) | The morning list (Nyx). Fix: the same 1/512-block lift along the facing in `FEmitter::Model` (`Source/UnnamedVoxel/Meshing/VoxelMesher.cpp`) |
| The Playground's 901 ISM section components (243 before S6b) cost about 0.15 ms render thread and 0.15 ms game thread | A budget item. If it grows: merge the model parts of each section into one mesh per layer |
| Plants cast shadows at every distance in the voxel world; in the Playground they cast like cubes (one ISM per section and mesh) | Jesus's choice (S6b handoff, section 5) |
| Model blocks place their default state; no support, facing, stacking or water rules | S7 |
| The combat "weak hit inside cooldown" check failed once under load (strength 0.52 against 0.44) | Watch; re-run alone on a quiet machine if it repeats |
| Sea pickles and glow lichen glow near white in daylight in the voxel world | Later look pass |
| The 30-frame jitter shimmer test (S6's G1) | Open |
