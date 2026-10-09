# Voxel world handoff: phase 5, into session 5 (surface rules) - October 9, 2026

Read this first if you are picking up the voxel world in a new chat. Phase 5 session 4 (biome source, storage, biome maps and minimap) is done and committed (`575dd19`). The next chat is **session 5: surface rules, temperature and the snow line** ([the phase 5 plan](2026-10-09-Voxel-Phase5-Plan.md), section 3). This handoff replaces [the session 4 handoff](2026-10-09-Voxel-Handoff-Phase5-Session4.md) as the entry point; the plan stays the reference for sessions 5 and 6. Facts were true at `575dd19`. Check `git status`, `git log -5` and newer [records](README.md) before trusting them.

## 1. Read these, in this order

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md).
2. **This handoff.**
3. **[The phase 5 plan](2026-10-09-Voxel-Phase5-Plan.md), only:** section 2 (design answers: surface rules and stone depth, cost control, altitude temperature and the snow line), section 3's "Session 5", section 4, section 6 (risks: steepness, `bandlands`), section 7 (Jesus's answers).
4. **The scout's verified facts for sessions 5 and 6:** `C:\dev\sandbox\scout-s5s6\facts-surface-tints.md` (black-box evidence JSON in `...\blackbox\`). Session 5 needs only these sections:
   - surface context and depths: 1.2 (what the jar's surface pass does to a column), 2.1 (column-context scan cases), 2.2 (`stone_depth`);
   - conditions and bounds: 1.3 (each condition's fields and exact behaviour), 2.3 to 2.5 and 2.7 (`y_above`, `water`, `noise_threshold`, `hole`, `above_preliminary_surface`, `vertical_gradient`, `biome`);
   - steep: 2.6 (with 1.3's `steep` entry);
   - bands and code-side parts: 1.4 (`bandlands`), 1.6;
   - whole-tree goldens: 2.8;
   - temperature and the snow line: section 3;
   - the freeze pass: section 4;
   - risks: 6.2.

   Section 5 (tints) is session 6's. The sandbox is disposable, so **session 5 copies the file into `Docs/Records/` (for example `2026-10-09-Voxel-Phase5-Scout-Facts.md`) when it commits**, and the black-box JSON it relies on with it or into its evidence.
5. **[Docs/VoxelWorld.md](../VoxelWorld.md), only:** "Biomes" (how a block reads its biome), the surface-rules row of "Worldgen data", and "Column density and terrain (generator 2)" where the blocks are filled.
6. **Code, only the ranges you need:**
   - `Source/UnnamedVoxel/Surface/VoxelSurfaceRules.h/.cpp` (the flat rule and condition nodes, `Validate`);
   - `Source/UnnamedVoxel/Terrain/VoxelDensityTerrain.cpp`, `Generate` (the block fill loop, after the biome fill);
   - `Source/UnnamedVoxel/Biome/VoxelBiomeStorage.h` (`GetBlock`) and `Biome/VoxelBiomeTable.h` (temperature, modifier);
   - `Source/Unnamed/Voxel/VoxelSurfaceReader.cpp` only if the reader needs a change.

No lighting, exposure, material or debug-view code is needed for session 5.

## 2. Working agreement with this user

| Topic | Agreement |
|---|---|
| Manager | **Hemera**, the day shift manager, in its afternoon chat "Hemera day shift manager (afternoon)" (remote session `session_01GiEPTqjUjZNt72BsnZNZRJ`; Jesus approved the switch at 12:29, and the morning chat no longer replies); at night Nyx, if one is running. Send it a short plan (files and why) and wait for the go-ahead. Report with `send_message` at two points. **A, before committing:** `git diff --cached --name-status`, suite counts from runs after the review's fixes, where each changed pinned count comes from, what changed in the `SHA256SUMS` files, and evidence paths best first with one montage. **B, after committing:** the hashes, the handoff path, the short prompt in full, and token figures (lead peak and each subagent). Hemera may approve the local commit after a clean Sonnet 5.5 review and green suites. **Hemera pushes** after it verifies; never push yourself. Changes to project rules or saved data, and anything destructive, wait for Jesus. If Jesus writes to you directly, his instructions win. |
| Look calls | Jesus's. On October 9 (10:42) he delegated that day's look calls to Hemera, and he can overrule them at a check-in. Send panels with `stats.py` numbers and GPU ms, plus a recommendation, and keep the other option one switch away. |
| Context budget | Keep the lead's context fairly short, but a longer chat beats a handoff when it saves re-reading. **Checkpoints** at about 300k, then every further 200k (500k, 700k): either start the wrap-up or send Hemera a short case for continuing (what's left, why it fits, what a new chat would re-read). Past 500k, continue only if a handoff would clearly cost more than it saves. **Hard stop:** start the wrap-up by about 850k. |
| Steps | Start with a short plan (files and why), confirm it, then build. End with measured timings, known issues and what is next. |
| Questions | Ask before anything that changes project rules or saved data. Otherwise state assumptions and continue. For Jesus's own calls (a look, a gameplay rule), give 2 to 4 options with a recommendation. |
| Commits | A Sonnet 5.5 pre-commit review of the staged diff. After its fixes, re-run every suite they touch; before committing, all of: the suites, the `-DisableAdaptiveUnity` build, `create-world.mjs` and the Playground probe (runtime changes). Message in a file (`git commit -F`), ending `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Branch `feature/voxel-world`. Stage with `git add -A -- . ':!Tests/Evidence' ':!logs'`: evidence and `logs/` stay untracked. |
| Subagents | 5.5 only: Sonnet 5.5 for the review, tests from a facts file and docs; Haiku 5.5 for log scraping. Give exact paths and line ranges, forbid whole-file reads of big files, and tell each agent to stop near 150k context. Give the review agent the diff as one file and a diff-only rule. Never run builds or editor runs in parallel; agents that only read or write files may run during a build. |
| Pictures | List evidence paths, best first. Mean levels, diffs and 16:9 montages with captions: `C:\dev\sandbox\phase5s3b\imgtools.py stats|diff|montage` (run with `uv run --no-project --with pillow python -I`). Biome map montages: `C:\dev\sandbox\phase5s4\biomemontage.py <dir> <out> <blocks per px> <title>`. |
| Machine | Jesus plays Minecraft (`javaw`) on this PC; on October 9 it was open from 10:44 through session 4. Leave it alone, mark absolute GPU and timing figures as measured with it open, and compare only inside one run or one back-to-back sequence. |
| Looks | The world's edge reads as distant haze, never a dark void or a hard horizon. |

**Settled decisions** (plan section 7 and earlier; treat as decided):
- **Fidelity:** 26.3's data from the jar, evaluated by our own code and noise. Facts files are added only through the reference's tools, with existing files byte-identical.
- **Generator 2** keeps changing until edit saving lands in phase 10, then freezes. Goldens are re-recorded only under a decided change, never to make a test pass. Session 5's surface rules change every generator-2 block: re-record `Density.Golden` and `Aquifer.Golden` once, under that decision, after the gates pass.
- **Biomes:** the climate-to-biome mapping is checked exactly against the jar's facts (done in session 4); positions only statistically.
- **Snow blocks:** powder snow and snow layers are in the registry (session 2). Snow layers have a k/8 shape; powder snow is solid for now.
- **Ore veins:** read in phase 5, switched on in phase 6 behind a switch.
- **Biome colours:** water plus grass and foliage tints per vertex, in phase 5 (session 6).
- **Cave exposure, emission and lighting** (session 3b; look calls, do not reopen): eye adaptation between EV100 6 and 12.5 with compensation -1 and middle-grey bias +2, chunk emission 8000, Lumen cards (the switch is `r.Lumen.HardwareRayTracing.LightingMode 1`).
- **Session 5's ground rules (Hemera's 12:01 requests, signed 11:53; under Jesus's delegation; reversible):**
  - Eroded-badlands hoodoos and frozen-ocean icebergs are generated in code, not in the data: deferred, a known gap, not phase 5.
  - Steep slopes follow vanilla's chunk-local, two-direction rule, for exact parity and no cross-column reads.
  - Climate noises (the snow-line wobble, frozen patches, swamp grass) stay seedless, as in vanilla.
  - The surface rules, the freeze pass and the tints all read the per-block, jittered biome: `FVoxelBiomeStorage::GetBlock(LocalX, LocalY, Z)`, also reachable as `FVoxelColumn::GetBiome(X, Y, Z)`.
  - Session 5 may pin climate samples (`facts/climate_samples.json`) through `tools/WorldgenFacts.java` and `build_reference.py`, with existing files byte-identical.
- **Session 4's calls (Hemera, 11:27):** biome storage keeps the 5 x 5 quart lattice per layer (no neighbour reads); the map palette lives in `UVoxelWorldSettings::BiomeMapColors` (`Config/DefaultGame.ini`), Amidst-style; maps and the minimap read biomes at depth 0 with an optional fixed Y; Single Biome wiring is session 5's.
- **Exact performance work only:** goldens byte-identical, skip on/off at 0 differing blocks, load-order hashes equal.

## 3. Where things stand

| Phase | Scope | Status | Commit |
|---|---|---|---|
| 0 to 4 | Scaffolding to terrain | Done | `b67b801` to `b4ad098` |
| 5 plan | Data check, design, questions | Done; answers recorded | `7cae8b1`, `406d2dd` |
| 5 session 2 | Reference data, blocks, readers | Done | `eaa6ef3` |
| 5 session 3 | Performance levers | Done | `f10f5b6` |
| 5 session 3b | Cave brightness, cave-lighting comparison | Done | `5524d5d` |
| 5 session 4 | Biome source, storage, biome maps, minimap | Done | `575dd19` |
| 5 session 5 | Surface rules, temperature, snow line, Single Biome | Next | |
| 5 session 6 | Ocean variants, tints, captures, performance, record | Then | |

**What session 4 built** (details in [Voxel world](../VoxelWorld.md), "Biomes"):
- **Lookup (`FVoxelBiomeSource`):** the overworld list's 7,594 points fall into 4 depth groups (0, 1.0, 0.2 to 0.9, 1.1). Per group, a bounding-box tree over the other five parameters finds the best point exactly (ties to the lowest list index); a target's answer is the best group winner plus its depth term. Per column: 25 quart columns x 4 group searches, then a sweep of 96 depths over the winners. Exact by construction; brute force (`FindBruteForce`) is the reference.
- **Storage (`FVoxelBiomeStorage`):** one byte per quart over the column's 5 x 5 x 96 lattice (2,400 bytes; the next column's first row and column included, so no neighbour reads). `FVoxelColumn::GetBiome(x, y, z)` is the jittered block lookup: the nearest of the cell's 8 corner sites, each moved up to 0.45 quart by `Hash64(seed, "biome/jitter", quart)`.
- **Column job:** `FVoxelDensityTerrain` fills it after the aquifer, before the blocks. `VoxelTerrainFactory` reads the overworld list for every generator-2 type; an unreadable list refuses a saved world (test in `Worlds.TerrainCheck`).
- **Fixed source:** `InitializeFixed(table, biome)` for Single Biome; not wired yet.
- **Debug:** `voxel.BiomeMap [size] [blocks/px] [preset] [surface|<y>]` (PNG plus CSV of shares), `voxel.Debug.Biomes 1` (stored biome at the player, climate targets, pick and fitness), `voxel.Debug.Minimap <biomes|C|E|W|PV|T|V|O|F|J|off>` with `voxel.Debug.MinimapScale` (256 px, north up, the world's own terrain). Off, nothing is registered. Voxel shots keep the UI while a biome view is on.
- **Orientation fix:** Y points south, so maps are now drawn north up (-Y at the top). `voxel.ClimateMap` was drawn south up before; old climate-map evidence is flipped relative to new maps.

**Figures** (Development editor, seed "Unnamed"; Minecraft 26.3 was open throughout, so absolute times run slow; compare within one run):

| What | Result |
|---|---|
| Biome stage per column (`voxel.Bench Density 4`, 81 columns) | mean 0.19 ms (p50 0.14 to 0.17, p95 0.27 to 0.45) across the three presets |
| Total column, biomes on / off, same run | overworld 4.09 / 4.31, large_biomes 4.65 / 4.19, amplified 4.92 / 4.22 ms (the totals' run-to-run spread exceeds the 0.19 ms stage) |
| Pruned vs brute force (9 columns per preset) | 0 of 21,600 quarts differ; brute force 172 to 181 ms per column |
| Biomes on vs off, blocks | 0 of 7,962,624 differ |
| Radius-8 block hashes | unchanged (`0x7bbf1f42c5434443`, `0xc70b24ba700501f2`, `0x27c2d49086a4614a`) |
| Radius-8 biome hashes, both load orders | overworld `0x787dbeb131ce9f63`, large_biomes `0x350119336bfbc720`, amplified `0x6a5593310ccd357d`; 0 columns differ |
| Column memory (capture) | 19.2 KB, against 16.8 KB before |
| Biome maps | 0.25 us per point; 1024 x 1024 in about 0.26 s |
| Maps at the surface, 8,192 blocks | overworld and AMPLIFIED 51 of 56 biomes (identical at depth 0 by construction), Large Biomes 28; at 32,768: 53 / 52 |

**Verification** (every run after the review's fixes):

| Check | Result | Evidence |
|---|---|---|
| `TestVoxel.ps1` | 52/52 | `Tests/Evidence/2026-10-09/VoxelAutomation-20261009-122013` |
| Every native suite | 100/100; one passed with a warning, a `LogHttp` timeout to `google.com/generate_204` during `Density.Terrain` (network; absent in 3b's run) | `AllNative-20261009-122056` |
| `-DisableAdaptiveUnity` build | OK | |
| `create-world.mjs --editor` | PASS | `Worlds/Create-1791562963032` |
| Playground probe | 0 failures (43.67 FPS at a 60 cap, with Minecraft open; 3b: 43.35) | `Playground-all/Runtime-20261009-122900` |
| `npm test`, `source-check.mjs` | 57/57, PASS | |
| Bench gates | pruned = brute force (0 of 21,600 quarts), biomes on/off 0 blocks, block hashes unchanged, biome load orders equal | `C:\dev\sandbox\phase5s4\logs\` |
| Mutations | ties to the last point fail `Biome.Lookup` (173 samples); another jitter salt fails `Biome.Golden`'s 3 block hashes | |
| Review | Sonnet 5.5 on the whole diff: no high or medium; it traced the branch and bound as exact. Lows fixed: the minimap's terrain check (a weak pointer, not a raw one), a comment, docs wording, `CLAUDE.md`'s test count, unregistering at engine exit. Nits fixed: test messages, case-sensitive preset names, struct placement, includes. Left: failed debug sources stay cached; one overlay for all viewports. | |

A stray re-launch of `create-world.mjs` was stopped within a minute and left a partial folder, `Worlds/Create-1791563452904`; ignore it.

Evidence, best first:
- `Tests/Evidence/2026-10-09/BiomeMaps-20261009-north/montage-8192.png`, `montage-32768.png` (three presets, legend with shares; CSVs beside each map);
- `VoxelShots-20261009-120938/montage-minimap.png` and `overview.png` (minimap and readout on), `ground.png` (stored biome jungle at spawn);
- `VoxelShots-20261009-121015` (overlays off; equal to 3b's `105556` within noise: means within 0.1, no pixel off by more than 24/255);
- `C:\dev\sandbox\phase5s4\logs\density1.txt`, `columns8.txt` (bench output).

## 4. Session 5: what to do

1. Plan to Hemera (files and why; the plan's session 5 table is the starting point), wait for the go-ahead.
2. The surface rule evaluator (`Surface/VoxelSurfaceEvaluator.cpp` beside `VoxelSurfaceRules.cpp`, plus `VoxelSurfaceContext.h`): the column context pass, the conditions, the vertical gradient with `Hash64(seed, random_name, x, y, z)`, our own badlands bands from `clay_bands_offset`, and the deep-stone shortcut.
3. `Terrain/VoxelDensityTerrain.*`: replace the grass, sand and gravel dressing and the bedrock and deepslate stand-ins with the rules; add the freeze pass. The `biome` condition reads `FVoxelColumn`'s storage through `FVoxelBiomeStorage::GetBlock` (already filled before the block loop).
4. `Biome/VoxelBiomeClimate.h`: per-block temperature (the biome's temperature, the frozen modifier, minus 0.00125 per block above Y 80, our own wiggle) and the snow line.
5. Single Biome: build the fixed source from world.json's `biome` (`InitializeFixed`). A non-overworld choice is a question for Jesus if it changes saved data (section 5).
6. Tests: `Surface.Conditions`, `.Exact` (shortcut on/off), `.Snow`; re-record `Density.Golden` and `Aquifer.Golden` once under the decided change.
7. Review, suites, commit (Hemera approves), handoff into session 6.

## 5. Notes for sessions 5 and 6

- **Reading a block's biome:** in the column job the storage is moved into the output column just before the block loop, so the loop can call `Out.GetBiomes().GetBlock(LocalX, LocalY, Z)` (or `Out.GetBiome(...)`). Each call hashes 8 corner sites: cheap for the surface blocks, but do not call it for every stone block; the deep-stone shortcut (plan section 2) avoids that.
- **Surface rules (session 5):**
  - `FVoxelSurfaceRules` keeps ids as strings (blocks, all default states; biomes; noises; random names; ore vein density functions). The evaluator resolves them when it compiles. Biome ids resolve against the source's `FVoxelBiomeTable` (`FindBiome`).
  - The conditions read 9 noises: `surface`, `surface_swamp`, `calcite`, `gravel`, `ice`, `packed_ice`, `powder_snow`, `small_patch`, `sulfur_cave_gradient`. `surface_secondary` and `clay_bands_offset` are read by code-side parts (surface depth, bands).
  - All 32 blocks the rules place exist.
  - Steepness reads neighbouring surface heights: get them without depending on load order (plan section 6) and prove it with the load-order hashes, which start cold in each order.
- **Single Biome:** `world.json`'s `biome` may name any biome the Create screen offers, but the reference holds only the 56 overworld biomes. Decide what a non-overworld choice does (refuse at Create, or plains); a question for Jesus if it changes saved data.
- **Top session 6 performance candidate: the normal noise.** It is 57 to 64% of corner time (overworld 1.03 of 1.62 ms per column before sharing; large_biomes 1.33 of 2.07; amplified 1.34 of 2.34), at 93 to 109 ns per point and node. Two exact steps:
  - batch `FVoxelNormalNoise` like `old_blended_noise` (`AccumulateOctave` exists);
  - hoist the x and z work shared by a corner column's 49 corners (each octave's floor, fraction, fade and hash partial products for x and z). That needs the corner batch grouped by corner column: today `CornerPoints` is in y-major index order.
- **Biome stage levers, if needed (session 6):** its climate pass (about 0.05 ms of the 0.19) could share the density's corner-column flat values (the quart columns are the corner columns) or neighbouring columns' ring values. Not needed so far.
- **Measure streaming with sharing** in session 6: `CaptureVoxel -Flight` (columns' mean generate time while workers mesh; session 5 of phase 4: 7.26 ms).
- **Captures after biomes:** exposure adapts. Compare outdoor shots against `VoxelShots-20261009-121015` (session 4, overlays off), not older ones.
- **Acceptance maps (session 6):** re-run `voxel.BiomeMap` at 8,192 and 32,768 blocks after the surface rules, and the montage script; the jar's sample shares are climate-space shares, not area (say so in the record).

## 6. Known issues, each with when it is due

| Issue | Due |
|---|---|
| Lava in a cave clips: 36% of the `cave-lava` view has a channel at 255. Knob: `CHUNK_LIGHT_EMISSIVE` (5000 estimated at about 0.7 stop less) | If a later look asks |
| Columns under 5 ms only with sharing at best case; surface rules add more on top of the biome stage's 0.19 ms | Session 6 (normal-noise lever, section 5) |
| The mesher (`VoxelMesher.cpp`, fluid case) and `AVoxelWorld::GetWaterBox` treat water against lava as one fluid; aquifer walls keep them apart today | With flowing lava |
| A saved world opened directly (`L_Generated?VoxelWorld=<id>`) with unbuildable terrain returns to the title with the reason only in the log | When the menu gets a notice channel |
| Lava counts as water for the player's movement; lava's sides show `lava_still` | With lava damage and flow |
| Snow layers: one layer per placement, no stacking; eight layers collide as a full cube (26.3: 7/8); the creative icon draws a cube | A later blocks pass |
| Powder snow: solid, no sinking, no item | With buckets and movement rules |
| `facts/biome_parameters.json` is staged for packages, but no package was built since; the package ensure `UVChannelData.bInitialized` is still open | Next package check (session 6) |
| `voxel.Debug.Climate C` in game is still untried (Jesus); the biome readout and minimap are confirmed in shots | Next playtest |
| A `LogHttp` timeout warning (`google.com/generate_204`) appeared once during `Density.Terrain` in the all-native run; unrelated to the voxel code | Watch |
| `Docs/MinecraftBlockReference.md` was hand-edited after it was generated: patch its counts by hand, never regenerate over it | When blocks change |

**Disposable, for the record:** `C:\dev\sandbox\phase5s4\` (patch scripts, logs, mutation backups, crops, `biomemontage.py`), `C:\dev\sandbox\phase5s3b\`, `phase5s3\`; `Tests/Evidence/2026-10-09/BiomeMaps-20261009-1148/` holds the first maps, drawn south up before the orientation fix.

## 7. Code map: session 4 changes

| What | Where | Notes |
|---|---|---|
| Biome lookup | `Source/UnnamedVoxel/Biome/VoxelBiomeSource.h/.cpp` | `Find`, `FindBruteForce`, `FillColumn(EVoxelBiomeFill)`, `SampleTargets`, `GetDepth`, `InitializeFixed`, `GetClimate` |
| Biome storage | `Source/UnnamedVoxel/Biome/VoxelBiomeStorage.h/.cpp` | 5 x 5 x 96 bytes; `GetBlock`, `GetBlockQuart`, `GetJitter` |
| Column | `Storage/VoxelColumn.h/.cpp` | `GetBiomes`, `SetBiomes`, `GetBiome`; counted in `GetAllocatedSize` |
| Terrain | `Terrain/VoxelDensityTerrain.h/.cpp`, `Terrain/VoxelTerrain.h` | biome table in the constructor, the fill before the blocks, `BiomesMs`, `IVoxelTerrain::GetBiomeSource` |
| Bench | `VoxelBench.h/.cpp` | biome stage, biomes-off pass, `CompareBiomeFills`, biome load-order hashes |
| Factory and console | `Unnamed/Voxel/VoxelTerrainFactory.*`, `VoxelConsole.cpp` | read the overworld list |
| Debug views | `Unnamed/Voxel/VoxelBiomeDebug.h/.cpp`, `VoxelClimateDebug.*` (`GetColor`, `WritePng`, north up), `VoxelWorld.*` (`GetTerrain`, `GetBiome`, shots keep UI with a biome view on), `VoxelWorldSettings.h` + `Config/DefaultGame.ini` (`BiomeMapColors`) | |
| Tests | `Unnamed/Tests/VoxelBiomeSourceTests.cpp`, `WorldsTests.cpp`; `Scripts/TestVoxel.ps1` expects 52 | |

## 8. What worked and what failed in session 4

**Worked; repeat it:**
- Looking at the data before designing: the 4 depth groups made an exact lookup cheap (0.19 ms instead of an estimated 0.1 to 0.3 for an approximate one).
- The gate before the golden: pruned against brute force (0 of 21,600) and the bench's biomes-on/off block comparison first, `Biome.Golden` recorded last.
- Mutation checks found a real gap: the quart golden did not depend on the jitter (only block lookups do), so a jitter-salt change passed every test. A block-level golden now pins it.
- Reading the docs' coordinate section caught that the maps were south up (Y points south) while their comments said north.
- Patch scripts written with the Write tool and run with `python -I`.

**Failed first; avoid it:**
- Backslash escapes (`\\n`) inside a Bash heredoc reached Python as real newlines and a pattern did not match: write patch scripts to files.
- A heredoc to `uv run python -` hung waiting on stdin; stop such a background task with TaskStop.
- clang-format re-wraps lines, so a patch written against pre-format text fails: grep the current text first. A failed multi-file patch script may have written the files before the failing one.
- The lead's context was not tracked: the 300k checkpoint passed unnoticed during the final suites (peak 432k), and Hemera got no case for continuing. The wrap-up was already under way, but check the budget counter after every build-and-run step.
- The review agent read the whole 2,817-line diff and peaked at 170k, over its 150k stop. For a diff this size, split it by area across two reviewers, or point one at the core files only.
- `node create-world.mjs --help` starts the full end-to-end run (the script has no help flag); never call it to inspect it.

**Still true:** unity builds need module-unique names; no shadowed locals; `-DisableAdaptiveUnity` before committing; `-ExecCmds "...,quit"` does not close `UnrealEditor-Cmd`; launch `UnrealEditor-Cmd` from PowerShell, not the Bash tool; the repository is LF (`Config/DefaultGame.ini` is CRLF; keep it so); `-script=` needs an absolute path.

## 9. Commands and tokens

```powershell
& C:\dev\sandbox\phase5s2\build.ps1 [-NoUnity]                          # build, short output
& C:\dev\projects\Unnamed\Scripts\TestVoxel.ps1                         # 52 Unnamed.Voxel tests
& C:\Users\jherediaramirez\AppData\Local\Temp\unnamed_scratch\RunAllNative.ps1   # every native suite (100)
& C:\dev\sandbox\phase5s2\bench.ps1 -Cmds "<cmds>" -Out <file>          # editor run, LogUnnamedVoxel lines only
& C:\dev\sandbox\phase5s3\runtest.ps1 -Filter <test> -Name <label>      # one automation test into the sandbox, errors printed
& Scripts\CaptureVoxel.ps1 [-Caves] [-ExecCmds 'voxel.Debug.Minimap biomes, voxel.Debug.Biomes 1']
voxel.BiomeMap 1024 8 overworld; voxel.Bench Density 4 overworld; voxel.Bench Columns 8 overworld
Set-Location Tests; node create-world.mjs --editor; Set-Location ..; & Scripts\TestFoundationRuntime.ps1 -Editor -FrameRates 60
```

Tokens: copy `<session>.jsonl` and `<session>/subagents/` from `~/.claude/projects/C--dev/` into `<temp>/projects/C--dev-projects-Unnamed/`, then run `node Scripts\MeasureAgentUsage.mjs --session <id>` with `CLAUDE_CONFIG_DIR=<temp>`.

**Session 4's cost,** session `c094261a-ea9c-5790-899a-f742008d7af7`, measured after the code commit, before this handoff:

| Agent | Peak context | Processed | Output |
|---|---|---|---|
| Lead (Opus 5.5) | 432k | 48.3M (47.7M cache reads) | 219k |
| Pre-commit review (Sonnet 5.5) | 170k (asked to stop near 150k) | 1.59M | 31k |
