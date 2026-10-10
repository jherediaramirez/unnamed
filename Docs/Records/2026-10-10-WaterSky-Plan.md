# Water and sky plan: flowing fluids and the day/night cycle (Unnamed, 26.3)

October 10, 2026: the water and sky track's plan session, run as a parallel lane (`feature/water-sky`, worktree `C:\dev\projects\Unnamed-water`, from `feature/voxel-world` at `41190e9`). Jesus asked today for flowing water and the day/night cycle to be their own project, with their own research and workflow. Two scouts did the research. This record turns it into the track's plan: goals, sessions, the order across the project, the merged questions with who decides each, and the short prompts for the first two sessions. It is a plan only; nothing was built.

**Sources:**
- **Scout M (fluids):** `C:\dev\sandbox\scout-water\facts.md`, its probes and `probes\oracles.json` (620 KB; read it through `analyze.py` and `side.py`, never whole).
- **Scout N (day/night):** `C:\dev\sandbox\scout-daynight\facts.md`, `probes\sweep.json`, `probes\model.py` and the jar data copies in `probes\jar-data\`.
- The sandbox is disposable. W1 and DN0 each commit their scout's facts byte-identical as a record, with its SHA-256, as scouts H, I and J were.

**Rules for sources:**
- **Never a source:** `Reference/minecraft-blocks-26.3/research/water_research.md` and `Docs/MinecraftBlockReference.md` section 4 were written from bytecode method bodies (scout M). Workers don't read or port them. Cite scout M's black-box measurements (its [P] marks) or the wiki instead. Where the two disagree, the probe (the jar) wins (Q8).
- **Code-made sky parts are look calls:** the sky dome, the sunrise fan, the star field, the sun and moon size, and the lightmap curve are tuned against vanilla screenshots, not ported. The client jar's shader code is not read.
- **Exposure is T3's area:** at `41190e9` the auto-exposure minimum is EV100 6, but T3's captures froze EV 7 (scout N). This track doesn't change exposure. DN2 and DN3 build on whatever T3 merges.

## 1. Goals and what "done" looks like

**Goals:**
- **Water and lava flow as in vanilla 26.3 after any edit:** timing, reach, the drop search, infinite sources, draining, the lava and water reactions, waterlogged sources and washed-away plants, all pinned to scout M's black-box oracles.
- **Generated worlds stay byte-identical.** Generated fluid edges wake near the player, as vanilla's cave waterfalls do (Q4).
- **Fluids look right and the player feels them:** side strips, slopes and flow textures; swimming, the current's push, and lava's slowdown and burns.
- **A 24,000-tick day from 26.3's own timeline data:** the sun, the moon with its 8 phases, and the stars move; the sky, fog and sky light follow. The night is dark and torches matter. Time is controlled like `/time` and saved with the world (Q2).
- **One shared 20 Hz world tick** drives both halves (Q1).

**Done when:**
- every scout oracle runs as an automation test, and each listed mutation fails it (W1, DN0, DN1);
- every golden and world hash is unchanged by the track: `Mesher.Digest`, `Aquifer`, `Density`, `Decoration` and `Biome.Golden`, and the six radius-8 hashes. Changes that would move them (Q5 (a), Q12) are flipped on the main lane;
- the flight's 2 ms chunk budget holds, a 1,000-cell flood stays inside the fluid budget, and the moving sun costs no more than DN-P's gate;
- the evidence exists:
  - the breach capture: dig into a sea from a cave, and the cave floods;
  - waterfall, spread and lava-fall shots;
  - six timed sky shots beside vanilla screenshots;
  - night shots with torches, where caves look the same at noon and midnight;
- with `voxel.Fluids 0` and `voxel.Sky.Cycle 0` the game equals `41190e9` (the controls);
- Jesus's sky pick is built, and one in-game pack confirms it before the default flips (handbook).

## 2. The shared world tick (Q1)

Both scouts ask for one 20 Hz source. Two counters drifting apart is scout N's risk 7.

- **One world-owned clock:** `FVoxelWorldClock` in the core. It is pure and UObject-free, and steps at 20 Hz (`FoundationProfile.TickSeconds`, 0.05 s). Each frame it turns the world's delta time into whole game ticks plus a partial tick. A cap per frame drops the excess after a hitch, and `voxel.Info` counts the dropped ticks.
- **Two counters on it, as in 26.3:**
  - the **game tick** (`uint64`) advances whenever the world runs. Fluids use it (later also random ticks and mobs);
  - the **day clock** holds `total_ticks`, `rate` and `paused`. It advances by `rate` per game tick, only while the `advance_time` rule is on and the clock isn't paused.
  - `advance_time false` stops the sky, but water keeps flowing, as in vanilla.
- **Owner:** DN1 builds it, both the core class and its place in `AVoxelWorld` (advanced at the top of `Tick`, before `UpdateEdgeFog`). W2a subscribes the fluid scheduler to the game tick. The sky reads the day clock and the partial tick each frame.
- **Not the pawn:** the pawn keeps its own 20 Hz step (`FoundationPawn.cpp` 236-320) with the same `TickSeconds`. Fluids flow under `voxel.Fly` and while the pawn is frozen. W4 reads the flow per cell from the pawn's step.
- **Saving:** the day clock as Q2 decides. The game tick goes with phase 10, because the fluids' pending acts need it.

## 3. Sessions

Sizes: S is about half a session, M one session, L may need a handoff.

| # | Area | Size | Waits for | Pins (tests) | Gate | Lane |
|---|---|---|---|---|---|---|
| W1 | **Fluid core**: state fields, rules, scheduler, test world | M | nothing | `Unnamed.Voxel.Fluid.*`: every scout M oracle; its 13 mutations fail | Goldens and hashes identical; every native suite | Lane, **now** |
| DN1 | **World clock**: the shared tick, `voxel.Time`, `-VoxelTime=`, F3 line, the `clocks` key | S-M | nothing (the key only waits for Q2) | `Unnamed.Voxel.Clock`: scout N §5 (f), markers, pause, rate, `advance_time`; saved-world round trips | Hashes identical; `create-world.mjs`; every native suite | Lane, **now** (diff T3's `VoxelWorld.cpp` first) |
| DN0 | **Sky data and sampler**: the data in a reference, `FVoxelTimeline` | S | Q3 | `Unnamed.Voxel.Sky.Timeline`: scout N §5 (a) to (d); 7 mutations fail | The reference's verifier and its mutations; the other verifiers; every native suite | Lane, once Q3 is answered |
| DN-P | **Moving-sun cost probe** | S | nothing (today's rig) | GPU ms: static, continuous VSM, stepped VSM, ray-traced; MegaLights if T3 merged | Proposes DN2's cost gate | Lane, **now**; its timed runs take a quiet window |
| MU | **Sky mock-up and look call** (browser) | S | nothing | Jesus picks | The pick, as JSON | **Sandbox, now** (no build slot) |
| SP1 | **Vanilla sky screenshots** (client probe) | S | Jesus's go (Q15) | Fixed spot, fixed ticks | Shots accepted for the mock-up and DN2 | Sandbox, with the go |
| W2a | **Fluid wiring and streaming** | M-L | W1, DN1, T3 merged | Pipeline tests across column borders, unload and reload, budget, frame-rate independence | Breach capture; flood trace; the flight (quiet window); goldens and hashes identical | Lane |
| W2b | **Waterlogging, washed away, fluid tags** | M | W2a; the breaking lane's authority changes merged | Slab and plant oracles in game; breaking a waterlogged block | Every native suite; the interaction scenarios | Lane |
| W3 | **Fluid meshing** | M | W2a; S9's digest re-records merged; T3's mesher light | `Mesher.Shapes` cases; `Mesher.Digest` identical | Shots; mesh cost per section | Lane |
| DN2 | **Sky and sun renderer** | L | MU's pick, DN-P, DN1, DN0; W3 (or a co-review) | The component's outputs at 6 ticks; cycle 0 equals today | DN-P's cost gate; in-game pack confirmed by Jesus | Lane |
| W4 | **Player in fluids** | M | W2b | Playground probe suites; vitals; current push against 1.39 m/s | Every native suite; probe suites | Lane; the suites take turns |
| W5 | **Generated edges** (wake scan) | S | W2a; S10 and S11 merged | Edge count per preset against vanilla's | Goldens identical; scan cost off the 5 ms column | Lane |
| W5s | **Springs on** | S | W5, S11 | Decoration goldens **re-recorded** | Main lane's gate | **Main lane only** (Q12) |
| DN3 | **Night readability** | M | T3 merged; DN2 | Night shots; caves equal at noon and midnight | Look call | Lane |
| Phase 10 | Saving fluid edits, pending acts and the game tick (and the `clocks` key if Q2 is (b)) | - | Phase 10's saves | - | - | Main lane |

**Changes from the scouts' splits:**
- The clock moved wholly into DN1, core included, so the shared tick no longer waits for the sky data's reference question (Q3). DN1 names the four markers as constants; DN0's data confirms them in a test.
- W2 is split into W2a (wiring, streaming, the reactions that W1's rules already produce) and W2b (waterlogging, washed-away pops and the tag data, which touch the authority's break path).
- Springs-on (W5s) is its own main-lane item, because it re-records goldens.
- SP1 is added: the mock-up and DN2 need real vanilla screenshots, and the breaking lane's VP1 harness can take them.

## 4. Order across the project

### The track's lane

The lane runs one session at a time. Take the first one in this list whose waits are met:

1. **W1** (now)
2. **DN1** (now)
3. **DN0** (once Q3 is answered; until then skip to the next)
4. **DN-P** (now; its timed runs take a quiet window)
5. **W2a** (after T3 merges)
6. **W2b**
7. **W3**
8. **DN2** (after Jesus's pick)
9. **W4**
10. **W5** (after S10 and S11 merge)
11. **DN3** (after T3 and DN2)

Beside the lane, outside the build slot: **MU** now, and **SP1** once Jesus gives the go.

**Why water first:** W1 starts the longest chain (W2a to W5 hang on it) and needs only a build slot. The sky's long pole is Jesus's pick, so the mock-up starts at once beside it.

**Can run now beside the other work:** W1, DN1, DN-P (timed runs in a quiet window), MU, and DN0 once Q3 is answered. None touches the mesher's output, a golden or a hash.

### Against the work in flight

| Work | What it shares with this track | Rule |
|---|---|---|
| **Main lane S9b** (trees) | `FVoxelStateInfo` fields in `VoxelBlockTable.h`; the decorator's `waterlogged` handling; a `Mesher.Digest` re-record | W1 rebases if S9b adds state fields first. W3 waits for S9's digest re-record. |
| **S10** (carvers) | Generate's fill and the aquifer; new carved fluid and air edges | W5 waits for it and counts the edges it leaves. |
| **S11** (underground features) | Lakes write sources under `cave_air`; springs are in the data and off (phase 6 Q3, answered (a)) | W5 waits for it. Springs-on re-records the Decoration goldens, so it is W5s on the main lane (Q12). |
| **S12** (ocean and cold) | `VoxelFreezePass.cpp` and `GetFluidBits` (`VoxelBlockPredicates.cpp`), which read `FluidAmount == 8` as "source" | W1 changes those lines minimally, through `IsFluidSource()`. Whoever merges second rebases. Goldens are verified identical, never re-recorded. |
| **T3** (torch light) | `VoxelWorld.{h,cpp}` (`Tick`, members); `VoxelBlocks.cpp` (light per state); the mesher's vertex G; `DefaultEngine.ini` (MegaLights); exposure | DN1 diffs and rebases on T3's `VoxelWorld.cpp`. W2a and W3 wait for T3 to merge. DN-P adds MegaLights rows if T3 has merged. DN3 waits for T3. |
| **T5** (block light, later) | A 4-bit block light in the core | It combines with DN1's internal sky light as max(sky − darkening, block). Fluid writes must update light (each flowing lava level gives light 15); T5's plan takes this. |
| **Breaking lane** | `FoundationInteractionView` (the Playground's water draw); the authority's break path (`FoundationAuthority.cpp` 720-755) | W2b waits for its authority changes to merge. DN3 checks the outline and cracks at night. |
| **Items lane** | Buckets (S9+) place sources through the authority | After W2a, a placed source flows through the `Set` hook with no item change. A bucket waterlogging a slab is a bucket rule (vanilla), for the bucket session; W2b provides the waterlogged state's rules. |
| **Phase 10** (saving) | Edits on disk | Fluid edits, pending acts with their remaining delay, and the game tick are saved in phase 10. The `clocks` key now or then (Q2). |
| **Menu** | Brightness (`gamma`) is saved but not wired | DN3 wires it (Q17). |

### Lane rules

- **Lanes never re-record goldens or world hashes.** Every lane gate re-runs them and expects identical values. A change that would move them (Q5 (a), Q12) is built behind a switch whose default keeps them, and the flip happens on the main lane.
- **One editor build at a time** (`-WaitMutex`). Native suites and editor runs take turns. Timed gates (the flight, DN-P, the flood trace) take the manager's quiet window. Before each editor or timed run, check `Get-Process UnrealEditor*, UnrealBuildTool, java*, python*`.
- **Commits** stay on `feature/water-sky`; the manager merges. Merges keep every records-index entry. No `CLAUDE.md` edits except those Jesus approves (Q3 (a)).
- **References** are rebuilt only through their own tools, after moving the accepted folder away (`CLAUDE.md`).
- **Evidence** goes in fresh folders under `Tests/Evidence/<date>/` (for example `WaterSky-W2a-Breach`).
- **Each session ends with a record**, `Docs/Records/<date>-WaterSky-<session>.md`, plus a handoff for the next session and an index line.

## 5. Session details

### W1: fluid core (M)

- **Scope:**
  - **State table:** the fluid kind (none, water, lava) and the level (0-15) in `FVoxelStateInfo`, filled by `VoxelBlocks::MakeTable` from `FFoundationFluid`, plus `IsFluidSource()`.
  - **The two readers** that take `FluidAmount == 8` as a source: the freeze pass and `GetFluidBits`. Falling water also has amount 8.
  - **`Source/UnnamedVoxel/Fluids/`:**
    - a pure `Act(cell, reader)` for scout M's sections 4.3 to 4.5;
    - the due-tick FIFO scheduler with one pending act per cell and fluid;
    - a `uint64` tick;
    - an in-memory test world.
  - **No game wiring.**
  - **Records:** commit scout M's facts byte-identical as a record (with its SHA-256), plus a compact oracle data file made by a script from `oracles.json`.
- **Files:**
  - `Source/UnnamedVoxel/Blocks/VoxelBlockTable.h` (130-170);
  - `Source/Unnamed/Voxel/VoxelBlocks.cpp` (95-195);
  - `Source/UnnamedVoxel/Surface/VoxelFreezePass.cpp` (55-80, 118-140);
  - `Source/UnnamedVoxel/Decoration/VoxelBlockPredicates.cpp` (95-110, 225-232);
  - new: `Source/UnnamedVoxel/Fluids/*` and `Source/Unnamed/Tests/VoxelFluidTests.cpp` with the oracle data.
  - The test world takes block properties (solid, slab shape, washed away) directly. The tag data comes in W2b.
- **Tests (`Unnamed.Voxel.Fluid.*`), from scout M section 5:**
  - **Water:** `w_plane`, `w_channel`, `w_hole1`-`w_hole6`, `w_hole_diag`, `w_hole_two`, `w_hole_tie`, `w_infinite`, `w_infinite_air`, `w_infinite_slab`, `w_above_beside2`, `w_cliff`, `w_falling_lands`, `w_merge`, `w_pool_wall`, `w_drain`, `w_slab`, `w_slab_wall` and `w_waterlogged_bottom`.
  - **Lava:** `l_plane0`-`3`, `l_channel`, `l_hole2`-`4`, `l_drain0`-`3` and `l_onto_lower0`-`3`.
  - **Lava meets water:** `lw_obsidian`, `lw_cobble`, `lw_stone`, `lw_lava_over_water`, `lw_water_onto_flowing_lava`, `lw_basalt`, `lw_place_beside` and `lw_fizz`.
  - **Compared:** the settled cells with their tick of last change, plus the full series for `w_plane`, `w_slab`, `w_drain`, `l_plane0` and `l_drain0`. Transient mismatches are reported, not pinned (Q9 (a)).
  - **Determinism:** the same edits at the same ticks give the same world, run twice.
  - **A micro-bench** of acts per ms (scout M estimates 1 to 4 µs per act).
- **Mutations that must fail** (each shown failing once, then reverted):
  - a spread delay of 4 or 6;
  - water drop-off 2;
  - drop search 4 or 6;
  - lava search 4;
  - the infinite rule without its floor check;
  - "a source above plus one beside" making a source;
  - a bottom slab not counting as a floor;
  - a target as "my amount − 1";
  - falling water spreading sideways;
  - lava drop-off 1;
  - the conversion also checking below;
  - flowing water waterlogging slabs;
  - a lava source not checked on placement.

  FIFO against LIFO only if Q9 is (b).
- **Gate:** goldens and the radius-8 hashes identical; every native suite green.

### DN1: world clock, time commands and saving (S-M)

- **Scope:**
  - `FVoxelWorldClock` (section 2), core and game side, owned by `AVoxelWorld`;
  - the `advance_time` rule, read as `ConfigurePlayer` reads rules;
  - `voxel.Time set|add|query|pause|resume|rate`, with the markers day 1000, noon 6000, night 13000 and midnight 18000;
  - `-VoxelTime=<ticks>` to pin shots and flights;
  - an F3 line: day, time of day, moon phase and game tick;
  - the world.json `clocks` key once Q2 is answered. Without an answer, DN1 ships without it and a small DN1b adds it.
  - The sky stays today's fixed rig.
- **Files:**
  - `Source/Unnamed/Voxel/VoxelWorld.{h,cpp}`: `Tick` 410-449, `EndPlay` 391-395, `ConfigurePlayer` 242-254;
  - `VoxelConsole.{h,cpp}`;
  - `Source/Unnamed/Worlds/UnnamedSavedWorlds.{h,cpp}` (`FromJson`, `ToJson`);
  - the F3 overlay;
  - `Docs/VoxelWorld.md`: Saved worlds and the cvars.
- **Tests (`Unnamed.Voxel.Clock`, from scout N §5 (f)):**
  - fixed deltas give 20 ticks per second, with the partial tick carried over;
  - rate 3, pause, and `advance_time` off: the day stops but the game tick runs;
  - a new world starts at 0;
  - `add 25000` from 2 gives 25002, and `query day` gives the total mod 24,000;
  - markers move forward;
  - the per-frame cap;
  - **saved worlds:** old files load, a missing `clocks` key means tick 0, the key round-trips, and a newer version is refused.
- **Gate:** hashes identical; `create-world.mjs` end to end; every native suite green.

### DN0: sky data and sampler (S)

- **Scope:**
  - **The data in a reference** (Q3): the four timelines and their tag, the four dimension types, the two world clocks, the environment attribute list, the eight moon phases, the sun and its atlas, and scout N's `sweep.json` as facts.
  - **`FVoxelTimeline`** in the core: sorted keyframes per track, wrapping around the period, linear, constant and cubic-Bezier ease, the modifiers (multiply, maximum, override, or, and), and ARGB colours.
  - **A record:** scout N's facts committed byte-identical.
  - **Optional colour probe:** scout N's 5-minute probe (§5 (d): data-pack biomes `#ffffff` and `#808080`), on the scout's headless-server harness, if the manager confirms that the scouts' go covers it. Otherwise colours pin only their end values.
- **Tests (`Unnamed.Voxel.Sky.Timeline`):**
  - `sun_angle` at the 13 ticks within 0.01°; the moon at the sun's angle plus 180°, the stars at the sun's;
  - `sky_light_level`, `sky_light_factor` and `star_brightness` within 1e-5;
  - the open-sky light as the ceiling of 15 × the track;
  - the detector rows as a table;
  - the moon phase at the 12 listed ticks;
  - `sky_color` `#78a7ff` at 6000 and `#000000` at 13670 and 18000;
  - DN1's marker constants equal `day.json`'s.
- **Mutations that must fail:**
  - linear ease instead of Bezier (315.0° against 325.4° at tick 3000);
  - the wrong keyframe at the 6000 wrap;
  - no 180° moon offset;
  - round or floor instead of ceiling (tick 13500 gives 5);
  - no wrap (ticks 0 and 23999);
  - maximum read as multiply for the stars;
  - the moon phase switching at noon or midnight.
- **Gate:** the reference's verifier green, with its own mutations; the other three verifiers; every native suite; hashes identical.

### DN-P: moving-sun cost probe (S)

- **Scope:** `voxel.Sky.SunSweep <rate>` turns today's directional light along the vanilla orbit (the T0 and T1 probe pattern). The cases:
  - the static sun (the control);
  - continuous VSM;
  - stepped VSM at 0.1°, 0.25° and 1°;
  - ray-traced sun shadows;
  - MegaLights, if T3 has merged.
- **Measurements:**
  - GPU ms p50 and p95 over a flight and a static view at 1600 x 900;
  - the step spike;
  - Lumen's settle frames after a jump (`voxel.Time set` if DN1 has landed).
- **Gate:** it proposes DN2's cost gate, for the manager to accept. Provisional: the chosen method adds at most 1.0 ms GPU p50 over the static sun in the flight (T1's allowance for 10 lights). Timed runs take a quiet window.

### MU: the sky mock-up and the look call (S, sandbox)

- **Where:** `C:\dev\sandbox\sky-mockup\`, one offline three.js page with an `Open-Mockup.cmd` server, the breaking lane's pattern (`C:\dev\sandbox\break-mockup\`).
- **The handbook rule:** the real vanilla assets and a "Today" preset as the control. Jesus picks by feel. Unreal builds only his picks, and one in-game pack confirms them before they become the default.
- **Contents** (scout N §9):
  - the terrain with a lake and a torch-lit cave mouth;
  - the time slider, play at 1x, 60x or 600x, and the marker buttons;
  - tabs A, B and C beside "Today";
  - the knobs: orbit tilt, night darkness, sun and moon shape, fan strength, star density, moon shadows, and the biome;
  - the curves strip from `day.json`;
  - the vanilla panel (SP1's shots, or Jesus's);
  - a shoreline toggle on the lake (Q5's (a) and (b));
  - a JSON export of the pick for DN2.
- **Assets:** the sun, the eight moon phases and the block textures are copied from the 26.3 client jar into the sandbox. The jar's shader code is not read.
- **Check:** a headless browser screenshot of each tab at 4 ticks.

### SP1: vanilla sky screenshots (S, sandbox, with Jesus's go)

- **Harness:** the breaking lane's VP1 (`C:\dev\sandbox\break-vp1`): the 26.3 client against a local server with `white-list=false`, a superflat plains spot and one known-seed spot, clear weather, `advance_time false`, FOV 70, 1920 x 1080.
- **Shots:** F2 screenshots (they work with the display asleep), with F1 hiding the HUD, at ticks 0, 6000, 12000, 12786, 13670, 18000 and 23000. Also a torch-lit field and a cave mouth at noon and at midnight.
- **For:** the mock-up's panel, DN2's six timed shots and DN3's night shots.

### W2a: fluid wiring and streaming (M-L)

- **Scope:**
  - `FVoxelFluidSystem`, owned by `AVoxelWorld` and run on DN1's game tick.
  - **The `Set` hook** (`FoundationBlocks.cpp` 125-136): a (cell, before, after) callback that schedules the cell and its 6 fluid neighbours, plus lava placement checks at once. Not `TakeChanges`, which the Playground view drains.
  - **Writes:** stamped edits through the authority, so they replay after Finalize and survive unloading.
  - **Batched writes:** `FVoxelColumnPipeline::SetCells(column, cells[])` makes one column copy, one heightmap pass and the union of dirty sections per column per tick.
  - **The simulation ring** (Q6):
    - an act whose reads touch an unknown column is parked per column and re-queued with its remaining delay at that column's Finalize;
    - the `IsKnown` guard;
    - pending acts outside the ring sleep.
  - **The stale-mesh fix** (pipeline :375): apply the stale result, then remesh.
  - **The frame budget** (Q10).
  - **The game rules** `water_source_conversion` and `lava_source_conversion` from world.json `rules`.
  - **The lava and water reactions** in game (W1's rules; their writes are ordinary blocks).
  - **Switches:** `voxel.Fluids`, `.BudgetMs` and `.SimRadius`.
  - The Playground stays still (Q13).
- **Tests** (pipeline, `bManualJobs`):
  - a flow across a column border while the neighbour isn't final, then once it is;
  - unload and reload mid-flow, and the return trip;
  - `SetCells` equals the same cells written one at a time (states, heights, dirty sections);
  - budget carry-over: a flood under a tiny budget ends in the same world, later;
  - 30, 60 and 144 fps give the same world at the same tick;
  - `voxel.Fluids 0` freezes everything.
- **Evidence:**
  - the breach capture (dig into a sea from a cave; the cave floods), which closes the gap "carving into water leaves air pockets" (`VoxelWorld.md` :1075);
  - a frame-time trace of a 1,000-cell flood: fluid ms per frame within the budget, and frame p95.
- **Gate:** goldens and hashes identical; the flight's 2 ms chunk budget with no holes (quiet window); every native suite; the Playground probe suites unchanged.

### W2b: waterlogging, washed away, fluid tags (M)

- **Scope:**
  - `washed_away_by_fluids` and the fluid tags, added to the blocks reference through its tools (Q11), and `bWashedAway` in the table;
  - pops through the existing `Popped` event, with no drop (Q7);
  - held-fluid model states and waterlogged slabs act as sources and never take flowing water;
  - breaking one leaves `water[level=0]`, which closes the `Blocks.md` :134 gap.
- **Tests:** `w_slab`, `w_slab_wall` and `w_waterlogged_bottom` through the authority in game; a broken waterlogged slab leaves a source that flows; the breaking lane's interaction scenarios still pass.

### W3: fluid meshing (M)

- **Scope:**
  - a fluid side face only for the strip above the neighbour's fluid height. This replaces "cull against any fluid", which would leave see-through gaps;
  - sloped tops from the four corners where neighbour levels differ, with `water_flow` and `lava_flow` turned along the steepest descent;
  - the falling-water look;
  - Playground parity in `FoundationBlockMeshes`;
  - `voxel.Fluids.SlopedTops 0|1|2`: 0 is today; 1, the default, is Q5 (b); 2 is vanilla's slopes everywhere, which changes the digest, so the lane uses it for shots only.
  - Corner heights and the flow direction are not black-boxed, so they are tuned by shots against vanilla.
- **Tests:** `Mesher.Shapes` cases (a 5 → 3 step, a falling column, slope corners, equal levels still merging); `Mesher.Digest` identical at 1.
- **Evidence:** a waterfall, a 7-cell spread, a lava fall, and a shoreline pair at 1 and 2 for the look call.
- **Gate:** the digest identical; mesh cost per touched section measured (scout M estimates 0.2 ms).

### DN2: sky and sun renderer (L)

- **Scope:** `UVoxelSkyComponent` on `AVoxelWorld`:
  - one movable directional light from `sun_angle`, handed to the moon below the horizon;
  - the sky material of Jesus's pick (A or B): sky, fog and sunrise colour and alpha, the sun and moon direction, the moon phase, and star brightness and rotation;
  - square 32 x 32 sun and moon quads;
  - 780 stars from our own seeded generator (wiki seed 10842), tuned by look;
  - the sky light's intensity and tint from the tracks;
  - the edge fog colour per frame, replacing `EdgeHazeLuminance` (`VoxelWorld.cpp`:56);
  - the night exposure of the pick;
  - the base colours from the camera's biome;
  - `voxel.Sky.Cycle` (0 is today's control), `voxel.Sky.OrbitTilt`, and the shadow method from DN-P;
  - the material made by script; `CreateGeneratedMap.py`'s `verify()` kept.
- **Tests:** the component's outputs at the six ticks equal the sampler's; cycle 0 reproduces today's shots (TAA off, noise near 0).
- **Evidence:** shots at ticks 0, 6000, 12000, 12786, 13670 and 18000 (`-VoxelTime=`) beside SP1's; water under the moving sky (the "pale water" gap).
- **Gate:** within DN-P's cost gate; cycle 0 unchanged; hashes identical; Jesus confirms the in-game pack before `voxel.Sky.Cycle` defaults to 1.

### W4, W5, DN3

- **W4 (player in fluids):**
  - swimming, drag and buoyancy;
  - the current's push along the flow, 1.39 m/s, for players and mobs but not creative fliers;
  - lava movement −50% and −20%, 4 damage per half second, and 15 s of fire;
  - separate water and lava tests (`VoxelWorld.cpp` 351-384; the gap at `VoxelWorld.md` :1081);
  - the flow vector settled by scout M's item-drift probe (§4.7);
  - tested by the Playground probe suites, new vitals tests and the push measurement.
- **W5 (generated edges):**
  - a wake scan on a worker when a column first enters the ring, over mixed fluid and air sections only, behind `voxel.Fluids.WakeGenerated` (Q4);
  - the edge count per preset against vanilla's 8.2 pending ticks per chunk (`gen_probe.py`: 665 in 81 chunks), measured before the switch defaults on;
  - cave waterfall and lava-fall shots;
  - goldens identical.
- **W5s (main lane):** springs on by default (Q12), with the Decoration goldens re-recorded.
- **DN3 (night readability):**
  - the night exposure tuned with T3's torches, against the floor T3 merges;
  - `video.gamma` wired (Q17);
  - the outline and cracks checked at night;
  - `GetInternalSkyLight()` for later systems;
  - night shots outdoors, in a torch-lit field, and in a cave at noon and midnight, where the cave must look the same.

## 6. Questions

Both scouts' questions merged, plus six new ones (marked "new"). **Who decides:**
- "Manager (away rule)": Jesus's rule while he's away (October 10): "Go with whatever gets closer to the vanilla style of Minecraft ... If it's easy to correct later down the road ... go with whatever the chat recommends as long as it's not a huge decision. Absolutely huge decision can be relayed to me."
- "Jesus": saved data, project rules, deletions, anything far from vanilla, and the sky look family (mock-up first; he picks by feel).

**Shared**

1. **What drives game ticks** (scout M Q3; scout N §6 and risk 7). (a) One world-owned 20 Hz clock in `AVoxelWorld`, built by DN1, with a game tick and a day clock as in 26.3 (section 2). (b) Each half keeps its own counter. (c) The pawn's `Authority.Step`. **Recommended: (a).** It's vanilla's model: fluids flow while the pawn is frozen, and `advance_time` stops only the sky. **Manager (away rule).** Blocks DN1 and W2a.

**Day/night**

2. **Saving the time** (scout N Q4). (a) Now, as an optional world.json `clocks` key (`total_ticks`, `paused`, `rate`, defaults left out, as vanilla's `world_clocks.dat` does), saved on leaving the world, keeping version 1. (b) Session-only until phase 10. (c) Now, with a bump to version 2 so older builds refuse the file. **Recommended: (a).** An older dev build just drops the key. **Jesus:** it is saved data, and the project rule asks for a version bump and a migration for layout changes; whether an optional new key counts as one is his call. Blocks only the key part of DN1.
3. **Where the sky data lives** (new; scout N §3 names a folder). (a) A new hash-checked `Reference/minecraft-sky-26.3/` with its own build and verify tools, plus two `CLAUDE.md` lines (the rules list and Commands), the items reference's pattern. (b) The timelines, dimension types and clocks added to the worldgen reference through its tools, with no rule change; its checksums then also move with the main lane's rebuilds, and the textures wait for DN2. **Recommended: (a):** sky data isn't worldgen, and it keeps clear of the main lane's reference rebuilds. **Jesus** (a project rule). Until he answers, the lane skips DN0.
4. **Generated water and lava edges** (scout M Q1). (a) Still until the player touches them. (b) Woken when the player comes near, as in vanilla; generation stays identical. (c) Settled during generation, which changes every golden and adds to the 5 ms column. **Recommended: (b),** behind `voxel.Fluids.WakeGenerated`, defaulting on only after W5 measures the edge count. **Manager (away rule).**

**Water**

5. **The water surface look** (scout M Q2). (a) Vanilla's sloped corners everywhere, shorelines included; `Mesher.Digest` re-recorded. (b) Slopes and side strips only where levels differ; generated water meshes byte-identical. **Recommended: (b) as W3's default, with (a) behind `SlopedTops 2`, a shot pair and the mock-up's lake toggle.** Flipping the default to (a), the vanilla look, is a later look call made on the main lane. **Manager (away rule).** The look call may go to Jesus with the sky mock-up.
6. **Simulation radius** (scout M Q4). (a) The in-view final columns minus one, as a cvar. (b) A smaller fixed radius, such as 8 columns, to bound floods. **Recommended: (a).** **Manager (away rule).**
7. **Washed-away plants and torches** (scout M Q5). (a) Pop with no drop, as plant pops do today. (b) Drop items as vanilla does. **Recommended: (a) now, (b) once item entities exist (items S8).** **Manager (away rule).**
8. **The wiki against the probe** (scout M Q6). No source from "one beside plus one above"; lava's drop search is 3, not the wiki's 4. **Recommended: follow the probe (the jar).** **Manager (away rule).**
9. **Test fidelity** (scout M Q7). (a) Pin settled states and wave timing exactly, and report transient mismatches. (b) Also pin vanilla's exact transients (the drain checkerboard), which needs vanilla's same-tick order. **Recommended: (a).** **Manager (away rule).**
10. **The fluid frame budget** (scout M Q8, which left it to Jesus). (a) Its own 0.5 ms allowance. (b) Shared inside `voxel.ApplyBudgetMs`'s 2 ms. **Recommended: (a).** **Manager (away rule):** it's a cvar, easy to change later.
11. **Fluid tags** (new; scout M risk 7). (a) Add `washed_away_by_fluids` and the fluid tags to the blocks reference through its tools, as T2 appended the torch, in W2b. (b) Hard-code the block list. **Recommended: (a).** **Manager (away rule):** it stays within the reference rule.
12. **Springs once water flows** (new; phase 6 Q3 was answered "(a) in the data, switched off by default" because water didn't flow). (a) Turn them on after W5, on the main lane, re-recording the Decoration goldens. (b) Keep them off. **Recommended: (a),** as vanilla does. **Manager (away rule).** The re-record is a main-lane session.
13. **Fluids in the Playground** (new). (a) Fluids only in voxel worlds: the system lives on `AVoxelWorld`, and the Playground's pools and probe scenes stay still. (b) Fluids flow in the Playground too. **Recommended: (a) for now.** W3's mesh parity still lands. **Manager (away rule).**

**Sky look and process**

14. **The sky look family and its knobs** (scout N Q1, Q2, Q3 and Q7). Option families: A vanilla sky, B hybrid, C Unreal physical. Knobs: orbit tilt, night darkness and moon shadows. **Recommended:**
    - B;
    - the vanilla orbit, with `voxel.Sky.OrbitTilt` and its default set in the mock-up;
    - a vanilla-dark night;
    - the shared directional light for moon shadows.

    The mock-up opens on these. **Jesus** (the sky look; he picks by feel in MU).
15. **Vanilla screenshots for the mock-up and DN2** (new; scout N risk 4). (a) SP1 on VP1's harness. (b) Jesus takes them himself. (c) None, tuning by memory. **Recommended: (a).** **Jesus:** the client launch needs his go.
16. **Shots and the Playground's sky** (scout N Q5). **Recommended:**
    - the Playground keeps its fixed rig;
    - shots use the fixed sun until DN2, then `-VoxelTime=6000` (noon) by default;
    - dawn, dusk and midnight shots are added.

    **Manager (away rule).**
17. **The brightness slider** (scout N Q6). (a) Wire `video.gamma` in DN3: Moody to Bright moves the night and cave floor by about ±1 EV. (b) Leave it pending. **Recommended: (a).** Vanilla's slider mostly lifts the dark end. **Manager (away rule).**
18. **The two bytecode-derived documents** (new; scout M). (a) Leave them, with this plan banning them as sources. (b) Also add a one-line warning at `Docs/MinecraftBlockReference.md` section 4. (c) Remove them: rebuild the blocks reference without `research/water_research.md`, and delete section 4. **Recommended: (a) and (b) now.** **Manager (away rule)** for (a) and (b); **Jesus** only for (c) (deletions).

**For Jesus:** Q2, Q3, Q14 and Q15, plus Q18 only if he wants the deletions. The manager can take the rest under the away rule.

## 7. Short prompts

**W1 (the lane's first build session):**

> Water and sky track, session W1 (a parallel lane): the fluid core. Work in `C:\dev\projects\Unnamed-water` on `feature/water-sky` (rebase on the current `feature/voxel-world` first if the manager says so). Never touch other worktrees, never push, and make no `CLAUDE.md` edits. Read `Docs/Records/2026-10-10-WaterSky-Plan.md` (sections 1, 4 and 5 W1, and the decided questions). Then read scout M's `C:\dev\sandbox\scout-water\facts.md`: sections 1 and 4 to 7, and its reading list items 4-6 and 16-18. Never read or port `Reference/minecraft-blocks-26.3/research/water_research.md` or `Docs/MinecraftBlockReference.md` section 4; cite scout M's probes instead. Build the state fields, `IsFluidSource()`, the two `FluidAmount == 8` fixes and `Source/UnnamedVoxel/Fluids/` (rules, the FIFO scheduler and a test world), with every oracle as `Unnamed.Voxel.Fluid.*` and each listed mutation shown failing. There is no game wiring. Commit scout M's facts byte-identical as a record, with a script-made oracle data file; never open `oracles.json` whole. Gate: goldens and radius-8 hashes identical (never re-recorded), and every native suite green. One build at a time: check for other editors and UnrealBuildTool first. Send the manager a short plan before building.

**MU (beside the lane, no build slot):**

> Water and sky track, the sky mock-up (sandbox only: no Unreal and no repo commits). Build a browser mock-up in `C:\dev\sandbox\sky-mockup\` so Jesus can pick the sky's look by feel. Read section 5 MU and Q5 and Q14 of `C:\dev\projects\Unnamed-water\Docs\Records\2026-10-10-WaterSky-Plan.md`. Read scout N's `C:\dev\sandbox\scout-daynight\facts.md` sections 4, 6 and 9. Read the handbook's mock-up rule (`C:\dev\tools\shift-managers\HANDBOOK.md`, line 55), and take the breaking lane's `C:\dev\sandbox\break-mockup\` as the pattern. Make one offline three.js page with an `Open-Mockup.cmd` server. Use the real vanilla sun, the eight moon phases and the block textures, copied from the 26.3 client jar; don't read its shader code. Include a "Today" preset as the control, tabs A, B and C, the knobs, and the time slider and markers. Include the curves strip from `probes\jar-data\timeline\day.json`, an empty vanilla-screenshot panel, the lake's shoreline toggle and a JSON export. Check that it renders with headless browser shots of each tab at 4 ticks. Send the manager the path, how to open it, and the shots.
