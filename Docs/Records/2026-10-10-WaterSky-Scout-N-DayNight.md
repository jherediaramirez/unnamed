# Scout N: the day/night cycle (the "water and sky" track, day/night half)

Scout N, 2026-10-10. Read-only research against `feature/voxel-world` at **41190e9** (worktree `C:\dev\projects\Unnamed-scout`). Vanilla facts come from the 26.3 jars' data files, black-box runs of the 26.3 server's public commands (no game client, no code reading), `javap -public` signatures, and minecraft.wiki. Probes, data copies and raw outputs are in `C:\dev\sandbox\scout-daynight\probes\`, and the wiki notes are in `notes\wiki-daynight.md`.

## 1. Summary and recommendations

1. **26.3's day cycle is data, not code, and we have it exactly.** `data/minecraft/timeline/day.json` in the server jar holds one 24,000-tick track per quantity: the sun, moon and star angles, star brightness, the sky, fog, cloud and sunrise colours, the sky light colour and factor, and the internal sky light level. `moon.json` (192,000 ticks) holds the 8 moon phases. A model of ours that reads those keyframes reproduces the running server's values: linear tracks within 1e-5, eased angles within 0.004° ([§5](#5-test-oracles)). The track's worker can port the data and test it against these numbers. No game code is needed.
2. **The server answers every question black-box.** 26.3's inline predicates (`float_value_check` over an `environment_attribute`, `environment_attribute_check`, `location_check` light) read any attribute at any position and tick. So the oracles in §5 come from the game itself, and `probes\sweep_probe.py` can re-measure them.
3. **What is code-made, and therefore unknown to us:** the sky dome's geometry, the sunrise fan's shape, the 780-star field (the wiki gives seed 10842), the sun's and moon's drawn size, and the lightmap formula that turns sky light, block light and the brightness slider into a pixel. The renderer pieces (sky, stars, lightmap) are therefore look calls tuned against vanilla screenshots, not ports. The 32 x 32 sun texture and the eight 32 x 32 moon phases are in the client jar. No reference has them yet ([§3](#3-data-inventory)).
4. **Our rig at 41190e9 is a fixed noon-ish sun built for one look.** The sun is at elevation 38° from the south-west, 75,000 lux. The sky light is ×3 and captures in real time. Exposure adapts between EV100 6 and 12.5. The edge fog's colour is a constant fitted to that sun. A moving sun touches all four. The fog colour and the exposure policy are cheap to drive per frame. The cost risk is the sun's **virtual shadow maps**: our chunks are not Nanite, and a rotating directional light invalidates the whole cache. Recommendation: before any look work, measure it in a probe session (continuous against stepped against ray-traced sun shadows), the same pattern as T0 + T1.
5. **Night must stay dark under eye adaptation.** In vanilla, sky-lit surfaces fall to a factor of 0.24 with a blue tint (`#7a7aff`), block light is unchanged, and caves look the same at noon and midnight. Our histogram exposure would brighten a uniformly dim night back towards day. Recommendation: art-directed night lighting (moon and sky light) that meters about 2 stops **below** the EV floor, so night reads dark blue, torches carry it, and caves are unchanged. That needs T3's torches, so the night-readability session comes after T3.
6. **Time, rules and saving fit what exists.** `advance_time` is already on the Create screen and saved in world.json's `rules` (default on); `FUnnamedSavedWorld::GetRule` reads it. Vanilla's clock state is total ticks, partial tick, rate and paused, and it saves `total_ticks`, `paused` and `rate`. Recommendation: one new optional world.json key, `clocks`, with those saved values, saved when the player leaves the world. Keep version 1 and rename nothing. Console `voxel.Time ...` mirrors 26.3's `/time` (set and add, markers, query, pause, resume, rate). `-VoxelTime=` pins shots.
7. **Proposed split** ([§9](#9-look-options-the-mock-up-and-the-session-split)): DN0 data and core (S); DN1 clock, commands and save (S-M); DN-P moving-sun cost probe (S); the browser mock-up and a look call; DN2 sky and sun renderer (L); DN3 night readability with T3's torches (M). DN0, DN1 and DN-P can run beside the water half and T3. DN2 waits for the look call. DN3 waits for T3.

## 2. Reading list (at 41190e9, in order)

1. This file, §1, §4 and §6.
2. `Docs/VoxelWorld.md` 877-956 (Lighting: cards, sky light ×3, cave exposure, torch probe numbers, edge fog), then 1071-1073 (gaps: lighting, edge fog, pale water) and 37 (axes: X east, Y south, Z up).
3. `Scripts/Editor/CreateGeneratedMap.py` 29-61 (rig constants and exposure maths), 90-111 (the actors it spawns), 151-175 (what `verify()` pins).
4. `Source/Unnamed/Voxel/VoxelWorld.cpp` 54-59 (edge haze constants), 181-207 (finding the fog and sky; the fog floor), 410-449 (`Tick`), 451-496 (`UpdateEdgeFog`, `SetEdgeFogVisible`), 218-254 (`GetSavedWorld`, `ConfigurePlayer`, the rule read). `Source/Unnamed/Voxel/VoxelWorld.h` 37-40, 107-109, 138-148.
5. `Docs/VoxelWorld.md` 766-789 (Saved worlds) and 790-825 (settings and cvars), then `Source/Unnamed/Worlds/UnnamedSavedWorlds.h` 10-55 and `UnnamedSavedWorlds.cpp` 118-141 (`ReadWorldOptions`), 161-226 (read/write settings), 236-239 (`GetRule`).
6. `Source/Unnamed/Voxel/VoxelConsole.h` 11-32 and `VoxelConsole.cpp` 29-43 (how cvars are declared).
7. `Docs/MenuPreferencePersistencePlan-2026-10-07.md` 311-317 (`advance_time`, Boolean, On) and 51 (Brightness `gamma` 0.5, 0..1, effect pending); `Docs/ControlRegistry.md` 270 and 575.
8. `Scripts/CaptureVoxel.ps1` 2-56 (shot switches, `-ExecCmds`), and `VoxelWorld.cpp` 65-73 and 1290-1296 (shot settle frames and the shot camera's exposure speed).
9. Torch records, summaries only: `Docs/Records/2026-10-10-Torch-Light-Probe.md` 1-15 and 125-151; `2026-10-10-Torch-Handoff-T3.md` 5-16 and 40-59; `2026-10-10-Torch-Scout-G-Research.md` 136-141 (T5) and 195-231 (vanilla light facts).
10. `Config/DefaultEngine.ini` 10-32 and 109 (VSM, Lumen, ray tracing, exposure defaults).
11. Sandbox: `probes\jar-data\timeline\day.json` and `moon.json` (the data itself), `probes\model.py` (our reading of it).

## 3. Data inventory

**Server jar** `C:\dev\sandbox\mc-26.3-blocks\datagen\versions\26.3\server-26.3.jar` (SHA-256 `a362163e...`, the worldgen reference's source). Copies are in `probes\jar-data\`:

| Path (under `data/minecraft/`) | Count | What it holds |
|---|---|---|
| `timeline/day.json` | 1 file, 16 tracks, 6 time markers | period 24,000; markers day 1000, noon 6000, night 13000, midnight 18000 (all `show_in_commands`), `wake_up_from_sleep` 0, `roll_village_siege` 18000; visual tracks: `sun_angle`, `moon_angle`, `star_angle` (cubic Bezier 0.362, 0.241, 0.638, 0.759), `star_brightness` (12 keys, maximum), `sky_color`, `fog_color`, `cloud_color` (4 keys each, multiply), `sky_light_color`, `sky_light_factor` (4 keys, multiply), `sunrise_sunset_color` (32 keys, ARGB); gameplay: `sky_light_level` (multiply), `monsters_burn`, `bees_stay_in_hive`, `eyeblossom_open`, `creaking_active`, `cat_waking_up_gift_chance`, `turtle_egg_hatch_chance`; audio `firefly_bush_sounds` |
| `timeline/moon.json` | 1, 2 tracks | period 192,000; `moon_phase` (8 keys, one per 24,000) and `surface_slime_spawn_chance` |
| `timeline/early_game.json`, `villager_schedule.json` | 2 | pillager patrols after 120,000 ticks; villager schedule (not ours) |
| `tags/timeline/in_overworld.json` | 4 entries | `#universal`, `day`, `moon`, `early_game` |
| `dimension_type/overworld.json` (+ `overworld_caves`, `the_nether`, `the_end`) | 4 | overworld base attributes: `sky_color #78a7ff`, `fog_color #c0d8ff`, `cloud_color #ccffffff`, `cloud_height 192.33`, `ambient_light_color #0a0a0a`; `default_clock overworld`, `timelines #in_overworld`, `has_skylight`, monster spawn light 0..7 with block light limit 0 |
| `world_clock/overworld.json`, `the_end.json` | 2 | empty objects (a clock has no fields) |

The registries report (`C:\dev\sandbox\mc-26.3-blocks\reports\reports\registries.json`) lists all 53 environment attributes. It also lists two visual attributes that no file sets in the overworld, so they keep their defaults: `block_light_tint` (wiki default `#FFD88C`, a warm tint relevant to T3) and `night_vision_color` (`#999999`). It lists `fog_start_distance`, `fog_end_distance`, `sky_fog_end_distance` and `cloud_fog_end_distance` too.

**Client jar** `%APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar` (SHA-1 `e877b6a0...`, the blocks and HUD references' client):

| Asset | Size | In a reference? |
|---|---|---|
| `textures/environment/celestial/sun.png` | 32 x 32, indexed | no |
| `textures/environment/celestial/moon/{full_moon, waning_gibbous, third_quarter, waning_crescent, new_moon, waxing_crescent, first_quarter, waxing_gibbous}.png` | 8 files, 32 x 32 each (26.x split the old 4 x 2 sheet) | no |
| `atlases/celestials.json` | the atlas over `environment/celestial` | no |
| `textures/environment/clouds.png` | 256 x 256, 1-bit (optional for this track) | no |
| `textures/environment/end_sky.png`, `celestial/end_flash.png` | 128 x 128, 64 x 64 (the End; out of scope) | no |
| `textures/environment/rain.png`, `snow.png` | weather (out of scope) | no |
| `shaders/core/sky.*`, `stars.*`, `lightmap.fsh`, `include/fog.glsl`, `light.glsl`, `sample_lightmap.glsl` | shader **code** | not read and not to be ported |

**What the references already have:** `Reference/minecraft-worldgen-26.3/data/minecraft/worldgen/biome/*.json` hold the 56 overworld biomes with `minecraft:visual/sky_color` (56), `visual/fog_color` (4: dappled_forest `#ccd8e2`, mangrove_swamp `#c0d8ff`, pale_garden `#817770`, sulfur_caves `#8cb831`), `visual/water_fog_color` (9), `visual/water_fog_end_distance` (2) and `effects.water_color` (56). The reference README says `attributes` are "not read" yet. The lang key `gamerule.doDaylightCycle` = "Advance time of day" is in `Content/Menu/Web/assets/minecraft/lang/en_us.json:4038`.

**Not in any reference** (name only; don't create one here): the four timelines and their tag, the four dimension types, the two world clocks, the attribute list, the celestial textures and the clouds texture. A later reference folder (for example `minecraft-sky-26.3`) would hold them plus `probes\sweep.json` as facts.

## 4. Vanilla behaviour, in our words

**Clock and time.**
- The overworld has one world clock (`minecraft:overworld`); timelines read it. A day is 24,000 ticks, 20 minutes at 20 ticks per second. The clock's total keeps counting past 24,000 (`/time add 25000` from 2 gave "Set minecraft:overworld to 25002 tick(s)"). The day timeline is that total modulo 24,000 (`/time query day` → "Timeline minecraft:day is at 1004 tick(s)"). Probe: `probes\cmds-discover.out.txt`.
- **A new world starts at tick 0**: right after "Done" the probe read game time 1 and the day timeline at 1. Tick 0 is just after sunrise, with the sun 12.4° above the eastern horizon. Wiki: Daylight_cycle, "New worlds".
- **26.3's `/time`** (probe `help time`): `set (<time>|<timemarker>)`, `add <time>`, `pause`, `resume`, `rate <rate>`, `query (time|<timeline>|gametime)`, `of <clock> ...`. `query daytime` is gone ("Can't find element 'minecraft:daytime'"). `set day/noon/night/midnight` answers "Set minecraft:overworld to time marker minecraft:<name>"; the wiki says a marker moves the clock forward to the next match. `rate 2` answers "will now advance at 2.0x normal rate". Wiki Commands/time: rate runs from above 0 to 1000; times take suffixes d (24,000), s (20) and t.
- **`advance_time`** (renamed from doDaylightCycle) defaults to true (probe: "Game rule advance_time is currently set to true"). False stops the clock, so the moon phases stop too. The probe's clock stayed at 6000 and 12000 across server restarts.
- **Saved state:** a clock's state is total ticks (long), partial tick (float), rate (float) and paused (bool), from the `javap -public` signature of `net.minecraft.world.clock.ClockState(long, float, float, boolean)`. Vanilla saves it in `data/minecraft/world_clocks.dat` as `total_ticks`, `paused` and `rate` per clock, leaving out default values (probe, §5 f).
- **Server-side sampling:** the server evaluates visual attributes too. At noon in plains, `environment_attribute_check` on `visual/sky_color` with `#78a7ff` passes.

**Sun, moon and stars** (data: `day.json`; check: §5):
- **The sun angle** is 0° straight overhead and grows westward through 90° (the western horizon), 180° (straight down) and 270° (the eastern horizon). It runs from 0° at tick 6000 to 360° one period later, eased by the cubic Bezier (0.362, 0.241, 0.638, 0.759). The two keyframes at tick 6000 (360, then 0) are the wrap. The sun is above the horizon from tick 23,218 to 12,782 (our model), 13,564 ticks or 56.5% of the day. The wiki gives the zenith angle crossings as 12,786 and 23,216. It moves 0.20°/s at noon and 0.36°/s at midnight; the average is 0.30°/s.
- **The orbit** is a vertical circle through the zenith, east to west, with no tilt (wiki: Sun). In our axes (X east, Y south, Z up), the direction **towards** the sun at angle a is (-sin a, 0, cos a). At tick 0 that is (0.977, 0, 0.214): east and low.
- **The moon** angle is the sun's plus 180°, so the moon is always opposite the sun. The stars' angle equals the sun's: the star field turns with the sun.
- **Moon phases:** `moon.json` steps through full, waning gibbous, third quarter, waning crescent, new, waxing crescent, first quarter and waxing gibbous, one per 24,000 ticks. A world starts on a full moon. The phase changes exactly at each multiple of 24,000 (probe: 23999 full, 24000 waning gibbous), so day d shows phase d mod 8.
- **Star brightness** is 0 from tick 627 to 11,373. It rises through 0.016 (11,732), 0.143 (12,399) and 0.258 (12,729) to 0.5 at 13,228, holds there to 22,772, then falls through 0.364, 0.225 and 0.101 (23,758) to 0.037 at tick 92 and 0 at 627. The modifier is maximum, so weather can only raise it. Star count and placement are code-made (wiki: 780 stars from 1,500 attempts, seed 10842, the same in every world).

**Colours** (the timeline multiplies the dimension's and then the biome's value; wiki layer order: dimension, biome, timeline, weather):
- **Sky:** the biome's `sky_color` (plains `#78a7ff`, desert `#6eb1ff`, frozen peaks `#859dff`, pale garden `#b9b9b9`; 56 biome values) times a track that is white from 133 to 11,867, black from 13,670 to 22,330, and linear between. Night sky is pure black. The end values are exact (probe). How the server interpolates between them is still open: it matches neither an sRGB nor a linear-light scalar (§5 d). On the server, a block reads exactly its own biome's colour, with no blending (probe, §5 e).
- **Fog:** `#c0d8ff` (the dimension), or the biome's override in 4 biomes, times a track that runs white to `#0f0f16` over the same ticks. Night fog is (11, 13, 22) sRGB on the model.
- **Sunrise and sunset:** an ARGB track. Its alpha is 0 from 730 to 11,270 and from 14,192 to 21,807. The glow peaks at 12,732 and 23,272 with `#feda6333`: alpha 0xfe (0.996) and RGB (218, 99, 51), an orange. Its colour runs from pale yellow (`#ffe533`) through orange to red (`#b33333`). So the dusk glow lasts from 11,270 to 14,192 (2.4 minutes) and dawn from 21,807 to 730. The fan's shape and where it sits are code-made; the wiki says only that it is drawn towards the sun near the horizon.
- **Clouds:** `#ccffffff` (white at 0.8 alpha) times white to `#ff191926`; height 192.33. Optional for this track.

**Light at night:**
- **Gameplay sky light** (`gameplay/sky_light_level`) is 15 times a track: 1.0 from 133 to 11,867, 0.2667 (= 4) from 13,670 to 22,330. It drives spawning, daylight detectors and melting. Wiki, Light: "sky light is not reduced at night"; the internal level is darkened.
- **Visual sky light** (what the lightmap uses): `sky_light_factor` runs 1.0 from 730 to 11,270 and 0.24 from 13,140 to 22,860; `sky_light_color` runs from white to `#7a7aff` (blue) over the same ticks. The overworld's `ambient_light_color` is `#0a0a0a`, the floor at light 0. Block light is not on the timeline: torches are equally bright at noon and midnight. How the lightmap combines these values, and what the brightness slider does, is shader code and unknown. The wiki gives only reference points: at the lowest brightness, day about 98%, night about 17% and total dark about 5% (Light).
- **The daylight detector** reads the darkened sky light: power 15 from tick 4,295 to 7,705, 0 from 13,670 to 22,330, 7 at tick 0 (wiki table; probe in §5). Inverted, it gives 15 minus the internal level (11 at midnight).
- **Weather (out of scope; hook only):** weather is the top layer. The wiki gives the internal sky light as 12 in rain and 10 in thunder, with dimmer fog and sky. The design keeps a "weather layer" slot after the timeline.

**Other day-dependent rules (for later systems):** `monsters_burn` is false from 12,542 to 23,460, the same window as sleeping (overworld `bed_rule`: `can_sleep when_dark`). Waking up sets the clock to the `wake_up_from_sleep` marker (0). Eyeblossoms open and creaking hearts are active from 12,600 to 23,401.

## 5. Test oracles

All values were measured on the 26.3 server (superflat plains, clear weather for the float tracks, `advance_time false`, then `/time set T`). They are in `probes\sweep.json` and `probes\sweep.txt`. Reproduce:

```powershell
cd C:\dev\sandbox\scout-daynight\probes
uv run --no-project --python 3.13 python -I time_probe.py cmds-discover.txt --fresh   # makes .\server; /time forms
uv run --no-project --python 3.13 python -I sweep_probe.py                             # about 45 min: floats, light, detectors, moon, blend
uv run --no-project --python 3.13 python -I model.py                                   # our data model against sweep.json
```

The reading method: `execute positioned 4 -60 4 if predicate {type:"minecraft:float_value_check",value:{type:"minecraft:environment_attribute",attribute:"<id>"},test:{min:A,max:B}}`, halved 24 times. Note that in 26.3 the loot-condition key is `type`, not `condition`.

**(a) Angles in degrees** (the model is `probes\model.py`; the largest model error is 0.0036° at tick 730, so the test tolerance is 0.01°):

| tick | 0 | 133 | 1000 | 3000 | 6000 | 9000 | 12000 | 12500 | 12786 | 13000 | 18000 | 22500 | 23999 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| sun_angle | 282.374 | 284.436 | 297.546 | 325.402 | 0.000 | 34.601 | 77.629 | 85.489 | 90.07 (model) | 93.52 | 180.000 | 256.42 (model) | 282.35 (model) |

The moon angle equals the sun's plus 180° (462.374 at tick 0, so moon angles run past 360). The star angle equals the sun's. **Mutations that must fail:** linear instead of Bezier ease (10.4° off at tick 3000: 315.0 against 325.4), the wrong keyframe chosen at the tick-6000 wrap (a 360° or 180° jump), the moon not offset by 180°.

**(b) Linear tracks** (these match the model to within 1e-5):

| tick | 0 | 92 | 133 | 500 | 11500 | 11867 | 12500 | 12786 | 13000 | 13228 | 13500 | 13670 to 22330 | 22500 | 23460 | 23999 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| sky_light_level (15 × track) | 14.18857 | 14.74986 | 15 | 15 | 15 | 15 | 11.1381 | 9.39323 | 8.08763 | 6.69662 | 5.03716 | 4.0 | 5.03716 | 10.89407 | 14.18247 |
| light at an open-sky block (`location_check`) | 15 | 15 | 15 | 15 | 15 | 15 | 12 | 10 | 9 | 7 | 6 | 4 | 6 | 11 | 15 |
| sky_light_factor | 0.70332 | 0.74071 | 0.75737 | 0.90652 | 0.90652 | 0.75737 | 0.50011 | 0.38387 | 0.2969 | 0.24 | 0.24 | 0.24 | 0.24 | 0.48385 | 0.70291 |
| star_brightness | 0.05463 | 0.037 | 0.03416 | 0.00878 | 0.00566 | 0.03265 | 0.1782 | 0.28564 | 0.38943 | 0.5 | 0.5 | 0.5 | 0.5 | 0.19292 | 0.05482 |
| daylight detector power | 7 | 7 | 7 | 8 | 8 | 7 | 4 | 3 | 2 | 1 | 1 | 0 | 1 | 4 | 7 |
| inverted detector | 0 | 0 | 0 | 0 | 0 | 0 | 3 | 5 | 6 | 8 | 9 | 11 | 9 | 4 | 0 |

- **Rules these pin:**
  - the light level is the **ceiling** of the fractional sky level (5.037 gives 6; 4.0 gives 4);
  - the inverted detector is 15 minus that light;
  - the normal detector reaches 15 only from about tick 4,500 to 7,500 (it also depends on the sun angle, so pin the table, not a formula).
- **Mutations that must fail:** round or floor instead of ceiling (tick 13500 gives 5, not 6); no wrap around the period (tick 0 and 23999 change); maximum read as multiply for stars.

**(c) Moon phase** (`environment_attribute_check` on `visual/moon_phase`): ticks 0, 6000, 18000 and 23999 give `full_moon`. **24000 gives `waning_gibbous`**: the phase changes exactly at each multiple of 24,000, at the day's tick 0. 48000 gives `third_quarter`, 90000 `waning_crescent`, 114000 `new_moon`, 138000 `waxing_crescent`, 162000 `first_quarter`, 186000 `waxing_gibbous`, and 210000 `full_moon` again. **Mutation:** switching at midnight or noon.

**(d) Colours, exact:** at tick 6000, plains `sky_color` is exactly `#78a7ff`; at 13670 and 18000 it is exactly `#000000`. **Still open:** the in-between values. At ticks 12318, 12768 and 13219, neither "biome × m in sRGB" nor "biome × m in linear light", for any m in steps of 1/4096, matched the server. So the colour space or the rounding is not the simple one (`probes\colour_probe.py`, `colour.json`). **Next probe (about 5 minutes):** a data pack biome with `sky_color #ffffff` and one with `#808080`, placed with `/fillbiome`. White makes all three channels equal, so a 256-value scan reads the multiplier itself, and the grey one shows the multiply space. Until then, tests should pin only the end values.

**(e) Biome edges on the server:** with desert filled from x 16, `sky_color` reads exactly plains at x ≤ 14 and exactly desert at x ≥ 16. The server samples the block's own biome with no blending. Client-side blending is unknown (code); a smooth blend in our renderer is a look choice.

**(f) Clock and save:**
- a new world starts at tick 0 (game time 1 at "Done");
- `time add 25000` keeps the total (25002), and `time query day` gives the total mod 24,000;
- vanilla saves `data/minecraft/world_clocks.dat` as `{"minecraft:overworld": {"total_ticks": 13219, "paused": 1b, "rate": 3.0f}, "minecraft:the_end": {"total_ticks": 20}}`, leaving out keys at their default (`probes\dump_clocks.py` on the probe world).

## 6. Draft design

**Data flow.** The data path is: the jar's timeline JSON (a future reference) → a UObject-free sampler (`FVoxelTimeline`) → per-frame values from the world clock's tick and the camera's biome → `AVoxelSky`, which drives the map's sun, moon, sky light, fog and exposure and the sky material's parameters.

- **Clock.** The clock is a deterministic counter of game ticks at 20 Hz. It holds the total ticks (int64), the partial tick, the rate and paused, and advances from the world's delta time when `advance_time` is on and the clock is not paused. It lives with the world, not the pawn: in or beside `AVoxelWorld`, next to the authority. It must use the same 20 Hz step as the authority, the pawn and the water half's fluid ticks, so whichever half lands first should own a single "world ticks" source.
- **Sampler.** Sorted keyframes per track; wrap around the period; linear, constant or cubic-Bezier ease; the modifiers multiply, maximum, override, or and and. Colours are ARGB. It is evaluated once per frame on the game thread for about 15 tracks, a few microseconds.
- **Biome input.** The camera cell's biome (`AVoxelWorld::GetBiome`) gives the base `sky_color` and `fog_color`. The server does not blend biomes (§5 e). The renderer may smooth the base colours over a few seconds as the camera crosses a border; that is a look choice.
- **Renderer outputs**, each a small property write per frame, skipped when unchanged:
  - **sun and moon:** one movable `DirectionalLight` rotated from `sun_angle`. Hand it to the moon below the horizon (intensity crossfade), so only one light casts VSM shadows. Option: a second, unshadowed moon light.
  - **sky:** a sky-sphere material (look option A or B, §9) with parameters for the sky colour, the fog colour at the horizon, the sunrise colour and alpha, the sun and moon direction, the moon phase index, star brightness and star rotation.
  - **sky light:** the intensity follows a curve on `sky_light_factor`, and the tint `sky_light_color`. Real-time capture is already on.
  - **edge fog:** `SetFogInscatteringColor` from the vanilla fog colour, converted to luminance at the current exposure target. This replaces the constant `EdgeHazeLuminance` (VoxelWorld.cpp:56) and needs a refit at dawn and dusk.
  - **exposure:** the map's PostProcessVolume keeps the histogram method. The day keeps EV100 12.5 as cap. The night is lit to meter about 2 stops below the EV floor (6) and is not adapted further. Optional: a bias curve on time, if the measured night needs it.
- **Gameplay output:** `GetInternalSkyLight()` (15 × the track) for later spawning and melting; daylight detectors when they exist.

**Hooks at 41190e9.**
- **`AVoxelWorld::BuildLayout`** (VoxelWorld.cpp 115-216) finds the fog and sky at 181-190. The day-cycle actor or component would find the `DirectionalLight` and `SkyLight` there too, which `AVoxelWorld` does not do today.
- **`AVoxelWorld::Tick`** (410-449) calls `UpdateEdgeFog()`. The clock advance and the sky update belong next to it.
- **`UpdateEdgeFog`** (451-485) sets the in-scattering once per radius change. The colour part moves into the per-frame sky update. The density part stays radius-driven.
- **`SetEdgeFogVisible`** (487-496): shots from outside keep turning the fog off.
- **`ConfigurePlayer`** (242-254) is the model for reading `advance_time`: `World->GetRule(TEXT("advance_time"), true)`.
- **`CreateGeneratedMap.py`:** the map keeps one sun, sky, sky light, volume and fog, and `verify()` keeps pinning them. The night values are runtime curves, not map edits, so the map needs no rebuild. If a sky-sphere actor is wanted in the map, add it with a `verify()` check; otherwise spawn it at runtime like the fog floor.
- **`UnnamedSavedWorlds`:** add an optional `clocks` object to world.json (read in `FromJson`, written in `ToJson`). Missing means a new world at tick 0. Never rename the existing keys. Save it from `AVoxelWorld::EndPlay` (391-395) through `UnnamedSavedWorlds::Save`.

**Switches and gates.**
- **Switches:** `voxel.Sky.Cycle` (0 = today's fixed rig, the control; 1 = the day cycle). `voxel.Time set|add|query|pause|resume|rate` works like `/time` (markers day, noon, night, midnight). `-VoxelTime=<ticks>` pins shots and flights. `voxel.Sky.OrbitTilt <deg>` (default 0, vanilla). `voxel.Sky.SunShadowStep <deg>` (the VSM mitigation, if DN-P wants it).
- **Gates:**
  - with the cycle off, every existing shot, hash and golden is unchanged: Mesher.Digest, the radius-8 hashes, `CaptureVoxel` means within noise;
  - the timeline oracles (§5) in an automation test;
  - shots at 6 fixed ticks (0, 6000, 12000, 12786, 13670, 18000) against vanilla screenshots for the look call;
  - the cost gates from DN-P.

**Cost risk.**
- **VSM:** the directional light's cache is invalidated whenever the light rotates. With non-Nanite chunks the whole clipmap re-renders every frame the sun moves: an expected +1 to 3 ms, unmeasured. The T1 probe saw VSM cost grow fast per light on this terrain. Mitigations to measure in DN-P:
  - step the shadow direction, for example every 0.25°, about once a second, a spike per step;
  - ray-traced sun shadows (no cache; fixed cost; HW RT is already on);
  - MegaLights, if T3 turns it on and 5.8 handles the directional light there.
- **Lumen:** moving direct light re-lights the surface cache over a few frames (temporal). Expect a small steady cost and a lag after `voxel.Time set` jumps. Shots already settle for 150 frames.
- **Sky light capture:** real-time capture is already on, so no new cost.
- **Sky atmosphere:** its LUTs are per frame anyway.
- **Fog and exposure writes:** negligible.

## 7. Code-shape sketches

Each sketch is **against 41190e9**. Signatures and outlines only.

**(a) Sampler**, new `Source/UnnamedVoxel/Sky/VoxelTimeline.h` (UObject-free; follows the `VoxelWorldgenReader` style of reading reference JSON). Assumes nothing else.
```cpp
enum class EVoxelEase : uint8 { Linear, Constant, CubicBezier };
enum class EVoxelModifier : uint8 { Override, Multiply, Maximum, Or, And };
struct FVoxelKeyframe { int32 Ticks; FVector4f Value; };          // floats in X, colours as linear-in-file RGBA 0..1
struct FVoxelTrack { TArray<FVoxelKeyframe> Keys; EVoxelEase Ease; FVector4f Bezier; EVoxelModifier Modifier; };
struct FVoxelTimeline
{
    int32 PeriodTicks = 0;
    TMap<FName, FVoxelTrack> Tracks;                                 // "minecraft:visual/sun_angle" ...
    TMap<FName, int32> Markers;                                      // day 1000, noon 6000 ...
    static bool Parse(const FString& Json, FVoxelTimeline& Out, FString& OutError);
    FVector4f Sample(FName Track, double DayTicks) const;            // wraps; stable order for equal ticks
};
FVector4f ApplyModifier(EVoxelModifier, FVector4f Base, FVector4f TrackValue);
```

**(b) Clock**, new `Source/UnnamedVoxel/Sky/VoxelWorldClock.h`. Assumes the 20 Hz reference tick (`FoundationBlockFaces::TicksPerSecond`, `Source/Unnamed/Foundation/FoundationBlockRegistry.h:161`).
```cpp
struct FVoxelClockState { int64 TotalTicks = 0; float PartialTick = 0; float Rate = 1; bool bPaused = false; };
struct FVoxelWorldClock
{
    FVoxelClockState State;
    void Advance(double Seconds, bool bAdvanceTimeRule);            // TotalTicks += whole ticks of Seconds * 20 * Rate
    bool SetToMarker(int32 MarkerTicks, int32 PeriodTicks);         // forward to the next match, as /time set <marker>
    double DayTicks(int32 Period) const { return double(State.TotalTicks % Period) + State.PartialTick; }
};
```

**(c) Driver**, new `Source/Unnamed/Voxel/VoxelSky.h/.cpp` (a component owned by `AVoxelWorld`, or an actor it spawns). Assumes `VoxelWorld.cpp` 181-190 (finding the map actors), `Tick` 410-449, `UpdateEdgeFog` 451-485, `GetBiome` (VoxelWorld.h:81) and `GetSavedWorld` (VoxelWorld.h:67).
```cpp
UCLASS() class UVoxelSkyComponent : public UActorComponent
{
    void Bind(UDirectionalLightComponent* Sun, USkyLightComponent* SkyLight, UExponentialHeightFogComponent* Fog,
              APostProcessVolume* Exposure, USkyAtmosphereComponent* Atmosphere);
    void TickSky(double DeltaSeconds, int32 CameraBiome);
    // per frame: clock.Advance -> sample tracks -> rotate the sun (or its stepped shadow direction) -> sky MID params
    //            -> sky light intensity and colour -> fog inscattering -> exposure bounds; each write skipped if unchanged
};
```

**(d) Saved world** (`Source/Unnamed/Worlds/UnnamedSavedWorlds.h` 10-55, `.cpp` 346-380): `TOptional<FVoxelClockState> Clock;` in `FUnnamedSavedWorld`. `ToJson` writes `"clocks": {"minecraft:overworld": {"total_ticks": N, "rate": r, "paused": b}}` only when set, with defaults left out as vanilla does, and `FromJson` checks it like the other fields. These key names are a proposal (see vanilla's own names, `total_ticks`, `paused` and `rate`, in §5 f).

## 8. Re-orientation list (diff these first)

| Who | What moves | Files at 41190e9 to diff before building |
|---|---|---|
| **T3** (torch light, `feature/voxel-light`) | a pooled MegaLights point light per torch, a warm torch look, a falloff option, `r.MegaLights.EnableForProject 1` | `Config/DefaultEngine.ini`; `Source/Unnamed/Voxel/VoxelWorld.{h,cpp}` (Tick, members); `VoxelLightShots.{h,cpp}`; `Scripts/Editor/CreateGeneratedMap.py` (if T3 touches exposure; the brief's "EV 7 floor" does not match 41190e9's EV100 6, so check whether T3 changed it); `Scripts/Editor/CreateBlockMaterials.py` (emission); `Docs/VoxelWorld.md` Lighting |
| **T5** (block-light field, later) | 4-bit block light in the core | `Source/UnnamedVoxel` storage and the mesher. The day cycle's internal sky light must combine with it: max(sky − darkening, block). T5 should also plan a sky-light field (or a heightmap test) for spawning and melting. |
| **S9a to S12** (tree core, decoration, goldens) | decoration, goldens, `Mesher.Digest` | `Tests/` goldens and `Docs/VoxelWorld.md` Decoration and Tests. The day cycle must leave the hashes untouched (no mesher change), so only re-run them |
| **Breaking prototype** | `FoundationInteractionView` (outline, cracks) | `Source/Unnamed/Foundation/FoundationInteractionView.cpp`. Check that the outline and cracks read at night: vanilla's outline is dark, and the material may need emission |
| **Water half** (scout M) | flowing water, the fluid tick, the water material | `Source/Unnamed/Voxel/VoxelWorld.{h,cpp}` (Tick), the `M_ChunkWater` material and `CreateBlockMaterials.py`, the mesher. Shared: one world-tick source; water's look under a moving sky (reflections; the "pale water" gap is the sky's reflection) |
| **Menu** | the video settings' Brightness (`gamma`), saved but not wired | `Docs/VideoSettings.md`, `MenuPreferencePersistencePlan` 51. DN3 is its consumer |

## 9. Look options, the mock-up and the session split

### Look options (deliverable 4)

Each option is a family. The knobs below combine with any of them.

| | A. Vanilla sky | B. Hybrid (recommended to mock first) | C. Unreal physical |
|---|---|---|---|
| Sky dome | unlit sky-sphere material: the timeline's sky colour overhead blending to the fog colour at the horizon, black below, the sunrise fan towards the sun | `SkyAtmosphere` as today for the gradient and sunsets, tinted towards the biome's sky colour; the vanilla fan as an additive layer | `SkyAtmosphere` alone (physical Rayleigh and Mie), optional volumetric clouds |
| Sun and moon | the 32 x 32 pixel textures as square quads; the 8 moon phases | the vanilla quads drawn over the atmosphere (its own sun disk off) | the atmosphere's round sun disk; a round moon texture |
| Stars | 780 square points from a fixed seed, brightness from the track | the same | a star texture or none |
| Night | art-directed: dark blue, about 2 stops below the EV floor | the same as A | physical moonlight, adapted by exposure (night looks like a dim day) |
| Day look against today | changes: flatter sky, harder horizon | closest to today's day shots | today's look by day |
| Cost | lowest (one material) | low | low (no clouds) to medium (clouds) |

**Knobs:**
- **orbit tilt:** 0° is vanilla, east to west through the zenith, so north and south faces never get direct sun and noon shadows fall straight down. 20 to 35° is shader-pack style; today's map sun sits 35° off that plane.
- **night darkness:** vanilla-dark, or adapted.
- **sun and moon shape:** square, or round.
- **fan strength.**
- **star density.**
- **moon shadows:** none, or the shared directional light.

### The browser mock-up (sketched, not built)

The handbook rule applies: with this many options, Jesus should pick by feel in a browser before any Unreal work. The mock-up is one HTML page, three.js, offline.

- **Scene:** a small cube terrain (a 64 x 64 heightmap of grass, sand and stone blocks, a lake, a 3 x 3 cave mouth with two torch point lights), seen from a fixed eye at y 70.
- **Time:** a slider for the tick (0 to 24,000), play at 1x, 60x or 600x, and buttons for the markers (day, noon, night, midnight).
- **Option tabs:** A, B and C, each rendering the sky and lighting as above (B and C use a simple analytic atmosphere in the shader).
- **Knobs:** the ones above, plus the biome (plains, desert, snowy, pale garden), switching the base sky and fog colours.
- **Curves strip:** under the view, the timeline tracks read directly from `day.json` (sky, fog, sunrise colour and alpha, star brightness, sky light factor, internal sky light level), with a cursor at the current tick.
- **Vanilla panel:** a slot showing Jesus's own vanilla 26.3 screenshots at noon, sunset (12,786), night (13,670) and midnight (18,000), from the same spot (a creative superflat or a known seed). The page cannot make them, and the scout cannot open a game client.
- **Output:** the chosen settings as JSON (option, tilt, night stops, shapes) for DN2 to read.

### Proposed session split (day/night half)

| Session | Area | Size | Tests and evidence | Waits for |
|---|---|---|---|---|
| **DN0** data and core | the sky reference (timelines, dimension types, clocks, celestial textures, `sweep.json` as facts) with build and verify scripts; `FVoxelTimeline`, `FVoxelWorldClock` in the core | S | `Unnamed.Voxel.Sky.Timeline`: every §5 oracle; reference verify | nothing; can run now, beside the water half and T3 |
| **DN1** clock, commands, saving | the clock in `AVoxelWorld`, the `advance_time` rule, `voxel.Time ...`, `-VoxelTime=`, the world.json `clocks` key, an F3 line (day, tick, phase) | S-M | world.json round-trip tests (old files load, missing key = tick 0, newer refused); markers move forward; pause, rate; hashes unchanged | DN0; Jesus's answer on saving (Q4) |
| **DN-P** moving-sun cost probe | `voxel.Sky.SunSweep` rotates today's sun along the vanilla orbit at rate N; continuous VSM, stepped VSM (0.1°, 0.25°, 1°), ray-traced sun shadows (and MegaLights if T3 has merged) | S | GPU ms over a flight and a static view, the step spike, Lumen settle frames after a jump; 1600 x 900 in a quiet window | nothing (today's rig); best before DN2 |
| **Mock-up and look call** | the browser page above | S | Jesus picks an option and knobs | DN0's data (the page reads the JSON) |
| **DN2** sky and sun renderer | `UVoxelSkyComponent`: the sun and moon light, the sky material (A or B), stars, the sunrise fan, the sky light curve, the edge fog colour per frame | L | 6 timed shots against the vanilla screenshots; the cycle-off control equals today's shots; the cost within DN-P's gate | the look call, DN-P, DN1 |
| **DN3** night readability | the exposure policy at night with T3's torches; the brightness slider (`gamma`) wired to the floor or night stops; the outline and cracks at night | M | night shots: outdoors, a torch-lit field, a cave at noon and at midnight (caves must match) | T3 merged; DN2 |
| later | weather (a layer after the timeline), mobs' `monsters_burn`, spawning with T5, beds and sleeping, daylight detectors | - | - | their systems |

**Ordering:**
- **Against the water half:** DN0, DN1 and DN-P touch no mesher or fluid code, so they run in parallel with water's sessions. One shared world-tick source should be agreed first: whichever half lands first owns it. DN2 and water's material session both change how water looks under the sky; run water's material first or co-review it, because "pale water" is partly the sky's reflection.
- **Against the torch track:** DN3 comes after T3. T5 is independent of DN0 to DN2. When it comes, it combines its block light with DN1's internal sky light (no rendering change).

## 10. Risks and open questions

### Risks

1. **VSM with a moving sun.** The cost is unmeasured and could exceed the frame budget on non-Nanite chunks. DN-P measures it first. The fallbacks are stepped shadows or ray-traced sun shadows.
2. **Every outdoor shot changes.** Vanilla's sun is never where today's sun is (38° elevation from the south-west), so turning the cycle on changes every outdoor evidence image. Keep `voxel.Sky.Cycle 0` as the default until DN2's look call, and pin shots with `-VoxelTime`.
3. **Eye adaptation fights the night.** If the night is lit too brightly, histogram exposure brightens it back towards day. If it is too dark, torches clip (T1 already saw 8,000 cd blow out at the cave exposure). DN3 tunes the two together.
4. **Unknowns that only screenshots settle:** the lightmap curve, the sun and moon size, the fan's shape and the star field. Without vanilla screenshots from Jesus, the look is tuned by memory.
5. **Edge fog seam.** The sun-side seam (a known gap) may show more at dawn and dusk, when the fog and sky colours change fastest.
6. **Lumen lag.** After a `voxel.Time set` jump, Lumen and the sky light settle over frames. Shots need their 150 settle frames, and the test should check that the settle is enough.
7. **Two ticks.** If the water half and this track each add their own 20 Hz counter, they drift. Agree one world-tick source.

### Open questions for Jesus

1. **Look family.** Options: A vanilla sky, B hybrid, C Unreal physical (§9). *Recommendation:* build the browser mock-up first (S), then pick. B is likeliest to keep today's day shots while giving vanilla's sun, moon, stars and night.
2. **Sun orbit.** Options: vanilla (east to west through the zenith; north and south faces never sunlit), a tilt as a setting (default 0), or a tilted default (20 to 35°). *Recommendation:* vanilla orbit with `voxel.Sky.OrbitTilt` available. Decide the default in the mock-up.
3. **How dark night is.** Options: vanilla-dark (about 2 stops below the EV floor; torches matter) or adapted (Unreal physical; night looks like dim day). *Recommendation:* vanilla-dark, tuned with T3's torches in DN3.
4. **Saving the time.** Options:
   - (a) now, as an optional world.json `clocks` key saved when leaving the world;
   - (b) session-only until phase 10 saves edits;
   - (c) now, with a format bump to version 2 so older builds refuse the file.

   *Recommendation:* (a), keeping version 1 (an older dev build would just drop the key).
5. **Shots and the Playground.** Options: shots pin to a tick (which?) or stay on today's fixed sun; the Playground gets the cycle or keeps its rig. *Recommendation:* the Playground keeps its fixed rig. Shots use the fixed sun until DN2, then `-VoxelTime=6000` (noon) by default, with dawn, dusk and midnight shots added.
6. **Brightness slider.** Options: wire `video.gamma` in DN3 (Moody to Bright moves the night and cave floor by about ±1 EV), or leave it pending. *Recommendation:* wire it in DN3; vanilla's slider mostly lifts the dark end, and the floor is our dark end.
7. **Moon shadows.** Options: none (an unshadowed moon fill) or the shared directional light at night. *Recommendation:* the shared light (one VSM; vanilla has no shadows at all, so either is a look call).
