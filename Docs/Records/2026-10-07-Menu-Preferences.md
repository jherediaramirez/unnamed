# Menu preference persistence - October 7, 2026

Completed the recommended first batch from the [review plan](../MenuPreferencePersistencePlan-2026-10-07.md): 101 additional scalar choices plus language, ordered resource packs, multiplayer warning dismissal and the extra telemetry choice now have native storage. Saved-only settings explain that their effects remain pending. No gameplay, audio playback, translation, world generation or networking was added.

Current package: C:/dev/projects/Unnamed/Packages/MenuPreferencesV1Development/Windows/Unnamed.exe

```powershell
& 'C:\dev\projects\Unnamed\Scripts\LaunchMenu.ps1'
```

Restart Unreal Editor before using Play to load the new C++ module. The current module metadata selects UnrealEditor-Unnamed-1014.dll; the already-open editor was confirmed to still hold module 1011. It was left open. PIE/session-only preferences are temporary; standalone preferences survive relaunch.

## Changes

- UMenuPreferences is a GameInstance subsystem with a strict generated catalog, checked native saving, a last-good backup and protection for invalid/future files. Native consumers can read the saved desired values later.
- Browser edits use one ordered queue. Language and packs save on Done; Cancel/Escape discards their drafts. Shared controls use the same value. Presets and High Contrast/pack membership commit their related fields together.
- Existing five video settings, seven movement bindings and two Hold/Toggle policies retain their existing stores. Friends child choices survive disabling Friends List.
- Confirmed Hard Reset explicitly resets all preference groups. Quit drains pending changes and flushes each store before normal shutdown. Reset reports partial failures across stores honestly.
- Packaging reuses the already-built editor module, including its selected suffix, instead of attempting to delete an open editor's DLL.

## Evidence

| Check | Result | Evidence |
|---|---|---|
| Editor and game builds | Passed | Tests/Evidence/2026-10-07/MenuPreferences-EditorBuild-4.log; Tests/Evidence/2026-10-07/MenuPreferences-Package-Final.log |
| Native automation | 8/8 suites | Tests/Evidence/2026-10-07/MenuPreferencesAutomation-20261007-131834/index.json |
| JavaScript unit tests | 39/39 | npm test --prefix Tests |
| Packaged preference tests | 126/126 | Tests/Evidence/2026-10-07/Captures/preferences-2026-10-07T17-22-50-913Z/results.json |
| Existing input/API regression | 50/50 | Tests/Evidence/2026-10-07/Captures/input-foundation-2026-10-07T17-23-22-613Z/results.json |
| Complete menu sweep | 369/369 at each size | Tests/Evidence/2026-10-07/Captures/review-preferences-v1-packaged-1280x720/results.json and matching 1920x1080 capture |
| Video regression | 37/37 at each size | Tests/Evidence/2026-10-07/Captures/review-preferences-v1-video-1280x720/results.json and matching 1920x1080 capture |
| Asset/source check | 938 original assets and working copies; reference hash and JavaScript syntax passed | Tests/source-check.mjs |
| Preservation | 9,556 prior package/reference/input/map files and all 3,157 staged entries unchanged | Tests/Evidence/2026-10-07/MenuPreferences-Preservation.json |

The preference suite checks every scalar through the actual packaged native/browser bridge, separate-process restoration, shared captions, language/pack Done and Escape, ordered packs, warning/telemetry flags, reset confirmation, unrelated-save preservation and Quit with a pending edit. Screenshots at 1280x720 and 1920x1080 were inspected. The resource-pack footer explains its pending appearance effect.

The final native suite rejects numeric values for boolean flags; it caught Unreal's permissive JSON conversion during development. Earlier failed native reports and UI-harness captures remain preserved. They are superseded by the passing reports above.

The complete sweep covers all 33 menu routes and regenerates the current control registry from the exact packaged HTML hash. Scripts/LaunchMenu.ps1 and current runtime test defaults point at the accepted package. BuildMenu.ps1 defaults to a fresh timestamped archive name and rejects an existing archive to preserve prior builds.

## Exact artifacts

- Native game binary: Packages/MenuPreferencesV1Development/Windows/Unnamed/Binaries/Win64/Unnamed.exe
- Game SHA-256: 7FE2FEDD67DFFB5036EA6CEEC023BCF3667EC3345FD757C6A7515691C3EB1BE6
- Generated HTML SHA-256: 4922F18AF17B35CD0CB06DBEAD826AF018BA9AE0AFE686E52C79F90129074FE9
- Editor module SHA-256: AF69237134C8C3CF26B06544FCD6BE8178E602EF09CE74F4EE6E9385F6C5B4EE
- Preservation snapshot: C:/dev/backups/Unnamed/menu-preferences-20261007-130053
- Source HEAD remains b51afde03d97f5113b29c8c6ba59f4879e3bc085. No commit, staging or reset was performed.

The Git index byte hash changed later in the session, while every staged mode, object ID and path still matches the baseline. The original index remains preserved in the snapshot; no index restoration was performed.

## Remaining boundaries

The new values remember choices; their future effects still require implementation and acceptance. Dormant bindings, real server favorites and durable world drafts remain separate batches. Reserved selectors remain disabled. Current validation uses Development Win64 and Unreal CEF diagnostics; physical OS focus/input and Shipping/other-platform checks remain separate. Editor compilation and session-mode native tests passed; the user's already-open editor was not restarted or claimed as a new interactive PIE pass.

See [Menu Preferences](../MenuPreferences.md) for the schema, ownership, save/reset semantics and rerun commands.
