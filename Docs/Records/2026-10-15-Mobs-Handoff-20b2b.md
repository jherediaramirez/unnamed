# Survival 20b2b handoff: the passive spawn cycle and caps

Start here for T6's next step on lane `feature/survival-mobs` (20b2a landed: [the 20b2a record](2026-10-15-Mobs-20b2a.md)).
Size S-M. 20b2 was split by the SM (2026-10-15): 20b2a built the biome table, chunk-generation packs, mob hazards and
the Codex 19 folds; 20b2b is the 400-tick creature cycle with its caps.

## 1. Reading list (in order)

1. [The 20b2a record](2026-10-15-Mobs-20b2a.md), whole (about 90 lines); [the 20b1 record](2026-10-15-Mobs-20b1.md)
   sections 2 and 5.
2. `C:\dev\sandbox\scout-mobs\facts.md` section 4 "Spawning" (lines 117-139) and section 6 "Spawning runs in"
   (lines 198-200).
3. `C:\dev\tools\shift-managers\survival\codex\research\02-t6-mobs-oracles.md` section A row "globalCap" (line 30),
   section B row "Spawning" (line 48), section C (lines 52-61).
4. Grep `C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md` (never whole) for `survival.r10.spawn_caps`
   (5461-5470) and `survival.r10.spawning_rules` (5472).
5. Grep `C:\dev\projects\value-holder\SURVIVAL-RUN2-FINDINGS-26.3.md` (59k lines, never whole): its R20 section
   "Four population runs" (lines 371-391: creature consideration on gameTime multiples of 400, 289 spawnable chunks,
   the global caps, packs can overshoot) and Appendix F (`survival2.r20.population.plains_day`, line 51481 onward;
   read the first 40 lines to learn the rows: cycle tick ranges and counts). Flat grass only: other terrain stays
   provisional.
6. Code: `FoundationAuthority.cpp` `StepMobSpawns`, `SpawnPack`, `FindPackCell` (grep); `FoundationMobs.h` the
   spawning constants block (grep `SpawnColumnCells`).

## 2. Working agreement

As 20b2a's: the SM confirms the plan (files against the hot files `FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`,
`FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`, diffed against
the latest `feature/survival`); values from VALUES, FINDINGS, RUN2-FINDINGS (cite ids), game data, the wiki or
black-box runs, never decompiled code; provisional rows in `C:\dev\tools\shift-managers\survival\provisional-values.md`;
`RunChecks -Profile final` before a Sonnet review; never push. Measure the lead with the copy
`C:\dev\sandbox\mobs-20b2\MeasureAgentUsage.mjs --session <id>` (the session id: the newest jsonl in
`C:\Users\jherediaramirez\.claude\projects\C--dev` holding your first prompt).

## 3. Settled (do not reopen)

- Everything in the 20a, 20b1 and 20b2a records' Decisions.
- Light: the proxy (sky-exposed above the motion-blocking top = 15, leaves block); a real light engine is later.
- Farm animals are persistent (never despawn); the creature cap counts them.

## 4. What 20b2b does (proposal; confirm with the SM)

0. **First: runtime spawn evidence** (SM, 2026-10-15). 20b2a tested packs on the authority only; `AVoxelWorld`'s
   `GetBiomeId` hook is untested at runtime. Add a spawned-mob count to `FFoundationMobView::Describe` (or a log line;
   `MobColumnsSeen`, `MobPacks`, `MobsPackSpawned` exist) and take a generated-world shot with animals in frame
   (`CaptureVoxel.ps1 -SpawnAt` a plains spot, or create-world). If the hook is broken, fix it before the cycle work.
0b. **Codex review 23** (`C:\dev\tools\shift-managers\survival\codex\reviews\23-T6-20b1\report.md`; PM 4 wants #2
   first, players see it): #2 grazing uses the `edible_for_sheep` tag (short_grass, fern, short_dry_grass,
   tall_dry_grass; `Reference/minecraft-blocks-26.3/research/block_tags.csv`): break that plant without drops before
   falling back to grass_block -> dirt; tests fern-on-grass and dry-grass-on-sand. #1 `FoundationMobPath.cpp:160-195`
   keeps scanning down when the first standable height is rejected for rise (headroom from the lower feet); test a
   bottom slab over a one-high corridor.
1. **The cycle**: on game ticks that are multiples of 400 [survival2.r20 population runs; spawning_rules], for creatures:
   eligible chunks = the columns within 8 of a live player (17 x 17 = 289 for one player), cap
   `floor(10 x eligible / 289)` [Codex 02 A]; when the creatures within the eligible chunks are under the cap, each
   eligible chunk tries up to 3 group rounds, `ceil(U x 4)` attempts per round [S 5472, Codex 02 B], a position
   spread +-5 (triangular, as `RollPackOffset`) from a random column, at least 24 blocks from every player and within
   128, on grass with the light proxy >= 9 and room (reuse `FindPackCell`), the species by weight from that position's
   biome, the cluster at most 4 [survival.r10.ai_defaults].
2. Pack completion may overshoot the cap (R20); a local per-player cap check (Codex 02 A) when more than one player.
3. **Tests**: the cap formula (289 chunks -> 10; fewer chunks scale down), the 24-block minimum, the 400-tick period,
   determinism, the switch (`foundation.Mobs.Spawn 0`), the cluster limit; a long-run count in a flat plains lab
   compared with R20's plains_day creature arrivals (sheep 7, chicken 1 over 12000 ticks) as a sanity band, not a gate.
4. A runtime shot of a generated world after a few cycles.

## 5. Code map (20b2a)

- `FoundationMobs.h/.cpp`: `FFoundationMobSpawn`, `FFoundationBiomeSpawns`, `PickSpawn`, `RollPackSize`,
  `RollPackOffset`, `SpawnColumnCells`, `PackSpreadCells`, `PassiveMinLight`, `SkyLightProxy`, `SpawnRingColumns`.
- `FoundationAuthority.cpp`: `StepMobSpawns` (the seen-set ring), `SpawnPack` (one roll per column, deterministic from
  `MobSpawnSeed` and the column), `FindPackCell` (top motion-blocking block = grass, room, no fluid), `StepMobHazards`,
  `IsMobInFluid`; `GetBiomeId` / `MobSpawnSeed` are set by `AVoxelWorld` (`VoxelWorld.cpp` `SetupAuthority`,
  `Regenerate`).
- Tests: `Unnamed.Foundation.Mobs.Packs` builds a 64 x 64 grass lab (`MakeGrassLab`) with plains everywhere.

## 6. Known issues, with when

- See the 20b2a record section 4.
- `feature/survival` moves: merge only at the SM's merge go (4ee7f171 was pending at the 20b2a commit; check
  `git log` for the merge).
- Baby models have their own geometry (R17; R20 `baby_sizes`): 20e.
- Notes for 20c: death.attack.mob's attacker name is a T4 11a TODO; 11a's `Vitals.Hurt` is the mob-attack entry; the
  respawn API `SetRespawnPoint` / `GetRespawnPoint` is session only.

## 7. What worked, what failed (20b2a)

- Worked: Python patch scripts with exact-match asserts for every edit; the mutation runner copied from 20b1
  (`C:\dev\sandbox\mobs-20b2\mutations\run.ps1`, 8 of 8 caught, unattended); recording each mob at spawn (Age 0)
  in the Packs test so strolling cannot blur per-column counts.
- Failed first: `Path.write_text` on Windows wrote CRLF into LF files (the diff showed 10k lines): write bytes, or
  `sed -i 's/\r$//'` afterwards; a bash heredoc holding C++ broke the shell's quoting (use the Write tool for patch
  scripts); `UnrealEditor-Cmd` needs the absolute `.uproject` path when started with `Start-Process`; editing sources
  during RunChecks turns its time chain red (finish the run, then edit); the Packs lab first had bounds smaller than its
  columns; `CaptureVoxel.ps1`'s default shots show no mobs.

## 8. Tokens (20b2a)

Lead 301k at report A (wrap-up from about 290k). Sonnet reviewer 106.9k.

## 9. Evidence

- RunChecks final: `Tests/Evidence/2026-10-15/Checks-final-20261015-085428/summary.txt`.
- Probe: `Tests/Evidence/2026-10-15/Playground-mobs/Runtime-20261015-092357`.
- Mutations `C:\dev\sandbox\mobs-20b2\mutations\results.txt`; review `C:\dev\sandbox\mobs-20b2\review\findings.md`.

## 10. Open questions (for the SM / Jesus)

- Push: track branches stay local (Jesus decides).

## Short prompt for the next chat

> Survival 20b2b (T6: the passive spawn cycle and caps), lane `C:\dev\projects\Unnamed-mobs`, branch
> `feature/survival-mobs`. Read `Docs/Records/2026-10-15-Mobs-Handoff-20b2b.md` first and follow its reading list;
> your first report is the plan (files against the hot files, hash impact) to the SM-Mobs manager; wait for the go.
