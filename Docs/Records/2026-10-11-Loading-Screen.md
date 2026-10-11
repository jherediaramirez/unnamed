# World loading screen, and a spawn fallback that always finds land - October 11, 2026

Jesus asked for a Minecraft-style screen while a world opens, instead of watching the columns stream in, and for the rare climate spawn that started on the water to find land. Brief: `C:\dev\sandbox\spawn-worker\next-loading-screen-prompt.md`. Plan, confirmed by the shift manager within the brief: `C:\dev\sandbox\loading-screen\PLAN.md`. Branch `feature/voxel-world`, base `5f495b9`.

**Look: Jesus picked A** (with the chunk map, the default; 2026-10-11, through the shift manager). Option B is the same screen without the map (`voxel.LoadingScreenMap 0`). Captures of both, from the same frame: `Tests/Evidence/2026-10-11/LoadingScreen/loading-a.png` and `loading-b.png`.

## 1. What 26.3 does (black box)

Sources: the client jar's `en_us.json` and textures, `javap -public` signatures, minecraft.wiki (`Loading_screen`, `Java_Edition_1.21.9`). Notes: `C:\dev\sandbox\loading-screen\mc-loading-facts.md` and `mc-loading-look.md`.

- **Screen order.** Create New World shows `GenericMessageScreen` with "Preparing for world creation..." (`createWorld.preparing`). Play Selected World shows "Reading world data..." (`selectWorld.data_read`) or "Preparing Resources..." (`selectWorld.resource_load`). Both then show `LevelLoadingScreen`. 26.3 has no `ReceivingLevelScreen`. This order is inferred from class names and keys; nobody has watched it.
- **What `LevelLoadingScreen` draws.**
  - Since 24w09a: a blurred, darkened title panorama.
  - A chunk colour map: one pixel per chunk, coloured by status. The wiki's table runs Empty 545454 to Full FFFFFF.
  - Since 25w31a / 1.21.9: a progress bar.
  - Esc does not close it.
- **When it closes.** `LevelLoadTracker` waits for the server's chunks, then for the player's own section to compile (with a timeout), then a short fixed delay. The timeout and delay values exist only in code, so ours are our own. `PlayerSpawnFinder.findSpawn` returns `CompletableFuture<Vec3>`.
- **Layout.** The wiki has no 1.21.9+ screenshot. Text, bar and map positions come from a crop on a third-party mod page, used as a visual reference only: https://modrinth.com/mod/no-loading-bar, saved in `C:\dev\sandbox\loading-screen\wiki-images\`, measured at GUI scale 2.
  - "Loading terrain..." (`multiplayer.downloadingTerrain`).
  - A 200 x 2 GUI px bar, 00FF00 on 000000, 12 px under the text's top.
  - The map 12 px under the bar.
  - The crop's dirt background and black map frame contradict the wiki (frame removed in 24w19a), so we follow the wiki on both. A vanilla 26.3 capture at a known GUI scale would pin the layout; it needs a client run on Jesus's screen.

## 2. What changed

- **Loading screen** (`Hud/HudLoadingScreen.{h,cpp}`, new). `SHudLoadingScreen` is a Slate leaf widget at vanilla's auto GUI scale (`SHudCreativeScreen::GetGuiScale`; 3 at 1280 x 720).
  - Background: panorama_0, loaded at runtime from the menu's own copy (`Content/Menu/Web/assets/...`), cover-fitted like the CEF menu's fallback. It is blurred by a Slate post-process blur with a standard deviation equal to the `menuBackgroundBlurriness` preference (CSS `blur(Npx)`, as the menu does). Over it, menu_background's 25% black.
  - Text: "Loading terrain..." in the HUD's 26.3 bitmap font, with its shadow.
  - Bar: the view's columns on screen (`InViewOnScreen / InView`).
  - Map (option A): the columns within the view radius of the spawn's column, 2 GUI px each, coloured by pipeline stage on 26.3's status colours: absent = Empty, generating = Noise, final blocks = Features, meshing = Light, on screen = Full.
- **Who shows it.** `UFoundationHud` adds the widget above the HUD (Z 300). `AFoundationController::UpdateLoadingScreen` drives it from `AFoundationLab::GetLoadState` (new; other labs are always ready, so the Playground never shows it). `AVoxelWorld` answers with `IsReady()`, `bSpawned` and the progress.
  - When it shows: from the generated map's first frame, and again when a world starts over (`voxel.Seed`). A flight or a moved view that is merely off screen does not bring it back.
  - When it goes: at ready, or after 15 s.
  - Underneath: `IsGameplay()` is false, so Esc and focus loss open no pause menu. Gameplay input and the pawn's simulation are off.
  - On close: the usual resume. If a fly camera exists it has frozen the pawn itself, so there is no resume.
  - After a timeout with the player not yet placed, the pawn stays off until it is placed.
  - Log lines: `Loading screen shown` and `Loading screen hidden after N ms (ready|timeout)`.
- **Capture flag.** `-VoxelLoadingShot=<folder>` saves `loading-a.png` and `loading-b.png` (with and without the map, consecutive frames) at half progress. While it waits for both, the screen stays up (still capped at 15 s).
- **CEF menu** (`MenuWeb/Patches/Worlds.mjs`, `MenuWeb/Runtime/MenuApplicationRuntime.js`). 26.3's message screen (`generic`: one centred line, no buttons, Esc ignored) shows from the Create or Play click until the level changes: "Preparing for world creation..." or "Reading world data...". On failure it goes back before the reason shows. This covers the settings drain and the ~0.6 s LoadMap freeze.
- **Spawn fallback** (`Terrain/VoxelTerrain.h`, `VoxelSpawnFinder.{h,cpp}`, `VoxelDensityTerrain.{h,cpp}`).
  - `IVoxelTerrain::FindDryRing(Column, FirstRing, LastRing)` returns the first dry top block on square rings of columns, in FindSpawn's order. It is built on `FVoxelSpawnFinder::SearchRings`, which reads each ring's columns in a `ParallelFor` and then takes them in order, so the result never depends on threads.
  - The density terrain reads one density per column (`GetSurfaceHeights`). The default implementation asks `ConfirmDry` per block column.
  - `FindSpawn` is rings 0-5 (26.3's 11 x 11, `SpawnRadius`), with the same results as before.
  - `Find`'s climate fallback is rings 6-32 (`FallbackReach`, now in columns: 512 blocks), else the water's surface over the pick.
  - `FindNearestDry` and the `bNearest` path (`-VoxelSpawnAt`, `-VoxelSpawnExact`, Superflat) are unchanged.

## 3. Results

- **Spawn probe** (a throwaway test, not committed; `C:\dev\sandbox\loading-screen\runs\probe-130713\editor.log`). Over seeds 0-59, Large Biomes has 4 seeds wet at the pick (1, 5, 7, 57), with their land within 6-10 rings. Overworld and Amplified: 0 of 60. A full 65 x 65 scan takes 8.9 s serial and 0.48 s parallel, so the search stays on the game thread behind the screen and no async search was built.
- **Spawn tests** (`C:\dev\sandbox\loading-screen\runs\spawn-final-131944`): 7 of 7 `Unnamed.Voxel.Spawn` tests pass.
  - Large Biomes seed 1 now stands at (1172, -114, 63). That value was recorded on its first run (`spawn-131812`) and is pinned.
  - Seeds 5, 7 and 57 land at (-1040, 304, 63), (160, -1600, 63) and (476, 464, 63). `Find` takes 70, 107, 53 and 98 ms for seeds 1, 5, 7 and 57.
  - Worst case, rings 6-32 all read (4104 densities, parallel): 542 ms.
  - The dev seed's pin (16, -32, 67) and the nearest-path pin hold.
- **Mutation check.** With the fallback's last ring set to `ViewReach / 16`, the new 400-block case fails at view radius 4 and 12. After a byte-exact restore and a rebuild it is green.
- **First real run** (`C:\dev\sandbox\loading-screen\captures\final-133103`, 1280 x 720 offscreen): the screen showed, then hid after 4249 ms (ready). The flight smoke passed (`flight-133019`).
- **Menu units** (`Tests`, `npm test`): 62 of 62, including a new case for a world that stops opening after its message screen.

- **Final checks after the review fixes** (`Scripts\RunChecks.ps1 -Profile final -Flight smoke`, GREEN after 19 min, machine otherwise idle). Summary: `Tests/Evidence/2026-10-11/Checks-final-20261011-134754/summary.txt`.
  - Native: 268 of 268 passed, 0 crashes.
  - Voxel: 174 suites. Hashes: 9 of 9 expected equal.
  - Playground runtime: 119, 81 and 27 checks, all passing.
  - create-world: 38 checks, all passing. The loading screen hid on ready in every world run: 4390 ms (Hello World), 852 ms (Superflat) and 4236 ms (played again).
  - The flight smoke passed; its screen hid after 3169 ms.
  - npm, source-check and the Reference verifiers passed.
- **The first final run** (`Checks-final-20261011-133626`) failed only in create-world. The CEF page closed before the test could read the message screen back: the level change follows the click within a few frames. The test now installs a MutationObserver before the click, and the page reports each screen on its console within the click's own task. The live screenshot stays best effort, and in the green run the page closed before it was taken. The menu screen's captures are a headless Edge render of the generated menu instead: `Tests/Evidence/2026-10-11/LoadingScreen/menu-preparing-create.png` and `menu-preparing-play.png`. The same render confirmed that Esc leaves the screen up.
- **Pre-commit review** (Sonnet 5.5, diff only): no blocking findings. Three minor notes were applied:
  - when the HUD cannot show the screen (no game viewport), nothing is logged or held;
  - the capture also fires when the world is ready before half progress;
  - the Large Biomes test asserts that each seed's pick is wet, so the fallback really runs.
  The create-world readback race it flagged is the failure above.

## 4. Known issues and open items

- Tuning the layout against a vanilla 26.3 capture comes later.
- **The 15 s timeout** was exercised with an existing switch, a default capture at `t.MaxFPS 4` (`C:\dev\sandbox\loading-screen\captures\timeout-fps4-141151`). The log shows `Loading screen hidden after 15142 ms (timeout)`. The run then finished its shots, and build.txt has the player grounded at the spawn cell, 0.0 cm from it.
- **The re-show after `voxel.Seed`** has no existing switch that issues a command after the world is ready (`-ExecCmds` runs at start-up, during the first load), so it is covered by code reading only.
- **Found while looking for a slow load: a crash at large view radii.** A default capture at `voxel.ViewRadius 24` or `32` asserts in the vendored RealtimeMesh plugin, about 10 s into streaming: `RayTracingGeometry->GetRHI()`, "Ray tracing instance must have a valid geometry", in `FRealtimeMeshComponentSceneProxy::GetDynamicRayTracingInstances`, on a background worker. It reproduced at 32 twice and at 24 once (`captures\timeout-140850`, `timeout-r32-140958`, `timeout-r24-141025`). The default radius 12 never crashed in any run, and Options > Render Distance is not wired to `voxel.ViewRadius`. **It predates this change.** The base commit `5f495b9` (the built worktree `Unnamed-spawn`, run read-only) asserts the same way at radius 24 with the same capture (`C:\dev\sandbox\loading-screen\captures\base-r24-141800\game.log`). The shift manager is passing it to Jesus as its own item; it matters for the far-LOD plan.
- The panorama is a still (26.3's turns). The blur is not divided by the DPI scale.
