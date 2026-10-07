# October 7, 2026 — keybindings foundation

Scope: review the existing bindings and implement movement binding settings and input intent before the FPS/character stage. See [Keybindings](../Keybindings.md) for the complete catalog, state rules and integration contract.

Implemented seven remappable movement bindings, native Enhanced Input assets, saved Hold/Toggle sprint/crouch policies, conflict/reserved-key checks, modifier-side-aware capture, unbind/cancel/reset behavior, menu context isolation and release/focus cleanup. Shift now defaults to sprint and Ctrl to crouch. The remaining 55 binding rows are explicitly Planned and disabled. Actual character movement, camera, networking, animation and physical OS acceptance remain future work.

Baseline preservation: `C:\dev\backups\Unnamed\keybindings-20261007-113809`. Source, config, content, documents, scripts, tests, saved configuration/log copies, Git status/patches and the Git index were preserved before edits. Earlier package files were hashed; logs held open by the already-running game cannot be hashed while locked and are excluded from stable package comparisons. No existing package or map was replaced. The user-facing reference and existing project identity are retained.

The separate GitHub restoration documentation checkpoint `b51afde` arrived during this work. Its updated Git index and commit were preserved; no source changes from this task were staged. The initial index remains in the preservation snapshot, and `Tests/Evidence/2026-10-07/InputFoundationPreservation-20261007.json` records both heads and the empty staged diff.

## Verified input foundation

- Final editor build: `Tests/Evidence/2026-10-07/InputEditorBuild-final.log`, succeeded using the separate `UnrealEditor-Unnamed-1011.dll`; the open editor keeps its previous loaded module until restarted.
- Five native suites: `Tests/Evidence/2026-10-07/InputAndMenuAutomation-20261007-final/index.json`, 5 successes, 0 failures. Includes intent rules, native profile/serializer validation, key-to-action-to-intent routing, video validation and editor Quit safety.
- Browser unit suite: 30 passed, 0 failed (`npm test`). Source/resource validation also passed for the 938 original assets and working copies.
- Packaged input/persistence suite: `Tests/Evidence/2026-10-07/Captures/input-foundation-2026-10-07T15-58-51-187Z/results.json`, 38 passed, 0 failed across 1280x720 and 1920x1080. Includes independent process relaunch, both modifier sides, mouse capture, resets, unrelated progress preservation and normal exit.
- Existing video behavior: `Tests/Evidence/2026-10-07/Captures/review-input-foundation-video-1280x720/results.json` and `review-input-foundation-video-1920x1080/results.json`, 37 passed per profile. These passing captures match the final HTML hash.
- Final complete menu regression: `Tests/Evidence/2026-10-07/Captures/review-input-foundation-final-packaged-1280x720/results.json` and `review-input-foundation-final-packaged-1920x1080/results.json`, 369 passed per profile. `Tests/Evidence/2026-10-07/review-input-foundation-final-ValidationResults.json` confirms both owned sessions closed normally. The registry and backlog were regenerated from the passing final 1920x1080 menu capture and matching passing video capture.
- Preservation: unchanged menu map and immutable reference; all 6,354 stable earlier package files match the baseline hashes. Mutable Saved files are excluded. Project and new-package HTML hashes match.

Initial failed harness runs are retained. One isolated native test fixture lacked a viewport; one viewport test accidentally restored the first profile's window size; the older smoke harness incorrectly treated the newly native Hold/Toggle policies as disposable mock values. Those harness assumptions were corrected, and final passing evidence is listed above and below.

## Current artifact

Launch `C:\dev\projects\Unnamed\Packages\MenuInputFoundationDevelopment\Windows\Unnamed.exe`, or run `C:\dev\projects\Unnamed\Scripts\LaunchMenu.ps1`.

Native executable: `Packages\MenuInputFoundationDevelopment\Windows\Unnamed\Binaries\Win64\Unnamed.exe`.

SHA-256: `700E46F5C4D0D2E72B0940563BC421151678DBE0071E41CD3DB4E727AA9D8A0A`.

HTML SHA-256: `9182C3021BD2307F3EFA135BE2BFBDFC37220923D79732733C43904E4C0411C9`.

Remaining acceptance: actual FPS character movement/clearance, camera, animation, multiplayer prediction/authority, physical OS focus/input, controller support and human feel. This completed stage proves binding settings and input intent, not character gameplay.
