# Voxel world handoff: phase 6, into S6 (block models: rendering) - October 9, 2026

Read this first if you are picking up the block models. Phase 6's S5 (block models: data) is done and committed (`08193eb`) on `feature/voxel-s5`, a parallel lane the night manager ran beside the main lane's S1b and S2. The 83 decoration blocks are in the block reference, `DA_Blocks` and the registry, with baked model quads, per-state shapes and offsets from server facts, and their 71 items with icons. Nothing draws them yet: that is **S6**. Facts were true at `08193eb`; check `git status`, `git log -5` and newer records before trusting them.

**When to run S6:** after S2 has merged into `feature/voxel-world`. S6 changes `Blocks/VoxelBlockTable.*` and `Source/Unnamed/Voxel/VoxelBlocks.cpp`, which S2 also changes, so S6 is not a parallel lane beside S2. Merge `feature/voxel-world` first if your branch is behind.

## 1. Read these, and only these

Budget: about 100k of reading.

1. **Rules:** `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's [CLAUDE.md](../../CLAUDE.md) (its handoff pointer is stale; this handoff wins).
2. **This handoff.**
3. **[The phase 6 plan](2026-10-09-Voxel-Phase6-Plan.md):** lines 79 to 95 (section 2, block gaps), 348 to 379 (S6), 583 to 597 (risks) and 643 to 667 (Jesus's answers).
4. **[Scout D, block models](2026-10-09-Voxel-Phase6-Scout-D-Block-Models.md):** sections 0 and 1 (the recommendations and captures G1-G6), 2.2, 2.4 and 2.5. S5 built R1 (data), R2, R6 (data), R7 (data) and R8; S6 builds R1 (normals), R3, R4, R5, R6 (the hash), R7 (the pick), R9, R10 and R11.
5. **[Docs/Blocks.md](../Blocks.md):** "States and shapes" (the model blocks bullet), "Budgets and known issues".
6. **[The block reference README](../../Reference/minecraft-blocks-26.3/README.md):** "Baked models", "Shapes and offsets", "Tints".
7. **[Docs/VoxelWorld.md](../VoxelWorld.md):** the meshing, chunk component and Lumen card sections only (find them by heading; do not read the file whole).

## 2. Working agreement with this user

Unchanged (see [the S1b handoff](2026-10-09-Voxel-Handoff-Phase6-S1b.md), section 2): the manager (Nyx at night, Hemera by day) confirms a short plan first and approves the local commit after a clean Sonnet 5.5 review and green suites; it pushes, never you. Look calls are delegated to the manager: send captures with numbers, a control pair proving the switch took effect, and a recommendation; keep the other option one switch away. Context checkpoints at about 300k, then every 200k, measured with `MeasureAgentUsage` (section 9). Subagents: Sonnet 5.5 for reviews, tests from a facts file and docs; Haiku 5.5 for log scraping; exact paths and line ranges; stop near 150k.

The machine: Jesus may play Minecraft (`javaw`) on this PC and other lanes run servers and editors. Check `Get-Process java, javaw, UnrealEditor*` before every timing and state the machine beside each figure.

## 3. Where things stand

**The data (S5):**
- **Reference:** `block_states.json` gives each model state a list of parts, each a list of alternatives (random variant lists keep every entry: lily pad 4, sea pickle 4, bamboo's stalk models), each an index into `block_models.json` (303 models, 2,410 quads; faces with no area are left out). Quads are in Mojang block space (x east, y up, z south), 0..1 but not clamped (coral fans reach -0.42 and 1.42, crosses 0.05 to 0.95), in `FaceInfo` vertex order, with UV, texture, `tintindex`, `cull`, `facing` (the declared face after the blockstate rotation), `shade` (`shade_direction_override`, else facing) and `light`; each model has `ao`. The verifier re-bakes all 949 model states from the jar.
- **Facts:** `facts/block_state_facts.json` (FactsDump, runtime public API): per state the outline and collision boxes (without the offset), light, `replaceable`, `occludes` and fluid; per block the offset kind (`xz` 25 blocks, `xyz` 5: short grass, fern, both dry grasses, small dripleaf), the largest horizontal offset (0.25; dripstone and sulfur spike 0.125), the Y range (-0.2 to 0, small dripleaf -0.1 to 0), whether it ignores Y (all do), and whether the outline (bamboo, dripstone, sulfur spike, flowers, propagule ...) and the collision (bamboo, dripstone, sulfur spike) move with it.
- **Registry:** `EFoundationBlockShape::Model`; `GetModelParts`, `GetModelQuads` (`FFoundationModelQuad`: Unreal block space, FaceInfo order, texture index, tint index, cull face, facing, shade, light), `IsModelAmbientOccluded`, `GetOutlineBoxes`, `GetCollisionBoxes`, `GetLightEmission` (per state: sea pickles, glow lichen, cave vines), `GetFluid` (286 water-holding model states: kelp, seagrass, corals, waterlogged ones). Selection and collision of a model state are the bounds of its boxes. The block's offset fields are on `FFoundationBlockType`.
- **Textures:** 689 (690 texture codes of 1,024 with white); 945 array layers of 2,048. `alpha_cutoff_bias` (cactus side and top, kelp, kelp plant) and `strict_cutout` (21) reach the mips; `.Textures` regenerates and compares them.
- **Items:** 71 new block items. 61 draw flat sprites (block textures, or 12 `textures/item` sprites appended to the icon atlas); 10 are block models projected with their gui transform. 12 model blocks have no item yet (their vanilla item has another id; `vanilla_item` records it).

**What does not exist yet:** any mesh for model states. `VoxelBlocks::MakeTable` gives Model states its default branch, so the voxel world holds them as non-solid air with no faces. The Playground draws nothing for them (`UFoundationInteractionView::AddInstance` returns early; the held block hides), and the galleries skip them so their layout is unchanged. Placement gives the default state (rules come with S7). Generation places none of them yet.

## 4. S6: what to do

The plan's S6 (lines 348 to 379), with scout D's recommendations:
- **Core shape:** `EVoxelShape::Model` and the quads in the core block table (`FVoxelStateInfo` or a side table), with the parts and alternatives. Keep the core UObject-free: copy the registry's quads in `VoxelBlocks::MakeTable`.
- **Winding:** Mojang to Unreal swaps Y and Z, which flips handedness: FaceInfo's counter-clockwise order becomes clockwise in Unreal. Check against the existing cube faces (and the RMC winding note in memory) before trusting any quad.
- **Normals (R1):** from `Shade`, through the existing face-index normal path. S5 turns `shade_direction_override` with the blockstate rotation, as it turns faces; so a ceiling vine, glow lichen or sculk vein (x-rotated) is shaded as south, while wall variants keep up. Whether the override should stay unrotated is our own reading either way (the format does not say): look at a ceiling glow lichen beside a wall one and make it a look call if it matters; the change is one line in `bake_models.py` (`turn(override)` to `override`).
- **Thin planes (R3):** every zero-thickness element already has both faces; keep `M_ChunkCutout` one-sided.
- **Lumen (R4):** never card model quads; decide ray-tracing membership with capture G3.
- **Own section (R5):** model quads in their own RMC section (same cutout material), `voxel.Models 0` for A/B, and a GPU gate (G2).
- **Offsets (R6):** our own hash of (x, z) with a salt and no Y, scaled by the block's `OffsetMaxHorizontal` and `OffsetMinVertical` (XYZ: Y in [OffsetMinVertical, 0]). Test: (x, y, z) and (x, y + 1, z) get the same offset.
- **Alternatives (R7):** pick one per part by a position hash (all weights are equal).
- **AO (R9):** models with `ao: false` stay open; for AO models, faces flush with a cell side (within 1/16) take that side's corner AO.
- **Tints:** `tint >= 0` takes the block's tint source. `VoxelBlocks::GetTintSource` maps colours: leaf litter's dry foliage (plains `#A37546`) would read as Constant, so add `EVoxelTintSource::DryFoliage` with the `dry_foliage` colormap (in the reference's assets) through the biome tint code. Lily pad is the constant `#208030`. Pink petals and wildflowers tint index 1 (the stems) only; the data already drops their index 0.
- **Emission:** firefly bush's overlay quads carry light 15 (vertex G).
- **Water:** mesh water around water-holding model states (`GetFluid`).
- **Playground:** one runtime mesh per baked model; remove the S5 skips (`AddInstance`, the held block, the galleries) and bring the new blocks into the tour galleries.
- **Evidence and gates:** the plan's, plus scout D's G1-G6 and R11 (quads and bytes per column next to mesh time).

## 5. Notes from S5

- **Decisions (Nyx, under Jesus's delegation):**
  - Lily pad tint `#208030` from minecraft.wiki "Lily Pad", Appearance section (no game code read); the other tint sources from minecraft.wiki "Tint". The lily pad item's GUI tint `#71C35C` is data: the jar's `items/lily_pad.json` (a constant tint, -9321636), and the same "Lily Pad" page gives it ("In the inventory, lily pads have the color: #71c35c").
  - The import re-saved assets it did not change by hand: the 8 block and chunk materials (`CreateBlockMaterials.py` rebuilds their graphs on every run) and `T_WaterFlowTextures` (rebuilt by `ImportBlocks.py` with the same 48 layers). Their diffs are re-saves by the official scripts, not edits.
  - `creative_tabs.json` cannot be re-dumped byte-identically: Java object hashes in potion patches (`Potion@39ac875`) change per run. The accepted file stays as the input; the rest of the dump is identical.
  - The 10 block-model item icons are projected from the baked quads (vanilla draws those items in 3D).
  - Placed model blocks are invisible, with facts outline and collision, until S6.
- **R8 corrected:** face codes hold a texture index (10 bits), not an array layer, so the budget is texture count (690 of 1,024); layers have their own limit (945 of 2,048).
- **Facts are black-box:** `FactsDump.java` samples `getOffset` over 4,096 columns and compares shapes at two columns; it calls only public state methods.
- **The unity build:** the grown block tests shifted the unity blobs; `FoundationInteractionTests.cpp`'s namespace-scope `Off` and `Down` then clashed (C4459) with three other test files. They are `OffHand` and `DownVector` now.

## 6. Known issues, each with when it is due

| Issue | Due |
|---|---|
| Model states draw nothing (Playground, voxel world); galleries skip them | S6 |
| Shelf mushroom's two textures are 32 x 32; the 16 x 16 array holds their vanilla mip 1 | S6 decides (a 32 x 32 array, or keep) |
| Model blocks place their default state (no support, facing or stacking rules) | S7 |
| 12 model blocks have no item (vanilla item has another id); 3 none at all | The inventory pass (Jesus's answer 7) |
| `replaceable` comes from the facts for model blocks only; cubes keep the old rule (water only) | When placement next changes |
| Sculk vein's icon shows its first animation frame (vanilla animates it) | Later, minor |
| The others in [the S1b handoff](2026-10-09-Voxel-Handoff-Phase6-S1b.md), section 6 | As listed there |

**Questions for Jesus** (`CLAUDE.md` lines no worker may edit; recommendation first):
1. **Block count:** `CLAUDE.md` does not count blocks, so nothing there changes for S5. The Commands block's `TestVoxel.ps1` count ("61") is still stale (66); recommend dropping the number.
2. **Facts path:** the reference rule names `tools/WorldgenFacts.java` for the worldgen facts only; the blocks reference's facts come from `tools/FactsDump.java` and `tools/build_reference.py`, as its README says. Recommend no change.

## 7. Code map: S5

| Area | Files |
|---|---|
| Reference tools | `Reference/minecraft-blocks-26.3/tools/`: `bake_models.py` (new), `build_reference.py`, `build_manifest.py` (the model groups), `FactsDump.java` (per-state facts), `verify_reference.py` (re-bake, facts, items, budgets), `doc_template.md`; `README.md` |
| Import | `Scripts/Editor/BlockImportData.py` (quads, parts, facts, icons), `ImportBlocks.py`; `FoundationBlockImport.*` (bias, 32 x 32 mip 1, item sprites in the atlas, `ReadLayerMip`, `DecodePngFile`) |
| Registry | `FoundationBlockAssets.h`, `FoundationBlockRegistry.*` |
| Playground and HUD | `FoundationInteractionView.cpp`, `FoundationPlayground.cpp`, `FoundationTourProbe.cpp` (shots 22b-22d), `Hud/HudCreative.*`, `HudCreativeScreen.cpp` |
| Tests | `FoundationBlocksTests.cpp` (`.Registry`, `.Codec`, `.Faces`, `.Textures`, `.Shapes`, new `.Models`), `FoundationInteractionTests.cpp` (renames) |
| Docs | `Docs/Blocks.md`, `Docs/MinecraftBlockReference.md` (regenerated; the hand-edit issue is closed), scout D in `Docs/Records` |

## 8. What worked and what failed in S5

- **Worked:** rebuilding the reference into a moved-aside folder and comparing file by file (1,231 old files byte-identical); a re-bake in the verifier; mutation checks per test (one UV, one facts box, one bias); the tour's scrolled creative shots for evidence.
- **Failed first:** `-ExecCmds=quit` does nothing in a NullRHI editor run (end with an automation test and `-TestExit`); `-script=` relative paths resolve against the engine's binaries (use absolute paths); `&Array[Num]` asserts even for an empty append (use `GetData() + offset`).

## 9. Commands and tokens

```powershell
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-blocks-26.3/tools/verify_reference.py
uv run --no-project --python 3.13 python -I Scripts/Editor/BlockImportData.py [--write-tags]
& C:\dev\sandbox\s5work\gate.ps1 -Label <name> [-SkipTestVoxel]      # machine, editor start, TestVoxel, radius-8 hashes x3 presets
& C:\dev\sandbox\s5work\runtests.ps1 -Label <name> [-Filter Unnamed.Foundation.Blocks+Unnamed.Hud]
& C:\dev\sandbox\s5work\import.ps1                                   # tags, move old assets aside, ImportBlocks, materials (only for a reimport)
node Scripts\MeasureAgentUsage.mjs --session <id>                    # with CLAUDE_CONFIG_DIR pointing at a copy (see C:\dev\sandbox\phase5s6\measure.ps1)
```

FactsDump: JDK 25 at `C:\Users\jherediaramirez\.gradle\jdks\eclipse_adoptium-25-amd64-windows.2`, classpath `C:\dev\sandbox\mc-26.3-blocks\datagen\versions\26.3\server-26.3.jar` plus `datagen\libraries\**\*.jar`; S5's run folder is `C:\dev\sandbox\s5work\factsrun` (block id list `ids.txt`). The research copy S5 rebuilt from is `C:\dev\sandbox\s5work\research` (sandboxes are disposable; the tools in `Reference/.../tools` are the record).

**S5's figures** (machine: a `java` server from the main lane at times, no `javaw`): radius-8 hashes identical before and after the import in both load orders (overworld `0xeb1c3d76838e2481`, large_biomes `0xcb42685280f8b57c`, amplified `0xc4403ff66465fcf0`); editor start (NullRHI, one test) 20.3 s before and after; import 14.7 s plus materials 13.6 s (the final import, after the review fixes; the first took 14.2 s); `DA_Blocks` 968,269 to 1,836,583 bytes, `T_BlockTextures` 330,662 to 403,778, `T_BlockIcons` 234,026 to 273,941.

**S5's cost** (session `8f11be6a-d9c0-5a50-9d9d-69865d74307f`):

| Agent | Peak context | Processed | Output |
|---|---|---|---|
| Lead (Opus 5.5) | 513.9k at the pre-commit report (352.5k at the 300k checkpoint, 463.6k before the review) | 74.9M (74.2M cache reads) | 232k |
| Tests from a facts file (Sonnet 5.5) | 121.8k | 1.50M | 8.1k |
| Doc template (Sonnet 5.5) | 71.6k | 1.00M | 1.6k |
| Review, data pipeline (Sonnet 5.5) | 146.8k | 3.01M | 0.2k |
| Review, C++ (Sonnet 5.5) | 105.9k | 0.98M | 0.2k |

## 10. The short prompt for S6

> Voxel world phase 6, session S6: block models, rendering. S5 (block models: data) ran in a parallel lane and is merged into `feature/voxel-world`. Run S6 only after S2 has merged, because both change `VoxelBlockTable` and `VoxelBlocks.cpp`.
>
> Read `C:\dev\projects\Unnamed\Docs\Records\2026-10-09-Voxel-Handoff-Phase6-S6.md` first; its section 1 is the whole reading list (with line ranges), so keep to it.
>
> Build S6 as the handoff's section 4 says: `EVoxelShape::Model` with the registry's baked quads in the core block table; the mesher appends model quads into their own cutout section (`voxel.Models 0` for A/B) and drops faces whose `cullface` neighbour is opaque; normals from the shade direction; offsets from our own hash of (x, z), without Y; one alternative per part by position hash; the AO rule; tints, including dry foliage; emission; water around water-holding states; one runtime mesh per baked model in the Playground, with S5's skips removed. Generated worlds keep their hashes and goldens.
>
> Send the manager a short plan (files and why) before building. Captures for look calls carry numbers and a control pair; scout D's GPU and Lumen captures (G1-G6) are part of the evidence. Report as the handoff's section 2 says, and measure your context at each checkpoint. End with a handoff into S7 (plant interaction and collision).
