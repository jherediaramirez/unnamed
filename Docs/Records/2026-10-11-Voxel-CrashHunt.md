# Voxel world: the intermittent allocator crash, a test session (lane Unnamed-s5) - October 11, 2026

Branch `feature/crash-hunt`, cut from main `b52066e`. This session hunted Efficiency audit A2's open half: the access violation at `0xffffffffffffffff` inside the allocator, in ParallelFor workers, mostly in `Unnamed.Voxel.Mesher.Digest`. It found no culprit. On this session's code the crash never reproduced in 65 runs. The two newest crashes point to a pattern (section 4) whose test is the next step. Committed here: RunChecks now keeps crash dumps, and its build step passes `-NoHotReloadFromIDE`.

## 1. Corrections to the S9d notes

- **The default allocator is Mimalloc, not binned.** Every log reads `LogInit: Allocator: Mimalloc`, the UE 5.8 editor default (engine `WindowsPlatformMemory.cpp:152`). A freed mimalloc block keeps its free-list `next` pointer in its first 8 bytes. So the fault means 8 bytes of 0xFF landed at the start of a freed block: a stale write, or a double free whose next owner wrote -1 there. `C:\dev\sandbox\phase6s9\s9d\crash-hunt.md` still says "binned"; this record supersedes it.
- **The rate is about 1 in 9, not 1 in 4 or 5.**

| Source | Default-allocator full runs | Crashed (no report) |
|---|---|---|
| RunChecks native steps, every lane (`Tests\Evidence\2026-10-1x\Native-*`, all Mimalloc) | 11 | 2: `Unnamed-water\...\Native-20261010-233243`, `Unnamed-items\...\Native-20261011-001500` |
| Sandbox full-suite runs (`RunTests Unnamed` under `C:\dev\sandbox\*\runs\*`) | about 26 | about 2: `phase6s10\runs\merge-all-20261010-165419`, `phase6s2\runs\olsuite2-050642` |
| Total | about 37 | about 4 (about 11%) |

  At 11%, 0 crashes in 12 runs happens about 25% of the time, so the old evidence says almost nothing: 2 clean stomp runs, 4 clean ANSI runs, 8 clean band runs.
- **Why the newest crashes left no dump.** RunChecks passed `-UserDir <temp>` and deleted that folder after the run. Crash folders go to `<UserDir>\Saved\Crashes` (engine `Paths.cpp:451-457, 485`), so they were deleted with it. Both crashes exited with 777004 and their logs end inside Digest without a callstack.

## 2. What ran (machine: other lanes' editors and suites throughout; the runs are untimed)

- **L1, reading (no runs).** Nothing races. Checked:
  - the preload ParallelFor and `ParseWorldgenJson` (`VoxelFeatureReader.cpp:1117-1123`, `VoxelWorldgenJson.h:82`);
  - `VoxelArea.cpp`'s three ParallelFors (per-index writes only);
  - the column pipeline (jobs hold shared refs, the destructor waits for every job);
  - the `GetDefault` caches (game thread only);
  - the `thread_local` scratch buffers: fixed size, never reallocated. Workers, standby ones included, are created once (engine `Scheduler.cpp:295-320`).
  
  A Sonnet subagent audited the pre-band tests' 11 ParallelFor bodies (Biome, Climate, Decoration, Density) plus the density scratch pool, the corner cache and the carver cache. It found no shared writes.
- **L2, bisection.** A sandbox runner (`C:\dev\sandbox\crash-hunt\worker.ps1`) ran every test through `Unnamed.Voxel.Mesher` (182 tests) and the two halves before the band, on two workers. Before each launch it checks `Scripts\QuietWindow.ps1`, the cancel file and a pause flag, and it keeps the profile when a run crashes. Result: short 0/12, nv+band 0/12, va+band 0/11.
- **L2b, the probe.** A local file, never committed and now deleted. With `-CrashHuntProbe`, after every test it ran the victim workload (3 x `VoxelWorldgen::ReadFeatures`), so a corrupted free list would fault right after the test that broke it. Result: 0/30. The probe changes allocation timing, so clean probe runs prove nothing by themselves.
- **Together: 0 crashes in 65 runs on `b52066e`.** Without the probe runs it is 0 in 35, which has p ≈ 0.017 at 11% (0.0005 if the 30 probe runs count at the same rate). This code, run this way, does not reproduce the crash.
- **L3 and L4 did not run.** Nothing was located to confirm. Jesus's change of plan, one building agent at a time, closed the lane after the probe.

## 3. The RunChecks change (committed)

- `KeepCrashes` copies `<UserDir>\Saved\Crashes` into `<Native folder>\Crashes` before the profile is deleted, on clean runs too (ensure reports). The CRASH line now says how many crash folders were kept. A failed copy warns and returns 0 instead of failing the step.
- The build step passes `-NoHotReloadFromIDE`, as `BuildFoundation.ps1` and `BuildMenu.ps1` do. Without it, a running play session's Live Coding blocks UBT (the manager's request).
- `Tests/run-checks.test.mjs`, in `npm test`: KeepCrashes runs from the script's syntax tree on a fake profile. It checks that a fake `UECC-Windows-FAKE_0000\CrashContext.runtime-xml` survives the profile deletion, that a profile without crashes keeps nothing, that a failed copy returns 0 without throwing, and that the native step calls KeepCrashes before `Remove-Item` and reports the count. Result: 4 of 4. Mutations: no copy and no call fail 2 of 3 (before the failed-copy test existed); rethrowing in the catch fails the failed-copy test.

## 4. The pattern, and the next step (waits for Jesus's word)

- **Both recent crashes were attempt 1 of a merge-profile run.** That is the first native run after a merge rebuild, and the retry passed both times:
  - water `8ff8d47`: `Checks-merge-20261010-233243`, 77 s into the run;
  - items `5d6e3a5`: `Checks-merge-20261011-001459`, 86 s into the run.
  
  S10's crash was also a merge run (`merge-all-20261010-165419`). Items S3 saw this crash in 2 of 4 full runs while `DA_Items` hard-referenced two new texture arrays with no platform data, and 0 of 3 with soft refs.
- **Hypothesis.** The corruption comes from work that only happens on first contact with new content: derived-data (DDC) misses, i.e. texture and asset builds on worker threads. Every asset in this worktree was already cached by other lanes, which would explain 0 in 65.
- **Test (recommended).** Run the short arm with `-ddc=Cold`: engine `BaseEngine.ini:2778`, an empty standalone cache flushed at every start, so every run rebuilds every asset's derived data. Add the arm with `& C:\dev\sandbox\crash-hunt\arms.ps1 -Name cold -Groups (& C:\dev\sandbox\crash-hunt\groups.ps1 -Set nv,va,band) -Extra '-ddc=Cold'`. Use one worker only: the Cold sandbox is shared, so two editors would flush each other.
  - If it reproduces: take the same set under `-stompmalloc` with `-ddc=Cold` (faults at the writer) or `-doublefreefinder` (callstacks of both frees), then bisect the asset-loading tests (Foundation.Blocks, Foundation.Items, Hud.*).
  - About 10 runs. A warm run takes about 1.5 min; the cold-cache cost per run is unmeasured, so time one run first.
- **Meanwhile:** RunChecks keeps the next crash's dump, and the callstack in it is the cheapest lead. Look in `<Native folder>\Crashes`.

## 5. Known issues

- The crash itself stays open. RunChecks still retries it once and counts it, for every lane, until the test in section 4 runs (Jesus's word).
- `C:\dev\sandbox\phase6s9\s9d\crash-hunt.md` says "binned" and "1 in 4-5". Section 1 corrects both; that sandbox file was left as is.

## 6. Token figures

- Lead peak: about 304k at the start of the wrap-up (MeasureAgentUsage via `C:\dev\sandbox\crash-hunt\measure.ps1`); the final figure is in report B.
- Sonnet audit subagent peak: 146k. It overran the 120k stop it was asked for.

## 7. Evidence

- Running notes: `C:\dev\sandbox\crash-hunt\crash-hunt.md`. Every run is in `C:\dev\sandbox\crash-hunt\runs\results.csv` (65 rows, 01:21-03:16): arm, seconds, exit, result, last test, allocator, machine state, folder. Run folders: `C:\dev\sandbox\crash-hunt\runs\<arm>-<stamp>-w1|w2`.
- Runner and helpers: `C:\dev\sandbox\crash-hunt\worker.ps1`, `arms.ps1`, `groups.ps1`, `summarize.ps1`, `wait.ps1`, `arms.json`.
- The two recent crashes (read only): `C:\dev\projects\Unnamed-water\Tests\Evidence\2026-10-10\Native-20261010-233243`, `C:\dev\projects\Unnamed-items\Tests\Evidence\2026-10-11\Native-20261011-001500`.

## 8. Open question

- **The next crash step.**
  - (a) Recommended: the `-ddc=Cold` test in section 4. One editor, no build, about 10 runs (time one cold run first).
  - (b) About 30 full native runs on current main with a warm cache, to rule out the tests after Mesher. About 45 min on two workers; with 0 in 65 already, it is unlikely to show anything.
  - (c) Wait for the next crash's kept dump.
