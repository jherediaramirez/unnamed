# Use lane handoff: step 14, axe stripping

From step 10 (eating, commit `4c359f6` on `feature/survival-use`, worktree `C:\dev\projects\Unnamed-use`). Survival program track T3. Steps after this one on the lane: 17 (buckets), then 16 (survival fixes).

## 1. Reading list, in order

1. `C:\dev\sandbox\scout-player\plan.md`: lines 1-25 ("Every step") and the step 14 section (search for "Step 14").
2. `C:\dev\sandbox\scout-player\facts.md`: "Using items (right-click)" (lines 127-136).
3. [2026-10-14-Use-Eating.md](2026-10-14-Use-Eating.md): sections 1, 2, 4 and 6 (what the use path is, its decisions, its known issues, the code map).
4. `Docs/Items.md`, "Eating" (the switch and the client pipeline that stripping joins).
5. [2026-10-13-Sound-Reference.md](2026-10-13-Sound-Reference.md), "For the steps that use it" (lines 40-55): `item.axe.strip` (in `Reference/minecraft-sounds-26.3/sound_events.json`).
6. Code:
   - `Source/Unnamed/Foundation/FoundationAuthority.cpp`, `UseOn`: step 2, where a non-placeable item passes today. Stripping goes in there, before the pass.
   - `FoundationInteraction.cpp`, `UsePipeline` and `UseTarget`: the per-hand order (target use, then item use).
   - `FoundationAuthority.cpp`, `BreakBlock`: how wear is applied (`Inventory.Wear`, `Options.bToolWear`). A strip costs 1 durability (wiki "Axe"; verify).

## 2. Working agreement (unchanged from the lane's brief)

- Gameplay only through `FFoundationAuthority` commands.
- Each step gets a switch; 0 = today. Stripping hangs off `foundation.Items.Use` (decided at step 10); no new switch unless the manager asks.
- A plan first, listing every file against the hot files. Diff the hot files against the latest `feature/survival` before planning.
- A Sonnet 5.5 pre-commit review of the diff only.
- `Scripts\RunChecks.ps1 -Profile final -Flight smoke` after the last code change.
- Reports go to the manager session; MeasureAgentUsage after every build, test run and subagent result.
- No `DA_Items` or `DA_Blocks` rewrite this stretch. Never push. No bare `git stash`.

## 3. Settled decisions

- Hold-to-use (eating) uses its own commands (`UseItemStart/Continue/Release`). Instant uses on a block (stripping, later the bucket's place and pick) go through `UseOn`, whose result already carries the cell and the state.
- Food data is a native table pinned against items.json. Do the same for stripping pairs if needed: a table pinned against the block reference (`Reference/minecraft-blocks-26.3/block_manifest.json` lists the 38 `stripped_*` blocks, all imported). Vanilla's strip map is in game code only, so cite the wiki "Axe" page (the log → stripped log, wood → stripped wood, stem/hyphae and bamboo block pairs) and keep the axis property.
- Vanilla first: when a call is easy to change later, take the value closer to vanilla Java and say so in the report.

## 4. Code map (step 10's additions)

- `FoundationAuthority.h`:
  - `FFoundationLabOptions::bItemUse` (the pawn copies `foundation.Items.Use` into it);
  - `FoundationItemUse::IsOn`;
  - `FFoundationItemUse` on `FFoundationPlayer::Use`;
  - events `Eating` and `Ate`.
- `FoundationAuthority.cpp`: `CVarItemUse`; `UseItemStart`, `UseItemContinue`, `CancelUse`; the cancels in `Step()` and `Execute` (select, swap).
- `FoundationItems.h/.cpp`: `FFoundationConsumable`, `FoundationConsumables` (the pinned table pattern to copy).
- `FoundationInteraction.cpp`: `StepItemUse`, `ReleaseItemUse`, `UsePipeline` → `UseTarget`.
- `FoundationInteractionView.cpp`: `HandleEvents` (the `PlayEvent` calls), `UpdateHeldItem` (eating pose).
- `FoundationController.cpp`: `NoteInteraction`'s event-name table. A new event kind must be added there; its `static_assert` pins the last kind (it crashed once at step 10).
- `FoundationInteractionProbe.cpp`: scenario 13c (eating), the pattern for a runtime case plus a capture pair with hold cases.
- Tests: `Source/Unnamed/Tests/FoundationEatingTests.cpp` (`FEatingLab`: the authority + client lab; route direct commands through `I.NextSequence` so they never collide with the client's sequences).

## 5. Known issues, with when

- No eat particles (the break chips draw block textures only). When: a crumb pass, or step 16.
- Food table not in `DA_Items`. When: an `ImportItems.py` migration once no other lane touches the item assets.
- Dropping one item from the stack being eaten keeps the meal going. When: step 16 (survival fixes), after checking vanilla.
- No motion for an offhand meal. When: when the offhand is drawn.
- Provisional until a vanilla capture (Jesus's go): the 20% slowdown, the eat and burp volumes and pitches, the effect cadence, the creative exception, and the eating pose. The pose is accepted as provisional; a stronger move to the centre is one constant away (`EatBlend` offset).

## 6. What worked and what failed

- Worked:
  - Python patch scripts with exact-match asserts (`C:\dev\sandbox\use-lane\patch_*.py`, written with `newline=''` to keep LF endings).
  - `C:\dev\sandbox\use-lane\run_items.ps1`: build plus one native filter.
  - `mutate.ps1`: one mutation, built, run, restored byte for byte.
- Failed:
  - A bash heredoc with apostrophes broke the Bash tool; write scripts with the Write tool.
  - Python text-mode writes turned LF files into CRLF.
  - `TPair` brace-init in a static table needs explicit `TPair<...>(...)`.
  - The first runtime run crashed on the controller's event-name table.
  - The first client test shared sequence numbers between direct commands and the client.
  - A PowerShell here-string commit message piped to `git commit -F -` was read as a pathspec; use a message file.
- Under other lanes' compiles the interaction suite ran at about 39 fps, and the wall-torch mining case failed once (it passed at 43 fps in the final run).

## 7. Token figures

Lead 306k at report A (MeasureAgentUsage, session 74e4a6c9). The Sonnet review subagent peaked at 113k, over its 100k brief; brief the next review tighter, or split it past about 1,000 diff lines.

## 8. Evidence

- `Tests/Evidence/2026-10-14/Checks-final-20261014-124859/summary.txt`: green; native 332, interaction 94 checks, runtime-all 139.
- Capture pair: `Tests/Evidence/2026-10-14/Playground-interaction/Runtime-20261014-124114/runtime-60-interaction-eating-on.png` and `-eating-off.png`.
- Mutation runs: `C:\dev\sandbox\use-lane\runs\mut-*`.

## 9. Open questions (with options)

- Does stripping need its own switch, or `foundation.Items.Use` alone? Recommended: the shared switch (decided at step 10). Alternative: `foundation.Items.Strip`.
- The strip map's source: a native table pinned against the stripped blocks in the block reference, citing the wiki (recommended), or a reference rebuild (heavier; Jesus's call).
- The push of `feature/survival-use` is Jesus's call; the merge go comes from the manager after T1 and T4.
