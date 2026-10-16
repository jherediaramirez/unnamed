# Source provenance — Unnamed Player v001

| Item | Value |
|---|---|
| Source file | `minecraft_steve_rigged.glb` (copied unchanged from `C:\dev\output\unnamed-player-review-20261015\`) |
| Size / SHA-256 | 661,040 bytes / `88E80A7E1D993092C4143C62EF71505F22B292AECED58A1139368FF3817CA961` (re-verified 2026-10-15) |
| Drive source | https://drive.google.com/file/d/1VhF4KGyA4OLhptpvxvyE4kVKOksxt64f/view |
| Title / author | "Minecraft Steve (Rigged)" by **Bully Magraptor** (https://sketchfab.com/irex6510) |
| Original URL | https://sketchfab.com/3d-models/minecraft-steve-rigged-fbe1d1fab86d47b09171608103af3b2d |
| License | **CC-BY-4.0** (http://creativecommons.org/licenses/by/4.0/) — attribution required in credits |
| Generator | Sketchfab-16.85.0 (glTF 2.0, `KHR_texture_transform`) |
| Embedded texture | 4096×4096 baseline JPEG, 484,578 bytes, SHA-256 `68104B7C0C75F65937E117EE558FD91A78B3666FD1F665CE31F1026118FEBC2F`, extracted byte-exact to `original_skin_texture.jpg` |

## Audit (s00, `validation/s00_source_audit.json`)
- 6 skinned meshes (head, torso, 2 arms, 2 legs; one quad per skin pixel, each box face its own grid), 1 skin with 24 joints
  (incl. 4 `*_end` helper joints), generic names (`Bone_002_1` weights the head), no animations, 1 material.
- Non-character objects: `Icosphere` (bone display shape only, in `glTF_not_exported`) and 10 hierarchy empties.
- Evaluated height 8.000002 m; soles Z −0.180509; top Z 7.819493; normalisation 1.8 / 8.000002 = 0.2249999.
- Faces +X with the standard Minecraft 64×64 UV layout (verified per face, `s00b_uv_layout.json`; arm bottoms are
  rotated 180° in the source, kept). Second-layer (hat/jacket) regions are black/unused.
- The JPEG is a 64× upscale whose 8×8 DCT blocks align with skin pixels: sampling texel centres reproduces the skin
  exactly (max deviation 2/255 at block corners).

## Modifications (all scripted, `scripts/`)
- Boxes rebuilt on the source pixel grid with the source UV layout; proportions preset `B_head40` (head 40 cm cube,
  body/limb pixels 5.0 cm wide × 5.833 cm tall, height 1.80 m) instead of the uniform 0.225 scale.
- Arms keep the original box (hands = the box's end block, bent at a new wrist joint; blocky palms/fingers/thumbs
  were built, reviewed and reverted, kept only as an option). Added toe caps whose faces sample existing shoe pixels
  (no new pixel art). Derived lossless `textures/T_UP_Skin_64.png` and nearest-upscaled `T_UP_Skin_1024.png`.
- New 22-bone skeleton, procedural weights, separate control rig, and all animation are new work.
- The original GLB and JPEG are preserved unmodified in this folder.
