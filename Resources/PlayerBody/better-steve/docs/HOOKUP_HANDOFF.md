# Better Steve: hookup handoff (body in Unnamed + spring-arm third-person preview)

Written 2026-10-13 at the end of the locomotion chat. Read this, then `README.md` (sections "Locomotion", "Look poses",
"Unreal import"), then Unnamed's `CLAUDE.md`, `Docs/Architecture.md`, `Docs/NamingConventions.md`, and the files
listed under "Unnamed facts".

## Goal

1. Give Unnamed's player a body: Better Steve, visible in the F5 views (back and front), driven by the game's own
   movement: idle, walk (jog), sprint, sneak, jump, crouch transitions, and head look from the mouse (Aim Offset).
   First person stays as it is (the held item / hand view), with the body hidden.
2. **Experiment:** a spring-arm style third-person camera (over the shoulder, smoothed, pulled in by walls), so Jesus
   can see what Unnamed would feel like as a third-person game. Moving the game from first to third person is a
   back-burner idea, so this is a **preview behind a switch**; vanilla F5 stays the default.

Not in scope: mining/placing/swimming clips (a parallel chat, `docs/ACTIONS_HANDOFF.md`, authors them in
`better-steve`; wire them later), multiplayer, physics asset tuning.

## The assets (ready, verified 2026-10-13)

`C:\dev\projects\better-steve\export\SK_BetterSteve.fbx` (+ `T_BetterSteve.png` embedded): 1.875 blocks tall, 56
bones (UE Mannequin names), faces +Y, 43 AnimSequences. Import notes, Blend Space sample placement, the jump state
machine and the Aim Offset setup are in the better-steve README. Proven in the scratch project
`C:\dev\sandbox\better-steve-ue-check` (`ue_locomotion_check.py` has working Python for the import, making the look
poses mesh-space additive and building an Aim Offset with its 9 samples: reuse it).

| Game state | Clips | Speed cm/s (straight / diagonal) |
|---|---|---|
| idle | `BS_Idle` | 0 |
| walk | `BS_Walk_<dir>` (F FR R BR B BL L FL = 0 45 90 135 180 -135 -90 -45) | 431.7 / 440.5 |
| sprint | `BS_Sprint_<dir>` (the game only sprints F/FL/FR) | 561.2 / 572.7 |
| sneak | `BS_CrouchIdle`, `BS_CrouchWalk_<dir>`, `BS_StandToCrouch`, `BS_CrouchToStand` | 129.5 / 183.2 |
| jump | `BS_Jump_Start` (0.3 s = the game's rise), `BS_Jump_Loop` (any fall), `BS_Jump_Land` | |
| look | `BS_Look_C U D L R UL UR DL DR` (Aim Offset, yaw/pitch -90..90) | |

All in place (root never moves); a planted foot moves at exactly the game speed, so with the capsule moving at that
speed the feet stick. The F5 camera is 4 blocks out, so the jog/sprint cadence is what the player will judge.

## Unnamed facts (checked 2026-10-13)

- `AFoundationPawn` (`Source/Unnamed/Foundation/FoundationPawn.h/.cpp`) is a plain `APawn`: a `UBoxComponent`
  `Collision` and a `UCameraComponent` `Camera`. **No mesh, no AnimBP, no Character Movement.** Movement is
  `FFoundationSimulation` (`FoundationSimulation.h/.cpp`): vanilla rules at 20 Hz, rendered between the last two ticks
  with `Alpha = Accumulator / TickSeconds` (`FoundationPawn.cpp` around line 364).
- What the simulation exposes: `Feet`, `PreviousFeet`, `VelocityPerTick` (cm per 50 ms tick), `LastDisplacement`,
  `bGrounded`, `bCrouched`, `bSprinting`, `bFlying`, `bJumped`, `bSprintJumped`, `HeightCm`, `EyeCm`,
  `PreviousEyeCm`. Profile numbers: `FoundationProfile.h` (eyes 162 / 127 cm, hitbox 180 / 150 cm).
- F5: `Perspective` 0 first person, 1 back, 2 front; the camera goes 400 cm along the view and is pulled in with
  `FFoundationSimulation::Clip` against `Lab->GatherCollision(...)`. The world's collision is the game's own boxes
  (voxels), **not** UE physics, so a stock `USpringArmComponent` probe would miss the terrain: build the spring arm
  in the pawn's camera code with the same `Clip`.
- First-person hand/held item: `FoundationInteractionView` (`HandPivot`, `foundation.Hand.Fit`). Keep it for
  first person only.
- Asset paths live only in `UUnnamedProjectSettings` (`Source/Unnamed/UnnamedProjectSettings.h`; Python
  `Scripts/Editor/ProjectPaths.py`). Follow `Docs/NamingConventions.md` for the folder, class prefixes and any new
  console variables (e.g. `foundation.Body`, `foundation.View.SpringArm`).
- The repo runs several lanes in worktrees (`git worktree list`); work in your own worktree/branch (for example
  `feature/player-body`), never in another lane's. Commits run the credential scan hook. Records go in
  `Docs/Records/` with an index line in `Docs/Records/README.md`.

## Suggested design (decide in the chat; ask Jesus where it's a taste call)

1. **Import** into the Unnamed content folder the naming doc implies (e.g. `/Game/Characters/BetterSteve`), via an
   editor Python script under `Scripts/Editor/` that verifies and never silently replaces (the project's pattern, see
   `ImportItems.py`). Texture: Nearest, NoMipmaps. Look poses: mesh-space additive on `BS_Look_C`.
2. **Body component**: a `USkeletalMeshComponent` on the pawn at the interpolated feet,
   `Lerp(PreviousFeet, Feet, Alpha)`, hidden when `Perspective == 0`.
3. **Body yaw** (taste call, show Jesus both if cheap): (a) always the view yaw (strafing via the 8-direction clips),
   or (b) vanilla-like: the head turns first and the body catches up past a limit, and turns toward the movement
   direction while walking. (b) looks more alive in F5 front view.
4. **Animation logic**: the anim graph must exist somewhere. AnimBP graphs are hard to author headless. Options:
   - a C++ `UAnimInstance` + custom `FAnimInstanceProxy` that samples the blend spaces/sequences in code (fully
     headless, testable, fits the project's code-first style; **recommended**);
   - an AnimBP built in the editor (by Jesus, or through the editor MCP toolsets the project enables, if they can
     edit anim graphs: check first).
   Inputs: Speed = horizontal speed from the interpolated velocity (cm/s), Direction = velocity angle relative to
   the body yaw (+ right), `bCrouched`, `bGrounded`, `bJumped`, `bSprinting`, LookYaw/LookPitch = control rotation
   minus body yaw (pitch clamped to +-90). States: locomotion Blend Space (stand / crouch), crouch transitions, jump
   Start -> Loop -> Land, Aim Offset on top. Flying and swimming have no clips yet: use `BS_Jump_Loop` or idle.
5. **Spring arm (preview)**: a switch (e.g. `foundation.View.SpringArm 1`, or a 4th F5 state behind it): arm about
   300-350 cm, shoulder offset (about 50-60 cm right, 30-40 cm up), position lag and rotation lag smoothing,
   collision pull-in with `Clip`, and a choice of facing: strafe (body faces the camera) or orient-to-movement
   (body turns toward travel; needs only the F clips). The first-person crosshair/targeting stays from the eyes;
   note where third person changes block targeting (out of scope to fix, worth a note).

## Verify

- Build, then the Playground: `Scripts\TestFoundationRuntime.ps1 -Editor -FrameRates 60` (the full probe must stay
  green), and screenshots of F5 back/front and the spring arm while idle, walking, sprinting, sneaking, jumping and
  looking up/down (`-Suite tour -FullCostRendering` captures screenshots; add a body/third-person stop if needed).
  `SendUserFile` may fail in this project; open the folder with `Invoke-Item` for Jesus instead.
- Feet: in a capture at walk speed the planted foot should stay on one spot of the dev grid across frames.
- Native suites: `Automation RunTests Unnamed` must stay green.
- Before UE runs, list other lanes' editors and wait out any `-VoxelFlight` run or bench.

## Gotchas

- The body is 187.5 cm, the hitbox 180 (the same as vanilla: the model is taller than its box). The crouch pose puts
  the eyes at 127 cm exactly, the game's crouching eyes.
- Re-importing into an existing Skeleton keeps its old reference pose: if the better-steve actions chat re-exports,
  re-import the animations only (the skeleton hasn't changed), or clear the folder.
- Inline PowerShell here-strings with "/" paths near Remove-Item get blocked; write scripts to files. Git Bash `sed`
  mangles Windows paths (`\u`, `\b`); use the Edit tool.

## Definition of done

Body visible and animated in F5 (back and front) with planted feet and head look; the spring-arm preview behind its
switch; screenshots shown to Jesus; the probe and native suites green; a record + handoff in `Docs/Records`; the
better-steve README pointed at where the game uses him. Commit on your own lane when Jesus asks.
