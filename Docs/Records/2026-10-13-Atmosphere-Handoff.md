# Atmosphere handoff: the sun follows the clock, into night exposure + moon fill

**2026-10-13.** The previous step is in [the atmosphere cycle record](2026-10-13-Atmosphere-Cycle.md). The plan and review are `C:\dev\sandbox\atmosphere-review\plan.md` and `review.md`. Direction: Minecraft is the baseline, not the visual target; the square sun and moon and the blocky clouds stay.

## Where it stands

- `AVoxelWorld::GetEnvironment()` returns `FVoxelEnvironment`: the clock's day, the moon phase, the sun and moon directions, elevation and daylight. Read the environment there; never start another clock.
- `voxel.Atmosphere.Cycle 1` moves the sun and fades its light out below the horizon. The night is black at 1. The default stays 0 until the night step and the DN-P run are accepted.
- `ApplyCycle` (`VoxelAtmosphere.cpp`) is the sun light's only writer. `ApplyLook` owns the exposure, sky luminance and sky light. When night needs those, compose the change in one place; don't add a second writer.

## Next step: night exposure + moon fill (plan step 5), one chat

- **Moon light:** a non-shadowed moon fill from `Environment.MoonDirection`, scaled by phase. Only one light casts shadows at a time. Fade the outgoing light near the horizon before handing over.
- **Exposure:** a bounded night exposure composed with the current `FVoxelLook` / SkyPin pin. The night must be dark but readable, and auto exposure must not turn it back into day.
  - Caves keep their settled response.
  - A night switch (e.g. `voxel.Atmosphere.NightExposure`) can take the night back to today's.
- **Acceptance:**
  - fixed captures at 0/6000/12000/13000/18000/23000 from the ground, at altitude and over the ocean;
  - a normal-speed adaptation walk: outdoors, then a roof or cave, then outdoors again;
  - torches at night;
  - full and new moon.
- **The owed quiet DN-P p95 run:** moving sun against a static one, VSM, the same views. Do it in a quiet window (`QuietWindow.ps1`) before any default change.

## How to check

- **Tests:** `runtests.ps1 -Filter Unnamed.Voxel.Atmosphere` (the sandbox evidence folder has a copy that targets this checkout).
- **Looks:** `capture.ps1 -Name <n> -Time <t> -Cycle <0|1> [-Running]` (same folder), which reuses the review's seed and camera. Compare with `compare.py`.
