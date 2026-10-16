# Better Steve: locomotion animation handoff

> **Status 2026-10-13: done.** Jesus chose (a) speed-correct (walk = easy jog) and sprint in all 8 directions. The set
> is in `scripts/animate_locomotion.py` (30 actions; see README "Locomotion"), checked by `scripts/check_locomotion.py`
> and in UE 5.8 by `C:\dev\sandbox\better-steve-ue-check\run_locomotion_check.ps1` (`ue_locomotion_check.py` writes
> `locomotion.json`). Body fix on the way: the boot's skin weights were front/back reversed (heel on `ball_l`, toe tip
> on `foot_l`), invisible until a clip bent the toes; `foot_w` in `build_better_steve.py` is fixed. Not done: a
> moving-jump variant, Blend Space / AnimBP assets in Unnamed (the game has no body mesh or AnimBP yet: its pawn is a
> box collider + camera, with custom 20 Hz movement rather than Character Movement).
> Same chat, later: 9 look poses (`BS_Look_*`, Aim Offset) added and verified in UE (additive + test Aim Offset).
> Next chats: `docs/ACTIONS_HANDOFF.md` (mining swing with a stand-in pickaxe, placing, swimming) and
> `docs/HOOKUP_HANDOFF.md` (body in Unnamed's F5 views + a spring-arm third-person preview).

Written 2026-10-13 at the end of the character build chat. Read this first, then `README.md`, then the files listed
under "Read before starting".

## Goal of the next chat

Author the locomotion set for Better Steve (adventurer style) in Blender, export it, and prove it in Unreal 5.8:

- **Standing:** idle (one exists), walk/jog in 8 directions (F, FL, L, BL, B, BR, R, FR, so backwards and every
  strafe), sprint (F, FL, FR: Minecraft only sprints forward).
- **Jumping:** jump start (takeoff), in-air loop (rise, apex, fall), land; ideally a moving-jump variant too.
- **Crouching:** crouch idle, crouch walk in 8 directions, stand-to-crouch and crouch-to-stand transitions.
- Everything loops cleanly, keeps the feet from sliding at the game's speeds, and imports into UE as AnimSequences
  ready for Blend Spaces (direction x speed) and a jump state machine.

## Current state (all verified this chat)

- Character: `C:\dev\projects\better-steve` (not a git repo yet). One generator script rebuilds everything.
- Height 1.875 blocks = vanilla Steve (32 px x 0.9375). Eyes at 1.62. 1 Blender m = 1 block = 100 UE units.
- Faces -Y in Blender and +Y in Unreal (like the UE5 Mannequin). The character's left is +X.
- 56 deform bones, Mannequin-style names, 11 Blender-only control bones. Unreal 5.8 headless import:
  187.5 cm tall, 65.2 cm wide, every bone at scale 1.0, FBX and GLB identical.
- Existing actions: `BS_Idle` (90 f, loops), `BS_Walk` (32 f, loops, in place, slow placeholder stride:
  feet travel +/-0.22 m, about 0.8 m/s, far below game speed), `BS_FaceTest`, `BS_FlexTest`. All at 30 fps.
- Previews: `previews/00_hero.png` ... `06_skeleton.png` (hero, turnaround, vs vanilla, face rig, poses, joints, skeleton).

## Files

| Path | What |
|---|---|
| `scripts/build_better_steve.py` | Builds mesh, atlas, rig, controls, the 4 actions, saves the .blend, exports. **Running it overwrites `foundry/BetterSteve.blend`**: anything hand-made in the .blend is lost. Importable (`main()` only runs as a script). |
| `scripts/export_ue.py` | `export_all(export_dir)`: GLB (metres) then FBX (centimetres) with **every action that keys pose bones**. Use it after adding actions. |
| `scripts/render_previews.py` | EEVEE preview sheets. `pose(action, frame)` + `shoot(...)` + `sheet(...)` are the pattern to copy for animation strips. Optional 2nd arg picks sheets: `hero,turnaround,compare,face,poses,hands,rig`. |
| `scripts/verify_export.py` | Re-imports FBX + GLB into Blender: bone count, height, action list. |
| `foundry/BetterSteve.blend` | Rig + mesh + actions, metres. |
| `export/SK_BetterSteve.fbx`, `.glb`, `T_BetterSteve.png` | Engine files. |
| `C:\dev\sandbox\better-steve-ue-check\` | Scratch UE project: `import_check.py` (imports FBX + GLB, writes `result.json`), `anim_check.py` (samples bone world positions in the imported walk, writes `anim.json`). |
| Blender | `C:\dev\tools\blender-5.2.2\blender.exe` (5.2.2). |
| Unreal | `C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe` |

Read before starting: `README.md`, `scripts/build_better_steve.py` sections "skeleton", "animation helpers"
(`apply_pose`, `bake_action`, `mirror_side`, `face`, `walk_pose`, `idle_pose`, `flex_pose`), and `scripts/export_ue.py`.

## Recommended pipeline

1. Leave the build script alone unless the body changes. Write `scripts/animate_locomotion.py`:
   open `foundry/BetterSteve.blend`, `import build_better_steve as bs` (add `scripts/` to `sys.path`; pass
   `-- <project root>` so its argument parsing works), add the new actions with `bs.bake_action(arm, name, frames,
   pose_fn)`, save the .blend, then `export_ue.export_all(<root>/export)`.
   Run: `blender -b foundry/BetterSteve.blend --python scripts/animate_locomotion.py -- <root>`
2. Full rebuild order if the body ever changes: `build_better_steve.py`, then `animate_locomotion.py`.
3. Generate gaits **procedurally and key every frame** (pose function of the frame). `bake_action` keys only the
   frames you pass and interpolates Bezier between them, which makes planted feet drift. Keying every frame (or
   setting LINEAR on the stance keys) avoids that.
4. Keep everything **in place** (root bone never moves): UE's Character Movement moves the capsule, and Blend Spaces
   expect in-place loops. If root motion is wanted later, animate `root` only, never `pelvis`, for travel.

## Rig: how to drive it

`bs.apply_pose(arm, spec)` resets every pose bone, then applies
`spec = {bone: {"loc": (dx, dy, dz) world metres, "rot": [(axis, degrees), ...]}}`. Rotations are about the
**rest-pose world axes**, applied in list order, relative to the parent (an arm rotation follows a rotated spine).
`bs.face(...)` returns the face spec (blink, smile, jaw, brows, lids, eye target); merge it in to keep the face alive.
`bs.mirror_side(left_spec)` makes the right side from the left (flips X, negates Y/Z rotations).

Signs (seen in this chat's renders unless marked "derived"):

| Bone | Input | Meaning |
|---|---|---|
| `CTRL_foot_ik_l/r` | loc | Moves the foot (IK on by default). Planted foot = ground contact. Knees aim at `CTRL_knee_pole_l/r`. |
| `CTRL_foot_ik_l/r` | rot X | + toes down (toe-off), - toes up (heel strike). Rotates about the ankle: lift the control so the heel/toe doesn't sink (heel strike 12 deg needs +0.014 m, toe-off 22 deg needs +0.06 m). |
| `pelvis` | loc | World offset of the whole upper body. z < 0 = crouch (feet stay planted by IK). |
| `pelvis`, `spine_02`, `spine_03` | rot Z | + turns toward the character's left (derived). |
| `spine_01..03` | rot X | + leans forward. |
| `head`, `neck_01` | rot X | + nods down, - looks up. |
| `upperarm_l/r` | rot X | + swings the arm back, - forward/up (-75 reach forward, -160 overhead). |
| `upperarm_l` / `upperarm_r` | rot Y | left - / right + = arm out to the side (-85 = T-pose). |
| `lowerarm_l/r` | rot X | - bends the elbow (hand forward/up). |
| `hand_l/r` | rot X / Y | wrist flex. |
| `index..pinky_01/02_l` / `_r` | rot Y | curl: left +, right - (fist about 85-95 deg per joint). |
| `thumb_01/02_l` / `_r` | rot Y | curl: left +, right -. |
| `backpack` | any | Rides `spine_03`. Small lagged bounce in loops, or leave it and use AnimDynamics in UE. |

Switches on the `root` pose bone (0..1): `leg_ik_l/r` (1), `arm_ik_l/r` (0), `eyes_track` (1). Arm IK targets:
`CTRL_hand_ik_l/r` + `CTRL_elbow_pole_l/r`. Pose bones use XYZ Euler. The FBX bake evaluates IK, so exported
deform bones get the solved FK result.

Rest geometry (metres): hip joint z 0.90, knee 0.48, ankle 0.095, ball (toe joint) y -0.11, toe tip y -0.195,
leg x +/-0.085. Shoulder 1.345 (x +/-0.258), elbow 1.07, wrist 0.815. Pelvis bone 0.90, head bone base 1.48,
eye centre 1.62.

## Game numbers to match (Unnamed = vanilla Minecraft movement)

From `C:\dev\projects\Unnamed\Source\Unnamed\Foundation\FoundationRuntimeProbe.cpp`:

| Thing | Value |
|---|---|
| Walk | 4.3171806 blocks/s (431.7 cm/s) |
| Sprint | 5.6123348 blocks/s |
| Crouch (sneak) walk | 1.2951542 blocks/s |
| Jump apex | 1.2522 blocks; airtime 12 ticks = 0.6 s on flat ground |
| Crouch (vanilla) | hitbox 1.5 tall, eye height 1.27 (check the Unnamed source before relying on these) |

**Decision for the user:** for a 1.875 m body, 4.3 m/s is a jog, not a walk (a walking step is about 0.78 m, so
4.3 m/s would need about 5.5 steps/s). Options:
(a) speed-correct: "walk" is an easy jog with a flight phase (about 2.7 steps/s, about 1.6 m per step), sprint
is a run (about 3 steps/s, about 1.9 m per step), crouch walk is a normal-cadence crouched walk (about 1.8
steps/s, about 0.72 m per step);
(b) stylised Minecraft walk (shorter stride, faster cadence) and accept some foot slide or fix it with UE
stride warping / play-rate.
Recommend (a); ask before committing.

No-slide rule for in-place loops: a planted foot moves toward +Y (backwards in Blender) at exactly the gait speed,
`speed / 30` metres per frame at 30 fps, for the whole contact phase. Cycle length = 2 steps.

Crouch target: drop the eyes from 1.62 to about 1.27 with pelvis down, knees bent, a forward spine lean and the
head levelled. The existing `BS_FlexTest` frame 20 is a deep squat (pelvis -0.32), not a sneak.

## UE side

- Import `export/SK_BetterSteve.fbx`. Animations arrive as `SK_BetterSteveArmature_<Action>`.
- **Re-import gotcha:** an existing `SK_BetterSteve_Skeleton` keeps its old reference pose. After any body/bone
  change, delete the old assets (or import into a fresh folder). The scratch check project must be cleared too:
  `Content\BetterSteve` and `Content\BetterSteveGLB`.
- Texture: Filter Nearest, NoMipmaps.
- Suggested assets: BS_Locomotion (2D Blend Space, Direction -180..180 x Speed 0..561), BS_CrouchLocomotion
  (Direction x 0..130), jump state machine (Start -> Loop -> Land), crouch transitions.
- Headless check: `UnrealEditor-Cmd <scratch>.uproject -run=pythonscript -script=<py> -unattended -nop4 -nosplash
  -NullRHI -abslog=... -UserDir=<scratch>\UserDir`, launched with PowerShell `Start-Process -PassThru` and
  `WaitForExit` (not from Bash). Write results to JSON; the log is noisy.
- **Before any UE run**, list other lanes' editors (`Get-CimInstance Win32_Process` command lines): if one has
  `-VoxelFlight` or a bench running, wait for it to exit (timed gates). Test suites are fine to run alongside.

## Verification checklist for every new action

1. Loop seam: frame 0 pose == last frame pose (compare bone matrices).
2. Foot slide: during contact, foot world y changes by speed/30 per frame (+/-2 mm); world z stays within 5 mm of
   its contact height.
3. Ground: lowest evaluated sole vertex >= -0.005 m on every frame (evaluate the mesh with the depsgraph).
4. Crouch: eye z is about 1.27 on crouch idle.
5. A small preview strip (6 frames, about 300 px tall tiles) per action. Image reads cost tokens; check numbers first.
6. Export, `verify_export.py`, then the UE scratch import + `anim_check.py` adapted to the new names.

## Known issues / gaps

- Clavicles carry no weights (they only move the arms); shoulders don't lift on shrugs.
- Bind pose is arms-down. Retargeting from the UE Mannequin needs a retarget pose; not tried yet.
- No physics asset tuning (UE generated one on import).
- Width 0.65 blocks: the arms poke slightly outside the 0.6 hitbox. Vanilla Steve is 0.94 wide.
- FBX take names carry an `Armature|` prefix; UE strips it into the `...Armature_<Action>` asset names.
- The Blender MCP server (`mcp__blender__*`) only works with Blender's GUI open and its add-on server started. This
  chat used headless scripts instead.
