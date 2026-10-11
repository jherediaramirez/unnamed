# Player Foundation — Java 26.3 baseline

This is the first-person foundation inside `C:\dev\projects\Unnamed\Unnamed.uproject`, built with Unreal 5.8.3. It uses a feet-origin, world-aligned box pawn and a 20 Hz movement simulation, while mouse look and rendering remain independent. The existing CEF main menu and Controls screens drive the native Enhanced Input profile. The **Playground** world ([Playground](../Playground.md)) adds session-only placement and mining of the Java 26.3 blocks, the creative inventory and practice melee over the same pawn ([interaction record](../Records/2026-10-08-Interaction.md)); no character model, survival loop, terrain generation or networking is included.

## Play

No packaged candidate contains the Playground yet. The newest package, `C:\dev\projects\Unnamed\Packages\InteractionDevelopment20261008-r3\Windows\Unnamed.exe`, still has the retired Interaction Lab. `VitalsDevelopment20261007-r2`, `HudDevelopment20261007-r2`, `CodeReviewDevelopment20261007` and `FoundationDevelopment20261007-r3` remain preserved checkpoints.

From PowerShell 7 (the default `-PackageName` is the r3 candidate, which predates the Playground):

```powershell
& 'C:\dev\projects\Unnamed\Scripts\LaunchFoundation.ps1'
```

Choose **Singleplayer → Playground → Play Selected World**. Double-click and Enter also work. The Playground's controls and stations are in the [interaction record](../Records/2026-10-08-Interaction.md#play) and [Playground](../Playground.md). Escape opens the existing menu with Resume, Options and Return to Main Menu. Change keys through Options → Controls → Key Binds; the changes apply to the player and save through Unreal.

Direct lab or editor executable:

```powershell
& 'C:\dev\projects\Unnamed\Scripts\LaunchFoundation.ps1' -DirectLevel
& 'C:\dev\projects\Unnamed\Scripts\LaunchFoundation.ps1' -Editor -DirectLevel
```

The native map is `/Game/Maps/L_Playground`. Normal project startup remains `/Game/Menu/L_Menu`. There is no change to the reference HTML file. Existing saved custom bindings take precedence over factory defaults; Reset Keys restores the project's personal layout, including **Left Shift sprint and C crouch**. The reference matrix separately records vanilla's original keys.

## Try the foundation

The Foundation HUD uses the original Java 26.3 sprites and ASCII bitmap text and, since HUD milestone 2, the player's real session-only vitals: hearts, armor, food, air bubbles, XP and the attack indicator follow Java 26.3 rules for damage, healing, hunger, drowning, falls, experience and attack recovery. Sprinting and jumping cost food, food 6 or less stops sprinting, and death respawns at the origin after one second. **PREVIEW ITEMS** marks the hotbar: slot selection is real input over the existing token test double, rendered with an original cyan placeholder. F3+F6 → PLAYER VITALS sets mode, difficulty and values and applies hits, healing, food and XP; the milestone-1 preview fixture (effects, Hardcore, timed sequences) is opt-in below it. No effects, equipment, items, combat against mobs or persistence is implied. See [player vitals](../Records/2026-10-07-Vitals.md) and [HUD milestone 1](../Records/2026-10-07-Hud.md).

| Input at factory defaults | Result |
|---|---|
| W/A/S/D, mouse | Move/look; opposite directions cancel |
| Space | Jump; holding repeats when the grounded cooldown permits. Creative: double-tap to fly (hold Space to rise, C to sink); landing ends flight |
| Left Shift + forward, or double-tap current Forward | Start sprint; releasing Shift alone does not end the sprint latch |
| C | Crouch; lower collision/eyes, lower speed and ledge protection |
| 1–9 / wheel | Select a diagnostic hotbar slot; wheel wraps |
| Left / right / middle mouse | Attack/break, use/place and pick: real block actions in the Playground; **contract fixtures** only in a plain lab with no lab actor, with real eye targeting |
| Q / Ctrl+Q / F | Drop one token / stack / swap selected tokens with offhand; in the Playground Q and Ctrl+Q are refused (no item entities) and F swaps the real offhand item |
| E | Open/close the inventory input fixture |
| T / slash | Local chat/command text fixture; Enter records text, Escape cancels |
| F1 / F2 / F5 | Hide HUD / engine screenshot / camera-only first-rear-front views |
| F3 | Passive diagnostic overlay; toggles on release |
| F3+F6 | Debug Options: text scale, section visibility, coordinates, Copy Snapshot, player vitals and HUD preview controls |
| F3+B / F3+C / F3+D / F3+V | Collision outlines / copy snapshot / clear event history / version event |

In the Playground the mouse buttons, hotbar, pick, drop and swap act on real blocks, the 26.3 tools and training targets instead of the fixtures; drop is refused because there are no item entities.

The debug modifier and supported debug keys are configurable. The F3 chord consumes its secondary key, including when perspective is rebound to F6. The unimplemented vanilla rows stay visibly Planned. The fixture HUD gives token counts; the overlay gives action acceptance/rejection and target distance. Screenshots use the engine's Saved/Screenshots location, reported by the runtime log.

The movement course starts 42 m east of spawn; its origin is 60 m east (`(6000, 0, 0)` cm). It has 50/60/61/100 cm steps at 63 to 74 m; a 155 cm ceiling at 63 to 68 m; 59 cm (blocked) and 61 cm (clear) gaps at 53 to 57 m; a 2 m raised ledge with 50/100/150 cm access steps at 42 to 50 m; a four-block jump stair and a five-block drop (fall damage) at 75 to 82 m; and a water column (air and drowning, no swimming physics) at 75 to 78 m. [Playground](../Playground.md) lists every zone. One block is exactly 100 cm. Falling below the lab resets to spawn as an explicit test-level convenience.

## Evidence and design

- [Audit and implementation order](Audit.md)
- [Pinned reference and source symbols](Reference.md)
- [Complete controls matrix](Controls.md) and [structured matrix](controls.json)
- [Scale, movement and action contracts](Behavior.md)
- [Architecture and extension points](Architecture.md)
- [Measured verification and remaining acceptance gaps](Verification.md)
- [Personal controls correction and new candidate verification](../Records/2026-10-07-Personal-Controls.md)
- [Durable test evidence](../../Tests/Evidence/2026-10-07/Foundation/FinalResults.json)

The numeric Minecraft comparison is derived from the inspected hash-pinned client implementation. It is not a claim that a played vanilla client was instrumented. Exact auto-jump fidelity outside the primitive fixture, arbitrary mesh collision, alternate keyboard layouts, OS focus/capture and subjective mouse feel retain the limits listed in the report.

## Rebuild and reproduce

Use a fresh package name so previous candidates remain intact:

```powershell
& 'C:\dev\projects\Unnamed\Scripts\BuildFoundation.ps1' -Package -PackageName 'Foundation-MyCandidate'
```

If another editor holds an old hot-reload DLL, pass a fresh numeric `-EditorModuleSuffix` (the personal-controls candidate uses 2047). The build script regenerates the menu, builds C++, creates the native input, dev grid and Playground assets through the installed editor Python API and cooks the entry maps in `MapsToCook` (`L_Menu`, `L_Playground`). It does not fabricate binary assets or overwrite an existing package candidate.

Native suites:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'C:\dev\projects\Unnamed\Unnamed.uproject' -unattended -nop4 -NullRHI '-ExecCmds=Automation RunTests Unnamed; Quit' '-TestExit=Automation Test Queue Empty' '-ReportExportPath=C:\dev\tmp\Foundation-New-Native-Report' '-UserDir=C:\dev\tmp\Foundation-New-Native-User/'
```

Rendered standalone probes at requested caps (the JSON records the frame rates actually achieved):

```powershell
& 'C:\dev\projects\Unnamed\Scripts\TestFoundationRuntime.ps1' -OutputDirectory 'C:\dev\tmp\Foundation-New-Runtime-Report'
```

`-Suite interaction` runs only the Playground's interaction scenarios (`-Preset LucidFluid` for the experimental preset; `-FrameRates 15` for the stress cap). This uses reduced rendering cost to test cadence, not a performance-quality preset for normal play. Add `-FullCostRendering` for the project defaults or `-Editor` for an editor executable game session. The probe injects application input through the real Enhanced Input/controller pipeline, and must not be described as OS hardware input testing.

CEF/native menu, capture, save/relaunch and three map round trips:

```powershell
Set-Location 'C:\dev\projects\Unnamed'
node Tests/foundation-menu.mjs --output=C:\dev\tmp\Foundation-New-Menu-Report
npm --prefix Tests test
```

The CEF test uses the installed Playwright dependency and loopback debug port 18944. It creates an isolated user directory, changes only that test profile and normally quits the owned process. Its diagnostic screenshots and JSON are retained. Neither test alters OS permission prompts.
