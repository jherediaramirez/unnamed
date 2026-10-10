# Java 26.3 sky reference

The input for the day/night cycle (the water and sky plan, [Docs/Records/2026-10-10-WaterSky-Plan.md](../../Docs/Records/2026-10-10-WaterSky-Plan.md), DN0; Jesus's decision Q3 (a)). It holds the overworld's timelines and their tags, the four dimension types, the two world clocks, the environment attribute list, the sun, the eight moon phases and their atlas, and two black-box facts files from a headless 26.3 server. The research behind it is [scout N's record](../../Docs/Records/2026-10-10-WaterSky-Scout-N-DayNight.md). This folder is generated, hash-listed and never edited by hand.

| Source | Hash |
|---|---|
| Official server jar `C:\dev\sandbox\mc-26.3-blocks\downloads\server.jar` (the block and item references' server) | SHA-1 `33680f5f2ac32864d6d7cf5e56a705fdb3e05f4c` |
| Its bundled `META-INF/versions/26.3/server-26.3.jar` (the worldgen reference's source) | SHA-256 `a362163eec5d1612d520772bc16e5b39c09e3b234fdc045f56bf544284ee8ae6` |
| Client `%APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar` (the block and item references' client) | SHA-1 `e877b6a07acd633fb3bb475002175cec036e7b87` |
| The data generator's `--reports` from that server jar: `registries.json`, in `C:\dev\sandbox\mc-26.3-blocks\reports\reports` | copied, see below |
| Scout N's sweep (`facts/sweep.json`) and DN0's colour probe (`facts/colour_scan.json`) | SHA-256 in `manifest.json` |

```powershell
uv run --no-project --python 3.13 python -I Reference/minecraft-sky-26.3/tools/verify_reference.py
```

The verifier checks every file against `SHA256SUMS.txt` and re-derives every generated file from this folder's own copies, through the same `derive()` the builder uses. It compares every data copy with the server jar and every copy with the client jar (which holds the same data files) when the jars are present, and the registry copy with the data generator's output when that folder is present. It checks the facts files against their pinned SHA-256, the counts and spot values (scout N, section 3), and then the timelines against the facts: the angles, star brightness, sky light factor and level and the open-sky light at all 65 ticks, read as "Reading a timeline" says (the two daylight detectors only as consistency checks: the inverted one is 15 minus the light, the normal one never exceeds it), the moon phase at the 16 moon rows, and the colour probe's controls and plateau edges against the keyframes that bound each plateau. When a jar or the report folder is absent, its comparisons are skipped and the result says PARTIAL.

## Files

| Path | What it is |
|---|---|
| `data/minecraft/timeline/` | The four timelines, byte for byte: `day.json` (period 24,000; 18 tracks, where scout N counted 16; the time markers), `moon.json` (period 192,000; the moon phase and the slime chance), `early_game.json` and `villager_schedule.json`. |
| `data/minecraft/tags/timeline/` | The four timeline tags: `in_overworld` (`#universal`, `day`, `moon`, `early_game`), `universal` (`villager_schedule`), `in_nether` and `in_end`. |
| `data/minecraft/dimension_type/` | `overworld`, `overworld_caves`, `the_nether` and `the_end`: base attribute values (the overworld's `sky_color #78a7ff`, `fog_color #c0d8ff`, `cloud_color #ccffffff`, `cloud_height 192.33`, `ambient_light_color #0a0a0a`), `default_clock` and `timelines`. |
| `data/minecraft/world_clock/` | `overworld.json` and `the_end.json`: empty objects (a clock has no fields). |
| `assets/minecraft/textures/environment/celestial/` | `sun.png` and the eight `moon/<phase>.png`, 32 x 32 each, byte for byte from the client jar. |
| `assets/minecraft/atlases/celestials.json` | The atlas over `environment/celestial`, byte for byte. |
| `reports/registries-environment_attribute.json` | The `minecraft:environment_attribute` registry from `registries.json` (51 ids with protocol ids), re-serialized. Scout N counted 53; the report is the source. |
| `attributes.json` | Generated: the 51 attributes in protocol order, each with its `wiki_default` (null where the sky does not read it), its `overworld` base value when the overworld dimension type sets one, and `overworld_timelines`: every overworld timeline track that sets it, with the period, `modifier` (default `override`), `ease` (default `linear`) and keyframe count. Also `overworld_timelines`, the resolved `#in_overworld` tag in file order. |
| `celestial.json` | Generated: the sun and the moon phases in `moon.json`'s order (index 0 full moon, as `FVoxelWorldClock::GetMoonPhase`), each with its texture path, size and PNG SHA-256, and the atlas path. |
| `facts/sweep.json` | Scout N's black-box sweep, byte for byte: the angles, star brightness, sky light factor and level, the open-sky light and both daylight detectors at 65 ticks; the moon phase at 16 ticks; the colour hypotheses scout N tried; the biome edge. Method: scout N's record, section 5. |
| `facts/colour_scan.json` | DN0's colour probe, byte for byte: 5 controls at noon, 10 plateau edges found by bisection and the white sky near tick 0 (see "Colours"). |
| `manifest.json` | Versions, jar hashes, the facts' SHA-256 and sources, counts and the file lists. |
| `tools/` | `build_reference.py` and `verify_reference.py`. |

## Reading a timeline

The data names the parts; minecraft.wiki ("Timeline", "Environment attribute") gives their meaning, and the facts confirm it black-box:

- **Keyframes** are sorted by tick, keeping file order for equal ticks. The day's angle tracks have two keyframes at tick 6000 (360 then 0, or 540 then 180 for the moon): that pair is the wrap.
- **Sampling at tick t:** t is taken modulo the period. A timeline without `period_ticks` (`early_game`) doesn't repeat (wiki): before its first keyframe it holds the first value, after its last the last value. The previous keyframe is the last one at or before t; if none, the last keyframe one period earlier. The next is the first one after t; if none, the first keyframe one period later. The progress between them is eased and the value lerped.
- **Eases:** `linear` (the default), `constant` (the previous keyframe's value) and `{"cubic_bezier": [x1, y1, x2, y2]}`: a CSS-style curve from (0, 0) to (1, 1); solve x(s) = progress for s and take y(s). Values that cannot be interpolated (booleans, the moon phase names) step like `constant`.
- **Modifiers** apply the track's value to the attribute's value beneath it (the dimension type's, else the wiki default): `override` (the default) replaces it, `multiply` multiplies (colours channel by channel), `maximum` takes the larger, `or` and `and` combine booleans. The layer order is the dimension, the biome, the timelines, then weather (wiki).
- **Single precision:** the attributes are floats. `sky_light_level` is 15 x the track; 15 x 0.26666668 is exactly 4.0 in single precision, and the facts measure an open-sky light of 4 there, the ceiling of the level.

## Where each value comes from

- **Timelines, tags, dimension types, clocks:** the server jar's data pack, as copied. The client jar holds the same bytes.
- **Attribute ids:** the data generator's registries report.
- **Attribute defaults (`wiki_default`):** minecraft.wiki "Environment attribute", as scout N's notes record them. The facts check the three the sweep reads: `sky_light_level` 15, `sky_light_factor` 1 and `star_brightness` 0.
- **Textures and atlas:** the client jar, as copied. Sizes are read from each PNG's header.
- **The facts:** a headless 26.3 server answering public commands only (`execute ... if predicate`). Scout N's sweep is described in its record; the colour probe in the DN0 record.

## Colours

The end values are exact: plains reads `#78a7ff` at tick 6000 and `#000000` at 13670 and 18000 (scout N, section 5 (d)). DN0's colour probe (`facts/colour_scan.json`; method in the file and in the DN0 record) added a data pack with a white-sky and a grey-sky copy of plains, held the clock and bisected each colour track's plateau edges:

- **The edges are the data's keyframes.** A plateau still matches at the keyframe that ends it and stops one tick later; a plateau that starts at a keyframe matches from that tick. That holds for the sky (white and plains), the fog, the sky light colour and the sunrise glow's off plateau (10 edges), so colour tracks are read at the clock's tick, as the floats are, and one tick off a plateau already reads differently.
- **The first day wraps too:** the white sky is not white at totals 0, 1 and 132, and is from 133, as at 24,000 and 24,133.
- **Still unknown: the in-between values.** At a held tick 0 the white-sky biome matched none of the 256 greys, and no lerp of the keyframes (in sRGB values, linear light or OKLab, rounded or truncated) fits every edge. How the server interpolates and stores an in-between colour is left for a later probe (DN2); until then the sampler lerps the file values channel by channel, and tests pin only the end values and the edges.

## Rebuild

`build_reference.py` needs the pinned jars, the report folder (`--reports`, default above) and the two facts files (`--sweep`, `--colour`; their SHA-256 are pinned in the builder). It refuses to run unless this folder holds only `tools/` and `README.md`, so to rebuild, move the accepted folder away first (keeping `tools/` and this README). After a README or tool edit, `build_reference.py --sums-only` rewrites only `SHA256SUMS.txt`; it refuses if any other listed file changed.

Minecraft art and data remain Mojang/Microsoft material. Local development builds may use them; anything distributed must replace them.
