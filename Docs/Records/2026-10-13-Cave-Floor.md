# The cave floor: walls no light reaches are dim, not black (2026-10-13)

Branch `feature/voxel-cave-floor` (worktree `C:\dev\projects\Unnamed-qol`) on `f0fde83`. Scout AB's facts and brief:
`C:\dev\sandbox\scout-cave-dark\facts.md`, `build-prompt.md`.

## What changed
- **`voxel.Look.CaveFloor`** (default **0.56**, Jesus's pick from the mock-ups, about vanilla's Moody; 0 = black as
  before, 1 = vanilla Default, Brightness 50). Vanilla never lights a block below its dimension's ambient light (26.3's
  overworld `visual/ambient_light_color` #0a0a0a), so a wall at light 0 is a dim copy of its texture.
- **`M_LookCaveFloor`** (`Scripts/Editor/CreateLookMaterials.py`, under `/Game/Blocks`): an unlit post-process after
  tone mapping, `max(scene, CaveFloor x face(N) x fade(depth) x sRGB(BaseColor))`, with vanilla's measured faces at
  Brightness 50: top 0.105, N/S (our +-Y) 0.071, E/W (+-X) 0.065; the bottom 0.05 is unmeasured (0.5 / 0.8 x N/S).
  - After tone mapping: no exposure, eye adaptation or filmic toe in the way; the metering reads the scene before it.
  - A max: lit walls, torches, daylight keep their pixels; the sky has no BaseColor and gets nothing.
  - The fade: out over the edge fog's last 1.5 columns. With the eye in water and `voxel.Look.WaterFog` 1 (the water
    session's switch, looked up by name), over the water fog's -8 to 96 blocks, so the floor lies under that fog.
  - The held item is in the GBuffer and gets the floor, like vanilla's lightmapped hand.
- `AVoxelWorld::UpdateCaveFloor` (`VoxelLook.cpp`, from `ApplyLook` each tick): one unbound `UPostProcessComponent` + MID,
  made on first use, enabled only above 0 (`VoxelLook::CaveFloorDrawn`), so 0 draws nothing.
- Test `Unnamed.Voxel.Look`: the default; the dial moves only "cave floor"; the C++ mirror `VoxelLook::CaveFloorDisplay`
  gives stone (125.5) N/S 8.9, E/W 8.2, top 13.2, bottom 6.3 at 1 (+-15%), N/S 5.0 at 0.56 and 14.2 at 1.6, keeps a lit
  pixel, fades out past the fog, and draws nothing at 0; the script's constants, formula lines and blend location; the
  asset's domain and location. Mutations, each caught then reverted: before tone mapping, no sRGB, no face factor, add
  instead of max, S squared, no fade, 0 not off.

## Measured (views suite, `-FullCostRendering`, iron pickaxe in hand; 0 vs 0.56)
| Shot | mean luma | pixels at or below 2 |
|---|---|---|
| dark cave (`held-light1-cave`) | 17.7 -> 19.3 | 45.8% -> 18.2% |
| by torches, midnight (`held-light1-torch`) | 37.7 -> 38.2 | 17.9% -> 7.1% |
| `cave-default` | 10.1 -> 13.4 | 56.9% -> 18.1% |
| daylight views (day-open, pads, held, render distance, water) | within +0.36 | |

At 0.56 the deepslate room stays dark (Moody by design: deepslate's texture is darker than stone's), but the walls,
floor and the pickaxe read. The vanilla light-0 frame (`C:\dev\sandbox\scout-cave-dark\mock\vanilla-light0-reference.png`)
is Default (1.0), one value away.

## Open
- The underwater cave frame with the water fog on waits for the water lane's merge (`voxel.Look.WaterFog` is not on this
  branch); the fade under water is wired by the switch's name.
- Bottom face and the Moody value are estimates; only Default is measured on vanilla. The Brightness slider is later.
- Lava: the floor is not tied to lava's fog (lava's fog is near-opaque within blocks; the max is then below it).
