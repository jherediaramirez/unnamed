# Voxel world phase 6: the ore look calls L1 and L2 level 1 - October 10, 2026

Scout E's look calls ([the ores record](2026-10-09-Voxel-Phase6-Scout-Ores.md), section 3), built after S4 in the main lane: L1, ores wear their host rock (`voxel.Look.OreHost`), and L2 level 1, ore-aware mips and gloss (`voxel.Look.OreGlint`). Both are render-only switches whose default is vanilla 26.3 (0). Both were accepted by Nyx (night manager, delegated by Jesus); Jesus may overrule either in the morning. Glow (L2 level 2) was not built: Jesus chose "level 1, no glow".

## What was built

- **The textures** (`Scripts/OreLooks.py`, uv + Pillow, from the 26.3 block reference only): `Art/OreLooks/T_OreLooks.png` (a 16-pixel strip, 5 frames per slot) and `manifest.json` (slots, input hashes, the generator's own hash, each layer's mip-0 hash). A rerun writes byte-identical files; `--check` compares.
  - **Ore pixels:** an ore texture's pixels whose colour is outside its base rock's palette (stone's 4 greys, deepslate's 5): 24.6 to 38.3% of the stone ores, 18.4 to 33.6% of the deepslate ores, as scout E measured.
  - **320 slots:** (mode x 5 + host) x 16 + ore x 2 + base. Hosts: the ore's own base (the vanilla texture), granite, diorite, andesite, tuff. Modes: composite (the ore pixels over the host's own texture), palette swap (the base's tones mapped by brightness onto the host's), and both again with ore-aware mips.
  - **Ore-aware mips:** at each level the ore pixels keep their share (at least one pixel down to 2 x 2) and their own colour; the cells whose footprint holds the most ore pixels become ore, coloured with those pixels' mean, the rest take their rock pixels' mean (linear light); 1 x 1 is the plain mean. Their alpha carries the gloss class: 255 matte (rock, coal), 170 metal (iron, copper, gold), 85 gem (redstone, emerald, lapis, diamond).
- **The asset** (`Scripts/Editor/ImportOreLooks.py`): `/Game/Blocks/T_OreLooks`, a texture array with the block array's settings (BGRA8, sRGB, nearest, its own mips). The vanilla-mip slots get 26.3's mips from mip 0 with the ore's own strategy (`FoundationBlockMipmaps`); the ore-aware slots take theirs from the strip through a new `"explicit"` strategy in `UFoundationBlockImport::BuildTextureArray`. Additive like `ImportBlocks.py`: an existing asset is verified against the manifest (a metadata tag holds the strip's hash), never rebuilt.
- **The classes** (`Source/UnnamedVoxel/Meshing/VoxelOreLooks.*`, called at the end of `VoxelBlocks::MakeTable`): `FVoxelStateInfo::Ore` (1 + ore x 2 + base, for the 16 stone and deepslate ores; full opaque cubes without an overlay) and `Rock` (1 stone or deepslate, 2 to 5 granite, diorite, andesite, tuff).
- **The host** (`VoxelOreLooks::PickHost`, scout E's option (i)): the rock class most common among the ore's 6 face neighbours or, when none of them is a rock, among its 20 edge and corner neighbours (all inside the padded section). A tie, no rock, or the base winning keeps vanilla.
- **The mesher** (`VoxelMesher.cpp`): with `OreHost` on, an ore cell's host goes into the merge key's spare byte above the 24-bit tint, so a merged quad never spans two hosts. At emission an ore face's look word (`VoxelOreLooks::GetLookWord`) takes its overlay code's place (ore states have none): bit 14 without the overlay's bit 15, the slot in bits 0 to 9, the material's level in bits 10 and 11. With both switches at 0 nothing changes: `Unnamed.Voxel.Mesher.Digest` (S6's byte digest of the decorated radius-4 area, default options) still equals its golden.
- **The material** (`CreateBlockMaterials.py`, `M_ChunkOpaque` only, `-Materials=M_ChunkOpaque`): `CHUNK_OPAQUE_HLSL` adds the look branch (samples `T_OreLooks`, parameter `OreLookTextures`) and three custom outputs: roughness and specular (1 and 0.1 on every face except a glossy look's: metal 0.4 and 0.6, gem 0.2 and 0.9) and an extra emissive (the debug mask's only). 277 pixel-shader instructions, 3 samplers. The other chunk and block materials are untouched; S6b's later re-save of the `M_Block*` materials merges as text, and whichever lands second reruns the script.
- **Switches** (`VoxelConsole`): `voxel.Look.OreHost` 0 vanilla, 1 composite, 2 palette swap (capture-only); `voxel.Look.OreGlint` 0 vanilla, 1 ore-aware mips and gloss, 3 debug (every ore face flat magenta, the mask for the captures' changed-pixel check); other values read as 0. A change remeshes every loaded column (`FVoxelColumnPipeline::SetOreLooks`); `voxel.Bench Columns` reads them too.

## The encoding bug the L2 sheet found

The first build marked look words with bit 23, so words sat near 2^23. The GPU interpolates the overlay channel as a float, and at that size it is off by one, so `round()` picked a neighbouring slot: vanilla lapis drew as emerald under the glint, stone iron as deepslate iron, and some debug-mask faces missed the mask. The fix moved the marker to bit 14, so words stay below 2^15. `OreLooks.Classes` now asserts no real overlay code has bit 14 without bit 15, and `OreLooks.Words` that every word stays below 2^15. The L1 captures were redone after the fix (`-L1b-`); the decided look did not change.

## Look call L1: ores wear their host rock

**Decided by Nyx (delegated) at 04:22: ACCEPT the composite (`voxel.Look.OreHost 1`) as L1's look; the default stays 0 (vanilla); the palette swap stays capture-only at 2.**

- **Frames:** seed 263, spawn search from (40, -120), the S4 vein windows fixed (`-VeinsAt 2,154,25,0,274,-41`), `-Caves`.
- **Sheet:** `Tests/Evidence/2026-10-10/OreLooks-L1b-call/sheet.png` (columns: OreHost 0, 1, 2, the ore mask; rows: two copper-vein windows, the iron vein, the section cut, 3x crops where host 1 changes most); numbers in `numbers.txt` beside it.
- **What it shows:** copper ore in granite draws its specks on granite and sits in the rock naturally; stone-hosted ores are untouched. Iron in tuff loses contrast with the composite (beige specks on light tuff), where vanilla showed a dark deepslate square; the swap keeps a little more.
- **Control:**
  - Two runs at 0: the outdoor shots carry temporal noise (max difference 45 to 48, 0.03 to 1.2% of pixels over 8).
  - Past that noise max (48), on and off ore faces (the mask, dilated 2 pixels): vein-copper 187 / 0, vein-iron 357 / 0, section 409 / 0; runs at 0 against each other 0 / 0.
  - Over 8 on ore faces: copper 2,014 (noise 220), iron 1,118 (7), section 3,201 (28).
  - Stone hosts: the cave-wall shot's ores sit in stone. Lit with Lumen GI off (`-L1-host0-nogi`, `-L1b-host1-nogi`), host 0 against 1 differs by at most 7 and 0 of its 57,896 ore-face pixels change by more than 8. In the tests, `OreLooks.Words` asserts that OreHost 1 gives an ore on its base no look word, `OreLooks.Merge` that two ores walled in different rocks get their own hosts' words (their vanilla top merges, theirs split), and `OreLooks.Mesh` that every look word at OreHost 1 is a rock host's composite slot.
  - The block hash is unchanged with looks on (the gate's bench, below).
- **Known issue:** iron in tuff (and other ores on light hosts) reads worse than vanilla. Question for Jesus: L1 on by default later, after a light-host contrast tweak (for example a darker rim around the ore pixels on tuff and diorite) in a follow-up?

## Look call L2 level 1: ore-aware mips and gloss

**Decided by Nyx (delegated) at 04:36: ACCEPT `voxel.Look.OreGlint 1` as L2's look; the default stays 0; glow (level 2) is a later option.**

- **Sheet:** `Tests/Evidence/2026-10-10/OreLooks-L2b-call/sheet.png` (columns: OreGlint 0, 1, OreHost 1 with OreGlint 1, the mask; rows: the lapis in the vein window at 24 blocks, the section cut, two overview windows far away); numbers in `numbers.txt`.
- **What it shows:** far away the ore-aware mips keep the ores readable: coal reads as black specks instead of a grey block, iron and copper specks keep their colour. Close up, metal and gem pixels catch a sky sheen; it lightens lapis's blue a little. The knob is the gem specular (0.9) in `CHUNK_OPAQUE_HLSL`.
- **Numbers:**
  - Past 48, on and off ore faces: vein 364 / 0, section 255 / 0, overview 14 / 1; runs at 0 against each other 0 / 0.
  - Luminance spread on ore faces (standard deviation): section 33.35 to 37.00 (+11%), overview 46.01 to 46.59, vein 30.60 to 30.89; between two runs at 0 it moves by 0.05 at most. The vein window's mean ore-face luminance rises from 64.4 to 69.0 (the sheen).
  - Scout E's saturation classifier fell slightly (overview 53.7% to 51.3% of ore-face pixels): in these frames stone in shadow reads bluish and counts, and coal and iron specks are unsaturated, so it does not measure readability here.
- **No lit near-cave frame:** the cave-wall shot is completely black under Lumen at this seed and spawn (the wall is unlit and the exposure is set for daylight; it predates this work), and the gloss needs light, so the frames are the sunlit cut faces.

## Gate

Quiet machine, windows given by Nyx; old (the merged tip before the looks) and new alternated by swapping the DLL pair, `UnrealEditor.modules` and `M_ChunkOpaque.uasset` (`C:\dev\sandbox\orelooks\gate.ps1`, summary `gatesum.py`). Before every launch: 0 java, UnrealEditor or UBT processes, CPU 0 to 4%. Variants: old, new with both switches at 0, new with `voxel.Look.OreHost 1` and `voxel.Look.OreGlint 1`.

- **GPU frame per shot** (window 04:41 to 04:51, old `cd6e616`; `CaptureVoxel` at seed 263 with the fixed vein windows and `-Caves`, medians of 3 at 1600 x 900; old / new / looks): vein-copper 3.22 / 3.22 / 3.25 ms, vein-iron 3.31 / 3.29 / 3.31, section 3.34 / 3.34 / 3.35, overview 3.53 / 3.53 / 3.60, lumen-scene 4.31 / 4.30 / 4.34; the other five shots within 0.02 ms. The material costs nothing at 0; both looks cost +0.00 to +0.07 ms. First view 3.3 to 3.7 s for every variant (`gate-out.txt`).
- **Mesh cost** (window to 05:21, old `a7e1bf3`; both sides built with `-DisableAdaptiveUnity`; `voxel.Bench Columns 4`, 3 rounds; `gate2-out.txt`):

  | | Old | New at 0 | Both looks on |
  |---|---|---|---|
  | One-thread area pass, mean ms per section | 0.109, 0.098, 0.092 | 0.089, 0.109, 0.091 | 0.092, 0.093, 0.089 |
  | Mesh ms per column (one thread, blend 2) | 2.117, 1.984, 2.314 | 2.301, 2.240, 2.051 | 2.081, 2.147, 2.171 |
  | Quads per column | 1,545.8 | 1,545.8 | 1,546.4 |

  New at 0 lies inside the old build's own spread on both measures; both looks on add +0.03 ms per column against old's median (+1.4%, within noise). The block hash is `0x44ebe3f1b15b524b` in every run.
- **The first mesh run was unfair:** the new build came from adaptive unity, so the modified `VoxelMesher.cpp` compiled outside its unity blob, and the area pass read +27% at 0 (0.111 against 0.087 ms per section) while the 30-worker pass and the looks-on runs did not. Rebuilt alike, the gap is gone.
- **Two tweaks before the second run:** `Ore` and `Rock` sit in `FVoxelStateInfo`'s padding after `TintSource`, so the struct stays 68 bytes (`static_assert` in `VoxelOreLooks.cpp`), and the mesher reads the switches once per section.

## Verified

- **Tests** (`Source/Unnamed/Tests/VoxelOreLookTests.cpp`):
  - `Unnamed.Voxel.OreLooks.Classes`: the classes of the ores and rocks, 18 ore states in 16 classes, no overlay on an ore, no real overlay code that reads as a look word.
  - `.Host`: ten neighbourhoods, ties included.
  - `.Words`: the words for each switch pair, below 2^15 and off the overlay's bits; the manifest's 320 slots against `GetSlot`. On its first run it caught a generator bug: every rock host was labelled "tuff" (a stale loop variable; the images were right). Fixed and regenerated; the strip stayed byte-identical and `T_OreLooks` verified without a rebuild.
  - `.Mesh`: the decorated radius-4 area of seed 263 under four switch pairs. OreGlint alone changes only the overlay channel of ore faces; OreHost keeps every layer's face area and only adds quads; at OreHost 1 every look word is a rock host's composite, at OreHost 2 with OreGlint 1 a swap slot with ore mips (some on a rock host); a quad's four vertices share their word.
  - `.Merge` (added after the pre-commit review): two iron ores side by side, one walled in granite and one in diorite; vanilla merges their tops into one quad, OreHost 1 gives two quads with granite's and diorite's words.
  - `.Texture`: `T_OreLooks`' settings, every layer's mip 0 against the manifest's hash, the 160 ore-aware slots' mips against the strip, `M_ChunkOpaque`'s parameter.
- **Suites:** all native suites 147 of 147 on `a7e1bf3` with the looks (the tip's 141 plus the 6 new), after the review's fixes; `Mesher.Digest` equals its golden.
- **A stale build, caught:** restoring the set-aside sources with `Copy-Item` kept their old timestamps, so UBT skipped some changed units and `Voxel.Edit.Authority` crashed in `FVoxelDecorator::GenerateSnapshot` (stale objects saw the old `FVoxelMeshOptions`). The old snapshot passed the same tests; after touching the sources and rebuilding, everything passes.
- **Mutations** (`C:\dev\sandbox\orelooks\mutate.ps1`, each built, run and reverted): M1, ties no longer keep vanilla, fails `OreLooks.Host` ("two granite, two diorite: a tie keeps vanilla" gives 1); M2, the host dropped from the merge key, fails `OreLooks.Merge` and `OreLooks.Mesh`.
- **Generated data:** `Art/OreLooks` was written after `OreLooks.py`'s last edit, `ImportOreLooks.py` verified `T_OreLooks` after it, and `M_ChunkOpaque` was re-saved after `CreateBlockMaterials.py`'s last edit.
