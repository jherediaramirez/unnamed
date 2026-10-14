# The player body: Better Steve in F5, and a spring-arm preview (2026-10-13)

Lane `feature/player-body` (worktree `C:\dev\projects\Unnamed-body`) on `0f31c09`. The brief was
`C:\dev\projects\better-steve\docs\HOOKUP_HANDOFF.md`; the scratch scripts (build, editor runs, contact sheets) are in
`C:\dev\sandbox\player-body`. Design: [Foundation architecture](../Foundation/Architecture.md#player-body-and-the-third-person-views).

## What changed

- **The body.** Better Steve stands, walks (the jog), sprints, crouches, crouch-walks, jumps (start, air loop, land)
  and turns his head with the mouse in F5's back and front views; first person is unchanged (the hand and held item),
  with the body hidden and not posed.
- **Assets.** `Art/Characters/BetterSteve/SK_BetterSteve.fbx` (the better-steve export, hash in `manifest.json`;
  `*.fbx` is now LFS-tracked) and `Scripts/Editor/ImportBetterSteve.py`, which imports it into the new
  `UUnnamedProjectSettings::PlayerBodyRoot` (`/Game/Characters/BetterSteve`, Python `ProjectPaths.player_body_asset`):
  the mesh, skeleton, physics asset, material, `T_BetterSteve` (Nearest, no mips) and the 43 actions renamed to their
  own names (`BS_Walk_F`, not `SK_BetterSteveArmature_BS_Walk_F`), the 9 look poses mesh-space additive on `BS_Look_C`
  frame 0. Like the other import scripts it verifies an existing folder (FBX hash tag, clips, additive settings,
  texture, no redirectors) and never replaces it.
- **No Animation Blueprint.** `FoundationBody.h` is pure and tested: `FFoundationBodyTurn` (the body's yaw at 20 Hz)
  and `FFoundationBodyAnimator` (per frame: which clips, at what time and weight). `UFoundationBodyAnimInstance`
  samples them in its proxy and adds the head look as mesh-space additive rotations.
- **Spring-arm preview.** `foundation.View.SpringArm 1` turns F5's back view into an over-the-shoulder camera: 325 cm
  back, 55 cm right, 30 cm up, a lagging pivot and rotation, pulled in by the world's own boxes (`Clip`), not UE
  physics.
- With the body showing, the cyan hitbox outline in the third-person views is drawn only with the collision boxes.
- **`hand_r_pickaxe` socket** on `SK_BetterSteve_Skeleton`, for the actions step's stand-in pickaxe: added by Jesus in
  the editor (UE 5.8's Python can't create sockets), with the transform he then set by hand: bone `hand_r`, location
  (0, -7.25, 2.70) cm, rotation X 0, Y 90, Z -90 (the better-steve README's values). Checked headless: the mesh sees it.
  A scripted set of the transform was refused by the auto-mode classifier (overwriting the saved asset), so it stayed
  manual.

| Switch | Values | Default |
|---|---|---|
| `foundation.Body` | 0 no body (the hitbox outline as before), 1 the body in the back and front views | 1 |
| `foundation.Body.Yaw` | 0 the view's yaw (strafe and backward clips), 1 vanilla (turns toward travel, walking backward keeps facing forward, the head at most 50 degrees off), 2 the travel direction, backward too (orient-to-movement) | 1 |
| `foundation.View.SpringArm` | 0 vanilla's back view, 1 the over-the-shoulder spring arm | 0 |

## For Jesus to pick (taste calls)

- **Where the body faces** (`foundation.Body.Yaw`): 1 vanilla is the default; the shots `back-strafe-yaw0` and
  `back-strafe-yaw1` compare it with 0 while strafing right, `arm-back-yaw1` and `arm-back-yaw2` show walking toward the
  spring-arm camera (vanilla runs backward, travel turns around and faces the camera).
- **The spring arm's shape** (325 / 55 / 30 cm, lags 12/s and 15/s): from the brief's ranges, untuned. With the game's
  FOV the body is small at 325 cm; a third-person game would usually sit closer and draw a crosshair.

## Verification

All on the editor build of this worktree (the review fixes and the socket included), with other lanes' benches and
flights waited out before each UE run.

- `ImportBetterSteve.py`: imported 48 assets (187.5 cm, 56 bones, 43 AnimSequences); later runs verified them
  (including the look poses' base and, after the socket, the edited skeleton) and imported nothing.
- `Scripts\RunChecks.ps1 -Profile final -Flight smoke`, on one DLL (built with `-DisableAdaptiveUnity`):
  - run `Checks-final-20261013-215653`: build, native 321 of 321 declared (the 6 new `Unnamed.Foundation.Body.*`
    included), TestVoxel 207 suites, radius-8 hashes 9 of 9 equal, runtime all 128 / interaction 83 / tour 27 checks
    with 0 failures; create-world failed in 1 s: the new worktree had no `Tests/node_modules` (`npm ci` fixed it).
  - run `Checks-final-20261013-220839` (the steps above skipped): create-world, the smoke flight (no errors besides
    LogPython), npm test, source-check, the Reference verifiers: GREEN.
  - A first run, `Checks-final-20261013-215551`, failed to build: the full unity build put `FoundationSimulation.cpp`'s
    anonymous `Epsilon` before an engine header's `Epsilon` parameter (C4459); renamed `SimulationEpsilon`.
- `-Suite body -FullCostRendering`: 53 checks, 0 failures, 30 shots
  (`Tests/Evidence/2026-10-13/Playground-body/Runtime-20261013-222641`): hidden in first person; shown and on the
  expected clip in every back, front and spring-arm pose (`BS_Walk_F 1.00`, `BS_Sprint_F 1.00`, `BS_CrouchIdle`,
  `BS_CrouchWalk_F`, `BS_Jump_Start` 0.3 s into the jump, `BS_Look_U` / `BS_Look_D` 0.67 at pitch +-60); strafing
  right plays `BS_Walk_R` under yaw 0 and mostly `BS_Walk_FR` with the head 50 degrees left under yaw 1.
- Planted feet: walking at 16.7-30 ms frames, a flat foot's ball stays within **0.18 cm** over 6 plants (limit 1 cm;
  `runtime-60-body-feet.csv`). The probe samples after the body's tick with the evaluation on the game thread, so each
  sample is one drawn frame; paired with the previous frame's position the same data would drift 5.52 cm.
- Spring arm: 330.98 cm from the eyes in the open (the 325 / 55 / 30 cm shape exactly); looking up 80 degrees it is
  pulled in by the ground to z 5.0 cm, 175.7 cm from the eyes.
- The `hand_r_pickaxe` socket read back headless: bone `hand_r`, location (0, -7.25, 2.70), rotation roll 0, pitch 90,
  yaw -90, scale 1.

## Found on the way

- **A frame of lag between pose and position.** The first animator built the frame's pose before advancing its gait
  clocks, so the drawn pose was one frame behind the drawn position. At steady frame times that is a constant offset,
  but every change of frame time moved a planted foot: the probe measured 5.86 cm of drift (0.27 cm when it paired
  each pose with the previous frame's position). The clocks now move first; a unit test pins it.
- **The ball of the foot is not the contact point at toe-off.** A 1 cm "planted" band around the ball's lowest height
  read 2.5 cm of slide that was the ball joint rolling forward over the planted toes; the probe counts the flat foot
  (0.5 cm) only.
- The importer names an action `<mesh><armature>_<action>`; the script renames them and checks no redirectors remain.
- **Pre-commit review** (Sonnet 5.5, diff only), all fixed:
  - A crouch toggled back mid-transition restarted the other clip at its far end. Each toggle now starts its clip where
    the eyes are (the clips are eased like the eyes, half the way per 50 ms).
  - The body kept its jump state and yaw through a respawn, lab reset or fall-out, so a landing played at spawn.
    `AFoundationPawn::ResetBody` now runs at each of those, and the probe uses it.
  - A jump straight out of a landing dropped the Land layer (a one-frame pop). Land fades on its own clock now.
  - `ImportBetterSteve.py`: an incomplete earlier import (no hash tag) gets its own message, and verify now checks the
    look poses' base (`BS_Look_C` frame 0).
  - The probe restores the parallel animation switches' previous values instead of setting them to 1.
  - Unit tests now cover the crouch toggle-back and the jump out of a landing.

## Open

- **A newer export is waiting.** The game has the 17:44 export (`bf666245...`, the 43 locomotion-chat actions). The
  better-steve actions chat re-exported at 20:30 (`053b6f85...`, 19.5 MB) with mining, placing and swimming clips and a
  stand-in pickaxe (`SM_StandInPickaxe.fbx`). Take it with the actions step: copy it over, update `manifest.json`, add
  the new action names to `ImportBetterSteve.py`, move `Content/Characters/BetterSteve` away, rerun the script, then
  rerun `-Suite body` (its locomotion may have changed: that chat baked IK to FK). Moving the folder away also drops
  the `hand_r_pickaxe` socket: add it again by hand, or keep the skeleton and re-import only the animations.
- **Unverified (recalled):** the vanilla body-turn constants (0.3 of the way per tick, the 50-degree head limit, the
  95-degree backward test, the 0.05-block threshold) are from `LivingEntity`'s `tick` and `tickHeadTurn` as
  remembered. No wiki or published source gives them (a 2026-10-13 search found only the mob-only
  `BodyRotationControl` in mapping indexes, without values), and they are not checked against the 26.3 jar.
- No clips yet for mining, placing, swimming or flying: swimming and flying play the air loop. The better-steve actions
  chat (`docs/ACTIONS_HANDOFF.md` there) authors them; wire them in `FoundationBodyClip` and the animator.
- No death pose (the body stands while the death screen shows), no held item in the body's hand, the backpack bone is
  static (no physics asset tuning).
- Third person as a game: targeting and the crosshair stay first-person (see the architecture section); out of scope.

## Handoff

Start here for the next body step (actions clips, or the spring arm if Jesus wants third person). Verify with
`Scripts\TestFoundationRuntime.ps1 -Editor -Suite body -FrameRates 60 -FullCostRendering` (shots and checks), the
native `Unnamed.Foundation.Body.*` tests, then the usual Playground probe and native suites.
