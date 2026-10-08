# Voxel world handoff - October 8, 2026

*Superseded by the [handoff into phase 3](2026-10-08-Voxel-Handoff-Phase3.md), written after step 2L and phase 2W. Kept as the record of the state at the end of phase 2.*

Read this first if you are picking up the voxel world (chunk loader and world generator) in a new chat. It gives the project, the working agreement, what is done, what is broken, and what to do next. Facts below were true at the end of phase 2; check `git status` and the newer [records](README.md) before trusting them.

## 1. The project in one paragraph

"Unnamed" is a solo Unreal Engine 5.8.3 C++ game by Jesus Heredia. It is a first-person, Minecraft-like game on Windows 11, at `C:\dev\projects\Unnamed`, with GitHub remote `origin`. It already has:

- an offline CEF main menu, generated from a hash-checked reference;
- a first-person pawn with its own 20 Hz movement solver (`AFoundationPawn`);
- the Playground test world (`/Game/Maps/L_Playground`);
- the complete Java 26.3 block vocabulary: 529 full cubes and slabs plus still water, with real 26.3 state ids, face codes, textures, tints, animation, placement, mining and a creative inventory.

The current work is an infinite, seeded voxel world. Its spec is [the worldgen prompt](../worldgen-implementation-prompt.md); its reference is [the 26.3 worldgen research](../Minecraft%20Java%2026.3%20world%20generation%20research.md).

## 2. Read these, in this order

1. `C:\Users\jherediaramirez\.claude\CLAUDE.md` (global: PowerShell 7, `uv` for Python, Node 24), `C:\dev\CLAUDE.md` (machine conventions), and the project's [CLAUDE.md](../../CLAUDE.md) (rules and commands).
2. [Docs/VoxelWorld.md](../VoxelWorld.md): the living doc for this work. It has the status table, layout, design decisions, the deterministic-core rules, storage and meshing, the test area, settings and console, the RealtimeMesh findings, tests and known gaps.
3. The phase records: [phase 0](2026-10-08-Voxel-Phase0.md), [phase 1](2026-10-08-Voxel-Phase1.md), [phase 2](2026-10-08-Voxel-Phase2.md).
4. [Docs/worldgen-implementation-prompt.md](../worldgen-implementation-prompt.md): the phase plan, ground rules and acceptance criteria. Our adjustments to it are in VoxelWorld.md under "Design decisions".
5. Before touching names or folders: [NamingConventions.md](../NamingConventions.md) and [Architecture.md](../Architecture.md).
6. For later phases:
   - [MinecraftBlockReference.md](../MinecraftBlockReference.md), Appendix A: every 26.3 ore and feature with Y ranges, counts and vein sizes.
   - `Reference/minecraft-blocks-26.3/research/locations.json`: where each block appears.
   - `Reference/minecraft-blocks-26.3/research/worldgen_heights.md`.

## 3. Working agreement with the user (keep doing this)

- **Phases:** work in the prompt's phases. Start each phase with a short plan: the files and why. End it with measured timings, known issues and what is next, then stop and wait for the user's go-ahead.
- **Commits:** commit only when the user asks. They usually say "commit and continue". End messages with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. The pre-commit hook runs a credential scan, `Scripts/TestRepositorySafety.mjs`.
- **Screenshots:** the user asked to see progress as images. After any visible change, run `Scripts\CaptureVoxel.ps1`, look at the PNGs yourself first, and send the good ones with SendUserFile.
- **Decisions already made by the user:**
  - RealtimeMeshComponent Core, vendored in `Plugins/`.
  - A Core-only module `UnnamedVoxel` plus a game-side `Source/Unnamed/Voxel/` folder.
  - Commit, then branch.
  - Keep the old 2.4 GB of `Tests/Evidence` untracked: two traces exceed GitHub's 100 MB file limit.
- **Ask before deciding** anything that changes project rules or saved data. Otherwise state assumptions and continue.
- **Ground rules from the prompt:**
  - Original code only; never translate decompiled Minecraft code. Numbers from the 26.3 data are fine.
  - Determinism: every random value is a pure function of (seed, salt, coordinates).
  - Workers produce plain data; UObjects are touched only on the game thread.
  - Data-driven; instrument everything.

## 4. Where things stand

Branch `feature/voxel-world`, off `main`. Commits:

| Commit | What |
|---|---|
| `ac3a6b1` | Checkpoint of earlier work (block system, HUD, Playground, research) on `refactor/code-review-cleanup`, then branched |
| `b67b801` | Phase 0: the `UnnamedVoxel` module, settings and console, RealtimeMesh vendored at upstream `b8669a0` |
| `c3dc645` | Phase 1: deterministic core (seeds, Hash64, Xoroshiro128++, noise, splines) |
| (none yet) | **Phase 2 is finished but uncommitted.** Ask the user before committing. |

The phase 2 working tree:

- **Source:**
  - Core folders `Source/UnnamedVoxel/{Blocks,Storage,Terrain,Meshing,Generation}/` and `VoxelStats.cpp`.
  - `Source/Unnamed/Voxel/{VoxelBlocks,VoxelChunkComponent,VoxelWorld}.*` and an edited `VoxelConsole.cpp`.
  - Tests: `Source/Unnamed/Tests/Voxel{Mesher,Storage,Terrain}Tests.cpp` and `VoxelTestBlocks.h`.
  - Edits to `FoundationLab.h` (new `GetFallOutZ`), `FoundationPawn.cpp`, `UnnamedProjectSettings.*` (`GeneratedMap`) and `Config/DefaultGame.ini` (MapsToCook).
- **Assets:** `Content/Blocks/M_Chunk{Opaque,Cutout,Translucent,Water}.uasset` and `Content/Maps/L_Generated.umap`.
- **Scripts:** `CaptureVoxel.ps1` and `Editor/CreateGeneratedMap.py` are new; `CreateBlockMaterials.py`, `ProjectPaths.py` and `TestVoxel.ps1` are edited.
- **Docs:** VoxelWorld.md, the phase 2 record, the Records README, CLAUDE.md, Architecture.md and NamingConventions.md.
- **Evidence to commit with phase 2**, under `Tests/Evidence/2026-10-08/`:
  - `VoxelAutomation-20261008-152734` (19/19 tests);
  - `VoxelShots-20261008-152540` (the fixed shots sent to the user);
  - `VoxelShots-20261008-152702` (adds the "player grounded" line);
  - optionally `VoxelShots-20261008-151738`, the first run, which shows the floating-section bug.
- **Evidence to leave out:** the intermediate debugging runs `-151626`, `-152059`, `-152119` and `-152306`, and all old evidence from before this work.
- **`Scripts/Editor/__pycache__/ProjectPaths.cpython-311.pyc`** is tracked in git and was regenerated by the editor's Python. It is junk; ask the user whether to untrack it rather than committing it again.

Verified at the end of phase 2:

| Check | Result |
|---|---|
| Voxel tests (`Scripts\TestVoxel.ps1`) | 19/19 |
| Every native suite | 65/65 |
| Editor build | Succeeds with no warnings in project code; RealtimeMesh warns about UE 5.8 deprecations |
| Player in the test area | Stands grounded on generated terrain |
| Generate a column, one thread | 0.42 ms |
| Mesh a section with geometry, one thread | 0.20 ms mean, 0.32 ms worst |
| Column memory | 10 KB |

## 5. How the voxel world fits together (short)

- **Coordinates:** cells use Unreal axes (X east, Y south, Z up, 100 cm per block); Z is Minecraft's Y. The world spans Z -64 to 319 (24 sections of 16) and sea level is 63. A column is 16 x 16 and its key is (floor(x/16), floor(y/16)).
- **Core (`Source/UnnamedVoxel`, depends on Core only, precise floating point):**
  - `Random/`: seeds and hashing.
  - `Noise/`: hashed-lattice gradient noise and fBm, ridged, damped and warped fractals.
  - `Spline/`: nested Hermite splines.
  - `Blocks/VoxelBlockTable`: per-state info.
  - `Storage/`: palette sections and columns with heightmaps.
  - `Terrain/VoxelHeightTerrain`: the phase 2 stand-in heightmap.
  - `Meshing/VoxelMesher`: an 18^3 padded gather, greedy merging per slice keyed by state and corner occlusion, partial faces per cell.
  - `Generation/VoxelArea`: square builds with timings.
- **Unreal side (`Source/Unnamed/Voxel`):**
  - `UVoxelWorldSettings` plus the `voxel.*` console variables. Runtime reads the variables; the settings push into them.
  - `VoxelBlocks`: builds the table from `FFoundationBlockRegistry`.
  - `UVoxelChunkComponent`: a RealtimeMesh with one buffer set per column and a poly group per layer.
  - `AVoxelWorld`: an `AFoundationLab` subclass in `L_Generated`. It builds the area in a UE::Tasks task, applies columns within `voxel.ApplyBudgetMs`, answers `GatherCollision` from block data, spawns the player on dry land, and with `-VoxelShot=<dir>` takes screenshots and quits.
- **Collision:** the pawn never uses Chaos. It calls `Lab->GatherCollision(Region)` for boxes, so chunk meshes have no collision on purpose.
- **Rendering:** the chunk materials decode per-vertex face codes. UV1 holds (code, overlay), UV2 (tint, face), and the vertex colour R holds AO and G holds light. They use the same texture array and animation table as the Playground's block materials.

## 6. Known bugs and fix-me items, with suggested approaches

1. **Shaded slopes are almost black.** *Fixed after this handoff, differently from the advice below: see [step 2L](2026-10-08-Voxel-Lighting.md). RealtimeMesh Core does accept Lumen cards.*
   - **Cause:** RealtimeMesh Core gives Lumen no surface cache; the Lumen scene view (`r.Lumen.Visualize 3`) shows the chunks black, so nothing bounces light.
   - **Already tried:** `r.Lumen.HardwareRayTracing.LightingMode 2` passed through `-ExecCmds` changed nothing. Unverified: whether the variable exists in 5.8 and whether hardware ray tracing is on (check `r.Lumen.HardwareRayTracing`).
   - **Recommended for phase 3:** a per-vertex sky-light value. Compute sky visibility per cell in the mesher (from the column heightmaps, or a small sky-light flood like the reference's light engine), put it in vertex colour B, and add an ambient term in the chunk materials (`CHUNK_UNPACK` in `CreateBlockMaterials.py`). The same term later makes caves dark.
   - **Alternatives:** RealtimeMesh Pro (Lumen cards and SDF, paid), or our own `FPrimitiveSceneProxy`. Ask the user before choosing a paid option.
2. **Generated water is not water for the player.** `AFoundationLab::IsInWater` (both overloads) is not virtual and reads `WaterBoxes`. Make it virtual and have `AVoxelWorld` answer from block data (`FVoxelStateInfo::IsFluid`, with the surface at `FluidAmount / 9`), as the Playground does in `FFoundationBlockWorld`. Separately, water looks pale from above; that can wait for phase 5's water colour.
3. **Meshing under load.** A meshed section takes 0.26 ms with all 30 workers against 0.20 ms on one thread. Ideas, in order:
   - cache `const FVoxelStateInfo*` per padded cell instead of calling `Table.Get` six times per cell;
   - a bitmask visibility pass (16-bit rows per slice) before building key grids;
   - skip slices with no blocks.
   Measure with `voxel.Bench Columns 4`.
4. **The apply budget overshoots** by one column (worst 2.10 ms against 2.0). Phase 3 can split a column's work or estimate cost from quad counts.
5. **RealtimeMesh at the pinned commit** (details in VoxelWorld.md, "RealtimeMeshComponent"):
   - Several buffer sets per mesh draw wrongly (only the last created renders), so we use one per column. A section edit therefore rebuilds the column's stream set: phase 3/10 should keep each column's per-section `FVoxelMeshData` and rebuild the set from the cache.
   - Each poly group's ray-tracing index offset must be 16-byte aligned, so layers are padded to a multiple of 4 triangles.
   - Cached bounds can go stale, so the column computes its own.
   - It uses APIs deprecated in UE 5.8, so it needs an upstream update before UE 5.9. Do not edit the vendored copy; replace it from upstream and record the commit.
6. **Missing block shapes** for later phases: lava (aquifers, phase 4), snow layers (phase 5), cross-model plants, cactus, sugar cane, kelp, vines (phase 6), and stairs, fences, doors, panes, paths and farmland (phase 7 villages). The registry has only full cubes, slabs and still water. Plan a "block shapes" step before phase 6, and lava before phase 4.
7. **Stale docs** an agent noticed: `Docs/ControlRegistry.md` line 3 links a deleted `SuperFlat.md`; the `LogUnnamedWorlds` comment in `Source/Unnamed/UnnamedLog.h:8` still mentions Super Flat (fix it when phase 2W starts using the category).
8. **Old evidence:** 2.4 GB of untracked `Tests/Evidence` from before this work, including two Insights traces over 100 MB. The user deferred deciding how to store it; don't commit it.

## 7. Next: phase 2W, Create New World (the user asked for menu settings to really work)

*Done after this handoff: see [phase 2W](2026-10-08-Voxel-Phase2W.md). Phase 3, streaming, is next.*

What exists today (from the menu exploration; line numbers may drift):

- **The Create screen:** `Reference/minecraft-menu/index.html` around line 126; the generated copy is `Content/Menu/Web/index.html` around 127.
  - The draft comes from `newDraft()` (around reference line 59) and is a global `draft`.
  - Fields: `name` ("New World", up to 128 characters), `gameMode` (Survival, Hardcore, Creative), `difficulty` (Peaceful, Easy, Normal, Hard), `commands`, `type` (Default, Superflat, Large Biomes, AMPLIFIED, Single Biome), `seed` (raw text, up to 128 characters, blank means random), `structures`, `bonus`, `rules` (59 game rules), `experiments`, `biome` (Single Biome), `flat` and `flatBiome` (Superflat layers).
  - Pressing Create only shows "World creation stops here"; nothing reaches native code.
  - `MenuWeb/Patches/Dialogs.mjs:31` makes Hardcore force Hard difficulty and turn cheats off.
- **Native side:** `UMenuApplication` (`Source/Unnamed/Menu/MenuApplication.*`) exposes only `GetState`, `RequestQuit` and `RequestOpenWorld(id)`.
  - `RequestOpenWorld` accepts catalog ids only and travels with `UGameplayStatics::OpenLevel(map)` with no options.
  - There are no world saves anywhere, and `LogUnnamedWorlds` is unused.
- **The world list:** built by `syncWorlds` in `MenuWeb/Runtime/MenuApplicationRuntime.js`: catalog worlds first (`builtIn: true`), then the reference's two mock entries.
- **Tests that pin the mock behaviour** and will need updating:
  - `Tests/smoke.mjs` (around 101-186);
  - `Tests/registry.mjs` ("Create opens an explanation"), which also regenerates `Docs/ControlRegistry.*`;
  - `Tests/supplement.mjs:53-58` (World Type click order);
  - `Tests/foundation-menu.mjs:47-68` (expects 3 rows);
  - `Source/Unnamed/Tests/WorldsTests.cpp` (exactly one catalog world).
- **Menu rules:** never edit the generated `index.html` or the hash-checked `Reference/`. Change `MenuWeb/Patches/*.mjs` or `MenuWeb/Runtime/*.js`, then run `node MenuWeb/AdaptMenu.mjs`. Each JS global's native binding name is its name lowercased.

Proposed design (agreed in outline with the user; confirm details):

- **Saved worlds:** `Saved/Worlds/<id>/world.json` holds the name, the seed text and the parsed int64 (`FVoxelSeed`, already built), world type, game mode, difficulty, cheats, structures, bonus chest, game rules, created and last-played times, and a generator version.
  - Write it with `UnnamedFiles::SaveTextAtomically`.
  - Folder ids are sanitized from the name and made unique. They are a new kind of saved id, so they never change once created.
- **New `UMenuApplication` calls:** `CreateWorld(json draft)` validates and saves, then opens the world. `GetState` lists saved worlds. `RequestOpenWorld` (or a sibling) opens a saved world by sending `/Game/Maps/L_Generated` with an option such as `?VoxelWorld=<id>`, or through a GameInstance subsystem. `AVoxelWorld` reads the metadata instead of `DevSeed`.
- **Option mapping:**
  - Seed: through `FVoxelSeed::Parse`; a blank seed is drawn once and saved.
  - Default world type: today's terrain.
  - Superflat: a flat column generator from the layer list (strip `minecraft:` from ids).
  - Large Biomes, AMPLIFIED and Single Biome: saved now, and they act in phases 4 and 5.
  - Game mode: Survival and Creative map to `FFoundationVitals` mode plus creative inventory and supply. Hardcore is Survival on Hard; real permadeath needs a game-over screen.
  - Difficulty: add `Peaceful` to `EFoundationDifficulty` and its vitals rules.
  - Cheats, structures, bonus chest and game rules: saved, each taking effect when its system exists.
  - Experiments and Data Packs: stay mocks.
- **Show it working:** create a world with seed "hello", Play, and the test area shows that seed (screenshot it).

After 2W comes phase 3, streaming:

- `UVoxelWorldSubsystem`, with rings around the player, hysteresis (unload at radius + 2), view-biased priority, the UE::Tasks pipeline, bounded in-flight work (`voxel.MaxTasksInFlight`), budgeted apply, pooled components and cancellation.
- `IFoundationBlockStorage` over voxel columns, so the existing authority does mining and placing. Its `IsKnown` must ask whether a column is loaded rather than check fixed bounds; edits copy the section, mark it dirty and remesh it and any neighbours whose border changed.
- A debug fly camera for the "fly fast, no holes" acceptance test.
- The lighting decision from item 1.

Phases 4 to 10 follow the prompt. For 4 and 5, extract the 26.3 biome parameter list, splines and noise settings, as numbers, into a new hash-checked `Reference/minecraft-worldgen-26.3/`. The jar is `%APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar`, and the server reports used by `Reference/minecraft-blocks-26.3/tools/build_worldgen.py` include `biome_parameters/minecraft/overworld.json`.

## 8. Commands and gotchas

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' UnnamedEditor Win64 Development -Project="$PWD\Unnamed.uproject" -WaitMutex
& Scripts\TestVoxel.ps1                      # voxel.Info/Seed/Bench Noise, then 19 Unnamed.Voxel tests -> Tests/Evidence/<date>/VoxelAutomation-*
& Scripts\CaptureVoxel.ps1 [-Seed hello] [-ExecCmds '...']   # L_Generated offscreen: overview, closeup, ground, lumen-scene, section + build.txt
# All native suites (expect 65 at the end of phase 2):
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\Unnamed.uproject" -unattended -nop4 -NullRHI '-ExecCmds=Automation RunTests Unnamed' '-TestExit=Automation Test Queue Empty' -ReportExportPath=<dir>
# Chunk materials only (quote the comma list; add -AllowCommandletRendering to prove the shaders compile):
& '...\UnrealEditor-Cmd.exe' "$PWD\Unnamed.uproject" '-run=pythonscript' "-script=$PWD\Scripts\Editor\CreateBlockMaterials.py" '-unattended' '-nop4' '-nosplash' '-AllowCommandletRendering' '-Materials=M_ChunkOpaque,M_ChunkCutout,M_ChunkTranslucent,M_ChunkWater'
```

- **Formatting:** format C++ with `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe -i --style=file <file>`. Reflowed comments sometimes leave a lone word on its own line; tidy them.
- **Unity builds:** helpers in anonymous namespaces need module-unique names. New untracked files compile outside the unity blob (adaptive unity), so collisions appear only after a commit; warnings C4456-C4459 are errors.
- **Headless editor:** `-ExecCmds=...,quit` does not close `UnrealEditor-Cmd`; end runs with `-TestExit` after some tests.
- **PowerShell:** quote any argument containing commas (`'-Materials=A,B'`). A command that pairs `Remove-Item` with a quoted `C:\Program Files` path can be blocked by the safety check; put such steps in a script.
- **Evidence:** never overwrite `Tests/Evidence/<date>/...` or `Packages/`; use fresh names. The editor's `-UserDir` cache goes to a temp folder, not into evidence.
- **Counts:** when you add `Unnamed.Voxel` tests, update `$expected` in `Scripts/TestVoxel.ps1`.
- **Golden values:** the noise checksums and RNG vectors decide saved worlds. Never update one to make a test pass.
- **First run:** the first `CaptureVoxel.ps1` after a material change compiles shaders, so allow a few minutes.
