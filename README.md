# Unnamed Menu Interface

Existing project: `C:\dev\projects\Unnamed\Unnamed.uproject`.
Engine: Unreal Engine 5.8.3. This phase is an offline, session-only Minecraft menu interface, not gameplay or real settings integration.

## Launch

Open `Packages\MenuDevelopment\Windows\Unnamed.exe`, or:

```powershell
& 'C:\dev\projects\Unnamed\Scripts\LaunchMenu.ps1'
```

The application opens directly into the menu. Quit Game opens a reversible mock endpoint. Close the OS window to end the application. The editor opens the empty `L_Menu` map; use Play to show the interface.

## Rebuild

Visual Studio 2022 with C++ and Windows SDK, and UE 5.8 are required. Node is only used by the adaptation/verification scripts; it is not a runtime UI framework or server.

```powershell
& 'C:\dev\projects\Unnamed\Scripts\BuildMenu.ps1' -CreateMap -Package
```

The map script preserves an existing map rather than replacing it. The adapter validates the unchanged reference hash and removes browser effects. HTML and all assets are staged as physical NonUFS files for offline Chromium loading.

## Verify

```powershell
Set-Location 'C:\dev\projects\Unnamed\Tests'
npm ci --ignore-scripts
node source-check.mjs
& '..\Scripts\LaunchMenu.ps1' -Diagnostics
node smoke.mjs
node supplement.mjs
node registry.mjs
```

Close the running test window before relaunching another profile on the same debug port. For the smaller profile launch with `-Width 1280 -Height 720`, then run `node smoke.mjs --profile=1280x720`. Chromium diagnostics use loopback and are enabled only by the diagnostic launch option, not ordinary launch. Native editor MCP is at `http://127.0.0.1:18777/mcp`; `Scripts\Invoke-UnrealMCP.ps1` is the rerunnable bridge.

See `Docs\Verification.md` for evidence, `Docs\KnownDifferences.md` for limitations, and `Docs\FunctionalBacklog.md` for future work. The unchanged reference and provenance are under `Reference\minecraft-menu`. Keep the source asset provenance and rights information with distributions; this interface does not grant rights to Minecraft branding/assets.
