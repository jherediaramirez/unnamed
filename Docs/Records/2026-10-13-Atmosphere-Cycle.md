# Atmosphere: the sun follows the clock

**2026-10-13 · branch `feature/atmosphere-cycle` from `bd0e1b2` · commit `11783c1`.** This is the first visible step of the atmosphere plan (`C:\dev\sandbox\atmosphere-review\plan.md`). Direction: Minecraft is the baseline, not the visual target; the square sun and moon and the blocky clouds stay.

## What changed

- **`FVoxelEnvironment`** (`Source/UnnamedVoxel/Sky/VoxelEnvironment.h`): a snapshot read from the existing `FVoxelWorldClock`. It is not a second clock and never changes the clock.
  - It holds the day, the time, the total, the rate, paused and advance_time, the smooth ticks, the moon phase, the sun and moon angles and directions, the sun's elevation, and daylight (0 below the horizon, 1 from 6°).
  - `SunAngle` is the classic eased orbit as a formula. It stays within 0.056° of 26.3's `minecraft:day` timeline, so a packaged game needs no `Reference/` files.
- **`voxel.Atmosphere.Cycle`** (default 0), in `Source/Unnamed/Voxel/VoxelAtmosphere.cpp`.
  - At 1, the map's sun (Movable, the atmosphere sun light first) turns with the clock and fades out below the horizon.
  - At 0, the sun light is never written. Switching from 1 to 0 puts the map's rotation and intensity back once.
  - Nothing else writes the directional light; `ApplyLook` and the SkyPin metering never did.
  - While the clock is paused, nothing is written, so the shadows aren't invalidated for nothing.
- **`voxel.Atmosphere.Info`** prints the snapshot.
- **Tests:** `Unnamed.Voxel.Atmosphere.Orbit`, `.Daylight` and `.Clock`. They cover the timeline oracle, the wraps, the light direction, the day's wrap, pause, rate, and that reading never moves the clock.
  - 3 mutations were all caught: the orbit shifted, advance_time ignored, and the light not negated.

## Evidence

Everything is in `C:\dev\sandbox\atmosphere-review\implementation-evidence\2026-10-13-cycle\` (README there).

- **Look** (`sheet-ground.png`, the review's seed and ground camera, clock paused):
  - Cycle 0 matches the review's baseline: the sky region differs by 0.005-0.023/255.
  - At Cycle 1, noon has the sun overhead and a deeper blue sky. 12000 has a low western sun and a warm horizon. 18000 is **black**: there's no moon light yet, the atmosphere has no sun, and the exposure is pinned.
- **Cost** (GPU ms, mean of 60 frames at 1280x720, clock running, min of 3):

  | Time | view | ground |
  |---|---|---|
  | noon | 3.54 → 3.53 | 3.82 → 3.53 |
  | sunset | 3.51 → 3.75 | 3.84 → 3.89 |

  This is on a busy machine and is not a p95. No measurable cost showed up.
- **RunChecks final, smoke flight:** green.
  - One interaction check went red in the first run under load ("mining the grass pops the dandelion" during a 4 FPS frame). The re-run on the same DLL was green.
  - Run folders: `Tests/Evidence/2026-10-13/Checks-final-20261013-112946` (red) and `Checks-final-20261013-114356` (green).

## Open

- **Night exposure and moon fill** (plan step 5): the next step.
- **The quiet DN-P p95 run:** moving sun against a static one, VSM; required before Cycle can default on.
- **The round atmosphere sun disk** is still drawn. The square sun and moon are step 7.
- **No afterglow** once the sun is below the horizon.
