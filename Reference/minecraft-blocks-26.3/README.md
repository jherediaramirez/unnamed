# Java 26.3 block reference

The import input for the block system: every full-cube block and slab of vanilla Java 26.3, plus the two blocks the world generator's surface rules and snow line need, powder snow and snow layers, plus the 83 blocks decoration places that are drawn from their own models, plus the torch and wall torch, plus the 310 survival family blocks of session B1 (926 blocks: 428 cubes, 101 slabs, powder snow, snow and 395 model blocks), and the fluids water and lava. It holds their ids, states, per-state faces or baked models, per-state shapes and behaviour, textures, mining facts, tints, block items and creative tab positions. The research behind it is summarized in [Docs/MinecraftBlockReference.md](../../Docs/MinecraftBlockReference.md). This folder is generated, hash-listed and never edited by hand.

| Source | SHA-1 |
|---|---|
| Client `%APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar` (same client as `Reference/minecraft-hud-26.3`) | `e877b6a07acd633fb3bb475002175cec036e7b87` |
| Official server jar (data-generator reports, runtime facts) | `33680f5f2ac32864d6d7cf5e56a705fdb3e05f4c` |
| Asset index 34 (the client's `assetIndex`; `sounds.json` and the `.ogg` files) | `abfaa525f923f807df8b4e4d29c1b5e3a104adbe` (official). The local Modrinth copy `%APPDATA%\ModrinthApp\meta\assets\indexes\34.json` differs from the official one only in 142 `minecraft/lang/` entries, so the tools pin the SHA-1 of the other entries (canonical JSON, sorted keys, compact separators): `9ba09e4fc9e2245b7395eee4732096038d9f6e2d`. |

```powershell
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-blocks-26.3/tools/verify_reference.py
```

The verifier checks every file against `SHA256SUMS.txt`, compares every `assets/` file with its jar entry when the jar is present (`sounds.json` and `sounds/` instead against the asset index objects: SHA-1 equals the object hash, size matches, when the index is present and its non-lang pin holds), compares every state id and property set with Mojang's report, re-bakes every model state and re-derives the chest stand-in tiles from the jar, and re-decodes every texture frame against its recorded RGBA hash. It also checks the survival tables against each other and the frozen base list, the use sound events against `sounds.json`, and that no item id here is also in `Reference/minecraft-items-26.3/items.json`.

## Files

| Path | What it is |
|---|---|
| `block_manifest.json` | One entry per block in registry order. It has the research fields (ids, faces, render layer, flags, tags, where found) plus `name`, `hardness`, `blast_resistance`, `requires_correct_tool_for_drops`, `light_emission`, `facts_source`, `tint`, `material_layer` and `random_variants`; model blocks also have `vanilla_item` (their item id, or none). The B1 blocks carry `added: "survival-B1"` and the three chests `stand_in: true`. Water and lava are under `fluids` (lava: opaque, light 15). |
| `block_states.json` | Every state of every imported block (28,552): the 26.3 state id, its properties, `shape` (`full`, `slab_bottom`, `slab_top`, `fluid`, `layers_1` to `layers_7` for snow layers k/8 high, and `model`) and six faces, or for `model` states its parts. See [Faces](#faces) and [Baked models](#baked-models). |
| `block_models.json` | The 4,867 distinct baked models the model states use: an ambient occlusion flag and quads. See [Baked models](#baked-models). |
| `textures.json` | The 965 textures: size, frame size and count, animation (`frametime`, frame order and times, `interpolate`), `mipmap_strategy`, `alpha_cutoff_bias` when the `.mcmeta` sets it, `alpha` class, PNG SHA-256, the target array and first layer, and the SHA-256 of every frame's RGBA bytes. The 18 chest stand-in tiles add `file` (their path under `stand_ins/`) and `derived_from`. It also lists the plains tint colors. |
| `items.json` | The 887 block items (item id = block id): English name, stack size, item model, GUI tint, and positions in every creative tab. Model blocks' items add an `icon`: flat `sprite` layers (117), or a baked block `model` with its gui display transform (241). The 16 beds' vanilla items are `minecraft:composite` (the head model, and the foot model translated one block south): their icon is one baked model of both, at the gui display both share (`block/template_bed`). 39 blocks have no item here: powder snow (its vanilla item is `powder_snow_bucket`), frosted ice (no item), and 37 model blocks: 21 whose vanilla item has another id (the wall torch: `torch`, cocoa: `cocoa_beans`, the wall fans, sweet berry bush, cave vines, big dripleaf stem, the crops and stems, the water, lava and powder snow cauldrons: `cauldron`), 7 with no item (kelp plant, cave vines plant, tall seagrass, weeping and twisting vines plant, fire, soul fire), and the 9 saplings, whose items live in the item reference. Also the 14 creative tabs: row, column, type, alignment and icon. |
| `stand_ins/` | The 18 chest stand-in tiles (16 x 16 PNG): derived pixels, not jar files. See [Chest stand-in](#chest-stand-in). |
| `assets/minecraft/` | Jar files, byte for byte: block textures and `.mcmeta`, the grass, foliage and dry foliage colormaps, the creative inventory GUI art, tab icon sprites and the model blocks' item sprites, item definitions, and the `block`/`generated`/`handheld` item display models. |
| `facts/` | `block_sounds.csv` (each imported block's sound type: volume, pitch and the break, hit, place, step and fall event paths without namespace), `block_facts.csv` (hardness, blast resistance, correct-tool flag, light, item, English name for all 1,286 blocks) and `creative_tabs.json` (every tab's display and search order), dumped by `tools/FactsDump.java`; and `block_state_facts.json`, per imported state: outline and collision boxes, light, `replaceable`, occluding faces and fluid, plus each block's position offset (see [Shapes and offsets](#shapes-and-offsets)); and `block_support.json`, per imported state the faces that hold a torch (see [Torch support](#torch-support)); and `plant_survival.json`, per state of the frozen base blocks which neighbouring blocks let it survive, and `family_survival.json`, the same for the rule-bearing states of every imported block (see [Plant survival](#plant-survival)); and `block_use_sounds.csv`, the open/close and click events of doors, trapdoors, fence gates, buttons, pressure plates and the lever (see [Use sounds](#use-sounds)). |
| `block_particles.json` | Per imported block (registry order), the particle texture of the default state and, only where it differs, of individual state ids; water and lava use their still texture. The top-level `textures_outside_textures_json` lists particle textures that are not in `textures.json` (`bamboo_fence_particle`, `bamboo_fence_gate_particle`, and `shelf_mushroom_particle`, states 11227-11234 of `shelf_mushroom`); their files are in `assets/minecraft/textures/block/`. |
| `assets/minecraft/sounds.json` | The client's sound event table (635,867 bytes), from the asset index, not the jar. |
| `assets/minecraft/sounds/` | 784 `.ogg` files (7,021,366 bytes): the break and hit sounds of the imported blocks' sound types, from the asset index. Step, place, fall and use sounds are not included, only their event names in `facts/block_sounds.csv`, `facts/block_use_sounds.csv` and `sounds.json`. |
| `assets/minecraft/textures/block/destroy_stage_0.png` to `destroy_stage_9.png` | The ten block-breaking crack overlays (jar files, 1,412 bytes in all); not in `textures.json`. |
| `assets/minecraft/textures/block/shelf_mushroom_particle.png`, `bamboo_fence_particle.png`, `bamboo_fence_gate_particle.png` | Particle textures of `shelf_mushroom`, `bamboo_fence` and `bamboo_fence_gate` (jar files, 200, 153 and 153 bytes); not in `textures.json`, listed in `block_particles.json`. |
| `reports/blocks-imported.json` | Mojang's `blocks.json` report entries for the imported blocks: the state-id oracle. |
| `research/` | The research outputs as generated: the original manifest, `blocks_registry.csv` (all 1,286 blocks), `block_tags.csv` (resolved tags), `edge_cases.txt` (excluded cube-like blocks), water and worldgen notes. |
| `tools/` | `build_reference.py` (this folder), `bake_models.py` (its model baker), `verify_reference.py`, `FactsDump.java`, `survival_base_ids.txt` (the frozen subject list of `plant_survival.json`: the 616 blocks imported before B1 plus water and lava, registry order), and the research generators (`build_manifest.py`, `classify_blocks.py`, `build_blocks.py`, `build_worldgen.py`, `doc_template.md`). |

## Where each value comes from

- **Ids and states:** the server's data-generator report. State ids follow Mojang's enumeration: a cartesian product of the properties in report order, last property fastest, values in declared order. `build_reference.py` asserts this for every state, so runtime code can rebuild every id from the property schema.
- **Hardness, blast resistance, correct tool, light, names, creative tabs:** the 26.3 server's own registries at runtime. `FactsDump.java` loads the vanilla data pack the way a dedicated server does, binding components and tags, then reads `Block.defaultDestroyTime()`, `getExplosionResistance()`, `BlockState.requiresCorrectToolForDrops()` and `CreativeModeTab.buildContents()`. Spot checks: stone 1.5/6.0 needs a tool, obsidian 50/1200, bedrock -1, oak slab 2/3.
- **Sound types:** the server's own `BlockBehaviour.getSoundType` through the public API, read by `FactsDump.java` into `facts/block_sounds.csv`; it fails unless every state of a block shares the default state's sound type.
- **Sounds:** asset index 34. `sounds.json` and the break and hit `.ogg` files of the imported blocks' sound types, each checked against the index SHA-1 and size.
- **Particle textures:** the jar's block models: the `particle` variable of the model each state uses (first applied variant or part, first entry of weighted lists), through parent chains.
- **Destroy stages:** the jar (`destroy_stage_0` to `destroy_stage_9`).
- **Faces:** the client's blockstate and model JSON, baked with the rules of 26.3 `FaceBakery` (from javap):
  - vertices from `FaceInfo`;
  - default UVs from `defaultFaceUV`;
  - UV vertex selection `(i + quadrant) % 4`;
  - blockstate rotation X then Y about the block centre;
  - the first entry of weighted variant lists.
  Only the three mushroom blocks use `uvlock`; their faces keep the unrotated mapping.
- **Blocks the classifier leaves out:** the research's `build_manifest.py` adds powder snow and snow, which the world generator places, as it adds the mushroom blocks. Powder snow's model is six thin face plates, so only each plate's outward face is baked and the shape is `full`. Snow layers 1 to 7 are boxes k/8 high (`layers_<k>`) and layers 8 is the snow block's cube; 26.3's layer models also hold an inverted inner box whose face shows from inside, which is skipped. The importer does not draw faces seen from inside a block.
- **Model blocks:** the research's `build_manifest.py` lists the 83 blocks by group (crosses, tinted and double crosses, seagrass, flowerbeds, carpets, multiface and custom models) with the face textures their models use. The spawner (a block entity) and the sculk shrieker wait. The torch group (`torch`, `wall_torch`) came later (torch session T2), then the survival families (session B1, see [Family blocks (B1)](#family-blocks-b1)).
- **Appended blocks:** `build_reference.py`'s `APPENDED_BLOCKS` lists the blocks added after the first ordering was fixed (`torch`, `wall_torch`, then the 310 B1 blocks in family order). Their new textures follow every earlier texture and their baked models every earlier model, item icons included, so adding blocks never moves an existing texture index, array layer or model index; blocks, states, items and facts stay in registry order. The verifier asserts it. Later blocks join the end of the list.
- **Fluids:** water and still lava. The 32 x 32 flow textures (`lava_flow`, then `water_flow`, in name order like every texture) share the array still named `water_flow`. The shelf mushroom's two still 32 x 32 textures stay in the block array; the importer stores their mip 1 there.
- **Tints:** 26.3 `BlockColors.createDefault`:
  - grass block: grass colormap;
  - oak, jungle, acacia, dark oak and mangrove leaves: foliage colormap;
  - spruce leaves `#619961`, birch leaves `#80A755`;
  - water: biome water color.

  The model blocks' sources come from minecraft.wiki "Tint" and "Lily Pad" (game code is not read):
  - short grass, fern, tall grass, large fern, bush and sugar cane: grass colormap; pink petals and wildflowers: grass, on their stems only (`tintindex` 1);
  - vine: foliage colormap; leaf litter: dry foliage colormap;
  - lily pad `#208030`;
  - pumpkin and melon stems: tint kind `stem_age`, the colour of the state's `age` (red age x 32, green 255 - age x 8, blue age x 4, the wiki "Tint" formula; provisional until checked in game). `color` is age 0 and `color_by_age` lists ages 0 to 7;
  - water cauldron: the water colour.
  A model `tintindex` without a registered source renders untinted, which applies to cherry, pale oak, azalea and poplar leaves, and to the lava and powder snow cauldrons. Colormap colors use the plains biome (temperature 0.8, downfall 0.4): grass `#91BD59`, foliage `#77AB2F`, water `#3F76E4`, dry foliage `#A37546`. Item tints come from each `items/<id>.json`; the grass block item is `#7CBD6B`.
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

A wall with no post and no side (`up=false`, every side `none`) matches no multipart case and draws nothing, as in vanilla: its state has an empty part list.

The verifier re-bakes every model state from the jar and compares.

## Family blocks (B1)

Survival session B1 (track T5) appended 310 blocks in 18 groups, listed in `tools/build_manifest.py`: stairs, fences, fence gates, walls, panes (iron and copper bars, glass panes), doors, trapdoors, buttons, the lever, pressure plates, the ladder, nether vines, beds, chests, farmland and dirt path, crops, saplings, and the cauldrons and fires (campfires included). Their manifest entries carry `added: "survival-B1"`, the importer's flag for the appended families.

- **Saplings:** their items belong to the item reference (`build_reference.py`'s `ITEMS_REFERENCE_ITEMS`), so they have no item here, only `vanilla_item`. No item id is in both references.
- **Crops and stems:** no item of their own id; `vanilla_item` names the seed or produce (wheat: `wheat_seeds`, carrots: `carrot`, potatoes: `potato`, beetroots: `beetroot_seeds`, pumpkin and melon stems: their seeds, torchflower crop: `torchflower_seeds`, pitcher crop: `pitcher_pod`).
- **Cauldrons:** the water, lava and powder snow cauldrons have `vanilla_item` `cauldron`. Fire, soul fire and the two vines plants have no item.

## Chest stand-in

Vanilla draws the chest, trapped chest and ender chest with a block entity; their blockstates name only a particle model. Here each draws one box from [1,0,1] to [15,14,15] (vanilla's footprint and height), front on north, turned by `facing`; `type` left and right draw the same single box. The item icon is the same baked box.

The box's six faces use 16 x 16 tiles cut from the 64 x 64 entity sheets `textures/entity/chest/normal.png`, `trapped.png` and `ender.png` in the jar. Each crop is flipped top to bottom (the sheet is drawn upside down); a side tile stacks the lid strip over the base strip, without the base row the lid overlaps. The 14 x 14 crop sits at (1,1) and its edge pixels fill the 1-pixel border, so the face UV [1,1,15,15] shows exactly the crop. The crop rectangles and steps are in each tile's `derived_from` in `textures.json`.

These are the first derived pixels in this folder, so they live outside `assets/`, in `stand_ins/`. `build_reference.py`'s `stand_in_pixels` makes them, and the verifier runs it again against the jar and compares the pixels.

## Use sounds

`facts/block_use_sounds.csv` is dumped by `FactsDump.java` from public API only: per door, trapdoor, fence gate, button, pressure plate and the lever, its kind, its set type, and the events it plays (`open`/`close`, or `click_on`/`click_off`), as paths without namespace. A door's set type is its public `DoorBlock.type()`. Trapdoors, buttons, plates and gates match their id (minus the family suffix, the waxed and oxidation prefixes; heavy and light weighted plates are iron and gold) to one of the public `BlockSetType` or `WoodType` values, and the dump checks that the block's sound type is the set's. The lever plays `block.lever.click`. Every event is in `sounds.json`; their `.ogg` files are not included.

## Shapes and offsets

`facts/block_state_facts.json` is measured by `FactsDump.java` through the 26.3 server's public block state methods at runtime, never from models: `getShape` and `getCollisionShape` (an empty world at the origin), `getLightEmission`, `canBeReplaced`, `getFluidState`, and per face whether `getFaceOcclusionShape` is a full square (`occludes`, a bit per face in `face_order`). A block with an offset function gets `offset`: its kind (`xz`, or `xyz` when Y moves), the largest horizontal offset and the Y range seen over 4,096 columns, whether it ignores Y, and whether the outline or collision box moves with it. Moving boxes are stored without the offset of the probe position.

## Torch support

`facts/block_support.json` is measured by `FactsDump.java` the same way: for every imported state, the torch's own `canSurvive` in a world that holds only that state (a proxy of the public `LevelReader` interface, air everywhere else). `support` is one mask per state in state order, a bit per face in `face_order`: `up` when a floor torch survives on top, a side when a wall torch survives against it (facing away), and `down` never, since no torch hangs. Leaves and powder snow hold none, every other full cube holds all five, an upside-down slab holds a floor torch, and a bottom slab holds nothing. `items` lists each imported block's standing-and-wall item with its floor and wall blocks, from the public `BlockItem.registerBlocks` (the torch: `torch` and `wall_torch`; the coral fans and their wall fans).

## Plant survival

`facts/plant_survival.json` is measured by `FactsDump.java` through the same public `LevelReader` proxy as the torch table: for every state of every base block (the subject S; the base blocks are the frozen list `tools/survival_base_ids.txt`, the blocks imported before B1) the block's own `canSurvive` in a world that holds only S at the probe position and one candidate state X at a neighbouring cell, air everywhere else, block light and sky light 15 (`light`). X ranges over air and every state of every base block. There are eight probes per subject, in this order: `west`, `east`, `north`, `south`, `down`, `up` (X in that direction from S, `face_order`), then `down_water` (X below S, with water sources on the four sides of X: sugar cane's rule) and `down_dark` (X below S, with light 0: the mushrooms' rule).

The result for a probe is the set of X that let S survive. `signatures` lists the distinct sets, numbered by first appearance, signature 0 being the empty set: each maps a block id (or `air`) to `"all"` (every state of that block passes) or to the ascending 0-based state indices within that block, in its `block_states.json` state order; blocks with no passing state are absent. `subjects` maps each block id (protocol order, as in `block_state_facts.json`) to one row per state in state order, each row the eight signature ids in probe order. Example: a vine with only `up` set survives under a top slab but pops under a bottom slab; a full cube like stone survives with air in every direction. The native test `Unnamed.Foundation.Blocks.Survival` checks the plants' survival table against this file. The dump takes about 8 s.

`facts/family_survival.json` has the same layout, probes and light, over every imported block. Its subjects are the rule-bearing states only: a state whose `canSurvive` fails alone (air everywhere), or with a stone block at one of its six neighbours. The stone test brings in farmland and dirt path, which fail under a solid block. Every other state's row is `null`, and blocks with no rule-bearing state are absent: 166 blocks, 3,472 subject states, 112 signatures. The candidates are air and every state of every imported block (28,553). The dump takes about 110 s. On the base subjects and base candidates it equals `plant_survival.json`, and every base state with a `null` row passes every probe there with every base candidate; the verifier checks both.

## Budgets

Face codes hold a texture index in 10 bits, and the importer's white texture takes index 0: 966 of 1,024 are used, 58 left. The block array holds 1,364 frames plus the white layer, 1,365 of Unreal's 2,048 texture array slices. There are 4,867 baked models. The verifier asserts both limits.

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

`build_reference.py` needs the pinned jars, the research work folder (`C:\dev\sandbox\mc-26.3-blocks`, regenerated by `tools/build_manifest.py` and its siblings) and the `FactsDump` output. `FactsDump` takes three arguments: the output folder, the imported block ids (one per line), and `tools/survival_base_ids.txt` (the subjects of `plant_survival.json`). The build copies the research generators from the work folder's `scripts/` and `FactsDump.java` from the facts folder's sibling `tools/`. It refuses to run unless this folder holds only `tools/`, so to rebuild, move the accepted folder away first. After a README or tool edit, `build_reference.py --sums-only` rewrites only `SHA256SUMS.txt`.

Minecraft art and data remain Mojang/Microsoft material. Local development builds may use them; anything distributed must replace them.
