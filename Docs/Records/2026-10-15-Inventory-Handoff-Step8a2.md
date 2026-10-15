# Handoff: survival step 8a2, the furnace's screen (track T1 Containers)

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. Step 8a1 is committed (fbda9952, on top of the 4b826e4 merge 3a359c75). `feature/survival` has since moved to **632a7466** (T5 B2a: block shapes; it touches `FoundationAuthority.cpp` and re-imports DA_Blocks). The merge waits for SM-Containers' merge go (a PM token). If it has not landed when 8a2 starts, ask the SM; never merge mid-step. Report to SM-Containers (session_01EuH6RLbTU1zzFVHK75nWYJ).

## 1. Reading list (exact sections)

1. `Docs/Records/2026-10-15-Inventory-8a1.md`, all of it: what 8a1 built, Codex review 12's outcomes, the review findings, the provisional rows, the known issues.
2. `Docs/Items.md`: "The furnace (T1 step 8a1)", plus "The crafting table and block use (survival step 7)" for the screen, the simulation under it and stillValid.
3. `Docs/Records/2026-10-15-Inventory-8a-Plan-Draft.md`, "Plan (files)": the Hud and import rows are 8a2's.
4. `C:\dev\tools\shift-managers\survival\value-swaps.md`:
   - section 2, rows 43-45 and 49-50: `FurnaceScreen` 176 x 166 at (232, 97); input/fuel/result at 56,17 / 56,53 / 116,35; player rows at y 84/102/120 and the hotbar at 142.
   - section 4.2, row 210 (progress sprites): the arrow at local 79,34, width 0/6/12/18/24 at 0/25/50/75/100%, h 16; the flame at x 56, w 14, lower edge y 50, height 0/5/8/11/14. Source `survival.r3.sprites` (SURVIVAL-FINDINGS F 3238-3256; grep, never read the file whole).
5. `C:\dev\tools\shift-managers\survival\PROGRAM.md` section 5 (asset locks) and section 6 (settled decisions).
6. Code, by symbol (no whole-file reads of the big files):
   - `FoundationPanels.cpp`: `OpenContainerScreen` (its first line refuses `EFoundationMenuKind::Furnace`; remove that), the event-log name.
   - `Hud/HudContainer.h/.cpp`: `FHudContainerModel::GetSlotPosition(Kind, Slot)`, `GetSlots`, `HitTest`, `IsInRecipeButton` (the furnace has a recipe-book button too; provisional position), `CanDragTo`.
   - `Hud/HudContainerScreen.cpp`: `GetBackground`, `OnPaint` (labels, the flame and arrow drawn from the open menu's furnace entity: `LitTime/LitDuration`, `CookProgress/CookTotal`). The view needs these two fractions: extend `FHudContainerView`, or add a getter to the screen's arguments.
   - `FoundationAuthority.h`: `BlockEntities`, `GetOpenMenu`; the entity is `FFoundationBlockEntity` (`IsLit()`).
   - `Scripts/Editor/ImportContainerGui.py` (`SOURCES`): add `gui/container/furnace`, `gui/sprites/container/furnace/lit_progress` and `burn_progress` as new assets under `/Game/Gui/Container` (no lock), each with a `Docs/StandIns.md` row.
   - `FoundationTourProbe.cpp`, case l ("tour crafting table"): the pattern for a new furnace shot. Place a furnace in a known air cell near the player, as the table case does (the floor under the feet is below the build bounds), and fill its slots. Use a new tour name (e.g. `24e-furnace`), never a re-record.

## 2. Working agreement

As the worker template says:
- Plan first: every file against the hot files (`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`), diffed against the latest `feature/survival`. Then wait for the go.
- A Sonnet 5.5 pre-commit review on the diff only, split by path list over about 1,500 lines (8a1's split: core logic vs presentation/tests).
- `Scripts\RunChecks.ps1 -Profile final -Flight smoke` (it builds with `-DisableAdaptiveUnity`).
- Measure context with `node Scripts/MeasureAgentUsage.mjs --session <id>`: copy the session's `.jsonl` (and its subagents folder) into `C:\dev\sandbox\inv5b\measure-cfg\projects\C--dev-projects-Unnamed\`, with `CLAUDE_CONFIG_DIR=C:\dev\sandbox\inv5b\measure-cfg`.

## 3. Settled decisions

- 8a1/8a2 split (SM). 8a1 Q1: no screen until 8a2. Q2: XP on take in 8a1 (done). Q3: only the survival screens run the simulation; the creative screen and the pause menu keep pausing.
- The lit block state (`furnace[lit=true]`) and light 13 are **8b**, with the smoker and blast furnace (ACTION-PLAN). 8a1's entity exposes `IsLit()`; nothing writes the world from the tick.
- Saved data (PM 3): the store stays in memory until the voxel plan's phase 10. No new save file, no version bump.
- Cooking data is generated outside `Reference/` (`Scripts/GenerateCookingData.py --check`). Never edit the hash-checked item reference.
- New GUI textures are our own assets (no lock). DA_Items, DA_Blocks and DA_Sounds stay untouched. `block.furnace.fire_crackle` (in DA_Sounds already) is a lit-furnace ambient sound: 8b with the lit state, unless the SM says otherwise.
- Track branches stay local; there is nothing to push.

## 4. Code map (8a1, what 8a2 builds on)

| Area | Where |
| --- | --- |
| Store, cooking, tick, XP roll | `Foundation/FoundationContainers.h/.cpp` (`FFoundationBlockEntities`, `FFoundationCooking`, `FoundationFurnace::Tick/OnInputChanged/RollExperience`, `foundation.Furnace`) |
| Generated data | `Foundation/FoundationCookingData.inl` from `Scripts/GenerateCookingData.py` |
| Menu layout | `FFoundationMenuLayout::Get(Furnace)` (`FirstContainer`, `ContainerSlots`, `TakeOnly`, `FuelSlot`, `HasCraftResult`) and `FFoundationContainerMenu` (`MayPlace`, `GetSlotMax`, `QuickMove`) |
| Authority | `UseFurnace`, `GetMenuGrid`, the furnace branch of ContainerClick (input change, XP on take), `IsMenuValid`, `StepOpenMenus`, `CloseMenu`, `StepFurnaces`, `PayFurnaceExperience`, `SpawnContents`, `BreakBlock` (contents) |
| Screen and simulation | `AFoundationPawn::SetScreenInput`, `StepInteraction` (open, refused open, auto-close, close on death), `UFoundationPanels::GetOpenContainer` |
| Tests | `Tests/FoundationFurnaceTests.cpp` (`FFurnaceLab`: `Load`, `Steps`, `Open`, `Click`, `Break`, `OrbTotal`), `CraftingTable.StillValid` |

## 5. Known issues (with "when")

- **Missing items** (DA_Items; SM routes them to its owner): only 78 of 116 recipes and 233 of 347 fuels resolve. When: as the items are added; no furnace change is needed.
  - Recipe items (48): baked_potato beef brick chainmail_boots chainmail_chestplate chainmail_helmet chainmail_leggings charcoal chicken chorus_fruit cod cooked_chicken cooked_cod cooked_mutton cooked_porkchop cooked_rabbit cooked_salmon copper_boots copper_chestplate copper_helmet copper_horse_armor copper_leggings copper_nautilus_armor copper_nugget dried_kelp golden_boots golden_chestplate golden_helmet golden_horse_armor golden_leggings golden_nautilus_armor green_dye iron_boots iron_chestplate iron_helmet iron_horse_armor iron_leggings iron_nautilus_armor iron_nugget lime_dye mutton nether_brick popped_chorus_fruit porkchop potato rabbit resin_brick salmon.
  - Fuels (114): blaze_rod, charcoal, bow, crossbow, fishing_rod, bowl, composter, lectern, daylight_detector, chiseled_bookshelf, scaffolding, and every wood's boat, chest boat (raft), sign, hanging sign and shelf, and every colour's banner, carpet and cushion.
  - Full list: `C:\dev\sandbox\inv8a\missing-items.txt`.
  - Effect: logs do not smelt into charcoal yet, and a log shift-clicks to the fuel slot (vanilla: the input).
- **Provisional values** (rows in the 8a1 record, for `provisional-values.md`):
  - stillValid distance 450 + 400 cm (`ContainerReachExtraCm`, `IsMenuValid`).
  - Breaking a furnace pays its stored XP (`BreakBlock`).
  - A broken container's contents use the block drops' spread (`SpawnContents`).
  - XP orbs on take appear at the player's feet (ContainerClick).
- **The creative screen and the pause menu still pause** the simulation (Q3). When: a creative-screen step.
- **Focus loss with a survival screen open:** the pause menu shows; resuming re-enables game input and hides the cursor while the screen stays up (input stays zeroed). This was already the case before (review B #3). When: a screen-handling pass; 8a2 may fix it if it is small (guard `ResumeFoundation` while `Panels->IsOpen()`).
- Carried over: after a lab reset, an open container screen shows an empty menu until closed; a key bound to two screen actions fires both (kept).

## 6. What worked and what failed (8a1)

- **Worked:**
  - Exact-once replacement specs (`C:\dev\sandbox\inv8a\rep.py <file> <spec>`, blocks `<<<<` old `====` new `>>>>`) and, for long lines, PowerShell `.Replace` with a count check.
  - The generator cross-checking the jar against the findings tables (counts, times, XP) before writing.
  - Reviewer split by path list (two Sonnet reviewers, ~115k each); 7 of their 10 findings were real.
- **Failed:**
  - **Unity build clashes:** a helper named `SameItem` in an anonymous namespace clashed in the `-DisableAdaptiveUnity` build only. Prefix new anonymous helpers (`ShrinkFurnaceSlot`, `FurnaceFuelRows`), and avoid `using enum` at namespace scope.
  - **Replacement specs that end mid-line** never match (a block's last old line must be whole). Three edits had to be redone.
  - **The Edit tool turned a mixed-ending file's CRLF lines into LF** (`Docs/Records/README.md`). Rebuild such a file from its HEAD blob plus the new lines (bash `git cat-file blob HEAD:<path>`, `head`/`tail`).
  - **The Playground floor is below the build bounds:** cells under the player's feet are unknown. Put probe blocks at feet level in a known air cell.
  - **The simulation now runs under the screens:** any probe that opens a container menu needs a real block at the menu's cell, or `StepOpenMenus` closes it in the next step.

## 7. Token figures (8a1)

Lead 366.3k at report A. Subagents: reviewer A 114.4k, reviewer B 117.5k (over the 100k brief size, under the 150k stop).

## 8. Evidence (8a1)

- `Tests/Evidence/2026-10-15/Checks-final-20261015-022338/summary.txt` (GREEN: native 399/399, runtime all 167, interaction 122, tour 34, create-world 38, flight smoke).
- The tour crafting shot: `Tests/Evidence/2026-10-15/Playground-tour/Runtime-20261015-025003/runtime-60-tour-24d-crafting-table.png`.
- `C:\dev\sandbox\inv8a\` (review diffs, build and check logs, `missing-items.txt`).

## 9. Open questions (for the SM)

1. The furnace screen's recipe-book button: draw it inert, as the crafting table's is (recommended), or leave it out until a recipe-book step.
2. Whether 8a2 also fixes the focus-loss resume under an open screen (recommended if it stays a one-line guard), or leaves it for a screen pass.

## 10. Short prompt for step 8a2

> Survival program, track T1 Containers, step 8a2: the furnace's screen. Worktree C:\dev\projects\Unnamed-inv, branch feature/survival-containers. Start with Docs/Records/2026-10-15-Inventory-Handoff-Step8a2.md (reading list, settled decisions, code map, known issues), then the 8a1 record. 8a1 (fbda9952) built the furnace's store, tick, fuel and XP but no screen: OpenContainerScreen refuses EFoundationMenuKind::Furnace. 8a2 imports gui/container/furnace and the lit_progress/burn_progress sprites (new assets of our own, StandIns rows), adds the furnace's slot positions and its flame and arrow (value-swaps row 210, survival.r3.sprites), opens the screen, and adds a new tour shot. First confirm with SM-Containers (session_01EuH6RLbTU1zzFVHK75nWYJ) that feature/survival 632a7466 is merged into the lane. Then diff the hot files against feature/survival, send the plan and wait for the go.
