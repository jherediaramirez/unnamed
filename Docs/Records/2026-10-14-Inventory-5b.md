# Survival step 5b: the survival inventory screen on E (record)

Lane `feature/survival-containers` (worktree `C:\dev\projects\Unnamed-inv`), track T1 Containers. Behaviour is in `Docs/Items.md`, "The survival inventory screen (survival step 5b)".

## 1. What was built

- **The screen** (`foundation.Inventory.Screen`, default 1; 0 the inventory text fixture): E outside creative opens Java 26.3's survival inventory, drawn from `gui/container/inventory.png` with the empty armor and offhand silhouettes, the recipe-book button (inert) and the "Crafting" label. A creative world still opens the creative screen.
- **`FHudContainerModel`** (new `Hud/HudContainer.h/.cpp`, UObject-free): the slot layout in InventoryMenu numbers, measured from the texture, and vanilla's gestures turned into `ContainerClick`s: press clicks with an empty cursor, drags (QUICK_CRAFT, built with `FFoundationContainerMenu::QuickCraftButton`) with a stack, double-clicks (PICKUP_ALL; shift: QUICK_MOVE of every stack of the item and damage), number keys and F (SWAP), Q (THROW), outside (-999). The drag preview (shares and the cursor's remainder).
- **`SHudContainerScreen`** (new `Hud/HudContainerScreen.h/.cpp`): draws and forwards input; `Press`/`Move`/`Release` drive the same path for probes. A lost mouse capture resets a pending gesture; keys hit-test the live cursor.
- **The shared painter** (new `Hud/HudGuiPainter.h/.cpp`): `FHudGuiArt` (brushes, item facts, the icon atlas, the tick) and `FHudGuiPainter` (box, nine-slice, text, item, stack, tooltip), moved out of `HudCreativeScreen.cpp` unchanged; the creative screen uses them.
- **Panels** (`FoundationPanels.h/.cpp`): `OpenSurvivalInventory` (public, for probes), the view from the authority (46 menu slots and the cursor), clicks through `FFoundationInteraction::ContainerClick`, `ContainerClose` in `Close()`; opening calls `CancelActions` as the creative screen does. The items list for both screens comes from one helper (`MakeScreenItems`).
- **Art:** `Scripts/Editor/ImportContainerGui.py` imports 8 textures from `Reference/minecraft-menu` into new standalone assets under `GuiRoot/Container` (`UUnnamedProjectSettings::GuiRoot` = `/Game/Gui`, `ProjectPaths.gui_asset`, cooked via `DefaultGame.ini`); additive, a second run verifies (settings and a SHA-256 tag) and creates nothing. No DA asset touched. Stand-ins listed in the new `Docs/StandIns.md`.
- **Codex's 5a review** (findings 1 and 3, the manager's scope addition): `Pickup`, `QuickMove`, `SwapWith` and `PickupAll` ignore a slot that is gone; the authority ends a drag when `foundation.Inventory.Full` changes (`SetFullInventory`); `ContainerClick` and `ContainerClose` end an item use in progress.
- **Tour:** `24-survival-inventory` and `24b-survival-drag` (the probe opens the screen in the creative Playground).

## 2. Decisions

- **The player-model frame stays empty** by Jesus's decision (PROGRAM.md section 6, "First person only, no hands": no third-person or player-body work for now), replacing Q1's "draw the pawn". The texture draws the frame.
- **The recipe-book button is drawn and inert** (Q1 (a)).
- **Crafting slots 0-4 highlight and do nothing** until step 6 (the authority refuses them too).
- **New assets, not a DA:** the textures are standalone (`/Game/Gui/Container`), so no lock was needed (manager's answer 1).
- **A refused container click still ends eating:** a screen is open, so nothing is being eaten (reviewer B's finding 1, kept by design and documented).
- **PICKUP_ALL goes to the double-clicked slot** (the press), not the slot under the release; vanilla uses the release slot. Kept: dragging off a double-clicked slot still collects (reviewer A's finding 5).

## 3. Provisional values (for `provisional-values.md`)

| Value | Number | Source | File:symbol |
| --- | --- | --- | --- |
| Double-click window | 250 ms | minecraft.wiki | `HudContainer.h:FHudContainerModel::DoubleClickSeconds` |
| Recipe-book button | leftPos + 104, topPos + 61 (height / 2 - 22), 20 x 18 | minecraft.wiki | `HudContainer.h:RecipeButtonX/Y/Width/Height` |
| "Crafting" label | 97, 8, colour 0x404040 | minecraft.wiki (the colour as the creative title) | `HudContainer.h:TitleX/TitleY`, `HudContainerScreen.cpp:TitleColor` |

Slot positions are measured from `inventory.png`'s pixels (not provisional).

## 4. Tests

- New `Unnamed.Hud.Container.Layout`, `.Clicks`, `.Drag`, `.DoubleClick`, `.Authority` (`Tests/HudContainerTests.cpp`); details in `Docs/Items.md`, Tests.
- Mutations (`C:\dev\sandbox\inv5b\mutate.ps1`, results `C:\dev\sandbox\inv5b\mutations.txt`): the drag's slot limit → Drag; one slot as a click → Drag, DoubleClick; the 250 ms window → DoubleClick; the armor filter → Drag; a plain press forgetting the shift-click → DoubleClick; the offhand's position → Layout; the switch ending a drag → Authority; a click cancelling a use → Authority; the `Pickup` null guard → the editor crashes in `FFoundationContainerMenu::Pickup` (access violation at `FoundationContainer.cpp:141`). Each mutant built; the sources were restored and touched after.
- The creative refactor, pixel for pixel: see section 6.

## 5. Known issues

- **Keys over the screen** (number keys, F, Q) repeat when held; every other key is swallowed while the screen is open (as the creative screen does). When: if a later step needs F3 combos over screens.
- **The hover highlight and tooltip** wait for the first mouse move after opening (keys use the live cursor). When: a later polish pass.
- **The tour's hover shots depend on the real pointer**: a real or synthesized mouse move over the game window replaces the probe's pointer (seen in one tour run while another editor was running). When: if it recurs, give probes a pointer lock.
- **Throws** (Q, outside clicks, a close without room) are refused `DropDeferred` until T2 step 4.
- **The world behind an open container screen** draws sharp under the dark backdrop; vanilla 26.x also blurs it (manager's note). When: a later look step.
- **Shift is read at the press** for a shift double-click (vanilla reads it at the release).

## 6. Evidence

- **Final checks** (after the review fixes, on the final code): `Tests/Evidence/2026-10-14/Checks-final-20261014-144605/summary.txt`, GREEN in 38 min: native 344 of 344 (5a's 339 + the 5 new), the voxel suites, radius-8 hashes 9 of 9 equal, the Playground runtime (all, interaction, tour), create-world, the smoke flight, npm, source-check, the verifiers. Other lanes' editors ran alongside (machine lines in the summary); nothing was timed. The first final run stopped at the build (`Checks-final-20261014-144513`): under `-DisableAdaptiveUnity` the new screen's anonymous-namespace `TitleColor` and `Background` met the creative screen's in one unity blob; they got unique names.
- **The creative refactor, pixel for pixel:** the creative panel rectangle (345,156 to 930,564 at 1280 x 720, GUI scale 3) of tour shots 20-23 from the final tour (`Playground-tour/Runtime-20261014-150454`) against 5a's merge run (`Runtime-20261014-134133`, the same creative code as d6705ce): 6 of 9 identical; 21, 22 and 22b differ only in the boxes where two runs of the old code also differ (`Runtime-20261014-125820` against `134133`: the search cursor's blink, animated icons). Script `C:\dev\sandbox\inv5b\cmp.py`. One earlier tour run (`Runtime-20261014-143517`) lost its hover and tooltip to a real pointer move (section 5); it is not used.
- **Survival shots:** `Runtime-20261014-150454/runtime-60-tour-24-survival-inventory.png` (the stone tooltip, the silhouettes, the worn pickaxe's bar) and `-24b-survival-drag.png` (7, 7, 7, 7 and 2 on the cursor, drawn over the last slot).
- **Suites:** `C:\dev\sandbox\inv5b\tests1.log`, `tests2.log` (after the review fixes: 17 of 17 of Hud.Container, Hud.Creative, Foundation.Inventory, Items.Eat); mutations `mutations.txt`, `mut-*.log`; import `import2.log` (created 8) and `import3.log` (verified 8).
- **Reviews:** two Sonnet reviewers (the diff was 2,355 lines), 96k and 85k tokens. Fixed: Q needs an empty cursor; a lost mouse capture resets a pending drag or double-click, and capture lasts while any button is down; keys hit-test the live cursor; the shift double-click compares item and damage and a plain press forgets it; a sprite load is tried once. Kept by design: a refused click still ends eating; PICKUP_ALL at the press slot.
