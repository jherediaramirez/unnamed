# Voxel world step 2L: lighting - October 8, 2026

Shaded slopes on generated terrain were almost black. Each column now carries Lumen cards, so Lumen's surface cache holds the terrain and light bounces off it, and the generated map's sky light is three times the Playground's. Phase 2 was committed first as `0dbb259`. The design and measurements are in [Voxel world, Lighting](../VoxelWorld.md#lighting).

## What changed

- **Lumen cards (`UVoxelChunkComponent`):**
  - `BuildCards` runs on the workers next to `BuildStreams`. For each face direction it makes a card per section, fitted to that section's opaque and cutout faces in that direction.
  - Lumen reads at most 32 cards per mesh, so a column over the limit merges the vertically closest cards of its busiest direction first.
  - `SetMesh` hands the cards to RealtimeMesh with the geometry (`FRealtimeMesh::SetCardRepresentation`), and its scene proxy passes them to Lumen.
- **Sky light:** `SKY_LIGHT_INTENSITY = 3.0` in `Scripts/Editor/CreateGeneratedMap.py`, set on creation and checked on verify. `/Game/Maps/L_Generated` was rebuilt from the script.
- **Instrumentation:**
  - The build report gives card counts and build time.
  - `CaptureVoxel.ps1`'s `build.txt` adds the lighting settings in effect and each shot's GPU frame time (mean of its last 60 settled frames).
- **Tests:** `Unnamed.Voxel.Mesher.Cards`, 20 voxel tests in all.

## Problems found

- **The handoff's premise was wrong.** It said RealtimeMesh Core has no Lumen cards or distance fields. At the pinned commit it accepts both and its scene proxy passes them to Lumen; it only generates neither.
- **Why the slopes were black:** with hardware ray tracing on (the project default), the chunks are in Lumen's scene through their ray-tracing geometry. With no cards, every hit read black from the surface cache. The Lumen scene view showed the whole area as a black silhouette.
- **Phase 2's hit-lighting test tried the wrong mode.** In 5.8, `r.Lumen.HardwareRayTracing.LightingMode 1` lights hits for GI and reflections, and `2` for reflections only, so phase 2's try of mode 2 changed nothing. Mode 1 does lift nearby shaded faces (27 to 47 of 255). Cards gave the same lift (to 48) for less GPU time, and also fix Lumen's scene for reflections and further bounces.
- **Bounce alone was not enough.** A wall that sees only sky stayed at 28 of 255 against 101 for a sunlit wall, in every Lumen mode and with Lumen GI off. That is the physical sky-to-sun ratio at this exposure. The sky light intensity was tuned with `r.SkylightIntensityMultiplier` at 1, 2, 3 and 4. At 3 shaded slopes read clearly and sunlit ground barely changes; 4 starts to look flat.

## Verification

- **Editor build:** no warnings in project code. Before committing phase 2, a build with `-DisableAdaptiveUnity` compiled the unity blobs the commit would produce, without name clashes.
- **`Scripts\TestVoxel.ps1`:** 20/20 (`Tests/Evidence/2026-10-08/VoxelAutomation-20261008-160749/`).
- **Every native suite:** 66/66 (`Tests/Evidence/2026-10-08/AllNative-20261008-161037/`).
- **Map:** `CreateGeneratedMap.py` verified the rebuilt map: "daylight rig, sky light 3.0".
- **Screenshots:** before `Tests/Evidence/2026-10-08/VoxelShots-20261008-152702/`, after `VoxelShots-20261008-160917/`, side by side in `VoxelLightingCompare-20261008-161235/`. Experiments are in `VoxelLighting-20261008-155118/` (baseline, GI hit lighting, software Lumen, no Lumen GI) and `VoxelLighting-20261008-160135/` (cards, then cards with sky x2, x3, x4).
- **Brightness** (mean sRGB of fixed regions, before to after):
  - hill facing away from the sun: 24 to 47;
  - shaded dirt side by sunlit ground: 27 to 64;
  - wall that sees only sky: 28 to 59;
  - sunlit wall: 101 to 113;
  - sunlit sand: 170 to 185.
- **Timings:**
  - Cards: 0.010 ms per column (0.023 ms worst). 832 cards for the 81 columns, 10.3 per column and at most 26.
  - GPU frame at 1600 x 900: 2.7 ms before, 2.9 to 3.0 ms after; all of the difference is the cards.
  - Apply: 81 columns over 3 frames, worst frame 2.15 ms against the 2 ms budget, with no measurable cost from the cards.

## Known issues

- **Hidden faces in a card.** A card captures the front face along its direction. A face behind another facing the same way in the same section stays black in Lumen's scene, and rays that hit it get no bounce. It shows as black strips on the seabed in the `lumen-scene` shot, and caves and overhangs in phase 4 will make it more common. Fix then: cards sliced along their direction, or hit lighting.
- **Software Lumen.** Without hardware ray tracing, Lumen traces distance fields. The chunks have none, so it cannot see them and caves would be lit by the sky. Mesh distance fields per column (`SetDistanceField`) are the fix.
- **Sky light 3 is art direction**, and only the generated map has it: the Playground keeps 1.
- **Still open from phase 2:** pale water seen from above, generated water not counting as water for the player, meshing under load, and the apply budget overshooting by one column.

## Next

Phase 2W, Create New World, as planned in the [handoff](2026-10-08-Voxel-Handoff.md#7-next-phase-2w-create-new-world-the-user-asked-for-menu-settings-to-really-work).
