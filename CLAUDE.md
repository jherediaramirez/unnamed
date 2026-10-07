# Unnamed - project notes

Unreal Engine 5.8.3 game project (one C++ module, `Unnamed`) with an offline CEF menu generated from an unchanged reference. Read [Docs/Architecture.md](Docs/Architecture.md) for how it fits together and [Docs/NamingConventions.md](Docs/NamingConventions.md) before adding or renaming anything.

## Rules

- `Reference/minecraft-menu/` is hash-checked and never edited. `Content/Menu/Web/index.html` and `preferences-schema.menuprefs` are generated: change `MenuWeb/Patches/*.mjs` or `MenuWeb/Runtime/*.js`, then run `node MenuWeb/AdaptMenu.mjs`.
- Map, world and input asset paths live only in `UUnnamedProjectSettings` (Python: `Scripts/Editor/ProjectPaths.py`; cooking: `MapsToCook` in `Config/DefaultGame.ini`).
- Never rename saved IDs (`key.*` binding IDs such as `key.sneak`, world IDs, the input save slot) or `UUnnamedInputSettings`: players' saved settings depend on them.
- Evidence under `Tests/Evidence/<date>/` and existing `Packages/` builds are never overwritten; use fresh names.
- Format C++ with the repository `.clang-format` (`C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe`).
- Commits run `Scripts/TestRepositorySafety.mjs` (credential scan) through `.githooks`.

## Commands

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' UnnamedEditor Win64 Development -Project="$PWD\Unnamed.uproject" -WaitMutex
Set-Location Tests; npm test; node source-check.mjs                  # menu runtime units, reference/asset checks
& Scripts\TestMenu.ps1; & Scripts\TestInput.ps1                         # native suites (Unnamed.Menu / Unnamed.Input)
& Scripts\TestFoundationRuntime.ps1 -Editor -FrameRates 60            # in-game Foundation probe
Set-Location Tests; node foundation-menu.mjs --editor                 # CEF + native end to end
& Scripts\BuildFoundation.ps1 -Package -PackageName <FreshName>       # full candidate package
```

Run every native suite at once with `UnrealEditor-Cmd.exe Unnamed.uproject -ExecCmds="Automation RunTests Unnamed" -TestExit="Automation Test Queue Empty" -unattended -NullRHI`.
