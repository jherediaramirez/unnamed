# Atmosphere handoff: the sun follows the clock and the night is never black; next the twilight dip and stars

**2026-10-13.** The steps so far are recorded in [the cycle record](2026-10-13-Atmosphere-Cycle.md) and [the night record](2026-10-13-Atmosphere-Night.md). The plan and review are `C:\dev\sandbox\atmosphere-review\plan.md` and `review.md`. Direction: Minecraft is the baseline, not the visual target; the square sun and moon and the blocky clouds stay. Jesus decides which atmosphere step comes next.

## Where it stands

- `AVoxelWorld::GetEnvironment()` returns `FVoxelEnvironment`: the clock's day, the moon phase and illumination, the sun and moon directions and elevations, daylight, moonlight and night. Read the environment there; never start another clock.
- `voxel.Atmosphere.Cycle 1` turns one light with the clock: the sun's across the day and twilight, then the moon's. `voxel.Atmosphere.Night 1` adds the night look.
  - The night's tunables are `MoonLux`, `NewMoonLight`, `NightStops` and `NightSky`.
  - Cycle stays 0 by default until the DN-P run and a look sign-off.
- **One writer each.** `ApplyCycle` (`VoxelAtmosphere.cpp`) writes the light. `ApplyLook` (`VoxelLook.cpp`) writes the exposure, sky, sky light and haze, and composes the night through `VoxelAtmosphere::ComposeNight`.

## Next, in order (one chat each)

1. **The twilight dip and stars.**
   - From sun elevation -2 to -9 degrees the scene is darker than at midnight: at 13000 and 23000, ground luma 4-9 against 16.
   - Options: start the moon's fade nearer the horizon, or ease `Night`'s exposure part over the moon's rise.
   - Stars: a cheap deterministic field of small squares in a sky material, fading with `Night` and hidden by terrain.
   - Pairs at 12500/13000/13500/22500/23000/23500 looking both east and west, against vanilla's frames (`C:\dev\sandbox\vanilla-probes`).
2. **The square sun and moon with phases** (plan step 7).
   - Suppress the atmosphere's round disks first, then add the original square textures. The moon's texture shows the phase.
   - Check bloom, so the silhouettes stay square.
3. **The quiet DN-P p95 run** (moving light against a static one, VSM, same views). Do it in a quiet window before any default change.

## How to check

The evidence folder is `C:\dev\sandbox\atmosphere-review\implementation-evidence\2026-10-13-cycle\`.

- **Tests:** `runtests.ps1 -Filter Unnamed.Voxel.Atmosphere` (it targets this checkout).
- **Captures:** `capture.ps1 -Name <n> -Time <t> -Cycle <0|1> [-Extra '<cvars>'] [-Running]`.
- **Sheets:** `compare.py` and `sheet_night.py`.
