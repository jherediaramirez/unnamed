# M0 report v002c (2-key pose clips), 2026-10-16

**Result: STOPPED at the pose-clip checks of proof C.** The 2-key export clears the "bake range is invalid" error,
but Interchange still imports each pose clip as 3 keys / 0.0333 s and warns "unexpected number of Baked
Transforms". The batch, the pickaxe import and the proof-folder deletion were not run.

## Export v002c (`fbx/`; the only change from v002b is the 2-key pose clips)
- Source SHA 26E585D5…4D22C7 is unchanged. The source was opened four times (export, round trip, two pose probes)
  and never saved.
- All raw gates pass. The manifest gives the 20 pose clips frames 1, keys 2, seconds 0.016667, `pose_clip: true` and
  the note "pose clip: runtime samples t=0".
- Round trip:
  - every pose clip has keys 0 and 1 identical (0.0 m) and equal to the v002b round-trip pose (0.0 m)
  - the other 46 clips give round-trip values identical to v002b
  - drift 0.08 cm; strike tips 0.0001 / 0.0004 / 0.0018 cm
- All file hashes differ from v002b. The byte diffs have the same file sizes with scattered changes (FBX IDs and
  timestamps), and the round-trip numbers match.

## Proof C (`/Game/Characters/UnnamedPlayer_ProofC`, fresh skeleton; mesh + 6 proof clips + all 20 pose clips)
- Gates 1-5 and 8 pass, with the same numbers as v002b: 22 bones, 180.0000 cm, joints ≤0.00004 cm, +Y, sockets
  ≤0.000007 cm, 1 skeleton, 0 redirectors.
- Idle, Walk_F, Crouch_FR and StrikeA pass gates 6-7:
  - ACL compressed vs raw: 0.0 cm, ≤4.2e-6°
  - drift: Walk_F 0.078 cm, Crouch_FR 0.049 cm
  - StrikeA tip at frame 9: 0.0003 cm
- The skin (T_UP_Skin → DiffuseColorMap) is assigned on both materials.
- All 18 looks were set to AAT_RotationOffsetMeshSpace, ABPT_AnimFrame, frame 0, with base UP_Look_C or
  UP_CrouchLook_C. Readback OK (`additive_ok` true).
- **Pose clips FAIL gate 6:** all 20 have 3 keys (not 2) and a length of 0.0333 s (not 0.0167 s). The log scan
  (`evidence/ue_proofC_logscan.json`) shows "Animation Payload … has unexpected number of Baked Transforms" for every
  pose clip, plus one ensure `AnimationTransformPayload.Transforms.Num() == BakeKeyCountForAnimationPayload`
  (`InterchangeAnimSequenceFactory.cpp:579`); an ensure reports only once per session. No "bake range is invalid"
  errors remain.
- Pose at t=0 vs the round trip:
  - C, L and R looks, Low_Pose and Pickaxe_Hold: ≤0.00008 cm
  - all D looks: 5.385 cm
  - all U looks: 4.031 cm
- **Diagnosis (read-only probe, `evidence/pose_source_vs_roundtrip.json`):** the source blend's own evaluated pose
  differs from the Blender round trip by the same 5.385 / 4.031 cm, at the `head` joint, in exactly the ±90° pitch
  clips. So the reference is the problem there. Blender's FBX *re-import* distorts the ±90° pitch poses (likely an
  Euler / gimbal issue), and Unreal most likely matches the source. This is inferred from the equal magnitudes; I did
  not save per-bone Unreal positions. The ≤0.001 cm check should compare against the source pose, not the round trip.
- My rotation sub-check (≤0.01°) is not reliable: I compared quaternion angles without normalizing (350° vs 10°).
  It needs a fix before it can be quoted.

## Decisions needed
1. Pose clips: accept 3 keys / 2-frame length (the runtime samples t=0), or try another export variant? One idea:
   check whether 3 identical keys over frames 0..2 removes the warning.
2. Use the source's evaluated pose as the pose-clip reference instead of the Blender round trip.

## Incidents
- A copied launcher still carried the v002b date. The first proof C attempt failed at script import (no assets) and
  the editor created a stray `C:\dev\output\unnamed-player-export-v002c-20261015\evidence\` holding only its log.
  I moved the log to `evidence/failed_run_wrong_path/`, deleted the empty stray folder, and fixed the paths.
- Another UnrealEditor process (not mine, not touched) was running during my proof C run. The quiet window was
  clear.
- Snapshot: 54 of 55 hashes are unchanged. `Unnamed-use` HEAD moved from 57fd0938; I only ran `git rev-parse`
  there, so another session committed.
