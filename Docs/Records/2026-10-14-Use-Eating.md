# Use step 10: eating (record and handoff)

Lane `feature/survival-use` (worktree `Unnamed-use`), from `9822d24`. Survival program track T3, scout AC's plan step 10 (`C:\dev\sandbox\scout-player\plan.md`, lines 203-217; facts sections "Using items" and 5). Next on this lane: step 14 (axe stripping), then 17 (buckets) and 16 (survival fixes).

## 1. What was built

- **Behaviour** (`Docs/Items.md`, "Eating"): hold use with an apple, bread or cooked beef (or the reference's melon slice and berries) and it is eaten over 32 reference steps (1.6 s). Hunger and saturation fill (saturation capped at the new hunger). The player slows to 20% and can't start a sprint. Full hunger refuses. Letting go, a slot change or a swap cancels, and nothing is consumed. One item is consumed at the end. Switch: `foundation.Items.Use`, default 1; 0 lets food pass, as before.
- **Authority** (`FoundationAuthority.h/.cpp`):
  - Three commands appended to the enum: `UseItemStart`, `UseItemContinue`, `UseItemRelease`.
  - Reasons `NotHungry` and `NotUsing`; events `Eating` and `Ate`.
  - A per-player `FFoundationItemUse`.
  - `Step()` drops a use that got no continue last step.
  - `Options.bItemUse`, which the pawn copies from the switch each step (tests set it themselves).
- **Data** (`FoundationItems.h/.cpp`):
  - `FFoundationConsumable` and `FFoundationItemDefinition::Consumable` / `GetConsumable()`.
  - `FoundationConsumables::FindReference`: a native table of items.json's six foods, which the reference test pins row by row.
- **Client** (`FoundationInteraction.h/.cpp`):
  - `UsePipeline` per hand: the target's use (now `UseTarget`), then `UseItemStart` if the hand holds a consumable, in the air too.
  - `StepItemUse` sends a continue each step and a release when let go.
  - A use the authority already ended (slot change, swap) ends quietly.
  - Full hunger asks again only after the held repeat.
  - Attack is skipped while eating.
- **Movement:**
  - `FoundationPawn.cpp`: `bItemSlows` comes from the authority's use.
  - `FoundationSimulation.cpp`: the input is scaled by 0.2 while it is set.
- **Presentation:**
  - `FoundationInteractionView.h/.cpp`: eat sounds on `Eating` and `Ate`, then the burp, through `PlayEvent`. The held item eases into an eating pose (`GetEatBlend()` for probes).
  - `FoundationController.cpp`: the event log's name table gained the two kinds (the probe crashed on the old table; its `static_assert` now pins `Ate`), and it logs "ate apple".

## 2. Decisions

- **Manager's go (2026-10-14):**
  - Q1: no `DA_Items` rewrite (binary conflicts with T2/T5). No native items.json reader exists outside the tests, so the data is a native table pinned by a test. Moving it into `ImportItems.py` is a known issue.
  - Q2: separate `UseItem*` commands, appended at the end.
  - Files outside the plan, both reported to the manager: `FoundationInteractionProbe.cpp` (an additive scenario, approved) and `FoundationController.cpp` (a crash fix).
- **Vanilla-first calls:**
  - 32 ticks, the foods' values and the full-hunger refusal come from the jar data and the wiki.
  - Provisional, until a vanilla capture (needs Jesus's go):
    - the 20% slowdown (facts: unverified);
    - the creative exception (invulnerable modes may eat at full hunger);
    - the eat sound's volume 0.5-1.0 and pitch 0.8-1.2, and the burp's 0.5 and 0.9-1.0 (unverified, to check against the wiki's Sounds.json table);
    - the effect cadence (every 4th tick from the 8th: the project's own);
    - the eating motion (the project's own).
- **Sonnet 5.5 pre-commit review** (diff only, `C:\dev\sandbox\use-lane\review.diff`): no blockers.
  - Fixed:
    - (1) Holding use at full hunger under a commit-cadence preset sent a refused start every step; it now waits the held repeat.
    - (2) A slot change while eating made the client send a continue that was refused with "no item in use" (log noise); it now ends quietly. A test covers this.
    - (3) A same-step continue refusal no longer ends the client's meal.
  - Checked, no change: (5) `CheckHand` already rejects a dead player.
  - Left as a known issue: (4) dropping one item from the stack being eaten keeps the meal going (the instance stays).

## 3. Tests

- **Native** (`Tests/FoundationEatingTests.cpp`, new):
  - `Unnamed.Foundation.Items.EatReference` and `.Eat` (`Docs/Items.md`, Tests). Items filter: 13 of 13.
  - Runs: `C:\dev\sandbox\use-lane\runs\items2-122803`; after the review, `items-review-124808`.
- **Mutations** (`C:\dev\sandbox\use-lane\mutate.ps1`: built, run, restored byte for byte):
  - 32 → 31 fails `.Eat` and `.EatReference` (`runs\mut-ticks-123051`).
  - The saturation cap removed fails `.Eat` (`mut-cap-123200`).
  - A release that doesn't cancel fails `.Eat` (`mut-release-123356`).
  - RunChecks final then rebuilt with `-DisableAdaptiveUnity`.
- **Interaction suite**, scenario 13c: hold to eat (hunger 10 → 14, one apple fewer, 7 eat sounds and 1 burp), release mid-way (nothing eaten), and the capture pair `eating-on` / `eating-off` (5 new checks). First pass: `Tests/Evidence/2026-10-14/Playground-interaction/Runtime-20261014-124114`, where all five eating checks passed. "mining the wall pops the wall torch" failed there at 39 fps under other lanes' compiles; it had passed in the run before.
- **Changed counts and their sources:**
  - Native tests: +2 (`Items.Eat`, `Items.EatReference`).
  - Interaction and runtime-all checks: +5 (13c's records).
  - No golden, world hash or hash-checked reference moved; nothing pinned changed.

## 4. Known issues

- No eat particles. The break chips draw block textures only, so item-sprite crumbs need a material path in `FFoundationBreakEffects` (not owned this stretch). When: with a crumb pass, or step 16.
- The food data is a native table, not a `DA_Items` field. When: a later `ImportItems.py` migration (`UNNAMED_ITEMS_MIGRATE=food`) once no other lane touches item assets.
- An offhand meal has no motion (the offhand isn't drawn). Dropping one item from the eaten stack doesn't cancel the meal; vanilla's behaviour is unverified.
- The press still starts the cosmetic swing for a frame before the eating pose takes over (the swing is a prediction on every press).
- The provisional values listed in section 2 wait on a vanilla capture.

## 5. Next: step 14, axe stripping

- Hang it off `foundation.Items.Use`. Stripping is an instant use on a block, so it goes through `UseOn`'s place-capability branch (`FoundationAuthority.cpp`, `UseOn`, step 2: a non-placeable item passes today).
- Add a strip capability (log → stripped log), from the block reference: the stripped logs are imported.
- `item.axe.strip` through `PlayEvent`.
- Axe wear: 1 per strip (wiki).

## 6. Code map

- `FoundationAuthority.h`: `EFoundationCommandType::UseItem*`, `FFoundationResult::UseTicks/UseDuration/bAte`, `FFoundationLabOptions::bItemUse`, `EFoundationEventKind::Eating/Ate`, `FFoundationItemUse`, `FoundationItemUse::IsOn/IsEffectTick`.
- `FoundationAuthority.cpp`: the switch `CVarItemUse`; `Step()` (the missed-step cancel); `Execute` (select/swap cancel, the release); `UseItemStart`, `UseItemContinue` and `CancelUse` (before `UseEntity`).
- `FoundationItems.h/.cpp`: `FFoundationConsumable`, `FoundationConsumables`, `GetConsumable`.
- `FoundationInteraction.cpp`: `StepUse` → `StepItemUse` / `ReleaseItemUse`; `UsePipeline` → `UseTarget`; `Attempt` (NotHungry wait).
- `FoundationInteractionView.cpp`: `HandleEvents` (sounds), `UpdateHeldItem` (`EatBlend`, `EatAge`).
- `FoundationInteractionProbe.cpp`: scenario 13c.

## 7. Evidence and token use

- Capture pair: `Tests/Evidence/2026-10-14/Playground-interaction/Runtime-20261014-124114/runtime-60-interaction-eating-on.png` and `-eating-off.png`.
- RunChecks final, green after the review fixes: `Tests/Evidence/2026-10-14/Checks-final-20261014-124859/summary.txt`. Native 332 of 332; 207 voxel suites; 9 of 9 hashes; runtime-all 139 checks and interaction 94 checks, 0 failures (about 43 fps, other lanes building); tour, create-world, flight smoke, npm, source-check and the verifiers all pass.
- Lead context: 270k at the first measure, 306k before report A (MeasureAgentUsage, session 74e4a6c9). The Sonnet review subagent peaked at 113k (over the 100k brief).
