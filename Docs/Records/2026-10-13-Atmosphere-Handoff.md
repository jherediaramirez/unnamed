# Atmosphere handoff: sun cycle, night, opt-in twilight and stars; next the twilight look limits or the square sun and moon

**2026-10-13.** The steps so far are recorded in [the cycle record](2026-10-13-Atmosphere-Cycle.md), [the night record](2026-10-13-Atmosphere-Night.md) and [the twilight and stars record](2026-10-13-Atmosphere-Twilight-Stars.md). The plan and review are `C:\dev\sandbox\atmosphere-review\plan.md` and `review.md`. Direction: Minecraft is the baseline, not the visual target; the square sun and moon and the blocky clouds stay. Jesus decides which atmosphere step comes next.

## Where it stands

- `AVoxelWorld::GetEnvironment()` returns `FVoxelEnvironment`: the clock's day, the moon phase and illumination, the sun and moon directions and elevations, daylight, moonlight and night. Read the environment there; never start another clock.
- `voxel.Atmosphere.Cycle 1` turns one light with the clock: the sun's across the day and twilight, then the moon's. `voxel.Atmosphere.Night 1` adds the night look.
  - The night's tunables are `MoonLux`, `NewMoonLight`, `NightStops` and `NightSky`.
  - Cycle stays 0 by default until the DN-P run and a look sign-off.
- **Twilight and stars (opt-in candidate, integrated locally 2026-10-13).** `voxel.Atmosphere.Twilight` (default 0; `TwilightFloor` 1.5, `TwilightSky` 0.2) bridges the dusk/dawn dip with a temporary exposure-compensated HDR terrain minimum before DOF (`M_AtmosphereTwilight`, `CreateTwilightMaterial.py`) plus a separately faded navy sky backdrop. `voxel.Atmosphere.Stars` (default 0; `StarBrightness` 0.3) draws 768 deterministic depth-tested square stars with no twinkle (`M_AtmosphereStars`, `CreateStarMaterial.py`). Both need Cycle 1 and Night 1.
- **One writer each.** `ApplyCycle` (`VoxelAtmosphere.cpp`) writes the light. `ApplyLook` (`VoxelLook.cpp`) writes the exposure, sky, sky light and haze, composes the night through `VoxelAtmosphere::ComposeNight`, and owns the twilight floor.
- **Commits (feature/atmosphere-cycle, local, not pushed):** candidate `4d23faf` (19 files), merge of `feature/voxel-world` at `42f6606` (far trees on by default) = `9ef2a4f`, then this documentation commit. Merged editor DLL SHA-256 `7E3897048D0B3CF2F420B8836E6ADC06313E412BDE7EB7C258213E4FAC613FBE` (the pre-merge candidate DLL was `DC871461...BF09C0F0`).
- **Merge validation:** `Tests/Evidence/2026-10-13/Checks-merge-20261013-161409/summary.txt`, `RunChecks -Profile merge -Flight smoke -Base feature/voxel-world`, GREEN in 24 min, every step run (native crashes 0, voxel, 9/9 hashes, runtime all/interaction/tour, create-world, flight, npm, source check, verifiers). Run with the process-only `UE-CmdLineArgs` override `-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False`, which keeps the auto-started MCP HTTP server from charging socket errors to unrelated native tests; the variable was absent before and restored to absent after.
- **Merged recaptures:** west 13000 and 23000, both switches OFF and ON, on the merged DLL: `Tests/Evidence/2026-10-13/AtmosphereTwilightStars-20261013-133953/captures/merge-*`, compared with the pre-merge `final2-*` in `merge-recapture/merge-vs-premerge-west.png` and `.json`. The opaque hillside, off-axis sky, top and lower-centre regions match the pre-merge values within 0.14 luma. The canopy and lower-half regions read 0.4-1.1 lower because the incoming far trees now draw trunks under the far crowns; that is the far-tree change, not a twilight change.

## Known look limits (unresolved; keep them stated)

- Water stays dark beside the lifted terrain (the terrain minimum is before DOF; water/glass filter it through their after-DOF blend).
- Elevated views show a black far-horizon band (cause not isolated: fog-floor geometry and the near-unlit handover are candidates).
- Stars streak during fast camera turns with motion blur enabled (`r.MotionBlurQuality 4` in the runs).
- Controlled performance (quiet p95), packaged-build validation and continuous moving-camera acceptance are not done.

## Next, in order (one chat each; Jesus picks)

1. **The twilight look limits:** the far-horizon band (matched feature-OFF and fog-floor-disabled flights first, to isolate the cause), water beside lifted terrain, star sharpness in turns (motion-blur-off comparison; consider excluding stars from motion blur). Keep both switches default 0 until Jesus signs off the look.
2. **The square sun and moon with phases** (plan step 7). Suppress the atmosphere's round disks first, then add the original square textures; the moon's texture shows the phase. Check bloom, so the silhouettes stay square.
3. **The quiet DN-P p95 run** (moving light against a static one, VSM, same views) in a quiet window before any default change.

## How to check

- Tests: `Unnamed.Voxel.Atmosphere.*`, `Unnamed.Voxel.Twilight*`, `Unnamed.Voxel.Stars*` (or `RunChecks -Profile final`).
- Captures (arguments only, offscreen): `Tests/Evidence/2026-10-13/AtmosphereTwilightStars-20261013-133953/capture.ps1 -Name <new> -Time <t> -Direction East|West -Twilight 0|1 -Stars 0|1`; it refuses to overwrite evidence.
- Earlier cycle/night helpers: `C:\dev\sandbox\atmosphere-review\implementation-evidence\2026-10-13-cycle\`.
