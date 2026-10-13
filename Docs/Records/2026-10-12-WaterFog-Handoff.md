# Handoff: after the water fog (2026-10-12)

The underwater fog is done ([2026-10-12-WaterFog.md](2026-10-12-WaterFog.md)): `voxel.Look.WaterFog` 1, the generated `M_WaterFog`, the biome's `water_fog_color` (default #050533), -8 to 96 blocks, and lava's fog in lava. Checks: `RunChecks -Profile final -Flight smoke` was green (`Tests/Evidence/2026-10-12/Checks-final-20261012-233330/summary.txt`).

## Next step: the surface from below, with light toward it

Jesus (2026-10-13), shipping `voxel.Look.WaterFogBright` 1 as it is: "I can't see the top. You would think you would see sunlight from the top. In Minecraft there's a gradual light density: lighter at the top, darker further away; you can see vaguely where the top of the water is."

So the step is the surface visible from below plus a vertical gradient (lighter toward the surface), together with the teal hue, matched against the wiki's 1.18 deep warm ocean frame (`C:\dev\sandbox\vanilla-web-refs\underwater\1.18_deep_warm_ocean.png`; the brightness numbers are in the record's Brightness section).

- **Problem:** with the fog on, the water's surface is invisible from under water (`Tests/Evidence/2026-10-12/WaterFog-up-on`). The fog is a post-process that reads scene depth, and the translucent water writes none, so the surface's pixels take the sky's distance and are fully fogged. Vanilla shows the surface 5 blocks up about 13% fogged.
- **Likely fix:** turn on "Output Depth and Velocity" (`output_translucent_velocity`) for the water materials in `Scripts/Editor/CreateBlockMaterials.py` (`M_BlockWater`, `M_ChunkWater`, `M_ChunkFarWater`), behind a switch if it can be one, then regenerate them with the script.
- **What to check:** the up-from-below shot against `WaterFog-up-on`; above-water shots unchanged (TSR, edge haze, far fade); W3d's water work in the same materials.
- **Spot:** dev seed, `-SpawnAt -1850,280 -SpawnExact`, eye at Z 58; `C:\dev\sandbox\water-fog\shots.ps1` makes every view.

## Open items

- Vanilla's underwater view clears over time (a thicker initial fog, 60% at 5 s, gone at 30 s; minecraft.wiki/w/Fog).
- Fog shape: distance in all directions; whether Java's is spherical or cylindrical is unverified.
- No blending across biome borders: the eye cell's biome sets the colour.
- Lava: the start of 0 is unverified, Fire Resistance's 3 blocks is not done, and there is no in-lava capture.
- If `M_WaterFog` is missing, its load is retried every frame while the eye is in a fluid (no latch).
