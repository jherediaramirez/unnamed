# Step 8a (the furnace): plan draft

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. Written at the program wind-down (2026-10-15) before the plan was sent or approved. **Nothing is built:** no source changes; this doc is the only change. Read it together with `Docs/Records/2026-10-14-Inventory-Handoff-Step8a.md`.

## State at the stop

- `feature/survival` pointed at 896f2d37 (this lane's step 7 merge), so the hot files had no diff against it. The only difference was the step 8a handoff commit (85073aa7: the handoff doc and `Docs/Records/README.md`). Re-diff before planning again: other lanes may have landed since.
- The SM answered the handoff's questions. Q1: the block-entity store goes in a new `Foundation/FoundationContainers.*` (UObject-free where it can be), shaped so the barrel (step 9) and the chest can reuse it. Q2: the furnace screen closes when its block breaks; breaking a furnace drops its contents and the block.

## Findings that shape the plan

- **No smelting recipes in the item reference.** `Reference/minecraft-items-26.3/recipes.json` holds only `shaped` (827) and `shapeless` (375). The reference is hash-checked, and changing it is a reference regeneration, so don't touch it. The 26.3 client jar (`%APPDATA%\ModrinthApp\meta\versions\26.3\26.3.jar`, the same jar `build_reference.py` reads) has `data/minecraft/recipe/*.json` with `"type": "minecraft:smelting"`, `ingredient`, `result.id`, `cookingtime` and `experience`. This is game data, so it's an allowed source. Recipe files aren't always named `*smelting*` (`glass.json`, for example), so filter by `type`.
- **Fuels exist only in game code.** Use the allowed findings table: `C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md` lines 2633-2986 (`survival.r3.fuels`, 347 items x furnace/smoker/blast ticks). Recipes with XP and cook times are at lines 2988-3110 (`survival.r3.recipes`, 116 including campfire). Burn boundaries are at lines 3114-3121, cooling at 3125-3192 (`survival.r3.furnace_ticks`), the XP rule at 3196-3203 (`survival.r3.xp_rule`), shift-click routing at 3209-3234 (`survival.r3.shift_click`) and the sprites at 3238-3256 (`survival.r3.sprites`).
- **The menu already fits.** `FFoundationContainerMenu(Inventory, Rules, bCreative, Kind, TArrayView<Grid>)` takes the container's slots as a view. A furnace can pass a trial copy of its three store slots, as the crafting table passes its grid (`FoundationAuthority.cpp` ~1004-1027 `ContainerClick`, ~205 `GetOpenMenu`, ~236 `OpenContainer`).

## Plan (files)

| File | Hot? | Change |
| --- | --- | --- |
| `Scripts/GenerateCookingData.py` (new) | no | Reads the jar's cooking recipes (smelting now; smoking, blasting and campfire kept for 8b) and the findings fuel table. Writes the generated `Source/Unnamed/Foundation/FoundationCookingData.inl` with item names, ticks, XP and the evidence ids in its header. Commit its output only from the generator's final version. |
| `Foundation/FoundationContainers.h/.cpp` (new) | no | `FFoundationBlockEntities`: a map from cell to entity. A furnace entity holds 3 slots, `LitTime`, `LitDuration`, `CookProgress`, `CookTotal` and the stored XP per recipe. It is generic enough for the barrel and chest (slots + kind). Plain C++ with no UObject. It has `TickFurnace(entity, rules)` with the value rows: lit at tick 1, output at the cook total, unlit at fuel + 1, cooling -2 per tick clamped at 0, the lava bucket leaving a bucket. `IsFuel`, `FindSmelting` and fractional XP are `floor(n*xp) + Bernoulli(frac)`. |
| `FoundationContainer.h/.cpp` | no (step 7's own) | `EFoundationMenuKind::Furnace` layout: 0 input, 1 fuel, 2 result, main 3-29, hotbar 30-38. `MayPlace`: result never; fuel only takes fuel or a bucket. Result slot take-only (no craft). `QuickMove` follows the routing: a recipe sends the item to input, else fuel goes to fuel, else main and hotbar trade; from 0-2 it goes to the player. Rules get `IsFuel` and `IsSmeltable` TFunctions. |
| `FoundationAuthority.h/.cpp` | **hot** | Owns the store. `furnace` is added to the built-in `FindBlockUse`. `OpenContainer` doesn't size a grid for a furnace. The `ContainerClick` furnace branch runs on a trial copy of the store slots. Taking from slot 2 pays the stored XP through `SpawnExperience`. `Step` ticks every furnace in a loaded cell when `foundation.Furnace` is 1. `BreakBlock` drops the slots, pays the stored XP (wiki, provisional) and erases the entity. Each step checks stillValid and auto-closes (Q2). `Reset` clears the store. `ContainerClose` leaves the furnace slots in the store. `GetOpenMenu` and a new `GetFurnace(Cell)` feed the screen. |
| `FoundationInteraction.cpp` or settings | **hot** | `foundation.Furnace` cvar, next to `foundation.Blocks.Use`. |
| `Hud/HudContainer.cpp`, `Hud/HudContainerScreen.cpp` | **hot** | Furnace slot positions 56,17 / 56,53 / 116,35 and the player rows at y 84/102/120/142. Background `furnace`. Flame at x 56, w 14, bottom y 50; arrow at 79,34, w 0-24, h 16. |
| `FoundationPanels.cpp` | **hot** | Only if `OpenContainerScreen` needs the kind's background (it may already route by kind). |
| Controller event-name table | hot-ish | Only if a new event kind is added (avoid if possible; `ContainerOpened` already carries the block). |
| `Scripts/Editor/ImportContainerGui.py` | no | `gui/container/furnace`, `sprites/container/furnace/lit_progress` and `burn_progress` become new assets under `/Game/Gui/Container`, plus 3 `Docs/StandIns.md` rows. |
| `Tests/FoundationFurnaceTests.cpp` (new) | no | 1 coal smelts 8 in 1,600 ticks; fuel ending mid-item cools -2 per tick; the burn boundaries; the XP sums; breaking drops contents; the store survives a column unload; the shift-click routing table; `foundation.Furnace 0` opens but doesn't tick. |
| `Docs/Items.md`, record, `Docs/Records/README.md` | **hot** (docs) | A furnace section, provisional values and the record. |

Numbers that hashes, goldens or saved worlds depend on: none expected. There are no reference edits and no new saved IDs: `furnace` already exists as a block and item. The tour may gain a shot (a new tour name, not a re-record).

## Saved data (not to build)

The store is in-memory: it survives column unloads (keyed by world cell, not by column) and is cleared by a world reset or exit. Persisting it would mean adding a per-world `Saved/Worlds/<id>/block_entities.json` (or a section in the per-column edit save once phase 10 exists) with cell, kind, slots (item id, count, damage) and the furnace timers, plus a `version` bump. That is the PM's and Jesus's call. Until it's made, furnace contents are lost when the world is closed.

## Open questions

1. Generated cooking data from the jar plus the findings table (recommended) vs. extending the hash-checked item reference (needs the reference regeneration rule and Jesus).
2. Furnaces in unloaded columns don't tick (vanilla: only loaded chunks tick). Recommended.
3. Breaking a furnace pays its stored XP (minecraft.wiki, provisional). Recommended.

## Split recommendation

The step is M-L, too much for one session. Split it:
- **8a1:** the cooking data generator, `FoundationContainers.*`, the tick, fuel, cooling, the furnace layout and click rules, break drops, the auto-close, the cvar and native tests.
- **8a2:** the screen (import, background, flame and arrow sprites), XP on take through `SpawnExperience`, shift-click routing tests, the tour shot and the docs.
