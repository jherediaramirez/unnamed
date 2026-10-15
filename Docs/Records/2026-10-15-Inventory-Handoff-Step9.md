# Handoff: survival step 9 (the barrel), then 8b2b (the furnace particles) (track T1 Containers)

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. Step 8b2a is committed; its hash is in report B. Report to SM-Containers (session_01EuH6RLbTU1zzFVHK75nWYJ). The program manager is PM 4 (session_01AARfM5mjEz5npqB79VSTLC).

SM-Containers' order (ACTION-PLAN.md, Line 1):
1. step 9, the barrel;
2. 18-chest;
3. 8b2b, the particles.

Each is its own chat. Section A is step 9's handoff; section B is 8b2b's, settled now so it needs no new plan round.

## Working agreement (both)

As in 8b1 and 8b2a:
- **Plan first:** every file, diffed against the latest feature/survival. Wait for the go.
- **Review:** a Sonnet 5.5 pre-commit review on the diff only, before the final checks.
- **Mutation checks** on new rule tests: `C:\dev\sandbox\inv8b2\mutate.ps1`. It takes a table of file, old text, new text and test; it builds, runs, restores and rebuilds. Don't edit while it runs.
- **Final checks:** `Scripts\RunChecks.ps1 -Profile final -Flight smoke` after the last code change.
- **Context:** measure with `node Scripts/MeasureAgentUsage.mjs`. Copy the session `.jsonl` into `C:\dev\sandbox\inv5b\measure-cfg\projects\C--dev-projects-Unnamed\` (overwrite; deleting there is blocked), then set `CLAUDE_CONFIG_DIR=C:\dev\sandbox\inv5b\measure-cfg`.
- **Editor scripts:** an absolute `-script=` path and `Start-Process -Wait`. Keep LF endings (`newline='\n'`).
- **Commit messages:** `git commit -F <file>`.
- **Test labs:** a lab whose first command is a container click must set `Inventory.bFull` itself. The authority applies `foundation.Inventory.Full` only on the first command; before that, main-grid `SetSlot`s are silently ignored (8b2a lost a build round to this).

## A. Step 9: the barrel

### Reading list

1. `Docs/Records/2026-10-15-Inventory-8b2a.md`, all of it. Then `2026-10-15-Inventory-8a1.md`, "What 8a1 built": the block-entity store, `FindBlockUse`, stillValid, and breaking a container dropping its slots.
2. `Docs/Items.md`, "The furnace, smoker and blast furnace": the store, the menu, breaking, the screen.
3. `C:\dev\projects\value-holder\SURVIVAL-RUN2-FINDINGS-26.3.md` (grep only):
   - R16, lines 158-185: `survival2.r16.animation_and_viewers` and the barrel rows. The `open` state flips on the first viewer and the last one leaving. `block.barrel.open` / `.close` play at volume 0.5, with pitch measured 0.92-0.99, from the block centre with range 16;
   - the full traces at `### survival2.r16.openers` (line ~40354).
4. `C:\dev\tools\shift-managers\survival\ACTION-PLAN.md`, Line 1 #4: 27 slots, the `generic_54` layout.
5. value-swaps.md: the generic_54 / barrel screen rows (grep `generic_54`, `Barrel`).
6. Code, by symbol:
   - `FFoundationBlockEntities` (the store, `EFoundationBlockEntityKind`);
   - `FFoundationMenuLayout::Get` (every table indexed by `EFoundationMenuKind`: layout, positions, button, art, title, the event-log name; 8b1 found them all through `IsFurnaceMenu`);
   - `MakeContainerRules`;
   - `UseFurnace` / `WouldUseFurnace` (the use pattern);
   - `HudContainerScreen.cpp` `GetFurnaceArt`;
   - `ImportContainerGui.py`.

### Settled decisions

- **Store and save:** the barrel uses the 8a store: a new `EFoundationBlockEntityKind::Barrel` with 27 slots, in memory like the furnace (no save file, no version bump; PM 3's call).
- **Menu:** a new `EFoundationMenuKind::Barrel`. Container 0-26, main 27-53, hotbar 54-62 (vanilla's ChestMenu with 3 rows). Shift-click: container to player from the hotbar's end (hotbar 8..0, main 35..9), player to container from the first slot.
- **Screen:** `textures/gui/container/generic_54.png`, top 3 rows plus the player part, as vanilla's 3-row ChestMenu draws it. Title "Barrel".
- **The `open` state:**
  - switches on the first viewer and off when the last viewer leaves (R16, verified);
  - plays `block.barrel.open` / `.close` at volume 0.5, pitch 0.9 + 0.1 x U, attenuation 16 (the chest's is 12). Source: value-swaps.md line 261 (F 3551-3555, `survival.r5.play_calls`), not provisional. R16's samples (0.92-0.99) fit it;
  - no animation (the barrel has no lid model);
  - the change is a plain state change, like the furnace's `lit` (no `Placed` event).
- **Switch:** the barrel opens under `foundation.Furnace`'s pattern with its own switch, `foundation.Barrel` (default 1). At 0 the use passes, as in 8b1's Codex 18 #1 fix.
- **Breaking** drops its 27 slots (8a1's path).
- **Assets:** generic_54 is a new GUI texture of our own (no lock), with a `Docs/StandIns.md` row. No DA_Blocks change: `barrel[open]` already exists.
- **Codex 18 #2** (instance ids through a shared container, two players) waits for multiplayer.

### Known issues

- Light 13 (T5 B3a).
- The furnace ambience scheduling.
- The creative screen pausing.
- The focus loss under a survival screen.
- The missing recipe and fuel items.

## B. Step 8b2b: the furnace, smoker and blast furnace particles (after 18-chest)

### Reading list

1. This section, then `2026-10-15-Inventory-8b2a.md` ("Codex review 25, finding 1": the clock the particles share).
2. `SURVIVAL-RUN2-FINDINGS-26.3.md` R15, lines 126-157: the particle table and `survival2.r15.exact_callback_chances`.
3. `Docs/Breaking.md`, the particle sections. `FFoundationBreakEffects::SpawnCrumbs` (its pool, `SpawnInto`, `UpdatePool`) and `Scripts/Editor/CreateBreakMaterials.py` `build_particle` (the `Graph` helper to reuse).
4. `UFoundationInteractionView::PlayFurnaceSounds` and `FoundationFurnace::StepAmbienceClock`.

### Settled design (SM-Containers approved it with the split)

- **Renderer** (the 8b2 handoff's option (a)): a third pool in `FFoundationBreakEffects` next to the crumbs. It has its own step: no gravity and no collision. The motion after spawn is provisional (below).
  - Material: a new `M_FurnaceParticle`, with the whole texture's UV, the layer from per-instance custom data, smoke lit, flame unlit or full bright.
  - Textures: a new `T_FurnaceParticles` texture array with `smoke_0`..`smoke_7` and `flame` (9 layers, from the 26.3 jar's `textures/particle/`).
  - A new script, `Scripts/Editor/ImportFurnaceParticles.py`, builds both and prints a verify line.
  - All are new assets of our own (no lock), with 9 `Docs/StandIns.md` rows.
  - Smoke steps through its 8 frames over its life (option (a) of the 8b2 handoff).
- **Spawn:** a pure `FoundationBreak::SpawnFurnaceParticles(Kind, Cell box, Facing, Random, Out)`, per R15:
  - per callback: the lit furnace emits one smoke and one flame; the smoker and blast furnace one smoke each;
  - the furnace and blast furnace front: centre + facing x 0.52, lateral U(-0.3, 0.3), y U[0, 6/16) or [0, 9/16);
  - the smoker: (0.5, 1.1, 0.5);
  - velocities uniform within R15's "constructed velocity" min/max (blocks per tick), for example furnace smoke x [-0.0166, 0.0161], y [-0.0065, 0.0255]. The uniform shape is provisional; the bounds are measured;
  - lifetimes uniform in smoke 8-39 and flame 12-43 ticks.
- **Scheduling:** `PlayFurnaceSounds` becomes the one ambience pass. Each callback rolls the sound (0.1) and spawns the particles, on `StepAmbienceClock` (paused: nothing).
- **Switch:** `foundation.Furnace.Ambience 0` draws nothing.
- **Dev cvar** for the tour shot: `foundation.Furnace.AmbienceForce` (default 0, dev only, documented, never shipped on). At 1, every lit block gets a callback each tick, so shot `24i-furnace-particles` is not luck.
- **Provisional rows to add:**
  - the motion after spawn (drag and the smoke's rise; from minecraft.wiki "Particles" or a black-box run, never code);
  - the quad sizes;
  - the frame stepping;
  - the uniform velocity distribution.
- **Tests:**
  - `Unnamed.Foundation.Furnace.Particles`: the counts per kind, every spawn inside R15's offset box per facing, the velocities and lives within bounds, nothing at switch 0;
  - mutation checks on the facing offset, the count and the switch.

### Known issues

- As section A, plus the scheduling model, which stays provisional.

## Token figures (8b2a)

- Lead: 110.8k after the reading; 249.0k at report A (111 requests).
- Subagent: the Sonnet reviewer, 73.3k peak.
- Cost drivers: three debug builds for the 2x2 lab's `bFull`, and two mutation runs.

## Evidence (8b2a)

- `Tests/Evidence/2026-10-15/Checks-final-20261015-093433/summary.txt`: GREEN, final, after the review fixes.
- `C:\dev\sandbox\inv8b2\`:
  - `mutants.txt` and `mutants-2.txt`, with the mutant logs;
  - `tests-1.log` to `tests-4.log`;
  - `review-diff.patch`;
  - `runchecks-final.log`.

## Open questions (for the SM)

1. **Push:** track branches stay local. Pushing is Jesus's call.

## Short prompt for step 9

> Survival program, track T1 Containers, step 9: the barrel (27 slots, generic_54). Worktree C:\dev\projects\Unnamed-inv, branch feature/survival-containers. Start with Docs/Records/2026-10-15-Inventory-Handoff-Step9.md section A (reading list, settled decisions incl. R16's open state and sounds, the 8a store reuse), then the 8b2a record. Diff the hot files against feature/survival, send the plan to SM-Containers (session_01EuH6RLbTU1zzFVHK75nWYJ) and wait for the go.

## Short prompt for step 8b2b (after 18-chest)

> Survival program, track T1 Containers, step 8b2b: the furnace, smoker and blast furnace particles. Worktree C:\dev\projects\Unnamed-inv, branch feature/survival-containers. Start with Docs/Records/2026-10-15-Inventory-Handoff-Step9.md section B (settled design: a pool in FFoundationBreakEffects, M_FurnaceParticle and T_FurnaceParticles, R15's spawn rows, the shared StepAmbienceClock), then the 8b2a record and the latest T1 record. Diff the hot files against feature/survival, send the plan to SM-Containers (session_01EuH6RLbTU1zzFVHK75nWYJ) and wait for the go.
