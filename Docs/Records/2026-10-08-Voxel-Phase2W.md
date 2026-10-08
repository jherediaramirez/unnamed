# Voxel world phase 2W: Create New World - October 8, 2026

Create New World now makes real worlds. The Create screen's settings are checked and saved natively as `Saved/Worlds/<id>/world.json`, and the world opens at once as `L_Generated?VoxelWorld=<id>`, built from its seed and type. The Singleplayer list shows the catalog's Playground and then the saved worlds, newest played first; Play, Edit (rename), Delete and Re-Create work on them. Game mode and difficulty reach the player, with Peaceful added. Step 2L was committed first as `3982935`. The design is in [Voxel world, Saved worlds](../VoxelWorld.md#saved-worlds).

## What changed

- **Saved worlds** (`Source/Unnamed/Worlds/UnnamedSavedWorlds`), the store on the game thread:
  - checks the draft;
  - makes ids: kebab-case from the name, unique, never a catalog, retired or device name;
  - reads and writes a versioned `world.json`;
  - lists, renames, touches and deletes worlds.
- **`UMenuApplication`:**
  - new `CreateWorld(draft)`, `DeleteWorld(id)` and `RenameWorld(id, name)`;
  - `RequestOpenWorld` also opens saved ids;
  - `GetState` adds `savedWorlds`, each with its draft for Re-Create, read once per menu showing.
- **`AVoxelWorld`:**
  - reads its world from the URL and builds Superflat's flat terrain or the height terrain from it;
  - gives the pawn its rules through the new `AFoundationLab::ConfigurePlayer`;
  - its shot report names the world and the player's mode and difficulty;
  - shot framing follows the actual surface.
- **Core terrain:**
  - `IVoxelTerrain` is the column-generator interface;
  - `FVoxelFlatTerrain` is new;
  - `FVoxelArea::Build` takes a terrain;
  - the spawn search asks `IsDry`.
- **Vitals:** `EFoundationDifficulty` gains Peaceful, in the reference's order. It means no mob damage and, with natural regeneration on, a point of health every second and of food every half second. The F3+F6 cycle and `Describe` follow.
- **Menu:**
  - `Worlds.mjs`: Create calls native; Delete is real (with the reference's confirm); Re-Create refills the draft from the save; the Edit screen's other actions say plainly they are not available yet.
  - `Dialogs.mjs`: Edit's Save renames natively.
  - `Core.mjs`: no sample saves.
  - `MenuApplicationRuntime.js`: a `create` exit (drain the stores, `CreateWorld`, follow travel), delete and rename, and a list built only from native.
  - `MenuWorldsRuntime.js`: actions on saved worlds only.
- **Scripts:** `CaptureVoxel.ps1 -World <id> [-WorldsDir]` captures a saved world.
- **Tests:**
  - native: `Unnamed.Worlds.Saved`, `Unnamed.Voxel.Terrain.Flat`, and Peaceful cases in `Unnamed.Foundation.Vitals.*`;
  - units: `menu-worlds.test.mjs` rewritten for the native list, Create, Delete and rename;
  - CEF: new `create-world.mjs`;
  - updated: `foundation-menu.mjs` (no sample rows), and `smoke.mjs` and `registry.mjs` (Create is a real action).

## Problems found and fixed

- **Lenient JSON reads.** `FJsonValue`'s `TryGet*` convert types: any number reads as a boolean, and text reads as a boolean or a number. The native test caught a text game rule, a rule beyond 32 bits and a number as an experiment getting through. Every read now checks the JSON type first.
- **A second random seed.** The creation log line re-parsed a blank seed text, which would have drawn another random seed. It now logs the seed that was saved.
- **Framing far below sea level.** The shot framing started from sea level and multiplied heights, so a Superflat world at Z -61 was a small plane at the bottom of the frame. Framing now follows the surface. The height terrain frames as before: its brightness regions measure identically to step 2L's.
- **A shared fake button.** A unit-test stub returned one fake button for every selector. The new Create-button refresh disabled it and broke the quit tests, so the refresh now skips pages without a draft.
- **Two compile slips:** a local `Layers` hid `AActor::Layers` (C4458), and the unity layout was checked with `-DisableAdaptiveUnity` before and after.

## Verification

- **Editor build:** no warnings in project code.
- **`Scripts\TestVoxel.ps1`:** 21/21 (`Tests/Evidence/2026-10-08/VoxelAutomation-20261008-172207/`).
- **Every native suite:** 68/68, also on the unity layout the commit will produce (`AllNative-20261008-171512`).
- **`npm test`:** 57/57; `source-check.mjs` passes (938 assets, reference hash unchanged).
- **`Tests/create-world.mjs --editor`:** 32/32 across four launches (`Tests/Evidence/2026-10-08/Worlds/Create-1791494165661/`):
  1. A fresh profile lists only the Playground. "Hello World" with seed `hello` creates `hello-world/world.json` and opens with that seed: `1656767905311477007 (text)`. The player is grounded in Survival on Normal.
  2. The list is the Playground, then the save, showing its folder, date and mode, and the save is selected. The Playground's Edit, Delete and Re-Create are disabled; the save's are enabled. Re-Create refills name, seed, type and mode. Edit renames it natively with the same id. "Flat Test" (Superflat, Creative, Peaceful) saves its three layers and opens with the player standing on the grass at Z -6,000 cm in Creative on Peaceful.
  3. The list is ordered most recently played first. Playing "Hello Again" builds the same seed and updates `lastPlayed`.
  4. Delete removes each world from the list and the disk.
- **`Tests/foundation-menu.mjs --editor`:** 36/36. Unknown ids are refused, there are no sample rows, the Playground is protected, and it travels by button, double click and Enter (`Tests/Evidence/2026-10-08/Foundation/Menu-1791493871432/`).
- **Playground runtime probe** (`TestFoundationRuntime.ps1 -Editor -FrameRates 60`): 103 checks, 0 failures, mean 57.5 FPS (`Tests/Evidence/2026-10-08/Playground-all/Runtime-20261008-171232/`, kept local: 22 MB of HUD captures). The pawn's new `ConfigurePlayer` call and the vitals changes leave the Playground unchanged.
- **Screenshots:** `Tests/Evidence/2026-10-08/WorldsCompare-20261008-171045/` (Create screen, list, seed `hello`'s land, Superflat), plus the dev seed's area unchanged (`VoxelShots-20261008-171008`).
- **Timings** (offscreen, this machine), from the native save to the area on screen:
  - Create writes `world.json` and issues `OpenLevel` within 14 ms.
  - The map loads in about 100 ms.
  - The 81-column area is on screen in 158 ms for "Hello World", 131 ms when replayed, and 80 ms for the flat world.

## Known issues

- **Hardcore** plays as Survival on Hard: no permadeath and no hardcore hearts yet.
- **World types:** Large Biomes, AMPLIFIED and Single Biome are saved but generate the default terrain until phases 4 and 5.
- **Superflat** refuses layers of blocks the game lacks yet (snow layers, lava).
- **Generated worlds** have no mining, placing or creative inventory until phase 3. Commands, structures, the bonus chest, experiments and the game rules other than `natural_health_regeneration` are saved only.
- **Peaceful's regeneration** follows the documented mechanic, not the pinned sources.
- **Stale generated docs:** `Docs/ControlRegistry.*` and `FunctionalBacklog.md` are generated from smoke and video captures and still describe the mock list. Regenerating them needs those captures, and `smoke.mjs` was already out of date (it expects 33 routes; the page has 34).
- **No lock between two game instances** sharing a world folder.

## Next

Phase 3, streaming:
- `UVoxelWorldSubsystem` with rings, hysteresis, priorities and bounded tasks;
- mining and placing on generated terrain through the authority;
- a fly camera for the "no holes" test.

Phase 4's caves will need the step 2L card limits revisited.
