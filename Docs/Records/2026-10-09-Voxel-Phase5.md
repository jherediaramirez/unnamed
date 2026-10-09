# Voxel world phase 5: biomes and surface - October 9, 2026

The generated world now has 26.3's biomes and surface. Each column picks its biomes from the overworld parameter list over the preset's climate, with an exact lookup. 26.3's own surface rules dress the ground per biome: sand, red sand, terracotta bands, mud, podzol, snow and ice. Snow falls above the snow line and still water freezes in cold biomes. Grass, foliage and water take their biome's colours, blended as vanilla blends them. All of it runs through our own code and noise from the jar's data. The world stays deterministic: every value is a pure function of the seed and the position, and two load orders give the same blocks. Five of the six acceptance criteria are met. The sixth, the 5 ms column limit, is not met for AMPLIFIED (below).

## What changed

Session by session; details in [Voxel world](../VoxelWorld.md) and each session's handoff.

- **Plan** (`7cae8b1`, `406d2dd`): the data check, design answers and Jesus's answers ([the phase 5 plan](2026-10-09-Voxel-Phase5-Plan.md)).
- **Session 2, reference data, blocks and readers** (`eaa6ef3`):
  - into the worldgen reference, through its own tools: the overworld surface rules and conditions, the 56 biomes, and the code-built biome parameter list;
  - the biome and surface rule readers;
  - still lava, powder snow and snow layers, through the block reference;
  - a hard error for saved worlds whose terrain cannot be built.
- **Session 3, performance levers** (`f10f5b6`): the shared corner cache between neighbouring columns, which brings caves-on columns under 5 ms in the bench's best case.
- **Session 3b, cave brightness** (`5524d5d`): eye adaptation between two EV bounds and stronger emission; the cave-lighting comparison.
- **Session 4, biome source, storage, maps and minimap** (`575dd19`):
  - `FVoxelBiomeSource`, an exact pruned search over the 7,594 points (0 of 21,600 quarts differ from brute force);
  - per-column quart storage with a jittered per-block lookup;
  - `voxel.BiomeMap`, `voxel.Debug.Biomes`, `voxel.Debug.Minimap`.
- **Session 5, surface rules, temperature, the snow line and Single Biome** (`c073950`):
  - `material_rule/overworld` compiled into a 261-instruction program with an exact shortcut (deep and interior tables);
  - temperature with the frozen modifier and the snow line, and the freeze pass;
  - Single Biome's fixed source.
- **Session 6, tints, ocean variants, the normal-noise lever, captures and this record** (this record's commit):
  - **Biome tints** (`Biome/VoxelBiomeTints.*`, `Meshing/VoxelMesher.cpp`): water, grass and foliage colours per biome, from the data and the block reference's colormaps, with the dark forest and swamp modifiers. The mesher writes each face's colour per vertex, blended over 5 x 5 blocks (vanilla's default). The greedy-merge key now includes the tint. `voxel.TintBlend` (2; 0 hard edges; -1 off) changes live.
  - **Which blocks:** `FVoxelStateInfo::TintSource`: grass block, the colormap leaves, and water; spruce and birch leaves keep their constant colours.
  - **Batched normal noise:** exact; it reuses each lattice cell's gradients across consecutive points.
  - **Tried and reverted:** the per-run memo of the surface tree. It was exact but did not pay.
  - **Captures:** `CaptureVoxel -SpawnExact` (capture only), so a shot can land on low dry ground such as a swamp's.
  - **Tests:** `Unnamed.Voxel.Tints` (5 tests).

## Decisions

- **Jesus's answers** (plan section 7): generator 2 keeps changing until edit saving (phase 10); biome positions are checked statistically, the climate-to-biome mapping exactly; powder snow and snow layers were added; ore veins are read now and switched on in phase 6; water, grass and foliage tints per vertex in phase 5.
- **Made by the day manager (Hemera) under Jesus's delegation of October 9, for his review:**
  - Session 5: hoodoos and icebergs deferred. The surface rules stay on with our 192-block badlands band pattern and the snow-line wobble, and the timing was accepted for session 5.
  - Session 6: biome blend 2 (16:43, on `TintScenes-20261009-s6c/montage-blend2-vs-0.png`), with 0 one switch away. The frozen-ocean checkerboard is classified as the freeze pass's (a known issue, no fix here). Speed-up stops with AMPLIFIED over 5 ms.
- **Still Jesus's:** Single Biome with a non-overworld id refuses the world (built as the default); the alternative is a plains fallback.

## Acceptance

The plan's section 4.

| Criterion | Result | Evidence |
|---|---|---|
| Plausible biome regions and borders | **Met.** `voxel.BiomeMap` at 8,192 and 32,768 blocks for the three presets; Large Biomes' regions are about 4x larger. 51 of 56 biomes in overworld and AMPLIFIED at 8,192 blocks, 28 in Large Biomes (53 and 52 at 32,768). The jar's lookup samples measure climate space, not area, so they confirm which biomes the mapping reaches; the mapping itself is exact (`Biome.Lookup`: all 5,120 of the jar's samples). Session 6's maps are byte-identical to session 4's | `Tests/Evidence/2026-10-09/BiomeMaps-20261009-north/montage-8192.png`, `montage-32768.png` and a CSV per map; `BiomeMaps-20261009-s6/` (the same maps again) |
| Snowy peaks above the snow line | **Met.** Snow covers the tops above the line; lower slopes stay grass; `Surface.Snow` passes, and snow at 0.15 instead of below fails it | `SurfaceScenes-20261009-1508/montage-snowy-peak.png`; `TintScenes-20261009-s6c/snowy-peak-blend2/` |
| Ocean variants | **Met.** Warm `#43D5EE`, lukewarm `#45ADF2`, cold `#3D57D6` and frozen `#3938C9` water, each in its own shot; frozen oceans freeze where the modifier keeps them cold. Icebergs are not built (decided) | `TintScenes-20261009-s6/montage-oceans.png` |
| Surface per biome | **Met.** Desert to badlands (sand, red sand, terracotta bands), stony and frozen peaks (session 5), a swamp shot from inside the swamp (the debug overlay reads `minecraft:swamp`) and a mangrove swamp | `SurfaceScenes-20261009-1508/montage-desert-badlands.png`, `montage-frozen-peaks.png`; `TintScenes-20261009-s6/montage-swamp-mangrove.png` |
| Reproducible | **Met.** Biome and block goldens unchanged by session 6; radius-8 hashes equal in both load orders (`0xeb1c3d76838e2481`, `0xcb42685280f8b57c`, `0xc4403ff66465fcf0`, as session 5); 0 of 7,962,624 blocks differ with the surface shortcut, biomes, sharing and skipping on and off; 0 of 478,352 vertex tints differ with the tint shortcut on and off | `BenchLogs-20261009-s6/final.txt`, `lever1-r8.txt` |
| Timings | **Not met for the 5 ms limit:** AMPLIFIED's column mean is 5.32 ms (median of three on a quiet machine), over the limit. Overworld (4.50) and Large Biomes (4.88) are under it. The 2 ms target is met by none. The flight keeps the 2 ms chunk budget in all but 4 of 10,302 frames, with no holes | below; `VoxelFlight-20261009-165749` |

## Performance against the targets

| Preset | Before session 6 | Run 1 | Run 2 | Run 3 | Median | Against 5 ms |
|---|---|---|---|---|---|---|
| overworld | 4.91 | 4.50 | 4.50 | 4.81 | **4.50** | under |
| large_biomes | 5.03 | 4.71 | 6.21 | 4.88 | **4.88** | under |
| amplified | 6.15 | 5.16 | 5.35 | 5.32 | **5.32** | **over** |

- **What was measured:** Development editor, seed "Unnamed", `voxel.Bench Density 4` (81 columns, one thread, caves, biomes and surface rules on), column mean in ms. The machine was quiet (no `java`, `javaw` or `UnrealEditor` process) at 15:45 (before), 16:24 (runs 1 and 2) and 16:49 (run 3). Run 2's large_biomes pass is an outlier (p95 11.9 ms), hence the median of three.
- **The 2 ms target** is met by no preset.
- **Stages, run 1** (overworld / large_biomes / AMPLIFIED, mean ms): corners 0.94 / 1.17 / 1.31, aquifer 0.57 / 0.48 / 0.77, biomes 0.17 / 0.15 / 0.17, surface 0.73 / 0.76 / 0.89.
- **Across 30 workers:** 0.35 to 0.46 ms per column.
- **What session 6 did about it:** the batched normal noise (exact) took 0.3 to 0.5 ms off the corners stage. The per-run memo of the surface tree was exact but did not pay, so it was reverted ([Voxel world](../VoxelWorld.md), "Surface rules"). Per the day manager's condition, speed-up work stopped there; AMPLIFIED is left for its own session.
- **Tints in the mesher:** +1.0% quads; the mesh time stays within run-to-run noise (+3.5% and -5.6% in two runs).
- **The flight** (`VoxelFlight-20261009-165749`, the acceptance path): 4 of 10,302 frames over the 2.0 ms chunk budget (east legs at 20, 40 and 80 blocks/s; worst 4.10 ms at 80), no holes, memory flat (3,857 -> 3,854 MB). Start to view on screen 3.2 s. Phase 4's record had 3 of 10,107 frames over, worst 2.12 ms.

## Problems found and fixed in session 6

- **`-ExecCmds` comes too late for start-time settings:** a capture's console commands run after the world has started streaming. `voxel.TintBlend` was read only at the start, so the first "blend 0" shots showed blend 2, and the first look call was made on them. The variable is now live (`FVoxelColumnPipeline::SetTintBlend` remeshes every column). The look call was made again on valid shots, and the invalid ones are marked (`TintScenes-20261009-s6`, `*-blend0`, `*-blend-1`).
- **Missing neighbours in the blend:** at the edge-walls view's cut, blend 2 mixed plains colour into the edge columns' water. A block in a missing column now takes the meshed column's nearest block.
- **Blend 2 first cost +19% mesh time** (each block's biome lookup hashed its 8 quart corners). Per-column jitter caches brought it within noise.
- **Swamp shots landed outside the swamp:** the spawn search's dry-land estimate calls swamp ground sea. `-SpawnExact` checks the real columns.

## Verification

After the reviews' fixes:

| Check | Result | Evidence |
|---|---|---|
| `TestVoxel.ps1` | 61/61 | `VoxelAutomation-20261009-170107` |
| Every native suite | 109/109; `Density.Terrain` passed with the known `LogHttp` warning (network) | `AllNative-20261009-170148` |
| `-DisableAdaptiveUnity` build | OK | |
| `create-world.mjs --editor` | PASS | `Worlds/Create-1791578987275` |
| Playground probe | 0 failures, 43.77 FPS at a 60 cap | `Playground-all/Runtime-20261009-165603` |
| The flight | 4 of 10,302 frames over budget, no holes | `VoxelFlight-20261009-165749` |
| `npm test`, `source-check.mjs`, both references' `verify_reference.py` | pass, PASS, OK (worldgen: 228 files; blocks: 1,246 files) | |
| Package `VoxelPhase5Session6-20261009` | Built. The colormaps are staged under `Reference/` and read: the packaged swamp shot shows the swamp's colours. The `UVChannelData.bInitialized` ensure still fires once, from a runtime-built static mesh in an instanced component (texture streaming), not from the chunks | `PackageShots-s6-swamp` |
| Biome maps | The six maps (8,192 and 32,768 blocks, three presets) are byte-identical to session 4's | `BiomeMaps-20261009-s6` |
| Mutations | Swamp threshold `<= -0.05` fails `Tints.Swamp`; blend radius 1 run as 2 fails `Tints.Mesher` | |
| Reviews | Two Sonnet 5.5 reviewers. **Code:** no high or medium; lows fixed (a biome-count guard for the byte cache, a `static_assert` on the column split, the scratch's one-sampler-per-thread rule written down). **Tests and docs:** two mediums fixed (a stale count; the test helper now follows the missing-neighbour rule, and a new case meshes with two neighbours missing); lows fixed | |

## Known issues

| Issue | Due |
|---|---|
| AMPLIFIED's column mean is over 5 ms on a quiet machine | Its own speed-up session (levers: conditional noodle corners, an aquifer status cache, block fill by runs, the surface tree's biome tests) |
| Frozen-ocean checkerboard: merged squares under ice read lighter or darker (translucent ice over water, sorted per merged quad); also with tints off, so from session 5's freeze pass | A translucency-sorting task |
| No eroded-badlands hoodoos or frozen-ocean icebergs (code-made in 26.3) | Decided out of phase 5 |
| Ore veins give nothing | Phase 6 |
| The freeze pass takes block light as 0, runs before trees and uses a hard-coded snow-support list | With trees and light; tags with the next blocks pass |
| Snow layers: one per placement, no stacking, eight collide as a full cube, a cube icon; a single layer is not replaceable | A later blocks pass |
| Powder snow: solid, no sinking, no item | With buckets and movement rules |
| Single Biome refuses non-overworld ids | Jesus's call |
| The swamp's grass pattern uses our own seedless info noise, so it does not match the jar block for block | Accepted ("Minecraft-like") |
| Dry foliage colours need `dry_foliage.png` (facts G1); no block uses them yet | When such a block arrives |
| Lava in a cave clips; water against lava meshes as one fluid; a saved world with unbuildable terrain returns to the title with the reason only in the log | As in session 5's list |
| `voxel.Debug.Climate C` in game is untried | Next playtest |

## Evidence

Best first, under `Tests/Evidence/2026-10-09/`:

- `TintScenes-20261009-s6c/montage-blend2-vs-0.png` (the look call); `TintScenes-20261009-s6/montage-oceans.png`, `montage-swamp-mangrove.png`, `montage-before-after.png`; `TintScenes-20261009-s6b/montage-frozen-ocean-checkerboard.png`;
- `SurfaceScenes-20261009-1508/` (session 5's surface montages);
- `BiomeMaps-20261009-north/` (session 4's maps and montages), `BiomeMaps-20261009-s6/` (the same maps again);
- `BenchLogs-20261009-s6/`: `baseline.txt` (before), `lever1.txt` and `lever1-r8.txt` (the noise lever and its hashes), `lever2.txt` and `memo-diag.txt` (the memo and why it was reverted), `final.txt` and `final-run3.txt` (the three quiet runs), `tintstats-c.txt` (the look call's means);
- `VoxelFlight-20261009-165749`, `PackageShots-s6-swamp`.

## Next

Phase 6, decoration, after a block-shapes step (plants and cross models), with ore veins switched on. Start with [the handoff into phase 6](2026-10-09-Voxel-Handoff-Phase6.md).
