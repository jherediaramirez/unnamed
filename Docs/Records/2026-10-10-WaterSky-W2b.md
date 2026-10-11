# Water and sky W2b: waterlogging, washed away, breaking leaves water

October 10, 2026, water and sky track, session W2b (lane `feature/water-sky`, worktree `C:\dev\projects\Unnamed-water`). It starts with the merge of main's `e214663` (S7b, S9c, S9d's test fix, items S4, S10) as `50f434a`, plus `535fd5d`, which renames two of this lane's locals that collided with `VoxelFeatureReader.cpp` in a unity build (C4459), with no behaviour change. W2b then gives the fluid system its view of plants and waterlogged blocks: washed-away blocks pop as fluid takes their cell, waterlogged states are sources that flowing fluid never enters, and breaking or popping a block that holds water leaves its source (plan section 5, W2b). `voxel.Fluids` still defaults to 0 (W3 flips it), so generated worlds, goldens and hashes are unchanged.

## Decisions

- **Q11 revised (manager, under Jesus's away rule, October 10): no reference change.** Plan Q11 (a) had W2b add `washed_away_by_fluids` and the fluid tags to the blocks reference through its tools, on scout M's risk 7 ("only the resolved CSV is in a reference"). The finding: the resolved tags are already in the data. `block_manifest.json` lists every imported block's vanilla tags, and the block import carries them into `DA_Blocks` as gameplay tags (`Config/Tags/MinecraftBlockTags.ini`): `Block.WashedAwayByFluids`, `Block.BlocksFluidFlow` and `Block.BlocksMotion` among them. So the game reads them from `DA_Blocks`, with no reference rebuild and no `DA_Blocks` restamp. The raw tag files (`washed_away_by_fluids.json`, `blocks_fluid_flow.json` and the ten `data/minecraft/tags/fluid/*.json`) wait until something reads them; adding them later is one tools change.
- **Which blocks fluid can't enter** (the W2a handoff's open question 2) is answered by the data. Among the imported blocks:
  - 76 are washed away: plants, torches, kelp, seagrass, snow layers and powder snow;
  - every other non-fluid block is in `blocks_fluid_flow`, except sugar cane, which is in neither tag and stays a wall (not probed).
- **Waterlogged slabs** (the W1 handoff's open question 2): option (b), the fluid system reads the `waterlogged` property itself. The block table, mesher, freeze pass and heightmaps are unchanged, so no golden moves; waterlogged slabs still draw dry until W3.
- Q7 (a) stands: pops drop nothing.

## What changed

**The fluid view** (`FVoxelFluidSystem::MakeCell`, `Voxel/VoxelFluidSystem.cpp`):
- A state of a block with a `waterlogged` property is waterloggable: flowing fluid never enters it [P `w_slab_wall`, `w_waterlogged_bottom`]. With `waterlogged=true` it is a water source [P `w_slab`]. The table carries a model state's water; a slab has only the property.
- A washed-away block holding no fluid (the `Block.WashedAwayByFluids` tag) is an open cell that blocks no motion, whatever its shape (powder snow is a cube): fluid takes the cell [P `w_slab_wall`]. This replaces W2a's stub, which made these walls.
- Other models and layers holding no fluid stay walls; those holding water stay its sources.

**Pops and settling** (`Flush`, `OnFlushed`; `FFoundationAuthority::SettleAfterFluid`):
- After each flush, every changed cell is handed back with the state the fluid replaced. `AVoxelWorld` passes it to its authority.
- A washed-away block raises `Popped` with no drop (Q7). Then the plants around settle as after an edit: a double plant's other half pops.
- Changes the settling makes wake the fluid through the `OnSet` hook, as edits do. `OnSet` keeps the frame's pipeline when it runs inside `Advance`.

**Breaking leaves water** (`FFoundationBlockRegistry::GetRemainder`; `BreakBlock` and `SettlePlants`' pop): a block that holds a water source (waterlogged, kelp, seagrass) leaves `water[level=0]`, else air (minecraft.wiki "Waterlogging"). This applies in the Playground too, where the water stays still (Q13). It closes the `Blocks.md` gap "breaking or popping a water-holding plant leaves air".

**Saved worlds:** a new break or pop of a water-holding block now saves a water edit where it saved air before. There is no format change and no migration; edits already saved stay as they are.

**Tests** (6 new; `TestVoxel.ps1` expects 161). `FFluidWorldRun` now holds an authority, wired as `AVoxelWorld` wires it.

| Test | What it pins |
|---|---|
| `Fluid.World.Slab` | Scout M's `w_slab` replayed in game with the real blocks: 113 cells, and the tick each last changed, equal the probe's. |
| `Fluid.World.SlabWall` | `w_slab_wall`: 109 cells and ticks equal the probe's (both slabs dry); the four plants pop once each; a player's inventory is untouched. The probe's oak sapling isn't imported, so a dandelion (washed away too) stands in. |
| `Fluid.World.WaterloggedBottom` | `w_waterlogged_bottom`: 7 cells and ticks equal the probe's; the slab stays `waterlogged=false`. |
| `Fluid.World.BreakWaterlogged` | A waterlogged slab walled in over a hole holds still; its remainder is `water[level=0]`, which falls into the hole. W1's oracle world agrees at every tick. |
| `Fluid.World.WashedAway` | A poppy and a tall grass on grass are washed away (W1's oracle world agrees at every tick). The upper half pops with the lower half and stays air; 3 pops; no drop. |
| `Foundation.Interaction.WaterRemains` | Through the command path: a waterlogged slab and seagrass leave `water[level=0]`, and so does seagrass that pops when the stone under it is broken; a dry slab leaves air. |

**Mutations** (the manager's three; `C:\dev\sandbox\water-w2b\mutate.ps1`, `mutations.txt`), each failing two tests:
- `GetRemainder` returning air: `WaterRemains` and `BreakWaterlogged`;
- a washed-away pop giving each player the block's item: `SlabWall` and `WashedAway`;
- a washed-away cell made a wall (W2a's stub): `SlabWall` and `WashedAway`.

## Results

**Gate** (run g1, after the last code change; `C:\dev\sandbox\water-w2b\gate-g1.txt`). Other lanes' editors were running; nothing here is timed.
- The four reference verifiers report REFERENCE OK.
- `TestVoxel.ps1` passes 161 of 161, and `Unnamed.Voxel.Fluid` 24 of 24. The goldens are unchanged: `Mesher.Digest` (trees on: overworld `0x9c60a0ec50ad7985` / `0x27dd91e77ccd164b`, large_biomes `0x112989521b6dc60c` / `0x762f1bf60ca6a7e5`, amplified `0x32e2efa3bd031917` / `0xa6e6109c9bf13ae3`, as S9c's record), and `Aquifer`, `Density`, `Decoration` and `Biome.Golden`.
- Every native suite: 241 passed, 0 failed; one passed with a warning (`Blocks.Textures`, as at the merge).
- `npm test`: 57 passed. `source-check` passes.
- **The six radius-8 hashes are identical** to main's (6 of 6), both load orders agree and 0 columns differ: plain `0xe3e4e339022a4ec0`, `0x92d83639acdee88f`, `0x615321e7b313fed6`; decorated `0xf427b0a0ec0e6eaa`, `0xcac6968a2e00c046`, `0x0a9cb42b08a0f10a`.

**The merge's gate** (m1, on `535fd5d` before any W2b code; `gate-m1.txt`, `e2e-m1.txt`): the same verifiers, 156 of 156, Fluid 19 of 19, the same digests and hashes, native 235 passed, npm 57, source-check, create-world 32 PASS, the probe with 0 failures (44.8 fps), and a smoke flight (holes in 212 frames, at most 19 columns; 3 of 9,446 frames over 2 ms).

**End to end** (e1, after the last code change; `C:\dev\sandbox\water-w2b\e2e-e1.txt`; smoke runs, not timed):
- `node create-world.mjs --editor`: 32 PASS, 0 FAIL.
- `TestFoundationRuntime.ps1 -Editor -FrameRates 60` (movement and interaction): 0 failures, 43.7 fps mean.
- `CaptureVoxel.ps1 -Flight`: holes in 205 frames (at most 16 columns), chunk work worst 6.69 ms on one circle frame, 2 of 9,442 frames over 2 ms, memory flat over laps 2 and 3 (4,962 to 4,979 MB). Another lane's editor was running at its start. Fluids are off in the flight.

## Known gaps (none probed)

- **Dry waterloggable plants are never entered,** as slabs: 22 of the 76 washed-away blocks are waterloggable (corals and coral fans, sea pickle, glow lichen, dripleaves, mangrove propagule, hanging roots). Flowing water stops at them instead of washing them away.
- **Lava falling onto a waterlogged slab turns it to stone** (W1's rule for lava over water).
- **Waterlogged full blocks and walled models** (leaves; waterlogged dripstone, amethyst and mangrove roots) hold their water: the leaves' full faces keep it in. Dripstone, amethyst and mangrove roots spill it on all sides: W2a's rule for models holding water.
- **Snow washes away at 8 layers** too, per the tag.
- **Settling runs for every flushed cell** (six neighbour checks each). Its time counts in the budget's flush estimate. W2b ran no flood trace, so its cost in a big flood is not measured (W5's flood work).
- **Sugar cane** is in neither tag and stays a wall.
- **Waterlogged slabs draw dry** (the mesher reads the table, which gives slabs no water): W3.
- **A lily pad over drained water** (W2a's example for settling) doesn't arise from fluid: lily pads need a source below, and fluid never removes one. Settling after fluid writes is pinned with the double plant instead.

## Subagents and tokens

- **Lead:** 417k at the gate (MeasureAgentUsage). About 120k went on the reading list and plan, 40k on the merge and its gate, and the rest on the code, tests, mutations, the review and these records. The manager accepted continuing past the 300k checkpoint, with the wrap-up by 500k.
- **Sonnet 5.5 pre-commit reviewer** (diff only, 846 lines): 64.8k peak. Ten findings, none high. Taken:
  - the washed-away tag is requested at each `Start`, with an error when it is missing, instead of a static;
  - a probe with no pinned cells fails.
  Recorded: the settling's cost (Known gaps). Checked and left:
  - a stale `Before` (`SettlePlants` re-reads the other half);
  - waterlogged leaves (the spread search checks walls);
  - player 0 (real players start at 1), and the rest.

## Evidence

- `C:\dev\sandbox\water-w2b\gate-g1.txt` and `e2e-e1.txt` (final code); `gate-m1.txt` and `e2e-m1.txt` (the merge); `mutations.txt` and `runs\mut-*` (the mutations); `runs\t1`-`t3` (the first test runs); `review\w2b.diff`; `build-*.log`.
- `Tests/Evidence/2026-10-10/WaterSky-W2b-g1-TestVoxel`, `-g1-Hashes`, `-g1-Native`, `-e1-Probe`, `-e1-Flight`, `Worlds/Create-1791688078652` (final code).
- `Tests/Evidence/2026-10-10/WaterSky-W2b-m1-TestVoxel`, `-m1-Hashes`, `-m1-Native`, `-m1-Probe`, `-m1-Flight`, `Worlds/Create-1791686549467` (the merge).
