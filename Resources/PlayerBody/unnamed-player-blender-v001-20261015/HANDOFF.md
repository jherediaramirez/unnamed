# Unnamed Player v001 — Blender character + animation library, Unreal integration handoff

Built 2026-10-15 in Blender 5.2.2 (headless, scripted). **No Unreal changes were made**: nothing imported, no C++,
no builds, no commits. Blender round-trip success is *not* proof of Unreal import/runtime acceptance.

Game contract used (read-only, `C:\dev\projects\Unnamed-survival` @ `3e7c5493`): 1 block = 100 cm; box 60×60;
standing 180/eyes 162, sneak 150/127, low 60/40; 20 Hz tick; jump 42 cm/tick, gravity 8, drag 0.98; reach 450;
sample speeds walk 4.3172/4.4053, sprint 5.6123/5.7269, sneak 1.2952/1.8316 m/s (cardinal/diagonal).

---------------------------------------------------------------------------------------------------------------------

## 1. What was built

| Deliverable | Where |
|---|---|
| Editable master (collections `UP_EXPORT`, `UP_RIG`, `UP_PROPS`, `REF_SOURCE_normalised`, `GUIDES`, `PREVIEW`) | `blend/UP_Player_master_v001.blend` |
| Preserved source + provenance (CC-BY-4.0, Bully Magraptor) | `source/` (`PROVENANCE.md`) |
| Derived skin (lossless 64 px + nearest 1024 px); originals untouched | `textures/` |
| Skeletal mesh FBX (cm, 22 bones, 2 material sections) | `exports/SK_UnnamedPlayer.fbx` |
| 66 baked clips, one FBX each (take = clip name) + all-in-one indexed file | `exports/anims/A_UP_*.fbx`, `exports/UP_AllClips.fbx` |
| Pickaxe static mesh (origin = grip frame) | `exports/SM_UP_Pickaxe.fbx` |
| Manifest (units, axes, dims, hierarchy + rest transforms, bone map, clips, speeds, loops, contacts, additive refs, turn-yaw curves, sockets/grips, limits) | `exports/UP_manifest_v001.json` |
| Review reel: every baked clip on one timeline with travel, jump arc, turn yaw, strikes on the stone (67 markers) | `blend/UP_Player_review_v001.blend` |
| Reproducible scripts (`run_all.ps1` runs the chain) | `scripts/` |
| Measured validation (JSON per stage) | `validation/` |
| Previews (sheets, MP4, GIF) | `previews/` |

**Character.** 1.800 m, soles at 0, faces −Y in Blender. Proportion preset `B_head40`: head 40 cm cube (was 45 cm at the
uniform 0.225 scale), body/limb pixels 5.0 cm wide × 5.833 cm tall, so head and torso stay equally wide. Painted eye row
155–160 cm (centre 157.5); the 162 cm gameplay eye sits 2 cm above the eye row (uniform scaling: centre 154.7 cm, 7.3 cm
below). Exact alignment needs a 32 cm head (Jesus kept B on review). **Hands: the vanilla arm box** (Jesus, after the
review: the palm/finger/thumb blocks "still look off" → reverted): the arm is the original 12-row box with its original
bottom face; its last 15 cm is the hand, bent by a real wrist bone, and the pickaxe handle passes straight through that
end block like vanilla Minecraft (Jesus chose this after a research pass and a side-by-side with "hidden pixel fingers",
`previews/s10_hands_compare.png`). Both alternatives stay scriptable: `UP_HANDS=pixel` (vanilla when relaxed, four
1-px bars fold around the handle, 26 bones) and `UP_HANDS=blocky` (palm/fingers/thumb, 30 bones). Feet: 4 cm toe caps
with a ball hinge. Head is separate geometry
(`SK_UnnamedPlayer_Head`, material `M_UP_Head`) on the same skeleton; the torso top face closes the neck.

**Skeleton (22, deform-only).** `root` (floor, identity) → `pelvis` → `spine_01` → `spine_02` → `neck_01` → `head`;
`clavicle → upperarm → lowerarm → hand` per side (hand = wrist pivot + socket carrier; blocky option adds
`thumb_01/02`, `fingers_01/02`);
`thigh → calf → foot → ball` per side (ball = real toe hinge 5 cm behind the leg's front face). Rest = I-pose, knees
1.2 cm forward / elbows 1.2 cm back as IK bend hints. Not a drop-in for the 56-bone Better Steve skeleton.

**Control rig `UP_Rig` (separate object, never exported).** FK chain mirroring every deform bone; leg IK with foot
control + reverse foot (heel / toe-tip / ball pivots, ball pivot on the toe hinge), knee poles; arm IK/FK switch +
elbow poles; grip locators `c_grip_l/r` (handle axis through the centre of the arm's end block); pickaxe control `c_pickaxe` (tool-led) and
`m_pickaxe` (follows the right grip when `pickaxe_in_hand` = 1); two-handed support target `c_support_hand_l`.
Switches live on `c_root` (`ik_leg_*`, `ik_arm_*`, `pickaxe_in_hand`, `support_grip_l`, `hand_r_on_pickaxe`) and are
keyed in every action. `UP_Skeleton` copies the rig (Copy Transforms); baking evaluates the rig per frame and writes
plain FK keys, then corrects IK legs/arms analytically onto their targets (Blender's iterative IK stops ~1 cm short).

**Clips (66, 60 fps, keys 0..N, duration N/60, frame N = frame 0 for loops).** Editable rig actions are `RIG_UP_*`,
baked deform actions `UP_*` (both in the master).

| Family | Clips | Frames (s) | Notes |
|---|---|---|---|
| Idle / SneakIdle | 2 | 120 (2.0) | static feet, breath, weight shift, look drift |
| Walk F FR R BR B BL L FL | 8 | 36 (0.6) | brisk jog (duty 0.25), hips 0.55–0.59 m |
| Sprint ×8 | 8 | 32 (0.5333) | duty 0.21, 13° lean |
| Sneak ×8 | 8 | **54 (0.9)** | **changed from 64 (1.0667 s)**: at 1.0667 s the 1.8316 m/s diagonal needs 0.98 m steps; crouched legs then forced the hips to 0.27–0.40 m. At 0.9 s all 8 directions hold 0.47–0.49 m |
| StandToSneak / SneakToStand | 2 | 16 (0.2667) | feet planted, small settle |
| Jump_Start / Jump_Rise (hold f12–18) / Jump_Fall (loop) | 3 | 18 / 18 / 48 | no anticipation delay; start = takeoff extension |
| Land_Soft / Land_Firm | 2 | 12 / 24 | end on the idle stance |
| Turn & SneakTurn L/R 45/90 | 8 | 18 (45°), 30 (90°) | root-neutral; body yaw = metadata (`turn.body_yaw_progress`) |
| Look_* and SneakLook_* (DL D DR L C R UL U UR) | 18 | 1 pose | yaw ±90, pitch ±90 (+ = up); refs `UP_Look_C` / `UP_SneakLook_C` |
| Pickaxe_Hold + Pickaxe_StrikeA/B/C | 4 | 1 / 18 (0.3) | contact frame 9 (0.15 s = step 3 of 6); shared link pose f0 = f18 |
| Low_Pose, Flight_Placeholder, Death_Placeholder | 3 | 1 / 60 / 30 | **placeholders**, not a swim/flight/death library |

Gait rules: phase 0 = left contact, 0.5 = right contact for every direction; sneak has its own phase; strafes take real
sideways steps (hips turn ~40° toward travel, chest counter-twists to face the view); backward directions use a
time-reversed foot roll; diagonals and the ±180° seam are continuous by construction (hip yaw 0 at B). Contact/toe-off
frames per clip are in the manifest.

Strikes: A = big diagonal shoulder-led chop over the right shoulder; B = opposite (backhand) diagonal whose recovery
unwinds through the right side; C = compact chip, cocked wrist, sharp rebound. All are hand-led FK swings (natural
arcs). Wind-up/approach keys are solved from tool targets (grip position + handle direction, pick head kept behind or
above the shoulder) with the wrist cock limited to 35°; the contact key is solved so the tip lands exactly on the
target (polish pass after review: pick-into-head A 11.1 → 0 cm, B 4.1 → 1.7, C 6.1 → 0; into torso → 0).

---------------------------------------------------------------------------------------------------------------------

## 2. Visually inspected (rendered and looked at)
- Source vs rebuilt character, guides (1 m block, eyes 162/127/40, collision boxes): `previews/s01_*`.
- Rig tests T01–T14 (bends 90/135, wrists, ankles, relaxed/open/fist, one- and two-handed grips, toe-off, twist,
  extreme look, crouch, low pose, forearm twist good/bad) + hand close-ups: `previews/s02_sheet_*`.
- Every gait direction at 4 phases: `previews/s03_sheet_{Walk,Sprint,Sneak}.png`; travelling MP4s on the 1 m grid
  (camera rides the travel parent, same clock): `previews/loco_*.mp4`.
- Jump on the simulated 20 Hz arc, 2-block edge fall → firm landing, 90° turns with the yaw applied once,
  stand↔sneak: `previews/s04_sheet_air_turns.png`, `air_*.mp4`, `turn_*.mp4`, `transition_stand_sneak.mp4`.
- Look grids (stand + sneak): `previews/s05_sheet_look_grids.png`.
- True first person (camera fixed at the gameplay eye, head hidden, **vertical** FOV 70/110, stand/sneak/low,
  look-down with/without the FP mesh offset): `previews/s05_sheet_first_person.png`.
- Pickaxe: six-pose strips per strike `previews/s06_strips.png`; repeated vs varied side by side in third and first
  person `previews/s06_compare_AAAA_vs_ABAC_{3p,1p}.gif` + key frames `s06_compare_keyframes_*.png`; strikes layered
  over idle / walking / sneaking legs `previews/s06_strikes_over_locomotion.png`.

## 3. Numerically checked (baked data, every rig constraint muted; files in `validation/`)
| Check | Result |
|---|---|
| Height / soles / width | 1.800 m, 0.000, 0.80 m (with arms); weights normalised, ≤ 2 influences |
| Rest integrity (rig drives skeleton at rest) | max matrix deviation 4e-5 |
| Planted-foot drift, travel space, stance windows | walk ≤ 0.08 cm, sprint ≤ 0.002 cm, sneak ≤ 0.08 cm at 60 fps; at 120 fps sub-frame sampling walk 0.24, sprint 0.68, sneak 0.08 cm; 30 fps ≤ 0.08 cm |
| Idle, transitions, landings, all 8 turns (world space, yaw applied) | 0.00–0.01 cm |
| Feet below floor | none (min sole z 0.000) |
| Loop seams | pose identical at N and 0; seam acceleration ≤ 0.98× the largest interior one (no velocity break) |
| Root motion / scale keys | 0 / ≤ 2e-6 in every clip |
| Look grid, head-into-torso | C/L/R ≤ 0.3 cm, U 2.6, D 2.1; corners 9.4–9.7 cm (see limits) |
| Mesh-space additive emulation | standing grid on the sneak pose: up to 10 cm head clip looking up → separate sneak grid kept |
| First person, nearest body surface to the eye | ≥ 16.9 cm everywhere (near clip 2 cm): no camera/shoulder intersection |
| Pickaxe contact | tip on target at frame 9: error ≤ 0.001 cm (A/B/C); A and C never inside the stone, B's arc dips 2.5 cm into it just before contact; all frames reachable |
| Pickaxe wrist | right wrist ≤ 35° in A, B and C (solver limit) |
| Pickaxe vs body | tool into head 0 (A), 1.7 (B), 0 cm (C); into torso 0; the pick head brushes the right upper arm by 4.7 cm in C's rebound; the handle through the hand end block is by design |
| Chaining | link pose f18 vs f0 across A/B/C: 0.000 cm |
| FBX raw parse | UnitScaleFactor 1 (cm); Y-up/front +Z/coord X; 22 LimbNodes, Lcl Scaling deviation 5e-7 (no bone-scale-100 trap); one `UP_Skeleton` Null at scale 1 |
| Fresh-scene re-import | SK: 22 bones, names + hierarchy match, 1.800 m, soles 0, rest = manifest (4e-7 m), texture embedded. 66 clip FBXs: 22 bones each, rest identical to SK (0.0), frame counts match, root motion 0, scale 2e-6, planted drift recomputed on imported motion ≤ 0.08 cm. `UP_AllClips.fbx`: 66 takes, no stray takes |

## 4. Known limitations (measured, unresolved)
1. **Strike clearance after the polish pass** (box hands): pick into the head volume A 0, B 1.7 cm (f4), C 0; B's approach
   dips 2.5 cm into the stone just before contact; the pick head brushes the right upper arm 4.7 cm in C's rebound; arm
   boxes overlap the torso 5–8 cm at the shoulder in wind-ups (the blocky shoulder joint, as in vanilla). B's backhand
   brings the hand close to the first-person camera.
2. **Look corners** (±90 yaw with ±90 pitch) clip the head into the shoulders ~9.5 cm. Proposal: clamp |yaw offset| to
   ~75° when |pitch| > 60°, or rely on the turn controller keeping offsets ≤ 50°.
3. **Bendy joints**: knees/elbows at 90° stretch the outer face 2.5× and collapse the inner face (blocky-bend style, like
   the source); forearm twist must go on the hand (lowerarm twist 90° candy-wraps to 15% area — test T14).
4. **Two-handed support** exists in the rig (tool-led mode, rig test T08) but is not used in A/B/C: in motion it needed
   ~180° of left wrist and pushed the arms 7–8 cm into the torso. B is one-handed.
5. **First-person body offset**: to see legs and feet when looking down, the mesh must sit behind the eye (chest-anchored
   `fp_anchor`): 20 cm standing at pitch 0, 37 cm at −90; sneak 48–60 cm because of the crouch lean. Not applied during
   strikes, so contact stays on the authoritative target.
6. **Eye alignment** residual: painted eye centre 4.5 cm below the 162 cm guide (preset B), as described in §1.
7. Running needs a bent-knee gait (hips 12–15 cm below standing) because Steve's 70 cm legs must cover 4.3–5.7 m/s at the
   given cadences; walk reads as a brisk jog.
8. Low pose, flight and death are placeholders; swimming strokes are not authored.
9. Untested: everything in Unreal (import, skeleton, sockets, blending, additive behaviour, FP offset).

## 5. Unreal integration plan (proposal; no changes made)
1. **Import** `SK_UnnamedPlayer.fbx`: import uniform scale **1.0**, convert scene on, Force Front X Axis **off**, new
   skeleton `SKEL_UnnamedPlayer`. Check bone scales = 1 and height 180 cm. Expect the mesh to face **+Y** → keep the
   pawn's `BodyYaw - 90` mesh rotation. Do **not** run `ImportBetterSteve.py` validation (hardcodes 187.5 cm, 56 bones,
   BS_* names).
2. Import `anims/A_UP_*.fbx` onto `SKEL_UnnamedPlayer` (animation only, the bind mesh is included for a correct rest).
   Asset names = clip names (`UP_*`); old `BS_*` mapping is in `manifest.clips[].maps_to_old`.
3. Texture `T_UP_Skin_1024` (or 64 px): sRGB, Nearest filter, Pixels2D/no-blur mips. Materials `M_UP_Body`,
   `M_UP_Head`: hide the head section in first person.
4. **Sockets** (manifest `sockets`, cm in bone space): `hand_r_pickaxe` (attach `SM_UP_Pickaxe` with identity),
   `hand_l_grip`, `head_eye`, `fp_anchor`. UE 5.8 Python cannot create sockets headless (Better Steve lesson): add by
   hand or via a C++/editor utility.
5. **FFoundationBodyAnimator**: point clip names at `UP_*`; `GetDefaultLength` sneak = 54/60 s; walk 36/60, sprint
   32/60 unchanged; keep sample speeds; phase 0 = left contact; walk/sprint share normalised phase, sneak its own.
   Advance clocks **before** building the pose and sample body/camera transforms at the same rendered time.
6. **Air**: Jump_Start on the jump tick (no delay), Jump_Rise while vz > 0 (hold last frame), Jump_Fall while vz ≤ 0,
   land soft (< 10 m/s impact) / firm (blend weight `clamp((v−8)/4,0,1)`); edge falls skip Jump_Start; a new jump may
   cut any landing. Flat-ground jump: apex 125.2 cm at tick 5, landing tick 11 at ~8.9 m/s (manifest `air`).
7. **Turns** (view-facing locomotion): when |view − body| > ~50°, play Turn 45 (separation < 95°) or 90, applying
   `body_yaw_total × progress(t)` to the body yaw **once** (the clips never rotate the root); settle within ~15°;
   movement input cross-fades into locomotion in 0.1–0.15 s keeping the yaw reached.
8. **Look**: mesh-space rotation additives, reference `UP_Look_C` (standing) / `UP_SneakLook_C` (sneak); yaw + = right,
   pitch + = up. Camera never parented to the head; head motion may differ from the camera (no double bob).
9. **Pickaxe**: upper-body layer (mask = `manifest.pickaxe.upper_body_mask`, legs from locomotion; mesh-space rotation
   blend recommended for the spine); contact at 0.15 s = gameplay step 3. Gameplay hit/drops/damage stay in
   `FFoundationAuthority` at the authoritative target. Distant targets (beyond ~1.1 m, up to the 4.5 m reach) are
   **gesture-only**: play the same strike aimed by the look additive, no arm stretching or IK, effects at the target.
10. First person: hide `M_UP_Head`; optional FP mesh offset from `fp_anchor` blended in only below ~−35° pitch.

## 6. Waiting on Jesus / human approval
- Decided on review: proportion preset **B** (40 cm head) and **box hands** (`previews/s10_hands_compare.png`).
- Strike feel after the polish pass (watch the reel), whether to reinstate a two-handed B, and walk-as-jog style.
- First-person offset policy, look clamp, turn thresholds (all tuning proposals).

## 7. Reproduce
`scripts/run_all.ps1` (PowerShell 7) chains every stage from the source GLB: s00 audit → s01 build → s02 rig tests →
s03 locomotion → s04 air/turns/placeholders → s05 look + first person → s06 pickaxe → s07 export + manifest →
s08 round trip (+ s03b/s04b previews, pickaxe composites). Each stage script was run and verified individually in
their final form; the runner itself has not been executed end to end. `scripts/tools/patch_p*.py` are one-off edit
history (already applied; they do not re-run).
