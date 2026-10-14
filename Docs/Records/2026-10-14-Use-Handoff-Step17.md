# Use lane handoff: step 17, buckets

From step 10b (eating polish, commit `8474c32` on `feature/survival-use`, worktree `C:\dev\projects\Unnamed-use`). Survival program track T3. **Base: `8474c32` plus the 10b landing merge of `feature/survival` (38399ad or later; merge hash: _to be added_).** Don't edit the lane until that merge has landed. After 17 comes step 16 (survival fixes). The sub-manager is "SM-Player" (session `session_01AQoPHG1guSaFSFHz6Pqd8N`); reports go there.

## Short prompt

> Survival program, track T3, step 17: buckets. Lane `C:\dev\projects\Unnamed-use`, branch `feature/survival-use`. Read `Docs/Records/2026-10-14-Use-Handoff-Step17.md` first and follow its reading list. The base is 8474c32 plus the 10b landing merge: check `git log` shows the merge before you edit anything. Payoff: an empty bucket scoops a water or lava source; a water or lava bucket pours one. No cauldrons (T5 B1) and no milk (mobs). The sounds item.bucket.fill / fill_lava and empty / empty_lava go through a silent hook until T5's sound batch lands. The fluid delays come from value-swaps (water 5 ticks, lava 30 / 10 fast; `extra.fluids`). block.lava.extinguish plays when water meets lava. All behind `foundation.Items.Use`. Start with a plan: every file, checked against the hot files after diffing them with the latest `feature/survival`. Report it to the manager session and wait for the go.

## 1. Reading list, in order

1. This file.
2. [2026-10-14-Use-EatingPolish.md](2026-10-14-Use-EatingPolish.md): sections 1, 2, 4 and 5 (the latest change; the crumb pool and the eat motion). Then [2026-10-14-Use-Stripping.md](2026-10-14-Use-Stripping.md), section 1: `UseOn`'s item-on-block step, which buckets extend.
3. `Docs/Items.md`: "Eating (step 10)" and "Stripping (step 14)" (the switch, `UseOn`'s order and the mode rules).
4. Values. Grep these files, don't read them whole:
   - `C:\dev\tools\shift-managers\survival\value-swaps.md`, lines 86-87 (water and lava flow, `extra.fluids`) and line 235 (bucket sounds, `survival.r5.play_calls`).
   - `C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md`: grep `bucket`, `extinguish` and `survival.r5.play_calls` (around line 3545).
   - `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md`: grep `bucket`, `fluid` and `extra.fluids`. **Never open Appendix H (from about line 8503) or `value-holder\build|src`.**
5. Scout AC's plan, `C:\dev\sandbox\scout-player\plan.md`: line 229 (step 17 row) and line 20 (Items S9).
6. Code:
   - `FoundationAuthority.cpp`: `UseOn` (step 2 `StripOn`, step 3 the place capability) and `StripOn` (the shape a bucket use copies: `Mode`, wear, event).
   - `Voxel/VoxelFluidSystem.h/.cpp`: `OnSet` (how a block edit wakes the fluids), `FSettings` (the delays), `Advance`.
   - `FoundationAuthority.h`, around line 423: `SettleAfterFluid`.
   - `FoundationInteractionView.cpp`, `HandleEvents`: `Stripped`, `Eating`/`Ate`, which show how an event plays a sound through `BreakEffects->PlayEvent`.
   - `Tests/FoundationStrippingTests.cpp`: `FStripLab` (aiming at a cell, `Use()`, `Put()`).

## 2. Scope (manager, 2026-10-14)

- **Buckets:**
  - An empty `bucket` used on a water or lava **source** becomes `water_bucket` / `lava_bucket`, and the source becomes air.
  - A full bucket poured on a block face places a source in the adjacent cell and becomes an empty `bucket`.
  - Survival consumes and returns the stack; creative keeps the bucket. Check the vanilla rule in the reports or the wiki before choosing.
  - Behind `foundation.Items.Use`.
  - **Not now:** cauldrons (they wait on T5 B1); milk (waits on mobs).
- **Sounds:**
  - `item.bucket.fill` / `item.bucket.fill_lava`: category PLAYERS, volume 1, pitch 1.
  - `item.bucket.empty` / `item.bucket.empty_lava`: category BLOCKS, 1/1.
  - Source: `survival.r5.play_calls`. The four events are in T5's sound batch (`DA_Sounds` is asset-locked for this lane), so call `PlayEvent` with them anyway; it is silent for an unknown event until the batch lands.
- **Fluid delays** from value-swaps (`extra.fluids`):
  - water: 5 ticks, drop-off 1, slope search 4;
  - lava: 30 ticks, 10 when fast; drop-off 1/2; slope search 4/2;
  - some rising spreads ×4 delay on a 3/4 roll.
  - Check how `VoxelFluidSystem`'s `FSettings` holds them today. Changing them may shift captures or hashes: if so, stop and report.
- **`block.lava.extinguish`** when water meets lava (where the fluid system turns lava to stone, obsidian or cobblestone).

## 3. Working agreement

- Gameplay goes only through `FFoundationAuthority` commands. Each step gets a switch; 17 stays behind `foundation.Items.Use`.
- **Plan first:** list every file against the hot files (`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`), diffed against the latest `feature/survival`. Report the plan and wait for the go.
- **Pre-commit review:** Sonnet 5.5, diff only. Give the reviewer one file path and the line count; split it past about 1,500 lines. Step 10b's review peaked at 80.5k.
- **Checks:** run `Scripts\RunChecks.ps1 -Profile final -Flight smoke -ExpectedHashes C:\dev\tools\shift-managers\expected-hashes-main.txt` after the last code change. Run clang-format **before** it.
- **Never:** change or hand-merge DA_Items, DA_Blocks or DA_Sounds (asset locks); edit CLAUDE.md; push; use a bare `git stash`; re-record goldens or world hashes (stop and report instead).
- **Game code:** values only from the reports' text and raw data, the wiki, or black-box runs. Never Appendix H, decompiled code or bytecode.

## 4. Settled decisions

- Hold-to-use has its own commands (`UseItemStart/Continue/Release`). Instant uses on a block go through `UseOn` (step 14 strips there), so buckets go there too, as step 2b after `StripOn`.
- `UseOn` and `StripOn` take the player mode (step 10b). Adventure passes a plain axe (`survival.r1.modes`); check whether the reports say anything about buckets in Adventure.
- Vanilla first when a call is easy to change later. Item only, no arm (first person, no hands).
- Sounds that aren't in `DA_Sounds` yet are called anyway, and play silently until T5's batch lands.

## 5. Known issues, with when

- **Motion blur:** UE's default motion blur smears the held item during the eat jiggle, and vanilla has none. No per-component opt-out was found. When: a project-wide look step (PM's call; the manager raises it).
- **Provisional eat-motion and crumb values:** the eat motion's first-tick clamp and the crumbs' spawn point, size, life and 0.4-block near cull. When: a vanilla capture (Jesus's go).
- **Offhand meal:** has no motion. When: when the offhand is drawn.
- **Dropping from the eaten stack:** dropping one item keeps the meal going. When: step 16.
- **Food table:** not in `DA_Items`. When: an `ImportItems.py` migration.
- **Step 14 gaps:** no copper scrape or wax-off, no shield-offhand exception, no arm swing on a strip. When: copper oxidation and waxing; shields.

## 6. Code map (step 10b's additions)

- `FoundationAuthority.h/.cpp`:
  - `EFoundationReason::ItemUseOff`;
  - `FoundationItemUse::IsEffectTick(Ticks, Duration)`;
  - `UseItemContinue`'s switch-0 cancel;
  - `UseOn`/`StripOn(…, EFoundationPlayerMode)`.
- `FoundationInteractionView.h/.cpp`:
  - `foundation.Hand.EatMotion`;
  - `FoundationHand::MakeEatMatrix/MakeEatPivot`;
  - `UpdateHeldItem` (`EatTicks`, `EatTickAge`);
  - `HandleEvents`: eat volume, `SpawnCrumbs`.
- `FoundationBreakEffects.h/.cpp`:
  - `FoundationBreak::SpawnCrumb` and the crumb constants;
  - `SpawnCrumbs`, `SpawnInto`, `UpdatePool(…, NearCm)`;
  - the crumb pool, which uses an item-sprite MID of `M_BreakParticle`. A splash of item particles could reuse it.
- `Tests/FoundationEatingTests.cpp`: `.EatPolish`. `Tests/FoundationStrippingTests.cpp`: Adventure.

## 7. What worked and what failed

- **Worked:**
  - Python patch scripts with exact-match asserts. Write them with the Write tool: a bash heredoc broke on an apostrophe.
  - `C:\dev\sandbox\use-lane\run_items.ps1 -Tag -Filter` (build plus one filter, about 20 s).
  - `mutate.ps1` (one mutation, built, run, restored).
  - `MeasureAgentUsageCdev.mjs --session <transcript id>`. The id is the jsonl's name under `C:\Users\jherediaramirez\.claude\projects\C--dev\`; find it by grepping for the short prompt.
  - Fitting a closed form black-box to raw report matrices, then embedding the samples in a test.
  - The final profile includes the interaction probe and its captures, so a separate probe run is only needed for a quick look.
- **Failed:**
  - A sed on a shared loop pattern almost changed an older test; check `git diff` after any sed.
  - A float bound of 1e-6 on velocities: `FMath::SinCos` is a polynomial approximation, so use 1e-3 cm/s.
  - Starting the final run before the look call came back cost a cancelled run. Send the look call and wait when changes are likely.

## 8. Token figures

Lead peak 262.0k at report A (MeasureAgentUsage, transcript `58484b26`). The Sonnet 5.5 review peaked at 80.5k.

## 9. Evidence (step 10b)

- `Tests/Evidence/2026-10-14/Checks-final-20261014-171802/summary.txt`: GREEN with the flight smoke. Native 355, interaction 108 checks, runtime-all 153, hashes 9 of 9.
- Captures: `Tests/Evidence/2026-10-14/Playground-interaction/Runtime-20261014-172952/runtime-60-interaction-eating-on.png`, `-eating-ease.png`, `-eating-off.png`.
- Mutation runs: `C:\dev\sandbox\use-lane\runs\mut-*` (165917, 165946, 170014, 170043, 170715).

## 10. Open questions (with options)

- **Fluid delays:** if `FSettings` already uses other delays, swap to the value-swaps numbers (recommended, vanilla first) or keep them and record a provisional row. Ask the manager if hashes or captures would shift.
- **Bucket in creative:** keep the stack unchanged as vanilla does (recommended, if the report or wiki confirms) or consume as in survival.
- **Pour target:** a waterloggable block takes the water as a state (vanilla) or the source goes in the adjacent cell only. Recommend the adjacent cell only for 17, with waterlogging as a known issue, unless the block reference makes it cheap.
- **Push of `feature/survival-use`:** Jesus's call.
