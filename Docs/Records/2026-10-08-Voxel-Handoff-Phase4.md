# Voxel world handoff: into phase 4 - October 8, 2026

Read this first if you are picking up the voxel world (chunk loader and world generator) in a new chat. It holds everything the next session needs:

- how this user works;
- where the project stands;
- what phase 3 did;
- a code map for phase 4;
- phase 4's scope, the user's answers to its open questions, and a plan;
- every known issue;
- what worked and what failed, so you can repeat the first and avoid the second.

It replaces the earlier handoffs ([into phase 3](2026-10-08-Voxel-Handoff-Phase3.md), [the phase 3 plan](2026-10-08-Voxel-Handoff-Phase3-Plan.md)); read those only for history. Facts were true at commit `ffcc3b3`. Check `git status`, `git log -5` and newer [records](README.md) before trusting them.

## 1. Read these, in this order

1. **Rules:**
   - `C:\Users\jherediaramirez\.claude\CLAUDE.md` (PowerShell 7, `uv`, Node 24);
   - `C:\dev\CLAUDE.md` (machine conventions);
   - the project's [CLAUDE.md](../../CLAUDE.md) (rules and commands).
2. **This handoff.**
3. **[The phase 3 record](2026-10-08-Voxel-Phase3.md):** what was built, the decisions, the problems found and fixed, the measured results.
4. **[Docs/VoxelWorld.md](../VoxelWorld.md):** the living doc for status, layout, design decisions, the deterministic core, storage and meshing, streaming, editing, lighting and the edge fog, settings, tests and known gaps.
5. **[The worldgen prompt](../worldgen-implementation-prompt.md):** "Ground rules", "Reference: how Minecraft Java 26.3 does it", "Target architecture", "Job graph", phase 4's acceptance criteria, the performance targets and "How to work with me".
6. **[Minecraft Java 26.3 world generation research](../Minecraft%20Java%2026.3%20world%20generation%20research.md):** sections "How a chunk gets built", "Seeds and randomness", "Terrain shape: density, climate and splines", "What blocks go where" and "World height, bedrock and the void".
7. **[NamingConventions.md](../NamingConventions.md) and [Architecture.md](../Architecture.md)** before adding files, folders or names.
8. **Code:** only what section 5 lists for the step at hand. Do not send explorers over the codebase: in the phase 3 planning session two Haiku explorers cost 266k and 310k tokens, and targeted greps answered the same questions for a few thousand.

## 2. Working agreement with this user

| Topic | Agreement |
|---|---|
| Phases | Start with a short plan (files and why), confirm its assumptions with the user, then build. End with measured timings, known issues and what is next, then stop for the go-ahead. |
| Questions | Ask before deciding anything that changes project rules or saved data (world ids, `world.json`, binding ids). Otherwise state your assumptions and continue. When something is genuinely the user's call (a look, a gameplay rule), give 2 to 4 options with a recommendation. |
| Commits | Only when the user says so ("commit", "commit and continue"). Before committing: a Sonnet pre-commit review of the diff, and a `-DisableAdaptiveUnity` build when there are new files. Write the message to a file (`git commit -F <file>`); here-strings do not reach stdin. End it with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. The hook runs the credential scan (`Scripts/TestRepositorySafety.mjs`). |
| Pushing | `feature/voxel-world` is local only; ask before pushing. |
| Screenshots | The user wants to see progress. After any visible change, run `Scripts\CaptureVoxel.ps1`, look at the PNGs yourself first, and send the good ones with SendUserFile (it works in these sessions). Before/after montages work well (Pillow through `uv run --with pillow`). |
| Subagents | Sonnet 5.5 only for test files written from a spec, the pre-commit review and docs. Haiku 5.5 only for scraping logs. Never run builds, editor runs or captures in parallel. |
| Tokens | The user's budget is about 100k to 200k per milestone, subagents included. Phase 3 spent far more (section 8). Split a phase into one session per step, keep subagent briefs narrow, and report usage with `node Scripts/MeasureAgentUsage.mjs --session <id>`. |
| Looks | The edge of the world must read as distant haze, never a dark void or a hard horizon line. The user found the game "lost its shaders" once; check lighting settings before assuming a regression. |
| Machine | The user plays Minecraft (NeoForge 26.3, `javaw`) on the same PC, so GPU times in captures can be contended. Check `Get-Counter '\GPU Engine(*engtype_3D)\Utilization Percentage'` before trusting GPU numbers. Your captures can make their game stutter, so mention it. |

**Ground rules from the prompt:**
- original code only, never translated decompiled code (numbers from the 26.3 data are fine);
- every random value is a pure function of (seed, salt, coordinates);
- workers produce plain data, and UObjects are touched only on the game thread;
- data-driven, and instrument everything.

## 3. Where things stand

"Unnamed" is a solo Unreal Engine 5.8.3 C++ game by Jesus Heredia, at `C:\dev\projects\Unnamed`, on branch `feature/voxel-world`. It is first-person and Minecraft-like, with:

- an offline CEF main menu generated from a hash-checked reference;
- a first-person pawn with its own 20 Hz movement and vitals (Foundation);
- the Playground test world;
- the Java 26.3 block vocabulary (full cubes, slabs and still water);
- Create New World with real saved worlds (`Saved/Worlds/<id>/world.json`);
- since phase 3, an endless streamed voxel world you can mine and build in.

| Phase | Scope | Status | Commit |
|---|---|---|---|
| 0 | Scaffolding: `UnnamedVoxel` module, settings, console, RealtimeMesh vendored | Done | `b67b801` |
| 1 | Deterministic core: seeds, Hash64, Xoroshiro128++, noise, splines | Done | `c3dc645` |
| 2 | Block table, paletted sections, columns, stand-in height terrain, greedy mesher, 9 x 9 test area | Done | `0dbb259` |
| 2L | Lumen cards for chunks, generated map's sky light 3 | Done | `3982935` |
| 2W | Create New World: saved worlds, seed, type, mode, difficulty | Done | `4987cdb` |
| 3 | Streaming, mining and placing on generated terrain, `voxel.Fly`, edge fog | Done | `ffcc3b3` |
| 4 | Terrain: climate, splines, 3D density, caves, aquifers, debug views | **Next** | |
| 5-10 | Biomes and surface, decoration, structures, mountains, far LOD, persistence | | |

Phase 3's verified results:
- the acceptance flight at up to 160 blocks/s showed no holes, 0 of 16,736 frames over the 2 ms chunk budget (worst 1.81 ms), and flat memory; again after the pre-commit review's fix, 0 of 13,106 frames over (worst 1.88 ms);
- TestVoxel 26/26 and every native suite 73/73;
- Playground probe 103 checks with 0 failures;
- `create-world` 32/32, `foundation-menu` 36/36, menu units 57/57, source-check passing;
- a clean non-unity build.

The user play-tested the build and was happy: "the only things that get me are things that just have not yet been implemented".

## 4. What phase 3 did, in short

The details are in [the record](2026-10-08-Voxel-Phase3.md). In short:

- **Planner** (`UnnamedVoxel/Streaming/`):
  - circular rings measured as gaps between column squares;
  - 545 columns in view and 657 generated at radius 12;
  - unload beyond `ViewRadius + 1 + UnloadMargin` (15);
  - nearest-first order, ahead of the view on ties.
- **Pipeline** (`Voxel/VoxelColumnPipeline`): UE::Tasks generation and mesh jobs, bounded in flight (60), gated on the 3 x 3 neighbours, with stale results dropped by serial and mesh version.
- **Subsystem** (`Voxel/VoxelWorldSubsystem`):
  - the rings follow the camera;
  - all chunk work per frame stays within `voxel.ApplyBudgetMs`, using a fitted apply-cost line and an 85% stop; a frame's first column always goes, so a big column never starves the queue (the review's fix);
  - pooled chunk components; columns out of view are hidden;
  - `stat Voxel` and a per-second log line.
- **Editing:**
  - `FVoxelBlockStorage` puts the Foundation authority over the streamed columns;
  - copy-on-write edits remesh exactly the sections whose padded box holds the cell;
  - edits are kept per column and replayed after generation, so they survive unloading (for the session; phase 10 saves them);
  - Creative worlds get infinite blocks and the creative inventory; Survival starts with the loadout's tools.
- **Water:** generated water counts as water (`AVoxelWorld::IsInWater` reads blocks).
- **Edge of the world:** a refitted exponential height fog clear until 1.5 columns before the radius, a fog floor under the world, haze matched to the horizon sky, and a stronger aerial perspective.
- **Tools:**
  - `voxel.Fly [speed]`, a free camera;
  - `CaptureVoxel.ps1 -Flight` (acceptance flight, flight.csv), `-FlightSeconds`, `-Edits`;
  - `voxel.Debug.EdgeWalls`.
- **Tests:** `Unnamed.Voxel.Streaming.Planner` and `.Pipeline`; `Unnamed.Voxel.Edit.Sections`, `.Remesh` and `.Authority`.

## 5. Code map for phase 4

Paths start at `Source/UnnamedVoxel/` (core) or `Source/Unnamed/` (Unreal side).

| What | Where | Notes for phase 4 |
|---|---|---|
| Terrain interface | `Terrain/VoxelTerrain.h` | `IVoxelTerrain`: `Generate(FIntPoint, FVoxelColumn&, TArray<uint16>& Dense)`, `GetSurfaceHeight(X, Y)`, `IsDry(X, Y)`. Immutable after construction; any worker may generate any column. **`Dense` is reused scratch: clear all of it (`Init`), never `SetNumZeroed`** (phase 3's stone-walls bug). |
| Stand-in terrain | `Terrain/VoxelHeightTerrain.h/.cpp` | 2D height from continent fBm, damped hills and detail; layers of bedrock, deepslate, stone and soil; water to Z 62. Phase 4 replaces it with a density terrain and keeps it for the bench and tests only if useful. |
| Flat terrain | `Terrain/VoxelFlatTerrain.h/.cpp` | Superflat; keep it. |
| Terrain choice | `Voxel/VoxelWorld.cpp`, `AVoxelWorld::MakeTerrain` | Superflat gets the flat terrain; everything else the height terrain. Large Biomes and AMPLIFIED are saved, and their density functions exist in the jar (section 6). |
| Spawn and framing | `AVoxelWorld::GetSpawnCell`, `UpdateShots` | Both call `GetSurfaceHeight` and `IsDry` many times (rings up to `16 * ViewRadius` blocks). With a 3D density these must stay cheap: estimate from the 2D climate/height pass, or cache. |
| Noise and splines | `Noise/VoxelNoise.h`, `Noise/VoxelGradientNoise.h`, `Spline/VoxelSpline.h` | Phase 1's primitives: fBm, ridged, damped and warped 2D/3D, `SampleGrid`, and nested splines with `FVoxelSplineBuilder`. Golden checksums pin them; never change one to pass a test. |
| Randomness | `Random/VoxelRandom.h`, `Random/VoxelSeed.h` | `Hash64(seed, salt, x, y, z)`, `Salt("name")`, Xoroshiro128++. |
| Columns and sections | `Storage/VoxelColumn.h`, `Storage/VoxelSection.h` | `Build(Dense, Table)` and `SetState(x, y, z, state, table)`; paletted 16^3 sections; four heightmaps. |
| Block table | `Blocks/VoxelBlockTable.h`, `Voxel/VoxelBlocks.cpp` | Per-state info from `DA_Blocks`. It has full cubes, slabs and still water only: **no lava** (aquifers need it). |
| Pipeline generation job | `Voxel/VoxelColumnPipeline.cpp`, `LaunchGenerate` | Calls `Terrain->Generate`, then replays the column's edits. BackgroundHigh priority, thread-local `Dense`. Per-column generation must stay under 5 ms (target 2 ms). |
| Bench | `VoxelBench.h/.cpp`, `Generation/VoxelArea.h/.cpp`, `voxel.Bench Columns [radius]` | Times generate, gather and mesh per stage on one thread and on all workers. Add density stages here. |
| Console | `Voxel/VoxelConsole.cpp` | Add `voxel.Debug.Climate <param>` and friends (named in the prompt). Debug-only variables use `voxel.Debug.` and are not settings. |
| Settings | `Voxel/VoxelWorldSettings.h` | Mirror a tunable here only if it is a real setting; the `Settings` test lists variables explicitly. |
| Tests | `Source/Unnamed/Tests/Voxel*Tests.cpp` | 26 voxel tests; `Scripts/TestVoxel.ps1` expects 26. Golden-value tests in `VoxelNoiseTests.cpp` and `VoxelRandomTests.cpp`. |
| Capture | `Scripts/CaptureVoxel.ps1`, `AVoxelWorld::UpdateShots` | The overview, closeup, ground, lumen-scene and section shots, plus `edits` with `-Edits`. Captures pin the rings to the spawn and turn edge walls on. |
| Lighting | `Scripts/Editor/CreateGeneratedMap.py`, `UVoxelChunkComponent::BuildCards`, `AVoxelWorld::UpdateEdgeFog` | Caves will expose Lumen card occlusion and software Lumen's blindness (section 7). |

## 6. Phase 4: what the prompt asks, and a proposed shape

**Done when:**
- oceans, plains, hills and mountains appear at sensible scales;
- caves exist;
- a seed reproduces exactly;
- per-column generation time is logged.

Scope from the prompt:
- a climate sampler;
- splines for offset, factor and jaggedness;
- coarse-cell 3D density with interpolation and height-bound skipping;
- sea level 63;
- bedrock and deepslate gradients;
- noise caves;
- basic aquifers;
- debug color views per climate parameter.

**Questions for the user before building** (answered; see below):
1. **Lava:** aquifers need it, and the block table has none. Add still lava as a fluid now (like water: a state, a texture, a mesher layer), or run phase 4 with water-only aquifers and add lava in the block-shapes step?
2. **Software Lumen:** without mesh distance fields, caves on GPUs without hardware ray tracing are lit by the sky. Build per-column distance fields on the workers (`SetDistanceField`), or require hardware ray tracing?
3. **Lumen card occlusion:** caves will make the black-face problem common. Slice cards along their axis, or use hit lighting (`r.Lumen.HardwareRayTracing.LightingMode 1`, which [step 2L](2026-10-08-Voxel-Lighting.md) found costs more GPU time than cards)? Measure both with the `lumen-scene` shot.
4. **Fidelity:** reproduce vanilla's noise router closely (same density functions, using the jar's numbers) or a simplified router using vanilla's splines? The prompt wants vanilla-like scales, and "Beating vanilla" in the research doc lists our improvements.

**Answered on October 8**, in the first phase 4 chat, before any code. The user took the recommendation each time:

- **Lava:** add still lava now, built like water, in the aquifer session; no damage or flow yet.
- **Software Lumen:** require hardware ray tracing for now; per-column distance fields come later.
- **Card occlusion:** decided in the caves session, from captures of the same cave with cards and with hit lighting.
- **Fidelity:** evaluate 26.3's density-function graph from the jar's JSON with our own code and noise. Large Biomes and AMPLIFIED use their own presets.
- **Old worlds:** the new terrain is generator 2; worlds made before it keep the stand-in terrain (the rule in `UnnamedSavedWorlds.h`).
- **Plan:** five sessions:
  1. reference data and loader;
  2. noise, climate, splines and debug views;
  3. density, surface and spawn;
  4. caves, aquifers and lava;
  5. bench, tests, captures and docs.

That chat keeps these in the memory note `C:\Users\jherediaramirez\.claude\projects\C--dev\memory\unnamed-voxel-phase4-decisions.md` until the phase 4 record holds them. Treat them as settled.

**Data first.** VoxelWorld.md's design decision is that worldgen numbers come from the 26.3 data into a new hash-checked `Reference/minecraft-worldgen-26.3/`, as numbers only, never code. The data is on this machine, in `C:\dev\sandbox\mc-26.3-blocks\datagen\versions\26.3\server-26.3.jar` under `data/minecraft/worldgen/`:
- `noise_settings/overworld.json` (7 presets);
- `density_function/overworld/*.json` (27; 8 more for `overworld_amplified`, 12 for `overworld_large_biomes`);
- `noise/*.json` (62).

The overworld biome parameter list is built in code, not stored as data; that is phase 5's problem. Copy the JSON verbatim into the reference folder, with a manifest of SHA-256 hashes and a verify script modelled on `Reference/minecraft-blocks-26.3/tools/verify_reference.py`. Sandbox folders are disposable. To get exact facts by running Mojang's own code, see the memory note `C:\Users\jherediaramirez\.claude\projects\C--dev\memory\mc-26-3-facts-dumper.md`. Sessions in this project do not load it on their own. It covers:

- `C:\dev\sandbox\mc-26.3-creative\tools\FactsDump.java`, built with JDK 25 against the server jar and its 39 libraries;
- loading the vanilla pack through `WorldLoader` the way a dedicated server does.

**A shape that fits the code.** The five sessions above group these steps; one session per step keeps the token budget.

1. **Reference data:** extract and hash-check the worldgen JSON; add a C++ loader that turns a density-function tree into our data, as a pure parse into structs in the core, tested against the JSON.
2. **Climate sampler and splines:**
   - 2D continentalness, erosion, peaks/valleys (from weirdness), temperature and humidity from the jar's noise parameters through `FVoxelNoise`;
   - offset, factor and jaggedness splines through `FVoxelSplineBuilder`;
   - `voxel.Debug.Climate <param>` as a color view (a debug material or a capture mode).
3. **3D density:**
   - corners on 4 x 8 x 4 cells (5 x 5 x 49 per column: about 160 µs per 4-octave 3D noise on one thread in phase 1), trilinear interpolation;
   - skip cells above or below the height bounds the 2D pass gives;
   - golden checksums for a few columns at fixed seeds.
4. **Surface basics:** bedrock 0-4 and the deepslate gradient (the stand-in already does both); grass, dirt, sand and gravel by simple rules. Full surface rules are phase 5.
5. **Caves:** vanilla's noise caves (cheese, spaghetti, noodle) through the density router; carvers are phase 6.
6. **Aquifers:** basic, per the lava answer.
7. **Spawn and framing on the new terrain:** a cheap surface estimate.
8. **Bench, tests, captures, docs:**
   - `voxel.Bench Columns` per stage;
   - order-independence tests;
   - captures of oceans, plains, hills, mountains and caves (a cave shot needs a camera inside the terrain: `voxel.Fly` or a new shot);
   - Sonnet docs at the end.

Keep the 2 ms frame budget, generation under 5 ms per column (target 2 ms), and `TestVoxel.ps1`'s expected count in step with the tests.

## 7. Known issues and gaps

| Issue | Notes | When |
|---|---|---|
| Late columns in the fog's fade | When a fast camera crosses a column corner, the rings move diagonally and up to 33 edge columns arrive one frame late inside the fog's fade (10.5 to 12 columns). Never where the fog is clear. A planner bias toward the direction of travel, or generating 2 rings past the view, would fix it. | Any time |
| Lumen card occlusion | A face behind another facing the same way in one section's card stays black to Lumen. Caves will make it common. | Phase 4's caves session |
| Software Lumen | Chunks have no distance fields, so no bounce or sky occlusion without hardware ray tracing. Decided: hardware ray tracing is required for now. | Later |
| No lava | Aquifers need it. Decided: still lava, built like water. | Phase 4's aquifer session |
| Unloads in one frame | From the pre-commit review. Released columns skip the apply budget: a teleport, a respawn or turning `voxel.Fly` off far away releases about 500 chunks in one frame. Queue them and drain them under the apply estimate. | Any time |
| Resetting a populated world | From the pre-commit review. `FFoundationBlockWorld::Clear` (from `FFoundationAuthority::Reset`) lists every solid cell, which over the streamed storage means every generated non-air cell. Safe today only because `AVoxelWorld::Regenerate` resets while the pipeline is still empty. **Don't call `Authority.Reset()` on a populated voxel world** until the storage can say it streams and `Clear` calls `Empty` only. | Before any reset feature |
| `voxel.Fly` restore | From the pre-commit review. `AVoxelFlyCamera::EndPlay` turns the pawn's tick and gameplay input on unconditionally, even on quit or a level change; record the state when freezing and restore only that. | Low |
| Missing block shapes | Snow layers, plants, cactus, kelp, vines, stairs, fences, doors, panes, paths, farmland. | Before phases 5 to 7 |
| Edge fog color | Tuned to the map's fixed sun, (184, 189, 184) sRGB at the horizon; a faint seam on the sun side. A day/night cycle needs it computed from the sky. | With time of day |
| First view | 0.5 to 0.8 s after the first frame, depending on how fast the workers run: 0.48 s in the cleanest run, 0.74 s in the last (60 jobs in flight, refilled once per frame). Mesh jobs with UE::Tasks prerequisites on the generation jobs (the prompt's job graph) would cut it. | When it matters |
| Survival inventory | No survival inventory screen; Survival starts with the loadout's tools, no blocks. | Gameplay phase |
| Edits are session-only | Saved in phase 10 (delta saves). | Phase 10 |
| No fluid flow | Digging into water leaves an air pocket. | Later |
| Pawn flight | Deferred: it changes the shared 20 Hz movement step the Playground probe pins. A follow-up session card ("Creative flight for the Foundation pawn") was proposed in the phase 3 chat with a full brief. It can start now that phase 3 is committed, but not while a phase 4 session edits shared files. | Follow-up |
| `voxel.Fly` keys | Physical W/A/S/D, Space, Left Ctrl and Left Shift, not the player's bindings. | Low |
| `section` shot | Shows the circle's stepped edge from outside. | Low |
| Capture text | `build.txt` still says "the dev seed's test area" for a bare map. | Low |
| Hardcore | No permadeath screen. | With the death screen |
| World types | Large Biomes, AMPLIFIED and Single Biome generate the default terrain. Phase 4's data has their density functions. | Phases 4 and 5 |
| Stale generated docs | `Docs/ControlRegistry.*`, `FunctionalBacklog.md`; `Tests/smoke.mjs` expects 33 screens, the page has 34. | Any time |
| Old evidence | About 2.4 GB of untracked `Tests/Evidence` from before phase 3, plus phase 3's intermediate captures. Never commit without asking. | Ask |
| Unknown zips | `Reference/minecraft-hud-26.3.zip` and `minecraft-hud-26.3 (2).zip` appeared on October 8 at 19:06, from the user or another session, not from phase 3. Leave them alone; ask before using them. | Ask |

## 8. What worked and what failed in phase 3

**Worked; repeat it:**
- **Pure functions in the core with spec-written tests.** The planner test (274 checks, written by Sonnet from a spec) caught a real hysteresis bug on its first run.
- **A mutation check on agent-written tests.** An off-by-one in the remesh rule was caught by both edit tests (13 and 11 errors). Cheap, and it proves the tests bite.
- **A scripted acceptance run that records every frame** (flight.csv). Scrape the CSV with PowerShell (`Import-Csv`) instead of reading it whole.
- **Logging the failing case's state** (the hole columns' status), which turned a vague "holes" into "diagonal moves bring edge columns in early".
- **A quick mode for visual tuning** (`-FlightSeconds 3`, about 20 s), then one full run to confirm.
- **Matching colors by sampling screenshots on both sides of an edge** with Pillow, not by eye.
- **`FRealtimeMeshUpdateBuilder`**, for one RealtimeMesh update per apply.
- **Copy-on-write columns**, so workers never see a write.
- **Checking the user's own settings and GPU load** before calling something a regression.

**Failed first; avoid it:**
- **`TArray::SetNumZeroed` on a reused buffer** zeroes only new elements. Stale blocks from the previous column showed as stone walls. Use `Init(0, N)`.
- **An acceptance run gated on "everything ready"** measured nothing: it paused the camera whenever streaming was busy. Once started, a scripted run must advance every frame.
- **Hysteresis math:** a needed column's gap reaches `R + sqrt(2)`, and a diagonal move adds `sqrt(2)`. Unload beyond the generated ring plus the margin.
- **Height fog with a distance cutoff alone leaves the sky's dark lower half below the horizon.** The user rejected it at once. A fog floor under the world fixed it.
- **The engine's own exponential height fog units:** density and falloff are divided by 1000 per centimetre (`SceneCore.cpp`).
- **Exporting a class (`UNNAMED_API`) whose members are templates over private incomplete types** makes MSVC instantiate copy members everywhere. Don't export what the module alone uses, and delete copies.
- **`TMap<K, V>` as a member needs `V` complete in the header.**
- **Generic helper names in anonymous namespaces** (`Block`) clash when new files reshuffle the unity blobs. Use module-unique names; build with `-DisableAdaptiveUnity` before committing.
- **Python patch scripts against clang-formatted code:** formatting rewraps lines, so exact-match patches fail. Re-read the region first, patch smaller strings, and check every match count (the scripts did, which saved several bad edits).
- **PowerShell pairing `Remove-Item` with a quoted `C:\Program Files` path is blocked.** Put such commands in a script file (the scratchpad's `RunAllNative.ps1` pattern).
- **Costly subagents:**

  | Subagent | Peak context |
  |---|---|
  | Planner tests | 268k |
  | Pipeline and edit tests | 427k |
  | Docs | 470k |
  | Pre-commit review (a 4,135-line diff) | 421k |

  The lead session reached 914k context and 134M tokens processed (mostly cache reads). Next time:
  - run one session per step;
  - give the docs agent the exact sections to edit;
  - give test agents line-ranged reads;
  - review each step's smaller diff when it is done.

  The review earned its cost: it found an apply gate that could starve every column behind a big one, fixed before the commit.

## 9. Commands and gotchas

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' UnnamedEditor Win64 Development -Project="$PWD\Unnamed.uproject" -WaitMutex   # add -DisableAdaptiveUnity before committing new files
& Scripts\TestVoxel.ps1                                   # voxel.Info/Seed/Bench, then 26 Unnamed.Voxel tests
& Scripts\CaptureVoxel.ps1 [-Seed <text>] [-World <id>] [-ExecCmds '<cvars>'] [-Edits]   # 5 (or 6) shots + build.txt
& Scripts\CaptureVoxel.ps1 -Flight [-FlightSeconds 3]     # acceptance flight: flight.csv, flight.txt, shots
& Scripts\TestFoundationRuntime.ps1 -Editor -FrameRates 60   # Playground probe: 103 checks
Set-Location Tests; npm test; node source-check.mjs; node create-world.mjs --editor; node foundation-menu.mjs --editor
& Scripts\LaunchMenu.ps1 -Editor -Width 1600 -Height 900  # play from the menu (the user's own settings)
node Scripts\MeasureAgentUsage.mjs --session <id>          # token usage, lead and subagents
```

- **Every native suite:** `UnrealEditor-Cmd.exe Unnamed.uproject -unattended -nop4 -NullRHI "-ExecCmds=Automation RunTests Unnamed" "-TestExit=Automation Test Queue Empty" -ReportExportPath=<fresh dir> -UserDir=<temp dir>`. Run it from a script file because of the Remove-Item block. 73 tests at the end of phase 3.
- **Formatting:** `clang-format -i --style=file` with the repository `.clang-format`.
- **Evidence:** never overwrite `Tests/Evidence/<date>/...`; `*.log` is ignored. Game runs must use a throwaway `-UserDir`, or console `sg.*` changes get saved into the user's profile (it happened, section 2).
- **Headless editor:** `-ExecCmds=...,quit` does not close `UnrealEditor-Cmd`; use `-TestExit`.
- **Golden values:** the noise checksums and RNG vectors decide saved worlds; never update one to make a test pass.
- **Generated map:** `L_Generated` is made by `Scripts/Editor/CreateGeneratedMap.py`, which verifies and never rewrites. The edge fog, fog floor and aerial perspective are applied at runtime by `AVoxelWorld::UpdateEdgeFog`, not in the map.
- **Capture shots** pin the streaming rings to the spawn (`SetFocusOverride`). Outside shots turn the edge fog off; inside shots keep it.
