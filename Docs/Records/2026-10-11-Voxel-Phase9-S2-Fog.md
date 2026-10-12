# Voxel world phase 9, S2: the fog look (far LOD step 2): clear air, light haze, and the edge fading into the sky

Previous step: [2026-10-11-Voxel-Phase9-S1b-Planning.md](2026-10-11-Voxel-Phase9-S1b-Planning.md). Jesus's pick (2026-10-11): "clear air plus light haze: the world's edge fades into the real sky, with light haze over distance and in valleys; feels like weather, not a wall."

Branch `feature/voxel-world`, from `f2743a5`. Sandbox: `C:\dev\sandbox\far-lod-2` (plan in `plan.md` there, approved by the manager: Q1 haze start 48 blocks, Q2 floor chosen from captures).

## 1. What changed

- **The switch `voxel.Fog.Haze`** (bool, default 1, `VoxelConsole::CVarFogHaze`):
  - While far meshes form the view's edge (`voxel.FarRadius` > `voxel.ViewRadius`), step 2's look is on: the haze plus the far meshes' edge fade.
  - `voxel.Fog.Haze 0`, or `FarRadius` <= `ViewRadius` (so `FarRadius 0` always), keeps step 1's fog wall at the drawn radius D, with the same calls and values as before. It also restores the map's own second fog layer and directional in-scattering, which the haze replaces.
  - The mode choice is `VoxelEdgeFog::UsesHaze` (`Source/Unnamed/Voxel/VoxelEdgeFog.h`).
- **Far materials and the edge fade** (far meshes only):
  - New materials `M_ChunkFarOpaque`, `M_ChunkFarCutout`, `M_ChunkFarWater`, made by `Scripts/Editor/CreateBlockMaterials.py`. The far mesher writes only the opaque, cutout and water layers.
  - Each is its near chunk material's graph plus a fade by horizontal distance to the camera (a cylinder, as vanilla's fog; a camera-relative World Position node), between the scalar parameters `FarFadeStart` and `FarFadeEnd` (cm).
  - Opaque and cutout are Masked with `dither_opacity_mask` on (the engine's TAA-friendly dither; the project runs TSR, `r.AntiAliasingMethod 4` by default). Cutout fades its 26.3 mask (alpha >= 0.5). Water is translucent with opacity x fade.
  - Shadow passes take the unfaded mask (a Shadow Pass Switch, `MaterialExpressionShadowReplace`), since their view is the light's. Unset parameters (1e9 cm) draw everything.
  - The fade runs over the drawn radius's last 2 columns (`VoxelEdgeFog::FadeColumns`, `GetFarFade`): 352 to 384 blocks at D = 24, near vanilla's fog band (92%-100% of the render distance).
  - `AVoxelWorld::LoadMaterials` makes one instance per far material (`FarMaterialNames`; model quads would share the cutout's, the translucent layer keeps its own). `UVoxelWorldSubsystem` sets the fade from D (`UpdateFarLooks`) and hands the instances to `UVoxelChunkComponent::SetFarLook`, which now takes a per-layer override list.
  - `voxel.Debug.FarTint` still wins. A missing far material logs a warning and far meshes keep their layer's material, with no fade.
- **Haze** (`AVoxelWorld::UpdateEdgeFog`): the height fog's second fog layer, placed at sea level; the first layer's density is 0. Every `voxel.Fog.*` is read on change (no restart), and the defaults are the picked values.

  | CVar | Default | Meaning |
  |---|---|---|
  | `voxel.Fog.HazeStart` | 48 | blocks of clear air (deep caves stay clear) |
  | `voxel.Fog.HazeDensity` | 0.004 | at sea level, per 10 blocks as the engine counts it |
  | `voxel.Fog.HazeFalloff` | 0.2 | the density halves every 10 / falloff = 50 blocks up, so valleys are hazier than peaks |
  | `voxel.Fog.HazeSky` | "6 4.2 2.6" | colour = the sky atmosphere's ambient light times this ratio (`SkyAtmosphereAmbientContributionColorScale`); no fixed in-scattering colour, no directional in-scattering |
  | `voxel.Fog.AerialScale` | 4 | the sky atmosphere's aerial perspective distance scale (step 1's wall used 50 x 12 / D, 25 at D = 24) |
  | `voxel.Fog.FloorDepth` | 2,000 | blocks under the world's bottom (see the floor) |

  - The ratio was fitted so the fogged floor below the horizon matches the sky just above it (within 0-7 sRGB levels in the east view). Because it is a ratio of the sky's own light, it follows the sky.
- **Fog floor:** with the haze it lies `voxel.Fog.FloorDepth` 2,000 blocks under the world's bottom and is 10,000 km wide (10x), so the haze's own height falloff covers it fully below the horizon. The edge then fades into the haze colour there, as vanilla's fog colour below the horizon. A value below 0 hides the floor (look checks). The wall keeps the floor at the world's bottom, 1,000 km wide.
- **Shots from outside the world:** while the edge fog is hidden (`AVoxelWorld::SetEdgeFogVisible(false)`), the subsystem sets the far materials to no fade (`VoxelEdgeFog::GetNoFade`). See section 2, item 2.
- **Not changed:**
  - `FarRadius 0` (the wall, as before).
  - The far mesher (digest `0x21cb3f5108dbe83b`), world data and goldens.
  - The near chunk materials, which are byte-identical.
- **Tests** (`Source/Unnamed/Tests/VoxelEdgeFogTests.cpp`): `Unnamed.Voxel.EdgeFog.Mode` (the mode choice) and `Unnamed.Voxel.EdgeFog.Fade` (fade distances).

## 2. What didn't work

1. **The haze coloured by the raw sky ambient light** (`HazeSky 1 1 1`): the fogged floor under the horizon showed as a saturated blue band, a hard step at the horizon. Fixed by the fitted ratio (section 3).
   - Evidence: `C:\dev\sandbox\far-lod-2\look\probe-east` and the fit captures `probe-east-s2` to `probe-east-s4`.
2. **The fade centred on the camera took the far ring away from the outside shots.** The outside shots (overview, section, lumen-scene) have a camera far from the rings, so they lost most of the far ring: 80% of pixels differed by more than 8 levels against the old look.
   - Evidence: `C:\dev\sandbox\far-lod-2\caves\new` (overview).
   - Fix: no fade while the edge fog is hidden (section 1). The overview is now 0.94 mean against the old look (`C:\dev\sandbox\far-lod-2\caves\new2` against `caves\old`).
3. **The materials script's first run failed:** Python has no `MaterialExpressionShadowPassSwitch`. The class is `MaterialExpressionShadowReplace` (the editor's "Shadow Pass Switch").
   - Evidence: `C:\dev\sandbox\far-lod-2\materials-1.log` (failed), `materials-2.log` (pass).

## 3. Look

All captures: 2560x1440 or 1600x900, exposure fixed (`r.EyeAdaptation.MethodOverride 3`), on a copy of new-world-2 (`C:\dev\sandbox\far-lod\worlds`), never Unnamed-play.

Best evidence first: `C:\dev\sandbox\far-lod-2\set1\grid-a.png`, `C:\dev\sandbox\far-lod-2\set1\grid-b.png`, `C:\dev\sandbox\far-lod-2\set1\grid-floor.png`; then the per-view folders under `C:\dev\sandbox\far-lod-2\set1\` (summary in `C:\dev\sandbox\far-lod-2\set1\summary.md`).

- **Facts checked before the plan:**
  - AA is TSR: `r.AntiAliasingMethod = "4" LastSetBy: Constructor`, no Config override (`C:\dev\sandbox\far-lod-2\look\head-v1\game.log`).
  - `r.SupportSkyAtmosphereAffectsHeightFog = 1` and `r.EarlyZPassOnlyMaterialMasking = 1`, both engine defaults (`C:\dev\sandbox\far-lod-2\look\t1-haze\game.log`).
  - The engine's height fog density is FogDensity x 2^(-Falloff/1000 x (z - FogHeight)) with z in cm (`HeightFogCommon.ush`), so the haze halves every 10 / Falloff blocks. The fog is added to the scene only while FogDensity + SecondFogData.FogDensity > 0, so the first layer can be 0.
- **Horizon band fit** (looking east over the desert, `C:\dev\sandbox\far-lod-2\look\probe-east`):

  | HazeSky | Fogged floor below the horizon (sRGB) |
  |---|---|
  | 1 1 1 (raw) | (176,207,226), under a horizon sky of (243,245,244) |
  | 2.1 1.5 1.2 | (209,220,230) |
  | 4.0 3.0 2.1 | (228,235,240) |
  | 6 4.2 2.6 (default) | (236,241,243), within 0-7 levels of the sky above |

- **Density x aerial ladder** on the plateau view (`C:\dev\sandbox\far-lod-2\look\mtn150-*`). Lower values are clearer.

  | Case | Plateau face luma | Std |
  |---|---|---|
  | density 0.006, aerial x6 | 168.9 | 28.5 |
  | density 0.006, aerial x3 | 165.4 | 29.8 |
  | density 0.003, aerial x6 | 162.4 | 31.6 |
  | density 0.003, aerial x3 | 158.2 | 33.3 |

  Picked the middle: density 0.004 and aerial x4.
- **Dither:** at 2x zoom the fade shows a fine stipple in a settled still frame (`C:\dev\sandbox\far-lod-2\look\dither.png`). It is softened because the haze and the sky behind are close in colour. Check in play.
- **Spawns:** the 02:04 spot (`-SpawnAt -472,280` snaps to (-492.5,245.5)) and an ocean at `-SpawnAt -472,-1300` (spawn (-450,-1401)). A top-down map of each (`C:\dev\sandbox\far-lod-2\look\map-top-grid.png`, `C:\dev\sandbox\far-lod-2\look\maps3.png`) placed the views. A "plains" spawn at -472,1800 turned out jungle, so the flat view is the desert east of the 02:04 spawn.
- **Views set** (`C:\dev\sandbox\far-lod-2\set1`, 2560x1440). Cases: new = defaults (Haze 1); old = `voxel.Fog.Haze 0` (today's fog, the control), same build, same spot, run back to back. No java/javaw/UnrealEditor process at any launch.

  | view | camera -> target (blocks) | diff new vs old: mean levels; > 8 levels | GPU ms new / old (view shot, mean of 60 settled frames) |
  |---|---|---|---|
  | 0204 (step 1's view 1, 16 blocks over the spawn mesa) | (-492.5,245.5,91.62) -> (-692.5,280.5,85.5) | 3.33; 13.49% | 5.46 / 5.47 |
  | river (eye level on the river, near Jesus's 02:04 frame) | (-525.5,330.5,70) -> (-700,350,80) | 2.92; 8.43% | 5.44 / 5.43 |
  | valley | (-470.5,330.5,78) -> (-820,370,72) | 4.40; 20.15% | 5.45 / 5.45 |
  | plateau (mountain range, edge visible) | (-492.5,245.5,150) -> (-830,60,105) | 4.58; 19.34% | 5.41 / 5.51 |
  | above (200 blocks up, looking down past the edge) | (-492.5,245.5,200) -> (-850,50,60) | 7.94; 28.62% | 5.43 / 5.51 |
  | ocean | (-449.5,-1400.5,106) -> (-850,-1400,60) | 1.93; 6.60% | 6.10 / 6.19 |
  | flat (desert to the horizon) | (-470.5,250.5,110) -> (-100,230,70) | 0.74; 0.01% | 5.47 / 5.47 |

- **GPU:** new minus old over the 7 views is a mean of -0.04 ms (min -0.10, max +0.01). The masked far material costs nothing measurable at these views; no quiet window was needed.
- **Control frames are valid:** luma 172-238, black <= 0.03%, saturated 0.08-25.8%. The flat desert is bright in both (25.8% new, 24.7% old); brightness is the later pass's.
- **Switch proof:** `Haze 0` against HEAD's capture of the same view (`C:\dev\sandbox\far-lod-2\look\head-v1` against `look\t1-wall`): mean 0.45 levels, 0.11% > 8 (run noise). The new-vs-old differences sit in the middle third of the 0204 frame (47.7% > 8 there in the first pass), where the far land is.
- **What the captures show:**
  - The far land is clearer and less white than under the wall (0204, river).
  - The valley floor is hazier than the peaks.
  - At the drawn radius the land dissolves into the sky (plateau) or into a pale band that meets the horizon (above, flat).
  - The ocean's horizon is nearly unchanged.
  - With `voxel.Debug.FarTint` (`C:\dev\sandbox\far-lod-2\set1\plateau-tint`), the dissolving land is far mesh.
- **Floor options** (`C:\dev\sandbox\far-lod-2\set1\above-*`, `grid-floor.png`):
  - **Lowered 2,000 blocks and fogged** (default, `above-new`): the edge fades into a pale band that meets the horizon. Recommended.
  - **No floor** (`voxel.Fog.FloorDepth -1`, `above-nofloor`): the sky atmosphere's planet shows as a dark navy band below the horizon; luma 155.5 against 207.3.
  - **At the world's bottom** (`voxel.Fog.FloorDepth 0`, `above-floor0`): the floor is only partly fogged and shows as a grey plane past the edge.

## 4. Checks

- **`FarRadius 0`** on the new build against step 1's capture of the same view and command line (`C:\dev\sandbox\far-lod-2\look\far0-v1` against `C:\dev\sandbox\far-lod\look2\v1a`, 1600x900): mean 0.29 levels, 0.03% > 8; luma 189.9 against 189.8.
- **`-Caves` pair** at the dev seed (`C:\dev\sandbox\far-lod-2\caves\old` = Haze 0, `caves\new2` = final build):
  - cave-wall 0.52 mean, 0.36% > 8 (noise); ground 0.17; lumen-scene 0.20; overview 0.94; section 1.12.
  - The open-sky cave view differs at its horizon band only (2.46% > 8).
  - The closeup's distant mesa is clearer (9.8% > 8).
  - cave-lava differs on the animated lava surface (`caves\new` = first build).
- **Final build against the set's build** at the 0204 view (`C:\dev\sandbox\far-lod-2\final\0204-new` against `set1\0204-new`): mean 0.42, 0.09% > 8.
- **Content hashes** (`C:\dev\sandbox\far-lod-2\content-hashes-before.txt`, `content-hashes-after.txt`): all 2,121 existing files are byte-identical; the three new .uassets are the only additions.
- **Materials run** (`C:\dev\sandbox\far-lod-2\materials-2.log`): `-Materials=M_ChunkFarOpaque,M_ChunkFarCutout,M_ChunkFarWater -AllowCommandletRendering` ends "BLOCK_MATERIALS PASS". Pixel shader instructions 293 / 248 / 1892; samplers 4 / 2 / 7.

- **RunChecks final with `-Flight smoke`** on the final code (`Tests/Evidence/2026-10-11/Checks-final-20261011-235024/summary.txt`): GREEN after 22 min, no java or UnrealEditor besides its own runs at any launch.
  - Native: 275 of 275 declared passed (273 before, plus `Unnamed.Voxel.EdgeFog.Mode` and `.Fade`), 0 crashes. The far mesher's digest `0x21cb3f5108dbe83b` is pinned in `VoxelFarMesherTests.cpp` and passed.
  - TestVoxel: 181 voxel suites. Hashes: 9 of 9 expected equal (radius 8: overworld `0xa96b931bfbc4c39b`, large_biomes `0xe2a8bc32dc58d73c`, amplified `0x5cae8edc58ba77d5`).
  - Playground runtime: all 119, interaction 81, tour 27 checks, 0 failures. Create-world: 38 checks. npm: 62 of 62. Source-check and the reference verifiers: pass.
  - Flight smoke: completes in 125 s with 0 error lines besides LogPython (`Tests/Evidence/2026-10-11/VoxelFlight-20261012-000933`).

## 5. Review

Sonnet 5.5 diff-only pre-commit review of the 798-line diff: nothing serious, seven low findings.

- **Applied:**
  - (3) The haze colour string is parsed only when it changes (it was parsed every frame).
  - (4) The fade uses `UFoundationProfile::BlockCm` instead of 100.
- **Not applied, with reasons:**
  - (1) The dither assumes TSR, the project default (documented).
  - (2) Far meshes carry no Lumen cards, so no other pass sees the camera fade.
  - (5) The map's fog values are captured once in BeginPlay, which runs once.
  - (6) The floor's rim was checked visually (no band at the horizon in the captures).
  - (7) The masked far material's cost was measured: -0.04 ms.
- **Found after the review:** the outside-shot fade (section 2, item 2), fixed.
- **Budget:** lead context 307.9k at the checkpoint (MeasureAgentUsage). Subagents: docs (Sonnet, 72.6k), review (Sonnet, 86.4k).

## 6. Open items

- **Dither stipple:** the fade shows as a fine stipple in still frames (TSR softens it). Check in play.
- **One-ratio horizon match:** the haze colour is one ratio fitted in the east view, so the sun side may still show a faint seam.
- **Haze over the near view:** the haze also lies over the near view past 48 blocks.
- **Fade centre:** the edge fade centres on the camera while the rings centre on their column. They are equal in play. Shots with a pinned focus can see a hard edge on the side nearer the rings' edge.
- **Brightness pass:** it should scale nothing extra. The haze follows the sky's own light; the wall's `EdgeHazeLuminance` stays absolute, but only for Haze 0.
- **Day cycle:** it needs no refit of the haze.
