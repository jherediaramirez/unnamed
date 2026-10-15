# Use lane handoff: step 16, survival fixes

From step 17 (buckets, commit `e65229c0` on `feature/survival-use`, worktree `C:\dev\projects\Unnamed-use`). Survival program track T3. **Base: `e65229c0` plus the step-17 landing merge of `feature/survival` (229528f or later; merge hash: _to be added_).** Don't edit the lane until that merge has landed; the manager sends "17 landed <hash>". The sub-manager is "SM-Player" (session `session_01AQoPHG1guSaFSFHz6Pqd8N`); reports go there.

## Short prompt

> Survival program, track T3, step 16: survival fixes. Lane `C:\dev\projects\Unnamed-use`, branch `feature/survival-use`. Read `Docs/Records/2026-10-14-Use-Handoff-Step16.md` first and follow its reading list. The base is e65229c0 plus the step-17 landing merge: check `git log` shows the merge before you edit anything. Six fixes, one switch each: (a) lava contact damage, ignition and burning; (b) the swim pose's hitbox and eye height; (c) fall damage rounding confirmed by a test; (d) pick block from the main inventory; (e) Codex review 9's two findings; (f) a test for the bucket's full-inventory held repeat. Your plan may split the step in two. Start with a plan: every file, checked against the hot files after diffing them with the latest `feature/survival`. Report it to the manager session and wait for the go.

## 1. Reading list, in order

1. This file.
2. [2026-10-14-Use-Buckets.md](2026-10-14-Use-Buckets.md), sections 1 and 3 (the latest change; its known issues feed (f)).
3. `Docs/Items.md`: "Buckets (step 17)", "Eating (step 10)" (for (e)).
4. Values. Grep these files, don't read them whole:
   - `C:\dev\tools\shift-managers\survival\value-swaps.md`, lines 89-93 (lava contact, burning, damage immunity, swim pose, fall damage) and line 234 (pick block).
   - `C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md`: grep `survival.r2.pick`, `survival.r2.rules` and `hurt.`; the 14 pick cases are F 2608-2623 (read only that range).
   - `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md`: around lines 291 (`hurt.lava_contact`, `hurt.lava_ignite`, `hurt.burning`), 326 (`fall.damage_thresholds`), 361 and 698 (`player.pose_dimensions`). **Never open Appendix H (from about line 8503) or `value-holder\build|src`.**
5. Codex review 9: `C:\dev\tools\shift-managers\survival\codex\reviews\9-T3-10b\report.md` (findings 1 and 2 only).
6. Code:
   - `FoundationVitals.h/.cpp`: `Hurt`, `DamageCooldown`/`IsInHurtWindow` (lines 65-104 of the header), `GetFallDamage` (cpp line 304) and its call at cpp line 66.
   - `FoundationLab.h` line 74: the water/lava contact query (lava wins).
   - `FoundationProfile.h`: `CrouchingHeightCm` (line 20) and the swim constants (line 80).
   - `FoundationInteraction.h` line 154: `Pick`.
   - `FoundationInteractionView.cpp`, `HandleEvents` (`Eating`/`Ate`: `SpawnCrumbs` takes the camera's rotation) for (e) #1.
   - `Tests/FoundationVitalsTests.cpp` around line 278 (fall damage tests), `Tests/FoundationBucketTests.cpp` (`FBucketLab`, the client test under both presets) for (f).

## 2. Scope (manager, 2026-10-14), one switch each

- **(a) Lava and burning:** lava contact 4 HP at ticks 1, 11, 21, … (`hurt.lava_contact`) and ignition 300 fire ticks (`hurt.lava_ignite`); burning 1 HP every 20 ticks in air, ended by water (`hurt.burning`); damage immunity per `hurt.cooldown` (cooldown 20; while above 10, equal or weaker hits are rejected and a stronger one applies only the excess). Sounds `entity.player.hurt_on_fire`, `entity.generic.burn`, `entity.generic.extinguish_fire` are in `DA_Sounds`. Fire-block contact waits on T5 B1 (known issue).
- **(b) Swim pose:** hitbox and eye height only: 0.6 x 0.6 with the eye at 0.4 (`player.pose_dimensions`). No arm or body animation (first person, no hands).
- **(c) Fall damage:** confirm floor(d + 1e-6 − 3) (`fall.damage_thresholds`: 3.5 and 3.9 give 0, 4.0 gives 1) with a test; probably no code change.
- **(d) Pick block from the main inventory** per R2 (`survival.r2.pick`, `survival.r2.rules`, the 14 cases at F 2608-2623): a hotbar hit selects in place; a main-inventory stack goes to the first empty hotbar slot cyclic from the selection, else the first non-enchanted, else the selected (swap); survival never creates; creative creates 1 (the displaced stack to the first empty storage slot). Ctrl+pick waits on block entities (known issue).
- **(e) Codex review 9:** #1 the crumbs use the pawn's aim, not the camera; #2 the EatMotion 0 probe must tell the two motions apart.
- **(f)** A test that a bucket refused for a full inventory waits the held repeat (step 17's review finding 2), under both presets, with its own mutation.

## 3. Working agreement

- Gameplay goes only through `FFoundationAuthority` commands (vitals through their owner). Each fix gets a switch.
- **Plan first:** list every file against the hot files (`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`), diffed against the latest `feature/survival`. Report the plan and wait for the go. Splitting into 16a/16b is the plan's call.
- **Pre-commit review:** Sonnet 5.5, diff only, one file path and its line count; split past about 1,500 lines. Step 17's peaked at 81.3k for 957 lines.
- **Checks:** clang-format, then `Scripts\RunChecks.ps1 -Profile final -Flight smoke -ExpectedHashes C:\dev\tools\shift-managers\expected-hashes-main.txt` after the last code change; re-run it after review fixes.
- **Never:** change or hand-merge DA_Items, DA_Blocks, DA_Sounds or DA_Recipes; edit CLAUDE.md; push; use a bare `git stash`; re-record goldens or world hashes (stop and report).
- **Game code:** values only from the reports' text, the wiki or black-box runs. Never Appendix H, decompiled code or bytecode.

## 4. Settled decisions

- Vanilla first when a call is easy to change later. First person only, no hands: (b) changes the hitbox and the eye, nothing drawn.
- Sounds in `DA_Sounds` play through `BreakEffects->PlayEvent` from an authority event (`Stripped`, `Filled` show the shape).
- A bucket is an instant item use in `UseItemStart` (`UseBucket`); its client path is `UsePipeline`, and `Attempt` sets the held repeat after a fill, a pour or an `InventoryFull` refusal.

## 5. Known issues, with when

- Step 17: a plant a pour replaces gives no drop (pops with item entities); the rising-spread lava x4 roll (fluids track); creative, Adventure and the extinguish sound's numbers are wiki values (a vanilla capture); cauldrons (T5 B1), milk and mob buckets (mobs).
- Step 10b: motion blur on the held item (a project-wide look step); provisional eat-motion and crumb values (a vanilla capture); offhand meal has no motion (the offhand drawn); dropping from the eaten stack keeps the meal going (**step 16 may take it**); the food table is not in `DA_Items` (an `ImportItems.py` migration).
- Step 14: no copper scrape or wax-off, no shield-offhand exception, no arm swing (copper, shields).

## 6. Code map (step 17's additions)

- `FoundationAuthority.h/.cpp`: `FoundationBucket::IsBucket`; `UseBucket` (fill, pour, waterlog, creative keep, stacks, `InventoryFull`); `Resolve(Facts, bFluidSources)`; events `Filled`, `Emptied`, `Extinguished` (from `SettleAfterFluid`, switch 1 only); `BucketUses`; result flags `bFilled`, `bEmptied`.
- `FoundationTargeting.h/.cpp`: `FFoundationRay::bFluidSources`, `GetSourceBox`, the eye-inside hit.
- `FoundationInteraction.cpp`: `UsePipeline` (buckets send `UseItemStart`; only a use with a duration is held), `Attempt` (held repeat).
- `FoundationInteractionView.cpp`: bucket and extinguish sounds. `FoundationController.cpp`: three event names.
- `FoundationInteractionProbe.cpp`: scenario 13e (pour, fill, control).
- `Tests/FoundationBucketTests.cpp`: `.Bucket`, `.BucketModes`, `.BucketWaterlog`, `.BucketExtinguish`.

## 7. What worked and what failed

- **Worked:**
  - Python patch scripts that read and write bytes (`read_bytes().decode()` / `write_bytes`), with exact-match asserts. `read_text`/`write_text` turned an LF file into CRLF on Windows.
  - `C:\dev\sandbox\use-lane\run_items.ps1 -Tag -Filter` (about 20 s) and `mutate.ps1` (one mutation, built, run, restored). A mutation's `-To` can't be empty; use `'    ;'`.
  - Running the review while the final run goes, then applying fixes after it ends and running final again.
  - `MeasureAgentUsageCdev.mjs --session <full transcript uuid>` (a short id is not found).
- **Failed:**
  - A held-repeat mutation survived under VanillaReference alone (its attempt cadence already waits); the commit-cadence preset LucidFluid catches it. Run cadence tests under both.
  - The controller's event-name table has a static_assert pinned to the last event kind: a new kind compiles without a name until the assert is moved. Grep `"one name per event kind"` when adding events.

## 8. Token figures

Lead peak 235.4k at report A (MeasureAgentUsage, transcript `8d6c380c-f5ac-500d-b898-95e222ab67a9`). The Sonnet 5.5 review peaked at 81.3k.

## 9. Evidence (step 17)

- `Tests/Evidence/2026-10-14/Checks-final-20261014-195026/summary.txt`: GREEN with the flight smoke. Native 369, interaction 120 checks, runtime-all 165, hashes 9 of 9.
- Captures: `Tests/Evidence/2026-10-14/Playground-interaction/Runtime-20261014-200524/runtime-60-interaction-buckets-on.png`, `-buckets-off.png`.
- Mutations: `C:\dev\sandbox\use-lane\runs\mut-creative-keep-191057`, `mut-waterlog-flip-191127`, `mut-extinguish-191216`, `mut-held-repeat2-191344`, `mut-extinguish-switch-194931`.

## 10. Open questions (with options)

- **Split:** one step or 16a (a, b, c) and 16b (d, e, f). Recommend deciding in the plan from the file overlap; (a) and (b) both touch the pawn and vitals.
- **Burning in rain:** vanilla rain puts out burning players; there is weather beyond vanilla here. Recommend water only for 16 (the scope's words), rain as a known issue unless the weather system exposes "is raining at the player" cheaply.
- **Push of `feature/survival-use`:** Jesus's call.
