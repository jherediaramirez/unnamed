# Scout M: flowing water and lava (water and sky track, water half)

Repo read at `C:\dev\projects\Unnamed-scout`, detached at 41190e9 (pushed `feature/voxel-world` tip). Written 2026-10-10 by scout M. Read-only: nothing in any repo was changed.

Sources are marked like this:
- **[P]** a black-box probe I ran on the 26.3 server jar (headless; commands and save files only);
- **[W]** minecraft.wiki;
- **[D]** game data in a reference or the jar's data files;
- **[C]** our code at 41190e9 (from two code maps, file:line).

`Reference/minecraft-blocks-26.3/research/water_research.md` and `Docs/MinecraftBlockReference.md` section 4 were written from bytecode method bodies. I treated them only as hypotheses. Every behaviour below is re-measured or comes from the wiki. Workers must not port those two documents.

---

## 1. Summary and recommendations

**What vanilla 26.3 does** (all measured [P] unless marked):

- **Water spreads one cell every 5 game ticks.** Level = cells from the source (1 to 7), so the reach on flat ground is 7. The cell at Manhattan distance d gets level d at tick 5·d, and a plane settles at tick 35 with 113 cells.
- **Falling water** is level 8 at every height and falls one cell per 5 ticks. Flowing water over a drop falls and does not spread sideways. On landing, the water spreads again from level 1.
- **The drop search** sends water only toward the nearest drop within 5 cells of the cell that spreads (a drop at 5 is found, at 6 it is not). Ties split.
- **A new cell takes the value computed from its own neighbours.** So the gap between two sources becomes a source in the same tick, and two sources' spreads merge as the minimum distance.
- **Infinite source:** a cell needs ≥ 2 horizontal source neighbours and a floor below. The floor can be a full block, a bottom slab or a source; falling water is not a floor. A source directly above plus one beside does **not** make a source. The wiki says it does; the probe says no.
- **Removing a source** dries a full plane 35 ticks later, decaying in a checkerboard.
- **Lava in the overworld** spreads one cell every 30 ticks with levels 2, 4, 6, so its reach is 3 and a plane settles at tick 90 with 25 cells. Its drop search is 3; the wiki says 4, the probe says 3. Lava falls one cell per 30 ticks. With its source removed, a plane is dry 90 ticks later. `lava_source_conversion` defaults to false and `water_source_conversion` to true.
- **No randomness** in any lava case I measured: 4 copies each of spread, drain and raise came out identical.
- **Lava meeting water:**
  - a lava source touching water (beside or above) becomes obsidian, at once when placed;
  - flowing lava touching water (beside or above) becomes cobblestone;
  - lava flowing **down into** water turns the water to stone;
  - lava sitting **above** water is not converted;
  - lava entering a cell over soul soil and beside blue ice becomes basalt.
- **Waterlogged blocks:** a waterlogged bottom slab spreads exactly like a source (identical to a source at every checkpoint). Flowing water never waterlogs a slab, and a source placed on a dry slab does not either. Flowers, short grass, saplings and torches in the path become water.
- **Generation leaves fluids still.** With ticks frozen, 81 fresh default-world chunks hold 1 flowing water cell and 665 pending fluid ticks. After 200 ticked ticks they hold 382 flowing or falling water cells and 48 lava cells. Cave waterfalls start only in ticking chunks.

**What our code has at 41190e9** [C]:
- Water and lava are real blocks with 16 level states each: state ids 89-104 and 105-120 [D]. `FluidAmount` is set per level.
- Terrain writes only level-0 sources.
- There is no tick, queue or clock anywhere in `UnnamedVoxel`.
- The mesher draws every fluid cell flat at `FluidAmount/9` and culls every fluid face against any fluid.
- Every write goes through `FFoundationBlockWorld::Set`, a single choke point. Each `SetCell` copies the whole column (about 10 KB) and rescans its heightmaps.
- The player has no swim physics; only vitals read water.
- Edits are session-only until phase 10.

**Recommendations:**
1. **Keep generation byte-identical.** No goldens move. Fluids flow only after an edit, as vanilla schedules them, plus (Q1) a wake-up of generated fluid edges near the player that matches vanilla's cave waterfalls without touching Generate.
2. **Put the rules in a new plain-C++ core.** `UnnamedVoxel/Fluids/`: pure rules, a deterministic due-tick FIFO scheduler and an in-memory test world. Pin it against the probe oracles first (session W1) before any game wiring.
3. **Drive it from a world-owned 20 Hz clock** (Q3). Wake it from `FFoundationBlockWorld::Set`. Write back through the authority with a new **batched per-column `SetCells`**, and cap each frame's work (0.5 ms suggested; the remainder carries over and time dilates).
4. **Meshing:** draw the side-face strips between unequal fluid heights, plus slopes and flow UVs, only where levels differ (Q2). Generated water then meshes byte-identical and `Mesher.Digest` holds.
5. **Cost:** 0 ms added to the 5 ms column. During play, a 113-cell spread is about 0.1 ms of rules spread over 7 waves. The real risks are per-write column copies and meshes thrown away when a column is edited every frame. Both are fixed in W2.

---

## 2. Reading list for the track's workers (41190e9, in order)

Line numbers come from the code maps against 41190e9. Re-check them on the tip you build on (see section 8).

1. This file, sections 4 to 7.
2. `Docs/VoxelWorld.md`:
   - 590-620 (storage, meshing rules, rendering, collision);
   - 694-713 (editing; the water box);
   - 1075 and 1081 (the fluid gaps).
3. `Docs/Blocks.md`:
   - 42-45 (shapes, waterlogging, still lava);
   - 75-81 (after an edit, one level deep);
   - 88 and 96 (Playground water meshes);
   - 132-134 (water gaps).
4. `Source/UnnamedVoxel/Blocks/VoxelBlockTable.h`:
   - 45-80 (shape, layer and tint enums);
   - 130-170 (`FluidAmount` :135, `HeldFluid` :155, `IsFluid`/`HoldsFluid`/`GetFluidHeight` :163-167).
5. `Source/Unnamed/Voxel/VoxelBlocks.cpp`:
   - 95-150 (water :123-132, lava :136-146: `FluidAmount` from `GetFluid(State).Amount`);
   - 180-195 (`HeldFluid` = water source).
6. `Source/Unnamed/Foundation/FoundationBlockRegistry.h` 25-50 (`FFoundationFluid`: level, amount, source, falling) and `FoundationBlockRegistry.cpp` 595-619 (`GetFluid`).
7. `Source/UnnamedVoxel/Meshing/VoxelMesher.cpp`:
   - 495-505 (the opaque mask);
   - 560-683: the face loop, with the fluid/model case at 619-639 (cull against any `HoldsFluid` :626, full height when fluid is above :628/:637, top plane :670) and the top-face greedy merge at 644-681 (key :632);
   - 380-395 (`GetSectionsTouching`).
8. `Source/UnnamedVoxel/Storage/VoxelColumn.cpp` 10-31 (heightmaps: fluids count for motion-blocking, not ocean floor) and 66-87 (`SetState` → a whole-column `UpdateHeights`).
9. `Source/Unnamed/Foundation/FoundationBlocks.h` 145-175 and `FoundationBlocks.cpp` 110-140 and 255-275: `IsKnown`, `Set` (the only mutation: revision, storage, change log) and `TakeChanges`.
10. `Source/Unnamed/Foundation/FoundationAuthority.cpp`:
    - 160-170 (`Step`, 20 Hz with the pawn);
    - 355-370 (`Execute`);
    - 550-580 (place);
    - 720-755 (break);
    - 830-895 (`SettlePlants`: the neighbour-reaction pattern to copy).
11. `Source/Unnamed/Voxel/VoxelColumnPipeline.h`:
    - 95-105 (edits kept for unloaded columns);
    - 150-190 (dropped buckets, settings, test hooks `bManualJobs` :185, `RunJob` :225);
    - 310-320 (`FEdit`);
    - 370-380 (`Edits`).
12. `Source/Unnamed/Voxel/VoxelColumnPipeline.cpp`:
    - 40-50 (`ReplayPipelineEdits`);
    - 590-660 (`MarkDirty`, the Generate path);
    - 700-725 (Finalize then replay);
    - 850-890 (`GetCell`, `SetCell`: column copy :876, `GetSectionsTouching` :883);
    - 370-380 (stale-mesh drop by `MeshVersion`).
13. `Source/Unnamed/Voxel/VoxelBlockStorage.cpp` 18-55.
14. `Source/Unnamed/Voxel/VoxelWorld.cpp` 351-384 (`GetWaterBox`, `IsInWater`) and 405-415 (`Tick`). `VoxelWorldSubsystem.cpp` 165-224 (`Tick`, `Update`, `ApplyReady`).
15. `Source/Unnamed/Foundation/FoundationPawn.cpp` 150-165 (body/eyes in water) and 236-320 (the 20 Hz step loop). `FoundationSimulation.cpp` 160-225 (movement: no water code).
16. `Source/UnnamedVoxel/Surface/VoxelFreezePass.cpp` 55-80 and 118-140 (`FluidAmount == 8` as "source"). `Decoration/VoxelBlockPredicates.cpp` 95-110 and 225-232 (`GetFluidBits`: the same assumption).
17. `Source/Unnamed/Foundation/FoundationBlockMeshes.cpp` 40-71 (Playground: eight water heights, full when water is above).
18. Tests that must stay green:
    - `Source/Unnamed/Tests/VoxelMeshDigestTests.cpp` 20-70;
    - `VoxelEditTests.cpp` 300-330, 860-880 and 1290-1300;
    - `VoxelAquiferTests.cpp` 20-30 and 250-260;
    - `VoxelDecorationTests.cpp` 1486-1500.
19. Probe data: `C:\dev\sandbox\scout-water\probes\oracles.json`, through `analyze.py` and `side.py` (section 5). Don't open the JSON whole: it is 620 KB.

---

## 3. Data inventory

**In the references** [D]:

- `Reference/minecraft-blocks-26.3/block_states.json`:
  - `water` (protocol id 37) has states 89-104 (`level` 0-15) and `lava` (38) has 105-120. Id 89 is source with amount 8; 90 is amount 7.
  - `"shape": "fluid"` appears 32 times.
  - 153 blocks have `waterlogged`, with 1,526 states between them.
- `facts/block_state_facts.json`: per state, `fluid: {fluid, amount, source}` or null (795 states carry a fluid), plus `replaceable` and `occludes`. Example: `oak_slab` 15177 (top, waterlogged) has a water source; 15178 has null.
- `facts/block_facts.csv`: light emission 0 for water and 15 for lava.
- `research/block_tags.csv` (resolved membership of all 1,286 blocks):
  - `washed_away_by_fluids` 254 blocks;
  - `blocks_fluid_flow` 1,026;
  - `replaceable` 30;
  - `ice` 4;
  - `blocks_motion` 1,000.
- Textures under `assets/minecraft/textures/block/`:
  - `water_still` 16x512, frametime 2;
  - `water_flow` 32x1024, default frametime;
  - `lava_still` 16x320, frametime 2 with a ping-pong frame list;
  - `lava_flow` 32x512, frametime 3;
  - `water_overlay` 16x16.

  The game's flow array already holds lava 16 + water 32 layers (Blocks.md :96).
- `Reference/minecraft-worldgen-26.3/data/minecraft/worldgen/`:
  - `feature/spring_water.json` (state water `falling: true`, 11 valid blocks), `spring_lava_overworld.json` (8 valid blocks), `spring_lava_frozen.json` and `lake_lava.json`;
  - placed `spring_water` (count 25), `spring_lava` (count 20, `very_biased_to_bottom`), `spring_lava_frozen` and `lake_lava_surface`/`_underground`;
  - `noise_settings/overworld.json`: `default_fluid` water, `sea_level` 63, the `aquifers` object;
  - tags `ice.json`, `blocks_motion.json`, `replaceable.json`, `lava_pool_stone_cannot_replace.json`.
- `Reference/minecraft-items-26.3/items.json`:
  - `bucket` (1128, stack 16);
  - `water_bucket` (1129, stack 1);
  - `lava_bucket` (1130, stack 1).
- `Reference/minecraft-menu/qa-results.json`: the rule names `rule.water_source_conversion` and `rule.lava_source_conversion`.

**Vanilla data not yet in a reference** (named here, not created):

- `data/minecraft/tags/fluid/*.json`: 10 files, including `water` and `lava` (each the still fluid plus its flowing form).
- These block tag **files** (only the resolved CSV is in the reference):
  - `washed_away_by_fluids.json` (123 ids + 11 tag refs);
  - `blocks_fluid_flow.json`;
  - `blocks_lava_fire_spread.json`;
  - `ice_melts_when_destroyed_above.json`;
  - `infiniburn_*`;
  - `soul_fire_base_blocks.json`;
  - `replaceable_by_mushrooms.json`;
  - `supports_lily_pad.json`.
- `data/minecraft/dimension_type/*.json`. `the_nether.json` has `minecraft:gameplay/fast_lava: true` and `minecraft:gameplay/water_evaporates: true`; the overworld has neither.
- `worldgen/configured_feature/spring_lava_nether.json`.
- The `minecraft:game_rule` registry. It is in the datagen report `C:\dev\sandbox\mc-26.3-blocks\reports\reports\registries.json`, around line 6875.
- `bubble_column` states 18651-18652 (only in `research/blocks_registry.csv` and the datagen `blocks.json`).
- Client sounds (`block.water.*`, `block.lava.*`, `item.bucket.*`). There is no client jar under `C:\dev\sandbox\mc-26.3-blocks`.

**Probe artefacts** (`C:\dev\sandbox\scout-water\probes\`):
- `fluid_probe.py`, the harness;
- `fluid_run.json` (round 1: 40 scenarios, 67 checkpoints to t=900);
- `fluid_run2.json` (round 2: 15 scenarios to t=600);
- `oracles.json`;
- `gen_counts.json`;
- the helpers `analyze.py`, `side.py`, `make_oracles.py` and `gen_probe.py`.

---

## 4. Behaviour in our own words

"t" counts game ticks after the fluid was placed by command. Every probe scenario stands on a flat stone floor (top at y 63) in a superflat 26.3 world, with ticks frozen and stepped. "Level" is the block state's `level`.

### 4.1 States and heights
- **Levels:**
  - level 0 is a source (amount 8);
  - levels 1-7 are flowing with amount 8 − level;
  - levels 8-15 are falling with amount 8.
  
  [D: `block_state_facts.json`; P: every falling cell observed is level 8.]
- **Surface height** is amount/9: 0.889 for a source, then 0.778 down to 0.111. A cell with fluid above is drawn full height. [W Water, "Height by level"]
- **Waterlogged** states report a water source [D: `block_state_facts.json`, the slab example]. A waterlogged bottom slab behaves exactly like a source: `w_slab` equals `w_plane` outside the centre cell at all 67 checkpoints [P].
- **Kelp, seagrass and bubble columns** always hold water ("Inherent") [W Waterlogging].

### 4.2 Timing
- Water acts 5 ticks after the event that woke it. A placed source first spreads at t=5, and every wave follows 5 ticks after the last [P `w_plane`; W: "one block per five ticks"].
- Lava in the overworld acts every 30 ticks [P `l_plane0-3`; W]. In the Nether it acts every 10 with reach 7 [W; the dimension attribute `fast_lava`, D]. The Nether is not probed.
- Falling fluid descends one cell per 5 ticks (water) or 30 ticks (lava) [P `w_cliff`: y69 at t=25, then one cell down every 5 ticks to y64 at t=50; `lw_stone`: one cell down every 30 ticks].
- **No random delays** in any lava case measured: fresh spread, drain and a raised level, 4 copies each, identical to the tick [P `l_plane0-3`, `l_drain0-3`, `l_onto_lower0-3`]. The wiki states none either [W Lava].

### 4.3 How a cell updates (our words, from the oracles)
When a woken flowing cell acts, it first re-derives its own state from its neighbours, then spreads.

- **Re-deriving:**
  - With ≥ 2 horizontal same-fluid source neighbours and a floor, it becomes a source. A floor is a block that stops movement, including a bottom slab, or a same-fluid source. This needs `water_source_conversion` (water) or `lava_source_conversion` (lava, default off).
  - Otherwise, with same fluid above, it becomes falling.
  - Otherwise it takes the best horizontal neighbour's amount minus the drop-off (1 for water, 2 for overworld lava). Below 1, it empties.
  - If it changed, it acts again one delay later.

  Sources: [P `w_infinite`, `w_infinite_slab` (source); `w_infinite_air` (falling water below: stays level 1); `w_above_beside2` (source above + one beside → falling, not a source; this contradicts W Water)].
- **Spreading:**
  - If the cell below can take fluid, the fluid goes down (a falling state).
  - A flowing cell that went down does not also spread sideways. A source that went down spreads sideways on its next act [P `w_falling_lands`: the source in the air goes down at t=5 and to its sides at t=10; `w_cliff`: edge cells at y70 stay level 4 and do not spread past the edge].
  - Otherwise it spreads sideways with amount − drop-off. Falling fluid restarts at the top flowing level (water level 1) [P `w_cliff`, the floor spread from the landing cell].
- **Target value:** the value written into a neighbour is that neighbour's own re-derived state, not "my amount − 1" [P `w_infinite`: the middle is already a source at t=5; `w_merge`: overlap cells take the nearer source's level].
- **Drop search:** a sideways spread goes only in the directions whose nearest reachable drop is closest. A drop is an open cell below a passable cell. The search walks passable cells. Water finds drops up to 5 cells from the spreading cell; lava up to 3. No drop in reach means all open directions [P `w_hole1`-`w_hole5` (a single line), `w_hole6` (diamond), `w_hole_two` (nearer only), `w_hole_tie` and `w_hole_diag` (both), `l_hole2`/`l_hole3` (line), `l_hole4` (diamond)]. W: "reachable in four or fewer blocks from the block it wants to flow to", which matches water's 5; the wiki gives lava the same, but the probe says 3.
- **Same-tick order:** acts due in the same tick run in the order they were scheduled. Drain transients depend on it [P `w_drain`: the source is removed at t=60 and refills to level 2 at t=65, because the first neighbour to act has already dropped]. Settled states and wave timing do not depend on it.

### 4.4 Walls, plants, waterlogging
- Full blocks stop flow. Bottom and top slabs are not entered by flowing water and are not waterlogged by it [P `w_slab_wall`, `w_waterlogged_bottom`; W Waterlogging: "Source", never flowing].
- Water from a waterlogged block leaves through its open faces [W Waterlogging]. Breaking a waterlogged block leaves its water source [W]. Our gap: "breaking a water-holding plant leaves air" (Blocks.md :134).
- Plants and torches in `washed_away_by_fluids` are replaced by the fluid: poppy, short grass, oak sapling and torch all became water [P `w_slab_wall`]. In vanilla they drop as items [W Water].

### 4.5 Lava meets water

| Case | Result | Source |
|---|---|---|
| Lava source with water reaching a side | the lava becomes obsidian in the tick the water arrives | P `lw_obsidian` (t=25), `lw_lava_onto_water_side` |
| Lava source placed next to (flowing) water | obsidian at once | P `lw_place_beside` (t=5 → 6), `lw_fizz` |
| Water above a lava source (falling onto it) | obsidian | P `lw_water_above_lava`, `lw_water_onto_flowing_lava` |
| Flowing lava with water beside or above | cobblestone | P `lw_cobble` (l6 → C), `lw_water_onto_flowing_lava` (l2, l4, l6 → C) |
| Lava flowing down into a water cell | the water becomes stone and the lava stays where it was | P `lw_stone` (t=90), `lw_lava_over_water` (t=90; then the lava resumes sideways at t=120) |
| Flowing lava directly above a water source | nothing; lava is not converted by water below | P `lw_lava_over_water` |
| Lava entering a cell over soul soil next to blue ice | basalt instead of lava | P `lw_basalt` (t=60); W Lava |

### 4.6 Generation against play
- **Vanilla's generation leaves fluid still.** With ticks frozen from start-up, 81 fresh default-world chunks (seed 263, x/z 4000-4143) hold 562k water sources, 2.5k lava sources, **1** flowing water cell and **665** pending fluid ticks in their `fluid_ticks`, about 8.2 per chunk. After those forced chunks tick for 200 ticks, they hold 382 flowing or falling water cells and 48 lava cells [P `gen_probe.py`, `gen_counts.json`]. So generated edges (cave waterfalls, lava falls, springs) are scheduled at generation but flow only where chunks tick.
- **Fluids tick only in block-ticking chunks,** inside the simulation distance [W summarised in Docs/MinecraftBlockReference.md 828-830; not re-probed].
- **Pending ticks are saved per chunk** (`fluid_ticks`: fluid id, position, remaining delay `t`, priority `p`) and resume on load [P: read back from the region files at every checkpoint].

### 4.7 Player
- **Current** pushes players and mobs at about 1.39 m/s; creative fliers are not pushed [W Water].
- **Lava:** horizontal speed is −50% and vertical −20%. It deals 4 HP per tick, with damage immunity making that once per half second, and sets the player on fire for 15 s. After leaving, the player burns for just under 15 s, taking 14 hits. Light 15 [W Lava].
- **Swimming:** sprinting fully submerged enters swim mode; Depth Strider adds ⅓ efficiency per level [W Water]. No numeric swim speeds on the wiki page read.
- **Unknown / not measured:** the exact flow vector a cell pushes along, and the client's corner heights and flow texture direction. These are client and entity code; no black-box was run. Follow-up probe: summon an item in a probe scenario and read its `Pos` with `/data get entity` per stepped tick to get push direction and speed per cell.

---

## 5. Test oracles

**Reproduce** (light, 1-1.5 min each; one headless server, no client):
```
cd C:\dev\sandbox\scout-water\probes
uv run --no-project --python 3.13 python -I fluid_probe.py run --out fluid_run.json
uv run --no-project --python 3.13 python -I fluid_probe.py run --set 2 --server-dir server2 --out fluid_run2.json --max-tick 600
uv run --no-project --python 3.13 python -I make_oracles.py        # writes oracles.json, prints the cross-checks
uv run --no-project --python 3.13 python -I analyze.py fluid_run.json w_hole5,lw_cobble --changes
uv run --no-project --python 3.13 python -I side.py fluid_run.json w_cliff 0 25,50,85
uv run --no-project --python 3.13 python -I gen_probe.py           # generation counts (35 s)
```
`oracles.json` holds, per scenario, the settled cells `[x, y, z, state, tick of last change]`, the removed cells, `settled_by` and the actions. For `w_plane`, `w_slab`, `w_drain`, `l_plane0` and `l_drain0` it also holds the full series at every checkpoint. Coordinates are relative to the scenario origin; the floor top is y 63.

**Exact values** (all [P]; "d" = |x| + |z|):

| Oracle | Expected |
|---|---|
| `w_plane` | 113 cells; level d, written at t = 5d; settled at t=35 (cross-check printed True) |
| `w_channel` | x 0..7 = source, levels 1..7, at t 5, 10 … 35 |
| `w_hole1`-`w_hole5` (drop 1 deep at +x D) | only x 1..D wet, with levels 1..D at t 5k; the drop cell (D, 63) at level 8, t = 5(D+1); nothing else wet |
| `w_hole6` | the full diamond, except x=7 on row z=0 stays dry (x=6 falls); drop (6, 63) level 8 at t=35 |
| `w_hole_diag` (drop at (2, 2)) | quadrant x, z ∈ 0..2 with level x+z; drop at t=25 |
| `w_hole_two` | only +x; `w_hole_tie`: both ±x; drops at t=15 |
| `w_infinite` | middle (1, 64) source at t=5; (3, 64) level 1 at t=5 |
| `w_infinite_air` | middle stays level 1; below it level 8 at y 63, 62, 61 at t 10, 15, 20 |
| `w_infinite_slab` | middle over a bottom slab: source at t=5 |
| `w_above_beside2` | (1, 64) level 8 at t=5 and stays (not a source) |
| `w_cliff` (7x7 platform, top y 69, source at (0, 70)) | 253 cells, settled t=85; columns at x=±4 fall at level 8, y69 at t=25 … y64 at t=50; the floor spreads to level 7 at 7 cells from the column |
| `w_falling_lands` (source at y 72 in air) | 185 cells settled by t=80, of which 40 at level 8 (5 columns x 8) |
| `w_merge` (sources at x 0 and 4) | 165 cells; level = distance to the nearer source; settled t=35 |
| `w_pool_wall` (5x5 sources, wall cell removed at t=60) | 25 sources stay; 39 flowing cells by t=95, levels {1:1, 2:1, 3:3, 4:5, 5:7, 6:9, 7:13} |
| `w_drain` (source removed at t=60) | 0 cells at t=95 (35 ticks) |
| `w_slab` | equal to `w_plane` outside the centre at every checkpoint |
| `w_slab_wall` | both slabs stay dry; poppy, grass, sapling and torch cells become water; 107 water cells |
| `w_waterlogged_bottom` | the slab stays `waterlogged=false` under a source |
| `l_plane0`-`l_plane3` | 25 cells; level 2d, written at t = 30d; settled t=90; all 4 identical |
| `l_channel` | source, 2, 4, 6 at t 30, 60, 90 |
| `l_hole2` / `l_hole3` | a line only; drop at t 90 / 120. `l_hole4`: the full 25-cell diamond, settled t=90 |
| `l_drain0`-`l_drain3` (source removed at t=100) | 0 cells at t=190; identical |
| `l_onto_lower0`-`3` (second source at x3, t=120) | x3 lava 6 → source (by t=130); x2 lava 4 → 2 at t=150; x4 lava 2 at 150, x5 lava 4 at 180, x6 lava 6 at 210; identical |
| `lw_obsidian` | (0, 64) lava → obsidian at t=25; water x1..5 at levels 5..1 |
| `lw_cobble` (water at x6 from t=300) | x3 lava 6 → cobblestone when water reaches x4 (level 2); x2 stays lava 4 |
| `lw_stone` | lava falls at y66 (t=30) and y65 (t=60); the water source at y64 → stone at t=90 |
| `lw_lava_over_water` | (2, 64) lava 4 is not converted; (2, 63) water → stone at t=90; (3, 64) lava 6 at t=120 |
| `lw_water_onto_flowing_lava` | lava source → obsidian, lava 2 and 4 → cobblestone, lava 6 → cobblestone once water lies above it |
| `lw_basalt` | (2, 64) basalt at t=60; (1, 64) lava 2; (3, 64) stays air |
| `lw_place_beside` / `lw_fizz` | a placed lava source next to water is obsidian at the next snapshot |
| Generation (`gen_probe.py`) | frozen: 1 flowing cell, 665 pending ticks in 81 chunks; after 200 ticks: 430 flowing or falling cells |

**Mutations that must fail:**
- spread delay 4 or 6 → the `w_plane` timing fails;
- water drop-off 2 → reach 3 in `w_plane` and `w_channel`;
- drop search 4 (water) → `w_hole5` becomes a diamond; search 6 → `w_hole6` becomes a line;
- lava search 4 → `l_hole4` becomes a line;
- infinite rule without the floor check → `w_infinite_air` becomes a source; with "source above + one beside" → `w_above_beside2` fails; with a bottom slab not counting as floor → `w_infinite_slab` fails;
- target value as "my amount − 1" instead of re-derived → `w_infinite`'s middle becomes level 1 at t=5 and a source only at t=10;
- falling flowing water also spreading sideways → `w_cliff` grows wider on top;
- lava drop-off 1 → `l_plane` reach 7;
- the lava conversion also checking below → `lw_lava_over_water` gets cobblestone at (2, 64);
- flowing water waterlogging slabs → `w_slab_wall` fails;
- a lava source not checked on placement → `lw_place_beside` stays lava;
- FIFO broken (LIFO for same-tick acts) → the `w_drain` series at t=65 no longer refills the centre to level 2. Only pin this if the worker matches the same-tick order (Q7).

---

## 6. Draft design

**The core** (`Source/UnnamedVoxel/Fluids/`, UObject-free and deterministic):
- **State info:** add the fluid kind (none, water, lava) and the level (0-15) to `FVoxelStateInfo`, filled in `VoxelBlocks::MakeTable` from `FFoundationFluid` (registry `GetFluid`), plus `IsFluidSource()`.
  - Fix the two readers that take `FluidAmount == 8` as "source" (the freeze pass :74/:138, `GetFluidBits` :104). Falling water also has amount 8.
  - Generation writes no falling states, so this changes no output and no golden. Verify with `Aquifer.Golden`, `Density.Golden`, `Decoration.Golden` and `Mesher.Digest`.
- **Rules:** a pure function `Act(Cell, Reader) → {new own state, writes[]}` over a cell reader (state at a world position, plus "known" for unloaded columns), implementing section 4.3-4.5. It uses only the block table: `bSolid` (blocks_motion) for walls and floors, slab shapes, `HeldFluid`, and a `bWashedAway` flag that needs the tag (section 3).
- **Scheduler:** a min-heap or bucketed queue keyed by `(due tick, sequence number)`. The sequence number gives FIFO within a tick. Plus a set that drops duplicate pending acts for the same cell, as vanilla keeps one pending tick per cell and fluid. A `uint64` world tick counter drives it.
- **Determinism:** the same edits at the same ticks give the same world. There is no randomness at all (no lava randomness is needed: section 4.2).

**The game side** (`Source/Unnamed/Voxel/`, new `FVoxelFluidSystem` owned by `AVoxelWorld`):
- **Clock:** a world-owned accumulator at 20 Hz (`FoundationProfile.TickSeconds` 0.05), run from `AVoxelWorld::Tick` (VoxelWorld.cpp :410) or the subsystem's `Tick`. It is not tied to the pawn's `bSimulate` loop, so fluid keeps flowing under `voxel.Fly` (Q3). A frame runs its due game ticks, under a work budget per frame.
- **Wake-up:** a hook in `FFoundationBlockWorld::Set` (FoundationBlocks.cpp :125-136): a callback with (cell, before, after). It schedules the changed cell (if fluid) and its 6 neighbours (if fluid) one delay ahead, plus lava placement checks at once (section 4.5).
  - Every edit, plant pop and the fluid's own writes pass through `Set`.
  - Do not use `TakeChanges`: the Playground view drains it, and nothing reads it in generated worlds.
- **Writes:**
  - Fluid writes go through the world/authority, so they are stamped edits that replay after Finalize and survive unloading, as the player's edits do.
  - Add **`FVoxelColumnPipeline::SetCells(column, cells[])`**: one column copy, one heightmap pass and the union of dirty sections per column per game tick. Today each `SetCell` copies about 10 KB and rescans the column.
- **Washing away:** fluid entering a `washed_away_by_fluids` cell pops it through the existing `Popped` event. It drops nothing, matching today's pop rule (Q5).
- **Waterlogging:**
  - a `HeldFluid` model state or a `waterlogged=true` slab acts as a water source for its neighbours and never receives flowing water;
  - breaking it leaves `water[level=0]`, which fixes the Blocks.md :134 gap.
- **Streaming:**
  - An act whose reads touch a column that is not known (not final, or unloaded) is deferred. It stays in a per-column pending list and is re-queued with its remaining delay when that column becomes final.
  - Writes never target a column that is not known (`IsKnown` guard; `Set` checks it).
  - A **simulation ring** limits which columns act: in-view final columns at least 1 column from the view edge. The drop search reaches 5 cells, so it can enter a neighbour column, but never 2 columns away.
  - Pending acts outside the ring sleep, as vanilla's do outside block-ticking chunks.
- **Generated edges (Q1, option b):** when a column enters the simulation ring the first time, a worker scans the sections that hold both fluid and air for fluid cells that would change if woken, and schedules them.
  - Generation stays byte-identical. Cave waterfalls and lava falls start near the player, as vanilla's do.
  - Expect about 8 per column if our aquifer matches vanilla; measure ours first (W5).
  - Springs (S11, plan Q3) then work as vanilla: the spring feature places a falling state, and the wake-up flows it.
- **Saving:** none until phase 10. Then save the fluid edits with the other edits, plus each column's pending acts with their remaining delay, plus the world tick. world.json changes only with a `version` bump. Until then, flowing state lasts the session, like edits.
- **Meshing:**
  - **Side faces:** draw a fluid side only for the strip above the neighbour's fluid height (the full face against air, none against an equal or higher neighbour). Today's rule culls against any fluid, which would leave see-through gaps beside lower flowing water.
  - **Tops:** slopes from the four corners only where neighbour levels differ (Q2), with UVs rotated along the flow (the direction of steepest descent, from the neighbours' heights) using `water_flow`/`lava_flow`.
  - Keep the merge key: state already includes the level, so equal levels merge.
  - Generated water (all sources, or full under water) keeps byte-identical geometry, so `Mesher.Digest` holds.
  - Fix the stale-mesh drop (pipeline :375) so a column edited every frame still shows its last mesh. Today a newer `MeshVersion` throws the result away.
- **Player** (separate session):
  - `FoundationSimulation` has no water code. Add swimming, drag, buoyancy and the current push (1.39 m/s along the flow, players and mobs; not creative fliers).
  - Lava movement −50% / −20%, damage 4 per half second and fire 15 s.
  - Split `IsInWater` into water and lava (VoxelWorld.cpp :351-384 treats only water as swimmable). Lava "counting as water" is a listed gap (VoxelWorld.md :1081).

**Switches:**
- `voxel.Fluids 0/1` (default 1 after W2; 0 freezes everything, for A/B);
- `voxel.Fluids.WakeGenerated 0/1` (Q1);
- `voxel.Fluids.BudgetMs` (0.5);
- `voxel.Fluids.SimRadius`;
- `voxel.Fluids.SlopedTops` (Q2);
- the game rules `water_source_conversion` and `lava_source_conversion` from world.json `rules` (already saved; Saved worlds table).

**Gates:**
- every oracle in section 5 passes, and each mutation fails;
- all goldens stay identical (`Mesher.Digest`, `Aquifer/Density/Decoration/Biome.Golden`);
- the flight's 2 ms chunk budget holds;
- a breach capture: dig into a sea from a cave and the cave floods. This closes the gap "carving into water leaves air pockets" (VoxelWorld.md :1075);
- a frame-time trace of a 1,000-cell flood under the budget.

**Cost** (estimates, to be measured in W1 and W2):
- **Column work:** 0 ms on the 5 ms column. Nothing is added to Generate, Decorate or Finalize. The wake scan runs only when a column first enters the simulation ring, on a worker, over mixed fluid/air sections only. Estimated 0.02-0.2 ms per column; it never touches the 5 ms column.
- **Rules:** about 6 neighbour reads plus a drop search of at most about 4 x 40 cells per act, so about 1-4 µs. A source on a plane is 113 acts plus re-acts over 7 waves: about 0.1-0.5 ms in total, 4-5 waves a second at most.
- **Writes:** today about 3-10 µs per `SetCell` (a 10 KB copy plus a column rescan plus marking 1-8 sections). Batched per column per tick, that is one copy per touched column.
- **Meshing:** about 0.2 ms per touched section, on workers, at most 4 times a second while water moves.
- **Worst case:** opening a sea into a large cave makes thousands of acts per wave. The budget caps the game-thread share, and the remainder carries over, so the flood runs slower than vanilla rather than hitching.
- **Per-frame budget:** 0.5 ms suggested inside `voxel.ApplyBudgetMs`'s 2 ms, or a separate allowance. Jesus's call if it should share.

---

## 7. Code-shape sketches

**`FVoxelStateInfo` additions.** Against 41190e9: `Source/UnnamedVoxel/Blocks/VoxelBlockTable.h`, `Source/Unnamed/Voxel/VoxelBlocks.cpp`.
```cpp
enum class EVoxelFluid : uint8 { None, Water, Lava };
// in FVoxelStateInfo, beside FluidAmount (:135)
EVoxelFluid Fluid = EVoxelFluid::None;  // HeldFluid states: Water
uint8 FluidLevel = 0;                   // 0 source, 1-7 flowing, 8-15 falling
bool bWashedAway = false;               // washed_away_by_fluids (needs the tag in a reference)
bool IsFluidSource() const { return Fluid != EVoxelFluid::None && (FluidLevel == 0 || HeldFluid != 0); }
```

**The rules and the scheduler.** Against 41190e9: new `Source/UnnamedVoxel/Fluids/VoxelFluidRules.h` and `VoxelFluidScheduler.h`; uses `VoxelBlockTable.h` only.
```cpp
struct FVoxelFluidParams { uint8 Delay, DropOff, SearchCells; bool bConvert; };  // water {5,1,5,true}; lava {30,2,3,false}
struct FVoxelFluidWrite { FIntVector Cell; uint16 State; };
template <typename TReader>  // TReader: uint16 Get(FIntVector), bool IsKnown(FIntVector)
bool VoxelFluidRules::Act(const FVoxelBlockTable&, const TReader&, FIntVector Cell,
                          TArray<FVoxelFluidWrite>& OutWrites);   // false = deferred (unknown cell read)

class FVoxelFluidScheduler {
public:
    void Schedule(FIntVector Cell, EVoxelFluid Fluid, uint64 Due);   // dedupe per (cell, fluid)
    // pops acts due at Tick in FIFO order; stops when Budget says so; the rest stay queued
    template <typename F> int32 RunDue(uint64 Tick, F&& ActFn, const FVoxelBudget&);
    void Park(FIntPoint Column);   void Unpark(FIntPoint Column);  // streaming
private:
    struct FEntry { uint64 Due, Seq; FIntVector Cell; EVoxelFluid Fluid; };
    TArray<FEntry> Heap; uint64 NextSeq = 0; TSet<FIntVector> Pending;
    TMap<FIntPoint, TArray<FEntry>> Parked;   // keyed by column; remaining delay kept
};
```

**The tick loop.** Against 41190e9: new `Source/Unnamed/Voxel/VoxelFluidSystem.h`/`.cpp`, called from `AVoxelWorld::Tick` (VoxelWorld.cpp :410).
```cpp
void FVoxelFluidSystem::Advance(float Seconds) {
    Accum += Seconds;
    while (Accum >= 0.05f && Budget.HasTime()) {          // 20 Hz
        ++WorldTick; Accum -= 0.05f;
        Writes.Reset();
        Scheduler.RunDue(WorldTick, [&](FIntVector C){ return Rules::Act(Table, Reader, C, Writes); }, Budget);
        ApplyBatched(Writes);   // groups by column -> Pipeline.SetCells, then World.Set records, wakes neighbours
    }
}
// wake hook, installed on FFoundationBlockWorld (FoundationBlocks.cpp :125-136)
void FVoxelFluidSystem::OnSet(FIntVector Cell, FFoundationBlockState Before, FFoundationBlockState After);
```

**The batched write.** Against 41190e9: `Source/Unnamed/Voxel/VoxelColumnPipeline.h`/`.cpp` (next to `SetCell` :870-890).
```cpp
// one copy-on-write per column, one heightmap pass, union of GetSectionsTouching, one MeshVersion bump
bool FVoxelColumnPipeline::SetCells(FIntPoint Column, TConstArrayView<FVoxelCellWrite> Cells, int64 Stamp);
```

**The mesher's fluid side rule.** Against 41190e9: `Source/UnnamedVoxel/Meshing/VoxelMesher.cpp` :619-639.
```cpp
// side face of fluid cell A toward neighbour B: draw [HeightOf(B), HeightOf(A)] where B is fluid and lower;
// [0, HeightOf(A)] where B is open; nothing where B is opaque-covering or as high.
// HeightOf = 1 if fluid above, else FluidAmount/9. Equal-height neighbours (all generated water) emit nothing new.
```

---

## 8. Re-orientation list (diff these before building)

| Session | Touches | What to diff first |
|---|---|---|
| S9a / S9b (trees) | `Decoration/VoxelDecorator.cpp`, the tree features, `Tests/VoxelDecorationTests.cpp` goldens, `Tests/VoxelMeshDigestTests.cpp` (`Golden`, `PlantGolden`) | new golden values; any `waterlogged` handling in the decorator (:520-524); the `FVoxelStateInfo` fields they add |
| S10 (carvers) | the Generate pass in `Terrain/VoxelDensityTerrain.cpp` (fill :381-441), `Terrain/VoxelAquifer.*`, cave goldens | the fluid in carved cells (S10-Q1: the exact aquifer per position recommended); the new carved fluid/air edges the wake scan will find |
| S11 (underground features, springs) | `Decoration/Features/*`, `VoxelFeatureReader.cpp`, the springs switch `voxel.Decoration.Springs` (off, plan Q3) | lakes and pools write sources with `cave_air` above; springs place a falling state, so W5 turns them on through the fluid rules |
| S12 (ocean, cold) | ocean and cold features, the freeze pass, icebergs | `VoxelFreezePass.cpp` (the `FluidAmount == 8` test W1 changes); `VoxelBlockPredicates.cpp` `GetFluidBits` |
| T3 (torch light) | the light code, `Voxel/VoxelWorld.cpp`, `VoxelBlocks.cpp` (light per state), the mesher's vertex G | `AVoxelWorld::Tick`, where the fluid clock hooks; lava as a light source when it flows (each lava level has light 15 [D]) |
| Breaking prototype | `Foundation/FoundationInteractionView.cpp` (water draw :225-238, :315-319, :360-407), possibly the authority break path | the Playground water mesh path; `TakeChanges` readers; the break command (:720-755), which the fluid wake-up and "waterlogged leaves water" touch |

---

## 9. Proposed session split (water half)

| Session | Area | Size | Tests and evidence | Waits for |
|---|---|---|---|---|
| **W1 Fluid core** | state fields, the `FluidAmount == 8` fixes, rules, scheduler, an in-memory test world; the oracles ported as a test table (e.g. `Tests/VoxelFluidTests.cpp` plus a data file built from `oracles.json`) | M | `Unnamed.Voxel.Fluid.*`: every section 5 oracle and mutation; every golden unchanged; a micro-bench of acts per ms | nothing (new files); rebase on S12/S9 if they touch `VoxelBlockTable.h` or the freeze pass |
| **W2 Wiring and streaming** | `FVoxelFluidSystem`, the 20 Hz world clock, the `Set` hook, batched `SetCells`, the simulation ring, park/unpark, the stale-mesh fix, waterlogged sources, washed-away pops, lava/water reactions in game, the game rules from world.json | L (split W2a wiring + streaming, W2b reactions + waterlogging if large) | pipeline tests (`bManualJobs`: a flow across a column border with the neighbour not final, then final; unload/reload mid-flow; return trip); the breach capture; a frame trace of a 1,000-cell flood; the flight gate unchanged | W1; T3 (VoxelWorld.cpp); the breaking prototype's authority changes |
| **W3 Fluid meshing** | side strips, slopes and flow UVs (Q2), the falling-water look, lava flow texture; Playground parity (`FoundationBlockMeshes`) | M | `Mesher.Shapes` cases (step 5→3, falling column, slope corners); `Mesher.Digest` identical (or re-recorded if Q2 is (a)); shots: a waterfall, a 7-cell spread, a lava fall | W1 (levels exist); S9's digest re-records; T3's mesher light |
| **W4 Player in fluids** | swim, drag, buoyancy, current push, lava movement, damage and fire; separate water and lava `IsIn*` | M | the Playground probe suites (the shared 20 Hz step!); new vitals tests; a current push measured against 1.39 m/s; an item-drift probe for the flow vector (section 4.7) | W2 (the flow direction per cell) |
| **W5 Generated edges and springs** | the wake scan on entering the ring, its count per column against vanilla's 8.2 pending ticks per chunk; springs on through the rules | S | the edge count per preset; cave waterfall and lava-fall shots; goldens identical | W2; S10 (carvers), S11 (springs) |
| Phase 10 | saving fluid edits plus pending acts plus the world tick | (in phase 10) | | phase 10 saves |

---

## 10. Risks and open questions

**Risks:**
1. **Mesh starvation.** A column written every frame by several flows never shows a mesh (stale results are dropped by `MeshVersion`). Fix in W2: apply the stale mesh, then remesh.
2. **Write cost.** Per-cell `SetCell` copies the column and rescans heights. Without batching, a flood costs milliseconds per wave.
3. **Unbounded floods.** A sea opened into a big cave grows without limit, as in vanilla. The budget keeps frames smooth but the flood runs slower than real time; the simulation ring bounds it in space.
4. **Edits growth.** Fluid writes become edits (session memory). A flooded cave of 10k cells is about 10k map entries, which is fine. Phase 10 saves them.
5. **Order fidelity.** Exact transients need vanilla's same-tick FIFO and its wake order. The settled states do not (Q7).
6. **Unknowns.** The flow vector and the client's corner heights are not black-boxed. W3 and W4 need a look call or the item-drift probe.
7. **Tag data.** `washed_away_by_fluids` and the fluid tags are not in a reference as files; only the resolved CSV is. A reference addition is needed (reference rules: regenerate through its tools only).
8. **Our aquifer's edges.** If our aquifer leaves far more fluid/air edges than vanilla's (about 8 per chunk), the wake-up could start many waterfalls at once. Measure in W5 before turning it on.

**Questions for Jesus:**
- **Q1. Generated water and lava edges** (cave waterfalls, lava falls, springs):
  - (a) stay still until the player touches them; cheapest.
  - (b) **Recommended:** wake when the player comes near, as vanilla does. Generation stays identical; the world then changes in play.
  - (c) settle during generation; changes every golden and adds to the 5 ms column.
- **Q2. The water surface look:**
  - (a) vanilla-like sloped corners everywhere, shorelines included; `Mesher.Digest` re-recorded.
  - (b) **Recommended first:** slopes and side strips only where levels differ; generated water meshes byte-identical. (a) can follow as a look call with shots.
- **Q3. What drives fluid ticks:**
  - (a) **Recommended:** a world clock at 20 Hz, so water keeps flowing under `voxel.Fly` and while the pawn is frozen.
  - (b) the pawn's movement step (`Authority.Step`).
- **Q4. Simulation radius:**
  - (a) **Recommended:** the in-view final columns minus one, as a cvar. Cost is near zero when nothing flows.
  - (b) a smaller fixed radius (e.g. 8 columns) to bound floods.
- **Q5. Washed-away plants and torches:**
  - (a) **Recommended:** pop with no drop, as today's plant pops do (earlier Q4).
  - (b) drop items as vanilla does, once item drops exist.
- **Q6. The wiki versus the probe:** the probe says no source from "one beside + one above", and lava's drop search is 3, not 4. **Recommended:** follow the probe (the jar). Not a real choice unless Jesus prefers the wiki.
- **Q7. Test fidelity:**
  - (a) **Recommended:** pin settled states and wave timing exactly, and report transient mismatches.
  - (b) also pin vanilla's exact transients (the drain checkerboard), which needs vanilla's same-tick order.
- **Q8. Frame budget for fluids:**
  - (a) **Recommended:** its own 0.5 ms allowance.
  - (b) shared inside `voxel.ApplyBudgetMs` (2 ms).
