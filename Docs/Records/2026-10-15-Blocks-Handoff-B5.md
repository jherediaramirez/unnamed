# Blocks B5 handoff: farmland, dirt path, hoe and shovel use (survival track T5)

Lane `feature/survival-blocks` (worktree `C:\dev\projects\Unnamed-blocks`). B4b is [its record](2026-10-15-Blocks-B4b.md).
B5: the hoe tills, the shovel flattens to a dirt path, farmland and dirt path turn to dirt under a solid block, and
trampling if T4's landing hook is one line. **First person only, no hands.**

**Manager:** SM-Blocks run 2 (session `session_01DUpWqKA5aBjmdjZtn1enW6`); report there, not to Jesus.

## 1. Reading list (in order)

1. `C:\dev\tools\shift-managers\survival\tracks\T5-plan-B5.md`: whole (about 90 lines): sections 1-8 and "SM-Blocks
   decisions" (Q4: trampling only as a one-line call into T4's landing path, else parked). Re-check its line pointers.
2. [The B4b record](2026-10-15-Blocks-B4b.md), whole (about 60 lines).
3. `Docs/Blocks.md`: "Ladders, climbing and vines (B4b)", "Buttons and levers (B3b)" and "Placement".
4. `Source/Unnamed/Foundation/FoundationBlockFamilies.h` (whole, about 130 lines): `GetSurvival` is where farmland's and
   the path's rows go (the vines' rule shows the shape of one that runs before the Interact gate).
5. `FoundationAuthority.cpp`: grep `StripOn` and `UseOn` (T3's axe stripping: B5's hoe and shovel are its siblings).
6. Values (grep, never read whole): `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md`,
   `SURVIVAL-RUN2-FINDINGS-26.3.md`; `Reference/minecraft-blocks-26.3/facts/family_survival.json` (farmland, dirt_path).

## 2. Working agreement

- **Plan first** to SM-Blocks: every file against the hot files (`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`,
  `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`, and T4's
  `FoundationSimulation.cpp`/`FoundationVitals.cpp` if trampling is in); hashes, goldens, saved worlds; the test plan.
  Wait for the go.
- **Checks:** `Scripts\RunChecks.ps1 -Profile quick|final` (`-Flight smoke` for the final). Pass
  `-ExpectedHashes C:\dev\sandbox\b4a-blocks\hashes\expected-hashes-b4a.txt` until SM-Blocks says B4a landed and the
  shared file moved. Don't edit sources or docs in the tree while RunChecks runs (draft in the sandbox); clang-format
  before the final run.
- **Pre-commit review:** Sonnet 5.5 on the diff only (split over about 1,500 lines); re-run what fixes touch.
- **Mutations:** a scripted apply/revert (`C:\dev\sandbox\b4b-blocks\scripts\mutate.py` is a template: backups,
  `touch` on revert); bundle mutations that hit different assertions, but one that masks another goes alone.
- **Never:** push, bare `git stash`, edit CLAUDE.md, re-record goldens or world hashes, hand-edit a DA_* asset.
- **Measure lead context** with `node C:\dev\sandbox\b0-sounds\MeasureAgentUsage.mjs --session <id>`.

## 3. Settled decisions

- Family logic in `FoundationBlockFamilies.h/.cpp`; the authority gets call lines (B5's use branch beside `StripOn`).
- Survival from `family_survival.json`, every state and candidate (`CheckFamilySurvival` in
  `FoundationInteractTests.cpp` takes a block filter: reuse it).
- Moisture, hydration and crop growth need random ticks: B6. B5 keeps `moisture` as data.
- Black-box probes may copy `Tests/Probes/LadderPlacementProbe.java` (public API only).
- **B4b, done and not to redo:** climbing, ladders, twisting and weeping vines' survival and stacks.
- **Expect in a later merge:** T1's step 18 hooks in `FoundationBlockFamilies.cpp` and `bSecondaryUse` in
  `FoundationBlockPlacement.h`; take them as they come.

## 4. Code map (after B4b)

- **Families:** `GetFamily` (+ `Ladder`), `GetStateForPlacement` (the ladder branch after the waterlogged one),
  `GetSurvival` (vines first, then the Interact gate, then button/lever/ladder, doors), `IsClimbOn`, `IsClimbable`,
  file-local `GetVineSupport`, `CVarBlocksClimb`.
- **Movement:** `FFoundationEligibility::bOnClimbable`, `FFoundationSimulation::bClimbing`, constants
  `ClimbClampCmPerTick`/`ClimbUpCmPerTick` (FoundationSimulation.cpp); `FFoundationVitalsStep::bClimbing`; the pawn's
  step loop sets the eligibility after `GetFluidFacts`.
- **Tests:** `Unnamed.Foundation.Climb.*` (`Tests/FoundationClimbTests.cpp`, `FClimbCourse`: a ladder column on a stone
  wall in an `FFoundationBlockWorld`), `.Interact.ClimbSurvival`, `.Interact.LadderPlacement`, `.Blocks.PopDrops`.

## 5. Known issues (when)

1. Climb ascent 0.2/tick, the feet cell and the trapdoor rule: provisional until a movement probe (Jesus's go).
2. Pops roll with the empty hand (B4a): until shears are imported.
3. Blocks without a 26.3 table drop themselves (doors, wall torches): B8.
4. A reload while a button is pressed: block ticks are not saved.
5. Any RunChecks run (flake): the voxel step's `ReadFeatures` crash (B3a); re-run once before suspecting code.
6. Stuck button after column unload: on SM-Codex's QC list, not a B5 task.

## 6. What worked, what failed (B4b)

- **Worked:** the probe copy (a Python rewrite of the switch probe) ran in 30 s and settled the ladder's odd case (a
  ladder clicked on a ladder's front face places nothing) without reading code; testing the climb in a real
  `FFoundationBlockWorld` (`GatherCollision` + `IsClimbable`) instead of stub boxes; the existing pinned movement suites
  as the "unchanged" oracle (M8); bundling mutations by target test.
- **Failed or slow:** Python heredocs in the Bash tool turned `\n` in C++ string literals into real newlines (write the
  script to a file, or fix with the Edit tool); the probe's `C` part had to be cut by hand; R13's vines differ only in
  box order (dropped from the oracle after a check); `Families.Place` places every family block on a floor, so blocks
  that cannot stand on one (ladder, weeping vines) need an explicit exception; a `while` loop in a test without a cap
  (the review caught it).

## 7. Token figures (B4b)

- **Lead:** 120.1k at the plan, 271.5k before the review; report A has the final figure (session
  `751ad8f5-59aa-58e8-8d15-7f66a8f5d0c8`).
- **Subagents:** pre-commit review (Sonnet 5.5) about 108k.

## 8. Evidence

- `C:\dev\sandbox\b4b-blocks\logs\`: build-1/2, tests-1/2, build-mut1/2, tests-mut1/2, runchecks-final-1.
- `C:\dev\sandbox\b4b-blocks\probe\` (work, work2, ladder-run2.json), `...\review\b4b-diff.patch`, `...\scripts\`.
- RunChecks final: see report A.

## 9. Open questions

1. **Push of `feature/survival-blocks`:** Jesus's call.
2. **Branches `wip/b4a`, `wip/b4b`:** kept after the squash; deleting them needs the PM.
3. **A black-box movement probe for climbing** (the ascent, the trapdoor rule): Jesus's go (recommended: keep VALUES
   numbers until QC shows a feel problem).
4. **CLAUDE.md's importer commands** use relative `-script=` paths (B1 known issue 7): Jesus's file.

## Short prompt for the next chat

> B5: farmland, dirt path, hoe and shovel use (survival track T5), lane `C:\dev\projects\Unnamed-blocks`, branch
> `feature/survival-blocks` at the B4b handoff commit. Start with `Docs/Records/2026-10-15-Blocks-Handoff-B5.md`: read it
> whole, then its reading list. SM-Blocks run 2 (`session_01DUpWqKA5aBjmdjZtn1enW6`) manages T5: report there. Report
> your plan first: every file against the hot files, whether any hash, golden or saved-world number moves, and the test
> plan. Wait for the go. First person only, no hands.
