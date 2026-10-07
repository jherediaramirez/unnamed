# Foundation verification — 2026-10-07

**Historical r2 measurements below.** The current r3 candidate restores the established **Left Shift sprint / C crouch** defaults. Its focused verification is in [Personal controls correction](../Documented%20Actions/2026-10-07-Personal-Controls.md). The original reports and r2 package remain unchanged; movement physics are unchanged by the correction.

Delivered candidate: `Packages/FoundationDevelopment20261007-r2/Windows/Unnamed.exe`, Unreal 5.8.3 Development Win64, D3D12/SM6. Launch instructions are in [README.md](README.md). The existing main menu launches the Foundation map through Play, double-click and Enter, then returns to a usable menu. This is a playable primitive-lab foundation with verified application-level input; human/OS input acceptance and played-client comparison remain open.

## Evidence boundaries

Reference: vanilla Java **26.3**, internal build `2026-09-15T11:20:48Z`, client SHA-1 `e877b6a07acd633fb3bb475002175cec036e7b87`. [REFERENCE.md](REFERENCE.md) records official URLs and actual inspected symbols. Values below are **source-derived**, not recorded from a played vanilla session. No old Java, Bedrock or mod behavior establishes the defaults.

Unreal measurements come from native automation or the real rendered game world with controller/Enhanced Input events injected by an opt-in application test component. CEF checks operate the actual embedded menu and native settings bridge. These are stronger than an HTML-only prototype test, but do not establish hardware scancode behavior, OS cursor capture, alternate layouts, IME or subjective feel. Rendered screenshots were visually inspected; they are engine captures, not mockups.

Durable evidence lives in `Tests/Evidence/2026-10-07/Foundation/`:

| Evidence | Result | Files |
|---|---|---|
| Editor compile, supported asset generation, Development build/cook/stage/archive | Pass, exit 0 | `BuildCookPackage.log`, `FinalEditorBuild.log` |
| Native automation | 13 suites passed, zero failures/warnings | `NativeAutomation/index.json`, `NativeAutomation.log` |
| Existing menu JavaScript regression tests | 44 passed | `JavaScriptTests.log` |
| Packaged runtime | 34 checks passed at each requested cap, 102 total check executions | `PackagedRuntime/runtime-{30,60,120}.json/.png/.log` |
| Editor executable game runtime | Same 34 checks passed at each requested cap | `EditorRuntime/runtime-{30,60,120}.json/.png/.log` |
| Packaged CEF/native Controls, relaunch and three menu/map trips | 26 checks passed | `PackagedMenu/results.json`, `travel.{1,2,3}.json/.png`, Controls/world-list captures |
| Editor CEF/native menu | 26 checks passed | `EditorMenu/results.json` and captures |
| Isolated real SaveGame write + new-process read | Three write checks and two read checks passed | `Persistence/write.json`, `read.json`, process logs |
| Baseline preservation and candidate hashes | Pass | `Preservation.json`, `PackageHashes.json` |

The last change after packaging extended only native test assertions for stopping, diagonal sneak and sprint-jump/air acceleration; it did not change production code or assets. Those assertions passed against the same production simulation in editor module suffix 2046. Package construction used suffix 2044. No failed intermediate result is counted as a pass; earlier logs remain under `C:\dev\tmp\Unnamed-FPS-Research-20261007`.

## Measured movement, scale and collision

Initial tolerances were set before tuning: speed 1%, jump apex .02 blocks, airtime one 50 ms reference tick, pose/eye dimensions .001 blocks, coordinate round trip 1e-7 cm. Extra stopping tolerance is .001 blocks; explicit boost/acceleration arithmetic tolerance is .0001 cm in the source recurrence. No initial tolerance was relaxed. Ordinary flat ground has friction .6, the fixture is mobile/sufficiently fed, and no fluid/effects/item slowdown apply.

| Scenario | Java 26.3 source reference | Unreal measurement / expectation | Tolerance | Result / evidence |
|---|---|---|---|---|
| Standing collision | .6 × .6 × 1.8 blocks | World-aligned 60 × 60 × 180 cm; half-height 90, actor at feet | .1 cm | Pass, native bounds and rendered height probe |
| Standing eyes | 1.62 blocks above feet | 162 cm | .1 cm | Pass, rendered probe |
| Crouch collision/eyes | .6 × .6 × 1.5; eyes 1.27 | 60 × 60 × 150 cm; eyes converge to 127 cm | .1 cm | Pass, native and rendered probes |
| Block scale | 1 m = 1 block | Engine cube scaled from its 100 cm primitive; conversion invariant 100 cm | Explicit invariant | Pass, native map/geometry and coordinate tests |
| Cardinal walk | 4.3171806167 blocks/s | 4.31718062 blocks/s | 1% | Pass at all requested runtime caps |
| Cardinal sprint | 5.6123348018 | 5.61233480 | 1% | Pass at all requested runtime caps |
| Cardinal sneak | 1.2951541850 | 1.29515418 | 1% | Pass at all requested runtime caps |
| Diagonal walk | 4.4052863436 | 4.40528634 | 1% | Pass; the small cardinal difference is actual 26.3 input scaling |
| Diagonal sneak | 1.8316246138 | 1.8316246138 | 1% | Pass, native simulation |
| Stop after steady walk release | .2558459955 blocks; first zero displacement on tick 8 | .2558459955 blocks; tick 8 | .001 blocks; exact tick | Pass, native measurement; rendered probe also fully stopped after 1 s |
| Jump apex | 1.2522033403 blocks | 1.25220335 blocks | .02 blocks | Pass |
| Jump airtime | Land on tick 12 = .60 s | 12 reference ticks | 1 tick | Pass |
| Held jump | Grounded jump repeats after cooldown | A new upward displacement after landing | Pass/fail | Pass, native simulation |
| Sprint-jump boost | .2 blocks/tick in facing direction, plus ordinary intent acceleration | 20 cm/reference tick added; first vertical displacement 42 cm | .0001 cm | Pass, native measurement |
| Sprint air acceleration | .026 × .98 blocks/tick² for cardinal intent | 2.548 cm/reference tick² | .0001 cm | Pass, native measurement |
| Eligibility loss | Insufficient food stops sprint | Separate eligibility facts cancel resolved sprint | Pass/fail | Pass; no hunger system implemented |
| 50/60 cm steps | At/below .6 blocks step limit | Traversed without jump | Pass/fail | Pass, native collision test |
| 61/100 cm steps | Above .6 step limit | Blocked without jump/auto-jump | Pass/fail | Pass, native collision test |
| 155 cm ceiling | Crouch fits; standing does not | Released sneak remains resolved crouch | Pass/fail | Pass, native and rendered probe |
| 59/61 cm gaps | .6-block AABB blocked/clear respectively | Blocked/clear | Pass/fail | Pass, native geometry test |
| Sneak ledge | Reduce unsupported offsets in .05-block increments | Remains supported through 300 forward ticks | Pass/fail | Pass, native fixture |
| Capsule versus AABB corner | Center (-22,-22) cm: radius30 circle clears; square overlaps 8 cm | Box is blocked; analytic circle distance 31.11 cm | Explicit geometry | Pass; this was an analytic comparison, not a Chaos CharacterMovement runtime comparison |
| Auto-jump primitive | Source eligibility/height rules; default off | 1 m obstacle jumped only when enabled | Pass/fail | Pass for this fixture; broader fidelity remains an approximation |
| Negative coordinates | Floor(-.1) = -1 | Negative XYZ floors and rebased round trips | 1e-7 cm | Pass, native coordinates suite |
| Facing/pitch | East -90, south0, west90, north180; down-positive pitch | Shared forward/reverse conversion matches | Exact cases | Pass, native coordinates suite |
| FOV at 16:9 | Vertical70° | Horizontal102.4478578° | .0001° | Pass, native conversion test |
| Mouse gain at sensitivity .5 | 100 counts =15° | 15° yaw, no frame-time multiplier | .05° | Pass, rendered application-input probe |
| Long frame | Bounded processing needed | A 2 s input frame executes five steps; excess time is recorded/dropped | Five steps | Pass; deliberate project hitch policy |

Standing/crouching dimensions and eyes are distinct from the interpolated camera and the feet-origin actor. Extra pose registrations are documented in BEHAVIOR.md; swimming/crawling, flight and mounted motion are not implemented.

## Frame cadence and performance limits

The pure simulation suite receives exact `1/30`, `1/60` and `1/120` second intervals and reaches exactly 200 reference steps in ten seconds with the same outcomes. This establishes fixed-step cadence independence at those input intervals.

The rendered Windows machine had an i9-14900KF, RTX 4070 Ti SUPER and 64 GiB RAM. Tests used 1280×720 offscreen D3D12, VSync off, low GI/reflection/shadow/AA and 50% screen percentage. These are validation settings, not a new default graphics preset. Actual rendered cadence did **not** sustain the requested caps:

| Requested cap | Whole-run observed mean FPS | Walk phase FPS | Sprint phase FPS | Jump phase FPS | Checks |
|---|---:|---:|---:|---:|---|
| 30 | 24.37 | 24.06 | 24.21 | 25.46 | 34/34 pass |
| 60 | 46.36 | 47.38 | 46.89 | 48.15 | 34/34 pass |
| 120 | 65.15 | 64.20 | 64.25 | 65.41 | 34/34 pass |

Whole-run means include UI transitions. These results prove behavior across the measured cadences; they are not proof of sustained rendered 30/60/120 FPS or a performance target. An earlier full-render-cost editor run passed its then-current runtime checks at about 43 FPS mean, but is not substituted for this final candidate's measurements. No render optimization or global 20 FPS cap was introduced. Native mouse latency/feel remains unmeasured.

## Input, menus, targeting and diagnostics

| Scenario | Reference/project expectation | Observed result | Evidence |
|---|---|---|---|
| W+S and W+D | Opposites cancel; simultaneous diagonal resolves through the source input rule | Zero opposing displacement; measured diagonal speed above | Rendered probe/native suite |
| Remap Forward to K | Old W stops; K moves and drives double-tap sprint | Pass | Rendered probe |
| Sprint/sneak Hold and Toggle | Press/release/repeat/toggle transitions are separate from resolved pose | Pass at intent-model level; crouch toggle survives relaunch | Native input suite, CEF and persistence |
| Forward/jump/sprint/sneak capture | Native keys and labels update; restart preserves them | K, Enter, Right Ctrl, Right Shift persisted | CEF/native process restart and SaveGame test |
| Mouse/extra mouse/wheel | Native mouse action remapping; wheel pulses survive a simulation step | Extra button captured, wheel captured, wheel-bound jump moves pawn | CEF + rendered probe |
| Conflict, cancel, unbind, reset-one/all | Same context/chord duplicates rejected; capture Escape cancels, Backspace unbinds | All passed; reset defaults Ctrl sprint/Shift sneak | CEF/native suite |
| Capturing F3 | Selecting binding cannot become an overlay release action | No overlay toggle after captured press | Rendered probe |
| Pause while moving; release while paused | Remove contexts, clear held intent, no blind resume | Pass | Rendered probe |
| Text entry | Gameplay does not receive typed movement keys | No movement in native text fixture | Rendered probe; OS IME not exercised |
| Focus loss | Cancel and pause; require a fresh press on return | Activation callback passes | Rendered probe; not an OS focus-switch test |
| Menu trips | No duplicate browser/context registration | Four pause cycles plus three full map/menu trips stay usable; one CEF page | Rendered probe + CEF |
| Inventory action | Open/close input fixture with current logical binding; cancel held actions | Same E binding closes; inventory cancels use/break | Rendered/native tests |
| Hotbar/wheel/drop/offhand | Individual slots; wrap9→1; drop1 or fixed Ctrl stack; swap once | Token fixture passes | Native action suite |
| Block/entity reach | Separate4.5/3 block limits; strict less-than | Block4.49 accepted,4.5 rejected; entity4.49 rejected,2.99 accepted | Native action suite |
| Occlusion | Solid geometry blocks entity selection | Nearer block chosen over hidden entity | Native action suite |
| Attack/use priority | Entity press versus held block break; release cancels; active use consumes attack/pick; active break blocks use | Pass | Native action suite; rendered eye-target attack and pause cancellation |
| Hands/sneak/target failure | Main before offhand; sneak bypasses ordinary block interaction with an item; block failure ends fallback | Pass, including entity-sneak distinction | Native action suite |
| F3 alone/chords | Standalone release toggles; F3+F6 opens options without perspective | Pass even with perspective rebound to F6 | Rendered probe |
| HUD hiding | F1 affects presentation, not simulation | Pass | Rendered probe |
| Screenshots and debug text | Actual engine rendering; real runtime state | PNG captured and visually inspected; feet/block/facing/state/contexts agree | Packaged runtime screenshots |
| Debug persistence | Scale is independent of menu GUI scale and saved natively | Scale1.4 and toggle/custom bindings survive a new process | Separate write/read probe |

F2's production action calls the same native screenshot API exercised by the render probe, but a physical F2 press was not tested. F11 is wired to the existing native video adapter; fullscreen is deliberately unavailable in offscreen tests. Copy Snapshot uses the native clipboard; an external application paste was not tested. Rear/front diagnostic camera controls and inversion/sensitivity preferences are implemented, but subjective framing, all extreme option combinations and physical-device acceptance remain manual checks.

## Known differences, deferred systems and remaining acceptance

1. **Played vanilla comparison remains unexecuted.** The exact client launched, but a Windows Security firewall permission prompt for UnrealGame covered the interactive desktop during native testing. The Computer Use skill forbids acting on security/privacy permission prompts, so no permission was accepted or bypassed. The attempted isolated client also used a stale asset-index argument (27); the pinned version metadata specifies **34**. Do not reuse that attempted launch as a clean reference run. Relaunch through a clean vanilla 26.3 launcher profile for the procedure below.
2. **OS hardware input remains unexecuted.** US-layout application events/CEF capture pass. Non-US physical keyboard mapping, AltGr/dead keys, IME, physical wheel/buttons, alt-tab, cursor recapture and native latency require the real desktop. The Windows adapter translates keys through the current layout to physical scan positions; displayed names currently use canonical Unreal/US key labels. Unreal's Windows input backend is used; the project does not replace it with Minecraft's SDL3 windowing stack.
3. **Collision is scoped to the lab's cuboid provider.** World-axis box shape, ordinary rectangular steps/gaps/ledges and ceiling tests pass. Arbitrary Unreal meshes, moving platforms, slopes, fluids, non-cuboid block shapes and crawling escape pose are deferred. The capsule comparison was analytic. Auto-jump is a tested primitive approximation, not complete vanilla geometry equivalence.
4. **Camera limits.** Correct reference eye heights, tick smoothing, relative-look handling and FOV conversion are implemented. Vanilla walk bobbing, damage tilt, cinematic camera, effects-dependent FOV and full third-person presentation are deferred. The menu's related existing preferences remain desired settings without runtime consumers. Rear/front views visualize a box and use approximate camera obstruction.
5. **Intentional task policies.** Menu/focus/capture loss clears held/toggled requests and suppresses stale keys, rather than restoring vanilla's optional toggle state. A long-frame backlog is bounded/discarded. Pause freezes the local pawn while UI/diagnostics keep running. Falling below the course resets spawn. These are explicit foundation/test-level policies.
6. **Fixtures are contracts only.** Token counts and typed results do not implement inventory items, combat damage, mining, placement, survival, crafting, networking, terrain generation or world persistence. Chat records local text only. The 27 deferred binding rows and additional fixed inventory/accessibility/platform/debug shortcuts retain their researched contracts in CONTROLS.md. Unsafe crash/debug operations are not reproduced.
7. **Rendering acceptance.** Actual sustained 30/60/120 FPS, visible-window mouse feel, editor PIE and longer human play sessions are not marked passed. Unavailable engine counters are labelled unavailable. The existing graphics/audio placeholders keep their earlier scope.

## Exact remaining reproduction steps

After the human resolves the existing Windows permission prompt, launch the delivered executable using README.md. Select Player Foundation, restore factory Controls only if desired, then test physical W/A/S/D/Space/Ctrl/Shift; rebind Forward to another key and double-tap it; open Controls while moving; release during pause; alt-tab out and back; verify no movement/attack resumes until a fresh press. Test a non-US layout with physical-position bindings and layout-dependent chat editing. Try F3, F3+F6, F1, physical F2/F11, copy/paste the diagnostic snapshot, and repeat entry/return three times. These actions should not require granting network access for the local lab.

For the reference, create an isolated **vanilla Java26.3** launcher profile with no loader/mods, Java25 and metadata asset index34. Verify the local client SHA-1 against REFERENCE.md. In a new single-player flat test world with cheats enabled, retain default movement options, normal20Hz tick rate, no effects and sufficient food. Build measured ordinary-block steps, ceiling/gap/ledge cases; observe standing/crouch collision/eye and reach with F3+F6's debug options. Use tick-stepping/recorded F3 positions to measure at least200 steady movement ticks, release stopping distance, jump peak/landing tick, and sprint-jump displacement. Use the same direction, friction and input combinations as the table, and record world feet positions rather than camera position. Repeat source comparisons before marking observed-vanilla parity accepted. Actual test-world commands and screen options should be selected from that running build; no unexecuted command sequence is asserted to have worked.

For automated Unreal reproduction, use the exact commands in README.md with fresh report directories. The native persistence probe can be repeated with two invocations of the candidate's inner executable, first `-FoundationPersistence=write` then `-FoundationPersistence=read`, both `/Game/Maps/L_Foundation -RenderOffscreen -unattended -UserDir=C:/dev/tmp/Foundation-Persistence-New/profile/`, distinct `-FoundationProbe=.../write.json` / `read.json` and the same isolated `-MenuPreferencesFile` / `-MenuVideoSettingsFile` paths. These are opt-in test flags, never normal startup behavior.

## Preservation and identity

Snapshot: `C:\dev\backups\Unnamed\fps-foundation-20261007-140749`. All 9,623 baseline files still exist; staged object IDs/modes are unchanged; all 33 historical package binary hashes match; the reference HTML and original menu map hashes match. Git's index byte hash can change when its stat cache refreshes; unchanged staged entries are the meaningful preservation check. Separate concurrent Super Flat work remains present. No commit, reset, clean, deletion of old candidates or broad staging was performed.

Final inner game executable SHA-256: `88E7A5DDDE666F8D6A8C7B96EC7F0B598C384CED04DA6B8FA38937E4896759ED`. The bootstrap executable hash is shared by multiple Unreal packages and is not enough to identify gameplay content; `Tests/Evidence/2026-10-07/Foundation/PackageHashes.json` records both executable and cooked-container hashes.
