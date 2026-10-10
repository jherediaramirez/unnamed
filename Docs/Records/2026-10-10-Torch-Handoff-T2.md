# Torch handoff: into T2 (torch data)

Start here for the torch work. T0 + T1, the light probe, is done (commit `bf93516`, branch `feature/voxel-light`, merged into `feature/voxel-world` by the night manager). Its results are in [2026-10-10-Torch-Light-Probe.md](2026-10-10-Torch-Light-Probe.md). Scout G's research and draft plan are committed unchanged as [2026-10-10-Torch-Scout-G-Research.md](2026-10-10-Torch-Scout-G-Research.md). Read its sections 1 and 2 (T2 and T3), then this file.

## The plan and where it stands

Jesus asked for "a torch. Especially for the caves." By 01:26 on October 10 he accepted scout G's ladder for the renderer:

1. one point light per torch with MegaLights ray-traced shadows, if T1's probe passes;
2. otherwise VSM shadows for the nearest 8 to 16 lights;
3. otherwise the block-light field (T5).

| Session | What | State |
|---|---|---|
| T0 + T1 | Glow-cube capture and the local-light probe | done; rung 1 (MegaLights), the look call by Nyx, delegated, 04:20 |
| **T2** | **Torch data: reference, facts, item** | **next; needs no renderer** |
| T3 | Placeable torch with light | after S6 (model rendering), S7 (placement and survival), T2 |
| T4 | Held light, a setting (optional) | after T1 |
| T5 | Block-light field | later, or sooner if T3's light system fails |

## This task: T2

Scout G's section 2 "T2. Torch data" is the scope. In short:

- **Reference:** the torch family enters `Reference/minecraft-blocks-26.3` through its own tools, never by hand. `CLAUDE.md` says that reference is hash-checked and regenerated only through its tools.
  - The first batch is `torch` and `wall_torch`, unless Jesus answers scout G's Q4 otherwise.
  - Manifest entries, baked models (`bake_models.py` already handles element rotation, which the wall torch's -22.5 degrees about z needs) and textures.
- **Facts (`FactsDump.java`, the public API at runtime):**
  - per state: outline and collision boxes, light, `replaceable` and occluding faces;
  - the new support table: for every imported state and face, whether a floor torch survives on top and a wall torch against the side. It records vanilla's answer; leaves are invalid supports.
  - Signatures with `javap -public` only; never method bodies or constants from bytecode.
- **Item:** one `torch` item whose placement picks `torch` or `wall_torch` from the clicked face. `wall_torch` gets no item (S5's rule). The items S2 registry records `places_block: torch` today and needs the wall alternative.
- **Gate:** no column change (radius-8 hashes identical), `Blocks.Registry` counts updated, the blocks verifier passes.

## What T3 needs from T1's numbers

Scout G's T3 design (section 2, "The light-source system") leaves three values to T1:

- **Pool size K** (the nearest K sources within R blocks get a live light; scout G proposed 64 to start).
  - From T1: start at 64 and cap at 100. 100 MegaLights lights clustered in one cave cost +1.39 ms in total, and 10 cost +0.78 ms, so 64 interpolates to about +1.1 ms in that worst case.
  - The cost follows how many lights overlap each pixel, so merge sources closer than 2 blocks (scout G's rule).
- **Radius R** (how far from the camera sources are considered; scout G proposed 48 blocks). The attenuation radius per light is a separate value: (level - 1) blocks, 13 for a torch.
  - From T1: keep 48. Lights wholly outside the frustum are culled before lighting (the per-light pass counts equal the in-frustum counts).
  - Not measured: lights spread out to R against the same number clustered. Time both in T3's capture.
- **Falloff and intensity:** UE's inverse-square falloff with the attenuation window, a radius of 1,300 cm and a source radius of 5 cm, as measured.
  - The probe's 8,000 cd blows a cave out at the cave exposure: the N = 10 and N = 100 frames are mostly white.
  - Tune the intensity, and the falloff exponent if needed, in T3's capture against vanilla screenshots (probe P4: a 9 x 4 x 9 stone room, one torch, then four). Expect several times less than 8,000 cd (not measured).
- **Glowing cubes** (T0's away test):
  - They light caves now: a wall 5 blocks away rises 39 to 83/255 (glowstone +52.4) at +0.03 to +0.06 ms each.
  - With the cube off screen the same wall drops 18 to 38/255, a known limit: 35 to 43% of the light comes from screen traces.
  - Whether T3 gives them a point light too is Jesus's question below.
- **Edit latency** (T3's "lights update when the authority applies an edit"):
  - A mined niche is lit correctly once its column is back on screen (2 to 3 frames): VSM at once, MegaLights after 5 to 11 frames of temporal accumulation, Lumen alone after 3 to 4.
  - Shadows need no work from T3 beyond following the edits.

## How to measure again

- **`Scripts\CaptureVoxel.ps1 -Lights`** runs the whole probe, with the usual shots after it, in about 4.5 minutes. Its output:
  - `lights.txt`: the tables;
  - `lights-passes.txt`: the per-case ProfileGPU signature check;
  - `lights.csv`: every patch value per frame;
  - `lights-*.png`.
- **Overrides:** the game-run option `-VoxelLightsIntensity=<cd>` changes the probe lights' intensity. `CaptureVoxel.ps1` does not pass it, so add it to its arguments for a one-off run.
- **Timings need a quiet machine:** GPU numbers are valid only with no other editor, build or Java process running. The lanes share one GPU.
- **In game:** `voxel.LightProbe <N> [megalights|vsm|none]` in `L_Generated` (development builds). Put `r.MegaLights.EnableForProject 1` first for megalights.
- **Montages:** `C:\dev\sandbox\torch-light-probe\montage.py <run folder>` (pillow) writes `montage-glowstone.png` and `montage-probe-<mode>.png` with their numbers.

## Gaps and known issues

- **The probe cave is chosen per seed.** It is the cave with the most floor cells near the centre, not the `cave` view: that view reaches open sky, had no plain floor near its camera, and had room for only 15 spaced lights. T3's capture should reuse the probe cave (it is deterministic for the dev seed), or build scout G's P4 room.
- **The leak test is inverted from scout G's text.** The light hangs over the surface and the cave floor below it is measured, because a top face lit from below reads 0 even unshadowed. T3's leak gate should use the same geometry.
- **The exposure is the cave floor's** (EV100 6 clamp, exposure 2^-7 = 0.0078125 in every case). Brighter scenes would read a light's change smaller.
- **The noise gate uses lit pixels.** The whole-frame noise mean grows with the lit area alone. Both figures are in the results record.
- **Not covered:** CPU cost per light beyond the game-thread mean (`r.Visibility.LocalLightPrimitiveInteraction`), translucent and cutout surfaces (leaves, glass) under MegaLights, and many lights at different distances from the camera.
- **`CLAUDE.md`'s commands list** does not name `-Lights` yet (worker chats never edit it). Suggested wording for the manager: add `[-Lights]` to the `CaptureVoxel.ps1` line, with "-Lights first runs the torch light probe (lights.txt, lights-passes.txt)".

## Questions for Jesus

1. **Glowing cubes and T3's light system.** Should glowstone and the other light-15 cubes also get a point light in T3, or stay emission-only with the off-screen fade?
   - *Recommendation:* give every placed light source at level 10 or more a pooled point light, glowing cubes included (scout G's conditional rule), and never lava. It is affordable (about 0.006 ms a light in a cluster of 100), and it ends the fade.
2. **MegaLights for the project.** T3 has to turn on `r.MegaLights.EnableForProject` (it costs +0.12 ms with no lights). That changes the project's default rendering, so it needs your yes when T3 lands.
3. **Scout G's open questions 3 to 5** still stand: a held light as a setting, the first torch batch, and particles in T3.

## Next

T2 as above. T3 waits for S6 and S7. T4 (a held light) can follow T2 at any time if Jesus wants it.
