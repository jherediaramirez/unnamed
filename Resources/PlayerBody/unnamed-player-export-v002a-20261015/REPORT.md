# M0 report: UP v002a export + Unreal import proof (2026-10-15)

**Result: STOPPED at proof gate 1.** Unreal (5.8.3, Interchange) kept the FBX armature node `UP_Skeleton` as an
extra root bone: 23 bones, with `root` under `UP_Skeleton`. As instructed, I stopped without a workaround. The
batch was not run and the proof folder was not deleted.

## Cause (confirmed read-only)
The export copies s07's object naming: the armature object is called `UP_Skeleton`. Better Steve's exporter names
it `Armature` (`better-steve/scripts/build_better_steve.py:643`: `"Armature" -> UE drops the node and roots at bone
"root"`). That name is why Better Steve came in at exactly 56 bones with the same settings.
**Proposed fix (needs approval):** in `export_up.py`, rename the export copy's armature object to `Armature` (one
line), re-export to a new folder (v002b), and re-run the proof.

## Export (Blender 5.2.2 d13f752e3b9c, headless, source never saved)
- Source `UP_Player_arm_polish_v002.blend` SHA-256 26E585D5…4D22C7, the same before and after the run (also checked
  after the round trip).
- Assertions passed: 66 UP_* actions with the manifest's names and frame counts; rest pose deviates 0 m from the
  v001 manifest; the leftover rig action `RIG_UP_Pickaxe_StrikeA` (scene frame 3) was cleared and the pose is at rest.
- Raw FBX gates all passed:
  - UnitScaleFactor 1
  - 22 LimbNodes, local scale deviation ≤6e-7
  - no leaf bones
  - one take per clip, named after the clip
  - `SOCKET_hand_r_grip`, `SOCKET_hand_l_grip` and `SOCKET_head_eye` are leaf Null children of hand_r, hand_l and
    head, in the mesh FBX only
- Blender round trip:
  - rest pose deviation 4e-7 m
  - root motion 0, bone scale deviation 1.4e-6
  - no frame-count mismatches
  - maximum planted drift 0.08 cm
  - sockets ≤0.00002 cm
  - strike tip at frame 9: A 0.0001, B 0.0004, C 0.0018 cm
- Clip status: 3 accepted-arm, 60 inherited-v001, 3 placeholder.

## Unreal proof (sandbox `C:\dev\sandbox\fpbody-m0`, run `ue_proof_20261015_230621.log`)
| Gate | Result | Numbers |
|---|---|---|
| 1 skeleton | **FAIL (stop)** | 23 bones, root `UP_Skeleton` (extra), local scale deviation 0 |
| 2 bounds | pass | height 180.0000 cm, soles −0.000002 cm (picture skipped, it is optional) |
| 3 ref pose | pass | joint positions ≤0.00004 cm (SX=+1 fit; mirrored fit gives 60 cm), parent-relative rotation ≤0.0013° |
| 4 facing | pass | +Y: ball − foot = +5.0 cm on both sides; no import rotation |
| 5 sections/sockets | pass | 2 sections, M_UP_Body/M_UP_Head; sockets imported with the `SOCKET_` prefix stripped, position error ≤0.000006 cm, axes ≤0.00002° (Blender +Y → UE −Y, +Z → +Z) |
| 6/7 clips | not measured | my gate code called a missing API (`AnimationLibrary.get_frame_rate`); fixing it needs another editor run, which the stop rule forbids |
| 8 assets | pass | 1 skeleton, 1 mesh, 0 redirectors |

Other findings:
- The 6 clips imported with the correct names (`UP_Sneak_FR` → `UP_Crouch_FR`) and no duplicate meshes.
- The single-frame `UP_Look_C` and `UP_Look_D` raised a handled Interchange ensure: `RangeStop > RangeStart`
  (`FbxAnimation.cpp:1049`).
- The embedded texture was not imported ("couldn't retrieve a payload"). The materials came in as
  MaterialInstanceConstants with no texture.
- Effective pipeline and translator properties are logged in `evidence/ue_proof_20261015_230631.json`.

## Untouched
The 55 snapshot hashes are unchanged (`snapshot_before.json`). There were no writes outside this folder and the
sandbox, and no commits.
