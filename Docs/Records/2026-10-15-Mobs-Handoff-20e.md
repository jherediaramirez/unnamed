# T6 handoff after 20c2: 20e (shearing, milk, breeding and babies, variants)

Start here for T6's next step on lane `feature/survival-mobs` (20c2 landed: [the 20c2 record](2026-10-15-Mobs-20c2.md)).
**Feature freeze** (Jesus via PM, 2026-10-15): nothing beyond the plan; bugs go to the QC list (SM-Codex's
`QC-KNOWN-BUGS.md`), not into the step. ACTION-PLAN line 2: 20e now, then 20d (creeper, skeleton; waits on T2 19c).

## 1. Reading list (in order)

1. `C:\dev\tools\shift-managers\survival\tracks\T6-plan-20e.md` whole (199 lines): the planner's plan, its split
   (20e1 farm extras, 20e2 breeding and babies) and sources. The SM decides the split.
2. [The 20c2 record](2026-10-15-Mobs-20c2.md) sections 1, 4 and 5 (about 90 lines in all); [the 20c1
   record](2026-10-15-Mobs-20c1.md) section 5 (the code map before 20c2).
3. `C:\dev\tools\shift-managers\survival\ACTION-PLAN.md` line 2 (lines 26-31) and section 3 (locks: shearing and milk
   touch items; DA_Items changes need a PM lock).
4. `C:\dev\tools\shift-managers\survival\value-swaps.md` lines 137-166 (breeding cooldown 6000 [mobs.breeding_cooldown])
   and 263-279; `codex\research\02-t6-mobs-oracles.md` C (shearing 1-3 and regrowth, baby sizes and growth,
   milking: no reference, provisional).
5. Grep (never read whole) `C:\dev\projects\value-holder\SURVIVAL-RUN2-FINDINGS-26.3.md` for `survival2.r20.baby_sizes`
   (lines 320-330) and R17 (the plan names the rows); `SURVIVAL-FINDINGS-26.3.md` `survival.r10.ai_defaults` for
   BreedGoal / FollowParentGoal rows (F 5349-5432).
6. Code: `FoundationMobs.h` (kinds, `Think`'s goal order), `FoundationAuthority.cpp` `StepMobs`, `EatGrass`,
   `DropMobLoot`, the interaction path a mob hit takes (`Attack`), `FoundationMobView.cpp` (layers: the sheep's wool).

## 2. Working agreement

As 20c2's: plan first to SM-Mobs (files against the hot files `FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`,
`FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`, diffed against
the latest `feature/survival`); values cite ids; provisional rows in
`C:\dev\tools\shift-managers\survival\provisional-values.md`; mutation runner (copy
`C:\dev\sandbox\mobs-20c2\mutations\run.ps1`: a list of pattern / replacement / test); Sonnet review on the diff only
(split by path list over ~1500 lines); `RunChecks -Profile final`; never push. Measure the lead with
`node C:\dev\sandbox\mobs-20b2\MeasureAgentUsage.mjs --session <id>` (the newest jsonl in
`C:\Users\jherediaramirez\.claude\projects\C--dev` holding a line you wrote). Launch `UnrealEditor-Cmd` with
`Start-Process` and absolute `-script=` / `-abslog=` paths.

## 3. Settled (do not reopen)

- Everything in the 20a-20c2 records' Decisions (20c2: blank entries for unbuilt monsters; cost fixed with the voxel
  heightmaps and palettes, `MonsterColumnsPerStep` 289 = vanilla's full pass).
- The light proxy (provisional rows T6 20c2) and its storage hooks `GetTopZ` / `MayHold`.

## 4. Code map (after 20c2)

The 20c2 record section 5. New since 20c1: categories and caps (`GetCategory`, `GetCategoryCap`), `RunSpawnCycle`,
`FindMonsterCell`, the light proxy functions, `ShouldDespawn` / `NoActionTicks`, `IsPeaceful` and facts' `Difficulty`,
the spider's AI branches and `GetSpiderLegSwing`, `FFoundationBiomeSpawns::Monsters` with `bBlank`.

## 5. Known issues, with when

- The 20c2 record section 4 (cost spikes up to ~3 ms in an under-cap monster cycle; emitter lists a second stale;
  spiders rarer than zombies in caves; non-spawnable full blocks allowed as floors).
- Known, on QC list: A* finds no path on the Playground's floor (20c1 record section 4).
- Known, on QC list: native attempt 1 of 20c1's final run crashed in `FVoxelDensityScratch`'s destructor.
- Breeding (20e) will add creatures that persist: the creature census (Codex 30 #1) now counts them all, loaded and
  not frozen, wherever they are.

## 6. What worked, what failed (20c2)

- Worked: generalising the cycle by category kept the creature path byte-for-byte except the census; pure
  `RollSkyLight` / `RollMonsterLight` / `ShouldDespawn` made the statistical tests one-liners; a temporary per-part
  timer in the real world found the cost in one capture (the column scan was 85%).
- Failed first: the first real-world night capture cost 1.8 ms a step (the column scans); a 32-column spread plus an
  8-section emitter budget starved spawns (2 in 1243 steps: a candidate needs up to 27 sections settled within their
  20-step life). The voxel world's heightmaps and section palettes fixed both. Removing the timer by index cut the
  file's top namespace once (restored from HEAD; check `git diff` after any scripted removal).
- The `Packs` test's roofed lab is dark now, so monsters spawn there: it counts creatures only.

## 7. Tokens (20c2)

Lead 120.8k at the plan, 355k at the wrap-up start, 428.7k at report A. Sonnet reviewers 99.2k and 118.2k peak (two halves of a 1766-line diff).

## 8. Evidence

- RunChecks final: `Tests/Evidence/2026-10-15/Checks-final-20261015-183232/summary.txt`; probe `-Suite mobs`
  `Tests/Evidence/2026-10-15/Playground-mobs/Runtime-20261015-183126` (spider shots 13a, 13b, 13c).
- Night captures: `Tests/Evidence/2026-10-15/VoxelShots-20c2-night-1` (before the cost fix), `-night-2` (the spread
  attempt), `C:\dev\sandbox\mobs-20c2\prof-2` (final cost figure).
- Mutations `C:\dev\sandbox\mobs-20c2\mutations\results.txt` (+ `results-retry.txt`); review
  `C:\dev\sandbox\mobs-20c2\review\findings-A.md`, `findings-B.md`; focused native `C:\dev\sandbox\mobs-20c2\native-3`.
- `feature/survival` moved to 8a5e87ea (T1 18-chest, includes T5 B4a ad045c40): merge at the SM's merge go; after it,
  RunChecks needs `-ExpectedHashes C:\dev\tools\shift-managers\survival\expected-hashes-survival.txt` (B4a moved the
  radius-8 amplified hash by Jesus's decision).

## 9. Open questions (for the SM / Jesus)

- Push: track branches stay local (Jesus decides).
- 20e split (planner recommends 20e1 + 20e2): the SM's call.

## Short prompt for the next chat

> Survival T6 20e (shearing, milk, breeding and babies, variants), lane `C:\dev\projects\Unnamed-mobs`, branch
> `feature/survival-mobs`. Read `Docs/Records/2026-10-15-Mobs-Handoff-20e.md` first and follow its reading list; your
> first report is the plan (files against the hot files, hash impact, the 20e split) to the SM-Mobs manager; wait for
> the go.
