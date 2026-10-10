# Torch light probe (T0 + T1): results

Torch session T0 + T1, October 10, 2026, night shift (parallel lane `C:\dev\projects\Unnamed-items`, branch `feature/voxel-light`). It follows scout G's plan, committed unchanged as [2026-10-10-Torch-Scout-G-Research.md](2026-10-10-Torch-Scout-G-Research.md) (sections 1 and 2, "T0 + T1"). Jesus accepted the plan's ladder by 01:26: one point light per torch with MegaLights ray-traced shadows if this probe passes; otherwise VSM shadows for the nearest 8 to 16 lights; otherwise the block-light field (T5). This session measures which rung holds. It builds no torch.

**Result: rung 1 holds. A MegaLights point light per torch passes every gate. VSM fails its own gate at 10 lights. Glowing cubes light caves, but partly only while on screen (a known limit).**

- **MegaLights gate (≤ 1.0 ms at N = 10, ≤ 2.5 ms at N = 100, over no lights with MegaLights off, floor included):** +0.78 ms at 10 lights and +1.39 ms at 100 (run 3; runs 1 and 2 within 0.03 ms). The floor with no lights is +0.12 ms.
- **Noise:** lit-pixel noise at N = 10 is 1.64 x N = 1 (gate 2 x). The whole-frame mean is 2.4 x, but it grows with the lit area alone.
- **VSM gate (≤ 1.5 ms at N = 10):** +1.68 ms, fails; 100 lights cost +8.95 ms.
- **Unshadowed:** cheap (+0.26 / +1.42 ms), but it leaks +31.0/255 onto a cave floor under three blocks of rock. Shadowed modes measure -0.01 (vsm) and -0.10 (megalights) against a gate of 1.
- **Glowing cubes (gate: a wall 5 blocks away rises at least 20/255):** glowstone +52.4, every cube +39 to +83.
- **Away view:** the same wall drops 18 to 38/255 with the cube off screen (gate 3). Screen traces carry 35 to 43% of a cube's light on that wall. Whether T3 also gives glowing cubes a point light is a question for Jesus (recommended: yes).
- **Cost of a glowing cube:** +0.03 to +0.06 ms of GPU.
- **Look call (Nyx, delegated, 04:20; Jesus may overrule):** rung 1, a MegaLights point light per torch. It passes every gate, with N = 10 at +0.78 ms (gate 1.0), N = 100 at +1.39 ms (gate 2.5), noise 1.64 x (gate 2 x) and the leak at -0.10 (gate 1). The control is clean: N = 0 against N = 1 differs by 16.4 luma on 34.4% of pixels, and the pass signatures prove each mode switch. The glowing cubes' away drop is a known limit, and the 8,000 cd blow-out goes to T3's look tuning (P4).

## What was built

- **`voxel.LightProbe <N> [megalights|vsm|none] [intensity cd] [radius cm]`** (`Source/Unnamed/Voxel/VoxelLightProbe.*`, development builds only).
  - **What it does:** N movable point lights on cave floor cells around the player's view; `voxel.LightProbe 0` removes them.
  - **Where the lights go:** a flood fill through covered air from the eye's cell (24 blocks across, 16 up or down), keeping floor cells with headroom. Cells in front of the view come first, each at least 2 blocks from the ones before.
  - **Light defaults:** 1 m above the floor, attenuation radius 1,300 cm, source radius 5 cm, 8,000 cd.
  - **Pool and modes:** the lights are pooled on the `AVoxelWorld` actor. `megalights` lights allow MegaLights and cast shadows, `vsm` lights cast shadows outside MegaLights, and `none` lights are unshadowed.
  - **MegaLights for a session:** `r.MegaLights.EnableForProject 1` by `-ExecCmds` or the console. The project default stays 0.
- **`Scripts/CaptureVoxel.ps1 -Lights`** (`-VoxelShotLights`, `Source/Unnamed/Voxel/VoxelLightShots.*`).
  - The probe runs once the first view is on screen, before the usual shots. It puts back every cell it edited and the MegaLights setting, then the five shots and `build.txt` follow as before.
  - **Files:** `lights.txt` (the tables), `lights.csv` (every patch value per frame), `lights-*.png`, and `lights-passes.txt` from `Scripts/SummarizeLightProbe.ps1`, which reads the ProfileGPU dumps in `game.log`.
- **Hooks:** `AVoxelWorld` holds the driver and exposes the `-VoxelShotCaves` views (`GetCaveShotViews`). Nothing changes in generation, meshing, config or the default rendering, so the radius-8 hashes, goldens and saved worlds are untouched.

## Method

- **Views.**
  - The probe cave serves every case but the leak test: a covered, dark cave, big enough for 100 lights. Of the caves sampled every 8 blocks within 3 columns of the centre, it is the one with the most floor cells.
  - Its camera stands on the floor cell that sees the most of a sample of them within 20 blocks, and faces their mean direction.
  - The `cave` view itself (it reaches open sky) was tried first. It had no plain floor cell near its camera for the cube, and room for only 15 spaced lights.
- **Exposure.**
  - Eye adaptation settles once on the probe cave at 50 f-stops per second, then the camera's adaptation speed is set to 0. The exposure is read back through a scene view extension at the end of every measured step.
  - Local exposure stays as the player sees it.
- **MegaLights.** The driver switches it per case by console (`r.MegaLights.EnableForProject`, set by console for that run) and restores it at the end. Every case is checked against its pass signature in the GPU profile (below).
- **Patches.** A patch is the mean sRGB (0 to 255, the mean of R, G and B) of a square about two fifths of a block across, centred on a block face.
  - Faces are chosen by a voxel line-of-sight test from the camera, turned towards the cube and the camera, with walls preferred.
  - Every patch is read from the same frames as the screenshots, through the viewport's screenshot delegate.
- **Lag.** Lag is the first frame from which a 3-frame centred mean stays within 2/255 of the settled value (the mean of frames 121 to 150).
- **Away view.** The smallest turn that takes the whole cube off screen while the 5-block patch stays on it.
- **GPU time.**
  - `RHIGetGPUFrameCycles`, the mean of 120 frames after 90 settle frames.
  - Three rounds per case, the second in reverse order, against drift.
  - One `ProfileGPU` per case in round 1.
- **Leak test, inverted from the scout's text.** One light hangs 1 m over the top of a column whose top three blocks are opaque, over a cave. The camera sits in the cave, and the measured patch is the cave floor under the light.
  - The scout's version would put a light in the cave and measure the surface above it. That patch could never fail: a top face lit from below has N·L < 0, so it reads 0 even unshadowed.
- **Edit test.** A cube in view 3 to 10 blocks away, with another behind it, is mined. One light hangs in the air cell two out from its face.
  - The test counts the frames until the column is back on screen, then the frames until the niche's back face settles within 2/255.
  - `none` gives the remesh plus Lumen baseline; `vsm` and `megalights` add their shadow update.
- **Noise.** The per-pixel temporal standard deviation of luma over 30 frames of a still view, reported three ways: the frame's mean, its 99th percentile, and the mean over lit pixels (those whose mean differs from the same MegaLights setting's N = 0 mean by more than 8/255).
  - The whole-frame mean grows with lit area alone, so the gate uses the lit-pixel figure.

## Results

Timed in the night manager's quiet window, 03:57 to 04:11. Before each run the only process matching UnrealEditor, UnrealBuildTool, ShaderCompileWorker, java, javaw or node was Adobe Creative Cloud's background `node.exe`. Minecraft was closed. All figures are run 3 (`Tests/Evidence/2026-10-10/VoxelShots-20261010-040617-lights-timed3`) unless marked. Runs 1 (`-035712-lights-timed1`) and 2 (`-040139-lights-timed2`) ran the same code without the cube timing control.

- **Patches:** values are mean sRGB, 0 to 255.
- **Exposure:** 0.0078125 (2^-7, the cave's EV100 6 floor) at the end of all 30 measured steps in every run.
- **Restore:** every run put back its 2 edited cells and `r.MegaLights.EnableForProject` 0.

### Glowing cubes (T0)

Mean sRGB of the patches before the cube and settled (frames 121 to 150), the lag to within 2/255, and the away view's drop. `wall2` is a floor face 1.5 blocks from the cube (no wall face lay near 2 blocks), `wall5` and `wall10` are walls 5.0 and 10.1 blocks away.

| Cube | wall2 | wall5 | wall10 | Lag (wall2 / wall5 / wall10 frames) | Away, wall5 (drop) | GPU ms (+ vs before) | Removed again |
|---|---|---|---|---|---|---|---|
| glowstone | 0.5 -> 91.3 (+90.8) | 12.3 -> 64.7 (+52.4) | 0.0 -> 1.7 (+1.7) | 59 / 36 / 0 | 42.0 (22.7) | 3.31 (+0.03) | 3.26 (-0.02) |
| sea_lantern | 0.5 -> 121.0 (+120.4) | 12.3 -> 80.9 (+68.6) | 0.0 -> 5.1 (+5.1) | 66 / 43 / 21 | 51.4 (29.5) | 3.31 (+0.03) | 3.28 (-0.01) |
| shroomlight | 0.5 -> 81.9 (+81.4) | 12.3 -> 66.5 (+54.2) | 0.0 -> 3.4 (+3.4) | 60 / 38 / 16 | 38.4 (28.0) | 3.31 (+0.03) | 3.29 (+0.01) |
| ochre_froglight | 0.5 -> 128.0 (+127.5) | 12.3 -> 95.3 (+83.0) | 0.0 -> 7.3 (+7.3) | 67 / 45 / 31 | 57.3 (38.0) | 3.34 (+0.06) | 3.28 (+0.00) |
| jack_o_lantern | 0.5 -> 66.1 (+65.6) | 12.3 -> 51.3 (+39.0) | 0.0 -> 2.3 (+2.3) | 54 / 34 / 6 | 32.9 (18.4) | 3.33 (+0.05) | 3.28 (-0.00) |

GPU before any cube: 3.28 ms. Runs 1 and 2 timed each cube straight after its 151 frames of read-backs and measured +0.8 to +2.3 ms; run 3 adds 60 frames without read-backs first and times the cube removed again as the control, so run 3 stands (the record's runs 1 and 2 cube timings are an artefact of the read-backs).

### Local lights (T1): GPU frame

Mean of 120 frames per round at 1600 x 900, three rounds (the second in reverse order), run 3; the last two columns are the same mean in runs 1 and 2, against their own base.

| Case | Lights shown | Round 1 | Round 2 | Round 3 | Mean ms | vs base | Run 1 vs base | Run 2 vs base | Game thread ms |
|---|---|---|---|---|---|---|---|---|---|
| base | 0 | 3.28 | 3.59 | 3.57 | 3.48 | +0.00 | +0.00 | +0.00 | 5.09 |
| floor | 0 | 3.59 | 3.59 | 3.63 | 3.60 | +0.12 | +0.14 | +0.13 | 6.45 |
| megalights-1 | 1 | 4.03 | 4.05 | 4.04 | 4.04 | +0.56 | +0.59 | +0.56 | 6.57 |
| megalights-10 | 10 | 4.25 | 4.28 | 4.24 | 4.26 | +0.78 | +0.81 | +0.79 | 6.69 |
| megalights-100 | 100 | 4.85 | 4.90 | 4.85 | 4.86 | +1.39 | +1.42 | +1.41 | 6.62 |
| vsm-1 | 1 | 3.78 | 3.74 | 3.74 | 3.75 | +0.27 | +0.30 | +0.28 | 6.56 |
| vsm-10 | 10 | 5.15 | 5.16 | 5.16 | 5.16 | +1.68 | +1.71 | +1.69 | 6.54 |
| vsm-100 | 100 | 12.31 | 12.74 | 12.22 | 12.42 | +8.95 | +8.94 | +8.96 | 7.26 |
| none-1 | 1 | 3.58 | 3.58 | 3.59 | 3.58 | +0.10 | +0.15 | +0.13 | 6.54 |
| none-10 | 10 | 3.76 | 3.72 | 3.73 | 3.74 | +0.26 | +0.28 | +0.27 | 6.60 |
| none-100 | 100 | 4.91 | 4.88 | 4.89 | 4.89 | +1.42 | +1.44 | +1.44 | 6.73 |

### Local lights (T1): look, noise and pass signatures

Image against the same MegaLights setting's N = 0 (mean luma difference, share of pixels changed by more than 8/255); per-pixel temporal standard deviation of luma over 30 frames (frame mean, 99th percentile, lit pixels); and the ProfileGPU signature (`lights-passes.txt`).

| Case | Image vs N = 0 | Noise std (mean, p99, lit) | Lights in view | MegaLights events (ms) | Per-light passes (probe lights) | VSM projections | ShadowDepths ms | Signature |
|---|---|---|---|---|---|---|---|---|
| base | - | 0.10, 0.45, -1.00 | 0 (0) | 0 (0.000) | 1 (0) | 1 | 0.491 | valid |
| floor | 0.04, 0.0% | 0.06, 0.40, -1.00 | 0 (0) | 0 (0.000) | 1 (0) | 1 | 0.446 | valid |
| megalights-1 | 16.37, 34.4% | 0.12, 0.50, 0.25 | 1 (1) | 2 (0.434) | 1 (0) | 1 | 0.623 | valid |
| megalights-10 | 79.74, 61.6% | 0.29, 1.20, 0.41 | 10 (10) | 2 (0.949) | 1 (0) | 1 | 0.508 | valid |
| megalights-100 | 163.28, 86.6% | 0.45, 1.60, 0.50 | 100 (99) | 2 (1.376) | 1 (0) | 1 | 0.553 | valid |
| vsm-1 | 16.44, 34.4% | 0.18, 0.60, 0.31 | 1 (1) | 0 (0.000) | 2 (1) | 2 | 0.503 | valid |
| vsm-10 | 80.43, 62.2% | 0.23, 0.95, 0.31 | 10 (10) | 0 (0.000) | 11 (10) | 2 | 0.915 | valid |
| vsm-100 | 167.19, 87.8% | 0.26, 0.90, 0.27 | 100 (99) | 0 (0.000) | 100 (99) | 2 | 3.580 | valid |
| none-1 | 18.34, 41.1% | 0.18, 0.55, 0.30 | 1 (1) | 0 (0.000) | 2 (1) | 1 | 0.437 | valid |
| none-10 | 82.77, 63.6% | 0.23, 0.95, 0.30 | 10 (10) | 0 (0.000) | 11 (10) | 1 | 0.555 | valid |
| none-100 | 171.91, 89.2% | 0.25, 0.85, 0.26 | 100 (99) | 0 (0.000) | 100 (99) | 1 | 0.446 | valid |

### Leak and edit tests

- leak-base: cave floor 34.44 mean sRGB (std 0.04)
- leak-none-1: cave floor 65.57 mean sRGB (std 0.03), +31.14 against leak-base
- leak-vsm-1: cave floor 34.53 mean sRGB (std 0.03), +0.10 against leak-base
- leak-floor: cave floor 34.35 mean sRGB (std 0.04)
- leak-megalights-1: cave floor 34.41 mean sRGB (std 0.06), +0.06 against leak-floor
- edit-none: niche on screen 3 frames after the edit; 16.3 before (where the cube still stood), 189.9 at frame 0, settled 191.9; settled within 2/255 from frame 0
- edit-vsm: niche on screen 2 frames after the edit; 15.8 before (where the cube still stood), 192.0 at frame 0, settled 193.4; settled within 2/255 from frame 0
- edit-megalights: niche on screen 2 frames after the edit; 15.8 before (where the cube still stood), 189.3 at frame 0, settled 192.8; settled within 2/255 from frame 4

## The rung the numbers pick

Rung 1: one movable point light per torch, shadowed through MegaLights (ray-traced shadows, the default method), with MegaLights turned on for the project when T3 lands.

- **Not decided here:** `r.MegaLights.EnableForProject` stays 0 in the project until T3 changes it. That changes the project's rendering, so it is T3's (and Jesus's) call. It costs +0.12 ms with no lights.
- **Not needed:** rung 2 (VSM for the nearest 8 to 16 lights). At 10 lights VSM costs more than MegaLights at 100.
- **T5's field** remains the gameplay light (spawning, melting) and a later look option. It is not needed for rendering torches.

## What T3 needs from these numbers

- **Pool size K: start at 64, cap at 100.**
  - **Measured:** 100 MegaLights lights in one cave, all within about 24 blocks of the camera and 99 of them in the view frustum, cost +1.39 ms in total. From 10 to 100 lights the cost grew by 0.61 ms.
  - **Estimated:** at the scout's K = 64 that interpolates to about +1.1 ms. This is a dense worst case: MegaLights' cost follows how many lights overlap each pixel, not how many exist.
  - T3's gate stays "within 0.3 ms of T1 at the same N".
- **Radius R: 48 blocks, as scout G proposed.**
  - Lights wholly outside the frustum are culled before lighting (the per-light pass counts equal the in-frustum counts), so sources beyond the view cost little. Distance dependence was not measured; T3's capture should time K lights spread out to R against the same K clustered.
- **Light shape:**
  - **Measured:** an attenuation radius of 1,300 cm (13 blocks, a torch's light 14 fading to 1), inverse-square falloff, source radius 5 cm.
  - **Intensity:** the probe's 8,000 cd is far too bright at the cave exposure; the N = 10 and N = 100 frames are mostly white. T3 tunes the intensity and falloff against vanilla screenshots (scout G's P4). Expect several times less (not measured).
- **Edits:**
  - **Measured:** a mined block's niche is lit correctly once its column is back on screen (2 to 3 frames): vsm at once, megalights after 5 to 11 more frames of temporal accumulation, none (Lumen alone) after 3 to 4.
  - T3's lights only need to follow the authority's edits; shadows need nothing extra.
- **Glowing cubes: a question for Jesus, not decided tonight.**
  - They light caves now: +52.4 on the wall at +0.03 to +0.06 ms each.
  - The away drop is a known limit.
  - Recommendation: T3's light system gives a point light to every placed light source at level 10 or more, glowing cubes included (scout G's conditional rule), so their light stops fading off screen. At about 0.006 ms per light in a cluster of 100, this is affordable. Lava never gets one.

## Evidence

- `Tests/Evidence/2026-10-10/VoxelShots-20261010-040617-lights-timed3/`:
  - montages: `montage-glowstone.png` (before, after, away) and `montage-probe-megalights.png` (N = 0, 10, 100), with `-vsm` and `-none` beside them;
  - `lights.txt`, `lights-passes.txt`, `lights.csv`, `lights-*.png`, `game.log`.
- Runs 1 and 2: `-035712-lights-timed1`, `-040139-lights-timed2`.
- Functional passes (not timed, run beside other lanes): `-lights-functional1` to `-functional5`.
- Montage script: `C:\dev\sandbox\torch-light-probe\montage.py`. Tables: `C:\dev\sandbox\torch-light-probe\tables.py`.

## Limits

- **One seed, one cave:** the dev seed and its probe cave, at 1600 x 900, on this PC (one GPU shared by three lanes, timed only in quiet windows).
- **Not measured:**
  - lights spread over distance;
  - leaves, glass and other cutout or translucent surfaces under MegaLights (MegaLights traces alpha-masked surfaces only on screen by default);
  - CPU cost per light beyond the game-thread mean, which barely moved (6.4 to 7.3 ms in every lit case);
  - many cubes at once.
- **The exposure is the cave floor's:** brighter scenes read the same light as a smaller change.
- **The `wall2` patch is a floor face** 1.5 blocks from the cube; no wall face lay within 2 ± 0.6 blocks of it.
