# Atmosphere: a night that is never black

**2026-10-13 · branch `feature/atmosphere-cycle` · plan step 5** (`C:\dev\sandbox\atmosphere-review\plan.md`). This follows [the sun following the clock](2026-10-13-Atmosphere-Cycle.md).

## What changed

- **`FVoxelEnvironment`** gains `MoonElevation`, `Moonlight` (0 until the moon is 6 degrees up, full at 12), `MoonIllumination` (by phase: full 1, new 0) and `Night`.
  - `Daylight` now eases across twilight, from +6 degrees of sun elevation down to -6. Below the horizon the sky atmosphere's transmittance keeps the sun off the ground but still lights the sky, which gives the twilight glow.
  - `Night` = 1 - `Daylight`.
- **`voxel.Atmosphere.Night`** (default 1) only acts with `voxel.Atmosphere.Cycle` 1, and Cycle stays 0 by default.
  - **Moon fill:** once the sun's light is out (Daylight 0), the same single light becomes the moon. It is cool (.62, .72, 1) and lights at `MoonLux` (300) x Moonlight x the phase share (new moon `NewMoonLight` .35). Both lights are 0 at the handover. `ApplyCycle` stays the light's only writer, and Cycle 0 puts back rotation, intensity and colour.
  - **Night look:** `VoxelAtmosphere::ComposeNight` runs in `ApplyLook`, the one writer of exposure and sky. It lowers the open-sky exposure range by `NightStops` (4) x Night x the SkyPin pin, so caves (pin 0) keep theirs. It scales the sky and haze luminance by `NightSky` (.25) at full night. Night is applied in steps of 1/64.
  - **Cave floor:** `voxel.Look.CaveFloor` applies at night unchanged. On its own it reads as near black (luma about 3).
- **Moon phases** dim the moon light. There are no stars.

## Evidence

Everything is in `C:\dev\sandbox\atmosphere-review\implementation-evidence\2026-10-13-cycle\`.

- **Pairs** (`sheet-night-2.png`, Cycle 1, Night 0 vs 1, the review's ground camera, clock paused): mean luma of the sky region and of the lower half.

  | Time | Night 0 sky / ground | Night 1 sky / ground | Look at Night 1 |
  |---|---|---|---|
  | 13000 | 0.6 / 3.9 | 6.7 / 8.9 | red twilight |
  | 18000 | 0 / 2.8 | 12.2 / 16.0 | navy sky, moonlit terrain and water |
  | 23000 | 0 / 2.8 | 3.4 / 3.6 | very dark: the camera faces away from the dawn |

  For comparison, vanilla 26.3 at midnight has a sky top of 9-23 and ground of 27 (field) and 64 (torch field).
- **Cycle 0** still matches the review's noon: the sky region differs by 0.013/255.
- **Tests:** `Unnamed.Voxel.Atmosphere.Night`. The mutants "cave pin ignored" and "phase ignored" were both caught.
- **RunChecks final, smoke flight:** green (`Tests/Evidence/2026-10-13/Checks-final-20261013-124628`).

## Open

- **Twilight is darker than midnight**, at sun elevation -2 to -9 degrees: the sky's glow is fading and the moon is not yet full.
- **The ground is darker than vanilla's midnight,** on purpose. `NightStops` raises it.
- **No stars.**
- **The moon draws as the atmosphere's round disk** (and the sun as its round disk). The square sun and moon are plan step 7.
- **No test drives `ApplyLook` or `ApplyCycle` themselves,** only their pure parts.
- **The quiet DN-P p95 run is still owed** (moving light, VSM) before Cycle can default on.
