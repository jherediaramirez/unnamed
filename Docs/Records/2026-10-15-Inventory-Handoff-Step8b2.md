# Handoff: survival step 8b2, the furnace particles and Codex 22's rest (track T1 Containers)

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. Step 8b1 is committed as aa42d5dc; the merge of feature/survival bf7a93e7 follows it, and its hash is in report B. Report to SM-Containers (session_01EuH6RLbTU1zzFVHK75nWYJ). The program manager is PM 4 (session_01AARfM5mjEz5npqB79VSTLC).

## 1. Reading list (exact sections)

1. `Docs/Records/2026-10-15-Inventory-8b1.md`, all of it: what 8b1 built, the review, the provisional rows, the known issues.
2. `Docs/Items.md`, "The furnace, smoker and blast furnace": the 8b1 bullets (smoker and blast, the lit state, sounds, shift-double-click).
3. `C:\dev\tools\shift-managers\survival\codex\reviews\22-T1-8a2\report.md`, findings #2 and #3.
4. `C:\dev\projects\value-holder\SURVIVAL-RUN2-FINDINGS-26.3.md` (59k lines; grep only, never read it whole):
   - R15, lines 126-157 (`survival2.r15.particles`, `survival2.r15.exact_callback_chances`): the particle rows;
   - R23, lines 505-540: `survival2.r23.crafting_rules` (the crafting-result shift-click's partial fit) and `survival2.r23.container_rule` (the menu reach).
5. `Docs/Breaking.md`, the particle sections: `FFoundationBreakEffects`' particle pools. Eating's crumbs (`SpawnCrumbs`, Docs/Items.md "Eating") are the closest pattern: a pool through `M_BreakParticle` with another texture array.
6. Code, by symbol:
   - `UFoundationInteractionView::PlayFurnaceSounds`: the lit-block loop, which particles can share;
   - `FFoundationBreakEffects::SpawnCrumbs` and its pool;
   - `FHudContainerModel::MouseDown` (the double-click guard on `GetResultSlot()`), `CanDragTo` and `GetDragPreview`;
   - `FFoundationContainerMenu::MayPlace` and the fuel slot's one-bucket cap.

## 2. Working agreement

As in 8b1:
- Plan first: every file, diffed against the latest feature/survival. Wait for the go.
- A Sonnet 5.5 pre-commit review on the diff only, run before the final checks.
- Mutation checks on new formula tests.
- `Scripts\RunChecks.ps1 -Profile final -Flight smoke` after the last code change.
- Measure context with `node Scripts/MeasureAgentUsage.mjs`: copy the session `.jsonl` and its subagents folder into `C:\dev\sandbox\inv5b\measure-cfg\projects\C--dev-projects-Unnamed\`, then set `CLAUDE_CONFIG_DIR=C:\dev\sandbox\inv5b\measure-cfg`.
- **Editor scripts:** an absolute `-script=` path and `Start-Process -Wait`.
- **ImportContainerGui.py:** keep its LF endings; write it with `newline='\n'` or .NET `WriteAllText`.
- **PowerShell commit messages:** `git commit -F <file>`. A here-string piped to `git commit -F -` failed (the message was read as a pathspec).

## 3. Settled decisions

- **8b2 order:**
  1. **Codex 22 #2 (low):** a double-click collect must work on the furnace output. Exclude only a crafting result (`Layout.HasCraftResult()`), not `GetResultSlot()`.
  2. **Codex 22 #3 (low):** the drag preview must respect the fuel slot's one-bucket cap. Use one `GetSlotLimit` shared with the authority's `MayPlace`.
  3. Both apply to all three menus (furnace, smoker, blast furnace), which share the layout.
  4. Then the particles.
- **The particles (R15, `survival2.r15.exact_callback_chances`; measured, not provisional):**
  - Each display callback emits:
    - lit furnace: one smoke and one flame;
    - smoker: one smoke;
    - blast furnace: one smoke.
  - Front coordinate: centre + facing x 0.52, lateral scatter +-0.3. Y scatter in [0, 6/16) for the furnace, [0, 9/16) for the blast furnace.
  - The smoker's smoke sits at (0.5, 1.1, 0.5).
  - Lifetimes (ticks): smoke 8-39, flame 12-43.
  - Initial velocities: the "constructed velocity" columns of the R15 table, after the provider's randomisation.
  - The callback's scheduling is the sounds' (8b1's provisional 1 tick in 50 per lit block; R15 says client-spatial). Reuse one scheduler for sound and particles.
- **Art:** `particle/smoke_*` and `particle/flame` from the 26.3 jar are stand-ins, as new assets of our own (no lock). Each gets a `Docs/StandIns.md` row.
- **Switch:** `foundation.Furnace.Ambience` (8b1) covers particles too. At 0 nothing draws.
- **Drop the provisional mark on the menu reach row** (8a1: stillValid within 450 + 400 cm of the block's box). It is verified: `survival2.r23.container_rule` gives a squared eye-to-box distance strictly below (reach + 4)^2, 8.5 blocks from the box. Cite the evidence id in the code comment and in `provisional-values.md`. No behaviour change.
- **R23 shift-craft partial fit** (`survival2.r23.crafting_rules`):
  - A crafting-result shift-click scans the hotbar 8..0, then the main grid 35..9, merging before filling.
  - With room for only part of a result, the rest is dropped and one craft is consumed. With no room, nothing happens.
  - Check the crafting table's and the inventory's result against these five cases. Fix only if they differ; it belongs to T1's crafting code (steps 6-7).
- **Light 13** is SM-Blocks' work (T5 B3a, per-state light for full cubes). Nothing changes in T1; the lit states already switch.
- **Codex 18 #2** (instance ids through a shared furnace, two players) waits for multiplayer.
- No DA_Items, DA_Blocks or DA_Sounds change. Track branches stay local; never push.

## 4. Code map (8b1)

| Area | Where |
| --- | --- |
| Use and switch 0 | `FoundationAuthority.cpp`: `UseFurnace`, `WouldUseFurnace`, the built-in use table in `FindBlockUse` |
| Cooking kind and speed | `FoundationContainers.h/.cpp`: `FoundationFurnace::GetCookingKind`, `GetSpeed`, `GetCookTotal`, `FFoundationCooking::GetBurnTicks(Item, Kind)` (fuels as an `FIntVector` of furnace, smoker and blast ticks) |
| Lit state | `FFoundationAuthority::StepFurnaces` (`Relit`, applied after the loop) |
| Menus | `EFoundationMenuKind::Smoker`, `BlastFurnace`, `IsFurnaceMenu` (`FoundationContainer.h`); `MakeContainerRules(Block)` for shift-click routing |
| Screens | `HudContainerScreen.cpp`: `GetFurnaceArt` (background, flame, arrow, title per kind); `HudContainer.cpp`: positions and button through `IsFurnaceMenu`; `MouseUp`'s side filter (Codex 22 #1) |
| Sounds | `UFoundationInteractionView::PlayFurnaceSounds`, `FurnaceSoundClock`; `foundation.Furnace.Ambience` (`FoundationContainers.cpp`) |
| Tour | `FoundationTourProbe.cpp`: case n loops over `TourFurnaces` (24e-24g); case o is the lit block (24h) |
| Tests | `Unnamed.Foundation.Furnace.Switch0`, `.Speed`; `Unnamed.Hud.Container.Furnace` (the new kinds' positions, shift-double-click both sides) |

## 5. Known issues (with "when")

- **Light 13:** lit states give light 0. When: T5 B3a (SM-Blocks).
- **Codex 22 #2 and #3.** When: first in 8b2.
- **The sound and particle scheduling** is provisional: 1 tick in 50 per lit block, a 16-block reach, the sound at the bottom centre. When: a scheduling model or capture.
- **Switched off mid-burn,** a block keeps its last lit state (as its entity keeps its timers). Accepted.
- **Native warnings:** about 109, mostly T6's skeleton loot. Report B identifies the one 8b1 added.
- **Carried over:**
  - focus loss under a survival screen;
  - the creative screen pausing;
  - the missing recipe and fuel items;
  - Codex 18 #2.

## 6. What worked and what failed (8b1)

- **Worked:**
  - `C:\dev\sandbox\inv8b\mutate.ps1`: replace text once, build, run one test, restore, rebuild. Five mutants in one background run.
  - The tour as a loop over a struct table, so a new block is one row.
  - 8a2's `compare.py`, extended with a block argument.
  - Deferring world writes out of the store loop.
  - Applying the review before the final run.
- **Failed:**
  - The first final run went red on a voxel flake (an HttpServer socket error under shared load). It passed alone; rerun the whole profile rather than patching the summary.
  - **`git commit -F -` from a PowerShell here-string:** use a message file.
  - **Editing during a background build or mutation run:** don't. The mutation script rewrites sources.

## 7. Token figures (8b1)

- Lead: 297.5k at the review, 336.4k at report A.
- Subagent: the Sonnet reviewer, 102.0k.

## 8. Evidence (8b1)

- `Tests/Evidence/2026-10-15/Checks-final-20261015-075811/summary.txt`: GREEN, final, after the review fixes.
- `Tests/Evidence/2026-10-15/Inventory-8b1-compare/`: the smoker and blast furnace against vanilla.
- The tour shots: `Tests/Evidence/2026-10-15/Playground-tour/Runtime-20261015-073939/runtime-60-tour-24f-smoker.png`, `-24g-blast-furnace.png`, `-24h-lit-furnace.png`.
- `C:\dev\sandbox\inv8b\`:
  - `mutants.txt` and its logs;
  - `tests-1.log`, `tests-2.log`;
  - `import-gui-1.log`;
  - `shiftplan-rerun.log`;
  - `review-diff.patch`.

## 9. Open questions (for the SM)

1. **The particle renderer.** Options:
   - (a) a pool in `FFoundationBreakEffects` like the crumbs, with a smoke/flame texture array. Recommended: one system, and the switches already exist.
   - (b) a small new presentation class next to `PlayFurnaceSounds`.
2. **Smoke animation.** Vanilla's smoke steps through `smoke_0`-`smoke_7` over its life. Options:
   - (a) the 8 frames as array layers. Recommended: closer to vanilla.
   - (b) one frame.

## 10. Short prompt for step 8b2

> Survival program, track T1 Containers, step 8b2: Codex review 22 #2/#3, then the furnace, smoker and blast furnace particles. Worktree C:\dev\projects\Unnamed-inv, branch feature/survival-containers. Start with Docs/Records/2026-10-15-Inventory-Handoff-Step8b2.md (reading list, settled decisions incl. the R15 particle rows and R23's reach and shift-craft items, code map, known issues), then the 8b1 record. Diff the hot files against feature/survival, send the plan to SM-Containers (session_01EuH6RLbTU1zzFVHK75nWYJ) and wait for the go.
