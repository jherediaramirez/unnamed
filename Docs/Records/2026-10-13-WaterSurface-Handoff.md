# Handoff: after the water surface (2026-10-13)

The surface from below is done: `voxel.Look.WaterSurface` 1 (the default; 0 = before) draws water with `M_ChunkDepthWater` and `M_ChunkFarDepthWater` (two-sided, writing depth, back faces lit as the front), so from under water the surface shows and the frame lightens toward it. The numbers and the vanilla pair are in [2026-10-12-WaterFog.md](2026-10-12-WaterFog.md), section "Surface from below". The switch is read when the world loads; captures set it with `CaptureVoxel -StartCVars 'voxel.Look.WaterSurface=0'`. The shot script is `C:\dev\sandbox\water-surface\shots.ps1` (spots: ordinary ocean -1850,280; warm ocean 296,-56; eye Z 57-58).

## Next

1. **The ordinary ocean's hue (unverified).** #050533 under the bright rule gives electric blue (24,24,241). `Under_water.png`'s teal (about 49,150,181) fits no rule found, and the frame's version is not stated. Jesus is asked for a dated Java 1.13+ ordinary-ocean frame. Measure that frame first, then fit the rule.
2. **The sheen band.** Where the far surface meets the fog, the water's specular at grazing angles lightens a band (up to 87,98,219). Vanilla has none. A likely fix is specular x saturate(TwoSidedSign) in the depth variants (CreateBlockMaterials.py), behind a switch.

The open items in [2026-10-12-WaterFog-Handoff.md](2026-10-12-WaterFog-Handoff.md) still stand: the view clearing over time, the fog's shape, biome blending, and lava.
