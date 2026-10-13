# Held light and glow lichen: handoff (2026-10-13)

Branch `feature/voxel-held-lichen` (worktree `C:\dev\projects\Unnamed-qol`), commit `7d63653` on `fa1ade6`; merging
`feature/voxel-world` (still `fa1ade6`) was a no-op. Pushed by the manager.

## What changed
- **`foundation.Held.Light`** (default 1, 0 = the old fixed `foundation.Held.Glow`): the held item takes the light at
  the player's eye (`FoundationInteractionView.cpp`, `UpdateHeldItem`). The level is the brighter of:
  - block light: `FVoxelLights::GetBlockLevel`, the source's level minus taxicab distance, walls ignored, sources at or
    above `voxel.LightMinLevel` only;
  - sky light: 15 x `VoxelLook::SkyPinFor(SkyOpenness)`.

  It is probed every 0.25 s and eased at 30 levels/s; entering the mode snaps. The level goes through
  `FoundationHeldLight::Brightness` (5% floor from the wiki, the f/(4-3f) curve unverified), to the power 2.2, into
  M_HeldItem's new `LightGlow` term (with the eye adaptation undone). It is scaled so level 15 in daylight equals the
  old glow. The Playground keeps the fixed glow.
- **`voxel.Look.LowGlow` 1 -> 0.03** (VoxelLook.h, `LOOK_PARAMETERS`, MPC_VoxelLook re-saved alone): glow lichen and the
  other light 1-7 emitters glow softly instead of clipping white. 0.1 is the next rung up.
- `-FoundationViewsHeldLight` (views suite, opt-in): `held-light0/1-{cave,torch}` and `lowglow-{1,0.3,0.1,0.03}-{cave,torch}`.
- Test `Unnamed.Voxel.HeldLight`; `VoxelLookTests` checks the 0.03 default and the MPC.

## Open
- No night term: the runtime has no `sky_light_level` yet, so the held item reads day-lit on the surface at night. Wire
  it in when the day/night work reads the sky timeline at runtime.
- Block light ignores walls, so a torch behind a wall lights the hand. Sources below `voxel.LightMinLevel` (9), lichen
  included, don't light it.
- Held *blocks* use the block materials and are not affected.
- Brightness curve unverified (the wiki puts level 7 slightly above 25%, the curve gives 22%).
- If the fog lane's merge conflicts on `MPC_VoxelLook.uasset`, regenerate it with CreateBlockMaterials.py after the
  merge.

## Evidence
- Pairs: `Tests\Evidence\2026-10-12\Generated-views\Runtime-20261012-235154`
- Contact sheets: `C:\dev\sandbox\held-lichen\held.png`, `C:\dev\sandbox\held-lichen\lowglow.png` (sandbox, not kept)
- Final checks: `Tests\Evidence\2026-10-13\Checks-final-20261013-000532\summary.txt`
