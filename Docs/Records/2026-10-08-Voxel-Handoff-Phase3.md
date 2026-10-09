# Voxel world handoff: into phase 3 - October 8, 2026

*Continued by [the phase 3 plan handoff](2026-10-08-Voxel-Handoff-Phase3-Plan.md): start there. It adds a code map, the proposed phase 3 plan with its test spec, and the risks. This record still holds the working agreement, the history, the open issues, the commands and the gotchas.*

Read this first if you are picking up the voxel world (chunk loader and world generator) in a new chat. It replaces the [earlier handoff](2026-10-08-Voxel-Handoff.md) and covers the project, the working agreement, what each step did, the known issues with fixes, phase 3 and how to approach it, and where subagents help. Facts below were true at commit `4987cdb` (phase 2W). Check `git status`, `git log` and newer [records](README.md) before trusting them.

## 1. The project in one paragraph

"Unnamed" is a solo Unreal Engine 5.8.3 C++ game by Jesus Heredia: first-person, Minecraft-like, on Windows 11, at `C:\dev\projects\Unnamed`. It has:

- an offline CEF main menu, generated from a hash-checked reference;
- a first-person pawn with its own 20 Hz movement and vitals (`AFoundationPawn`);
- the Playground test world;
- the Java 26.3 block vocabulary: full cubes, slabs and still water.

The voxel world is being built in phases from [the worldgen prompt](../worldgen-implementation-prompt.md). Done so far:

- a deterministic core;
- paletted storage and greedy meshing on a fixed 9 x 9-column area, lit through Lumen cards;
- Create New World with real saved worlds (seed, type, mode, difficulty).

Next is phase 3, streaming.

## 2. Read these, in this order

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md` (PowerShell 7, `uv`, Node 24), `C:\dev\CLAUDE.md` (machine conventions) and the project's [CLAUDE.md](../../CLAUDE.md) (rules and commands).
2. **This handoff.**
3. **[Docs/VoxelWorld.md](../VoxelWorld.md)**, the living doc: status, layout, design decisions, the deterministic core, storage and meshing, the test area, Saved worlds, Lighting, the RealtimeMesh findings, tests and known gaps.
4. **The records**, newest first, for the why: [phase 2W](2026-10-08-Voxel-Phase2W.md), [step 2L lighting](2026-10-08-Voxel-Lighting.md), [phase 2](2026-10-08-Voxel-Phase2.md), [phase 1](2026-10-08-Voxel-Phase1.md), [phase 0](2026-10-08-Voxel-Phase0.md).
5. **[The worldgen prompt](../worldgen-implementation-prompt.md):** the phase plan, ground rules and acceptance criteria. Read phase 3 closely.
6. **Before adding names or folders:** [NamingConventions.md](../NamingConventions.md) and [Architecture.md](../Architecture.md).
7. **For phase 3, the code it builds on:**
   - `Source/Unnamed/Foundation/FoundationBlocks.h`: `IFoundationBlockStorage` (its comment says the chunk loader will implement it) and `FFoundationBlockWorld` (registry, bounds, revisions, change log; `IsKnown` checks fixed bounds today).
   - `Source/Unnamed/Foundation/FoundationAuthority.h` and [Foundation architecture](../Foundation/Architecture.md#playground-block-interaction): the only writer of block state, through commands.
   - `Source/Unnamed/Foundation/FoundationPlayground.*`: how a lab owns an authority (`GetAuthority`), targeting (`TraceTarget`) and collision.
   - `Source/Unnamed/Voxel/VoxelWorld.*`: today's fixed area, with its build task, budgeted apply, `GatherCollision`, spawn, saved world and shots.
   - `Source/Unnamed/Voxel/VoxelChunkComponent.*`: one RealtimeMesh buffer set per column, plus Lumen cards.
   - `Source/UnnamedVoxel/Generation/VoxelArea.*`: the stages (generate, gather, mesh) to turn into per-column jobs.
   - `Source/UnnamedVoxel/Meshing/VoxelMesher.h`: a section meshes alone from its 3 x 3-column neighbourhood.

## 3. Working agreement with the user (keep doing this)

- **Phases:** start each with a short plan (files and why). End it with measured timings, known issues and what is next, then stop for the go-ahead.
- **Commits:**
  - Commit only when the user asks; they usually say "commit and continue".
  - End messages with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; the hook runs the credential scan.
  - Before committing new files, build with `-DisableAdaptiveUnity`. New files compile outside the unity blobs until committed, so name clashes otherwise show up only afterwards.
- **Pushing:** nothing on this branch is on GitHub. `main` is at `origin`, but `refactor/code-review-cleanup` and `feature/voxel-world` are local only. Ask before pushing.
- **Screenshots:** the user wants to see progress. After any visible change, run `Scripts\CaptureVoxel.ps1` (or `-World <id>`), look at the PNGs yourself first, and send the good ones with SendUserFile. Before/after montages work well.
- **Ask before deciding** anything that changes project rules or saved data. Saved world ids and the `world.json` layout are saved data now. Otherwise state assumptions and continue.
- **Ground rules from the prompt:**
  - original code only, never translated decompiled code (numbers from the 26.3 data are fine);
  - every random value is a pure function of (seed, salt, coordinates);
  - workers produce plain data, and UObjects are touched only on the game thread;
  - data-driven, and instrument everything.

## 4. Where things stand

| Commit | What |
|---|---|
| `b67b801` | Phase 0: the `UnnamedVoxel` module, settings and console, RealtimeMesh vendored at upstream `b8669a0` |
| `c3dc645` | Phase 1: deterministic core (seeds, Hash64, Xoroshiro128++, noise, splines) |
| `0dbb259` | Phase 2: block table, paletted sections, columns, stand-in height terrain, greedy mesher, chunk component and materials, the 9 x 9 test area in `L_Generated` |
| `3982935` | Step 2L: Lumen cards for chunks, generated map sky light 3 (shaded slopes no longer black) |
| `4987cdb` | Phase 2W: Create New World, saved worlds, Superflat, game mode and difficulty (Peaceful added) |
| next | This handoff |

Verified at the end of 2W:

| Check | Result |
|---|---|
| Voxel tests (`Scripts\TestVoxel.ps1`) | 21/21 |
| Every native suite | 68/68 (also on the unity layout) |
| Menu units (`npm test`) and `source-check.mjs` | 57/57; 938 assets unchanged |
| `Tests/create-world.mjs --editor` | 32/32 |
| `Tests/foundation-menu.mjs --editor` | 36/36 |
| Playground probe (60 FPS) | 103 checks, 0 failures |
| From Create to the area on screen | about 0.3 s (save 14 ms, map load ~100 ms, area 80-160 ms) |
| Generate a column / mesh a section, one thread | 0.42 ms / 0.20 ms mean |
| GPU frame, 1600 x 900, test area | about 3.0 ms |

## 5. What each step did, in short

- **Phase 0:**
  - a second module, `UnnamedVoxel` (Core only, precise floating point, so editor and packaged builds generate identically);
  - `UVoxelWorldSettings` mirrored into `voxel.*` console variables;
  - the stat group and log;
  - RealtimeMeshComponent vendored, never edited.
- **Phase 1:** seeds (`FVoxelSeed`: text hashed with FNV-1a 64), positional `Hash64`, Xoroshiro128++, hashed-lattice gradient noise (fBm, ridged, damped, warped) and nested splines. Golden checksums pin them: never change one to pass a test.
- **Phase 2:**
  - per-state block table from the registry;
  - paletted 16^3 sections and columns with four heightmaps;
  - a stand-in 2D height terrain;
  - a greedy mesher (18^3 padded gather, ambient occlusion);
  - one RealtimeMesh per column, with workarounds for the pinned commit;
  - chunk materials;
  - collision from block data (`GatherCollision`), with no mesh collision;
  - offscreen captures.
- **Step 2L:**
  - The chunks were in Lumen's ray-traced scene but had no cards, so every hit read black. Each column now builds its own cards (`UVoxelChunkComponent::BuildCards`, at most 32 per mesh).
  - The sky light is 3 in the generated map, because the sky alone was 12:1 darker than the sun.
  - Phase 2's hit-lighting test had used mode 2, which in 5.8 lights reflections only; GI hit lighting is mode 1.
- **Phase 2W:**
  - `UnnamedSavedWorlds` (`Source/Unnamed/Worlds/`) stores `Saved/Worlds/<id>/world.json`.
  - `UMenuApplication` creates, opens, renames and deletes worlds; travel is `L_Generated?VoxelWorld=<id>`.
  - `AVoxelWorld` builds the world's terrain through `IVoxelTerrain` (the height terrain or the new flat terrain) and gives the pawn its rules through `AFoundationLab::ConfigurePlayer`.
  - Peaceful is in the vitals.
  - The menu list is native, with no samples.

## 6. Known issues and gaps, with suggested fixes

| Issue | Suggested fix | When |
|---|---|---|
| **Lumen card occlusion:** a face behind another facing the same way in one section's card stays black to Lumen (seabed strips in `lumen-scene`); caves will make it common | Slice side cards along their axis (one per depth band), or use GI hit lighting (`r.Lumen.HardwareRayTracing.LightingMode 1`, about +0.3 ms) for underground areas. Measure with the `lumen-scene` shot and the brightness regions. | Phase 4 |
| **Software Lumen sees no chunks** (GPUs without ray tracing): no bounce, and caves would be lit by the sky | Build mesh distance fields per column on the workers and pass them with `SetDistanceField` (RealtimeMesh accepts them). Or accept hardware ray tracing as a requirement; ask the user. | Before phase 4's caves |
| **Generated water is not water to the player** (air, swimming, drowning) | Make `AFoundationLab::IsInWater` (both overloads) virtual and answer from block data in `AVoxelWorld` (`FVoxelStateInfo::IsFluid`, surface at `FluidAmount / 9`), as the Playground's block world does. | Phase 3 |
| **No mining, placing or creative inventory on generated terrain** | The `IFoundationBlockStorage` adapter in section 7. Creative's screen is gated on `Cast<AFoundationPlayground>` (`FoundationPanels.cpp` around 511-522); make it a lab capability. | Phase 3 |
| **Apply budget overshoots by one column** (worst 2.15 ms against 2 ms); `SetMesh` commits twice (buffer set, then cards) | Estimate cost from quad counts and stop before the budget, or split a column's apply. Try one RealtimeMesh update for both commits. | Phase 3 |
| **Meshing under load:** 0.26 ms per section with all workers against 0.20 alone | Cache `const FVoxelStateInfo*` per padded cell; a bitmask visibility pass; skip empty slices. Measure with `voxel.Bench Columns 4`. | Phase 3 if streaming needs it |
| **RealtimeMesh at the pinned commit:** several buffer sets per mesh draw wrongly, ray-tracing segments need 16-byte-aligned offsets, cached bounds go stale, and it uses APIs deprecated in UE 5.8 | Keep the workarounds (one set per column, padded layers, own bounds). For section edits, keep each column's per-section `FVoxelMeshData` and rebuild the set from the cache. Before UE 5.9, replace the folder from upstream and record the commit. | Phase 3 (cache); later (update) |
| **Hardcore** has no permadeath or hardcore hearts (`HudPresenter` sets `bHardcore = false`; death auto-respawns) | A game-over screen (spectate or delete the world), and the world's hardcore flag to the HUD. | When the death screen is built |
| **World types:** Large Biomes, AMPLIFIED and Single Biome generate the default terrain | Climate scale, height factor and single-biome selection. | Phases 4 and 5 |
| **Missing block shapes:** lava, snow layers, plants, cactus, kelp, vines, stairs, fences, doors, panes, paths, farmland. Superflat also refuses layers it lacks. | A "block shapes" step before phase 6; lava before phase 4's aquifers. | Before phases 4 and 6 |
| **Stale generated docs:** `Docs/ControlRegistry.*` and `FunctionalBacklog.md` still describe the mock world list. `Tests/smoke.mjs` expects 33 screens; the page has 34. | Fix smoke's route count, run smoke and the video suite, then `node registry.mjs --capture=... --video-capture=...`. | Any time |
| **Old evidence:** 2.4 GB of untracked `Tests/Evidence` from before this work (two Insights traces over 100 MB); also this session's experiment captures | The user deferred the decision; never commit them without asking. | Ask |

## 7. Next: phase 3, streaming

**Goal:** an endless world around the player. Done when (from the prompt):
- flying fast shows no holes;
- the game thread never exceeds the apply budget on chunk work;
- memory stays flat while circling.

Also in scope: mining and placing on generated terrain through the existing authority, near-only collision (already data-driven), and a debug fly camera.

**A shape that fits the code:**

1. **A pure planner in the core** (`Source/UnnamedVoxel/Streaming/`, unit-tested):
   - Given the player's column, `voxel.ViewRadius`, `voxel.UnloadMargin` and the view direction, it returns the columns to load (by priority: distance, biased toward the view) and to unload (beyond radius + margin: hysteresis).
   - It is plain data, so most of phase 3's logic is testable without the engine.
2. **`UVoxelWorldSubsystem`** (`UTickableWorldSubsystem`, `Source/Unnamed/Voxel/`) owns the column map and runs the pipeline each tick. `AVoxelWorld` stays the lab (collision, spawn, player rules, shots) and asks the subsystem for columns. The fixed test area becomes radius-limited streaming.
3. **Job pipeline on UE::Tasks, with stages gated by neighbours** (the prompt's job graph):
   - Generate a column: independent.
   - Mesh a section: needs the 3 x 3 columns generated, because the padded gather reads neighbours.
   - Build streams and cards: when all of a column's sections are meshed.
   - Apply on the game thread within `voxel.ApplyBudgetMs`.
   - Bound in-flight work (`voxel.MaxTasksInFlight`, 0 = twice the workers).
   - Cancel stale work with serial numbers when a column leaves the radius (finished results for unloaded columns are dropped).
4. **Rendering:**
   - Pool `UVoxelChunkComponent`s rather than creating and destroying them.
   - Keep each column's per-section `FVoxelMeshData` so an edit rebuilds the column's single buffer set from cached sections plus the new one.
   - Cards are rebuilt with the set.
5. **Editing (`IFoundationBlockStorage` over voxel columns):**
   - `FFoundationBlockWorld::IsKnown` asks "is the column loaded" instead of fixed bounds.
   - Columns are shared read-only with workers, so an edit copies the section (copy on write), sets the cell and marks it dirty.
   - It remeshes that section and any neighbour whose border changed (the 26-neighbour rule from `Mesher.Borders`).
   - Per-cell revisions (`FFoundationCell`) can live in a sparse map of edited cells.
   - The lab returns an authority from `GetAuthority`, so targeting, mining, placing and the HUD work unchanged.
   - Edits are session-only until phase 10's delta saves; say so in the record.
6. **Debug fly camera:** a console command or a lab key that detaches a fast camera from the pawn, for the "no holes" test.
7. **Instrument and test:**
   - `stat Voxel` counters: loaded columns, queued, in flight, applied per frame, apply ms, memory.
   - A per-second build line in the log.
   - Insights scopes on every stage.
   - Tests:
     - planner unit tests;
     - streamed columns identical regardless of load order (block hashes);
     - unload and reload equality;
     - an edit remeshes exactly the right sections (compare with an independent reading, as `Mesher.Borders` does);
     - a capture that flies a path and reports holes (expected but unapplied columns in view), frame times and memory.

**Suggested order:**
1. planner and its tests;
2. subsystem with generate and mesh jobs, replacing the fixed area;
3. apply budget, pooling, unload and cancellation;
4. fly camera and the acceptance capture;
5. storage adapter and the authority (mining and placing), plus the `IsInWater` fix;
6. docs, record, timings.

Show screenshots after steps 2, 4 and 5.

## 8. Subagents (Haiku 5.5 and Sonnet 5.5)

**What helped this session:** two read-only explorers ran in parallel, one mapping the menu web side and one the native side. Each took about 8 to 11 minutes and returned file:line maps, which kept the main context free for design and code.

**What would have helped more:**

- **Engine and plugin research as an explorer.** Finding which Lumen setting does what in 5.8, the 32-card limit and RealtimeMesh's card API was done by hand. A Haiku 5.5 explorer given precise questions ("where does Lumen read a primitive's card representation, and what limits apply?") would have saved main-context tokens.
- **A reviewer before each commit.** A Sonnet 5.5 subagent reading the diff could have caught the lenient JSON reads (`FJsonValue::TryGet*` converting types) before the native test did.
- **The documentation sweep.** Updating VoxelWorld.md, Architecture.md, naming, CLAUDE.md, Verification and the record is mechanical once the code is final. A Sonnet 5.5 subagent with a brief of facts and the docs' style can do it while the main session runs tests.
- **Updating stale test suites** (`smoke.mjs`, `registry.mjs`) suits a Sonnet 5.5 subagent.

**Do not parallelize:**
- builds, editor runs or captures: one build mutex, a shared derived-data cache and GPU timings;
- two writers on the same files.

**For phase 3:**

| Work | Who |
|---|---|
| Map the authority, `FFoundationBlockWorld`, the Playground's interaction loop and RealtimeMesh's update APIs | Haiku 5.5 explorers, in parallel, read-only |
| Design the planner, pipeline and storage adapter from those maps | Sonnet 5.5 planner, or the main session |
| The core implementation (subsystem, jobs, adapter) | Main session |
| Planner and storage unit tests from a written spec | Sonnet 5.5, a separate file from the implementation |
| Review the diff before each commit | Sonnet 5.5 |
| Docs and the record at the end of the phase | Sonnet 5.5, given the measured numbers |
| Scrape logs and evidence into tables (timings, hole counts) | Haiku 5.5 |

Give each subagent the files, the conventions (naming, unity-safe helper names, `.clang-format`) and what done looks like. Check its result before relying on it.

## 9. Commands and gotchas

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' UnnamedEditor Win64 Development -Project="$PWD\Unnamed.uproject" -WaitMutex
#   add -DisableAdaptiveUnity before committing new files (compiles the unity blobs the commit will produce)
& Scripts\TestVoxel.ps1                                   # voxel.Info/Seed/Bench, then 21 Unnamed.Voxel tests
& Scripts\CaptureVoxel.ps1 [-Seed hello] [-World <id> [-WorldsDir <dir>]] [-ExecCmds '...']
Set-Location Tests; npm test; node source-check.mjs      # menu units, reference checks
Set-Location Tests; node create-world.mjs --editor       # Create New World end to end (four launches)
Set-Location Tests; node foundation-menu.mjs --editor    # menu, bindings and Playground travel end to end
& Scripts\TestFoundationRuntime.ps1 -Editor -FrameRates 60   # Playground probe
# Every native suite: UnrealEditor-Cmd.exe Unnamed.uproject -unattended -nop4 -NullRHI '-ExecCmds=Automation RunTests Unnamed' '-TestExit=Automation Test Queue Empty' -ReportExportPath=<dir>
```

- **Formatting:** `clang-format -i --style=file`. Reflowed comments sometimes leave a lone word on its own line; tidy them.
- **Unity builds:** helpers in anonymous namespaces need module-unique names. Locals must not shadow members: `AActor::Layers` and `USceneComponent::Bounds` bit this project. Warnings C4456-C4459 are errors.
- **Headless editor:** `-ExecCmds=...,quit` does not close `UnrealEditor-Cmd`; use `-TestExit`.
- **PowerShell:**
  - `git commit -F -` with a here-string does not reach stdin: write the message to a file.
  - A command pairing `Remove-Item` with a quoted `C:\Program Files` path is blocked: put it in a script.
  - Quote arguments that contain commas.
- **JSON:** `FJsonValue::TryGet*` converts types. Check `Type` (or `HasTypedField`) when input comes from the page or a file.
- **Evidence:**
  - never overwrite `Tests/Evidence/<date>/...`;
  - keep editor `-UserDir` caches out of it (`Tests/Runtime/` is ignored);
  - `*.log` files are ignored.
- **Golden values:** the noise checksums and RNG vectors decide saved worlds; never update one to make a test pass.
- **Generated map:** `L_Generated` is made by `Scripts/Editor/CreateGeneratedMap.py`, which verifies and never rewrites. To change the rig, edit the script, delete the `.umap`, rerun the script, and check its "GENERATED MAP OK" line in `Saved/Logs/Unnamed.log`.
- **Lighting experiments:** `CaptureVoxel -ExecCmds` takes cvars such as `r.SkylightIntensityMultiplier 3` and `r.Lumen.HardwareRayTracing.LightingMode 1`. Each capture takes about 22 s, and `build.txt` records GPU time per shot.
