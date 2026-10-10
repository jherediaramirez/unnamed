# MC docs scout (block models) - 2026-10-09
Sources: minecraft.wiki unless noted. Pages fetched via summarizer, so quotes are paraphrase-level; verify critical ones.

## 1. Model JSON (https://minecraft.wiki/w/Model)
- root: parent (child `elements` replaces parent's), ambientocclusion (bool, default true; only honored on the model that sets it / per wiki Tutorials/Models "only works on Parent file"), textures (`#var` refs; `particle`; object form with `sprite` + `force_translucent`), display, elements.
- element: from/to in [-16,32]; rotation {origin, axis x|y|z, angle, rescale(default false)}; faces down/up/north/south/west/east, missing face = not rendered.
- rotation history: 14w07a single axis, 22.5 steps; 25w16a (1.21.6) 22.5 restriction dropped (angle -45..45); 25w46a (1.21.11) multi-axis rotation and +-45 cap removed (wiki notes item-model section still lists old cap).
- light_emission: int 0-15, per element, min light level. Added 24w33a (1.21.2).
- shade: per element bool existed. REMOVED in 26.3 snapshot 3, replaced by `shade_direction_override` (shade faces as if facing a given direction; terrain only). Added 26.3 snapshot 3. (Wiki caveat: some tables still mention shade.)
- face: uv [x1,y1,x2,y2] optional (since 15w31a), texture `#ref`, cullface (also picks which neighbor light is used), rotation 0/90/180/270, tintindex default -1.
- block `overrides` removed 24w45a (1.21.4).
- Render type: wiki has no `render_type` key for vanilla. Vanilla: 26.1 snapshot 7 - "Any block model can now support cutout or translucent (partially transparent pixels) textures; whether a quad is rendered in the cutout or translucent render pass is determined by the contents of its assigned sprite" (patchbot.io/games/minecraft/articles/47-minecraft-261-snapshot-7). Fabric 26.1 write-up (https://fabricmc.net/2026/03/14/261.html): ChunkSectionLayer auto-set per quad: translucent = sprite has semi-transparent pixels; cutout = only opaque/transparent pixels; solid = no transparency; overridable by block model. So: derived from texture in 26.1+, not set in code per block (pre-26.1 it was per-block in code via ItemBlockRenderTypes / BlockRenderLayerMap, not JSON). `force_translucent` texture flag overrides.
- cutout_mipped: no source confirms removal (unverified). Mip control: texture .mcmeta `texture.mipmap_strategy` = auto|mean|cutout|strict_cutout|dark_cutout (default auto) + `alpha_cutoff_bias` float default 0 (https://minecraft.wiki/w/Resource_pack; NeoForge docs 1.21.11 list them, 1.21.9/10 don't; 25w42a had only a boolean darkened-cutout -> reworked ~1.21.11 snapshots; exact snapshot unverified). Wiki: cutout textures "turn all pixels less than 10% opaque fully transparent" (no version). NeoForge desc (secondary): cutout threshold 0.2, strict_cutout 0.6, dark_cutout = mean skipping fully transparent neighbors.
- Blockstates (https://minecraft.wiki/w/Blockstates_definition - fetched summary was thin): variants (comma-joined props, "" for none), arrays = random pick (weights/x/y/uvlock not covered by summary; from general knowledge `weight`, `x`,`y`,`uvlock` exist - unverified on wiki here), multipart `when` single / `OR` / `AND` lists, `apply` single or random array. 25w46a added optional `z` rotation to variant.

## 2. Block offset (https://minecraft.wiki/w/Block_offset, last edit 19 Aug 2026)
- Types: XYZ (horizontal + Y texture offset): short grass, fern, short dry grass, tall dry grass; small dripleaf (JE texture only; Y half magnitude).
- XZ: mangrove propagule, dandelion, golden dandelion, torchflower, poppy, blue orchid, allium, azure bluet, tulips, oxeye daisy, cornflower, wither rose, lily of the valley, bamboo sapling, bamboo, pointed dripstone, sulfur spike (hitbox and texture); sunflower, lilac, rose bush, peony, tall grass, large fern, pitcher plant, warped/crimson roots, nether sprouts, hanging roots (JE texture only, BE hitbox); tall seagrass (JE texture); corals/fans BE only.
- Max horizontal: +-0.25 clamp generally; +-0.125 for pointed dripstone and sulfur spike. Y: no explicit clamp; always <= 0, formula range -0.2..0 (small dripleaf -0.1..0).
- Deterministic: "depends solely on its X and Z coordinates, and not the world seed". Java Y offset depends on X, Z and block type. Y position not included. Bedrock algo unknown. Hitbox offset on JE for XZ group listed Yes only for the first group.
- Not described: no exact hash (not sought). Wiki says nothing on model-JSON offset fields; offset is renderer/block-code, not JSON. Community write-up (gitea ChromeCrusher/Texploit-Guide): 16x16 grid in central 8x8 px -> unverified/old.

## 3. Lighting / AO
- https://minecraft.wiki/w/Light: smooth lighting blends light per face corner, AO darkens corners "checks diagonally for full blocks" (only full blocks described; nothing documented for non-full). Java: paintings, item frames, water surface unaffected. Beta 1.3 smooth lighting added; 13w09a settings; 1.19.3 RC1 min/max merged. Directional shading: wiki only qualitative ("north/south slightly darkened; bottom and east/west more"). Numeric 1.0/0.5/0.8/0.6 NOT on wiki pages fetched (unverified; widely known values).
- Tutorials/Models: cross.json sets ambientocclusion false and per-element shade false; torch template ambientocclusion false.
- Sodium: no release note found explicitly for partial-block AO. Addon "Sodium Shadowy Path Blocks" (https://modrinth.com/mod/EIa1eiMm) says vanilla's dark shading on non-full blocks (paths, farmland, slabs, stairs, fences) next to full blocks is a bug that Sodium fixes and the addon restores. Sodium 0.9.1-beta.4 (mc26.1.2): "Fix non-terrain block lighting" (https://newreleases.io/project/github/CaffeineMC/sodium/release/mc26.1.2-0.9.1-beta.4). sodium-fabric 0.2.0 notes: smooth lighting fixes incl. emissive.
- Godot Voxel Tools blocky mesher (https://voxel-tools.readthedocs.io/en/latest/blocky_terrain/): bakes AO on cube edges only (vertex color); mesh models, no cross/offset features; culling only of faces on unit-cube sides, all-or-nothing; transparency_index, culls_neighbors.
- Luanti lua_api.md (https://github.com/luanti-org/luanti/blob/master/doc/lua_api.md): plantlike = two crossed quads; paramtype2="meshoptions" bits: 8 random horizontal offset, 16 mesh 1.4x, 32 random down shift up to 1/8; also `visual_scale` (not seen in fetch, unverified); light from node's own internal light value (paramtype light). place_param2 not verified. plantlike_rooted: base cube + plantlike extension.
- Vintage Story docs: not researched (unverified).

## 4. Tints
- https://minecraft.wiki/w/Biome: grass.png: grass block, short/tall grass, ferns, sugar cane, bush, pink petals/wildflowers stems. foliage.png: vines, oak/jungle/acacia/dark oak/mangrove leaves. dry_foliage.png: leaf litter. Spruce/birch leaves fixed colors in Java. Swamp variants special; dark forest, badlands, pale garden, cherry grove fixed. Lily pad / attached stem fixed tints: not on that page (unverified; known from code-agnostic wiki Lily Pad page not fetched).
- Leaf litter (https://minecraft.wiki/w/Leaf_Litter): added 25w02a (1.21.5); biome coloring (dry_foliage) 25w08a. Colormap itself presumably 25w08a (inferred).
- tintindex: -1 none; pink petals only vanilla block with multiple tint indices (per Tutorials/Models, may be stale given wildflowers/leaf litter).

## 5. Specific blocks
- Pointed dripstone: states thickness tip/tip_merge/frustum/middle/base, vertical_direction; XZ offset +-0.125.
- Sulfur spike: JE 26.2 snapshot 3; same states as dripstone; offset +-0.125; up tip_merge texture changed 26.3 snapshot 3.
- Big dripleaf: tilt none/unstable/partial/full; model changed 21w07a. Small dripleaf: XYZ offset JE texture (Y half).
- Bamboo: random offset XZ, wiki Bamboo page says "oriented randomly"; age/leaves details not retrieved.
- Leaf litter / wildflowers "work like pink petals": facing + segment_amount 1-4.
- Shelf mushroom: 26.3 snapshot 1; JE states age 0/1 (small/large), facing; 26.3-pre1 removed hidden faces.
- Cactus: model 7/8 wide-ish with spines; collision 15/16; cactus flower 25w06a (1.21.5).
- Firefly bush: 25w05a; light level 2; glowing sprites stay full-bright when placed (emissive-style overlay, item form not); no tint.
- Moss carpet: wiki page has no model detail (side heights unverified here; known from blockstate: tall/low/none per side + bottom).
- Not retrieved: cocoa, sea pickle, propagule hanging/age, glow lichen/vine multiface, spore blossom, bush, short/tall dry grass models (dry grass confirmed XYZ offset).
- 26.3 changes list (shade removal, shade_direction_override) from the Model page; full 26.3 changelog was truncated.
