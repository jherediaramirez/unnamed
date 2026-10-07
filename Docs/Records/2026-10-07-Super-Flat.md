# Super Flat test world - October 7, 2026

Created `/Game/Maps/L_SuperFlat`: 2 km square, solid flat ground, centered spawn, daylight, and a 1 m / 10 m measurement grid. Singleplayer shows Super Flat plus the two original design references. Only Super Flat loads from that list; its mock Edit/Delete/Re-Create actions are disabled. The separate in-progress Foundation lab remains available through its own development launch path.

Menu travel drains pending settings and flushes the existing stores. The outgoing browser is removed and a fresh menu is restored on return. A stock fly camera provides inspection until the FPS implementation is connected; this change does not implement or claim FPS movement.

Launch: `C:\dev\projects\Unnamed\Packages\SuperFlatDevelopment\Windows\Unnamed.exe`, or `Scripts/LaunchMenu.ps1`. Native executable SHA-256: `6A5F2FCE88FAE28669D110AB708EAA84EE69AAD150B5718D8F7ECAF3AAB4FEF6`.

Passed editor/package builds, asset validation, the native routing suite, 44 browser unit tests, and 32 packaged checks including three menu/world round trips and ground/spawn collision. Rendered screenshots were inspected. Physical input and embedded PIE remain separate acceptance checks. The packaged snapshot does not certify ongoing FPS work in the shared checkout.

Backup: `C:\dev\backups\Unnamed\super-flat-20261007-140749`. No commits or index changes. Original reference files, menu map and older package executables are preserved. Concurrent FPS input-asset changes were retained.

See [Super Flat](../SuperFlat.md) for map units, future character integration, exact evidence, and the reproducible setup/verification scripts.
