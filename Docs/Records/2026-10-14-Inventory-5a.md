# Survival step 5a: the full player inventory and container clicks (record and handoff to step 5b)

Lane `feature/survival-containers` (worktree `Unnamed-inv`), from `9822d24`. Survival program track T1 Containers (`C:\dev\tools\shift-managers\survival\PROGRAM.md`), scout AC's plan step 5a (`C:\dev\sandbox\scout-player\plan.md` lines 107-133; facts.md lines 111-126 and 232-256). Next: step 5b, the survival inventory screen on E (section 5).

## 1. What was built

- **Behaviour** (`Docs/Items.md`, "Inventory and container clicks"): the player's inventory is vanilla's 36 + 4 armor + offhand (`foundation.Inventory.Full`, default 1; 0 = the lab inventory, hotbar and offhand only, as before, with one difference in the creative screen: two copies of a tool with different damage now swap on a click where they did nothing before; that is vanilla's result and is kept. Wording corrected in step 6, after Codex's 5a review, finding 4). Pickups, loot returns and the creative screen fill the 27 main slots; the creative screen's survival tab reads and writes all 41 slots. Nothing is visible in survival yet: there is no survival screen until 5b.
- **`FFoundationInventory`** (`FoundationItems.h/.cpp`): `Main[27]`, `Armor[4]` (feet, legs, chest, head), `Carried` (the cursor), `QuickCraft` (a drag), `bFull`; vanilla's Inventory numbers (0-8 hotbar, 9-35 main, 36-39 armor, 40 offhand) through `GetSlot`/`SetSlot`. `Add`/`GetSpaceFor` fill in vanilla's order (selected, offhand, 0-35 merge; then the first free of 0-35), provisional until P3. `Return` puts a whole stack back keeping its damage and identity (the cursor on close).
- **`FFoundationContainerMenu`** (new `FoundationContainer.h/.cpp`, UObject-free): vanilla's seven click types over the player's menu in InventoryMenu numbers (0-4 crafting, refused until step 6; 5-8 armor head..feet; 9-35 main; 36-44 hotbar; 45 offhand; -999 outside). `QuickCraftButton(Phase, Type)` builds a drag's button. Every click snapshots the 46 slots, the cursor and the drag, and bumps the revision of each stack that kept its id but changed, and the inventory's on any change.
- **Item data:** `FFoundationItemDefinition::Equipment` (`EFoundationEquipmentSlot`, default None; no asset resaved or reimported). Armor slots take only their own slot's items; until step 19a no item has one, so they take nothing in play.
- **Authority** (`FoundationAuthority.h/.cpp`): commands `ContainerClick` (runs the menu on a copy; a throw is refused `DropDeferred` and nothing commits) and `ContainerClose` (cursor back through `Return`; refused `DropDeferred` when it does not fit); `Options.bFullInventory` (the pawn copies the console variable; the authority sets each player's `Inventory.bFull` on every command and pickup step); `MakeContainerRules`. `SetCreativeSlot` takes Inventory numbers 0-40; **the offhand moved from 9 to 40** (callers moved: the creative screen, `FoundationMiningTests.cpp`, `HudCreativeTests.cpp`; no probe or script passed 9).
- **Client:** `FFoundationInteraction::ContainerClick`/`ContainerClose`.
- **Creative screen** (`HudCreative.h/.cpp`, `HudCreativeScreen.cpp`, `FoundationPanels.cpp`): the hotbar, main, armor and offhand slots run the shared menu on a copy of the 41 slots (`ClickMenu`, items mapped to click-local indices) and send each changed slot as `SetCreativeSlot`; shift is QUICK_MOVE in the survival tab (still deletes in the picker tabs), middle clicks clone, number keys swap over any player slot; main, armor and offhand slots draw their stacks and show tooltips. With the switch at 0 the main and armor slots draw empty and do nothing, shift and middle do nothing in the survival tab, and only hotbar tooltips show, as before.

## 2. Decisions

- **Vanilla numbering on both levels** (manager's go): Inventory numbers for storage and `SetCreativeSlot`, InventoryMenu numbers for clicks, so step 5b's screen and step 6's crafting slots (menu 0-4) slot in without renumbering.
- **The cursor and drag live on the inventory**, so its revision covers them; a future container (furnace, step 8a) gets its own menu over the inventory plus its block entity's slots.
- **QUICK_CRAFT is in 5a** (the model has the three phases); only the screen's mouse gestures wait for 5b.
- **Creative clicks stay client-side** (vanilla's creative screen clicks its own menu and sends creative slot packets); survival screens will send `ContainerClick`.
- **Provisional:** the fill order (wiki; probe P3), and the creative middle drag keeping the cursor stack (from the wiki's "places a full stack in each"; the reviewer was unsure, so it is P3's to confirm). Step 6: both settled by the value report: the fill order is the same, and the creative middle drag empties the cursor (`survival.r7.creative`), which step 6 changed.

## 3. Tests

- New `Unnamed.Foundation.Inventory.FillOrder`, `.QuickMove`, `.Pickup`, `.SwapCloneThrow`, `.QuickCraft`, `.PickupAll`, `.Authority` (`Tests/FoundationInventoryTests.cpp`); `Unnamed.Hud.Creative.Clicks` and `.Authority` extended (41 slots, quick move, swaps, the armor slots with test items, the switch at 0); `Unnamed.Foundation.Mining.Wear`'s offhand write moved to slot 40. Details in `Docs/Items.md`, Tests.
- Mutations (`C:\dev\sandbox\inv5a\mutate.ps1`, results `mutations.txt` and `mutations-rerun.txt`): fill order → FillOrder; quick-move routing → QuickMove; the drag's split → QuickCraft; PICKUP_ALL's passes → PickupAll; the armor filter (rerun: the first mutant did not compile) → Pickup, SwapCloneThrow, Creative.Clicks; the switch → Inventory.Authority, FillOrder, Creative.Authority. Each failed; the code was restored and rebuilt after.
- Pre-commit review: two Sonnet reviewers (the diff was 1,732 lines). Fixed: the cursor's return on close dropped a tool's damage and identity (now `Return`); an offhand stack was not routed by its equipment on shift-click; tooltips on main, armor and offhand slots; four test gaps (the armor slots' order and filter through `Equipment`, a swap's stacks, the switch at 0's shift, middle and number keys, a shadowed name).

## 4. Known issues

- **No survival screen yet** (5b): in survival the main grid fills but cannot be seen or used except through the creative screen.
- **Death and other "every slot" paths** iterate the hotbar and offhand only (the HUD's hotbar, probes' counters, `VoxelWorld`'s mine probe line). Items dropping on death is T4's step 11: it must walk all 41 slots and the cursor. When: step 11.
- **Creative writes are N independent commands:** if one is refused (the creative rule off), the screen's cursor and the authority can diverge for that click, as before this step (a multi-slot quick move makes it likelier). When: 5b, if the shared screen keeps creative writes.
- **SWAP of a stack larger than the slot's max** (armor with more than one) does nothing instead of vanilla's split-and-return; unreachable until stackable armor exists. When: 19a if it matters.
- **A no-op click commits** (vanilla also accepts it); the revision only bumps on a change.
- **Throwing** (THROW, clicks outside, a close without room) waits for T2 step 4: wire `Click.Thrown` to `SpawnItem` there and drop the `DropDeferred` branches.
- **Pick block** from the main inventory is T3 step 16 (`FoundationInteraction.cpp:460-482` still searches the hotbar).

## 5. Next: step 5b, the survival inventory screen on E (handoff)

**Reading list:** this record; `Docs/Items.md` "Inventory and container clicks"; plan.md lines 120-133 (step 5b); facts.md lines 232-256; `FoundationContainer.h` (whole, short); `HudCreativeScreen.cpp` painter and input (`OnPaint` slot drawing ~405-470, `OnMouseButtonDown` ~270-300, `OnKeyDown` ~320-355); `FoundationPanels.cpp` `OpenInventory` (~558-600) and `MakeCreative` (~468-540).

**Settled:** vanilla numbering as above; the screen sends `ContainerClick` with container 0 and InventoryMenu slot numbers, and `ContainerClose` on close; the client builds QUICK_CRAFT buttons with `FFoundationContainerMenu::QuickCraftButton`; switch `foundation.Inventory.Screen` (1 the vanilla screen, 0 the text fixture); textures from `Reference/minecraft-menu/.../gui/container/inventory.png` and the slot silhouettes (asset import: ask the manager first, DA locks).

**Code map:** `FFoundationInventory` (`FoundationItems.h`), `FFoundationContainerMenu` (`FoundationContainer.h/.cpp`), the `ContainerClick`/`ContainerClose` cases in `FFoundationAuthority::Execute`, `FFoundationInteraction::ContainerClick` (`FoundationInteraction.cpp`), the creative model's `ClickMenu`/`GetInventorySlot` (`HudCreative.cpp`).

**What worked:** one shared menu for the authority and the creative screen (the creative tests caught slot-mapping slips at once); scripted mutations with a stale-DLL guard. **What failed:** a mutant that does not compile leaves the previous DLL in place and its test row is meaningless (check the build line); splicing C++ with `IndexOf` needs a start index (one splice hit `GetSlotPosition`'s `case Destroy`).

**Open questions for Jesus (via the manager):** probe P3 (a vanilla client run) to confirm the fill order and the creative middle drag; the push of `feature/survival-containers` (never pushed by a worker).

**Short prompt for step 5b:**

> Survival program, track T1 Containers, step 5b: the survival inventory screen on E. Worktree C:\dev\projects\Unnamed-inv, branch feature/survival-containers (merge feature/survival first if your SM says it moved). Start with Docs/Records/2026-10-14-Inventory-5a.md section 5 (reading list, settled decisions, code map), then plan.md lines 120-133. Build a vanilla screen from `container/inventory.png` that sends `ContainerClick` (container 0, InventoryMenu slots) and `ContainerClose`, with the drag and double-click gestures, behind `foundation.Inventory.Screen` (0 = the text fixture). Asset imports need your SM's lock first. Diff the hot files against feature/survival, send the plan to your SM and wait for the go.

## 6. Evidence and token use

- Final checks (after the review fixes): `Tests/Evidence/2026-10-14/Checks-final-20261014-124800/summary.txt`, GREEN in 36 min: native 337 of 337, 207 voxel suites, radius-8 hashes 9 of 9 equal, the Playground runtime (all, interaction, tour), create-world, the smoke flight, npm, source-check, the reference verifiers. The first final run stopped at the build: `-DisableAdaptiveUnity` put the new test file in a unity blob with another test's anonymous-namespace `Main`, so its helpers got unique names. Suite logs: `C:\dev\sandbox\inv5a\tests1.log`, `tests3.log` (after the review fixes); mutation logs `C:\dev\sandbox\inv5a\mut-*.log`.
- Merge: 45c5473 (feature/survival 5efbf6c, T3 eating; six additive conflicts kept both sides), `Tests/Evidence/2026-10-14/Checks-merge-20261014-132740/summary.txt` GREEN (native 339 of 339, hashes 9 of 9).
- Lead context: about 200k at the merge (measured from the transcript). Subagents: two Sonnet reviewers, 80k and 89k.
