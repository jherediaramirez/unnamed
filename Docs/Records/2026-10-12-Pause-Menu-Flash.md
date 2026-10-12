# The ESC flash: a kept-alive pause menu and the world behind it (2026-10-12)

Jesus (2026-10-12): "any time I hit ESC it does this weird flicker black and then it opens the menu ... It's been there since we first started making the game." Scout Y found the cause (`C:\dev\sandbox\scout-pause-flicker\facts.md`); this build measured it, fixed it behind a switch and put the world behind the menu, as Jesus picked (Q1 = a: the world, darkened and blurred by his Menu Background Blur setting, like Minecraft).

## The cause, measured (switch 0 = the behaviour until today)

Every ESC made a brand-new opaque `SWebBrowser` for the 6.35 MB menu page and showed it in the same frame. CEF draws opaque black until the page has painted, and the page booted on the title screen until a native round trip switched it to "Game Paused".

The pause suite's frames at 2560x1440 (one screenshot per frame, so about 6 fps; counts, not ms):

| Run | A (first ESC) | C (second ESC) | LogWebBrowser errors |
|---|---|---|---|
| 0 | black frames 0-4 (frame 0 still has the HUD), the title screen at 5, Game Paused from 6 | the same | 0 |
| 0, `-nocefaccelpaint` | black 0-5, then the panorama without UI and the title drawn in pieces, Game Paused from 12 | the same | 0 |

Log-only timing at a 60 fps cap, 5 presses, machine idle: the page script ran a median 221 ms after ESC (219-257) and the document finished loading at 422 ms (407-458). Game Paused comes after that, one more native round trip later. Resume (B) was always clean: the game returned in frame 0. CEF's shared-texture errors (scout Y's cause 3, seen in old test runs) did not appear in any run.

## The fix and its switches

Read when a world starts (`AFoundationController::BeginPlay`); change one in the console, then reload the world. Listed in `Docs/Architecture.md` (The pause menu in a world), beside `UMenuBrowserHost`.

| Switch | 0 | 1 (default) | 2 |
|---|---|---|---|
| `foundation.Pause.Menu` | today: a fresh opaque browser per ESC, destroyed on resume | one browser per world: made at world start on `index.html#pause`, transparent and invisible until its pause screen has painted (`MenuReady`), then collapsed; ESC shows it (`FoundationMenu.reveal()` re-reads the settings stores, then `MenuShown`); resume collapses it | diagnostic: a fresh browser per ESC, transparent until painted (no black, today's delay) |
| `foundation.Pause.Background` (modes 1 and 2) | the title screen's blurred panorama | the world: a transparent page with vanilla's `inworld_menu_background` (24w11a) instead of the panorama, and a Slate `SBackgroundBlur` under the browser at Z 99, strength = Menu Background Blur x viewport height / 1080 | |

Native: `UMenuBrowserHost` (`SetPauseMode`, `SetPauseBackground`, `Prewarm`, `HasBrowser`; `IsOpen()` now means shown: every caller wanted that), `UFoundationMenuBridge::MenuReady` (returns `{"shown": bool}`, so a page that finishes painting after ESC shows itself) and `MenuShown`. Page: `MenuWeb/Runtime/FoundationMenuRuntime.js` (`#pause`, `pause-hidden`, `&bg=world`), a `reload()` per settings store and `MenuStores.reload()`. The menu map never sets a mode, so it keeps mode 0 and its page has no `#pause`.

-ExecCmds cannot set these switches for the first world: Unreal runs it after that world's BeginPlay (UnrealEngine.cpp:2552 queues it in `UEngine::Init`; GameEngine.cpp:1338 loads the map; LaunchEngineLoop.cpp:6109 runs it at the end of frame 1). Runs pass them as `-ExtraArgs '-ini:Engine:[ConsoleVariables]:foundation.Pause.Menu=0'` (new in `Scripts/TestFoundationRuntime.ps1`), and every run logs "Pause menu mode m, background b".

## After the fix

Frames at 2560x1440 from the pause suite (`Tests/Evidence/2026-10-12/Playground-pause/`), classed by `C:\dev\sandbox\pause-menu-build\pause_frames.py` (scout Y's script, first run here; changes listed in its docstring: a "menu" class on the button column, the game class widened to 15 for the HUD hiding with the menu, a separate output CSV):

| Run (folder) | A: black / title / first pause frame | C: black / title / first pause frame |
|---|---|---|
| 0 (`M0-today-20261012-124347`) | 5 / 1 / 6 | 5 / 1 / 6 |
| 1, panorama, before the background switch (`M1-fix-20261012-125723`) | 0 / 0 / 1 | 0 / 0 / 0 |
| 1, world (`M1-bg1-20261012-133334`) | 0 / 0 / 1 | 0 / 0 / 0 |
| 1, panorama (`M1-bg0-20261012-133523`) | 0 / 0 / 1 | 0 / 0 / 0 |
| 2, panorama (`M2-diag-20261012-130205`) | 0 / 0 / 5 (game frames until then) | 0 / 0 / 4 |
| 2, world (`M2-bg1-20261012-133739`) | 0 / 0 / 7 | 0 / 0 / 5 |

At 1, A's frame 0 is the game (the collapsed browser's last texture is the invisible pre-warm page) and C's frame 0 is already the pause screen (its last texture is the pause screen it was hidden on). LogWebBrowser errors: 0 in every run. Strips: `Playground-pause/Strips-20261012-134141/` (1: A and C, today against the fix; 2: the other values; 3: the menu over the world at blur 5 and 0, and the panorama).

Log-only timing, 5 presses, 60 fps cap, ms from ESC (machine state in each folder's `machine-state.txt`):

| Mode | Median (range) | Machine |
|---|---|---|
| 0 | page script 221 (219-257); document loaded 422 (407-458); Game Paused one round trip later | idle (`M0-timing-20261012-124803`) |
| 1, panorama | page shown 4.5 (3.5-30.8) | idle (`M1-timing-20261012-130612`) |
| 1, world | page shown 18.3 (4.3-20.2) | idle (`M1-bg1-timing-20261012-133924`) |
| 1, world, after the review fix | page shown 18.6 | `M1-reviewfix-20261012-134650` |
| 2, panorama | ready 481, shown 526 (507-627) | another lane's UnrealEditor-Cmd running (`M2-timing-20261012-130648`) |
| 2, world, after the review fix | ready 427, shown 471 | `M2-reviewfix-20261012-134722` |

Mutations (`C:\dev\sandbox\pause-menu-build\mutate.ps1`, each restored and the file touched; logs `mutant-*.log` there):

| Mutation | Killed by |
|---|---|
| `foundation.Pause.Menu` default 0 | `Unnamed.Menu.PauseMenu.Modes` (defaults to 1) |
| Hide destroying at 1 | probe: pre-warmed, shown within 100 ms (658.9 ms), keeps the browser, reuses it |
| `IsOpen()` returning `HasBrowser()` | probe: every timing press and the pre-warm case (gameplay never resumes) |
| no `#pause` at 1 | probe: never ready, so the pause screen stays over gameplay; four checks fail |
| `reveal()` skipping the store reload | node test, and the probe's stale-settings case (key.jump still SpaceBar). A first probe run of this mutant used the previous mutant's DLL (page mutants do not rebuild) and is not counted; the GUI scale alone could not kill it (the video store's own poll catches it up), so the case also changes key.jump |
| transparency off at 2 | frames: black 0-5 in A and C (`mut-opaque-mode2-*`) |

Pre-commit review (one Sonnet subagent, the diff without the generated page): today's path at 0 equals HEAD's; every `IsOpen()` caller wants "shown"; no saved-data change. Applied: a page that finishes painting while the menu is shown also gets `reveal()` from native (a lost `MenuReady` reply could have left it hidden under UI-only input). Recorded, not applied: see the open items.

Checks after the last code change (the review fix): `Scripts\RunChecks.ps1 -Profile final -Flight smoke`, all green (`Tests/Evidence/2026-10-12/Checks-final-20261012-134850/summary.txt`): native 283 of 283, voxel 184, radius-8 hashes 9 of 9, the Playground probe (all 128 checks, interaction 83, tour 27), create-world 38, the flight smoke, npm 76 of 76, source-check, the five reference verifiers. Then `foundation-menu.mjs --editor`: 36 checks, exit 0 (`Tests/Evidence/2026-10-12/Foundation/Menu-1791828558569`), including three menu-world-menu trips with one browser after each. The Playground probe at `foundation.Pause.Menu 0` (`Playground-all/Runtime-20261012-132547`) and at 1 (`Runtime-20261012-132349`) both passed before the background switch.

## Open items

- When someone looks at memory: the kept CEF page stays allocated for the whole world session (not measured: process working set with the menu shown and collapsed). Its settings pollers keep calling native `GetState` while collapsed (throttled by Chromium).
- When the blur is tuned against vanilla: the strength mapping (setting x height / 1080) is a choice; vanilla's 50% has not been measured side by side.
- When the pause should freeze the world: the world keeps animating behind the menu (only the pawn's simulation stops); vanilla freezes singleplayer.
- High contrast: the menu page never applies its high-contrast class (`applyAppearance` keeps the reference presentation baseline), so the in-world high-contrast background rule is in place but cannot show today.
- The first ESC after a world starts shows one game frame before the menu (the collapsed browser's last texture is the invisible pre-warm page); later presses show the menu in the press frame.
- From the review, when the probe's numbers matter for a rapid ESC-resume-ESC: a `MenuShown` from a reveal hidden before its next frame can arrive after the following Show and count as its report (diagnostics only; a Show token would fix it). After a window resize while collapsed, the first frame after ESC can show the old size for a frame or two.

## Commits and how they were checked

- `5bbe6b8` the pause probe and the baseline; `8ab205f` `foundation.Pause.Menu` (with the review fix); `811607a` `foundation.Pause.Background`; this record with the switch rows in `Docs/Architecture.md`.
- Each commit's tree builds on its own: the first is exactly what build 3 compiled (no source changed between that build and the commit's patch); the second was built alone (`-DisableAdaptiveUnity`, the third's changes taken off and put back byte for byte, `C:\dev\sandbox\pause-menu-build\build-c2-check.log`); the third is the final state, rebuilt after that (`build-final.log`), the same sources RunChecks tested.
- The worker's runner, scripts and patches: `C:\dev\sandbox\pause-menu-build\` (`run-pause.ps1` for one pause run, `strip.py` for frame strips, `mutate.ps1`, `cef-e2e.ps1` for an end-to-end driver under the CEF lock).

## Token use

Lead (this chat) 464k peak context at the record (MeasureAgentUsage); one subagent, the Sonnet pre-commit review, 105k peak. Scout Y's reading was done before this chat.

## Follow-up prompt

> Unnamed (C:\dev\projects\Unnamed, feature/voxel-world): read Docs/Records/2026-10-12-Pause-Menu-Flash.md. Pick one open item there (the world freezing behind the ESC menu as vanilla singleplayer does; the blur strength against vanilla's 50%; the kept CEF page's memory; the first ESC's one game frame) and keep today's behaviour one switch away. The pause frames: `& C:\dev\sandbox\pause-menu-build\run-pause.ps1 -Label <name> -Mode 1 -Background 1` (frames, sheets, the menu-over-world views); timings with `-NoShots`.