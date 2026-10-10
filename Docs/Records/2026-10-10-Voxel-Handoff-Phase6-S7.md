# Voxel world handoff: phase 6, into S7 (plant interaction and collision) - October 10, 2026

Read this first if you are picking up plant interaction. Phase 6's S6 (block models drawn in the generated world) is done ([the S6 record](2026-10-10-Voxel-Phase6-S6.md)). **S7 runs after S6b** ([the S6b handoff](2026-10-10-Voxel-Handoff-Phase6-S6b.md): the Playground draws the model blocks), because S7's interaction scenario needs plants that render in the Playground. S6b updates this handoff when it finishes; its notes win over the ones below. Facts were true at S6's commit; check `git status`, `git log -5` and newer records first.

## 1. Read these, and only these

Budget: about 100k of reading.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md) (its handoff pointer may be stale; this handoff wins).
2. **This handoff**, then S6b's notes in it (if any) and S6b's record.
3. **[The phase 6 plan](2026-10-09-Voxel-Phase6-Plan.md):** lines 380 to 404 (S7), 583 to 597 (risks) and 643 to 667 (Jesus's answers: 6 plant interaction, 7 items).
4. **[Docs/Blocks.md](../Blocks.md):** "States and shapes", "Placement", "Budgets and known issues".
5. **[The block reference README](../../Reference/minecraft-blocks-26.3/README.md):** "Shapes and offsets" (outline and collision boxes, `replaceable`, the offset's effect on outlines and collision).
6. **[Docs/VoxelWorld.md](../VoxelWorld.md):** the "Block models" section and the chunk component's collision bullet (find them by heading).
7. **Code, by range:** `Source/Unnamed/Foundation/FoundationBlockPlacement.*`, `FoundationBlocks.*` (`GatherCollision`), the parts of `FoundationAuthority.*` that place and break; `Source/Unnamed/Voxel/VoxelBlockStorage.*`; `AVoxelWorld::GatherCollision` and `GetWaterBox` in `Source/Unnamed/Voxel/VoxelWorld.cpp`; `VoxelModels::GetOffset` in `Source/UnnamedVoxel/Blocks/VoxelBlockTable.*`.

## 2. Working agreement with this user

As in [the S6b handoff](2026-10-10-Voxel-Handoff-Phase6-S6b.md), section 2: plan first to the manager, Sonnet 5.5 review before the commit, look calls with numbers and a control pair, context measured after every build, test run and subagent, quiet windows respected.

## 3. Where things stand

- **Data (S5):** every model state has its outline and collision boxes from the server facts (`GetOutlineBoxes`, `GetCollisionBoxes`, without the offset), `replaceable`, light and fluid. Per block there is the offset kind and range, and whether the outline (bamboo, dripstone, sulfur spike, flowers, propagule ...) and the collision (bamboo, dripstone, sulfur spike) move with it. Selection and collision of a model state are today the bounds of its boxes.
- **Rendering (S6, S6b):** the voxel world draws every model state with the offset from `VoxelModels::GetOffset(Info, X, Y)` (a hash of X and Y only, 16 steps per axis, up to the block's horizontal range, down for XYZ kinds). S7's moving outlines and collision boxes must use the same function, so the box sits on the drawn plant.
- **Movement and water:** a model state's `bSolid` is 26.3's `blocks_motion` tag. A water-holding model state (kelp, seagrass, waterlogged ones) is water for swimming (`AVoxelWorld::GetWaterBox`) and for the motion-blocking heightmaps (`FVoxelStateInfo::HoldsFluid`).
- **Placement** gives the default state with no support, facing or stacking rules. `replaceable` comes from the facts for model blocks only (cubes keep the old rule: water only). Jesus's answer 4: S7 switches every block to the facts' flag, with plant placement, and tests it.

## 4. S7: what to do

The plan's S7 (lines 380 to 404):
- collision and outline boxes from the facts, in `GatherCollision` and targeting, moved by the offset where the facts say so;
- placement checked against the support tags (Jesus's answer 6);
- both halves of double plants;
- attached faces: vine, glow lichen and sculk vein faces, cocoa;
- pop-when-unsupported after mining, one level deep;
- instant mining from the facts;
- placing into replaceable plants, with every block on the facts' `replaceable` flag (answer 4);
- no items with another id (answer 7).

Tests: `Blocks.Placement` (truth tables for plants), `Blocks.Survival` (the cascade), and an interaction-suite scenario: a flower on grass; mine the grass and the flower pops. Gate: the Playground probe's FPS is not lower beyond noise, and the collision gather time per tick is reported (at most +10%).

## 5. Notes from S6

- **Shade overrides:** in 26.3, glow lichen and sculk vein carry no `shade_direction_override`; the vine's ceiling part and the amethyst cluster and buds do. This does not affect placement. It corrects the S6 handoff's premise.
- **Water around plants:** water-holding states draw their water in the voxel world. Placing kelp or seagrass in air is S7's to refuse (support rules); until then it draws water in mid-air.
- **Plant shadows:** cast at every distance. Jesus decides between the options in the S6b handoff, section 5.
- **Generation places no model block yet.** S8 (plants and patches) will, through the decoration features; S7's rules must not depend on generated plants.

## 6. Known issues

| Issue | Due |
|---|---|
| Model blocks place their default state; no support, facing or stacking rules | S7 |
| `replaceable` comes from the facts for model blocks only | S7 (Jesus's answer 4) |
| 12 model blocks have no item (vanilla item has another id); 3 have none at all | The inventory pass (answer 7) |
| Plants cast shadows at every distance | Jesus's choice (S6b handoff, section 5) |
| Generation places no model block | S8 |
| The 30-frame jitter shimmer test (S6's G1) | Open |

## 7. The short prompt for S7

> Voxel world phase 6, session S7: plant interaction and collision. S6 and S6b (block models drawn in the generated world and the Playground) are done and merged into `feature/voxel-world`.
>
> Read `C:\dev\projects\Unnamed\Docs\Records\2026-10-10-Voxel-Handoff-Phase6-S7.md` first; its section 1 is the whole reading list (with line ranges), so keep to it.
>
> Build S7 as the handoff's section 4 says: outline and collision boxes from the facts (moved by S6's `VoxelModels::GetOffset` where the facts say), placement against the support tags, both halves of double plants, attached faces, pop-when-unsupported one level deep, instant mining, placing into replaceable plants with every block on the facts' `replaceable` flag. Tests: `Blocks.Placement`, `Blocks.Survival`, and the interaction-suite flower scenario.
>
> Send the manager a short plan (files and why) before building. Report as the handoff's section 2 says, and measure your context after every build, test run and subagent. End with a handoff into S8 (plants and patches).
