# better-steve

A slimmer, fully articulated, face-rigged take on Minecraft's Steve, built entirely from a Blender Python script
(Blender 5.2) and verified by importing into Unreal 5.8.

![hero](previews/00_hero.png)

## Styles

One body, skeleton and face rig, two outfits (`--style`, default `adventurer`):

- **adventurer**: the rugged explorer. Blocky beard and moustache framing the mouth (the lip rig still shows
  talk/smile/frown), a scar through the left brow, an open leather jacket over a muted Steve-teal shirt, fleece
  collar and lapels, rolled cuffs, leather bracers and fingerless gloves, belt with a brass buckle, khaki cargo
  pants, tall boots, and a canvas backpack (bedroll, pockets, an iron pickaxe across the back) on its own
  `backpack` bone. Bulkier: V-taper chest and thicker arms (0.65 blocks wide).
- **classic**: Steve's teal shirt, blue pants, grey shoes (0.61 blocks wide).

Textures are one atlas of noisy pixel tiles with per-material patterns (leather blotches and scuffs, canvas weave,
twill pants, fleece clumps, hair strands, wood grain).

![turnaround](previews/01_turnaround.png)

## What was wrong with Steve, and what changed

| Vanilla Steve | Better Steve |
|---|---|
| 6 rigid boxes; limbs can only swing at shoulder/hip | Continuous limbs with bend zones: elbows, knees, wrists, ankles, 3-bone spine, neck, 4 fingers + thumb (2 joints each), toes |
| Face is a painted texture: no blinking, no talking | Real geometry: recessed eyes with moving irises/pupils + glints, sliding upper/lower lids, brows, a nose, a mouth with teeth that opens, smiles and frowns |
| Boxy 16 px wide silhouette (0.94 blocks with arms) that pokes far outside the 0.6 block hitbox | Tapered torso (shoulders > waist < hips), slim limbs, 0.61-0.65 blocks wide |
| Head is 1/4 of the body | Smaller head (0.39 of 1.875), longer legs |
| Limbs end in flat stumps | Hands with fingers, shoes with soles and a toe joint |
| 1 texel = 5.9 cm | Same pixel look at 3x density (1 texel = 1.95 cm), one 256 px atlas, one material |

## Scale (exact)

- 1 Blender metre = 1 block = 100 Unreal units.
- Height **1.875 blocks** = vanilla Steve as rendered (32 px x the 0.9375 player scale). Unreal bounds: 187.5 cm.
- Eye centre at **1.62** = Minecraft's (and this repo's game's) eye height, so a first-person camera sits in his eyes.
- Faces -Y in Blender; imports facing **+Y in Unreal**, like the UE5 Mannequin. Feet on the origin.

## Files

| Path | What |
|---|---|
| `foundry/BetterSteve.blend` | Source: mesh, rig, controls, actions, vanilla reference (texture packed) |
| `export/SK_BetterSteve.fbx` | Unreal import (centimetres inside, texture embedded, 51 animations baked) |
| `export/SM_StandInPickaxe.fbx` | Stand-in iron pickaxe (static mesh, centimetres, pivot on the grip): see "Actions" |
| `export/SK_BetterSteve.glb` | Same as glTF (metres) |
| `export/T_BetterSteve.png` | The pixel atlas |
| `previews/*.png` | Turnaround, vanilla comparison, face rig, poses, joint close-ups, skeleton; `10_<family>.png` locomotion strips; `11_look.png` look poses; `12_actions.png` swings (side + three-quarter, every frame), place, hold; `13_swim.png` |
| `previews/locomotion_check.json` | Last `check_locomotion.py` result (the Unreal check reads it too) |
| `scripts/build_better_steve.py` | Builds everything from nothing (geometry, atlas, rig, actions), then exports |
| `scripts/animate_locomotion.py` | Adds the locomotion set and look poses to the .blend (walk, sprint, crouch, jump, look), saves, exports |
| `scripts/animate_actions.py` | Adds the mining, placing and swimming clips and the stand-in pickaxe, saves, exports (+ the pickaxe FBX) |
| `scripts/check_locomotion.py` | Loop seams, foot slide, ground contact, IK reach, crouch eye height on the solved rig; pickaxe tip, prop-in-body and swim-pull checks |
| `scripts/export_ue.py` | Exports FBX + GLB from the .blend (use this after editing by hand) |
| `scripts/render_previews.py`, `scripts/verify_export.py` | Preview sheets; FBX/GLB round-trip report |
| `docs/ANIMATION_HANDOFF.md` | Brief for the locomotion animation work (rig controls, game speeds, pipeline, checks) |

```powershell
$b = "C:\dev\tools\blender-5.2.2\blender.exe"; $r = "C:\dev\projects\better-steve"
& $b -b --factory-startup --python "$r\scripts\build_better_steve.py" -- $r     # rebuild + export (adventurer)
& $b -b --factory-startup --python "$r\scripts\build_better_steve.py" -- $r --style classic
& $b -b "$r\foundry\BetterSteve.blend" --python "$r\scripts\animate_locomotion.py" -- $r   # add locomotion + export
& $b -b "$r\foundry\BetterSteve.blend" --python "$r\scripts\animate_actions.py" -- $r      # add actions + export
& $b -b "$r\foundry\BetterSteve.blend" --python "$r\scripts\check_locomotion.py" -- $r
& $b -b "$r\foundry\BetterSteve.blend" --python "$r\scripts\export_ue.py" -- $r   # re-export an edited .blend
& $b -b "$r\foundry\BetterSteve.blend" --python "$r\scripts\render_previews.py" -- $r [locomotion|actions,swim]
```

A full rebuild is `build_better_steve.py`, `animate_locomotion.py`, then `animate_actions.py` (each step overwrites the
.blend; `animate_locomotion.py --no-export` skips an export the last step redoes). `export_ue.py` alone does not write
the pickaxe FBX.

## Rig

55 deform bones (56 with the adventurer's `backpack`, a child of `spine_03`: add AnimDynamics or a physics body
in UE to make the pack bounce), 11 control bones (Blender only). Body names follow the UE Mannequin
(`root, pelvis, spine_01..03, neck_01, head, clavicle/upperarm/lowerarm/hand_l|r, thumb|index|middle|ring|pinky_01..02_l|r,
thigh/calf/foot/ball_l|r`), so Unreal's IK Retargeter can map Mannequin animations onto him.

**Controls in Blender** (bone collections: Body, Fingers, Face, IK Controls; left blue, right red):
- `CTRL_foot_ik_l|r` + `CTRL_knee_pole_l|r`: leg IK (on by default). Move `pelvis` down to crouch, feet stay planted.
- `CTRL_hand_ik_l|r` + `CTRL_elbow_pole_l|r`: arm IK (off by default).
- `CTRL_eyes_target`: the eyes follow it (child of the head; move it to look around).
- Switches on the `root` pose bone (N panel > Bone > Custom Properties): `leg_ik_l/r`, `arm_ik_l/r`, `eyes_track` (0..1).

**Face bones** (also exported, so Unreal can animate them):
| Bone | Do | Range |
|---|---|---|
| `eyelid_upper_l/r` | move down (local +Y) | 5.5 cm = fully closed |
| `eyelid_lower_l/r` | move up (local +Y) | ~1-2 cm (squint/happy) |
| `eye_l/r` | rotate | about +/-8 deg before the iris leaves the socket |
| `jaw` | rotate (chin down) | ~3.4 deg = fully open |
| `lip_corner_l/r` | move up/down/out | ~+/-1.2 cm (smile/frown) |
| `lip_upper` | move | small (sneer) |
| `brow_l/r` | move up/down, roll about the forward axis | angry/sad tilts ~15 deg |

**Actions** (exported as Unreal AnimSequences): `BS_Idle` (breathing, blink, glance; loops), `BS_Walk` (the original slow
placeholder stride, 32 f; superseded by `BS_Walk_<dir>`), `BS_FaceTest` (blink, smile, talk, surprise, angry, sad, look),
`BS_FlexTest` (crouch + fists, high knee + reach), and the locomotion, action and swimming sets below.

## Locomotion

Generated by `scripts/animate_locomotion.py`: in place (the root never moves), 30 fps, every frame keyed. Speeds are
Unnamed's (vanilla) movement, and a planted foot's contact point (heel, sole, then toes) travels at exactly the capsule's
speed, so the feet don't slide. The game's walk (4.32 blocks/s) is jogging pace for a 1.875-block body, so "walk" is
authored as an easy jog (decided 2026-10-13).

| Actions | Game state | Blocks/s (straight / diagonal) | Cycle |
|---|---|---|---|
| `BS_Walk_<dir>` | walk | 4.3172 / 4.4053 | 18 f, 3.33 steps/s: a light jog with a short flight |
| `BS_Sprint_<dir>` | sprint | 5.6123 / 5.7269 | 16 f, 3.75 steps/s: a run, more lean and arm drive |
| `BS_CrouchWalk_<dir>` | sneak | 1.2952 / 1.8316 | 32 f, 1.88 steps/s, always a foot on the ground |
| `BS_CrouchIdle` | sneak, still | 0 | 60 f loop, eyes at 1.27 (the game's crouching eye height) |
| `BS_StandToCrouch`, `BS_CrouchToStand` | crouch toggle | 0 | 8 f, eased like the game's eye smoothing |
| `BS_Jump_Start`, `_Loop`, `_Land` | jump | - | 9 f takeoff to apex (the game's 0.3 s rise), 24 f air loop, 12 f land into `BS_Idle` frame 0 |

`<dir>`: `F FR R BR B BL L FL` = Blend Space direction `0 45 90 135 180 -135 -90 -45` (+ is to the right). Diagonals
are faster in the game (a diagonal input reaches full speed; sneaking diagonally is 41% faster), so each diagonal clip
is authored at its own speed. Every loop starts on the left foot's touchdown, so the clips of one gait share phase.
Strafes turn the hips 60% toward the travel direction and twist the spine back so the chest faces forward; backward
directions run backward. The game only sprints with forward input (F, FL, FR); the other five sprints are for a
future mode.

**In Unreal:** 2D Blend Spaces, Direction (-180..180) x Speed (cm/s). Place each clip at its own speed: straight
samples at 431.7 / 561.2 / 129.5, diagonals at 440.5 / 572.7 / 183.2, and idle (crouch idle) at speed 0 in every
direction; keyboard input lands exactly on the samples. Jump state machine: Start on the jump, Loop when Start ends
(and for any fall), Land on touchdown, then back to locomotion. Land is in place: when landing while moving, blend back
to locomotion quickly or play Land as an additive.

**Look poses (Aim Offset):** `BS_Look_C U D L R UL UR DL DR` = BS_Idle frame 0 looking at yaw/pitch 0 or +-90 (yaw +
right, pitch + up; the game's pitch reaches +-90). The turn is shared by spine_02 5%, spine_03 15%, neck 25%, head 55%
as fractions of one rotation, so every pose points exactly where it says; the eyes lead about 5 degrees, the upper lids
follow a downward look. In Unreal: set each to Additive Anim Type = Mesh Space, Base Pose = Selected Animation Frame,
`BS_Look_C` frame 0; then an Aim Offset (Yaw -90..90, Pitch -90..90, 2 grid divisions) with each pose at its yaw/pitch,
layered over locomotion and driven by the control rotation minus the body's yaw.

![look](previews/11_look.png)

Checks (`check_locomotion.py` on the solved rig and deformed mesh, then the same numbers on Unreal 5.8's evaluated
poses): planted-foot slide <= 0.05 mm per frame (limit 2 mm), contact height within 0.1 mm, lowest vertex >= -0.1 mm,
loop seams 0, root fixed, bone scale 1.0, crouch eyes 1.270, look poses 0 degrees off. The Unreal check also makes the
look poses additive and builds a test Aim Offset (`AO_BetterSteve_Look`, 9 samples) in the scratch project.

![walk](previews/10_Walk.png)

## Actions: mining and placing

Generated by `scripts/animate_actions.py`: upper-body clips over the idle stance (legs as `BS_Idle` frame 0), 30 fps,
every frame keyed, root fixed. Timing is Unnamed's (`FoundationBreakEffects.h`, `FoundationInteraction.h`).

| Action | Frames | Game timing | What |
|---|---|---|---|
| `BS_Hold_Pickaxe` | 60 loop | - | Right fist around the handle, pick head forward and up, breathing |
| `BS_Mine_Swing` | 9 (0.3 s) | the 6-tick swing; the pick bites on frames 4-5, around tick 3 (0.15 s) when the game plays the hit | Frame 0 = frame 9 = the hold, so held mining loops it back to back |
| `BS_Mine_Swing_Up`, `BS_Mine_Swing_Down` | 9 | same | The same swing aimed at view pitch +60 / -60 (same first and last frame) |
| `BS_Place` | 6 (0.2 s) | held use repeats every 4 ticks | Reach forward, grip, set the block, let go; starts and ends at the idle arms |

Better than vanilla's rigid shoulder swing: the joints fire in sequence. The torso winds up (spine twist and lean) and
uncoils first, the clavicle lifts and then drives forward, the elbow leaves the windup later and whips straight, the
wrist stays cocked longest and snaps last, and all of them land together on frame 4; the fist squeezes and the brows
set, the pick bounces off on frame 5 and the arm settles back into the hold. In the windup the forearm turns out, so the
pick hangs behind the shoulder instead of swinging through the head; the left arm counterbalances.

Checked in Blender (`check_locomotion.py`, on the solved rig and deformed mesh): on the contact frame the pickaxe tip is
0.84 m in front of the eyes at eye level (elevation -4 degrees; `_Up` +58, `_Down` -66), arriving at 33 m/s and leaving
under 3 m/s (the impact stop); no pickaxe vertex goes more than 3 mm into the body on any frame (the fist excepted).

![actions](previews/12_actions.png)

**Stand-in pickaxe:** `export/SM_StandInPickaxe.fbx`, a pixel-style iron pickaxe (handle 0.59 blocks, head 0.27). In the
.blend it is bone-parented to `hand_r` and the previews show it only in clips that hold it. Its pivot is the grip (the
centre of the closed fist) and its axes are the character's at rest: handle toward Unreal +Y (forward) with the head at
that end, blade vertical. In Unreal give `hand_r` a socket (`hand_r_pickaxe`) with **Relative Location (0, -7.25, 2.70)
cm, Relative Rotation X 0, Y 90, Z -90** (roll, pitch, yaw) and attach the mesh with no extra offset; the same
relative transform works for attaching straight to the bone. Add the socket in the Skeletal Mesh editor: UE 5.8's
Python can't create one (`SkeletalMeshSocket.socket_name` is read-only). Verified headless: on each swing's contact
frame the tip through this transform lands within 0.001 cm of Blender's. For the game's own `iron_pickaxe` mesh, use the
same socket: the handle runs through the fist along the socket's +Y (toward the head) and the blade along its Z.

**In Unreal:**
- Hold: Layered Blend Per Bone from `clavicle_r` (right arm only) over locomotion while a pickaxe is held, so the left
  arm keeps its stride swing.
- Swing and place: an upper-body slot montage, Layered Blend Per Bone from `spine_01` with Mesh Space Rotation Blend,
  so the legs keep walking. Start it on every swing-clock start: 9 frames = 6 ticks, and frame 9 equals frame 0, so
  held mining plays back to back. Aiming by pitch: a 1D Blend Space (Pitch -60..60: `_Down`, base, `_Up`) in the
  upper-body layer, its time driven by the game's swing clock (phase x 0.3 s) instead of a montage.
- The swings keep the head facing forward; the Aim Offset look poses still layer on top.
- Without an Animation Blueprint (Unnamed's `FFoundationBodyAnimator`, see "In the game" below) the rule is the same:
  take `spine_01` and its children from the action clip (mesh-space rotations), everything else from locomotion; for
  the hold, only `clavicle_r` and its children.

## Swimming

| Action | Frames | Swim speed | What |
|---|---|---|---|
| `BS_Swim_Tread` | 48 loop (1.6 s) | 0 | Upright: sculling hands at belly height, bicycle legs, a gentle bob |
| `BS_Swim_F` | 30 loop (1 s) | 1.96 blocks/s | Breaststroke: outsweep, insweep with a breath, frog kick, glide; the body about 25 degrees off horizontal |
| `BS_Swim_Sprint` | 40 loop (2 x 0.67 s) | 3.92 blocks/s | Front crawl: high-elbow pull, body roll (hips 25, shoulders 40 degrees), flutter kick; a breath to the right on the second stroke cycle |

In place, full body. The arms are posed by the arm IK (hand paths, elbow poles, palm directions) and baked to FK (the
rig's IK switches are never keyed); the legs are FK handed to the leg IK controls. The wrists pull backward at about
the swim speed (mean 2.13 m/s in the breaststroke, 3.68 in the crawl), so the hands look like they grip the water.
Tread and breaststroke sit above the root inside the upright player box (head and hands reach forward of it in the
breaststroke). The crawl follows vanilla's swimming pose: body horizontal with its centre line 0.30 above the root
(vanilla's 0.6-block swimming box, eyes about 0.4), head forward; the pull dips below the root.

**In Unreal:** a swim state: water and not sprinting is a 1D Blend Space (Speed 0..196 cm/s: `BS_Swim_Tread`,
`BS_Swim_F`); sprint-swimming plays `BS_Swim_Sprint`. Its body pitch follows the view: rotate the mesh (or the root
bone) by the view pitch about the body centre, the root + 30 cm up. Unnamed keeps the upright 1.8 box while swimming
today; until it has a swimming box, raise the crawl to sit in the water, not on the box floor.

![swim](previews/13_swim.png)

## Unreal import

Drag `export/SK_BetterSteve.fbx` into the Content Browser (default settings). Verified headless in UE 5.8: skeletal mesh
187.5 cm, every bone at scale 1.0, rooted at `root`, 51 AnimSequences, material + texture created. Then:
- `T_BetterSteve`: **Filter = Nearest**, Mip Gen Settings = NoMipmaps (keeps the pixels crisp; tiles don't bleed).
- Why the FBX is written in centimetres: an FBX in metres makes Unreal scale the root joint, leaving every bone at
  scale 100 (props on sockets come out 100x). `export_ue.py` handles this; don't export with Blender's default settings.

## In the game (Unnamed)

Since 2026-10-13 he is the player's body in Unnamed's F5 views (lane `feature/player-body`, worktree
`C:\dev\projects\Unnamed-body`; record `Docs/Records/2026-10-13-Player-Body.md`, design in
`Docs/Foundation/Architecture.md`, "Player body and the third-person views"):
- The game keeps its own copy of the FBX, `Art/Characters/BetterSteve/SK_BetterSteve.fbx` (hash in its `manifest.json`;
  as of 2026-10-13 the 17:44 locomotion export, not yet the 20:30 one with the actions),
  imported by `Scripts/Editor/ImportBetterSteve.py` into `/Game/Characters/BetterSteve` (actions renamed to `BS_*`, look
  poses made mesh-space additive, texture Nearest without mips). After a re-export here: copy the FBX over, update the
  manifest hash, move `Content/Characters/BetterSteve` away and rerun the script (an existing skeleton keeps its old
  reference pose).
- No Animation Blueprint: `FFoundationBodyAnimator` (C++) picks the clips by name (`FoundationBodyClip::GetName`) at the
  speeds in "Locomotion" above, and `UFoundationBodyAnimInstance` samples them. New clips (mining, placing, swimming;
  `docs/ACTIONS_HANDOFF.md`) are wired there.
