# Voxel world phase 6, S7: plant interaction and collision - October 10, 2026

The S7 record (parallel lane `feature/voxel-s6`, run by the night manager Nyx for Jesus). Plants and the other model blocks now collide and are targeted by their own boxes, place by their properties, check their support, keep double plants together and pop when unsupported, one level deep. [The S7 handoff](2026-10-10-Voxel-Handoff-Phase6-S7.md) set the scope; [scout H's prep](2026-10-10-Voxel-Phase6-Scout-H-Plant-Interaction.md) supplied the measured rules. Rules and behaviour: [Blocks.md](../Blocks.md), Placement and States and shapes.

## 1. What was built

- **Shapes** (part A, commit `d40ff4e`):
  - one offset mapping, `FoundationBlockOffsets`, for the voxel table, the Playground's models and the block world's boxes;
  - per-box outlines and collision moved where the facts say (`FFoundationBlockWorld::GetOutlineBoxes`, `GetCollisionBoxes`), used by `GatherCollision`, targeting (each box, nearest), the outline (each box) and `AVoxelWorld::GatherCollision`, which gives model states their boxes through a per-state `ModelCollides` flag. `bSolid` stays for the heightmaps.
- **Placement** (`FoundationPlantRules`): a model block without an asset rule takes its state from its properties (half, facing, the multiface faces, `vertical_direction`, waterlogged in a water source). T2's wall torch keeps its `WallAttached` rule.
- **Double plants:** the authority writes the upper half; mining or replacing either half removes the other (`FFoundationAuthority::SettlePlants`). No room above refuses (`DestinationOccupied`, or `OutsideBounds` at the build limit).
- **Survival** (part B): S8's `FVoxelPlantSurvival` is the one table. S7 added five kinds:
  - `Attached`: a sturdy face, or a tag, at a side: amethyst, shelf mushroom, coral wall fans, corals, spore blossom, hanging roots, cave vines, pale hanging moss;
  - `TagAt`: cocoa, propagule, azalea, bamboo, big and small dripleaf;
  - `Multiface`, with a public `HoldsFace`;
  - `Pointed`: dripstone, sulfur spike;
  - `DripleafStem`.

  `VoxelBlocks::GetPlantSurvival` builds the table once per process from the default table and the worldgen reference's tags. The Playground, the voxel world and the test fixtures hand it to their authority.
- **After an edit**, one level deep (answer 6): each neighbour of the changed cells that no longer survives pops (a `Popped` event, no drop), with its other half. Multiface blocks lose only unheld faces. An unknown cell refuses at placement and keeps at settle.
- **Replaceable:** the asset already matched the facts' flag for every block except snow (water and lava were true, the 15 model blocks came from the facts), so no reimport was needed. Snow stays not replaceable (Q5).
- **Switch:** `Options.bPlantRules` (default on).
- **Docs:** Blocks.md (States and shapes, Placement, known issues, Tests), VoxelWorld.md (the collision bullet).
- **Scout H's prep**, committed byte-identical as `2026-10-10-Voxel-Phase6-Scout-H-Plant-Interaction.md`, SHA-256 `e2ec7af4c26ddbc9616917392af75be7ae06d36c8e434360bd29f74a28fc556d` (copied from `C:\dev\sandbox\scout-plant-interaction\facts.md`). Its sketches were re-checked against `10da63e`. The Foundation files were unchanged since its base `015bb42`. Two sketches changed: its own survival table gave way to S8's, and the DA_Blocks rebuild was not needed. One claim was wrong: a thick bamboo stalk's outline can reach 1/16 block into the next cell at its largest offsets.

## 2. Verified

| What | Result |
|---|---|
| Foundation suites (`Unnamed.Foundation`) | 38 of 38, new: Blocks.PlantShapes, .PlantPlacement, .Replaceable, .Survival, .GatherBench, Interaction.Plants |
| TestVoxel | 101 of 101 |
| Radius-8 hashes, decorated | unchanged: 0x41fda26c3faa91da, 0x652c06643974705f, 0x5454860304d8f56c (S8's plants-on values); plant goldens pinned in `VoxelDecorationTests` pass |
| Mutations, each caught | partner dropped from the unit (Interaction.Plants, 4 asserts); a moved shape's first box skipped (Blocks.PlantShapes); an unknown cell lets a plant pop (Blocks.Survival) |
| Interaction suite (cap 60) | 74 checks, 0 failures. Plants group exact: dandelion placed (cells 5,126 of 5,126, instances 6,457 of 6,457), popped when its grass was mined (back to 5,124 / 6,455), refused on stone, tall grass both halves (5,128 / 6,459) and gone together |
| Gate | section 3 |

## 3. Gate

Run in Nyx's quiet window with no other editor or Java on the machine, checked before every launch. Both sides are DLL pairs (`UnrealEditor-Unnamed` with `UnrealEditor-UnnamedVoxel`), built with `-DisableAdaptiveUnity`: "old" is `c049d46` (main before S7) plus the bench file, "new" is S7. The order was new, old, new, old. Accepted by Nyx as measured.

| Figure | New | Old |
|---|---|---|
| Playground probe FPS, cap 60 (S6b's baseline 45.7) | 45.07 / 45.62 | 45.61 / 45.71 |
| Interaction p50, ms (gather + interaction + preview per update) | 0.0151 / 0.0147 | 0.0118 / 0.0117 |
| Interaction p95, ms | 0.0455 / 0.0373 | 0.0312 / 0.0318 |
| `Blocks.GatherBench`, microseconds per gather (boxes per gather) | 1.479 / 1.476 (30.47) | 0.868 / 1.820 (21.09) |
| Probe failures | 0 / 0 | 0 / 0 |

- **FPS:** at noise on the 60 cap (frame-pacing bound): mean -0.3 FPS, and run 1 is the low one.
- **The clean figure is the interaction p50:** +0.003 ms per update (+27%), against the 0.5 ms budget.
- **Gather:** the bench is too short for a clean percentage (about 20 ms in all; the old side's two runs differ 2x). The new side returns 44% more boxes by design: an azalea's five boxes instead of one bound, and bamboo moved.
- **Not timed:** `AVoxelWorld::GatherCollision`. A grass cell costs one more array read there (`ModelCollides`).
- **Follow-up** (in the S7b handoff): the fast paths, and a 2,000-round bench.

## 4. Jesus's questions (scout H, section 9) and what was built meanwhile

| Question | Recommended | Built until answered |
|---|---|---|
| Q1 stacks under "one level" | (a) same-block vertical chains pop fully, bounded | strictly one level plus the double plant's other half |
| Q2 sturdy faces | (a) dump isFaceSturdy bits through FactsDump (Nyx adds: T2's harness) | S8's `GetSturdyFaces`; no test row for leaves, mud, soul sand or 8-layer snow |
| Q3 survival oracle | (a) the probe in the reference tools, the full matrix | about 80 pinned rows (`Blocks.Survival`) |
| Q4 pop drops | (a) none until item entities | none |
| Q5 snow under answer 4 | (a) only `layers=1` replaceable | snow not replaceable, as before |
| Q6 underwater plants | (a) kelp and seagrass in water, corals anywhere | corals need only a sturdy floor |
| Q7 coral wall fans | (a) the fan item places them on sides | floor fans only |
| Q8 the vine's top face | (a) as measured (the support's upper face) | no vine placed on an underside; an up face is never checked or trimmed |

## 5. Known issues

- Breaking or popping a water-holding plant leaves air, not its water (breaks already did).
- Project blocks hold no plant: the survival table covers the imported blocks only.
- A thick bamboo outline reaches 1/16 block into the next cell at its largest offsets; a ray through that sliver alone misses it.
- Replacing a tall grass's lower half with tall grass pops the old upper half (an event) before the new upper is written.
- Generated worlds: no S7 kind is reached by generation yet (the hashes did not move). Nyx's rule: if a later generation session reaches them, that is the main lane's decided change.

## 6. Evidence (`C:\dev\sandbox\s7-plants\`)

- `tests\b1-interaction\runtime-60.json` and `runtime-60-interaction-plant-flower.png`: the plants scenarios.
- `tests\b1-foundation`, `tests\b1-voxel\Report`: the suites; `tests\mut-partner`, `mut-movedbox`, `mut-unknown`: the mutations.
- `tests\b1-hashes\log.txt`, `tests\b1-hashes-amplified\log.txt`: the radius-8 hashes.
- `ab\results.txt`: the gate.

## 7. Token use

| Agent | Peak context | Processed |
|---|---|---|
| Lead | 543.4k at the record (the wrap-up started at about 456k) | 75.3M |
| Pre-commit review, Foundation (Sonnet 5.5) | 104.7k | 1.53M |
| Pre-commit review, voxel and tests (Sonnet 5.5) | 105.7k | 0.87M |

Review fixes:
- a missing `VineUpFace`, which would not have compiled;
- the per-state `ModelCollides` flag for grass cells in generated worlds;
- unique names for the test helpers;
- the bamboo outline exception, and guards so that only a lower half writes an upper half.

All are covered by the suites and the gate runs above.
