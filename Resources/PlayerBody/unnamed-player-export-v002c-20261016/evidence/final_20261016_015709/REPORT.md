# M0 final: UP v002c library in the sandbox (2026-10-16)

**Result: DONE.** Every gate passed, with two check-definition corrections listed below. The final library is in
`C:\dev\sandbox\fpbody-m0` under `/Game/Characters/UnnamedPlayer`. The Proof, ProofB and ProofC folders are
deleted.

## Inputs
- The v002c FBXs were reused unchanged (`../../fbx/`, manifest `UP_manifest_v002c.json`, SHA 1DB9A7E3…).
- Pose-clip import fix, per `C:\dev\output\up-pose-clip-investigation-20261016\REPORT.md`: the 20 `pose_clip`
  entries use `animation_range = SET_RANGE`, `frame_import_range = 0..2` and
  `custom_bone_animation_sample_rate = 60`. The 46 moving clips stay on Source Timeline. The 30 Hz override stays off.
  Effective properties are logged in `ue_final_*.json` (`pipeline_pose_clip_effective`).
- Oracle: `source_poses.json`, copied unchanged from the investigation (SHA A9F5B0BD…; evaluated from the source
  blend 26E585D5…). The comparison method is copied into `scripts/final/ue_m0_lib.py` (`source_compare`):
  - positions (x, −y, z)·100
  - R_expected = C·R_src_pose·inv(R_src_rest)·C·R_UE_rest, with C = diag(1, −1, 1)
  - normalized quaternions, shortest arc
  - rest calibration 0.0000176 cm

## Gates (final folder)
| Gate | Result | Numbers |
|---|---|---|
| 1 skeleton | pass | 22 bones, root `root`, names and hierarchy match, bone scale 1 |
| 2 bounds | pass | 180.0000 cm, soles −0.000002 cm (picture skipped, optional) |
| 3 ref pose | pass | joints ≤0.0000435 cm, parent-relative rotation ≤0.0013° |
| 4 facing | pass | +Y, no import rotation |
| 5 sections/sockets | pass | 2 sections, M_UP_Body/M_UP_Head; hand_r_grip, hand_l_grip, head_eye ≤0.000007 cm |
| 6 timing | 66/66 | moving clips: N+1 keys, N/60 s, 60 Hz, root static, scale 1; loops seam ≤0.01 cm |
| 6 pose clips | 20/20 | 3 imported keys over 0.0333 s (source: 2 keys, 1/60 s); 22 bone tracks each; every key, raw and compressed, vs source ≤0.0000784 cm / ≤0.0000852° |
| 7 compression | 66/66 | ACL raw vs compressed ≤0.0 cm / ≤4.8e-6°; compressed planted drift ≤0.080 cm (Walk/Crouch, 16 clips); StrikeA/B/C tip at frame 9: 0.0003 / 0.0003 / 0.002 cm |
| Additive | 18/18 | AAT_RotationOffsetMeshSpace, ABPT_AnimFrame, frame 0; base UP_Look_C (standing) or UP_CrouchLook_C (crouch); the reconstructed full pose matches the source ≤0.0000784 cm / ≤0.0000852° |
| 8 assets | pass | after cleanup, /Game holds 1 Skeleton, 1 SkeletalMesh, 1 StaticMesh, 66 AnimSequence, 2 MaterialInstanceConstant, 1 Texture2D, 0 redirectors, nothing outside the final folder |
| Pickaxe | pass | origin inside the bounds (grip); tip to surface 0.000007 cm at (0, −42.75, 26.5) in UE axes, which is the Blender-frame (0, 42.75, 26.5); the socket readback maps tool +Y to UE −Y |
| Skin | pass | `T_UP_Skin` (64×64, Nearest, no mips, sRGB) → `DiffuseColorMap` on both MICs (FBXLegacyPhongSurfaceMaterial parent), read back |

**Log scan** (`ue_final_logscan.json`): 69 imports. There were zero payload-count warnings, range errors or ensures
on any clip. The only finding is the known embedded-texture payload error on `SK_UnnamedPlayer.fbx`; the separate
`T_UP_Skin` replaces that texture.

## Check-definition corrections (made openly, no asset change)
1. Gate 6 seam: my script required pose(N) = pose(0) on every clip. The contract applies that only to loops. The 7
   one-shot clips (Death_Placeholder, Jump_Start, Jump_Rise, Land_Soft, Land_Firm, StandToSneak, SneakToStand)
   failed only that test, and they pass every other gate-6 test. `scripts/final/regate_offline.py` re-scored them
   into `ue_final_*_regated.json`; the original JSON is kept.
2. Pickaxe tip: my first test used the nearest vertex, which gave 3.65 cm = √(2.4² + 2.75²). The tip is the centre of
   the pick's end face, not a vertex. The cleanup run measured point-to-triangle distance (0.000007 cm) before
   deleting the proof folders.

## Evidence (this folder)
- `ue_final_20261016_015814.log` and `ue_final_20261016_015825.json` (+ `_regated.json`), `ue_final_logscan.json`
- `ue_cleanup_20261016_020006.log` and its JSON
- `final_asset_hashes.json` (72 files under `Content/`)
- `source_poses.json`
- Scripts in `../../scripts/final/`

## Runs and rules
- Two editor runs (final, cleanup). The quiet window was clear before each, no other UnrealEditor process was
  running, and both ran at BelowNormal.
- No Unnamed* or shift-managers writes, no commits.
- Snapshot: 54/55 unchanged. `Unnamed-use` HEAD is now ed35e737 (another session's commits; only read here).
