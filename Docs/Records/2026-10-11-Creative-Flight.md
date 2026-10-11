# Creative flight: double-tap Space to fly (option A)

Lane: main folder, `feature/voxel-world`. Brief: `C:\dev\sandbox\scout-flight\build-prompt.md`; facts: `C:\dev\sandbox\scout-flight\facts.md` (scout O). Jesus's picks: Creative only; Space rises and the Sneak binding (C) sinks; FOV +10% while flying; a fixed 7-tick double-tap window.

## What changed
- `FFoundationSimulation` (`FoundationSimulation.h/.cpp`): `FFoundationEligibility::bMayFly`, state `bFlying`, `bHadJump`, `FlyTriggerTimer`. In `Step`:
  - **Toggle:** the jump press edge arms `FlyToggleWindowTicks`; a second edge while time remains toggles flight. It copies the sprint double tap (decrement at step start, then check), so taps up to 6 ticks apart toggle and 7 apart don't. Losing `bMayFly` or mobility ends flight.
  - **While flying:** no jump impulse and no gravity. Horizontal friction is air drag only, and acceleration is `FlyAccelerationCmPerTick` (× `FlySprintMultiplier` sprinting); food never blocks sprint-flying. Jump − Sneak × `FlyVerticalCmPerTick` vertically, then `Vz *= FlyVerticalDrag`. Sneak doesn't crouch (a low ceiling still forces the pose), there's no ledge restraint and no fall distance, and the FOV target is `FlyingFovMultiplier × sprint`.
  - **Landing:** while flying, grounded counts only a downward move that was stopped (hovering at floor height stays flying), and grounded ends flight.
- `FoundationProfile.h`: `FlyToggleWindowTicks` 7, `FlyAccelerationCmPerTick` 5, `FlySprintMultiplier` 2, `FlyVerticalCmPerTick` 15, `FlyVerticalDrag` .6, `FlyingFovMultiplier` 1.1.
- `FoundationPawn.cpp`: `bMayFly = Vitals.Mode == Creative`; `Vitals.ResetFall()` after each flying step's `StepVitals`; `SetSimulationEnabled` clears the double-tap state and keeps `bFlying` (pausing doesn't drop you).
- `FoundationHud.cpp`: F3 motion line `fly=`.
- Docs: `Docs/Foundation/Behavior.md` (step order, sneak while flying, a flight paragraph), `Controls.md` and `controls.json` (jump row), `README.md` (Space row).
- Untouched: `voxel.Fly` (dev and capture tool), input assets and bindings, the menu, every walking number.

## Pinned values and sources
| Value | Ours | Source |
|---|---|---|
| Fly speed | 10.8889 blocks/s (5 × .98 / (1 − .91) cm/tick) | wiki Flying 10.92; Transportation 11.0 |
| Sprint-fly speed | 21.7778 blocks/s | wiki Flying 21.6; Transportation 22.0 |
| Vertical | 7.50 blocks/s (15 / (1 − .6)) | wiki Transportation 7.49 |
| FOV | ×1.1 flying, ×1.265 sprint-flying | wiki Flying (+10%) |
| Double-tap window | 7 ticks | Flight Window mod listing ("vanilla is 7"), secondary |
| Release drift | 5.4742 blocks (measured in the test) | wiki Flying: "a few blocks" |

## Open unknowns
- The 7-tick window comes from a secondary source (a mod listing); the wiki doesn't state it.
- The 15 cm/tick, .6 split of the vertical speed is a feel choice; the wiki pins only the 7.49 blocks/s terminal speed.
- A low ceiling forces the crouch pose while flying, and our pose keeps the ×.3 crouch slowdown; whether Java slows flight in that case is unknown.
- Step-up while flying isn't documented; flying bumps into ledges (step-up needs ground).
- A tap shorter than one 50 ms tick can fall between steps (per-tick sampling, as Java's).

## Tests
- New `Unnamed.Foundation.Flight` (pure simulation on a flat ground box, tolerances from `Behavior.md`: speed ≤ 1%):
  - **Toggling:** without `bMayFly`, taps 3 ticks apart never fly. Taps 2, 3 and 6 ticks apart toggle, 7 and 8 don't. Another double tap ends flight and gravity lands you. Losing `bMayFly` or mobility ends flight.
  - **Speeds:** 10.8889 blocks/s at 30/60/120 FPS through `Advance` (200 ticks each). Sprint-flying is 21.7778 blocks/s, with and without food. FOV is 1.1 flying and 1.265 sprint-flying.
  - **Rise and sink:** climb and sink are 7.50 blocks/s, sinking keeps 180 / 162 and doesn't crouch, and both held hold height. Release drift is 5.4742 blocks.
  - **Landing:** sinking onto the floor lands, with fall distance 0 throughout. Hovering .0005 cm above the floor for 40 ticks stays flying.
  - **Collision:** a wall stops flight and ends sprint. A 1.5-block ceiling forces the crouch pose while flying.
- **Mutations:** 8 of 8 fail the test (`C:\dev\sandbox\flight-build\runs\mut-*`): no `bMayFly` gate, no mobility gate, window 6, window 8, no landing rule, gravity in flight, Sneak crouching in flight, fly sprint ×1.3.
- **Guards:** `Unnamed.Foundation.Movement` and `Unnamed.Input` pass unchanged. The Sonnet pre-commit review found no bugs and confirmed Survival is unchanged (`bFlying` stays false without `bMayFly`, and every edited expression reduces to the old one); its one applied point is the mobility case.
- **`RunChecks -Profile final -Flight smoke`:** green in 19 min (`Tests/Evidence/2026-10-11/Checks-final-20261011-114609/summary.txt`).
  - Native: 261 of 261.
  - TestVoxel: 167.
  - Hashes: 9 of 9.
  - Playground all / interaction / tour: 119 / 81 / 27 checks, 0 failures.
  - create-world: 32 checks.
  - Flight smoke: pass.
  - npm: 61.
  - source-check and verifiers: pass.
