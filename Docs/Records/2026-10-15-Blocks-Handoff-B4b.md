# Blocks B4b handoff: climbing, ladders and vines (survival track T5)

Lane `feature/survival-blocks` (worktree `C:\dev\projects\Unnamed-blocks`). B4a is [its record](2026-10-15-Blocks-B4a.md);
this handoff replaces the climbing parts of [the B4 handoff](2026-10-15-Blocks-Handoff-B4.md). B4b is climbing for
`#minecraft:climbable`: ladder placement and survival, the fall-speed clamp, climbing on push or jump, holding on sneak,
fall distance zeroed, an open trapdoor over a same-facing ladder. Switch `foundation.Blocks.Climb`. **First person
only, no hands** (no arm, no third-person pose).

**Manager:** SM-Blocks run 2 (session `session_01DUpWqKA5aBjmdjZtn1enW6`); report there, not to Jesus.

## 1. Reading list (in order)

1. `C:\dev\tools\shift-managers\survival\tracks\T5-plan-B4.md`: section 2 (the value table), 3 "(d)" and 4 "B4b", 6
   "(d)", 7 and 8 (risks 6). Read-only research with file and line pointers at ab28a230; re-check lines.
2. [The B4a record](2026-10-15-Blocks-B4a.md) (about 60 lines), whole; [the B3b record](2026-10-15-Blocks-B3b.md)
   section on placement by looks.
3. [The B4 handoff](2026-10-15-Blocks-Handoff-B4.md) sections 1 (reading list items 3-6), 3 (settled decisions) and 4
   (code map); its working agreement is repeated below.
4. `Docs/Blocks.md` "Buttons and levers (B3b)" (the wall-placement pattern a ladder reuses), "Placement" and "Pop
   drops, the firefly bush and chests (B4a)".
5. `Source/Unnamed/Foundation/FoundationSimulation.cpp`: grep the gravity and `FallDistanceCm` lines (plan: about
   :267, :270-290). T4 owns it: diff against `feature/survival` first and ask SM-Blocks (who asks the T4 SM) before
   editing. `FoundationVitals.cpp` `StepHazards` (:48) holds the second fall accumulator: zero both.
6. Values (grep, never read whole): `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md` lines 351-353
   (`movement.ladder`, `movement.ladder_clamp`: horizontal ±0.15, down -0.15 blocks/tick, sneak holds 0);
   `C:\dev\tools\shift-managers\survival\value-swaps.md` lines 125-126 (ladder 0.4, vine 0.2; ascent 0.2,
   source-inspected: provisional); `Reference/minecraft-blocks-26.3/facts/family_survival.json` (ladder signatures
   10-13, vine 30-57, twisting/weeping vines 88/89).

## 2. Working agreement

- **Plan first** to SM-Blocks: every file against the hot files (`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`,
  `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`, and
  `FoundationSimulation.cpp` for B4b); whether hashes, goldens or saved worlds move; the test plan. Wait for the go.
- **Checks:** `Scripts\RunChecks.ps1 -Profile quick|final` (`-Flight smoke` for the final). Until the PM updates
  `C:\dev\tools\shift-managers\expected-hashes-main.txt` at B4a's landing, pass
  `-ExpectedHashes C:\dev\sandbox\b4a-blocks\hashes\expected-hashes-b4a.txt` (r8 amplified moved with the firefly
  switch). Don't edit sources while a RunChecks run is going; clang-format before the final run.
- **Pre-commit review:** Sonnet 5.5 on the diff only (split by path list over about 1,500 lines); re-run what fixes
  touch.
- **Never:** push, use bare `git stash`, edit CLAUDE.md, re-record goldens or world hashes, hand-edit a DA_* asset.
- **Measure lead context** with `node C:\dev\sandbox\b0-sounds\MeasureAgentUsage.mjs --session <id>` (find the id by
  grepping `%USERPROFILE%\.claude\projects\C--dev\*.jsonl` for a phrase you wrote).

## 3. Settled decisions

- **First item: twisting and weeping vines** (SM-Blocks, B4a): they never pop today (no Foundation survival rule, not
  in `FoundationPlantRules::GetStack`). Wire their survival from `family_survival.json` signatures 88/89 and add them
  as stacks (twisting grows up, weeping hangs down); B4a's pop drops then apply to them as to cane.
- Family logic in `FoundationBlockFamilies.h/.cpp`; the authority and the simulation get call lines.
- A ladder places like a wall button (B3b): the first of `GetClickLooks` where it survives, sides only (measure with a
  copy of `Tests/Probes/SwitchPlacementProbe.java` parts A and B; public `useOn` only).
- Survival from `family_survival.json` (every state and candidate, as `.SwitchSurvival` does).
- Movement values from VALUES 351-353; ascent 0.2/tick ships provisional (a row in `provisional-values.md`), no
  movement probe (SM-Blocks' Q2). Which cell counts as "in a climbable" (feet cell) and the trapdoor-over-ladder rule
  are wiki-only: provisional.
- Climbable blocks come from the tag `Block.Climbable` (`Config/Tags/MinecraftBlockTags.ini:51`), never a hard-coded
  list; check scaffolding against the 26.3 tag file.
- `ScheduleBlockTick` (B3b) exists for delayed updates; vines' random spread is B6's.
- **B4a, done and not to redo:** pop drops (empty hand, before the block goes), the firefly bush as short grass
  (`voxel.Decoration.FireflyBushAsGrass`). Chest no-mesh was dropped by the feature freeze (deferred with the lid
  animation, after QC): do not rebuild it in B4b. T1's step 18 (chests) will add one-line hooks
  in `FoundationBlockFamilies.cpp` and a `bSecondaryUse` field in `FoundationBlockPlacement.h`: take them as they come.

## 4. Code map (after B4a)

- **Pops:** `FFoundationAuthority::SettlePlants` (`Pop(At, bRoll)`, `PopOtherHalf`, `Settle`, the stack walk),
  `SettleAfterFluid`, `RollBlockDrops`, file-local `FindAuthorityLootTable`.
- **Families:** `FoundationBlockFamilies` (`GetFamily`, `GetStateForPlacement`, `GetSurvival`, `IsFaceSturdy`,
  `UpdateNeighbourShapes`); plant stacks `FoundationPlantRules::GetStack`.
- **Movement:** `FoundationSimulation.cpp` `Step` (T4's), `FoundationVitals.cpp` `StepHazards`; `Facts.bClimbing`
  exists only as a fact today.
- **Decoration switch:** `FVoxelDecorationOptions::bFireflyBushAsShortGrass`, `VoxelConsole::CVarDecorationFireflyBushAsGrass`.
- **Tests:** `Unnamed.Foundation.Blocks.PopDrops` (FoundationInteractionTests.cpp), `Unnamed.Foundation.Interact.*`
  (B3), `Unnamed.Voxel.Plants.FireflyBushAsGrass`.

## 5. Known issues (when)

1. Twisting and weeping vines never pop: B4b's first item.
2. Pops roll with the empty hand (provisional); untestable against a held tool until shears are imported.
3. Blocks without a 26.3 table drop themselves (doors, wall torches): B8.
4. A hand-placed firefly bush draws black in voxel worlds: when the bush returns (fireflies may come back as an
   ambient effect).
5. A reload while a button is pressed: block ticks are not saved.
6. Any RunChecks run (flake): the voxel step's `ReadFeatures` crash (B3a); re-run once before suspecting code.
7. Stuck button after column unload, on the QC list (Codex 33 #1; SM-Codex's QC-KNOWN-BUGS.md, not a B4b task).

## 6. What worked, what failed (B4a)

- **Worked:** reading the lane through `git show ab28a230:<path>` while B3b's merge was pending; a compile-time state
  swap (`ReplaceState`) instead of a placement-time hook kept every random stream; separate headless hash runs at
  switch 0 and 1 (`C:\dev\sandbox\b4a-blocks\hashes\hashrun.ps1`) gave the control and the moves in one go; the B3a
  re-import recipe plus a T3D line diff proved `DA_Blocks` moved only by the flag (and, after the freeze, that it is
  back to byte-identical); an offline Python scan of
  `block_models.json` answered "any other two-layer plant?" in seconds.
- **Failed or slow:** a redirect into a sandbox folder that did not exist failed the first baseline (create the folder
  first); Python string escapes ate a Windows path in Docs/Blocks.md (`\b`, `\f`, `\t`: use raw strings); the first
  PopDrops run had two setup errors (hardness-0 blocks break on the first command, so `MineSteps` returns 0; shears are
  not imported); `Mesher.Digest` pins its values in source, so following a worldgen switch there would have meant
  hand-editing goldens (it now forces 26.3 decoration).

## 7. Token figures (B4a)

- **Lead:** 93.1k at the plan, 284.8k at the hash checkpoint; report A has the final figure (MeasureAgentUsage,
  session `074180a3-77fa-5909-94e9-066483b13997`).
- **Subagents (Sonnet 5.5):** B4a pre-commit review 91.9k; B3b merge review 62.1k; see report A for any later.

## 8. Evidence

- `C:\dev\sandbox\b4a-blocks\hashes\`: `switch0\`, `switch1\` (hashes.txt, native.log, machine.txt), `hashrun.ps1`,
  `expected-hashes-b4a.txt`, `expected-hashes-b4a.diff`.
- B3b landing merge: `Tests\Evidence\2026-10-15\Checks-merge-20261015-142951\summary.txt` (GREEN), HEAD 5d271969.
- `C:\dev\sandbox\b4a-blocks\import\`: `t3d-diff.txt`, `t3d-summary.txt`, `blocks1.log`, `blocks2-verify.log`,
  `assets-pre-b4a\`, `textures-b4a-import\`.
- `C:\dev\sandbox\b4a-blocks\firefly\two-layer-models.txt`; `C:\dev\sandbox\b4a-blocks\logs\` (builds, native-1/2,
  quick-baseline, final-1); `C:\dev\sandbox\b4a-blocks\review\b4a-diff.patch`.
- RunChecks: baseline quick `Tests\Evidence\2026-10-15\Checks-quick-20261015-133939\summary.txt`; final: report A.

## 9. Open questions

1. **Push of `feature/survival-blocks`:** Jesus's call.
2. **Shared expected hashes:** the PM updates `expected-hashes-main.txt` (r8 amplified `0xdbc9e9ea53ee788b`) at B4a's
   landing (PROGRAM.md 5). Until then lanes keep main's value; B4b uses the lane file.
3. **Branch `wip/b4a`:** kept after the squash; deleting it needs the PM.
4. **CLAUDE.md's importer commands** use relative `-script=` paths that do not resolve (B1 known issue 7): Jesus's file.
5. **A black-box movement probe for climbing** (ascent, trapdoor over ladder): needs Jesus's go. Options: run it in
   B4b (exact values), or ship VALUES numbers with the ascent provisional (recommended, SM-Blocks' Q2).

## Short prompt for the next chat

> B4b: climbing, ladders and vines (survival track T5), lane `C:\dev\projects\Unnamed-blocks`, branch
> `feature/survival-blocks` at the B4a handoff commit. Start with `Docs/Records/2026-10-15-Blocks-Handoff-B4b.md`: read it
> whole, then its reading list. SM-Blocks run 2 (`session_01DUpWqKA5aBjmdjZtn1enW6`) manages T5: report there. First
> item: twisting and weeping vines' survival and stacks. Report your plan first: every file against the hot files
> (including `FoundationSimulation.cpp`), whether any hash, golden or saved-world number moves, and the test plan. Wait
> for the go. First person only, no hands.
