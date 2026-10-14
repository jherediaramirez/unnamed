# Use step 10b: eating polish (record)

Lane `feature/survival-use` (worktree `Unnamed-use`), from `366458b`. Survival program track T3, step 10b, from [the step 10b handoff](2026-10-14-Use-Handoff-Step10b.md). Everything is behind `foundation.Items.Use`; the motion has its own presentation switch, `foundation.Hand.EatMotion`. Next on this lane: step 17 (buckets), then 16 (survival fixes).

## 1. What was built

- **(a) Eating motion** (`FoundationInteractionView.h/.cpp`, `FoundationHand`):
  - `MakeEatMatrix(Remaining, Duration)` is vanilla's eat transform as a closed form, fitted black-box to the value report's 33 right-hand matrices (`eating.hand`, `eating.hand_transform`, value-holder `reports/reference/data/client.json`, arm RIGHT).
    - Let f = remaining ticks − partial + 1 and p = f / d.
    - While p < 0.8 there is a jiggle of 0.1·|cos(f·π/4)| up.
    - Then s = 1 − p^27, and the item moves (0.6s, −0.5s, 0) and turns Ry(90s)·Rx(10s)·Rz(30s).
  - Largest element error over all 33 samples is 2.1e-4 (float rounding), including remaining 31 (identity) and 32 (overshoot, s = −1.3). The left hand mirrors it (side −1), with the same error.
  - `MakeEatPivot` applies it outside the hand pivot at 100 cm per unit; the fitted pivot 71.9, 56.2, −52.2 is vanilla's 0.72, 0.56, −0.52. Settled, the pivot is at 74.8, −13.8, −54.4: bottom centre, slightly left.
    - Applying it inside instead puts the item off-screen, which is how the order was chosen.
  - The pose applies at once and replaces the swing. The partial tick is the time since `Interaction.UseTicks` last changed.
  - `foundation.Hand.EatMotion` 0 keeps step 10's ease.
- **(b) Crumbs** (`FoundationBreakEffects.h/.cpp`):
  - `SpawnCrumbs` / `FoundationBreak::SpawnCrumb` spawn 5 per `Eating` and 16 at `Ate` (`eating.crumbs`).
  - They live in their own pool: an ISM using `M_BreakParticle` through a dynamic instance whose `BlockTextures` is `T_ItemTextures`. That is an existing parameter, so there is no new asset and no stand-in row.
  - Each crumb draws a random 4×4 patch of the item's 16×16 sprite.
  - The crumbs have their own random stream (seed `CrumbsSeed`), so the chips' draws don't shift.
  - The chips' spawn and update now go through `SpawnInto` / `UpdatePool`, shared by both pools.
- **Effect ticks** (`FoundationAuthority.h/.cpp`):
  - The rule is `IsEffectTick(Ticks, Duration)`: t > floor(d × 0.21875), (d − t) a multiple of 4, and t < d (`eating.crumbs`). For 32 ticks that is still 8 to 28.
  - Tick 32 gets the completion effect (`Ate`) only. It already did; the rule now states it, and the tests pin it.
- **(c) Codex review 1:**
  - #1: with `foundation.Items.Use 0` mid-meal, the next continue cancels: `Canceled`, new reason `ItemUseOff`, appended at the end of the enum and the name table. The client already ends a canceled meal quietly.
  - #2: probe 13c's "letting go mid-way eats nothing" now checks 1.2 s after the release, past the 1.0 s that was left. It checks: no use running, nothing eaten since the release, hunger 14, 3 apples.
- **(d) Eat sound:** volume 0.5 or 1 at equal odds, pitch 1 + 0.2(U − V) (SURVIVAL-FINDINGS R5, `survival.r5.rules`; play calls `survival.r5.play_calls`). The burp (0.5, 0.9-1.0) and the creative full-hunger exception are unchanged; `survival.r7.creative` confirms them.
- **(e) Adventure stripping:** `UseOn` and `StripOn` take the player mode. A plain axe in Adventure passes and the block stays, with no wear (SURVIVAL-FINDINGS R1, `survival.r1.modes`). Items with Adventure permission components are not modelled.
- **Docs:** `Docs/Items.md` (Eating, Stripping, Tests). The strip sound's volume 1 and pitch 1 now cite `survival.r1.axe`.

## 2. Decisions

- **Manager's go (2026-10-14):**
  - Q1: the report fit is the default, with the old ease at `foundation.Hand.EatMotion 0`; the capture pair goes to the manager as a look call.
  - Q2: crumbs use a MID on the existing particle material.
  - Q3: a new reason `ItemUseOff`, appended.
  - Mutation-check the effect-tick rule and the Adventure pass.
- **Mine, easy to change:**
  - The first tick's overshoot is not drawn: the pose holds at remaining ≤ d − 1. The reasoning: vanilla's client counts down before it draws, so the remaining-32 sample (s = −1.3) would be a one-tick flick. This is provisional and unverified.
  - **Provisional crumb values** (the report gives none):
    - spawn point: 0.8 blocks ahead of the eye, (U − 0.5) × 0.3 across, 0.3 to 0.9 below, turned with the view;
    - size: 3 cm, half a hit chip;
    - none drawn within 0.4 blocks of the camera (`CrumbNearCm`);
    - life: `foundation.Break.ParticleLife`'s;
    - gravity and collision: the chips'.
  - These came from the manager's look call (2026-10-14): the first try (0.6 blocks ahead, 6 cm) had a crumb fill 70 px near the top of the frame; the re-shot crumb is about 18 px.
- **Look call (manager, 2026-10-14):** EatMotion 1 is accepted as the default, with the ease at 0. On motion blur: no per-component or per-material velocity opt-out was found in the 5.8 headers, so it is left alone. No project-wide post-process change was made, since it could move world captures.
  - The report's velocity is used as given. It didn't live-count particle events.
- **Sonnet 5.5 pre-commit review** (diff only, `C:\dev\sandbox\use-lane\10b\review.diff`, 1021 lines): no correctness defects.
  - Fixed:
    - (1) the crumb pool's hidden transforms now `Init` to zero scale (`SetNum` only shrank the chips' array, so this was latent);
    - (3) a null `UFoundationItemAssets` no longer caches "no sprite" for an item.
  - (2) said the switch-0 test's later checks were vacuous. It was overstated: `Eat(10)` + 1 + 30 already exceeded 32, and the mutation run showed hunger 14. The loop is now 40 for clarity, and the mutation re-run fails all five checks.

## 3. Tests

- `Unnamed.Foundation.Items.EatPolish` (new):
  - effect ticks for 32, 20 and 16;
  - `MakeEatMatrix` against the 33 embedded samples, within 5e-4;
  - the settled pivot;
  - crumb velocity looking level and straight down;
  - switch 0 mid-meal.
- `.Strip`: Adventure passes (the log stays, no wear, no strip).
- Mutations: each one was built, run and restored. Runs are under `C:\dev\sandbox\use-lane\runs\mut-*-1659*`, `mut-*-170*`.
  - `>=` for `>`: `.EatPolish` fails (20).
  - The completion tick counted as periodic: fails 32/20/16.
  - The Adventure check dropped: `.Strip` fails, 4 checks.
  - The switch-0 cancel removed: `.EatPolish` fails, 5 checks.
- Probe 13c: 46 crumbs; the release checked past the deadline; captures `eating-on` (EatMotion 1), `eating-ease` (EatMotion 0) and `eating-off` (switch 0).

## 4. Known issues

- **Motion blur:** UE's default motion blur smears the held item under the eat jiggle (the `eating-on` capture); vanilla has no motion blur, and no per-component opt-out was found. When: a project-wide look step (the PM's call).
- **Offhand:** an offhand meal has no motion (the offhand is not drawn). When: when the offhand is drawn.
- **Dropping from the eaten stack:** dropping one item keeps the meal going. When: step 16.
- **Food table:** not in `DA_Items`. When: an `ImportItems.py` migration.
- **Unverified (vanilla capture pending Jesus's go):** the 20% slowdown, the first-tick overshoot clamp, and the crumbs' provisional spawn, size and life.

## 5. Code map

- `FoundationAuthority.h/.cpp`: `EFoundationReason::ItemUseOff`; `FoundationItemUse::IsEffectTick(Ticks, Duration)`, `EffectsDelayFraction`; `UseItemContinue` (the switch-0 cancel); `UseOn`/`StripOn(…, EFoundationPlayerMode)`.
- `FoundationInteractionView.h/.cpp`: `CVarHandEatMotion`, `FoundationHand::GetEatMotion/MakeEatMatrix/MakeEatPivot`; `UpdateHeldItem` (`EatTicks`, `EatTickAge`); `HandleEvents` (eat volume, `SpawnCrumbs`).
- `FoundationBreakEffects.h/.cpp`: `FoundationBreak::SpawnCrumb`, `CrumbsPerEffect`/`CrumbsAtCompletion`; `SpawnCrumbs`, `SpawnInto`, `UpdatePool`, `GetLiveCrumbCount`, `GetCrumbsSpawned`; the crumb pool members.
- `FoundationInteractionProbe.cpp`: 13c (`eat release wait`, `eat capture ease`).
- `Tests/FoundationEatingTests.cpp` (`EatSamples`, `.EatPolish`); `Tests/FoundationStrippingTests.cpp` (Adventure).

## 6. Provisional-values rows (T3; for the manager to copy into the last column)

- T3 10 slowdown: unchanged (`eating.movement`, `eating.use_movement`: SAME).
- T3 10 eating hand pose / motion: **swapped** to vanilla's eat transform, a closed form fitted to `eating.hand_transform`'s 33 right-hand matrices (2.1e-4); `foundation.Hand.EatMotion 1` (`eating.hand`, `eating.hand_transform`); step 10b.
- T3 10 creative exception: confirmed as is (`survival.r7.creative`).
- T3 10 eat sound volume / pitch: **swapped** to volume 0.5 or 1 at equal odds, pitch 1 + 0.2(U − V) (`survival.r5.rules`, `survival.r5.play_calls`); step 10b.
- T3 10 burp: confirmed 0.5 / 0.9-1.0 (`survival.r7.creative`).
- T3 10 eat effect cadence: the report's rule, t > floor(d × 0.21875) and (d − t) % 4 == 0, before the last tick; the same 8-28 for 32, with tick 32 the completion effect only (`eating.crumbs`, `eating.food_components`); step 10b.
- T3 10 crumbs (new row): 5 per effect and 16 at completion, with the report's velocity (`eating.crumbs`). Spawn point (0.8 blocks ahead), size (3 cm), life and the 0.4-block near cull are provisional (none in the report).
- T3 14 axe strip sound: confirmed 1.0 / 1.0, category BLOCKS (`survival.r1.axe`).
- T3 14 Adventure (new row): a plain axe passes (`survival.r1.modes`); step 10b.

## 7. Evidence and token use

- `Tests/Evidence/2026-10-14/Checks-final-20261014-171802/summary.txt`: GREEN after 24 min, with `-Flight smoke` and the expected hashes (9 of 9 equal). Native 355 of 355; voxel 207 suites; runtime-all 153 checks; interaction 108; tour 27; create-world; flight smoke.
- Captures from that run: `Tests/Evidence/2026-10-14/Playground-interaction/Runtime-20261014-172952/runtime-60-interaction-eating-on.png`, `-eating-ease.png`, `-eating-off.png`. The look call's first set is `Runtime-20261014-170155`.
- Native runs: `C:\dev\sandbox\use-lane\runs\10b-*` (10b-e is the last, Items 21 of 21) and the mutation runs `mut-tick-ge-165917`, `mut-tick-last-165946`, `mut-adventure-170014`, `mut-useoff-170043`, `mut-useoff2-170715`.
- A cancelled final run (`Checks-final-20261014-170840`): cancelled after the look call asked for crumb changes; its build and native steps had passed on the earlier code.
- Tokens: lead peak 262.0k at report A (MeasureAgentUsage, transcript 58484b26). The Sonnet 5.5 review peaked at 80.5k.
