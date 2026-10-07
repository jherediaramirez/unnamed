# Personal controls restored — October 7, 2026

The first Foundation setup incorrectly replaced the project's established **Left Shift sprint / C crouch** defaults with vanilla Minecraft's Ctrl/Shift defaults. The existing decision was already documented in [Keybindings](../Keybindings.md) and [Input v2](2026-10-07-Input-v2.md). This correction restores the personal layout in the native mapping asset, fresh profiles, the existing Controls labels, Reset One and Reset All.

Minecraft's original bindings remain in the sourced reference matrix. The structured matrix records the project's two deliberate overrides separately. Movement physics, sprint eligibility/latching, hold/toggle policies, jump behavior and collision are unchanged. In particular, releasing the sprint key alone still leaves an eligible forward sprint latched; that is the existing researched movement contract, independent of whether the key is Shift or Ctrl. Fixed context modifiers such as Ctrl+Drop keep their documented behavior.

## Changed

- `Scripts/CreateInputAssets.py` authors Left Shift sprint and C crouch for missing assets.
- `Scripts/SetupFoundation.py` restores only the known accidental Ctrl/Shift factory mappings; other authored keys are preserved.
- The installed Unreal editor regenerates `Content/Input/MappingContexts/IMC_OnFoot.uasset`. No binary asset was fabricated.
- Native input and rendered runtime regression checks now exercise the personal layout, including C versus F3+C.
- The launch/test scripts and current launch documentation select the new r3 candidate. The research and historical r2 evidence remain available.

The native Enhanced Input profile remains the single runtime binding authority. Saved custom choices are not forcibly overwritten. If an existing profile explicitly saved different keys, use the Crouch and Sprint row Reset buttons to adopt C/Left Shift without resetting unrelated bindings. Restart an already-open editor or old game process to load the rebuilt module and assets.

## Play

`C:\dev\projects\Unnamed\Packages\FoundationDevelopment20261007-r3\Windows\Unnamed.exe`

```powershell
& 'C:\dev\projects\Unnamed\Scripts\LaunchFoundation.ps1'
```

Choose **Singleplayer → Player Foundation → Play Selected World**. For the editor build, pass `-Editor`; current module suffix is `2047`.

Inner executable SHA-256: `07D89D1FDDAFBCD7524DD80A7BDA2253DD72E12999D0FA4F17074FF4B4DDCF14`.

## Verification

Evidence root: `C:\dev\projects\Unnamed\Tests\Foundation\PersonalControls-20261007`.

| Check | Result | Evidence relative to that root |
|---|---|---|
| UE 5.8.3 editor build, supported asset setup, Win64 Development build/cook/package | Passed | `BuildCookPackage-retry.log` |
| Native automation, including actual Enhanced Input routing, defaults, resets, serialization and movement suite | 13 suites passed; zero failures/warnings | `NativeAutomation/index.json` |
| Menu JavaScript regression suite | 44 passed | `JavaScriptTests.log` |
| Packaged CEF/native bridge: fresh defaults and labels, reset-one/all, rebinding, conflict/cancel, save/relaunch, three menu/map trips | 30 checks passed | `PackagedMenu/results.json` |
| Rendered packaged runtime: Shift sprint without crouch, C crouch, Ctrl no sprint, F3+C consumed, low ceiling, movement and existing action/routing probes | 38 checks passed | `PackagedRuntime/runtime-60.json` |
| Existing r2 custom profile copied into an isolated r3 user directory | Five custom bindings, toggle policy and debug scale survived; new reset defaults are Left Shift/C | `Migration/read.json`, `Migration/SourceProfileHashes.json` |
| Historical package executables and all three existing maps | All 31 protected files unchanged | `Preservation.json` |
| Git staged entries | Unchanged | `Preservation.json` |

The rendered runtime was requested at a 60 FPS cap and achieved a whole-run mean of **46.87 FPS**; this is not a sustained-60 performance claim. Measured sprint remained **5.61233480 blocks/s**, crouch **1.29515418 blocks/s**, matching the unchanged baseline within the original 1% tolerances. The prior 30/60/120 cadence study remains historical evidence; this focused default-key correction did not repeat it. The initial attempt found a field-name error in the newly added probe assertions; that was fixed before the successful build and checks. The failed build log is retained, not counted as a pass.

These checks inject application input or CEF events and exercise real Unreal settings/gameplay. Physical OS keyboard input, alternate layouts and subjective feel are still manual acceptance checks. In the new candidate, verify W+Left Shift sprint, C crouch/release, held C under the low ceiling, F3+C without crouch, then Controls row resets and relaunch. Remaining general foundation limitations are in [the baseline verification report](../Foundation/VERIFICATION.md).

Before edits, the dirty source/assets/docs, test scripts, Git index and diffs were preserved in `C:\dev\backups\Unnamed\personal-controls-20261007-154411`. All prior packages and maps were retained. Movement implementation files and unrelated menu/settings behavior are unchanged; no OS firewall or security setting was altered.
