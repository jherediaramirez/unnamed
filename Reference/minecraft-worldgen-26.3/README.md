# Java 26.3 worldgen reference

The overworld terrain's worldgen data from vanilla Java 26.3, byte for byte: the noise settings of the three overworld world types, the density functions they use, every top-level noise, the overworld's surface rules, its biomes, and the decoration those biomes reach (placed features, features, block state providers, carvers and block tags). The voxel world's terrain is built from it ([Voxel world](../../Docs/VoxelWorld.md)). This folder is generated, hash-listed and never edited by hand. It holds data and numbers only, no game code.

| Source | SHA-256 |
|---|---|
| `server-26.3.jar` from the official 26.3 server download (on this machine: `C:\dev\sandbox\mc-26.3-blocks\datagen\versions\26.3\`, which is disposable) | `a362163eec5d1612d520772bc16e5b39c09e3b234fdc045f56bf544284ee8ae6` |

```powershell
uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/verify_reference.py
```

The verifier checks every file against `SHA256SUMS.txt`, compares every `data/` file with its jar entry when the jar is present, resolves every reference between the files (density functions; rules to rules, conditions, noises, biomes and the ore veins' density functions), checks that every rule file is reached from the presets, and checks the facts' shape: the splines, and the biome points and lookup samples against the biome files. For decoration it resolves, from the biomes' `features` and `carvers` lists, every placed feature, feature, provider, carver and block tag reference, requires every file in those folders to be reached, and finds every block id in the blocks reference's registry list (`../minecraft-blocks-26.3/facts/block_facts.csv`).

## Files

| Path | What it is |
|---|---|
| `data/minecraft/worldgen/noise_settings/` | `overworld`, `large_biomes` and `amplified`: sea level, height, the noise router's 8 entry points, the `aquifers` inputs and the `debug_functions` views. All three name the surface rule `minecraft:overworld` (`material_rule`); `spawn_target` is not read yet. |
| `data/minecraft/worldgen/density_function/` | The 4 shared functions (`shift_x`, `shift_z`, `y`, `zero`) and the folders `overworld` (27, with `caves/` and `ore_vein/`), `overworld_amplified` (8) and `overworld_large_biomes` (12). |
| `data/minecraft/worldgen/noise/` | The 62 top-level noises. |
| `data/minecraft/worldgen/material_rule/` | The overworld's surface rule tree, 37 files: `overworld` (bedrock floor, the copper and iron ore veins, the surface above the preliminary surface, then the underground), `bedrock_floor` and the 35 under `overworld/` (per-biome subtrees in `biome_surface/` and `under_biome_surface/`). |
| `data/minecraft/worldgen/material_condition/` | The 8 named conditions (`on_floor`, `under_floor`, `deep_under_floor`, `very_deep_under_floor`, `on_ceiling`, `under_ceiling`, `not_underwater`, `not_under_deep_water`); the overworld tree does not use `under_ceiling`. |
| `data/minecraft/worldgen/biome/` | The 56 overworld biomes: temperature, downfall, precipitation, the temperature modifier, colours under `effects`, `attributes` (not read), and the `features` and `carvers` lists the decoration below comes from. |
| `data/minecraft/tags/worldgen/biome/is_overworld.json` | The jar's list of overworld biomes; `build_reference.py` copies the biome files it names. |
| `data/minecraft/worldgen/placed_feature/` | The 210 placed features the biomes reach: the 171 their `features` lists name (by generation step) and the 39 those nest. A placed feature is `{"feature", "placement"}`; either may be inline. |
| `data/minecraft/worldgen/feature/` | The 182 features they reach (26.3's name for configured features; ids nest, such as `coral/brain_block`). A feature's fields sit beside its `type`. |
| `data/minecraft/worldgen/block_state_provider/` | The 8 named block state providers (flower bands, cave vines, mangrove propagules, the soil and podzol under trees). |
| `data/minecraft/worldgen/carver/` | `cave`, `cave_extra_underground` and `canyon` (26.3's name for configured carvers). |
| `data/minecraft/tags/block/` | 127 block tags: those the decoration data names, the 24 the game's code reads by name (`CODE_TAGS` in `build_reference.py`, each with what reads it: what plants and trees may replace, the heightmaps' block sets, the plants' and snow's support), and every tag those include. |
| `data/minecraft/worldgen/multi_noise_biome_source_parameter_list/overworld.json` | Only `{"preset": "minecraft:overworld"}`: the jar builds the list in code, so its points are in `facts/biome_parameters.json`. Normal, Large Biomes and AMPLIFIED all use this preset. |
| `facts/spline_samples.json` | Mojang's own values for the 9 terrain splines (offset, factor and jaggedness for each world type): 512 seeded samples each, as coordinates then value, about a quarter of the coordinates exactly on a point's location. Dumped by `tools/WorldgenFacts.java`. |
| `facts/noise_samples.json` | The value spread of all 62 noises as the jar's `NormalNoise` makes them: the declared range, then mean, standard deviation and 11 percentiles of `get(x, 0, z)` and `get(x, y, z)` over 4 fixed seeds x 16,384 seeded positions spanning 64 periods of the lowest octave. Our own noise is calibrated against it (`Unnamed.Voxel.Worldgen.NoiseSpread`). Dumped by `tools/WorldgenFacts.java`. |
| `facts/biome_parameters.json` | The overworld biome parameter list as the jar builds it (`MultiNoiseBiomeSourceParameterList.knownPresets()`): 7,594 points in list order (the order decides ties), each as the biome's index in `biomes`, then min and max of temperature, humidity, continentalness, erosion, depth and weirdness, then the offset, all quantized as the game stores them (x 10,000). Dumped by `tools/WorldgenFacts.java`. |
| `facts/biome_lookup_samples.json` | 5,120 seeded climate target points in the same units with the biome the jar picks: by its brute-force scan (the first best point in list order) and by its indexed search. They differ on 95 samples, where several points fit equally well. 4,096 points are uniform, half of them at depth 0; 1,024 put one to three parameters on a random point's bounds, give or take one unit. Dumped by `tools/WorldgenFacts.java`. |
| `manifest.json` | The source jar's hashes and the file counts. |
| `SHA256SUMS.txt` | Every other file's SHA-256. |
| `tools/` | `build_reference.py` copies the data from the jar (decoration by reachability from the biomes) and writes the manifest and sums; `verify_reference.py`; `WorldgenFacts.java`. |

## Rebuilding

1. `uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/build_reference.py [server jar] [--probes <staging folder>]` replaces `data/`. `--probes` copies staged feature probe facts into `facts/feature_probes/`; a file already there must come out byte-identical.
2. With JDK 25 and a classpath of the server jar plus the 39 jars of its `libraries` folder: `javac -cp <classpath> -d <temp> tools/WorldgenFacts.java`, then `java -cp <temp>;<classpath> WorldgenFacts Reference/minecraft-worldgen-26.3` (writes the four facts files; the existing ones come out byte-identical; run it outside the project, since the game leaves a `logs/` folder in the working directory).
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
- **Ore veins:** the router no longer references them. The surface rules (`material_rule`) do, through the `ore_vein` rule.
- **Surface rules:** they moved out of the noise settings into two registries, `material_rule` (rules: `sequence`, `condition`, `block`, `ore_vein`, `bandlands`) and `material_condition` (named conditions). The overworld tree uses the conditions `biome`, `noise_threshold` (with `is_3d`), `vertical_gradient`, `y_above`, `water`, `stone_depth` (with `surface_type` and `secondary_depth_range`), `not`, `steep`, `hole` and `above_preliminary_surface`. A `block` rule's `result_state` is a string such as `minecraft:grass_block`.
- **Biomes:** colours under `effects` are `#rrggbb` strings; sky, fog, music and mob spawns moved to `attributes`.
- **Evaluation:** the game compiles density functions into samplers (`DensityFunction.compileSampler`), so a single point is no longer one `compute` call. `WorldgenFacts.java` samples the splines through `CubicSpline` and its codec alone.
