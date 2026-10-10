# Breaking: the vanilla client probe VP1 (measured)

Break lane `feature/voxel-break`. VP1 measures the breaking presentation that neither the wiki nor the data publishes ([ideas](2026-10-10-Breaking-Feel-Ideas.md), section 4.4). It is a black-box run of the 26.3 client: screenshots and the audio output, with no game code read. It ran on Jesus's go, while he was away from the PC, at 10:20 (outline, cracks, particles) and 10:29-10:31 (sounds, swing), with the Mojang server EULA accepted at his word.

## Harness (sandbox, not committed)

`C:\dev\sandbox\break-vp1\`:
- **Scripts:** `scripts\run_probe.py` (the steps) and `scripts\harness.py` (launch, RCON, input, capture, onsets). `README.md` covers the design.
- **Server:** a local offline server (`server-26.3.jar`, RCON) on a flat world.
- **Client:** the Modrinth 26.3 client in `game\`, offline, joining by `--quickPlayMultiplayer`.
- **Control:** aim by `/tp`, input by `SendInput`. Mining Fatigue I stretches the stone break to about 500 ticks.
- **Captures:**
  - frames come from the game's own F2 screenshots;
  - sound comes from a WASAPI loopback of the default speaker;
  - the swing comes from 60 fps DXGI desktop duplication (`scripts\dxgrab.py`, enabled with `VP1_DXGI=1`).
- **Commands:**
  - Everything except sound and swing: `uv run --no-project --python 3.13 --with mss --with soundcard --with numpy --with pillow python -I run_probe.py --all --skip sounds,swing --no-dry-run --accept-eula`, from `scripts\`.
  - Sounds: `--step sounds`.
  - Swing: `VP1_DXGI=1 ... --with dxcam ... --step swing`.
  - A `CANCEL` file stops the run before any launch.
- **Gotchas found on the way:**
  - The 26.3 dedicated server defaults to `white-list=true`, so the harness sets it false.
  - With the display asleep, desktop capture (GDI, and DXGI too) is all black. The game's F2 screenshot reads its own framebuffer and works regardless.
  - F1 hides the block outline as well as the HUD, so outline shots keep the HUD on.
  - `soundcard` and `dxcam` cannot share one process (COM thread mode), so sounds and swing run as separate launches.
  - Each F2 shot takes about 0.4 s, so frames after the first are spaced by that, not by the plan.

## Results

The analysis is in `C:\dev\sandbox\break-vp1\analysis\vp1-analysis.md`, with `outline_sheet.png`, `cracks_sheet.png` and `particles_sheet.png`. The run folders are `out\20261010-102027` (main), `out\20261010-101832` (HUD-hidden outline control), `out\20261010-102918` (sound WAV) and `out\20261010-103016` (swing).

| What | Measured | Confidence |
|---|---|---|
| Outline | Black at 40% opacity, 3 px at 1 block and 2-3 px at 3 blocks, constant in screen pixels. Where edges overlap the opacity compounds (0.36). It follows the outline shape: a slab gets a half box, a dandelion its small box. No outline at 6 blocks (out of reach) | high |
| High Contrast outline | An opaque core, rgb(87, 255, 225), 3 px, with an opaque black halo of about 3 px each side: about 9 px close, about 7 px at 3 blocks | high |
| Crack stages | Stage = floor(progress × 10). The changes fall at progress 0.10-0.15, 0.28-0.30, 0.50-0.53 and 0.89-0.91 (frames 8 ticks apart). None on the first frame, stage 0 by 0.4 s | medium-high |
| Crack blend | Multiply 2×: crack texels give 0.479 (grey 61) and 1.215 (grey 155) of the uncracked colour, against 2·grey/255 = 0.478 and 1.216. Identical in R, G and B; other texels 1.004 | high |
| Crack coverage | Every visible face. A slab's cracks stay on its half. Glass and leaves show cracks over their transparent texels. Gone 0.5 s after release | high (stone, slab); medium (glass, leaves) |
| Break particles | Fragments about 0.1 block, which land and lie on the ground (collision) and disappear between about 1.5 and 2.3 s. Fragments left at about 0.8 s: All 9, Decreased 6, Minimal 3. Grass block's are dirt-brown (81, 58, 41). The count at spawn could not be measured (the +50 ms cloud overlaps), and the 26.3 F3 screen shows no particle counter | low-medium |
| Hit particles | A few fragments fall from the hit face to the ground while mining. The rate could not be measured (0.4 s frame spacing) | low |
| Hit sound | Every 4 ticks: 51 onsets in 10 s of loopback, intervals 160-230 ms, median 200 ms | high |
| Swing | Holding attack moves the arm continuously, with no rest frames. One click shows about 0.5 s of motion including the rise back. The 6-tick duration stays from the wiki | medium |

Not measured: dandelion cracks (it breaks in one tick), the stages of slab, glass and leaves, the exact particle life and spawn count, and the hit-particle rate.
