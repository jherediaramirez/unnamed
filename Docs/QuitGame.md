# Functional Quit Game

Quit Game on the main menu now requests a normal standalone-game shutdown directly. The source's mock Game closed/Return to Title route is not included in the working interface. Its styling is unchanged; the later Realms removal closes one row above it.

## Ownership And Safeguards

- `UMenuApplication`, bound as `window.ue.menuapplication`, exposes only GetState, RequestQuit and RequestOpenWorld (catalog IDs). It retains weak references to the local controller and the video, input and preference services. `UMenuBrowserHost` owns and unbinds every browser service.
- `MenuApplicationRuntime.js` serializes a quit request, disables repeated submission and asks both `MenuVideo.prepareForExit()` and `MenuInput.prepareForExit()` to lock further edits and finish queued changes.
- The next controller tick waits for a settled native window, flushes the video preferences and native input profile, then calls `UKismetSystemLibrary::QuitGame` with Quit preference and platform restrictions respected. It does not kill a process, delete files, reset preferences, run a supplied command, or assume a world-saving system exists.
- Editor execution and invalid/nonlocal game owners are rejected natively; the editor Play button is disabled. A missing binding also leaves the button disabled.
- The native window wait is bounded to eight seconds. Browser bridge requests are bounded to five seconds, and the twenty-second overall Quit deadline starts before settings drain. A failed save or unanswered browser wait reports an error and releases the browser exit lock. Late/stale replies cannot replace newer state. Browser deadlines cannot cancel native work already dispatched; native readback remains authoritative. Normal save-on-change behavior and protected Hard Reset remain in place.

The runtime implementation follows the installed UE 5.8.3 source and Epic's [Quit Game API](https://dev.epicgames.com/documentation/unreal-engine/BlueprintAPI/Game/QuitGame). No force/platform-bypass flag is used.

## Build And Verification

The current input-foundation package is `Packages\MenuInputFoundationDevelopment\Windows\Unnamed.exe`. It adds input-profile draining and final-save checks to this lifecycle. See [Keybindings](Keybindings.md) and [the dated record](Records/2026-10-07-Keybindings.md) for its current evidence. The checkpoint details below remain historical.

The native Quit checkpoint is `C:\dev\projects\Unnamed\Packages\MenuQuitDevelopment\Windows\Unnamed.exe`. The current review-fix candidate is `C:\dev\projects\Unnamed\Packages\MenuReviewFixesDevelopment\Windows\Unnamed.exe`, with identical native source/binaries and refreshed physical HTML. The previous Realms-removal, menu/video/debug-reset/Quit candidates remain at their original paths. Native Quit preservation is at `C:\dev\backups\Unnamed\quit-game-20261007-091347`; the review-fix snapshot is at `C:\dev\backups\Unnamed\review-fixes-20261007-104750`. See [Records](Records/README.md) and Verification.md for current results.

```powershell
& 'C:\dev\projects\Unnamed\Scripts\BuildMenu.ps1' -Package
& 'C:\dev\projects\Unnamed\Scripts\TestMenu.ps1'
Set-Location 'C:\dev\projects\Unnamed\Tests'
npm test
node source-check.mjs
node quit-game.mjs --report=ReviewQuitResults.json
```

The owned-process quit suite uses independent settings files. It presses the actual main-menu button using browser-level mouse and Enter input, verifies normal process exit code zero and shutdown logs without forced termination, checks queued settings on disk and after relaunch, and preserves an unrelated save sentinel. It also injects a failed-write path and verifies that fixing it allows a successful retry. Failed harness attempts are retained separately, not counted as acceptance.

The whole-menu sweep excludes the destructive Quit action and checks its live binding; the owned-process suite verifies actual shutdown. The current interface has 33 routes after deliberately removing Realms and Join Realm (35 at the native Quit checkpoint). Shipping, other platforms, physical OS input and future world/progression save contracts remain separate acceptance gates.

The native Quit checkpoint record is 16/16 owned-process quit/relaunch/recovery checks, 4/4 live editor-safety checks, 37/37 video checks per profile, 426/426 complete menu checks per profile, 11 JavaScript unit tests and two native automation tests. Tests/Evidence/2026-10-07/QuitArtifactManifest.json records that checkpoint's candidate hashes and preservation checks. See Verification.md for the later Realms-removal results and exact report paths.
