# The brightness pass: look dials, pinned daylight, the vanilla outline (2026-10-12)

Jesus, 2026-10-11: "Sunlit blocks render ~1.6-2x too bright … I love the shadows and shader look. We don't want to have a
flat styled Minecraft world. Just not as bright." And after playing at 4edf3d6: "Go from light to dark places quickly you
are left with this blinding light." Brief: `C:\dev\sandbox\scout-brightness\build-prompt.md` (scout T's facts beside it).
Evidence: `C:\dev\sandbox\look-brightness\` (runs, pack sheets, metrics, tools).

## What changed

- **Look dials** (`Source/Unnamed/Voxel/VoxelLook.{h,cpp}`): `voxel.Look.*` variables that `AVoxelWorld::ApplyLook`
  applies at BeginPlay and whenever one changes (polled in Tick, beside `UpdateEdgeFog`). Defaults are Jesus's pick; each
  dial is one switch away from the look before it.

| Dial | Default (pick) | Before | What it moves |
|---|---|---|---|
| `voxel.Look.DayEV` | 0.6 | 0 | daylight exposure offset in EV, + darker; caves keep theirs |
| `voxel.Look.SkyPin` | 1 | 0 | 1: the camera's open sky pins the daylight exposure whatever is on screen |
| `voxel.Look.SkyPinAdapt` | 2 | (0) | with SkyPin, stops a darker view under open sky (a forest floor) may still brighten |
| `voxel.Look.Cap` | 12.5 | 12.5 | without SkyPin: the metering pin (views metered at it or above get the daylight exposure) |
| `voxel.Look.Sky` | 1.637 | 1 | x SkyAtmosphere sky and aerial perspective luminance (so the haze and the sky light's capture) and the wall fog's colour; 2^0.6 x 1.08 keeps the sky as bright as before |
| `voxel.Look.SkyFill` | 1 | 1 | x the sky light (3) |
| `voxel.Look.LocalExposure` | 1 | 1 | 0: contrast scales 1 |
| `voxel.Look.Film` | 0 | 0 | 1: near-straight filmic curve |
| `voxel.Look.SpeedUp` | 2 | 2 | eye adaptation towards brighter views, f-stops per second |
| `voxel.Look.LowGlow` | 1 | 1 | x glow of emitters of level 1-7 (MPC_VoxelLook) |
| `voxel.Look.WaterSpecular` | 0.5 | 0.5 | water specular (MPC_VoxelLook); Jesus picked 0.25 for W3c to apply |

- **Exposure maths.** The map's volume keeps bias -1, EV100 6 to 12.5 and local-exposure middle grey +2: that is the
  cave state. With SkyPin, `VoxelLook::SkyOpenness` casts nine rays (up, and eight at 45 degrees) a quarter block at a
  time up to 48 blocks through the game thread's `GetState` (final columns only; an unloaded column reads as air, so
  open sky) every 0.2 s; full opaque cubes block them, leaves, plants, glass and fluids don't. The open fraction maps to
  a pin (smooth from 0.15 to 0.45), eased at 4 per second; `SolveSkyPinned` raises the floor to 12.5 - SkyPinAdapt and
  lowers the bias by DayEV x pin. So under open sky every view gets 2^-(13.5 + DayEV), or up to SkyPinAdapt stops
  brighter for a dark view; in caves (pin 0) the exposure is exactly today's. Probe cost: 73-109 us per call (game.log
  "Look: sky pin"), about 0.4 ms of game thread per second. Without SkyPin, DayEV and Cap go through an exposure
  compensation curve evaluated at the raw metered EV100 (`PostProcessEyeAdaptation.usf:176`): views metered at the pin or
  above get 2^-(13.5 + DayEV); 1.5 EV below it and darker keep today's.
- **Scout T's maths corrected.** Bias = Cap - 13.5 - DayEV with the floor at bias + 7 keeps only the floor: every view
  metered between floor and cap gets (12.5 - Cap + DayEV) stops darker. Measured at DayEV 0.6, cap 11: cave-lava
  (130,80,20) to (103,45,7).
- **A canopy meters like a cave** (VisualizeHDR): the player's forest pose EV100 6.7, cave-lava 9.0, the spawn's
  sky-reaching cave 12.4, open day 13.3. No metering pin can pin canopies without pinning caves, hence SkyPin.
- **The edge fog.** Haze mode's colour is the sky's distant light, which the luminance factor already scales
  (`SkyAtmosphere.usf:1462`); the wall's own colour is scaled by `Sky` in `UpdateEdgeFog`.
- **Materials.** `MPC_VoxelLook` (LowGlow 1, WaterSpecular 0.5) in every block, chunk and far material
  (`CreateBlockMaterials.py`); output identical at the defaults.
- **Outline mode 1 after tone mapping** (`FoundationBreakEffects`, `CreateBreakMaterials.py`): the strips draw
  `M_BreakOutlineMask` into custom depth only; an unbound post-process (`M_BreakOutlineShade`, after tonemapping) darkens
  the final image by 40% where a strip (custom depth under 1 km) lies in front of the scene. Switch
  `foundation.Break.OutlineTonemapped` (0: the old blend before tone mapping). Modes 0, 2, 3 and high contrast unchanged.
- **Maps.** `CreateGeneratedMap.py`: the SkyAtmosphere factor 1.637, applied to the existing map and verified.
  `CreatePlaygroundMap.py` (the Playground follows the pick): fixed exposure EV100 15.1 (14.5 + 0.6) and the factor 1.637.
  `r.EyeAdaptation.CachedLightingPreExposure` stays 8 (EV 15.1 is inside its range).
- **Capture tool.** `CaptureVoxel.ps1 -Transition` (`FVoxelTransitionShots`): settled on a sun view, the camera cuts into a
  shade view and back at the map's adaptation speeds, frames 0-5 s after each cut.

## Cause table (matched scenes and the forest rock spot; texture-relative sRGB box ratios)

| Case | Stone top | Stone E face | Forest dirt sun / shade | Forest metered EV100 |
|---|---|---|---|---|
| today | 1.56 | 0.35 | 3.12 / 0.16 | 11.0 (cap 12.5: exposure 3.1x the capped scenes') |
| LocalExposure 0 | 1.48 | 0.22 | 3.02 / 0.07 | 11.1 |
| SkyFill 0.333 | 1.39 | 0.11 | 3.66 / 0.06 | 10.6 |
| Film 1 | 1.38 | 0.46 | 2.93 / 0.34 | 11.0 |
| Cap 11 | 1.56 | 0.35 | 1.35 / 0.04 | 11.1 (clamped at 11) |

Cause 3 (view-dependent brightening) is real; the filmic toe and local exposure add to it.

## The look pack and the pick

Sheets: `C:\dev\sandbox\look-brightness\pack\` (10-before-after, 01-scenes-vs-vanilla, 02-forest, 06b-transition-fixed,
03-player-views, 04-river, 11-skypin-adapt, outline\outline-old-new-fixed.png). Rows B 0.4 / 0.6 / 0.8 with SkyPin:

| | today | B 0.4 | B 0.6 | B 0.8 | vanilla |
|---|---|---|---|---|---|
| Stone top (terrace box) | 1.56 | 1.21 | 1.05 | 0.91 | 0.85 |
| Forest dirt, sun | 3.12 | 0.97 | 0.81 | 0.68 | - |
| Sun : shade (forest) | 19.6 | - | 21.4 | - | no sun shadows |
| Sky 20 degrees | 89,126,164 | 85,121,160 | 83,119,158 | 82,117,156 | 146,178,238 |

Jesus's pick (2026-10-12): B 0.6; SkyPin with 2 stops of outdoor adaptation; the sky kept, trimmed (x1.08); water
specular 0.25 (W3c applies it); LowGlow 1, Film 0, shade fill as is, SpeedUp 2; the Playground follows.

Checks at the pack: caves (cave-lava, cave-wall) within ±5% on every row; torch room ±1.1 levels; lava against sunlit
sand +8.8% at B 0.6 (±10%); SkyPin 0 equal to today (±0.6); GPU ms unchanged (scenes +0.002, forest +0.014, within the
spread; 3 alternating runs each in a quiet window); no new horizon seam (haze and wall fog). Transition (sun, cut into a
forest view, back out): today the shade brightens 22 to 94 luma over 5 s and the exit flashes at 1.8x the settled sun;
with SkyPin + 2 stops the shade reaches 37 and the exit flashes 1.3x, back within 1 s; full SkyPin none.
Outline mode 1 in daylight: 0.63x on average (0.59-0.69 over 16 edges; 12 within 0.60 ± 0.03), was 0.80x; 3-5 px wide.

## Tests

`Unnamed.Voxel.Look`: today's SolveExposure; random DayEV and pins keep the floor, caves and daylight; SkyPin's pin 0 and
1; SkyPinAdapt; the sky test on hand-made worlds (open 1, cave 0, canopy 1, cave mouth between); each dial and variable
moves only its own targets; the picked defaults; the map's volume, sky light, atmosphere and MPC equal the defaults.
Mutations caught (each reverted): a default off; the floor not moved; Sky without the fog factor; the cap ignored; solid
blocks passing the rays; leaves blocking them; the pin as a step; the floor not raised; the curve on caves; SkyPinAdapt
ignored.

## Known issues and open items

- **DN2** must read `FVoxelLook` (DayEV, Sky, SkyPin) as its day peak, not overwrite the exposure or the sky factor.
- **W3c** re-baselines water after this pass and applies `voxel.Look.WaterSpecular` 0.25 (Jesus's pick).
- **T3c** and `LowGlow` share one emission knob. No dark glow-lichen cave was found for the ladder (the S8 seed-263 wall
  is black with no lichen in view); a vanilla probe of a level-7 emitter (scout T's recipe) decides it.
- **The river hillside** stays pale ("like sand"): that is aerial haze, which `Sky` scales with the sky. A sky-only
  factor would darken it but risks a horizon seam; it belongs with the fog work.
- **Forest view vs flat tops**: at B 0.6 the forest's sunlit coarse dirt reads 0.78 of the flat stone top's ratio; the
  view is pinned, the rest is the filmic toe on a dark texture (Film 1: 0.99).
- **Entering a cave from a forest** is darker for its first seconds than before: outdoor shade no longer pre-brightens
  the eye (views suite cave pose 6.9 vs 14.5 luma, shot mid-adaptation; settled caves unchanged).
- **The `VoxelWorld.md` cave table** (S3b, `cave` / `cave-lava` means) is stale: the spawn's caves changed since; this
  pass uses its own today runs as the control.
- **The held item's glow** (`foundation.Held.Glow`, a fixed emissive) shows 0.6 EV darker with the darker daylight (about 0.66x of its earlier level); retune with the hand's own look if wanted.
- **Outline mode 1** reads custom and scene depth after the upscaler: on a low screen percentage the 3 px band may crawl by a sub-pixel with the jitter (not seen at 100%). The transition frames right after a cut may carry the anti-aliasing history of the view before.
- **`CreateGeneratedMap.py` now saves the map** when `apply_look()` changes it (the sky factor), while CLAUDE.md's Commands line still says "(verifies, never rewrites)". That line is Jesus's to update.
- **TestVoxel** died once silently in `Mesher.Digest` (exit 777004): the known A2 allocator crash; the re-run passed.
