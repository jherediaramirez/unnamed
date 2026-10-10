# Breaking: the first Unreal prototype (record and handoff)

Break lane, worktree `C:\dev\projects\Unnamed-break`, branch `feature/voxel-break`. This is the prototype's record and the handoff for the next breaking work (C8 carved-geometry cracks, if Jesus wants them, or polish). The system doc is [Docs/Breaking.md](../Breaking.md). The short prompt is at the end.

## 1. What was done

- **The prototype** of Jesus's picks ([the prototype handoff](2026-10-10-Breaking-Handoff-Prototype.md), section 4), with every effect behind a `foundation.Break.*` switch (`94d0cbe`). The pieces:
  - `FFoundationBreakEffects`, a plain helper the interaction view owns, with the data asset `UFoundationBreakAssets`;
  - `/Game/Breaking`, built from the block reference by three scripts;
  - 13 `Unnamed.Foundation.Break.*` tests;
  - the capture pack (`TestFoundationRuntime.ps1 -BreakPack`).
- **His picks became the defaults** after he confirmed the pack (`69f97b8`):
  - outline 2, cracks 2 with smooth growth, particles 1 with collision and spray, sounds 2, swing 2, pop on;
  - his scatter change: the particle velocity spread went from 0.1 to 0.3 blocks/s, so the particles no longer land in their spawn grid;
  - `/Game/Breaking` became `UUnnamedProjectSettings::BreakRoot` and is always cooked (`Config/DefaultGame.ini`).
- **A unity-build clash fix** (`16020de`, the manager's call): `FoundationItemTests.cpp`'s anonymous-namespace `ReadReference` became `ReadItemReference`.
  - It clashed with `FoundationBlocksTests.cpp`'s once adaptive unity put both in one blob (C2084).
  - The merge renamed S3's new `lang.json` call too.
  - **Items S4, the next session in this worktree, and every lane:** use exactly this rename if C2084 appears.
- **The merge** of `feature/voxel-world` `c90be1f` (`d8dd505`). It had one conflict, in `Docs/Records/README.md`, where both lanes' entries were kept. Section 7 has the merge's checks.
- **C8, carved-geometry cracks, is researched only.** Jesus asked to see real carved-out geometry (section 6).

## 2. Reading list for the next breaking chat

1. This record, whole.
2. [Docs/Breaking.md](../Breaking.md), whole: the switches, data flow, how each effect draws, checks and known issues.
3. For C8: `C:\dev\sandbox\break-proto\carved-geometry-design.md`, whole (sandbox; copy it into Records if the sandbox may go). Also the mock-up style "C8 Carved geometry" in `C:\dev\sandbox\break-mockup` (`?block=stone&crack=geometry`) and its stills in `stills\geometry\`.
4. Code, by ranges:
   - `Source/Unnamed/Foundation/FoundationBreakEffects.h` (whole, about 280 lines);
   - in `FoundationBreakEffects.cpp`: `UpdateCracks` and `GetStateParts` (grep), the parts a C8 overlay replaces;
   - `FoundationInteractionView.cpp`: the hooks (grep `BreakEffects`) and `AddInstance` (the mesh pivot is the cell's minimum corner).

## 3. Working agreement (unchanged from the prototype handoff, section 3)

- **Reporting:** report to the manager chat named in your start prompt. On this date the manager handed over to `session_01Rm6t4pNqopFBUje9vnNw8Q`.
- **Approvals:** the manager approves plans and commits after a diff-only Sonnet 5.5 review and green suites. Look calls are Jesus's, through the manager, with a capture pack.
- **Lanes:** the chunk meshes, chunk materials and mesher files belong to the main lane (they move `Mesher.Digest` and the hashes). Any change there goes to the manager first.
- **Never:** push, use bare `git stash`, edit CLAUDE.md, or re-record goldens or world hashes.

## 4. Settled decisions

- **Jesus's picks are the defaults** (`foundation.Break.*`, Docs/Breaking.md's table). Every other option stays one switch away.
- **Particles:** the scatter is 0.3 blocks/s (Jesus). The amount stays driven by the menu's Particles pref, whose default stays vanilla's "All"; he sets Minimal in Options (the manager's call under his away rule).
- **The manager's calls:**
  - the post-break delay of 6 ticks goes to items S5 (`MiningDelaySteps` stays 5);
  - sounds play at the reference volumes, and the menu's audio prefs come later;
  - the pose stays today's, with the new timing, until S4.
- **Files:** `UFoundationBreakAssets` lives in its own header-only `FoundationBreakAssets.h` (one UCLASS per file, Docs/NamingConventions.md), not in `FoundationBreakEffects.h` as the prototype handoff said.
- **C8 carved geometry,** if Jesus wants it, gets a fresh chat. That chat sequences the three `M_Chunk*Clip` materials in `CreateBlockMaterials.py` and the chunk component's material swap with the main lane, and the manager approves them.

## 5. Spec points filled in (the manager accepted them)

- Hit chips stay on at Decreased and go off only at Minimal. They live the same 1.5-2.3 s as break particles.
- A pop has no mined face, so its spray push is only the 0.5 blocks/s away from the camera.
- The carved rule does not wrap at the texture's borders.
- The vanilla outline (1) draws box by box, not merged outer edges.
- Cracks reset on release (VP1 saw vanilla's last about 0.5 s).
- With Swing 0 or 1, hits keep the 4-step cadence.
- A hit chip on a moved target falls back to the mined face's centre.
- A switch whose asset is missing falls back to today's path.

## 6. C8: carved-geometry cracks (researched, not built)

- **What Jesus saw:** the mock-up style, with real 1/16-deep grooves lit by the scene.
- **The design** (sandbox doc, section 2):
  - **The mesh:** a 32^3 carve grid (two cells per texel), one surface mesh per shape kind, stage, exposed-face mask and caps, shared by every block. The dark crack greys (61) cut 1/16 deep and the light lips (155) 1/32.
  - **Materials:** the existing `M_Block*`, with one ISM instance per part (UV 0 canonical, the face index in UV 1, the state's custom data).
  - **Size:** unmerged, a cube is 3.2k-4.8k triangles across stages 0-9.
  - **What gets carved:** cubes, slabs, layers and leaves. Glass and crosses keep the overlay, and dandelion and torch break before any crack shows. Stairs and fences are a phase 2.
  - **Hiding the real face:**
    - Playground: shrink the view's own instances for the cell (in-lane).
    - Generated world: swap the mined column's chunk component to clip variants `M_ChunkOpaqueClip`, `M_ChunkCutoutClip` and `M_ChunkTranslucentClip` in `CreateBlockMaterials.py`. That file is the main lane's, so it needs the manager's approval.
  - **Rejected:** inflating the copy (the block stands 6 cm proud), a global clip (all terrain becomes Masked), remeshing (four or more files in two lanes, 1-3 frames of latency).
  - **Growth:** stepped by default. Smooth growth scales the next stage's part instances by Fade along the face axis, with no material change.
  - **Risks:**
    - in noon light the real grooves read fainter than vanilla's dark core;
    - there is no vertex AO on the copy;
    - copy `LightEmissive` (8000), or glowstone goes dark;
    - the clip PSOs need warming.
  - **Effort:** about 10 h for a Playground look, about 19 h for both worlds and the pack, plus 5 h for models.

## 7. Verification

**Prototype, after the review fixes:**
- **Builds and tests:**
  - the default build is clean;
  - `Unnamed.Foundation.Break.*` 13/13, and every native suite 175/175;
  - `npm test` 57/57; `source-check.mjs` PASS;
  - the blocks, items and worldgen reference verifiers: REFERENCE OK (unchanged).
- **Scripts:**
  - `ImportBreakAssets.py` rerun: PASS "not rebuilt", with the hardened checks (the crack array's path, tag, 20 layers, uncompressed, nearest, leave-mips, linear; the attenuation's and concurrency's paths and settings; each wave's import source; no extra waves);
  - `CreateBreakMaterials.py` PASS, all three shaders compiling.
- **The volume mutation:** dropping the sound type's volume factor fails `Break.Sounds`. The file was restored byte-identical and touched.
- **The pack:** `Runtime-20261010-134622` 64/64 checks. Its analysis, `Pack-20261010-134622`, passes all 92 control frames (luma 0.05-0.95, under 2% clipped, block-rect std 0.10-0.22), and 86-99% of changed pixels for carved against today sit inside the block's rect.
- **E2E:**
  - `create-world.mjs --editor` PASS;
  - the Playground probe, 0 failures (`Playground-all/Runtime-20261010-140122`);
  - the flight (`VoxelFlight-20261010-140321`): no holes, 1 of 9,788 frames at 2.01 ms over the 2.0 ms budget.

**Defaults:**
- Break tests 13/13.
- The import verifies against `BreakRoot`.
- The pack: `Runtime-20261010-142313` 64/64. At 400 ms the particles lie scattered, with no lattice.

**The merge (`d8dd505`, rebuilt with `-DisableAdaptiveUnity`):**
- **Suites:**
  - the build is clean;
  - every native suite 190/190;
  - `TestVoxel.ps1` PASS, all 111 voxel suites (hashes and digests included; `VoxelAutomation-20261010-144706`);
  - `npm test` 57/57; `source-check` PASS;
  - the three reference verifiers: REFERENCE OK.
- **The Playground probe:** 0 failures (`Playground-all/Runtime-20261010-145449`).
- **The break pack:** 64/64 (`Playground-break/Runtime-20261010-145642`).
- **The flight** (`VoxelFlight-20261010-150132`):
  - 79 frames with holes, at most 11 columns. This is main's baseline with trees on since `1d76ade`.
  - 17 of 9,639 frames over the 2.0 ms budget, worst 2.52 ms.
  - With `voxel.Decoration.Trees 0` (`VoxelFlight-20261010-151800`): 38 frames with holes (at most 29 columns), and 4 of 9,137 frames over the budget, worst 2.49 ms.
  - Another lane's `UnrealEditor-Cmd` ran during both flights.
  - The pre-merge flight on an idle machine had no holes. The break lane touches no streaming code, so this needs main's own flight on an idle machine to compare (open question 4).
- **create-world:** the first run failed: a click target detached from the DOM while another lane's CEF e2e shared the fixed CDP port (the CEF slot rule came after). Rerun under the CEF lock: PASS (`Worlds/Create-1791659450202`).

**Measured numbers (pack `Pack-20261010-134622`; idle machine, i9-14900KF, RTX 4070 Ti SUPER):**

| What | Measured |
|---|---|
| Outline on a silhouette edge | today 2 px; thinner 1 px; vanilla mode 4 px (a square strip at an angle); high contrast a 4 px core, rgb(141, 224, 213), on a 7 px halo |
| Cracks | darkened area grows at every progress point on all 7 blocks (stone carved: 1.9k, 9.7k, 24k and 47k px at 0.05, 0.35, 0.65 and 0.95) |
| Particles | 21 per block at Minimal, 64 at All; landed by 400 ms |
| Pop | visible at +16 ms, gone by +150 ms |
| Sounds | 1 break per break at volume 1.0, pitch 0.80; stone hits 0.25 at pitch 0.486-0.515; hits 6 steps apart (Swing 2), 4 with Swing 1 |
| Cost (`LastViewMs`, 10 s mining stone) | today 0.096 ms mean, 0.170 ms p99; the picks 0.112 ms, 0.169 ms |

## 8. Code map

| What | Where |
|---|---|
| Switches, pure functions (stage, carve, cadence, swing clock, pop, pixels, sound pick, spawn, particle step) | `FoundationBreakEffects.h` (namespace `FoundationBreak`), top of `FoundationBreakEffects.cpp` |
| The effects class | `FFoundationBreakEffects` in the same files: `Initialize` (pools), `BeginFrame` (switches, prefs, camera, lazy load), `SubmitOutline`/`DrawOutlineStrips`, `UpdateMining`/`UpdateCracks`, `OnBlockRemoved`, `GetSwing`, `EndFrame` |
| View hooks | `FoundationInteractionView.cpp`: `BeginPlay`, `HandleEvents` (Broken/Popped), `DrawSelection`, `TickComponent` (mining branch, `EndFrame`), `UpdateHeldItem` (swing). The view is a friend of the effects (`GetBlockMesh`, `LayerMaterials`, `Owned`) |
| Data asset | `FoundationBreakAssets.h` |
| Asset root | `UUnnamedProjectSettings::BreakRoot`, `GetBreakAssetPath`; Python `ProjectPaths.break_asset` |
| Scripts | `Scripts/Editor/BreakImportData.py`, `ImportBreakAssets.py`, `CreateBreakMaterials.py` |
| Pack | `FoundationTourProbe.cpp` (`-FoundationTourBreak`), `Scripts/TestFoundationRuntime.ps1 -BreakPack`; analysis `C:\dev\sandbox\break-proto\analyze_pack.py` (sandbox; edit its RUN/OUT paths) |

## 9. Known issues, with "when"

- **The high-contrast core goes through the tonemapper:** rgb(141, 224, 213) instead of (87, 255, 225). When: if Jesus wants it exact (a post-tonemap pass).
- **A state with more parts than any block's default state** loses its extra parts from the crack overlay and the pop. When: models with such states get mined.
- **The vanilla outline mode measures 4 px**, not 3 (square strips). When: only if someone picks mode 1.
- **The carved cracks at late stages** read as dark mottling at 2.5 blocks. When: Jesus's look call; C8 is the alternative.
- **Timing checks in the pack flake under load:** run `131111` failed 6 break-age checks while another lane's `UnrealEditor-Cmd` ran (frames 50-90 ms). When: rerun on an idle machine.
- **`CrackSmooth`'s "equal at stage boundaries"** is untested in the pack: every capture point sits mid-stage. The pure test covers the fade.

## 10. What worked and what failed

- **Worked:**
  - A header with full declarations written by the lead first, then parallel subagents per group: scripts, pure functions and tests, the class into a sandbox fragment the lead appended. All of it compiled on the first full build.
  - Compiling new files alone with UBT `-SingleFile=<path>` while the class was still being written.
  - A one-file scratch import proved UE 5.8 imports `.ogg` natively (no WAV route).
  - Three diff-only reviewers split by path list found real bugs: dirty flags, outline joints, silent verify gaps.
  - The browser mock-up answered "what would carved geometry look like" within the hour.
- **Failed, then fixed:**
  - **Big Opus briefs overran their stop:** the class 249k, the pack probe 381k, the C8 research 207k. They can't see their meters. Keep briefs to one deliverable, and send "stop and report" early.
  - **Relative `-script=` paths** resolve against the engine's binaries folder; use absolute paths.
  - **The material's camera vector** is pixel-only; the WPO uses normalize(camera position - world position).
  - **Unlit emissive colours** need an eye-adaptation inverse in daylight.
  - **The ISM legacy index calls** default `bMarkRenderStateDirty` to false.
  - **The pack's first break-age checks** depended on frame time (the editor has no fixed time step); they were moved to dilation steering and the effects' spawn count.

## 11. Token figures (MeasureAgentUsage peaks)

- **Lead:** about 480k at this handoff (145k after the reading list).
- **Subagents:**
  - over the stop (Opus): class 249k, pack probe 381k, C8 research 207k;
  - Sonnet: scripts 117k, pure functions and tests 109k, mock-up 80k, analysis 125k, C++ fixes 96k, script fixes 59k;
  - Sonnet reviews: 88k, 82k and 103k.

## 12. Open questions (with options)

1. **C8 carved geometry:** (a) a fresh chat builds the Playground look first (about 10 h), then the generated world with the main lane's clip materials; (b) drop it. Jesus's call after the mock-up.
2. **The high-contrast colour:** (a) accept the tonemapped cyan (recommended for now); (b) a post-tonemap outline pass.
3. **The menu's audio prefs** (Master, Blocks) scaling the break sounds: (a) with the audio work (recommended); (b) now.
4. **The merged flight's holes with trees off** (38 frames, under load): (a) rerun main's flight and this branch's on an idle machine, one bench in one window, before anyone treats them as a regression (recommended); (b) accept them as main's baseline.

## Short prompt (for the next breaking chat)

> Unnamed break lane, after the prototype. Read `C:\dev\projects\Unnamed-break\Docs\Records\2026-10-10-Breaking-Prototype.md` whole, then its section 2 reading list. If Jesus wants C8 carved-geometry cracks, send the manager (named in your start prompt) a plan from section 6: the Playground look first, then the main lane's clip materials sequenced with it. Wait for the go. Work only in that worktree on `feature/voxel-break` (or the branch the manager names); never push; report to the manager.
