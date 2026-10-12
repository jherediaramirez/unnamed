# Handoff: after the brightness pass (look dials, pinned daylight, vanilla outline), 2026-10-12

The pass is done and committed on `feature/voxel-world` (af8d8a0 dials, 047ccbb outline, 86fa518 record, merged with 2597cb9 in d5b11c1; RunChecks merge GREEN: `Tests/Evidence/2026-10-12/Checks-merge-20261012-111018`; record: `Docs/Records/2026-10-12-Look-Brightness.md`, read it
whole, 9 KB). This handoff is for whoever picks up a look follow-up: DN2 (day and night), W3c (water), T3c (torch glow),
the fog's river haze, or a retune of Jesus's pick.

## Reading list
1. `Docs/Records/2026-10-12-Look-Brightness.md`: all of it (dials, exposure maths, cause table, pack, pick, open items).
2. `Source/Unnamed/Voxel/VoxelLook.h`: the dials and their defaults, `SolveExposure` (the curve), `SolveSkyPinned`, the
   sky probe constants; `VoxelLook.cpp`: `ApplyLook`, `UpdateSkyPin`, `SkyOpenness`.
3. `Docs/VoxelWorld.md`, Lighting: the S3b rig and the paragraph on this pass.
4. `Source/Unnamed/Tests/VoxelLookTests.cpp`: what is pinned.
5. Only if touching the outline: `Docs/Breaking.md` (`foundation.Break.Outline` row), `FoundationBreakEffects.cpp`
   `DrawOutlineStrips`, `Scripts/Editor/CreateBreakMaterials.py` (`OUTLINE_SHADE_HLSL`).

## Working agreement (as this pass ran)
- Every look change is a runtime switch whose default is the current look; Jesus picks from captured sheets (a look
  pack), then the pick becomes the defaults (C++ `FVoxelLook`, the map scripts' constants and `verify()`).
- Captures: `Scripts/CaptureVoxel.ps1` (`-Fluids scenes` on a fresh copy of the VP1 Superflat world, `-View`, `-Caves`,
  `-TorchLook`, `-Transition`) and `Scripts/TestFoundationRuntime.ps1 -Editor -Suite views` for player views; a queue
  runner with a pause flag, the CEF lock and the quiet-window gate is in `C:\dev\sandbox\look-brightness\tools\Queue.ps1`.
- Pixel numbers come from scripts (`tools\measure.py` boxes, `metrics_a.py`, `metrics_b_*.py`), and each control frame
  is checked for black or saturated before it is cited.
- GPU A/B only in a quiet window, alternating, three runs each.

## Settled decisions
- DayEV 0.6; SkyPin 1 with SkyPinAdapt 2; Sky 1.637 (2^0.6 x 1.08, the sky as bright as before); LowGlow 1; Film 0;
  SkyFill 1; SpeedUp 2; WaterSpecular stays 0.5 here, W3c applies Jesus's 0.25; the Playground follows (EV100 15.1).
- Caves keep the S3b exposure exactly (pin 0); the floor stays 2^-7.
- Outline mode 1 is vanilla's blend on the final image (0.6x).

## Code map
- `Source/Unnamed/Voxel/VoxelLook.{h,cpp}`: dials, maths, `AVoxelWorld::ApplyLook` / `UpdateSkyPin` (members in
  `VoxelWorld.h`: `LookVolume`, `SkyFill`, `LookCollection`, `LookCurve`, `AppliedLook`, `SkyPin*`).
- `VoxelWorld.cpp`: `BuildLayout` finds the rig and calls `ApplyLook`; `Tick` calls it before `UpdateEdgeFog`; the wall
  fog colour x `EdgeFogColorScale`; the `-VoxelShotTransition` hook.
- `Source/Unnamed/Voxel/VoxelTransitionShots.{h,cpp}`: the transition capture.
- `Scripts/Editor/CreateBlockMaterials.py`: `MPC_VoxelLook`, `LOW_GLOW_HLSL`, the water's specular parameter.
- `Scripts/Editor/CreateGeneratedMap.py` / `CreatePlaygroundMap.py`: `SKY_LUMINANCE_FACTOR`, `apply_look()`.

## Known issues (when they matter)
- DN2: read `FVoxelLook` as the day peak (scale DayEV, Sky, the pin over the day) instead of writing the volume or the
  sky factor directly. When DN2 starts.
- Entering a cave from a forest is darker for its first seconds (the eye no longer pre-brightens under trees). If Jesus
  finds cave entries slow: a `SpeedDown` dial, or a faster pin release.
- River hillside haze stays pale: the fog work (sky-only factor vs aerial perspective; check the horizon seam).
- Glow lichen: no dark lichen cave was captured; probe vanilla's level-7 emitter first. With T3c.
- Held item glow is 0.6 EV darker now. If the hand looks dim.
- Outline mode 1 may crawl by a sub-pixel at low screen percentage. If seen.
- CLAUDE.md's Commands line says CreateGeneratedMap.py "verifies, never rewrites"; it now saves the sky factor when it changes (Jesus's line to update).
- `VoxelWorld.md`'s S3b cave table is stale (the spawn's caves changed). When someone re-baselines caves.

## What worked / what failed
- Worked: VisualizeHDR readings of the metered EV100 (crop the overlay text); deterministic captures (same means run to
  run); the queue with a pause flag (PAUSE/RESUME cost nothing); Sonnet subagents for metrics and sheets.
- Failed first: scout T's bias/floor maths (it moves every view between floor and cap); a metering pin for canopies
  (a canopy meters like a cave); the outline shade without a coverage bound (it darkened the sky); a transition frame
  taken in the same tick as the cut. All fixed and recorded.

## Tokens
Lead 656k at report B; subagent peaks 51-97k (8 Sonnet subagents, some resumed; none over 100k).

## Evidence
`C:\dev\sandbox\look-brightness\`: `pack\` (sheets 01-11, metrics-a.md, metrics-b.md), `runs\` (every capture,
runs.log with machine state, ab-summary.txt, hdr-read\, mutations*, looktest-*, testvoxel-*), `outline\`.

## Open questions
- Forest floor brightness under SkyPin + 2: if still too dark, SkyPinAdapt 3 (one switch).
- Water: W3c applies WaterSpecular 0.25 and re-baselines its water numbers against this pass.
