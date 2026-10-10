# Breaking handoff: into the first Unreal prototype (Jesus's picks)

Break lane, worktree `C:\dev\projects\Unnamed-break`, branch `feature/voxel-break`. Start here in a new chat. The short prompt is at the end.

## 1. Where things stand

**Steps 1-2 are done:** the [ideas doc](2026-10-10-Breaking-Feel-Ideas.md) (commit `936bb2c`).

**The vanilla assets are in the reference:** [Breaking reference record](2026-10-10-Breaking-Reference.md) (commit `df7fca6`):
- the 10 destroy stages;
- `sounds.json` and the 762 break and hit oggs;
- every block's sound type (`facts/block_sounds.csv`);
- every state's particle texture (`block_particles.json`).

`DA_Blocks` changed only its digest, through `Scripts/Editor/RestampBlockDigest.py`, with a T3D proof.

**The unpublished vanilla numbers are measured:** [VP1 probe record](2026-10-10-Breaking-VP1-Probe.md).

**Jesus chose his first prototype in a browser mock-up** at `C:\dev\sandbox\break-mockup` (sandbox, never committed; section 4 has every number, so the sandbox can go):
- the first launch: `Open-Mockup.cmd`, or `node C:\dev\sandbox\break-mockup\serve.mjs`;
- if the server already runs, `Start-Process http://127.0.0.1:8765/`.

**Not started:** no Unreal code for breaking exists yet. Today's game is unchanged.

**Build:** the worktree built cleanly at the base (`Build.bat UnnamedEditor`, 130 s), and no C++ has changed since. `Tests` has had `npm ci`.

**Branch state:**
- Commits are local. The manager pushes after verifying; never push yourself.
- `feature/voxel-world` is getting items S3 (Foundation items, HUD). Merge it into this branch once, only on the manager's merge go (section 3).

## 2. Reading list (in order, with exact sections)

1. This handoff, whole.
2. [Ideas doc](2026-10-10-Breaking-Feel-Ideas.md):
   - section 3 (today against vanilla);
   - section 5, only the rows for the picks: O1/O2, C1/C2, P1-P4, S1/S2, W1-W3, X1;
   - sections 8 (how to compare) and 9 (files and lanes).
   - Skip section 4: the VP1 record supersedes it.
3. [VP1 probe record](2026-10-10-Breaking-VP1-Probe.md), the Results table.
4. [Breaking reference record](2026-10-10-Breaking-Reference.md), "What changed" and "Notes".
5. `Reference/minecraft-blocks-26.3/README.md`, the rows for `facts/block_sounds.csv`, `block_particles.json`, `assets/minecraft/sounds*` and the destroy stages.
6. Code, by ranges only:
   - `Source/Unnamed/Foundation/FoundationInteractionView.cpp`: 106-182 (BeginPlay, pools), 412-431 (Burst), 446-492 (HandleEvents), 527-554 (UpdateEffects), 556-645 (DrawBox, DrawSelection; the outline is 588-612), 647-677 (DrawCracks), 684-808 (UpdateHeldItem; the swing is 795-807), 810-851 (TickComponent; mining is 830-848);
   - `FoundationInteractionView.h` (190 lines: members, pools, `MaxParticles = 96`);
   - `FoundationInteraction.h`: lines 69-110 (`bMining`, `MiningCell`, `MiningProgress`, `MiningDelay`, `PressSerial`, `Step`, `Target`, `MiningDelaySteps = 5`);
   - `FoundationAuthority.h`: 217-246 (`EFoundationEventKind`, `FFoundationEvent`).
7. `Scripts/Editor/CreateBlockMaterials.py` by ranges: how block materials read texture arrays and per-instance custom data. `Scripts/Editor/ImportOreLooks.py`: header and verify pattern, for a new additive import.

## 3. Working agreement (this lane)

- **Manager:** session `session_017HhaP4tdUTpPSPC4igAAce`, "Manager (2026-10-10 day)". Report there with `send_message`, and end the turn where you would wait for Jesus. If Jesus writes to you directly, his instructions win.
- **Approvals:** the manager approves plans within Jesus's picks, and the local commit after a clean pre-commit review (Sonnet 5.5, diff only, split over about 1,500 lines) and passing suites. Re-run every suite the review fixes touch. Runtime changes need the end-to-end checks after the last code change: `node create-world.mjs --editor`, the Playground probe `TestFoundationRuntime.ps1 -Editor -FrameRates 60`, and the flight (`CaptureVoxel.ps1 -Flight`).
- **Reports:** short. Report A, before committing, adds `git diff --cached --name-status`, suite counts after the review fixes, the source of any changed pinned count or golden, and any change to a hash-checked reference. Report B, after committing, gives hashes, the handoff path and the short prompt.
- **Look calls:** the final picks are Jesus's, through the manager. Send captures with numbers, a control pair proving each switch took effect (the control frame not black, saturated or empty), and a recommendation. Keep every other option one switch away.
- **Switches:** console variables in this lane's own file (`foundation.Break.*`), never `VoxelConsole.cpp`. Default off until Jesus confirms the in-game pack, so today's game stays unchanged. Step 4 then makes his confirmed picks the defaults.
- **Lanes:**
  - Never edit `FoundationInteractionProbe.cpp` (torch T3, items S3), `VoxelConsole.cpp`, `VoxelWorld.cpp`, `Docs/Blocks.md`, the Decoration code, goldens, the worldgen reference or `Docs/VoxelWorld.md`.
  - No CLAUDE.md edits: put questions in your handoff.
  - Never push. Never use bare `git stash`. Never re-record goldens or world hashes; if your work would, stop and report.
- **Approved file list** (report any other file before editing it):
  - new: `FoundationBreakEffects.h/.cpp`, `Source/Unnamed/Tests/FoundationBreakTests.cpp`, `Scripts/Editor/ImportBreakAssets.py` (+ `BreakImportData.py`), `Content/Breaking/*`, `Docs/Breaking.md`, and this lane's records and handoff;
  - changed: `FoundationInteractionView.h/.cpp`, `FoundationTourProbe.cpp`, `Scripts/TestFoundationRuntime.ps1`;
  - menu, if Q6 needs it: the `MenuWeb/Patches` or `Runtime` route plus `node MenuWeb/AdaptMenu.mjs`, with the files listed first.
- **Hash-checked block reference:** change it only through `FactsDump`/`build_reference.py`. Run both verifiers, and repeat the diff and regeneration proofs. Until this lane merges, it is the only lane changing `minecraft-blocks-26.3`; the sturdy-face follow-up waits.
- **Game code:** behaviour and numbers come from data, assets, the wiki and black-box runs. Never `javap -c`, decompile, or reflect into private members.
- **Builds:** after a file restore, touch the sources or build with `-DisableAdaptiveUnity`. Integer codes in float vertex channels stay under 2^15.
- **Timing:** check `Get-Process java, javaw, UnrealEditor*` before any timed run, and log the machine state. Timed gates need the manager's quiet window. On "QUIET WINDOW HOLD", finish the step and reply "held". Jesus may play test builds; anything that takes over the screen goes to the manager first.
- **Budget:** measure the lead after every build, test run or subagent result with `bash C:\dev\sandbox\break-ideas\measure.sh` (edit `S=` to your session id; for a session started in `C:\dev` it copies the transcript to a temp config dir). From about 300k, and every 200k after, send a case or wrap up. Subagents use 5.5 models, exact paths and line ranges, and stop near 150k; follow-ups go to a fresh agent.

## 4. Settled decisions and the exact prototype spec

**Jesus's answers (ideas doc section 10):**
- Q1 (a), vanilla assets through the reference: done.
- Q2 (a), digest-only `DA_Blocks` re-stamp: done.
- Q3 (a), break and hit sounds only.
- Q5 (a), VP1: done.
- Q6 (a), the menu's saved High Contrast and Particles prefs drive O2 and P3.
- Q7 (a), the model's own particle texture.

**His first-prototype picks (mock-up, 2026-10-10 afternoon):**

| Area | Pick | Exact spec |
|---|---|---|
| Outline | Thinner (design) | Black at 30% opacity, 1 px, constant in screen pixels. One box per outline box, as today. With the menu's High Contrast pref on, use vanilla's measured High Contrast instead: an opaque core rgb(87, 255, 225), 3 px, with a 3 px opaque black halo each side. For reference, vanilla's normal outline is black at 40%, 2-3 px |
| Cracks | C6 Carved + smooth growth (C2) | Vanilla's 10 stage masks (the reference's `destroy_stage_N.png`, alpha > 127) made into an engraved look. Crack texel grey 60. The texel just right of or below a crack (lit side, light from the up-left in face texture space) grey 170. The texel just left of or above it (shadow side) grey 90; shadow wins where both apply. Everything else unchanged. Blend multiply 2× (out = 2·grey·dst; 128 = no change; measured in vanilla). 16 texels per block face, projected per face in world space (a slab shows half). Stage = clamp(floor(progress × 10), 0, 9), visible from the first mining tick. Smooth: sample stage s and s+1 and fade in s+1's new texels by fract(progress × 10). Cracks also show over transparent texels (vanilla does). Reset on release |
| Particles | P1 textured + P2 hit + collision + P4 spray, amount from the menu pref (Jesus chose Minimal in the mock-up) | Break: 64 × amount (All 1, Decreased 0.67, Minimal 0.33) camera-facing quads from a 4×4×4 grid inside each outline box. Size 0.1 block. Each shows a random 4×4-texel patch of the state's particle texture (`block_particles.json`; tint like the block). Velocity = (point − centre) × 1.5 + uniform ±0.1, plus 1 block/s up; P4 adds 1.5 along the mined face's normal and 0.5 away from the camera. Gravity 16 blocks/s². Life 1.5-2.3 s; they land and lie on solid cells (collision), then vanish. Hit (P2): 1 per mining tick at the hit point, size 0.06, 0.4 out of the face (0.6 with spray), then gravity; off at Minimal. Vanilla measured: about 0.1 block, lying on the ground, gone at 1.5-2.3 s, 9 / 6 / 3 left at 0.8 s. The count at spawn and the hit rate are estimates |
| Sounds | S2 (vanilla + pitch variety) | Per block sound type (`facts/block_sounds.csv`, 95 types). Break: `block.<type>.break` at volume 1.0 and pitch 0.8, times the file's own volume and pitch (`sounds.json`) and the type's pitch (metal 1.5). Hit: `block.<type>.hit` at 0.25 and 0.5, ± a random 3% on pitch, at most 2 overlapping hit voices. A random file per play, linear attenuation over 16 blocks. Pops play the break sound. Water is silent (`intentionally_empty`) |
| Swing | W3 synced hits (design) | A 6-tick (0.3 s) swing per press, repeating while mining. The hit sound and hit particles fire at each swing's contact (tick 3 of 6), so hits come every 6 ticks, not vanilla's measured 4; the switch keeps W1/W2's 4-tick cadence one step away. Count in reference steps (`Interaction.Step`), not frame time. The pose stays today's until items S4. Expose a swing clock (start step, duration, phase) that S4 can read |
| Break pop | X1 on (design) | At the break, a copy of the block (its mesh and custom data, as the held block draws it) scales uniformly to 0 over 80 ms (ease-in, scale = 1 − t²) while the particles spawn. The world removes the block at the commit, as today |
| Post-break delay | 6 ticks | Owned by the items lane (S5: `FFoundationInteraction::MiningDelaySteps = 5`, wiki says 6). Do not change it here; it is noted for S5 (section 10) |

## 5. How I'd build it (suggested; the plan goes to the manager first)

1. **Assets.** `Scripts/Editor/ImportBreakAssets.py` (+ a stdlib `BreakImportData.py`), additive and verifying like `ImportOreLooks.py`. It never touches `DA_Blocks`.
   - **Crack array:** `T_BreakCracks`, a 16×16×10 array of the carved stages computed from the reference PNGs with section 4's rule (nearest filtering, no mips), plus the plain vanilla stages as layers 10-19 to keep C1 one switch away.
   - **Sounds:** a `USoundWave` per needed ogg. UE 5.8's ogg import is unverified: if `AssetImportTask` refuses `.ogg`, decode to WAV in the import script (record the route).
   - **Attenuation:** linear over 1,600 cm.
   - **Data asset:** a small `UFoundationBreakAssets` data asset (C++ class in `FoundationBreakEffects.h`) holding:
     - per sound type, the break and hit files with their volume and pitch;
     - per block id, its type;
     - per block, the default particle texture name, which resolves at runtime to `DA_Blocks.Textures` by name → `FirstLayer`.
   - **`shelf_mushroom_particle`:** not in the texture array. Fall back to `shelf_mushroom_stage0` and record it.
2. **Effects.** `FoundationBreakEffects.h/.cpp`: a component or helper owned by the view, with the switches `foundation.Break.Outline` (0 today / 1 vanilla / 2 thinner / 3 none), `.Cracks` (0 today / 1 vanilla / 2 carved), `.CrackSmooth`, `.Particles` (0 today / 1 textured), `.ParticleCollision`, `.ParticleSpray`, `.Sounds` (0 off / 1 vanilla / 2 + pitch variety), `.Swing` (0 today / 1 vanilla 4-tick hits / 2 synced) and `.Pop`.
   - **The view:** calls in from `HandleEvents` (Broken/Popped → particles, sound, pop), the mining branch of `TickComponent` (cracks, hit cadence), `DrawSelection` (outline) and `UpdateHeldItem` (swing clock). With every switch at 0, the old paths run unchanged.
   - **Cracks:** a mesh overlay of the target's own shape. Use the cube, slab or layer kind, or the model parts through `GetBlockMesh`, scaled about 1.002 with a depth bias. The material is translucent, Modulate and unlit, projected per face in world space. Created by an editor script (Python, like `CreateBlockMaterials.py`), so the materials are reproducible.
   - **Particles:** one `UInstancedStaticMeshComponent` of quads facing the camera, rotated on the CPU per frame. Its custom data holds the layer and UV patch. The material is lit with an up normal, so caves stay dark. Collision is a cell query against `Authority.World` (it works in both worlds).
   - **Outline:** line thickness recomputed per frame from the distance, for constant pixels. Check whether `ULineBatchComponent` honours alpha; if not, use a thin translucent mesh outline.
   - **Menu prefs (Q6):** read the High Contrast and Particles prefs from `UMenuPreferences`. Find where it exposes them natively; if they are JSON-only, list the menu files before editing.
3. **Probe and captures.**
   - Extend `FoundationTourProbe.cpp` (or add a `break` suite in `TestFoundationRuntime.ps1`): a fixed row (stone, grass block, oak log, glass, oak leaves, sand, a bottom slab, a dandelion).
   - Shots at progress 0.05, 0.35, 0.65 and 0.95, and at +16, +50, +150 and +400 ms after the break. Freeze time with dilation 0 to take every switch's frame of the same state.
   - TAA off for diffs (`r.AntiAliasingMethod 0,r.ScreenPercentage 100`), on for the look.
   - Control pairs, contact sheets, `LastViewMs` mean and p99, and the translucency pass in `ProfileGPU`.
   - A sound event log (time, event, file, volume, pitch) for the cadence test.
4. **Tests** (`Unnamed.Foundation.Break.*`, pure functions):
   - the stage mapping;
   - the carved texels against the reference PNGs (a few known texels);
   - the particle spawn count per box and amount;
   - sound selection (block → type → event → file, volume and pitch, metal 1.5);
   - the hit cadence (W1 every 4 steps, W3 every 6 at tick 3) at 30, 60 and 144 FPS;
   - the pop's scale curve.

   Mutation-check any bound loosened from this spec.
5. **Pack for Jesus:**
   - per pick, today against the pick (control pair) on the fixed row;
   - a contact sheet;
   - the sound event list and a loopback recording if he wants one;
   - the measured cost (`LastViewMs`, GPU ms) with the machine state.

   He confirms in one pack before anything becomes the default (step 4).

## 6. Code map

| What | Where |
|---|---|
| Outline today | `FoundationInteractionView.cpp:588-612`: `GetOutlineBoxes`, `DrawBox(Box.ExpandBy(.3), (.02,.02,.02), .9f)` |
| Cracks today | `:647-677` `DrawCracks`, line polylines. Faces after the first re-pattern at every stage, because one `FRandomStream` runs across the faces |
| Mining state per frame | `:830-848`: `Pawn->Interaction.bMining`, `MiningProgress`, `MiningCell`; the darkening overlay for untextured fallback blocks only (`:833`) |
| Break and pop events | `:446-462`: `Broken`/`Popped` → `Burst(Cell centre, Block.Color, 10, 12, 260, Serial)` |
| Particle pool | `:167-168` (96 `UStaticMeshComponent` cubes) and `:527-542` (update). Also used by critical hits (`:480`) and tool breaks (`:485`), which keep it |
| Swing | `:795-807`: `PressSerial` → `SwingAge`; a 0.25 s sine, plus an \|sin\| bob with a 0.3 s period while mining |
| Held block (model for the pop copy) | `:739-789`: `GetModelParts`, `GetBlockMesh`, `MakeCustomData`, `LayerMaterials` |
| Mining timing | `FoundationInteraction.cpp:145-256` (StepAttack, StartMining, ContinueMining; `MiningDelaySteps = 5`). Items S5 owns it |
| Generated world | `VoxelWorld.cpp:212-214` creates the view with `bDrawBlocks = false` (torch lane's file: read only). Effects live in the view, so both worlds get them |
| Particle colour today | `FoundationBlockRegistry.cpp:217`: `Block.Color = ParticleColor` (the averaged top face) |
| Reference facts | `facts/block_sounds.csv` (618 rows: id, volume, pitch, break…fall), `block_particles.json` (per block a default plus differing states; `textures_outside_textures_json`), `assets/minecraft/sounds.json`, `assets/minecraft/sounds/**`, `assets/minecraft/textures/block/destroy_stage_*.png` |

## 7. Known issues, with "when"

- **Research inputs:** the block reference's research inputs live in `C:\dev\sandbox\t2work\research`. When: before deleting `t2work` or rebuilding the reference (see the reference record).
- **`shelf_mushroom_particle`:** not in `T_BlockTextures`. When: the P1 import. Fall back to stage0, or add a tiny extra array.
- **UE 5.8 ogg import:** unverified. When: `ImportBreakAssets.py`.
- **Line batch alpha** (Thinner needs 30%): unverified. When: the outline switch.
- **Modulate above 1** (the 170 and 155 greys brighten): unverified in UE's translucent Modulate. When: the crack material. If it clamps, use a `DestColor` workaround or a two-pass darken/brighten.
- **Not measured in VP1:** particle count at spawn, hit-particle rate, crack stages on slab, glass and leaves, dandelion cracks. When: only if Jesus asks.
  - The harness is ready: run the steps with `--skip`. The client run needs Jesus away; the monitor and speakers must be on for sound or swing.
- **Sandbox only:** the mock-up and the VP1 harness. When: before a sandbox cleanup, keep `C:\dev\sandbox\break-mockup` and `C:\dev\sandbox\break-vp1` if Jesus wants to re-run them.
- **W3:** moves the hit sound from vanilla's measured 4 ticks to 6. It's by design (Jesus's pick); keep W1 one switch away.

## 8. What worked and what failed

- **Worked:**
  - Small single-purpose subagents with exact paths. Peaks were 57-124k, none over 150k.
  - A diff-only review that caught real verifier gaps: a deleted ogg undetected, a silent pass without the index.
  - Regenerating the reference into an empty folder as the final proof.
  - The browser mock-up as the look call before any UE code. It was fast, and Jesus picked from it.
- **Failed, then fixed:**
  - VP1 run 1: the 26.3 server's default whitelist.
  - VP1 run 2: black desktop captures with the display asleep. The game's F2 screenshots fixed it.
  - F1 hides the outline.
  - `soundcard` and `dxcam` clash in one process.
  - Asking an agent to "skip on missing particle textures" without a fallback: it stopped at `shelf_mushroom_particle` and the lead decided.
  - `cd` in Bash commands moved the session's working directory: use subshells `( cd … )` or absolute paths.

## 9. Token figures

- **Lead:** about 444k at this handoff (MeasureAgentUsage).
- **Subagent peaks (Sonnet 5.5 unless noted):**
  - research: sound types 57k, assets 124k (Haiku 5.5), wiki 75k;
  - mock-up: 109k, the update 62k, crack styles 97k;
  - reference: 99k, finish 72k, fixes 82k;
  - review: 58k;
  - VP1: harness 88k, analysis 110k.
- A fresh chat should spend about 100k reading section 2.

## 10. Open questions (with options and a recommendation)

1. **P3 default:** Jesus chose Minimal in the mock-up, while Q6 says the menu's Particles pref drives it.
   - (a) Keep the pref driving it, and Jesus selects Minimal in Options. **Recommended**: no saved-default change.
   - (b) Change the menu's default to Minimal, which changes everyone's saved-settings default.
2. **Post-break delay 6 (Jesus's pick):**
   - (a) Pass it to items S5, which already plans "6 ticks per the wiki" with probe P5. **Recommended.**
   - (b) A switch in this lane. That would cross the items line.
3. **Sound volume and menu audio prefs** (Master, Blocks): their effects are pending in the menu.
   - (a) Play at the reference volumes now and wire the prefs later with the audio work. **Recommended.**
   - (b) Scale by the saved Blocks slider now, if it is exposed natively.
4. **The swing pose** is S4's. The swing clock is the interface.
   - (a) Keep today's pose with the new timing. **Recommended.**
   - (b) Wait for S4.

## Short prompt (for the next chat)

> Unnamed break lane, UE prototype. Read `C:\dev\projects\Unnamed-break\Docs\Records\2026-10-10-Breaking-Handoff-Prototype.md` whole, then its section 2 reading list. Then send the manager (session_017HhaP4tdUTpPSPC4igAAce) a plan for Jesus's first-prototype picks (section 4) behind default-off `foundation.Break.*` switches, with the file list against the approved list and the numbers hashes depend on (none expected). Wait for the go. Work only in that worktree on `feature/voxel-break`; never push; report to the manager.
