# Unnamed Player v001 (Blender-first)

The new player character and animation library for *Unnamed*, built and validated in Blender 5.2.2. No Unreal changes.
Start with **`HANDOFF.md`** (what was built, the evidence, limits, Unreal plan).

```
source/       original GLB + byte-exact original texture + PROVENANCE.md (CC-BY-4.0, Bully Magraptor)
textures/     T_UP_Skin_64.png (lossless), T_UP_Skin_1024.png (nearest), preview grid/stone
blend/        UP_Player_master_v001.blend  (editable: UP_EXPORT skeleton+meshes, UP_RIG control rig, props, guides)
              UP_Player_review_v001.blend  (watch everything: open, press Space; markers name each clip)
exports/      SK_UnnamedPlayer.fbx, anims/A_UP_*.fbx (66), UP_AllClips.fbx, SM_UP_Pickaxe.fbx, UP_manifest_v001.json
validation/   measured JSON per stage (audit, build, rig tests, locomotion, air/turns, look, first person, pickaxe,
              round trip)
previews/     sheets (s01..s07), MP4 (locomotion, air, turns, stand<->sneak), GIF (pickaxe AAAA vs ABAC, 3p/1p);
              iterations/ = earlier strike drafts; _tmp/ = raw frames
scripts/      up_*.py libraries, s00..s08 stage scripts, run_all.ps1, tools/ (sheets, textures, one-off patches)
logs/         run logs
```

Rebuild everything: `pwsh -File scripts\run_all.ps1` (add `-Preset A_uniform|C_head36` to try another proportion,
`-SkipPreviews` for a fast data-only run). Hands default to vanilla arm ends (`box`); set `UP_HANDS=pixel` (hidden pixel
fingers) or `UP_HANDS=blocky` (palm/fingers/thumb) before running to build an alternative style
(comparison: `previews\s10_hands_compare.png`).
