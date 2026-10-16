# M0 report v002b (armature renamed to "Armature"), 2026-10-15

**Result: STOPPED on step 5 (single-pose clips).** The armature fix works: gates 1-5 and 8 pass, and the four
multi-frame proof clips pass gates 6-7 raw and compressed. But the single-frame clips do not import reliably.
`UP_Look_D` came in 5.385 cm off the Blender round trip. As instructed I did not change the export; the batch, the
pickaxe import and the proof-folder deletion were not run.

## Export v002b (only change vs v002a: the export copy's armature object is named "Armature")
- Source SHA 26E585D5…4D22C7, identical before and after the export and after the round trip.
- All raw gates pass. New gates: the `Armature` Null sits at the scene root in the mesh FBX and in all 66 clip FBXs.
  Sockets are leaf children of hand_r, hand_l and head.
- Round trip:
  - rest pose 4e-7 m
  - sockets ≤0.00002 cm
  - drift 0.08 cm
  - strike tip at frame 9: A 0.0001, B 0.0004, C 0.0018 cm
  - pose-0 dump for single-pose clips: `evidence/roundtrip_v002b.json`

## UE proof B (`/Game/Characters/UnnamedPlayer_ProofB`; fresh skeleton, run `ue_proofB_20261015_233751.log`)
| Gate | Result | Numbers |
|---|---|---|
| 1 | pass | 22 bones, root `root`, names and hierarchy match, scale deviation 0 |
| 2 | pass | 180.0000 cm, soles −0.000002 cm (picture skipped) |
| 3 | pass | joints ≤0.00004 cm (SX +1), parent-relative rotation ≤0.0013° |
| 4 | pass | +Y (ball − foot = +5.0 cm), no import rotation |
| 5 | pass | 2 sections (M_UP_Body/M_UP_Head); hand_r_grip / hand_l_grip / head_eye ≤0.000007 cm, axes ≤0.000015° |
| 6 Idle / Walk_F / Crouch_FR / StrikeA | pass | 121/37/55/19 keys, lengths 2.0/0.6/0.9/0.3 s (±2.4e-8), 60 Hz, seam ≤6e-17 cm, root static, scale 1 |
| 7 same clips | pass | ACL compression: position 0.0 cm, rotation ≤4.2e-6°. Compressed drift Walk_F 0.078 cm, Crouch_FR 0.049 cm. StrikeA tip at frame 9 = 0.0003 cm (raw and compressed) |
| 6 Look_C | pose matches (≤0.00004 cm) | but 3 keys and length 0.0333 s, payload error logged |
| 6 Look_D | **FAIL** | pose 5.385 cm off the round trip; 3 keys, length 0.0333 s |
| 7 Look_C/D | n/a | compressed identical to raw |
| 8 | pass | 1 skeleton, 1 skeletal mesh, 0 redirectors (ProofB scope) |

## Single-frame clips (step 5)
For both Look_C and Look_D, Interchange logs four things:
- the ensure `RangeStop > RangeStart` (`FbxAnimation.cpp:1049`)
- the ensure `FileExists(AnimationPayloadFile)` (`InterchangeFbxTranslator.cpp:719`)
- "Cannot fetch FBX animation bake transforms payload because the bake range is invalid"
- "unexpected number of Baked Transforms"

The bone payload is therefore not loaded from the clip's animation. Look_C's match is not proof of a working path.
The other 18 single-pose clips (the remaining looks, `UP_Pickaxe_Hold`, `UP_Low_Pose`) go through the same path, so I
didn't import them.

Proposed fallback (yours to approve): export the single-pose clips with 2 identical keys (frames 0..1, length 1/60
s, additive reference frame 0). Then re-run the proof with Look_C and Look_D.

## Skin texture
- Imported `T_UP_Skin_64.png` (from `fbx/`) as `T_UP_Skin` (64×64, Nearest, no mips, sRGB) with the legacy
  AssetImportTask.
- Assigned it to `DiffuseColorMap` on M_UP_Body and M_UP_Head with
  `MaterialEditingLibrary.set_material_instance_texture_parameter_value`, and read the assignment back.
- Both are MaterialInstanceConstants of `/InterchangeAssets/Materials/FBXLegacyPhongSurfaceMaterial`, with no
  static switches. The setter returned False, but the readback shows `T_UP_Skin` assigned.

## Other
- The 55 snapshot hashes are unchanged.
- A separate UnrealEditor process was running at the end (not mine; not touched).
- The v002a proof folder still sits in the sandbox (deletion is gated on a full pass).
