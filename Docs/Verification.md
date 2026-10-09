# Verification

## Voxel world: phase 4 session 1, worldgen data and reader - October 8, 2026

- **Builds:** the editor build and a `-DisableAdaptiveUnity` build are clean, with no warnings in project code.
- **Reference:** `verify_reference.py`: 122 files hashed, 116 compared with the jar, 580 references resolved, 9 splines with 4,608 samples. Given a jar without the data, `build_reference.py` stops and leaves `data/` untouched.
- **Native tests:**
  - `Scripts\TestVoxel.ps1` 29/29 (`Tests/Evidence/2026-10-08/VoxelAutomation-20261008-235254/`);
  - every native suite 76/76, 73 before (`Tests/Evidence/2026-10-08/AllNative-20261008-235326/`);
  - zeroing spline derivatives in `BuildSpline` and negating the reader's constants fail `Worldgen.Splines` (6 errors), `Worldgen.RoundTrip` (34) and `Worldgen.Presets` (1) (`Tests/Evidence/2026-10-08/VoxelAutomation-20261008-232628/`).
- **Measurements:** each preset reads in about 8 ms in the editor. `FVoxelSpline` matches Mojang's `CubicSpline` within 9.5e-7 on 4,608 samples.
- **Other suites:** JavaScript units 57/57; `source-check.mjs` passes (938 assets unchanged).
- **Not run**, because no runtime path changed (nothing calls the reader yet): the Playground probe, `create-world.mjs` and `foundation-menu.mjs`.

Details: [Voxel world, Worldgen data](VoxelWorld.md#worldgen-data) and the [session 2 handoff](Records/2026-10-08-Voxel-Handoff-Phase4-Session2.md).

## Voxel world: streaming and editing (phase 3) - October 8, 2026

- **Builds:** the editor build and a `-DisableAdaptiveUnity` build are clean, with no warnings in project code.
- **Native tests:**
  - `Scripts\TestVoxel.ps1` 26/26 (`Tests/Evidence/2026-10-08/VoxelAutomation-20261008-210609/`);
  - every native suite 73/73, 68 before (`Tests/Evidence/2026-10-08/AllNative-20261008-211040/`);
  - a deliberate off-by-one in `GetSectionsTouching` fails `Unnamed.Voxel.Edit.Sections` (13 errors) and `Unnamed.Voxel.Edit.Remesh` (11) (`Tests/Evidence/2026-10-08/VoxelAutomation-20261008-210535/`).
- **Acceptance flight** at view radius 12 (`Tests/Evidence/2026-10-08/VoxelFlight-20261008-200725/`, 16,736 frames over 99.9 s):
  - holes: none;
  - chunk work on the game thread: worst 1.81 ms, 0 frames over the 2 ms budget;
  - memory over laps 2 and 3: process 3230 -> 3238 MB, column data 7.11 MB at the end of each lap;
  - GPU mean 3.25 ms, frame mean 6.0 ms.
- **Editing:** 166 cells in 4 columns around a column corner are back on screen 22 to 30 ms (2 frames) after the edit (`Tests/Evidence/2026-10-08/VoxelShots-20261008-210841/` and `-210934/`).
- **Foundation and menu**, because phase 3 changes Foundation files:
  - the Playground runtime probe at 60 FPS: 103 checks, 0 failures (`Tests/Evidence/2026-10-08/Playground-all/Runtime-20261008-202349/`);
  - `Tests/create-world.mjs --editor` 32/32 (`Tests/Evidence/2026-10-08/Worlds/Create-1791508280714/`);
  - `Tests/foundation-menu.mjs --editor` 36/36 (`Tests/Evidence/2026-10-08/Foundation/Menu-1791508711248/`);
  - JavaScript units 57/57, and 938 reference assets unchanged.
- **After the pre-commit review's apply-gate fix**, which touches only `UVoxelWorldSubsystem`:
  - `Scripts\TestVoxel.ps1` 26/26 (`Tests/Evidence/2026-10-08/VoxelAutomation-20261008-225350/`);
  - the acceptance flight again (`Tests/Evidence/2026-10-08/VoxelFlight-20261008-225439/`, 13,106 frames): no holes, worst chunk work 1.88 ms, 0 frames over budget, memory flat over laps 2 and 3 (process 3365 -> 3372 MB, columns 7.11 MB);
  - one first-view frame reached 3.31 ms, because a single column took 2.01 ms (estimate 0.13 ms) to get a new chunk component; either gate lets a column with that estimate through;
  - a repeat capture's first view stayed within budget, worst 1.86 ms (`Tests/Evidence/2026-10-08/VoxelShots-20261008-225725/`).

Details, evidence and gaps: [phase 3 record](Records/2026-10-08-Voxel-Phase3.md).

## Voxel world: lighting (step 2L) and Create New World (phase 2W) - October 8, 2026

- **Step 2L** (Lumen cards for chunks, the generated map's sky light 3):
  - voxel tests 20/20 and native 66/66;
  - shaded slopes measure 24 to 47 of 255;
  - evidence: `Tests/Evidence/2026-10-08/VoxelShots-20261008-160917/` and `VoxelLightingCompare-20261008-161235/`.
- **Phase 2W** (saved worlds; Peaceful in the vitals):
  - voxel tests 21/21, native 68/68, JavaScript units 57/57, and 938 reference assets unchanged;
  - `Tests/create-world.mjs --editor` 32/32 (create, list, rename, re-create, play, delete; Superflat in Creative on Peaceful);
  - `Tests/foundation-menu.mjs --editor` 36/36;
  - the Playground runtime probe at 60 FPS: 103 checks, 0 failures (`Tests/Evidence/2026-10-08/Playground-all/Runtime-20261008-171232/`, kept local: 22 MB of HUD captures).

Details, evidence and gaps: [lighting record](Records/2026-10-08-Voxel-Lighting.md) and [phase 2W record](Records/2026-10-08-Voxel-Phase2W.md).

## Blocks - October 8, 2026

Native `Automation RunTests Unnamed` 46/46; runtime probe (`-FrameRates 60 -HudCaptures`) 139 checks, 0 failures; screenshot tour 24 shots, 0 failures, including the block gallery and creative inventory. Evidence: `Tests/Evidence/2026-10-08/Blocks-20261008/` (`Native-6`, `Runtime-all-3`, `Tour-5`). Details and gaps: [blocks record](Records/2026-10-08-Blocks.md).

## Playground - October 8, 2026

Pending: see `Tests/Evidence/2026-10-08/Playground-*`. No Playground result is recorded here yet. Layout, creation and test commands are in [Playground](Playground.md); the record is [Records/2026-10-08-Playground.md](Records/2026-10-08-Playground.md).

```powershell
& Scripts\TestFoundationRuntime.ps1 -Editor -FrameRates 60                     # Playground: movement and interaction suites
& Scripts\TestFoundationRuntime.ps1 -Editor -Suite interaction -FrameRates 60  # add -Preset LucidFluid for the other preset
Set-Location Tests; node foundation-menu.mjs --editor                          # CEF + native end to end
```

Native suites `Unnamed.Worlds.*` and `Unnamed.Foundation.*` run with the commands in [CLAUDE.md](../CLAUDE.md).

## Dummy Block interaction - October 8, 2026

The [interaction record](Records/2026-10-08-Interaction.md) has the rules, presets, decisions, defects, evidence paths and remaining gaps; evidence is under `Tests/Evidence/2026-10-08/Interaction-20261008/`. The editor build has no warnings. Native automation is 37/37 (nine new `Unnamed.Foundation.Interaction.*` suites) and JavaScript units 53/53, and 938 reference assets are unchanged. The fresh candidate `Packages/InteractionDevelopment20261008-r3/Windows/Unnamed.exe` cooks 738 packages with no errors or warnings; `Content/` is hash-identical before and after packaging. Packaged, the Interaction Lab passes 61/61 at 30/60/120/144 FPS caps and at a 15 FPS stress cap, the full LucidFluid run 61/61 at all four caps, the Foundation regression with HUD captures 74/74 at 30/60/120 and CEF capture/persistence/travel 33/33. Interaction logic costs at most 0.028 ms at p95 per engine update (budget 0.5 ms); 4,096 cubes build in about 4.3 ms and 1,000 place/break cycles run in about 2 ms; whole-frame p99 in the Foundation course is 18.17 ms against 18.13 ms for `VitalsDevelopment20261007-r2`.

OS-level mouse and keyboard play (synthesized with Win32 `SendInput`, not physical devices or a human player) placed, jump-stacked, mined, fought and switched presets in the packaged lab, and showed LucidFluid placing under a falling player where VanillaReference's held repeat misses. It also found that closing the game window during play crashed, in this and the previous candidate; that is fixed and verified in both labs. Block edits are session-only and nothing is networked.

## Player vitals (HUD milestone 2) - October 7, 2026

The [vitals record](Records/2026-10-07-Vitals.md) contains the rules, sources, evidence paths and remaining gaps; evidence is under `Tests/Evidence/2026-10-07/Vitals-20261007-204932/`. The editor build has no warnings. Native automation is 28/28 (six new `Unnamed.Foundation.Vitals.*` suites and `Unnamed.Hud.VitalsAdapter`), HUD suites 7/7 and JavaScript units 53/53, and 938 reference assets are unchanged. In the editor the Foundation regression, milestone-1 HUD captures and 12 vitals cases pass 74/74 at 30, 60 and 120 FPS caps, and CEF capture/persistence/travel passes 30/30. The fresh candidate `Packages/VitalsDevelopment20261007-r2/Windows/Unnamed.exe` cooks 737 packages with no errors or warnings and passes 74/74 at 1920×1080 and at 1280×720 with 30/60/120 FPS caps, plus CEF 30/30; all exits are normal. `Content/` is hash-identical before and after packaging.

Health, food, air, falls, XP, attack recovery, mode and difficulty are real session-only player state; the token hotbar is still labelled **PREVIEW ITEMS**. Effects, Hardcore, Peaceful, equipment, items, the death screen, persistence and multiplayer are not implemented.

## HUD milestone 1 - October 7, 2026

Current candidate: `Packages/HudDevelopment20261007-r2/Windows/Unnamed.exe`. The [HUD record](Records/2026-10-07-Hud.md) contains all evidence paths, hashes, real-versus-preview ownership and remaining gaps. Import reruns leave all 194 textures plus registry unchanged; original-reference verification passes. Native automation is 21/21, final HUD suites 6/6, input suites 3/3 and JavaScript units 53/53. Editor regression passes at 30/60/120 FPS caps; CEF capture/persistence/travel passes 30/30 in both editor and package. Packaged HUD checks pass 62/62 at 720p, 1080p, 1440p, 4K and ultrawide, with actual PNG dimensions and exact source-pixel blocks verified.

Resources, XP, attack and mode are labelled preview fixtures; no gameplay backend or vanilla visual equivalence is claimed. Normal runs exit cleanly. Opt-in tracing yields usable Insights data but returns 777003 during shutdown and is not accepted as a clean-exit pass. Historical checkpoints below remain unchanged.

## Code-review cleanup - October 7, 2026

Current candidate: `Packages/CodeReviewDevelopment20261007/Windows/Unnamed.exe`, packaged from branch `refactor/code-review-cleanup` with the three maps from `MapsToCook` (542 cooked packages, 0 errors). Every cleanup step was verified before its commit; the final state passed:

| Verification | Result | Evidence |
|---|---|---|
| Editor build | 0 errors, 0 warnings | - |
| Native automation (`Unnamed.*`) | 15/15 (13 at the start; new `Unnamed.Worlds.Catalog`, `Unnamed.Project.AtomicSave`) | `Scripts/TestMenu.ps1`, `TestInput.ps1` or `RunTests Unnamed` |
| Node unit tests (`npm test`) | 53/53 (44 at the start) | - |
| Reference, mirrored assets, generated script | 938 assets unchanged, hash unchanged, syntax valid | `node Tests/source-check.mjs` |
| Foundation probe, packaged | 38/38 at 30, 60 and 120 FPS caps (29.6 / 58.9 / 117.1 FPS) | `Tests/Evidence/2026-10-07/Foundation/CodeReview-Runtime-Package/` |
| CEF end to end, packaged | 30/30: capture, relaunch persistence, three travel paths, return, two normal exits | `Tests/Evidence/2026-10-07/Foundation/CodeReview-Menu-Package/results.json` |
| Foundation probe and end to end, editor `-game` | 38/38 and 30/30 after every behavioral step | session scratch runs |

The menu generator split kept `index.html` byte-identical; later changes to the page were covered by the Node and CEF suites. The fullscreen-key fix is covered by a native request test; the actual window-mode change still needs a visible standalone window. Physical OS input, Shipping and other platforms remain untested, as before. See [the record](Records/2026-10-07-Code-Review-Cleanup.md).
## Super Flat - October 7, 2026

`Packages/SuperFlatDevelopment/Windows/Unnamed.exe` passed editor/package builds, authored-level validation, the native routing/isolation suite, 44 browser unit tests and 32 packaged integration checks. Play, double-click and Enter each loaded the actual level, passed nine ground traces and a spawn-clearance check, and returned to a working menu with retained settings. Native rendering was captured and visually inspected. Physical input and embedded PIE were not verified in this pass. This fixed package does not certify the concurrent FPS task's continuing changes. See [the dated record](Records/2026-10-07-Super-Flat.md). The world is retired; see [Playground](Playground.md).

## Schema import fix - October 7, 2026

Current candidate: Packages/MenuPreferencesV2Development/Windows/Unnamed.exe. The raw schema now uses a non-importable extension. The editor/game were rebuilt; eight native suites, 39 JavaScript tests and 126 packaged preference checks passed. A native factory probe confirms that none of its 42 import factories register the new extension. HTML and schema contents are unchanged. [The fix record](Records/2026-10-07-Schema-Import-Fix.md) explains the import prompt, current hashes and preserved earlier evidence.

## Menu preference persistence - October 7, 2026

Current candidate: Packages/MenuPreferencesV1Development/Windows/Unnamed.exe. Eight native suites, 39 JavaScript unit tests, 126 packaged preference checks and 50 existing input/API regression checks passed. At each of 1280x720 and 1920x1080, all 369 menu checks and 37 video checks passed. The new 101 scalar preferences and four additional domains restore across document reload and separate process launches. Existing video/input stores are preserved. [The dated record](Records/2026-10-07-Menu-Preferences.md) contains hashes, reports, preservation and remaining acceptance limits. The older candidates below remain historical evidence.

## Input v2 capture fix — October 7, 2026

The current candidate is `Packages/MenuInputCrouchCDevelopment/Windows/Unnamed.exe`. Three native input suites, 31 browser unit tests and 50 packaged input/API/persistence checks passed. Defaults are C crouch and Left Shift sprint. The Windows capture failure was reproduced in the user's old preview and fixed by handling Unreal CEF events without native scan codes. The final Windows input retest awaits a user-dismissed Firewall dialog; this is not counted as a pass. [The follow-up record](Records/2026-10-07-Input-v2.md) contains current instructions, hashes, evidence and remaining acceptance gates. Earlier checkpoints below describe earlier preserved packages.

## Current keybindings foundation — October 7, 2026

The current candidate is `Packages/MenuInputFoundationDevelopment/Windows/Unnamed.exe`. Five native suites, 30 browser unit tests, 38 packaged input/persistence checks, 369 complete menu checks per viewport and 37 video checks per viewport passed. The tested viewports are 1280x720 and 1920x1080. Native input events prove action/intent routing without a character; FPS gameplay and physical OS input remain future gates. [Keybindings](Keybindings.md) and [the dated record](Records/2026-10-07-Keybindings.md) provide exact report paths, hashes, preservation and integration instructions. Earlier sections below are historical checkpoints.

## Current Review Fixes

The five independent-review findings are fixed: dialog draft loss/display-submit mismatch, lost queued edits after failure, unbounded bridge waits, blank server-name acceptance, and shared/mixed capture evidence. Timeout recovery also covers an accepted Quit whose completion never replies. [Records](Records/README.md) contains the concise work record, fixes, file ownership, and known gaps.

Current candidate: `Packages/MenuReviewFixesDevelopment/Windows/Unnamed.exe`. This JavaScript/HTML-only follow-up preserves native source and every non-HTML file from MenuNoRealmsDevelopment. Native build, automation, and editor-safety evidence is inherited unchanged, not newly executed.

| Verification | Result | Evidence |
|---|---|---|
| JavaScript units | 25/25 | `Tests/Evidence/2026-10-07/ReviewFixesUnitTests.tap` |
| Draft/validation packaged regressions | 24/24 at both sizes | `Tests/Evidence/2026-10-07/Captures/review-accepted-drafts-*/results.json` |
| Full menu sweeps | 423/423 at both sizes | `Tests/Evidence/2026-10-07/Captures/review-accepted-packaged-*/results.json` |
| Focused native video/presentation/reset | 37/37 at both sizes | `Tests/Evidence/2026-10-07/Captures/review-accepted-video-*/results.json` |
| Normal quit, relaunch persistence, failed-save retry | 16/16 | `Tests/Evidence/2026-10-07/ReviewFixesFinalQuitResults.json` |
| Reference/assets/syntax | Passed; 938 original/mirrored assets unchanged | `node Tests/source-check.mjs` |
| Actual renderer | D3D12/SM6 | `Tests/Evidence/2026-10-07/ReviewFixesGraphicsAPI.json` |

These suites total 984 passing runtime checks, separate from 25 units. All six diagnostic ownership/cleanup checks passed in `Tests/Evidence/2026-10-07/review-accepted-ValidationResults.json`; successful cases use normal game shutdown, not forced termination. Main-menu and reset captures were visually inspected at both sizes.

The registry now records 697 observed controls, 682 deferred features, and 33 routes. It consumes only the explicit matching `review-accepted-packaged-1920x1080` and `review-accepted-video-1920x1080` captures, including contextual confirmations, restore actions, and selected-pack arrows. Historical shared supplemental files are unchanged and no longer consumed.

`Tests/Evidence/2026-10-07/ReviewFixesArtifactManifest.json` verifies matching working/package HTML, selected capture identity, native package preservation, source/config/map/saves, historical evidence, and unchanged Git index entries. The pre-edit snapshot is `C:/dev/backups/Unnamed/review-fixes-20261007-104750` (1217 files). No staging, commit, reset, or cleanup was performed.

Before-fix runs and failed harness attempts remain historical, not accepted evidence. The corrected baseline suite reproduced 15 failed draft/validation assertions; the corrected candidate passes all 24 focused checks. The original review ZIP/prompt is a preserved pre-fix snapshot. Simulated promise stalls/late replies are unit evidence, not an injected real Unreal/CEF transport outage. Physical input, driver VSync, multi-monitor/DPI, Shipping/other platforms, and future gameplay save contracts remain open.

## Realms Removal Checkpoint

Minecraft Realms, Join Realm, their main-menu entry, invites/news/trial/purchase controls, join-code form, feature CSS and the Online Settings Realms news/invites option/definition are removed. The main menu closes the vacated row. The working interface now has 33 routes; immutable reference material, unused source assets/translations and earlier candidates remain preserved.

- Current candidate: Packages/MenuNoRealmsDevelopment/Windows. This is a presentation-only copy of MenuQuitDevelopment with regenerated physical HTML. Native source and every non-HTML package file are unchanged; no new native compilation or automation run is claimed. Build/automation/editor-safety evidence below is inherited from the identical native checkpoint.
- Complete rendered menu sweeps passed 417/417 at both 1280x720 and 1920x1080 in Tests/Evidence/2026-10-07/Captures/no-realms-packaged-*. These explicitly verify absent Realms routes, main button and retired preference, plus the remaining mock/native workflows. Both main-menu screenshots were visually inspected for the collapsed row, bounds and retained styling.
- Focused video sweeps passed 37/37 at both sizes in Tests/Evidence/2026-10-07/Captures/no-realms-video-*. They retain actual fullscreen/FPS/VSync/layout/blur checks and protected reset behavior. All 15 current JavaScript unit tests passed; Tests/Evidence/2026-10-07/NoRealmsUnitTests.tap records the run, including four new Realms-removal/layout checks.
- Registry regenerated from the current rendered capture: 696 distinct controls, 681 deferred features and 33 routes. Removed Realms items are absent from active inventory/backlog; navigation mocks are not misrepresented as real game/services.
- Current owned-process checks passed 16/16 in Tests/Evidence/2026-10-07/NoRealmsQuitResults.json: normal mouse/Enter exit, queued preference completion, windowed/fullscreen relaunch restoration, unrelated-save preservation and failed-save cancellation/retry. Successful cases did not force-terminate a game. The current rendered/owned-process suites total 924 passing checks; the 15 JavaScript units are separate.
- The running candidate's RHI is D3D12/DirectX 12, verified from actual engine log excerpts in Tests/Evidence/2026-10-07/NoRealmsGraphicsAPI.json. Graphics API remains a deferred mock control in this snapshot; a locked DirectX 12 display was recommended and awaits user confirmation.
- Tests/Evidence/2026-10-07/NoRealmsArtifactManifest.json records current source/package hashes, current runtime reports and preservation of the previous native Quit package. Generate this current manifest with `node Tests/video-artifact-manifest.mjs --no-realms`. Older manifests and their generation modes describe historical checkpoints and should not be regenerated against newer working HTML.
- Dirty work, configuration, map, index and preference data were preserved before edits at C:/dev/backups/Unnamed/no-realms-20261007-093729 (1189 files). The source reference and all 938 mirrored assets still verify. Git index entries, map and configuration remain preserved.
- Docs/Archive/Independent_Menu_Review_Prompt.txt is the read-only independent-review handoff. Docs/Archive/Independent_Menu_Review_Packet.zip contains source and evidence for a ChatGPT reviewer without local access; its entry manifest identifies sanitized config and exclusions. It does not contain executable packages or private save data.

## Quit Game Checkpoint

Quit Game now performs normal standalone shutdown after pending preferences finish and the dedicated file is flushed. The mock Game closed route is removed; 35 working routes remain. Editor Play is protected. See QuitGame.md for ownership, commands and remaining scope.

- Editor/runtime builds and full Win64 Development BuildCookRun succeeded. Tests/Evidence/2026-10-07/QuitBuild.log records build/cook/stage/archive success for Packages/MenuQuitDevelopment.
- Both native automation tests passed in Tests/Evidence/2026-10-07/MenuAutomation-20261007-091836/index.json. All 11 JavaScript unit tests passed, including queue draining, repeated-click rejection, failed-save recovery and editor capability protection.
- Tests/Evidence/2026-10-07/QuitGameResults.json passed 16/16: normal exit code zero and engine shutdown logs through mouse/Enter activation, queued settings written before exit, windowed/fullscreen relaunch restoration, unrelated save preservation, and failed-save cancellation/retry. Successful cases never terminate the game process externally.
- Live editor Play checks passed 4/4 in Tests/Evidence/2026-10-07/QuitEditorSafetyResults.json. The button is disabled, a direct native request is rejected, and Play remains responsive. The owned Play session was then stopped normally and the editor remains open.
- Focused video suites passed 37/37 at both sizes in Tests/Evidence/2026-10-07/Captures/quit-video-*. Complete menu suites passed 426/426 at each size in Tests/Evidence/2026-10-07/Captures/quit-packaged-*. Their main-menu captures were inspected; the menu style and Quit placement remain unchanged.
- Registry regenerated from the current 35-route capture: 707 controls, with normal Quit marked implemented and removed from the deferred backlog. The source reference and all 938 assets still verify.
- Tests/Evidence/2026-10-07/QuitArtifactManifest.json records that checkpoint's source/package hashes, passing reports, unchanged reference/map/config/index entries and preservation of the previous debug-reset candidate. It was generated with `node Tests/video-artifact-manifest.mjs --quit` before the later Realms removal. Earlier manifests/results remain historical checkpoints; failed harness attempts are retained under Tests/Evidence/2026-10-07/IssueRuns.

## Temporary Debug Reset Follow-Up

The user confirmed that Hard Reset is temporary debugging UI and unrelated saves stay protected. The button is centered at the end of Video Settings, with the main menu's 200x20 logical size, textured style and font. No main-menu reset button was added. `BuildMenu.ps1 -ExcludeDebugTools` excludes it from the generated interface.

- Both updated standalone video suites passed 37/37, including placement, main-menu style parity, confirmation/cancellation and the existing five functional settings. Evidence: Tests/Evidence/2026-10-07/Captures/debug-reset-1280x720 and debug-reset-1920x1080; both button screenshots were visually inspected.
- All six adapter/debug-control unit tests passed. Excluded adapter generation was checked, then the normal debugging interface was restored. All 938 source assets and the immutable reference still verify.
- Packages/VideoSettingsDebugDevelopment is a presentation-only candidate. Native binaries and all non-HTML package files match the previously tested VideoSettingsDevelopment package, which remains untouched. No new native compilation or whole-menu sweep is claimed for this styling-only follow-up.
- Tests/Evidence/2026-10-07/DebugResetArtifactManifest.json records that candidate's hashes and preservation checks; it was generated with `node Tests/video-artifact-manifest.mjs --debug-reset` at that checkpoint. The earlier manifest and complete persistence/regression evidence below remain unchanged.

## Functional Video Settings Checkpoint

See VideoSettings.md for scope and reproducible commands. Fullscreen, FPS limit, VSync, GUI scale and blur now have real functionality, save between launches, and support confirmed preference reset.

- Win64 Development editor/runtime builds and BuildCookRun succeeded. The original MenuDevelopment package is preserved; the new package is VideoSettingsDevelopment.
- Strict native request validation passed in Tests/Evidence/2026-10-07/VideoAutomation-20261007-083946/index.json. Four JavaScript adapter tests passed, including coalescing, rejection/readback and editor protection.
- Focused standalone video suites passed 34/34 at both 1280x720 and 1920x1080. They verify actual native modes and restored client dimensions, both shortcuts, measured 30/60 FPS engine cadence, VSync readback, Unlimited, GUI/focus behavior, rendered blur pixels, shared controls, native validation and confirmed reset.
- Whole-menu regression suites passed 429/429 at each profile under Tests/Evidence/2026-10-07/Captures/video-packaged-*. They retain all 36 routes and test that unfinished controls remain session-only while the five functional values survive reload.
- Independent-process persistence/reset checks passed 11/11 in Tests/Evidence/2026-10-07/VideoPersistenceResults.json, including restoration of all five effects after process termination, a second reset/defaults relaunch, and preservation of an unrelated save sentinel.
- Actual 1024x768 standalone pack selection renders at scale 2, with both columns within the viewport. Tests/Evidence/2026-10-07/Captures/video-small-packs.png records the narrower-window guard.
- Original source/map/config/index and prior package were preserved before edits. Reference index SHA-256 and all 938 assets remain unchanged. Final generated HTML and the physical package copy are hash matched after the presentation-only scale guard was staged.
- Tests/Evidence/2026-10-07/VideoArtifactManifest.json records the final package/source hashes, passing reports and verified preservation of the map, project configuration, Git index entries and original package files. Git refreshed index stat metadata, but all paths, stages, modes and blob IDs match the saved baseline. Generate the manifest with `node Tests/video-artifact-manifest.mjs`; the earlier ArtifactManifest.json remains historical evidence.

Native window readiness was found to occur after BeginPlay and is now handled by deferred initialization. Initial native tests also caught JSON numeric coercion; explicit JSON types now reject strings/booleans without mutation. Historical failed attempts are not counted as passing evidence.

## Historical UI Phase

The following records describe the earlier mock-only checkpoint, not the current functional settings implementation.

| Check | Result / evidence |
|---|---|
| Reference/source | Unchanged index SHA-256; all 938 manifest assets and their working copies hash/size verified; adapted JavaScript syntax and forbidden-effect scan pass |
| Unreal build | UnrealBuildTool/UnrealHeaderTool and Win64 Development targets succeeded; no Hot Reload used for class creation |
| Map setup | Editor Python commandlet created and saved the empty 6515-byte L_Menu map; no binary asset hand edits |
| Packaging | BuildCookRun build/cook/stage/archive exit 0; package under Packages/MenuDevelopment/Windows |
| Early vertical slice | Editor-hosted and standalone 1920x1080 passed 17/17 original checks before whole-route investment |
| Complete standalone 1920x1080 | 427/427 checks; Tests/Evidence/2026-10-07/Captures/packaged-1920x1080/results.json |
| Complete standalone 1280x720 | 427/427 checks; Tests/Evidence/2026-10-07/Captures/packaged-1280x720/results.json |
| Final package follow-up | Both profiles rerun after the dynamic GUI mock-range guard; 122/122 per profile in the targeted capture folders |
| Relocated path / different cwd | 18/18 slice checks in `C:\dev\sandbox\Unnamed Menu Path Test\Windows`, launched with cwd C:\dev; Tests/Evidence/2026-10-07/Captures/path-space-1280x720-slice |
| Supplemental | 18/18: selection/disabled states, sample delete/restore, Friends confirmation Cancel, pack ordering, shared settings, Single Biome/Realm endpoints, dynamic GUI mock-range unit check, no window-size effects, unchanged GameUserSettings hash, intact images and live panorama animation; Tests/Evidence/2026-10-07/SupplementResults.json |
| Native editor composition | Real PIE floating window screenshot: Tests/Evidence/2026-10-07/Captures/native-pie/MainWindow.png; not a DOM-only mock or NullRHI rendering claim |
| Registry | 706 observed control IDs plus structured reference definitions; contextual confirmations, Restore Entry and pack arrows included; ControlRegistry.json is authoritative |

Complete smoke covers every source renderer, Create tabs, enabled button actions, mouse navigation, Escape return, keyboard slider changes, mock F11, shared state, Hardcore constraints, rule validation, staged Cancel/Done behavior, packs, language, key capture, scrolling, source service endpoints, bundled credits, resource restrictions and reload reset. Programmatic preparation isolates renderers; route buttons and targeted nested navigation establish their actual transitions separately.

The historical complete runs recorded zero uncaught JavaScript errors and no remote menu-resource requests. Browser console output was forwarded to the dedicated engine log. At that checkpoint no real engine-settings bridge existed. The historical supplemental check hashed the engine GameUserSettings file before and after FOV, Fullscreen, VSync and FPS mock interactions, with the same result on both sides. Normal engine startup/cache activity was not misrepresented as menu persistence.

## Visual Comparison

Main, Options and Video screenshots exist for both exact packaged client profiles. The source's integer scale and logical dimensions were asserted; nonblank pixel diversity and cubemap-loaded status passed. Inspected the actual rendered captures against the supplied JPEG reference captures: composition, control bounds, section order, pixel assets and footer placement are retained. Panorama/splash moving regions are masked conceptually; small text-raster/baseline differences are recorded in KnownDifferences rather than claimed perfect.

## Issue Log

| Symptom | Evidence / cause | Fix | Verification |
|---|---|---|---|
| Host compilation fails at else | UE_LOG macro statement expansion without braces | Explicit branch braces | Full UBT/UHT build succeeded |
| WebGL SecurityError, no panorama texture | CEF console URL/line 178; file-origin images are cross-origin for texture upload | Embed the original six PNG byte streams as data URLs | Real cubemap, nonblank captures and animation pass; security remains enabled |
| Initial route harness reports pack failures and times out | State was changed by its own earlier button sweep; wrong external button label | Compare staged packs against entry snapshot; use exact source label; reload before each suite | Both complete runs 427/427; initial report retained in Tests/Evidence/2026-10-07/IssueRuns |
| Animation readback seemed static | WebGL default framebuffer is not preserved for later toDataURL reads | Compare actual rendered screenshot pixels | Supplemental moving-frame check passes |
| Synthetic native Tab did not establish DOM focus | Slate tool creates native key code 0, unlike OS input | Do not count synthetic tool return as acceptance | Documented physical-input residual check; browser-level keyboard tests pass |

## Remaining Gates

Human physical-input/feel, driver-level VSync/tearing and native Minecraft pixel parity remain open. Other platforms, Shipping builds, injected unsupported-window failures and multi-monitor/DPI coverage were not run. The five implemented settings have the native/runtime/persistence evidence listed above; no other graphics or gameplay acceptance is claimed.

Build logs are retained under Saved/MenuBuild.log and Saved/MenuSmoke-*.log. Source-control checkpoints preserve the source and evidence, not generated caches or credentials. Tests use supported rendered CEF diagnostics; NullRHI was used only for data/map creation, never as visual proof.

The historical UI-phase package sizes/SHA-256 values and run totals are in Tests/Evidence/2026-10-07/ArtifactManifest.json; Saved/MenuBuild-Final.log is its earlier build/cook/stage/archive record. The current review-fix checkpoint is recorded separately in Tests/Evidence/2026-10-07/ReviewFixesArtifactManifest.json and the current section above.
