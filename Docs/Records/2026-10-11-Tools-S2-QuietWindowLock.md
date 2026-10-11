# Tools S2: the quiet-window lock, one switch for every lane (October 11, 2026)

Branch `feature/lock-holds` (worktree `Unnamed-s5`), from `8ff8d47`. Row 2 of the Efficiency Manager's scripts audit (`C:\dev\tools\efficiency-manager\audits\2026-10-10-1831-scripts.md`), approved by Jesus on 2026-10-10. No game, test or world output changed; no hash or golden moved.

**Why:** a quiet window used to take a "QUIET WINDOW HOLD" message to every lane, a "held" from each, a hand-started guard that had to be killed, and a "WINDOW OVER". That was about three extra manager wake-ups and 6 to 9 worker turns per window, at about ten windows a day. Now one file, `C:\dev\sandbox\locks\quiet-window.lock`, holds every chain.

## What changed

- **`Scripts/QuietWindow.ps1`** (new, in the repository): the gate every chain calls before each heavy launch. `RunChecks.ps1` calls it at every launch.
  1. The lane's cancel file stops the chain. Default `C:\dev\sandbox\locks\cancel-<lane>.flag`; it throws `OperationCanceledException`.
  2. It waits while the lock holds this lane: the window is another lane's, or ours while it is still clearing.
  3. Inside our own open window it goes and returns a note.
  - `-Game` also waits while the CEF lock exists. `-Slot` also waits while another `-game` UnrealEditor runs.
  - A wait past `-MaxWaitMinutes` (120) throws `TimeoutException`.
  - The lane is the worktree folder of the copy you call (`Unnamed`, `Unnamed-items`, ...); `-Lane` overrides it.
- **`Scripts/RunChecks.ps1`:** `Gate` calls the helper. Its wait lines, cancel (exit 2) and give-up failure read as before.
  - Inside its own window, the launch line ends with `inside our quiet window (open since <time>, <reason>)`, which is evidence that a timed run ran in the window.
  - New `-QuietLock` (default the real lock), for tests.
- **`C:\dev\tools\shift-managers\scripts\quiet_window.ps1`** (new, the manager's folder): `-Open <lane> [-Reason] [-ClearMinutes 30] [-MaxMinutes 60]`, `-Close [-Lane <lane>]`, `-Status`. How it works is under "The lock" below.
- **`C:\dev\tools\shift-managers\scripts\quiet_check.ps1`** (the manager's folder):
  - `-Lock`: `-Guard` and `-Window` end when the lock that existed at their start is removed or replaced. `-WaitClear -Lock` exits 3.
  - `-Window`: guard the lane the lock names.
  - `-KeepWatching`: log each intruder when it is confirmed and when it is gone, keep going, and list them at the end; exit 1 if any.
  - `-HeavyNames`: a test seam that replaces the heavy list.
  - Lanes come from the `dev\projects\<folder>` part of a command line, or else from the nearest parent's (up to six levels), instead of a fixed folder list. A new worktree no longer reads as `?`, and build children (cl, link, ShaderCompileWorker) usually find their lane through Build.bat or the editor.
  - Any `UnrealEditor*` counts as heavy.
  - The `?` rule (the manager's condition): a `?` process that isn't an editor is excused while the guarded lane has processes, as before; a `?` `UnrealEditor*` always counts.

## The lock

JSON from `quiet_window.ps1 -Open`. The `*Unix` fields carry the logic, so `ConvertFrom-Json`'s date parsing never matters. The text times are for people.

```json
{"owner":"Unnamed","state":"clearing|open","reason":"S9d bench","id":"<guid>","created":"2026-10-11 14:02:03","opened":"","expires":"...","expiresUnix":0,"watcherPid":1234,"watcherStartUnixMs":0}
```

- **Writes:**
  - The lock is created with a temp file and `File.Move`, which never overwrites.
  - It flips to open with `File.Replace`, which fails when a `-Close` got there first.
  - Writes, deletes and reads retry for a moment, because a gate reading the file shares no delete access.
- **Clearing:** every lane's gate waits, the owner's too, while runs already in flight finish.
  - `quiet_check.ps1 -WaitClear` must see no heavy process on two polls in a row, 10 s apart.
  - If that doesn't happen within `-ClearMinutes`, the watcher removes its lock and exits 3 with the BUSY list.
- **Open:** the owner's gates go; the others still wait. The watcher's guard logs intruders with their times until the lock is removed. At `-MaxMinutes` the watcher removes the lock itself (exit 5).
- **Stale:** a gate ignores the lock, and says so once, in these cases:
  - its watcher's pid is gone or reused (the start time differs);
  - it is more than 5 min past its expiry;
  - it has no watcher (a hand-written lock) and was last written 120 min ago or more.
  - The next `-Open` replaces a stale lock.
- **Watcher exit codes:** 0 clean, 1 intruders, 2 refused (a live lock exists, any owner), 3 could not clear, 4 closed before it opened, 5 expired, 6 an error or Ctrl+C (it removes its lock).
  - Every line also goes to `quiet-window.log` beside the lock, tagged with the window id.
  - `-Close` prints that window's lines and exits 1 if they hold an INTRUDER.

## How to use it

- **Manager:**
  - Open: `pwsh -NoProfile -File C:\dev\tools\shift-managers\scripts\quiet_window.ps1 -Open <lane folder> -Reason '<what>'` as a background task. Give the task a timeout above the clear and window minutes (Bash: `timeout` 7200000, keeping `-ClearMinutes` plus `-MaxMinutes` under about 115). If the task is stopped, the lock goes stale and the gates ignore it.
  - Its exit is the one notification per window.
  - Close it on "window done" with `-Close`, unless the lane already did. Never kill the watcher to end a window.
- **Owner lane:**
  - Ask for the window. Your gated launches start by themselves when it opens.
  - After the timed runs, run `pwsh -NoProfile -File C:\dev\tools\shift-managers\scripts\quiet_window.ps1 -Close -Lane <your folder>`.
  - Send "window done" with any intruder lines, and re-run the rounds they overlapped.
- **Every lane, outside RunChecks:** call the gate before every heavy launch (a build, an editor, a suite, a bench, a capture, a subagent's chain):

```powershell
& Scripts\QuietWindow.ps1 -Name 'bench round 3'            # add -Game for windowed -game runs
pwsh -NoProfile -File Scripts/QuietWindow.ps1 -Name x && <launch>   # from Bash
. Scripts\QuietWindow.ps1; Wait-QuietWindow -Name x          # dot-sourced, as RunChecks does
```

- **Transition:** a lane on the old `RunChecks.ps1` waits on any lock, its own included, until it merges this branch. The main lane gets the new runner at the fast-forward. Give an unmerged lane its window the old way (HOLD messages), or have it merge first.

## Tests

All of these ran against sandbox locks (`C:\dev\sandbox\quiet-window-s2\locks\`, `locks-d\`). The real lock was absent before and after every suite. The heavy processes were the fake editor (`UnrealEditor-Fake.exe`, a copy of cmd.exe) and `FakeWorker.exe`, with `-HeavyNames`, so the lanes' real runs didn't count. After the manager's note, fakes name only `dev\projects\Unnamed-fake1/2/3`, never a real worktree. Scripts and outputs are in `C:\dev\sandbox\quiet-window-s2\`.

74 checks, all passing on the final scripts (after the review fixes), 00:54 to 01:02, plus B10 at 01:03. The machine was busy with other lanes' editors and suites throughout, which didn't matter because the tests count only the fakes.

- **A, the gate (`test-helper.ps1`, 26 of 26, `test-helper-final.txt`):**
  - no lock;
  - another lane's window;
  - our own window while clearing, and while open (with its note, and the owner match ignoring case);
  - stale locks: watcher gone, pid reused, 400 s past expiry (200 s still holds), a hand-written text or JSON lock fresh and 121 min old;
  - the cancel file, which wins over a hold;
  - the default lane (`Unnamed-s5`);
  - a wait released when the lock goes (6.3 s for a 6 s removal);
  - `-Game` with the CEF lock;
  - `-Slot` with a `-game` fake (its control went before the fake started);
  - an unreadable lock holds;
  - `pwsh -File` exit codes;
  - a stop ends a PowerShell chain;
  - dot-sourced use.
- **B, `quiet_check.ps1` (15 of 15, `test-check-final.txt`, plus B10 below):**
  - the old one-shot usage on the real machine;
  - lanes from a command line, from a parent, and `?`;
  - `-Window` with no lock (exit 2);
  - `-Guard` ending 3.4 s after the lock's removal;
  - `-Window -KeepWatching`: an intruder logged, then "gone", the owner's own fake never listed, exit 1 at the removal;
  - the `?` rules, all three cases;
  - `-WaitClear -Lock`: exit 3 when the lock goes, and CLEAR 16.3 s after the in-flight fake ended.
  - B10, lane parsing as a unit (`test-lanes.ps1`, 8 of 8, `test-lanes-final.txt`): `Get-LaneOf` is taken from the script's syntax tree and run on made-up process records, so no fake names a real worktree.
    - The play worktree reads `Unnamed-play`, not the main lane.
    - Also covered: the main lane, forward slashes, a folder closed by a quote or a space, MyProject under Documents (`?`), and cl → UBT → Build.bat naming `Unnamed-play`.
    - A reused parent pid (younger than its child) is not followed.
- **C, `quiet_window.ps1` (20 of 20, `test-window-final.txt`):**
  - `-Status`;
  - an unknown lane refused;
  - the full window: written at once as clearing, every lane holding, refusals of a second `-Open` from both lanes, open 21.2 s after the in-flight run ended, the owner going and the others holding, an intruder logged;
  - `-Close -Lane` by a non-owner refused; by the owner, 5.1 s with the window's log and exit 1;
  - the watcher's summary and the log file;
  - could not clear (exit 3, lock removed);
  - closed while clearing (exit 4);
  - expiry (exit 5, lock removed);
  - a killed watcher: stale for the gates and `-Status`, replaced by the next `-Open`, its orphaned guard exiting;
  - no leftover processes.
- **D, RunChecks (5 of 5, `test-runchecks-final.txt`):** `-Profile quick -Skip build,voxel,hashes,npm -QuietLock <sandbox>`, so source-check is the only launch.
  - Behind another lane's open window it logged `waiting before source-check: quiet window for Unnamed-fake3 (open, D1 foreign window)` and launched 5 s after `-Close`; GREEN: `Tests\Evidence\2026-10-11\Checks-quick-20261011-010028\summary.txt`.
  - In its own window it launched at once, with `inside our quiet window (open since 01:01:27, D2 own window)` on the launch line; GREEN: `Checks-quick-20261011-010128`.
  - With the cancel file: CANCELLED, exit 2 (`Checks-quick-20261011-010134`).
- **Review:** Sonnet read the diff only (487 lines; 57.8k peak) and confirmed no defect. Fixes taken:
  - the gate looks again 200 ms after a missing lock, because `File.Replace`'s flip may leave no file for an instant;
  - the log callback's output is discarded;
  - quiet_check retries its start-up lock read for 2 s;
  - quiet_window treats an unexpected quiet_check exit as a failure (exit 6, lock removed), never as BUSY or expiry.
  - Not taken, by design: `-Guard` by hand now ends with the window (the asked-for end signal), and a `?` editor always counts (the manager's condition).
  - All four suites were re-run after the fixes (the counts above).

## Known issues

- **A check-then-delete race:** between reading the lock and deleting or flipping it, a close and a new open in the same few milliseconds could remove or overwrite the newer window. When: only if two windows are ever opened or closed by scripts at the same instant; the manager runs one command at a time. A named mutex around the writes would close it.
- **Editors that read as `?`:** an editor whose command line names no `dev\projects\<folder>`, and whose parents name none either, is `?` and always an intruder. Examples: Jesus's MyProject editor under Documents, or a lane's editor started with a relative `Unnamed.uproject` from a shell whose command line names no path. When: such an editor is open during a window; it also keeps the machine from clearing. The first is intended ("no window while Jesus plays"). For the second, launch editors with an absolute `.uproject` path, as the runners do.
- **Untested:** the exit 6 paths (quiet_check failing under the watcher).
- **Window length:** a window lasts at most as long as the manager's background task. The Bash tool stops it after its timeout (at most 2 h), and the lock then goes stale.
- **Transition:** lanes on the old RunChecks wait on any lock, their own included, until they merge this.

## What worked and what failed

- Worked:
  - The fake editor with `-HeavyNames` let the clearing and guard tests run on a busy machine.
  - Taking a process's lane from its parents made `?` rare.
- Failed and fixed:
  - In the test helper, `Kill` is PowerShell's alias for `Stop-Process`, so it shadowed the test's own function (renamed `KillFake`).
  - `Start-Process -ArgumentList` doesn't quote arguments with spaces.
  - `[IO.File]::Replace(..., $null)` passes an empty string; it needs `[NullString]::Value`.
  - An OrderedDictionary indexed with an int picks by position, so string keys are needed.
  - A Bash heredoc ate a regex's `\\`, as the note in quiet_check warns.
- The first quiet_window test run gave its fakes real worktree names (`Unnamed-items`). Items' RunChecks logged one in its machine state, and verify_lane listed it as that lane's editor. The manager flagged it. The run was stopped and every fake was renamed `Unnamed-fake*`.

## The merge

The manager verified report A and approved the commit, `8593a78`. Then `feature/voxel-world` at `5d6e3a5` was merged once (`0e00c36`, parents `8593a78` + `5d6e3a5`). There were no conflicts: the index kept both dates' lines. 17 incoming paths, of which `Source/Unnamed` Foundation and two test files are code.

**Proof:** `Scripts\RunChecks.ps1 -Profile quick` with no skips and no lock present, so the gate passed straight through with no wait lines.
- The build took 48 s; the `Unnamed` DLL is from 01:05:50.
- TestVoxel: 161 voxel suites.
- Mesher.Digest: the 6 digest hashes equal `expected-hashes-main.txt` (valid at `5d6e3a5`); quick doesn't run the 3 radius-8 hashes.
- npm: 57 of 57. source-check: passed.
- The time chain is clean. The machine had other lanes' editors running.
- GREEN in 3 min: `C:\dev\projects\Unnamed-s5\Tests\Evidence\2026-10-11\Checks-quick-20261011-010551\summary.txt`; suite `VoxelAutomation-20261011-010551`.

## Usage

- Lead: 267k peak context by MeasureAgentUsage, after the merge proof.
- One subagent, the Sonnet review: 57.8k peak, 95.7k processed.

## Open questions

- The fake editor still appears in other lanes' machine-state lines (`UnrealEditor*`) while a test runs. That is harmless, but say so in the fake editor's README.
- Answered: the watcher stays a background task of the manager session (the manager, 2026-10-11).
