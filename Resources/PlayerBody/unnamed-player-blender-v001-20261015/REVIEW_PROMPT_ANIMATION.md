# Hyper-polish review request: animation only (Unnamed Player v001)

You are a senior gameplay animator reviewing a finished procedural animation set for a blocky, Minecraft-style player
character before it ships to a game engine. Review **only the motion**: timing, spacing, weight, arcs, contacts,
silhouettes, transitions, and readability at game speed. Do not comment on export, textures, file formats, the engine,
or code style. We want a hyper-polish critique with fixes we can apply directly to the parameters below.

## 1. The character (what moves)

- Classic blocky Steve, 1.80 m tall: head is a 40 cm cube; torso 40×20×70 cm; arms and legs are 20×20×70 cm boxes.
  Limbs bend at elbows and knees with short smooth bend bands (blocky "bendy" style). **Hands are the vanilla arm end**:
  no fingers; the last 15 cm of each arm is the hand, bent by a wrist bone; held tools pass through that end block (as
  in Minecraft). Feet have a small 4 cm toe cap with a real ball (toe) hinge.
- Skeleton (22 bones): root (floor) → pelvis → spine_01 → spine_02 → neck_01 → head; per side clavicle → upperarm →
  lowerarm → hand; thigh → calf → foot → ball. Rest pose = arms down.
- Short legs for the speeds: hip joint 67 cm, leg (hip→ankle) 58 cm. This shapes every gait decision below.
- Axes: Z up, character faces −Y, +X is the character's left. Angles in degrees. Rotation signs used in the
  parameters: on a hanging limb +X swings it back (−X forward); on spine/neck/head +X bends forward/down; +Z yaws left.

## 2. Hard constraints (do not propose breaking these)

- 60 fps. Loops: keys at frame 0 and frame N are identical and duration = N/60 s (no endpoint dwell).
- **In place**: root never moves or rotates; travel and jump height come from the game. Feet must stay planted with
  < 1 cm drift (currently ≤ 0.08 cm at 60 fps, ≤ 0.68 cm sampled at 120 fps).
- Phase: 0 = left-foot contact, 0.5 = right-foot contact, shared by all 8 directions of a gait (runtime blends
  directions and walk/sprint by normalized phase). Directions F FR R BR B BL L FL = 0, 45 … −45° (+ = right).
- Game speeds are fixed (m/s, cardinal/diagonal): walk 4.3172/4.4053, sprint 5.6123/5.7269, sneak 1.2952/1.8316.
  Durations fixed: walk 36 f, sprint 32 f, sneak 54 f (shortened from 64: the 1.83 m/s diagonal sneak cannot fit
  crouched legs at 64 f), idle 120 f, transitions 16 f, jump start 18, rise 18 (holdable), fall 48 loop, soft land 12,
  firm land 24, turns 18 (45°) / 30 (90°), strikes 18 with contact on frame 9.
- No animation-driven input delay: a jump starts on the input tick, so no wind-up crouch before takeoff.
- Gameplay owns the camera: first-person eye is fixed at 162 cm (sneak 127, low 40), never parented to the head.
- Strikes are an upper-body layer (spine and up); legs come from idle/locomotion underneath.

## 3. How each family was built, and why

All clips are authored on a control rig (IK legs with a reverse-foot heel/ball/tip roll, FK upper body) and baked to
plain FK keys every frame, with an analytic two-bone correction so baked feet land exactly on their targets.

**Locomotion (walk, sprint, sneak × 8 directions).** A stance/swing model per foot.
- Stance: the foot moves backward at exactly the ground speed (cancels travel). Heel strike rolls flat in the first 22%
  of stance; heel lifts over the ball from 42% to 100%.
- Swing: Hermite path that keeps travelling back after toe-off (slope −0.55k), reaches past the contact point, then
  retracts (slope −0.7k), so touchdown foot speed is about 30% of ground speed. Lift ∝ sin^1.4 with its peak at 38% of
  swing (50% backward).
- Pelvis height is solved per clip from leg reach (stance legs ≤ 96.5% of full length). Bob = −bob·cos(4π(φ−D/2)),
  lowest at mid-stance.
- Walk at 4.3 m/s with these legs is therefore a bent-knee jog with a flight phase: hips 55–59 cm vs 67 cm standing.
  Sprint hips 55–57 cm, sneak 47–49 cm.

| Param (walk / sprint / sneak) | Value | Meaning |
|---|---|---|
| D (duty), diag | .25 / .21 / .58 (.47 diag) | fraction of the cycle a foot is planted |
| lead / lead_back | .47 / .45 / .50 ; .50 / .54 / .52 | share of stance ahead of the hips at contact |
| lift (m) | .17 / .27 / .075 | swing foot peak |
| lean (°) | 7 / 13 / 27 | forward lean (pelvis .65 + spine; sneak: pelvis .55, spine .25/.2) |
| bob (m) | .016 / .02 / .008 | pelvis vertical amplitude |
| width (m) | .085 / .08 / .105 | half stance width (+.035 on lateral moves) |
| arm (°), elbow (°) ±osc | 30, −72±10 / 48, −88±14 / 10, −40±6 | arm swing amplitude; elbow bends more when the arm is forward |
| pyaw, cyaw, proll (°) | 7,9,3 / 9,12,3.5 / 5,6,2.5 | pelvis yaw oscillation, chest counter-yaw, swing-side pelvis drop |
| strike, push (°) | 7, 38 / 4, 46 / 10, 28 | heel-strike toes-up angle, heel lift at toe-off |
| yaw_k | .42 / .42 / .38 | hips/feet turn this share of the travel angle toward travel |

- **Strafes and diagonals:** the hips and feet turn 42% of the travel angle toward it (R ≈ 38°, sneak 34°). The chest
  counter-twists 92% to face the view, the head stabilizes the rest, and feet step truly sideways.
- **Backward and back-diagonal directions:** a time-reversed foot roll (land on the ball, leave from the heel) with the
  hips turned away from travel (BR = +17–19°). The seam between forward style (R) and backward style (BR) is a real
  style switch; B has hip yaw 0, so BL↔B↔BR is continuous.
- **Arms:** swing opposite the legs, lagging 0.04 cycle, right arm 6% larger; 6±2° abduction, small clavicle drive.
- **Head:** counters 80–95% of accumulated pitch/yaw/roll, plus a 1.2° nod at 2× stride frequency.
- **Asymmetries:** left toe-out +5.5° vs right 4°, a 1.5 mm left/right bob difference, the right arm 6% bigger.

**Idle / sneak idle (120 f).**
- One breath per loop: chest ±1.1°.
- Weight on the right leg: pelvis 1.4 cm right, ±0.4 cm sway.
- Head drift on two harmonics (±1.6° and ±0.8° yaw).
- Arms hang 3° forward with elbows at −12°.
- Sneak idle is the same layer over a crouch: pelvis −20 cm and 6 cm back, 27° lean, arms 23° forward, elbows −42°.

**Stand↔sneak (16 f).** Feet stay planted. The crouch weight uses a smootherstep with a 6% settle overshoot.

**Air.** Sparse Bezier keys.
- Jump_Start: frame 0 is the takeoff extension (toes pointed 26–34°, arms swung forward to −62°), tuck by f11 (feet
  14–20 cm up), rise pose by f18.
- Jump_Rise: holds f12–18.
- Fall loop: arms out 38±6°, legs pedal ±2.5 cm.
- Land_Soft: compress to −7.5 cm by f3, back to idle by f12.
- Land_Firm: −19 cm with 22° lean by f5, recovers by f24.
- Game jump: 42 cm/tick takeoff, apex 125 cm at tick 5 (0.25 s); flat landing at tick 11, impact ≈ 8.9 m/s.

**Stationary turns (root-neutral).** The body yaw is metadata (progress = smootherstep(t)) applied once by the game.
Planted feet counter-rotate in mesh space, so they stay fixed in the world (0.00 cm drift). Each stepping foot lands
pre-rotated by the yaw still to come.
- 45°: lead foot steps at 6–48%, trail foot at 52–92%.
- 90°: lead 4–46%, trail 50–93%.
- Step height 7 cm (sneak 5).
- Head leads by up to 9°, chest by 4°, pelvis lags by 3°; 2.5 cm weight shift onto the planted foot.

**Look grid (9 static additive samples, yaw ±90 × pitch ±90, + pitch = up).**
- One reference pose (idle frame 0, breath frozen).
- Cumulative aim: each bone's orientation is Rz(yaw so far)·Rx(pitch so far), so there is no roll.
- Yaw shares: spine_01 .12, spine_02 .18, neck .28, head .42.
- Pitch-up shares: .10, .18, .32, .40. Pitch-down shares: .07, .11, .34, .48.
- The head slides 5 cm forward when looking down and 2 cm back + 3.5 cm up when looking up.
- A separate sneak grid uses more spine on pitch-up (.16, .22, .26, .36), because the standing grid applied to the
  crouch pushed the head 8–10 cm into the torso.

**Pickaxe strikes A/B/C (18 f, contact f9; hand-led FK, the tool rides the right hand).** Shared link pose at f0 = f18,
so any order chains.
- Keys at f0, 3, 6, 9, 10, 12, 15, 18 with auto-clamped Bezier. The right-arm channels have a sharp (vector) corner at
  the f9 impact.
- Wind-up/approach keys (f3, f6) are solved from tool targets (grip position + handle direction + tip hint) with the
  wrist cock capped at ±35°. This was the fix after the pick passed through the head.
- The f9 pose is solved numerically so the tip lands exactly on the stone. The correction is carried into f6 (50%),
  f10 (90%), f11 (75%) and f12 (55%).
- Arm angles (flex, abduct, humeral twist, elbow, wrist cock):
  - link (−40, 18, −12, −66, 0).
  - **A**, big diagonal shoulder-led chop over the right shoulder: f3 (−142, 60, −25, −76, −20) torso twist −22;
    contact (−92, 4, 0, −22, 14) twist +22, lean 15. Tip speed into contact ≈ 32 m/s.
  - **B**, backhand opposite diagonal from the left, whose recovery unwinds through the right: f3 twist +32; contact
    twist +24. ≈ 24 m/s.
  - **C**, compact chip with a cocked wrist and a sharp rebound by f11. ≈ 20 m/s.

## 4. Measured state and known weak spots (please judge these)

1. Walk reads as a bent-knee jog with a flight phase. This is inherent to 4.3 m/s on 58 cm legs at a fixed 0.6 s cycle.
   Is it believable and appealing, and what parameter changes would make it read as a brisk walk-run?
2. Strafes and the R→BR forward-to-backward style switch (a hip flip during direction blends).
3. Arm swing is still largely cosine-driven, though it has lag and asymmetry. We want more character without breaking
   the phase rules.
4. Jump start has no anticipation by rule. Do takeoff, tuck, fall and landing read with enough weight?
5. Look corners (±90 yaw with ±90 pitch) push the head about 9.5 cm into the shoulders.
6. Strikes:
   - B's approach dips 2.5 cm into the stone just before contact.
   - Arm boxes overlap the torso 5–8 cm at the shoulder in wind-ups.
   - C's rebound brushes the pick head 4.7 cm into the upper arm.
   - B touches the head 1.7 cm on one frame.
   - Is A's 32 m/s tip speed too snappy for 0.3 s?
7. First person (camera fixed, head hidden): is the strike and arm motion comfortable when seen from the eye?

## 5. What to look at

Previews (60 fps; videos travel on a 1 m grid, camera riding the travel at the same clock):
`previews/gallery.html` (everything), or individually:
- `s03_sheet_{Walk,Sprint,Sneak}.png` and `loco_*.mp4`.
- `s04_sheet_air_turns.png`, `air_*.mp4`, `turn_*.mp4`, `transition_stand_sneak.mp4`.
- `s05_sheet_look_grids.png` and `s05_sheet_first_person.png`.
- `s06_strips.png`, `s06_compare_AAAA_vs_ABAC_{3p,1p}.gif`, `s06_strikes_over_locomotion.png`.

Code (for exact edits): `scripts/up_loco.py` (gaits, idle, transitions), `scripts/up_actions.py` (air, turns),
`scripts/up_look.py`, `scripts/up_pickaxe.py`. Metrics: `validation/s03_locomotion.json`, `s04_air_turns.json`,
`s05_look.json`, `s06_pickaxe.json`.

## 6. What we want back

1. A prioritized findings table: **P0** (visibly wrong), **P1** (clearly improvable), **P2** (polish). Columns:
   - clip(s) and frames/phase
   - what you see
   - the cause, in terms of section 3
   - the exact fix: parameter name and new value, a new or moved key, or a curve shape
   - expected visual result
   - any constraint from section 2 it touches. If it touches one, say why it is worth it, or offer an alternative.
2. For locomotion, give per-gait recommended parameter sets (from the table above) and say which directions they help.
3. For the strikes, give revised key poses (same angle format) for any key you would change, plus whether A/B/C read as
   clearly distinct and chain well (A→B→A→C).
4. A short "keep" list: what already works and must not regress.

Be specific and numeric. Prefer small, high-impact changes. If something cannot be judged from the material,
say exactly what extra render or measurement you need.
