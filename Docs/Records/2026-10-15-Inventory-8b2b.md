# Survival step 8b2b: the furnace, smoker and blast furnace particles (track T1 Containers)

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. Base: 8a5e87ea (18-chest merged with feature/survival ad045c40; feature/survival was at the same commit). Plan: `C:\dev\tools\shift-managers\survival\tracks\T1-plan-8b2b.md` with the final plan's differences, approved by SM-Containers. Line 1's last step under the feature freeze: particles only.

## What 8b2b built

- **The spawn** (`FoundationBreak::SpawnFurnaceParticles`, pure): per display callback, R15's particles (`survival2.r15.particles`, `survival2.r15.exact_callback_chances`): the furnace one smoke and one flame, the smoker and the blast furnace one smoke each. The furnace's and blast furnace's leave the front (the centre + facing x 0.52, lateral U[-0.3, 0.3) along the face, y U[0, 6/16) or U[0, 9/16)); the smoker's leave (0.5, 1.1, 0.5). Velocity uniform per axis within R15's constructed bounds (in world axes, as measured), life uniform in whole ticks (smoke 8-39, flame 12-43).
- **The pool** (`FFoundationBreakEffects`): a third pool of 512 camera-facing quads next to the chips and the crumbs, its own seeded stream, its own flight (`StepFurnaceParticle`: no gravity or collision, provisional drag and rise) and the smoke's layer stepped over its life (`GetFurnaceParticleLayer`). `GetLiveFurnaceParticleCount`, `GetFurnaceParticlesSpawned` for probes.
- **The assets** (new, ours, no lock): `T_FurnaceParticles` (9 layers, 8 x 8: the 26.3 jar's `generic_7`..`generic_0` in `particles/smoke.json`'s order, then `flame`; PNGs in `Art/StandIns/particle`, 9 `Docs/StandIns.md` rows) and `M_FurnaceParticle` (masked, two-sided, lit; CD0 the layer, CD2 1 for the full-bright flame; the smoke in a provisional grey), both by `Scripts/Editor/ImportFurnaceParticles.py` (additive: the array is verified against the PNGs' pixel hashes, never rebuilt).
- **The one ambience pass** (Codex 25 #1's clock): `PlayFurnaceSounds` became `UFoundationInteractionView::PlayFurnaceAmbience`. Per lit block within 16 blocks, `FoundationFurnace::RollDisplayCallbacks` (new, pure) turns the clock's ticks into display callbacks (1 in 50 a tick, provisional as before; none with `foundation.Furnace.Ambience 0`); each callback spawns the particles and rolls the sound's 0.1. Paused, the clock gives no ticks, so nothing spawns; a container screen keeps it running. The facing comes from the block state.
- **The force** (`foundation.Furnace.AmbienceForce`, a cheat variable, default 0): every tick is a callback, for the tour shot and probes.
- **The tour** (`FoundationTourProbe.cpp`, case n1): a lit furnace, smoker and blast furnace 2 blocks ahead, fronts toward the camera, forced; the control shot `24i-furnace-particles-off` with `foundation.Furnace.Ambience 0`, then `24i-furnace-particles` with 1. Blocks, entities and switches are put back after.
- **Found while doing it:** the first tour placed the blocks facing away. `FoundationBlockPlacement::GetHorizontalLook` takes Minecraft's yaw, not Unreal's (the probe now converts through `GetYawPitch`); the smoke was spawning behind the blocks.

## Values

Measured (R15, `SURVIVAL-RUN2-FINDINGS-26.3.md` lines 126-157): the counts, the offsets, the velocity bounds, the lives. Provisional (6 rows in `provisional-values.md`, T1 8b2b): the flight (x 0.96 a tick, no gravity), the smoke's rise (0.001 blocks per tick each tick), the quad sizes (smoke 12 cm, flame 8 cm), the frame stepping (8 equal parts of the life; the frame order is game data), the uniform distributions, the smoke's grey (0.3) and the flame's brightness (1). The two 8b1 rows now point at `PlayFurnaceAmbience` / `FoundationFurnace::DisplayChance`.

Deviation from the plan file: the plan named the layers `smoke_0`..`smoke_7`; the 26.3 jar has no such files. Smoke uses `generic_0`..`generic_7` (`assets/minecraft/particles/smoke.json`), in that file's order.

## Tests

- `Unnamed.Foundation.Furnace.Particles` (new): for each block and the 4 facings, 2,000 callbacks with a seeded stream. It checks the count per callback (2 / 1 / 1), the order (smoke, then the furnace's flame), every spawn inside R15's box (front -0.02 or 1.02 on the facing's axis, lateral [0.2, 0.8) on the other, the height range, the smoker's fixed point), and that the lateral and height scatter fill their ranges. Velocities sit within R15's bounds (the numbers written out in the test) and the smoke's and flame's spreads fill over 90% of them. Lives are whole ticks within 8-39 / 12-43 and reach both ends. It also checks the layers (0, 4, 7 over the smoke's life; 8 for the flame) and the flight (a still flame stays put, a still smoke rises).
- `Unnamed.Foundation.Furnace.DisplayCallbacks` (new): 1 in 50 over 100,000 ticks (1,800-2,200); the force on by default is off (0); forced, every tick; switch 0, none (also forced); paused (the clock's 0 ticks), none.
- `Unnamed.Foundation.Furnace.AmbiencePause` (8b2a) is unchanged and still passes (a container screen keeps the clock running; the pause menu stops it).
- Mutation checks (`C:\dev\sandbox\inv8b2\mutate-8b2b.ps1`, `mutants-8b2b.txt`, `mutants-8b2b-2.txt`): front 0.5 for 0.52 (8 failing checks), the lateral not rotated (8), the smoker at 1.0 (4), the blast furnace's height 6/16 (4), the furnace without its flame (12; on the first run it crashed the test on `Out[1]`, so the check was made bounds-safe and rerun), and the switch ignored in `RollDisplayCallbacks` (2). All 6 are caught.
## Verification

- `Scripts\RunChecks.ps1 -Profile final -Flight smoke -ExpectedHashes C:\dev\tools\shift-managers\survival\expected-hashes-survival.txt`, after the review fixes and a `-DisableAdaptiveUnity` build: **GREEN** in 26 min (`Tests/Evidence/2026-10-15/Checks-final-20261015-190240/summary.txt`). Test guards: 105 files, 0 outside. Native: 506 of 506. Voxel: 211 suites. Hashes: 9 of 9 equal. Runtime all: 191 checks, 0 failures. Tour: 51 checks, 0 failures. Create-world and the smoke flight passed. Machine at the start: only this lane's UnrealEditor-Cmd; no java.
- In that run's tour (`Playground-tour/Runtime-20261015-191647`): the control `24i-furnace-particles-off` placed 3 lit blocks, spawned 0 and had 0 live. `24i-furnace-particles` spawned 80 in about 1 s (furnace 2 a tick, smoker 1, blast furnace 1, forced) and had 73 live.
- The control pair, measured on the earlier tour run `Runtime-20261015-184755` (`C:\dev\sandbox\inv8b2\pair.py`):
  - Both frames are valid: mean RGB 100.8 / 118.3 / 136.8, saturation 83, 44,229 and 45,629 colours.
  - Pixels changed by more than 24 between the frames: the furnace's box 666, the blast furnace's 84, the rest of the frame 34. Most of the smoker box's 2,128 is its lit front's animation; its smoke shows only as a small mark above it.
  - Crops: `C:\dev\sandbox\inv8b2\crop-on.png`, `crop-off.png`, `crop-diff.png`.
- `ImportFurnaceParticles.py -AllowCommandletRendering`: `FURNACE_PARTICLES PASS: T_FurnaceParticles built, 9 layers match the PNGs; M_FurnaceParticle PS instr 239, samplers 2` (`C:\dev\sandbox\inv8b2\import-8b2b-1.log`).
- The Sonnet 5.5 pre-commit review (diff only, 1,046 lines) found no bugs. Fixed from it:
  - the stale "particles are 8b2's" sentence in Docs/Items.md;
  - the control check now counts spawns since its setup, not the live total, which particles from earlier shots could flake;
  - the flame's velocity spread is now checked;
  - an unused lambda capture is removed.
  Kept: `ECVF_SetByCode` in the test (the repository's convention); the smoke's normal (below). The furnace tests were rerun after the fixes (9 of 9) before the final run.

## Known issues (with "when")

- The smoke and flame are small and the smoke is faint, so the smoker's smoke barely shows at 2 blocks. The sizes, grey, flight and frame timing are provisional. When: when value-holder or a black-box capture gives them (provisional-values.md, T1 8b2b), or a look call in QC.
- The smoke is lit by its camera-facing quad's own normal (the chips use an up normal), so its shade can change with the view angle. When: with the look values above.
- The display-callback scheduling stays provisional (1 in 50 per lit block within 16 blocks; vanilla's is client-spatial, R15). When: a scheduling model from value-holder.
- Carried from 18-chest: the chest lid (after QC), a double chest's reach check, the 5-tick opener recheck, the creative screen pausing, the focus loss under a survival screen, the missing recipe and fuel items, Codex 18 #2 (multiplayer).

## Token figures

- Lead: 83.0k at the plan; 252.9k after the review (MeasureAgentUsage).
- Subagent: the Sonnet 5.5 reviewer, 100.8k peak.
- Cost drivers: the two tour runs (the facing fix) and the mutation rerun after the noflame crash.

## Evidence

- `Tests/Evidence/2026-10-15/Checks-final-20261015-190240/summary.txt`: GREEN, final, smoke flight.
- `Tests/Evidence/2026-10-15/Playground-tour/Runtime-20261015-191647/`: `runtime-60-tour-24i-furnace-particles.png` and `-off.png` from the final run.
- `C:\dev\sandbox\inv8b2\`:
  - mutation runs: `mutants-8b2b.txt`, `mutants-8b2b-2.txt`, and the `mut8b2b*` logs;
  - test runs: `tests-8b2b-1.log`, `tests-8b2b-2.log`;
  - the import: `import-8b2b-1.log`;
  - review: `review-diff-8b2b.patch`;
  - final checks: `runchecks-final-8b2b.log`;
  - the frame comparison: `pair.py` and the crops.