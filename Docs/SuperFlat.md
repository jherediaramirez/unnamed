# Super Flat

## Scope and implementation plan

Add a 2,000 m by 2,000 m level at `/Game/Maps/L_SuperFlat`, with a flat collision surface at Z = 0, a central Player Start, daylight, and a world-space measurement grid (1 m / 10 m). The existing menu map, input assets, settings, reference entries, and previous packages stay intact.

1. Create the editable level and its floor material through an idempotent Unreal Python setup script. Give it its own game mode so opening the level does not reopen the menu.
2. Add **Super Flat** to Singleplayer. Only its fixed native ID may travel to a map. Keep Fictional Forest Camp and Fictional Creative Workshop as labeled design references. Disable editing/deleting the built-in test map through the mock save controls.
3. Drain and save pending menu settings before travel. Remove the browser on departure and restore it on return. Use Unreal's stock spectator pawn temporarily; the new FPS character and its movement are separate work.
4. Build the editor module and a fresh package containing both maps. Verify native rejection of reference/arbitrary map IDs, floor collision/spawn, actual menu-to-world-to-menu travel, settings retention, and the rendered level.

The original Create New World screens remain design prototypes. They do not generate playable worlds.

The Player Foundation (`/Game/Maps/L_Foundation`) now sits beside Super Flat in the Singleplayer catalog; the two original entries remain design references. The FPS implementation can later replace Super Flat's preview game mode once its collision provider supports this surface. This task does not alter the separate foundation classes or remove its lab.

## Preservation

Baseline: `b51afde03d97f5113b29c8c6ba59f4879e3bc085`, including all existing local changes. Backup: `C:\dev\backups\Unnamed\super-flat-20261007-140749`. See `Tests/SuperFlat-Preservation.json` and the backup's status, index, patches, copied files, and protected-file hash manifest.

Status: implemented and verified for the preview-world scope.

## Play

Run `Scripts/LaunchMenu.ps1` or `Packages/SuperFlatDevelopment/Windows/Unnamed.exe`. Choose **Singleplayer → Super Flat → Play Selected World**. Double-click and Enter also open it. WASD and mouse move/look with the stock preview camera; E/Q move up/down; Escape returns to the main menu. These are temporary engine camera controls, separate from the upcoming configurable FPS character.

In Unreal, open the compiled project, press Play in `L_Menu`, then select Super Flat. The level is also directly editable under **Content/Maps/L_SuperFlat**. Restart any editor that was opened before the C++ rebuild. Editor Escape may stop PIE using Unreal's normal editor shortcut. Live embedded PIE and physical keyboard/mouse acceptance were not exercised in this pass; native packaged travel and rendering were.

## Verification - October 7, 2026

- Editor and Development package build: passed (`Tests/Runtime/SuperFlat-Package.log`). Packaging includes both the menu and Super Flat; setup preserves existing map/material edits.
- Asset verification: passed (`Scripts/Editor/VerifySuperFlatMap.py`, `Tests/Runtime/SuperFlat-VerifyMap-2.log`). Checks dimensions, ground height, collision profile/enabled state, mesh, material, spawn, and GameMode.
- Native routing/isolation suite: passed (`Tests/SuperFlat-NativeAutomation/index.json`). Rejects the two references and arbitrary package/command strings; checks separate menu and world classes.
- Browser logic: 44/44 tests passed, including settings drain, rejected/duplicate world requests, PIE capability policy, timeout recovery, and stale poll protection.
- Packaged integration: 32/32 passed in `Tests/Captures/super-flat-2026-10-07T18-36-54-558Z/results.json`. Three actual round trips using Play, double-click, and Enter; nine ground traces and a clear standing spawn per trip; preferences/bindings retained; one restored browser; normal Quit. The first harness run missed CEF's replacement browser after a successful return; the final harness reconnects after native map travel.
- Native world rendering and the Singleplayer layout were captured and visually inspected. See `super-flat-world.png` and `singleplayer-1280x720.png` in the accepted capture directory.
- Original menu map, reference assets, previous package executables, and all staged Git entries are unchanged. The concurrent FPS task changed `IA_Look` and `IMC_OnFoot`; those changes were preserved, not reverted. See `Tests/SuperFlat-FinalPreservation.json`.

The accepted package is a fixed snapshot. The concurrently active FPS task continued changing the working HTML/C++ after packaging, so this package's passing checks do not certify that task's newer changes. Its separate movement/F3/action acceptance remains with that task.

Native executable SHA-256: `6A5F2FCE88FAE28669D110AB708EAA84EE69AAD150B5718D8F7ECAF3AAB4FEF6`.

## FPS integration handoff

The floor occupies `[-100000, -100000, -100]` to `[100000, 100000, 0]` in Unreal centimetres. The PlayerStart is `(0, 0, 110)`. One grid cell is 100 cm; major lines are 1,000 cm. Red follows X; green follows Y. There is no voxel generation or world-save creation.

Replace the preview controller/pawn and HUD through the Super Flat GameMode once the FPS character is ready. The existing custom Foundation solver uses its own box provider, so merely changing the pawn class is insufficient: that provider must include this floor and use the agreed feet/spawn convention. The real gameplay controller must activate/bind the existing input subsystem; the preview controller deliberately leaves it disabled. Retain the restricted `super-flat` menu ID and the return to `/Game/Menu/L_Menu`.
