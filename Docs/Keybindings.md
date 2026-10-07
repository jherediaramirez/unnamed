# Keybindings foundation

Reviewed and implemented October 7, 2026 in `C:\dev\projects\Unnamed\Unnamed.uproject`, UE 5.8.3.

The binding settings and input-intent foundation are implemented. There is still no FPS character, movement simulation, camera controller or animation system. Native automated key events verify the input route; character gameplay remains a future acceptance gate. The follow-up work record distinguishes real Windows input verification from browser-injected tests.

## Review findings

The menu-only controller creates no pawn and deliberately uses UI-only input. All 62 keybinding rows previously changed browser memory: reload discarded them, and they did not drive native Unreal input. The project already selected Enhanced Input engine classes but had no explicit module dependency, input actions, mapping context or remapping service. Capture used `event.key`, collapsed left/right modifier keys, and treated Escape as unbinding. Conflict detection compared unrelated debug, spectator and gameplay bindings as one flat list.

The original Minecraft defaults assigned Shift to Sneak and Ctrl to Sprint. The project defaults now follow the requested arrangement: Shift sprints and C crouches. The immutable reference remains unchanged. Existing custom bindings remain user choices; use the Crouch row's Reset to adopt C.

## Defaults and behavior

| Binding | Default | Native action | Intent |
|---|---|---|---|
| Walk Forward / Backward | W / S | IA_Move, Y +/- | Movement request |
| Strafe Left / Right | A / D | IA_Move, X -/+ | Movement request |
| Sprint | Left Shift | IA_Sprint | Sprint requested; Hold by default |
| Crouch | C | IA_Crouch | Crouch requested; Hold by default |
| Jump | Space | IA_Jump | Held flag plus a consumable press edge |
| Mouse movement | Mouse X / Y | IA_Look | Look delta; future camera consumer |

Opposite movement keys cancel. Diagonals are clamped to length one. Without movement the requested gait is Idle; movement normally requests Walk; movement plus sprint requests Sprint unless crouch is requested. Crouch takes precedence, including at rest. Releasing crouch can restore a still-requested sprint. Hold clears on release; Toggle flips once per press and ignores repeats. Switching policies clears current intent. Intent, pressed flags and toggled stance requests are never saved between sessions.

These are requests, not claims about an actual character. The future character can reject sprint because of stamina or movement mode, retain crouch because a ceiling blocks standing, and accept jump only when legal. Grounded/airborne, swimming, flight, climbing, death and possession are independent confirmed movement modes; do not flatten them into one Idle/Walking/Sprinting/Crouched enum. Use native CharacterMovement prediction and server authority when that stage begins.

## Organization and ownership

`Content/Input/Actions` contains five real editable Unreal assets. `Content/Input/MappingContexts/IMC_OnFoot` contains seven player-mappable movement rows plus two mouse mappings. Move uses cumulative aggregation with negate/swizzle modifiers. Stable mapping names retain the existing menu IDs, including `key.sneak` for the user-facing Crouch action. Display text and physical key names do not become gameplay identity.

`UUnnamedInputSettings` extends Epic's Enhanced Input user settings and stores the two Hold/Toggle policies alongside the native key profile. Defaults come from the authored mapping context. `Scripts/Editor/CreateInputAssets.py` creates missing assets once and preserves an existing authored context. Asset changes belong in Unreal's input asset editors; never alter an authored mapping context to perform a player's rebind.

`UUnnamedInputSubsystem` owns registration, gameplay context activation, action bindings and the intent model for the local player. Registering settings makes them available in the menu without activating on-foot input. Menu entry explicitly removes the on-foot context and clears intent. Only this owned context is removed; unrelated contexts are preserved. Completed/Canceled events release actions, and application deactivation clears held/toggled intent. Unreal's pressed-key flush and ignore-held-keys options prevent a held key from acting as a fresh press during context transitions.

The future gameplay controller should call `BindGameplayInput` with its `UEnhancedInputComponent`, enable the on-foot context when gameplay has focus, and disable it before opening a blocking menu, unpossessing or disabling gameplay. Consume intent once per simulation update. `ConsumeIntent` clears only the jump press edge and look delta; continuous movement and held requests remain available. The subsystem does not move a pawn or rotate a camera.

`UMenuInputSettings` exposes only seven movement bindings, their resets and two policies to CEF. `MenuWeb/Runtime/MenuInputRuntime.js` renders authoritative replies, rejects unsupported captures and waits for pending edits before Quit. This preserves the existing menu layout and keeps gameplay authority outside browser JavaScript.

## Remapping and persistence

Click an enabled key button, then press a physical keyboard key or mouse button. Left/right modifiers remain distinct. Escape cancels capture; Backspace unbinds. Reset restores one binding; Reset Keys restores the seven bindings while preserving Hold/Toggle choices. The confirmed Hard Reset also restores both policies to Hold. A conflicting reset is rejected rather than silently assigning the same key to two movement actions.

Duplicate keys within the simultaneously active movement context are rejected. Escape, F11 and the console key remain reserved. Other context conflicts must be designed when those contexts are implemented; the old global duplicate-key heuristic is removed. F11/Alt+Enter still use the existing functional window shortcut; the Fullscreen row's future remapping remains Planned.

Bindings and policies save through the native SaveGame serializer in the `UnnamedInputSettings` slot under the application's Saved/SaveGames location. Successful changes save immediately; save failures are reported, and final-save failure cancels Quit. Editor Play and `-MenuSettingsSessionOnly` do not write normal preferences. Standalone automated tests use an isolated `-UserDir`; test video files are separate per viewport so restored window preferences do not override the requested test size. No world/save files are deleted by input resets.

## Complete catalog and future contexts

| Current category | Rows | This stage | Future organization |
|---|---:|---|---|
| Movement | 7 | Native settings/actions/intent | IMC_OnFoot |
| Gameplay | 3 | Planned, disabled | Attack/use/pick commands in the applicable gameplay context |
| Inventory | 12 | Planned, disabled | E inventory; F offhand; Q drop; slots 1-9; distinguish gameplay shortcuts from inventory UI |
| Multiplayer | 5 | Planned, disabled | T chat, Tab player list, slash command, O friends, P social; text-entry UI suspends movement |
| Misc | 8 | Rebinding planned, disabled | Screenshot, perspective, cinematic camera, advancements, quick actions, GUI and spectator shader; fixed fullscreen shortcut already works |
| Creative | 2 | Planned, disabled | C/X toolbar actions only when the mode permits them |
| Spectator | 2 | Planned, disabled | Separate spectator context, including its middle-mouse use |
| Debug | 23 | Planned, disabled | Separate development context with explicit F3 modifier/chord semantics |

All 55 non-movement binding rows are labeled Planned. Controller bindings, alternate key slots, mouse sensitivity/inversion, remappable Pause, double-tap sprint, auto-jump, attack/use toggles and gameplay context transitions beyond the menu remain later work. Enabling them requires implementing their behavior and a context-aware conflict policy.

## Verification and rebuild

Run `Scripts/TestInput.ps1` for three native suites: intent rules, native profile validation/serialization, and actual Enhanced PlayerInput key processing. Run `npm test` in Tests for browser request/recovery checks. `node Tests/input-settings.mjs` owns isolated packaged sessions and verifies capture, conflict rejection, modifier sides, unbinding, resets, reload/relaunch persistence, progress preservation and normal Quit at both viewport profiles.

`Scripts/BuildMenu.ps1 -Package -PackageName MenuInputCrouchCDevelopment` builds the editor, preserves/creates the input assets and packages the menu. Close Unreal Editor for a conventional unsuffixed rebuild. `-EditorModuleSuffix <unused number>` allows a separate editor DLL build while the existing editor remains open; the already-open editor still runs its previously loaded module until restarted. A clean isolated copy under `C:\dev\sandbox\Unnamed-InputBuild-20261007` is used for packaging while preserving the open editor and earlier packages.

## Confirm updates in the running application

The Key Bindings screen shows **Input v2** and either **Changes save automatically** or **Session only; changes do not save**. Stop Play and restart Unreal Editor after a native module rebuild; an already-running process continues using its loaded DLL. Earlier packaged executables remain earlier builds. The current packaged executable is `C:\dev\projects\Unnamed\Packages\MenuInputCrouchCDevelopment\Windows\Unnamed.exe`.

Open Options > Controls > Key Binds. Confirm Crouch = C and Sprint = Left Shift. Click Walk Forward's W, press Up, and verify the button changes to Up. Done does not revert the edit. In a save-enabled standalone run, close normally with Quit Game, reopen the same executable, and verify Up remains. Reset Walk Forward to W afterwards. Escape cancels capture; Backspace unbinds. Rows marked Planned are disabled until their systems exist.

The Windows Unreal CEF adapter omits the native scan code, leaving DOM `event.code` empty even for valid keys. Capture now falls back to the virtual-key identity sent by Unreal; CEF modifier location preserves left/right Shift and Control. This was a real defect missed by earlier browser-injected tests.

Video Settings shows Graphics API: DirectX 12. Hover reports the configured default and the native RHI actually running. API switching is disabled until an appropriate startup/restart path is implemented.

Current evidence and package identity are recorded in [the dated work record](<Documented Actions/2026-10-07-Keybindings.md>).

Epic references: [Enhanced Input](https://dev.epicgames.com/documentation/unreal-engine/enhanced-input-in-unreal-engine), [Enhanced Input user settings](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/EnhancedInput/UEnhancedInputUserSettings). Installed UE 5.8 headers and source were also checked for the mapping, key-profile, SaveGame and flush APIs.
