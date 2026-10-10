# Voxel world handoff: phase 6, after S7, into S7b (plant interaction: Jesus's answers) - October 10, 2026

Read this first if you are picking up plant interaction after S7. S7 (plant interaction and collision) is done ([the S7 record](2026-10-10-Voxel-Phase6-S7.md)) on the parallel lane `feature/voxel-s6`. It was merged with S8 part A and torch T2. S7b is small: it applies Jesus's answers to scout H's eight questions (section 4) and fixes the known issues he picks. The main lane's S8b and S9 do not depend on S7b, and torch T3 ([its handoff](2026-10-10-Torch-Handoff-T3.md)) waits only for S7's merge, not for S7b. Facts were true at S7's commit; check `git status`, `git log -5` and newer records first.

## 1. Read these, and only these

Budget: about 60k of reading.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md) (its handoff pointer may be stale; this handoff wins).
2. **This handoff**, then [the S7 record](2026-10-10-Voxel-Phase6-S7.md), sections 1, 4 and 5.
3. **[Docs/Blocks.md](../Blocks.md):** the "Placement" section from "Plants and the other model blocks", and "Budgets and known issues".
4. **[Scout H's prep](2026-10-10-Voxel-Phase6-Scout-H-Plant-Interaction.md):** section 4.1 (table T1) and section 9 (the questions), only for the rows an answer touches.
5. **Code, by range:**
   - `Source/UnnamedVoxel/Blocks/VoxelPlantSurvival.h/.cpp`: the kinds, `HoldsFace`, `Initialize` (S7's `AttachedRule`);
   - `Source/Unnamed/Foundation/FoundationAuthority.cpp`: `CheckPlantPlacement`, `CanPlantSurvive`, `TrimPlantFaces`, `SettlePlants`;
   - `Source/Unnamed/Foundation/FoundationPlantRules.cpp`: placement states;
   - the tests: `FoundationPlantTests.cpp`, and `Blocks.Survival` and `Interaction.Plants` at the end of `FoundationInteractionTests.cpp`.

## 2. Working agreement with this user

As in [the S6b handoff](2026-10-10-Voxel-Handoff-Phase6-S6b.md), section 2. In short:
- the plan goes to the manager first;
- a Sonnet 5.5 review before the commit;
- look calls come with numbers and a control pair;
- context is measured after every build, test run and subagent;
- quiet windows are respected;
- a parallel lane never re-records goldens or world hashes.

## 3. Where things stand

- **One survival table:** `FVoxelPlantSurvival` in the voxel core. S8 wrote the ground plants; S7 added `Attached`, `TagAt`, `Multiface`, `Pointed` and `DripleafStem`. `VoxelBlocks::GetPlantSurvival` builds it once per process. Authorities get it from their owner (`AFoundationPlayground`, `AVoxelWorld::SetupAuthority`, the test fixture `FLab`).
- **Generation:** it reads the same table, but no S7 kind is reached yet. The radius-8 hashes and the plant goldens did not move. Generating vines, lichen, dripleaf and the like (S9, S11) will use S7's kinds: that is the main lane's decided change.
- **The authority:**
  - `CheckPlantPlacement`: faceless multiface states and double plants without room are refused, and so is a state that does not survive (an unknown cell refuses);
  - `SettlePlants`, after every placement and break: the double plant unit, then one level of neighbours (pop, or trim the faces of a multiface block); an unknown cell keeps.
- **Shapes:** per-box outline and collision moved by `FoundationBlockOffsets`, in both worlds.

## 4. S7b: what to do

Apply Jesus's answers (his morning round). Each answer is small; the "built meanwhile" column of the S7 record, section 4, is what each one replaces:

| Question | If (a), as recommended | Where |
|---|---|---|
| Q1 stacks | after the one-level pass, follow same-block vertical chains (sugar cane, cactus, bamboo, kelp, cave vines, dripstone, pale hanging moss, vine under vine, the dripleaf column) to the column's end | `SettlePlants`; a `Blocks.Survival` row (today: "the bottom segment pops, the ones above stay") flips |
| Q2 sturdy faces | dump the sturdy bits (FULL and CENTER per face) through FactsDump, T2's harness; `GetSturdyFaces` reads them | the blocks reference rebuild, then `VoxelPlantSurvival.cpp` (S8's switch); add the leaves, mud, soul sand and snow-8 rows |
| Q3 oracle | the scout's probe in the reference tools, signatures as a facts file, `Blocks.Survival` over the whole matrix | the reference tools, then the test |
| Q4 drops | nothing to do | |
| Q5 snow | `CanBeReplaced` accepts `snow[layers=1]` only; keep `GetDestination` in sync | `FoundationBlockPlacement.cpp`, `FoundationAuthority.cpp:GetDestination`; the snow row in `Blocks.Replaceable` |
| Q6 underwater | nothing to do (corals already need only a floor) | |
| Q7 coral wall fans | the fan item places `*_coral_wall_fan` on a side face (as T2's `PlacesWallBlock` does for the torch) | the item data or a placement rule |
| Q8 vine top | the up face needs the block above's upper face: full collision or sturdy on its Up side (a top slab yes, a bottom slab no); place vines on undersides | `FoundationPlantRules.cpp` (the underside case), `VoxelPlantSurvival.h` (`VineUpFace` becomes a checked face) |

Tests: re-run `Unnamed.Foundation` and `TestVoxel.ps1`, and the radius-8 hashes (S8's plants-on values; they must not move). Gate: the Playground probe's FPS if `SettlePlants` grows (Q1).

## 5. Notes from S7

- **Placement order:** `CheckPlantPlacement` runs before the supply and obstruction checks, so a plant that cannot survive reports `CannotSurvive` first.
- **The Playground floor is not a block:** a plant needs an imported block below (the floor's cells are outside the block world, so unknown).
- **Test registries:** `FLab`'s registry is the imported blocks plus project blocks. The survival table knows only the imported states, so project blocks never hold a plant (use dirt, stone, sand in tests).
- **The gate's A/B** swaps the `UnrealEditor-Unnamed` and `UnrealEditor-UnnamedVoxel` pair together (`C:\dev\sandbox\s7-plants\ab-build.ps1`, `ab-run.ps1`): an old pair built from the old sources plus `FoundationGatherBench.cpp`, which uses only the public block-world API.
- **Mutation scripts:** `C:\dev\sandbox\s7-plants\mutate.py` and `mutrun.ps1` (apply, build, run a filter, revert, rebuild).

## 6. Known issues

| Issue | Due |
|---|---|
| Jesus's questions Q1, Q2, Q3, Q5, Q7, Q8 (S7 record, section 4) | S7b |
| Breaking or popping a water-holding plant leaves air, not its water | S7b, or with water flow |
| Project blocks hold no plant (the survival table covers imported blocks) | When a project block needs to |
| A thick bamboo outline reaches 1/16 block into the next cell at its largest offsets; a ray through that sliver alone misses it | Later (cosmetic) |
| Replacing a tall grass's lower half with tall grass pops the old upper half before the new one is written (an extra event) | Later (cosmetic) |
| The mushrooms ignore light (S8) | With light in survival |
| S7's interaction p50 is +0.003 ms per update (+27%; 0.5 ms budget). Fast paths not taken: cubes keep a single-box path in `GetOutlineBoxes` and `GetCollisionBoxes` (no `GetShapeMin` lookup); a reused (thread_local) outline array in `FoundationTargeting::Resolve`; the model check first in `FoundationPlantRules`'s `IsMultiface`. And `Blocks.GatherBench` needs 2,000 rounds for a clean percentage (200 today) | A follow-up, with a DLL-pair A/B (`C:\dev\sandbox\s7-plants\ab-build.ps1`, `ab-run.ps1`) |
| The rows below are carried over from the S7 handoff | |
| 12 model blocks have no item (vanilla item has another id); 3 have none at all | The inventory pass (answer 7) |
| Plants cast shadows at every distance | Jesus's choice (S6b handoff, section 5) |
| The voxel world's firefly bush draws black with lit fireflies (coplanar overlay, masked in the prepass only) | The morning list: the 1/512-block lift in `FEmitter::Model` |
| Playground water draws every face (z-fights against coplanar solids) | Later |
| 901 ISM section components cost about 0.15 ms render and 0.15 ms game thread | A budget item |
| The Playground's model codes in UV1's second channel reach about 2^21 | A 30-minute follow-up (S7 handoff, section 6) |
| The combat "weak hit inside the target's cooldown" check failed once under load | Watch it |
| The 30-frame jitter shimmer test (S6's G1) | Open |

## 7. The short prompt for S7b

> Voxel world phase 6, session S7b: plant interaction, Jesus's answers. S7 (plant interaction and collision) is done and merged into `feature/voxel-world`.
>
> Read `C:\dev\projects\Unnamed\Docs\Records\2026-10-10-Voxel-Handoff-Phase6-S7b.md` first; its section 1 is the whole reading list, so keep to it. Its section 4 maps each of Jesus's answers to scout H's eight questions to the code it changes.
>
> Apply the answers Jesus gave, and the known issues he picked, as section 4 says. Keep one survival table (`FVoxelPlantSurvival`), and never move the radius-8 hashes or the goldens from a parallel lane. Tests: `Unnamed.Foundation`, `TestVoxel.ps1` and the hashes.
>
> Send the manager a short plan (files and why) before building. Report as the handoff's section 2 says, and measure your context after every build, test run and subagent. End with a handoff into the next session.
