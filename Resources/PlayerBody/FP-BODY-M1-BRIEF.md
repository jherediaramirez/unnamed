# Brief: first-person body, M1 (Unnamed Player in the game)

You are starting **M1 of the full-body first/third-person plan** for Jesus's Unreal 5.8.3 game in
`C:\dev\projects\Minecraft 26.3` (engine project still called `Unnamed`). The goal Jesus states: unlike Java Minecraft,
the player always has a real body. A really good third person, and an even better first person: look down and you see
your own legs and feet, with your head hidden only from your own camera.

## Read first, in this order
1. `C:\dev\projects\Minecraft 26.3\CLAUDE.md` and `_START-HERE.md` (project rules, commands, the build command).
2. **The plan:** `C:\dev\output\unnamed-fp-tp-integration-plan-20261015\PLAN.md`. Sections 0 (Jesus's decisions),
   1 (design and M1), 2.2-2.4 (trace, what the 22-bone set breaks, sources of truth), 3 (architecture, visibility,
   camera/body numbers), 4.3-4.5 (Unreal import, gates, rollback), 5 (animation timing), 7 (M1a/M1b acceptance and
   stop conditions). **Section 2.1 is stale:** every lane it lists has since been merged into `main`.
3. The M0 result (the proven import procedure to repeat in the game):
   `C:\dev\output\unnamed-player-export-v002c-20261016\REPORT.md` and its `evidence\final_20261016_015709\REPORT.md`.
4. The current body in the game: `Docs/Records/2026-10-13-Player-Body.md`, `Docs/Foundation/Architecture.md` (section
   "Player body and the third-person views"), `Source/Unnamed/Foundation/FoundationBody*.{h,cpp}`, `FoundationPawn.*`.

## Where things stand (checked 2026-10-16 evening)
- **M0 is done** (2026-10-16 ~06:00 UTC, every gate passed): 72 assets imported into the sandbox project
  `C:\dev\sandbox\fpbody-m0` under `/Game/Characters/UnnamedPlayer` (22 bones, 180 cm, faces +Y, 66 clips including
  18 additive looks, the pickaxe, grip sockets). That sandbox is only a proof; nothing is in the game yet.
- **M1 was waiting for survival QC and the final merge.** The merge happened on 2026-10-16: `main` is now the only
  branch (local and GitHub) and the only folder is `C:\dev\projects\Minecraft 26.3`. Jesus has decided to start M1 now.
  CLAUDE.md's "no new features until Jesus decides on the open bugs" is answered for this work; leave a dated note in
  `_START-HERE.md` saying so.
- The game today draws **Better Steve** (`Art/Characters/BetterSteve/SK_BetterSteve.fbx`, imported to
  `/Game/Characters/BetterSteve` by `Scripts/Editor/ImportBetterSteve.py`) in F5 only; first person shows the held
  item and no body. Better Steve stays as `foundation.Body.Set 0`.

## The character and its files (in the repo since 2026-10-16: `Resources/PlayerBody/`)
Everything below was copied into `Resources/PlayerBody/` (index: `Resources/README.md`), keeping the original folder
names: `C:\dev\output\<name>` in PLAN.md is `Resources\PlayerBody\<name>`, and `C:\dev\projects\better-steve` is
`Resources\PlayerBody\better-steve`. Work from the repo copies.
- **Unnamed Player (UP):** a real Minecraft Steve. Source: "Minecraft Steve (Rigged)" by Bully Magraptor, Sketchfab,
  **CC-BY-4.0, attribution required** (`unnamed-player-motion-polish-v002-20261015/source/PROVENANCE.md`). Rebuilt in
  Blender 5.2.2 (`C:\dev\tools\blender-5.2.2\blender.exe`) with a 22-bone rig and 66 clips.
- **Export source (accepted):** `unnamed-player-motion-polish-v002-20261015/blend/UP_Player_arm_polish_v002.blend`,
  SHA-256 `26E585D5676D5A8A18A6E6A2FAE5D682DF3470FA7B83A01972CFAE138D4D22C7` (the repo copy matches). Not
  `UP_Player_master_v002.blend` (experimental) and not the v001 exports (old strikes): see PLAN.md §2.4.
- **Accepted export:** `unnamed-player-export-v002c-20261016/fbx/` (SK_UnnamedPlayer.fbx, anims/A_UP_*.fbx,
  SM_UP_Pickaxe.fbx, T_UP_Skin_64/1024.png, UP_manifest_v002c.json), with its export script in `../scripts/`.
- Builder package (scripts, validation, review .blend): `unnamed-player-blender-v001-20261015/`.
- Better Steve's source (the current F5 body): `better-steve/` (its original folder is not a git repo).

## Work, in order. Stop and ask Jesus at each "ask".
1. **Put the engine's import copy in place:** `Art/Characters/UnnamedPlayer/` gets the v002c FBXs, textures and a
   `manifest.json` with hashes (like `Art/Characters/BetterSteve/`), copied from `Resources/PlayerBody/`. The sources
   stay in `Resources/` (Jesus's choice over `foundry/`). Add the CC-BY-4.0 credit where the game shows credits (find
   the place; ask if none exists).
2. **Game import:** `Scripts/Editor/ImportUnnamedPlayer.py`, repeating M0's proven procedure (PLAN.md §4.3-4.4 and
   the v002c REPORT: Interchange; the armature object named `Armature`; `SOCKET_*` leaf nodes become sockets; the 20
   pose clips with fix (a): `animation_range = SET_RANGE`, `frame_import_range = 0..2`,
   `custom_bone_animation_sample_rate = 60`; looks mesh-space additive). It verifies an existing folder and never
   replaces it, like the other import scripts. The asset path lives only in `UUnnamedProjectSettings` (and
   `Scripts/Editor/ProjectPaths.py`). Run it with the editor closed; check the same gates M0 checked.
3. **M1a: the body live in both views, no tools** (PLAN.md §7): switches `foundation.Body.Set 0|1` and
   `foundation.Body.FirstPerson 0|1`. Two synchronized components (WorldBody, FirstPersonBody), one animator. Meet
   every row of the M1a acceptance table with numbers. **Stop condition:** crouched neck clearance under 15 cm means
   report with numbers and shots; don't improvise.
4. **M1b: StrikeA through the real right hand** on the existing 6-step swing clock (contact frame 9 of 18 at step 3),
   with the camera-held pickaxe hidden while the body holds it. Meet the M1b table.
5. Taste calls (anything the plan marks *proposed*, how it feels in first person): **ask** Jesus, with shots or
   short captures he can compare. He plays in the editor himself; give him exact steps to try.

## How to work (Jesus's rules since 2026-10-16)
- **One folder, one branch.** Work in `C:\dev\projects\Minecraft 26.3`. A short-lived branch per milestone is fine
  (`feature/fp-body-m1a`, then `-m1b`). Merge each into `main` only after the build, the full native suite
  (`Automation RunTests Unnamed`, 585 tests at the start) and `Scripts\TestFoundationRuntime.ps1 -Editor -Suite body
  -FrameRates 60 -FullCostRendering` are green. Then push `main` and delete the branch. No worktrees, no parallel
  lanes, no side sessions unless Jesus asks.
- **Builds need the editor closed** (Live Coding blocks UBT). Jesus may be play-testing: ask before closing anything.
- Format C++ with the repo `.clang-format`; keep new logic in new files (`FoundationBodyPose.*`, `FoundationBodyGrip.*`)
  so the hooks in `FoundationPawn` and `FoundationInteractionView` stay small. Gameplay changes go through
  `FFoundationAuthority` commands, never the view. The simulation must stay bit-identical with the body on or off.
- Never rename saved IDs; never edit `Reference/*` or `Plugins/RealtimeMeshComponent/`; evidence goes under a fresh
  `Tests/Evidence/<date>/` name and is never overwritten.
- Write a record `Docs/Records/<date>-Player-Body-M1.md`, update `Docs/Foundation/Architecture.md` and
  `_STATUS/FEATURES-BUILT-AND-MISSING.md`. A bug you find goes in `_STATUS/KNOWN-ISSUES.md` and
  `_STATUS/LATEST-REPORT.md` in the same commit as its fix.
- An importer "success" or a plausible screenshot proves nothing: report measured numbers, and say plainly what was
  not checked.

## Done for this session
M1a merged into `main` and pushed with every acceptance row measured and green, and Jesus has the steps to try it.
Then M1b the same way, or stop at M1a if Jesus says so. M2-M4 (rest of the library, new clips, polish) are later
sessions.
