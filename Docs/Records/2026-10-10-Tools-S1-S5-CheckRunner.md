# Tools S1 + S5: one check runner, and TestVoxel's count from the source (October 10, 2026)

Branch `feature/check-runner` (worktree `Unnamed-s5`), from `23671cb`. Rows 1 and 5 of the Efficiency Manager's scripts audit (`C:\dev\tools\efficiency-manager\audits\2026-10-10-1831-scripts.md`), approved by Jesus at 19:24. No game or test C++ code changed; no hash or golden moved.

## What changed

- **`Scripts/RunChecks.ps1`** (new): one check runner for every lane. It replaces the one-off sandbox chains (items S4's `mergechain.ps1`, W2a's `gate.ps1`, T3b's `gate-chain.ps1`) and calls the project's own runners (`TestVoxel.ps1`, `TestFoundationRuntime.ps1`, `CaptureVoxel.ps1`).
- **`Scripts/ListAutomationTests.ps1`** (new): lists the automation tests `Source/` declares under a prefix. It gives 227 `Unnamed` and 147 `Unnamed.Voxel` at `23671cb`.
  - It counts only `IMPLEMENT_SIMPLE_AUTOMATION_TEST` at the start of a line.
  - It stops with an error on a complex test or a spec under the prefix, because those run a variable number of tests.
- **`Scripts/TestVoxel.ps1`** (S5): the expected tests are now the declared `Unnamed.Voxel` set, not `$expected=147`.
  - It fails when a declared test didn't run, or an undeclared one ran, and names it.
  - Lanes that add tests no longer edit a number.

## Usage

```powershell
& Scripts\RunChecks.ps1 -Profile quick|final|merge [-ExpectedHashes <file>] [-Flight smoke] [-Skip native,runtime-tour] [-KeepGoing] [-Allocator stomp|ansi] [-LaunchTimeoutMinutes 60]
```

| Profile | Steps |
|---|---|
| `quick` | build; TestVoxel; `Mesher.Digest` against the expected hashes; npm test; source-check |
| `final` | build `-DisableAdaptiveUnity`; the full native set; TestVoxel; radius 8 and Digest against the expected hashes; runtime all (the Playground probe), interaction and tour; create-world; npm test; source-check; the Reference verifiers |
| `merge` | `final`, plus HEAD's parents and a scan for conflict markers and unmerged files |

- **Native:** every declared `Unnamed` test must run and pass.
  - A run that leaves no report counts as a crash and is retried once. The summary counts every crash, also when the retry passes, and names the crashed run's folder.
  - `-Allocator stomp|ansi` passes `-stompmalloc` or `-ansimalloc` to that run for crash hunts. It is untested here.
- **Timeouts:** each launch gets `-LaunchTimeoutMinutes` (default 60).
  - When it runs out, a watchdog stops this worktree's UnrealEditor processes started since that launch. The step fails and names them.
  - It does not stop a hung node or npm that runs without an editor.
  - The radius-8 editor sometimes sits idle after its hashes (items S5 and S7). It gets 30 s after the third set of hashes and is then stopped. The summary says which ending it had.
- **Hashes:** `-ExpectedHashes` defaults to `C:\dev\tools\shift-managers\expected-hashes-main.txt`, the manager's copy of main's values. It is kept current at every fast-forward that moves hashes.
  - Format: one `<key> 0x<hash>` per line, e.g. `r8 overworld 0x…` or `digest overworld, plants and trees on, models on 0x…`.
  - A missing file fails the step.
  - `hashes.txt` in the run folder has every hash the run produced, in the same format.
- **Runtime:** the fps is a figure in the summary, not a gate.
- **`-Flight smoke`:** the flight must complete with no errors besides the engine's LogPython traceback (EditorToolset, at every start).
- **Gates before every launch:**
  - The run stops if the cancel file exists (default `C:\dev\sandbox\locks\cancel-<worktree folder>.flag`, exit 2).
  - It waits while `C:\dev\sandbox\locks\quiet-window.lock` exists.
  - It logs the machine state.
  - Windowed `-game` runs wait while `cef-e2e.lock` exists.
  - create-world takes the CEF lock once no other `-game` UnrealEditor runs. It creates the lock atomically with the lane and time, and deletes it afterwards if it is still its own.
- **Output:**
  - Every suite gets a fresh folder under `Tests/Evidence/<today>`: `Native-*`, `VoxelAutomation-*`, `Radius8-*`, `Playground-<suite>/Runtime-*`, `CreateWorld-*`, `VoxelFlight-*`.
  - `Checks-<profile>-<stamp>/summary.txt` holds the summary and the step logs. It is made after the build, so it is never older than the DLLs.
  - The summary gives pass or fail per step, the hashes, and the time chain: the newest source changed since `-Base` < its DLL < each suite. `-Base` defaults to the main folder's branch, which is the lanes' "main" (the `main` branch is stale).
  - It ends with the `-Suites` list for `verify_lane.ps1`.
- **Exit codes:** 0 = green, 1 = red, 2 = cancelled.

## How lanes adopt it

- Use `-Profile quick` while iterating, `final` before the pre-commit review, and `merge` after merging the main lane.
- Report the summary path and its `-Suites` line. The manager runs `verify_lane.ps1` with them.
- Lane-specific checks (light gates, mutation batches, DLL-swap A/B) stay small add-on scripts, run after the runner. Leave out what they replace with `-Skip`.
- Only a lane that moves hashes on purpose sees the hash step fail. It records the new values; the manager updates the expected file at the fast-forward.

## Merge conflict note (items S7, W2b, any lane that changed `$expected`)

Lanes that set `$expected=<n>` in `Scripts/TestVoxel.ps1` conflict with this change when they merge the main lane.

- **Resolve it by taking main's version:** `git checkout --theirs -- Scripts/TestVoxel.ps1`, then `git add` it. While merging the main lane into a lane, "theirs" is the main lane.
- The lane's new tests are counted from the source, so nothing is lost.
- TestVoxel's `PASS: <n> voxel suites` line then shows the new total.

## Proof

All on `23671cb` with the committed scripts (`b2d3fc9`). Folders are under `Tests/Evidence/2026-10-10`.

- **S5 mutation:** a temporary, never-built `.cpp` declared `Unnamed.Voxel.MutationS5`.
  - 148 were declared and 147 ran, and TestVoxel threw: `not run: Unnamed.Voxel.MutationS5`.
  - It was run twice: before and after the review fixes. The file is gone and the count is back to 147. Log: `C:\dev\sandbox\check-runner\mutation-s5.txt`.
- **`-Profile merge`** (`Checks-merge-20261010-214724`). The quiet window at 22:00 stopped it after create-world.
  - native 227 of 227, 0 crashes; TestVoxel 147.
  - Hashes 9 of 9 equal to main's:
    - Radius 8: `0xf427b0a0ec0e6eaa`, `0xcac6968a2e00c046`, `0x0a9cb42b08a0f10a`.
    - Digest: S9c's six "plants and trees on" values.
  - Runtime all, interaction and tour: 0 failures at 44.3, 43.8 and 40.2 fps.
  - create-world 32 of 32.
  - Time chain clean.
- **The rest, with `-Skip`** (`Checks-merge-20261010-222021`): green.
  - Flight passed in 126 s with no errors. Its gate first waited 6.5 min behind the main lane's CEF lock.
  - npm 57 of 57; source-check passed; 5 of 5 verifiers.
- **Gates:**
  - A cancel file stops the run before the next launch with exit 2. The default flag stopped two proofs cleanly.
  - With `-LaunchTimeoutMinutes 1`, the watchdog stopped only the timed TestVoxel editor, not the worktree's older create-world editor (`Checks-quick-20261010-214224`).
- **Reviews:** Sonnet reviewed the diff twice: the scripts, then the later watchdog, radius grace and allocator. The fixes guard the CEF lock and the radius editor with `finally`, and close several false-PASS paths. The comment-aware lister also came from the review.

## For S2 (quiet windows)

- The runner waits while `C:\dev\sandbox\locks\quiet-window.lock` exists, before every launch. A launch already running finishes first.
- The manager asks that this file be the window's single on/off switch.
  - Today the guard (`quiet_check.ps1 -Guard`) has no end signal and is killed after every window.
  - If the guard also watches the file and exits when it is deleted, the guard and the held lanes all key off one file.

## Not done

- `foundation-menu.mjs` is not a step. It would use the same CEF-slot code if lanes want it.
- No timeout for a hung node or npm without an editor.
- `-Allocator` is untested.
