# Phase 6 block models: facts for S5 and S6

Scout D, October 9, 2026. This is read-only research. Copied into the records by phase 6 session 5, which built from it (R1, R2, R6, R7 and R8, with R8 corrected: the 10-bit face-code field holds a texture index, so its budget is 690 of 1,024 textures, and the 945 array layers stay within Unreal's 2,048 slices). The `work/` tables and the `notes/` are in [2026-10-09-Voxel-Phase6-Scout-D-Tables](2026-10-09-Voxel-Phase6-Scout-D-Tables/); the jar extract and the scripts stayed in the disposable sandbox. Model facts are counted from the 26.3 client jar's blockstate, model, texture and `.mcmeta` files, extracted to `jar-assets/` here; no game code was read. The terrain shader that section 2.4 describes is a resource-pack asset. Web facts come from three Sonnet subagents; their notes are in `notes/`. Scripts are in `scripts/` and outputs in `work/`. Items marked *unverified* rest on search snippets, not the page itself.

## 0. Summary

1. **The format matches the plan, with one rename.** 26.3 has no `shade` key: it was replaced by `shade_direction_override` (in 99 of 238 unique elements: `up` 83, `north` 16). Elements also carry `light_emission`, used by the firefly bush overlay at 15. The plan's S5 baker line names `shade`. It should record the override and the emission instead.
2. **The geometry is simple.** Element rotations are single-axis only, ±22.5° or ±45° (plus 0° with an origin), with `rescale` on the 45° crosses. There are no multi-axis or free angles in these 83 blocks, although the format has allowed them since 25w46a.
   - `uvlock`: 37 blockstate refs, all from vine, glow lichen, sculk vein and pale moss carpet.
   - Face UV `rotation`: 62 faces.
   - Random variant lists: lily pad, sea pickle and bamboo. The reference's current "first entry" rule would make them uniform.
3. **Thin planes already have two faces.** Every cross, vine, lichen, seagrass and flowerbed plane is a zero-thickness element with both opposite faces (north and south, or up and down). The existing one-sided `M_ChunkCutout` is therefore right. A two-sided material would draw each plane twice and z-fight.
4. **Distant plants are mostly handled already.** `FoundationBlockMipmaps` is a bit-exact port of 26.3's mip generator, including the cutout strategies' alpha-coverage scaling, and the importer already calls it per layer. The gaps:
   - 21 new textures use `strict_cutout`, which the parser knows but no current texture uses.
   - 4 use `alpha_cutoff_bias 0.1` (cactus side and top, kelp, kelp plant). Nothing passes the bias today: `textures.json` has no field for it, and `FoundationBlockImport.cpp:169` calls `Generate` without it.
   - Grass, ferns and the other tinted crosses are `auto`, which resolves to `cutout`.
5. **Textures:** 127 of the 135 new textures have binary alpha, 8 are opaque and none has partial alpha, so nothing is translucent. Animated textures add 103 more frames than single images would. The array goes from 709 to about **947 layers, against a 10-bit layer field (1,024)**. Phase 7 will overflow it.
6. **Quads per state** (all 949 states expanded): crosses 4, seagrass 8, flowerbeds 14-42, multiface 2-12, sea pickle 7-44, mangrove propagule 8-60. The densest vanilla patches (jungle grass: 25 patches x 32 tries per chunk) give about 1,000 extra quads per column at most, against about 300 per column today. Mesh time is unlikely to be the problem. GPU overdraw of masked quads (prepass, base pass, shadow depths) is the cost to gate, and the plan's gates are CPU-only.
7. **Lumen is the main unplanned risk.** The project uses hardware-ray-traced Lumen with its own per-section, axis-aligned cards, and RealtimeMesh puts the whole column into ray tracing.
   - Plant quads would be in the ray-tracing scene but in no card, which is exactly how terrain read black before step 2L.
   - Masked alpha in Lumen's hit path is unverified. If it is not evaluated, whole quads occlude.
   - RMC has no per-section ray-tracing switch. `bForceOpaque` is stored but unused by the proxy.
   - Recommendation: never card the model quads, and decide ray-tracing membership with a ground-brightness capture.
8. **Offsets** are documented as a function of X and Z only, not the seed and not Y.
   - That keeps two-cell plants and dripstone columns aligned, so our hash must leave Y out.
   - XZ range: ±0.25; pointed dripstone and sulfur spike ±0.125.
   - Y range (short grass, fern, the dry grasses, small dripleaf at half): -0.2 to 0, never positive.
   - For bamboo, flowers, dripstone, sulfur spike and the propagule, the offset also moves the outline (and bamboo's collision).
9. **AO:** 67 of 83 blocks turn `ambientocclusion` off in at least one model. Vanilla's smooth lighting for partial blocks is not documented, and Sodium changes it, so a simple rule of our own is fine (R9).
10. **Tints:** `tintindex` 0 is grass (6 tinted crosses including sugar cane and bush), foliage (vine), dry foliage (leaf litter) or a fixed colour (lily pad). Index 1 is only the pink petals and wildflowers stems, on grass. The jar has the `dry_foliage` colormap: plains is `#A37546`, read the same way as grass `#91BD59` and foliage `#77AB2F`, which match the project.

## 1. Recommendations, ranked

Each says what it changes in the plan.

| # | Recommendation | Changes |
|---|---|---|
| R1 | **Bake `shade_direction_override` and `light_emission`, not `shade`.** In S6, the override becomes the quad's lighting face. The face index already sets the normal in `VoxelChunkComponent` (`FaceNormals[Face]`), so crosses get up-facing normals with no new vertex stream. That matches vanilla's intent of shading crosses as the top face, and the usual foliage trick. Without an override, use the face's direction after the blockstate rotation. Emission goes in vertex G (light x 17), which already exists. | S5 scope ("`shade`" becomes "`shade_direction_override`, `light_emission`"); S6 "model rendering details" gains normals from the override. |
| R2 | **Plumb `alpha_cutoff_bias` through `textures.json` into `FoundationBlockMipmaps::Generate`, and check that `strict_cutout` reaches `ParseStrategy`.** Keep the clip at 0.5: the port's coverage scaling is tuned to it, and per-texture clip values would break parity. Drop `CreateBlockMaterials.py (cutout mips)` from S5's files unless something else needs it. Add a `.Textures` test: the 4 biased and 21 strict textures generate the expected levels (compare a few against vanilla output, as the port was verified). | S5 files and tests; risk "Cutout mips need care" becomes "plumb the two `.mcmeta` fields". |
| R3 | **Keep the cutout material one-sided and emit both baked faces.** Test: each zero-thickness element yields two opposite quads, and only one shows from either side. | Prevents a wrong two-sided change (the UE subagent suggested one); S6 tests. |
| R4 | **Never put model quads in Lumen cards. Decide ray-tracing membership with a capture.** Capture a grass field and measure the ground's mean sRGB with models on and off. If the ground darkens, move non-occluding model quads into a second RMC component per column with "Visible in Ray Tracing" off. That is faithful too: plants do not block light in vanilla. An alternative is `r.Lumen.HardwareRayTracing.SurfaceCacheAlphaMasking`, whose cost is unverified. Cards then stay as today; check that the per-column card count does not change. | S6 "Lumen cards: skipped if they cost" becomes "never carded; ray-tracing scene decided by capture G3". |
| R5 | **Put model quads in their own layer section (same cutout material).** RMC sections have `bCastsShadow`, so plant shadows can be switched without touching leaves. A debug cvar (for example `voxel.Models 0`) gives on/off A/B for every capture. Add a GPU gate. | S6 scope ("into the cutout layer" becomes "a model-cutout section"); S6 gates gain G2. |
| R6 | **Offsets: our hash of (x, z), with a salt and no Y.** XZ range ±0.25 x the facts' maximum, and Y in [-0.2, 0] for XYZ kinds. The facts dump's kind and maximum override the wiki list. Apply the offset to the outline, and to collision where the facts say so (bamboo). `FactsDump` should record whether the shape moves with the offset. | S5 `FactsDump` field; S6 offset detail; `Voxel.Mesher.Models` test: (x, y, z) and (x, y+1, z) get the same offset. |
| R7 | **Bake every entry of random variant lists** (lily pad and sea pickle rotations, bamboo's 4 stalk models; all equal weight, no `weight` keys), and pick one by position hash in S6. | Reverses the reference's "first entry" rule for these 3 blocks; S5 baker, S6 mesher. |
| R8 | **Texture layer budget:** report it in S5 (about 947 of 1,024), assert it in `.Textures`, and note in `Docs/Blocks.md` that phase 7 must widen the field or add a second array. | S5 evidence and test; a known issue for phase 7. |
| R9 | **AO rule for model quads:** models with `ambientocclusion: false` stay open (R = 255). For AO models (carpets, cactus, azalea, dripleaf, dripstone, sulfur spike, seagrass, sea pickle, shelf mushroom, spore blossom, small dripleaf, mangrove roots, and parts of bamboo and the propagule), a face flush with a cell side, or within 1/16 of it (carpet top, cactus sides), takes that side's full-face corner AO. Other faces stay open. This is our own rule: look-check it against a vanilla screenshot. | S6 "the AO flag" gets a defined rule and a test. |
| R10 | **Distance-shimmer capture (G1), with the method below.** If texel-edge shimmer shows beyond what TSR hides, consider later, in our own code, a texel-footprint UV snap in the chunk materials (the approach 26.3's terrain shader asset takes: section 2.4). | S6 evidence "a distance shot" becomes a measured capture. |
| R11 | **Keep the cross fast-path gate, but expect it to be unneeded.** About 1,000 pre-baked quads appended per dense column is a copy loop. Use a jungle or flower-forest column as the plant-dense test, and report quads and bytes per column next to mesh time (columns are about 10 KB today). | S6 gate wording; adds memory to it. |

### Captures and gates to add

- **G1, distance shimmer.**
  - Setup: fixed camera, one patch each of grass, flowers and leaf litter at 8, 16, 32, 64 and 128 blocks; TSR on, 100% screen percentage.
  - Coverage: an unlit mask view (or plant-colour pixel count) per distance, against mip 0 coverage scaled by projected area. Expected: within about 5% to mip 2, then plant-specific losses, which are vanilla's too (section 2.3).
  - Shimmer: 30 frames with 0.1 px camera jitter, per-pixel temporal variance on the plants.
  - Optional black-box comparison: the vanilla client at mipmap levels 4, same distances.
- **G2, plant-dense GPU.** Plains or jungle view, `ProfileGPU` PrePass, BasePass, ShadowDepths and Lumen passes, with models on and off and plant shadows on and off. The plan has no GPU gate. Suggested limit: models add no more than about 0.5 ms at 1600 x 900 on this PC (the column budget work used the same resolution). Also try `r.EarlyZPassOnlyMaterialMasking` on and off (the name is from a forum post: confirm it in the 5.8 console).
- **G3, ground brightness under plants.** Mean sRGB of a ground patch in a grass field, models on and off, from the same spot. A drop of more than about 3/255 means plants are occluding Lumen rays (R4).
- **G4, the existing `lumen-scene` shot** with plants: the ground is unchanged. Plants may show as missing or pink in the surface-cache view; that is expected.
- **G5, cards:** `ScreenCards` per column is unchanged with models on (it is already in `build.txt`).
- **G6, close-ups:**
  - a cross at a grazing angle: no z-fighting, both sides lit;
  - tall grass, large fern and sunflower halves aligned;
  - a dripstone column aligned;
  - lily pads and sea pickles not all facing one way.

## 2. Findings

### 2.1 Model format as the 83 blocks use it (Q1)

**Elements of the 26.3 model format** (minecraft.wiki "Model", plus jar data):

- **Kept from earlier versions:** `parent`, `textures` (with `#` references), `elements` (`from`/`to`, `rotation` {`origin`, `axis`, `angle`, `rescale`}), faces (`uv`, `texture`, `cullface`, `rotation`, `tintindex`) and `ambientocclusion`.
- **Changes since the reference's FaceBakery notes:**
  - `shade` was removed in 26.3 snapshot 3 and replaced by `shade_direction_override`, which shades faces as if they faced the given direction, terrain only. The docs subagent quoted older wiki text claiming `cross.json` has `shade: false`; the 26.3 jar shows `shade_direction_override: "up"` instead.
  - `light_emission` (0-15 per element) arrived in 24w33a (1.21.2).
  - Rotation: arbitrary angles since 25w16a (1.21.6); multi-axis rotation and no ±45 cap since 25w46a (1.21.11). Blockstates also gained an optional `z` rotation in 25w46a.
  - Textures may be objects `{sprite, force_translucent}`.
- **Render type:** no `render_type` key exists. Since 26.1 snapshot 7, the pass is chosen per quad from the sprite's pixels: translucent if any pixel is semi-transparent, cutout if pixels are only opaque or fully transparent, otherwise solid. A model can force translucent. Whether `cutout_mipped` survives as a separate pass is unverified, but mip behaviour is now per texture (`mipmap_strategy`, 25w43a). For our blocks this means every model quad is cutout except quads whose texture is fully opaque: `bamboo_stalk`, `azalea_top`, `flowering_azalea_top`, `pale_moss_carpet`, `shelf_mushroom_stage0/1`, plus `moss_block`, which is already imported.

**Template parents** (the model that owns the elements; one count per block):

| Template | Blocks | Notes |
|---|---|---|
| `block/cross` | 40 (all of A0, A0 facing, A2, plus sunflower's lower half) | 2 elements, y 45° rescale, 4 quads, AO off, override `up` |
| `block/tinted_cross` | 6 | as cross, `tintindex 0` |
| `block/cross_emissive` | firefly bush | cross plus a second cross with `light_emission 15` (8 quads); the overlay is animated (10 frames) |
| `block/sunflower_top` | sunflower upper | cross plus a z 22.5° head (6 quads) |
| `block/template_seagrass` | seagrass, tall seagrass | 4 axis-aligned planes at 4/16 and 12/16, 8 quads, **AO on** (parent `block/block`), override `up` |
| `flowerbed_1..4`, `template_leaf_litter_1..4` | pink petals, wildflowers, leaf litter | multipart by `facing` x amount; flowerbeds have y -45° stems with override `north` and tint index 1; litter is flat planes at y 0.25/16 with up and down faces |
| `block/carpet`, `mossy_carpet_side` | moss carpet, pale moss carpet | carpet with 5 cullfaced faces; pale moss adds side planes at 0.1/16 (low and tall textures), multipart with `uvlock` |
| `glow_lichen`, `sculk_vein`, `vine` | multiface | one plane per face at 0.1/16 (vine 0.8/16), multipart x/y rotation with `uvlock`, AO off |
| `coral_fan`, `coral_wall_fan` | 10 fans | 4 or 2 planes tilted x/z ±22.5° (wall fans rotate about origin z 14), override `up` |
| custom (26 blocks) | the D group | azalea (box top plus a cross), bamboo (4 stalk models per age plus leaf crosses), big dripleaf (3 tilts), cactus (inset 1/16 sides), cocoa (3 stages), dripstone and sulfur spike (cross, AO on), sea pickle (1-4 pickles, rotated variants), shelf mushroom, small dripleaf, spore blossom, propagule (up to 60 quads), mangrove roots |

**Feature counts** (from `work/models.txt`):

| Feature | Count | Unusual users |
|---|---|---|
| Element rotations (unique elements) | 121 of 238 rotated: y ±45 (most with rescale), x/z ±22.5, x -45, and y 0 with an origin (propagule) | none multi-axis |
| `shade_direction_override` | 99 (`up` 83, `north` 16) | `north`: flowerbed stems only |
| `light_emission` | 2 elements | firefly bush overlay |
| Face `rotation` | 62 face refs (90: 22, 180: 16, 270: 24) | dripleaf, propagule, cocoa and others |
| `cullface` | 81 face refs | carpets, cactus, azalea, bamboo, mangrove roots, sea pickle, shelf mushroom; none on crosses |
| `tintindex` | 0 and 1 | 1 = flowerbed stems |
| Blockstate `uvlock` | 37 refs | glow lichen, sculk vein, vine, pale moss carpet. The reference baker so far handled uvlock only for mushroom blocks by keeping unrotated UVs; these need real uvlock. |
| Blockstate x/y | x 30, y 153 refs | amethyst facings, multiface, flowerbeds, cocoa, dripleaf and others |
| Random variant lists | lily pad, sea pickle (rotations), bamboo (4 stalk models) | no `weight` keys |
| Multipart | 8 blocks (flowerbeds x3, pale moss carpet, multiface x3, bamboo); no OR/AND | |
| AO | off in at least one model for 67 blocks (bamboo and the propagule mix on and off); on throughout for 16 | seagrass is AO on |

**Mip metadata on the 135 textures:** 21 `strict_cutout` (all flowers, mushrooms, amethyst, cactus flower, eyeblossom, sweet berry stage 0), 2 `dark_cutout` (mangrove roots), 4 `alpha_cutoff_bias: 0.1` (cactus side and top, kelp, kelp plant), and 108 with none (`auto`). Published semantics (25w43a, wiki and NeoForged 1.21.11 docs):

- `auto`: `mean` if the texture has no fully transparent pixel, else `cutout`.
- `mean`: average of 4 texels.
- `cutout`: each level built from the original with alpha snapped.
- `strict_cutout`: like `cutout`, with a stricter threshold.
- `dark_cutout`: a mean that skips transparent neighbours (leaves).
- `alpha_cutoff_bias`: added to the threshold in lower mips only.

The NeoForged docs give thresholds of 0.2 and 0.6, while the project's port uses coverage references of 0.5 and 0.3. The port is verified bit-for-bit against vanilla output, so it is the authority; do not re-derive from the docs.

### 2.2 Quads, states and the layer budget

- **949 states verified** by expanding every property combination against variants and multipart (`work/states.txt`). The baked index can deduplicate: at most 333 distinct (model, x, y, uvlock) refs, and multipart states are sums of parts.
- **Per-state quads:**
  - crosses 4;
  - seagrass and coral fans 8;
  - wall fans 4;
  - multiface 2-12 (mean 6.2);
  - pale moss carpet 2-14;
  - flowerbeds 14-42 (mean 28.5);
  - sea pickle 7-44;
  - propagule 8-60;
  - azalea 14;
  - small dripleaf 4-22.
- **Texture layers:** 128 single-frame plus 110 animation frames (kelp 20, kelp plant 20, seagrass 18, tall seagrass top and bottom 19 each, sculk vein 4, firefly overlay 10) = 238 layers. With today's 709 that is **947 of the 1,024** the 10-bit face-code layer field allows. `moss_block` may already be present, so the total could be one less.
- **Density:**
  - plains: `patch_grass_plain` gives 5-10 patches x 32 tries;
  - jungle: `patch_grass_jungle` gives 25 x 32;
  - forest: 2 x 32;
  - `patch_tall_grass` (rarity 1/5): 96 tries.

  A jungle column can fill most of its 256 surface cells with 4-quad plants, so about 1,000 quads. Today's average is 24,477 quads over 81 columns, about 300.

### 2.3 Cutout at a distance (Q2)

**Why plants dissolve.** Box-filtered alpha averages opaque and clear texels, so thin features fall under the cutoff and vanish in lower mips (Castaño 2010). Clear texels' colour also bleeds into the average and makes dark fringes. A hard alpha test makes binary edges that shimmer as sub-pixel features cross the cutoff.

**Our textures, measured** (`work/coverage.txt`; naive box mips with a 0.5 cutoff, first frame):

- Most plant textures keep total coverage within about ±20% through mip 2, which is 4 px per block, about 200 blocks away at 1600 x 900 with UE's 90° horizontal FOV. Mip 1 is at about 100 blocks. Flat quads seen at grazing angles (leaf litter, lily pads, carpets) reach these mips much sooner.
- 1-px stems vanish by mip 2: the pink petals and wildflowers stems (1-2% coverage), small and big dripleaf sides, the propagule. Flowers (12-20% coverage) vanish at mip 3.
- With a 0.1 cutoff, coverage inflates instead (sugar cane 0.55 becomes 1.0, glow lichen 0.41 becomes 0.83 at mip 1). That is the opposite failure: blobs.
- Coverage scaling preserves area, not thin lines. Losing stems at mip 2 or 3 is therefore expected, and vanilla does the same with its strategies. The bit-exact port gives vanilla parity.

**Fixes and how each maps to UE 5.8** (deferred renderer, TSR):

| Fix | How it works | In UE 5.8 for us |
|---|---|---|
| Coverage-preserving mips (Castaño 2010; NVTT, DirectXTex `ScaleMipMapsAlphaForCoverage`) | scale each mip's alpha so the fraction passing the cutoff matches mip 0; offline, free at runtime | **Already done**: `FoundationBlockMipmaps` implements 26.3's coverage scaling per strategy. UE's own `bDoScaleMipsForAlphaCoverage` is irrelevant because we generate mips (its Texture2DArray support is unverified). |
| Alpha-to-coverage (Golus 2017) | alpha becomes the MSAA sample mask | Forward renderer with MSAA only. Not available with deferred, Lumen and TSR. |
| Shader alpha sharpening (Golus) | rescale alpha around the cutoff by `fwidth(alpha)` so edges stay about 1 px | Possible in the chunk material HLSL; still binary, so TSR has to smooth it. Not needed while parity holds. |
| Hashed or dithered alpha (Wyman & McGuire 2017; UE's DitherTemporalAA) | stochastic threshold, expected coverage equals alpha | Works with TSR (UE's dither node assumes TAA or TSR). Our alpha is binary, so it adds nothing at mip 0; at most a fade for distant plants. |
| Colour dilation or premultiplied filtering | stops dark fringes | vanilla's strategies already handle it (`dark_cutout` for leaves, solidified colour for cutout) |

**Vanilla's sampling.** 26.3's terrain shader asset samples with a texel-footprint snap: UVs are pulled toward texel centres according to the texel's on-screen size. That antialiases texel edges without blurring. An optional RGSS mode (4 rotated-grid taps blended across 2 mips) applies when texels shrink below a pixel. The cutout test is `alpha < ALPHA_CUTOUT`, with the value set by the pipeline; the project's 0.5 comment stands. Our chunk material samples `TF_Nearest` (point, so mip switches are hard) with continuous-UV gradients. TSR covers most of the difference; R10 covers the rest if G1 shows it.

### 2.4 UE 5.8 specifics for our renderer (Q3)

**Masked materials**

- Clipped pixels skip shading, but masked materials need the pixel shader in the depth prepass and in shadow depths.
- "Mask material only in early Z-pass" (`r.EarlyZPassOnlyMaterialMasking`; older name `r.EarlyZPassOnlyMaskedMaterials`) changes that trade. Measure both settings: a UE 4.20 report saw less overdraw but no FPS gain.
- A 5.8 forum bug reports camera-dependent z-fighting between overlapping masked planes. That is one more reason not to overlap planes (R3).

**Shading model**

- Default Lit plus up-facing normals (R1) is the cheap route.
- Two Sided Foliage adds transmission through a subsurface colour, and Lumen counts those pixels as foliage for reflections (`r.Lumen.Reflections.MaxRoughnessToTraceForFoliage`). Not needed for vanilla's look.

**Lumen**

- Software Lumen covers static meshes, ISM and landscape. Foliage in the surface cache needs Nanite. Thin surfaces get poor distance fields. Cards and distance fields are built offline, so RMC meshes get neither.
- Our project avoids all this with hardware ray tracing plus its own cards.
- The guidance is to keep the ray-tracing scene under about 100k instances after culling; the TLAS is rebuilt every frame. Plants inside the column component add triangles, not instances; a second component would double the column instances, still far below that.
- `r.Lumen.HardwareRayTracing.SurfaceCacheAlphaMasking` exists; the only evidence is a landscape bug, UE-347428 (5.5, fix targeted at 5.8). Its cost and default are unverified.
- Debug views: `r.Lumen.Visualize.CardPlacement 1` and Surface Cache, where pink means no coverage.

**RealtimeMesh** (the vendored core plugin)

- Section config: `bIsVisible`, `bCastsShadow`, `bIsMainPassRenderable`, `bForceOpaque`. `bForceOpaque` is serialized but unused when ray-tracing segments are built.
- Ray tracing is one geometry per buffer set, with segments per section. It is switched by the component's ray-tracing visibility or the global `r.RayTracing.Geometry.RealtimeMeshes`, never per section.
- The vendor's paid Pro edition advertises runtime cards and distance fields (marketing, unverified). Issue #303 (September 3, 2026) reports base-pass rendering problems on 5.8; our pinned copy evidently works.

**Shadows (virtual shadow maps)**

- Non-Nanite geometry is much costlier in VSMs: it is drawn per draw call, and masked materials run the pixel shader.
- A rebuilt section invalidates the cached pages it overlaps.
- Epic's foliage advice: contact shadows can be enough for grass; cull small casters (`r.Shadow.RadiusThreshold`); turn shadow casting off at distance.
- A forum report (5.6.1, dense foliage) measured about 7.5 ms in shadow depths. That is why G2 includes plant shadows on and off.

**What other voxel renderers learned**

- **Sodium:**
  - "material downgrading" (0.6) moves quads whose texture has no transparency to the solid pass; vanilla 26.1 now does the same per quad;
  - translucent sorting (0.7);
  - add-ons cull plants by distance (Cullify) and hide shared leaf faces.
- **Godot Voxel Tools:**
  - three materials on one atlas: opaque, alpha clip, transparent;
  - alpha clip drawn in the opaque pass;
  - only faces on the cube's sides are cullable, which matches `cullface`;
  - custom models kept low-poly;
  - AO baked into vertex colours on cube edges only;
  - no crossed-plant or offset features.
- **Luanti:** `plantlike` is 2 crossed quads. `meshoptions` adds a random horizontal offset, a 1.4x scale and a random downward shift of up to 1/8. The same idea as vanilla's Y offset, never upward.
- No write-up about Unreal Minecraft clones and cross plants was found.

### 2.5 AO, lighting and offsets (Q4)

**AO**

- The wiki describes smooth-lighting corner darkening for full blocks only. Partial-block sampling is undocumented.
- A Sodium add-on calls vanilla's dark shading of paths and slabs next to full blocks a bug that Sodium fixes.
- `cullface` also picks which neighbour's light a face uses. In our renderer, light comes from Lumen, and vertex R is only the occlusion term.
- So R9's rule is a choice, not a port.
- Directional face shade values are not on the wiki (1.0 / 0.8 / 0.6 / 0.5 is folklore, unverified). We use UE lighting anyway, which is why R1's normal matters more.

**Offsets** (minecraft.wiki "Block offset")

| Kind | Blocks per the wiki | Shapes move? |
|---|---|---|
| XYZ (Y -0.2 to 0) | short grass, fern, short and tall dry grass; small dripleaf (Java, Y half range) | texture only |
| XZ | mangrove propagule, small flowers, bamboo sapling, bamboo, pointed dripstone, sulfur spike (±0.125) | outline too (bamboo also collision) |
| XZ, texture only | tall flowers, tall grass, large fern, hanging roots, tall seagrass, nether roots and sprouts | no |

The offset depends on X and Z only, not on the world seed. The facts dump (S5) is the authority for kind and maximum. Our hash is our own; the project rule is (seed, salt, coordinates), and leaving out the seed and Y is allowed and matches the docs.

**Tints:** grass colormap for grass, ferns, tall grass, sugar cane, bush and the flowerbed stems; foliage for vine; dry foliage for leaf litter (biome-coloured since 25w08a, 1.21.5). The lily pad's fixed colour is not confirmed from docs: take it from the wiki's Lily Pad page or a client screenshot probe.

### 2.6 Against the plan (Q5)

**Supported:**

- Generic baked models (answer 1). The format is regular, and one path covers all 83 blocks.
- Unmerged model quads in the cutout pass.
- `cullface` culling.
- Collision and outline from facts, never from models (offsets are not in model JSON).
- The 0.2 ms fast-path gate.
- Per-vertex tint by `tintindex`, the emissive overlay, and the water work.

**Challenged or refined:**

- `shade` becomes `shade_direction_override` (R1).
- "Cutout mips need care" is mostly done; plumb the bias (R2).
- Lumen cards: never, and ray-tracing membership by capture (R4).
- The gates are CPU-only; add GPU and visual captures (R5, G1-G6).
- Random variants (R7).
- The layer budget (R8).
- Offsets must exclude Y (R6).

## 3. Open questions for Jesus

1. **Should plants cast sun shadows?** Vanilla has none; UE's look benefits near the camera. *Recommendation:* yes, near the camera, with a per-section switch; turn them off if G2 shows more than about 0.5 ms of shadow depth.
2. **Should non-occluding plants be left out of Lumen's ray-traced scene?** Vanilla light passes through plants. It costs one more RMC component per column. *Recommendation:* yes if G3 shows the ground darkening; otherwise keep one component.
3. **Is it acceptable that 1-px stems and small flowers vanish at mips 2-3, as in vanilla?** *Recommendation:* yes, keep vanilla parity, and revisit only if G1 looks worse than the vanilla client at the same distance.

## Appendix A. Sources

Dates are publication or version dates where known.

- **Minecraft (published docs):**
  - minecraft.wiki: [Model](https://minecraft.wiki/w/Model), [Blockstates definition](https://minecraft.wiki/w/Blockstates_definition), [Block offset](https://minecraft.wiki/w/Block_offset), [Resource pack](https://minecraft.wiki/w/Resource_pack) (`mipmap_strategy`), [Java Edition 25w43a](https://minecraft.wiki/w/Java_Edition_25w43a) (October 21, 2025), [26.1 Snapshot 7](https://minecraft.wiki/w/Java_Edition_26.1_Snapshot_7), [Biome](https://minecraft.wiki/w/Biome), [Light](https://minecraft.wiki/w/Light).
  - [NeoForged 1.21.11 texture docs](https://docs.neoforged.net/docs/1.21.11/resources/client/textures); [Minecraft 1.21.11 Pre-Release 1](https://www.minecraft.net/fr-fr/article/minecraft-1-21-11-pre-release-1) (`alpha_cutoff_bias`, from a search summary).
  - Snapshot 7 write-ups: [patchbot](https://patchbot.io/games/minecraft/articles/47-minecraft-261-snapshot-7), [Fabric for 26.1](https://fabricmc.net/2026/03/14/261.html).
- **Mipmaps and alpha:**
  - Castaño, [Computing Alpha Mipmaps](https://www.ludicon.com/castano/blog/?p=1218) (2010).
  - DirectXTex [ScaleMipMapsAlphaForCoverage](https://source-hub.etos-pro.ru/microsoft/DirectXTex/wiki/ScaleMipMapsAlphaForCoverage).
  - Golus, [Anti-aliased Alpha Test](https://bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f) (2017; the page returned 403, so this is from summaries).
  - Wyman and McGuire, [Hashed Alpha Testing](https://research.nvidia.com/sites/default/files/pubs/2017-02_Hashed-Alpha-Testing/Wyman2017Hashed.pdf) (I3D 2017).
  - Cesium, [dither and TAA](https://cesium.com/blog/2022/10/20/smoother-lod-transitions-in-cesium-for-unreal/) (2022).
- **Unreal Engine:**
  - [Blend modes](https://dev.epicgames.com/documentation/en-us/unreal-engine/material-blend-modes-in-unreal-engine), [Shading models](https://dev.epicgames.com/documentation/en-us/unreal-engine/shading-models-in-unreal-engine), [Lumen technical details](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine), [Lumen performance guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine), [Virtual shadow maps](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine), [TSR](https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine).
  - [UE-347428](https://issues.unrealengine.com/issue/UE-347428).
  - Forum: [masked-plane z-fighting (5.8)](https://forums.unrealengine.com/t/masked-out-pixels-cause-camera-dependent-z-fighting-between-overlapping-planes/2747068), [mesh terrain and software Lumen (5.8)](https://forums.unrealengine.com/t/mesh-terrain-doesnt-work-with-software-lumen/2730678).
- **RealtimeMesh:** [vendor page](https://triaxis.games/realtime-mesh/); [GitHub](https://github.com/TriAxis-Games/RealtimeMeshComponent) issue #303.
- **Other engines:** [Sodium](https://modrinth.com/mod/sodium) release notes, [Cullify](https://modrinth.com/mod/cullify), [Godot Voxel Tools blocky terrain](https://voxel-tools.readthedocs.io/en/latest/blocky_terrain/), [Luanti lua_api](https://github.com/luanti-org/luanti/blob/master/doc/lua_api.md).
- **Subagent notes,** with more links and quotes: `notes/mips.md`, `notes/ue-rendering.md`, `notes/mc-docs.md`.

## Appendix B. Raw data and how to regenerate it

```powershell
# from C:\dev\sandbox\scout-block-models; the jar is %APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar
uv run --no-project --python 3.13 python -I scripts\extract.py <jar> C:\dev\sandbox\scout-phase6\work\shape_groups.txt jar-assets
uv run --no-project --python 3.13 python -I scripts\analyze.py jar-assets work\models.json work\models.txt
uv run --no-project --python 3.13 --with pillow python -I scripts\coverage.py jar-assets work\coverage.txt
uv run --no-project --python 3.13 python -I scripts\states.py jar-assets C:\dev\projects\Unnamed\Reference\minecraft-blocks-26.3\research\blocks_registry.csv work\states.txt
```

- `jar-assets/`: 83 blockstates, 152 models (with parents), 135 textures plus 32 `.mcmeta`, the 3 colormaps, item definitions, and 3 terrain shader assets.
- `work/models.txt`: per-block group, blockstate kind, template, quads, tints, AO, rotated elements, cullfaces and mip metadata; totals for every key used.
- `work/states.txt`: per-block states and quads per state (min, max, mean); 83 blocks and 949 states.
- `work/coverage.txt`: per texture, alpha class, frames, mip 0 coverage, and naive mips 1-4 at cutoffs 0.5 and 0.1 against coverage-scaled mips.

**Key totals** (over all model refs; unique-element figures are in section 2.1):

- Model keys: `ambientocclusion` 113, `display` 31, `elements` 152, `gui_light` 31, `parent` 131, `textures` 152.
- Element keys: `faces`, `from`, `to` 448 each; `rotation` 320; `shade_direction_override` 295; `name` 8; `light_emission` 2.
- Face keys: `texture` and `uv` 1,210 each; `tintindex` 116; `cullface` 81; `rotation` 62.
- Blockstate keys: `model` 333, `y` 153, `uvlock` 37, `x` 30.
- Texture `.mcmeta`: 32 files; 23 have `mipmap_strategy` (21 strict, 2 dark), 4 have `alpha_cutoff_bias`, 7 have `animation` (1 with `interpolate`).
- Alpha classes: 127 binary, 8 opaque, 0 partial.

**Project facts this doc relies on** (read-only look):

- `FoundationBlockMipmaps.h`: 26.3 port with strategies Auto, Mean, Cutout, StrictCutout and DarkCutout, plus `AlphaCutoffBias`.
- `FoundationBlockImport.cpp:169`: calls `Generate` without the bias.
- `textures.json` today: 541 `auto`, 14 `dark_cutout`, 1 `mean`; no `strict_cutout`, no bias field.
- `CreateBlockMaterials.py`: cutout is one-sided, clip 0.5, `SampleGrad` with continuous UVs.
- `ImportBlocks.py` and `FoundationBlockImport.cpp`: `TF_Nearest`.
- `VoxelChunkComponent.cpp`: normals come from the face index, and it builds the cards.
- `Docs/VoxelWorld.md`: hardware ray tracing, cards per section and direction (cap 32), virtual shadow maps.
- RMC `RealtimeMeshSectionConfig.h`: section flags.
