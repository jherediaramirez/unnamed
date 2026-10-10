# Voxel world handoff: phase 6, into S7 (plant interaction and collision) - October 10, 2026

Read this first if you are picking up plant interaction. Phase 6's S6 (block models drawn in the generated world) is done ([the S6 record](2026-10-10-Voxel-Phase6-S6.md)). **S7 runs after S6b** ([the S6b handoff](2026-10-10-Voxel-Handoff-Phase6-S6b.md): the Playground draws the model blocks), because S7's interaction scenario needs plants that render in the Playground. S6b is done ([the S6b record](2026-10-10-Voxel-Phase6-S6b.md)) and updated this handoff (section 5b); its notes win over the older ones. Facts were true at S6b's commit; check `git status`, `git log -5` and newer records first.

## 1. Read these, and only these

Budget: about 100k of reading.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md) (its handoff pointer may be stale; this handoff wins).
2. **This handoff** (section 5b holds S6b's notes), then [S6b's record](2026-10-10-Voxel-Phase6-S6b.md), sections 1, 2 and 5 (about 6k).
3. **[The phase 6 plan](2026-10-09-Voxel-Phase6-Plan.md):** lines 380 to 404 (S7), 583 to 597 (risks) and 643 to 667 (Jesus's answers: 6 plant interaction, 7 items).
4. **[Docs/Blocks.md](../Blocks.md):** "States and shapes", "Placement", "Budgets and known issues".
5. **[The block reference README](../../Reference/minecraft-blocks-26.3/README.md):** "Shapes and offsets" (outline and collision boxes, `replaceable`, the offset's effect on outlines and collision).
6. **[Docs/VoxelWorld.md](../VoxelWorld.md):** the "Block models" section and the chunk component's collision bullet (find them by heading).
7. **Code, by range:** `Source/Unnamed/Foundation/FoundationBlockPlacement.*`, `FoundationBlocks.*` (`GatherCollision`), the parts of `FoundationAuthority.*` that place and break; `Source/Unnamed/Voxel/VoxelBlockStorage.*`; `AVoxelWorld::GatherCollision` and `GetWaterBox` in `Source/Unnamed/Voxel/VoxelWorld.cpp`; `VoxelModels::GetOffset` in `Source/UnnamedVoxel/Blocks/VoxelBlockTable.*`.

## 2. Working agreement with this user

As in [the S6b handoff](2026-10-10-Voxel-Handoff-Phase6-S6b.md), section 2: plan first to the manager, Sonnet 5.5 review before the commit, look calls with numbers and a control pair, context measured after every build, test run and subagent, quiet windows respected.

## 3. Where things stand

- **Data (S5):** every model state has its outline and collision boxes from the server facts (`GetOutlineBoxes`, `GetCollisionBoxes`, without the offset), `replaceable`, light and fluid. Per block there is the offset kind and range, and whether the outline (bamboo, dripstone, sulfur spike, flowers, propagule ...) and the collision (bamboo, dripstone, sulfur spike) move with it. Selection and collision of a model state are today the bounds of its boxes.
- **Rendering (S6, S6b):** the voxel world and, since S6b, the Playground draw every model state with the offset from `VoxelModels::GetOffset(Info, X, Y)` (a hash of X and Y only, 16 steps per axis, up to the block's horizontal range, down for XYZ kinds). S7's moving outlines and collision boxes must use the same function, so the box sits on the drawn plant.
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

## 5b. Notes from S6b

- **The Playground draws every model state:**
  - one ISM per (section, mesh, layer); model meshes are numbered `FoundationBlockMeshes::Count` + model index;
  - one instance per part, with `VoxelModels::PickAlternative` and the offset from `VoxelModels::GetOffset`;
  - the Playground maps the block's offset kind as `VoxelBlocks::MakeTable` does (`GetModelOffsetCm` in `FoundationInteractionView.cpp`). S7's moved outline and collision boxes should share that mapping: move it to one helper rather than writing a third copy.
- **Held water:** a water-holding state also draws a water-source instance in its cell, full height under water. Seagrass placed into a pool keeps the pool continuous. In air it draws water in mid-air until S7's support rules refuse it.
- **Exact instance counts:** the interaction suite checks drawn cells against the world (`GetDrawnCellCount`) and the instances against `CountExpectedBlockInstances` in `FoundationInteractionProbe.cpp`: one per part, plus held water. S7's plant scenarios must keep both checks green. A placed or popped plant changes both counts, and `RemoveInstance` removes every part of a cell.
- **Layout:**
  - the plant row is showcase row 7 (`AFoundationPlayground::PlantRow`, y 35, 12 plants; tall grass has both halves);
  - the registry gallery has 26 rows and the state gallery 75 rows (4,419 states); their origins are unchanged;
  - the flower scenario needs a free cell away from these.
- **The probe's baseline after S6b:** 45.7 FPS at cap 60, frame-pacing bound; render thread about 4.1 ms; 901 section meshes (243 before). S7's FPS gate compares against S6b's build, not S6's.
- **Masked materials clip only in the depth prepass** in UE 5.8 (`r.EarlyZPassOnlyMaterialMasking` defaults to 1). Two coplanar quads in a masked material therefore let the later one paint its transparent texels over the earlier one. S6b lifts the firefly bush's overlay 1/512 block in the Playground. Any new coplanar decal or overlay quad needs the same treatment.
- **Selection and collision are still the bounds of the boxes,** at the cell, without the offset; the drawn plant may sit up to the block's offset range away from it. That is S7's to fix.

## 6. Known issues

| Issue | Due |
|---|---|
| Model blocks place their default state; no support, facing or stacking rules | S7 |
| `replaceable` comes from the facts for model blocks only | S7 (Jesus's answer 4) |
| 12 model blocks have no item (vanilla item has another id); 3 have none at all | The inventory pass (answer 7) |
| Plants cast shadows at every distance | Jesus's choice (S6b handoff, section 5; on the morning list with (a) recommended) |
| The voxel world's firefly bush draws black with lit fireflies: its overlay is coplanar with the cross, masked in the prepass only. Crop: `Tests/Evidence/2026-10-10/S6b/S6b-voxel-firefly-bush-black-crop.png` | The morning list. Fix: the same 1/512-block lift along the facing in `FEmitter::Model` |
| Playground water draws every face, so a water face coplanar with a solid neighbour z-fights (the pool stone at (12, 6, 0)); held water meets the same faces where water-holding plants touch solids | Later: face-culled water meshes or a small water inset |
| 901 ISM section components cost about 0.15 ms render thread and 0.15 ms game thread in the Playground | A budget item. If it grows: merge each section's model parts into one mesh per layer |
| The Playground's model codes in UV1's second channel reach about 2^21 (texture, tint, light, and the marker at bit 20). The ore looks found float interpolation off by one near 2^23 and keep their words under 2^15. Tonight's shots render correctly (from S6b) | A follow-up of about 30 minutes: split the code across UV1's two channels (each under 2^12), re-save the four `M_Block*` materials, and check a cube with TAA off |
| The combat "weak hit inside the target's cooldown" check failed once under load (strength 0.52 against 0.44) | Watch it; re-run it alone on a quiet machine if it repeats |
| Generation places no model block | S8 |
| The 30-frame jitter shimmer test (S6's G1) | Open |

## 7. The short prompt for S7

> Voxel world phase 6, session S7: plant interaction and collision. S6 and S6b (block models drawn in the generated world and the Playground) are done and merged into `feature/voxel-world`.
>
> Read `C:\dev\projects\Unnamed\Docs\Records\2026-10-10-Voxel-Handoff-Phase6-S7.md` first; its section 1 is the whole reading list (with line ranges), so keep to it. Its section 5b holds S6b's notes: the Playground draws the plants, and your scenarios must keep the exact instance counts green.
>
> Build S7 as the handoff's section 4 says: outline and collision boxes from the facts (moved by S6's `VoxelModels::GetOffset` where the facts say), placement against the support tags, both halves of double plants, attached faces, pop-when-unsupported one level deep, instant mining, placing into replaceable plants with every block on the facts' `replaceable` flag. Tests: `Blocks.Placement`, `Blocks.Survival`, and the interaction-suite flower scenario.
>
> Send the manager a short plan (files and why) before building. Report as the handoff's section 2 says, and measure your context after every build, test run and subagent. End with a handoff into S8 (plants and patches).
