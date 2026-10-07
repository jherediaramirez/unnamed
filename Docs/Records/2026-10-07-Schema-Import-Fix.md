# Preference schema import fix - October 7, 2026

The editor's automatic Content watcher treated preferences-schema.json as a DataTable source and opened an import dialog. This was an integration mistake in the preference pass. Cancel that dialog; no DataTable or row type is needed.

The raw schema now uses the extension .menuprefs. Its JSON contents and the generated menu HTML are byte-for-byte unchanged. Native loading, generation, tests and staging use the new filename. Normal editor auto-import settings were not changed. The player save file remains Saved/Config/MenuPreferences.json, outside the watched Content directory.

The build rules track the schema as an external dependency, and both editor/game receipts now reference the new filename. This also fixes an initially stale packaging receipt that still attempted to copy the old name. Failed build logs and prior packages remain preserved.

Current editor module: UnrealEditor-Unnamed-1015.dll. Restart the editor once after dismissing the already-open dialog, then press Play to load it. An already-open dialog does not close itself when its queued file is renamed.

Current package: C:/dev/projects/Unnamed/Packages/MenuPreferencesV2Development/Windows/Unnamed.exe

Scripts/LaunchMenu.ps1 and current runtime-test defaults select this package.

Verification:

- The installed Unreal Python factory probe inspected 42 import factories: JSON is importable; menuprefs is unregistered. Tests/Evidence/2026-10-07/SchemaImportFix-FactoryExtensions.json records the declarations. No assets were imported by the probe.
- Eight native suites passed: Tests/Evidence/2026-10-07/SchemaImportFix-Automation-20261007-133854/index.json.
- All 39 JavaScript unit tests and the 938-asset source/hash/syntax check passed.
- The corrected package contains the .menuprefs file and no legacy preferences-schema.json.
- All 126 packaged preference checks passed, including every scalar, reload, independent relaunch, Done/Cancel, reset and Quit: Tests/Evidence/2026-10-07/Captures/preferences-2026-10-07T17-45-44-468Z/results.json.
- Earlier full menu/video/input captures remain applicable to the unchanged HTML; they were not rerun or relabeled as new captures.

Hashes:

- Schema: BE71BB5C190AEFFE2A23D4DB43912987C65270EA9888FA6354DD1E27E7551C3B
- HTML: 4922F18AF17B35CD0CB06DBEAD826AF018BA9AE0AFE686E52C79F90129074FE9
- Native game: E45CE43927DD6FB34BEEE9FA3AE8CC12B01321890E8D46449678FC6EC6DE321D
- Editor module: FA36EB045C17F8B11D505822F266E8A58B1A0D1FBBC77BFA3039C5C6B6F72906

Preservation snapshot: C:/dev/backups/Unnamed/preference-import-fix-20261007-133653. The user's open editor was left open; the modal was not silently counted as an interactive PIE pass. No commit, staging, map edit or input-asset edit was performed.
