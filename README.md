# Unnamed Menu Interface

Existing project: `C:\dev\projects\Unnamed\Unnamed.uproject`.
Engine: Unreal Engine 5.8.3. The offline menu implements normal Quit Game and five persistent video settings: Fullscreen, Max Framerate, VSync, GUI Scale and Menu Background Blur. Other settings and gameplay remain mocks. Minecraft Realms and its complete menu flow have been removed. See `Docs\VideoSettings.md` and `Docs\QuitGame.md` for ownership, safeguards and verification. `Docs\Independent_Menu_Review_Prompt.txt` is the read-only independent-review handoff, including the functionality/deferred-scope distinction.

## Launch

Today's concise work record, five independent-review fixes, and known gaps are indexed in [Docs/Documented Actions](<Docs/Documented Actions/README.md>). The earlier independent-review ZIP/prompt remains a preserved pre-fix snapshot, not the current checkpoint.

The private source backup is [jherediaramirez/unnamed](https://github.com/jherediaramirez/unnamed). Checkpoint commands, exclusions, and local recovery instructions are in [GitHub Backup](<Docs/Documented Actions/2026-10-07-GitHub-Backup.md>).

Open `Packages\MenuReviewFixesDevelopment\Windows\Unnamed.exe`, or:

```powershell
& 'C:\dev\projects\Unnamed\Scripts\LaunchMenu.ps1'
```

The application opens directly into the menu. The five real settings save when changed and restore at launch. Hard Reset is temporary debugging UI at the bottom of Video Settings and requires confirmation; it restores preferences without deleting world saves. Quit Game completes pending settings changes, flushes preferences and requests normal Unreal shutdown. A failed save cancels quitting and reports the error. Quit Game is disabled in editor Play so it cannot close Unreal Editor. The editor opens the empty `L_Menu` map; use Play to show the interface. Native window/FPS/VSync controls require standalone execution, not an embedded editor Play viewport.

## Rebuild

Visual Studio 2022 with C++ and Windows SDK, and UE 5.8 are required. Node is only used by the adaptation/verification scripts; it is not a runtime UI framework or server.

```powershell
& 'C:\dev\projects\Unnamed\Scripts\BuildMenu.ps1' -CreateMap -Package -PackageName MenuNextDevelopment
```

The map script preserves an existing map rather than replacing it. The adapter validates the unchanged reference hash and removes browser effects. HTML and all assets are staged as physical NonUFS files for offline Chromium loading.

## Verify

```powershell
Set-Location 'C:\dev\projects\Unnamed\Tests'
npm ci --ignore-scripts
node source-check.mjs
& '..\Scripts\LaunchMenu.ps1' -Diagnostics -SessionOnly
node video-settings.mjs --port=18888 --profile=1920x1080 --capture=next-video-1920x1080
node smoke.mjs --mode=next-packaged
node quit-game.mjs --report=NextQuitResults.json
```

Close the running test window before relaunching another profile on the same debug port. For the smaller profile launch with `-Width 1280 -Height 720`, then run `node smoke.mjs --mode=next-packaged --profile=1280x720`. Chromium diagnostics use loopback and are enabled only by the diagnostic launch option, not ordinary launch. Native editor MCP is at `http://127.0.0.1:18777/mcp`; `Scripts\Invoke-UnrealMCP.ps1` is the rerunnable bridge. Use unique capture/report names when independently rerunning checks so accepted evidence is not overwritten. `video-persistence.mjs` and `supplement.mjs` target earlier candidates and are historical suites, not current acceptance commands.

The owned `node review-validation.mjs --prefix=next-menu-review` and `node review-regression.mjs --capture=next-drafts-1280x720` suites launch and close their own current-candidate diagnostic games. Registry generation explicitly selects matching successful captures, for example `node registry.mjs --capture=next-menu-review-packaged-1920x1080 --video-capture=next-menu-review-video-1920x1080`. Existing capture/report names are rejected to preserve accepted evidence.

The old UI-only capture suites and their results remain historical evidence, not current functional-settings acceptance. Use `Scripts\TestVideoSettings.ps1`, `npm test` and the current video suites for this stage. See `Docs\Verification.md` for evidence, `Docs\KnownDifferences.md` for limitations, and `Docs\FunctionalBacklog.md` for future work. The unchanged reference and provenance are under `Reference\minecraft-menu`. Keep the source asset provenance and rights information with distributions; this interface does not grant rights to Minecraft branding/assets.
