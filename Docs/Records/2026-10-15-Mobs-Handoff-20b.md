# Survival 20b handoff: pig, sheep, chicken, AI and passive spawning

Start here for T6's next step on lane `feature/survival-mobs` (20a landed: [the 20a record](2026-10-15-Mobs-20a.md)).
Size M. Track lettering: the scout's 20b (`C:\dev\sandbox\scout-mobs\facts.md` section 9, "20b").

## 1. Reading list (in order)

1. [The 20a record](2026-10-15-Mobs-20a.md), whole (about 100 lines).
2. `C:\dev\sandbox\scout-mobs\facts.md`: section 9's 20b scope and section 5's oracles O1-O2 (pig, sheep, chicken), O7
   (pig, sheep, chicken loot), O9-O11 (spawn weights, packs, chunk-gen chances); sections 4 (spawning) and 6 (A*).
3. `C:\dev\tools\shift-managers\survival\codex\research\02-t6-mobs-oracles.md` (61 lines): its section A and B values win
   over the scout's (tempt modifiers, panic speeds pig/sheep 1.25 and chicken 1.4, chicken fall ×0.6, sheep grazing).
4. `C:\dev\tools\shift-managers\survival\value-swaps.md` lines 137-166 and 263-279 (T6 rows); grep
   `C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md` for `survival.r10.ai_defaults` (lines 5349-5432, per-mob
   goals), `survival.r10.idle_and_sun` (5457), `survival.r10.spawning_rules` (5472), `survival.r10.spawn_registry`.
5. `Source/Unnamed/Foundation/FoundationMobs.h` (whole, about 190 lines: every constant with its source) and
   `FoundationMobs.cpp` `Think`, `Steer`, `Move` (about 150 lines).
6. `FoundationAuthority.cpp` `StepMobs` / `DropMobLoot` (grep `StepMobs`), and `Attack`'s mob branch (grep `FindMob`).
7. `FoundationMobView.cpp` `GetPartAnimation`, `MakeRootTransform` (grep): the leg names are the quadruped's; the
   chicken's are `right_leg` / `left_leg` and its wings.
8. `C:\dev\sandbox\mobs-20a\speed_probe\summary.md` (the cow's black-box walk numbers) and
   `C:\dev\tools\shift-managers\survival\provisional-values.md` (the T6 20a rows).

## 2. Working agreement

As 20a's: the manager (SM-Mobs) confirms the plan; first report the plan against the hot files
(`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`,
`Docs/Items.md`, `Docs/Records/README.md`); values from the value report, SURVIVAL-FINDINGS, game data, the wiki or
black-box runs (never decompiled code); provisional values get rows; `RunChecks -Profile final` before the review; a
Sonnet review split by path lists above about 1,500 diff lines; never push.

## 3. Settled (do not reopen)

- Rigid vanilla box models from `facts/entity_models.json`, drawn per (mob, part); no skeletal rig.
- Movement as measured: ground acceleration (attribute × modifier)² blocks per tick; retention 0.546 ground / 0.91 air
  after the move; vy = (vy − 0.08) × 0.98; knockback per `ApplyKnockback`. Re-measure a pig or chicken only if their
  numbers disagree with this rule (the speed probe is reusable: `C:\dev\sandbox\mobs-20a\speed_probe\speed_probe.py`).
- Entity loot through `DA_Mobs` (`ImportMobs.py`), not `DA_Items`. The 18 drops are already in `DA_Items` (no lock
  needed for 20b-20d drops; arrow is T2 19c's). Wool items are block items (blocks reference).
- The entities reference already holds every texture, model and loot table 20b needs: no lock needed.
- Player damage to mobs uses `Attack`'s damage line; sweeps and the attack sounds are 20c.

## 4. What 20b does (proposal; confirm with the SM)

- Pig, sheep, chicken spawnable (`bSpawnable`), their goal parameters (panic 1.25 / 1.25 / 1.4, tempt with their food
  tags at 1.2 / 1.1 / 1.0 (cow 1.25), stop at 2.5 blocks), chicken slow fall (vy × 0.6) and no fall damage, sheep grazing
  (`nextInt(1000)` adult) without shearing (20e).
- Leg names per model (chicken biped legs and wings) in `GetPartAnimation`; the sheep's wool layer drawn over its body.
- A* over cells (scout section 6: 256 nodes, step 1 up, 3 down, no corner cutting), used by stroll, panic and tempt
  when the straight line is blocked.
- Passive spawning: chunk-generation packs (`creature_world_gen_spawn_probability`, default 0.1) and the 400-tick cycle
  with the creature cap 10 × chunks / 289, on grass with light ≥ 9 (light proxy: scout Q1).
- Look gaps from 20a's look call: the overlay strength at close range against `Cow_Death.png`, head-look turns, the
  walk cycle's speed calibration.

## 5. Code map

See [the 20a record](2026-10-15-Mobs-20a.md) section 5. Tests: `Source/Unnamed/Tests/FoundationMobTests.cpp`
(`MobTestsLocal::FMobLab`: a floor, one player at (-200, 0) facing +X, mobs on; `MoveAlone` steps a mob without AI).

## 6. Known issues, with when

- Zombie and creeper tables left out of `DA_Mobs` (`MobImportData.py` raises `Unsupported`: a weighted choice between
  entries, the `vehicle` predicate, a `tag` entry): 20c / 20d extend `MobImportData.Table` and `FoundationLoot`.
- Skeleton's table skipped at runtime until `arrow` exists (T2 19c).
- No A*: mobs walk into walls and give up after 40 ticks (`GiveUpTicks`): 20b.
- Effects of raw chicken, rotten flesh, spider eye: when status effects exist.
- Player attack sounds: 20c.
- `feature/survival` moved on after 20a (632a7466 T5 B2a block shapes; e2c3dc52 T3 16a lava/burning `foundation.Fire`, swim pose): the merge overlaps the Authority, InteractionView and Pawn hunks. 20c can reuse T3 16a's fire damage path for the zombie's and skeleton's daylight burn, and `is_on_fire` for the cooked drops (`FFoundationLootContext::bOnFire`).

## 7. What worked, what failed (20a)

- Worked: the scout's probes (models, attributes) moved into the reference's tools as they were; a black-box speed
  probe answered the walking rule in one run; the runtime probe's flush cases (a shot lands one frame later, so an idle
  case follows each shot); `MobImportData.py` as a dry run before Unreal.
- Failed first: anonymous-namespace names clashed in the `-DisableAdaptiveUnity` build (`Kinds` vs
  `FoundationTargeting.cpp`): prefix file-local names with `Mob`. `unreal.Vector2f` cannot be built from Python: use
  `FVector`/`FVector2D` in asset structs. The 86-item pins in seven tests moved with the items append. A new `EFoundationEventKind` needs its name in `AFoundationController::NoteInteraction`'s table (its static_assert named the old last kind, so `MobSound` read past the end and crashed a capture run): bump the assert to the new last kind.

## 8. Tokens (20a)

Lead peak: see report B. Subagents (Sonnet): entities reference 82k, items reference 84k, speed probe 76k, two reviewers.

## 9. Evidence

- 20a capture: `Tests/Evidence/2026-10-15/Playground-mobs/Runtime-20261015-021424`.
- Speed probe: `C:\dev\sandbox\mobs-20a\speed_probe\summary.md`.
- Mutations: `C:\dev\sandbox\mobs-20a\mutations\results.txt`.

## 10. Open questions (for the SM / Jesus)

- Push: track branches stay local; the lane is not pushed (Jesus decides).
- Light for spawning (scout Q1): a proxy (sky exposure + emitters) or a CPU light step first. Recommendation: the proxy
  in 20b, swap later.

## Short prompt for the next chat

> Survival 20b (T6: pig, sheep, chicken, AI and passive spawning), lane `C:\dev\projects\Unnamed-mobs`, branch
> `feature/survival-mobs`. Read `Docs/Records/2026-10-15-Mobs-Handoff-20b.md` first and follow its reading list; your
> first report is the plan (files against the hot files, hash impact) to the SM-Mobs manager; wait for the go.
