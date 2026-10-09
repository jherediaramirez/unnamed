# Voxel world handoff: phase 5, into session 3 (performance levers) - October 9, 2026

Read this first if you are picking up the voxel world in a new chat. Phase 5 session 2 (reference data, blocks and readers) is done and committed (`eaa6ef3`). The next chat is **session 3: the performance levers before new work** ([the phase 5 plan](2026-10-09-Voxel-Phase5-Plan.md), section 3), with the cave brightness fix proposed for the rest of that session (section 5 below). This handoff replaces [the phase 5 handoff](2026-10-09-Voxel-Handoff-Phase5.md) as the entry point; the plan stays the reference for sessions 3 to 6. Facts were true at `eaa6ef3`. Check `git status`, `git log -5` and newer [records](README.md) before trusting them.

## 1. Read these, in this order

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md).
2. **This handoff.**
3. **[The phase 5 plan](2026-10-09-Voxel-Phase5-Plan.md), only:** section 3 (sessions), section 6 (risks) and section 7 (Jesus's answers; settled).
4. **[Docs/VoxelWorld.md](../VoxelWorld.md), only:** "Density evaluator", "Column density and terrain (generator 2)" (the figures and levers), and "Lighting" if you reach the brightness fix.
5. **Code:** only what section 4 names, and only the ranges you need.

Session 2's lead peaked at **498k context** against the 350k stop line, and its four Sonnet subagents at 160k to 486k against a 100k budget (section 10). The scope (two references, three blocks, readers and a hard error) was too big for one session. Read less: no whole large files, tool output short, one montage at a time with `stats.py` first.

## 2. Working agreement with this user

| Topic | Agreement |
|---|---|
| Manager | **Hemera**, the day shift manager (remote session `session_01H3hHUQorWeftktqfPcd4VX`), confirmed by Jesus on October 9. Report with `send_message` at two points. **A, before committing:** `git diff --cached --name-status`, suite counts from runs after the review's fixes, where each changed pinned count comes from, what changed in the `SHA256SUMS` files, and evidence paths best first with one montage. **B, after committing:** the hashes, the handoff path, the short prompt in full, and token figures (lead peak and each subagent). Hemera may approve the local commit after a clean Sonnet 5.5 review and green suites. **Hemera pushes** (Jesus approved pushes for today after Hemera verifies): never push yourself. Changes to project rules or saved data and anything destructive wait for Jesus. If Jesus writes to you directly, his instructions win. At night the manager is Nyx, if one is running. |
| Steps | Start with a short plan (files and why), confirm it, then build. End with measured timings, known issues and what is next. |
| Questions | Ask before anything that changes project rules or saved data. Otherwise state assumptions and continue. For Jesus's own calls (a look, a gameplay rule), give 2 to 4 options with a recommendation. |
| Commits | A Sonnet 5.5 pre-commit review of the staged diff. After its fixes, re-run every suite, the `-DisableAdaptiveUnity` build, `create-world.mjs` and the Playground probe (runtime changes). Message in a file (`git commit -F`), ending `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Branch `feature/voxel-world`. Stage with `git add -A -- . ':!Tests/Evidence' ':!logs'`: evidence and `logs/` stay untracked. |
| Subagents | 5.5 only: Sonnet 5.5 for tests from a facts file, the review and docs; Haiku 5.5 for log scraping. Facts files worked again (`C:\dev\sandbox\phase5s2\facts-*.md`), but give exact paths and line ranges, forbid whole-file reads of big files, and tell each agent to stop near 150k context. The review agent read 486k: give it the path list and a diff-only rule. Never run builds or editor runs in parallel; agents that only write files may run during a build. |
| Tokens | About 100k to 200k per session including subagents; lead under about 350k, then stop at a clean point and hand on. |
| Pictures | List evidence paths, best first. Montages: `C:\dev\sandbox\phase4s2\montage.py`; mean levels: `C:\dev\sandbox\phase4s4\stats.py`; crop and stretch a region: `C:\dev\sandbox\phase5s2\crop.py <in> <out> [red threshold]`. |
| Machine | Jesus plays Minecraft (`javaw`) on this PC: check `Get-Process javaw` and the GPU counter before timings. |
| Looks | The world's edge reads as distant haze, never a dark void or a hard horizon. |

**Settled decisions** (plan section 7 and earlier; treat as decided):
- **Fidelity:** 26.3's data from the jar, evaluated by our own code and noise. Facts files are added only through the reference's tools, with existing files byte-identical.
- **Generator 2** keeps changing until edit saving lands in phase 10, then freezes. Goldens are re-recorded only under a decided change, never to make a test pass.
- **Biomes:** the climate-to-biome mapping is checked exactly against the jar's facts; positions only statistically.
- **Ore veins:** read in phase 5, switched on in phase 6 behind a switch.
- **Biome colours:** water plus grass and foliage tints per vertex, in phase 5 (session 6).
- **Sessions:** five, with this one (performance) before any biome work.
- **Cave lighting:** Lumen cards. Re-run the same-cave comparison right after the brightness fix.
- **Already done in session 2:** lava, powder snow and snow layers; the hard error for saved worlds.
- **Exact performance work only:** goldens byte-identical, skip on/off at 0 differing blocks, load-order hashes equal.

## 3. Where things stand

| Phase | Scope | Status | Commit |
|---|---|---|---|
| 0 to 4 | Scaffolding to terrain | Done | `b67b801` to `b4ad098` |
| 5 plan | Data check, design, questions | Done; answers recorded | `7cae8b1`, `406d2dd` |
| 5 session 2 | Reference data, blocks, readers | Done | `eaa6ef3` |
| 5 sessions 3 to 6 | Performance; biome source; surface rules; tints, captures, record | Next: session 3 | |

**What session 2 built** (details in [Voxel world](../VoxelWorld.md) and [Blocks](../Blocks.md)):
- **Worldgen reference:** the overworld surface rule tree (37 `material_rule` files), the 8 named conditions (the tree leaves `under_ceiling` unused), the 56 overworld biomes chosen by the jar's `#minecraft:is_overworld` tag, that tag, and `multi_noise_biome_source_parameter_list/overworld.json`.
  - Two facts from `WorldgenFacts.java`: `facts/biome_parameters.json` (7,594 points in list order, quantized x 10,000) and `facts/biome_lookup_samples.json`.
  - The samples are 5,120 seeded target points with the jar's brute-force and indexed answers. They differ on 95, all ties: an independent Python scan reproduced the brute force on every sample.
  - The 123 earlier files are byte-identical apart from the manifest, README and tools. Packages now also stage `biome_parameters.json`.
- **Core types:** `FVoxelBiomeTable` and `FVoxelSurfaceRules` (5 rule ops, 10 condition ops, flat nodes, ids kept as strings), each with `Validate`.
- **Readers:** `ReadBiomes`, `ReadBiomeParameters`, `ReadSurfaceRules` and the writers, strict like `ReadPreset`; the overworld tree reads as 231 rules and 107 conditions. The JSON helpers moved into `VoxelWorldgenJson.h`.
- **Blocks,** through the block reference's tools (531 blocks plus water and lava, 3,470 states, 556 textures, 202 tags):
  - still lava with its animated `lava_still`, opaque, light 15, still 26.3's state 105;
  - powder snow as a solid cube with no item;
  - snow layers as a new k/8 shape whose collision is one layer lower, as in 26.3.
  `DA_Blocks`, the arrays and the materials were re-imported, and the voxel table's hand-made lava is gone.
- **Hard error:** `VoxelTerrainFactory` builds every world's terrain. A saved generator-2 world whose preset cannot be built is refused by Create and Play with the menu's error line, before travelling. Opened directly, it gets an empty terrain and goes back to the title screen. The dev seed keeps the stand-in fallback.
- **Tests:** `Biome.Read`, `Surface.Read`, `Surface.RoundTrip` (all 44 named files written back equal their files) and `Worlds.TerrainCheck`, plus snow, powder snow and lava checks in both block tables and the mesher.
- **Review:**
  - The Sonnet review's medium finding (a pending title-screen error that the page never showed) was removed.
  - Three strict-reader gaps were fixed.
  - One latent issue is in section 7.

**Verification after the review's fixes:**

| Check | Result | Evidence |
|---|---|---|
| `TestVoxel.ps1` | 46/46 | `Tests/Evidence/2026-10-09/VoxelAutomation-20261009-082618` |
| Every native suite | 94/94 | `AllNative-20261009-082700` |
| `-DisableAdaptiveUnity` build | OK | |
| `create-world.mjs --editor` | PASS | `Worlds/Create-1791548896869` |
| Playground probe | 0 failures | `Playground-all/Runtime-20261009-083433` |
| `npm test`, `source-check.mjs` | 57/57, PASS | |
| Worldgen verifier | `228 files hashed, 219 compared with the jar, ... 44 rule and condition files with 182 references, 7594 biome points over 56 biomes, 5120 lookup samples` | |
| Block verifier | `1246 files hashed, 1220 compared with the jar, 531 blocks + water and lava, 3470 states, 708 block layers + 48 flow layers` | |

**Pinned counts that changed:**
- `Blocks.Registry`: 531 -> 534 blocks and 3,445 -> 3,470 states.
- `Blocks.Faces`: 3,438 x 6, because it now skips both fluids.
- `Blocks.Textures`: 756, from 708 block frames + 48 flow frames (`lava_flow` 16, then `water_flow` 32, in name order).
- `TestVoxel.ps1`: 46.

No golden changed.

**Performance:** no generation code changed, so phase 4's figures stand (`voxel.Bench Density 8`, caves on, mean / p50 / p95 ms):

| Preset | Figures |
|---|---|
| overworld | 4.53 / 3.84 / 7.71 |
| large_biomes | 5.13 / 4.44 / 8.62 |
| amplified | 5.74 / 4.94 / 10.40 |

GPU per shot is unchanged too (`VoxelShots-20261009-074457`: cave 3.28 ms, cave-lava 3.48 ms).

**Jesus's playtest this morning:** fine, only the known rough edges. Those were dark caves, stand-in lava (now real lava), grass up to the peaks and wide beaches; the last two wait for session 5's surface rules.

## 4. Session 3: performance levers before new work

From plan section 3, levers 1 and 2. **Timebox 60% of the session**; anything left moves to session 6.

- **Gates (exact only):** goldens byte-identical (`Density.Golden`, `Aquifer.Golden`), skip on/off 0 differing blocks, load-order hashes equal (`voxel.Bench Columns 8`, `Density.LoadOrder`).
- **Goal:** overworld under 4 ms mean and all three presets under 5 ms (now 4.53 / 5.13 / 5.74), to make room for 0.5 to 1 ms of biome and surface work.
- **Lever 1, cross-column corner sharing:**
  - A column evaluates 5 x 5 corner columns, of which 4 x 4 are its own, so up to 36% of corners repeat across neighbours.
  - It needs a cache shared across workers, keyed by seed, preset and corner column.
  - The values are pure functions, so a cache hit must equal a fresh evaluation bit for bit, and load order must not matter.
  - Decide the memory bound, the eviction rule and the locking (sharded or lock-free); prove it with the load-order hashes.
- **Lever 2, `base_3d_noise`:**
  - A bit-exact speed-up of `FVoxelBlendedNoise` (16-octave limit noises), about 35% of AMPLIFIED's corner time.
  - Only transformations that keep every float operation's order and inputs: hoisting per-octave invariants, removing repeated conversions.
  - No runtime SIMD dispatch: it can round differently on other CPUs (see "Design decisions").
- **Profile before choosing** (phase 4's lesson: guesses from the stage split were wrong). A temporary `thread_local` per-node timer (never global state on workers), removed before the commit.
- **Code:**
  - `Density/VoxelColumnDensity.h/.cpp` (stages 1 to 4, corners);
  - `Density/VoxelDensityEvaluator.h/.cpp`;
  - `Noise/VoxelBlendedNoise.h/.cpp`;
  - `Terrain/VoxelDensityTerrain.cpp` (the column job);
  - `VoxelBench.cpp` (`RunDensity`, `RunColumns`, `HashLoadOrders`).
- **Benches:** `C:\dev\sandbox\phase5s2\bench.ps1 -Cmds "voxel.Bench Density 8 overworld, voxel.Bench Density 8 large_biomes, voxel.Bench Density 8 amplified, voxel.Bench Columns 8" -Out <file>`, machine idle.

## 5. The cave brightness fix: proposal for session 3's remaining 40%

**Before image:** `C:\dev\sandbox\phase5s2\lava-montage.png`. Its panels:
- phase 4 at +3 EV: tinted water;
- session 2 at `r.ExposureOffset 6`: real `lava_still`;
- session 2 at daylight exposure: **real lava is still invisible in a cave** (the lava view measures pure black, `stats.py` 0/0/0).

The glow half of the fix must make lava read at normal exposure.

**Decision (plan section 7):** eye adaptation between two EV bounds, plus stronger emission for light-emitting blocks.

**Where it goes:**
- **Exposure:** `Scripts/Editor/CreateGeneratedMap.py`.
  - Today `EXPOSURE_EV100 = 14.5` is set as both `auto_exposure_min_brightness` and `max` in the map's PostProcessVolume. It becomes two bounds with the histogram method and a slow speed.
  - The script verifies and never rewrites `L_Generated`, so move the map aside deliberately and re-create it.
  - Related: `Config/DefaultEngine.ini` (`r.EyeAdaptation.CachedLightingPreExposure=8`, the extended luminance range).
- **Emission:** `Scripts/Editor/CreateBlockMaterials.py`.
  - The `LightEmissive` scalar ("color x light / 15 x LightEmissive") is raised in the `M_Chunk*` materials only, so the Playground keeps its look.
  - The chunk mesher packs `LightEmission x 17` into vertex colour G (`Meshing/VoxelMesher.cpp`, near line 78).

**Options:**
- **(a) Recommended:** auto exposure for `L_Generated` between about EV100 8 (caves) and 14.5 (daylight, as now), with a slow speed, plus a chunk emission scale. The scale is chosen so the lava view's mean red is well above black at daylight exposure (target about 60/255 or more, from `stats.py`). Outdoor shots must not change: compare the overview, closeup and ground means with `VoxelShots-20261009-074457`.
- **(b) Emission only:** lava reads, cave interiors stay black.
- **(c) Exposure only:** caves lift, glowing blocks stay dim in daylight.

**Then the cave-lighting comparison (plan section 7):** the same cave with `CaptureVoxel -Caves`, cards against `r.Lumen.HardwareRayTracing.LightingMode 1`, with GPU ms. Switch to hit lighting if it looks clearly better in caves. Background images: `C:\dev\sandbox\overnight-review-20261009\cave-lighting.html`.

**If the levers take the whole session,** the fix becomes a short step before session 6.

## 6. Notes for sessions 4 and 5

- **Biome lookup (session 4):**
  - Quantize targets as the jar does (`FVoxelBiomeTable::Quantize`: value x 10,000, truncated toward zero).
  - Fitness is the sum over the six parameters of the squared distance outside each inclusive range, plus the point's offset squared. Ties go to the first point in list order, which is the facts' `brute_force` answer.
  - `Biome.Lookup` must match `brute_force` on all 5,120 samples. Where `indexed` differs (95 samples), check that its biome also has a best-fitting point.
- **Surface rules (session 5):**
  - `FVoxelSurfaceRules` keeps ids as strings (blocks such as `minecraft:grass_block`, all default states; biomes; noises; random names; ore vein density functions). The evaluator resolves them when it compiles.
  - `Surface/VoxelSurfaceRules.cpp` already holds `Validate` and the op names; put the evaluator beside it (for example `VoxelSurfaceEvaluator.cpp`).
  - The conditions read 9 noises: `surface`, `surface_swamp`, `calcite`, `gravel`, `ice`, `packed_ice`, `powder_snow`, `small_patch`, `sulfur_cave_gradient`. `surface_secondary` and `clay_bands_offset` are read by code-side parts (surface depth, bands).
  - All 32 blocks the rules place exist now.
- **Single Biome:** `world.json`'s `biome` may name any biome the Create screen offers, but the reference holds only the 56 overworld biomes. Session 5 decides what a non-overworld choice does (refuse at Create, or plains); it is a question for Jesus if it changes saved data.

## 7. Known issues, each with when it is due

| Issue | Due |
|---|---|
| The mesher (`VoxelMesher.cpp`, fluid case) and `AVoxelWorld::GetWaterBox` treat water against lava as one fluid. Aquifer walls keep them apart today. | With flowing lava |
| A saved world opened directly (`L_Generated?VoxelWorld=<id>`) with unbuildable terrain returns to the title with the reason only in the log. The page shows errors only for its own requests; Create and Play show it on screen. | When the menu gets a notice channel |
| Dark caves, and lava invisible at daylight exposure | Session 3 (section 5) |
| Large Biomes 5.13 and AMPLIFIED 5.74 ms over the 5 ms limit | Session 3 |
| Lava counts as water for the player's movement (as the stand-in did) | With lava damage and flow |
| Lava's sides show `lava_still` (vanilla puts the flow texture there) | With flowing lava |
| Snow layers: one layer per placement, no stacking; eight layers collide as a full cube (26.3: 7/8); the creative icon draws a cube | A later blocks pass |
| Powder snow: solid, no sinking, no item (vanilla's is `powder_snow_bucket`) | With buckets and movement rules |
| `facts/biome_parameters.json` is staged for packages, but no package was built this session; the package ensure `UVChannelData.bInitialized` is still open | Next package check (session 6) |
| `voxel.Debug.Climate C`, the in-game readout, is still untried | Next playtest |
| `Docs/MinecraftBlockReference.md` was hand-edited after it was generated: patch its counts by hand, never regenerate over it | When blocks change |

**Disposable, for the record:**
- `C:\dev\sandbox\phase5s2\blocks-accepted\` (the block reference as accepted before session 2) and `blocks-assets-old\` (the old `DA_Blocks` and arrays);
- the patched research script stays in `C:\dev\sandbox\mc-26.3-blocks\scripts\build_manifest.py`; the reference's `tools/` holds its copy.

## 8. Code map: session 2 additions

| What | Where | Notes |
|---|---|---|
| Biome table | `Source/UnnamedVoxel/Biome/VoxelBiomeTable.h/.cpp` | Biomes, parameter points, `Quantize`, `Validate` |
| Surface rules | `Source/UnnamedVoxel/Surface/VoxelSurfaceRules.h/.cpp` | Flat rule and condition nodes, `Validate`, op names |
| Readers | `Source/Unnamed/Voxel/VoxelSurfaceReader.cpp`, declared in `VoxelWorldgenReader.h` | Shared helpers in `VoxelWorldgenJson.h` |
| Terrain factory | `Source/Unnamed/Voxel/VoxelTerrainFactory.h/.cpp` | Used by `AVoxelWorld::MakeTerrain` and the menu's checks |
| Block shapes | `Foundation/FoundationBlockAssets.h` (`Layers1..7`, `GetFoundationShapeLayers`), `FoundationBlockRegistry.cpp` (selection, collision), `FoundationBlockMeshes.*`, `UnnamedVoxel/Blocks/VoxelBlockTable.h` (`EVoxelShape::Layers`), `Meshing/VoxelMesher.cpp`, `Voxel/VoxelBlocks.cpp` | |
| Reference tools | `Reference/minecraft-worldgen-26.3/tools/*`, `Reference/minecraft-blocks-26.3/tools/{build_reference,verify_reference,build_manifest}.py`, `Scripts/Editor/BlockImportData.py` | |
| Tests | `Tests/VoxelBiomeTests.cpp` (new), `WorldsTests.cpp`, `VoxelMesherTests.cpp`, `VoxelTestBlocks.h`, `VoxelTerrainTests.cpp`, `FoundationBlocksTests.cpp` | |

## 9. What worked and what failed in session 2

**Worked; repeat it:**
- Rebuilding each reference through its own tools, then diffing every entry against the accepted copy: only the intended entries changed.
- A research precedent for hand-added blocks (the mushroom blocks in `build_manifest.py`).
- Confirming the jar's facts with an independent Python scan before writing them into docs.
- Holding the build until both test agents had reported.
- Facts files for subagents: the test agents' counts matched the data on the first run (46/46).
- The review caught a feature that could never show (the pending error).

**Failed first; avoid it:**
- **26.3's snow layer models have an inverted inner box,** and powder snow's model is six face plates that the classifier dropped silently: read a block's model before assuming its shape.
- **A patch script missed text** that clang-format had rewrapped: re-grep after formatting.
- **Scope too big for one chat:** the lead's context reached 498k. Split sessions that touch two references.
- **Subagents read whole large files** (the review reached 486k): brief them with ranges.

**Still true:** unity builds need module-unique names, and repeated `using` declarations are legal; no shadowed locals; `-DisableAdaptiveUnity` before committing; `-ExecCmds "...,quit"` does not close `UnrealEditor-Cmd`; launch `UnrealEditor-Cmd` from PowerShell, not the Bash tool; the repository is LF.

## 10. Commands and tokens

```powershell
& C:\dev\sandbox\phase5s2\build.ps1 [-NoUnity]                          # build, short output
& C:\dev\projects\Unnamed\Scripts\TestVoxel.ps1                         # 46 Unnamed.Voxel tests
& C:\Users\jherediaramirez\AppData\Local\Temp\unnamed_scratch\RunAllNative.ps1   # every native suite (94)
& C:\dev\sandbox\phase5s2\bench.ps1 -Cmds "<cmds>" -Out <file>          # editor run, LogUnnamedVoxel lines only
& Scripts\CaptureVoxel.ps1 -Caves [-ExecCmds 'r.ExposureOffset 6']        # the offset shows emissive blocks for checks
Set-Location Tests; node create-world.mjs --editor; Set-Location ..; & Scripts\TestFoundationRuntime.ps1 -Editor -FrameRates 60
```

Tokens: copy `<session>.jsonl` and `<session>/subagents/` from `~/.claude/projects/C--dev/` into `<temp>/projects/C--dev-projects-Unnamed/`, then run `node Scripts\MeasureAgentUsage.mjs --session <id>` with `CLAUDE_CONFIG_DIR=<temp>`.

**Session 2's cost:** session `051aa6f1-bc46-54ac-82b1-24a932cdaa8e`, measured after the code commit and before this handoff:

| Agent | Peak context | Processed | Output |
|---|---|---|---|
| Lead (Opus 5.5) | 498k | 61.7M (61.0M cache reads) | 258k |
| Reader tests (Sonnet 5.5) | 287k | 6.4M | 2.1k |
| Mesher and hard-error tests (Sonnet 5.5) | 183k | 3.1M | 55.8k |
| Docs (Sonnet 5.5) | 160k | 4.4M | 26.0k |
| Pre-commit review (Sonnet 5.5) | 486k | 23.1M | 51.0k |
