# Blocks B6b handoff: the growth blocks, seeds, saplings to trees, grass, cane age (survival track T5)

**Deferred until after QC (Jesus's hard stop, 2026-10-15 ~23:45).** Start B6b only when SM-Blocks or the PM says so.

Lane `feature/survival-blocks` (worktree `C:\dev\projects\Unnamed-blocks`). B6a is [its record](2026-10-15-Blocks-B6a.md).
**First person only, no hands.**

**Manager:** SM-Blocks run 2 (session `session_01DUpWqKA5aBjmdjZtn1enW6`); report there, not to Jesus.

## 0. B6b's items, in this order (SM-Blocks, 2026-10-15)

1. **Reference additions** (first): `attached_pumpkin_stem`, `attached_melon_stem`, `torchflower`, `pitcher_plant`,
   B1-style through the blocks reference's own tool (`Reference/minecraft-blocks-26.3/tools/build_reference.py`),
   additions only, the B2a freeze holding (model order frozen; new blocks appended), then one `DA_Blocks` re-import
   (B3a/B6a procedure, section 6). Then wire: a grown stem's fruit turns it into the attached stem facing the fruit (one
   fruit per stem; an attached stem whose fruit goes reverts to `*_stem[age=7]`, wiki "Melon Seeds"/"Pumpkin Seeds");
   `torchflower_crop` age 1 grows into `torchflower`; check whether `pitcher_plant` needs a transition (vanilla: the
   pitcher crop stays at age 4; the plant only drops). Closes QC-KNOWN-BUGS `K-BLOCKS-B6a-1`.
2. **Seeds** (second): under a DA_Items lock SM-Blocks requests, the 5 missing items (`beetroot_seeds`,
   `pumpkin_seeds`, `melon_seeds`, `torchflower_seeds`, `pitcher_pod`) with sprites through the items reference's own
   tool; carrot and potato as "plant on farmland, else eat" (`FoundationInteraction.cpp` is hot but any lane may touch
   it; T2 owns item use). Closes `K-BLOCKS-B6a-2`. Today only `wheat_seeds` plants (B6a's id table in
   `UFoundationInteractionSettings::BuildRules`; move the other links into the same table or DA_Items).
3. Then the plan's B6b scope: saplings (stage, tree: Q1 below), grass and mycelium death and spread (R22), sugar cane
   age (R22).

## 1. Reading list (in order)

1. `C:\dev\tools\shift-managers\survival\tracks\T5-plan-B6.md`: sections 1, 2 (sapling, cane, grass rows), 4 (saplings),
   6 (B6b tests), 8 Q1 and "SM-Blocks decisions" (Q1: expose `PlaceTreeAt` from the decorator, about 1 hour time box,
   else stage only + a QC row).
2. [The B6a record](2026-10-15-Blocks-B6a.md), whole.
3. `Docs/Blocks.md`: "Random ticks, crops, stems and farmland moisture (B6a)".
4. `Source/Unnamed/Foundation/FoundationGrowth.h` (whole, about 120 lines) and `FoundationGrowth.cpp` (`RandomTick`).
5. `FoundationAuthority.cpp`: grep `StepRandomTicks`.
6. Blocks reference: `Reference/minecraft-blocks-26.3/README.md` (the rebuild section, the B1 additions), B1's and B2a's
   records (`Docs/Records/2026-10-15-Blocks-B1*.md`, `-B2a.md`, grep "freeze", "added").
7. Values (grep, never read whole): `SURVIVAL-RUN2-FINDINGS-26.3.md` R22 (the 76-state histogram near line 51300, the
   grass controls), `MINECRAFT-26.3-VALUES.md` "Saplings".

## 2. Working agreement

- **Plan first** to SM-Blocks (files against the hot files `FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`,
  `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`; hashes,
  goldens, saved worlds; tests). Wait for the go. Build on the wip branch SM-Blocks names; squash-merge at commit; keep
  the wip branch.
- **Checks:** `Scripts\RunChecks.ps1 -Profile quick|final` (`-Flight smoke` for the final), always with
  `-ExpectedHashes C:\dev\tools\shift-managers\survival\expected-hashes-survival.txt`. Don't edit the tree while it runs.
- **Pre-commit review:** Sonnet 5.5, diff only. **Mutations:** `C:\dev\sandbox\b6a-blocks\scripts\mutate.py` +
  `run-mutations.ps1` (apply, build, targeted tests, revert with touch, final rebuild; `mutate.py check` first).
- **Unity builds:** file-specific helper names (B6a: `GetFamilyCropSoilTag`, `IsGrowthCrop`, `GetGrowthAge`,
  `WithGrowthAge`, `GrowthSides`, `GrowthPickSalt`); grep each new name across `Source/Unnamed`.
- **Never:** push, bare `git stash`, edit CLAUDE.md, re-record goldens or world hashes, hand-edit a DA_* asset, touch
  DA_Items without the lock.
- **Measure lead context** with `node C:\dev\sandbox\b0-sounds\MeasureAgentUsage.mjs --session <id>` (find the id: the
  newest `.jsonl` in `C:\Users\jherediaramirez\.claude\projects\C--dev`, confirmed by grepping a line you wrote).

## 3. Settled decisions

- Random ticks: the player radius (4 sections, `Rules.RandomTickRadius`), the whole build height (the cost is 0.03 ms
  a step, no clamp needed), picks seeded by `Rules.LootSeed`; switch `foundation.Blocks.Growth` (`Options.bGrowth`).
- Light proxy: open sky 15 at any time of day (wiki "Crop farming", cited); else the brightest emitter of 9+ within a
  9x9x9 box. Provisional rows exist.
- Beetroots' 1/3 skip (wiki "Beetroot Seeds"); torchflower and pitcher at the crop formula (provisional).
- No growth events or sounds (vanilla has none).
- The darkness crop pop is QC (`K-BLOCKS-B6a-3`), not B6b's unless SM-Blocks says so.
- Growth is not saved (edits are session-only).

## 4. Code map (after B6a)

- `FoundationGrowth`: `GetRandomTickCell`, `FGrowthRoll`, `FGrowthTable` (per state: kind, age, moisture, tags),
  `GetGrowthPoints`, `GetChanceDenominator`, `GetLight`, `IsHydrated`, `RandomTick` (returns writes; pure).
- `FFoundationAuthority::StepRandomTicks` (after `StepBlockTicks`): sections, picks, writes, then `SettlePlants` per
  write (all written first, so the pitcher's halves still pair). Counters `RandomTicks`, `RandomTickChanges`.
- `FoundationBlockFamilies::GetSurvival`: the crop/stem soil rule (`GetFamilyCropSoilTag`), right after farmland's.
- Stem tint: `FFoundationBlockAsset::StateTint` (UPROPERTY), `FFoundationBlockRegistry::GetTint`, `VoxelBlocks.cpp`;
  importer `BlockImportData.state_tint_fields`, `ImportBlocks.py`.
- Tests: `Tests/FoundationGrowthTests.cpp` (`FGrowthLab`: a bare block world with the test's own sky; the authority labs
  for `.Authority` and `.Plant`).

## 5. Known issues (when)

1. `K-BLOCKS-B6a-1` (attached stems, torchflower): B6b item 1.
2. `K-BLOCKS-B6a-2` (seeds 2-8): B6b item 2.
3. `K-BLOCKS-B6a-3` (crops in darkness never pop): QC.
4. Provisional: tick area, light proxy, torchflower/pitcher rates, the pitcher's space rule (`provisional-values.md`).
5. B5's: trampling parked; farmland and the path drop themselves (B8).
6. Any RunChecks run (flake): the voxel step's `ReadFeatures` crash (B3a); re-run once before suspecting code.

## 6. What worked, what failed (B6a)

- **Worked:** a pure growth module (reads the world, returns writes), so 8 of 9 test files need no authority; oracles
  written from VALUES/R22/R23/wiki in the test; seeded 10^5-tick rate checks within 4 sigma; the DA_Blocks re-import by
  B3a's procedure with a python T3D check (2 lines differ, each equal without `StateTint`); 12 mutations in 9 rounds,
  bundled in one background script.
- **Failed or slow:** the first pick hash (`tick*4+index`) repeated streams once more than 4 picks a step ran (found by
  the accelerated authority test; the `.Picks` repeat check now pins it); a PowerShell regex over the 52 MB T3D
  backtracked for minutes (use python); a test lab without `A.PlantSurvival` let wheat stand on dirt (set it as the game
  does); the block table lacked the attached stems and the torchflower, and DA_Items the seeds (check the targets of
  every transition exist before planning).

## 7. Token figures (B6a)

- See report A/B and the B6a record.

## 8. Evidence

- `C:\dev\sandbox\b6a-blocks\logs\` (builds, tests, `mutations.log`), `...\import\` (T3D before/after, `t3d-proof.txt`),
  `...\scripts\` (mutations), `...\review\`.

## 9. Open questions

1. **Push of `feature/survival-blocks`:** Jesus's call.
2. **Branches `wip/b4a`, `wip/b4b`, `wip/b5`, `wip/b6a`:** kept after the squash; deleting them needs the PM.
3. **Trampling:** as in B5's handoff (a T4 "lift by" hook, or parked to QC).
4. **CLAUDE.md's importer commands** use relative `-script=` paths (B1 known issue 7): Jesus's file.

## Short prompt for the next chat

> B6b: the growth blocks (attached stems, torchflower, pitcher plant), the seeds, saplings to trees, grass and cane age
> (survival track T5), lane `C:\dev\projects\Unnamed-blocks`, branch `feature/survival-blocks` at the B6a handoff
> commit. Start with `Docs/Records/2026-10-15-Blocks-Handoff-B6b.md`: read it whole, then its reading list. SM-Blocks
> run 2 (`session_01DUpWqKA5aBjmdjZtn1enW6`) manages T5: report there. Report your plan first: every file against the
> hot files, whether any hash, golden or saved-world number moves, and the test plan. Wait for the go. First person
> only, no hands.
