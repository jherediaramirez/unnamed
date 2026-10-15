# Blocks B2 handoff: shapes and connections (survival track T5)

Lane `feature/survival-blocks` (worktree `C:\dev\projects\Unnamed-blocks`). B1 (the reference extension) is commit `f32f120` on this branch, a squash of `wip/b1-park` (the WIP branch stays; never delete it) on top of `feature/survival` 896f2d3. B2 is [the blocks plan](2026-10-14-Blocks-Plan.md) section 1's B2 row.

**This is the track's resume point.** After B1 landed, the program wound down (Jesus, October 14): no block step starts until a manager is assigned again. Whoever resumes T5: first check with the survival program's home (`C:\dev\tools\shift-managers\survival\`, `PROGRAM.md`) who manages the track now (it was the SM-Blocks chat, session `session_01AmANvUc5PUpNmYL3yAvm4F`), and report there, not to Jesus. Then merge the newest `feature/survival` into this lane once (never rebase or force), rebuild with `-DisableAdaptiveUnity`, and run `Scripts\RunChecks.ps1 -Profile quick` before planning. B3 (interactive blocks) and B4 (climbing) do not depend on B2's code, but the plan's order is B2 first.

## 1. Reading list (in order)

1. [The blocks plan](2026-10-14-Blocks-Plan.md) section 1 (the B2 row, the hot-file column, the paragraph under the table: logic in new `FoundationBlockFamilies.h/.cpp`, single call lines in the authority).
2. [B1's record](2026-10-14-Blocks-B1.md) whole (about 80 lines): what exists now, the checks, known issues 1 and 6.
3. `Docs/Blocks.md` section "Survival family blocks (B1)" (grep the heading).
4. `C:\dev\sandbox\scout-blocks\facts.md` sections 4.1-4.3 (stairs, fences/walls/panes/bars, gates: behaviour in our words), 5 (test oracles), 7 (code sketches).
5. `Reference/minecraft-blocks-26.3/tools/FactsDump.java` (559+ lines; grep `dumpSupport`, `survivalWorld`, the `LevelReader` proxy): B2 adds a table of `BlockState.updateShape` answers through the same public proxy.
6. Values: `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md` (grep, never read whole, never open Appendix H paths; cite evidence ids) and `C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md` (grep; R5 sound volumes and pitches, R9 buttons and switches matter for B3: note them there). Other wiki values go in `C:\dev\tools\shift-managers\survival\provisional-values.md`.
7. `C:\dev\tools\shift-managers\survival\PROGRAM.md` sections 4-6.

## 2. Working agreement

- Plan first to SM-Blocks: every file against the hot files (`FoundationAuthority.h/.cpp`, `FoundationItems.h/.cpp`, `FoundationInteraction.cpp`, `FoundationPanels.cpp`, `Hud/*`, `Docs/Items.md`, `Docs/Records/README.md`), and whether hashes, goldens or saved worlds move. Wait for the go.
- Switch `foundation.Blocks.Shapes` (0 = B1's behaviour: default states, no neighbour updates, in-cell collision gather).
- Hash-checked references change only through their own `tools/build_reference.py` (move the accepted folder aside, never delete; new entries last; append proof, regeneration proof, verifier). A reference tool or README edit changes `SHA256SUMS.txt`, which `DA_Blocks` (and `DA_Items` for the items reference) digest: plan a re-import after any such edit.
- Importers: use absolute `-script=` paths (relative ones resolve against the engine's folder). An importer that refuses is moved past by moving the named asset aside to `C:\dev\sandbox\...` (never deleted), then a T3D before/after proof (`C:\dev\sandbox\b1-blocks\export_t3d.py`, `proof\t3d_proof.py`).
- Checks: `Scripts\RunChecks.ps1 -Profile quick|final|merge` (`-Flight smoke` for the final); `Scripts\QuietWindow.ps1 -Name <what>` before any heavy launch outside RunChecks. A source edit after a RunChecks build makes its time chain red: re-run.
- Pre-commit review by Sonnet 5.5 subagents on the diff only (split over ~1,500 lines; the reference tools apart from the game code); re-run every suite the fixes touch. Report A, the SM's commit approval, commit, handoff, report B. Never push. No `git stash`; no CLAUDE.md edits; never re-record goldens or world hashes. Subagents: no deletes, including their own scratch files (two B1 subagents slipped).
- Measure lead context with `node C:\dev\sandbox\b0-sounds\MeasureAgentUsage.mjs --session <id>` (the newest `.jsonl` in `%USERPROFILE%\.claude\projects\C--dev`).

## 3. Settled decisions

- B1's blocks place their default state; B2 adds `GetStateForPlacement` for stairs (facing, half, shape), fences, walls, panes and bars (connections), gates (facing), the one-ring neighbour pass `UpdateNeighbourShapes` beside `SettlePlants`, and both collision gatherers one cell lower (1.5-tall fences, walls, closed gates; the pitcher crop).
- The oracle is a new FactsDump table of `BlockState.updateShape` answers (public API, the same `LevelReader` proxy), appended to the blocks reference through its tool.
- With `foundation.Blocks.Families 0`, the 9 sapling items stay in the creative inventory (as before B1) and placing them is refused.
- B3's dependency on T1 step 7 is met (`feature/survival` 896f2d3, merged into B1): the block-use hook is `FFoundationAuthority::RegisterBlockUse(BlockId, handler)`, documented in `Docs/Items.md` "The crafting table and block use".
- With `foundation.Blocks.Families 0`, recipes whose result is a B1 block's item craft nothing (`MakeContainerRules`; the 2x2 grid and the crafting table share that path).
- Loot of the B1 families waits for each family's session (`Reference/minecraft-items-26.3/tools/loot_later.txt`); remove an id from that file only by appending its table last.

## 4. Code map (B1)

- Switch and flag: `FoundationBlockRegistry.h/.cpp` (`FoundationBlockFamilies::AreEnabled/IsHidden`, `bAppendedFamily`); guard `FoundationAuthority.cpp` `CheckPlacement`; creative filter `FoundationPanels.cpp` `MakeCreative`.
- Gallery tails: `AFoundationPlayground::GetGalleryCells`; the tour frames from it (`FoundationTourProbe.cpp`).
- Model data: the registry accepts 0-part states (bare walls); `VoxelMesherTests` exempts exactly those.
- Collision: `Blocks.PlantShapes` allows only B1 fences, walls, gates and the pitcher crop out of their cell (pin 127,524 boxes): B2 replaces the allowlist with real out-of-cell gathering and moves the pin.
- Reference: `build_reference.py` (`APPENDED_BLOCKS`, stand-ins, composite icons), `bake_models.py` (uvlock, `bake_composite`), `FactsDump.java` (`dumpFamilySurvival`, `dumpUseSounds`).

## 5. Known issues (when)

1. **Now (B2's first check):** the uvlock rule for partial faces (`bake_models.py`, the uvlock fallback) composes the face's own UV transform and the lock rotation in one order; the whole-face branch uses the other. For a partial face with a rotation or flip, the result may differ from vanilla. Compare a rotated stair, wall and fence against vanilla screenshots or a hand-computed case before building on it (pre-commit review, tools finding 2).
2. **Now:** fences, walls and closed gates collide 1.0 tall; nothing connects; doors are one lower half.
3. **B6:** stems draw their age-0 tint at every age (one colour per block in the importer).
4. **Now:** the water cauldron is one translucent block (accepted).
5. **Any verifier run without the jar:** the blocks verifier skips the stand-in re-derivation and re-bake silently; it should require them.
6. **Notes from B1's reviews, not fixed:** the texture "earlier" set is proven per build by the append proof, not frozen in the tool; the survival null-row assumption (a state surviving alone and next to stone survives anywhere) is unchecked for B1's own blocks; `get_flattened_data` needs a recent Pillow; the tour's gallery framing moved half a cell even with the switch at 0; flipping the switch to 0 at runtime blanks held B1 stacks in the creative screen; the registry accepts 0-part states for any block.
7. `sounds-batch.md:8` misspells the blast furnace event (the program's file).
8. The blocks README says only doors are checked by `type()`; since B1's review FactsDump also throws on any set whose sound type differs (no other family class has a public `type()` in 26.3).

## 6. What worked, what failed (B1)

- Worked: one subagent per deliverable (data, build, install, cascades, imports, tests), each with exact files and a stop; proofs as sandbox scripts (`C:\dev\sandbox\b1-blocks\proof\`); WIP commits on `wip/b1-park` at each safe point, so a landing could park the work.
- Failed or slow: importers refused five times, each a separate round trip (the stand-in path, the stale tags, the bamboo particles, `T_BreakCracks`' digest, the item arrays). For B2: dry-run each import data module (`BlockImportData.py`, `BreakImportData.py`, `SoundImportData.py`, `ItemImportData.py` run as scripts) before the editor. Any change to `block_models.json` re-gates `DA_BreakAssets` and `T_BreakCracks`.

## 7. Token figures (B1)

Lead about 400k at this handoff (the SM allowed past 300k for the bed icons and the 079fe3e merge). Subagents peaked 64k-217k; the longest was the tests worker (217k, many native runs).

## 8. Evidence

- `C:\dev\sandbox\b1-blocks\proof\` (append, regen, items, sounds, t3d proofs; `pins.md`), `logs\`, `t3d\before|after\`, `assets-before\` (pre-B1 assets), `accepted-*` (moved references).
- The final RunChecks summary is in B1's report B.

## 9. Open questions

1. Push of `feature/survival-blocks`: Jesus's call.
2. `CLAUDE.md`'s importer commands use relative `-script=` paths that do not resolve: fix the doc (Jesus's file)?
3. The uvlock composition (known issue 1): fix in B2's first commit (recommended) or leave until a visual check shows it?
