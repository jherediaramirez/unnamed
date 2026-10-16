# Unnamed - project notes

Unreal Engine 5.8.3 game project with an offline CEF menu generated from an unchanged reference. Two C++ modules: the game module `Unnamed`, and `UnnamedVoxel`, the voxel world's plain C++ core (depends on `Core` only). Read [Docs/Architecture.md](Docs/Architecture.md) for how it fits together and [Docs/NamingConventions.md](Docs/NamingConventions.md) before adding or renaming anything. The voxel world (chunk loader and generator) is built in phases; status and decisions are in [Docs/VoxelWorld.md](Docs/VoxelWorld.md). Picking up that work in a new chat: start with the records index, [Docs/Records/README.md](Docs/Records/README.md), which lists the current handoff.

**Project state (since 2026-10-16):** this folder (`C:\dev\projects\Minecraft 26.3`) is the only checkout; read [_START-HERE.md](_START-HERE.md) first. The bug list and latest report are [_STATUS/KNOWN-ISSUES.md](_STATUS/KNOWN-ISSUES.md) and [_STATUS/LATEST-REPORT.md](_STATUS/LATEST-REPORT.md): update both in the same commit as a fix. No new features until Jesus decides on the open bugs.

## Rules

- `Reference/minecraft-menu/`, `Reference/minecraft-blocks-26.3/`, `Reference/minecraft-worldgen-26.3/`, `Reference/minecraft-items-26.3/`, `Reference/minecraft-sky-26.3/`, `Reference/minecraft-entities-26.3/` and `Reference/minecraft-sounds-26.3/` are hash-checked and never edited. The worldgen facts are regenerated only through `tools/WorldgenFacts.java`, then `build_reference.py`. The item reference is regenerated only through its `tools/build_reference.py` (the 26.3 client jar and the data generator's item components report), after moving the accepted folder away. The sky reference is regenerated only through its `tools/build_reference.py` (the 26.3 server jar's timelines, dimension types and clocks, the client jar's celestial textures, the data generator's registries report and scout N's sweep), after moving the accepted folder away. The entity reference is regenerated only through its `tools/build_reference.py`, after moving the accepted folder away. The sound reference is regenerated only through its `tools/build_reference.py`, and only by appending since the survival batch. `Content/Menu/Web/index.html` and `preferences-schema.menuprefs` are generated: change `MenuWeb/Patches/*.mjs` or `MenuWeb/Runtime/*.js`, then run `node MenuWeb/AdaptMenu.mjs`.
- Map, world and input asset paths live only in `UUnnamedProjectSettings` (Python: `Scripts/Editor/ProjectPaths.py`; cooking: `MapsToCook` in `Config/DefaultGame.ini`).
- The Playground (`playground`, `/Game/Maps/L_Playground`) is the one built-in world; see [Docs/Playground.md](Docs/Playground.md). `/Game/Maps/L_Generated` (`UUnnamedProjectSettings::GeneratedMap`) is the generated voxel world's map, not a catalog world: Create New World's saved worlds (`Saved/Worlds/<id>/world.json`, `Source/Unnamed/Worlds/`) open it as `L_Generated?VoxelWorld=<id>`; opened plainly it streams the dev seed's world. The 26.3 blocks and their items come from the generated registry asset `DA_Blocks` (`UUnnamedProjectSettings::BlockRoot`; see [Docs/Blocks.md](Docs/Blocks.md)); the other 26.3 items (tools, materials, buckets, food) come from `DA_Items` (`UUnnamedProjectSettings::ItemRoot`, `/Game/Items`; see [Docs/Items.md](Docs/Items.md)). `UFoundationInteractionSettings` holds only project blocks and items, placement presets and the loadout. Gameplay changes go through `FFoundationAuthority` commands, never presentation or the HUD. Block and item IDs and the world ID `playground` are saved IDs: item IDs are the 26.3 item names; a block's item has the block's id. Retired IDs are never reused: worlds `foundation`, `super-flat` and `interaction-lab`; block/item `dummy_block` and `dummy_block_item`; items `practice_pickaxe` and `practice_sword`.
- Never rename saved IDs (`key.*` binding IDs such as `key.sneak`, world IDs, saved world folder ids, the input save slot) or `UUnnamedInputSettings`: players' saved settings and worlds depend on them. world.json's keys keep the Create screen's draft keys; change its layout only with a `version` bump and a migration.
- `Source/UnnamedVoxel/` stays UObject-free and deterministic: every random value is a pure function of (seed, salt, coordinates). `Plugins/RealtimeMeshComponent/` is vendored upstream source at a pinned commit and never edited; update it by replacing the folder (see Docs/VoxelWorld.md).
- Evidence under `Tests/Evidence/<date>/` and existing `Packages/` builds are never overwritten; use fresh names.
- Format C++ with the repository `.clang-format` (`C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe`).
- Commits run `Scripts/TestRepositorySafety.mjs` (credential scan) through `.githooks`.

## Commands

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' UnnamedEditor Win64 Development -Project="$PWD\Unnamed.uproject" -WaitMutex
Set-Location Tests; npm test; node source-check.mjs                  # menu runtime units, reference/asset checks
& Scripts\TestMenu.ps1; & Scripts\TestInput.ps1                         # native suites (Unnamed.Menu / Unnamed.Input)
& Scripts\TestVoxel.ps1                                                  # voxel.Info/Seed/Bench, then Unnamed.Voxel
uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/verify_reference.py   # worldgen reference hashes, references, spline facts
& Scripts\CaptureVoxel.ps1 [-Seed <text>] [-World <id>] [-ExecCmds '<cvars>'] [-Edits] [-Caves] [-Flight [-FlightSeconds <s>]] [-SpawnAt X,Y [-SpawnExact]] [-NoSurfaceRules]   # L_Generated offscreen: 5 screenshots + build.txt (first-view timings, lighting, GPU ms, world); -Edits adds an edit shot; -Caves adds a cave view (one reaching open sky) and a lava view; -Flight flies the acceptance path instead (flight.csv, flight.txt, 4 shots: holes, budget, memory)
Set-Location Tests; node create-world.mjs --editor                    # Create New World end to end: create, list, rename, re-create, play, delete
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -run=pythonscript "-script=Scripts\Editor\CreateGeneratedMap.py" -unattended -nop4 -nosplash   # /Game/Maps/L_Generated (verifies, never rewrites)
& Scripts\TestFoundationRuntime.ps1 -Editor -FrameRates 60            # Playground probe (movement + interaction)
& Scripts\TestFoundationRuntime.ps1 -Editor -Suite interaction -FrameRates 60   # Playground interaction scenarios
Set-Location Tests; node foundation-menu.mjs --editor                 # CEF + native end to end
& Scripts\BuildFoundation.ps1 -Package -PackageName <FreshName>       # full candidate package (also runs the two map/material scripts below)
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -run=pythonscript "-script=Scripts\Editor\CreateDevGridMaterial.py" -unattended -nop4 -nosplash
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -run=pythonscript "-script=Scripts\Editor\CreatePlaygroundMap.py" -unattended -nop4 -nosplash
uv run --with pillow python Scripts/Editor/CreateDevGridTexture.py   # regenerate Art/DevGrid/T_DevGridMarkup.png
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-blocks-26.3/tools/verify_reference.py   # block reference hashes
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -run=pythonscript "-script=Scripts\Editor\ImportBlocks.py" -unattended -nop4 -nosplash          # /Game/Blocks (verifies, never replaces)
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-items-26.3/tools/verify_reference.py   # item reference hashes, re-derivation, spot values
uv run --no-project --python 3.13 python -I Reference/minecraft-sky-26.3/tools/verify_reference.py   # sky reference hashes, re-derivation, the timelines against scout N's sweep
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -run=pythonscript "-script=Scripts\Editor\ImportItems.py" -unattended -nop4 -nosplash           # /Game/Items: DA_Items and the item sprites (verifies, never replaces)
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' Unnamed.uproject -run=pythonscript "-script=Scripts\Editor\CreateBlockMaterials.py" -unattended -nop4 -nosplash  # block materials
& Scripts\TestFoundationRuntime.ps1 -Editor -Suite tour -FrameRates 60   # screenshot tour incl. block gallery and creative inventory
```

Run every native suite at once with `UnrealEditor-Cmd.exe Unnamed.uproject -ExecCmds="Automation RunTests Unnamed" -TestExit="Automation Test Queue Empty" -unattended -NullRHI`.
