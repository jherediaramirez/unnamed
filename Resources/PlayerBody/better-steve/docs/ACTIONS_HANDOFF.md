# Better Steve: actions handoff (mining swing, placing blocks, swimming)

## Done (2026-10-13, actions chat)

- `scripts/animate_actions.py` adds 8 clips + a stand-in pickaxe; README "Actions: mining and placing" and "Swimming"
  have the clip tables, the Unreal layering rules and the `hand_r_pickaxe` socket transform.
  - `BS_Hold_Pickaxe` (60 f), `BS_Mine_Swing` (9 f; bites on frames 4-5 = tick 3; first == last == hold), plus
    `_Up` / `_Down` aimed at pitch +-60, `BS_Place` (6 f = the 4-tick held-use repeat).
  - `BS_Swim_Tread` (48 f), `BS_Swim_F` (breaststroke 1.96, 30 f), `BS_Swim_Sprint` (crawl 3.92, 40 f, vanilla's
    horizontal swimming pose, a breath every second stroke cycle).
- The swing chains the joints (torso, clavicle, elbow, wrist leave the windup in turn and land together), and the
  forearm turns out in the windup so the pick clears the head. Swim arms are posed with the arm IK and baked to FK; FK
  legs are handed to the leg IK controls. The root IK switches are never keyed.
- Checks green: Blender (`check_locomotion.py`: seams 0, tip at eye level 0.84 m out on the contact frame, no pickaxe
  in the body, swim pulls at 2.13 / 3.68 m/s against 1.96 / 3.92) and UE 5.8 (51 sequences, scale 1.0, root fixed,
  seams 0, tip through the socket transform within 0.001 cm). Strips: `previews/12_actions.png`, `13_swim.png`.
- Not done: Unnamed has no swimming box yet (the crawl assumes vanilla's 0.6 box), the socket asset must be added by
  hand (UE 5.8 Python can't set the socket name), and nothing is wired into the game (the hookup lane owns that).
  Possible next steps: a held-block clip for placing, swim strafes, a mining-while-crouched check.

---

Written 2026-10-13 at the end of the locomotion chat. Read this, then `README.md` (sections "Locomotion" and "Look
poses"), then `scripts/animate_locomotion.py` (it is the template for this work).

## Goal

Author Better Steve's action clips in Blender, export them and prove them in Unreal 5.8, the same way the locomotion
set was done:

1. **Mining swing** with a stand-in pickaxe in the right hand, plus a hold-the-pickaxe pose.
2. **Placing a block** (the use-item swing).
3. **Swimming**: treading water, swimming, sprint-swimming.

Jesus's bar: it should look **better than Java Minecraft**, where the whole arm swings rigidly from the shoulder.
Better Steve has a 3-bone spine, clavicles, elbows, wrists and two-joint fingers, so use them: the swing starts in the
torso, the shoulder drives, the elbow whips, the wrist snaps at contact, and the fingers wrap the handle. This is an
exploration ("see what it can roughly look like"), so 2-3 good clips beat 10 rough ones.

A parallel chat may be hooking the character into Unnamed tonight (`docs/HOOKUP_HANDOFF.md`). This chat stays in
`better-steve` and does not edit Unnamed.

## Current state (all verified 2026-10-13)

- 43 actions exported (`export/SK_BetterSteve.fbx`, `.glb`): idle, the old placeholder walk, face/flex tests, the 30
  locomotion clips (walk = jog, sprint, crouch walk in 8 directions; crouch idle and transitions; jump start/loop/land),
  and 9 look poses (`BS_Look_*`, Aim Offset).
- Checks pass in Blender (`scripts/check_locomotion.py`) and in UE 5.8 (scratch project, see below): foot slide
  <= 0.05 mm/frame, soles never below -0.1 mm, loop seams 0, root fixed, bone scale 1.0.
- **Body fix this chat:** the boot's skin weights were front/back reversed (heel on `ball_l`, toe tip on `foot_l`);
  fixed in `build_better_steve.py` (`foot_w`). Lesson: a clip that moves a bone nobody moved before can expose a
  weighting bug, so always check the **deformed mesh**, not just bones.

## Game facts to match (Unnamed source)

| Thing | Value | Where |
|---|---|---|
| Swing | 6 ticks = 0.3 s; the hit lands on the swing's tick 3; holding attack on a block repeats swings back to back | `Source/Unnamed/Foundation/FoundationBreakEffects.h` (`SwingSteps`, `FSwingClock`, `IsHitStep`) |
| First-person swing pose today | `sin(phase x pi)` curve on the held item | `FoundationBreakEffects.h` `GetSwing` |
| Place | uses the same swing; check the repeat delay for held use in `FoundationInteraction.cpp` before timing a loop | |
| Swim speeds | 1.96 blocks/s in water, 3.92 sprint-swimming with the eyes under | `FoundationProfile.h` comments; `foundation.Swim 1` |
| Swim states | Space rises, Sneak sinks, jump-out at walls, lava variant | `Docs/Records/2026-10-12-Swimming.md` |
| Tick | 20 Hz (0.05 s); clips are 30 fps, so a 6-tick swing is 9 frames | `FoundationProfile.h` |

Vanilla's sprint-swim is a horizontal crawl with the body pitched to the view; plain water movement is the walk pose
moving slowly. A better-than-vanilla swim: a treading loop (sculling hands, eggbeater legs), a breaststroke-ish swim for
1.96 blocks/s, and a front crawl for 3.92 blocks/s with the body pitch following the view (or a few pitch variants).

## The pickaxe stand-in

Options, pick one:
- **Blender prop** (recommended for authoring): model a pixel-style iron pickaxe like the one on the backpack
  (`build_better_steve.py`, `build_gear`, the "iron pickaxe strapped diagonally" block, about line 472), as a separate
  object parented to `hand_r` (bone parent) so previews show it. Export it separately as `export/SM_StandInPickaxe.fbx`
  if Unreal should see it. Do **not** add a bone to the skeleton: a new bone changes the reference pose and forces a
  clean re-import of everything (see the re-import gotcha below). In Unreal, a socket on `hand_r` holds the item.
- **The game's item**: Unnamed already has the 26.3 `iron_pickaxe` (an extruded sprite mesh, `FoundationItemMesh`).
  The hookup chat can attach it to a `hand_r` socket. Author the grip so the handle runs through the closed fist along
  the hand's local axis, and note the socket transform in the README.

## Suggested clips

| Clip | Frames | Notes |
|---|---|---|
| `BS_Hold_Pickaxe` | loop, e.g. 60 | right fist around the handle, forearm angled so the head rests forward; an upper-body overlay |
| `BS_Mine_Swing` | 9 (0.3 s) | windup, strike with contact on frame ~4-5 (tick 3), follow-through; first and last frames equal so back-to-back swings loop |
| `BS_Place` | about 6-9 | a shorter forward push/tap with the hand open or holding the block |
| `BS_Swim_Tread` | loop | in-place treading |
| `BS_Swim_F` (+ other directions if cheap) | loop | 1.96 blocks/s |
| `BS_Swim_Sprint` | loop | crawl at 3.92 blocks/s |

**Layering:** swing, place and hold are upper-body clips. In Unreal they go through a Layered Blend Per Bone from
`spine_01` up (mesh-space rotation blend) over locomotion, so the legs keep walking. Author them with the legs in the
idle stance; only the upper body matters. **Aim:** vanilla swings toward the view; a better version adds pitch
variants (for example swing at pitch -60 / 0 / +60, blended by pitch) so he mines blocks at his feet and overhead
convincingly. Optional: try one if time allows.

Swim clips are full-body, in place, and need no foot-slide check (no ground). Check instead: loop seams, no
self-intersection that reads badly in the strips, and that the hands' stroke speed roughly matches the swim speed.

## Pipeline (reuse, don't rewrite)

- New script `scripts/animate_actions.py`: `import animate_locomotion as al` (its `main()` is guarded) and reuse
  `al.STAND`, `al.spec`, `al.keyed`, `al.mix`, `al.arms`, `al.level`, `al.look_spec` and `al.bake(..., raw=True)` for
  hand-built specs. Give each action `bs_family` (e.g. `Action`, `Swim`) so the checks and previews pick it up.
  Run order for a full rebuild: `build_better_steve.py`, `animate_locomotion.py`, `animate_actions.py` (only the last
  one needs to export). Do not change the existing clips in `animate_locomotion.py`.
- Pose specs: `{bone: {"loc": world delta, "rot": [(axis, deg), ...]}}`, rotations about rest-world axes relative to
  the parent; for exact compound rotations, build a quaternion and convert to Euler XYZ, as `look_spec` does.
  Rig signs are in `docs/ANIMATION_HANDOFF.md` ("Rig: how to drive it"). Fingers curl: left `+`, right `-` about Y.
  The arms are FK by default; `arm_ik_l/r` on the root bone switches to IK (`CTRL_hand_ik_*`), which helps to pin a
  hand on a handle or a block face.
- Key **every frame** (pose function of the frame); a 1-frame action can drop out of the FBX bake, so use at least 2.
- Checks: `scripts/check_locomotion.py` runs on every action with `bs_family` (ground, loop seams); add what the new
  families need (e.g. hand position on the contact frame, prop not inside the body).
- Previews: `render_previews.py -- <root> locomotion` renders a 6-frame strip per action into `10_<family>.png`
  (`look` renders `11_look.png`). A side view helps for swings; see `C:\dev\sandbox\better-steve-anim\side_strip.py`.
- Unreal: `C:\dev\sandbox\better-steve-ue-check\run_locomotion_check.ps1` clears the scratch content, imports the FBX
  and runs `ue_locomotion_check.py` (writes `locomotion.json`). Extend it for the new clips. Before any UE run, list
  other lanes' editors; wait if one has `-VoxelFlight` or a bench running (test suites are fine).

## Gotchas from the locomotion chat

- Blender 5.2: `action.fcurves` is gone (layers > strips > channelbags); `export_ue.py` handles it.
- Writing PowerShell with inline here-strings that contain "/" paths next to Remove-Item gets blocked: write scripts
  to files with the Write tool. In Git Bash, `sed` replacements containing Windows paths mangle `\u`, `\b`: use the
  Edit tool. `python` isn't on PATH (use `uv run` or Blender's Python).
- UE re-import keeps an existing Skeleton's old reference pose: the check script clears `Content\BetterSteve` first.
- The camera-facing direction: the character faces -Y in Blender, +Y in Unreal; his left is +X in both.

## Definition of done

Clips authored, checks green, strips rendered and looked at, exported, verified in the scratch UE project, README
updated (clip table + Unreal layering notes), and a short note at the top of this file with what was done and what's
next. Not a git repo yet: nothing to commit unless Jesus asks to make it one.
