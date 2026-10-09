# Voxel world handoff: phase 5, into session 4 (biome source) - October 9, 2026

Read this first if you are picking up the voxel world in a new chat. Phase 5 session 3b (cave brightness and the cave-lighting comparison) is done and committed (`5524d5d`). The next chat is **session 4: biome source, storage, debug map and minimap** ([the phase 5 plan](2026-10-09-Voxel-Phase5-Plan.md), section 3). This handoff replaces [the session 3b handoff](2026-10-09-Voxel-Handoff-Phase5-Session3b.md) as the entry point; the plan stays the reference for sessions 4 to 6. Facts were true at `5524d5d`. Check `git status`, `git log -5` and newer [records](README.md) before trusting them.

## 1. Read these, in this order

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md).
2. **This handoff.**
3. **[The phase 5 plan](2026-10-09-Voxel-Phase5-Plan.md), only:** section 2 (design answers: the biome lookup), section 3's "Session 4", section 4 (acceptance captures and figures), section 7 (Jesus's answers).
4. **[Docs/VoxelWorld.md](../VoxelWorld.md), only:** the biome parts of "Worldgen data", and "Storage and meshing" where column data is filled.
5. **Code:** only what the plan's session 4 table names (plus `FVoxelBiomeTable::Quantize` and the facts file `biome_lookup_samples.json`), and only the ranges you need.

No lighting, exposure or material code is needed for session 4.

## 2. Working agreement with this user

| Topic | Agreement |
|---|---|
| Manager | **Hemera**, the day shift manager (remote session `session_01H3hHUQorWeftktqfPcd4VX`); at night Nyx, if one is running. Send it a short plan (files and why) and wait for the go-ahead. Report with `send_message` at two points. **A, before committing:** `git diff --cached --name-status`, suite counts from runs after the review's fixes, where each changed pinned count comes from, what changed in the `SHA256SUMS` files, and evidence paths best first with one montage. **B, after committing:** the hashes, the handoff path, the short prompt in full, and token figures (lead peak and each subagent). Hemera may approve the local commit after a clean Sonnet 5.5 review and green suites. **Hemera pushes** after it verifies; never push yourself. Changes to project rules or saved data, and anything destructive, wait for Jesus. If Jesus writes to you directly, his instructions win. |
| Look calls | Jesus's. On October 9 (10:42) he delegated that day's look calls to Hemera, and he can overrule them at a check-in. Send panels with `stats.py` numbers and GPU ms, plus a recommendation, and keep the other option one switch away. |
| Context budget | Keep the lead's context fairly short, but a longer chat beats a handoff when it saves re-reading. **Checkpoints** at about 300k, then every further 200k (500k, 700k): either start the wrap-up or send Hemera a short case for continuing (what's left, why it fits, what a new chat would re-read). Past 500k, continue only if a handoff would clearly cost more than it saves. **Hard stop:** start the wrap-up by about 850k. |
| Steps | Start with a short plan (files and why), confirm it, then build. End with measured timings, known issues and what is next. |
| Questions | Ask before anything that changes project rules or saved data. Otherwise state assumptions and continue. For Jesus's own calls (a look, a gameplay rule), give 2 to 4 options with a recommendation. |
| Commits | A Sonnet 5.5 pre-commit review of the staged diff. After its fixes, re-run every suite they touch; before committing, all of: the suites, the `-DisableAdaptiveUnity` build, `create-world.mjs` and the Playground probe (runtime changes). Message in a file (`git commit -F`), ending `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Branch `feature/voxel-world`. Stage with `git add -A -- . ':!Tests/Evidence' ':!logs'`: evidence and `logs/` stay untracked. |
| Subagents | 5.5 only: Sonnet 5.5 for the review, tests from a facts file and docs; Haiku 5.5 for log scraping. Give exact paths and line ranges, forbid whole-file reads of big files, and tell each agent to stop near 150k context. Give the review agent the diff as one file and a diff-only rule. Never run builds or editor runs in parallel; agents that only read or write files may run during a build. |
| Pictures | List evidence paths, best first. Mean levels, lava share and clipping, diffs and 16:9 montages with captions: `C:\dev\sandbox\phase5s3b\imgtools.py stats|diff|montage` (run with `uv run --no-project --with pillow python -I`). Older: `C:\dev\sandbox\phase4s4\stats.py`, `C:\dev\sandbox\phase4s2\montage.py` (square cells). |
| Machine | Jesus plays Minecraft (`javaw`) on this PC. On October 9 it was open from 10:44 through session 3b: leave it alone, mark absolute GPU and timing figures as measured with Minecraft open, and compare only inside one run or one back-to-back sequence. Bench means also drift by about 1 ms between idle-looking runs. |
| Looks | The world's edge reads as distant haze, never a dark void or a hard horizon. |

**Settled decisions** (plan section 7 and earlier; treat as decided):
- **Fidelity:** 26.3's data from the jar, evaluated by our own code and noise. Facts files are added only through the reference's tools, with existing files byte-identical.
- **Generator 2** keeps changing until edit saving lands in phase 10, then freezes. Goldens are re-recorded only under a decided change, never to make a test pass.
- **Biomes:** the climate-to-biome mapping is checked exactly against the jar's facts; positions only statistically.
- **Ore veins:** read in phase 5, switched on in phase 6 behind a switch.
- **Biome colours:** water plus grass and foliage tints per vertex, in phase 5 (session 6).
- **Cave exposure and emission (done in 3b; a look call, do not reopen it):** eye adaptation between two EV bounds, plus chunk emission 8000. Hemera picked option (a) as built on October 9 at 11:16, under Jesus's delegation; Jesus can overrule it.
  - Hemera accepted the cap of 12.5, with compensation -1 and middle-grey bias +2, in place of the planned 14.5 cap, because it keeps daylight exactly.
  - Knobs: `CHUNK_LIGHT_EMISSIVE` in `CreateBlockMaterials.py` (lava's glow; 5000 is estimated to clip about 0.7 stop less in caves), and `EXPOSURE_MAX_EV100` / `EXPOSURE_MIN_EV100` / `EXPOSURE_SPEED_*` in `CreateGeneratedMap.py`. Changing the map's settings means moving `L_Generated.umap` aside and re-creating it.
- **Cave lighting (done in 3b; a look call, do not reopen it):** Lumen cards stay. Hemera decided at 11:16: the cave is identical, and hit lighting differs only on the lava surface, at 1 to 1.5 ms more GPU. The switch, if a later look asks, is `r.Lumen.HardwareRayTracing.LightingMode 1`.
- **Exact performance work only:** goldens byte-identical, skip on/off at 0 differing blocks, load-order hashes equal.

## 3. Where things stand

| Phase | Scope | Status | Commit |
|---|---|---|---|
| 0 to 4 | Scaffolding to terrain | Done | `b67b801` to `b4ad098` |
| 5 plan | Data check, design, questions | Done; answers recorded | `7cae8b1`, `406d2dd` |
| 5 session 2 | Reference data, blocks, readers | Done | `eaa6ef3` |
| 5 session 3 | Performance levers | Done | `f10f5b6` |
| 5 session 3b | Cave brightness, cave-lighting comparison | Done | `5524d5d` |
| 5 session 4 | Biome source, storage, debug map, minimap | Next | |
| 5 sessions 5 and 6 | Surface rules; ocean variants, tints, captures, record | Then | |

**What session 3b built** (details in [Voxel world](../VoxelWorld.md), "Lighting", "Cave exposure and glow"):
- **Emission:** `CHUNK_LIGHT_EMISSIVE = 8000` (`Scripts/Editor/CreateBlockMaterials.py`) for `M_ChunkOpaque`, `M_ChunkCutout`, `M_ChunkTranslucent`; the block materials keep 4 (`BLOCK_LIGHT_EMISSIVE`), so the Playground is unchanged. Re-run with `-Materials=M_ChunkOpaque,M_ChunkCutout,M_ChunkTranslucent` to re-save only those.
- **Exposure:** `L_Generated`'s unbound PostProcessVolume (`EXPOSURE_SETTINGS` in `Scripts/Editor/CreateGeneratedMap.py`):
  - histogram metering between EV100 6 and 12.5, at 2 f-stops per second up and 1 down;
  - exposure compensation -1 and local exposure's middle-grey bias +2.
  - Why not 8 to 14.5 as planned: metering reads sunlit terrain at only EV100 12.8 to 13.5, so a 14.5 cap brightened every outdoor view (overview 88 -> 133). Lowering the cap and the compensation by the same 2 stops keeps daylight exactly. Local exposure pivots on middle grey times the compensation, hence the +2; without it daylight came out 0.4 EV darker.
  - `verify()` checks every setting and that `r.DefaultFeature.AutoExposure.Bias` is still 1. The old map fails it with each setting listed.
- **Shots:** the `-VoxelShot` camera adapts at 50 f-stops per second (`ShotExposureSpeed`, `Voxel/VoxelWorld.cpp`), so each shot lands on its adapted exposure. `-ExecCmds 'ShowFlag.VisualizeHDR 1'` prints the metered EV100 and the exposure on every shot.

**Figures** (mean sRGB, `CaptureVoxel -Caves`, 1600 x 900; Jesus's Minecraft 26.3 was open during every measurement of session 3b, so its absolute GPU and FPS figures run about 1 to 2 ms slow, and only comparisons in one sequence count):

| View | Before | After |
|---|---|---|
| `cave-lava` | 0.0 / 0.0 / 0.0 | 130.2 / 85.1 / 27.0 (36% of the view has a channel at 255) |
| `cave` | 63.1 / 70.2 / 58.9 | 105.9 / 110.5 / 93.5 |
| `overview`, `closeup`, `ground`, `section` | | within 0.1 of before |
| Lava at the daylight exposure (emission only) | 0 | view mean red 76.4, lava pixels 188, none at 255 |

- **Cards against hit lighting, same cave:** `cave` identical (0.02% of pixels differ by more than 8/255; two cards runs 0.03%). `cave-lava`: 21% differ by more than 24/255 (two cards runs 0.05%), all on the lava surface. GPU ms, cards / hit / cards, with Minecraft open: `cave` 4.12 / 5.53 / 3.79, `cave-lava` 5.13 / 6.22 / 5.31. Cards stay; the switch is `r.Lumen.HardwareRayTracing.LightingMode 1`.

**Verification:**

| Check | Result | Evidence |
|---|---|---|
| `TestVoxel.ps1` | 48/48 | `Tests/Evidence/2026-10-09/VoxelAutomation-20261009-110351` |
| Every native suite | 96/96 | `AllNative-20261009-110440` |
| `-DisableAdaptiveUnity` build | OK | |
| `create-world.mjs --editor` | PASS | `Worlds/Create-1791558407042` |
| Playground probe | 0 failures (43.35 FPS at a 60 cap, with Minecraft open; session 3: 43.6) | `Playground-all/Runtime-20261009-111313` |
| `npm test`, `source-check.mjs` | 57/57, PASS | |
| `CreateGeneratedMap.py` | GENERATED MAP OK (EV100 6.0 to 12.5); the old map fails verify | `C:\dev\sandbox\phase5s3b\logs\` |
| Review | Sonnet 5.5 on the diff: no high or medium; lows fixed (comments, a default-compensation check, docs wording and evidence), two C++ style nits left as fine | |

Evidence, best first: `Tests/Evidence/2026-10-09/CaveBrightness-20261009-3b/look-brightness.png`, `look-lighting.png`; `VoxelShots-20261009-105556` (after), `-104219` (before), `-104748` (emission only), `-105747` / `-105826` / `-105906` (cards / hit / cards), `-105247` / `-105415` (HDR overlays, new / old map).

## 4. Session 4: what to do

1. Plan to Hemera (files and why; the plan's session 4 table is the starting point), wait for the go-ahead.
2. Biome source (`Biome/VoxelBiomeSource`), storage (`Biome/VoxelBiomeStorage.h`), filled in the column job.
3. Debug: `voxel.BiomeMap` PNGs with a data-driven palette, `voxel.Debug.Biomes`, the on-screen climate and biome minimap with the readout at the player.
4. Bench stage and tests (`Biome.Lookup`, `.Exact`, `.Jitter`, `.Golden`).
5. Review, suites, commit (Hemera approves), handoff into session 5.

## 5. Notes for sessions 4 to 6

- **Biome lookup (session 4):**
  - Quantize targets as the jar does (`FVoxelBiomeTable::Quantize`: value x 10,000, truncated toward zero).
  - Fitness is the sum over the six parameters of the squared distance outside each inclusive range, plus the point's offset squared. Ties go to the first point in list order, which is the facts' `brute_force` answer.
  - `Biome.Lookup` must match `brute_force` on all 5,120 samples. Where `indexed` differs (95 samples), check that its biome also has a best-fitting point.
  - Biome storage fills in the column job. If it reads corner-like values, it can share them the way density corners are shared (edge columns only, pure values, `ResetCaches` in benches).
- **Surface rules (session 5):**
  - `FVoxelSurfaceRules` keeps ids as strings (blocks such as `minecraft:grass_block`, all default states; biomes; noises; random names; ore vein density functions). The evaluator resolves them when it compiles.
  - `Surface/VoxelSurfaceRules.cpp` already holds `Validate` and the op names; put the evaluator beside it (for example `VoxelSurfaceEvaluator.cpp`).
  - The conditions read 9 noises: `surface`, `surface_swamp`, `calcite`, `gravel`, `ice`, `packed_ice`, `powder_snow`, `small_patch`, `sulfur_cave_gradient`. `surface_secondary` and `clay_bands_offset` are read by code-side parts (surface depth, bands).
  - All 32 blocks the rules place exist.
  - Steepness reads neighbouring surface heights: get them without depending on load order (plan section 6) and prove it with the load-order hashes, which start cold in each order.
- **Top session 6 performance candidate: the normal noise.** It is 57 to 64% of corner time (overworld 1.03 of 1.62 ms per column before sharing; large_biomes 1.33 of 2.07; amplified 1.34 of 2.34), at 93 to 109 ns per point and node. Two exact steps:
  - batch `FVoxelNormalNoise` like `old_blended_noise` (`AccumulateOctave` exists);
  - hoist the x and z work shared by a corner column's 49 corners (each octave's floor, fraction, fade and hash partial products for x and z). That needs the corner batch grouped by corner column: today `CornerPoints` is in y-major index order.

  The costliest corner noises are `cave_cheese`, `cave_entrance`, `spaghetti_3d_rarity` and `pillar`, then the spaghetti and noodle noises. Gates as in session 3. Other levers on file: conditional noodle corners, an aquifer status cache, block fill by runs, the flight's 3 frames over 2 ms, inner rings first.
- **Measure streaming with sharing** in session 6: `CaptureVoxel -Flight` (columns' mean generate time while workers mesh; session 5 of phase 4: 7.26 ms).
- **Single Biome:** `world.json`'s `biome` may name any biome the Create screen offers, but the reference holds only the 56 overworld biomes. Session 5 decides what a non-overworld choice does (refuse at Create, or plains); it is a question for Jesus if it changes saved data.
- **Captures after biomes:** exposure now adapts. Compare outdoor shots against `VoxelShots-20261009-105556` (after 3b), not older ones. A darker biome view metered below EV100 12.5 will be exposed brighter than daylight; that is the design, not a regression.

## 6. Known issues, each with when it is due

| Issue | Due |
|---|---|
| Lava in a cave clips: 36% of the `cave-lava` view has a channel at 255 (lava reads yellow-orange, texture visible). Knob: `CHUNK_LIGHT_EMISSIVE`; 5000 is estimated (not measured) at about 0.7 stop less clipping, with the daylight lava view's mean red near 63 | If a later look asks |
| Columns under 5 ms only with sharing at best case; without it overworld 4.13 to 4.37 and amplified 4.96 to 5.24 ms, before biomes and surface rules add 0.5 to 1 ms | Session 6 (normal-noise lever, section 5) |
| The mesher (`VoxelMesher.cpp`, fluid case) and `AVoxelWorld::GetWaterBox` treat water against lava as one fluid. Aquifer walls keep them apart today. | With flowing lava |
| A saved world opened directly (`L_Generated?VoxelWorld=<id>`) with unbuildable terrain returns to the title with the reason only in the log. Create and Play show it on screen. | When the menu gets a notice channel |
| Lava counts as water for the player's movement | With lava damage and flow |
| Lava's sides show `lava_still` (vanilla puts the flow texture there) | With flowing lava |
| Snow layers: one layer per placement, no stacking; eight layers collide as a full cube (26.3: 7/8); the creative icon draws a cube | A later blocks pass |
| Powder snow: solid, no sinking, no item (vanilla's is `powder_snow_bucket`) | With buckets and movement rules |
| `facts/biome_parameters.json` is staged for packages, but no package was built since; the package ensure `UVChannelData.bInitialized` is still open | Next package check (session 6) |
| `voxel.Debug.Climate C`, the in-game readout, is still untried | Next playtest |
| `Docs/MinecraftBlockReference.md` was hand-edited after it was generated: patch its counts by hand, never regenerate over it | When blocks change |

**Disposable, for the record:** `C:\dev\sandbox\phase5s3b\` (the old map in `map-before\` and the intermediate maps, logs, scratch montages, `imgtools.py`, `remap-capture.ps1`); `C:\dev\sandbox\phase5s3\`; `C:\dev\sandbox\phase5s2\blocks-accepted\` and `blocks-assets-old\`.

## 7. Code map: session 3b changes

| What | Where | Notes |
|---|---|---|
| Chunk emission | `Scripts/Editor/CreateBlockMaterials.py` | `BLOCK_LIGHT_EMISSIVE` 4, `CHUNK_LIGHT_EMISSIVE` 8000 |
| Eye adaptation | `Scripts/Editor/CreateGeneratedMap.py` | `DAYLIGHT_EV100`, `EXPOSURE_*`, `LOCAL_EXPOSURE_MIDDLE_GREY_BIAS`, `EXPOSURE_SETTINGS`; `verify()` checks them |
| Shot exposure speed | `Source/Unnamed/Voxel/VoxelWorld.cpp` | `ShotExposureSpeed` on the shot camera's post-process settings |
| Assets | `Content/Maps/L_Generated.umap` (re-created), `Content/Blocks/M_Chunk{Opaque,Cutout,Translucent}.uasset` | |

## 8. What worked and what failed in session 3b

**Worked; repeat it:**
- A fresh "before" capture first, to measure the noise of the outdoor gate (0.1/255) before judging any change against it.
- Reading the engine's own numbers instead of inferring from sRGB: `ShowFlag.VisualizeHDR 1` printed the metered EV100, compensation and exposure on each shot, and comparing old and new maps found the 0.4 EV gap in one run.
- Reading the engine shaders (`PostProcessEyeAdaptation.usf`, `PostProcessLocalExposure.cpp`) to find what local exposure pivots on.
- `-Materials=` on `CreateBlockMaterials.py`, so only the intended assets were re-saved.
- Cards, hit, cards back to back to bracket GPU drift, and a cards-against-cards diff as the noise floor (lava's animation alone moves 29% of its pixels by more than 2/255).

**Failed first; avoid it:**
- The planned bounds (8 to 14.5) brightened every outdoor view: metering sits under 14.5 in daylight, and raising the floor cannot help. Check the metered EV before choosing bounds.
- Lowering the compensation alone made daylight 0.4 EV darker, because local exposure's pivot moved with it.
- PowerShell variables ignore case: `$B` and `$b` were one variable, and a montage panel showed the wrong capture. Use distinct names.
- `-script=Scripts\Editor\...` with a relative path resolves against the engine's `Binaries\Win64` in `UnrealEditor-Cmd`: pass an absolute path.

**Still true:** unity builds need module-unique names; no shadowed locals; `-DisableAdaptiveUnity` before committing; `-ExecCmds "...,quit"` does not close `UnrealEditor-Cmd`; launch `UnrealEditor-Cmd` from PowerShell, not the Bash tool; the repository is LF; write scripts with the Write tool, and edit files with `[IO.File]::ReadAllText` / `WriteAllText`.

## 9. Commands and tokens

```powershell
& C:\dev\sandbox\phase5s2\build.ps1 [-NoUnity]                          # build, short output
& C:\dev\projects\Unnamed\Scripts\TestVoxel.ps1                         # 48 Unnamed.Voxel tests
& C:\Users\jherediaramirez\AppData\Local\Temp\unnamed_scratch\RunAllNative.ps1   # every native suite (96)
& C:\dev\sandbox\phase5s2\bench.ps1 -Cmds "<cmds>" -Out <file>          # editor run, LogUnnamedVoxel lines only
& C:\dev\sandbox\phase5s3\runtest.ps1 -Filter <test> -Name <label>      # one automation test into the sandbox, errors printed
& Scripts\CaptureVoxel.ps1 -Caves [-ExecCmds 'ShowFlag.VisualizeHDR 1']   # the overlay prints metered EV100 and exposure per shot
Set-Location Tests; node create-world.mjs --editor; Set-Location ..; & Scripts\TestFoundationRuntime.ps1 -Editor -FrameRates 60
```

Tokens: copy `<session>.jsonl` and `<session>/subagents/` from `~/.claude/projects/C--dev/` into `<temp>/projects/C--dev-projects-Unnamed/`, then run `node Scripts\MeasureAgentUsage.mjs --session <id>` with `CLAUDE_CONFIG_DIR=<temp>`.

**Session 3b's cost,** session `73b8eeea-14e6-5c59-b56a-9558f71d2cc7`:

Measured after the code commit, before this handoff:

| Agent | Peak context | Processed | Output |
|---|---|---|---|
| Lead (Opus 5.5) | 236k | 15.1M (14.8M cache reads) | 97k |
| Pre-commit review (Sonnet 5.5) | 66k | 0.16M | 1.2k |
