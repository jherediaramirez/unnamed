# Voxel world handoff: phase 6, into S11 (the carvers on, then underground features); the chain is paused - October 11, 2026

Read this first if you are picking up the voxel world's main lane. It replaces [the S9e part 4 handoff](2026-10-11-Voxel-Handoff-Phase6-S9e4.md) as the main lane's entry point. **Jesus has paused the phase 6 chain; player fixes come next.** Start S11 only when he or a manager says so. Facts were true at this handoff's parent commit; check `git status -uno`, `git log -5` and newer records first.

## 1. Where things stand

- **S9e part 4** ([the record](2026-10-11-Voxel-Phase6-S9e4.md)) merged sparse bins in the tree tests' chi-square and committed with the mangroves off ("Commit it as with mangroves off"). The world is unchanged and every hash in `C:\dev\tools\shift-managers\expected-hashes-main.txt` holds.
- **The mangroves are parked off:** `IsMangroveBuilt()` is false. Only the two moss lines fail with them on. Resuming them starts from the record's section 3 (F7's folder `C:\dev\sandbox\phase6s9\s9e4\fit\` and F6b's report) and is Jesus's call. Do not mix it into S11.
- **S10's carvers** are in main behind `voxel.Carvers`, default 0. S11 turns them on, then builds the underground features. S12 (ocean and cold, and the phase record) follows.

## 2. Read these, and only these (about 60k)

1. Rules: `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md).
2. This handoff, whole.
3. [The S10 record](2026-10-10-Voxel-Phase6-S10.md):
   - section 2 (lines 42-52), its reading list for S11's start: follow it;
   - section 6 (lines 103-119): turning the carvers on, what moves;
   - section 8 (lines 141-171): known issues;
   - section 9 (lines 172-191): questions S10-1 to S10-4.
4. [The phase 6 plan](2026-10-09-Voxel-Phase6-Plan.md), "S11. Underground features" (lines 461-480).
5. [Scout K](2026-10-10-Voxel-Phase6-Scout-K-Underground.md), "Part S11" (lines 302-540): give it to subagents by range; the lead does not read it whole.

## 3. What to do, in order (once unpaused)

1. Plan with the manager: files and why, and answers to S10-1 to S10-4.
2. The carvers on (the S10 record's section 6): the re-records and the gate it lists.
3. The underground features (the plan's S11 and scout K's Part S11), probed and fitted like S9's trees, one chat per part if it grows.
4. The wrap-up:
   - the Sonnet review;
   - `Scripts\RunChecks.ps1 -Profile final` with the lane's hash file;
   - the gate and the flight in a quiet window;
   - the record and the handoff into S12.

## 4. Working agreement (unchanged)

- Report to whichever manager last messaged you (at this handoff, "Shift manager 7", session_015T3WdXHDc1483hqjGoVUVG).
- Plan before building; report A before a commit, report B after; never push.
- Measure context with `C:\dev\sandbox\phase5s6\measure.ps1 -Session <id>` after every build, test run and subagent result.
- Subagents are 5.5 models, briefed for one deliverable of about 90k.
- Run gates and flights only in quiet windows.
- The game's behaviour comes only from its data, docs and black-box probes.

## 5. Known issues carried

| Issue | When |
|---|---|
| The mangroves are off: the moss's root walk is unfitted | When Jesus resumes them |
| fancy_oak_bees_0002_leaf_litter's 6-connected parts sit at p 0.015 under the merge: the line to watch | Watch |
| The S10 record's section 8 and the S9e part 4 handoff's section 7 | As listed there |

## 6. Commands

```powershell
& C:\dev\sandbox\phase6s9\s9e2\build.ps1 -NoUnity                                     # build (-NoHotReloadFromIDE, -DisableAdaptiveUnity)
& C:\dev\sandbox\phase6s9\s9e\trees.ps1 -Filter 'Unnamed.Voxel.Trees' -Name <n>       # tree tests (lines.txt in the run)
& C:\dev\sandbox\phase6s9\s9e4\mut.ps1 -Mutations <n>                                  # tree test mutations, restored by SHA-256
& Scripts\RunChecks.ps1 -Profile final -ExpectedHashes <hash file> -Flight smoke
```

## 7. The short prompt for the next session

> Voxel world, main lane: phase 6's S11: the carvers on, then the underground features. S9e part 4 is committed: the chi-square's sparse bins are merged and the mangroves are parked off (the world is unchanged). The phase 6 chain was paused for player fixes; start only when Jesus or the manager says so.
>
> Read `C:\dev\projects\Unnamed\Docs\Records\2026-10-11-Voxel-Handoff-Phase6-S11.md` first. Its section 2 is the whole reading list, with line ranges, so keep to it.
>
> Do section 3 in order. Send the manager that starts you a short plan (files and why) before building, report A before a commit and B after, run gates and flights only in quiet windows, and measure your context after every build, test run and subagent result. End with a handoff into S12 and do not start it.
