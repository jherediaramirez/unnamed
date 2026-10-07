# Functional Video Settings

## Scope

The authorized settings are Fullscreen, Max Framerate, VSync, GUI Scale and Menu Background Blur. Every other setting remains a mock. Exclusive fullscreen and monitor video-mode selection are not enabled.

| Control | Default | Actual effect |
|---|---|---|
| Fullscreen | OFF | Native windowed/borderless transition; remembers the windowed client size and position for returning |
| Max Framerate | 120 | Engine frame cap; the menu's 260/Unlimited position maps to an engine limit of zero |
| VSync | ON | Engine r.VSync, with readback and rejection of higher-priority overrides |
| GUI Scale | Auto | Integer CSS layout scale; explicit choices are constrained by the current viewport |
| Menu Background Blur | 5 | Panorama/fallback blur on settings screens; Main remains unblurred |

The panorama animation remains capped at 30 FPS and the browser producer at 60 FPS. Neither is falsely presented as the engine frame rate. Menu blur is shared between Video and Accessibility. The mock graphics preset cannot overwrite the functional blur value.

The live scale calculation reserves 416 logical pixels of width for the widest pack-selection screen and 240 of height, with a maximum preference of 32. This preserves the original 1920x1080/1280x720 scales while avoiding clipped pack columns at narrower desktop sizes. A stored explicit scale is clamped for rendering when the window shrinks, without losing its preference for a larger window.

## Ownership

Current review fixes: `MenuBridgeRuntime.js` bounds each browser/native wait to five seconds. A failed request retains a newer queued choice; timed-out and pre-edit polling replies cannot overwrite newer completed state. A browser deadline cannot cancel an already-dispatched native operation. The current candidate is `Packages/MenuReviewFixesDevelopment`; later sections naming earlier packages describe preserved historical checkpoints.

`Source/Unnamed/MenuVideoSettings.*` owns validation, engine changes, native readback, window-transition completion, persistence and reset. It exposes GetState, ApplySetting and HardReset through one browser-bound UObject. No console-command execution, arbitrary variable setter, filesystem path setter, gameplay service or world deletion API is exposed.

`MenuWeb/Runtime/MenuVideoRuntime.js` owns asynchronous request serialization, coalescing of pending edits, synchronization, presentation and error display. Native state is rehydrated on document reload. A window transition must finish before the next request is applied. A failed change restores the displayed actual state and reports the failure.

The controller waits for the actual standalone Slate window to exist before enabling native settings. Engine/window mutations are unavailable in Play-in-Editor so a menu setting cannot alter the editor's global frame cap or VSync. GUI and blur remain available in PIE. F11 and Alt+Enter use the same native request path; automatic Unreal shortcut handling remains disabled to prevent double toggles.

Fullscreen requests use only window-mode/resolution application. They do not call the broad ApplySettings method, which would apply unrelated quality/audio settings and persist them. The requested native window mode is verified and a failed transition times out and requests restoration. Borderless mode does not change the monitor video signal, so exclusive-mode confirmation policy remains deferred.

## Persistence And Reset

Ordinary standalone launches save the five implemented values in `Unnamed/Saved/Config/MenuVideoSettings.ini` beside the packaged game content. Editor-hosted standalone uses the project's Saved directory. Windowed dimensions are included only to restore the window after leaving fullscreen. No unfinished option is saved, and the immutable reference is never edited.

Saving happens when a change completes, without relying on a shutdown handler. The functional Quit Game also drains pending changes and flushes the dedicated file before normal exit; see QuitGame.md. `-MenuSettingsSessionOnly` disables persistence for isolated tests. `-MenuVideoSettingsFile=<path>` selects an isolated native test file; the browser cannot change this path.

Hard Reset is the last button in the Video Settings scroll area. Confirmation Cancel/Escape changes nothing. Confirmation Hard Reset restores the five defaults, writes them to the dedicated settings file, returns to windowed mode, and reloads the document. Reload recreates all temporary menu preferences, key bindings, language/packs, fictional lists and staged drafts from first-run defaults.

Hard Reset is a settings/preferences reset, not a world wipe or a recursive deletion of Saved. It does not delete worlds, player progress, unrelated settings files, logs, packages or asset caches. There are no real worlds or progression systems in this project. Any future factory-wipe action must have its own explicit scope and confirmation.

The user confirmed that save data stays protected and Hard Reset is temporary debugging UI, not a planned final-game feature. It uses the same textured `mc-button`, 200x20 logical dimensions and font as the primary main-menu buttons, centered as the last Video Settings item. It is not added to the main menu. `BuildMenu.ps1 -ExcludeDebugTools` omits the control from the generated interface; direct adapter generation uses `--without-debug-tools`. This hides the debugging UI, not a security restriction on the native service.

The styling follow-up is in `Packages/VideoSettingsDebugDevelopment/Windows`, with unchanged native binaries and refreshed physical HTML. The prior `VideoSettingsDevelopment` package and its test evidence remain intact. Source/configuration/saves and the dirty work were snapshotted at `C:\dev\backups\Unnamed\debug-reset-style-20261007-090408` before the follow-up.

## Reproducible Verification

```powershell
& 'C:\dev\projects\Unnamed\Scripts\BuildMenu.ps1' -Package
& 'C:\dev\projects\Unnamed\Scripts\TestVideoSettings.ps1'
Set-Location 'C:\dev\projects\Unnamed\Tests'
npm test
node source-check.mjs
& '..\Scripts\LaunchMenu.ps1' -Width 1280 -Height 720 -Diagnostics -DebugPort 18891 -SessionOnly
node video-settings.mjs --port=18891 --profile=1280x720 --capture=review-video-1280x720
node smoke.mjs --port=18891 --profile=1280x720 --mode=review-packaged
node quit-game.mjs --report=ReviewQuitResults.json
```

Close the diagnostic window before launching a second window on its port. The current owned-process quit suite creates unique test settings files, verifies normal exit and relaunch restoration, and leaves unrelated save data untouched. The historical `video-persistence.mjs` suite targets VideoSettingsDevelopment and terminates its owned processes after completed saves; that earlier evidence established that save-on-change does not depend on a shutdown handler. See Verification.md for current versus historical candidates.

Native validation acceptance is based on the automation JSON report, not the editor process exit code alone. Runtime acceptance requires native window readback, actual measured engine cadence, real CEF screenshots, saved-file readback and independent process relaunches.

## Remaining Human Gates

Physical keyboard/mouse focus feel, driver-level VSync/tearing, multi-monitor/DPI combinations, unsupported-window failure injection, Shipping and other platforms require separate checks. Browser-level shortcut automation is not claimed to prove physical OS key routing.

The starting source/map/config/index, saves and prior package were preserved at `C:\dev\backups\Unnamed\video-settings-20261007-081817`; its PreservationManifest.json records 3366 file hashes and the starting Git state. The original package stays under Packages/MenuDevelopment; the new candidate uses Packages/VideoSettingsDevelopment.
