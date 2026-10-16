# The full-body player, M1: the Unnamed Player in both views, and the strike in its hand (2026-10-16)

M1a on branch `feature/fp-body-m1a`, M1b on `feature/fp-body-m1b`, each merged into `main`. The brief was `Resources/PlayerBody/FP-BODY-M1-BRIEF.md`; the plan is
`Resources/PlayerBody/unnamed-fp-tp-integration-plan-20261015/PLAN.md` (sections 1, 3, 5, 7). Design:
[Foundation architecture](../Foundation/Architecture.md#player-body-and-the-third-person-views).

## What changed

- **Assets.** `Art/Characters/UnnamedPlayer/` is the engine's import copy of the accepted export v002c (every file
  checked against its manifest, then hashed into `manifest.json`), with M0's oracle files in `gates/`.
  `Scripts/Editor/ImportUnnamedPlayer.py` repeats M0's proven Interchange procedure into
  `UUnnamedProjectSettings::UnnamedPlayerRoot` (`/Game/Characters/UnnamedPlayer`, Python
  `ProjectPaths.unnamed_player_asset`). It verifies an existing folder and never replaces it, measures PLAN's gates 1-8
  on every run (`UnnamedPlayerGates.py`) and writes `Saved/ImportUnnamedPlayer/report-*.json`.
- **Credit.** "Minecraft Steve (Rigged)" by Bully Magraptor, CC BY 4.0. The game has no credits screen of its own, so
  Jesus chose a repository file: `CREDITS.md`, staged beside packaged builds (`Unnamed.Build.cs`).
- **The body.** `foundation.Body.Set` 1 (default) is the Unnamed Player in every view; 0 is Better Steve, unchanged.
  `foundation.Body.FirstPerson` 1 (default) adds the owner's first-person copy:
  - no head;
  - legs and feet in view looking down;
  - the world body still casts the owner's shadow, head included.

  The corrections are in `FoundationBodyPose.*` (pure, unit-tested); the pawn's body code moved to
  `FoundationPawnBody.cpp`. The clip table gained the crouching look grid and the swim pose.

| Switch | Values | Default |
|---|---|---|
| `foundation.Body.Set` | 0 Better Steve (third person only), 1 the Unnamed Player | 1 |
| `foundation.Body.FirstPerson` | 0 no body in first person, 1 the first-person copy | 1 |
| `foundation.Body.FP.StartPitch` / `EndPitch` | where the looking-down corrections blend in (degrees) | -20 / -75 |
| `foundation.Body.FP.PelvisBack` / `PelvisBackCrouch` | hips back at full blend (cm) | 10 / 22 |
| `foundation.Body.FP.Anchor` | neck behind the eye looking down, standing (cm; 4 looking ahead) | 16 |
| `foundation.Body.FP.AnchorCrouchMin` / `AnchorCrouch` | neck behind the eye, crouched, looking ahead / down (cm) | 18 / 30 |
| `foundation.Body.FP.CrouchDrop` | hips lower when crouched (cm) | 14 |
| `foundation.Body.FP.CrouchTwist` | chest turned toward the view when crouched (0..1) | 1 |

## The crouch stop condition, and Jesus's choice

The first measurement with PLAN 3.4's proposed values hit PLAN 7's stop condition. Crouched, the copy came within
0.09-0.13 cm of the eye:
- the near upper arm, with the view 60 degrees off the body;
- 7.5 cm (the torso top) looking straight ahead.

Standing (20.6 cm) and swimming (17.4 cm) passed. Jesus asked for a stronger crouch correction in the copy only.
Two new crouch-only controls (drop, chest twist) and a sweep of 24 settings (`-FoundationBodySweep`, run
`Runtime-20261016-182034`) showed the twist is what matters: without it the arm reaches the eye at any drop. Jesus
picked setting B (drop 14, anchor 18-30, back 22, twist 1): 23.1 cm.

## Verification

- **Import:** 72 assets with M0's numbers. 22 bones; 180.0000 cm; joints within 0.0000435 cm; sockets within
  0.000007 cm. 66/66 clips pass timing and compression: ACL within 0.0 cm / 4.8e-6 degrees; compressed planted drift
  within 0.080 cm; StrikeA/B/C tip at frame 9 0.0003/0.0003/0.0020 cm. The 20 pose clips and 18 additive looks match
  the source within 0.0000784 cm / 0.0000852 degrees; the pickaxe tip lies on its surface (7.2e-6 cm). Log scan: 69
  imports, one error, the known embedded texture (`T_UP_Skin` replaces it). A second run imported nothing, files
  byte-identical (`Tests/Evidence/2026-10-16/UnnamedPlayer-import-20261016-174726`).
- **Native:** `Unnamed.Foundation.Body.*` 10 tests, 4 new: Sets, Low, FirstPerson, HeadSection (the `M_UP_Head`
  section is exactly the 140-180 cm head cube). Full `Automation RunTests Unnamed` on the final code: 589 of 589 (585 at the start).
  `Unnamed.Voxel.Pipeline.ShiftPlan` and `.Stages` failed in one full run under load (K-BUILD-3) and passed alone.
- **Body suite** with setting B, `-FullCostRendering`: 79 of 79 at 30, 60 and 120 fps
  (`Tests/Evidence/2026-10-16/Playground-body/Runtime-M1a-B3-190140-{30,60,120}`). M1a's table:

| Check | Target | Measured (30 / 60 / 120 fps) |
|---|---|---|
| F5 parity | expected clips on the UP | all back, front and spring-arm poses green |
| Planted drift | <= 0.5 cm target, 1 cm limit | back view 0.01/0.03/0.08; first person 0.010/0.013/0.197; jittered 16.7-33 ms: first person 0.087/0.113/0.094, back 0.087/0.094/0.575 cm |
| Head in first person | 0 head pixels | 0 in all 14 shots per rate (stand and crouch, pitch -89.9..89.9); the front-view control shows 691/702/711 |
| Feet in first person | toe caps visible at pitch <= -75, vFOV 70 and 110 | 17-20 toe-cap points per foot |
| Clearance | >= 15 cm | stand 20.5, crouch 23.0, swim 17.4 cm (minimum over 21 views each) |
| Shadow | one shadow, head included, low sun | one long shadow, head at its end (`runtime-60-body-fp-shadow-low-sun.png`, by eye) |
| View switching | 20 F5 cycles mid-jump | 0 bad frames; the least-posed frame 11-34 cm from the reference pose |
| Simulation unchanged | bit-identical, scripted 30 s | 600 steps equal with either set |
| Perf | <= 0.10 ms per body; <= 0.3 ms frame | 0.058-0.059 ms per body; median frame +0.06/+0.01/+0.02 ms; game thread +0.31/+0.31/+0.27 ms |

## Found on the way

- **The probe's old spot is now the registry gallery.** `(-30, 30)` m sat inside it, so idle played the crouch and
  walking was blocked; the body probes now use `(200, 200)` and `(200, 220)` m.
- **A probe shot sees the next case's Start.** A shot is captured at the end of its frame, after the next case starts.
  The head control and the low-sun shot showed already restored materials and sun until flush cases went in. The first
  head-pixel results meant nothing (the control's head was not magenta).
- **Not verified:** packaged builds. `/Game/Characters` is not in `DirectoriesToAlwaysCook` and the bodies load by soft
  path, so a package may have no body (Better Steve's too); not checked with a package.

## For Jesus to judge (taste)

- Standing and looking straight down, the torso's top fills the lower part of the view and the legs show between the
  shoulders (`fp-stand-pitch-89`). The anchor and hips-back values are PLAN's proposal, untuned.
- Crouched, the crouch idle holds the arms forward: two big hands frame the knees looking down.
- Swimming, the low pose fills much of the view.
- Held items still draw at the camera (LEGACY-HAND): looking down you see the body's arm and the floating item.

## M1b: the pickaxe in the real hand

- **What changed.** The pickaxes are body-held (`FoundationBodyGrip::IsBodyHeld`).
  - `SM_UP_Pickaxe` hangs on `hand_r_grip` of both bodies with an identity transform (its origin and axes are the
    grip's).
  - It is drawn with flat colours on the dev grid material: a wood handle, the head tinted by tier (provisional; the
    tool has no texture).
  - The world body's tool casts the owner's shadow in first person; the copy's tool is owner-only.
  - The view hides its camera pickaxe while the owner sees the body's (`AFoundationPawn::IsMainItemInBody`). Other
    items stay on the camera (LEGACY-HAND).
- **The strike.** `FFoundationBodyAction` runs StrikeA on the view's swing clock:
  - A press starts it at the interaction's step. Time = (step + alpha - start) / 6 x 0.3 s, with the pawn's alpha, so
    frame 9 (the contact) is drawn as step start + 3 begins, the tick the hit lands.
  - It repeats while mining, and a new press restarts it with a 0.05 s crossfade. It fades back to `UP_Pickaxe_Hold`
    over 0.1 s; the hold fades with the item over 0.08 s.
  - The proxy blends it over the gait with PLAN 5's mask: spine_01 half and spine_02 whole in component space; neck,
    head, clavicles, arms and hands whole; pelvis and legs keep the gait.
  - The head look leaves the spine during actions. The chest aims at the view (pitch -45..35, yaw +-30; proposed). The
    first-person corrections run after it.
- **Verification.** Native `Unnamed.Foundation.Body.Strike` and `.UpperBody`; the full native suite 591 of 591. Body suite 95 of 95 at 30, 60 and
  120 fps (`Tests/Evidence/2026-10-16/Playground-body/Runtime-M1b2-193830-{30,60,120}`):

| Check (PLAN 7) | Target | Measured (30 / 60 / 120 fps) |
|---|---|---|
| Socket read back | <= 0.05 cm | 0.00000 cm, 0.0000 deg on both bodies |
| Strike start | on the swing start step | the body's start step equals the view's swing clock's |
| Contact timing | frame 9 drawn in the frame containing step start + 3 (+-1 at 30 fps) | the same frame at every rate |
| No doubled pickaxe | in either view | exactly one drawn in the hold, in the back view and at frames 0-15 in both views |
| Arms vs the approved strike | <= 0.5 deg arm-local | <= 0.039 / 0.043 / 0.051 deg (frames 0-15, both bodies) |
| Mining unchanged | results and timings | the stone breaks in 8 steps with either set |
| Rollback | `Body.Set 0` = today's camera pickaxe | the camera's pickaxe, no body tool |

  - The tip at the contact frame lands 1.3-2.1 cm from the authored target: the gait's half share of spine_01 and the
    aim are blended in.
  - In first person the tool comes no closer than 21.9-22.7 cm to the eye.
  - Stills: `runtime-60-body-strike-{fp,back}-frame03..15.png` (paused at exact frames), `strike-contact.png`.
  - Not made: the plan's 60 fps and 1/3-speed MP4s. The stills at exact frames and the arm-angle check stand in for
    them.
- **For Jesus to judge.**
  - The tool's look: flat-coloured blocks, or the game's own item sprite.
  - The aim limits.
  - The wind-up (frame 6) brings the fist close past the camera in first person.

## Handoff

M2: the rest of the existing library — Rise/Firm landings, turns in place, the A-B-A-C strike chain, contact and
gesture, the other tools, offhand hold, empty-hand arm, the swim/flight/death placeholders (PLAN 7). The new P1 clips
(swim, tread, use, eat, death) are authored in `Resources/PlayerBody/unnamed-player-clips-v003-20261016/`
(uncommitted until Jesus reviews); importing them is M3's.