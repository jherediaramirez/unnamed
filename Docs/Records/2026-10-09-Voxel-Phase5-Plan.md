# Voxel world phase 5 plan: biomes and surface - October 9, 2026

The plan for phase 5, written in a plan-only chat. Nothing was built, and no code, asset or data changed. It follows [the phase 5 handoff](2026-10-09-Voxel-Handoff-Phase5.md), which stays the entry point for the next chat; read this plan right after it. Facts were checked at commit `bb57535` on `feature/voxel-world`, by reading the 26.3 server jar (`C:\dev\sandbox\mc-26.3-blocks\datagen\versions\26.3\server-26.3.jar`) and the two references. The jar's JSON pulled for reading is in `C:\dev\sandbox\phase5plan\jar\`, with the scan script `scan.py` beside it (disposable).

**Acceptance (from the worldgen prompt):** the biome debug map shows plausible regions and borders, and peaks above the snow line are snowy. Also in scope: the data-driven biome table (6D boxes plus offset), 4x4x4 storage with seeded border jitter, altitude temperature, surface rule trees per biome, and ocean temperature variants.

## 1. What the data check found

**The handoff was wrong about one thing.** The surface rules are *not* in the reference's noise settings. In 26.3, `noise_settings/overworld.json` holds only `"material_rule": "minecraft:overworld"`, a reference into two new registries:

| Jar folder | Files | Notes |
|---|---|---|
| `worldgen/material_rule/` | 46 (the overworld tree is `overworld.json`, `bedrock_floor.json` and the 36 files under `overworld/`; also nether, end, caves and floating islands) | The rule tree. `overworld.json` is a sequence: bedrock floor, copper ore vein, iron ore vein, `above_preliminary_surface` then `overworld/surface`, then `overworld/underground` (sulfur cave bands, then the deepslate gradient). Per-biome subtrees live in `overworld/biome_surface/` and `overworld/under_biome_surface/`. |
| `worldgen/material_condition/` | 8 | Named conditions: `on_floor`, `under_floor`, `deep_under_floor`, `very_deep_under_floor`, `on_ceiling`, `under_ceiling`, `not_underwater`, `not_under_deep_water`. |
| `worldgen/biome/` | 67 (about 54 overworld) | Keys: `temperature`, `downfall`, `has_precipitation`, `temperature_modifier` (2 files), `effects` (`water_color` in all 67; `grass_color`, `foliage_color`, `dry_foliage_color`, `grass_color_modifier` in a few), `attributes` (26.3's environment attributes: sky and fog colours, music, mob spawns), `features`, `carvers`. 26.3 adds `dappled_forest` and `sulfur_caves`. |
| `worldgen/multi_noise_biome_source_parameter_list/overworld.json` | 1 | Only `{"preset": "minecraft:overworld"}`: the boxes are built in code. Normal, Large Biomes and AMPLIFIED all use this one preset (`world_preset/*.json`); Single Biome uses a `fixed` source (`plains` by default). |

**Rule and condition types the overworld tree uses** (14): `sequence`, `condition`, `block`, `ore_vein`, `not`, `biome`, `noise_threshold`, `vertical_gradient`, `y_above`, `water`, `stone_depth` (now with `surface_type` and `secondary_depth_range`), `steep`, `hole`, `above_preliminary_surface`, `bandlands`. There is no `temperature` condition in 26.3's overworld tree.

**Noises the rules read** (all 13 already in the reference's 62): `surface`, `surface_secondary`, `surface_swamp`, `calcite`, `gravel`, `ice`, `packed_ice`, `powder_snow`, `small_patch`, `sulfur_cave_gradient`, `clay_bands_offset`, plus the badlands and iceberg pillar noises for phase 6. The ore vein density functions (`overworld/ore_vein/*`, 6 files) are already in the reference too.

**Blocks the rules place** (31): air, bedrock, calcite, cinnabar, coarse_dirt, copper_ore, deepslate, deepslate_iron_ore, dirt, granite, grass_block, gravel, ice, mud, mycelium, orange_terracotta, packed_ice, podzol, **powder_snow**, raw_copper_block, raw_iron_block, red_sand, red_sandstone, sand, sandstone, snow_block, stone, sulfur, terracotta, tuff, water, white_terracotta. `bandlands` adds the other terracotta colours (yellow, brown, red, light gray), and the snow line needs snow layers and `grass_block[snowy=true]`.

| Block | In the block reference / `DA_Blocks` | What to do |
|---|---|---|
| Everything above except the two below | Yes (full cubes; `grass_block` has `snowy`) | Nothing |
| `powder_snow` | **No**: the reference keeps full cubes and slabs, and powder snow has no collision shape | Question 3 |
| `snow` (layers 1 to 8) | **No**: a partial shape | Question 3. Vanilla places snow layers and ice with its freeze step, not the surface rules |
| `lava` | No (voxel table only, state 105) | Unchanged; handoff question 3 |

The grass and foliage colormaps are already in the block reference (`assets/minecraft/`), so biome tints need no new data.

## 2. Design answers to the handoff's open points

- **Where biome lookup runs:** on the worker, in the column job, after density. Per quart column (4 x 4 blocks; 5 x 5 of them per column for the jitter), the five flat climate values come from the climate sampler, which already does exactly this grid (46 µs). Depth is the only y-dependent value, so each quart column gets its candidate boxes pruned once by their 5D distance, then one exact sweep down the 96 quarts in depth. It must equal a brute-force nearest search on every quart (a test, and a `skip on/off` style bench gate). Expected cost 0.1 to 0.3 ms per column. Ties break by list order, our own rule.
- **Storage:** biome indices per 4 x 4 x 4 cell, 64 per section, one byte each (1 KB per column), in the column data so features, spawning and far LOD can read them later. Border jitter: each block reads the cell of a jittered quart position, `Hash64(seed, "biome/jitter", quart)`, our own design with vanilla's effect (ragged borders, no grid).
- **Surface rules and stone depth:** after density and the aquifer fill, one top-down pass per block column builds the context the conditions read: stone depth above and below in the current solid run (floor and ceiling), the water level above the run, the surface depth noise (`surface`, `surface_secondary`), the preliminary surface (PS) and steepness from the neighbouring heights. The rule tree compiles into flat nodes (as the density graph does), evaluated only for default-block (stone) positions.
- **Cost control, exact:** a stone block deeper than the tree's largest reachable depth (offsets plus surface depth plus secondary ranges, found at compile) can only reach the y-only branches (bedrock gradient, deepslate gradient, ore veins), so those results are precomputed per y. The gate is the same as phase 4's: full-tree evaluation on and off gives 0 differing blocks, and goldens stay byte-identical.
- **Altitude temperature and the snow line:** per-block temperature is the biome's temperature, the frozen modifier where the biome has it, minus 0.00125 per block above Y 80, with a small wiggle from our own noise. Below 0.15 our own freeze pass (run after the rules) puts a snow layer on the top solid block and turns still surface water to ice. This is vanilla's freeze step brought forward from phase 6, because the acceptance needs it.
- **Ocean temperature variants:** these come straight from the parameter list's temperature bands (warm, lukewarm, cold, frozen, and their deep forms). What makes them visible is per-biome water colour, so the fluid mesher needs a tint per vertex (question 5).
- **Biome fidelity** (handoff question 7): black-box the *mapping*, not the positions. A facts file of seeded climate points with the jar's answer lets us test that our lookup picks the same biome from the same six values exactly (ties aside). World positions can only match statistically, because our noise differs.

## 3. Sessions

Five build sessions after this one, each about 100k to 200k tokens with subagents, one chat per session. Each ends with a Sonnet 5.5 review, every suite, the `-DisableAdaptiveUnity` build, `create-world.mjs`, a local commit and a handoff.

### Session 2: reference data, blocks and readers

| File | Why |
|---|---|
| `Reference/minecraft-worldgen-26.3/tools/build_reference.py` | Copy `material_rule/overworld*`, `bedrock_floor`, `material_condition/*` and the overworld biomes byte for byte. Existing files stay byte-identical. |
| `tools/WorldgenFacts.java` | Dump the overworld parameter list (every point's six ranges, offset and biome) to `facts/biome_parameters.json`, and `facts/biome_lookup_samples.json` (seeded climate points and the jar's chosen biome). |
| `tools/verify_reference.py`, `README.md`, manifest, sums | Count and resolve the new files: rules to conditions, biomes and noises. |
| `Reference/minecraft-blocks-26.3/tools/*` | Only if question 3 is (a): add `powder_snow` and `snow` through the tools; then `ImportBlocks.py` with the old assets moved away deliberately. |
| `Source/UnnamedVoxel/Biome/VoxelBiomeTable.h/.cpp` (new) | Plain C++: biomes (name, temperature, modifier, downfall, colours) and the parameter points. |
| `Source/UnnamedVoxel/Surface/VoxelSurfaceRules.h` (new) | The flat rule and condition nodes, `Validate`. |
| `Source/Unnamed/Voxel/VoxelWorldgenReader.*` | `ReadBiomes`, `ReadBiomeParameters`, `ReadSurfaceRules`: strict, file and field in every error; ignore `features`, `carvers` and `attributes` explicitly by name. |
| Tests: `Unnamed.Voxel.Biome.Read`, `.Surface.Read`, `.Surface.RoundTrip` | Counts, every type, strictness. |

### Session 3: performance levers before new work

The handoff's levers 1 and 2: cross-column corner sharing (a cache of corner columns shared across workers, keyed by seed and corner column) and a bit-exact `base_3d_noise` speed-up. Gates: goldens byte-identical, skip on/off 0 differing blocks, load-order hashes equal. Goal: overworld under 4 ms mean, all three presets under 5 ms, to make room for 0.5 to 1 ms of phase 5 work. Timebox 60% of the session; anything left moves to session 6.

### Session 4: biome source, storage, debug map and minimap

| File | Why |
|---|---|
| `Source/UnnamedVoxel/Biome/VoxelBiomeSource.h/.cpp` (new) | Pruned nearest search per quart column with the exact depth sweep; a fixed source for Single Biome. |
| `Source/UnnamedVoxel/Biome/VoxelBiomeStorage.h` (new) | 4x4x4 indices per column and the jittered block lookup. |
| `Terrain/VoxelDensityTerrain.*`, column data | Fill the storage in the column job. |
| `Unnamed/Voxel/VoxelClimateDebug.cpp` (or a new `VoxelBiomeDebug.cpp`) | `voxel.BiomeMap` PNGs with a data-driven palette, `voxel.Debug.Biomes`, and the on-screen climate and biome minimap with the readout at the player. |
| `VoxelBench.cpp`, `VoxelConsole.cpp` | A biome stage in `voxel.Bench Density` and `Columns`; brute force against pruned. |
| Tests: `Biome.Lookup` (every sample in `biome_lookup_samples.json`), `.Exact` (pruned equals brute force), `.Jitter` (determinism, load order), `.Golden` (biome id hashes, seed 263, three presets) | |

### Session 5: surface rules, temperature and the snow line

| File | Why |
|---|---|
| `Source/UnnamedVoxel/Surface/VoxelSurfaceRules.cpp`, `VoxelSurfaceContext.h` (new) | The evaluator: the column context pass, conditions, the vertical gradient with `Hash64(seed, random_name, x, y, z)`, our own badlands band pattern from `clay_bands_offset`, and the deep-stone shortcut. |
| `Terrain/VoxelDensityTerrain.*` | Replace the grass, sand and gravel dressing and the bedrock and deepslate stand-ins with the rules; add the freeze pass. Single Biome builds the fixed source from world.json's `biome`. |
| `Source/UnnamedVoxel/Biome/VoxelBiomeClimate.h` (new) | Per-block temperature and the snow line. |
| Tests: `Surface.Conditions` (each condition on hand-made columns), `.Exact` (shortcut on/off), `.Snow` (a peak above the line is snowy), `Aquifer.Golden` and `Density.Golden` re-recorded only as decided in question 1 | |

Ore veins: question 4.

### Session 6: ocean variants, tints, captures, performance and the record

- Per-biome water colour, and grass and foliage tints from the colormaps, as a per-vertex tint in the mesher (question 5).
- `FindBiomeShots` in code (as `FindCaveShots`): the biome map, a snowy peak above the line, each ocean variant, a desert to badlands edge, a swamp or mangrove, and a flight. One montage at a time, `stats.py` first.
- Benches against the 5 ms limit and 2 ms target; the flight's 2 ms frame budget.
- `Docs/VoxelWorld.md`, `Docs/Blocks.md` if blocks changed, and the phase 5 record.

## 4. Acceptance captures and figures

| Criterion | Evidence |
|---|---|
| Plausible biome regions and borders | `voxel.BiomeMap` at 8,192 and 32,768 blocks for the three presets (Large Biomes about 4x larger), plus the area share of each biome against the jar's lookup samples. |
| Snowy peaks above the snow line | A `CaptureVoxel` shot of a mountain whose top is snow covered and whose lower slopes are not; the test `Surface.Snow`. |
| Ocean variants | One shot each of warm, lukewarm, cold and frozen oceans, with their water colours. |
| Surface per biome | Desert to badlands edge (sand, red sand, terracotta bands), a swamp, stony and frozen peaks. |
| Reproducible | Biome and block goldens; load-order hashes equal; shortcut on/off 0 differing blocks. |
| Timings | Biome and surface stages in `voxel.Bench Density` and `Columns`; the flight. |

## 5. Questions for Jesus

1. **Generator version** (handoff question 4). Phase 5 changes every generator-2 block.
   - (a) **Recommended:** keep changing generator 2 until edit saving lands in phase 10, then freeze it. No saved blocks exist yet, and re-recording goldens under a decided change is not "updating one to make a test pass".
   - (b) Bump to generator 3 now; generator-2 worlds keep phase 4's terrain forever.
2. **Biome fidelity** (handoff question 7).
   - (a) **Recommended:** the same data and rules, with the climate-to-biome mapping checked exactly against the jar's answers (a facts file) and world positions checked statistically.
   - (b) Statistical only, no lookup facts.
   - (c) Also try to match vanilla positions (not possible with our own noise).
3. **Powder snow and snow layers** (handoff question 6). The rules place `powder_snow`, and the snow line needs snow layers. Neither is in the registry.
   - (a) **Recommended:** add both to the block reference through its tools. Snow layers get a new partial shape (k/8 high) in the mesher and collision; powder snow draws as a full cube and is solid for now, with no sinking, until a movement rule exists.
   - (b) Add snow layers only; draw `snow_block` where the rules say `powder_snow`.
   - (c) Neither for now: `snow_block` for both, and the snow line puts snow blocks on top.
4. **Ore veins.** The 26.3 rule tree places the large copper and iron veins (raw ore blocks, granite, tuff) through `ore_vein` rules. Every block needed is in the registry, but they cost noise per underground block.
   - (a) **Recommended:** read them in phase 5 but turn them on with ores in phase 6, behind a switch, so phase 5's goldens and timings do not include them.
   - (b) Turn them on in phase 5.
5. **Biome colours.** Ocean variants are only visible through water colour.
   - (a) **Recommended:** per-biome water colour plus grass and foliage tints from the colormaps, per vertex, in phase 5 (session 6). Blending across borders comes later.
   - (b) Water colour only now; grass and foliage tints in phase 6 with vegetation.
   - (c) No tints in phase 5; the biome map alone shows the variants.
6. **Session order.**
   - (a) **Recommended:** the five sessions above, with the performance levers (session 3) before any biome work, so phase 5 starts with headroom.
   - (b) The handoff's four build sessions, with the levers inside the biome-source session.
7. **Still open from the handoff** (no new recommendation): cave exposure and emission (question 1, recommended eye adaptation between two EV bounds plus stronger emission); the cave-lighting review (2); lava in the block reference (3, recommended yes, and it fits session 2 with question 3 here); the silent fallback (5, recommended a hard error for saved worlds); the untracked evidence, zips, `logs/`, `Packages/Phase4Terrain20261009`, and the two modified `Content/DevGrid` assets (9); and `voxel.Debug.Climate C` in game (10).

## 6. Risks

- **Cost:** biomes and rules add an estimated 0.5 to 1 ms per column. Without session 3, overworld would sit near or over 5 ms and the other presets further over.
- **Code-built data:** the parameter list exists only in the jar's code, so `WorldgenFacts.java` must reach it through the registries the bootstrap builds. The spline facts worked this way, so it is likely fine.
- **`bandlands` and the vertical gradients** use code-side randomness in vanilla. We use our own hashes and our own band pattern, so badlands will look like vanilla's but not match it.
- **Underground biomes matter for blocks:** `overworld/underground` checks `sulfur_caves`, so storage must be 3D everywhere, not only at the surface.
- **Steepness** compares neighbouring surface heights. At a column's edge those belong to the next column, which may not be generated yet. Session 5 must get them without depending on load order (heights inside the column only, or the edge heights from the preliminary surface), and prove it with the load-order hashes.

## 7. Jesus's answers (October 9, morning)

These are settled. Treat them as decisions.

**Phase 5 questions (section 5).** Each one took the recommendation.

1. **Generator version:** keep changing generator 2 until edit saving lands in phase 10, then freeze it.
2. **Biome fidelity:** check the climate-to-biome mapping exactly against jar facts. Check biome positions statistically.
3. **Snow blocks:** add powder snow and snow layers through the block reference's own tools. Snow layers get a new k/8 shape; powder snow is solid for now.
4. **Ore veins:** read them in phase 5 and switch them on in phase 6.
5. **Biome colours:** water plus grass and foliage tints per vertex, in phase 5.
6. **Session order:** the five sessions in section 3, with the performance levers (session 3) before any biome work.

**The handoff's still-open questions.**

- **Cave exposure and emission (handoff question 1):**
  - Decision: eye adaptation between two EV bounds, plus stronger emission for light-emitting blocks.
  - It isn't in a session yet. Session 2's plan proposes a place for it: with the real lava in session 2 if the budget allows, otherwise a short step before session 6.
- **Cave lighting (2):**
  - Decision: keep Lumen cards for now.
  - Right after the brightness fix, re-run the same-cave comparison in both modes (`CaptureVoxel -Caves`, cards vs `r.Lumen.HardwareRayTracing.LightingMode 1`, with GPU ms). Switch to hit lighting if it looks clearly better in caves.
  - Background, from the morning review: Lumen's own view with cards has holes at cave mouths, overhangs and steep slopes, and 24% of its pixels differ from hit lighting. Today the on-screen image differs on only 0.04 to 0.16% of pixels. Hit lighting costs 0.4 to 1.2 ms more per frame at 1600 x 900.
  - Images: `C:\dev\sandbox\overnight-review-20261009\cave-lighting.html`.
- **Lava in the block reference (3):**
  - Decision: yes. In session 2, extend `Reference/minecraft-blocks-26.3` with lava through its own tools (the fluids entry, plus the `lava_still` and `lava_flow` textures), then give lava its real animated texture.
  - Lava stays still, with no damage and no flow.
- **Silent fallback (5):** a saved world gets a hard error when the reference is missing or a preset fails to build. The fallback stays only for the dev seed.
- **Housekeeping (9), done on October 9:**
  - The two `Content/DevGrid` assets were restored with git.
  - Moved to the Recycle Bin: `Packages/Phase4Terrain20261009`, the untracked pre-phase-3 `Tests/Evidence` (27 folders, 4.95 GB; the 2.4 GB in earlier handoffs was an undercount), and the two HUD zips.
  - `logs/` stays.
- **`voxel.Debug.Climate` in game (10):** still for Jesus to try.
