# Facts for phase 5, sessions 5 and 6: surface rules, temperature, the freeze pass, tints

Scout report, October 9, 2026. Read-only research for `C:\dev\projects\Unnamed` (branch `feature/voxel-world`).

*Copied into the records by phase 5 session 5 from the disposable `C:\dev\sandbox\scout-s5s6\` (unchanged apart from the evidence paths). The evidence, the two probes (`java/`) and the scripts (`scripts/`, without the machine-specific classpath `cp.txt`) are in [2026-10-09-Voxel-Phase5-Scout-Evidence](2026-10-09-Voxel-Phase5-Scout-Evidence/); `biome_climate_colours.json` is there too.*

**Sources:**
- The data: `Reference/minecraft-worldgen-26.3/data/minecraft/worldgen/` (`material_rule/`, `material_condition/`, `biome/`, `noise/`, `noise_settings/overworld.json`) and `Reference/minecraft-blocks-26.3/`.
- Black-box runs of Mojang's `server-26.3.jar`. My Java only calls public 26.3 API, found with `javap -public`. I read no method bodies.
- Every behaviour below comes from blocks the jar produced from inputs I made by hand. Each formula was fitted to those outputs and then checked against every recorded sample. The counts are given.

**Evidence:** all in [`2026-10-09-Voxel-Phase5-Scout-Evidence/blackbox/`](2026-10-09-Voxel-Phase5-Scout-Evidence/blackbox/); the file list is in the appendix.
- Probes: `java\SurfaceProbe.java` and `java\ClimateProbe.java`.
- Rule trees in compact form: `scripts\surface_tree.txt` and `scripts\golden_summary.txt`.
- Per-biome climate and colour fields from the data: `biome_climate_colours.json`.

**Method:** a `ProtoChunk` is filled with stone, water and air in a chosen shape. The jar's own `MaterialSystem.buildSurface` then runs over it, with a marker rule decoded from JSON (`if <condition> then gold_block`), with a fixed biome, seed 0 unless noted, and the real overworld noise router. Where gold lands is the condition's truth value at each block. Climate facts come from the jar's `Biome` methods. Freeze facts come from running the jar's `SnowAndFreezeFeature` on a small map-backed level (a `java.lang.reflect.Proxy` of `WorldGenLevel`). Heightmaps on that level use the jar's own `Heightmap.Types.isOpaque()` predicate.

The jar runs took about 15 s in total, with no UnrealEditor-Cmd running (checked before each run).

---

## 1. Rules and conditions in overworld's `material_rule`

### 1.1 26.3 names and the top-level order

- 26.3 renamed surface rules to **material rules**: registries `worldgen/material_rule` and `worldgen/material_condition`, and the jar's `levelgen.material.MaterialSystem`. `noise_settings/overworld.json` names the tree as `"material_rule": "minecraft:overworld"`.
  - `default_block` is `minecraft:stone`, `default_fluid` is `minecraft:water`, `sea_level` is 63, and the world is `min_y -64, height 384`.
- `material_rule/overworld.json` is a sequence, and the first rule that gives a block wins:
  1. `minecraft:bedrock_floor`: a vertical gradient from `above_bottom 0` to `above_bottom 5`, giving bedrock.
  2. `overworld/copper_ore_vein`, then `overworld/iron_ore_vein`: new in 26.3, ore veins live in the material rules (`ore_vein` rule type; phase 6).
  3. `if above_preliminary_surface then overworld/surface`.
  4. `overworld/underground`: sulfur_caves bands; then deepslate as a vertical gradient from `absolute 0` (true at and below) to `absolute 8` (false at and above).
  - When nothing matches, the stone stays.
- The rule types used: `sequence` (35), `condition` (123), `block` (69, with `result_state` a plain block-id string), `bandlands` (2), `ore_vein` (2).
- The condition types used: `biome` (43), `noise_threshold` (32), `y_above` (10), `stone_depth` (6), `steep` (5), `not` (4), `hole` (3), `water` (2), `vertical_gradient` (2), `above_preliminary_surface` (1).
  - `minecraft:temperature` exists in the jar (it encodes as `{"type":"minecraft:temperature"}`), but overworld never uses it.
- Named conditions (`material_condition/*.json`), which rules refer to by id:

| Name | Type | Fields |
|---|---|---|
| `on_floor` | stone_depth | offset 0, add_surface_depth false, secondary 0, floor |
| `under_floor` | stone_depth | offset 0, add_surface_depth **true**, secondary 0, floor |
| `deep_under_floor` | stone_depth | offset 0, true, secondary **6**, floor |
| `very_deep_under_floor` | stone_depth | offset 0, true, secondary **30**, floor |
| `on_ceiling` | stone_depth | offset 0, false, 0, ceiling |
| `under_ceiling` | stone_depth | offset 0, true, 0, ceiling |
| `not_underwater` | water | offset **-1**, surface_depth_multiplier 0, add_stone_depth false |
| `not_under_deep_water` | water | offset **-6**, multiplier **-1**, add_stone_depth **true** |

### 1.2 What the jar's surface pass does to a column (observed)

- **Which blocks:** the rule is evaluated at every stone position of the column, from the top of the column down to y = -64.
  - Deepslate and bedrock marks appear at y 1..7 and -63..-60, far below the surface.
  - The rules never changed an air or fluid block. Only the code-side additions in 1.6 put blocks into air and water.
  - Not tested: solids other than stone placed before the pass (our terrain only makes stone).
- **The results do not feed back.** A block replaced higher up does not change the counts for blocks below it.
  - Mangrove swamp: y = 62 becomes water, and the blocks under it still count as solid (`water mud*4`, golden_columns.json).
- **Stone depth above** (`depthAbove`, 1-based): the number of solid blocks from the top of the current solid run down to this block, counting this block. The top block of a run has `depthAbove = 1`.
  - **Air resets it. Fluid does not reset it, and fluid is not counted.**
    - Stone 46..60, water 41..45, stone ≤ 40: y = 40 has depthAbove 16, so it is *not* `on_floor`.
    - Stone 70..79 over water 60..69 over stone: `floor offset 12` marks 79..70 and 59..57, so depthAbove at 59 is 11 (stone_depth.json, extra.json).
- **Stone depth below** (`depthBelow`, 1-based): the number of solid blocks from this block down to the next air **or fluid**, counting this block. The world bottom ends the run: y = -64 has depthBelow 1 when y = -63 is solid.
  - Stone 46..60 over water 41..45: y = 46 is `on_ceiling`.
- **Water height** `W`: set at the first fluid block met going down after the last air, as **that fluid block's y + 1**, so it is the y just above the topmost fluid run.
  - Air resets it to "none". Stone does not.
  - A second, lower fluid run does not change it: stone ≤ 59, water 60..69, stone 70..79, water 80..89 gives W = 90 for both stone runs.
  - **Lava counts as fluid:** stone ≤ 60 under lava 61..70 gives W = 71 (extra.json).
- **Surface depth** `sd`, one integer per column: `sd = trunc(surface(x, 0, z) * 2.75 + 3.0 + r * 0.25)`.
  - `r` is a per-column value in [0, 1). The jar's comes from its positional random; ours should be a hash of (seed, x, z).
  - Fits all 2,304 measured columns, within the two values `r` allows (depth.json, extra.json).
  - `surface` is the `minecraft:surface` noise sampled at y = 0 (equal to its 2D form on every column).
  - Observed values 0..5. The noise's minimum over a 16,384² scan (step 8) is -1.356, which gives -0.73 before truncation. `sd` was never negative: values in (-1, 0) truncate to 0, and they were seen.
- **Secondary depth** (only in `stone_depth` with `secondary_depth_range` R > 0): `sec = trunc((surface_secondary(x, 0, z) + 1) / 2 * R)`.
  - **Not clamped.** `surface_secondary` reaches -1.445..+1.413, so `sec` can be negative (down to -6 for R = 30) or larger than R (up to 36).
  - Fits all 1,536 + 197 + 215 checked columns, including every column with |ss| > 1. A column whose total limit is ≤ 0 marks no block.
  - **It is added whether or not `add_surface_depth` is set:** `floor, offset 0, add false, R 6` marks 1 + sec blocks (stone_depth.json `range6_noSD`).
- **Preliminary surface:** `minSurfaceLevel = floor(P(x, z)) + sd - 8`, where P is `preliminarySurfaceFunction()` (the noise router's `overworld/preliminary_surface_level`) at the column.
  - P is the same at y 0 and y 100, and it comes in steps of 1/16.
  - Fits 1,280 of 1,280 columns (above_preliminary_surface.json).
  - Only positive P was seen, so floor and truncation were not told apart.
- **Biome:** the biome at the **block's own position** as the jar's `BiomeManager.getBiome(pos)` gives it, a jittered 3D zoom of the 4 x 4 x 4 biome grid.
  - It agrees on 7,936 of 7,936 positions; a plain quart lookup agrees on only 7,614.
  - On a straight quart border, the marking changed with y in 48 of 256 columns (biome_condition.json).
- **Heightmap for steep:** a chunk-local heightmap that **counts water** (a world-surface heightmap). Under a flat sea, nothing is steep (extra.json).

### 1.3 Each condition: fields and exact behaviour

Notation: y is the block's y; `d↑` and `d↓` are depthAbove and depthBelow; sd, sec and W are as in 1.2. Every inequality below was checked by black box, and the bounds are as written.

| Condition | JSON fields | True when | Evidence |
|---|---|---|---|
| `stone_depth` | `offset` int, `add_surface_depth` bool, `secondary_depth_range` int, `surface_type` floor/ceiling | `d ≤ 1 + offset + (add ? sd : 0) + (R > 0 ? sec : 0)`, with d = d↑ for floor and d↓ for ceiling (inclusive) | stone_depth.json, all 4 profiles x 18 variants x 3 columns |
| `y_above` | `anchor` {absolute / above_bottom / below_top}, `surface_depth_multiplier` int, `add_stone_depth` bool | `y + (add ? d↑ : 0) ≥ anchorY + sd * multiplier` (**inclusive**) | y_above.json, 10 cases x 5 columns |
| anchors | | `absolute n` → n; `above_bottom n` → -64 + n; `below_top n` → 319 - n (top = min_y + height - 1) | above_bottom 5 → -59, below_top 10 → 309 |
| `water` | `offset` int, `surface_depth_multiplier` int, `add_stone_depth` bool | `W` none → **true**; else `y + (add ? d↑ : 0) ≥ W + offset + sd * multiplier` (inclusive) | water.json, 16 water depths x 5 variants + gap cases |
| `noise_threshold` | `noise` id, `min_threshold`, `max_threshold` doubles, `is_3d` bool (optional, default false) | `min ≤ v ≤ max`, **both inclusive**; v = noise(x, 0, z), or noise(x, y, z) with `is_3d` | noise_threshold.json: 512/512 columns; min = v and max = v true, nextUp/nextDown false; 3D 135/135 |
| `vertical_gradient` | `random_name` id, `true_at_and_below`, `false_at_and_above` anchors | `y ≤ T` → true; `y ≥ F` → false; between, true with probability `(F - y) / (F - T)` from a deterministic per-block random of (seed, random_name, x, y, z) | vertical_gradient.json (frequencies below) |
| `steep` | none | `h(x, z+1) - h(x, z-1) ≥ 4` **or** `h(x-1, z) - h(x+1, z) ≥ 4`; neighbours are **clamped to the chunk's 0..15**; h is the chunk's world-surface heightmap (water counts) | steep.json, 20 slopes + 3 plateaus; extra.json underwater |
| `hole` | none | `sd ≤ 0` (observed: sd = 0) | 768/768 columns, 256 holes (extra.json) |
| `above_preliminary_surface` | none | `y ≥ floor(P) + sd - 8` | 1,280/1,280 |
| `biome` | `biome_is`: one id string or a list | the block's (zoomed, 3D) biome is in the set | 7,936/7,936 |
| `not` | `invert`: a condition | the inverse | used 4 times |
| `temperature` | none | not used by overworld; not probed | |

Notes that matter for the evaluator:
- **`add_stone_depth` turns a y test into a test of the run's top.** In a solid run whose top is at `t`, every block has `y + d↑ = t + 1`.
  - So `y_above(A, m, add)` is true for the whole run or for none of it.
  - `water(o, m, add)` measures the water depth above the run's top: with `D = W - 1 - t`, the number of fluid blocks directly over the run, `not_under_deep_water` is `D ≤ 6 + sd` (water.json: sd 4, true up to depth 10).
- **`not_underwater` (offset -1, multiplier 0, no stone depth) is "no fluid since the last air".** Any stone with fluid directly above it has `y ≤ W - 2`. This includes lava.
- **Steep is direction-dependent.**
  - Only slopes that **rise toward +z** (south) or **rise toward -x** (west) are steep, that is faces looking north or east.
  - Slopes rising toward +x or -z are never steep, at any steepness.
  - A plane rising 2 or 3 blocks per column is steep inside the chunk but not at its first and last column (clamped neighbour, difference 2 or 3); at 4 or more per column every column is steep.
  - At a 4-block step, both the low column in front of the cliff and the top edge are steep.
- **`max_threshold` 1.7976931348623157e+308** (Double.MAX) appears as "no upper bound".

Vertical gradient frequencies (fraction of marked blocks at each y; seed 0 and seed 1):

| | T | F | y = T+1 | T+2 | T+3 | T+4 | T+5 | T+6 | T+7 | expected (F - y)/(F - T) |
|---|---|---|---|---|---|---|---|---|---|---|
| bedrock_floor (1,024 columns) | -64 | -59 | 0.793 / 0.797 | 0.601 / 0.602 | 0.402 / 0.399 | 0.191 / 0.198 | 0 | | | 0.8, 0.6, 0.4, 0.2 |
| deepslate (4,096 columns) | 0 | 8 | 0.879 / 0.877 | 0.758 / 0.741 | 0.630 / 0.617 | 0.495 / 0.492 | 0.367 / 0.379 | 0.249 / 0.242 | 0.126 / 0.128 | 0.875 ... 0.125 |

At y ≤ T the fraction is exactly 1, and at y ≥ F exactly 0.

Determinism (1,792 positions):
- Re-running the same chunk, even after another chunk ran in between, gives identical results.
- Another seed changes 783 positions; another `random_name` changes 644.
- **Another biome changes 0:** the random does not depend on the biome.

### 1.4 The `bandlands` rule (badlands terracotta)

- The band is a function of (seed, y, column offset): `band(y) = pattern[(y + round(clay_bands_offset(x, 0, z) * 4)) mod 192]`.
- The pattern has a **period of 192** and differs between seeds.
- **Rounding is to nearest:** at seed 1, offsets -0.569..-0.608 (×4 → -2.27..-2.43) gave one phase, and -0.638 (×4 = -2.55) was shifted by one block. Floor would have put all five in one phase (bands.json).
- Pattern statistics over one period (seeds 0 and 1):
  - Plain terracotta is 87 to 94 of 192, in runs of 1..5.
  - Orange is 27 to 37, always single blocks.
  - Yellow 8 to 20 (runs 1..6), brown 14 to 17 (1..4), red 15 to 21 (1..3).
  - White 9 to 11 and light gray 10 to 14, singles, usually next to each other.
- The plan already calls for **our own** band pattern from `clay_bands_offset`. These numbers are its look target, not something to copy.

### 1.5 The overworld tree in short (per biome, what a floor gets)

The full trees are in `scripts\surface_tree.txt`. The `surface` part, in order:
1. **On floor**, three biome-specific cases:
   - wooded_badlands at `y_above 97, m 2`: coarse dirt in three surface-noise bands, [-0.909, -0.5454], [-0.1818, 0.1818] and [0.5454, 0.909], else default.
   - swamp: y = 62 only (`y_above 62` and not `y_above 63`), with `surface_swamp ≥ 0` → **water**.
   - mangrove_swamp: y 60..62 with the same noise → water. Only the top block is on the floor, so in practice only the top changes.
2. **The badlands family** (badlands, eroded, wooded):
   - On floor: y ≥ 256 → orange terracotta.
   - `y_above 74, m 1, +stone`: the same three noise bands → terracotta, else bandlands.
   - Not underwater: red sandstone if on a ceiling, else red sand.
   - Not a hole: orange terracotta.
   - Not under deep water: white terracotta.
   - Else gravel, or stone on a ceiling.
   - Then, for `y_above 63, m -1, +stone`: y ≥ 63 and not `y_above 74, m 1, +stone` → orange terracotta, else bandlands. Under the floor, not under deep water → white terracotta.
3. **On floor and not underwater:**
   - frozen_ocean and deep_frozen_ocean with a hole → **air**.
   - Then `biome_surface`: per-biome tops; default is grass block, or dirt underwater.
4. **Not under deep water:**
   - frozen oceans: on floor with a hole → water.
   - Under the floor → `under_biome_surface`; default is dirt.
   - warm_ocean, beach and snowy_beach: `deep_under_floor` → sandstone.
   - desert: `very_deep_under_floor` → sandstone.
5. **On floor, otherwise** (deep water and the rest):
   - frozen_peaks and jagged_peaks → stone.
   - warm_ocean, lukewarm_ocean and deep_lukewarm_ocean → sand, or sandstone on a ceiling.
   - Everything else → gravel, or stone on a ceiling.

The per-biome tops (`biome_surface/*`). Noise ranges are inclusive.

| Biome | Top (floor, not underwater) | Under (`under_biome_surface`) |
|---|---|---|
| frozen_peaks | steep → packed ice; `packed_ice` noise [0, 0.2] → packed ice; `ice` [0, 0.025] → ice; else snow block | steep → packed ice; packed_ice [-0.5, 0.2]; ice [-0.0625, 0.025]; snow block |
| snowy_slopes | steep → stone; `powder_snow` [0.35, 0.6] → powder snow; snow block | steep → stone; powder_snow [0.45, 0.58]; snow block |
| jagged_peaks | steep → stone; snow block | stone |
| grove | powder_snow [0.35, 0.6]; snow block | powder_snow [0.45, 0.58]; dirt |
| stony_peaks | `calcite` [-0.0125, 0.0125] → calcite; stone | (same rule) |
| stony_shore | `gravel` [-0.05, 0.05] → gravel (stone on a ceiling); stone | (same) |
| windswept_hills | surface ≥ 0.1212 → stone; else default | (same) |
| warm_ocean, beach, snowy_beach, desert | sand (sandstone on a ceiling) | (same) |
| dripstone_caves | stone | (same) |
| sulfur_caves | `sulfur_cave_gradient` bands: [-0.4, -0.1] cinnabar, [0, 0.4] sulfur, ≥ 0.4 cinnabar; else stone | (same) |
| mangrove_swamp | mud | (same) |
| windswept_savanna | surface ≥ 0.2121 → stone; ≥ -0.0606 → coarse dirt; else default | surface ≥ 0.2121 → stone; else dirt |
| windswept_gravelly_hills | surface ≥ 0.2424 → gravel; ≥ 0.1212 → stone; ≥ -0.1212 → default; else gravel | the same, with dirt for default |
| old_growth_pine_taiga, old_growth_spruce_taiga | surface ≥ 0.2121 → coarse dirt; ≥ -0.1152 → podzol; else default | dirt |
| ice_spikes | snow block | dirt |
| mushroom_fields | mycelium | dirt |
| dappled_forest | `small_patch` ≥ 1.2 → coarse dirt; else default | dirt |
| everything else | grass block, or dirt underwater | dirt |

"(same)": inside `under_biome_surface`, the shared middle block uses the same `biome_surface/*` rule for stony_peaks, stony_shore, windswept_hills, beaches, desert, dripstone, sulfur caves and mangrove.

Noises the surface uses (26.3 noise JSON format: `base_amplitude`, `base_octave`, `octave_count`, `amplitude_modifiers`):

| Noise | base_octave | octave_count | base_amplitude | modifiers |
|---|---|---|---|---|
| `surface` | -6 | 3 | 0.93817 | |
| `surface_secondary` | -6 | 4 | 1.05828 | [1, 1, 0, 1] |
| `surface_swamp` | -2 | 1 | 0.95539 | |
| `packed_ice` | -7 | 4 | 0.93955 | |
| `ice` | -4 | 4 | 0.93955 | |
| `powder_snow` | -6 | 4 | 0.93955 | |
| `calcite` | -9 | 4 | 0.93955 | |
| `gravel` | -8 | 4 | 0.93955 | |
| `small_patch` | -3 | 1 | 0.95539 | [3] |
| `sulfur_cave_gradient` | -5 | 3 | 1.18175 | [1, 0, 1] |
| `clay_bands_offset` | -8 | 1 | 0.95539 | |

These are the project's existing noise readers' input, and all are sampled at (x, 0, z).

### 1.6 Code-side additions the data does not describe

`buildSurface` adds blocks *above* the terrain in two biome families. Nothing in `material_rule` describes this; the shapes come from noises that exist in `noise/`:
- `iceberg_pillar`, `iceberg_pillar_roof`, `iceberg_surface`
- `badlands_pillar`, `badlands_pillar_roof`, `badlands_surface`

What I observed (extensions.json, 8 chunks per case):
- **Eroded badlands pillars ("hoodoos"):**
  - On a flat surface at 66, 604 of 2,048 columns rose, to at most y = 93, built of the band colours (terracotta 1,528 blocks, orange 1,198, yellow 927, brown 605, light gray 589, white 442, red sand 365 on tops).
  - On a surface at 90, only 53 columns rose, still to at most 93. So the pillar tops sit at absolute heights, not heights above the ground.
  - Raised columns had |badlands_surface| ≥ 0.133.
- **Frozen ocean icebergs:**
  - frozen_ocean and deep_frozen_ocean over a sea floor at 40 with water to 62: 362 and 359 of 2,048 columns got packed ice, up to y = 81 above the sea and replacing water down to about 52, with water gaps inside.
  - They also appear over land when the biome is assigned there (frozen_ocean on land at 66: 235 columns, up to 81).
  - **Only packed ice** appeared, never snow blocks, at seed 0 over these chunks. 53% of frozen-ocean area is not cold enough to snow; see 3.2.
  - Raised columns had |iceberg_surface| ≥ 0.22.

This is a design call for session 5 (gap G2): skip both for now, or build our own versions from these noises. The plan's acceptance list asks for "frozen peaks" and ocean variants, not icebergs or hoodoos.

---

## 2. Test cases for `Surface.Conditions` (copyable)

The inputs are the context values of 1.2, so tests can build a context by hand instead of running terrain. Each expected result follows from a black-box-confirmed rule above, and the cases marked **BB** were themselves run in the jar (file named).

### 2.1 Column-context scan (build these columns, check d↑, d↓, W)

| # | Column, bottom → top | Block y | d↑ | d↓ | W | Source |
|---|---|---|---|---|---|---|
| C1 | stone -64..39, air 40..49, stone 50..70, air | 70 | 1 | 21 | none | BB stone_depth.json `cave` |
| C2 | same | 50 | 21 | 1 | none | BB (`on_ceiling` marks 50) |
| C3 | same | 39 | 1 | 104 | none | BB (`on_floor` marks 39) |
| C4 | same | -64 | 104 | 1 | none | BB (`on_ceiling` marks -64) |
| C5 | stone ≤ 60, water 61..70, air | 60 | 1 | 125 | 71 | BB `underwater`, water.json |
| C6 | stone ≤ 40, water 41..45, stone 46..60, air | 46 | 15 | 1 | none | BB `water_pocket` |
| C7 | same | 40 | **16** | 105 | **46** | BB (40 not `on_floor`); W from the pocket |
| C8 | stone ≤ 59, water 60..69, stone 70..79, water 80..89, air | 59 | **11** | 124 | **90** | BB extra.json |
| C9 | stone ≤ 40, air 41..50, stone 51..60, water 61..70, air | 40 | 1 | | **none** (air resets) | BB water.json gap case |
| C10 | stone ≤ 60, lava 61..70, air | 60 | 1 | | **71** (lava counts) | BB extra.json |
| C11 | stone ≤ 66, air 67, stone 68, air 69, stone 70, air | 70 / 68 / 66 | 1 / 1 / 1 | 1 / 1 / 131 | none | BB `thin` |

For C7: W is set by the first fluid after the last air. The pocket 41..45 is below the stone 46..60, so W = 46 applies to y ≤ 40.

### 2.2 stone_depth

| # | Variant | Context | Expected |
|---|---|---|---|
| S1 | on_floor | d↑ = 1 | true |
| S2 | on_floor | d↑ = 2 | false |
| S3 | floor, offset 2 | d↑ = 3 / 4 | true / false (BB: marks 70..68) |
| S4 | under_floor | sd 5: d↑ = 6 / 7 | true / false (BB at column 0,0: 70..65) |
| S5 | under_floor | sd 0: d↑ = 1 / 2 | true / false |
| S6 | deep_under_floor (R 6) | sd 4, ss 0 (sec = 3): d↑ = 8 / 9 | true / false |
| S7 | deep_under_floor | sd 4, ss -1.3 (sec = trunc(-0.9) = 0): d↑ = 5 / 6 | true / false |
| S8 | deep_under_floor | sd 4, ss -1.44 (sec = trunc(-1.32) = -1): d↑ = 4 / 5 | true / false |
| S9 | very_deep_under_floor (R 30) | sd 5, ss -0.0907 (sec 13): d↑ = 19 / 20 | true / false (BB desert column x0: sand*6 sandstone*13) |
| S10 | very_deep_under_floor | sd 4, ss -1.44 (sec = trunc(-6.6) = -6): d↑ = 1 | **false** (limit -1; BB: 0 marked) |
| S11 | very_deep_under_floor | sd 4, ss 1.4 (sec = trunc(36) = 36): d↑ = 41 / 42 | true / false (unclamped) |
| S12 | floor, add false, R 6 | sd 5, ss -0.0907 (sec 2): d↑ = 3 / 4 | true / false (BB `range6_noSD`: 70..68) |
| S13 | on_ceiling | d↓ = 1 / 2 | true / false |
| S14 | ceiling, offset 2, add true | sd 5: d↓ = 8 / 9 | true / false (BB: 57..50) |

### 2.3 y_above

| # | Variant | Context | Expected |
|---|---|---|---|
| Y1 | absolute 70, m 0 | y 70 / 69 | true / false (BB) |
| Y2 | absolute 70, m 1 | sd 5: y 75 / 74 | true / false (BB) |
| Y3 | absolute 70, m 2 | sd 3: y 76 / 75 | true / false (BB column 15,15) |
| Y4 | absolute 70, m -1 | sd 5: y 65 / 64 | true / false (BB) |
| Y5 | above_bottom 5 | y -59 / -60 | true / false (BB) |
| Y6 | below_top 10 | y 309 / 308 | true / false (BB) |
| Y7 | absolute 61, +stone | run top 60, any block of the run | true (y + d↑ = 61) (BB) |
| Y8 | absolute 62, +stone | run top 60 | false for every block (BB) |
| Y9 | absolute 63, m -1, +stone (badlands) | sd 3: run top 59 / 58 | true / false (60 ≥ 60, 59 < 60) |
| Y10 | absolute 74, m 1, +stone (badlands) | sd 3: run top 76 / 75 | true / false (77 ≥ 77, 76 < 77) |
| Y11 | swamp: y_above 62 and not y_above 63 | y 62 / 61 / 63 | true / false / false |

### 2.4 water

| # | Variant | Context | Expected |
|---|---|---|---|
| W1 | not_underwater | W none | true (BB) |
| W2 | not_underwater | y 60, W 62 (one water block above) | false (BB lx 1) |
| W3 | water offset 0 | y 60, W 62 | false |
| W4 | water offset -1, +stone | run top 60, W 62 | true (61 ≥ 61) (BB lx 1) |
| W5 | water offset -1, +stone | run top 60, W 63 | false (BB lx 2) |
| W6 | not_under_deep_water | sd 4, run top 60, W 71 (10 deep) / W 72 (11 deep) | true / false (BB lx 10 / 11) |
| W7 | not_underwater | C9 (cave floor below an air gap, sea above) | true (BB) |
| W8 | not_underwater | C10 (under lava) | false (BB) |
| W9 | water offset -25 | C8, y 59 (W = 90) | false (59 < 65; BB) |

### 2.5 noise_threshold

| # | Context | Expected |
|---|---|---|
| N1 | v = 0.0, [-0.2, 0.3] | true |
| N2 | v = min exactly | true (BB, v = 0.5557277202606201) |
| N3 | v = max exactly | true (BB) |
| N4 | min = max = v | true (BB) |
| N5 | v = nextDown(min) / nextUp(max) | false / false (BB) |
| N6 | max 1.7976931348623157e+308, v = 5 | true |
| N7 | `is_3d` absent | samples (x, 0, z), so every y of a column agrees (BB 512/512) |
| N8 | `is_3d: true` | samples (x, y, z) (BB 135/135) |

### 2.6 steep (heights as `h(x, z)`, column inside a chunk)

| # | Context | Expected |
|---|---|---|
| T1 | h(x, z+1) = 68, h(x, z-1) = 64 | true (rises toward +z by 4) |
| T2 | h(x, z+1) = 64, h(x, z-1) = 68 | **false** (rises toward -z) |
| T3 | h(x-1, z) = 68, h(x+1, z) = 64 | true (rises toward -x) |
| T4 | h(x+1, z) = 68, h(x-1, z) = 64 | **false** (rises toward +x) |
| T5 | difference 3 either way | false |
| T6 | lz = 0 on a plane rising 2 per column toward +z: z-1 clamps to lz 0, difference 2 | false (BB grid row 0) |
| T7 | lz = 0, plane rising 4 per column | true (BB) |
| T8 | a slope under a flat sea at 62 | false (water counts in h; BB) |
| T9 | plateau of height 4 at lx, lz 3..12 on flat ground | steep cells: row lz 2 (lx 3..12), row lz 3 (lx 3..13), columns lx 12 and 13 (lz 3..12); 39 in all (BB steep.json) |

### 2.7 hole, above_preliminary_surface, vertical_gradient, biome

| # | Condition | Context | Expected |
|---|---|---|---|
| H1 | hole | sd 0 / 1 | true / false (BB 768/768) |
| P1 | above_preliminary_surface | P 40.5, sd 5 (min level 37): y 37 / 36 | true / false (BB column 0,1) |
| P2 | | P 40.0, sd 5: y 37 / 36 | true / false (BB column 0,0) |
| P3 | | P 41.9375, sd 0: y 33 / 32 | true / false |
| G1 | vertical_gradient deepslate (T 0, F 8) | y 0 / y 8 | always true / always false |
| G2 | | y 4, rand r | true iff r < 0.5 (frequency 0.495 over 4,096 columns) |
| G3 | | the same (seed, name, x, y, z) twice | the same result; another biome gives the same result |
| B1 | biome | the block's own biome in the set | true; the biome is per block, not per column (1.2) |

### 2.8 Whole-tree goldens (overworld rule; hand-made columns; seed 0)

The inputs per column (x, z = 0) are in golden_columns.json `inputs_seed0`:
- x0: sd 5, surface 0.7882, ss -0.0907, packed_ice -0.1563, ice -0.0654, swamp -0.5108.
- x9: sd 4, surface 0.552, ss 0.0907, packed_ice 0.0263, swamp 0.4305.

Columns read top down:

| Biome | Profile | x0 | x9 |
|---|---|---|---|
| plains | stone ≤ 70, air | grass, dirt*5 | grass, dirt*4 |
| plains | stone ≤ 50, water 51..62 (12 deep) | gravel, then stone | gravel |
| desert | ≤ 70 | sand*6, sandstone*13 | sand*5, sandstone*16 |
| beach | ≤ 70 | sand*6, sandstone*2 | sand*5, sandstone*3 |
| warm_ocean | 12 deep water | sand | sand |
| frozen_peaks | ≤ 70 | snow block, packed ice*5 | packed ice*5 (noise 0.026 is in [0, 0.2]) |
| frozen_peaks | 12 deep water | stone | stone |
| jagged_peaks | ≤ 70 | snow block, then stone | snow block |
| snowy_slopes | ≤ 70 | snow block*6 | snow block*5 |
| grove / ice_spikes | ≤ 70 | snow block, dirt*5 | snow block, dirt*4 |
| swamp | stone ≤ 62 | grass, dirt*5 | **water at 62**, dirt*4 |
| mangrove_swamp | ≤ 62 | mud*6 | water at 62, mud*4 |
| badlands | ≤ 70 | red sand, orange terracotta*7 (y 69..63), then bands | the same |
| badlands | ≤ 140 | terracotta (surface 0.788 is in [0.5454, 0.909]), then bands | terracotta, bands |
| wooded_badlands | ≤ 140 | coarse dirt, then bands | coarse dirt (x10..15 with surface < 0.5454: grass block) |
| old_growth_pine_taiga | ≤ 70 | coarse dirt, dirt*5 | coarse dirt, dirt*4 |
| windswept_gravelly_hills | ≤ 70 | gravel*6 | gravel*5 |
| stony_shore / stony_peaks / windswept_hills | ≤ 70 | stone | stone |

---

## 3. Per-block temperature and the snow line

### 3.1 Inputs and numbers

- **Base temperature T** and **downfall**: per-biome `temperature` and `downfall` in `biome/<id>.json`; the full table is in `biome_climate_colours.json`.
- **Frozen modifier:** only `frozen_ocean` (T 0.0) and `deep_frozen_ocean` (**T 0.5**) have `"temperature_modifier": "frozen"`. Every other biome is `none`. The modifier replaces T before the height rule; see 3.2.
- **Height rule** (temperature.json: 8 columns x 17 heights x 2 sea levels, bisected to float precision against the jar's `coldEnoughToSnow`):
  - `y ≤ seaLevel + 17` (y ≤ 80): `temp = T'`, with no height term and no noise.
  - `y > seaLevel + 17`: `temp = T' - (y - (seaLevel + 17) + N(x, z)) / 800`. The slope is 0.05 per 40 blocks.
  - The threshold moves with the sea level passed in: with sea level 0, y = 62 already loses 45 + N.
- **N(x, z)** is a smooth 2D noise in blocks, independent of y and with no world-seed input (the jar's API has no seed).
  - It is exactly 0 at (0, 0), (-64, 64) and (30000, -30000).
  - Observed -6.1..+6.6 over 41 points. Along z = 0 it changes by up to about 6 blocks per 2 blocks of x, which suggests a feature size near 8 blocks.
  - A matching stand-in: amplitude about ±8 blocks, one octave at about 1/8 frequency.
  - N can be negative, so just above y = 80 the temperature can rise by a little (y 81 at (7, 7): +1.55/800).
- **The snow test is strict:** a block is cold enough to snow when `temp < 0.15`. temp = 0.15 exactly is not cold (taiga at y 160 with N = 0).
- **Two other thresholds the jar uses:**
  - `warmEnoughToRain` is the inverse (temp ≥ 0.15).
  - `shouldMeltFrozenOceanIcebergSlightly` is `temp > 0.1`: 2,164 of 4,000 frozen-ocean points at y 63, exactly the share where the modifier gives 0.2.

### 3.2 The frozen modifier (frozen_modifier.json; fits 4,000/4,000)

`T' = 0.2` when `F(x * 0.05, z * 0.05) * 7 + I(x * 0.2, z * 0.2) < 0.3` **and** `I(x * 0.09, z * 0.09) < 0.8`; otherwise `T' = T`.

- F is the jar's public `Biome.FROZEN_TEMPERATURE_NOISE` and I is `Biome.BIOME_INFO_NOISE`. Both have fixed seeds and ignore the world seed.
  - Observed F -0.758..0.754 and I -0.998..0.998; the jar reports `range()` [-2, 2] for both.
- It does not depend on y (4,000 of 4,000 equal at y + 37) or on T. It changes 53% of positions.
- Consequences:
  - **frozen_ocean:** 47% of its surface is 0.0 (freezes, takes snow) and 53% is 0.2 (open water, no snow; icebergs "melt slightly").
  - **deep_frozen_ocean is never cold enough at sea level:** its T' is 0.5 or 0.2. Its water does not freeze in the freeze pass; only its surface-pass icebergs (1.6) show ice.

### 3.3 Worked examples

The snow line is the first y with `temp < 0.15`. For T' between 0.15 and 0.45 it is `y_snow = floor(80 + (T' - 0.15) * 800 - N) + 1`.

| Biome (T) | y 64 | y 100 | y 160 | y 200 | y 319 | Snow line (N = 0) | Line at (-2000, 513), N = +3.566 |
|---|---|---|---|---|---|---|---|
| plains (0.8) | 0.8 | 0.775 | 0.7 | 0.65 | 0.50125 | never (it would need y > 600) | never |
| meadow, cherry_grove (0.5) | 0.5 | 0.475 | 0.4 | 0.35 | 0.20125 | never (y > 360) | never |
| old_growth_pine_taiga (0.3) | 0.3 | 0.275 | 0.2 | **0.15 (not cold)** | 0.00125 | 201 | 197 |
| taiga, old_growth_spruce (0.25) | 0.25 | 0.225 | 0.15 (not cold) | 0.1 | -0.04875 | 161 | 157 |
| windswept_*, stony_shore (0.2) | 0.2 | 0.175 | 0.1 | 0.05 | -0.09875 | 121 | 117 |
| snowy_beach (0.05), snowy_plains, ice_spikes, frozen_river (0.0) | < 0.15 | | | | | everywhere | everywhere |
| grove (-0.2), snowy_slopes (-0.3), snowy_taiga (-0.5), frozen/jagged peaks (-0.7) | < 0.15 | | | | | everywhere | everywhere |
| stony_peaks (1.0) | 1.0 | 0.975 | 0.9 | 0.85 | 0.70125 | never | never |
| savanna, desert, badlands (2.0; no precipitation) | 2.0 | | | | | never | never |
| frozen_ocean (0.0, frozen) | 0.0 or 0.2 | | | | | everywhere where T' = 0; from y 121 where T' = 0.2 | |

BB check: the feature run on a T = 0.3 biome (freeze.json `snow_line_T0.3`):
- tops 180..192 had no snow;
- top 196 (snow at y 197) had snow in 7 of 16 columns;
- top 200 (y 201) in 13 of 16;
- tops of 204 and above in every column.

This matches `y > 200 - N` with N in about ±6.

**For the session 5 test `Surface.Snow`:** in a cold-band biome (T 0.2 to 0.3) the snow line sits at y 121 to 201, give or take N. That is low enough for normal mountains. stony_peaks (T 1.0) and meadow or cherry (T 0.5) never get snow in vanilla. A peak whose top is snowy while its slopes are not comes either from the peak biomes (frozen/jagged peaks, snowy slopes, with snow blocks from the surface rules) or from taiga-like and windswept biomes crossing their height line.

---

## 4. The freeze pass (observed: the jar's `SnowAndFreezeFeature` on a map-backed level; freeze.json)

### 4.1 What the feature does, per column of its chunk (16 x 16 from the origin; it never wrote outside the chunk)

- It reads the **MOTION_BLOCKING** heightmap. `top` is the first y above the highest block that blocks motion **or holds fluid**; leaves count.
- It reads the **biome at (x, top, z)** and uses it for both checks below.
  - A split level (cold from y 63 up, warm below) froze water at y 62.
  - A split at y 71 snowed on grass at 70.
- The level's sea level (63) feeds the temperature.
- **Ice:** if the block at `top - 1` passes `shouldFreeze` with no edge requirement, it becomes **ice**.
  - This needs a water **source** (a flowing level-1 surface did not freeze), temp < 0.15, block light < 10, and y in -64..319. Water fully surrounded by water still freezes.
  - Lava never freezes.
- **Snow:** if `(x, top, z)` passes `shouldSnow`, it becomes **one snow layer** (`layers=1`). If the block below has a `snowy` property (grass block, podzol, mycelium), that block becomes `snowy=true`.
  - Both can happen in one column. They did not in the tests, because snow cannot stand on the new ice.
- **No snow blocks and no powder snow:** those come only from the surface rules (1.5).

### 4.2 `shouldSnow(pos)` and `shouldFreeze(pos)` (y = 64 unless noted)

| Check | Cold, light 0 or 9 | Cold, light 10 | Warm |
|---|---|---|---|
| freeze: water source at pos | **true** | false | false |
| freeze: water level 1, ice, air, stone, lava | false | false | false |
| freeze with "must be at edge": source with 4 water neighbours / with 1 stone neighbour | false / true | false | false |
| snow: air at pos over stone | **true** | false | false |
| snow: a 1-layer or 7-layer snow block at pos | true (layers below 8 accept more) | false | false |
| snow or freeze at y -65 / -64 / 319 / 320 | false / true / true / false | | |

Block light matters only once lighting exists. During generation our light is not computed yet, so treat it as 0.

**Snow can rest on** (cold, air at pos, the block below varied):
- **Yes:** stone, grass block, dirt, sand, gravel, **blue ice**, snow block, snow with **8 layers**, oak and spruce leaves, glass, **top** slab, mud, soul sand, honey block, podzol, mycelium.
- **No:** **ice, packed ice**, snow with 1 to 7 layers, **powder snow**, water, air, **bottom** slab, barrier, dirt path, farmland, lava, fence, torch.

This is data-driven through two block tags, both in the jar's data and in `block_manifest.json` tags:
- `cannot_support_snow_layer` = {ice, packed_ice, barrier} blocks snow.
- `support_override_snow_layer` = {honey_block, soul_sand, mud} allows snow even where the top face is not full.
- Otherwise snow needs a full top face below. Snow layers count only at 8 layers; bottom slabs, paths and farmland have no full top face.

### 4.3 Scene results (cold biome T = -0.5)

| Scene, top block | Change |
|---|---|
| grass at 70 | 70 grass → `snowy`, 71 air → snow layer 1 |
| water surface at 62 (7 deep) | 62 water → ice; nothing above |
| 1-deep water over grass | 62 → ice |
| flowing water (level 1) at 62 | nothing |
| oak leaves at 70 on a log | 71 → snow layer |
| sand, dirt, glass, snow block | snow layer on top |
| podzol, mycelium | snowy + snow layer |
| ice, packed ice, powder snow, bottom slab | nothing |
| lava at 62 | nothing |

A warm biome changed nothing.

---

## 5. Session 6: water colour, grass and foliage tints

### 5.1 Where each value lives

`biome/<id>.json`:
- `effects.water_color`: every biome has one.
- `effects.grass_color`, `effects.foliage_color`, `effects.dry_foliage_color`: optional overrides.
- `effects.grass_color_modifier`: `swamp` or `dark_forest`; absent means none.
- `temperature` and `downfall`.

The `attributes` map holds `minecraft:visual/sky_color` (all 56 biomes), `visual/water_fog_color` (9), `visual/fog_color` (4) and `visual/water_fog_end_distance` (2, a multiply of 0.85).

Defaults not in the reference (environment_attribute_defaults.json):
- water fog `#050533` (0.0196, 0.0196, 0.2), water fog start -8 and end 96;
- fog end 1024;
- overworld `fog_color #c0d8ff` and `sky_color #78a7ff`, which are in `dimension_type/overworld.json`;
- all are spatially interpolated.

### 5.2 The colormap lookup (colors.json; 40,401 of 40,401 grid points match, for grass and for foliage)

- The colormaps are in the reference: `Reference/minecraft-blocks-26.3/assets/minecraft/textures/colormap/grass.png` and `foliage.png`, each 256 x 256.
- With `t` = temperature and `d` = downfall, both already in [0, 1]:
  - `dd = d * t`
  - `px = trunc((1 - t) * 255)`, `py = trunc((1 - dd) * 255)`
  - `color = pixel[py * 256 + px]`
  - The downfall is scaled by the temperature, so lookups stay in the colormap's valid lower-left triangle.
- **The lookup itself does not clamp.** Out-of-range inputs read the wrong pixel (`t 0.05, d 1.5` → `#FFFFFF`), fall back (`#FF00FF` for grass, `#48B518` for foliage), or throw (t > 1). So:
- **The biome clamps first:** `clamp(T, 0, 1)` and `clamp(downfall, 0, 1)` of the **base** temperature. Per-block height, noise and the frozen modifier play no part.
  - frozen_ocean grass is `#80B497` (T 0), and desert (T 2.0) is `#BFB755`, the same as T 1, D 0.
- **Grass** = `grass_color` if present, else the colormap; then the **modifier** at the block's (x, z):
  - `swamp`: the result ignores the colormap colour. It is `#4C763C` where `I(x * 0.0225, z * 0.0225) < -0.1`, else `#6A7039`. I is the fixed-seed `BIOME_INFO_NOISE`; 43% of positions are `#4C763C`. 20,000 of 20,000 match.
  - `dark_forest`: `((c & 0xFEFEFE) + 0x28340A) >> 1` of the colormap colour c, a 50% blend toward `#28340A`. 20,000 of 20,000 match; dark forest grass is `#507A32`.
- **Foliage** = `foliage_color` if present, else the colormap at the clamped base climate. No modifier applies, and it does not change with position.
- **Water** = `water_color` exactly. The jar's `getWaterColor` matched the data for every biome.
- Constants: `FOLIAGE_EVERGREEN #619961` (spruce leaves), `FOLIAGE_BIRCH #80A755` (birch), `FOLIAGE_DEFAULT #48B518`, `FOLIAGE_MANGROVE #92C648` (an item constant), `GrassColor.getDefaultColor #7CBD6B` (the grass item), `FOLIAGE_DRY_DEFAULT #5C3C32`.
- Which blocks take which tint: the block reference README already records it from `BlockColors`.
  - grass_block uses grass; oak, jungle, acacia, dark oak and mangrove leaves use foliage; spruce and birch use constants; water uses the water colour.
  - `block_manifest.json` has the `tint` field.

### 5.3 Per-biome results from the jar (the jar's `getWaterColor`, `getGrassColor`, `getFoliageColor`)

| Biome | T / D | Water | Grass | Foliage |
|---|---|---|---|---|
| plains, sunflower_plains, beach, dripstone_caves, deep_dark | 0.8 / 0.4 | #3F76E4 | #91BD59 | #77AB2F |
| forest, flower_forest | 0.7 / 0.8 | #3F76E4 | #79C05A | #59AE30 |
| dark_forest | 0.7 / 0.8, dark_forest modifier | #3F76E4 | #507A32 | #59AE30 |
| birch_forest, old_growth_birch_forest | 0.6 / 0.6 | #3F76E4 | #88BB66 | #6BA940 |
| jungle, bamboo_jungle | 0.95 / 0.9 | #3F76E4 | #59C93C | #30BB0B |
| sparse_jungle | 0.95 / 0.8 | #3F76E4 | #64C73F | #3EB80F |
| swamp | 0.8 / 0.9, swamp modifier | **#617B64** | #4C763C or #6A7039 | **#6A7039** (override) |
| mangrove_swamp | 0.8 / 0.9, swamp modifier | **#3A7A6A** | #4C763C or #6A7039 | **#8DB127** (override) |
| badlands, eroded, wooded | 2.0 / 0.0 | #3F76E4 | **#90814D** (override) | **#9E814D** (override) |
| desert, savanna, savanna_plateau, windswept_savanna | 2.0 / 0.0 | #3F76E4 | #BFB755 | #AEA42A |
| stony_peaks | 1.0 / 0.3 | #3F76E4 | #9ABE4B | #82AC1E |
| mushroom_fields | 0.9 / 1.0 | #3F76E4 | #55C93F | #2BBB0F |
| meadow | 0.5 / 0.8 | **#0E4ECF** | #83BB6D | #63A948 |
| cherry_grove | 0.5 / 0.8 | **#5DB7EF** | **#B6DB61** | **#B6DB61** |
| pale_garden | 0.7 / 0.8 | **#76889D** | **#778272** | **#878D76** |
| dappled_forest (new) | 0.6 / 0.6 | **#375154** | **#DF6827** | **#E68E30** |
| sulfur_caves (new) | 0.8 / 0.4 | **#34BF89** | **#ABA64F** | #77AB2F |
| ocean, deep_ocean, river, lush_caves | 0.5 / 0.5 | #3F76E4 | #8EB971 | #71A74D |
| warm_ocean | 0.5 / 0.5 | **#43D5EE** | #8EB971 | #71A74D |
| lukewarm_ocean, deep_lukewarm_ocean | 0.5 / 0.5 | **#45ADF2** | #8EB971 | #71A74D |
| cold_ocean, deep_cold_ocean | 0.5 / 0.5 | **#3D57D6** | #8EB971 | #71A74D |
| frozen_ocean | 0.0 / 0.5 (frozen) | **#3938C9** | #80B497 | #60A17B |
| deep_frozen_ocean | 0.5 / 0.5 (frozen) | **#3938C9** | #8EB971 | #71A74D |
| frozen_river | 0.0 / 0.5 | **#3938C9** | #80B497 | #60A17B |
| snowy_beach | 0.05 / 0.3 | **#3D57D6** | #83B593 | #64A278 |
| snowy_taiga | -0.5 / 0.4 | **#3D57D6** | #80B497 | #60A17B |
| snowy_plains, ice_spikes, grove, snowy_slopes, frozen_peaks, jagged_peaks | ≤ 0 | #3F76E4 | #80B497 | #60A17B |
| taiga, old_growth_spruce_taiga | 0.25 / 0.8 | #3F76E4 | #86B783 | #68A464 |
| old_growth_pine_taiga | 0.3 / 0.8 | #3F76E4 | #86B87F | #68A55F |
| windswept_hills, _forest, _gravelly_hills, stony_shore | 0.2 / 0.3 | #3F76E4 | #8AB689 | #6DA36B |

Dry foliage overrides exist for dappled_forest `#8C3A04`, dark_forest, swamp and mangrove `#7B5334`, and pale_garden `#A0A69C`. The others would need the missing `dry_foliage.png` (gap G1); the jar returned `#000000` without it.

**Not in the server jar:** the client's biome blend (how many neighbouring biomes are averaged per vertex) is a client option, so session 6 chooses its own blend radius. Note that tint is a function of (x, z) and the biome only.

---

## 6. Gaps and risks

### 6.1 Gaps that need reference additions (through the references' own tools, later)

| # | Gap | Who needs it | Suggested addition |
|---|---|---|---|
| G1 | `dry_foliage.png` is not in `Reference/minecraft-blocks-26.3` (only grass and foliage) | Only blocks with a dry-foliage tint (leaf litter and the like), none in our cube and slab set | Add the colormap through the block reference's import tools when such a block arrives; low priority |
| G2 | The eroded badlands pillars and frozen ocean icebergs (1.6) are code behaviour; their noises are in `noise/`, but no rule describes them | Session 5 (surface per biome) | Decide: skip in phase 5, or design our own from these noises. Samples to look at: extensions.json |
| G3 | The temperature height noise N and the frozen modifier's two noises are fixed-seed jar noises, not in the reference | Session 5 (`VoxelBiomeClimate.h`) | Our own noises with the observed scales (3.1, 3.2) are enough under "Minecraft-like". To pin numbers, `WorldgenFacts.java` could dump a `facts/climate_samples.json` (positions, adj, modifier outputs; the probes here show the method). |
| G4 | Water fog, sky and fog defaults live in `dimension_type/overworld.json` and in code (5.1), outside the reference | Session 6 only if it does underwater fog | Add `dimension_type/overworld.json` through `build_reference.py` if needed |
| G5 | The snow support tags (4.2) are in `block_manifest.json` tags. `powder_snow` and `snow` are in the block set; ice, packed ice, blue ice, mud and soul sand are too | Session 5 freeze pass | None; read the tags instead of hard-coding the list |
| G6 | `minecraft:temperature` (condition) is not parsed by `VoxelSurfaceRules`; overworld does not use it | | None now |
| G7 | Ore vein constants for phase 6 (public fields of `OreVeinRule`, x0_basics.json; names only, behaviour not probed): `VEININESS_THRESHOLD 0.4`, `EDGE_ROUNDOFF_BEGIN 20`, `MAX_EDGE_ROUNDOFF 0.2`, `VEIN_SOLIDNESS 0.7`, `MIN_RICHNESS 0.1`, `MAX_RICHNESS 0.3`, `MAX_RICHNESS_THRESHOLD 0.6`, `SKIP_ORE_IF_GAP_NOISE_IS_BELOW -0.3`; `raw_ore_chance` 0.02 in the data | Phase 6 | Probe the `ore_vein` rule the same way when phase 6 starts |

### 6.2 Exactness and determinism risks

- **The steep test is chunk-local.** Neighbours are clamped to the chunk, and only two of four slope directions count.
  - Our version must read heights that do not depend on load order: the column's own pre-surface terrain heights, ideally from the density, never neighbour chunks' finished blocks.
  - If we decide to look across chunk edges (a deliberate improvement), the results change at every edge, and the shortcut-on/off test still has to compare the same definition.
- **The biome is per block (3D, zoomed).**
  - Surface rules (badlands vs desert at a border, frozen peaks stone) can differ by y inside one column.
  - Session 4's biome storage decides what "the block's biome" is for us. Surface rules, the freeze pass and the tints must all use the same answer, or seams appear.
- **Water height includes lava, and it is "first fluid after the last air".**
  - Floors under aquifer lava count as underwater: gravel, no grass, and ocean sand.
  - A stone layer between two water layers inherits the upper W.
- **Fluid does not reset the stone depth above.** A floor under a water pocket inside stone is not a floor.
- **Truncation vs floor.**
  - Surface depth and secondary depth truncate toward zero, which matters for secondary depth with negative values. C++ `static_cast<int>` matches.
  - The preliminary surface uses floor, seen only for positive P. `FMath::FloorToInt` is right, and it is safe both ways.
- **The secondary depth is unclamped and applies even without `add_surface_depth`.** This is easy to get wrong when copying the field names' meaning.
- **Inclusive noise bounds on doubles.** Our noise values differ from the jar's anyway, so only the bound semantics matter. Keep doubles; the reader stores `MinThreshold` and `MaxThreshold` as double.
- **Randomness:**
  - The vertical gradient and the surface-depth jitter must be pure functions: (seed, random_name, x, y, z) and (seed, x, z).
  - The jar's gradient result does not depend on the biome or on the run order.
  - The plan's `Hash64(seed, random_name, x, y, z)` fits.
- **Climate noises without a seed.** The jar's temperature noise N and the frozen and swamp noises ignore the world seed, so every world shares the same snow-line wobble and swamp grass pattern. We can keep that (seedless salt) or seed it; either way, document it.
- **The snow line inside a tall column:** temp is evaluated at the snow layer's own y, top + 1, with the biome at that position.
- **Freeze-pass ordering:**
  - Vanilla runs it last, after trees, so snow lands on leaves.
  - With no features yet, our pass sees only terrain. When trees arrive, run the pass after them or snow will be missing on canopies.
  - The pass must use a MOTION_BLOCKING-style height: the top non-air, non-passable block, with fluids counting.
- **Deep frozen ocean water never freezes in the freeze pass.** A capture expecting ice on every "frozen ocean" should pick frozen_ocean at a T' = 0 spot (47% of its area), or show icebergs.

---

## Appendix: evidence files (`2026-10-09-Voxel-Phase5-Scout-Evidence/blackbox/`)

| File | Content |
|---|---|
| `x0_basics.json` | `is_3d` field name, temperature encoding, ore vein constants, density-volume argument order |
| `depth.json` | 1,536 columns: under_floor, hole, deep and very-deep counts, surface and secondary noise |
| `extra.json` | low-noise and secondary-extreme columns (768), two water layers, lava, steep under water |
| `stone_depth.json` | 4 profiles x 18 stone_depth variants x 3 columns |
| `y_above.json`, `water.json` | the y and water cases of section 2 |
| `noise_threshold.json` | 512 columns, exact bounds, the 3D variant |
| `vertical_gradient.json` | frequencies per y, determinism counts |
| `steep.json` | 20 slope grids, 3 plateaus |
| `above_preliminary_surface.json` | 1,280 columns with P and corner values |
| `biome_condition.json` | the biome border test |
| `bands.json` | 384-block band strings (5 columns x 2 seeds) and legend |
| `golden_columns.json` | the overworld rule on 37 biomes x 5 profiles x 16 columns, with inputs |
| `extensions.json` | iceberg and hoodoo columns with their noises |
| `temperature.json` | the bisected height adjustment at 8 x 17 positions, sea levels 63 and 0 |
| `frozen_modifier.json` | 4,000 modifier samples with the noises |
| `freeze.json` | shouldFreeze and shouldSnow matrices, snow support by block, feature scenes, the snow line at T 0.3 |
| `colors.json` | the colormap grid, hypothesis checks, modifier checks, per-biome colours |
| `environment_attribute_defaults.json` | fog and sky defaults |

Re-run: compile with JDK 25 against `server-26.3.jar` and its `datagen\libraries` jars (the classpath is in `scripts\cp.txt`):
- `java -cp classes;<cp> SurfaceProbe <blackbox> x0 depth stone y water noise gradient steep prelim biome bands golden extra ext`
- `java -cp classes;<cp> ClimateProbe <blackbox> <colormap dir> colors frozen freeze temp attrs`

Run them from `run\`, since the server writes `logs\` into the working directory. The jar's unpacked tags are in `jar-data\`.
