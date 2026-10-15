# Blocks B6a handoff: random ticks, crops, stems, farmland moisture, the stem tint (survival track T5)

Lane `feature/survival-blocks` (worktree `C:\dev\projects\Unnamed-blocks`). B5 is [its record](2026-10-15-Blocks-B5.md).
B6a: the random-tick system, crops (wheat, carrots, potatoes, beetroots, torchflower, pitcher), stems (age, fruit),
farmland moisture (hydration, drying, fresh farmland's revert) and the stem tint fix. **First person only, no hands.**

**Manager:** SM-Blocks run 2 (session `session_01DUpWqKA5aBjmdjZtn1enW6`); report there, not to Jesus.

## 1. Reading list (in order)

1. `C:\dev\tools\shift-managers\survival\tracks\T5-plan-B6.md`: whole (about 105 lines), especially section 1 (the
   B6a/B6b split), 2 (values), 6 (tests), 7 (world-hash risk) and "SM-Blocks decisions". Re-check its line pointers.
2. [The B5 record](2026-10-15-Blocks-B5.md), whole (about 70 lines).
3. `Docs/Blocks.md`: "Farmland, dirt paths, the hoe and the shovel (B5)", "Pop drops and the firefly bush (B4a)".
4. `Source/Unnamed/Foundation/FoundationBlockFamilies.cpp`: grep `IsTurnedToDirt`, `DoesKeepFarmland`,
   `GetTurnedState` (fresh farmland's revert to dirt reuses `GetTurnedState`: write dirt, no drop).
5. `FoundationAuthority.cpp`: grep `ScheduleBlockTick` (B3b's deterministic ticks; random ticks are new), `SettlePlants`
   (the `Settle` lambda's turned-state branch), `TillOn`.
6. Values (grep, never read whole): `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md` ("Watering farmland",
   crops, stems), `SURVIVAL-RUN2-FINDINGS-26.3.md` (R22).

## 2. Working agreement

- **Plan first** to SM-Blocks: every file against the hot files (`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`,
  `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`); hashes,
  goldens, saved worlds (random ticks in worldgen-time paths would move hashes: plan section 7); the test plan. Wait
  for the go. Build on the wip branch SM-Blocks names, squash-merge at commit, keep the wip branch.
- **Checks:** `Scripts\RunChecks.ps1 -Profile quick|final` (`-Flight smoke` for the final), always with
  `-ExpectedHashes C:\dev\tools\shift-managers\survival\expected-hashes-survival.txt`. Don't edit the tree while it
  runs (draft in the sandbox). clang-format only the changed lines' files, and check the diff for unrelated reflows
  (`FoundationAuthority.cpp`'s `StepMobs` reflows under the formatter: keep that hunk out).
- **Pre-commit review:** Sonnet 5.5 on the diff only; re-run what fixes touch.
- **Mutations:** `C:\dev\sandbox\b5-blocks\scripts\mutate.py` + `run-mutations.ps1` are the template (apply, build,
  targeted tests, revert with `touch`, final rebuild).
- **Never:** push, bare `git stash`, edit CLAUDE.md, re-record goldens or world hashes, hand-edit a DA_* asset.
- **Measure lead context** with `node C:\dev\sandbox\b0-sounds\MeasureAgentUsage.mjs --session <id>`.

## 3. Settled decisions

- B5 keeps `moisture` as data: fresh farmland is moisture 0 and nothing changes it until B6a's random ticks.
- Farmland and the path turn to dirt by block id in `FoundationBlockFamilies` (no `EFoundationBlockFamily` entry);
  a turn writes dirt with no drop and no `Popped` event.
- Trampling is parked (SM-Blocks Q4; design note in `provisional-values.md`); not B6a's unless SM-Blocks says so.
- Family logic in `FoundationBlockFamilies.h/.cpp`; the authority gets call lines.
- **Expect in a later merge:** the B4b landing (SM-Blocks' "B4b MERGE GO"), T1's step 18 hooks; new event kinds from
  other lanes (keep both in landing order in `EFoundationEventKind` and the controller's `Kinds[]`).

## 4. Code map (after B5)

- **Tilling:** `FoundationTilling` (`GetHoesTag`, `GetShovelsTag`, `GetHoeTable`, `GetShovelTable`, `Find`,
  `WearPerUse`); `FFoundationAuthority::TillOn` (beside `StripOn`), `Result.bTilled/bFlattened`, events
  `Tilled`/`Flattened`, counter `Tills`; the view's two sounds (`FoundationInteractionView.cpp`).
- **Survival:** `FoundationBlockFamilies::GetSurvival` (farmland/path first, then vines, then the Interact gate),
  `IsFarmlandOn` (`foundation.Blocks.Farmland`), `GetTurnedState`, `GetStateForPlacement`'s dirt rule,
  `UsesFamilyPlacement`.
- **Tests:** `Tests/FoundationTillingTests.cpp` (`FTillLab`: aims at a cell from any face), `.Interact.FarmlandSurvival`.

## 5. Known issues (when)

1. Trampling parked: a later step with T4 (the 1/16 lift).
2. Farmland and the path drop themselves: B8 (loot tables held back in the items reference).
3. The hoe's face rule and rooted dirt's any-face rule: provisional until a probe.
4. Climb ascent and the B4b provisional rows: unchanged (a movement probe needs Jesus's go).
5. Any RunChecks run (flake): the voxel step's `ReadFeatures` crash (B3a); re-run once before suspecting code.

## 6. What worked, what failed (B5)

- **Worked:** an independent oracle table in the test (the wiki's rows) instead of reading the production table; exact
  pinned case counts (they show blocked looks, for example water does not block a top look); 4 mutation rounds bundled by
  target test in one background script; the review's test gaps turned into M8.
- **Failed or slow:** in PowerShell, `R` is a built-in alias (Invoke-History): a helper named `R` silently did nothing;
  a `-like` pattern with a backtick matched nothing and a null `Replace` inserted a row on every line of Breaking.md
  (restored from git: build replacement anchors with `StartsWith` or `Contains` and throw when they miss); clang-format
  on a whole hot file reflowed an unrelated hunk.

## 7. Token figures (B5)

- **Lead:** 113.2k at the plan, 236.2k before the final run; report A has the final figure (session
  `c8d03fa0-ea2f-5046-be49-2df3e754f92c`).
- **Subagents:** pre-commit review (Sonnet 5.5) 88.6k peak.

## 8. Evidence

- `C:\dev\sandbox\b5-blocks\logs\`: build-1..4, tests-1..3, mutations.log, build-mut1..5, tests-mut1..5.
- `C:\dev\sandbox\b5-blocks\review\b5-diff.patch`, `...\scripts\` (mutate.py, run-mutations.ps1).
- RunChecks final: see report A.

## 9. Open questions

1. **Push of `feature/survival-blocks`:** Jesus's call.
2. **Branches `wip/b4a`, `wip/b4b`, `wip/b5`:** kept after the squash; deleting them needs the PM.
3. **Trampling:** when T4 can take the 1/16 lift (options: a T4 step adds a "lift by" hook and B-later adds the roll;
   or keep it parked to QC).
4. **CLAUDE.md's importer commands** use relative `-script=` paths (B1 known issue 7): Jesus's file.

## Short prompt for the next chat

> B6a: random ticks, crops, stems, farmland moisture and the stem tint (survival track T5), lane
> `C:\dev\projects\Unnamed-blocks`, branch `feature/survival-blocks` at the B5 handoff commit. Start with
> `Docs/Records/2026-10-15-Blocks-Handoff-B6a.md`: read it whole, then its reading list. SM-Blocks run 2
> (`session_01DUpWqKA5aBjmdjZtn1enW6`) manages T5: report there. Report your plan first: every file against the hot
> files, whether any hash, golden or saved-world number moves, and the test plan. Wait for the go. First person only,
> no hands.
