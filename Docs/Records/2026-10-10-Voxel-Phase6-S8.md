# Voxel world phase 6, S8 part A: plants and patches - October 10, 2026

Phase 6's S8 ([the plan](2026-10-09-Voxel-Phase6-Plan.md), lines 405 to 428), run in the main lane by the night manager's chain (Nyx), from `10da63e`, after the S6b merge. Part A is the cut line Nyx approved: every vegetation type the data's S8 placed features need, with no answer from Jesus required. Part B waits for his answers to scout I's questions 1 to 3 (below). The timed gate, the flight, the evidence shots and `Docs/VoxelWorld.md` move to **S8b** by Nyx's 500k rule. Scout I's prep doc is committed byte-identical as [2026-10-10-Voxel-Phase6-Scout-I-Plants-Patches.md](2026-10-10-Voxel-Phase6-Scout-I-Plants-Patches.md) (SHA-256 `831F131105CCF867A129988A6083B6E1B809BDE4A71CED261AC155A717E67AC8`, 45,337 bytes, from `C:\dev\sandbox\scout-plants-patches\facts.md`).

## 1. What was built

| Area | Files | What |
|---|---|---|
| Block properties | `Blocks/VoxelBlockTable.*`, `Voxel/VoxelBlocks.cpp` | `FVoxelBlockProperty`; `SetBlockProperties`, `WithValue`, `GetValue`, `MakeState`. MakeTable copies each block's properties and first state (the registry's layout: a cartesian product in declaration order, the last property fastest). Plants with properties (double-plant halves, kelp age, pickles, waterlogging, petal facing and amount) resolve to the registry's state ids. |
| Survival table | new `Blocks/VoxelPlantSurvival.*` | One rule per state from 26.3's support tags (`supports_vegetation`, `_dry_vegetation`, `_sugar_cane`, `_sugar_cane_adjacently`, `_cactus`, `_lily_pad`, `support_override_cactus_flower`, `cannot_support_kelp`, `_seagrass`, and `small_flowers` for the flowers). Kinds: Always, OnTag, SugarCane, Cactus, CactusFlower, UpperHalf, Underwater, Mushroom, SturdyBelow, LilyPad, NotAirBelow. `Survives` (the full rule, side checks included), `SurvivesOn` (the vertical part, which Finalize re-checks), `GetSturdyFaces` (leaves are not sturdy, scout H's measurement). The S7 lane plans against this API (Nyx relayed the signatures). |
| Int providers | `Decoration/VoxelNumberProviders.*` | `biased_to_bottom`, `weighted_list` (entries of any compiled provider), `clamped`; exact distributions. |
| Live heightmaps | `Decoration/VoxelDecorationContext.*` | `EVoxelDecorationHeightmap` (26.3's six names). The `_WG` maps are the snapshot's; WORLD_SURFACE, OCEAN_FLOOR, MOTION_BLOCKING and MOTION_BLOCKING_NO_LEAVES are raised by this Decorate's writes (FVoxelColumn's classes). Only plants turn it on (the context gets the table). |
| Write log | `Decoration/VoxelWriteLog.h` | Mode `IfUnchanged` (the cell must still hold the state the Decorate saw), supports `Plant` (the state's vertical survival against the merged cell below) and `PlantPair` (a lower half lands only with the next write, its upper half). |
| Predicates | new `Decoration/VoxelBlockPredicates.*` | true, matching_blocks, matching_block_tag, matching_fluids, solid (read as blocks_motion), replaceable, has_sturdy_face, would_survive, inside_world_bounds, all_of, any_of, not. Offsets turned to our axes. Others skip their placed feature. |
| State providers | new `Decoration/VoxelStateProviders.*` | simple (bare states), weighted, rule_based, randomized_int (bare source), noise, noise_threshold, dual_noise, named or inline. Noises are ours (`FVoxelNormalNoise`) seeded by `GetPatternSeed(world seed, data seed)` (Q4). The mappings left open by the wiki are ours: noise linear over [-1, 1); dual_noise's slow noise sets the variety and where in the list it starts, the fast noise picks. |
| Features | new `Decoration/Features/VoxelPatchFeatures.*`, `VoxelSelectorFeatures.h` | simple_block (survival checked; two-cell plants need their upper cell free and go in as a pair), block_column (heights drawn first, the free run trims the base first with prioritize_tip, else the top), disk (dx² + dy² <= r², columns top down, target per cell), vegetation_patch (floor only: our footprint, corners out, edges by chance, vegetation after the whole ground), the four selectors, sequence, no_op. |
| Decorator | `Decoration/VoxelDecorator.*` | The new modifiers (heightmap, offset, block_predicate_filter, noise_threshold_count, noise_based_count, surface_relative_threshold_filter), nested placed features (compiled once, never with biome), `bPlants` (default on), `bLaterPlants` (default off: Q1's seven), Finalize's replay of the new mode and supports, `Describe` with the skip reasons, two test mutations. Not copyable (its parts point at each other). |
| Game | `Voxel/VoxelConsole.*`, `VoxelWorldSubsystem.*`, `VoxelModelBench.cpp` | `voxel.Decoration.Plants` (default 1): streamed worlds rebuild on a change; every bench follows it. |
| Tests | new `Tests/VoxelPlantTests.cpp`; `VoxelDecorationTests.cpp`, `VoxelMeshDigestTests.cpp`; `Scripts/TestVoxel.ps1` (101) | below |

What runs: 104 placed features compile (30 ores, **74 plants**: 70 in vegetal_decoration, the 4 disks in underground_ores), 67 skipped: trees 17, fallen trees 8, Q1's seven, springs 3, lakes 3, and single types (glow lichen's multiface_growth among them).

## 2. Decisions and design notes

- **Plants off is the old code path:** with `bPlants` false only ore features compile, the context gets no table, and every replay branch S8 added is inert. Today's goldens, radius-8 hashes and mesh digest reproduce exactly (section 3).
- **`IfUnchanged` instead of the plan's `IfReplaceable` for plants:** a plant lands only where the cell still holds what its own Decorate saw. Two columns' patches at one cell resolve in merge order (the first wins), and no plant ever lands on half of another's two-cell plant. Disks keep `IfReplaceable` over their target set, re-checked at replay.
- **What replay re-checks:** the vertical support only (the cell below lies in the target column, so the check is exact). Sugar cane's water and cactus's free sides are checked at Decorate, not again at replay (a neighbour's write beside them can break them; recorded as known).
- **Q1's seven stay off** (`bLaterPlants`, one switch away), as recommended to Jesus; glow lichen waits for Q3; the five modifiers with no S8 user, the unused predicates and rotated / random_block wait for Q2.
- **Leaves are not sturdy** (S7 lane's FYI from scout H's measurement); no test pins leaves, mud, soul sand or eight-layer snow.
- **Pre-commit review** (two Sonnet 5.5 reviewers, split by path; nothing serious found). Fixed:
  - `voxel.BenchModels` now follows `voxel.Decoration.Plants`;
  - the replay test checks that a pair's upper half is the next write, in the cell above;
  - `GetValue` skips a property with no values;
  - `clamped`'s range when the source lies outside the clamp;
  - dual_noise's variety listed in descending order;
  - randomized_int over an unbuilt int provider now skips its feature instead of failing the decorator;
  - `Describe`'s skip list is in a stable order;
  - vegetation_patch's footprint reaches the drawn radius plus one, from scout I's pale moss probe (69 blocks per origin; farthest 3.6 to 6.4 blocks fits radii 3 to 5).

  Recorded, not changed: the replay's vertical-only re-check; snow layers' sturdy faces (Jesus's question); `Refused` counts approximate in one corner case.
- **A non-adaptive unity build** (`-DisableAdaptiveUnity`) found two anonymous-namespace helpers whose names clash in one unity blob (`GetNumber`, `GetString`); they were renamed.

## 3. Verified

- **All native suites 156 of 156** after the review fixes, on T2's merged tree (`0273eb0`), built with `-DisableAdaptiveUnity` (`C:\dev\sandbox\phase6s2\runs\native-064540`); the voxel suite alone 101 of 101 (`Scripts/TestVoxel.ps1`, `Tests/Evidence/2026-10-10/VoxelAutomation-20261010-064143`, after the review fixes and before T2).
- **Plants off reproduces everything:** `Decoration.Golden` (0xa28113883ba6e7ca, 0xf87e27cd1455c61a, 0xfc7c6279d46e6b76; writes 761,145, 768,534, 823,270), `Mesher.Digest` (0xa8ba8d0d0a39174f, 0xe49601da349c7c0d, 0x7f9491318204f4b2, models on and off), and the six radius-8 hashes (`voxel.Bench Columns 8`, plain 0xe3e4e339022a4ec0, 0x92d83639acdee88f, 0x615321e7b313fed6; decorated 0x80485dafc2e934f4, 0xf0684ae48c1cd603, 0xc01314216b7aa817).
- **Plants on, recorded under the decided change:** Golden 0x9cce36407bad56b5, 0x92fd17ac831fcd42, 0x6cdc112a2c9575f6 (writes 768,854, 774,466, 829,090); digest models on / off: overworld 0x75602ae1d22e81f3 / 0x6b673e48bda8a0b7, large_biomes 0x33f1173bd0580659 / 0x81cbb033a8f2c493, amplified 0x1d96e2fcc34c085e / 0xa0fbd505d2c267f9 (107,601 quads against S6's area); radius 8: overworld 0x41fda26c3faa91da, large_biomes 0x652c06643974705f, amplified 0x5454860304d8f56c, each equal in both load orders (row by row on one thread, backwards across workers). `Decoration.LoadOrder`, `.Oracle` and `.Reach` pass with plants on (0 writes dropped).
- **Scout I's oracles** (`Plants.PatchCounts`, 2,000 origins on flat ground, bound about 4.5 standard errors): short grass 7.835 (analytic 7.79, ±0.25), tall grass 22.228 (22.12, ±0.40; every lower half has its upper), flowers ±6/±2 28.018 (27.83, ±0.40), seagrass 41.029 (41.02, ±0.40).
- **Replay (Nyx's condition 4, `Plants.Replay`):** real writes of seed 263 replayed alone land on their snapshot and are refused (and counted) when changed under them: a plant's support removed, its cell taken, a two-cell plant's upper cell taken (both halves refused), a disk's target replaced by stone.
- **Mutations (condition 3):** 5, supports_vegetation left empty: `Golden`, `Plants.PatchCounts` and `Plants.Survival` fail. 6, the live-heightmap raise frozen: `Plants.LiveHeightmaps` fails (the seed-263 golden area happens not to depend on the raise).
- **Providers.Distribution:** 101 providers checked (80 before; 21 of S8's types), worst z 2.56.
- **After T2's merge:** the plants-off overworld and all three plants-on radius-8 hashes are unchanged.
- **End to end:** Playground probe 0 failures (`Tests/Evidence/2026-10-10/Playground-all/Runtime-20261010-063606`); `node create-world.mjs --editor` passes every step, normal exit (`Tests/Evidence/2026-10-10/Worlds/Create-1791629328005`).

## 4. Not measured here (S8b)

- **The gate** (quiet window, both builds `-NoUnity`, old and new alternated, medians of three): Decorate with plants on minus off at most 0.4 ms per column per preset; mesh per column in plant-dense biomes against S6's 0.28 ms per meshed section; the flight. A first look on a busy machine (another lane's tests running; not a gate figure): `voxel.Bench Decorate 2 overworld` gave Decorate 1.19 to 1.25 ms per column against S2's gated 0.600, Generate 3.40 (gated 2.87 to 3.19). If the quiet figure is over 0.4 ms, the levers are scout I's (5.3): biome lookups dominate.
- **Evidence:** the nine biome shots and the column-grid overlay (`voxel.Debug.ColumnGrid`, a `voxel.FindBiome` helper, `CaptureVoxel` arguments; keep off the torch probe's and S4's shot code in `VoxelWorld.cpp`).
- **`Docs/VoxelWorld.md`**: the Decoration section (S8's pieces, the switch, the hashes above, "ours" for each open algorithm).
