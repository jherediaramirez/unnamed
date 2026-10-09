# Voxel world phase 6 plan: decoration - October 9, 2026

This plan was written in a plan-only chat. Nothing was built, and no code, asset or data changed. It follows [the phase 6 handoff](2026-10-09-Voxel-Handoff-Phase6.md), which stays the entry point for the next chat; read this plan right after it.

Facts were checked at commit `38dd7a3` on `feature/voxel-world`. The plan builds on a read-only scout's research, copied here as [the phase 6 scout research](2026-10-09-Voxel-Phase6-Scout-Research.md), with its tables in [2026-10-09-Voxel-Phase6-Scout-Tables](2026-10-09-Voxel-Phase6-Scout-Tables/). This session re-checked the research's key figures against the scout's jar extract and the repository. The check scripts are `check_features.py` and `check_blocks.py` in `C:\dev\sandbox\phase6plan\` (disposable).

**Acceptance (from the worldgen prompt):**
- Trees across column borders are whole.
- An ore histogram by Y matches the configured shapes.
- Regenerating the same area in a different order produces identical block hashes.

Also in scope:
- placed features with modifiers and the 11 steps;
- ores per the table, trees and plants, lakes and springs;
- carver caves and canyons;
- deferred cross-column writes with a deterministic merge;
- meshing gated on 3x3 finalization.

## 1. What the data check found

### 1.1 Confirmed

| Check | Result |
|---|---|
| Placed features the 56 overworld biomes list | 171 top-level placed features (210 with nested ones). Per step: 0, 4, 5, 4, 4, 0, 34, 7, 3, 109, 1. No feature appears in two steps. |
| Carvers | `cave`, `cave_extra_underground` and `canyon`, in all 56 biomes |
| Jar folders | `worldgen/placed_feature` 273 files, `worldgen/feature` 240 (formerly `configured_feature`), `worldgen/block_state_provider` 8, `worldgen/carver` 4 (formerly `configured_carver`) |
| The worldgen reference | Has none of those four folders and no block tags (`data/minecraft/worldgen/` holds `biome`, `density_function`, `material_condition`, `material_rule`, `multi_noise_biome_source_parameter_list`, `noise` and `noise_settings`) |
| Missing blocks | All 85 blocks of the scout's missing list (83, plus chest and spawner) are absent from `block_manifest.json` (531 blocks: 429 full cubes, 101 slabs, 1 layers block) |
| Blocks decoration needs that exist | All 71 checked are present, including every ore, log and leaves (the poplar log and leaves included), bee nest, creaking heart, the mushroom blocks, sculk and the raw ore blocks |
| Snow support tags (facts G5) | Each block in `block_manifest.json` carries a `tags` list, so the freeze pass can read them |

### 1.2 Corrections to the scout's research

1. **Ore targets use a second predicate language.** Ore `targets` take rule tests (`predicate_type`), not the block predicates (`type`) that filters use. Across the 240 feature files they occur as: `tag_match` 111, `all_of` 68, `any_of` 34, `not` 34, `height_match` 34, `block_match` 8, `always_true` 2.
   - In 17 ores, tuff (the tag `height_specific_ore_replaceables`) becomes the stone ore at Y 0 and above and the deepslate ore at Y 8 and below; between 0 and 8, the first matching target decides.
   - So the scout's "`stone_ore_replaceables` gives the ore" holds only for blocks outside that tag.
   - The scout's node counts (its 1.7) leave these rule tests out.
2. **The heightmaps are data.** The block tags `blocks_motion_in_heightmap` (`#minecraft:blocks_motion`) and `blocks_motion_in_heightmap_no_leaves` define which blocks the motion-blocking heightmaps count. Our `EVoxelHeightmap` must be checked against them once the tags are in the reference.
3. **Feature ids nest.** 240 feature files include subfolders (for example `coral/brain_block`), so a feature id is a path, not a bare name.

### 1.3 How the data gets in

Everything goes through the reference's own tools, with the existing 228 files byte-identical (only `manifest.json` and `SHA256SUMS.txt` change, as in phase 5 session 2).

- **`tools/build_reference.py`:** `wanted()` gains the four folders. The rule today is that the `is_overworld` tag decides the biomes. In the same way, reachability from the 56 biomes' `features` and `carvers` lists decides what is copied:
  - 210 placed features, 182 features, 8 providers and 3 carvers;
  - the block tags they and the support rules reference, with the tags those include, and the two heightmap tags.
- **`tools/verify_reference.py`:** these must resolve:
  - every placed feature to a feature;
  - every nested placed feature, provider and tag;
  - every block id to the blocks reference.

  It also checks that every file is reachable from a biome.
- **`tools/WorldgenFacts.java`** (public API after `WorldLoader`, as the spline and biome facts were dumped):
  - histograms of the int, float and height providers the features use (10^6 samples each);
  - vanilla's sorted per-step feature order, which our own order is compared with;
  - the ore-vein probe (section 3.3).
- **A feature probe harness (new, black-box):**
  - A headless 26.3 dedicated server runs a flat test world, driven through its console: `/fill`, then `/place feature <id> <pos>` many times, then `save-all`.
  - A Python region reader records what each placement wrote, relative to its origin.
  - Output: `facts/feature_probes/<id>.json`, as statistics (vanilla's random differs per call), so tests compare distributions, not blocks.
  - Session 1 builds it with the ore probes. Each later session adds the probes it needs through the same tool, with existing files byte-identical.
  - Before each server run, check `Get-Process java, javaw, UnrealEditor*`; Jesus plays on this PC.

### 1.4 Code-made parts (as session 2 found for the parameter list)

| Made in the jar's code, not data | What we do | Source |
|---|---|---|
| The per-step feature order (vanilla sorts the biome lists in code) | Our own order: a topological sort of the 56 lists per step, ties by id (acyclic for all 11 steps) | Compared with vanilla's dumped order (S1) |
| Each feature type's behaviour: the ore blob, trunk and foliage placers, decorators, geode, lake, iceberg, spike, speleothems, coral, disk | Our own algorithms, matched to probe statistics | The probe harness, per session |
| Blocks a feature type places by itself (vine, cocoa, bee nest, creaking heart, pale hanging moss, shelf mushroom, bamboo, magma, snow, ice, the corals: the scout's 35 "observed") | Confirmed by the probes before use | Per session |
| Structure templates (desert well, fossils) and block entities (chest, spawner) | Deferred (Q2) | |
| The ore vein rule (constants in facts G7) | Our own evaluator, matched to probe outputs | S1 probe, S4 |
| Carver tunnel shapes | Our own code from the carver data, matched to probe statistics | S10 |
| Flower and noise-count patterns on the fixed seed 2345 | Ours take the world seed (Q4) | |
| The heightmap block sets | Now data (1.2) | S1 tags, S2 check |

## 2. Block gaps

The scout's 2.2 is the inventory, confirmed above. Decoration touches 183 blocks:
- 97 work today;
- 83 are missing (about 950 states and 135 textures);
- chest and spawner are deferred with Q2;
- `sculk_shrieker` is out (ancient cities only).

| Group | Blocks | Needs beyond geometry |
|---|---|---|
| Crosses (plain, facing, tinted), double crosses, seagrass | 49 | Two-sided cutout quads without AO; cutout-aware mips (23 textures carry a strict-cutout mip strategy); a position-hash offset; a grass tint by `tintindex`; two cells for double plants; water around seagrass, kelp and corals |
| Flowerbeds and leaf litter, carpets | 5 | Multipart segments by facing; leaf litter takes the dry-foliage colormap (facts G1), a new `EVoxelTintSource` |
| Multiface: vine, glow lichen, sculk vein | 3 | Thin planes on any face (288 states); lichen emits 7; vine takes the foliage tint |
| Custom element models | 26 | Their own element boxes: dripleaf, azalea, cocoa, bamboo, coral fans, propagule, sea pickle, cactus, lily pad, pointed dripstone and others |

What closes the gaps is the Status table's "block-shapes step": generic baked models (Q1), split into data (S5), rendering (S6) and interaction (S7). Collision and outline boxes come from dumped facts, never from render models.

## 3. Design answers to the handoff's points

### 3.1 Cross-column writes, the extra stages and their cost (handoff point 3)

Decoration is a push with write logs (the scout's design A). Recomputing the neighbourhood (design B) serves as the test oracle.

| Stage | Runs when | Reads | Produces | Kept until |
|---|---|---|---|---|
| Generate | the column is needed (ring R+3) | nothing; a pure function, as today | the snapshot: blocks, biomes and the `_WG` heightmaps | its 8 neighbours have decorated |
| Decorate | its 3x3 is generated (ring R+2) | the 3x3 snapshots, plus its own writes so far | one write log: writes into its 3x3, each tagged with its target column | every target has finalized |
| Finalize | its 3x3 is decorated (ring R+1) | its snapshot, plus every write aimed at it from the 9 logs | the final column: the merge, the freeze pass, heightmaps, then the replayed edits | unload |
| Mesh | its 3x3 is final (ring R, the view) | the 3x3 final columns | sections, as today | as today |

- **The working copy:** Decorate works on a 48 x 48 window over the 3x3 snapshots, copied per section on first write. A feature therefore sees the column's earlier features wherever they landed (vegetation sees the column's own trees), and never a neighbour's decoration. This is what makes the result independent of timing.
- **The merge:**
  - Finalize sorts the writes aimed at its column by (step, order index, source offset dz then dx, sequence) and replays them against the merged state.
  - Write modes: `Force` (lakes, geodes), `IfReplaceable(tag)` (trees: `replaceable_by_trees`; plants: air or `replaceable`) and `IfRule` (ores: their rule test; filters that checked a block).
  - Two trees from different columns can overlap. The earlier one keeps its logs, a known and deterministic difference from vanilla. A tree computes its leaves' `distance` from its own logs.
- **Reach:** a write outside the 3x3 is dropped and counted. Patch offsets reach ±10; trees, lakes, geodes and icebergs stay inside the 3x3 by construction. `Decoration.Reach` requires 0 drops across 1,000 columns per preset.
- **Which features run:** a column runs a feature if any biome in its 3x3 lists it. The `biome` modifier (on all 171 top-level placements) then keeps a position only if that position's own jittered biome lists it.
- **Heightmaps:** the `_WG` maps come from the snapshot. `MOTION_BLOCKING` and `OCEAN_FLOOR` come from the working copy, with block sets from the heightmap tags (1.2). Finalize recomputes all four from the final blocks.
- **Rings:** today's generated ring R+1 becomes R+3. At `ViewRadius` 12 that is about 707 generated columns instead of 531 (+33%), and +15% new columns per column moved in a straight flight. The first view needs 7x7 generated columns instead of 3x3 (Q8).
- **Memory:**
  - The scout estimates 5k to 15k writes per column, 8 to 12 bytes each, held from Decorate to the targets' Finalize. A snapshot is about 19 KB.
  - A Decorate job's working copy is at most 9 x 24 sections, copied on write.
  - The final column stays under the prompt's 64 KB. S2 measures all of these.
- **Edits** replay after Finalize, as they replay after generation today.
- **Switch:** with `voxel.Decoration 0`, Finalize keeps the snapshot. It serves comparisons and gates.

### 3.2 Randomness and order

- **Streams:** each feature gets its own Xoroshiro128++ stream, seeded from `Hash64(seed, "decoration", column x, column z, step, hash of the placed feature id)` (Q4). Nothing is shared across columns or features.
- **Per-position values** (a plant's render offset) come from position hashes, as the biome jitter does.
- **Order:** one global order per step, computed once at load and checked against vanilla's dumped order. It decides write priority in the merge.

### 3.3 Ore veins (handoff point 4)

- **Today:** `ore_vein` compiles to nothing in the surface evaluator. The three vein density functions (`overworld/ore_vein/*`) are already in the reference, and the constants are in facts G7 (`VEININESS_THRESHOLD` 0.4, `VEIN_SOLIDNESS` 0.7, richness 0.1 to 0.3, gap skip below -0.3, `raw_ore_chance` 0.02).
- **S1, the probe:** `WorldgenFacts.java` evaluates the rule on hand-made inputs, as session 5's scout probed the other rules. It records the block given per position and seed, with the three density values beside it.
- **S4, the build:** our own evaluator matches the probe. An exact shortcut evaluates the vein noises only inside the vein types' Y ranges (copper 0 to 50, iron -60 to -8, to be confirmed by the probe).
- **The switch:** on in phase 6 (Jesus's answer 4).

### 3.4 The freeze pass (handoff point 5)

- **Where it runs:** S3 moves it from Generate to Finalize, after the merge. It then runs after every feature, a neighbour's trees included, so snow lands on all leaves. This removes vanilla's bare-leaves border artifact.
- **Snow support** reads the block tags (facts G5) instead of the hard-coded list.
- **Light:** in vanilla, light is initialized after the features stage. The phase 5 facts (section 4) say to treat block light as 0 during generation, so the pass keeps 0 by design, and the known issue closes as that.
  - S1's optional vanilla-world run can confirm it: water beside a surface lava lake in a cold biome.

### 3.5 The 5 ms limit and a gate in every session (handoff point 6)

- **The budget:** the prompt's limit covers stages 2 to 6, which includes decoration. Each column costs Generate, plus Decorate once, plus Finalize once.
- **Measuring:** `voxel.Bench Decorate 4 <preset>` (new in S2) reports the three stages and the whole column. The same run includes the Generate figures from `voxel.Bench Density 4`.
  - Each session records its starting figures and its final figures in the same run, on a quiet machine, with the machine state stated beside them.
  - Gate figures are medians of three runs.
- **Flags:** a session must tell the manager before committing if:
  - a stage exceeds its allowance below;
  - a preset's whole-column mean crosses 5 ms;
  - a preset that is already over 5 ms rises further (today AMPLIFIED: 5.32 ms).
- **Streaming sessions** add the flight (the 2 ms per-frame apply budget, no holes, flat memory) and the first view.

| Session | Stage it adds to | Allowance (mean ms per column, one thread) |
|---|---|---|
| S1 data | none | 0; reading at world start at most 50 ms |
| S2 decoration core, ores | Decorate / Finalize | 0.6 / 0.2 |
| S3 streaming stages | Finalize (the freeze pass moves there from Generate) | +0.1 net |
| S4 ore veins | Generate | 0.3 |
| S5 to S7 block models | none in columns | 0; mesh cost per S6 |
| S8 plants and patches | Decorate | 0.4 |
| S9 trees | Decorate | 0.4 |
| S10 carvers | Generate | 0.5 |
| S11 underground features | Decorate | 0.3 |
| S12 ocean and cold | Decorate | 0.2 |
| **Sum** | | **about 3 ms** |

- **The arithmetic is plain:** from overworld's 4.50 ms, phase 6 at its allowances ends near 7.5 ms. Even with a speed-up session first (Q9), every preset will likely end over 5 ms.
- **Q11** asks Jesus how to read the limit once decoration exists. The allowances bound how far over it goes; they are not a promise to stay under it.

### 3.6 Carvers

- **A pull in Generate:** S10 runs the three carvers as a pull pass after the surface and before the snapshot. A column carves the parts of tunnels that start in columns up to 8 away.
- **Order-independent by construction:** tunnel paths are pure functions of the seed and their start column, so they need no logs and come before decoration.
- **Aquifers:** carving below the aquifer's levels fills with water or lava, as the noise caves do.
- **A path cache:** paths are cached per start column, and results never depend on the cache (bench gate: cache on and off, 0 differing blocks).

### 3.7 Out of scope or deferred

- Templates and block entities: dungeons, desert wells and fossils (Q2).
- Flowing springs (Q3); terrain slabs (Q5).
- Eroded-badlands hoodoos: surface code, not a feature, so they stay deferred.
- `sculk_shrieker`: ancient cities only.

Icebergs come back in S12 as the `iceberg` feature type; phase 5 deferred them only out of phase 5.

## 4. Sessions

Twelve build sessions, plus an optional speed-up session first (Q9). Each covers one area; the few bundles are justified in a line. Each ends as the handoff's section 2 says:
- a Sonnet 5.5 review;
- the suites, the `-DisableAdaptiveUnity` build, `create-world.mjs` and the Playground probe;
- the flight, for runtime changes;
- the session's performance gate (3.5);
- a local commit and a handoff.

Goldens are re-recorded only where the session's own decided change alters the blocks (generator 2 keeps changing until phase 10), and the session names that change.

| # | Session | Area | Size | Needs |
|---|---|---|---|---|
| S0 | AMPLIFIED speed-up (first: Q9 answered yes) | Generate's density, aquifer and block fill | M | |
| S1 | Worldgen data, reader and probes | The worldgen reference and its reader | L | |
| S2 | Decoration core, with ores | `Source/UnnamedVoxel/Decoration/` on the fixed area | L | S1 |
| S3 | Streaming stages and Finalize | The column pipeline | M to L | S2 |
| S4 | Ore veins | The surface evaluator's `ore_vein` | S | S1 |
| S5 | Block models: data | The blocks reference and `DA_Blocks` | L | |
| S6 | Block models: rendering | The voxel mesher and the Playground's meshes | M to L | S5 |
| S7 | Plant interaction, collision and the plant items | Placement, survival, collision, targeting, and the items that place plants | M | S6 |
| S8 | Plants and patches | Vegetation feature types | L | S3, S6 |
| S9 | Trees and big vegetation | Tree feature types | L | S8 |
| S10 | Carvers | The carver pass in Generate | M | |
| S11 | Underground features | Underground and fluid feature types | L | S8, S10 |
| S12 | Ocean and cold features, and the phase record | The last feature types; the record | M | S11 |

A lead usually ends a session between 300k and 550k tokens (phase 5 session 6: 547k). The handoff's checkpoints apply.

### S0. AMPLIFIED speed-up (runs first)

Jesus answered Q9 yes, so this session runs first. The [phase 6 handoff](2026-10-09-Voxel-Handoff-Phase6.md)'s section 4b already plans it: its levers, gates, figures and what was ruled out.
- **Gate:** AMPLIFIED's median of three under 5 ms; no preset slower; every gate at 0 differing blocks.
- **Housekeeping (decided October 9):** its commit also adds `Tests/Evidence/` and `logs/` to `.gitignore`, deleting nothing.

### S1. Worldgen data, reader and probes

**Area:** data in and data read; the reader is the reference's consumer, as in phase 5 session 2.

- **Scope:**
  - 1.3 in full: `build_reference.py`, `verify_reference.py`, `WorldgenFacts.java` (provider histograms, vanilla's step order, the ore-vein probe) and the probe harness with the ore probes (blob block counts and extents per size; discard on air exposure).
  - **Core data:** `FVoxelFeatureGraph`, a flat table of typed nodes: placed features, features, modifiers, block predicates, rule tests, state providers and number providers, with children by index and ids as strings, as `FVoxelDensityGraph` keeps its nodes.
    - Types are checked against the list of 26.3 types the data uses, and every reference resolves.
    - Each later session adds the compile step for its own types, as the surface evaluator compiles its tree.
  - **The reader:** strict, with the file and field in every error. It writes nodes back as the reference's JSON for a round-trip test.
  - **The order:** our per-step order.
  - **Optional:** the vanilla-world statistics run (scout section 5), only if the machine is free.
- **Files:**
  - the reference: `Reference/minecraft-worldgen-26.3/tools/` (the three tools and a new `feature_probe.py` with its server driver), the regenerated `data/`, `facts/`, `manifest.json`, `SHA256SUMS.txt` and `README.md`;
  - core: `Source/UnnamedVoxel/Decoration/VoxelFeatureGraph.h/.cpp`, `VoxelFeatureOrder.h/.cpp`;
  - game: `Source/Unnamed/Voxel/VoxelFeatureReader.cpp` (declared in `VoxelWorldgenReader.h`, helpers in `VoxelWorldgenJson.h`), `VoxelTerrainFactory.cpp` (`ReadInputs`; an unreadable feature set refuses a saved world, as the rules do), `Unnamed.Build.cs` (stage the new folders);
  - docs: `Docs/VoxelWorld.md` ("Worldgen data").
- **Tests:**
  - `Unnamed.Voxel.Features.Read`: 210, 182, 8 and 3; every file round-trips byte-equal as JSON; strict errors.
  - `Features.Order`: acyclic; equal to vanilla's order where vanilla's lists agree.
  - The verifier's new checks; `TestVoxel.ps1`'s count.
- **Evidence:** the verifier's line; the reading time; the probe facts' summary.
- **Gate:** no column stage changes. Goldens and the radius-8 hashes stay byte-identical, and reading adds at most 50 ms at world start.

### S2. Decoration core, with ores

**Area:** the decoration core, run on the fixed area (`Generation/VoxelArea`) and in the bench, not yet in streaming.

**Why ores here:** they are the core's proving feature. They need no new blocks, their blobs cross column borders (exercising the logs and the merge), and they bring the ore-histogram acceptance with them.

- **Scope:**
  - the decoration context (the 3x3 snapshot view, the working copy, the heightmaps from the tags, the per-block biome);
  - the random streams and the step order;
  - the write log and the merge (3.1);
  - Finalize as a core function;
  - the five modifiers ores use (`count`, `in_square`, `height_range`, `rarity_filter`, `biome`);
  - the int and height providers (constant, uniform, trapezoid, and those the ores reach);
  - the seven rule tests;
  - the `ore` feature (our own blob, matched to the S1 probes, with discard on air exposure);
  - block-tag membership in the core block table (`tag_match`, `replaceable`);
  - `voxel.Bench Decorate`;
  - `voxel.OreHistogram <radius> <preset>` (CSV), and a figure script.
- **Files:**
  - core: `Source/UnnamedVoxel/Decoration/` (`VoxelDecorationContext`, `VoxelWriteLog`, `VoxelDecorator`, `VoxelPlacementModifiers`, `VoxelRuleTests`, `VoxelNumberProviders`, `Features/VoxelOreFeature`), `Generation/VoxelArea.*`, `Storage/VoxelColumn.*` (heightmap block sets), `Blocks/VoxelBlockTable.*`, `VoxelBench.cpp`;
  - game: `Source/Unnamed/Voxel/VoxelBlocks.cpp` (tags), `VoxelConsole.*`;
  - tests: `Source/Unnamed/Tests/VoxelDecorationTests.cpp`, `Scripts/TestVoxel.ps1`;
  - docs: `Docs/VoxelWorld.md`.
- **Tests:**
  - `Decoration.Oracle`: a 3x3 decorated alone equals the area's result.
  - `Decoration.LoadOrder`: a 9x9 area in 8 shuffled orders, with 1 and 30 workers; every hash equal.
  - `Decoration.Reach`, `Decoration.Golden`.
  - `Decoration.OreHistogram`: blocks per Y per ore, caves and discards off, against the height provider's shape, with a chi-square bound.
  - `Providers.Distribution` (against S1's histograms); `RuleTests`; a mutation check (ore order swapped in the merge fails `LoadOrder`).
- **Evidence:** the ore histogram (CSV plus PNG per ore, `OreHistogram-*`); an edge-walls cut through ores (`CaptureVoxel -ExecCmds 'voxel.Debug.EdgeWalls 1'`); write-log sizes.
- **Gate:**
  - Decorate (ores) at most 0.6 ms and Finalize at most 0.2 ms per column;
  - log and working-copy memory reported;
  - goldens re-recorded under the decided change "ores on".

### S3. Streaming stages and Finalize

**Area:** the column pipeline.

**Why the freeze pass is here:** moving it is what gives Finalize its first column-wide job, so it is part of building that stage.

- **Scope:**
  - **Records** gain the stages: generated (snapshot), decorated (log), final, meshed.
  - **Jobs** are gated as in 3.1, with the new rings (`FVoxelStreamingRings::GetNeededExtent`, the unload distance and its margin).
  - **Lifetimes:** snapshot and log lifetimes; cancellation and unload with logs in flight.
  - **Edits** replay after Finalize.
  - **The freeze pass** moves to Finalize, with snow support from tags and light 0 (3.4).
  - **Switch:** `voxel.Decoration 0/1`.
  - **Stats** per stage in `voxel.Info`.
- **Files:**
  - game: `Source/Unnamed/Voxel/VoxelColumnPipeline.h/.cpp`, `VoxelWorldSubsystem.cpp`, `VoxelConsole.*`, `VoxelWorldSettings.*`;
  - core: `Source/UnnamedVoxel/Streaming/VoxelStreamingPlanner.*`, `Terrain/VoxelDensityTerrain.*` (the freeze pass leaves Generate), `Surface/VoxelFreezePass.*`;
  - tests: `Source/Unnamed/Tests/VoxelPipelineTests.cpp`, `VoxelStreamingTests.cpp`;
  - docs: `Docs/VoxelWorld.md` ("Streaming").
- **Tests:**
  - `Pipeline.Stages`: no mesh before the 3x3 is final; no finalize before the 3x3 is decorated.
  - Streaming load order: two focus paths give the same hashes.
  - `Freeze.AfterDecoration`: a test feature writes a block across a border, and the snow lands on it.
  - The existing edit-replay tests.
- **Evidence:** the flight (`CaptureVoxel -Flight`); first view; the five standard shots.
- **Gate:**
  - Finalize at most +0.1 ms net, against Generate's 0.07 ms freeze leaving it;
  - the flight within the 2 ms budget on 99.9% of frames, no holes, memory flat;
  - first view at most 4.0 s (today 3.2 to 3.6 s);
  - generated columns at radius 12 reported (expected about +33%).

### S4. Ore veins

**Area:** the surface evaluator's `ore_vein` rule (3.3).

- **Files:** `Source/UnnamedVoxel/Surface/VoxelSurfaceEvaluator.*`, `VoxelSurfaceRules.*`, `Terrain/VoxelDensityTerrain.*` (the switch), `Source/Unnamed/Voxel/VoxelConsole.*`, tests in `VoxelSurfaceTests.cpp`.
- **Tests:** `Surface.OreVein` against S1's probe; the shortcut on and off gives 0 differing blocks; goldens re-recorded under Jesus's answer 4.
- **Evidence:** an edge-walls cut through a copper vein and an iron vein; the ore histogram again, with veins counted on their own.
- **Gate:** at most 0.3 ms per column added to Generate.

### S5. Block models: data

**Area:** the blocks reference and the registry asset.

- **Scope:**
  - **The reference:** the 83 blocks enter `Reference/minecraft-blocks-26.3` through its tools: blockstates, models with their parents, the 135 textures with their mip strategy, states and ids, and the classification past cube, slab and layers.
  - **A model baker** in the reference's tools: per state, quads with UV, texture layer, `tintindex`, `cullface`, `shade` and rotation (with uvlock).
  - **`FactsDump.java`:** per state, collision and outline boxes, fluid state, offset kind and maximum, `replaceable` and occluding faces; and tags for the new blocks.
  - **`DA_Blocks`:** reimported, with old assets moved deliberately. `EFoundationBlockShape::Model` and a baked-model index in the registry.
  - **Items:** icons as flat sprites from each item model's layers, for the new blocks whose vanilla item has the block's id. The facts dump records each new block's vanilla item, so S7 can add the items with other ids (Jesus's answer 7).
- **Files:**
  - the reference: `Reference/minecraft-blocks-26.3/tools/` (`build_manifest.py`, `classify_blocks.py`, `build_blocks.py`, `FactsDump.java`, a new `bake_models.py`, `verify_reference.py`) and the regenerated reference;
  - editor scripts: `Scripts/Editor/ImportBlocks.py`, `BlockImportData.py`, `CreateBlockMaterials.py` (cutout mips);
  - registry: `Source/Unnamed/Foundation/FoundationBlockAssets.h`, `FoundationBlockImport.*`, `FoundationBlockRegistry.*`, `FoundationBlockMipmaps.*`, `FoundationBlockTextureTable.*`;
  - docs: `Docs/Blocks.md`, and the regenerated `Docs/MinecraftBlockReference.md` (closing the hand-edit known issue).
- **Tests:**
  - `Blocks.Registry`: 617 blocks with air, water and lava, about 4,420 states.
  - `.Codec`, `.Faces`, `.Textures`.
  - `.Shapes`: every new state's boxes against the facts.
  - New `.Models`: quad counts and UVs against the reference.
  - `Tests/source-check.mjs`; the blocks verifier.
- **Evidence:** the tour's creative inventory pages with the new items.
- **Gate:** no column change (hashes identical); editor start and import time and the asset size reported.

### S6. Block models: rendering

**Area:** drawing model states, in the voxel mesher and the Playground.

- **Scope:**
  - **The core shape:** `EVoxelShape::Model` with its quads in the core block table.
  - **The mesher** appends a model's quads per cell into the cutout layer, unmerged, and drops faces whose `cullface` neighbour is opaque.
  - **Model rendering details:**
    - the AO flag;
    - per-vertex tint by `tintindex` (grass, foliage, and dry foliage with its colormap through the blocks reference's tools);
    - the position-hash offset;
    - emission and the emissive overlay (firefly bush);
    - water meshed around always-water and waterlogged states.
  - **Lumen cards:** skipped for small cutout models if they cost.
  - **The Playground:** one runtime mesh per baked model.
- **Files:**
  - core: `Source/UnnamedVoxel/Blocks/VoxelBlockTable.*`, `Meshing/VoxelMesher.*`, `Meshing/VoxelMeshData.h`, `Biome/VoxelBiomeTints.*`;
  - game: `Source/Unnamed/Voxel/VoxelBlocks.cpp`, `VoxelChunkComponent.*`, `VoxelColumnPipeline.cpp` (cards), `Source/Unnamed/Foundation/FoundationBlockMeshes.*`;
  - tests: `VoxelMesherTests.cpp`.
- **Tests:**
  - `Voxel.Mesher.Models`: culling against neighbours, quad counts, tints, deterministic offsets.
  - The tour: the registry and state galleries.
- **Evidence:**
  - the tour galleries with every new block, and a plant row in the showcase;
  - a hand-placed plant garden in `L_Generated`;
  - a section-border case;
  - a distance shot (cutout mips).
- **Gate:**
  - Mesh time per column for the generated world is unchanged within noise.
  - A plant-dense test column is measured; the cross fast path (Q1 c) only if crosses cost more than 0.2 ms per column over the generic path.
  - Mesh per section stays under the prompt's 0.2 ms.

### S7. Plant interaction, collision and the plant items

**Area:** gameplay with model blocks, in the voxel world and the Playground.

**Why the items are here:** an item whose id differs from its block (glow berries placing cave vines) is useful only once placing it obeys the support rules, which this session builds.

- **Scope:**
  - collision and outline boxes from the facts (`GatherCollision`, targeting);
  - placement checked against the support tags (Jesus's answer 6);
  - both halves of double plants;
  - attached faces (vine, lichen, cocoa);
  - pop-when-unsupported after mining, one level deep;
  - instant mining from the facts;
  - placing into replaceable plants.
  - **The vanilla items with their own ids (Jesus's answer 7):**
    - Add the decoration blocks' items whose id differs from the block's, taking the list from S5's facts. At least `sweet_berries` (places `sweet_berry_bush`), `glow_berries` (`cave_vines`) and `cocoa_beans` (`cocoa`).
    - Each gets the vanilla name, stack size, icon and creative-tab position, and places its block through the support rules.
    - `kelp` and `seagrass` already have item id = block id. `kelp_plant`, `tall_seagrass`, `cave_vines_plant` and `big_dripleaf_stem` have no vanilla item, so they get none.
    - Powder snow's bucket stays with buckets.
  - **The id rule:**
    - This session updates the project's id rule as Jesus decided, in `CLAUDE.md` ("Block/item IDs (item id = block id)") and in `Docs/Blocks.md` ("Item ids").
    - It adds these items as new saved ids, and never renames an existing block or item id.
- **Files:**
  - `Source/Unnamed/Foundation/FoundationBlockPlacement.*`, `FoundationBlocks.*` (`GatherCollision`, the item rules), `FoundationAuthority.*`, `FoundationBlockRegistry.*` (items apart from blocks);
  - `Source/Unnamed/Voxel/VoxelBlockStorage.*`;
  - tests: `FoundationInteractionTests.cpp`, `FoundationBlocksTests.cpp`;
  - docs: `CLAUDE.md`, `Docs/Blocks.md`.
- **Tests:**
  - `Blocks.Placement`: truth tables for plants.
  - `Blocks.Survival`: the cascade.
  - `Blocks.Items`: each other-id item places its block, every existing item id is unchanged, and the new ids are listed.
  - An interaction-suite scenario: a flower on grass; mine the grass, and the flower pops.
- **Evidence:** `TestFoundationRuntime.ps1 -Editor -Suite interaction`; a walk-through-grass clip or shots.
- **Gate:** the Playground probe's FPS is not lower beyond noise; collision gather time per tick reported (at most +10%).

### S8. Plants and patches

**Area:** vegetation feature types.

- **Scope:**
  - **The other modifiers:** heightmap, offset, `block_predicate_filter`, `surface_water_depth_filter`, `environment_scan`, `random_chance`, `noise_threshold_count`, `noise_based_count`, `cuboid`, `randomly_selected`, `surface_relative_threshold_filter`.
  - **The 14 block predicates.**
  - **The state providers:** simple, weighted, `rule_based`, noise, `noise_threshold`, `dual_noise`, `randomized_int`, rotated, and the named providers.
  - **The features:** `simple_block`, `block_column`, `disk`, the four selectors, `sequence`, `no_op`, `multiface_growth`, `vegetation_patch` (and its waterlogged form).
  - **What the data then places:** grass, ferns, flowers, double plants, sugar cane, cactus, kelp, seagrass, sea pickles, lily pads, berries, pumpkins, melons, mushrooms, glow lichen, firefly bushes, leaf litter, wildflowers, dry grass, dead bushes, and the sand, clay, gravel and grass disks.
  - **Flower noise** takes the world seed (Q4).
- **Files:**
  - `Source/UnnamedVoxel/Decoration/` (`VoxelPlacementModifiers`, `VoxelBlockPredicates`, `VoxelStateProviders`, `Features/VoxelPatchFeatures`, `Features/VoxelSelectorFeatures`, `Features/VoxelMultifaceFeature`);
  - `VoxelDecorationTests.cpp`;
  - `Docs/VoxelWorld.md`.
- **Tests:**
  - units for the modifiers and predicates;
  - `Providers.Distribution`;
  - `Decoration.LoadOrder` and `.Oracle` again;
  - `Decoration.Golden`, re-recorded under "plants on".
- **Evidence:**
  - plains, flower forest, meadow, desert, badlands, jungle floor, a swamp with lily pads, a warm ocean floor with seagrass and kelp, and a taiga with berries and ferns;
  - a column-grid overlay shot of patches crossing borders (a new debug cvar).
- **Gate:** Decorate (patches) at most 0.4 ms per column; mesh per column in plant-dense biomes reported against S6's figure; the flight.

### S9. Trees and big vegetation

**Area:** tree feature types.

- **Scope:**
  - `tree` with its parts:
    - 10 trunk placers, 12 foliage placers and the mangrove root placer;
    - the two- and three-layer feature sizes;
    - 11 decorators: beehive, `leave_vine`, `trunk_vine`, cocoa, `alter_ground`, `attached_to_leaves`, `attached_to_logs`, `place_on_ground`, `pale_moss`, `creaking_heart`, `shelf_mushroom`.
  - Other big vegetation: `fallen_tree`, the huge mushrooms, bamboo, `vines`, `root_system` (azalea) and `block_blob` (forest rock).
  - Leaves' distance comes from the tree's own logs.
  - Probes for the 36 tree configurations, through S1's harness.
- **Files:** `Source/UnnamedVoxel/Decoration/Features/` (`VoxelTreeFeature`, `VoxelTrunkPlacers`, `VoxelFoliagePlacers`, `VoxelTreeDecorators`, `VoxelBigVegetation`), tests, `Scripts/CaptureVoxel.ps1` (`-Trees`, a shot finder for trees on a column border).
- **Tests:**
  - `Trees.Shapes`: each configuration's height range, log connectivity and leaf distance at most 7, against the probes.
  - `Trees.Borders`: a tree straddling a column corner is whole in every load order.
  - `Decoration.Golden`, re-recorded under "trees on".
- **Evidence:** trees whole across borders, with the column-grid overlay, in:
  - forest, taiga (mega spruce), jungle (mega jungle with vines and cocoa);
  - dark forest, mangrove, cherry, dappled forest, savanna and swamp.
- **Gate:** Decorate (trees) at most 0.4 ms per column, reported per biome group; Finalize stays at most 0.3 ms with trees; the flight.

### S10. Carvers

**Area:** the carver pass in Generate (3.6).

- **Files:** `Source/UnnamedVoxel/Terrain/VoxelCarvers.h/.cpp` (new), `Terrain/VoxelDensityTerrain.*`, `Terrain/VoxelAquifer.*` (read access), the reader for `worldgen/carver`, `VoxelBench.cpp`, the settings and console, tests in `VoxelTerrainTests.cpp`.
- **Tests:** `Carvers.LoadOrder`, `Carvers.Golden`; path cache on and off, 0 differing blocks.
- **Evidence:** `CaptureVoxel -Caves` with a canyon and a worm cave; an edge-walls cut.
- **Gate:** at most 0.5 ms per column added to Generate.

### S11. Underground features

**Area:** underground and fluid feature types.

- **Scope:**
  - lava lakes (`lake`, surface and underground);
  - springs (data supported, off by default: Q3);
  - geodes;
  - dripstone: large dripstone, speleothems and their clusters (dripstone and sulfur);
  - sculk: the patch (`sequence`) and the vein;
  - lush caves: `vegetation_patch`, cave vines, spore blossom, dripleaf, clay, and the rooted azalea tree;
  - sulfur caves: the rooted spring and the pool;
  - underwater magma.

  Monster rooms and fossils follow Q2.
- **Files:** `Source/UnnamedVoxel/Decoration/Features/` (`VoxelLakeFeature`, `VoxelGeodeFeature`, `VoxelDripstoneFeatures`, `VoxelCaveFeatures`), tests.
- **Tests:** `Geode.Shape`; `Lake.Contained` (no fluid leaks into air at borders); goldens re-recorded under "underground features on".
- **Evidence:** `CaptureVoxel -Caves` covering a geode, lush caves, dripstone caves, the deep dark, sulfur caves and a lava lake.
- **Gate:** at most 0.3 ms per column.

### S12. Ocean and cold features, and the phase record

**Area:** the last feature types.

**Why the record is here:** a phase's record is written by its last session, as phase 5 session 6 did.

- **Scope:**
  - coral tree, claw and mushroom (warm-ocean vegetation);
  - icebergs (packed and blue), blue ice, ice spikes and the ice patch;
  - snow on leaves at Finalize, confirmed in shots;
  - the phase 6 record with the acceptance table and one montage.
- **Files:** `Source/UnnamedVoxel/Decoration/Features/` (`VoxelCoralFeatures`, `VoxelIceFeatures`), tests, `Docs/VoxelWorld.md`, `Docs/Records/<date>-Voxel-Phase6.md`.
- **Tests:** `Iceberg.Shape`; `Freeze.OnLeaves`; goldens re-recorded under "ocean and cold features on".
- **Evidence:**
  - a warm reef, a frozen ocean with icebergs, ice spikes, and a snowy taiga with snow on leaves;
  - every acceptance capture in one montage;
  - the flight and the final benches.
- **Gate:** at most 0.2 ms per column; the final per-preset medians of three against 5 ms, read as Q11 decides.

## 5. Acceptance captures and figures

| Criterion | Evidence | Session |
|---|---|---|
| Trees across column borders are whole | `Trees.Borders` in every load order; border shots with the column-grid overlay | S9 |
| Ore histogram by Y matches the configured shapes | `voxel.OreHistogram` CSV and a PNG per ore against the height provider's shape, with a chi-square bound (caves and discards off); veins counted on their own | S2, S4 |
| Same area, different order, identical hashes | `Decoration.LoadOrder` (fixed area, shuffled orders, 1 and 30 workers); streaming hashes over two focus paths; `Decoration.Oracle` | S2, S3; every later session reruns them |
| Placed features with modifiers and the 11 steps | `Features.Read` counts; our order against vanilla's | S1, S2 |
| Lakes and springs | A lava-lake shot; springs behind their switch | S11 |
| Carver caves and canyons | `-Caves` canyon and worm cave | S10 |
| Meshing gated on 3x3 finalization | `Pipeline.Stages`; the flight with no holes | S3 |
| Timings | Each session's gate (3.5); the final medians of three | All; S12 |

## 6. Questions for Jesus

The scout's eight (its 4.3) come first, each with my recommendation; I agree with all eight of the scout's. Three more follow.

1. **Block shapes approach.**
   - (a) **Recommended:** generic baked models: per-state quads from the models, collision from facts.
   - (b) Hand-coded shapes.
   - (c) Generic plus a fast path for crosses.

   (a) covers all 83 blocks and phase 7's stairs, fences and doors. Add (c) only if S6's bench demands it.
2. **Templates and block entities: dungeons (chest, spawner, loot), desert wells, fossils.**
   - (a) **Recommended:** defer them to phase 7, with the template loader.
   - (b) Now.
   - (c) Never.

   They need a template reader and block entities, which phase 7 builds anyway, and they are rare.
3. **Springs while water does not flow.**
   - (a) **Recommended:** supported in the data, switched off by default.
   - (b) A single static source in the wall.
   - (c) A precomputed static fall to the floor.

   (b) looks like a bug, and (c) is throwaway work until flowing fluids exist.
4. **Feature seeds.**
   - (a) **Recommended:** a stable hash of the placed feature's id, and the world seed for flower and noise-count patterns.
   - (b) Vanilla's `featureIndex + 10000 * step` and its fixed seed 2345.

   With (a), a data edit reseeds only the edited feature, and flower fields differ per world. We do not need seed parity.
5. **Terrain slabs.**
   - (a) In S12 as a Finalize pass behind a switch, with the ids and assets decided first.
   - (b) **Recommended:** a separate session after phase 6.
   - (c) Park them again.

   The design is ready (scout 2.4). Its five decisions (ids, textures, tint, drops, grass to dirt) should not slow phase 6.
6. **Plant interaction depth (S7).**
   - (a) **Recommended:** placement checked against the support tags, plus pop-when-unsupported, one level deep.
   - (b) Place anywhere; nothing pops.

   A flower floating after its grass is mined reads as a bug.
7. **Items for decoration blocks whose vanilla item has another id** (sweet berry bush, cave vines, cocoa, kelp plant, tall seagrass).
   - (a) **Recommended:** keep the `Blocks.md` rule: no block item, as with powder snow.
   - (b) Add the vanilla items now.

   Items come with an inventory pass.
8. **The bigger generation ring** (R+3: about +33% generated columns at radius 12; the first view needs 7x7 generated columns).
   - (a) **Recommended:** accept it and measure, with S3's gate (first view at most 4.0 s, the flight within budget).
   - (b) Lower `ViewRadius`.
   - (c) Later, a cheaper "decorated but not meshed" outer ring.

   Columns cost about 0.35 to 0.46 ms of worker time each across 30 workers.
9. **A speed-up session before phase 6** (the handoff's 4b).
   - (a) **Recommended:** yes, timeboxed, with exact levers only and the handoff's gates. AMPLIFIED is at 5.32 ms, and decoration adds to the same budget (3.5).
   - (b) No: go straight to phase 6 and speed up after it.
   - (c) Fold the levers into S2.

   The levers touch only Generate, so they do not conflict with phase 6's code. Doing them first gives every phase 6 gate a baseline under the limit, so decoration's own cost stays visible. (c) mixes two areas.
10. **Session order.**
    - (a) **Recommended:** section 4's order: data, the decoration core with ores, streaming, ore veins, then the block models, then plants, trees, carvers, underground, and ocean and cold.
    - (b) The block models first (S5 to S7), as the Status table lists the block-shapes step before phase 6.
    - (c) Fewer, larger sessions (the scout's six to eight), bundling areas.

    The pipeline change is the riskiest part, and it needs no new blocks. Ores and the load-order acceptance land early, and S5 can run any time before S8. (c) costs more than the split.
11. **What the 5 ms limit covers once decoration exists.** At the allowances in 3.5, phase 6 adds about 3 ms per column.
    - (a) **Recommended:** the whole column (Generate, Decorate and Finalize), as the prompt says. Each session keeps to its allowance and flags any rise past 5 ms. Phase 6 will likely end over 5 ms on every preset, recorded as a known gap with its levers and a speed-up after phase 6.
    - (b) 5 ms for Generate, with decoration on its own allowance of 2 ms for phase 6.
    - (c) Hold every session under 5 ms for the whole column, with speed-up work inside the sessions as needed.

    (a) keeps the prompt's yardstick and makes the cost visible without stalling the phase. (c) mixes areas in every session.

**Still open from earlier** (no new recommendation): Single Biome with a non-overworld id refuses the world (built). The alternative is a plains fallback. Answered in section 9.

## 7. Risks

- **Behaviour from probes:**
  - About 40 feature types, 23 placers and 11 decorators are made in the jar's code. Ours come from black-box probes and must stay original code.
  - Each session probes before it builds; matching is statistical ("Minecraft-like"), as phase 5's surface rules were.
  - The probe harness needs a server run on Jesus's PC, so check the processes first.
- **The pipeline change (S3)** can bring holes, memory growth or cancellation bugs.
  - Mitigations: S2 proves the merge on the fixed area first (oracle, load order); `voxel.Decoration 0` stays one switch away; the flight gates S3.
- **Cost:** about 3 ms per column on top of 4.5 to 5.3 ms (Q11). Write logs and working copies add transient memory; S2 measures it.
- **The asset reimport (S5):** 83 blocks, 135 textures and about 950 states into `DA_Blocks`.
  - Saved ids are string ids, so they hold.
  - The tour, `create-world.mjs` and the Playground probe catch regressions.
  - Cutout mips need care, or distant grass dissolves.
- **Overlapping trees:** a known, deterministic difference from vanilla. The earlier tree in the merge order keeps its blocks.
- **Goldens:** five sessions change blocks on purpose. Each re-record names its decided change. A golden that moves without one is a bug.

## 8. Short prompts for the first two sessions

Jesus answered Q9 yes (section 9). The chain runs S0, then S1, then stops for his review before S2.

S0:

> Voxel world: the AMPLIFIED speed-up session (S0), the first session of phase 6's chain.
>
> Read, in this order:
> 1. `C:\dev\projects\Unnamed\Docs\Records\2026-10-09-Voxel-Handoff-Phase6.md`: sections 1 to 3, 4b and 5 to 9.
> 2. The phase 6 plan (`Docs/Records/2026-10-09-Voxel-Phase6-Plan.md`): section 3.5, S0 in section 4, and Jesus's answers in section 9.
>
> The goal is AMPLIFIED's column mean under 5 ms (`voxel.Bench Density 4`, median of three on a quiet machine), with no preset slower.
> - Use exact levers only:
>   - every gate at 0 differing blocks;
>   - `Density.Golden`, `Aquifer.Golden` and `Noise.Golden` unchanged;
>   - the radius-8 hashes equal in both load orders.
> - Profile before trying levers, and timebox each lever.
> - In the same commit, add `Tests/Evidence/` and `logs/` to `.gitignore`; delete nothing.
>
> Send the manager a plan (levers, files, gates) before building. Report as the handoff's section 2 says, and measure your context at each checkpoint. End with a handoff into S1 (worldgen data, reader and probes); S1 runs next.

S1:

> Voxel world phase 6, session 1 (S1): worldgen data, reader and probes. S0, the AMPLIFIED speed-up, ran before it.
>
> Read, in this order:
> 1. The newest voxel handoff: the first voxel handoff in `C:\dev\projects\Unnamed\Docs\Records\README.md`. That is S0's, which replaces the phase 6 handoff as the entry point.
> 2. The phase 6 plan (`Docs/Records/2026-10-09-Voxel-Phase6-Plan.md`): sections 1, 3.1, 3.2 and 3.5, S1 in section 4, and Jesus's answers in section 9.
> 3. The scout's research (`Docs/Records/2026-10-09-Voxel-Phase6-Scout-Research.md`): sections 1.1 and 5.
>
> Build S1 only:
> - the reachable placed features, features, providers, carvers and tags into the worldgen reference through `build_reference.py`, with the existing files byte-identical;
> - the verifier's new checks;
> - `WorldgenFacts.java`'s provider histograms, vanilla's step order and the ore-vein probe;
> - the feature probe harness, with the ore probes (check `Get-Process java, javaw, UnrealEditor*` before server runs);
> - `FVoxelFeatureGraph` and its strict reader, with round-trip tests.
>
> Goldens and the radius-8 hashes must not change. Feature seeds follow Jesus's answer 4: a stable hash of the placed feature's id, and the world seed for flower and noise-count patterns.
>
> Send the manager a short plan (files and why) before building. Report as the handoff's section 2 says, and measure your context at each checkpoint.
>
> After S1 the chain stops for Jesus's review: end with a handoff into S2 (the decoration core, with ores) and do not start S2.

## 9. Jesus's answers (October 9, 2026, just before 17:38)

Jesus answered every question by tap, through the day manager (Hemera). These are settled; treat them as decisions.

**Phase 6 questions (section 6):**

1. **Block shapes:** (a) generic baked models.
2. **Templates and block entities:** (a) deferred to phase 7, with the template loader.
3. **Springs:** (a) in the data, switched off by default.
4. **Feature seeds:** (a) a stable hash of the placed feature's id, plus the world seed for flower and noise-count patterns.
5. **Terrain slabs:** (b) their own session after phase 6.
6. **Plant interaction:** (a) placement checked against the support tags, plus pop-when-unsupported, one level deep.
7. **Items:** **(b) add the vanilla items now**, overriding the recommendation (a).
   - This changes a project rule: `CLAUDE.md` says item id = block id, and these items have their own ids.
   - S7 does the work (section 4). It updates the id rule in `CLAUDE.md` and `Docs/Blocks.md` as Jesus decided, adds the items as new saved ids, and never renames an existing id.
8. **The R+3 ring:** (a) accept it and measure, with S3's gate.
9. **Speed-up first:** (a) yes. S0 runs first.
10. **Session order:** (a) section 4's order: data, the decoration core with ores, streaming, ore veins, then the block models, then the rest.
11. **The 5 ms limit:** (a) the whole column (Generate, Decorate and Finalize), with each session's allowance and its flags (section 3.5).

**Single Biome:** a non-overworld id refuses to open the world. The built default stays; the plains fallback is not taken.

**Also decided, for the chain:**
- The chain runs S0, then S1, then stops for Jesus's review before S2.
- S0's commit also adds `Tests/Evidence/` and `logs/` to `.gitignore`; nothing is deleted.
- Today's four look calls, made by the day manager under Jesus's delegation of October 9, stand.
