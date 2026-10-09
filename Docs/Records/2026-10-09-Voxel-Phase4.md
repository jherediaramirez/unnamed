# Voxel world phase 4: terrain - October 9, 2026

The voxel world now has real terrain. Generator 2 evaluates Minecraft Java 26.3's own density-function graph, read from the jar's JSON, with our own noise, evaluator, aquifers and code. Oceans, beaches, plains, hills, mountains and caves appear on the three presets (overworld, Large Biomes, AMPLIFIED), and a seed reproduces exactly: the goldens and the load-order hashes match. Still lava exists, built like water. Generation is slightly over the 5 ms per-column limit on two of the three presets, and no preset reaches the 2 ms target with caves on; the section "Performance against the targets" has the numbers. The game-thread budget held in the flight: 3 of 10,107 frames over 2 ms, worst 2.12 ms. Phase 4 ran as five sessions. Sessions 1 to 4 are commits `6c2c608`, `003bf7b`, `416a190` and `b306f87`; session 5: see git log (commit after `516afdb`). The design is in [Voxel world](../VoxelWorld.md).

## What changed

Session by session.

- **Session 1, the 26.3 worldgen data and the density graph** (`6c2c608`):
  - `Reference/minecraft-worldgen-26.3/`, new and hash-checked: the noise settings `overworld`, `large_biomes` and `amplified`, the 51 density functions they use and the 62 top-level noises, byte for byte from `server-26.3.jar`; Mojang's own values for the 9 terrain splines; a manifest, a verifier and a README with the 26.3 format changes;
  - `Density/VoxelDensityGraph`: `EVoxelDensityOp` (30 node types), `FVoxelDensityGraph` (flat nodes, every node after its inputs) and `FVoxelTerrainPreset`;
  - `VoxelWorldgenReader` in the game module: a strict reader with the file and field in every error;
  - tests: `Unnamed.Voxel.Worldgen.Presets`, `.RoundTrip` and `.Splines`. `FVoxelSpline` matches Mojang's `CubicSpline` on all 4,608 samples within 9.5e-7.
- **Session 2, own noise, evaluator, climate, splines and maps** (`003bf7b`):
  - `Noise/VoxelNormalNoise`: octaves from `base_octave` with the modifiers, over our own gradient noise. One constant, `SpreadScale` 1.06, puts all 62 noises within the tolerance fixed beforehand (std ±10%, p1/p99 ±15%) of the spread dumped from the jar (`facts/noise_samples.json`);
  - `Density/VoxelDensityEvaluator`: node by node over a batch of points, only the nodes the roots read;
  - `Terrain/VoxelClimate`: continents, erosion, weirdness, peaks and valleys, temperature, vegetation, and the offset, factor and jaggedness splines per column (46 µs on one thread);
  - black-box facts from the jar for `noise`, `shift_a`, `shift_b`, `lerp`, `gradient`, `range_choice` and others, through a probe that implements `CompileContext` itself;
  - `voxel.ClimateMap` (PNG maps), `voxel.Debug.Climate`, `voxel.Bench Climate`;
  - five new tests.
- **Session 3, every op, base 3D noise, column density with skipping, generator 2 with caves off** (`416a190`):
  - the last ops confirmed against the jar: `interpolated`, `find_top_surface`, `old_blended_noise`, `interval_select` (threshold K-1 inclusive, K exclusive);
  - `Noise/VoxelBlendedNoise`: our own base 3D noise, `OutputScale` 1.89, std 0.1980 against the jar's 0.1982;
  - `Density/VoxelColumnDensity`: corners on 4 x 8 x 4 cells, trilinear inside cells, per-cell bounds that skip cells certainly solid or empty. It equals direct evaluation bit for bit;
  - `Terrain/VoxelDensityTerrain`: generator 2 with bedrock, deepslate, grass, dirt, sand, gravel, water to Z 62, spawn from a surface estimate, and the three presets in `AVoxelWorld::MakeTerrain`;
  - the worldgen reference staged for packages in `Unnamed.Build.cs`;
  - debug views N and PS, `voxel.Bench Density`, stats "Density column" and "Fill column blocks";
  - five new tests, with goldens for seed 263.
- **Session 4, caves, aquifers, still lava, lazy interval_select, cave lighting** (`b306f87`):
  - caves on: the full `final_density` runs (`bCaves` defaults to true); caves off gives session 3's terrain exactly;
  - `Terrain/VoxelAquifer`: our own aquifers on 26.3's inputs and numbers. Cells of 16 x 12 x 16 with jittered centers, statuses global (sea water, lava below Z -54), own level or dry, walls between differing statuses, an open-sky rule that keeps oceans clean. `Prepare` runs before density and tells the column which y levels must not be skipped as empty;
  - still lava, state 105 in the voxel table (block 38, light 15): the fluid shape in the opaque layer so the chunk material's emission applies, the still water texture tinted orange. No damage, no flow;
  - dressing: with caves, only the first surface from the sky gets grass, sand or gravel;
  - lazy `interval_select` (`FindLazyFunctions`): exact, about 10% off a caves-on column;
  - `CaptureVoxel.ps1 -Caves` and `AVoxelWorld::FindCaveShots`;
  - the cave-lighting comparison (decided overnight, below);
  - tests: `Unnamed.Voxel.Aquifer.Determinism`, `.Fluids` and `.Golden`.
- **Session 5, performance, benches, captures, flight, docs** (see git log):
  - four exact performance levers (below);
  - `voxel.Bench Columns [radius] [preset|standin] [nocaves]`: the area stages on one thread and on 30 workers, the generation stages (density, aquifer, blocks), and block hashes in two load orders;
  - the test `Unnamed.Voxel.Density.LoadOrder`; `Scripts/TestVoxel.ps1` expects 43;
  - captures of ocean, plains, hills, mountains and caves, a flight, and the docs.

**The four levers of session 5.** All are exact: goldens stayed byte-identical, and skipping on and off gave 0 of 28,409,856 differing blocks in every preset, caves on and off.

1. **Cells from corners** (stage 3b): after a mixed cell's corners are evaluated, each interpolated node is bounded by its 8 corner values, and the per-block nodes are bounded over the cell. A cell that is certainly solid or certainly empty needs no per-block work. With caves on this settled 34.2% more cells. Cells to blocks went from 2.38 to 0.75 ms; the column from 7.83 to 6.09 ms.
2. **Top-down bounds** (stage 2): each cell column is bounded from the top in stacks of 8 cells while a whole stack is certain, then one cell at a time to the first mixed cell. Bounds went from 1.51 to 0.27 ms.
3. **Trilinear weights table** (stage 4): weights by offset precomputed with the same expression direct evaluation uses, so the result is bit-identical. Skip-off cells to blocks went from 4.4 to 2.5 ms; skip-on from 0.8 to 0.6 ms.
4. **Lazy `range_choice`**: a branch's private nodes run only at the points that pick that branch. Overworld corners went from 2.0 to 1.7 ms, amplified from 3.68 to 2.81 ms.

## Decisions confirmed with the user

On October 8, 2026, before any phase 4 code, the user picked these. All four were the recommended options.

| Question | Decision |
|---|---|
| Lava | Add still lava now, built like water: a state from the 26.3 block reference, its animated texture, a fluid mesher layer, emissive light. Done in the aquifer session. No damage or flow yet |
| Software Lumen | Require hardware ray tracing for now. Per-column mesh distance fields are a later task |
| Fidelity | Evaluate 26.3's density-function graph from the jar's JSON (30 node types) with our own code and noise, checked against vanilla's value spread. Large Biomes and AMPLIFIED use their own presets |
| Old worlds | The new terrain is generator 2. Generator 1 worlds keep the stand-in height terrain (the rule in `UnnamedSavedWorlds.h`) |
| Lumen card occlusion | Decided in the caves session from captures (cards against hit lighting), not up front |
| Plan | Five sessions: reference data and loader; noise, climate, splines and debug views; density, surface and spawn; caves, aquifers and lava; bench, tests, captures and docs |

The user also answered on October 9 (session 3 handoff, section 8): facts files may be added to the reference only through its own tools, with existing files byte-identical; the sentence about regenerating facts went into the `CLAUDE.md` rule; evidence stays untracked.

## Decisions made overnight by the manager (for Jesus's review)

On the night of October 8 to 9, a manager chat ("Chunk loader project manager handoff", local name `dev-10`) stood in for Jesus. Each session reported to it and waited for its reply. It answered routine questions within the settled decisions and could not change project rules, saved data, anything destructive or any push. Everything below needs Jesus's review.

- **Cave lighting keeps Lumen cards.** Session 4 captured the same cave with cards and with hit lighting. The images matched and cards were 0.4 ms cheaper. Hit lighting is one switch away: `r.Lumen.HardwareRayTracing.LightingMode 1` (or `CaptureVoxel -ExecCmds`). Evidence: `C:\dev\sandbox\phase4s4\montage\cavecmp2.png`; `Tests/Evidence/2026-10-09/VoxelShots-20261009-025021` (cards), `-025054` (hit), `-025126` and `-025159` (both at +3 EV). Caveat: the fixed daylight exposure keeps cave interiors near black, which could hide card artifacts. Re-run the comparison once the exposure question is settled.
- **Caves stay in generator 2.** There is no generator 3. Edits are session-only until phase 10, so no saved data changed.
- **Session 5's plan was approved:** exact levers only (goldens byte-identical, skip on and off at 0 differing blocks), a 40% timebox for performance work, the climate minimap as a stretch that moves to phase 5 if it slips, a fresh package name for the package check, and never deleting the existing package.
- **The facts file `noise_samples.json` was approved** in session 2, under four conditions, all met: tools only, existing files byte-identical, the verifier passes, and the file listed in the handoff for Jesus.
- **End-of-session local commits were approved** for sessions 2 to 4, after a clean Sonnet 5.5 review and passing suites. Session 4's handoff added that every suite, the `-DisableAdaptiveUnity` build and `create-world.mjs` re-run after the review's fixes (the manager checks this).
- **Token stop line:** the lead stops at a clean point near 350k tokens of context.
- **The manager pushes after the night's last commit.** The branch is still local only; this record does not say a push happened.

## Acceptance

| Criterion | Result | Evidence |
|---|---|---|
| Oceans, plains, hills and mountains at sensible scales | Met | `VoxelShots-20261009-034919` (ocean, beaches, hills in the overview; terraced plains in the closeup), `-034958` (seed "mountains2": a mountain peak by the sea, a steep slope), `-035028` (seed "isles": hills and beaches). Montage `C:\dev\sandbox\phase4s5\terrain1.png`. Climate maps from session 2 show continents thousands of blocks across |
| Caves exist | Met | `VoxelShots-20261009-034919`: a cave run to open sky at X 250, Y 192, Z 85, and the lava view. Aquifers fill caves with water and lava (`Unnamed.Voxel.Aquifer.Fluids`) |
| A seed reproduces exactly | Met | Goldens in `Unnamed.Voxel.Density.Golden` (caves off) and `Unnamed.Voxel.Aquifer.Golden` (caves on), seed 263, three presets. `Unnamed.Voxel.Density.LoadOrder` plus the `voxel.Bench Columns` hashes: the same block hashes row by row on one thread and backwards across every worker, 0 columns differ (overworld `0x7bbf1f42c5434443`, large_biomes `0xc70b24ba700501f2`, amplified `0x27c2d49086a4614a`, seed "Unnamed", radius 8). Skipping on and off: 0 of 28,409,856 blocks differ |
| Per-column generation time is logged | Met | `voxel.Bench Density` and `voxel.Bench Columns` (mean, p50, p95 per stage); stats "Generate column", "Density column", "Aquifer fill" and "Fill column blocks"; the capture's `build.txt` |

The performance limits are separate from these four criteria and are not all met; see the next section.

## Performance against the targets

Limits from the worldgen prompt: generation under 5 ms per column (target 2 ms), and 2 ms of chunk work per frame on the game thread.

**Generation per column** (`voxel.Bench Density 8 <preset> [nocaves]`, 289 columns, one thread, ms, mean / p50 / p95; machine idle, GPU 0.8%, no `javaw`). Raw: `C:\dev\sandbox\phase4s5\bench-final.txt`.

| Preset | Caves off, skip on | Caves on, session 4 | Caves on, final | Caves on, skip off | Cells skipped (caves on) | 30 workers (caves on) |
|---|---|---|---|---|---|---|
| overworld | 2.03 / 1.63 / 3.84 | 7.83 / 6.57 / 13.78 | **4.53 / 3.84 / 7.71** | 8.09 / 7.12 / 14.01 | 53.8% + 34.2% = 88.0% | 0.260 |
| large_biomes | 2.26 / 1.75 / 4.38 | 8.65 / 7.44 / 14.04 | **5.13 / 4.44 / 8.62** | 7.84 / 6.68 / 13.30 | 41.6% + 44.6% = 86.2% | 0.281 |
| amplified | 2.34 / 2.09 / 4.20 | 11.04 / 10.16 / 18.09 | **5.74 / 4.94 / 10.40** | 7.74 / 6.47 / 13.37 | 15.1% + 72.3% = 87.4% | 0.285 |

- **Against the 5 ms limit:** the overworld mean (4.53) is under it and its p95 (7.71) is over. The large_biomes mean (5.13) and the amplified mean (5.74) are slightly over 5 ms. The p50 is under 5 ms for all three. Every p95 is over.
- **Against the 2 ms target:** no preset reaches it with caves on. With caves off all three are 2.0 to 2.3 ms mean (amplified was 4.06 before the levers).
- **Across 30 workers** a column costs 0.26 to 0.29 ms, so the wall-clock cost is far lower than the one-thread figure. The limit is stated per column, so these rows are reported against it as measured.
- **Overworld split now** (caves on, mean ms): flat 0.08 to 0.09, bounds 0.27 to 0.28, corners 1.7 to 2.1, cells from corners 0.09, cells to blocks 0.6, aquifer 0.65 to 0.70, block fill 0.97 to 1.08.
- **Under streaming load** (the capture): columns generate in 7.58 ms mean while workers also mesh. In the flight it was 7.26 ms (13.8 ms in session 4).

**Game-thread budget** (flight `VoxelFlight-20261009-035142`, 10,107 frames): 3 frames over the 2 ms chunk budget, worst 2.12 ms, one per circle. Session 4's flight had 2 of 10,230 over (worst 2.35 ms); before caves it was 0. The worst apply frame was 1.76 ms.

**Start to view:** 3.34 s in the flight (3.57 s in the capture). Session 4 had 3.64 s; before caves it was 2.5 to 2.6 s; the stand-in terrain was 0.5 to 0.8 s.

**Late columns in the fog's fade:** 2,392 frames, at most 35 columns (session 4: 2,890 frames). No holes where the fog is clear.

**Memory** (laps 2 and 3): process 3,831 to 3,841 MB, column data 9.19 to 9.59 MB. Flat.

**GPU per shot** (capture `VoxelShots-20261009-034919`, ms): overview 4.23, closeup 3.66, ground 3.33, lumen-scene 4.90, section 3.63, cave 3.31, cave-lava 3.47. Phase 3's streamed world measured 3.16 to 3.95 ms on the same shots, so the real terrain costs about 0.3 to 1 ms more.

## Problems found and fixed

- **Caves made columns about 4x slower** (session 4: overworld 2.05 to 8.47 ms mean). Cave bounds were too loose to prove any cell solid, so solid skipping went from 27% to 0%. Session 5's four exact levers brought it to 4.53 ms. Cells now settle from their own corner values instead of from wide noise bounds.
- **Existing tests pinned old behaviour.** `Climate.Ops` expected `interpolated` to be refused, and `WorldsTests` and `create-world.mjs` expected generator 1. Search the tests for the old value when a decision changes it.
- **Restored files kept an old timestamp.** After a mutation check, `Copy-Item` kept the backup's timestamp, so the build skipped the files and the tests ran the mutated code. Restore with `git checkout` or touch the files.
- **An all-black cave montage.** An enclosed cave is pure black in every lighting mode. `FindCaveShots` now finds a cave run that reaches open sky, and `stats.py` measures a capture's mean level before anyone looks at it.
- **Weights that hid a swap.** The session 2 climate golden weights each parameter by its index, so swapped parameters fail it. A single sum could not.
- **The first noise weighting** (octaves halving, summed in quadrature) put vanilla ratios at 0.48 to 0.84. The per-noise scale lives in `base_amplitude` and the linear sum of weights; one global constant then fitted everything.
- **Review findings.** Session 1: seven dormant bugs and test gaps (case-sensitive matching, noise defaults, extractor counts and `..` in entry names, the verifier's missing-jar message). Session 2: eight robustness findings (cached failures, NaN in maps, the preset argument escaping `Saved/`). Session 3: a `find_top_surface` cell height of 0 or less could loop forever, and now fails at compile. Session 4: a preset without `aquifers` fails construction with caves on (all three have one). Each was fixed before its commit, except where listed under known issues.
- **Tooling.** `-ExecCmds "...,quit"` does not close `UnrealEditor-Cmd`: end the list with a short `Automation RunTests` and pass `-TestExit`. The shell's working directory drifted after `Set-Location`: pass absolute `-Project` paths. A Python heredoc turned `\n` inside `TEXT("\n")` into a real newline: use the Edit tool for C++ string literals. Running the jar's bootstrap leaves a `logs/` folder in the working directory.

## Verification

Sessions 1 to 4 from their handoffs, session 5 measured:

| Session | `TestVoxel.ps1` | Every native suite | Also |
|---|---|---|---|
| 1 | 29 | 76 | `npm test` 57/57, verifier OK, clean `-DisableAdaptiveUnity` build |
| 2 | 34/34 | 81/81 | mutation check fails `Climate.Ops` (4) and `.Presets` (3) |
| 3 | 39/39 | 86/86 | mutation check fails `Density.Ops` (3), `.Column` (3), `.Golden` (3); create-world passes; Playground probe 0 failures |
| 4 | 42/42 | 89/89 | mutation check fails `Aquifer.Fluids` (3) and `.Golden` (3); create-world passes (`Worlds/Create-1791529522498`); Playground probe 0 failures |

| 5 | 43/43 (`VoxelAutomation-20261009-040524`) | 90/90 (`AllNative-20261009-040658`) | `npm test` 57/57, `source-check.mjs` passes, verifier OK, clean `-DisableAdaptiveUnity` build, create-world passes (`Worlds/Create-1791533286134`), Playground probe 0 failures; skip on/off 0 differing blocks in all six preset runs after the review fixes |

Session 5's Sonnet 5.5 review found one bug and one risk, both fixed before the commit. The bug: an outer lazy range_choice's frontier missed what a nested choice's branch reads from outside it, so the nested scratch could read unset values; no 26.3 preset hit it. The risk: a NaN bound in the top-down pass would have read as empty.

## Measurements

- **Per-column generation:** the table above.
- **Load-order hashes** (`voxel.Bench Columns 8`, seed "Unnamed", text seed -3224043261468865476): overworld generate 4.41 ms mean on one thread (6.89 on 30 workers, wall 70.9 ms for 289 columns), mesh sections with geometry 0.295 ms mean. Hashes as in the Acceptance table. Raw: `C:\dev\sandbox\phase4s5\columns.txt`.
- **Profile before lever 4** (overworld corner stage, ms per column): `base_3d_noise` 0.86, `cave_cheese` 0.49, `cave_entrance` 0.22, `pillar` 0.14, three `interval_select` nodes about 0.09 each, about 15 single-octave noises about 0.074 each. After lever 4, amplified: `base_3d_noise` 0.99, about 35% of its corners.
- **Earlier sessions:** climate column 46 µs; density column with caves off 2.05 ms (session 3); the aquifer stage 0.65 to 0.70 ms.
- **Flight and captures:** the section "Performance against the targets".
- **Package check:** `Packages/Phase4Terrain20261009` (1.5 GB, built with `BuildFoundation.ps1 -Package`, kept until Jesus decides). The first try failed: the game target had not compiled since `ac3a6b1` (editor-only data in `FoundationBlockMeshes.cpp` and the Blocks Faces and Textures tests), now guarded with `WITH_EDITORONLY_DATA`. All 116 staged worldgen reference files are byte-identical to `Reference/minecraft-worldgen-26.3/data`. The packaged game opened `L_Generated` and streamed the dev seed's generator-2 terrain (3,745 sections, as in the editor; columns 7.78 ms mean, start to view 4.35 s): `Tests/Evidence/2026-10-09/PackageShots-040421`. It logged one handled ensure, `UVChannelData.bInitialized`, for the runtime-built Foundation block mesh (game builds only). The likely fix is to give that static material `FMeshUVChannelInfo(1.f)`; it is not verified yet.

## Known issues

- **Generation is over the 5 ms limit on two presets.** Large Biomes (5.13 ms) and AMPLIFIED (5.74 ms) mean over it, every p95 is over, and nothing reaches 2 ms with caves.
- **Remaining performance levers, best first** (all for phase 5 or later):
  - cross-column corner sharing: a column evaluates 5 x 5 corner columns, 4 x 4 unique, so up to 36% of corners repeat. It needs a cache shared across workers;
  - base 3D noise speed (35% of amplified corners), bit-exact only;
  - conditional noodle corners (thickness and ridges only where the toggle can be 0 or more, about 10% of corners);
  - an aquifer status cache across columns (0.65 ms);
  - block fill by runs or palette (1.0 ms).
- **Flight: 3 frames over 2 ms** (worst 2.12 ms). Likely the "first column always goes" apply rule with heavier cave meshes. A candidate fix is splitting a column's apply per section across frames; confirm first.
- **Start to view 3.3 s, and late columns** (2,392 frames, at most 35). Generate the inner rings first at higher priority.
- **Climate minimap** (a stretch goal) moved to phase 5, to be built with phase 5's biome debug map, which needs the same view.
- **`voxel.Debug.Climate` on screen is unconfirmed.** Offscreen captures show no on-screen messages, so no cheap check exists. For Jesus to try in game.
- **Dark caves.** The fixed daylight exposure (EV100 14.5, min = max) keeps cave interiors near black. Lava's emission is nearly invisible: at most 17/255 even at +3 EV, and glowstone has the same limit underground. Options: (a) recommended, eye adaptation between two EV bounds plus stronger emission for light-emitting blocks; (b) a minimum ambient light underground; (c) leave caves dark until torches and a light system exist. Cave-lighting captures should be re-run after this is settled.
- **Lava has no texture of its own.** It draws with an orange-tinted still water texture. Recommended: extend `Reference/minecraft-blocks-26.3` with lava through its own tools. The registry, the block reference and saves are untouched.
- **Generator 2 keeps changing** (caves now, biomes and surface in phase 5). Recommended: freeze it, and bump the generator version for any later change, once edit saving lands in phase 10 or when worlds are kept.
- **Silent fallback.** If the reference data is missing or a preset fails to build, a generator-2 world falls back to the stand-in terrain with only a log error. Recommended: a hard error for saved worlds, keeping the fallback for the dev seed.
- **Lumen card occlusion** is decided for caves (cards stay), but the near-black caves could hide artifacts.
- **Surface estimate biased low.** The PS estimate rounds down to 8-block cells and ignores the 3D noise, so 74% of the overworld area estimates below sea level. Spawn confirms against the real column.
- **Wide beaches.** Sand wherever the surface is Z 56 to 64, until phase 5's surface rules.
- **Sky and fog seam at the horizon.** The line is the same in the stand-in's flight, so it is phase 3's edge-fog color issue. Proposed fix: match the fog to the sky at the horizon.
- **Aquifer watch item.** Statuses sample the preliminary surface at each cell column's middle, not at the jittered center. Suspect it first if pockets appear under steep slopes.
- **Still open from before:** unloads released in one frame, never `Authority.Reset()` a populated world, `voxel.Fly` restore, missing block shapes, edits session-only, no fluid flow, stale generated docs, about 2.4 GB of untracked `Tests/Evidence` from before phase 3, the zips `Reference/minecraft-hud-26.3.zip` and `minecraft-hud-26.3 (2).zip`, and the untracked `logs/` folder at the project root.

## Evidence

Evidence stays untracked: `Tests/Evidence/` is not in any phase 4 commit. Best first:

1. `C:\dev\sandbox\phase4s5\terrain1.png`: montage of the three overviews and closeups (ocean, plains, hills, mountains).
2. `Tests/Evidence/2026-10-09/VoxelShots-20261009-034919/`: dev seed with `-Caves`: overview, closeup, the cave and the lava view.
3. `Tests/Evidence/2026-10-09/VoxelShots-20261009-034958/`: seed "mountains2".
4. `Tests/Evidence/2026-10-09/VoxelShots-20261009-035028/`: seed "isles".
5. `Tests/Evidence/2026-10-09/VoxelFlight-20261009-035142/`: the flight (`flight.csv`, `flight.txt`, four shots).
6. `C:\dev\sandbox\phase4s4\montage\cavecmp2.png`: the cave-lighting comparison, with `VoxelShots-20261009-025021`, `-025054`, `-025126` and `-025159` under `Tests/Evidence/2026-10-09/`.
7. `C:\dev\sandbox\phase4s5\bench-final.txt` and `columns.txt`: the final bench and `voxel.Bench Columns` output.
8. `Tests/Evidence/2026-10-09/VoxelShots-20261009-005643/`: the first generator 2 world (session 3), and `VoxelFlight-20261009-024034` (session 4, caves, before the levers).
9. `Tests/Evidence/2026-10-09/ClimateMaps-20261009-001155/` and `DensityMaps-*`: the climate and density maps.

## Next

Phase 5, biomes and surface rules. It carries the climate minimap with its biome debug map, the remaining performance levers, the cave exposure decision and the generator 2 freeze question.

Session 5 is committed after `516afdb`; its hash is in the git log.
