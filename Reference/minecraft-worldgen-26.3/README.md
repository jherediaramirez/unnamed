# Java 26.3 worldgen reference

The overworld terrain's worldgen data from vanilla Java 26.3, byte for byte: the noise settings of the three overworld world types, the density functions they use, and every top-level noise. The voxel world's terrain is built from it ([Voxel world](../../Docs/VoxelWorld.md)). This folder is generated, hash-listed and never edited by hand. It holds data and numbers only, no game code.

| Source | SHA-256 |
|---|---|
| `server-26.3.jar` from the official 26.3 server download (on this machine: `C:\dev\sandbox\mc-26.3-blocks\datagen\versions\26.3\`, which is disposable) | `a362163eec5d1612d520772bc16e5b39c09e3b234fdc045f56bf544284ee8ae6` |

```powershell
uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/verify_reference.py
```

The verifier checks every file against `SHA256SUMS.txt`, compares every `data/` file with its jar entry when the jar is present, resolves every reference between the files, and checks the spline facts' shape.

## Files

| Path | What it is |
|---|---|
| `data/minecraft/worldgen/noise_settings/` | `overworld`, `large_biomes` and `amplified`: sea level, height, the noise router's 8 entry points, the `aquifers` inputs and the `debug_functions` views. Their surface rules (`material_rule`) and `spawn_target` are for phase 5. |
| `data/minecraft/worldgen/density_function/` | The 4 shared functions (`shift_x`, `shift_z`, `y`, `zero`) and the folders `overworld` (27, with `caves/` and `ore_vein/`), `overworld_amplified` (8) and `overworld_large_biomes` (12). |
| `data/minecraft/worldgen/noise/` | The 62 top-level noises. |
| `facts/spline_samples.json` | Mojang's own values for the 9 terrain splines (offset, factor and jaggedness for each world type): 512 seeded samples each, as coordinates then value, about a quarter of the coordinates exactly on a point's location. Dumped by `tools/WorldgenFacts.java`. |
| `manifest.json` | The source jar's hashes and the file counts. |
| `SHA256SUMS.txt` | Every other file's SHA-256. |
| `tools/` | `build_reference.py` copies the data from the jar and writes the manifest and sums; `verify_reference.py`; `WorldgenFacts.java`. |

## Rebuilding

1. `uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/build_reference.py [server jar]` replaces `data/`.
2. With JDK 25 and a classpath of the server jar plus the 39 jars of its `libraries` folder: `javac -cp <classpath> -d <temp> tools/WorldgenFacts.java`, then `java -cp <temp>;<classpath> WorldgenFacts Reference/minecraft-worldgen-26.3`.
3. Run `build_reference.py` again so `SHA256SUMS.txt` covers the new facts, then the verifier.

## What 26.3 changed

Older docs and the wiki describe the 1.18 to 1.21 format. In 26.3:

- **Router:** it has 8 entries: `continents`, `erosion`, `ridges`, `temperature`, `vegetation`, `depth`, `chunk_surface_level` and `final_density`. The aquifer inputs moved to the noise settings' `aquifers` object: `barrier`, `fluid_level_floodedness`, `fluid_level_spread`, `lava`, `exclusion` and `surface_level`.
- **Noises:** a noise file holds `base_octave` and `base_amplitude`, plus `octave_count` when it is not 1 and `amplitude_modifiers` when they are not all 1.
- **Density functions:** 30 node types. These are new or changed against the 1.21 format:
  - `sub`, `div`, `negate`, `lerp` and `interval_select`;
  - `gradient`, with an axis (only `y` is used);
  - a single `cache`;
  - `noise` with optional `shift_x` and `shift_z`, which replaces `shifted_noise`;
  - `interpolated`, which carries its own cell sizes;
  - `find_top_surface`, which gives the preliminary surface level.
- **Ore veins:** the router no longer references them. The surface rules (`material_rule`) do.
- **Evaluation:** the game compiles density functions into samplers (`DensityFunction.compileSampler`), so a single point is no longer one `compute` call. `WorldgenFacts.java` samples the splines through `CubicSpline` and its codec alone.
