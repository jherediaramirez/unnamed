# T6 handoff after 20e1: the after-QC resume point (shears and dyes, 20e2 breeding and babies, then 20d)

Start here when Jesus's QC is done and the mob line resumes on lane `feature/survival-mobs`
(`C:\dev\projects\Unnamed-mobs`). 20e1 landed: [the 20e1 record](2026-10-15-Mobs-20e1.md) (its section 6 lists what the
mob line has built, partly built and not built). Jesus via PM 5 (2026-10-15): 20e1 was the line's last step before QC;
20e2 and 20d wait for it, and the DA_Items lock asked for 20e2 was withdrawn (ask again).

## 1. Order of work after QC

1. **Shears and the 14 missing dyes as items** (needs a PM lock on DA_Items and the items reference). The items
   reference (`Reference/minecraft-items-26.3/items.json`, 146 items) has no `shears` and only `green_dye` and
   `lime_dye`; add them append-only through its `tools/build_reference.py` (move the accepted folder away first; prove
   additions only, as 20e1's `C:\dev\sandbox\mobs-20e1\refproof.py` did for the entities reference), then
   `ImportItems.py` (verify output in the report). Check whether `recipes.json` already carries shears (2 iron ingots)
   and the flower dyes once the items exist. Then remove the shears stand-in in `FoundationMobTests.cpp` `FFarmLab`
   (it only adds one when DA_Items lacks shears) and run `Mobs.Shear` against the real item (238 durability).
2. **20e2: breeding and babies**, per `C:\dev\tools\shift-managers\survival\tracks\T6-plan-20e.md` (sections 2-7, the
   20e2 rows): love mode, the breed goal, babies with their own sizes (`survival2.r20.baby_sizes`) and geometry (the
   baby layers: `cow_baby`, `cold_cow_baby`, `warm_cow_baby`, `pig_baby`, `sheep_baby` main + wool, `chicken_baby`),
   growth, feeding a baby, follow-parent, no drops; `foundation.Mobs.Breed`. Babies are not adult x 0.5. The baby
   textures exist in the jar (`entity/<mob>/<mob>_<variant>_baby.png`, `sheep_baby.png`, `sheep_wool_baby.png`); append
   them to the entities reference's `TEXTURES` and the baby layers to `EntityModelFacts.java`'s `APPEND` (after
   `cold_chicken`), as 20e1 did. Shearing and milking a baby must then refuse (`UseOnMob`).
3. **20d: creeper and skeleton**: `C:\dev\tools\shift-managers\survival\tracks\T6-plan-20d.md`.

## 2. Reading list

1. [The 20e1 record](2026-10-15-Mobs-20e1.md) whole (about 110 lines).
2. `C:\dev\tools\shift-managers\survival\tracks\T6-plan-20e.md` sections 3 (values, the 20e2 rows), 4 (files), 6
   (tests) and 7 (risks 1, 2, 4, 5, 6, 9); the SM note at its end.
3. [The 20c2 record](2026-10-15-Mobs-20c2.md) section 5 (the code map before 20e1) and [the 20c1 record](2026-10-15-Mobs-20c1.md)
   section 5.
4. `C:\dev\tools\shift-managers\survival\ACTION-PLAN.md` section 3 (locks) and the current FINAL-SCOPE.md / QC list.
5. Values: grep (never read whole) `C:\dev\projects\value-holder\SURVIVAL-RUN2-FINDINGS-26.3.md` for
   `survival2.r20.baby_sizes`, `survival2.r20.walk_input_rule`, R17 `survival2.r17.model_layers`; `MINECRAFT-26.3-VALUES.md`
   `mobs.breeding_cooldown`; `SURVIVAL-FINDINGS-26.3.md` `survival.r10.ai_defaults` (BreedGoal, FollowParentGoal,
   F 5342-5432) and `survival.r10.idle_and_sun` (baby grazing nextInt(50)). `MODEL-LAYERS-26.3.json` is an allowed
   source (R17, SM note) for cross-checking the dumped baby layers.

## 3. Working agreement (as 20e1)

Plan first to SM-Mobs (files against the hot files `FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`,
`FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`, diffed against
the latest `feature/survival`); values cite ids; provisional rows in `provisional-values.md`; mutations with
`C:\dev\sandbox\mobs-20e1\mutations\run.ps1` (a pattern / replacement / test list; per-mutation `-abslog`); Sonnet review
on the diff only (split by path list over ~1500 lines); `RunChecks -Profile final -ExpectedHashes
C:\dev\tools\shift-managers\survival\expected-hashes-survival.txt`; never push. Builds pass `-NoHotReloadFromIDE` (another
lane's `-game` run holds the engine-wide Live Coding lock) behind `Scripts\QuietWindow.ps1`. Measure the lead with
`node C:\dev\sandbox\mobs-20b2\MeasureAgentUsage.mjs --session <id>`.

## 4. Settled (do not reopen)

- Everything in the 20a-20e1 records' Decisions.
- The entities reference grows only by appending through its tools (`LOOT`, `TEXTURES`, `EntityModelFacts.java`
  `APPEND`); the dumper is compiled against the Modrinth client jar and its libraries (classpath builder
  `C:\dev\sandbox\mobs-20e1\cp.py`, JDK 25 `C:\Users\jherediaramirez\.gradle\jdks\eclipse_adoptium-25-amd64-windows.2`).
- Colour and variant draws use `FoundationMobFarm::MakeSeed`, apart from `Mob.Random` (keep babies' draws off it too).

## 5. Code map

The 20e1 record section 5.

## 6. Known issues, with when

- The 20e1 record section 4; the 20c2 record section 4.
- Known, on the QC list: A* finds no path on the Playground's floor (20c1); `FVoxelDensityScratch` destructor crash once
  (20c1); the dye sound (20e1, SM).

## 7. What worked, what failed (20e1)

- Worked: extending the reference's own dumper (`APPEND`) and `LOOT` kept every old file byte-identical and the facts
  JSON an exact prefix; the importer's loot inliner took the shearing tables with one rule (uniform rolls of one plain
  item = a uniform count); keeping the authority's farm logic in `FoundationMobFarm.cpp` kept the hot-file hunks small.
- Failed first: the plan read `item_index.json` (every 26.3 item) instead of `items.json` (the 146 imported), so the
  missing shears and dyes surfaced only at the first test run; check the imported list (`Rules.FindItem`) at plan time.
  Bash heredocs mangle `\` line continuations inside Python patch scripts; write patch scripts with the Write tool.

## 8. Tokens (20e1)

Lead 108.5k at the plan, 270.3k at the shears blocker, 316k at the wrap-up start, 363.9k at report A. Sonnet reviewer
104.5k peak (one 1443-line diff). The mutation runner and RunChecks ran as background shells, not agents.

## 9. Evidence

The 20e1 record section 7.

## 10. Open questions (for the SM / Jesus)

- Push: track branches stay local (Jesus decides).
- The DA_Items + items-reference lock for shears and the 14 dyes (withdrawn for now; ask after QC).
- Look call on the wool tint (x 0.75, white E6E6E6) and the undercoat rule (20e1 shot 14a vs 14b).

## Short prompt for the next chat

> Survival T6 after QC: shears and dyes, then 20e2 (breeding, babies), then 20d (creeper, skeleton); lane
> `C:\dev\projects\Unnamed-mobs`, branch `feature/survival-mobs`. Read `Docs/Records/2026-10-15-Mobs-Handoff-AfterQC.md`
> first and follow its reading list; your first report is the plan (files against the hot files, the locks needed, hash
> impact) to the SM-Mobs manager; wait for the go.
