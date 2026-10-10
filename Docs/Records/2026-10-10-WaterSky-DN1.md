# Water and sky DN1: the world clock, voxel.Time and saving the time

October 10, 2026. Lane `feature/water-sky` (worktree `C:\dev\projects\Unnamed-water`), session DN1 of the [water and sky plan](2026-10-10-WaterSky-Plan.md) (section 2, the shared world tick; section 5, DN1). The manager confirmed the plan and DN1's six calls under Jesus's away rule. The next session's handoff is [2026-10-10-WaterSky-Handoff-DN0.md](2026-10-10-WaterSky-Handoff-DN0.md).

**First, the merge:** `feature/voxel-world` at `c90be1f` (S9a's trees, torch T3 and T3b) was merged once, without rebase or force (`24d76ee`). There were only the two expected conflicts: TestVoxel's `$expected` became main's 111 plus W1's 10 (121), and the records index kept both sides. It was built with `-DisableAdaptiveUnity`, and everything was green before any change:
- TestVoxel 121 of 121;
- the six radius-8 hashes at main's trees-on values;
- native 187, `npm test` 57 and source-check;
- `create-world.mjs` 32 PASS.

## What DN1 adds

- **`FVoxelWorldClock`** (`Source/UnnamedVoxel/Clock/`): pure and UObject-free. It is the world's one 20 Hz clock (Q1 (a)).
  - Each frame's seconds become whole game ticks. A partial tick carries between frames, and at most `UFoundationProfile::MaxCatchupSteps` (5) ticks run per frame, the pawn's own cap. After a hitch the excess is dropped and counted.
  - Two counters, as in 26.3:
    - the **game tick** (`uint64`) runs whenever the world does;
    - the **day clock** (`FVoxelDayClock`: total `int64`, partial tick, rate, paused; 26.3's `ClockState`) moves `rate` day ticks per game tick, only while `advance_time` is on and the clock isn't paused.
  - A day is 24,000 ticks, and the total keeps counting. The day timeline is the total modulo a day; the moon phase is the day number modulo 8, starting full (scout N 5 (c)).
- **`AVoxelWorld`** owns the clock:
  - `Tick` takes the frame's ticks at its top and calls `Step` per tick. W2a's fluids go after each `Step`.
  - `SetupClock` (at `BuildLayout`) reads the pawn's tick length and cap, the saved world's clock and its `advance_time` rule (read as `ConfigurePlayer` reads rules; default on), then `-VoxelTime`.
  - `SaveClock` (at `EndPlay`) re-loads world.json, sets the clock and saves, so the menu's changes stay. It never runs for the dev seed or a pinned session.
- **`voxel.Time set|add|query|pause|resume|rate`** (in its own block at the end of `VoxelConsole.cpp`):
  - The replies are 26.3's own words from scout N's probes: "Set minecraft:overworld to 25002 tick(s)", "Set minecraft:overworld to time marker minecraft:noon", "Timeline minecraft:day is at 1004 tick(s)", "Clock minecraft:overworld is at 6000 tick(s)", "The game time is 91 tick(s)", "Paused clock minecraft:overworld", "Resumed clock minecraft:overworld", "Clock minecraft:overworld will now advance at 2.0x normal rate".
  - Times are plain decimals with `d`, `s` or `t`, up to 2,147,483,647. Markers are day 1000, noon 6000, night 13000 and midnight 18000; a marker moves forward to its next tick of the day and stays if already there. The rate runs from above 0 to 1000.
  - `voxel.Info` adds the clock line and the dropped ticks.
- **`-VoxelTime=<time or marker>`** pins the day clock for shots and flights: set and paused. The game tick still runs, and the session saves no time.
- **The F3 line** (pose section, through a new `AFoundationLab::DescribeDebug`, empty by default): `Time: day 1 at 1004 (07:00) | moon waning gibbous | game tick 4521`, plus the rate, `paused` or `advance_time off` when they apply.
- **world.json's optional `clocks` key** at version 1. **This is Jesus's own decision (the plan's Q2 (a), 2026-10-10), chosen over a version bump.** It is not a breach of the version-bump rule:
  - layout: `"clocks": {"minecraft:overworld": {"total_ticks": 13219, "paused": true, "rate": 3.0}}`, vanilla's `world_clocks.dat` layout, with defaults left out;
  - `total_ticks` is written as its exact digits (`FJsonValueNumberString`);
  - other clock ids and unknown fields are ignored; malformed values refuse the file, as every other field does;
  - a missing key is a new world's tick 0;
  - a world not left since DN1 writes no key and is byte-identical to before.
- **The sky stays today's fixed rig.** The game tick is not saved; it goes with phase 10, with the fluids' pending acts.

## The calls made (manager, under the away rule)

1. **The cap** is the pawn's (0.05 s, 5 ticks). Dropped ticks are counted (in ticks), as `FFoundationSimulation` counts dropped seconds.
2. **The rate** is day ticks per game tick, with the fraction carried in the partial tick. The range is (0, 1000] (wiki); `set` and `add` refuse negatives.
3. **`set <n>`** sets the total (probe). A marker goes forward to the next match at or after now. The exact-match case isn't probed; it stays.
4. **`-VoxelTime` pins** (set and paused) and never saves.
5. **The save** happens at `EndPlay`, read-modify-write, only for a saved world.
6. **The F3 line's** day counts from 0 (vanilla's F3), the hour is 06:00 at tick 0, and the moon phase names come from scout N.

## Tests

`Unnamed.Voxel.Clock` (`Source/Unnamed/Tests/VoxelClockTests.cpp`), 6 tests, from the plan's list (scout N 5 (c) and (f)):
- `Ticks`: 20 ticks a second at 30, 60, 144, 165 and 240 fps; 0.07 s frames run 1, 1, 2 with 0.2 carried; a new world at tick 0; `TakeTicks` plus `Step` equals `Advance`.
- `DayClock`: rate 3 and 0.5 (half a tick carried); pause and `advance_time` off stop the day but not the game tick; the rate clamps; the moon at the 12 ticks of scout N 5 (c); the F3 line.
- `Commands`: the probe's `add 25000` from 2 gives 25002, and `query day` 1004; every reply's wording; the units; 13 refusals ("1x", "-5", "rate 0", "add 2147483648" and others); a tiny rate shown in full.
- `Markers`: noon from 0 is 6000; at noon it stays; day, night, midnight and `minecraft:noon` each go forward.
- `Cap`: 0.15 s runs 3 ticks; a 1 s hitch runs 5 and drops 15; a cap of 1.
- `SavedWorld`:
  - **a genuine pre-DN1 world.json** (`create-world.mjs`'s played "Hello World", written by the `24d76ee` binaries and captured during the run) loads with no clock and writes back byte for byte;
  - the key round-trips at version 1, with defaults left out and 2^53 − 1 written as its digits;
  - a tiny rate's text reads back the same float;
  - other clocks are ignored; a newer version and 11 broken clocks are refused;
  - save, rename and touch keep the clock.

**Mutation batch** (`C:\dev\sandbox\water-dn1\mutate.ps1`: one build, six mutations, each aimed at one test). All six were caught, and the sources were restored, touched and rebuilt:
- M1: an unset clock writes an empty key. Caught by SavedWorld's byte-for-byte check.
- M2: the moon switches at noon. Caught by "Moon at 18000".
- M3: a marker already reached moves a day. Caught by "At noon, noon stays".
- M4: no partial tick carried. Caught by "20 ticks at 30 to 240 fps" and "a third two".
- M5: `query day` answers the total. Caught.
- M6: no per-frame cap. Caught by "drops 15".

**Black-box save check** (`C:\dev\sandbox\water-dn1\savecheck.ps1`, only in its own `-WorldsDir`):
- (A) the pre-DN1 world opened headless, ran `voxel.Time set 13219`, `rate 3` and `pause`, then quit. world.json gained `{"minecraft:overworld":{"total_ticks":13219,"paused":true,"rate":3.0}}` at version 1.
- (B) Reopened, the world logged `World clock: Time: day 0 at 13219 (19:13) | ... | rate 3.0x | paused`, and the file was byte-identical afterwards.
- (C) `-VoxelTime=noon` showed 6000 and left the file unchanged.
- So `EndPlay` saves on `quit`.

## Pre-commit review

One Sonnet 5.5 reviewer, diff only (1,199 lines; peak 101k), found no correctness bug. Three of its points were taken:
- a test that `total_ticks` is written as its digits;
- refusing a stored rate whose float is 0 (`1e-50`);
- showing tiny rates in full in the replies (`DescribeRate`).

Two nits were left, as known issues: markers within a day of 2^53 clamp, and load hitches count as dropped ticks.

## Gate (after the last code change)

`C:\dev\sandbox\water-dn1\gate-g2.txt` and `e2e-e2.txt`; evidence in `Tests/Evidence/2026-10-10/WaterSky-DN1-g2-*` and the `Worlds\Create-1791660581543` and `Playground-all\Runtime-20261010-153600` folders (on disk only). Other lanes' editors ran alongside (machine state is in each summary), so nothing here is timed.

- **Verifiers:** both report REFERENCE OK, and the references are unchanged.
- **`TestVoxel.ps1`:** 127 of 127 (121 plus the 6 Clock tests). That includes the goldens (`Mesher.Digest`, `Aquifer`, `Density`, `Decoration`, `Biome.Golden`).
- **The six radius-8 hashes are identical to main's trees-on values** in both load orders, with 0 columns differing:
  - plain: `0xe3e4e339022a4ec0`, `0x92d83639acdee88f`, `0x615321e7b313fed6`;
  - decorated: `0x52c29bae6e786bfc`, `0x1b665f4703a79406`, `0xcc437793fbd65281`.
- **Every native suite:** 193 passed (187 plus 6), 0 failed. One test passed with the editor's LogHttp `generate_204` timeout warning.
- **`npm test` and source-check:** `npm test` passed 57, and source-check passes.
- **`create-world.mjs --editor`:** 32 PASS, 0 FAIL, run under the CEF slot lock.
- **`TestFoundationRuntime.ps1 -Editor -FrameRates 60`:** 0 failures, 44.8 fps mean (W1: 44.9).
- **`CaptureVoxel.ps1 -Flight`:** exit 0.
  - Chunk work: worst 3.40 ms, with 29 of 9,229 frames over the 2 ms budget.
  - Memory was flat over laps 2 and 3 (4,847 to 4,912 MB).
  - Holes in 253 frames, at most 21 columns, all on the circle laps.
  - This is the known trees-on regression S9a recorded for S9b (96 and 49 frames in its two trees-on runs). Another editor was running when the flight started.
  - DN1 adds at most five `Step` calls a frame (a few nanoseconds) and changes nothing in streaming. W2a certifies the flight in a quiet window.

## Known gaps

| Gap | When |
|---|---|
| The first frames after a map load have a large delta, so `voxel.Info`'s dropped ticks include load hitches. Harmless: the pawn drops them the same way. | If a gameplay metric needs it, count from `IsReady` |
| The exact-match marker (at noon, `set noon`) stays; that case isn't probed. | A later probe, if it matters |
| `query time` and `query gametime` follow the probe's wording. `/time of <clock>` and the `minecraft:moon` timeline aren't offered. | When another clock or timeline exists |
| Markers within a day of 2^53 clamp, so they don't land on the marker. Only reachable by hand-editing world.json. | Never in play |
| `create-world.mjs` and `foundation-menu.mjs` attach to CEF on the fixed port 18946, which every windowed game opens. The manager's CEF slot rule (`C:\dev\sandbox\locks\cef-e2e.lock`) now sequences them. | A per-run port would remove the rule |

## Files

- New: `Source/UnnamedVoxel/Clock/VoxelWorldClock.{h,cpp}`, `Source/Unnamed/Tests/VoxelClockTests.cpp`.
- Changed:
  - `Source/Unnamed/Voxel/VoxelWorld.{h,cpp}` and `VoxelConsole.cpp`;
  - `Source/Unnamed/Worlds/UnnamedSavedWorlds.{h,cpp}`;
  - `Source/Unnamed/Foundation/FoundationLab.h` and `FoundationHud.cpp` (two lines each);
  - `Scripts/TestVoxel.ps1` (121 to 127, for the 6 new tests);
  - `Docs/VoxelWorld.md` (Saved worlds, the game rules row, the commands list, the tests table).
- No golden, hash, reference or generation code changed.
