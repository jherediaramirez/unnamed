# Voxel world handoff: phase 5, into session 6 (tints, ocean variants, captures, performance, record) - October 9, 2026

Read this first if you are picking up the voxel world in a new chat. Phase 5 session 5 (surface rules, temperature, the snow line and Single Biome) is done and committed (`c073950`). The next chat is **session 6: ocean variants, tints, captures, performance and the phase 5 record** ([the phase 5 plan](2026-10-09-Voxel-Phase5-Plan.md), section 3). This handoff replaces [the session 5 handoff](2026-10-09-Voxel-Handoff-Phase5-Session5.md) as the entry point. Facts were true at `c073950`. Check `git status`, `git log -5` and newer [records](README.md) before trusting them.

## 1. Read these, in this order

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md).
2. **This handoff.**
3. **[The phase 5 plan](2026-10-09-Voxel-Phase5-Plan.md), only:** section 3's "Session 6", section 4 (acceptance captures and figures), section 7's answer 5 (biome colours).
4. **[The scout's facts](2026-10-09-Voxel-Phase5-Scout-Facts.md), only:** section 5 (water colour, grass and foliage tints: where each value lives, the colormap lookup, per-biome results) and 6.1 (gaps G1, G4, G5). Evidence: [2026-10-09-Voxel-Phase5-Scout-Evidence/](2026-10-09-Voxel-Phase5-Scout-Evidence/) (`blackbox/colors.json`, `biome_climate_colours.json`).
5. **[Docs/VoxelWorld.md](../VoxelWorld.md), only:** "Biomes", "Surface rules, temperature and snow" (its table is the timing baseline), and "Storage and meshing" where the mesher writes vertices.
6. **Code, only the ranges you need:**
   - the mesher's vertex writes and the fluid case (`Source/UnnamedVoxel/Meshing/VoxelMesher.cpp`, `VoxelMeshData.h`) for a per-vertex tint;
   - `Biome/VoxelBiomeTable.h` (`FVoxelBiome`: `WaterColor`, `GrassColor`, `FoliageColor`, `GrassColorModifier`), `Biome/VoxelBiomeClimate.h` (`GetInfoNoise` for swamp grass), `Biome/VoxelBiomeStorage.h` (`GetBlock`);
   - `Terrain/VoxelDensityTerrain.cpp` `FillWithRules` and `Surface/VoxelSurfaceEvaluator.cpp` `ApplyColumn` only for the performance lever (section 5).

## 2. Working agreement with this user

| Topic | Agreement |
|---|---|
| Manager | **Hemera**, the day shift manager (remote session `session_01GiEPTqjUjZNt72BsnZNZRJ`, "Hemera day shift manager (afternoon)"); at night Nyx, if one is running. Send it a short plan (files and why) and wait for the go-ahead. Report with `send_message` at two points. **A, before committing:** `git diff --cached --name-status`, suite counts from runs after the review's fixes, where each changed pinned count comes from, what changed in the `SHA256SUMS` files, evidence paths best first with one montage. **B, after committing:** the hashes, the handoff path, the short prompt in full, token figures (lead peak and each subagent). Hemera may approve the local commit after a clean Sonnet 5.5 review and green suites; **Hemera pushes**, never push yourself. Changes to project rules or saved data, and anything destructive, wait for Jesus. If Jesus writes to you directly, his instructions win. If a turn ends without a report to Hemera, say so in the next reply. |
| Look calls | Jesus's; on October 9 he delegated that day's look calls to Hemera, and he can overrule them. Send panels with `stats.py` numbers and GPU ms, plus a recommendation; keep the other option one switch away. |
| Context budget | Checkpoints at about 300k, then every 200k (500k, 700k): start the wrap-up or send Hemera a short case for continuing. Past 500k, continue only if a handoff clearly costs more. Start the wrap-up by about 850k. Note the lead context in every report. |
| Steps | Short plan, confirm, build. End with measured timings, known issues and what is next. |
| Subagents | 5.5 only: Sonnet 5.5 for reviews, tests from a facts file and docs; Haiku 5.5 for log scraping. Exact paths and line ranges, no whole-file reads of big files, stop near 150k. A diff over about 1,500 lines goes to two or three reviewers by path list, diff only (session 5: three reviewers peaked near 75k each). Never run builds or editor runs in parallel; a subagent may build while the lead only edits non-built files. |
| Commits | A Sonnet 5.5 pre-commit review of the staged diff; after its fixes re-run every suite they touch; before committing: the suites, the `-DisableAdaptiveUnity` build, `create-world.mjs` and the Playground probe (runtime changes). Message in a file (`git commit -F`), ending `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Branch `feature/voxel-world`. Stage with `git add -A -- . ':!Tests/Evidence' ':!logs'`. |
| Pictures | Evidence paths best first. Means, diffs and 16:9 montages: `C:\dev\sandbox\phase5s3b\imgtools.py stats|diff|montage` (`uv run --no-project --with pillow python -I`). Biome map montages: `C:\dev\sandbox\phase5s4\biomemontage.py`. |
| Machine | Jesus plays Minecraft (`javaw`) on this PC; on October 9 it was open from 10:44 through session 5. **Session 6 benchmarks on a quiet machine: Hemera will ask Jesus to close Minecraft first.** Otherwise mark absolute figures as measured with it open and compare only inside one run. |
| Looks | The world's edge reads as distant haze, never a dark void or a hard horizon. |

**Settled decisions** (plan section 7 and earlier; treat as decided):
- **Fidelity:** 26.3's data from the jar, evaluated by our own code and noise. Facts files are added only through the reference's tools, with existing files byte-identical.
- **Generator 2** keeps changing until edit saving lands in phase 10, then freezes. Goldens are re-recorded only under a decided change, never to make a test pass. Session 6's tints change no block, so no golden should move; mesh-level goldens, if any, only under a decided change.
- **Biome colours:** water plus grass and foliage tints per vertex, in phase 5 (session 6).
- **Session 5's ground rules** (Hemera, under Jesus's delegation): hoodoos and icebergs deferred (a known gap); steep follows vanilla's chunk-local two-direction rule; climate noises stay seedless; the rules, the freeze pass and the tints all read the per-block jittered biome (`FVoxelBiomeStorage::GetBlock`).
- **Session 5's look call (Hemera, 15:14):** keep the surface rules on, with our 192-block band pattern and the snow-line wobble as built. `bSurfaceRules = false` stays as the switch (`CaptureVoxel -NoSurfaceRules`, dev seed only).
- **Single Biome refusal:** a non-overworld id refuses the world (built as the default, no saved-data change). Hemera passed it to Jesus as his gameplay call; he can switch it to a plains fallback. Check for his answer.
- **Session 5's timing decision (Hemera):** accepted for session 5. For session 6, see section 5 below: quiet-machine benchmarks, one timeboxed exact lever, then report and stop if any preset stays over 5 ms.
- **Cave exposure, emission and lighting** (session 3b; do not reopen). **Exact performance work only:** goldens byte-identical, skip on/off at 0 differing blocks, load-order hashes equal.

## 3. Where things stand

| Phase | Scope | Status | Commit |
|---|---|---|---|
| 0 to 4 | Scaffolding to terrain | Done | `b67b801` to `b4ad098` |
| 5 plan | Data check, design, questions | Done | `7cae8b1`, `406d2dd` |
| 5 session 2 | Reference data, blocks, readers | Done | `eaa6ef3` |
| 5 session 3 | Performance levers | Done | `f10f5b6` |
| 5 session 3b | Cave brightness, cave-lighting comparison | Done | `5524d5d` |
| 5 session 4 | Biome source, storage, maps, minimap | Done | `575dd19` |
| 5 session 5 | Surface rules, temperature, snow line, Single Biome | Done | `c073950` |
| 5 session 6 | Ocean variants, tints, captures, performance, record | Next | |

**What session 5 built** (details: [Voxel world](../VoxelWorld.md), "Surface rules, temperature and snow"):
- **Evaluator** (`Surface/VoxelSurfaceEvaluator.*`, `Surface/VoxelSurfaceContext.h`): `material_rule/overworld` compiled into a 261-instruction linear program; per-column context (surface depth with our hash jitter, the preliminary surface from the column's 4 corners lerped, chunk-local steep, depth above/below, water height incl. lava); our own vertical gradients and 192-block badlands bands. Ore veins give nothing (phase 6).
- **Exact shortcut** (`bSurfaceShortcut`): deep table with resume points below the preliminary surface, interior table (backwards partial evaluation) for blocks deeper than every reachable stone_depth condition, layer biome masks, cached corner jitters, and a lower segment that visits only the deep table's active heights.
- **Climate and freeze** (`Biome/VoxelBiomeClimate.*`, `Surface/VoxelFreezePass.*`): temperature with the frozen modifier and the snow line above Y 80 (seedless wobble); ice on still water, one snow layer on cold tops, snowy grass/podzol/mycelium (`FVoxelBlockTable::GetSnowyState`).
- **Wiring:** `VoxelTerrainFactory::ReadInputs` (preset, overworld list, the preset's `material_rule` and its 11 noises) for the factory, console benches and tests; a saved world whose rules cannot be read is refused. Single Biome: `FVoxelDensityTerrainSettings::FixedBiome`.
- **Bench:** passes for rules off, shortcut off, biomes off with rules off; surface stage, preliminary and freeze times; blocks per column by path.
- **Captures:** `CaptureVoxel -SpawnAt X,Y` (spawn search centre) and `-NoSurfaceRules` (dev seed only).

**Figures** (Development editor, seed "Unnamed", Minecraft open; same run):

| What | overworld | large_biomes | amplified |
|---|---|---|---|
| Whole column, rules on / off, mean ms (`voxel.Bench Density 4`) | 5.16 / 4.41 | 5.20 / 4.66 | 6.33 / 4.86 |
| Surface stage mean / p50 / p95 | 0.76 / 0.61 / 1.70 | 0.82 / 0.63 / 1.70 | 1.04 / 0.89 / 1.97 |
| Shortcut off, stage mean | 7.35 | 9.21 | 7.77 |
| Blocks per column: deep / interior / whole tree | 14,525 / 3,730 / 3,780 | 14,503 / 4,808 / 3,736 | 14,063 / 15,020 / 3,922 |
| Radius-8 block hashes, both load orders | `0xeb1c3d76838e2481` | `0xcb42685280f8b57c` | `0xc4403ff66465fcf0` |

- Gates (radius 4): 0 of 7,962,624 blocks differ with the shortcut, biomes (rules off), sharing and skipping on and off, all three presets. Biome hashes unchanged from session 4. Across 30 workers: 0.27 / 0.27 / 0.29 ms per column.
- Goldens re-recorded once: `Density.Golden` `0xf7ba992062080de1`, `0x9cdb8dff3e065521`, `0xab4971c1b4ee6520`; `Aquifer.Golden` `0x22523b7400c2f38e`, `0x6b19b1927b73266a`, `0x863f9b8b01468a3f`.
- Mutations caught: snow at <= 0.15 fails `Surface.Snow`; steep's directions swapped fail `Surface.Conditions` (6 cases).

**Verification** (every run after the review's fixes): 

| Check | Result | Evidence |
|---|---|---|
| `TestVoxel.ps1` | 56/56 | `Tests/Evidence/2026-10-09/VoxelAutomation-20261009-152547` |
| Every native suite | 104/104; `Density.Terrain` passed with the known `LogHttp` timeout to `google.com/generate_204` (network, as in session 4) | `AllNative-20261009-152627` |
| `-DisableAdaptiveUnity` build | OK | |
| `create-world.mjs --editor` | PASS | `Worlds/Create-1791574040193` |
| Playground probe | 0 failures, 43.92 FPS at a 60 cap (Minecraft open) | `Playground-all/Runtime-20261009-153338` |
| `npm test`, `source-check.mjs`, `verify_reference.py` | 57/57, PASS, reference unchanged | |
| Review | Three Sonnet 5.5 reviewers by path. Core: no high or medium (traced the shortcut as exact); lows fixed (secondary depth for any nonzero range, a check on ApplyColumn's Top hint). Wiring: one medium fixed (`-SpawnAt` unquoted became "X Y": now `[int[]]`, the game warns on a bad value), docs and nits fixed. Tests: no high; fixed: the shortcut runs proven to take different paths, bedrock and deepslate checks, literal 0.2, every noise injected. | |

Evidence, best first:
- `Tests/Evidence/2026-10-09/SurfaceScenes-20261009-1508/montage-desert-badlands.png`, `montage-snowy-peak.png`, `montage-frozen-peaks.png`, `montage-swamp.png` (rules on left, old dressing right; raw shots in `<scene>-on|off/`);
- `C:\dev\sandbox\phase5s5\logs\gates-r4-r8.txt` (bench output: gates, stage times, hashes);
- **superseded, keep in place:** `SurfaceScenes-20261009-1503` (all 8 captures landed near the origin: `FParse::Value` stopped at the comma of `-VoxelSpawnAt`, fixed).

## 4. Session 6: what to do

1. Plan to Hemera (files and why), wait for the go-ahead.
2. **Tints** (plan answer 5; facts section 5): per-biome water colour, grass and foliage from the colormaps (with the dark-forest and swamp modifiers; swamp grass reads `FVoxelBiomeClimate::GetInfoNoise`), as a per-vertex tint in the mesher. Read the biome per block with `GetBlock` (the same answer the rules used, so no seams).
3. **Captures:** `FindBiomeShots` in code (as `FindCaveShots`), or `CaptureVoxel -SpawnAt` with places from a biome map: a snowy peak above the line, each ocean variant (warm, lukewarm, cold, frozen; pick frozen_ocean where the modifier keeps 0.0, since deep frozen ocean never freezes), a desert to badlands edge, a swamp or mangrove **with the ground shot inside the swamp** (session 5's landed on plain grass), and a flight. One montage at a time, `stats.py` first.
4. **Acceptance maps:** `voxel.BiomeMap` at 8,192 and 32,768 blocks for the three presets, and the montage script; the jar's sample shares are climate-space shares, not area.
5. **Performance** (Hemera's 15:15 conditions): benchmark on a quiet machine; run the per-run memo (section 5) as the one timeboxed, exact-or-nothing lever alongside the normal-noise batch. If any preset is still over 5 ms after that, report the numbers and stop: further speed-up work becomes its own session.
6. Package check (the open `UVChannelData.bInitialized` ensure; `facts/biome_parameters.json` staged), `Docs/VoxelWorld.md`, `Docs/Blocks.md` if blocks change.
7. **The phase 5 record** (`Docs/Records/`): each acceptance criterion of the plan's section 4 (biome regions and borders, snowy peaks above the line, ocean variants, surface per biome, reproducibility, timings) marked met or not met, with its evidence.
8. Review, suites, commit (Hemera approves), handoff into phase 6.

## 5. Notes for session 6

- **Per-run memo (the timeboxed lever):** about 3,800 solid blocks per column still run the whole tree (7 to 9 instructions each; mostly the top 20 blocks of each run, where on_floor, under_floor and the biome tests are undecided). Within one run of one block column, blocks with the same biome, the same height class (the anchors and gradients the tree reads) and depths past the same stone_depth limits give the same result: memoize per run, exact or revert. The gate is the bench's "shortcut on and off" (0 differing blocks) plus the goldens unchanged.
- **The normal noise** is still 57 to 64% of corner time (session 4's note): batch `FVoxelNormalNoise` like `old_blended_noise`, and hoist each corner column's x and z work.
- **Tints read the per-block biome:** `Out.GetBiomes().GetBlock(LocalX, LocalY, Z)` in the column, or `FVoxelColumn::GetBiome`. The mesher runs on workers with the column; the biome storage is in the column already.
- **Captures after the rules:** compare outdoor shots against `SurfaceScenes-20261009-1508` (rules on), not older ones; grass and water are still untinted there.
- **The red frame** in the snowy-peak ground shot is the placement preview (red: blocked). The target cell holds the new snow layer, which our placement counts as occupied; 26.3 lets a block replace a single snow layer (a known gap for the blocks pass).

## 6. Known issues, each with when it is due

| Issue | Due |
|---|---|
| Rules on take every preset's column mean over 5 ms with Minecraft open (5.16 / 5.20 / 6.33; off 4.41 / 4.66 / 4.86) | Session 6, on a quiet machine (section 4, step 5) |
| No eroded-badlands hoodoos or frozen-ocean icebergs (code-made in 26.3) | Decided out of phase 5 |
| Ore veins give nothing (rules read, `ore_vein` compiles to nothing) | Phase 6 |
| The freeze pass takes block light as 0 and runs before trees; snow support uses a hard-coded block list (facts G5 suggests the `block_manifest.json` tags) | With trees and light; tags with the next blocks pass |
| A single snow layer is not replaceable when placing blocks (26.3: it is) | A later blocks pass |
| Snow layers: one layer per placement, no stacking; eight layers collide as a full cube (26.3: 7/8); the creative icon draws a cube | A later blocks pass |
| Powder snow: solid, no sinking, no item | With buckets and movement rules |
| Single Biome refuses non-overworld ids (Jesus's call pending; plains fallback is the alternative) | When Jesus answers |
| Lava in a cave clips (36% of the `cave-lava` view at 255) | If a later look asks |
| The mesher and `GetWaterBox` treat water against lava as one fluid | With flowing lava |
| A saved world opened directly with unbuildable terrain returns to the title with the reason only in the log | When the menu gets a notice channel |
| `facts/biome_parameters.json` is staged for packages, but no package was built since; the package ensure `UVChannelData.bInitialized` is still open | Session 6 package check |
| `voxel.Debug.Climate C` in game is still untried (Jesus) | Next playtest |
| `Docs/MinecraftBlockReference.md` was hand-edited after it was generated: patch counts by hand | When blocks change |

**Disposable, for the record:** `C:\dev\sandbox\phase5s5\` (patch scripts, logs, scene scripts, review diffs, mutation backups), `C:\dev\sandbox\scout-s5s6\` (now copied into the records), `phase5s4\`, `phase5s3b\`, `phase5s3\`.

## 7. Code map: session 5 changes

| What | Where | Notes |
|---|---|---|
| Context | `Source/UnnamedVoxel/Surface/VoxelSurfaceContext.h` | `EVoxelSurfaceKind`, `FVoxelSurfaceColumn`, `FVoxelSurfaceBlock`, `IsSteep`, `LerpPreliminary`, `ComputeDepthBelow` |
| Evaluator | `Surface/VoxelSurfaceEvaluator.h/.cpp` | `FVoxelSurfaceInputs`, `FVoxelSurfaceChunk` (deep/interior tables, masks, jitter cache, counters), `Compile`, `PrepareChunk`, `PrepareColumn`, `ApplyColumn`, `Evaluate`, `TestCondition`, `GetNoiseIds` |
| Freeze | `Surface/VoxelFreezePass.h/.cpp` | `Initialize`, `Apply` (Heights hint), `CanHoldSnow` |
| Climate | `Biome/VoxelBiomeClimate.h/.cpp` | `GetBaseTemperature`, `GetTemperature`, `AdjustForHeight`, `GetWobble`, `GetInfoNoise` |
| Storage | `Biome/VoxelBiomeStorage.*` | `FindBlockQuart` template, `FillJitterLayer`, cached `GetBlock` |
| Blocks | `Blocks/VoxelBlockTable.*`, `Unnamed/Voxel/VoxelBlocks.cpp` | `GetSnowyState` / `SetSnowyState` |
| Terrain | `Terrain/VoxelDensityTerrain.*` | settings `bSurfaceRules`, `bSurfaceShortcut`, `FixedBiome`; `FVoxelDensityTerrainOverrides`; `FillDressed`, `FillWithRules`; timings and counters |
| Bench | `VoxelBench.cpp` | the new passes and gates |
| Reader and factory | `Unnamed/Voxel/VoxelWorldgenReader.*` (`MaterialRule`), `VoxelSurfaceReader.cpp` (`ReadSurface`), `VoxelTerrainFactory.*` (`ReadInputs`, Single Biome, `-VoxelNoSurfaceRules`), `VoxelConsole.cpp` | |
| World | `Unnamed/Voxel/VoxelWorld.*` | `-VoxelSpawnAt=X,Y` |
| Tests | `Unnamed/Tests/VoxelSurfaceTests.cpp` (Conditions, Rules, Snow, Exact), `WorldsTests.cpp` (TerrainCheck refusals), goldens in `VoxelDensityTests.cpp`, `VoxelAquiferTests.cpp`; `Scripts/TestVoxel.ps1` expects 56 | |
| Records | `Docs/Records/2026-10-09-Voxel-Phase5-Scout-Facts.md` and `...-Scout-Evidence/` | the scout's facts, black-box JSON, probes, scripts |

## 8. What worked and what failed in session 5

**Worked; repeat it:**
- Counters before guessing: per-path block counts in the bench showed the cost was in blocks just above the preliminary surface, not in instruction counts or noise. Each lever was then exact and gated (1.35 -> 0.66-0.92 ms mean).
- Reachability under the column's biomes made the interior table useful (444 -> 3,730 blocks per column) at no risk.
- A static "may read context" pass per instruction let deep stone skip the depth bookkeeping exactly.
- Patch scripts that load every file, apply all hunks in memory and write at the end (atomic), written with the Write tool.
- A Sonnet agent writing tests from the facts while the lead edited docs; three reviewers by path list.

**Failed first; avoid it:**
- Patch scripts that wrote file by file left half-applied edits when a later hunk failed (clang-format had re-wrapped the target); re-running then duplicated hunks whose new text contained the old. Grep the current text first; make scripts atomic.
- `FParse::Value` stops at commas by default: `-VoxelSpawnAt=X,Y` needs `bShouldStopOnSeparator = false`. A PowerShell `[string]` parameter turns an unquoted `1000,2000` into "1000 2000": take `[int[]]`.
- A Bash heredoc with nested triple quotes broke; write scripts to files.
- **The lead's context was misjudged:** estimated from the harness's tokens-left counter (about 450k at report A), it was really 553k, so the 500k checkpoint passed without a case to Hemera. Run `MeasureAgentUsage.mjs` (section 9) at each checkpoint instead of estimating.
- The test writer, resumed for the review's follow-up, peaked at 250k against its 150k stop (its own estimate was "well under 150k"). Give follow-ups to a fresh agent with only the file and the list.
- After the 12:40 interruption the turn ended with no report, and the chat sat idle until 14:05. End every turn with a report to Hemera or say why not.

**Still true:** unity builds need module-unique names; no shadowed locals (a constructor parameter named like a member failed the build); `-DisableAdaptiveUnity` before committing; launch `UnrealEditor-Cmd` from PowerShell; the repository is LF (`Config/DefaultGame.ini` is CRLF); `node create-world.mjs --help` starts the full run.

## 9. Commands and tokens

```powershell
& C:\dev\sandbox\phase5s2\build.ps1 [-NoUnity]                          # build, short output
& C:\dev\projects\Unnamed\Scripts\TestVoxel.ps1                         # 56 Unnamed.Voxel tests
& C:\Users\jherediaramirez\AppData\Local\Temp\unnamed_scratch\RunAllNative.ps1   # every native suite (104)
& C:\dev\sandbox\phase5s2\bench.ps1 -Cmds "<cmds>" -Out <file>          # editor run, LogUnnamedVoxel lines only
& C:\dev\sandbox\phase5s3\runtest.ps1 -Filter <test> -Name <label>      # one automation test, errors printed
& Scripts\CaptureVoxel.ps1 [-SpawnAt X,Y] [-NoSurfaceRules] [-ExecCmds 'voxel.Debug.Biomes 1']
voxel.Bench Density 4 <preset>; voxel.Bench Columns 8 <preset>; voxel.BiomeMap 1024 8 overworld
uv run --no-project --with pillow python -I C:\dev\sandbox\phase5s5\find_scenes.py <map.png> Config\DefaultGame.ini   # places by biome from a map
```

Tokens: copy `<session>.jsonl` and `<session>/subagents/` from `~/.claude/projects/C--dev/` into `<temp>/projects/C--dev-projects-Unnamed/`, then run `node Scripts\MeasureAgentUsage.mjs --session <id>` with `CLAUDE_CONFIG_DIR=<temp>`.

**Session 5's cost,** session `9e94decd-1f6b-5b14-8f85-7d93d249d61b`, measured after the code commit, before this handoff:

| Agent | Peak context | Processed | Output |
|---|---|---|---|
| Lead (Opus 5.5) | 553k | 73.6M (72.7M cache reads) | 295k |
| Test writer (Sonnet 5.5), with its review follow-up | 250k (asked to stop near 150k) | 8.91M | 56k |
| Review, core (Sonnet 5.5) | 98k | 0.49M | 17k |
| Review, wiring and docs (Sonnet 5.5) | 105k | 0.56M | 0.1k |
| Review, tests (Sonnet 5.5) | 120k | 0.71M | 8k |
