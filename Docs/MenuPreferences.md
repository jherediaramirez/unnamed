# Menu preferences

The menu now remembers 101 additional scalar preferences, the chosen language, ordered resource packs, multiplayer warning dismissal and the extra telemetry choice. These values are available to future native feature consumers without adding those features now. Existing video and Enhanced Input stores remain their own authorities.

The original complete review and per-control inventory remain in [the plan](MenuPreferencePersistencePlan-2026-10-07.md) and [the inventory](MenuPreferenceInventory-2026-10-07.json). This implements the recommended first batch. Dormant bindings, real server favorites and durable world drafts remain separate batches.

## Ownership and behavior

| Domain | Owner | Behavior |
|---|---|---|
| 101 ordinary settings | UMenuPreferences GameInstance subsystem | Validate, save on edit, and return accepted values to all copies of a control. |
| Language and resource packs | Same subsystem | Draft selection stays local until Done succeeds. Escape/Cancel discards it. Language translation and pack appearance remain pending. |
| Warning dismissal / extra telemetry choice | Same subsystem | Save the checkbox value. Warning dismissal follows the existing menu route; no telemetry is collected or sent. |
| Five functional video settings | Existing UMenuVideoSettings | Existing behavior and file remain authoritative. |
| Seven movement keys and two Hold/Toggle policies | Existing native Enhanced Input settings | Existing bindings, defaults and serializer remain authoritative. |
| Graphics API / Sound Device / minecart rotation | Existing informational or reserved rows | No additional saved selector. API and device are fixed; minecart rotation remains disabled. |
| Worlds, servers, rules, experiments and generation drafts | Existing menu draft logic | Session-only samples; no real world or connection is created. |

Saved-only scalar controls have a tooltip explaining that their feature effect is pending. FOV, sound levels, mouse sensitivity, chat, skin and other desired settings are readable through UMenuPreferences::FindPreference. A later adapter must decide when and how to apply them.

Graphics presets commit their quality group together and leave functional menu blur alone. Manual quality changes mark the preset Custom. Loading restores each saved value directly, without replaying the preset. High Contrast and its pack membership commit together, including pack order. Disabling Friends List retains child choices.

## Storage

The native service owns Saved/Config/MenuPreferences.json relative to the project's runtime save directory. Schema version 1 stores only settings, language, packs, skipWarning and telemetryOptInExtra. Browser callers cannot select a file path or issue console commands. A trusted diagnostic launch can override the location with -MenuPreferencesFile.

Scripts/MenuPreferenceSchema.mjs generates the browser and native validation contract from the reference-derived catalog. Content/Menu/Web/preferences-schema.menuprefs ships as a physical NonUFS dependency beside the HTML. Its contents are JSON, but its custom extension prevents Unreal's Content watcher from attempting a DataTable import. Native validation rejects unknown fields, wrong types, invalid enums, ranges and slider steps. Explicit false, zero and empty lists survive; missing fields get defaults. The exact unfocused-chat-height default is accepted despite its older non-grid value.

Saving writes a temporary document, backs up the previous document to .bak, and checks replacement success before acknowledging the new state. The backup can recover a missing primary file after interrupted replacement. Invalid or future-version primary files are protected from automatic overwriting. Confirmed Hard Reset first preserves such a document in a unique .protected-* file. This is recoverable file replacement, not a claim of power-loss durability on every filesystem.

The service belongs to GameInstance, so document reload or controller recreation does not discard accepted choices. Standalone launches save to disk. PIE and -MenuSettingsSessionOnly use temporary preferences starting at defaults, leaving the player's saved file untouched.

## Routing and failures

Scripts/MenuPreferencesRuntime.js provides an ordered, coalescing browser queue with bounded bridge waits and revision checks. New queued choices remain visible when an earlier save fails. Failed requests read back canonical native state, restore accepted values and show an error. Done locks a staged dialog until the save resolves.

Quit drains video, input and ordinary preference changes, then the native application service flushes all stores before normal shutdown. A write failure prevents exit. An invalid protected file that has never been edited can still exit without changing that file.

Hard Reset explicitly resets this store, existing input preferences and video preferences before reloading the page. Cancellation leaves preferences alone. World/player saves remain outside reset. Stores are separate; if a later group fails, the error states that some groups may already have reset and offers retry through the existing reset action.

## Editor use

If Unreal shows a DataTable Options dialog for preferences-schema, cancel it and restart with the corrected build. The raw schema now uses .menuprefs so the Content watcher ignores it. See [the import fix](<Documented Actions/2026-10-07-Schema-Import-Fix.md>).

Restart Unreal Editor after a C++ rebuild to load its new module, then open L_Menu and press Play. An editor already running can retain its previously loaded DLL. HTML changes reload with the browser, but a new native binding requires the rebuilt module. The current editor build selects UnrealEditor-Unnamed-1015.dll.

PIE can exercise the menu and temporary preference values. Saving across application launches is verified in the packaged game. Native window/FPS/VSync controls and Quit remain unavailable in embedded PIE as before.

## Verification

- All eight native suites passed in Tests/MenuPreferencesAutomation-20261007-131834/index.json, including the three new preference suites and the existing input/video/quit suites.
- All 39 JavaScript unit tests passed, covering the new queue, stale reads, invalid values, Done failure, reset and Quit drain.
- All 938 original assets and working copies passed source/hash/syntax verification.
- Tests/MenuPreferences-Preservation.json confirms 9,556 prior package/reference/input/map files and all 3,157 staged index entries are unchanged. The index byte hash changed later in the session; its original bytes remain in the preservation snapshot.
- Packaged preference checks passed 126/126; existing input/API checks passed 50/50. The complete menu sweep passed 369/369 and video regression passed 37/37 at both 1280x720 and 1920x1080.
- The accepted candidate is Packages/MenuPreferencesV2Development/Windows/Unnamed.exe. Exact paths, hashes and captures are in [the dated work record](<Documented Actions/2026-10-07-Menu-Preferences.md>).

Relevant reruns:

```powershell
npm test --prefix Tests
node Tests/source-check.mjs
node Tests/preferences-settings.mjs --package=MenuPreferencesV2Development
node Tests/input-settings.mjs --package=MenuPreferencesV2Development
node Tests/review-validation.mjs --package=MenuPreferencesV2Development --prefix=review-next-preferences
```

Use fresh capture/report names and isolated runtime directories. Packaged browser tests use Unreal's CEF diagnostics; physical OS focus/input, Shipping, other platforms and the future feature effects remain separate acceptance checks.
