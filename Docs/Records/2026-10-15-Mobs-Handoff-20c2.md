# T6 handoff after 20c1: 20c2 (spider, the monster cycle, the light proxy, despawn)

Start here for T6's next step on lane `feature/survival-mobs` (20c1 landed at 168c0b31: [the 20c1 record](2026-10-15-Mobs-20c1.md)).
**Feature freeze** (Jesus via PM, 2026-10-15): nothing beyond this scope; bug fixes go to the QC pass, not here.
ACTION-PLAN line 2, 20c's second half. After it: 20e (shearing, milk, breeding, variants), then 20d (waits on T2 19c).

## 1. Reading list (in order)

1. [The 20c1 record](2026-10-15-Mobs-20c1.md), whole (about 90 lines); [the 20b2b record](2026-10-15-Mobs-20b2b.md)
   sections 1 and 5 (the creature cycle you generalise).
2. `C:\dev\tools\shift-managers\survival\ACTION-PLAN.md` line 2 (lines 26-31) and section 3 (locks).
3. `C:\dev\tools\shift-managers\survival\value-swaps.md` lines 137-166 (spider 16 / 0.3 / attack 2, 1.4 x 0.9 eye 0.65;
   despawn 128/32 + 1/800 after 600; spawn light: block 0, random 0-7, sky vs random 0-31) and 263-279.
4. `C:\dev\tools\shift-managers\survival\codex\research\02-t6-mobs-oracles.md` A (line 31), B (lines 44-47: spider
   leap 0.4, spider stroll 0.8, MONSTER cap 70), C (spider light 11 and climbing: no reference, provisional).
5. Grep (never read whole) `C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md` for `spider |` near 5420-5432 (the
   spider's goals: SpiderAttackGoal follows an unseen target, unlike the zombie's), `survival.r10.spawn_caps`
   (5461-5470), `survival.r10.spawning_rules` (5472), `survival.r10.spawn_registry` (monster entries per biome);
   `SURVIVAL-RUN2-FINDINGS-26.3.md` R20 lines 371-391 (night runs: monsters every tick; final zombie 26 / spider 14
   of a cap of 70 shared with creepers and skeletons) and `survival2.r20.ai_goals` for the spider's rows.
6. Code: `FoundationMobs.cpp` `ThinkHostile`, `FoundationAuthority.cpp` `StepMonster`, `StepSunBurn`, `IsSkyVisible`,
   `RunCreatureCycle`, `SpawnCycleColumn`, `FindPackCell` (grep).

## 2. Working agreement

As 20c1's: plan first to SM-Mobs (files against the hot files `FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`,
`FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`, diffed
against the latest `feature/survival`); values cite ids; provisional rows in
`C:\dev\tools\shift-managers\survival\provisional-values.md`; mutation runner (copy
`C:\dev\sandbox\mobs-20c1\mutations\run.ps1`); Sonnet review on the diff; `RunChecks -Profile final`; never push.
Measure the lead with `node C:\dev\sandbox\mobs-20b2\MeasureAgentUsage.mjs --session <id>` (the newest jsonl in
`C:\Users\jherediaramirez\.claude\projects\C--dev` holding a line you wrote). Launch `UnrealEditor-Cmd` with
`Start-Process` and absolute `-script=` paths (a relative one resolves under the engine's Binaries).

## 3. Settled (do not reopen)

- Everything in the 20a-20c1 records' Decisions.
- The hostile spawn light is a provisional proxy behind its switch: sky-exposed above the motion-blocking top = the
  open-sky light of the day's `sky_light_level` (already wired: `GetSkyFacts`, `VoxelSky::OpenSkyLight`); block light =
  the max over emitters within 15 of level minus Manhattan distance, no occlusion; logged in provisional-values.md. No
  CPU light engine.
- The cycle as built (generalise `RunCreatureCycle` by category rather than copy it).

## 4. What 20c2 does (proposal; confirm with the SM)

- Spider: kind already has 16 / 0.3 / attack 2 / 1.4 x 0.9, death roll 180, follow 16 (provisional); make it
  spawnable; its 8 legs' animation (`survival.r10.model_motion` spider yaw/roll, rest +-45); the leap at a target
  (vertical 0.4); stroll 0.8; follows an unseen target (SpiderAttackGoal); targets only in the dark (light 11 claim is
  provisional); climbing walls (provisional). Its loot table is already in DA_Mobs.
- The monster cycle every step, cap 70 x eligible / 289 [S 5461-5470; R20], with Codex review 30 #1 (the cap counts
  every loaded mob of the category, globally) and #2 (frozen mobs are skipped) folded in (Codex 30 #3-#5 and the step
  cadence are on the QC list, not 20c2), the biome's monster entries (DA_Mobs
  must import the monster category: today only creatures), placement anywhere a monster fits (not only grass tops),
  the 0-7 / 0-31 / block 0 light rule [mobs.spawn_light].
- Despawn of monsters: beyond 128 at once; beyond 32 after 600 idle steps, 1/800 a step [mobs.despawn].
- Peaceful removes monsters and spawns none (the authority does not know the difficulty yet: pass it like the mode).
- Natural monsters change generated-world captures: check before the plan that no golden or hash covers mobs (20c1
  changed none).

## 5. Code map (20c1)

See the 20c1 record section 5.

## 6. Known issues, with when

- The 20c1 record section 4.
- `feature/survival` moved to d6c26416 (T2 19a2 armor): merge at the SM's merge go. Expect a conflict in
  `FoundationItemAssets.*` (both sides added fields: keep both); DA_Items, `Content/Items` and the items reference: take
  theirs, re-run ImportItems and ImportMobs verifies on top.
- Doors and gates as path types (R10) are still not in `GetPathCell`.
- Known, on QC list: A* finds no path on the Playground's floor (its collision query reports tall boxes in open
  cells; the 20c1 record section 4). Probes summon chasers in reach.
- Known, on QC list: native attempt 1 of the final run crashed in `FVoxelDensityScratch`'s destructor during
  `FVoxelFreezeMovedTest` (`Tests/Evidence/2026-10-15/Native-20261015-145709/Crashes`); attempt 2 clean.

## 7. What worked, what failed (20c1)

- Worked: the hit queue (authority) + the pawn applying it kept the hot-file hunks small; pure `ThinkHostile` made the
  targeting test deterministic; the mutation runner caught 10 of 10.
- Failed first: `UnrealEditor-Cmd` called with `&` and a relative `-script=` ran nothing (exit 1, no log); re-importing
  DA_Mobs re-saves every mob texture and M_Mob (revert those, keep DA_Mobs); a parameter named `Players` shadowed the
  member (C4458).

## 8. Tokens (20c1)

Lead 111.8k at the plan, 295k at the wrap-up start, 360k after the Playground probe hunt (four probe rounds). Sonnet reviewer 106.5k peak.

## 9. Evidence

- RunChecks final: `Tests/Evidence/2026-10-15/Checks-final-20261015-145708/summary.txt`; probe `-Suite mobs`
  `Tests/Evidence/2026-10-15/Playground-mobs/Runtime-20261015-145554`.
- Mutations `C:\dev\sandbox\mobs-20c1\mutations\results.txt`; review `C:\dev\sandbox\mobs-20c1\review\findings.md`;
  focused native run `C:\dev\sandbox\mobs-20c1\native-1`.

## 10. Open questions (for the SM / Jesus)

- Push: track branches stay local (Jesus decides).
- The zombie arm pose (straight ahead): accepted by SM-Mobs (provisional).

## Short prompt for the next chat

> Survival T6 20c2 (spider, monster cycle, light proxy, despawn), lane `C:\dev\projects\Unnamed-mobs`, branch
> `feature/survival-mobs`. Read `Docs/Records/2026-10-15-Mobs-Handoff-20c2.md` first and follow its reading list; your
> first report is the plan (files against the hot files, hash impact) to the SM-Mobs manager; wait for the go.
