# Survival 20b2 handoff: passive spawning

Start here for T6's next step on lane `feature/survival-mobs` (20b1 landed: [the 20b1 record](2026-10-15-Mobs-20b1.md)).
Size M. 20b was split by the SM (2026-10-15): 20b1 built the three mobs, tempt and A*; 20b2 is passive spawning plus
the folds below.

## 1. Reading list (in order)

1. [The 20b1 record](2026-10-15-Mobs-20b1.md), whole (about 80 lines), and [the 20a record](2026-10-15-Mobs-20a.md)
   sections 1 and 5.
2. `C:\dev\sandbox\scout-mobs\facts.md`: section 4 "Spawning" (lines 117-139), section 5 oracles O9-O11 (lines 159-161),
   section 6 "Spawning runs in" (lines 198-200), section 10 Q1 (light, lines 408-411).
3. `C:\dev\tools\shift-managers\survival\codex\research\02-t6-mobs-oracles.md` section A rows "globalCap" and "sky
   light" and section B row "Spawning".
4. Grep `C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md` (never read whole) for `survival.r10.spawn_caps`
   (5461-5470), `survival.r10.spawning_rules` (5472), `survival.r10.spawn_registry` (5476-6293: read only the plains,
   forest and a snowy row to learn the shape). value-swaps.md lines 263-279.
5. `Reference/minecraft-worldgen-26.3/data/minecraft/worldgen/biome/plains.json` lines 1-60 (the
   `minecraft:gameplay/natural_mob_spawns` attribute: `spawns_by_category.creature[]` with `type`, `count` (an int or a
   uniform), `weight`); grep the folder for `creature_spawn_probability` / `world_gen_spawn` to find the chunk-gen chance.
6. Code: `FoundationAuthority.cpp` `StepMobs` and `SpawnMob` (grep), `FoundationMobs.h` (constants), the voxel column
   biome API `Source/UnnamedVoxel/Storage/VoxelColumn.h` lines 46-56 (`GetBiome(X, Y, Z)`, a biome index into the
   terrain's table) and `Source/UnnamedVoxel/Terrain/VoxelTerrain.h` line 49 (`GetBiomeSource`).

## 2. Working agreement

As 20b1's: the SM confirms the plan; report it against the hot files (`FoundationAuthority.h/.cpp`,
`FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`,
`Docs/Records/README.md`), diffed against the latest `feature/survival`; values from the value report, SURVIVAL-FINDINGS,
game data, the wiki or black-box runs (never decompiled code); provisional values get rows; `RunChecks -Profile final`
before the review; a Sonnet review split by path lists above about 1,500 diff lines; never push. Measure the lead with
`Scripts\MeasureAgentUsage.mjs` (from a C:\dev session: a copy with `const projectRoot = "C:/dev";` and `--session`).

## 3. Settled (do not reopen)

- Everything in the 20a and 20b1 records' Decisions.
- Light for spawning: the proxy (scout Q1 (a)): sky-exposed above the `MotionBlocking` heightmap = 15; behind
  provisional rows. A real CPU light engine is a later program step.
- The overlay: multiply the texture by about (1, 0.55, 0.55) while hurt or dying, the 0.3 mix kept one switch away
  (SM look call 2026-10-15; a scalar in `M_Mob`, `ImportMobs.py` `build_material`, reimport `DA_Mobs`).
- Walk cycle: kept as is (provisional).

## 4. What 20b2 does (proposal; confirm with the SM)

1. **Biome spawn table**: import each biome's `natural_mob_spawns.creature` entries (type, weight, count min/max) from
   the worldgen reference into a runtime table. Options: (a) a small generated asset (`DA_Mobs` gains `SpawnEntries`
   per biome id, written by `ImportMobs.py` reading the worldgen reference; no reference change); (b) parse the JSON at
   startup. Recommendation (a): `DA_Mobs` is T6's own asset, no lock. Only built mobs count (O9: plains renormalises to
   sheep 12, pig 10, chicken 10, cow 8 over 40). Map the column's biome index to its id through the terrain's biome
   table (check what `FVoxelBiomeSource` exposes; Playground has no biomes: use plains or skip).
2. **Chunk-generation packs**: when a column first becomes loaded (a seen-set of columns), roll the biome's
   `creature_world_gen_spawn_probability` (default 0.1; O11: snowy_plains 0.07, badlands 0.03), then one pack: a species
   by weight, `count` members spread +-5 blocks (provisional) on grass blocks with the light proxy >= 9 and room for the
   box; deterministic from (seed, column).
3. **The 400-tick cycle**: creatures every 400 ticks, cap `floor(10 x eligibleChunks / 289)` [survival.r10.spawning_rules,
   spawn_caps], up to 3 group rounds with `ceil(U x 4)` attempts, min player distance 24, radius 8 chunks; farm animals
   are persistent (no despawn).
4. **Switch** `foundation.Mobs.Spawn` (1 default once green; 0 = only `foundation.Mobs.Summon`).
5. **Folds**: the overlay multiply (section 3); rotate the A* budget's start index per step (`StepMobs`, `PathsPerStep`
   goes in mob order today, so later mobs starve under load); a ledge + turn waypoint test (`Steer` reaches a step-up
   waypoint within 0.5 block horizontally, Z ignored); the Codex review 19 fold-ins the SM adds to the prompt.
6. **Tests**: O9 (weights over the built mobs), O10 (pack sizes), O11 (chance per biome), the cap formula, the 24-block
   minimum, determinism, the switch; a runtime shot of a generated world with spawned animals (create-world or
   `CaptureVoxel.ps1`).

Size guard: if the import plus both spawn paths will not fit, propose 20b2a (table + chunk-gen packs) and 20b2b (cycle
and cap).

## 5. Code map (20b1)

- `FoundationMobPath.h/.cpp`: A* (`GetStand`, `FindPath`; 256 nodes, rise <= 1.25, drop <= 3).
- `FoundationMobs.h/.cpp`: kinds (`MobKinds`: panic, tempt, `bSpawnable`), `IsTemptItem` / `MobFoods`, `Think` (panic >
  tempt > graze > stroll; look), `PlanPath` (stand cell for random targets, `PathGoal`), `Steer` (waypoints), `Move`
  (chicken fall), `Animate` (flap), the graze / wing / biped-leg helpers.
- `FoundationAuthority.cpp`: `SpawnMob` (egg timer), `GetPathCell` (collision boxes that start in a cell; fluids
  avoided; scratch `PathBoxes`), `CanGraze`, `EatGrass`, `StepMobs` (players' hands, the per-step path cache and
  budget, eggs). Counters `MobPaths`, `MobEggs`, `MobGrazes`.
- `FoundationMobView.cpp`: layers (`FLayerDraw`), wings, biped legs, graze head; `FoundationMobAssets` `FindLayers`;
  `MobImportData.LAYERS`.
- Tests: `FoundationMobTests.cpp` (Spawn ... Graze), `FoundationMobPathTests.cpp` (Path).

## 6. Known issues, with when

- Grazing's break particles and sound: silent hooks (20b2 if cheap, else when block-break events serve mobs).
- Tempt has no calm-down or scare check (no source); pig's carrot on a stick: when it exists.
- Wool untinted white, no undercoat; shearing, colours: 20e. Breeding, babies: 20e.
- Zombie and creeper tables left out of `DA_Mobs`; skeleton's table waits for T2 19c's arrow (its warning now logs once
  per process): 20c / 20d.
- `feature/survival` moved to 60d2f90a (T4 11a death screen; T5 B2b stair/fence/wall/pane/gate shapes with 1.5-tall
  fence and wall collision): merged after the 20b1 commit at the SM's merge go (check `git log` for the merge commit);
  A* already refuses floors taller than one block.

## 7. What worked, what failed (20b1)

- Worked: a pure A* module with a lambda query, tested on a map of cells; the authority's query reads the same collision
  the mobs move against (so the Playground's static floor works); a scripted mutation runner
  (`C:\dev\sandbox\mobs-20b\mutations\run.ps1`: replace, build, run one test, restore and touch) ran 8 mutations
  unattended; the review split into two diffs of about 1,000 lines each.
- Failed first: the A* subagent built while the lead was mid-edit and stopped on those errors (give a subagent its own
  window or don't build in parallel); its corner test used one-high blocks, which a mob climbs (use two high); a braced
  `UE_LOG` in an unbraced if/else does not compile; `ImportMobs.py` needs an absolute `-script=` path; the importer
  re-saves every texture and `M_Mob`: restore the unchanged ones with `git checkout --` before committing.

## 8. Tokens (20b1)

Lead peak 348.1k at report A (wrap-up started at the 300k checkpoint). Subagents (Sonnet 5.5): A* module 89.7k,
reviewers 87.1k and 96.6k.

## 9. Evidence

- RunChecks final: `Tests/Evidence/2026-10-15/Checks-final-20261015-063224/summary.txt`.
- Capture: `Tests/Evidence/2026-10-15/Playground-mobs/Runtime-20261015-070212`.
- Mutations: `C:\dev\sandbox\mobs-20b\mutations\results.txt`; overlay sample `C:\dev\sandbox\mobs-20b\overlay_sample.py`.

## 10. Open questions (for the SM / Jesus)

- Push: track branches stay local (Jesus decides).
- The Playground has no biomes: no passive spawning there (recommended) or plains entries.

## Short prompt for the next chat

> Survival 20b2 (T6: passive spawning, plus the overlay multiply and the A* folds), lane
> `C:\dev\projects\Unnamed-mobs`, branch `feature/survival-mobs`. Read `Docs/Records/2026-10-15-Mobs-Handoff-20b2.md`
> first and follow its reading list; your first report is the plan (files against the hot files, hash impact) to the
> SM-Mobs manager; wait for the go.
