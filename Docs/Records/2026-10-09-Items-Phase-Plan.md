# Items phase plan (Unnamed, 26.3)

Night of 2026-10-09, items session S1, run as a parallel lane (`feature/items-1`, worktree `C:\dev\projects\Unnamed-items`) next to the voxel chain's phase 6 S3. This record turns [scout F's research](2026-10-09-Items-Scout-Research.md) (its section 5, "Draft plan") into the phase plan, records Jesus's answers as decisions, and gives the short prompts for S1 and S2. S1 (this session) built the item reference, [`Reference/minecraft-items-26.3/`](../../Reference/minecraft-items-26.3/README.md); section 6 records it.

## 1. Decisions

Jesus answered on October 9, about 22:40, by tap to Nyx (recorded in [the voxel S3 handoff](2026-10-09-Voxel-Handoff-Phase6-S3.md), "Jesus's answers"). He said yes to every one of scout F's recommendations. These are settled; sessions build on them without asking again.

| # | Question | Decision |
|---|---|---|
| Q1 | The id of a non-block item | The 26.3 item registry name (`iron_pickaxe`, `stick`, `water_bucket`). A block's item keeps the block's id, so nothing is renamed. `CLAUDE.md` now says: "item IDs are the 26.3 item names; a block's item has the block's id" (S1). Only `air` and `wheat` share a block's id without being that block's item (`item_index.json`); neither is imported (wheat is a crop, not a full block). |
| Q2 | Where non-block item data lives | A new generated asset `DA_Items` (`UFoundationItemAssets`, `/Game/Items` through a new `UUnnamedProjectSettings::ItemRoot`), imported from the new hash-checked `Reference/minecraft-items-26.3/` (S1). `DA_Blocks` and its reference are not touched. `BuildRules` merges block items, then `DA_Items`, then project items (S2). |
| Q3 | `practice_pickaxe`, `practice_sword` and the loadout | They retire when the vanilla tools land (S2). The loadout becomes `stone, iron_pickaxe, iron_sword`; tests and the interaction probe move to vanilla ids, and both practice ids join the retired list. |
| Q4 | The torch | An item only: icon, held, in the creative tab, no placement (blocks stay full blocks). |
| Q5 | Drops | Straight to the inventory through the tool sessions. Item entities get their own session (S8). |
| Q6 | Ore drops | A small data-driven loot subset (S5b) from the jar's `loot_table/blocks/*.json`. Fortune, Silk Touch and explosion decay wait for enchantments. |
| Q7 | Held-item mesh | Approach C: a pure C++ builder makes the exact extruded mesh from each sprite's opacity mask at runtime, cached per item; item textures in a texture array. The masks are already in the reference (`textures.json`). |
| Q8 | Copper tools | In from the start: Tier A has all 7 materials. |
| Q9 | Hotbar and inventory icons | As vanilla: flat sprites for items, 3D icons for blocks. |

## 2. Scope

- **Tier A, the first playable set** (scout F's section 1, all in the reference now): the 35 tools; the 7 spears (data and sprites now, their combat in Tier C); the 14 ore and tier materials; stick; bucket, water bucket and lava bucket; apple, bread and cooked beef.
- **Tier B, cheap once Tier A's pipeline exists:** shears, torch (item only), flint and steel, arrow, ender pearl, snowball and compass. They start inert.
- **Tier C, separate systems, planned later:** spear combat, mace, trident, shield, bow and crossbow, armor, clock, map, elytra and totem.
- **Each tier or data family that a session needs** (Tier B items, loot tables) is added by rebuilding the item reference in that session: move the accepted folder away, extend the builder's lists, rebuild, verify. The reference is never edited in place.

## 3. Sessions

Sizes: S is about half a session, M one session, L may need a handoff. "Lane" says whether a session can run as a parallel lane next to the voxel chain or must wait for it.

| # | Area | Size | Depends on | Pins (tests) | Gate | Lane |
|---|---|---|---|---|---|---|
| S1 | **Item reference** | M | none | The reference verifier: hashes, re-derivation of every generated file, copies against the jar and the report, counts, scout F's appendix A values, worked break times; 4 mutations fail it | Verifier green; both older verifiers, `npm test` and `source-check.mjs` green | **Done** (this session, parallel) |
| S2 | **Item registry and rules** | M | S1 | `Unnamed.Foundation.Items.Registry`, `.Rules`; interaction probe with the new loadout | Every native suite; the three verifiers; voxel radius-8 hashes unchanged | Parallel |
| S3 | **Icons and tooltips** | M | S2 | `Unnamed.Hud.Creative.Tabs` with the new counts; tour shots of each tab; durability bar pixels against probe P2 | Tour accepted by the manager (look call) | Parallel |
| S4 | **Held-item rendering** | M-L | S2, S3 (the texture array) | `Unnamed.Foundation.Items.Mesh`: quad counts for known masks, closed silhouette, front and back mirror; shots per material against probe P1 | Shots accepted by the manager (look call) | Parallel |
| S5 | **Mining by tool and tier** | S | S2 | `Unnamed.Foundation.Mining.Tiers` against `facts/break_times.json` (every imported block, hand and 35 tools: ticks and drops); interaction scenario: iron ore with wooden, then stone pickaxe | Every native suite; one `CaptureVoxel.ps1 -Edits` run | Parallel; the capture takes a quiet turn on the main lane |
| S5b | **Ore loot subset** | S-M | S2, S5 | A drop table per block and tool; no Silk Touch or Fortune behaviour | Every native suite | Parallel |
| S6 | **Durability** | S | S2 (merge into S5 if small) | Wear sequences per kind; instant blocks cost nothing | Every native suite | Parallel |
| S7 | **Combat by item** | S-M | S2 | A damage table per tool at full and half charge; crits; the sweep truth table | Every native suite | Parallel |
| S8 | **Item entities** | L | S4; voxel S3 (streaming) | Pickup delay, merge box, stack copies, despawn; a tour shot against probe P3 | Every native suite; a flight or capture with drops | **Waits for voxel S3** |
| S9+ | **Behaviours:** buckets and eating (Tier A), then Tier B and Tier C systems | S each | S2 to S4 | Per system | Per system | Parallel |

**Order:** S1 (done) → S2 → (S3 ∥ S5) → S6 → S7 → S4 → S5b → S8, then S9+. S3 (Hud) can run beside S5 (Foundation). S5, S6 and S7 all edit the authority's rules, so they run one after another (or S6 folds into S5).

### Open dependencies on the voxel chain, by session

| Session | Waits for | Why |
|---|---|---|
| S2 | nothing | Foundation and settings only; no voxel files. The authority is shared with the voxel world, so the gate re-checks the voxel radius-8 hashes. |
| S3 | nothing | Hud only. |
| S4 (held items) | nothing | `FoundationInteractionView` and a new mesh builder; the first-person view already exists. |
| S5 (mining by tier) | nothing | It needs the voxel block table's tags (`Block.Mineable.*`, `Block.IncorrectForTool.*`, `Block.NeedsTool.*` in `Config/Tags/MinecraftBlockTags.ini`), which are already there. Only its one `-Edits` capture shares the main lane's capture tools, so it takes a quiet turn. |
| S5b (ore loot) | nothing | Ores already generate (voxel phase 6 S2) and mine through the same authority. Ore veins (voxel phase 6 S4) add raw ore blocks and need no new loot code. |
| S6, S7 | nothing | Authority rules only. |
| S8 (item entities) | **voxel S3, streaming stages** | Dropped items collide with, and despawn across, streamed columns. Start after S3 is merged and its streaming states are settled. |
| S9 buckets | nothing for static sources | Water and lava are still sources in the voxel world, so a bucket takes or places a source through the authority's edit path. Flowing fluids would be a later world feature. |
| Inventory saves | voxel phase 10 (world saves) | Q1 is settled now, before phase 10 writes the first inventory to disk. |

### Shared-resource rules for parallel lanes

- One editor build at a time (`-WaitMutex`); no native suites while another worker's suites run; editor runs (`ImportItems.py`) take turns the same way.
- Commits on the lane's own branch; the manager merges and fast-forwards. Merges keep every records-index entry.
- `CLAUDE.md` edits are limited to what Jesus approved for that session, made at a quiet moment for the voxel worker.
- A vanilla-client probe (P1, P2, P3, P5) is a Java run: check `Get-Process java, javaw, UnrealEditor*` first, keep it short, black box only.

## 4. Session details

### S2: item registry and rules

The handoff is [2026-10-09-Items-Handoff-S2.md](2026-10-09-Items-Handoff-S2.md); in short:

- **Asset:** `UFoundationItemAssets` `DA_Items` in `/Game/Items` (`UUnnamedProjectSettings::ItemRoot`; Python `ProjectPaths`). It holds the 63 items' definitions, the reference's SHA-256 and each sprite's opacity mask (for S4).
- **Import:** `Scripts/Editor/ImportItems.py` reads `Reference/minecraft-items-26.3`, and verifies an existing `DA_Items` instead of replacing it, like `ImportBlocks.py`. Item tags become `Item.*` gameplay tags in `Config/Tags/MinecraftItemTags.ini` (the block tags' pattern).
- **Rules:** `BuildRules` merges block items (`AddBlockItems`, unchanged), then `DA_Items`, then project items. An id clash is an error. Tool rules map to the existing `MiningRules` (scout F's section 4 table; no new fields for Tier A). Single-block selectors (the swords' `minecraft:cobweb`) and the empty `sword_instantly_mines` stay in the data and match nothing until those blocks exist.
- **Loadout and retirement (Q3):** `stone, iron_pickaxe, iron_sword`. Tests and the interaction probe move to vanilla ids; `practice_pickaxe` and `practice_sword` join the retired ids. Until S3 and S4, the held view keeps today's `Icon` key (`pickaxe` or `sword` by kind).
- **CLAUDE.md (S2's edits):** the retired ids, the `ItemRoot` path rule and the Commands line for the item reference's verifier. Each needs the manager's approval within Jesus's decisions.
- **Tests:** `Unnamed.Foundation.Items.Registry` (every item's numbers against `items.json`; no id clash with block items) and `.Rules` (selectors resolve to `Block.*` tags; first-rule semantics for speed and drops).

### S3: icons and tooltips

- **Textures:** a `T_ItemIcons` atlas and the `T_ItemTextures` array from `textures.json` (70 sprites, all cutout; the 7 spear in-hand textures are 32 x 32 and go in a second array or a 32 x 32 atlas region).
- **Creative tabs:** Tools & Utilities, Combat, Food & Drinks and Ingredients show their known items, at the reference's `creative` positions.
- **Hotbar:** sprites for items, 3D icons for blocks (Q9).
- **Durability bar:** 13 x 2 px under the icon; its colour ramp from probe P2.
- **Tooltips:** the "When in Main Hand" attack lines from the jar's lang keys.

### S4: held-item rendering

- **Mesh:** the mask-to-mesh builder (Q7 C), pure and unit-tested. Front and back quads one pixel apart, plus a side face at each opaque-to-transparent edge.
- **Material:** a cutout item material.
- **Transforms:** the display transforms from `models.json` (`generated`, `handheld`, `spear_in_hand`; later `handheld_mace` and `handheld_rod`), and the hand anchor measured with probe P1.
- **Scope:** it replaces the cube pickaxe and sword. Third person waits for a player body.

### S5: mining by tool and tier

- **Rules:** all 35 tools on the real tags, the sword rules, and `can_destroy_blocks_in_creative` (false for swords).
- **Delay between breaks:** 6 ticks per the wiki; probe P5 confirms it.
- **Truth table:** S5 decides how the project's float mining step meets the exact table (section 6, "Finding"). Recommended: probe P5 first (dirt with an iron shovel, a whole-inverse case), then either match exactly or document the one-tick tolerance on whole-inverse entries.

### S5b, S6, S7, S8, S9+

As scout F's section 5:
- **S5b:** extends the reference with the ores' loot tables and drop items. Random counts (lapis, redstone) use the project's seeded randomness.
- **S6:** wear of 1 or 2 per block or hit; breaking, with its sound and event.
- **S7:** cooldown, sweep, knockback, and the axe's shield-disable as data.
- **S8:** the wiki's item-entity rules.
- **S9+:** buckets (stack 16 and 1; take or place a still source) and eating (`food`, `consumable`, `FoundationVitals`), then Tier B and C.

## 5. Probes for later sessions (scout F's appendix C)

| Probe | For | What |
|---|---|---|
| P1 | S4 | Hand pose, swing and equip shots in the 26.3 client (FOV 70, 1920x1080, GUI scale 2). |
| P2 | S3 | Durability bar colours at known damage values. |
| P3 | S8 | Dropped item spin, bob and copy counts. |
| P5 | S5, S6 | The delay between breaks; a whole-inverse break time (iron shovel on dirt); hoe and sword wear. |

## 6. S1: what was built

`Reference/minecraft-items-26.3/` (see its [README](../../Reference/minecraft-items-26.3/README.md)): 281 hashed files, 1.9 MB.

- **Items:** `items.json` holds the 63 Tier A items: components (tool rules with the documented defaults, max damage, attack totals and raw modifiers, repair with resolved ids, enchantability, food), item tags, creative positions, the model definition and the sprites.
- **Models and sprites:** `models.json` (70 models, chains, resolved display transforms) and `textures.json` (70 sprites, hashes and opacity masks).
- **Tags:** `tags.json` (43 item tags, 16 block tags).
- **Index:** `item_index.json` lists all 1,658 items, with the block each places and its definition kind.
- **Break times:** `facts/break_times.json` covers the hand and the 35 tools on all 1,286 blocks; `facts/break_times.md` shows scout F's 21 samples.
- **Copies:** the 63 report files, the item registry, and 206 jar files (63 definitions, 73 models, 70 sprites).
- **Tools:** `build_reference.py` and `verify_reference.py`. Both run the same `derive()`, so the verifier re-derives every generated file from the folder's copies and the jar.

**Verified:**
- The new verifier is green. Its four mutations each fail it:
  - an item's max damage changed;
  - the same, with the sums rewritten (re-derivation and the spot value catch it);
  - one SHA-256 line corrupted;
  - a report copy changed and re-summed (re-derivation and the report comparison catch it).
- The block and worldgen verifiers are green, `npm test` passes 57 of 57, and `source-check.mjs` passes.
- `CLAUDE.md` has exactly the two approved edits.

**Finding: scout F's break-time table is one tick long in some whole-number cases.** The exact formula gives 45 ticks (2.25 s) for a wooden pickaxe on deepslate or coal ore. Scout F's `break_times.py` (its line 70, in double precision) computes 1 / d as 45.00000000000001 and rounds it up to 46 (2.30 s). 1 / d is a whole number in 35,393 of the table's 46,296 entries, so the game's float steps matter there; S5 settles it with probe P5 before pinning ticks.

## 7. Short prompts

**S1 (this session, as run):**

> Items phase, session 1 (a parallel lane): the items plan record and the item data reference. Work in `C:\dev\projects\Unnamed-items` on `feature/items-1` (from `feature/voxel-world` at `8f91b1f`); never touch the main lane's folder. Read scout F's research (`Docs/Records/2026-10-09-Items-Scout-Research.md`), Jesus's answers in the voxel S3 handoff, `CLAUDE.md`, `Docs/Blocks.md` and the block reference's README and tools. Build: (a) this plan record; (b) `Reference/minecraft-items-26.3` with its own tools, Tier A first, from the 26.3 jar and the data generator's item components report, plus the break-time truth table; (c) the two approved `CLAUDE.md` edits (the id rule; the new hash-checked reference). No Unreal code or assets. Send the manager a short plan before building.

**S2 (next):**

> Items phase, session 2: the item registry and rules (`DA_Items`, `ImportItems.py`, `ItemRoot`, the `BuildRules` merge, the loadout switch and retiring the practice tools). Read `Docs/Records/2026-10-09-Items-Handoff-S2.md` first and follow it; it holds the reading list, the decisions, the scope, the tests and the gate. Send the manager a short plan before building.
