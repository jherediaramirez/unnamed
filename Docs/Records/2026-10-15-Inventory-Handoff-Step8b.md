# Handoff: survival step 8b, the lit furnace, the smoker and the blast furnace (track T1 Containers)

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. Step 8a2 is committed: the screen in 81560021, the cooking outputs as the items commit after it, and the fc7b0517 merge for landing. Report to SM-Containers (session_01EuH6RLbTU1zzFVHK75nWYJ).

## 1. Reading list (exact sections)

1. `Docs/Records/2026-10-15-Inventory-8a2.md`, all of it: the screen, the cooking outputs, the two reviews, the provisional rows.
2. `Docs/Records/2026-10-15-Inventory-8a1.md`: "What 8a1 built" and the known issues (the store, the tick, XP).
3. `Docs/Items.md`, "The furnace (T1 steps 8a1 and 8a2)".
4. `C:\dev\tools\shift-managers\survival\ACTION-PLAN.md` line 21: the 8b scope. If a `DA_Blocks` change is needed, it is SM-Blocks', through the PM.
5. `C:\dev\tools\shift-managers\survival\value-swaps.md`:
   - row 46: the smoker and blast furnace screens are identical to the furnace's;
   - row 50: block defaults, light 0 unlit;
   - rows 203-204: burn ticks per block and the speed multiplier 2 for the smoker and blast furnace;
   - the light 13 row, if there is one; grep for "lit".
6. `C:\dev\tools\shift-managers\survival\codex\reviews\18-T1-8a1\report.md`, findings #1 and #2.
7. Code, by symbol:
   - `FoundationAuthority.cpp`: `UseFurnace`, `StepFurnaces`, `MakeFurnaceRules`.
   - `FoundationContainers.h`: `FoundationFurnace::Tick`, whose `Kind` parameter already selects Smelting/Blasting/Smoking.
   - `FoundationContainer.cpp`: `FFoundationMenuLayout::Get`.
   - `Hud/HudContainer.cpp`: `GetSlotPosition`, the furnace branch.
   - `Hud/HudContainerScreen.cpp`: `GetBackground`.
   - `FoundationPanels.cpp`: `OpenContainerScreen`.
   - `FoundationTourProbe.cpp`: case n, `tour furnace`.

## 2. Working agreement

As before:
- Plan first: name every file and its overlap with the hot files (`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`), diffed against the latest `feature/survival`. Wait for the go.
- Sonnet 5.5 pre-commit review on the diff only. Leave generated reference JSON out of it and use a proof script for that.
- `Scripts\RunChecks.ps1 -Profile final -Flight smoke` after the last code change. Cancel and restart it (cancel file) if review fixes land mid-run.
- Measure context with `node Scripts/MeasureAgentUsage.mjs --session <id>`: copy the session's `.jsonl` and its subagents folder into `C:\dev\sandbox\inv5b\measure-cfg\projects\C--dev-projects-Unnamed\`, and set `CLAUDE_CONFIG_DIR=C:\dev\sandbox\inv5b\measure-cfg`.
- Editor scripts: pass `-script=` an **absolute** path (`$PWD\Scripts\Editor\X.py`); a relative path resolves against the engine's Binaries folder. Use `Start-Process -Wait` with `-abslog=`.

## 3. Settled decisions

- **First in 8b (Codex review 18 #1, CONFIRMED):** with `foundation.Furnace 0`, a furnace must behave as before 8a1. `UseFurnace` passes the use on, so planks place against a furnace. Add a switch-0 test. Today switch 0 still opens the menu.
- Codex 18 #2 (PLAUSIBLE, a shared-furnace instance-id clash between two players) stays a known issue until multiplayer.
- **8b scope (ACTION-PLAN):**
  - the lit block state `furnace[lit=true]` and light 13;
  - the particles;
  - the smoker and the blast furnace: their screens, their menus and the speed multiplier 2;
  - `block.furnace.fire_crackle` and `block.blastfurnace.fire_crackle` (sound event 332).
- A `DA_Blocks` change goes through SM-Blocks and the PM.
- **The furnace screen** (8a2): the recipe-book button stays drawn and inert. The title and button positions are provisional.
- **Saved data:** the block-entity store stays in memory until the voxel plan's phase 10. No save file, no version bump.
- **Item reference:** extend it only through `build_reference.py` and `ItemFacts.java`, appending an `APPEND_LAST` group. Prove append-only with `C:\dev\sandbox\inv8a2\proof\prove_append.py <old> <new>`. After any reference change, DA_Items must be moved away and reimported, because it records the reference hash.
- Track branches stay local; never push.

## 4. Code map (8a2)

| Area | Where |
| --- | --- |
| Screen layout | `FHudContainerModel`: `GetSlotPosition(Kind, Slot)` (furnace branch through `IsContainer`), `GetRecipeButton`, `GetResultSlot` (the crafting result or `TakeOnly`), `FuelItems`, `GetFlameHeight`, `GetArrowWidth`, the `Furnace*`/`Flame*`/`Arrow*` constants |
| Screen paint | `SHudContainerScreen::OnPaint` (the `bFurnace` branch), `GetBackground`, `GetCurrentView` (public), the `FuelItems` argument |
| View | `FHudContainerView::bLit/LitFraction/CookFraction`, filled in `UFoundationPanels::OpenContainerScreen`'s `GetView` from `BlockEntities.Find(OpenMenu.Cell)` |
| Art | `Scripts/Editor/ImportContainerGui.py` `SOURCES` (+ furnace.png, lit_progress, burn_progress) |
| Items | `Reference/minecraft-items-26.3/tools/build_reference.py` group `cooking_output`; `FoundationItems.cpp` food rows dried_kelp and baked_potato |
| Tests | `Unnamed.Hud.Container.Furnace`; `Unnamed.Foundation.Furnace.Data/Menu` (charcoal, log smelt, oak_log and beef routes); tour case n `24e-furnace` |

For 8b, the smoker and blast furnace can reuse everything:
- a menu kind or a layout parameter;
- the same slot positions;
- the backgrounds `gui/container/smoker` and `blast_furnace`;
- their own lit/burn sprites, if the jar has them (check `gui/sprites/container/smoker/*`, `blast_furnace/*`).

## 5. Known issues (with "when")

- **Codex 18 #1:** `foundation.Furnace 0` still opens the menu. When: first thing in 8b.
- **Focus loss with a survival screen open:** resuming from the pause menu re-enables game input under the screen. Not a one-line fix. When: a screen-handling pass.
- **Recipes and fuels still unresolved** (106/116 recipes, 234/347 fuels). Missing: rabbit, cod and salmon, chorus fruit, the armor sets and horse/nautilus armor, blaze rod, boats and rafts, signs, hanging signs, shelves, banners, carpets, cushions, bow, crossbow, fishing rod, bowl, composter, lectern, daylight detector, chiseled bookshelf, scaffolding. When: as their owners add them.
- **The creative screen and the pause menu still pause** the simulation. When: a creative-screen step.
- **Provisional:** the furnace title (centred, y 6) and the recipe-book button (20,34); 8a1's rows (stillValid 450 + 400 cm, XP on break, the contents' spread, orbs at the feet).
- **Native warnings:** about 103 "skeleton loot skipped: arrow not defined" (T6's; 20b quiets them).

## 6. What worked and what failed (8a2)

- **Worked:**
  - Python replacement scripts that assert each old block occurs once and keep the file's line endings.
  - `clang-format -i` after edits.
  - The vanilla stand-in frame: the jar's background with its sprites composited at the measured positions, compared per region with the tour shot (`Tests/Evidence/2026-10-15/Inventory-8a2-compare/compare.py`).
  - Mutation checks on formulas: a short PowerShell loop that rewrites, builds, runs one test, then restores.
  - The append proof script.
- **Failed:**
  - **A relative `-script=` path** made the editor look in Engine/Binaries; use an absolute path.
  - **`sed` replacements with `\r` or `\d` in Windows paths** wrote control characters. Use Python bytes, or check with `cat -A`.
  - **ImportItems ran before `ItemImportData.py --write-tags`:** it stops on the stale tags ini, but only after creating sprites. Run `--write-tags` first.
  - **A reference README or tool edit after an import** changes the hash DA_Items records, which forces a second reimport. Finish all reference edits, including review fixes, before importing.
  - **Review fixes mid-run:** two final runs were cancelled because fixes touched files the run covers. Run the review before the final checks when the diff is small.

## 7. Token figures (8a2)

Lead: 205.0k peak at the screen's report A, figure for the items report in report A. Subagents: screen reviewer 75.2k, items reviewer 70.8k.

## 8. Evidence (8a2)

- `Tests/Evidence/2026-10-15/Checks-final-20261015-045353/summary.txt`: the screen, GREEN.
- The items final run: see report A.
- The tour shot: `Tests/Evidence/2026-10-15/Playground-tour/Runtime-20261015-050838/runtime-60-tour-24e-furnace.png`.
- The vanilla comparison: `Tests/Evidence/2026-10-15/Inventory-8a2-compare/furnace-vanilla-vs-ours.png`.
- `C:\dev\sandbox\inv8a2\`: build logs, review diffs, mutants, import logs, `proof\proof-2.txt`, `recipe_counts.txt`, `items-ref-old` (the reference before), `items-assets-before` (DA_Items before).

## 9. Open questions (for the SM)

1. Smoker and blast furnace as new `EFoundationMenuKind` values, or one furnace kind with a block parameter? Recommended: new kinds, matching vanilla's menu types. The layout rows are identical.
2. Whether the lit state needs SM-Blocks (`DA_Blocks` already has the `lit` property if B1 imported it). Check first, then ask through the PM if needed.

## 10. Short prompt for step 8b

> Survival program, track T1 Containers, step 8b: the lit furnace, the smoker and the blast furnace. Worktree C:\dev\projects\Unnamed-inv, branch feature/survival-containers. Start with Docs/Records/2026-10-15-Inventory-Handoff-Step8b.md (reading list, settled decisions, code map, known issues), then the 8a2 record. First item: Codex review 18 #1 (foundation.Furnace 0 must pass the use on, as before 8a1, with a switch-0 test). Then the lit state and light 13, particles, the smoker and blast furnace (screens identical to the furnace's, speed 2) and the fire-crackle sounds; any DA_Blocks change is SM-Blocks', through the PM. Diff the hot files against feature/survival, send the plan to SM-Containers (session_01EuH6RLbTU1zzFVHK75nWYJ) and wait for the go.
