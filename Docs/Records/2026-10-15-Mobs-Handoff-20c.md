# T6 handoff after 20b2b: 20c (hostiles) or 20e (farm extras)

Start here for T6's next step on lane `feature/survival-mobs` (20b2b landed: [the 20b2b record](2026-10-15-Mobs-20b2b.md)).
ACTION-PLAN line 2: **20c** zombie, spider, night spawning, mob damage and player knockback **waits on T4 11**; **20e**
shearing, milk, breeding, variants runs first if T4 11 has not landed. The SM names which one.

## 1. Reading list (in order)

1. [The 20b2b record](2026-10-15-Mobs-20b2b.md), whole (about 80 lines); [the 20b2a record](2026-10-15-Mobs-20b2a.md)
   sections 4-5.
2. `C:\dev\tools\shift-managers\survival\ACTION-PLAN.md` line 2 (lines 26-31) and section 3 (asset locks).
3. `C:\dev\sandbox\scout-mobs\facts.md` section 4 "Spawning" (lines 117-139: hostiles' light, despawn) for 20c; for
   20e grep it for `shear`, `breed`, `milk`.
4. `C:\dev\tools\shift-managers\survival\codex\research\02-t6-mobs-oracles.md` sections A (line 31: the sky-light rule),
   B (lines 44-47), C (lines 52-61).
5. Grep (never read whole) `C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md` for `survival.r10.spawn_caps`
   (5461-5470: monster cap 70, despawn 128/32), `survival.r10.ai_defaults`, `survival.r10.ai_rules`; and
   `SURVIVAL-RUN2-FINDINGS-26.3.md` R20 (lines 371-391; night runs: monsters every tick) and Appendix F
   `survival2.r20.population.plains_night`.
6. Code: `FoundationAuthority.cpp` `RunCreatureCycle`, `SpawnCycleColumn`, `FindPackCell` (grep); `FoundationMobs.h`
   the cycle constants (grep `CreatureCycleTicks`).

## 2. Working agreement

As 20b2b's: the SM confirms the plan (files against the hot files `FoundationAuthority.h/.cpp`,
`FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`,
`Docs/Records/README.md`, diffed against the latest `feature/survival`); values from VALUES, FINDINGS, RUN2-FINDINGS
(cite ids), game data, the wiki or black-box runs, never decompiled code; provisional rows in
`C:\dev\tools\shift-managers\survival\provisional-values.md`; `RunChecks -Profile final` before a Sonnet review; never
push. Measure the lead with `C:\dev\sandbox\mobs-20b2\MeasureAgentUsage.mjs --session <id>` (the newest jsonl in
`C:\Users\jherediaramirez\.claude\projects\C--dev` holding your first prompt). The system prompt and images make the
lead's peak climb fast (20b2b: 99k at the plan, 240k after nine captures): view shots sparingly.

## 3. Settled (do not reopen)

- Everything in the 20a, 20b1, 20b2a and 20b2b records' Decisions.
- The creature cycle as built (phase from the authority's steps; cap floor(10 x eligible / 289); local cap 10 per
  player; 24/128 blocks; 3 rounds, ceil(U x 4); cluster 4).
- Light: the proxy (sky-exposed above the motion-blocking top = 15); a real light engine is later.

## 4. What the next step does (proposal; confirm with the SM)

- **20c**: a monster category beside `IsCreature` (zombie, spider first); the cycle every tick for monsters with cap 70
  [S 5461-5470] reusing `RunCreatureCycle`'s shape (generalise it by category rather than copying); the hostile light
  rule (block light 0, sky light vs a random 0-7, Codex 02 A line 31) needs a night sky proxy from the world clock;
  despawn (beyond 128 at once; beyond 32 after 600 idle ticks, 1/800 a tick) for non-persistent mobs; mob melee on the
  player through `Vitals.Hurt` with the knockback event; T4 11's death screen.
- **20e**: shearing (`edible_for_sheep` regrowth is done), milk with T3's bucket, breeding (tempt foods exist), wool
  colours and the baby models (R17/R20 `baby_sizes`).

## 5. Code map (20b2b)

See the 20b2b record section 5. `-VoxelShotMobs` / `CaptureVoxel.ps1 -Mobs [-MobsSteps n]` is the generated-world
evidence tool (build.txt's `Mobs:` line counts live mobs and the last cycle's columns, creatures and cap).

## 6. Known issues, with when

- See the 20b2b record section 4.
- The mobs shot's follow camera can end in a tree's canopy when the mob strolls under it (open air is tested only at the search).
- No natural-world shot of cycle-spawned animals yet (the chunk-generation packs fill the cap where there is grass).
- `feature/survival` moves: merge only at the SM's merge go.
- Doors, trapdoors and gates are usable since T5 B3a (8c076207): A* should treat them per R10's path types (a closed
  oak door DOOR_WOOD_CLOSED, a closed fence gate like FENCE); `GetPathCell` does not yet. 20c (mobs that open doors
  and zombies breaking them need it anyway).
- Notes for 20c: death.attack.mob's attacker name is a T4 11a TODO; 11a's `Vitals.Hurt` is the mob-attack entry; the
  respawn API `SetRespawnPoint` / `GetRespawnPoint` is session only.

## 7. What worked, what failed (20b2b)

- Worked: `CaptureVoxel -Mobs` for runtime evidence (a count and a shot in one run); public `SpawnCycleColumn` let the
  tests check the cluster and distance rules directly; the mutation runner (`C:\dev\sandbox\mobs-20b2b\mutations`).
- Failed first: `[IO.File]::WriteAllLines` wrote CRLF into an LF file (the diff showed 4k lines: `sed -i 's/\r$//'`);
  a capture chained after a failed build ran the old DLL (stop on `$LASTEXITCODE` first); a mass rename of a local
  also changed a log string; the shot loop returns early once every shot is taken, so a waiting shot must be searched
  where the list is built (`FindMobShot`) and the tail split out (`FinishShots`); a moving shot camera blurs.

## 8. Tokens (20b2b)

Lead 284.4k peak at report A (the system prompt and nine shot images: 99k at the plan, 240k after the captures). Sonnet reviewer 87.9k.

## 9. Evidence

- RunChecks final: `Tests/Evidence/2026-10-15/Checks-final-20261015-112406/summary.txt`.
- Captures: `Tests/Evidence/2026-10-15/VoxelShots-mobs-devseed-102210` (mobs.png), `...-mobs-cycles-devseed-103728`,
  `...-mobs-cycles-meadow-103904`, `...-mobs-meadow-104332`.
- Mutations `C:\dev\sandbox\mobs-20b2b\mutations\results.txt`; review `C:\dev\sandbox\mobs-20b2b\review\findings.md`.

## 10. Open questions (for the SM / Jesus)

- Push: track branches stay local (Jesus decides).

## Short prompt for the next chat

> Survival T6 after 20b2b (20c hostiles, or 20e if T4 11 has not landed; the SM names it), lane
> `C:\dev\projects\Unnamed-mobs`, branch `feature/survival-mobs`. Read `Docs/Records/2026-10-15-Mobs-Handoff-20c.md`
> first and follow its reading list; your first report is the plan (files against the hot files, hash impact) to the
> SM-Mobs manager; wait for the go.
