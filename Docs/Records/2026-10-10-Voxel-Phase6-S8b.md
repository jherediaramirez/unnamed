# Voxel world phase 6, S8b: the plants' gate, evidence and docs - October 10, 2026

S8b finishes phase 6's S8 ([the plan](2026-10-09-Voxel-Phase6-Plan.md), lines 405 to 428). Part A ([its record](2026-10-10-Voxel-Phase6-S8.md), `fa3a5c1`) built the plants. S8b ran in the main lane under the night manager's chain (Nyx), from `c049d46`, and did four things:
- the timed gate and the flight, in a quiet window;
- the evidence shots, with two debug helpers and a capture option to take them;
- the S8 section of [VoxelWorld.md](../VoxelWorld.md#plants-and-patches-phase-6-s8);
- this record.

Part B (glow lichen, the unused modifiers and predicates, the seven later plants) waits for Jesus's answers to scout I's questions 1 to 3; none had arrived.

## 1. What was built

| Area | Files | What |
|---|---|---|
| `voxel.FindBiome` | `Source/Unnamed/Voxel/VoxelBiomeDebug.cpp` | `voxel.FindBiome <biome> [X Y] [MaxRadius 6000] [preset]` searches a 32-block lattice outward from (X, Y), sampling the biome source at the terrain's surface estimate (sea level for oceans). It confirms candidates by generating their 3 x 3 columns and logs up to three spots: the cell, column, distance, surface and floor Z, top block, and coverage (at least 60% of the 3 x 3's surface cells, reading the floor under water). It runs headless with the world's seed or the dev seed. |
| `voxel.FindBlock` | `Source/Unnamed/Voxel/VoxelBiomeDebug.cpp` | `voxel.FindBlock <block> [X Y] [radius in columns 12] [preset]` decorates columns outward, each alone (`DecorateAlone`, as `voxel.Decoration.Plants` says), and logs the first three that hold the block: count, lowest cell nearest the centre, floor and surface Z. It was written by the lead after the first ocean shots, which showed seagrass but no kelp: kelp's count is noise-based and sea pickles grow in one chunk in 16. |
| `voxel.Debug.ColumnGrid` | new `Source/Unnamed/Voxel/VoxelColumnGridDebug.cpp` | Debug lines on the borders of the 7 x 7 columns around the player. The lines follow OCEAN_FLOOR (the ground under plants and water) and have posts at the corners. They are drawn each frame from `FWorldDelegates::OnWorldPostActorTick` while the variable is 1. |
| The `view` shot | `Source/Unnamed/Voxel/VoxelWorld.*`, `Scripts/CaptureVoxel.ps1` | `-VoxelShotView=CX,CY,CZ,TX,TY,TZ` (`CaptureVoxel -View`) adds an aimed shot after `edits`. The torch probe's and S4's shot code are untouched (Nyx's condition). |
| Docs | `Docs/VoxelWorld.md` | Status; the Decoration row's counts; "Plants and patches (phase 6, S8)" (the pieces, the "ours" list, merging, the switch, the hashes, the gate, the evidence, the tools); the variable and command entries; the `Plants.*` test row; known gaps. |

A Sonnet 5.5 subagent wrote the three code helpers from an exact spec, edits only, during the quiet window. The lead reviewed them and fixed three things: the handler's argument order (a compile error), the grid's heightmap (WORLD_SURFACE rode on the plants), and FindBiome's line format.

## 2. The gate (quiet window, 07:10 to 07:31)

**Setup:**
- Both sides were built with `build.ps1 -NoUnity`, which passes `-DisableAdaptiveUnity`. Old is `0273eb0`, fa3a5c1's parent: its Source was reverse-applied with `git apply`, so the files got fresh timestamps. New is S8 part A.
- The DLLs and `.modules` were saved in `C:\dev\sandbox\phase6s8b\{old,new}build` and swapped per run (S4's method; `gate8.ps1`, `gate8b.ps1`; summaries by `gatesum8.py` and `gaterounds.py`).
- Each launch checked a cancel flag and logged the machine: no other editor, java or UnrealBuildTool; 16 idle Python MCP servers; CPU 0 to 26%.

**Two runs of the bench:**
- **In one process:** `gate8.ps1` ran three presets per editor process, alternating old and new, with new running plants 0 and 1 per preset in an order that flipped each round.
- **One bench per process:** its Finalize figures looked like an order effect, so the manager granted 8 more minutes. `gate8b.ps1` ran overworld and amplified with one bench per process, rotating new plants 0, new plants 1 and old. It showed that the "order effect" was mostly single-run outliers of 0.2 to 0.3 ms on both sides.

| Preset | Patches (Decorate on minus off), p50 / mean | Decorate p50, old to plants on | Finalize p50, old / off / on | Whole column, old / off / on |
|---|---|---|---|---|
| overworld (one per process) | 0.129 / 0.062 | 0.579 to 0.759 | 0.177 / 0.221 / 0.278 | 4.270 / 4.561 / 4.670 |
| large_biomes (in process) | 0.094 / 0.140 | 0.532 to 0.631 | 0.187 / 0.225 / 0.256 | 4.088 / 4.256 / 4.789 |
| amplified (one per process) | 0.192 / 0.172 | 0.638 to 0.846 | 0.208 / 0.375 / 0.259 | **5.594 / 5.392 / 5.499** |
| overworld (in process) | 0.107 / 0.015 | 0.606 to 0.755 | 0.178 / 0.216 / 0.507 | 4.406 / 4.289 / 4.437 |
| amplified (in process) | 0.103 / 0.067 | 0.610 to 0.810 | 0.190 / 0.295 / 0.274 | 4.967 / 5.510 / 5.349 |

- **Met:** the patches are 0.09 to 0.19 ms at p50 and 0.02 to 0.17 in the mean, against 0.4 (scout I estimated 60 to 110 µs). The busy-machine 1.2 ms of part A was the machine.
- **Control:** at plants 0 the final-column hash equals the old build's (overworld `0x44ebe3f1b15b524b`, large_biomes `0xc49639a9f7f90b09`, amplified `0xaf4e0e637ea1c2dc`); at plants 1 it differs (`0x9abcfaf52b07169b`, `0x73eef12b16a77239`, `0x15b93961120789d0`).
- **Finalize:** plants' own cost is about 0 to 0.05 ms. Plants-off against old is about +0.04 p50 on overworld (S8's inert replay branches). One bench per process, the means stay under S2 + S3's 0.3, apart from a noisy plants-off amplified median. In one process, overworld with plants on reads 0.507: two of its three runs were that preset's second bench.
- **Flagged, amplified against 5 ms:** every build, old included, read over 5 ms in this window; S4's quiet window had 4.63. Generate's mean swings 3.7 to 4.9 ms on identical code. By stages S8 adds about 0.28 ms (Decorate mean +0.26, Finalize +0.02), about 4.91 on S4's footing. Nyx accepted it with no lever; it joins the speed-up item on Jesus's list (plan 3.5, Q11).
- **Meshing** (`voxel.BenchModels 4 3`, 2 runs, plants off and on in each): the decorated area with plants has 1,328 quads per column (92 model, +16.6 KB) against 1,236. Per meshed section: 0.305 / 0.276 ms with plants, 0.321 / 0.322 without; S6 had 0.28. The synthetic planted case's model quads cost 0.072, 0.103, 0.145 and 0.265 ms per planted column (S6: 0.176 / 0.136).
- **Flights** (old, new, new plants 0, old, new; `Tests/Evidence/2026-10-10/VoxelFlight-20261010-071410`, `-071613`, `-071816`, `-072019`, `-072222`):

| Run | Holes | Frames over 2 ms | Memory, laps 2 to 3 (process; columns MB) |
|---|---|---|---|
| old | none | 0 of 10,264 | 4,212 to 4,211; 18.55 to 19.22 |
| new | none | 0 of 9,678 | 4,267 to 4,268; 18.85 to 19.53 |
| new, plants 0 | none | 1 of 10,207 | 4,078 to 4,086; 18.55 to 19.22 |
| old | none | 3 of 10,270 | 3,980 to 3,976; 18.55 to 19.22 |
| new | none | 1 of 9,697 | 4,290 to 4,294; 18.85 to 19.53 |

Chunk work per circle averages 0.43 ms with plants against 0.37 to 0.38 without; about 6% fewer frames over the 100 s (rendering the plants). The first view takes 3.7 to 4.1 s on both builds.

## 3. Evidence

**The setup:**
- Every pair is the same aimed `view` shot at a `voxel.FindBiome` spot (dev seed): `CaptureVoxel -SpawnAt X,Y -SpawnExact -View ...`, plants on and then `voxel.Decoration.Plants 0`, the control (`C:\dev\sandbox\phase6s8b\shots8.ps1`).
- Folders are `Tests/Evidence/2026-10-10/S8b-Biomes/<biome>-plants{1,0}-<time>`; the sheets are in that folder.
- Each frame's luma and its share of near-black and saturated pixels are from `C:\dev\sandbox\phase6s8b\sheet8.py`. No frame is invalid: black at most 5.0% (badlands' shadows), saturated at most 0.1%.

| Biome (FindBiome spot) | With plants | Luma on / off |
|---|---|---|
| plains (168, -152) | short and tall grass | 94.6 / 105.5 |
| flower forest (808, 1640) | flowers, grass; a sand disk under the water | 101.1 / 101.4 |
| meadow (-1272, -1272) | dandelions, cornflowers, daisies, grass | 105.1 / 109.7 |
| desert (232, 72) | cactus, dead bushes, dry grass | 180.6 / 183.5 |
| badlands (264, 136) | dead bushes | 84.8 / 84.7 |
| jungle (-312, 296; the hits near the origin were water) | dense grass and ferns | 96.8 / 92.6 |
| swamp (200, -1240; a wide view) | lily pads on the water, many brown mushrooms, grass | 93.9 / 92.5 |
| lukewarm ocean (8, 104; camera under water) | seagrass and tall seagrass; no kelp in frame | 157.7 / 194.9 |
| warm ocean (296, -56; camera under water) | seagrass and tall seagrass; no pickles in frame | 174.6 / 187.7 |
| taiga (-792, -952) | some grass, ferns, a few flowers and mushrooms; no berries in frame | 94.3 / 93.8 |
| column grid, flower forest (corner 800, 1632) | `voxel.Debug.ColumnGrid 1`: flower and grass patches cross the magenta borders; a sand disk spans two columns | 110.6 / 100.7 |

**The look call:** Nyx decided it, delegated (Jesus may overrule): **accept, plants stay on by default.** The sheet was `S8b-Biomes/sheet-look-meadow-jungle-desert.png` (meadow, jungle, desert; luma above). The control was one run per biome, and every control is the same terrain, bare. All 11 pairs are in `sheet-all-1.png` and `sheet-all-2.png`.

**Observations for Jesus (none blocking):**
- **(a)** The swamp has many brown mushrooms. That follows Q6's wiki rule (mushrooms ignore light during generation); a probe only if he finds it wrong.
- **(b)** The taiga is sparse in view. Berries are rare by the data (patch_berry_common), and there is no vanilla density oracle per biome: scout I's oracles cover per-patch counts only.
- **(c)** Under water the camera sees no water (it isn't drawn from below), and kelp and pickles weren't in frame. Kelp's count is noise-based (`kelp_warm`: noise_based_count 80), and sea pickles take one chunk in 16 (rarity 16). The reshoot below shows both, at `voxel.FindBlock` spots; the water itself still isn't drawn from below.

**The ocean reshoot** after `voxel.FindBlock` (`C:\dev\sandbox\phase6s2\runs\findblock-080503`; sheet `S8b-Biomes/sheet-oceans.png`):

| Pair | With plants | Luma on / off |
|---|---|---|
| kelp, lukewarm, under water (kelp at -11,72; floor 47) | kelp columns up to 9 cells with their tips, seagrass | 135.2 / 164.9 |
| kelp, lukewarm, from above the surface | the same kelp through the water | 136.7 / 151.4 |
| sea pickles, warm ocean, from above (376, 8; floor 55) | pickle clusters among seagrass (seen in a full-resolution crop) | 158.5 / 172.7 |

FindBlock also found one sweet berry bush within 8 columns of the taiga spot (-749, -1072): berries are rare there, as observation (b) says.

## 4. Decisions and notes

- **Auto exposure for the biome pairs:** `r.EyeAdaptation.MethodOverride 3` (the S6 note for brightness comparisons) washed the frames out (luma about 200). The pairs only have to show the switch, so they use the default auto exposure.
- **One bench per editor process** for future gates: in-process repeats of a preset added outliers that read as costs.
- **Part B not started:** Jesus's answers to questions 1 to 3 had not arrived.

## 5. Verified

- Build: `-DisableAdaptiveUnity`, 0 warnings in the changed files. The compile error noted in section 1 was fixed before any run.
- The helpers: FindBiome found all ten biomes (`C:\dev\sandbox\phase6s2\runs\findbiome-073448`, `findjungle-073658`). The grid and the view render (the grid pair).
- **The voxel suite: 101 of 101** (`Scripts/TestVoxel.ps1`, `Tests/Evidence/2026-10-10/VoxelAutomation-20261010-081011`), after the review fixes, on the final build.
- **`voxel.FindBlock`** found kelp, sea pickles and a sweet berry bush (`C:\dev\sandbox\phase6s2\runs\findblock-080503`). FindBiome's results after the review fixes were unchanged (warm ocean, same three spots).
- **Pre-commit review** (one Sonnet 5.5 reviewer; the diff is about 400 lines): nothing serious. Fixed:
  - FindBlock's search is bounded: radius at most 24 columns, 400 columns, a lone number read as the radius;
  - FindBiome's radius is clamped to 20,000, and both commands visit only each ring's perimeter;
  - the docs: the build mode is named as one thing; the patches' range covers p50 and mean; the in-process Finalize outlier is noted; FindBiome's centre-cell condition is stated.

  Not changed: duplicate FindBiome hits on the same ground (usability only); the default terrain settings in both commands (as the benches use).
