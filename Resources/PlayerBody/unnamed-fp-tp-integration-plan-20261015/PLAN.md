# Unnamed: full-body first/third person integration plan

Prepared 2026-10-15 (evening) for Jesus Heredia from a read-only audit. Nothing was imported, built, committed or
pushed for this plan; this file is the only thing written.

> **Head policy (stated interpretation, confirmed with the plan).** The head and headwear are hidden **only from the
> owning player's first-person camera**. The full character, head included, stays in F5 rear/front, the shoulder
> preview, any other player's or spectator's view, the owner's world shadow and ray-traced reflections. The shared
> asset keeps its head; hiding is a per-component runtime setting, never a deletion.

## 0. Decisions taken (Jesus's taps, 2026-10-15)

| Question | Choice |
|---|---|
| How the first-person body makes room to see legs/feet | **Planted feet, chest moves**: feet stay on the true position; only the owner's first-person copy shifts its hips (≤10-15 cm, legs re-solved) and moves its chest back under the camera. The whole-body offset (Blender's 20-37 / 48-60 cm) is not built. |
| How the body's hands/tool look in first person (default FOV 110 vs held items' fixed 70) | **World FOV now, test fixed later**: M1 draws arms, hands and tool like the world; Unreal's first-person projection for forearm/hand/tool is an M4 trial behind a seam test. |
| When to start | **Asset proof now, code after QC**: M0 (export + Unreal import proof) now in a separate worktree with no game code; M1 code starts on a fresh lane once survival's pending landings and final merge settle. |
| Save the plan | This file. |
| M0 go/no-go (Jesus asked Survival PM 5, session_01T56ySS741nM1kpyxmvShGm; answer ~02:55 UTC 2026-10-16) | **GO now**, assets only. Run it in a **blank content-only UE 5.8.3 project in `C:\dev\sandbox\fpbody-m0\`** (no C++ build, no build slot), not a worktree. The M1 code lane becomes a fresh worktree from the post-QC tip later. Kept outside the survival program until after QC (PM notes the chat in PROGRAM.md; no LANDINGS row, no token). Conditions: check `pwsh -NoProfile -File C:\dev\tools\shift-managers\scripts\quiet_window.ps1 -Status` before every UnrealEditor-Cmd run and never start while a window is clearing or open; one editor at a time, at BelowNormal priority; don't touch any `C:\dev\projects\Unnamed*` folder (incl. Unnamed-play and Unnamed-body), shift-managers files, or other UnrealEditor processes; no pushes; evidence only in our own output/sandbox folders; a classifier refusal is stop-and-tell-Jesus, never a reworded retry. |

Still open (for M2, recommendation yes): show the empty right arm in first person like vanilla, via the same
presentation IK.

**Open chats:** M0 runner `session_0114WGY52DwNTBxfxvswhMGR` ("UP M0: export + Unreal import proof (sandbox)",
started 2026-10-16 02:54 UTC by the planning chat `session_01Se4YftFDXKqfe2uM3AkET9`; Jesus's tap "I open a fresh M0
chat"). It reports to the planning chat and stops for Jesus's review.
**M0 status (2026-10-16 ~05:25 UTC): ON HOLD by Jesus's tap after v002c.**
- v002a: extra root bone, fixed by naming the armature "Armature".
- v002b: gates 1-8 pass on the mesh and the 4 multi-frame clips; single-frame pose clips fail to bake.
- v002c (`C:\dev\output\unnamed-player-export-v002c-20261016`): gates 1-5, 8 and 6-7 pass again (ACL error 0.0 cm; drift 0.078/0.049 cm; StrikeA tip 0.0003 cm); 18 looks set additive. The 20 pose clips import as 3 identical-looking keys over 0.033 s with an Interchange warning/ensure.
- The D/U looks' 5.385/4.031 cm "errors" match the Blender round trip's own distortion against the source, so the source .blend pose is the right reference (UE-vs-source not yet measured directly).
- To resume, pick one: (a) accept the 3-key pose clips after a per-bone check against the source pose (≤0.001 cm, normalized angles), then final library + pickaxe + cleanup; or (b) a 3-identical-key export test first.
- Sandbox `C:\dev\sandbox\fpbody-m0` still holds Proof, ProofB and ProofC. The M0 chat stays open (idle, ~279k tokens, $9.18).

**Pose-clip investigation (curiosity, Jesus's request): DONE.**
- Jesus ran the brief `POSE-CLIP-INVESTIGATION-PROMPT.md` in a Codex session.
- The Claude chat the planning chat had started (`session_01LCsJdXF9rcqD2dMYtmUFyP`) was stopped by Jesus after only reading the brief, and is archived.
- Report: `C:\dev\output\up-pose-clip-investigation-20261016\REPORT.md`. Sandbox `C:\dev\sandbox\up-posefix\`; 2 editor runs; all 243 protected hashes unchanged.

Findings:
- **Unreal matched the source all along.** All 20 v002b/v002c poses are within 0.00008 cm / 0.00009°, measured per bone with shortest-arc rotations. But they got there through an error/fallback path: Look_D baselines have 19 fallback tracks instead of 22, so this is not an acceptable import contract.
- **Unreal cause:**
  - `AnimTypes.h:27-28`: minimum animation length 1/30 s.
  - The Interchange FBX parser bakes `round(max(stop-start, 1/rate)·rate)+1` samples (2 at 60 Hz for frames 0..1).
  - The anim-sequence factory expects `max(duration, 1/30 s)`, i.e. 3 samples (`InterchangeAnimSequenceFactory.cpp:531-537, 553-586`). Result: a count mismatch, a broken track loop and constant-track fallback (`:664-710`).
- **Blender round-trip cause:** not gimbal lock. Blender's FBX importer auto-connects `head` to `neck_01` (`import_fbx.py:2620-2633`) and drops the head's authored translation: Look_D (0, 2, 5) cm, Look_U (0, 3.5, −2) cm. A direct FBX decode shows the exported data is faithful (≤0.00006 cm).
- **Runtime note (checked here):** the look poses' head translation survives the game's mesh-space rotation additive. `AnimationRuntime.cpp:1328-1342` converts only rotations to mesh space and adds translation locally, matching the report's 18/18 additive reconstructions.

**Proven M0 change (choose one; both clean, with 0 warnings/errors/ensures in a fresh session, raw = compressed, reloads OK):**
- **(a)** Keep the v002c FBXs unchanged. For the 20 pose clips only, set the Interchange animation pipeline to `animation_range = SET_RANGE`, `frame_import_range = 0..2`, `custom_bone_animation_sample_rate = 60` (exact snippet in the report). Expect 3 keys / 2/60 s imported, and record the source timing (2 keys, 1/60 s) separately.
- **(b)** Next export: `frame_end = 2` for the pose clips (3 identical keys), with Source Timeline import.

Also: replace the round-trip pose oracle with the source pose (or disconnect the imported head in memory before comparing), and compare rotations with normalized shortest-arc quaternions.
**M0 RESUMED** (Jesus's tap, 2026-10-16) in the M0 chat with fix (a): v002c FBXs unchanged, source-pose oracle, then
the final library, the pickaxe and cleanup. The report goes to the planning chat.

**M0 DONE (2026-10-16 ~06:00 UTC); every gate passes.**
- Final library in `C:\dev\sandbox\fpbody-m0` under `/Game/Characters/UnnamedPlayer`: 72 assets (1 skeleton, 1 skeletal mesh, 1 static mesh, 66 AnimSequences, 2 material instances, 1 texture; 0 redirectors). The proof folders are deleted.
- Skeleton and mesh: 22 bones, root `root`; 180.0000 cm; joints ≤0.0000435 cm; faces +Y; 2 sections; sockets ≤0.000007 cm.
- Clips: 66/66 pass gates 6-7. ACL ≤0.0 cm / ≤4.8e-6°; compressed planted drift ≤0.080 cm; strike tips at f9 A/B/C 0.0003/0.0003/0.002 cm.
- Pose clips: 3 keys / 2/60 s; all 22 tracks; ≤0.0000784 cm / ≤0.0000852° vs the source (every key, raw and compressed).
- Additives: 18/18 mesh-space looks match the source when reconstructed.
- Pickaxe: tip on the surface at 0.000007 cm.
- Log scan over 69 imports: 0 payload-count warnings, range errors or ensures. The only finding is the known embedded-texture error, superseded by T_UP_Skin.
- Two check corrections, both re-scored offline with no asset change: the loop-seam test was wrongly applied to 7 one-shot clips, and the pickaxe tip is a face centre, not a vertex.
- Evidence: `C:\dev\output\unnamed-player-export-v002c-20261016\evidence\final_20261016_015709\` (REPORT.md, final_asset_hashes.json for all 72 files, logs, source_poses.json).
- 54/55 snapshot hashes unchanged. The one change is Unnamed-use's HEAD, moved by that lane's own commits.
- **Next:** M1 (game code) waits for the survival QC/final merge, on a fresh lane from the post-QC tip. The game import repeats this proven procedure with `Scripts/Editor/ImportUnnamedPlayer.py`.
- The M0 chat `session_0114WGY52DwNTBxfxvswhMGR` is **archived** (Jesus's tap). No integration chats are open.

Sources read: `FIRST-THIRD-PERSON-INTEGRATION-PROMPT.txt` and `INTEGRATION-CONTEXT-20261015.txt` (polish folder), the
v001 package (HANDOFF, README, manifest, scripts, validation, previews), the focused candidate's evidence, the two
Codex transcripts from disk (`~\.codex\sessions\2026\10\15\rollout-…-01a140ca…jsonl` and `…-01a141aa…jsonl`; read, not
messaged), live `feature/survival` source, the installed UE 5.8.3 headers/source, and the official Epic pages listed in
the brief (fetched and summarized by a helper; key claims re-checked in the installed headers).

## 1. Recommendation and first milestone

**One animation state, two synchronized renders of the same character.**

- **`WorldBody`**: the full `SK_UnnamedPlayer` (head included). It is what F5, other views, shadows and reflections
  use. In owner first person it is set to `EFirstPersonPrimitiveType::WorldSpaceRepresentation` (hidden from the owner,
  but it keeps the owner's shadow, head included, and its ray-traced reflection); in F5 it goes back to `None`.
- **`FirstPersonBody`**: the same asset with the same per-frame clip times and weights from the one
  `FFoundationBodyAnimator`. It is owner-only, casts no shadow, and has its head/headwear sections off. It adds
  bounded first-person corrections nobody else sees: legs planted on the true feet, hips back ≤10-15 cm with legs
  re-solved, a camera-relative chest anchor that moves back as you look down (belly, legs and feet come into view),
  and a wall pull-back for hands/tool.
- **Tools** hang from a real `hand_r` grip on each body. The camera-attached held item is suppressed for those tools.
- The gameplay camera, eye, reach, collision and action results are unchanged.

**Why not one shared body.** On this character the gameplay eye (162 cm) sits at the **centre** of a 40 cm head: 20
cm behind the face and 22 cm above the torso's top. Looking down at a single world-placed body shows the neck/shoulder
top filling the view (Blender's own `previews/s05_sheet_first_person.png`, "NO body offset": nearest surface 22.4 cm,
legs hidden). Crouched, the head centre sits ≈28 cm *ahead* of the eye, so you would look down onto your own upper
back. Any fix applied to a shared body would also lean or shift the character in F5, in the shadow and for everyone
else.

**First useful milestone: M1 (playable slice), after the M0 export/import proof.**

- The UP body runs behind a switch; Better Steve stays as `foundation.Body.Set 0`.
- In F5: idle, walk, sprint and crouch locomotion, jump/fall/land, and look.
- In first person: the body is present, looking down shows your planted legs and feet with no head, and the world
  shadow keeps the head.
- F5 switching works mid-action.
- The accepted **StrikeA** plays through the real right hand on the existing 6-step swing clock: contact at step 3 is
  frame 9 of 18.

Acceptance is numeric (§7): planted drift <1 cm at 30/60/120 fps and jittered frames, contact at frame 9/60, a
simulation bit-identical with the body on or off, ≥15 cm between the eye and any body surface, and no frame with a
doubled or missing body when switching.

## 2. Current system and sources of truth

### 2.1 Integration checkout

`C:\dev\projects\Unnamed-survival` (`feature/survival` @ `db4e78cd`, clean at audit) holds the current player: body,
swim pose, death screen, use/shield, footsteps, hand fit. `Unnamed` and `Unnamed-body` sit at `9822d24`
(pre-survival). `Unnamed-body` is dirty (`Config/DefaultEditor.ini`, untracked `Art/Characters/BetterSteve/SK_BetterSteve.fbm/`);
preserve it.

Five live lanes change the same files and had not landed at audit time:

| Lane | Work | Player files it changes |
|---|---|---|
| `Unnamed-ent` | bow 19c, 3 commits | `FoundationPawn`, `FoundationController`, `FoundationInteractionView`, `FoundationSimulation`, `M_HeldItem` |
| `Unnamed-blocks` | ladders/climbing B4b | `FoundationPawn`, `FoundationSimulation` |
| `Unnamed-inv` | furnace particles 8b2b | `FoundationBreakEffects`, `FoundationInteractionView` |
| `Unnamed-mobs` | 20c2 | `FoundationPawn` |
| `Unnamed-use` | dirty: 13 modified + 3 new files | includes `FoundationPawn`, `FoundationInteractionView`, `FoundationAuthority` |

Survival is in feature freeze, and world saving comes first after QC (`C:\dev\tools\shift-managers\survival\FINAL-SCOPE.md`).
→ Integrate on a **new lane branched from `feature/survival` after those landings**. Put new logic in new files so the
hook edits in `FoundationPawn` and `FoundationInteractionView` stay small. Recheck `git worktree list` and status before
choosing; the snapshot above goes stale as work advances.

### 2.2 Trace: input → simulation → render → body → anim → camera → held item → effects

Paths are relative to the repository root; line numbers are snapshot aids at `db4e78cd`.

- **Input → intent:** `ConsumeIntent` → `MakeSimulationIntent` (`Source/Unnamed/Foundation/FoundationPawn.cpp:83`,
  `:607-625`). Look goes to the control rotation, with pitch clamped by the camera manager to ±89.9 (`:617-623`).
- **Authoritative simulation:** `TakeDueSteps` at 20 Hz, at most 5 catch-up steps. Each step runs `Simulation.Step`
  (`:652`), then `BodyTurn.Step` (`:656`), jump count (`:657`), vitals, footsteps, `StepInteraction` (`:678`) and
  `HandleDeath`. The actor stands at the latest step (`:684`).
- **Rendered transform:** `Alpha = Accumulator/Tick` (`:707`); `Eye = lerp(feet) + lerp(eye height)` (`:708-709`).
- **Camera:**
  - first person at `Eye`;
  - F5 rear/front ±400 cm along the view, clipped by a 5 cm probe against the voxel boxes (`:718-731`);
  - spring-arm preview 325/55/30 cm with lag, clipped the same way (`:231-258`);
  - hurt tilt and death roll in first person only (`:736-742`);
  - FOV = the vertical preference (**default 110**, range 30-110, `Content/Menu/Web/*.menuprefs`) × the sprint/flight
    multiplier, converted to horizontal (`:747-751`): ≈137° horizontal at 16:9;
  - `foundation.Hand.Fit 1` (default) gives first-person primitives a fixed vertical 70° (`:754-758`).
- **Body (Better Steve):**
  - `CreateBody` hard-loads `SK_BetterSteve` (`:137-170`): one component, a native anim instance, no collision,
    ticks after the pawn, rejects an incomplete clip set.
  - `UpdateBody` (`:196-229`) builds the frame (velocity from the last step's displacement, view yaw/pitch, the
    interpolated `BodyTurn` yaw, `CrouchAmount` from the smoothed eye, flags, jumps), calls `Advance`, places the mesh
    at the interpolated feet rotated `BodyYaw-90`, and shows it only when perspective ≠ 0 and the camera is >40 cm
    from the eye; while shown it uses `AlwaysTickPoseAndRefreshBones`.
- **Anim:** `FFoundationBodyAnimator::Advance` (`Source/Unnamed/Foundation/FoundationBody.cpp:116-285`): smoothed
  velocity (40 ms), an 8-sector direction blend, idle/walk/sprint weights by speed, a shared stand phase plus a
  separate crouch phase both advanced *before* the pose is built (`:152-160`), eased crouch transitions, the air
  sequence Start/Loop/Land (0.15 s grace, 0.1 s blends), and a 3×3 bilinear look. The proxy blends base layers, then
  adds look as a mesh-space rotation additive (`FoundationBodyAnimInstance.cpp:63-77`). Yaw modes 0/1/2 live in
  `FFoundationBodyTurn`; Vanilla's constants (0.3 per step, 50° head limit) are unverified.
- **Held item:** `UpdateHeldItem` (`FoundationInteractionView.cpp:1011-1222`; the component ticks in
  `TG_PostUpdateWork`, `:114`): camera first-person scale 0.5 (`:1021-1022`), `HandPivot` on the camera at
  (42, 24, -24) cm, held items are first-person primitives with no shadow (`:1040-1070`), shown only in first person
  while alive (`:1076`), swing from a 0.25 s press sine / mining |sin| / the swing clock (`:1163-1175`), eating via
  `MakeEatPivot` (`:1207-1218`).
- **Action clocks:** `FSwingClock` (`FoundationBreakEffects.h:100-107`, `GetPhase` `.cpp:194-201`). A press starts a
  6-step (0.30 s) swing at the interaction step (`.cpp:779-782`); it repeats while attack is held on a block
  (`:784`); in mode 2 (default) hits play on `SwingStart+3+6k` (`:177-181`). Its render alpha is `StepAlphaSeconds`,
  frame time counted since the effects object saw the step (`:772-775`, `:1237`), **not** the pawn's alpha.
- **Authority (unchanged by this plan):** mining progress per step in `MineContinue`, break decided by
  `CompletesBreak` (`FoundationAuthority.cpp:3100-3146`); melee `Attack`/`Whiff` (`:3474`, `:3624`); reach 450 cm
  blocks / 300 cm entities; eat uses `ConsumeTicks`; the shield blocks after a delay (`BlockingUseTicks` 72000);
  buckets and armor equip are instant.

### 2.3 Assumptions the 22-bone UP set breaks

- Names: `SK_BetterSteve` (`FoundationPawn.cpp:139`), `BS_*` (`FoundationBody.cpp:18-33`), the 40-slot table
  (`FoundationBody.h:26-41`).
- Air has one Loop and one Land; UP has Rise (holdable), a Fall loop, Soft and Firm.
- One look grid; UP has a stand grid and a separate crouch grid with their own references.
- No turn or action slots; UP has 8 turns, a hold pose and 3 strikes.
- Default lengths in 30 fps units: crouch is 32/30 s versus UP's **54/60 s**. Runtime uses asset lengths, but the
  unit tests use the defaults.
- `PlayerBodyRoot` is a single path (`Source/Unnamed/UnnamedProjectSettings.h:135`).
- `Scripts/Editor/ImportBetterSteve.py` checks 187.5 cm, 56 bones and `BS_*` names, and uses `BS_Look_C` as the
  additive base.
- The animator frame has no swim/low pose, use/equipment, death, or (pending) ladder input.
- The body hides at a 40 cm camera distance, which also drops its shadow.
- The held item never appears in F5.
- The probe asserts "body hidden in first person" (`FoundationBodyProbe.cpp:125-126`); it uses `ball_l`/`ball_r`,
  which exist in UP.
- Mesh facing +Y with `BodyYaw-90` is *expected* to hold (same exporter axes as Better Steve); verify once.

### 2.4 Sources of truth and stale claims

| Item | Use | Status / evidence |
|---|---|---|
| `C:\dev\output\unnamed-player-motion-polish-v002-20261015\blend\UP_Player_arm_polish_v002.blend` | **Export source** | SHA-256 `26E585D5676D5A8A18A6E6A2FAE5D682DF3470FA7B83A01972CFAE138D4D22C7` re-verified. v001 scene plus 6 strike actions copied at 19:11:13 from the master saved at 19:10:25. `validation/focused_arm_preservation.json`: lower-body delta 0, other actions unchanged. The approved "after" renders were made from *this* file (`logs/arm-render-after.log`). |
| Strikes A/B/C | Accepted (anticipation/approach only) | `s06_pickaxe.json` and `arm_polish_240hz.json` (19:10:25 / 19:10:32, run on the master, same actions): contact frame 9, error ≤0.002 cm, tool-in-body 0, arm-in-torso ≤3.4 cm, wrist ≤31°. Not accepted: first-person comfort, composition over locomotion (contact 1.04-1.09 m walking, 0.79-0.85 m crouched, against 1.25 m). |
| Other 60 clips, mesh, rig | Inherited v001, not re-accepted | v001 `validation/` applies: look corners put the head 9.4-9.7 cm into the torso; walk reads as a jog; R↔BR boundary. |
| Polish folder `s03/s04/s05` and `clip_meta_*` | **Do not use for the candidate** | They describe the experimental master. Polish `s05_look.json` shows corners at 0.0 cm versus 9.36/9.47 cm in the candidate. |
| `C:\dev\output\unnamed-player-blender-v001-20261015\exports\` | Do not import as "accepted" | Old strikes. |
| `UP_Player_master_v002.blend` (SHA `F2DBF903…0241`) | Not a source | Experimental. `up_config.MASTER` points to it, and `s07_export.py` also `rmtree`s `exports/anims`. |
| `UP_Player_master_v001.blend` (SHA `56A59E5F…E2F9`) | Baseline reference | Unchanged. |
| Old handoff: "+Y facing, keep −90" | Hypothesis | Verify once; never also add an import rotation. |
| Old handoff: "Python can't create sockets" | Partly true | 5.8.3: `SocketName` is `VisibleAnywhere` (`SkeletalMeshSocket.h:24`), and `RenameSocket` can't start from a None name (`FindSocketAndIndex` returns null for `NAME_None`, `SkeletalMesh.cpp`). But **Interchange imports leaf `SOCKET_*` nodes under the skeleton as mesh sockets** (`InterchangeGenericAssetsPipelineSharedSettings.h:119-123`, `InterchangeSkeletalMeshFactory.cpp:1904-1983`). |
| Old handoff first-person offsets 20-37 / 48-60 cm | Preview experiment | Not adopted (§0, §3). |
| Codex audit line numbers (`UpdateHeldItem:997`) | Stale | From `3e7c549`; now `1011`. |
| Jesus's prior call, bow handoff 19c (`Unnamed-ent`, `Docs/Records/2026-10-15-Bow-Handoff-Step19c.md`) | Binding | "Hold bobbing, keep visuals simple": a rigged first-person body replaces held-item poses later. Shield visuals (S-2) deferred to "the first-person body". |

## 3. Architecture

### 3.1 Alternatives

| | A. One shared world body + head component (OwnerNoSee + hidden shadow) | **B. Synchronized first-person body + world representation (chosen)** | C. Whole owner body as an Unreal first-person primitive (FOV/scale, feet alpha) + world representation |
|---|---|---|---|
| Looking down | Neck top fills the view; legs hidden; crouched you see your upper back | Owner-only chest anchor and hip shift reveal belly, legs and feet; feet planted | Same occlusion as A unless also corrected; the projection warps the torso against the floor |
| Hands and tool at pitch 0 | The hold pose leaves the hand ~66° below the view axis, out of frame | Owner-only arm presentation brings the tool into view | Possible, but needs a gradient |
| Shadow / reflections | Exact | World body (WSR) exact; the first-person body's pose differs slightly from its shadow | WSR shadow; first-person primitives cast no scene shadow and aren't in the ray-tracing scene |
| F5 / remote | Any first-person fix shows to everyone | Unaffected | Unaffected |
| FOV continuity | World FOV | World FOV (M1); first-person projection only for forearm/hand/tool later, if proven | Default vFOV 110 vs a fixed 70 magnifies parts ×2.04: seam or bend |
| Cost | 1 pose | 2 pose evaluations of 22 bones, ≈16 clip samples each (budget ≤0.1 ms each, to measure) | 2 evaluations plus material work |

Not proposed: an arms-only rig (excluded by the brief).

### 3.2 Chosen design (B)

- **One asset, one skeleton, two `USkeletalMeshComponent`s, one `FFoundationBodyAnimator`.** The pawn advances the
  animator once per frame and hands the same `FFoundationBodyPose` to both anim instances. Each carries a view mode,
  `World` or `FirstPerson`; the corrections are the only difference. Leader Pose can't serve the first-person body
  (followers can't run their own corrections); Copy Pose adds nothing over evaluating the same pose twice.
- **The head survives the importer as a section, not a component.** `UP_Body` and `UP_Head` combine into one skeletal
  mesh with 2 material sections (`M_UP_Body`, `M_UP_Head`); verify. That is sufficient: the first-person body turns
  the `M_UP_Head` section off (slot found by name) with `ShowMaterialSection`, safe only because that component is
  owner-only and casts no shadow. Future headwear/helmets attach to the world body only.
- **Tools:** two static-mesh components per body-supported item, one per body, attached to the `hand_r_grip` socket
  with per-item grip data. The world copy follows its body's first-person primitive type (WSR in owner first person);
  otherwise the owner would see a floating world pickaxe.
- **Visibility flags:**
  - First-person body: `bOnlyOwnerSee=1`, `CastShadow=0`, `bVisibleInRayTracing=0`,
    `bVisibleInReflectionCaptures=0`, `bVisibleInRealTimeSkyCaptures=0`, `bHiddenInSceneCapture=1`, type `None`.
  - World body: type `WorldSpaceRepresentation` in owner first person (and when the F5 camera is within 40 cm,
    replacing today's `SetHiddenInGame`, which also drops the shadow); `None` otherwise.
- **Death:** the first-person body is hidden (as held items are today); the world body plays the death placeholder.
- **Spectator mode:** both bodies hidden.

### 3.3 Visibility table

| Part | Owner 1st person | Owner F5 rear / front / shoulder | Other or spectator view | Owner's world shadow | Ray-traced reflections | Scene capture (if added) |
|---|---|---|---|---|---|---|
| Head | ✗ (section off on the first-person body; world body is WSR) | ✓ world body | ✓ | ✓ (WSR shadow) | ✓ world body | ✓ world body |
| Headwear (helmet, pumpkin; future) | ✗ (attached to world body only) | ✓ | ✓ | ✓ | ✓ | ✓ |
| Torso, legs, feet | ✓ first-person body (planted) | ✓ world body | ✓ | ✓ world body only | ✓ | ✓ |
| Both arms and hands | ✓ first-person body (+ owner arm presentation) | ✓ | ✓ | ✓ (world pose) | ✓ | ✓ |
| Body-supported tool | ✓ on first-person hand | ✓ on world hand | ✓ | ✓ (WSR copy) | ✓ | ✓ |
| Legacy items (food, bow, shield, blocks, offhand; temporary) | camera pivot, as today | not drawn, as today | – | none, as today | – | – |
| Armor body pieces (future) | ✓ (no helmet) | ✓ | ✓ | ✓ | ✓ | ✓ |

Each mechanism's real limits (verified in 5.8.3 headers/source unless marked):

| Mechanism | What it does | Limit |
|---|---|---|
| `HideBoneByName` | Zero-scales a bone and its children | Every view, its shadow and reflections; breaks anything attached to that bone |
| `ShowMaterialSection` | Hides a section for the whole component | Every view and shadow of that component; no per-view control |
| `bOwnerNoSee` / `bOnlyOwnerSee` | Per-view, by view-actor ownership | An OwnerNoSee primitive casts no shadow in the owner's view unless the hidden-shadow path applies |
| `WorldSpaceRepresentation` | Sets that hidden-shadow path up internally (`PrimitiveComponent.h:153-161`) | Forces OwnerNoSee, so switch back to `None` for F5. Shadows/reflections need the first-person GBuffer bit (`RenderUtils.cpp:2589`: deferred, non-mobile, no static lighting). This project qualifies (`r.AllowStaticLighting=False`, DX12); Substrate is on and needs a runtime check. |
| `FirstPerson` type | Takes the camera-wide first-person FOV and scale (shared with today's held items: scale 0.5, 70°) | No scene shadow (shadow flags are edit-locked for this type); not in the ray-tracing world |
| Screen-space and Lumen screen-trace reflections | Reflect only what the owner's view drew | In first person that is the headless first-person body: water may reflect it without a head; off-screen rays may hit the WSR body. Needs evidence on this renderer (Lumen GI and reflections, VSM, HW RT, MegaLights). |

### 3.4 Camera/body relationship (numbers from the manifest and Blender evidence; tuning marked *proposed*)

- **Camera stays simulation-owned:** feet + eye 162/127/40 cm, control rotation, never parented to the head;
  animation compression never moves it.
- **Standing:** the eye is 22 cm above the torso top (head cube 140-180 cm) and 20 cm behind the face; the painted eye
  is 4.5 cm lower (157.5 cm). With feet under the box and the torso front 10 cm forward, toe tips (≈14 cm forward) hide
  behind hips and torso when looking straight down. Line-of-sight estimate: hips and torso must sit ≥≈8 cm further back
  to clear them.
  - First-person correction (*proposed*), blend `c = smoothstep(pitch −20° → −75°)`:
    - pelvis back ≤10 cm, legs re-solved two-bone to the base pose's ankle and ball targets;
    - spine kept upright (look-pitch bend suppressed in this body only);
    - chest anchor (neck_01) 4→16 cm behind the eye and ~20-22 cm below it;
    - spine bend ≤35° per bone; total chest displacement ≤30 cm from the base pose.
- **Crouched (main geometric risk):** Blender's eye offset of 47.7 cm at pitch 0 puts the head centre ≈28 cm ahead of
  the 127 cm eye, so the camera sits behind the head, above the upper back. An upright torso on 47 cm hips would put
  the neck only ≈10 cm under the eye, and the near plane is 10 cm.
  - *Proposed:* pelvis back ≤15 cm with leg IK; chest anchor 10→22 cm behind the eye; reduce the forward lean.
  - M1 measures neck-to-eye distance at every sampled pitch. If it stays under 15 cm, that is a stop condition (§7)
    and the fallback choice comes back to Jesus.
- **Low/swim** (eye 40, 60 cm box): placeholder pose; Blender nearest surface 16.9 cm; arms in view; fine for a
  placeholder.
- **Look extremes:** the first-person body drops head/neck look (not visible anyway); chest pitch becomes aim, clamped
  −45°/+35° (*proposed*). Looking fully up: arms follow the chest; clearance checked. Neck closure: the torso's top
  face closes the neck; verify in first person at −90°. Arm boxes overlap the torso 3.1-3.4 cm in strikes
  (shoulder joint ~3.6 cm), by design (blocky joint).
- **Near plane:** engine default 10 cm (`BaseEngine.ini:361`; no project override). Acceptance: every first-person
  body surface ≥15 cm from the eye. A 5 cm near plane would be a separate, project-wide decision.
- **FOV and aspect (decided):** M1 draws the first-person body and tool at the world FOV, continuous with the floor.
  Unreal's first-person projection is an M4 trial for forearm/hand/tool only, with per-vertex alpha and a first-person
  FOV blended toward the world FOV as you look down; it must pass a seam test. Never apply first-person scale/FOV to
  legs and feet. Test vFOV 30/70/110 at 16:9, 21:9 and 4:3 (110 gives ≈137°, ≈147° and ≈125° horizontal).
- **Walls:** the box keeps the eye ≥30 cm from walls. First-person pull-back, presentation only, with the existing voxel
  clip (`FFoundationSimulation::Clip`) along the view: when free distance D < 60 cm, the hand target retracts by
  (60−D), up to 25 cm; under 35 cm the arm also lowers 30° (*proposed*). Strikes near walls are not altered in M1
  (measured). The world body is never pulled back.
- **View switching:** both bodies are evaluated from the same pose every frame while the body feature is on
  (`AlwaysTickPoseAndRefreshBones`; trivial at 22 bones), so there are no stale poses, offscreen skips or stale
  sockets on a switch. The world body's type flip happens in the same frame as the perspective change.
- **Third person stays an inspection camera.** The rear view sits on the eye's ray, so a centred crosshair would be
  exact there. The shoulder preview has 55/30 cm parallax: if playable third person is wanted later, draw the
  crosshair at the eye ray's hit point and show the outline through obstructions; targeting stays at the eye. Moving
  targeting to the camera ray would be a separate gameplay-rule change and is **not** part of this plan.

### 3.5 One render sample and ordering

- `FFoundationRenderSample {LastStep, Alpha, dt, feet, eye, view, body yaw}` is computed once in
  `AFoundationPawn::Tick`. Camera, animator, both bodies and the strike phase all read it.
- Strike phase = `(LastStep + Alpha − SwingStartStep) / 6`, using the pawn's alpha, not `StepAlphaSeconds`.
- Order is unchanged: simulation steps → camera → `Advance` (clocks first, then pose) → both mesh ticks (each with a
  pawn prerequisite) → worker evaluation completes before dependents → socket-attached tools update with their bones →
  `UFoundationInteractionView` (`TG_PostUpdateWork`).
- Remote/network: a remote copy would interpolate the same inputs from snapshots (feet, eye height, yaw/pitch, stance
  flags, swing start step and chain index, use kind/ticks, item ids, death ticks). Not claimed; no networking exists.

## 4. Export and import

### 4.1 Source selection

Export only from the focused candidate, gated on its SHA-256. Strike metadata from the polish folder's
`validation/s06_pickaxe.json`; everything else from **v001**'s `validation/` and `exports/UP_manifest_v001.json`,
never the polish folder's other JSONs. Each clip in the new manifest is marked `accepted-arm`, `inherited-v001` or
`placeholder`.

### 4.2 Export runner (new; the source is never saved)

- Folder: `C:\dev\output\unnamed-player-export-v002a-<date>\` with its own `scripts\export_up.py`, derived from
  `s07_export.py`.
- Arguments: `--source <focused.blend> --expect-sha256 26E585D5… --dest <empty new dir>`. It refuses a non-empty dest.
  No `rmtree`; never reads `up_config.MASTER`. Blender: `C:\dev\tools\blender-5.2.2\blender.exe -b --factory-startup`.
- Before export it asserts 66 `UP_*` actions with the manifest's names and frame counts and rest pose = manifest
  (≤1e-4 m); it clears the scene's leftover `RIG_UP_Pickaxe_StrikeA` frame-3 assignment and puts the skeleton at rest.
- Export conversion and settings identical to the proven s07 ones: in-memory centimetre copy, `FBX_SCALE_NONE`, global
  scale 1, axes −Z/Y, `add_leaf_bones=False`, primary/secondary bone axes Y/X, deform bones only, armature as a Null,
  forced start/end keys, bake step 1, simplify 0.
- **Name the export copy's armature object `Armature`** (M0 v002a finding, 2026-10-16). UE 5.8.3 drops a root joint's
  parent node named "armature" (case-insensitive) only when it sits directly under the FBX root (Interchange
  `FbxScene.cpp:940-943`, legacy `FbxMainImport.cpp:1122`, `:2621-2624`). s07's name `UP_Skeleton` came in as a 23rd
  root bone. Better Steve was already named `Armature`, hence its exact 56 bones.
- Outputs:
  - `SK_UnnamedPlayer.fbx`: armature + `UP_Body` + `UP_Head`, plus bone-parented empties `SOCKET_hand_r_grip` and
    `SOCKET_hand_l_grip` (and `SOCKET_head_eye` for debugging) at the manifest bone-space transforms. `fp_anchor` is
    dropped.
  - 66 × `A_UP_<clip>.fbx`: each armature + meshes (bind-pose carrier), take = clip name, frames 0..N, no empties.
  - `SM_UP_Pickaxe.fbx` (origin = grip) and the skins.
  - `UP_manifest_v002a.json`: per-file SHA-256, source hash, Blender 5.2.2 build, status per clip.
  - `export_report.json`.
- Checks inside the runner: raw FBX parse (UnitScaleFactor 1, 22 LimbNodes, local scale ≈1, one take per clip, no leaf
  bones, `SOCKET_*` nodes are leaf children of the right bone nodes); a Blender round trip (s08 pointed at the new
  folder: rest = manifest, drift recomputed, strike tip at frame 9 ≤0.01 cm); source hash identical before and after.

### 4.3 Unreal import (UE 5.8.3, Interchange)

Interchange is the active FBX path: `Interchange.FeatureFlags.Import.FBX` defaults to true
(`Engine/Plugins/Interchange/Runtime/Source/Import/Private/Fbx/InterchangeFbxTranslator.cpp:34-36`) and the project's
`Config/` doesn't override it. Better Steve's option-less import went through it with the same exporter settings and
came in at exactly 56 bones facing +Y.

- **New script** `Scripts/Editor/ImportUnnamedPlayer.py` (for the code lane; the M0 proof uses a script kept in the
  export folder). Like the existing importers it **verifies an existing folder and never replaces it**.
- FBX copies go to `Art/Characters/UnnamedPlayer/` (LFS) with `manifest.json` hashes; assets go to
  `/Game/Characters/UnnamedPlayer`, a new root alongside `PlayerBodyRoot` (`UnnamedProjectSettings`,
  `Scripts/Editor/ProjectPaths.py`).
- Explicit pipeline overrides; the script logs the effective properties read back from the installed pipeline classes:
  - **Mesh import:** new skeleton `SK_UnnamedPlayer_Skeleton`, combine skeletal meshes on (2 sections), import
    sockets on, no morph targets, no physics asset, **time-zero-as-bind-pose off**, convert scene on, convert scene
    units on, force front X off, uniform scale 1.0, no import rotation.
  - **Clip import:** animations only, skeleton = `SK_UnnamedPlayer_Skeleton`, **mesh, material and texture import
    off** (the clip FBXs carry meshes; otherwise 66 duplicate meshes), sample rate 60, the file's exported range, bone
    tracks on, root motion off.
- **Names:** `SK_UnnamedPlayer`, `M_UP_Body`, `M_UP_Head`, `T_UP_Skin` (Nearest, no mips, sRGB), `SM_UP_Pickaxe`.
  Clips `UP_*` with "Sneak" → "**Crouch**" (`Docs/NamingConventions.md`: crouch, not sneak); the mapping stays in the
  manifest. Look clips mesh-space additive on `UP_Look_C` frame 0 and `UP_CrouchLook_C` frame 0. Each asset gets a
  hash metadata tag.
- **Sockets:** Interchange creates them as mesh-only sockets and strips the `SOCKET_` prefix. Read back via
  `find_socket` + local transform against the manifest: ≤0.01 cm, ≤0.05°. On reimport, same-name sockets are
  **overwritten from the file** and sockets on vanished bones are removed; other names survive. So Blender owns the
  grip sockets; a hand edit in Unreal gets back-ported to the manifest. Fallback if Interchange misbehaves: a ~30-line
  editor-only C++ socket helper; a manual edit is the last resort. Runtime doesn't strictly need sockets: per-item
  grip data can attach to `hand_r` with a relative transform.

### 4.4 Proof first, then the batch (gates)

Proof set: mesh/rest pose, `UP_Idle`, `UP_Walk_F`, `UP_Crouch_FR`, `UP_Look_D` (+ `_C`), `UP_Pickaxe_StrikeA`, into a
throwaway `/Game/Characters/UnnamedPlayer_Proof`.

1. 22 bones, root bone `root`, hierarchy and names = manifest, **no extra armature root**, every bone scale 1 (±1e-4).
   (With socket import off, `SOCKET_` nodes would become bones; this gate catches that too.)
2. Bounds height 180 ±0.5 cm, soles 0 ±0.1 cm, next to a 100 cm block in a shot.
3. Ref pose = manifest (≤0.01 cm, ≤0.01°).
4. Faces +Y (toe caps at +Y in component space). With the existing `BodyYaw−90` it faces the view. No second rotation.
5. 2 sections with their material names; sockets read back.
6. Clips: play length = N/60 ±1e-4 s, N+1 keys, seam pose(N) = pose(0) (≤0.01 cm), root track static.
7. Raw vs compressed, every bone at every frame: max position error ≤0.05 cm and ≤0.1° (*proposed*). Planted ball drift
   on the *compressed* Walk_F/Crouch_FR ≤0.1 cm. StrikeA tool tip (socket × tip (0, 42.75, 26.5) cm) at frame 9
   within ≤0.1 cm raw and ≤0.5 cm compressed of the manifest target. Stricter compression settings for UP clips only if
   a gate fails.
8. Exactly one new skeleton asset; no redirectors.

Batch: the remaining 59 clips, gates 6-8 per asset; then delete the proof folder. Better Steve's folder is untouched.

### 4.5 Reimport and rollback

Never reimport over accepted assets. A changed rest pose means a new skeleton: move the folder away deliberately (the
Better Steve lesson: a reimport keeps the old skeleton's reference pose). An animation-only update reimports the clips,
then re-applies and verifies the additive settings and tags. Rollback: delete `/Game/Characters/UnnamedPlayer` and
`Art/Characters/UnnamedPlayer`; `foundation.Body.Set 0` keeps working throughout.

## 5. Animation composition and gameplay timing

**Extend the native proxy; no AnimBP.** One clock owner, headless unit tests, the clocks-before-pose ordering kept, no
binary graph to diff, and every needed operation exists in C++ (per-bone blend, mesh-space rotation blend,
`AccumulateMeshSpaceRotationAdditiveToLocalPose`, AnimationCore two-bone IK; an engine module, not a plugin). New pure
pose code goes in `FoundationBodyPose.*`. An AnimBP or Control Rig stays an option for later slope/foot polish.

```
Pawn (game thread): steps → RenderSample → camera → Animator.Advance (clocks, then pose)
  Base: locomotion [stand phase walk/sprint | crouch phase] · crouch transition · air (Start→Rise(hold)→Fall loop→Land Soft/Firm)
        · turn-in-place (metadata yaw applied once to the component) · placeholders (low / flight / death)
  Look: stand grid ⊕ crouch grid by CrouchAmount
  Action: {clip, time, weight, chain index} from the swing/use clocks
Proxy (worker), per body:
  1 base blend → 2 [FP] hips back + leg IK to base feet → 3 action layer (mask) → 4 look (head/neck; spine weight 0 during actions)
  → 5 [FP] chest anchor (spine_01/02) → 6 aim (spine_02 toward target) → 7 contact (arm IK, window) → 8 [FP] wall pull-back → out
```

Why this order holds: anything that moves the shoulders (hip shift, chest anchor, aim) runs before arm IK; contact runs
last so the tip lands where intended; first-person comfort (wall pull-back) can override cosmetic contact, but only in
the owner's copy; during actions, aim replaces look on the spine, so the two never stack.

Inputs: feet, eye height, view yaw/pitch, body yaw; velocity (step displacement); `bCrouched`/`CrouchAmount`, grounded,
flying, `bSwimming`/`bLowPose`, jumps, vz and landing speed; swing start step, repeat flag, chain index; use active,
hand, ticks, duration, kind; main/offhand item ids and their body support; death ticks; forward wall distance (first
person only); the authoritative target point (preview).

| Setting | Value (*proposed* unless from the contract) |
|---|---|
| Action mask | spine_01 0.5; spine_02, neck, head, clavicles, arms, hands 1; pelvis and legs 0; mesh-space rotation blend on the spine |
| Action blend | In: 0 s from Hold (frame 0 = frame 18 = shared link pose), else 0.05 s. Out: 0.10 s to Hold |
| Aim (chest, toward target) | Pitch −45°/+35°, yaw ±30° (turns handle the rest), rate ≤360°/s |
| Contact (accept band) | Target within 55-110 cm of the shoulder and ±35° of the aimed contact direction |
| Contact (window and limits) | Frames 6-12, peak at 9; hand ≤12 cm, wrist ≤15°. Outside the band: **aimed gesture**, effects at the authoritative target, never arm stretching; block reach of 450 cm is beyond any physical arm |
| Landing | Soft <10 m/s; Firm weight clamp((v−8)/4) (manifest proposal). A flat jump lands at ≈8.9 m/s, so Soft is the everyday landing |
| Turns | Standing still with \|view−body\| >50°: Turn45 if <95°, else Turn90; settle within 15°; movement interrupts with a 0.12 s crossfade, keeping the yaw reached |

**Locomotion:** real velocity and resolved stance drive it; normalized phase is kept across all 8 directions and
across walk/sprint. *New:* on a stance toggle while moving, carry the phase so the stepping foot doesn't swap during the
0.27 s blend (target ≤2 cm foot deviation). Stops, starts and reversals pass through the 40 ms velocity smoothing;
blocked movement gives zero displacement, so idle (no running in place). Root travel and jump height stay
gameplay-owned; errors are never hidden by changing speeds or the box.

**Strikes (accepted A/B/C kept as authored FK):**

- Start on the swing start step; phase from the shared render sample; clip time = phase × 0.30 s, so **frame 9 = step
  3**, the same tick as the existing hit sound/chip (`IsHitStep`).
- Order: A on the first swing of a hold, then B, A, C, A, B… A new press within a swing restarts at frame 0 with a
  0.05 s crossfade; the chain advances.
- Interruptions:

| Event | Behaviour |
|---|---|
| Target lost | The swing in progress finishes (≤0.3 s), then Hold |
| Tool switch | 0.08 s fade to the new item's presentation |
| Pause or focus loss | Steps stop, so the pose holds |
| Stance change or jump | The upper body carries on over the new legs |
| Death | Layer cleared at once |
| Respawn | `ResetBody` clears the chain |
| View switch | No restart (both bodies evaluate the same clip time) |

- Anticipation and approach stay intact: aim rotates the chest only; contact acts only around frames 6-12; arm
  rotations relative to spine_02 must match the clip within 0.5° outside that window.
- **Authority unchanged:** mining progress, break, drops, damage, placement and consumption stay in
  `FFoundationAuthority`. No anim notify ever drives gameplay. The 0.15 s contact delays nothing.

**Held items (migration):**

- Per-item grip data `{bone, relative transform, display}`. The pickaxe uses the manifest grip; other tools reuse it
  temporarily.
- **Temporary policy "LEGACY-HAND"** for items without body support (eat/drink, bow draw, shield raise, blocks,
  offhand): today's camera-pivot drawing in first person, unchanged; nothing in F5, as today; the body's arm on that
  side stays in its locomotion pose. Known artifact: looking down shows the body arm *and* the floating item. Named in
  the record and removed per item as clips arrive.
- No item is ever drawn twice: a body-held item hides its pivot copy.
- Support-hand IK (left hand on the handle) stays off, as in the accepted strikes (a two-handed B needed ~180° of
  wrist).

## 6. Coverage matrix and new-clip briefs

Classes: usable now, polish, adaptation, placeholder, missing, deferred. "Wiring" = runtime work still needed.

| Capability (gameplay today) | UP asset | Wiring | Class | Stage |
|---|---|---|---|---|
| Idle stand/crouch | `UP_Idle`, `UP_CrouchIdle` | rename | usable now | M1 |
| Walk ×8 | yes | rename | usable now; polish later (reads as jog; R↔BR boundary) | M1 / M4 |
| Sprint ×8 (forward only) | yes | rename | usable now | M1 |
| Crouch walk ×8 (54 f) | yes | lengths from assets | adaptation | M1 |
| Direction blends, walk↔sprint | – | existing | usable now; polish R↔BR | M1 / M4 |
| Starts, stops, pivots, reversals | none | velocity smoothing | deferred (add only if measured pops) | M4 |
| Crouch interruption and toggle-back | transitions 16 f | existing ease | usable now (+ phase carry) | M1 |
| Jump / edge fall / Rise / Fall / Soft / Firm | 5 clips | new air model (Loop→Fall, Land→Soft in M1) | adaptation | M1 / M2 |
| Moving landing | – | existing fade | usable now | M1 |
| Turn in place 45/90 stand/crouch | 8 clips | missing | adaptation | M2 |
| Look stand / crouch grids | 18 | second grid missing | adaptation + polish (corners 9.5 cm; clamp \|yaw\| ≤75° when \|pitch\| >60°, proposed) | M1 / M2 |
| Swim pose (60 cm box) | `UP_Low_Pose` | missing | placeholder | M2 |
| Swim strokes, tread water, entry/exit | none | missing | missing | M3 |
| Creative flight | `UP_Flight_Placeholder` | plays air loop | placeholder | M2 / M3 |
| Death / respawn | `UP_Death_Placeholder` | body stands during death screen | placeholder → missing | M2 / M3 |
| Empty-hand first-person arm (vanilla shows it) | none (IK presentation) | missing | open choice (recommend yes) | M2 |
| Pickaxe hold / strikes A-B-A-C | 4 (accepted strikes) | missing | usable now (A in M1, chain in M2) | M1 / M2 |
| Other tools | reuse with grip | missing | adaptation | M2 |
| Punch / mining by hand, melee and whiff | reuse strikes | missing | adaptation | M2 |
| Place / use / bucket | none (today: 0.25 s first-person press swing) | missing | missing | M3 P1 |
| Eat / drink | none | LEGACY-HAND | missing | M3 P1 |
| Shield (19b) | none | LEGACY-HAND | missing | M3 P2 |
| Bow (19c, not landed) | none | LEGACY-HAND | missing | M3 P2 |
| Offhand items | grip socket | LEGACY-HAND | adaptation | M2 |
| Tool switch / equip (instant today) | none | – | deferred (P2) | M3 |
| Armor on the body, helmet policy | none | missing | deferred (visibility policy defined now) | M4 |
| Ladder / climb (B4b, not landed) | none | missing | missing | M3 P2 |
| Hurt (camera tilt today) | none | – | deferred (P3 additive) | M4 |

**New clips** (60 fps; loops have identical keys 0 = N, last N/60 s, no dwell). Each must be reviewed in first person,
F5 rear and F5 front at full speed and 1/3 speed, at vFOV 70 and 110. In first person none may bring a first-person
body surface within 15 cm of the eye. Old Better Steve action clips are references only, not assets.

| Clip | Priority / purpose | Frames | Event | Layer / mask | Entry / exit · interruption |
|---|---|---|---|---|---|
| `UP_Use_A` | P1: place, use, bucket | 18 (0.30 s, the 6-step clock) | touch at 9 = step 3 (cosmetic) | right arm + 30% spine_02 | from/to Hold or idle arm; restart: 0.05 s crossfade |
| `UP_Eat_In` / `_Loop` / `_Out` | P1: eat, drink | 6 / 12 loop / 6 | bob every 12 f = 4 ticks (matches effect ticks) | right arm, neck small | driven by `UseTicks`; cancel: 0.1 s out. First person: the hand reaches bottom centre, not the camera |
| `UP_Death` | P1 | 60 (1.0 s = 20 ticks), then hold | – | full body; pelvis rotates; root static | starts on death tick; first-person body hidden (camera death roll unchanged) |
| `UP_Swim` / `UP_Tread` | P2 | 36 loop / 48 loop | strokes on phase 0 / 0.5 | full | swim pose and in-water state; 0.15 s blends |
| `UP_Shield_Raise` / `_Hold` / `_Lower` | P2: offhand | 6 / 1 / 6 | raised within 5 ticks (block delay) | left arm + 30% spine | `UseTicks`; axe disable → lower |
| `UP_Bow_Draw` / `_Hold` / `_Release` | P2, after 19c lands | 60 (time = UseTicks/20; confirm against 19c) / 1 / 9 | release frame 0 | both arms, chest | charge-driven, not time-driven |
| `UP_Equip_Lower` / `_Raise` | P2 | 9 / 9 | – | arms | slot change; never delays the item switch (cosmetic) |
| `UP_Climb` | P2, after B4b lands | 48 loop | hand/foot contacts at block spacing | full | phase from vertical travel |
| `UP_Fly_Hover` / `_Move` | P3 | 60 / 36 loop | – | full | replaces the flight placeholder |
| `UP_Hurt_Add` | P3 | 6 | – | additive spine | on hurt tick |

## 7. Staged work, acceptance, rollback

**M0: snapshot, export, import proof (no game code; decided: start now).**
- Hash the dirty/untracked files in `Unnamed-use` and `Unnamed-body`, the Better Steve assets and the three blends.
- Run the export runner (§4.2), then the proof import (§4.4) in a blank content-only UE 5.8.3 sandbox project,
  `C:\dev\sandbox\fpbody-m0\` (PM 5's condition: no worktree, no build). The game import happens later on the M1 lane.
- Gates: §4.2 checks and §4.4 gates 1-8 all pass; then the batch.
- Stop: an extra root or bone-scale problem, facing other than +Y, compressed errors above the gates.
- Rollback: delete the proof folder and the new export folder.

**M1a: body live in both views, no tools (after survival QC/final merge).**
- Touches: `FoundationBody.*` (clip table per body set, UP air and grids); new `FoundationBodyPose.*` (first-person
  corrections, leg IK); `FoundationBodyAnimInstance.*` (view mode); `FoundationPawn.*` (two components, render
  sample, visibility; hook only); settings and paths; tests, probe, `Docs/Foundation/Architecture.md`, a new record.
- Switches: `foundation.Body.Set 0|1`, `foundation.Body.FirstPerson 0|1`.
- Acceptance:

| Check | Target |
|---|---|
| F5 parity | `-Suite body` expected-clip checks green on UP |
| Planted drift | ≤0.5 cm target (limit 1 cm) at 30/60/120 fps and 16.7-33 ms jitter, in both views |
| Head in first person | 0 head-texel pixels at pitch −90…+90, standing and crouched |
| Feet in first person | toe caps visible at pitch ≤−75° standing idle, vFOV 70 and 110 |
| Clearance | ≥15 cm from the eye over {stand, crouch, low} × pitch {−90, −60, −30, 0, 30, 60, 90} × yaw offset {−60, 0, 60} |
| Shadow | one body shadow, head included, in the owner's first person at low sun |
| View switching | 20 F5 cycles mid-jump: never both or neither body; no reference-pose frame |
| Simulation unchanged | per-step feet/eye/velocity bit-identical with `Body.Set 0` vs `1` on a scripted 30 s path |
| Perf | ≤0.10 ms per body evaluation; ≤0.3 ms frame delta vs the Better Steve baseline (median of 3 runs, same path; *proposed*) |

- Stop: crouched neck clearance stays under 15 cm. Report with numbers and shots; no improvising.
- Rollback: the switches; the lane is unmerged.

**M1b: StrikeA through the real hand.**
- Touches: new `FoundationBodyGrip.*` (grip data, tool components); a hook in `UpdateHeldItem` to hide body-held items;
  the action layer and aim v1; the render-sample strike phase.
- Acceptance: socket read back ≤0.05 cm; the strike starts on the swing start step; the frame-9 pose is drawn in the
  frame that contains step start+3 (±1 frame at 30 fps); no doubled pickaxe in either view; 60 fps and 1/3-speed
  first-person and F5 videos matching the approved arm MP4 (arm-local error ≤0.5°); mining results and timings
  identical with the body on or off.
- Rollback: `foundation.Body.Set 0` restores today's pivot pickaxe exactly.

**M2: complete existing library.** Rise/Fall/Firm, turns, crouch grid, the A-B-A-C chain, Hold, contact and gesture,
placeholders (swim, flight, death), other tools, offhand static hold, empty-hand presentation. Re-run the M1 matrix plus
contact over walk/sprint/crouch hips (target on hit point ≤1 cm inside the band), aim extremes, edges/steps/slopes, low
ceilings, pause/focus loss/respawn.

**M3: new clips** (P1 → P2 → P3) in a new versioned Blender source that keeps the accepted arms; each clip gets its own
gate and review videos.

**M4: polish and optimization.** First-person projection for forearm/hand/tool behind a seam gate; look-corner clamp,
gait boundary, turn thresholds; armor and headwear followers; bounds extension (+60 cm for tool reach, so strike shadows
aren't culled); a perf pass.

Static checks, Blender round trips, native unit tests, editor playback, packaged runtime and Jesus's comfort sign-off
are separate claims. An importer "success" or a plausible screenshot closes nothing.

## 8. M0 kickoff prompt (for a fresh chat)

> Unnamed M0: export the accepted UP candidate and prove its Unreal import (assets only, no game code). Read
> `C:\dev\output\unnamed-fp-tp-integration-plan-20261015\PLAN.md` first (sections 0, 2.4, 4 and 7/M0), then the hard
> contract in `C:\dev\output\unnamed-player-motion-polish-v002-20261015\FIRST-THIRD-PERSON-INTEGRATION-PROMPT.txt`.
> Jesus approved: planted-feet first-person body, world FOV first, asset proof now with code after survival QC.
>
> 1. Snapshot first: hash the dirty/untracked files in `C:\dev\projects\Unnamed-use` and `C:\dev\projects\Unnamed-body`,
>    `Content/Characters/BetterSteve` and the three UP blends; save the list in the new export folder. Change none of
>    them.
> 2. Export: create `C:\dev\output\unnamed-player-export-v002a-<yyyymmdd>\` (must not exist) with
>    `scripts\export_up.py` derived from the v001 `s07_export.py` per PLAN §4.2 (explicit `--source`,
>    `--expect-sha256 26E585D5676D5A8A18A6E6A2FAE5D682DF3470FA7B83A01972CFAE138D4D22C7`, empty `--dest`; no rmtree,
>    never `up_config.MASTER`, never save the source; add the `SOCKET_hand_r_grip`, `SOCKET_hand_l_grip`,
>    `SOCKET_head_eye` empties to the mesh export only). Run headless with `C:\dev\tools\blender-5.2.2\blender.exe -b
>    --factory-startup`. Produce the manifest v002a, raw-parse report and a Blender round trip; confirm the source hash
>    is unchanged.
> 3. Import proof (Survival PM 5's GO and conditions, PLAN §0): create a blank **content-only** UE 5.8.3 project in
>    `C:\dev\sandbox\fpbody-m0\` (a minimal `.uproject` with `PythonScriptPlugin` enabled; no C++ modules, no build).
>    First read the game's `Config/*.ini` from `C:\dev\projects\Unnamed-survival` **read-only** for anything affecting
>    import or animation compression (none for Interchange was found at audit) and mirror it in the sandbox config.
>    Keep the proof importer script in the export folder and run it with `UnrealEditor-Cmd.exe <absolute uproject>
>    -run=pythonscript "-script=<absolute path>"` through PowerShell `Start-Process` (not `-Wait`), at BelowNormal
>    priority, with `-abslog` into a fresh evidence folder in the export folder. Before **every** editor run, check
>    `pwsh -NoProfile -File C:\dev\tools\shift-managers\scripts\quiet_window.ps1 -Status` and do not start while a
>    window is clearing or open; run one editor at a time; never touch other UnrealEditor processes. Import the proof
>    set into `/Game/Characters/UnnamedPlayer_Proof` with the explicit Interchange settings of PLAN §4.3, log the
>    effective pipeline properties, and check gates 1-8 of §4.4 (raw and compressed; the gate-2 picture is optional in
>    M0, the numeric bounds are mandatory). If all pass, batch the other 59 clips with gates 6-8 each into
>    `/Game/Characters/UnnamedPlayer` (sandbox only), then delete the proof folder.
>    Never write in any `C:\dev\projects\Unnamed*` folder (Unnamed-play is Jesus's play copy) or the shift-managers files.
> 4. Stop and report (no workarounds) on: an extra root bone, bone scale ≠ 1, facing other than +Y, sockets missing or
>    off by more than the gate, compressed errors over the gates, duplicate skeletons/meshes. A classifier refusal is a
>    stop-and-report.
> 5. No game code changes, no commits or pushes, no overwriting of any existing evidence, export or package. Report the
>    hashes, the gate table, the asset list and the evidence paths, then stop for Jesus's review.
