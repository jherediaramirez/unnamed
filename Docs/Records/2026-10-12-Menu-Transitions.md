# No black or stale screens around the menu (2026-10-12)

After the ESC fix ([record](2026-10-12-Pause-Menu-Flash.md)) Jesus asked: "could there be similar issues around our menu?" A probe (`C:\dev\sandbox\menu-transitions\facts.md`) found Return to Main Menu and game start black for about 0.6 s, Create New World frozen on its screen for about 1 s, the Playground's first world frame dark, and the first creative-inventory open drawing one frame without its background. Jesus's answers: Q1 (a) one menu browser kept for the whole game, switched to the title; fix all three extras; Q2 commit the probe with the fix.

## The causes, measured (all switches 0 = the behaviour until today)

Baseline at afef593 plus the probe (`Tests/Evidence/2026-10-12/Menu-transitions/base-*`), 2560x1440, shot runs for frames and 3-loop log-only runs (60 fps cap, machine idle) for ms:

| Transition | Frames | Real ms |
|---|---|---|
| R, Return to Main Menu from the Playground | 5 black (f1-5): a fresh opaque browser until the 6 MB page paints | 863 (818-879) to the title |
| F, the same from a voxel world | 4 black | 951 (947-967) |
| S, game start | 5 black (12 in the fixed-step run) | title 3135 ms after the browser was made |
| N, Create New World | the Create screen stays until the loading screen; the page reports `create` on the last menu frame | gameplay 7017 |
| P, Play Selected World (Playground) | 1 unexposed frame, mean luma 15 | gameplay 684 |
| I, first inventory open | f0 without the container background | open frame 57-60 ms (`SHudCreativeScreen::Construct` loads its textures, about 40 ms, plus the item list, about 9 ms) |
| Guards (ESC, Options screens, inventory close) | clean | 33-46 |

## The fixes and their switches

Rows in [Architecture](../Architecture.md) (The title screen, world entry and the first inventory). Set at launch with `-ini:Engine:[ConsoleVariables]:<name>=<value>`; each run logs `Switch <name> = <value>`.

| Switch | 0 | 1 (default) | 2 |
|---|---|---|---|
| `menu.Title.Keep` | a fresh opaque browser per menu map | one title browser for the game (`UMenuBrowserSubsystem`); after each world loads, the page goes back to the title out of view (CEF kept painting with `IWebBrowserWindow::SetViewportSize`, since CEF hides a browser Slate no longer ticks and its draw ignores RenderOpacity) and reports `TitleReady`, so Return to Main Menu shows the title in the menu map's first frame; a changed window size, video setting or preference repaints it; game start (or no report) shows a Mojang Studios overlay until the title has painted, then a 2 s fade with input after it | the same, the overlay cuts with no fade |
| `menu.World.WaitForPaint` | the page asks at once | the page waits two frames (at most 250 ms) after the preparing screen, native holds the travel two frames | |
| `foundation.Load.Cover` | loading screen only while not ready | loading screen from the first frame until 3 frames after ready | |
| `foundation.Panels.Preload` | the screen is built in the first open's frame | built at world start, reused by the first open | |

## After the fixes

Shot runs (`final2-all1-shots-20261012-191345` all 1, `final-all0-shots-20261012-182806` all 0): all 1 passes every check (R and F 0 black, title from f1, the same browser on every trip; S 0 black, 15-17 overlay frames; N the preparing screen on the last menu frame; P 0 dark; I f0 complete); all 0 reproduces the baseline. Each switch alone at 0 with the others at 1 (`alone-*`) brings back only its own transition. Keep 2 (`keep2-*`): 6 overlay frames, then a cut.

Real ms, 3 loops:

| Transition | All 0 | All 1 | All 1, confirming run on the final build |
|---|---|---|---|
| R | 863 (855-873) | 199 (197-210) | 211 (208-226) |
| F | 965 | 301 | 342 |
| S (title usable) | 2329 | 3018 (includes the 2 s fade) | 3935 |
| P (gameplay) | 770 | 996 (WaitForPaint and 3 cover frames) | 1003 |
| N (gameplay) | 7730 | 6961 | 7087 |
| I open frame | 58-101 | 16-29 (the 60 fps cap's own pacing) | 16-29 |
| Guards, median | 33-46 | 33-46 | 33-47 |

The title shows 6-9 ms after the first menu frame. In-world frame time (uncapped Playground, 873 frames) 8.19 ms at Keep 0 against 8.22 at 1. Voxel loading time per run: Keep 0 6113 and 7014 ms, Keep 1 6170 and 7299 ms (noise). Memory (working set, CEF = EpicWebHelper.exe): CEF 468-479 MB in a world at Keep 1 against 339-341 at 0 (one more process), flat over 3 trips (341-366 on the title); the editor's own working set grows after voxel worlds at both values.

GUI Scale 3 set in a world's Video Settings (on screen, settings persisted, `guiscale-onscreen-*`): the hidden title repainted 393 ms after the save, and Return to Main Menu shows the title at the new scale in its first frame.

Fixed-step runs (`-UseFixedTimeStep`) delay the menu-map page by 3-9 frames at both values, so N's picture is not judged there (strip-9).

Strips: `Tests/Evidence/2026-10-12/Menu-transitions/Strips-final-20261012-191017/` (made before the logo fix, so their game-start rows show the logo as a white box; the fixed overlay is in `final2-all1-shots-20261012-191345`).

Mutations (`C:\dev\sandbox\menu-transitions\mutant-*.log`), all killed: Keep default 0 (native); kept browser destroyed on travel (probe browser identity, R 626 ms); no title reset (R never reaches the title); reveal before the ack (native); no launch overlay (S 5 black); bridges not re-pointed (foundation-menu.mjs); WaitForPaint skipped (node); cover frames 0 (native); preload skipped (I f0 partial).

Checks after the last code change: `Scripts\RunChecks.ps1 -Profile final -Flight smoke` all green (`Tests/Evidence/2026-10-12/Checks-final-20261012-193500/summary.txt`: native 287 of 287, voxel, hashes 9 of 9, runtime 128 + 83 + 27, create-world, flight smoke, npm 85 of 85, source-check, verifiers); `foundation-menu.mjs` 36 of 36 (`Foundation/Menu-1791849308653`); the pause suite at `foundation.Pause.Menu` 1 (`Playground-pause/menu-transitions-final-20261012-195631`: A and C 0 black).

## The loading overlay's sources

The overlay is our own code. A first draft took its numbers from a `javap -c` dump of 26.3's LoadingOverlay (`C:\dev\sandbox\menu-transitions\vanilla\LoadingOverlay.javap.txt`, left in place); that is not an allowed source, so every value was re-sourced. Clean sources: the Monochrome Logo option's black background (minecraft.wiki Options; Java Edition 21w13a) and the texture's layout (`mojangstudios.png`: two stacked 512x256 halves drawn side by side, 4:1). The rest is unverified until a capture of the vanilla client measures it: the red #EF323D (only from mod pages), the logo's size and position, the bar's geometry and smoothing, the 1 s + 1 s fade and input after it.

## Open items

- When a vanilla client capture is allowed: the overlay's red, logo size and position, bar and fade timings (above).
- When the pause menu's memory is looked at: one browser for title and pause (today two CEF pages live through a world, about +130 MB).
- When guard timing matters: in runs with any of the new switches on, the in-world menu screens occasionally settle in 61-63 ms (3 of 18 samples) instead of 33-46; medians are unchanged and the all-0 runs show none; cause not found.
- The Playground's dark first frame: still unexplained (Lumen diffuse GI off did not change it, `exp-cover0-nolumengi-*`); the cover hides it.
- Quit's frozen title (unmeasured offscreen) and the page-internal one-frame partials (FOV slider, scrollbar, world icon): left as they are.
- `MenuTitleRuntime` polls for its binding for 10 s on pages that never get it (harmless).
- Each commit's tree was rebuilt from a verified patch chain; only the final tree was built.

## Commits

`7ecddaa` the probe and checker; `eefbf8f` `menu.Title.Keep`; `c873ef1` `menu.World.WaitForPaint`; `038e4b8` `foundation.Load.Cover`; `b0adf1d` `foundation.Panels.Preload`; this record. Worker scripts and patches: `C:\dev\sandbox\menu-transitions\`.

## Token use

Lead (this chat) about 690k peak context (MeasureAgentUsage). Subagents (Sonnet): checker 83.6k, reviewers 110.7k and 86.7k, overlay re-sourcing 69.1k, strips 47.0k.

## Follow-up prompt

> Unnamed (C:\dev\projects\Unnamed, feature/voxel-world): read Docs/Records/2026-10-12-Menu-Transitions.md. Pick one open item (one CEF browser for the title and the pause menu; the guard 61-63 ms outliers; the Playground's dark first frame; the overlay's values once Jesus allows a vanilla capture) and keep today's behaviour one switch away. Transitions: `& Scripts\TestMenuTransitions.ps1 -Label <name>` (shots) or `-NoShots -Loops 3` (timing).
