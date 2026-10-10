# Ores: the vanilla 26.3 baseline and better-looking options

Scout E, phase 6, October 9, 2026. Written for S2 (the decoration core, with ores), S4 (ore veins) and Jesus's look calls.

- **Jesus's direction (19:28):** vanilla 26.3's ore data is the default. Better-looking options are look calls, each a switch with vanilla one setting away.
- **Where the numbers come from:** `Reference/minecraft-worldgen-26.3/data/minecraft/` (working tree, S1's uncommitted data): `worldgen/placed_feature/ore_*.json`, `worldgen/feature/ore_*.json`, `worldgen/biome/*.json` (which biomes list which feature), `tags/block/*ore_replaceables*.json`, `worldgen/material_rule/overworld/*_ore_vein.json` and `worldgen/density_function/overworld/ore_vein/*.json`. Nothing comes from memory or the wiki. The two helper scripts are in `C:\dev\sandbox\scout-ores\tools\` (`biome_ores.py`, `ore_shapes.py`).
- **Not known from data alone:** how many blocks a blob of a given `size` really places, and the exact in-vein rule. S1b's probes (`tools/feature_probe.py`, the 24 ore probes, and `facts/ore_vein_samples.json`) measure both; this doc says where they matter.

---

## 1. How to read the tables

- **Tries per chunk:** how many times the feature is attempted per 16x16 column (`count`), or "1 in N chunks" (`rarity_filter`). Each try picks a random X and Z inside the column (`in_square`) and a Y from the shape. A try that lands in air or water places nothing.
- **Shape:**
  - *uniform*: equally likely at every Y in the range.
  - *triangle*: a `trapezoid` with no plateau. Most tries land at the peak (the middle of the range), fading in a straight line to none at both ends.
  - Some triangles reach outside the world (-64 to 319). Those tries are wasted, so the **in world** figure is what really lands (computed by `ore_shapes.py`).
- **Blob size:** the ore feature's `size`. It is a size setting, not a block count; S1b's probes give the real blocks per blob.
- **Discard on air:** `discard_chance_on_air_exposure`. Each would-be ore block that touches air (a cave, a ravine, the open surface) is skipped with this chance. At 1.0 the feature is "buried": it never shows on a cave wall. Water does not count as air.
- **Host rock (the targets):**
  - Each ore feature has two targets: the stone ore, which replaces stone, granite, diorite and andesite (`stone_ore_replaceables`), and the deepslate ore, which replaces deepslate (`deepslate_ore_replaceables`).
  - **New in 26.3:** tuff is "height-specific" (`height_specific_ore_replaceables`). An ore landing in tuff becomes the stone ore at Y 0 and above, and the deepslate ore at Y 8 and below. In the overlap (0 to 8) the data lists the stone ore first. The documented rule is that the first matching target wins; S1b's tuff-band probe confirms it.
  - Nothing else is replaced: not dirt, gravel, water, air, or another ore.
  - Deepslate fills everything at Y 0 and below, nothing at Y 8 and above, and a random mix in between (`material_rule/overworld/underground.json`). Bedrock is -64 to -60. Sea level is 63.
- **Order:**
  - All ores run in step 6 (`underground_ores`), in the biome's list order: dirt, gravel, granite, diorite, andesite, tuff, then coal, iron, gold, redstone, diamond, lapis, copper. In badlands `ore_gold_extra` comes after copper; in the emerald biomes `ore_emerald` comes last.
  - The rock blobs come first, so ores later land inside granite, diorite, andesite and tuff. That matters for the look (section 3).
  - Infested stone runs in step 7 (`underground_decoration`).
- **"Every biome":** all 56 biomes in the reference.

## 2. The vanilla baseline, per ore

### 2.1 Table

| Ore | Placed feature (feature) | Where: Y range, shape | Tries per chunk (in world) | Blob size | Discard on air | Biomes |
|---|---|---|---|---|---|---|
| **Coal** | `ore_coal_upper` (`ore_coal`) | 136 to 319, uniform | 30 | 17 | 0 | every biome |
| | `ore_coal_lower` (`ore_coal_buried`) | 0 to 192, triangle, peak 96 | 20 | 17 | 0.5 | every biome |
| **Copper** | `ore_copper` (`ore_copper_small`) | -16 to 112, triangle, peak 48 | 16 | 10 | 0 | every biome except dripstone caves |
| | `ore_copper_large` | -16 to 112, triangle, peak 48 | 16 | **20** | 0 | dripstone caves only (replaces the one above) |
| **Iron** | `ore_iron_upper` (`ore_iron`) | 80 to 384, triangle, peak 232 | 90 (82) | 9 | 0 | every biome |
| | `ore_iron_middle` (`ore_iron`) | -24 to 56, triangle, peak 16 | 10 | 9 | 0 | every biome |
| | `ore_iron_small` | -64 to 72, uniform | 10 | 4 | 0 | every biome |
| **Gold** | `ore_gold` (`ore_gold_buried`) | -64 to 32, triangle, peak -16 | 4 | 9 | 0.5 | every biome |
| | `ore_gold_lower` (`ore_gold_buried`) | -64 to -48, uniform | 0 or 1 (0.5 average) | 9 | 0.5 | every biome |
| | `ore_gold_extra` (`ore_gold`) | 32 to 256, uniform | **50** | 9 | 0 | badlands, eroded badlands, wooded badlands |
| **Redstone** | `ore_redstone` | -64 to 15, uniform | 4 | 8 | 0 | every biome |
| | `ore_redstone_lower` (`ore_redstone`) | -96 to -32, triangle, peak -64 | 8 (4.1) | 8 | 0 | every biome |
| **Lapis** | `ore_lapis` | -32 to 32, triangle, peak 0 | 2 | 7 | 0 | every biome |
| | `ore_lapis_buried` | -64 to 64, uniform | 4 | 7 | **1.0** | every biome |
| **Diamond** | `ore_diamond` (`ore_diamond_small`) | -144 to 16, triangle, peak -64 | 7 (3.5) | 4 | 0.5 | every biome |
| | `ore_diamond_medium` | -64 to -4, uniform | 2 | 8 | 0.5 | every biome |
| | `ore_diamond_large` | -144 to 16, triangle, peak -64 | 1 in 9 chunks (1 in 18) | 12 | 0.7 | every biome |
| | `ore_diamond_buried` | -144 to 16, triangle, peak -64 | 4 (2) | 8 | **1.0** | every biome |
| **Emerald** | `ore_emerald` | -16 to 480, triangle, peak 232 | 100 (79) | 3 | 0 | 10: the 7 mountain biomes (frozen, jagged and stony peaks, snowy slopes, the 3 windswept), plus meadow, grove, cherry grove |
| **Infested** | `ore_infested` (step 7) | -64 to 63, uniform | 14 | 9 | 0 | the same 10 as emerald |
| **Deepslate variants** | none of their own | each feature's second target | — | — | — | where the host is deepslate (Y 8 and below), or tuff at Y 8 and below |

**Ore veins** (the noise router's material rules, run in Generate, not Decorate; S4):

| Vein | Y range | Full strength | Ore block | Filler | Bonus block | Where |
|---|---|---|---|---|---|---|
| **Copper** | 0 to 49 | 20 to 29 | `copper_ore` (always the stone variant) | granite | `raw_copper_block` (`raw_ore_chance` 0.02) | where the vein toggle noise is +0.4 or more |
| **Iron** | -60 to -9 | -40 to -29 | `deepslate_iron_ore` (always the deepslate variant) | tuff | `raw_iron_block` (0.02) | where the toggle is -0.4 or less |

How the vein data reads (`density_function/overworld/ore_vein/*.json`):
- **Kind:** `toggle` (noise `ore_veininess`, scale 1.5, only from Y -64 to 56) picks the kind. Strongly positive regions get copper, strongly negative regions iron, and the middle (-0.4 to 0.4) gets no veins.
- **Shape:** `mask` needs both ridge noises (`ore_vein_a` and `ore_vein_b`, scale 4) to be near zero (within 0.08). That carves long, thin, winding ribbons.
- **Fade:** within 20 blocks of either end of a vein's Y range, the toggle threshold rises from 0.4 to 0.6, so veins thin out toward the ends.
- **Richness:** `richness` maps the toggle's strength (0.4 to 0.6) to 0.1 to 0.3, so a vein's core holds more ore.
- **Gaps:** `gap` (noise `ore_gap`) skips ore in patches below -0.3, leaving filler there.
- **Not in the data:** the in-vein rule (the solidness 0.7 share, richness as the ore chance per block, 2% of ore as raw blocks) is published in words only. S1b's `ore_vein_samples.json` pins it.

**The rock blobs the ores land in** (same step, earlier in the list; replace any `base_stone_overworld` block):
- Granite, diorite and andesite: 2 tries each at 0 to 60, plus 1 in 6 chunks at 64 to 128, size 64.
- Tuff: 2 tries at -64 to 0, size 64.
- Dirt: 7 tries at 0 to 160, size 33. Gravel: 14 tries over the whole world, size 33.
- Clay: lush caves only, 46 tries, size 33.

### 2.2 What a player sees

- **Coal:** the surface and mountain ore, in the biggest blobs.
  - Above Y 136, mountains are speckled with fully exposed coal (30 tries, nothing discarded).
  - Between 0 and 192 there is more coal (peak 96), but half of it is removed where it would touch a cave.
  - Deepslate coal is rare. Coal never goes below 0, so it only appears where deepslate pokes into the 0 to 8 mixed band.
- **Copper:** the ore of the first caves under the surface (peak 48, just below sea level), never hidden.
  - Dripstone caves get blobs twice the size, so their walls are streaked orange and green.
  - Copper veins (Y 0 to 49): long granite ribbons studded with copper and the odd raw copper block, which you follow through cave walls.
- **Iron:** two homes.
  - High mountains: 82 tries above Y 80, so cliffs and peaks are dotted with exposed iron. Flat land gets almost none of it, because those tries land in the air.
  - A modest band around Y 16, plus small 4-blobs all the way down.
  - Iron veins (Y -60 to -9): tuff ribbons with deepslate iron and raw iron blocks.
- **Gold:**
  - Normally deep and shy: peak at -16, half the exposed blocks removed, plus a trickle at the very bottom.
  - Badlands: 50 extra tries from 32 to 256 with nothing discarded, so caves and stone under the terracotta glitter with gold. Terracotta itself is never replaced.
- **Redstone:** the deep red ore, never hidden.
  - 4 tries spread evenly up to Y 15, and 4 more piling up toward the bottom (-64).
  - Below Y 0 it is the most common exposed ore (by tries times size, before veins).
- **Lapis:** mostly a branch-mining ore.
  - 2 visible tries peak at Y 0, the stone and deepslate seam.
  - 4 buried tries (-64 to 64) never touch a cave, so tunnelling finds about twice what caving shows.
- **Diamond:** clusters just above the deepslate bottom.
  - Densest at -64, fading to none at Y 16.
  - Every exposed diamond feature drops half (the large blob 70%), and the buried feature never shows. Cave walls show small specks; the rich finds are inside solid deepslate.
  - A 12-blob turns up about once per 18 chunks.
- **Emerald:** single specks (size 3) through the mountain itself, peaking high (Y 232), not in deep caves.
  - 100 tries per chunk with nothing discarded, so they show on cliff faces.
  - Only the 10 mountain-like biomes have them.
- **Infested stone and deepslate:** look exactly like stone and deepslate.
  - Silverfish traps in the same 10 biomes, below Y 63.
- **Deepslate variants:** the same ore, on a deepslate background, wherever the host is deepslate.
  - Stone ores sit on a grey stone background even when the host is granite, diorite, andesite or tuff above 0. Those are visible grey squares in pink, white or dark rock (section 3.2).

### 2.3 Numbers S2 needs beyond the table

- **Tries per column** (ore features only, rock blobs included):
  - About 241 in an ordinary biome.
  - About 355 in an emerald biome (+100 emerald, +14 infested), about 291 in badlands.
  - Magma (44 to 52) and the disks are extra.
- **Wasted tries:**
  - In flat land, about 112 of those (iron upper and coal upper) land above any stone.
  - An exact early-out skips a try whose blob box lies wholly above the highest solid block of the 3x3 snapshot. It is exact because the ores replace nothing but rock.
  - It only stays exact if skipping does not shift the random stream for later tries. Seed each try from (feature seed, column, try index), which follows Jesus's answer 4.
- **Probes S2 should match (from S1b):**
  - blocks per blob per `size`;
  - the tuff band (Y 0 to 8) target;
  - the exposed against solid ratio for discard 0.5, 0.7 and 1.0.

## 3. Look-call proposals (after S2 lands)

Each one is a switch whose default (0 or `vanilla`) is the 26.3 baseline above. L1 and L2 are render-only: the saved world, the block ids and the goldens do not change. L3 changes where ore blocks sit, so it is a worldgen switch with its own goldens. Vanilla's own big look win, the large ore veins, is already S4 and on by default (Jesus's answer 4).

### L1. Ores wear their host rock (`voxel.Look.OreHost 0|1`)

- **The problem it fixes:** vanilla draws every stone ore on grey stone, even inside granite, diorite, andesite or tuff. The rock blobs run first in step 6, so many ores sit in them as grey squares in pink, white or dark rock. The veins make it worst:
  - A copper vein puts `copper_ore` (grey stone background) inside granite.
  - An iron vein puts `deepslate_iron_ore` inside tuff.
- **What it changes:** an ore whose host is granite, diorite, andesite or tuff draws its ore pixels on that rock. Stone and deepslate hosts keep the vanilla texture byte for byte.
- **Measured on the 26.3 textures** (`tools/ore_palette.py`):
  - The vanilla ore art redraws the stone around each speck, so "ore minus stone" is not a clean mask.
  - The pixels whose colour is outside the host palette are a clean mask: 25 to 38% of each stone ore (stone has 4 greys), 18 to 34% of each deepslate ore (deepslate has 5).
  - Two ways to build the variants, both at import, from the reference textures only:
    - **(a) Composite:** draw the ore-palette pixels over the real granite, diorite, andesite or tuff texture. The rock's own pattern shows.
    - **(b) Palette swap:** map stone's 4 greys onto 4 of the host's tones by brightness. Vanilla's drawing is kept exactly, but the background is stone's pattern in the rock's colours.
  - Recommend (a), with (b) as a second capture if (a) looks noisy.
  - The variants: 8 ores x 2 bases x 4 hosts = 64 extra 16x16 textures.
- **Which host:**
  - **(i)** The mesher takes the most common of the four rocks among the ore block's 6 neighbours. No storage and deterministic, but it can miss at blob edges.
  - **(ii)** The ore feature and the vein rule record what they replaced: 2 to 3 bits per ore block, or render-only states.
  - Recommend (i) for the look test.
- **Gameplay:** none. Same blocks, drops and saved ids.
- **Cost:** 0 in Generate and Decorate. The mesher reads the neighbours for ore blocks only (a small share of underground blocks). Plus one texture-array slice per variant.
- **Captures** (same seed, same spawn, `-SpawnExact`, switch 0 and then 1):
  1. An `EdgeWalls` cut through a copper vein (Y 20 to 30), after S4.
  2. The same through an iron vein in tuff (about Y -35).
  3. A cave wall through a granite or diorite blob with copper or iron (Y 0 to 60).
  4. A plain stone wall with ores (the control view).
- **Control pair:**
  - The world block hash is equal for 0 and 1.
  - View 4 is pixel-identical for 0 and 1 (stone hosts untouched).
  - In views 1 to 3, every changed pixel falls on an ore block (checked against an ore-only debug view).
  - Two runs at 0 are pixel-identical (the capture is deterministic).

### L2. Ores you can read: ore-aware mips plus glint (`voxel.Look.OreGlint 0|1|2`)

- **The problem:** 16x16 ore specks average into the rock at lower mip levels, so ores fade into the rock with distance and a cliff of iron can read as plain stone (to be confirmed by the level-0 captures).
- **Level 1:**
  - The ore textures get their own mip chain that keeps the ore pixels' share and colour (a max-pool on the ore mask, the ore colour averaged over ore pixels only).
  - Metal ores get low roughness on their ore pixels; gems get a sharper highlight. Ore catches the player's light and the sun on cliffs.
  - No light means no glint, so it is not an x-ray.
  - Silhouettes need nothing: the 26.3 textures already have the distinct per-ore shapes from 21w08a.
- **Level 2:** adds a faint emission on gem pixels (diamond, emerald, lapis, redstone), capped far below a torch and faded out by about 16 blocks.
  - This one moves gameplay a little: ores show in unlit caves at close range.
  - Vanilla's own precedent is narrower: redstone ore lights up (level 9) only when touched.
- **Gameplay:** none at level 1; small at level 2.
- **Cost:** mips are made at import. A few material instructions read the mask. 0 in worldgen.
- **Captures** (same seed and view, levels 0, 1, 2):
  - A lit cave wall at 8, 32 and 64 blocks.
  - A mountain cliff with coal, iron and emerald seen from 64 and 128 blocks.
  - An unlit cave (level 2 only).
- **Control pair:**
  - The world hash is equal at every level.
  - At 8 blocks, levels 0 and 1 differ only on ore pixels.
  - At 64 blocks, a colour classifier counts more ore-coloured pixels at level 1 than at level 0. The count at level 0 on two runs is identical.

### L3. Coal and mountain iron as seams (`voxel.Ore.Shape vanilla|seams`)

- **What it changes:** `ore_coal_upper`, `ore_coal_lower` and `ore_iron_upper` place flat lenses (about 3 high, wider) instead of round blobs.
  - Kept the same: the tries, the height shape, the targets, the discard, and the average blocks per blob (sized from S1b's measured counts).
  - These are the ores players see from outside, on hills and mountain faces. Flat coal beds are also what TerraFirmaCraft and Vintage Story do. Every other ore stays vanilla.
- **Gameplay:** small. Same totals and depths, but a find is a stripe you follow sideways instead of a ball.
- **Cost:** the same tries and roughly the same blocks, so Decorate stays inside 0.6 ms. Each lens touches more columns, so the write log spreads wider.
  - The lens radius must stay at most 16 so it never leaves the 3x3; `Decoration.Reach` enforces it.
  - Later ores can land where a seam now sits (or no longer sits), so a few of their blocks move too.
- **Captures** (same seed and spawn, `vanilla` and then `seams`):
  - An `EdgeWalls` cut through a mountain (the coal-upper and iron-upper band).
  - A cliff face at 64 blocks.
  - A cave wall around Y 96.
- **Control pair:**
  - At `vanilla`, the hashes equal S2's goldens.
  - At `seams`:
    - `OreHistogram` totals per feature are within 3% of `vanilla`, and the per-Y chi-square against the height provider still passes.
    - `Decoration.LoadOrder` and `Decoration.Reach` pass.
    - Every block that differs from `vanilla` is a coal or iron block, or one a seam covered or uncovered.

## 4. The options catalogue (research)

**One pipeline fact sorts these ideas:**
- **A Decorate feature** (vanilla's blobs) writes through S2's logs and must fit the 3x3 reach and the 0.6 ms gate.
- **A noise rule** (vanilla's veins) is a pure per-block function in Generate. It can be any size, has no logs, and pays per evaluated block instead (S4's 0.3 ms gate).
- Big deposits must be noise rules or region-seeded functions, never blob features.

### 4.1 Generation

| # | Idea (who does it) | What it changes | Gameplay vs vanilla | Cost here | Links |
|---|---|---|---|---|---|
| G1 | **Large ore veins** (Minecraft 1.18+) | Long winding copper (granite) and iron (tuff) ribbons with raw-ore blocks | none: it is vanilla | S4: Generate, no logs, 0.3 ms gate | A1 |
| G2 | **Seams and discs** (Vintage Story discs that follow the terrain; TFC coal and halite "broad level beds") | Flat lenses or beds instead of balls | small, if totals are kept (L3) | Decorate, wider log; radius 16 or less | A5, A8 |
| G3 | **Ores prefer a host rock** (Vintage Story and TFC rock-type rules; vanilla's own vein pairings) | Copper biased into granite, iron into tuff or andesite, and so on; counts raised to keep totals | small to medium: players learn "pink rock means copper"; the Y shape bends toward where the blobs are (0 to 60, below 0) | A rule-test change, plus re-tries | A5, A8 |
| G4 | **Grades by depth** (Vintage Story: poor to bountiful, deeper is richer; TFC: poor, normal, rich) | Several ore blocks per ore with different yields | medium: needs new blocks, drops and items | out of phase 6 scope (new blocks) | A5, A8 |
| G5 | **Rare big deposits** (TFC "massive, sparse, yet rare veins"; Geolosys plutons; Large Ore Deposits 300 to 500 blocks, 1 in 2400 chunks) | Few, huge, mixed-mineral bodies replace the many small blobs | large: feast or famine, needs finder tools | must be a noise or region function in Generate | A8 to A12 |
| G6 | **Surface hints** (Vintage Story loose ores, TFC small ores, Geolosys samples, 7 Days to Die boulders, Large Ore Deposits flower circles) | Small markers on the surface above a deposit | medium: makes ore findable from above | needs new blocks; a surface feature step | A5, A9, A13 |
| G7 | **Additive rare rich veins** (Rare Rich Veins mod, for MC 26.2) | Vanilla untouched, plus a rare rich bonus vein (for example diamond in 0.25% of chunks) | small: a bonus on top of vanilla | one more Decorate feature | A12 |
| G8 | **Tiers by depth band** (Terraria: one ore per tier per world, alternates set by the seed, shrinking diamond-shaped veins) | A per-world ore pick | large | n/a | A14 |
| G9 | **Amethyst geodes** (vanilla since 1.17) | Vanilla's own "pretty deposit": crystal shells | none: it is vanilla data (`amethyst_geode`) | a later decoration session | A2 |

**Verdict:**
- G1 is planned.
- G2 is the best "looks better, plays the same" generation change, hence L3.
- G3 is the next candidate after L3 if Jesus wants rock to mean something.
- G4 to G8 change the game, not the look. Revisit them with items and prospecting, not in phase 6.

### 4.2 Visual

| # | Idea (who does it) | What it changes | Gameplay | Cost here | Links |
|---|---|---|---|---|---|
| V1 | **Host-matched ores** (Excavated Variants generates an ore for every stone at runtime via Dynamic Asset Generator, with masking and palette switching; Underground Biomes overlays ores on the local stone; Unearthed blends ores into its 15 rocks) | Ore drawn on its real host rock | none | render-only (L1) | A15 to A18 |
| V2 | **Distinct ore silhouettes** (Minecraft 21w08a, 1.17: ores redesigned so they differ by shape, not only colour; reported as a colour-blind change) | Each ore readable without colour | none | already in the 26.3 textures | A3 |
| V3 | **Ore-aware mips** (Better Mipmaps and TexTweaks expose the mip settings; the rest is general graphics practice: custom mip chain, mild negative bias, a separate mask) | Ores stay visible at distance | tiny | import-time only (L2) | A19 |
| V4 | **Glint, not glow** (labPBR specular maps in shader packs, Better Visible Ores) | Metal and gem pixels catch light | none | material only (L2 level 1) | A20, A21 |
| V5 | **Emissive ore pixels** (OptiFine `_e` textures drawn at full brightness, no light cast; Emissive Ores and Bibu's Glowing Ores light only the mineral pixels; Deep Rock Galactic minerals glimmer in the dark; Terraria's Spelunker potion makes it a timed buff) | Ores visible in the dark | small to medium: a cave-finding aid. No source says how packs prevent the x-ray effect; cap, fade and scale with darkness | material only (L2 level 2) | A22 to A24, A14 |
| V6 | **Event glow** (vanilla redstone ore: light 9 and particles when touched, relaxes on a random tick) | A flourish on interaction | none: it is vanilla behaviour | needs block states and a tick; a later session | A4 |

## 5. Open questions for Jesus (each with a recommendation)

1. **Which look calls to capture first, once S2 lands?** Recommend L1 and L2 level 1: render-only, no gameplay change, no goldens touched. L3 after S4, so its cuts sit beside the veins.
2. **Glowing ores (L2 level 2)?** Recommend capturing it but keeping it off. It is the only visual option that changes play: diamonds become findable in the dark. Revisit if level 1 still reads poorly in caves.
3. **How L1 picks the host:** neighbours at mesh time, or stored by the ore feature? Recommend neighbours first (no storage, no save-format change). Store it only if the captures show wrong picks at blob edges.
4. **Composite or palette swap for L1?** Recommend composite (the real granite pattern shows). Capture the palette swap once beside it so the call is visual.
5. **Rock-type ores, grades, big deposits, surface hints (G3 to G6):** recommend none in phase 6. They need new blocks or items, or move balance; park them for the items and prospecting work. G3 is the only one possible with today's blocks.

## Appendix A: links

Dates and versions as the sources give them. "Unverified" means only secondary sources were read.

- **A1** Minecraft snapshot 21w37a (September 2021), the official notes on the 1.18 ore distribution and large ore veins: <https://www.minecraft.net/en-us/article/minecraft-snapshot-21w37a>.
  - Mojang's stated goals: coal and diamonds "reduced air exposure, so you will find more ... buried or underwater than exposed to air"; veins are "large, rare, snake-like".
  - Also "Caves & Cliffs Part II: The Features" (2021, fetch timed out): <https://www.minecraft.net/en-us/article/caves---cliffs-part-ii-the-features>. "The best way to mine" (2022, unverified): <https://www.minecraft.net/en-us/article/the-best-way-to-mine>.
- **A2** Amethyst geodes (1.17): in the reference as `worldgen/feature/amethyst_geode.json`.
- **A3** Minecraft snapshot 21w08a (February 2021): new ore textures and deepslate ores. <https://www.minecraft.net/en-us/article/minecraft-snapshot-21w08a>. Colour-blind framing per TechRaptor: <https://techraptor.net/gaming/news/new-minecraft-update-adds-colorblind-friendly-ores>.
- **A4** Redstone ore lighting (wiki mirror): <https://minecraftbedrock-archive.fandom.com/wiki/Redstone_Ore>.
- **A5** Vintage Story wiki, Ore Deposits (v1.20.7): <https://wiki.vintagestory.at/Ore_Deposits/en>.
  - Discs following the terrain, child deposits, host-rock caps on grade.
  - Example deposits: hematite radius 26 ± 10, 0.5 tries per chunk; galena radius 4 ± 4, 40 tries.
- **A6** Vintage Story, Prospecting: <https://wiki.vintagestory.at/Guide:Prospecting>.
- **A7** Vintage Story, WorldGen Concept (v1.19): <https://wiki.vintagestory.at/index.php/Modding:WorldGen_Concept>.
- **A8** TerraFirmaCraft Field Guide, "Ores and Minerals" (1.21.1, v4.2.10): <https://terrafirmacraft.github.io/Field-Guide/en_us/the_world/ores_and_minerals.html>.
  - "Massive, sparse, yet rare veins"; coal and halite as level beds in sedimentary rock; emerald and kimberlite as columns up to 100 tall; grades poor, normal and rich.
  - Vein config fields as shown by the TerraFirmaGreg modpack (rarity 1 per N chunks, density %, Y range, size or height and radius): <https://wiki.terrafirmagreg.team/modern/field-guide/de_de/tfg_ores/earth_vein_index.html>.
  - Hint-rock addon: <https://moddex.gg/mod/tfcgyres-orehints>.
- **A9** Geolosys (1.20.1, v7.0.14, July 2025; repository archived January 2026): plutons (dense, dike, sparse, layer, top layer), surface samples, prospector's pick. <https://www.curseforge.com/minecraft/mc-mods/geolosys>, <https://github.com/oitsjustjose/Geolosys>.
- **A10** Large Ore Deposits (Forge, 1.20.1): <https://www.curseforge.com/minecraft/mc-mods/large-ore-deposits>.
- **A11** Massive Ore Deposits: <https://www.curseforge.com/minecraft/mc-mods/massive-ore-deposits>. Immersive Engineering excavator veins (an extraction mechanic, not worldgen): <https://cleanroommc.com/groovy-script/mods/immersiveengineering/excavator>.
- **A12** Rare Rich Veins (Fabric, MC 26.2): <https://www.curseforge.com/minecraft/mc-mods/rare-rich-veins-1-1-0>.
- **A13** 7 Days to Die, Mining (wiki, several versions): <https://7daystodie.wiki.gg/wiki/Mining>.
- **A14** Terraria wiki, Ores: <https://terraria.wiki.gg/wiki/Ores>. Spelunker Potion: <https://terraria.wiki.gg/wiki/Spelunker_Potion>.
- **A15** Excavated Variants: <https://modrinth.com/mod/IZaMGxb0>. Dynamic Asset Generator: <https://www.modpackindex.com/mod/33257/dynamic-asset-generator>.
- **A16** Underground Biomes: <https://www.curseforge.com/minecraft/mc-mods/underground-biomes>.
- **A17** Unearthed (1.21.1 unofficial port): <https://www.curseforge.com/minecraft/mc-mods/unearthed-unofficial-port>.
- **A18** Geolosys on Modrinth (rock-specific ores): <https://modrinth.com/mod/geolosys>.
- **A19** Better Mipmaps: <https://modrinth.com/mod/better-mipmaps>. TexTweaks: <https://modrinth.com/mod/textweaks>.
- **A20** labPBR specular format: <https://github.com/rre36/lab-pbr/wiki/Specular-Texture-Details> (2020 copy), <https://shaderlabs.org/wiki/LabPBR_Materials>.
- **A21** Better Visible Ores: <https://www.modpackindex.com/mod/131563/better-visible-ores>.
- **A22** OptiFine emissive textures: <https://optifine.readthedocs.io/emissive_textures.html>.
- **A23** Emissive Ores: <https://www.modpackindex.com/mod/128894/emissive-ores>. Bibu's Glowing Ores: <https://www.modpackindex.com/mod/167481/bibus-glowing-ores>.
- **A24** Deep Rock Galactic wiki, Resources and Ores: <https://deeprockgalactic.wiki.gg/wiki/Resources>, <https://deeprockgalactic.wiki.gg/wiki/Ores>.
- Brief mentions, not used above:
  - Valheim copper deposits (mounds, Black Forest only): <https://gamewith.net/valheim/article/show/24993>.
  - Core Keeper (ore tied to biome and wall block): <https://core-keeper.fandom.com/wiki/Biomes>.
  - Hytale: no official source found.

## Appendix B: helper scripts

All in `C:\dev\sandbox\scout-ores\tools\`. They are read-only over the reference; run each with `uv run --no-project --python 3.13 [--with pillow] python -I <script> <dir>`.

- `biome_ores.py <worldgen dir>`: which biomes list each ore feature, and in which step.
- `ore_shapes.py <worldgen dir>`: the share of tries in the world and per band.
- `ore_masks.py`, `ore_palette.py <block texture dir>`: how each ore texture differs from stone and deepslate.
