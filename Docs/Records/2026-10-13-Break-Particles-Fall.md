# Break particles fall when their block goes

Lane `feature/break-particles-fall`, from `0f31c09`. Jesus's report (2026-10-13): after breaking a block and then the one under it, the first block's particles hang in the air "like I never broke the block". His pick: both of the following. (1) Particles keep checking their support and fall when the block under them is gone, as vanilla's do. (2) They go a bit sooner, with a fade, so nothing lingers.

## Cause

Particle collision already read the live block world every frame (`FFoundationBreakEffects::UpdateParticles`). But `FoundationBreak::StepParticle` returned at once for a resting particle, so a landed particle never looked at its block again. It hung for the rest of its 1.5-2.3 s life. That life was also long next to vanilla's. VP1, the black-box run of the 26.3 client ([record](2026-10-10-Breaking-VP1-Probe.md)), counted 9 fragments left at 0.8 s at All. We kept every particle for at least 1.5 s.

## Change

- **`foundation.Break.ParticleSupport` (default 1):** a resting particle looks up the point 0.02 cm under it (it lies 0.01 cm above its box) through the same cell lookup as collision. That is one cell read per resting particle per frame, with no raycast. Once no box holds that point, the particle falls again in the same step and lands on whatever is below. 0 = until 2026-10-13.
- **`foundation.Break.ParticleLife` (default 1), break particles and hit chips:**
  - 0 = 1.5-2.3 s uniform (until 2026-10-13).
  - 1 = 0.2 s / (0.1 + 0.9u): 0.2-2.0 s, median 0.36 s, 1 in 3 past 0.5 s, 1 in 6 past 0.8 s. The quad shrinks to nothing over its last 0.1 s, which gives a fade with no material change.
  - 2 = the same lives popping out at full size, as vanilla does (ideas P5), one value away.
- **Vanilla reference, unverified:** nothing published gives the block particle's life. Mode 1's spread is a fit to VP1's two black-box facts: 9 left at 0.8 s at All (1/6 of 64 is 10.7), and the last gone at about 2 s. VP1 could not measure the spawn count or single lives (0.4 s frame spacing).
- **Random draws:** every mode takes one draw at the same place in the stream, so mode 0's spawns (lives, patches, everything after) are bit-identical to before; the `Life` test checks it.
- **The pack:** `FoundationTourProbe.cpp`'s capture pack lists both switches, saves them and restores them. Its configurations keep both at 0, so its frames are as before. The restore case is now a lambda shared with the fall pair.

## Checks

- `Unnamed.Foundation.Break.*`: 16 of 16 pass (2026-10-13, after the build). There are two new tests:
  - `Support`: a particle resting on the upper of two stacked boxes. It stays while the box stands. At 1 it falls in the step the box goes and lands on the lower box at 100.01; at 0 it hangs at 200.01.
  - `Life`: mode 0 equals the old draw for 100 draws. Modes 1 and 2 stay in 0.2-2.0 s, with 1/6 past 0.8 s and 1/3 past 0.5 s (60,000 draws, within 0.01). The shrink is 1 until the last 0.1 s, 0.5 halfway through it and 0 at the end. Modes 0 and 2 never shrink. Break particles take the mode.
- The fall pair (`TestFoundationRuntime.ps1 -Editor -BreakPack -FrameRates 60 -ExtraArgs '-FoundationTourBreakFall'`, offscreen; see [Breaking](../Breaking.md#checks)): 0 failures and 24 shots, in `Tests/Evidence/2026-10-13/Playground-break-fall/Runtime-20261013-214621`. The second break came 1.04-1.06 s after the first. Particles counted on the gone block's top face:

  | Config | Before the break | +50 ms | +150 ms | +300 ms | +600 ms |
  |---|---|---|---|---|---|
  | `old` | 31 | 31 | 31 | 31 | 27 |
  | `support` | 29 | 0 | 0 | 0 | 0 |
  | `new` | 5 | 0 | 0 | 0 | 0 |

  Live particles in `new`: 77 at the break, then 46 at +300 ms and 16 at +600 ms. In `old` there were 132 at the break, still 121 at +600 ms. In the `old` frames a cluster hangs one block up; in `support` every particle lies on the floor.
- `Scripts\RunChecks.ps1 -Profile final -Flight smoke`: GREEN (21 min), `Tests/Evidence/2026-10-13/Checks-final-20261013-212532`. It ran while Jesus's Minecraft was open, so its timings are not findings.

## Open

- Jesus picked `ParticleLife 1` (the shrink) on 2026-10-13; 2 (vanilla's pop) stays one value away. Open: whether the shorter life feels too short with Minimal particles.
