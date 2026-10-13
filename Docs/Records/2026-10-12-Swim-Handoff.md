# Swim handoff: swimming step 1 done, into underwater fog/tint

## Reading list
1. [2026-10-12-Swimming.md](2026-10-12-Swimming.md): what step 1 changed, values and sources, tests, evidence.
2. `Docs/Foundation/Behavior.md`, "Simulation order and policies": the swimming paragraph.
3. Scout AA's facts: `C:\dev\sandbox\scout-swim\facts.md` §4.10 (fog "very dense", clears over ~30 s, colour #050533; lava fog dense) and §1 (later steps).
4. The water lane's look work (W3c: `VoxelLook`, `CreateBlockMaterials.py` water hunks) now on `feature/voxel-world`: the fog/tint step builds on it.

## Settled decisions (Jesus, 2026-10-12)
- Q1 (a): `foundation.Swim` 1 by default; 0 keeps the old land walk bit for bit.
- Q2 (a): creative flight ignores fluids.
- Q3 (a): lava stops counting as water for vitals only under Swim 1.
- Q4 (a): the next step is underwater fog/tint.

## Code map
- `FoundationSimulation.h/.cpp`: `EFoundationFluid`, the fluid facts in `FFoundationEligibility`, the `InFluid` branches in `Step` (jump gate, accel, vertical, drag/gravity, jump-out).
- `FoundationProfile.h`: the swim block after flight (probe values and unverified feel values).
- `FoundationLab.h/.cpp`: `GetFluid`, `GetFluidAboveCm`, `GetFluidFacts` (Playground water boxes); `VoxelWorld.h/.cpp`: `GetFluidBox` with the lava flag and the overrides.
- `FoundationPawn.cpp`: CVar `foundation.Swim`, facts before each step, vitals' water facts.
- Tests: `Unnamed.Foundation.Swim` (`FoundationTests.cpp`); probe `-Suite swim` (`FoundationSwimProbe.cpp`).
- For fog/tint: the pawn already exposes `bEyesInWater` after each step (water only under Swim 1); `GetFluid(eye point)` tells lava apart.

## Known issues
- Space/Sneak (±4 cm/tick), shallow jump (40 cm) and jump-out (60 cm raise, 30 cm/tick) are unverified feel values.
- Lava uses water's controls; its acceleration is unverified. The Playground has no lava (sim test only).
- Not yet: current push (needs a per-cell flow vector), swim pose and 1-block gaps, sounds, FOV.
- `Docs/VoxelWorld.md` still lists "lava counts as water" as a gap.

## Evidence
- `Tests/Evidence/2026-10-12/Checks-final-20261012-201747/summary.txt` (final -Flight smoke, green), the merge run in report B.
- `Tests/Evidence/2026-10-12/Playground-swim/Runtime-20261012-204851/runtime-60.json` (control pair).
- `C:\dev\sandbox\scout-swim\mutations\summary.txt` (5 of 5).

## Open questions
- After Jesus tries a voxel lake: tune the feel values?
- Fog/tint: vanilla's ~30 s clearing, or a fixed underwater distance first?

## Next step: underwater fog/tint
When the eyes are in water, draw vanilla's dense underwater fog tinted #050533 (and lava's dense orange fog), and clear it on exit. It's a rendering step on top of W3c's water look. Scout it first from the data and wiki (fog distances, colour, clearing time); don't re-derive swimming.
