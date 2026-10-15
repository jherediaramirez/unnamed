# Atmosphere T7a, parked: day/night on by default, horizon band, stars in turns (unfinished)

**2026-10-15.** Lane `feature/survival-atmosphere` (worktree `C:\dev\projects\Unnamed-atmos`). Parked mid-step on the PM's order: the atmosphere line is deferred until after Jesus's QC. WIP commit `4bd81710` on top of `d6c26416`. Not merged, not reviewed, not checked. Read [the 10-13 atmosphere handoff](2026-10-13-Atmosphere-Handoff.md) first, then this.

## Goal (from the step brief)

Jesus: "I guess we never did properly implement the day/night cycle/sun/moon? It looks the same to me?" His tap was "Fix the 3 issues, then turn on". So: fix the far-horizon band, the dark water at dusk and the star streaks; turn `voxel.Atmosphere.Cycle`, `Twilight` and `Stars` on by default (0 keeps today's behaviour); check that `/time set` changes the sky at once and that the clock survives save and load; then before/after captures for Jesus.

## State in `4bd81710`

- **Defaults flipped in source:** `Cycle 1`, `Twilight 1`, `Stars 1`; `Night` was already 1. New test `Unnamed.Voxel.Atmosphere.Defaults` (VoxelTwilightTests.cpp, inside WITH_DEV_AUTOMATION_TESTS).
- **Horizon band, fixed on the side away from the sun.**
  - Cause: the haze colours the fog floor below the horizon from the sky's ambient light. At dusk and dawn that ambient is dark red while the sky near the horizon is navy, and the haze never sees the navy twilight backdrop. Day and midnight already matched.
  - Fix: new cvar `voxel.Atmosphere.TwilightHaze` (default `"0.3 1 4"`, linear R G B, space-separated because CaptureVoxel splits -ExecCmds on commas). It multiplies the haze's sky-ambient colour by tint ^ TwilightSkyWeight, so the tint is white by day and at midnight.
  - Wiring: `FNight::HazeTint` (VoxelAtmosphere), then `EdgeHazeTint` (set in ApplyLook), then `FEdgeFogState::HazeTint`. It is applied at the haze branch's `SetSkyAtmosphereAmbientContributionColorScale` in VoxelWorld.cpp. `"1 1 1"` turns it off.
- **Stars:** `M_AtmosphereStars` now draws after motion blur (`CreateStarMaterial.py`). That pass does not depth-test against terrain: the stars and the twilight backdrop showed over the ground. So the graph also compares SceneDepth with PixelDepth (12 nodes). The backdrop shares the material. Captures after the mask show nothing drawn over the fog floor.
- **Reverted:** moving the twilight terrain floor after DOF. It had no effect on water, because the engine composites the after-DOF water after that blendable.
- **Clock save/load:** already implemented before this step. world.json `clocks` / `minecraft:overworld` {ticks, paused, rate} (UnnamedSavedWorlds.cpp ~231-287), read by `AVoxelWorld::SetupClock` and written by `SaveClock`, with a round-trip test (VoxelClockTests.cpp ~472). It has not been checked black-box.

## Numbers so far

Mean row sRGB, in an elevated view (camera -31,-29,140 blocks; west toward the dusk sun, east away from it), sky just above the horizon vs the fog floor below it:

| time, side | sky above horizon | floor before | floor with "0.3 1 4" | floor with "0.6 1 3" |
|---|---|---|---|---|
| 6000 noon | (184,193,193) | (171,187,194) | (171,187,194) | – |
| 18000 midnight | (22,35,53) | (22,36,53) | (22,36,53) | – |
| 13000 away (east) | (3,8,33) | (24,8,11) | (5,5,39) | (14,6,31) |
| 13000 sun (west) | glow (62,23,31) | (24,7,10) | (5,5,37) | (14,6,29) |
| 23000 away (west) | (3,8,32) | (31,11,12) | (8,8,42) | – |
| 23000 sun (east) | glow (62,23,31) | (31,11,12) | (8,8,40) | – |

- Luma step from the glow to the floor on the sun side at 13000: 22.6 before, 26.5 with the default tint, 24.0 with "0.6 1 3".
- The PM's rule was: keep the away side matched and the sun-side step no harsher than before; if no single tint does both, favour the away side. Following it, the default stays `"0.3 1 4"`, and the sun-side step is a known limit.
- Water vs sand in the 13000 overview: Twilight 0 gives water (6,6,6) vs sand (11,10,9). Twilight 1 gives water (7.3,8.2,13.6) vs sand (22.7,22.4,24.2). By day (6000): (75,115,179) vs (136,153,192).
- Capture GPU frame ms (1280x720, other lanes' editors running, CPU 1-68%): about 6.5 overview and 3.5 view with all switches off. With them on, similar when the machine was quiet; 12-15 ms when other lanes were busy, so no timing claim.

## Known issues

- **Dark water at dusk and dawn** (Jesus tapped "Known issue, fix after QC"). Water is about 1/3 of sand at twilight vs about 2/3 by day. The engine composites water after the twilight floor pass, and moving the floor after DOF did not help. Fixes:
  - **C (preferred):** the water's own twilight emissive minimum through the look MPC, in `CreateBlockMaterials.py`. Needs SM-Blocks' block-material lock.
  - **B (fallback):** move the twilight floor after tonemapping (display space, like the cave floor). This re-tunes the terrain twilight look and its oracle/test numbers.
- **Sun-side band step:** a dark navy floor under the red glow, 26.5 luma vs 22.6 before. A sun-side glow in the haze would need a second light: the one directional light has handed over to the moon by 13000. "0.6 1 3" is the compromise, one cvar away.
- The 13500 west floor reads (0,10,69) under a (25,31,44) sky, bluer than the sky. Check this when tuning.
- A thin brighter seam remains at the horizon row (y≈270). It was present before too.
- clang-format ran after the last build (comment reflow only), so the DLL is older than the sources. Rebuild before any check.

## Remaining T7a work, in order

1. Rebuild. Fast-turn star proof: the flight at 18000 with `r.MotionBlurQuality 4`, after, against `captures/stars-before-flight-18000/flight-circle.png`. Plus Stars 1 vs 0 at the same view (overview and view) as a pixel-diff floor row over terrain and water.
2. Pick the band tint for good (the default or "0.6 1 3") with the 12500/13500/23500 rows. The 13500 west row needs a look.
3. Defaults-on check: `RunChecks -Profile quick`, comparing every hash line with the baseline `Tests/Evidence/2026-10-15/Checks-quick-20261015-142506/hashes.txt` (6 of 9 equal there, as expected for quick). If any line moves, stop and report.
4. `/time set day|noon|night|midnight` captures (`-Extra 'voxel.Time set <marker>'`), and the clock save/reload black-box run (create-world, set night, leave, reopen, `voxel.Atmosphere.Info`).
5. Before/after captures at 6000/12000/18000/23000, same view. The before set exists (`captures/before-*`, all switches 0). Build a side-by-side sheet.
6. Sonnet pre-commit review, `RunChecks -Profile final -Flight smoke`, the end-to-end checks, report A, then commit.
7. Merge `feature/survival` (it moved to 5d271969, which touches this README) at the merge point.
8. Then **T7b**: the square sun and moon with phases (deferred until after QC).

## Evidence

All under `C:\dev\projects\Unnamed-atmos\Tests\Evidence\2026-10-15\AtmosphereT7a-20261015-142236\` (capture.ps1 there; machine\*.json per run):

- `band-fix-13000.png`: band before (top) vs default tint (bottom), both sides.
- `iso-band.png`, `iso-band-2.png`: the isolation set (cycle only, twilight, fog floor hidden, noon, midnight, dawn).
- `water-pair-13000-v2.png`, `water-grid.png`: water at 13000 with Twilight 0, the floor before DOF, the floor after DOF.
- `captures/stars-before-flight-18000/flight-circle.png`: streaking stars with the old material.
- `captures/before-{6000,12000,18000,23000}`: today's look (all off) for the before/after sheet.
- Void runs: `stars-after-flight-18000-VOID-staleDLL` and `stars-after-flight2-18000-INCOMPLETE-parked`. The `t1-*` and `t2*` band rows were contaminated by the unmasked star material, and the `t3*` rows had the tint split by commas; none of them counts.
- Baseline checks: `Tests\Evidence\2026-10-15\Checks-quick-20261015-142506\summary.txt` (GREEN on d6c26416, 210 voxel suites).

## Open questions for Jesus (after QC)

- Sun-side band: keep the default (best match away from the sun) or take "0.6 1 3" (softer step under the glow)?
- Water: C (block materials, needs SM-Blocks) or B (floor after tonemapping)?
- Push: the lane stays local; pushing is Jesus's call.
