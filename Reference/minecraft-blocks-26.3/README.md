# Java 26.3 block reference

The import input for the block system: every full-cube block and slab of vanilla Java 26.3, plus the two blocks the world generator's surface rules and snow line need, powder snow and snow layers (531 blocks: 428 cubes, 101 slabs, powder snow and snow), and the fluids water and lava. It holds their ids, states, per-state faces, textures, mining facts, tints, block items and creative tab positions. The research behind it is summarized in [Docs/MinecraftBlockReference.md](../../Docs/MinecraftBlockReference.md). This folder is generated, hash-listed and never edited by hand.

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
| `block_manifest.json` | One entry per block in registry order. It has the research fields (ids, faces, render layer, flags, tags, where found) plus `name`, `hardness`, `blast_resistance`, `requires_correct_tool_for_drops`, `light_emission`, `facts_source`, `tint`, `material_layer` and `random_variants`. Water and lava are under `fluids` (lava: opaque, light 15). |
| `block_states.json` | Every state of every imported block (3,470): the 26.3 state id, its properties, `shape` (`full`, `slab_bottom`, `slab_top`, `fluid`, and `layers_1` to `layers_7` for snow layers k/8 high) and six faces. See [Faces](#faces). |
| `textures.json` | The 556 textures: size, frame size and count, animation (`frametime`, frame order and times, `interpolate`), `mipmap_strategy`, `alpha` class, PNG SHA-256, the target array and first layer, and the SHA-256 of every frame's RGBA bytes. It also lists the plains tint colors. |
| `items.json` | The 529 block items (item id = block id): English name, stack size, item model, GUI tint, and positions in every creative tab. Powder snow has none, because its vanilla item is `powder_snow_bucket`. Also the 14 creative tabs: row, column, type, alignment and icon. |
| `assets/minecraft/` | Jar files, byte for byte: block textures and `.mcmeta`, the grass and foliage colormaps, the creative inventory GUI art and tab icon sprites, item definitions, and the `block`/`generated`/`handheld` item display models. |
| `facts/` | `block_facts.csv` (hardness, blast resistance, correct-tool flag, light, item, English name for all 1,286 blocks) and `creative_tabs.json` (every tab's display and search order), dumped by `tools/FactsDump.java`. |
| `reports/blocks-imported.json` | Mojang's `blocks.json` report entries for the imported blocks: the state-id oracle. |
| `research/` | The research outputs as generated: the original manifest, `blocks_registry.csv` (all 1,286 blocks), `block_tags.csv` (resolved tags), `edge_cases.txt` (excluded cube-like blocks), water and worldgen notes. |
| `tools/` | `build_reference.py` (this folder), `verify_reference.py`, `FactsDump.java`, and the research generators (`build_manifest.py`, `classify_blocks.py`, `build_blocks.py`, `build_worldgen.py`, `doc_template.md`). |

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
- **Fluids:** water and still lava. The 32 x 32 flow textures (`lava_flow`, then `water_flow`, in name order like every texture) share the array still named `water_flow`.
- **Tints:** 26.3 `BlockColors.createDefault`:
  - grass block: grass colormap;
  - oak, jungle, acacia, dark oak and mangrove leaves: foliage colormap;
  - spruce leaves `#619961`, birch leaves `#80A755`;
  - water: biome water color.
  A model `tintindex` without a registered source renders untinted, which applies to cherry, pale oak, azalea and poplar leaves. Colormap colors use the plains biome (temperature 0.8, downfall 0.4): grass `#91BD59`, foliage `#77AB2F`, water `#3F76E4`. Item tints come from each `items/<id>.json`; the grass block item is `#7CBD6B`.
- **Material layer:**
  - translucent when the model forces it (glass) or any face texture has partial alpha;
  - cutout when a face texture has fully transparent pixels;
  - otherwise opaque.
  The grass block's tinted side overlay is composited inside the opaque material.

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
