# Water and sky DN0: the sky reference and the timeline sampler

October 10, 2026, lane `feature/water-sky` (worktree `C:\dev\projects\Unnamed-water`). DN0 builds the data the sky reads (the plan, [2026-10-10-WaterSky-Plan.md](2026-10-10-WaterSky-Plan.md), section 5) and a sampler for it. Nothing is wired into the game yet: the reader is called only by tests, so goldens, world hashes and saved worlds are unchanged.

**First, the merge:** `feature/voxel-world` at `0762f96` (S9b's big trees) was merged once, without rebase or force (`a15d63f`, parents `2f67e61` and `0762f96`). It had two conflicts, both resolved by keeping both sides: TestVoxel's `$expected` became main's 112 plus W1's 10 plus DN1's 6 (128), and `Docs/VoxelWorld.md` took S9b's Trees row followed by DN1's Clock row. `VoxelBlockTable.h` merged on its own (S9b's look states beside W1's fluid fields). It was built with `-DisableAdaptiveUnity`, and everything was green before any DN0 change (`C:\dev\sandbox\water-dn0\gate-m0.txt`):
- the block, worldgen and items verifiers;
- TestVoxel 128 of 128;
- the six radius-8 hashes identical to main's;
- Mesher.Digest at main's values;
- native `Unnamed`: 205 passed, plus 2 with the editor's HTTP-timeout warning;
- npm 57 of 57, and source-check.

The manager fast-forwarded `feature/voxel-world` to `a15d63f`, which brought W1 and DN1 into the game branch.

## What DN0 adds

- **`Reference/minecraft-sky-26.3/`** (Jesus's decision, Q3 (a)), hash-checked and built only by its own `tools/build_reference.py`:
  - **From the official server jar** (SHA-1 `33680f5f…`; its bundled `server-26.3.jar`, SHA-256 `a362163e…`, is read): the four timelines, the four timeline tags, the four dimension types and the two world clocks.
  - **From the client jar** (`e877b6a0…`): the sun, the eight moon phases and `atlases/celestials.json`.
  - **From the data generator's `registries.json`:** the environment attribute registry, with 51 ids (scout N counted 53).
  - **The facts:** scout N's `sweep.json`, and DN0's colour probe `colour_scan.json` (below), byte for byte and pinned by SHA-256 in the builder.
  - **Generated files:**
    - `attributes.json`: each attribute with its wiki default, its overworld base value and the overworld timeline tracks that set it;
    - `celestial.json`: the moon phase order mapped to its textures;
    - `manifest.json`.
  - **`tools/verify_reference.py`** re-derives the generated files from the folder alone, compares every copy with both jars and the report, and checks counts and spot values. It compares the data copies with the server jar, every copy with the client jar, and the registry copy with the report. It re-samples the timelines at all 65 sweep ticks and the 16 moon rows in single precision (the two detectors only as consistency checks), and checks that each colour edge sits at a keyframe bounding a plateau. Its README states the reading of a timeline.
- **`Source/UnnamedVoxel/Sky/VoxelTimeline.{h,cpp}`** (core, plain C++):
  - **The sampler:** `FVoxelTimeline` and `FVoxelTimelineTrack`. Keys are sorted with file order kept for equal ticks; sampling wraps around the period, and a timeline without a period (`early_game`) holds its end values. Eases: linear, constant, cubic Bezier. Values: floats, colours, booleans and names. The lerp is in float.
  - **`VoxelSky` helpers:**
    - `ApplyModifier`: override, multiply, maximum, or, and;
    - `CubicBezier`;
    - `ParseColor` and `ColorToHex`;
    - `OpenSkyLight`, the ceiling of the level;
    - `SampleAngles`: the sun, moon and stars, plus directions (-sin a, 0, cos a) in the voxel axes;
    - `SampleMoonPhase`, in `FVoxelWorldClock`'s order.
- **`Source/Unnamed/Voxel/VoxelSkyReference.{h,cpp}`:** the strict reader. It refuses an unknown field, ease, modifier or value type, a track mixing types, and a fractional or out-of-period tick, with the file and field named. It also has `ReadOverworldBase`, which returns the overworld's value, else the wiki default, from `attributes.json`.
- **A record:** scout N's facts, committed byte-identical as [2026-10-10-WaterSky-Scout-N-DayNight.md](2026-10-10-WaterSky-Scout-N-DayNight.md), SHA-256 `1FD8601A102F6782C1F78C66DE0D1B83BE1E533827E276E351C26D3BFB3C237E`.
- **`CLAUDE.md`:** the two lines Jesus approved (Q3 (a)), worded as shown to the manager. One adds the sky reference to the rules list's hash-checked references, with its regeneration sentence; the other adds its verifier to Commands.

## What the data and the probes showed

- **The data differs from scout N's text in three places, and the data wins:**
  - `day.json` has 18 tracks, not 16;
  - the registry lists 51 environment attributes, not 53;
  - section 5 (a)'s table gives 256.42 at tick 22500 as a model value, but the sweep measured 258.291, and the model agrees with the sweep.
- **Single precision matters:** 15 × 0.26666668 is exactly 4.0 as floats but 4.0000002 as doubles. The open-sky light is the ceiling, so a double product would give 5 at night where the server measures 4. The sampler and the verifier multiply in float.
- **The colour probe** (the manager approved scout N's 5 (d) probe). It used a data pack with a white-sky and a grey-sky copy of plains, on scout N's headless harness and its own port, with commands only. The facts are from one consolidated run on a fresh world, with the clock held: 136 checks in 35 s.
  - All five noon controls hold.
  - All 10 plateau edges sit exactly at the data's keyframes. This covers the sky (white and plains), the fog, the sky light colour and the glow's off plateau. A plateau still matches at the key that ends it, and from the key that starts it.
  - The first day wraps like any other: total 0 is not white; 133 is.
  - **The in-between values are still unknown.** At a held tick 0, the white-sky biome matched none of the 256 greys. No lerp of the keyframes (in sRGB values, in linear light or in OKLab, rounded or truncated) fits every edge. Left for DN2 to probe. Until then, the sampler lerps the file values channel by channel, and the tests pin only the end values.
  - **A pitfall on the way:** three exploration runs reused a world whose server had been stopped without saving. The `advance_time false` rule was lost, so the clock ran during the reads. Every edge then looked one tick early, and tick 0 read white. Only a fresh world's run went into the facts.
  - **Another:** the codec takes `#rrggbb`, a packed int or a list. An `any_of` predicate with 250 colour terms per command, then halving, reads a colour within a 117,000-colour cube in about 470 commands.

## Tests (`Unnamed.Voxel.Sky.Timeline`, 7; TestVoxel expects 135)

`Source/Unnamed/Tests/VoxelSkyTimelineTests.cpp`. Scout N's section 5 tables are written out in the test, and the reference's `facts/sweep.json` is read as well.
- **`Parse`:**
  - the four timelines: periods, clock, 18 day tracks, 6 markers;
  - the sun's Bezier and the 360-then-0 pair;
  - the stars' maximum, the sky's multiplied colour, the booleans' or, and the moon's 8 names;
  - `early_game` false until 120,000 and true after;
  - equal ticks keep file order;
  - 10 broken inputs are refused, each with a reason.
- **`Angles`:**
  - the 13 ticks of section 5 (a) and all 65 sweep ticks, within 0.01°: the sun, the moon (the sun plus 180°) and the stars;
  - the wrap at 6000, and a later day;
  - the sun's direction at tick 0, (0.977, 0, 0.214), and the moon opposite it.
- **`Levels`:** `sky_light_level`, `sky_light_factor` and `star_brightness` within 1e-5, on the bases 15, 1 and 0 read from the reference, at section 5 (b)'s 16 rows and all 65 sweep ticks. Midnight's level is exactly 4.
- **`OpenSkyLight`:** the light is the ceiling at the 16 rows and the 65 ticks. The inverted detector is 15 minus the light; the normal detector never exceeds it.
- **`MoonPhase`:** section 5 (c)'s 12 ticks and the 16 sweep rows; DN1's `GetMoonPhase` agrees at each.
- **`Colors`:** plains `#78a7ff` at 6000, and `#000000` at 13670 and 18000; an ARGB round trip.
- **`Markers`:** DN1's `FindMarker` equals `day.json`'s four shown markers; `wake_up_from_sleep` is 0 and not shown.

**Mutation batch** (`C:\dev\sandbox\water-dn0\mutate.ps1`: each mutation built and run on its own, then restored and touched): all eight were caught, each by the test it aims at, with the named assertion in the log (`mutations.txt`, 16:25 to 16:30; again on the final code after the review fixes, `mutations-final.txt`, 16:44 to 16:49):
- M1, linear ease instead of the Bezier: `sun at 3000` (315.0 against 325.4);
- M2, the earlier key of the 6000 pair: `sun at 9000`;
- M3, no 180° moon offset (the moon read from the sun's track): `moon at 3000`;
- M4a, round instead of the ceiling: `5 (b): light at 13500` (5, not 6);
- M4b, floor instead of the ceiling: the same assertion;
- M5, no wrap (a repeating timeline holding its end values): `5 (b): sky_light_level at 0`;
- M6, maximum read as multiply: `5 (b): star_brightness at 0`;
- M7, the moon phase switching at midnight: `5 (c): phase at 18000`.

**The reference's own mutations** (`C:\dev\sandbox\water-dn0\mutate_reference.py`, each on a fresh copy; the unmutated copy passes):
- a Bezier control point in `day.json`, re-summed: the re-derivation, both jar comparisons and the sweep check all fail it;
- one SHA-256 line corrupted;
- a sweep value, re-summed: the pinned hash and the sweep check;
- a wiki default in `attributes.json`, re-summed: the re-derivation and the sweep check;
- a stray file;
- the registry copy, re-summed: the re-derivation and the report comparison.

All six fail it.

## Pre-commit review

Two Sonnet 5.5 reviewers read the staged diff, split by path at 1,762 lines: (A) the C++ and TestVoxel, 1,053 lines, peak 84k; (B) the reference tools, README, `CLAUDE.md` and this record, 709 lines, peak 75k. Neither found a bug.

**Fixed from A:**
- The reader's numeric, string and bool fields now must hold their own JSON type, since the engine's `TryGet*Field` converts `"10"` to 10.
- A keyframe or marker tick must be a whole number within int32.
- A timeline without a period takes markers at any tick.
- Track, marker and moon-phase names compare case-sensitively; names are no longer merged by `AddUnique`.
- The helpers carry a `Sky` prefix, the `using namespace` is gone, and the includes are explicit (unity builds).
- A non-finite tick reads as 0.
- The sweep helper asserts 65 and 16 rows, and the key indexing is guarded.
- The refusal test names each expected reason, with 18 cases (8 new: string and huge ticks, a Bezier x of 1.5, a non-object track, a number clock, a marker past the period, a string `show_in_commands`, a number modifier). A period-less marker parses.

**Fixed from B:**
- Each colour edge's keyframe must bound a plateau on the right side.
- The verifier's base is the overworld's value, else the wiki default (as its README says).
- An unknown ease or modifier fails a check instead of passing as linear or raising.
- `--sums-only` refuses when any listed file outside `README.md` and `tools/` changed (checked: an edited `attributes.json` was refused).
- A missing jar or report folder makes the result say PARTIAL.
- Bad or repeated `SHA256SUMS.txt` lines fail.
- The builder refuses a track or overworld attribute missing from the registry.
- The README's verifier paragraph now says the detectors are consistency checks only.

**Not changed:**
- `CLAUDE.md`'s sky sentence doesn't name the colour probe file. It is the approved text; the README names both facts files.
- The `.gitattributes` worry: `* -text` already keeps every byte.
- A modifier is not checked against its value type, since only the reference feeds the reader.

The sampler mutations were run again on the final code (`mutations-final.txt`).

## Gate (after the last code change)

`C:\dev\sandbox\water-dn0\gate-g1.txt`, 16:50 to 16:56. It ran after the final mutation batch (16:44 to 16:49: all eight caught again, `mutations-final.txt`) and a clean rebuild. The machine at the start: only this lane's editor.
- **Verifiers:** blocks, worldgen and items green. The sky verifier is green: 33 files hashed, 3 generated files re-derived, 38 copies compared with the jars, the report, the sweep at 65 ticks and 16 moon rows, and 10 colour edges.
- **TestVoxel:** PASS, 135 of 135 (125 s).
- **Radius-8 hashes:** identical to main's, 6 of 6: plain `e3e4e339…`, `92d83639…`, `615321e7…`; decorated `194a623a…`, `1b665f47…`, `bde96a3d…`. `Mesher.Digest` passes at its goldens.
- **Native `Unnamed`:** 214 passed (207 before DN0, plus 7), 0 warnings, 0 failed (137.5 s).
- **npm:** 57 of 57. **source-check:** PASS.
- **No end-to-end runs:** DN0 changes nothing at runtime. The new code is reached only from tests, and no map, asset, menu or game path calls it.

## Known gaps

| Gap | When |
|---|---|
| Colour in-betweens (how the server interpolates and stores a colour between keyframes) are unknown; the sampler lerps file values. | DN2's probe, before the sky's colours are tuned |
| The sampler isn't wired in: no sky, sun or light moves yet. | DN2 (renderer) and DN3 (light) |
| Weather is not modelled (the layer after the timelines). | Out of scope (plan) |
| `voxel.Time query moon` is not offered (the DN1 handoff's known issue). | When a command needs it; `SampleMoonPhase` now gives the phase from the data |
| The daylight detector is pinned only as a table; its formula depends on the sun and is not ported. | When detectors exist |

## Files

- `Reference/minecraft-sky-26.3/` (new: 33 hashed files plus `SHA256SUMS.txt`)
- `Source/UnnamedVoxel/Sky/VoxelTimeline.h`, `.cpp` (new)
- `Source/Unnamed/Voxel/VoxelSkyReference.h`, `.cpp` (new)
- `Source/Unnamed/Tests/VoxelSkyTimelineTests.cpp` (new)
- `Scripts/TestVoxel.ps1` (135)
- `CLAUDE.md` (the two approved lines)
- `Docs/Records/2026-10-10-WaterSky-Scout-N-DayNight.md` (new, byte-identical), this record, the handoff and the index

Evidence: `C:\dev\sandbox\water-dn0\` (`gate-*.txt`, `mutations.txt`, `mut-trial\mutations.txt`, `colour\colour_final.py` and `colour_scan.json`, the exploration logs `run*.log` and `explore*.log`) and `Tests/Evidence/2026-10-10/WaterSky-DN0-*`.
