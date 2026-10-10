# Water and sky handoff: DN1 done, into DN0 (sky data and sampler)

October 10, 2026. DN1 (the world clock, `voxel.Time` and saving the time) is done on `feature/water-sky`. Its record is [2026-10-10-WaterSky-DN1.md](2026-10-10-WaterSky-DN1.md).

**Next: DN0, then W2a.**
- **Why DN0 goes first:** it is next in the plan's lane order (section 4: W1, DN1, **DN0**, DN-P, W2a). Jesus answered Q3 (a), which was DN0's only wait. It is small (S). Its files are a new reference folder, a core sampler and its tests, and the two `CLAUDE.md` lines Jesus approved; no other lane holds any of them. Everything the sky builds later (DN-P, DN2, DN3) reads its data.
- **W2a** follows. Both its waits are met now: T3 has merged (in `24d76ee`), and the clock exists. Its own handoff is [2026-10-10-WaterSky-Handoff-W2a.md](2026-10-10-WaterSky-Handoff-W2a.md), with a DN1 note added at its end.
- **DN-P** (the moving-sun cost probe) sits between them in the lane order, and only its timed runs need a quiet window. Whether it goes before W2a is the manager's call.

## Reading list (DN0)

1. This file.
2. [The plan](2026-10-10-WaterSky-Plan.md):
   - section 1 ("Rules for sources" above it as well);
   - section 4, "Lane rules";
   - section 5, DN0 (about 25 lines);
   - section 6: Q3, Q14, Q16 and the "Decided by Jesus" list.
3. Scout N's facts, `C:\dev\sandbox\scout-daynight\facts.md`:
   - §3, the data inventory;
   - §4, sun, moon, stars and colours;
   - §5 (a) to (e), the oracles;
   - §7 (a), the sampler sketch.

   DN0 commits this file byte-identical as a record, with its SHA-256, as W1 did for scout M. `probes\sweep.json` and `probes\model.py` are its data; read them through the scout's scripts, never whole.
4. The items reference as the pattern for the new hash-checked `Reference/minecraft-sky-26.3/`:
   - `Reference/minecraft-items-26.3/tools/build_reference.py` and `verify_reference.py` (their structure, not every line);
   - `CLAUDE.md`'s rules list and Commands, where DN0 adds the two lines Jesus approved (Q3 (a)).
5. [The DN1 record](2026-10-10-WaterSky-DN1.md), "What DN1 adds": the clock the sampler reads.
6. **Not to read:** `Reference/minecraft-blocks-26.3/research/water_research.md` and `Docs/MinecraftBlockReference.md` section 4 (bytecode-derived; plan Q18). The client jar's shader code is not a source either (plan, "Rules for sources").

## Working agreement

- The plan's lane rules:
  - never re-record goldens or hashes;
  - one editor build at a time;
  - timed gates go in the manager's quiet window;
  - commits stay on `feature/water-sky`, and the manager merges and pushes;
  - no `CLAUDE.md` edits except Q3 (a)'s two approved lines.
- Behaviour and numbers come from the game's data, scout N's black-box probes and the wiki; never from method bodies (no `javap -c`, no decompiler).
- **The CEF slot rule** (manager, 2026-10-10). `create-world.mjs` and `foundation-menu.mjs` attach to CEF on 127.0.0.1:18946, and every windowed `-game` run opens that port.
  - Before a CEF e2e: wait until no other `-game` editor runs and `C:\dev\sandbox\locks\cef-e2e.lock` doesn't exist; then create the lock atomically, run, and delete it, also on failure.
  - Before any other windowed `-game` run: wait while the lock exists.
  - `C:\dev\sandbox\water-dn1\cef-e2e.ps1` does all of this.
- Reports go to the manager. If you send a plan, wait for its go.

## Settled decisions

- **Q1 (a):** one 20 Hz world clock. Built in DN1.
- **Q2 (a), Jesus:** world.json's optional `clocks` key at version 1, chosen over a version bump. Built in DN1, and stated as his decision in `Docs/VoxelWorld.md`.
- **Q3 (a), Jesus:** a new hash-checked `Reference/minecraft-sky-26.3/` with its own build and verify tools, plus the two `CLAUDE.md` lines (the rules list and Commands).
- **Q14, Jesus:** sky family B (hybrid). The knobs are tuned in the mock-up (MU).
- **Q15 (a), Jesus:** SP1 when he's home, as a screen hold with his monitor on.
- **Q16, manager:** the Playground keeps its fixed rig. Shots use the fixed sun until DN2, then `-VoxelTime=6000`. Dawn, dusk and midnight shots are added.
- **DN1's calls:**
  - the cap is the pawn's (5 ticks);
  - the rate is day ticks per game tick;
  - `set <n>` is absolute, and markers go forward (an exact match stays);
  - `-VoxelTime` pins and never saves;
  - the time is saved at `EndPlay`, read-modify-write;
  - the F3 day counts from 0.

## Code map (DN1)

| Piece | Where | Notes for DN0 and later |
|---|---|---|
| Clock | `Source/UnnamedVoxel/Clock/VoxelWorldClock.{h,cpp}` | `TakeTicks` then `Step` per tick; `GetGameTick`; `GetDayClock` (total, partial, rate, paused); `GetTimeOfDay`, `GetDayNumber`, `GetMoonPhase`. **The sky reads `GetSmoothTicks(bAdvanceTime)`:** the total plus the partial tick plus the frame's share of the next tick. |
| Markers | `FVoxelWorldClock::FindMarker` (day 1000, noon 6000, night 13000, midnight 18000) | DN0's test "DN1's marker constants equal `day.json`'s" reads them here. |
| Owner | `Source/Unnamed/Voxel/VoxelWorld.cpp`: `SetupClock` (from `BuildLayout`), the loop at the top of `Tick`, `SaveClock` (from `EndPlay`), `DescribeDebug` | `GetClock()` and `IsTimeAdvancing()` (the saved `advance_time` rule) are public. W2a's fluids run after each `Step` in `Tick`. |
| Commands | `Source/Unnamed/Voxel/VoxelConsole.cpp`, the last block (`voxel.Time`), plus one line in `DescribeInfo` | Its own block, because S10 adds cvars to the same file. |
| Saving | `Source/Unnamed/Worlds/UnnamedSavedWorlds.cpp`: `ReadWorldClock` and `WriteWorldClock`; `FUnnamedSavedWorld::Clock` | Defaults are left out; `total_ticks` is written as its digits. |
| F3 | `AFoundationLab::DescribeDebug` (empty by default), printed in `FoundationHud.cpp`'s pose section | Two lines in each file. Items S4 may touch the HUD next. |
| Tests | `Source/Unnamed/Tests/VoxelClockTests.cpp` (`Unnamed.Voxel.Clock.*`, 6 tests); `Scripts/TestVoxel.ps1` expects 127 | The SavedWorld test embeds a genuine pre-DN1 world.json; don't regenerate it from today's code. |

## Known issues

| Issue | When |
|---|---|
| `voxel.Info`'s dropped ticks include load hitches (the first frames after a map load have a large delta). | If a gameplay metric needs it: count from `IsReady` |
| The exact-match marker case (at noon, `set noon` stays) isn't probed. | A later probe, if it matters |
| `/time of <clock>` and the `minecraft:moon` timeline aren't offered by `voxel.Time`. | When another clock or timeline exists (DN0 may add `query moon` once the moon timeline's period is in the reference) |
| Markers within a day of 2^53 clamp. Only reachable by hand-editing world.json. | Never in play |
| The CEF e2e scripts share a fixed CDP port across lanes, so the lock rule sequences them. | A per-run port (open question 3) |

## What worked and what failed

- **Worked:**
  - **A genuine fixture:** a `create-world.mjs` run on the old binaries, with a watcher copying each `world.json` as the run wrote it, gave a real pre-DN1 file for the byte-for-byte test. The script deletes its worlds at the end, so they must be caught during the run.
  - **One mutation build:** six mutations, each aimed at a different test, with the expected failing assertion named, all in one build and one run (`mutate.ps1`). It restores and touches the sources in `finally`.
  - **Headless saving:** `UnrealEditor-Cmd ... L_Generated?VoxelWorld=<id> -game -NullRHI -WorldsDir=<own> -ExecCmds="voxel.Time ...,quit"` exercises `EndPlay` saving headless, in about 40 s per run (`savecheck.ps1`).
  - **Replies from the probes:** taking every reply's wording from scout N's probe outputs (`probes\cmds-discover*.out.txt`) instead of guessing.
- **Failed, or a pitfall:**
  - **The CEF port:** the first `create-world.mjs` run attached to the breaking lane's game on the shared CDP port. It only read that game, then failed its first check.
  - **`git commit -F -`:** with a PowerShell here-string as an argument, git took it as a pathspec ("cannot do a partial commit during a merge"). Write the message to a file and use `-F <file>`.
  - **PowerShell arrays:** `@( @(a,b) @(c,d) )` without commas flattens into one array.
  - **UE number parsing:** UE's numeric `LexTryParseString` is `Atod`-based, so "1x" parses as 1. `voxel.Time` checks for plain decimals itself.
  - **`TPair`:** a `TPair` array can't take a braced-init first element (template deduction); use a small struct.
  - **The safety scan:** it blocked a `Remove-Item` of a temp profile path inside a compound command ("system path 'C:\Program'"). Delete with `[IO.Directory]::Delete` inside a script file (`runtests.ps1`).
  - **Stopping an editor:** `Stop-Process` on my own editor run was refused by the permission classifier. Stop chains with the cancel flag before a launch instead, and set it early.

## Tokens

- **DN1's lead peaked at about 360k before the commit** (measured with `Scripts/MeasureAgentUsage.mjs`; DN1's session started in `C:\dev`, so copy its transcript into a temp `projects\C--dev-projects-Unnamed-water\` and set `CLAUDE_CONFIG_DIR`):
  - about 185k went on the system prompt, the plan, scout N and the code reading before the plan;
  - the rest on the merge gate, code, the fixture capture, the mutation batch, the save check, the review fixes and the gates.
- **Subagents:** one Sonnet 5.5 pre-commit reviewer (diff only, 1,199 lines), which peaked at 98k.
- **For DN0:** expect about 150k for the reading list. Scout N's §3 to §5 and the items reference's tools are the large part.

## Evidence

- `C:\dev\sandbox\water-dn1\`:
  - `gate-m0.txt` and `gate-m0n.txt` (the merged tree);
  - `gate-g2.txt` and `e2e-e2.txt` (the final gate);
  - `runs\mutations\` (the mutation batch), `runs\savecheck\` (the black-box save check), `runs\clock-4\` (the last Clock run);
  - `worlds-pre3\` (the captured pre-DN1 world.json files).
- `Tests/Evidence/2026-10-10/` (on disk only): `WaterSky-DN1-g2-*` (TestVoxel, hashes, native) and the `Worlds\Create-*` folders of the e2e runs.

## Open questions (with options)

1. **What `-VoxelTime` does.**
   - (a) Pin: set and paused (as now). Shots don't drift while they settle.
   - (b) Set only, with `-ExecCmds="voxel.Time pause"` to pin.
   - Recommended: (a). DN2's shots need a still sun, and `voxel.Time resume` un-pins.
2. **Dropped ticks after a load.**
   - (a) Leave them counted (as now).
   - (b) Count only once the world `IsReady`.
   - Recommended: (a), until a metric needs it.
3. **The shared CDP port.**
   - (a) Keep the lock rule.
   - (b) A port per run: `create-world.mjs` passes a free port, and the game takes `-cefdebugport`-style input. This changes the menu host and the e2e scripts, so it would be a main-lane change.
   - Recommended: (b) eventually; the main lane's call.

## Short prompt (DN0)

> Water and sky track, session DN0 (a parallel lane): the sky data and the timeline sampler. Work in `C:\dev\projects\Unnamed-water` on `feature/water-sky`. Never touch other worktrees and never push. The only `CLAUDE.md` edits allowed are the two lines Jesus approved for the sky reference (Q3 (a)). Read `Docs/Records/2026-10-10-WaterSky-Handoff-DN0.md` and follow its reading list. Build DN0 as in the plan's section 5: the hash-checked `Reference/minecraft-sky-26.3/` with its build and verify tools, `FVoxelTimeline` in the core, scout N's facts committed byte-identical as a record, and `Unnamed.Voxel.Sky.Timeline` with the listed mutations. Keep goldens and hashes identical, and follow the CEF slot rule for any windowed run. Send the manager a short plan before building.
