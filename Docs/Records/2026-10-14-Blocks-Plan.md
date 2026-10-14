# Blocks plan: survival track T5 (B0-B7), with B0 and B1 ready for a go

T5-plan, October 14, 2026, lane `feature/survival-blocks` (from `9822d24`). Plan only: nothing is built. Sources: scout T5-A's `C:\dev\sandbox\scout-blocks\facts.md` (and its `notes\family_probe.json`, `probes\out\`), the survival `PROGRAM.md` sections 3-6, the torch record [2026-10-10-Torch-T2.md](2026-10-10-Torch-T2.md) (the recipe B1 copies) and the code at `9822d24` by line range.

Settled by SM-Blocks (vanilla first, in the track file): the chest block is a stand-in box; beds skip the night and set the spawn point (the monster check is vacuous until mobs exist); buttons and levers change only their own `powered` state, and iron doors stay shut; the nether vines are climbable and enter in B1. Answered by Jesus (through the PM, October 14): B1 imports all 302 blocks; the use sounds are one program-wide batch, built in this lane as T5's first build session, **B0**, before B1 (`C:\dev\tools\shift-managers\survival\sounds-batch.md`).

## 1. The session split

Every session adds a console variable whose `0` is today's behaviour and ships with it on once its checks pass. Hot files are the program's list (section 4); "none" means no hot file.

| # | Session, size | Waits on | Scope | Switch (0 = today) | Acceptance and tests | Hot files |
|---|---|---|---|---|---|---|
| **B0** | Sound batch (S-M, mechanical) | the PM's deadline for `sounds-batch.md` | Every track's missing sound events appended to `Reference/minecraft-sounds-26.3` through its own tool; the tool made append-safe for B1's cascade; `DA_Sounds` reimported once. Section 2A | none (data only: nothing plays the new events until a step calls `PlayEvent`; `foundation.Sound.Events` already gates playing) | Sound verifier, append proof, regeneration proof, `ImportSounds.py` second run no-op, the sound tests, RunChecks `final` | `Docs/Records/README.md` only |
| **B1** | Reference extension (M-L, mechanical) | nothing | All 302 family blocks plus the PM's 8 (cauldrons, fires, campfires) as data (Jesus: all 302): reference, facts, import, creative positions; each places its default state, as model blocks did before S7. `poplar_sapling` joins `VegetationBlocks`. The chest stand-in box. The survival matrix, `Blocks.Survival` and the state gallery narrowed. Section 2 | `foundation.Blocks.Families`: 0 = the B1 blocks are absent from the creative inventory and the gallery tail, and refuse placing | Verifiers, append proof, regeneration proof, second imports no-op, radius-8 hashes and `Mesher.Digest` unchanged, the poplar control, all native suites, RunChecks `final` with the flight. New `FoundationFamilyTests.cpp` | `FoundationAuthority.cpp` (one guard in `CheckPlacement`), the creative list builder (`FoundationPanels.cpp` or `FoundationItems.cpp`, a filter of a few lines), `Docs/Items.md` (one line), `Docs/Records/README.md` |
| **B2** | Shapes and connections (M) | B1 | Stairs (facing, half, shape), fences, walls, panes and bars, gate facing; `GetStateForPlacement` for them; the one-ring neighbour pass `UpdateNeighbourShapes` beside `SettlePlants`; both collision gatherers one cell lower (1.5-tall fences, walls, closed gates; the pitcher crop's -1/16). A new FactsDump table of `BlockState.updateShape` answers (public API, the same `LevelReader` proxy) is the oracle for connections and stair shapes | `foundation.Blocks.Shapes` | Placement and neighbour results equal the `updateShape` table for every family (sampled rows); collision tests for 1.5-tall boxes; `Blocks.PlantShapes`' out-of-cell pins become boxes that intersect the region; Playground probe cost of the extra gather layer against base | `FoundationAuthority.cpp` (two call lines in `UseOn` and `BreakBlock`); logic in new `FoundationBlockFamilies.h/.cpp`. Also `VoxelWorld.cpp` (shared with T2 and T6 collision; not hot, but diff it) |
| **B3** | Interactive blocks (M) | B1 + **T1 step 7** (`foundation.Blocks.Use` in `UseOn` step 1) | Doors (two-half placement, hinge, open both halves), trapdoors (half from the click), fence gates (open, re-face away), buttons (`powered`, unpower after 20 or 30 ticks on an authority timer list, cancelled on break), levers; iron doors and trapdoors refuse hand use; door, button and lever survival rules in `VPS`; an `Interacted` event; the B0 sounds, picked per block from B1's `block_use_sounds.csv` | `foundation.Blocks.Interact` (under T1's `foundation.Blocks.Use`) | Each family's toggle and timer; survival rows against `family_survival.json` (section 2.4); hinge and `in_wall` from the `updateShape` table or a P3-style placement probe (light; the SM lists it if it needs a vanilla client) | `FoundationAuthority.cpp` (one dispatch line in T1's branch, the timer step), `FoundationInteraction.cpp` only if the client use path needs it |
| **B4** | Climbing (S-M) | B1; diff T4's `FoundationSimulation.cpp` | Ladder placement (wall-attached, facing) and survival; climbing for `#climbable` (ladder, vine, cave vines, weeping and twisting vines, an open trapdoor over a same-facing ladder): fall speed clamp, climb on push or jump, hold on sneak, fall distance zeroed | `foundation.Blocks.Climb` | Movement tests at the wiki's speeds (marked provisional until a vanilla movement probe, which needs Jesus's go); ladder survival rows | none (`FoundationSimulation.cpp`, `VPS`) |
| **B5** | Farming A: farmland and path (S) | B1 + **T3's use-on-block branch** (hoe, shovel) | Hoe tills dirt-like blocks, shovel makes a path, when the block above is air; revert to dirt under a solid block (the survival table's `up` probe, through `SettlePlants`' pass); trampling on landing (chance = fall distance - 0.5, wiki) | `foundation.Blocks.Farmland` | Till and path tests, revert rows, a trample test with a seeded roll | `FoundationAuthority.cpp` (inside T3's branch) |
| **B6** | Farming B: random ticks, crops, saplings (M) | B5 | `FVoxelRandomTicks` in `UnnamedVoxel` (3 per loaded section per tick, a pure function of seed, salt, section and tick); crop growth (wiki formula; sky light read as 15 until a light field exists, flagged); moisture; a sapling grows by running its tree feature at that cell. Seeds and crop items: the items reference gains the crop items (wheat, carrot, potato, beetroot and seeds, torchflower seeds, pitcher pod, melon and pumpkin seeds) as an appended group, and the crop families' loot tables join (section 2.6) | `voxel.RandomTicks` (and `foundation.Blocks.Crops`) | Determinism test (same seed and tick, same cells); growth rates against the wiki within tolerance; a sapling-to-tree test; the per-tick cost in the Playground probe and the flight. The radius-8 capture must run with ticks off or before the first tick (state which) | none expected (`FoundationItems.cpp` if seeds need a place link) |
| **B7** | Beds (M) | B1 + **T1 step 7** + **T4** (respawn, death screen) | Two-part horizontal placement; use: sleep when the clock allows, skip to morning, set the spawn point in the player's saved state that T4's respawn reads; bounce on landing; "You can sleep only at night" message | `foundation.Blocks.Beds` | Sleep window from `VoxelWorldClock` (wiki times), spawn point save and load, bounce height (wiki, provisional) | `FoundationAuthority.cpp`, `Hud/*` (message), `FoundationSimulation.cpp` |
| — | Chest behaviour | T1 "18-chest" | B1 carries the chest's data and stand-in look; T1 brings the block-entity store and `generic_54` | T1's | — | — |

B2 and B4 do not wait on other tracks, so they follow B1 directly; B3 and B7 take T1 step 7's `UseOn` branch as a merge point. Every session after B1 keeps its logic in `FoundationBlockFamilies.h/.cpp` (new) with single call lines in the authority, to keep `FoundationAuthority.cpp` conflicts small (scout risk 6). No session after B1 rebuilds the blocks reference except to append a family's loot or sounds (serialise with T1 if it ever rebuilds it).

## 2. B1, ready for a go

### 2.1 Which blocks enter

All 302 blocks of scout T5-A's inventory, 23,543 states: 97 stairs, 14 fences, 13 fence gates, 32 walls, 26 panes and bars, 22 doors, 22 trapdoors, 15 buttons, the lever, 17 pressure plates, the ladder, 4 nether vines (weeping and twisting, each with its plant part), 16 beds, 3 chests (chest, trapped chest, ender chest), farmland and dirt path, 8 crops, 9 saplings (oak, spruce, birch, jungle, acacia, cherry, dark oak, pale oak, poplar).

Plus the PM's 8 for T3 (fire damage, buckets) and later cooking, measured from the jar (blockstates, models, textures; `C:\dev\sandbox\scout-blocks` probe script in this session's notes): all 8 fit the way the reference imports model blocks (block models with elements; animated textures through `.mcmeta`, as water and lava already are), so none is left out.

| Block | States | New textures | Notes |
|---|---|---|---|
| `cauldron` | 1 | `cauldron_top`, `_side`, `_bottom`, `_inner` (4, shared by all four) | item `cauldron` |
| `water_cauldron` | 3 (`level`) | none (`water_still` is the water's) | no item of its own (`vanilla_item` cauldron); the water quad's tint follows the water's (check `TINT_SOURCES`) |
| `lava_cauldron` | 1 | none (`lava_still`) | as above |
| `powder_snow_cauldron` | 3 | none (`powder_snow`) | as above |
| `fire` | 512 (`age` x 5 sides) | `fire_0`, `fire_1` (32 frames each) | multipart; no item; spreading and burning need random ticks (B6) |
| `soul_fire` | 1 | `soul_fire_0`, `_1` | no item |
| `campfire` | 32 | `campfire_fire`, `campfire_log`, `campfire_log_lit` | item (sprite); a block entity in vanilla: the cooking items its renderer draws are absent until a cooking step (gap, not a stand-in) |
| `soul_campfire` | 32 | `soul_campfire_fire`, `soul_campfire_log_lit` | as above |

None is named by an overworld feature (tags only: `cauldrons`, `replaceable`, `blocks_motion_no_leaves`), so they carry no world-hash risk. They join `APPENDED_BLOCKS` after the 302, in that order, with a `cauldrons_fire` model group.

**Totals:** 310 new blocks and 24,128 new states: 926 blocks and 28,552 states. **Texture codes: 691 + 246 (families) + 13 (these 8) + 18 (chest stand-in tiles) = 968 of 1,024, 56 left.** Array slices: 946 + 246 + 157 + 18 = 1,367 of 2,048. About 1,000 new baked models after the 308.

### 2.2 The reference tools (copy T2; new entries last, no index shift)

- **`build_manifest.py`:** one `MODEL_GROUPS` entry per family after `"torch"` (stairs, fences, fence_gates, walls, panes, doors, trapdoors, buttons, lever, plates, ladder, nether_vines, beds, chests, farmland_path, crops, saplings). Every family is a model block (shape `model`), as the torch is; beds bake from their 26.3 block models (`template_bed_head/foot`). The chest group is the stand-in (2.3).
- **`build_reference.py`:** `APPENDED_BLOCKS = ["torch", "wall_torch", <the 302 in family order>, <the 8 cauldron and fire blocks>]`, so their new textures (691+) and models (308+) and their 3D item icons follow every earlier one. Blocks, states, items and facts stay in registry order (T2's precedent: those are keyed by id and state id, never by position). Two new rules:
  - `ITEMS_REFERENCE_ITEMS = {the 9 saplings}`: their block item already exists in the items reference (loot step 2b's `drop` group), so the blocks reference makes no item for them and records `vanilla_item` (as for cocoa). Without it `oak_sapling` would exist in both `DA_Items` and `DA_Blocks`.
  - The chest stand-in model and tiles (2.3).
- **`FactsDump.java`:** `dumpStates`, `dumpSupport`, `dumpSounds` and `block_facts.csv` take the longer id list unchanged; their files grow by the new blocks only. `dumpSurvival` is narrowed (2.4).
- **`verify_reference.py`:** the new counts (926 blocks, 28,552 states, 395 model blocks, items, models, textures, budgets); the append invariant already generalises over `APPENDED_BLOCKS`; new checks: no item id shared with the items reference's `items/`; the chest tiles re-derived from the jar (2.3); `plant_survival.json`'s subjects equal the frozen base list; `family_survival.json`'s rows for base subjects and base candidates equal `plant_survival.json`'s (a free regression proof).
- **README and `doc_template.md`:** the family groups, the chest stand-in, the two survival files.

### 2.3 The chest stand-in box (decided)

A 14/16 box (`[1,0,1]`-`[15,14,15]`, vanilla's chest footprint and height per minecraft.wiki) for `chest`, `trapped_chest` and `ender_chest`, all 56 states drawing the single box, rotated by `facing`; pairing (`type=left/right`) draws two single boxes until T1's chest step. The jar's blockstates name only a particle model, so `build_reference.py` holds the stand-in model as data (one element, six faces) and marks the blocks `stand_in: true` in the manifest. Its faces come from `textures/entity/chest/normal.png` (`trapped.png`, `ender.png`): the 64x64 entity sheet does not fit the 16x16 block array, so the tool crops each box face's region (lid and base stacked, located from the sheet's opaque islands, no code read) into a 16x16 tile under `stand_ins/chest_<kind>_<face>.png`, records `derived_from` (jar entry, crop rectangles) in `textures.json`, and the verifier re-derives the tiles from the jar. These are the reference's first derived pixels: they are not jar copies, which is why they live outside `assets/`. About 18 texture codes (6 faces x 3). The item icon is the baked box (vanilla's `items/chest.json` is `special`). No latch, no lid animation.

### 2.4 Narrowing the survival matrix and its test

- **`plant_survival.json` stays byte-identical.** Adding candidates would change existing signatures (a flower survives on farmland) and could renumber them, which the append rule forbids. `dumpSurvival` takes a second, frozen id list (`tools/survival_base_ids.txt`, today's 616 ids) for its subjects and candidates.
- **New `facts/family_survival.json`** (same format and probes): subjects are the rule-bearing states of every imported block (a state that fails with air on all sides: plants, doors, buttons, lever, plates, ladder, nether vines, crops, saplings, farmland and path via `up`); candidates are air plus every imported state. Estimate: about 4,000 subject states (fire alone 512) x 8 x 28k, about 5x today's 156 M calls. B1 first times today's dump; if the new one passes 20 minutes, candidates drop to one state per (block, support mask, occlusion bits, collision box) class and the file says so.
- **`Unnamed.Foundation.Blocks.Survival`** keeps reading `plant_survival.json` only, so its time is unchanged. B3-B7 each add a sampled check of their own rules against `family_survival.json`.

### 2.5 The Playground galleries

Both galleries keep every existing cell where it is: the base blocks first in registry order as now, then a tail for the B1 blocks (the importer copies a per-block `bAppendedFamily` from the manifest's `added: "survival-B1"` key, set only on the new entries). Registry gallery: +310 cells (about 13 rows). State gallery: the tail holds one cell per B1 block (its default state), about 6 rows, instead of 24,128 cells (about 410 rows). Both tails draw only with `foundation.Blocks.Families 1`. Each later session adds its own showcase row.

### 2.6 The cascades

- **Items reference** (T2's route, rebased on the latest `feature/survival` items reference): its inputs hash `block_manifest.json`; `break_times.json` flips `imported` to true for the new blocks' items. Its `loot_blocks()` would pull in about 290 new loot tables, inserted mid-list and needing new vocabulary (`block_state_property` for doors and beds, `binomial_with_bonus_count` for crops). B1 holds them back: a new `tools/loot_later.txt` lists the 310 new ids, `loot_blocks()` skips them, so `loot_tables.json` stays byte-identical. Mining a B1 block drops itself (the provisional rule): vanilla for most families, wrong for panes without silk touch, crops, farmland and path until their sessions append those tables last.
- **`DA_Items`:** the 9 sapling items get `PlacesBlock` = their sapling (`ImportItems.py`, item id = block id), so they place their default state like the rest. Their creative positions already exist.
- **Sound reference and `DA_Sounds`:** the sound reference derives its step and fall events from the blocks reference's `block_sounds.csv` (its SHA-256 is in `sound_events.json`), so B1 regenerates it through its own tool, made append-safe by B0 (2A): new sound types' step and fall events after every earlier event, files already homed in the sound reference stay there. `DA_Sounds` holds each block's step and fall events in `DA_BreakAssets`' block order, and `ImportSounds.py` refuses a changed order: B1's pre-flight checks how `ImportBreakAssets`/`DA_BreakAssets` orders blocks; the new blocks go after the old ones there (no index shift), and `DA_Sounds` is reimported a second time in the program, by this lane, append-only. B1 also records each family block's use-sound events from the public `BlockSetType`/`WoodType` accessors (FactsDump, black-box) into `facts/block_use_sounds.csv`, for B3 and B5.
- **Worldgen and sky references:** verify only.

### 2.7 The runtime import and its effects

- `ImportBlocks.py` into `/Game/Blocks` (`DA_Blocks`, the texture array, model data) by the documented route; the families keep `DEFAULT` placement (`BlockImportData.py` unchanged apart from the gallery flag and the stand-in tiles' source path); a second run is a no-op.
- **Creative inventory:** every new block item takes its vanilla tab position from `creative_tabs.json` (stairs, walls and slabs interleave in Building Blocks, beds and panes in Colored Blocks, chests and ladders in Functional, doors and buttons in Redstone, crops' seeds still absent). Behind the switch, so `0` is today's grid. Tour shots of the inventory change only with the switch on.
- **Known gaps carried to B2:** fences, walls and closed gates collide 1.0 tall (the gatherers stay in the cell); `Blocks.PlantShapes`' out-of-cell count is pinned for them and the pitcher crop. Nothing connects; doors are one lower half.

### 2.8 `poplar_sapling` (same commit)

`Source/UnnamedVoxel/Blocks/VoxelPlantSurvival.cpp:13-17`: `poplar_sapling` joins `VegetationBlocks` (scout measured it on oak's support set). Once the saplings are in the table, every tree's `would_survive` compiles to `EOp::WouldSurvive` instead of the `InSet supports_vegetation` fallback (`VoxelBlockPredicates.cpp:172-188`, comment updated, the fallback kept for any other `_sapling`). **Gate:** radius-8 hashes (both load orders, decoration on and off) and `Mesher.Digest` equal the fresh baseline; any shift stops B1 and goes to the SM. **Control:** the same capture without the poplar line must move a hash; if the radius-8 area has no poplar feature, a `VoxelPlantTests` case proves instead that all 9 saplings' compiled rule equals the old `InSet` answer for every state below.

### 2.8b World numbers

Nothing hashes, goldens or saved worlds read moves: state and protocol ids are vanilla (`VoxelBlocks.cpp:113-121`), textures and models append, saves store `{id, properties}`, generation places none of the families (scout section 9). Registry slot indices of later blocks shift, as with the torch, and nothing saved reads them. Pins that move are counts in tests only.

### 2.9 Docs/StandIns.md

The file is not in this lane at `9822d24`: B1 merges `feature/survival` first and adds rows there (or creates it in the program's format). Rows: `textures/entity/chest/normal.png`, `trapped.png`, `ender.png` (assets/minecraft/, the 26.3 client jar) → `stand_ins/chest_*` tiles → the three chest blocks and their item icons → B1. The other families' textures are their real 26.3 textures, not stand-ins.

### 2.10 Files B1 changes

| Area | Files |
|---|---|
| Blocks reference | `Reference/minecraft-blocks-26.3/tools/{build_manifest,build_reference,verify_reference}.py`, `FactsDump.java`, new `tools/survival_base_ids.txt`; regenerated outputs, new `facts/family_survival.json`, new `stand_ins/`, README, SHA256SUMS |
| Items reference | `tools/build_reference.py` (`loot_later.txt` skip), new `tools/loot_later.txt`, `manifest.json`, `facts/break_times.json`, README, SHA256SUMS, `verify_reference.py` if it pins counts |
| Sound reference | only if its verifier fails (2.6) |
| Import | `Scripts/Editor/BlockImportData.py`, `ImportItems.py` (or its data module), `Content/Blocks/*` (DA_Blocks, array, models), `Content/Items/DA_Items` |
| Voxel core | `Source/UnnamedVoxel/Blocks/VoxelPlantSurvival.cpp`, `Decoration/VoxelBlockPredicates.cpp` (comment) |
| Game | `FoundationBlockAssets.h` (`bAppendedFamily`), `FoundationPlayground.h/.cpp` (tails), the switch's cvar; **hot:** `FoundationAuthority.cpp` (guard), the creative list builder (`FoundationPanels.cpp` or `FoundationItems.cpp`) |
| Tests | `FoundationBlocksTests.cpp` (151, 191, 229, 826-827, 1023), `FoundationTorchTests.cpp` 211, `VoxelMesherTests.cpp` (`Mesher.Models`, `.ModelData`), `FoundationItemTests.cpp` (creative counts), `FoundationPlantTests.cpp` (`PlantShapes` pins), `VoxelPlantTests.cpp` (poplar), new `FoundationFamilyTests.cpp` (every B1 block present, default state placed with the switch on and refused off, the chest box, the gallery tails, no item id in both DAs) |
| Docs | `Docs/Blocks.md` (counts, budgets: 968 of 1,024 codes, 56 left; phase 7 must widen the field), `Docs/Playground.md`, `Docs/StandIns.md`, **hot:** `Docs/Items.md` (saplings place), `Docs/Records/2026-10-xx-Blocks-B1.md` and its `README.md` line |

Before planning edits B1 diffs every hot file against the latest `feature/survival`.

### 2.11 Checks (after the last code change)

1. `verify_reference.py` for blocks, items, worldgen, sky and (if touched) sounds: REFERENCE OK.
2. The append script (`diff_reference.py`, extended): each aggregate minus the B1 entries equals the accepted file; textures 0-690 and models 0-307 (layers 0-945) unchanged; every existing asset byte-identical; `plant_survival.json` and `loot_tables.json` byte-identical; SHA256SUMS keeps every old line except the changed files'.
3. Regeneration proof: the repo's `FactsDump.java` and `build_reference.py` rerun into temp folders give byte-identical outputs (`creative_tabs.json` by T2's masked comparison).
4. `ImportBlocks.py` and `ImportItems.py`: second run is a no-op.
5. All of `Unnamed` native; `TestVoxel.ps1`; `Mesher.Digest` against `expected-hashes-main.txt`; radius-8 hashes both orders and `voxel.Decoration 0`, plus the poplar control.
6. `Scripts\RunChecks.ps1 -Profile final -Flight smoke` (create-world, the Playground probe, interaction, tour, the flight), then `-Profile merge` at landing.
7. Playground probe at cap 60, switch 0 against 1, and the tour's gallery and creative shots as the control pair for the SM.
8. Pre-commit review (Sonnet 5.5, diff only).

## 2A. B0, the sound batch, ready for a go

Jesus's call (October 14): one program-wide batch, built in this lane before B1, through the sound reference's own tool (`Reference/minecraft-sounds-26.3/tools/build_reference.py`), with `DA_Sounds` reimported once. The event list is `C:\dev\tools\shift-managers\survival\sounds-batch.md` at the PM's deadline; later needs go in a second small batch at the end.

**T5's events** (each confirmed in the jar's `sounds.json` by the build, which stops on a missing one): `block.{wooden,iron,copper,cherry_wood,bamboo_wood,nether_wood}_door.{open,close}`, the same six `_trapdoor.{open,close}`, `block.{fence_gate,cherry_wood_fence_gate,bamboo_wood_fence_gate,nether_wood_fence_gate}.{open,close}`, `block.{wooden,stone,cherry_wood,bamboo_wood,nether_wood}_button.click_{on,off}`, `block.{wooden,stone,metal,cherry_wood,bamboo_wood,nether_wood}_pressure_plate.click_{on,off}`, `block.lever.click`, `item.hoe.till`, `item.shovel.flatten`, `item.crop.plant` (B6's seeds): about 60. The worker enumerates them once from `sounds.json` with a pattern, writes the explicit list into the tool, and the verifier checks the list against the pattern so none is missed. Which block plays which event is B1's `block_use_sounds.csv`, not B0's.

**Tool changes** (`build_reference.py`, `verify_reference.py`, README):
1. `APPENDED_NAMED`: the batch's events, group `named`, after every existing event. Order becomes: the 21 `NAMED`, the 86 step and 86 fall events, then `APPENDED_NAMED`, then (from B1 on) new step and fall events. Events 0-192 stay byte-identical.
2. **Append-safe for B1:** `STEP_BASE` and `FALL_BASE` freeze today's 86 + 86 sound-type values; values that B1's `block_sounds.csv` adds go after every earlier event (sorted among themselves), instead of being sorted into the middle as `derive()` does today.
3. **Home rule frozen:** a new `tools/own_files.txt` lists every file homed here after B0; such a file stays homed here even when B1's blocks reference later gains a copy (today's rule would flip its `home`). Other files follow today's rule.
4. A `sounds` entry with `type: event` still stops the build; if a batch event has one, the worker reports it to the SM (inline one level, or drop the event) rather than choosing.
5. The verifier: the new counts; events 0-192 and files 0-(old count) identical to the accepted file; `APPENDED_NAMED` in order; the `own_files.txt` rule; the pattern check.

**`DA_Sounds`:** `ImportSounds.py` (and `SoundImportData.py` if it pins counts) appends the new events and their new `.ogg` waves under `/Game/Sounds`; existing waves and entries unchanged (T3D export of the old entries identical); a second run is a no-op. Pre-flight: confirm the per-block step and fall entries reference events by name, or by an index that appending keeps.

**Switch:** none. B0 is data only: nothing plays the new events until a step calls `PlayEvent`, and `foundation.Sound.Events` already gates playing. (For the SM to accept; a cvar here would have no behaviour to switch.)

**Checks:** the sound verifier (REFERENCE OK); the blocks reference untouched (`git diff --stat` empty under it); the append script (events 0-192, old files, old `.ogg` bytes, SHA256SUMS' old lines); the regeneration proof (the tool rerun into a temp folder, byte-identical); `ImportSounds.py` second run no-op; the native sound tests with their counts updated; `Scripts\RunChecks.ps1 -Profile final -Flight smoke`.

**Files:** `Reference/minecraft-sounds-26.3/` (`tools/build_reference.py`, `tools/verify_reference.py`, new `tools/own_files.txt`, `sound_events.json`, the new `.ogg` files, README, SHA256SUMS); `Scripts/Editor/ImportSounds.py` and `SoundImportData.py` only if they pin counts; `Content/Sounds/` (`DA_Sounds`, new waves); the sound tests' count pins; `Docs/Breaking.md` (other sound events); the B0 record and its line in `Docs/Records/README.md` (the one hot file). Size S-M.

## 3. Blocks other tracks use

From `PROGRAM.md` section 4's T1, T3 and T4 steps, checked against `block_manifest.json` at `9822d24` and B1's list:

| Block | Used by | Status |
|---|---|---|
| `crafting_table` | T1 step 7 | imported today |
| `furnace` | T1 8a | imported today |
| `smoker`, `blast_furnace` | T1 8b | imported today |
| `barrel` | T1 9 | imported today |
| `chest` (`trapped_chest`, `ender_chest`) | T1 18-chest | **B1** (stand-in box) |
| `cauldron`, `water_cauldron`, `lava_cauldron`, `powder_snow_cauldron` | T3 17 buckets | **B1** (PM's addition) |
| `fire`, `soul_fire` | T3 16 fire damage | **B1** (PM's addition) |
| `campfire`, `soul_campfire` | T3 16 (damage), later cooking | **B1** (PM's addition) |
| logs, wood, stems, hyphae, `bamboo_block` and their stripped forms | T3 14 axe stripping | imported today |
| `water`, `lava` (fluids), `powder_snow` | T3 17 buckets, 16 lava | imported today |
| `farmland`, `dirt_path` | T3's hoe and shovel branch (B5 uses it) | **B1** |
| the 16 beds | T4 respawn (spawn point, B7) | **B1** |
| `torch`, `respawn_anchor`, `copper_block` and its stages | various | imported today |

Not imported and in no plan: signs (`oak_sign` and kin). T2 and T6 name no block beyond these.

## 4. For the SM to confirm (not Jesus's)

1. B0 ships without a switch (2A).
2. B1 regenerates the sound reference and reimports `DA_Sounds` a second time in this lane, append-only (2.6); `sounds-batch.md` says no other lane touches them, which still holds.
3. The chest tiles are the first derived (cropped, not copied) pixels in a reference; they live in `stand_ins/`, outside `assets/`, and the verifier re-derives them (2.3).
4. Vanilla-first calls logged here: the B1 blocks' loot waits for their sessions (2.6); sapling items place saplings in B1; the state gallery tail is one state per block; fire and the campfires import as data only (no burning, no damage, no cooking until T3 and B6).
