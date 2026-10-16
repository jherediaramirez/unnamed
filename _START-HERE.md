# Minecraft 26.3 — the single source of truth

**This is the only folder for the game.** It holds the repository (`.git`), the latest code, the latest build, the known bugs and the latest report. Since 2026-10-16 every old `Unnamed-*` folder is retired (see the bottom of this file).

## What this folder is

| | |
|---|---|
| Git | Main repository folder (`.git` lives here). Remote `origin` = github.com/jherediaramirez/unnamed |
| Branch | `feature/survival` |
| Latest commit | `0c8d9a74` (eb52cea7 + the three QC fixes below + a RunChecks fix). **Not pushed yet**: `origin/feature/survival` is still `eb52cea7` |
| Build | Editor built here at `0c8d9a74` on 2026-10-16 |
| Checks | Final profile GREEN on the committed code. Results are in `_STATUS\LATEST-REPORT.md` |
| Engine | UE 5.8 (`C:\Program Files\Epic Games\UE_5.8`) |

Open it with: double-click `Unnamed.uproject`, or play directly:

```
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'C:\dev\projects\Minecraft 26.3\Unnamed.uproject' -game
```

The engine project is still called `Unnamed` inside, including `Unnamed.uproject`, the `Unnamed` module and the GitHub repo name. Only the folder was renamed; renaming the project itself would be a large change of its own.

## Status files (`_STATUS\`)

| File | What it is |
|---|---|
| `LATEST-REPORT.md` | The latest report: 156 tracked bugs, 28 fixed, 127 remaining, and what still needs a human replay |
| `KNOWN-ISSUES.md` | The full known-bugs list (canonical; update it here) |
| `FEATURES-BUILT-AND-MISSING.md` | What is built, what is partly built, and what was never built (don't file the last group as bugs) |
| `pending-work\` | Work that is deliberately **not** in the build (see below) |
| `reference\` | Snapshots of the old program's stop handoff, scope freeze, landing ledger, play checklist and decision log |

The old program folder `C:\dev\tools\shift-managers\survival\` is frozen history; its docs point here.

## Fixed 2026-10-16 (needs your play check)

| Bug | Commit | Try it |
|---|---|---|
| R40-4: turning the bow switch off mid-draw kept the slowdown and zoom | `1d81fbf6` | Draw a bow, F12 → `foundation.Items.Bow 0`, keep holding |
| R17-1: turning fire off didn't put out a player already burning | `e8275a71` | Touch fire/lava, F12 → `foundation.Fire 0` |
| R47-4: turning farm extras off left cold/warm animals in their variant skins | `1215000b` | Near warm/cold cows/pigs/chickens, F12 → `foundation.Mobs.Farm 0`, then `1` |

## Not in the build (on purpose)

| Patch | What | Why |
|---|---|---|
| `pending-work\ATMOSPHERE-T7a-parked-WIP.patch` | Day/night look: twilight haze, stars (branch `feature/survival-atmosphere`) | A feature, deferred until after QC |
| `pending-work\Unnamed-body-DefaultEditor-ini.patch` | Editor settings from the old player-body lane | Noise, probably never needed |

## Retired folders

Everything was moved, not deleted, to `C:\dev\sandbox\retired-unnamed-worktrees-2026-10-16\`, including ignored test evidence, `Saved` data and old builds. Every branch is kept.

| Folder | What it was | Its work is now |
|---|---|---|
| Unnamed | The old main folder (`feature/voxel-world`); its `.git` moved here | Archive `Unnamed-main-folder` (build output, Saved data) |
| Unnamed-survival | `feature/survival` integration checkout | This folder |
| Unnamed-feel | LOW01 fixes, last fully gated build before today | In this build |
| Unnamed-codex-low-02 | LOW02 (R40-4) | In this build (`1d81fbf6`) |
| Unnamed-mobs | Q06 and LOW03 (R17-1, R47-4) | In this build (`e8275a71`, `1215000b`) |
| Unnamed-codex, -play, -use, -inv, -blocks, -ent | Old QC review, play copy, survival lanes T1-T5 | Already merged |
| Unnamed-atmos | Parked T7a atmosphere | Branch `feature/survival-atmosphere` + pending patch |
| Unnamed-body | Player body lane | Archive copy keeps the untracked `Art\Characters\BetterSteve\SK_BetterSteve.fbm` |
| Unnamed-scout, -qol, -water, -lod3, -s11, -review, 3 agent worktrees | Pre-survival voxel-world lanes | Already merged |

`C:\dev\sandbox` is "disposable" by machine convention. Delete the archive yourself when you're sure nothing in it is needed.

## Rules going forward

1. Work happens here. No new `Unnamed-*` lanes or worktrees without updating this file.
2. No new features until the open bugs in `_STATUS\LATEST-REPORT.md` are decided.
3. Only `feature/survival` is pushed. The final merge into `feature/voxel-world` happens only on Jesus's word.
4. When a bug is fixed, update `_STATUS\KNOWN-ISSUES.md` and `_STATUS\LATEST-REPORT.md` in the same commit.
