# Verification

## Executed Evidence

| Check | Result / evidence |
|---|---|
| Reference/source | Unchanged index SHA-256; all 938 manifest assets and their working copies hash/size verified; adapted JavaScript syntax and forbidden-effect scan pass |
| Unreal build | UnrealBuildTool/UnrealHeaderTool and Win64 Development targets succeeded; no Hot Reload used for class creation |
| Map setup | Editor Python commandlet created and saved the empty 6515-byte L_Menu map; no binary asset hand edits |
| Packaging | BuildCookRun build/cook/stage/archive exit 0; package under Packages/MenuDevelopment/Windows |
| Early vertical slice | Editor-hosted and standalone 1920x1080 passed 17/17 original checks before whole-route investment |
| Complete standalone 1920x1080 | 427/427 checks; Tests/Captures/packaged-1920x1080/results.json |
| Complete standalone 1280x720 | 427/427 checks; Tests/Captures/packaged-1280x720/results.json |
| Final package follow-up | Both profiles rerun after the dynamic GUI mock-range guard; 122/122 per profile in the targeted capture folders |
| Relocated path / different cwd | 18/18 slice checks in `C:\dev\sandbox\Unnamed Menu Path Test\Windows`, launched with cwd C:\dev; Tests/Captures/path-space-1280x720-slice |
| Supplemental | 18/18: selection/disabled states, sample delete/restore, Friends confirmation Cancel, pack ordering, shared settings, Single Biome/Realm endpoints, dynamic GUI mock-range unit check, no window-size effects, unchanged GameUserSettings hash, intact images and live panorama animation; Tests/SupplementResults.json |
| Native editor composition | Real PIE floating window screenshot: Tests/Captures/native-pie/MainWindow.png; not a DOM-only mock or NullRHI rendering claim |
| Registry | 706 observed control IDs plus structured reference definitions; contextual confirmations, Restore Entry and pack arrows included; ControlRegistry.json is authoritative |

Complete smoke covers every source renderer, Create tabs, enabled button actions, mouse navigation, Escape return, keyboard slider changes, mock F11, shared state, Hardcore constraints, rule validation, staged Cancel/Done behavior, packs, language, key capture, scrolling, source service endpoints, bundled credits, resource restrictions and reload reset. Programmatic preparation isolates renderers; route buttons and targeted nested navigation establish their actual transitions separately.

The complete runs record zero uncaught JavaScript errors and no remote menu-resource requests. Browser console output is also forwarded to the dedicated engine log. No real engine-settings bridge exists in source. A supplemental check hashes the engine GameUserSettings file before and after FOV, Fullscreen, VSync and FPS mock interactions, with the same result on both sides. Normal engine startup/cache activity is not misrepresented as menu persistence.

## Visual Comparison

Main, Options and Video screenshots exist for both exact packaged client profiles. The source's integer scale and logical dimensions were asserted; nonblank pixel diversity and cubemap-loaded status passed. Inspected the actual rendered captures against the supplied JPEG reference captures: composition, control bounds, section order, pixel assets and footer placement are retained. Panorama/splash moving regions are masked conceptually; small text-raster/baseline differences are recorded in KnownDifferences rather than claimed perfect.

## Issue Log

| Symptom | Evidence / cause | Fix | Verification |
|---|---|---|---|
| Host compilation fails at else | UE_LOG macro statement expansion without braces | Explicit branch braces | Full UBT/UHT build succeeded |
| WebGL SecurityError, no panorama texture | CEF console URL/line 178; file-origin images are cross-origin for texture upload | Embed the original six PNG byte streams as data URLs | Real cubemap, nonblank captures and animation pass; security remains enabled |
| Initial route harness reports pack failures and times out | State was changed by its own earlier button sweep; wrong external button label | Compare staged packs against entry snapshot; use exact source label; reload before each suite | Both complete runs 427/427; initial report retained in Tests/IssueRuns |
| Animation readback seemed static | WebGL default framebuffer is not preserved for later toDataURL reads | Compare actual rendered screenshot pixels | Supplemental moving-frame check passes |
| Synthetic native Tab did not establish DOM focus | Slate tool creates native key code 0, unlike OS input | Do not count synthetic tool return as acceptance | Documented physical-input residual check; browser-level keyboard tests pass |

## Remaining Gates

Human physical-input/feel and native Minecraft pixel parity remain open. Other platforms, Shipping builds and multi-monitor/DPI coverage were not run. No real gameplay or functional settings acceptance is claimed. The next separately authorized real feature can be Fullscreen only, following FunctionalBacklog.md.

Build logs are retained under Saved/MenuBuild.log and Saved/MenuSmoke-*.log. Source-control checkpoints preserve the source and evidence, not generated caches or credentials. Tests use supported rendered CEF diagnostics; NullRHI was used only for data/map creation, never as visual proof.

The final package file sizes/SHA-256 values and executed run totals are in Tests/ArtifactManifest.json. Its HTML hash is checked against the adapted working file. Final rebuild output is retained in Saved/MenuBuild-Final.log; it completed build/cook/stage/archive successfully after the last working-copy change.
