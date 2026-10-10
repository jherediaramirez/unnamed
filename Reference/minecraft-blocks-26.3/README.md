# Java 26.3 block reference

The import input for the block system: every full-cube block and slab of vanilla Java 26.3, plus the two blocks the world generator's surface rules and snow line need, powder snow and snow layers, plus the 83 blocks decoration places that are drawn from their own models (614 blocks: 428 cubes, 101 slabs, powder snow, snow and 83 model blocks), and the fluids water and lava. It holds their ids, states, per-state faces or baked models, per-state shapes and behaviour, textures, mining facts, tints, block items and creative tab positions. The research behind it is summarized in [Docs/MinecraftBlockReference.md](../../Docs/MinecraftBlockReference.md). This folder is generated, hash-listed and never edited by hand.

| Source | SHA-1 |
|---|---|
| Client `%APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar` (same client as `Reference/minecraft-hud-26.3`) | `e877b6a07acd633fb3bb475002175cec036e7b87` |
| Official server jar (data-generator reports, runtime facts) | `33680f5f2ac32864d6d7cf5e56a705fdb3e05f4c` |

```powershell
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-blocks-26.3/tools/verify_reference.py
```

The verifier checks every file against `SHA256SUMS.txt`, compares every `assets/` file with its jar entry when the jar is present, compares every state id and property set with Mojang's report, and re-decodes every texture frame against its recorded RGBA hash.

## Files

| Path | What it is |
|---|---|
| `block_manifest.json` | One entry per block in registry order. It has the research fields (ids, faces, render layer, flags, tags, where found) plus `name`, `hardness`, `blast_resistance`, `requires_correct_tool_for_drops`, `light_emission`, `facts_source`, `tint`, `material_layer` and `random_variants`; model blocks also have `vanilla_item` (their item id, or none). Water and lava are under `fluids` (lava: opaque, light 15). |
| `block_states.json` | Every state of every imported block (4,419): the 26.3 state id, its properties, `shape` (`full`, `slab_bottom`, `slab_top`, `fluid`, `layers_1` to `layers_7` for snow layers k/8 high, and `model`) and six faces, or for `model` states its parts. See [Faces](#faces) and [Baked models](#baked-models). |
| `block_models.json` | The 303 distinct baked models the model states use: an ambient occlusion flag and quads. See [Baked models](#baked-models). |
| `textures.json` | The 689 textures: size, frame size and count, animation (`frametime`, frame order and times, `interpolate`), `mipmap_strategy`, `alpha_cutoff_bias` when the `.mcmeta` sets it, `alpha` class, PNG SHA-256, the target array and first layer, and the SHA-256 of every frame's RGBA bytes. It also lists the plains tint colors. |
| `items.json` | The 600 block items (item id = block id): English name, stack size, item model, GUI tint, and positions in every creative tab. Model blocks' items add an `icon`: flat `sprite` layers, or a baked block `model` with its gui display transform. Powder snow has none, because its vanilla item is `powder_snow_bucket`; nor do 12 model blocks: 9 whose vanilla item has another id (cocoa: `cocoa_beans`, the wall fans, ...) and 3 with no item (kelp plant, cave vines plant, tall seagrass). Also the 14 creative tabs: row, column, type, alignment and icon. |
| `assets/minecraft/` | Jar files, byte for byte: block textures and `.mcmeta`, the grass, foliage and dry foliage colormaps, the creative inventory GUI art, tab icon sprites and the model blocks' item sprites, item definitions, and the `block`/`generated`/`handheld` item display models. |
| `facts/` | `block_facts.csv` (hardness, blast resistance, correct-tool flag, light, item, English name for all 1,286 blocks) and `creative_tabs.json` (every tab's display and search order), dumped by `tools/FactsDump.java`; and `block_state_facts.json`, per imported state: outline and collision boxes, light, `replaceable`, occluding faces and fluid, plus each block's position offset (see [Shapes and offsets](#shapes-and-offsets)). |
| `reports/blocks-imported.json` | Mojang's `blocks.json` report entries for the imported blocks: the state-id oracle. |
| `research/` | The research outputs as generated: the original manifest, `blocks_registry.csv` (all 1,286 blocks), `block_tags.csv` (resolved tags), `edge_cases.txt` (excluded cube-like blocks), water and worldgen notes. |
| `tools/` | `build_reference.py` (this folder), `bake_models.py` (its model baker), `verify_reference.py`, `FactsDump.java`, and the research generators (`build_manifest.py`, `classify_blocks.py`, `build_blocks.py`, `build_worldgen.py`, `doc_template.md`). |

## Where each value comes from

- **Ids and states:** the server's data-generator report. State ids follow Mojang's enumeration: a cartesian product of the properties in report order, last property fastest, values in declared order. `build_reference.py` asserts this for every state, so runtime code can rebuild every id from the property schema.
- **Hardness, blast resistance, correct tool, light, names, creative tabs:** the 26.3 server's own registries at runtime. `FactsDump.java` loads the vanilla data pack the way a dedicated server does, binding components and tags, then reads `Block.defaultDestroyTime()`, `getExplosionResistance()`, `BlockState.requiresCorrectToolForDrops()` and `CreativeModeTab.buildContents()`. Spot checks: stone 1.5/6.0 needs a tool, obsidian 50/1200, bedrock -1, oak slab 2/3.
- **Faces:** the client's blockstate and model JSON, baked with the rules of 26.3 `FaceBakery` (from javap):
  - vertices from `FaceInfo`;
  - default UVs from `defaultFaceUV`;
  - UV vertex selection `(i + quadrant) % 4`;
  - blockstate rotation X then Y about the block centre;
  - the first entry of weighted variant lists.
  Only the three mushroom blocks use `uvlock`; their faces keep the unrotated mapping.
- **Blocks the classifier leaves out:** the research's `build_manifest.py` adds powder snow and snow, which the world generator places, as it adds the mushroom blocks. Powder snow's model is six thin face plates, so only each plate's outward face is baked and the shape is `full`. Snow layers 1 to 7 are boxes k/8 high (`layers_<k>`) and layers 8 is the snow block's cube; 26.3's layer models also hold an inverted inner box whose face shows from inside, which is skipped. The importer does not draw faces seen from inside a block.
- **Model blocks:** the research's `build_manifest.py` lists the 83 blocks by group (crosses, tinted and double crosses, seagrass, flowerbeds, carpets, multiface and custom models) with the face textures their models use. Chest and spawner (block entities) and the sculk shrieker wait.
- **Fluids:** water and still lava. The 32 x 32 flow textures (`lava_flow`, then `water_flow`, in name order like every texture) share the array still named `water_flow`. The shelf mushroom's two still 32 x 32 textures stay in the block array; the importer stores their mip 1 there.
- **Tints:** 26.3 `BlockColors.createDefault`:
  - grass block: grass colormap;
  - oak, jungle, acacia, dark oak and mangrove leaves: foliage colormap;
  - spruce leaves `#619961`, birch leaves `#80A755`;
  - water: biome water color.

  The model blocks' sources come from minecraft.wiki "Tint" and "Lily Pad" (game code is not read):
  - short grass, fern, tall grass, large fern, bush and sugar cane: grass colormap; pink petals and wildflowers: grass, on their stems only (`tintindex` 1);
  - vine: foliage colormap; leaf litter: dry foliage colormap;
  - lily pad `#208030`.
  A model `tintindex` without a registered source renders untinted, which applies to cherry, pale oak, azalea and poplar leaves. Colormap colors use the plains biome (temperature 0.8, downfall 0.4): grass `#91BD59`, foliage `#77AB2F`, water `#3F76E4`, dry foliage `#A37546`. Item tints come from each `items/<id>.json`; the grass block item is `#7CBD6B`.
- **Material layer:**
  - translucent when the model forces it (glass) or any face texture has partial alpha;
  - cutout when a face texture has fully transparent pixels;
  - otherwise opaque.
  The grass block's tinted side overlay is composited inside the opaque material.

## Baked models

`tools/bake_models.py` bakes every state of a model block from the client's blockstate and model JSON (minecraft.wiki "Model" and "Blockstates definition"):

- **Parts and alternatives:** a state is a list of parts (one for `variants`, one per matching multipart case), each a list of alternatives, each an index into `block_models.json`. Random variant lists keep every entry (lily pad, sea pickle and bamboo; all equal weight), for the mesher to pick by position.
- **Quads:** per element face with an area, 4 vertices in `FaceInfo` order in Mojang block space (0..1), the UV per vertex (0..1, with the face's UV rotation), the texture, `tintindex` (-1 when the block has no tint source for it), `cull` (the side whose full neighbour hides it, or null), `facing` (the declared face after the blockstate rotation, so the axis nearest the normal or one of two tied ones), `shade` (`shade_direction_override`, else `facing`) and `light` (`light_emission`). Each model carries its `ambientocclusion` flag.
- **Element rotation:** about the element's origin, right-handed about the positive axis; `rescale` stretches the two other axes by 1 / cos(angle).
- **Blockstate rotation:** X then Y about the block centre, as for faces, turning `cull` and `shade` with it.
- **uvlock:** a face's UV in model space is one of the 8 transforms of the canonical UV; after the rotation it keeps that transform on the canonical UV of its new direction, so the texture stays aligned with the world.

The verifier re-bakes every model state from the jar and compares.

## Shapes and offsets

`facts/block_state_facts.json` is measured by `FactsDump.java` through the 26.3 server's public block state methods at runtime, never from models: `getShape` and `getCollisionShape` (an empty world at the origin), `getLightEmission`, `canBeReplaced`, `getFluidState`, and per face whether `getFaceOcclusionShape` is a full square (`occludes`, a bit per face in `face_order`). A block with an offset function gets `offset`: its kind (`xz`, or `xyz` when Y moves), the largest horizontal offset and the Y range seen over 4,096 columns, whether it ignores Y, and whether the outline or collision box moves with it. Moving boxes are stored without the offset of the probe position.

## Budgets

Face codes hold a texture index in 10 bits, and the importer's white texture takes index 0: 690 of 1,024 are used. The block array holds 944 frames plus the white layer, 945 of Unreal's 2,048 texture array slices. The verifier asserts both.

## Faces

`block_states.json` lists faces in `face_order` (`west, east, north, south, down, up`; the project's `EFoundationFace` order). Each face is `{"t": texture, "x": uv transform, "tint": tintindex or -1, "cull": bool}`, with an optional `"overlay": {"t", "tint"}` that shares the face's transform.

Every mesh face carries the UV an unrotated full cube would show at that point, in block-relative coordinates. Mojang axes are x east, y up, z south:

| Face | u | v |
|---|---|---|
| down | x | 1 - z |
| up | x | z |
| north | 1 - x | 1 - y |
| south | x | 1 - y |
| west | z | 1 - y |
| east | 1 - z | 1 - y |

A half-height slab therefore samples half the texture. The transform `x` (0-7) maps that UV to the texture UV:
- bit 2 mirrors U first;
- bits 0-1 then turn `(u, v) -> (v, 1 - u)` that many times.

## Rebuild

`build_reference.py` needs the pinned jars, the research work folder (`C:\dev\sandbox\mc-26.3-blocks`, regenerated by `tools/build_manifest.py` and its siblings) and the `FactsDump` output. It refuses to run unless this folder holds only `tools/`, so to rebuild, move the accepted folder away first. After a README or tool edit, `build_reference.py --sums-only` rewrites only `SHA256SUMS.txt`.

Minecraft art and data remain Mojang/Microsoft material. Local development builds may use them; anything distributed must replace them.
