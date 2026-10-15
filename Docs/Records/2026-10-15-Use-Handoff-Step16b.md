# Use lane handoff: step 16b, survival fixes (pick block, Codex review 9, the bucket held repeat)

From step 16a (lava, burning, the swim pose, fall rounding, Codex review 13), lane `feature/survival-use`, worktree `C:\dev\projects\Unnamed-use`. Survival track T3. **Base: the 16a commit on `feature/survival-use` (hash in report B; `git log -3` shows "survival step 16a").** The sub-manager is "SM-Player" (session `session_01C87ZqKQ2vSBHdtPkSU7KJn`); reports go there.

## Short prompt

> Survival program, track T3, step 16b: survival fixes. Lane `C:\dev\projects\Unnamed-use`, branch `feature/survival-use`. Read `Docs/Records/2026-10-15-Use-Handoff-Step16b.md` first and follow its reading list. Three fixes: (d) pick block from the main inventory (`foundation.Items.Pick`), (e) Codex review 9's two findings, (f) a test for the bucket's full-inventory held repeat. The plan is approved (T3-use.md, "T3 step 16", section 16b): re-check it against the latest `feature/survival` hot files, report to SM-Player and wait for the go.

## 1. Reading list, in order

1. This file.
2. `Docs/Records/2026-10-14-Use-Handoff-Step16.md`: sections 2 (scope (d)-(f)), 3 (working agreement), 4-5, 7 (what worked and failed). Its sections 1.4-1.6 are the 16b reading list: value rows (`value-swaps.md` line 234; `survival.r2.pick`, `survival.r2.rules`, F 2608-2623 in `SURVIVAL-FINDINGS-26.3.md`), Codex review 9 findings 1-2, `FoundationInteraction.h` `Pick`, `FoundationInteractionView.cpp` `HandleEvents` (`Eating`/`Ate`), `Tests/FoundationBucketTests.cpp` (`FBucketLab`, the client test under both presets).
3. The approved plan: `C:\dev\tools\shift-managers\survival\tracks\T3-use.md`, "T3 step 16 (survival fixes): approved plan", sections 16b and Settled.
4. `Docs/Records/2026-10-15-Use-SurvivalFixes-16a.md` section 3 (what 16a left).

## 2. Working agreement (unchanged, plus)

- As step 16's handoff section 3. Every RunChecks: `Scripts\RunChecks.ps1 -Profile final -Flight smoke -ExpectedHashes C:\dev\tools\shift-managers\expected-hashes-main.txt`.
- 16b touches hot files (`FoundationAuthority.h/.cpp`, `FoundationInteraction.cpp`, `Docs/Items.md`): diff them against the latest `feature/survival` first.

## 3. Settled decisions (16a added)

- `foundation.Fire` and `foundation.SwimPose` default 1; their 0 is the old behaviour.
- Codex review 13 fixes have no switch (bug fixes). A pour's replaced block pops its drop only with item entities on.
- The CLAUDE.md hash-checked list now names the entities and sounds references (Jesus's tap); no further CLAUDE.md edits.

## 4. Code map (16a's additions)

- `FoundationVitals.h/.cpp`: `EFoundationDamage::Lava`/`OnFire`, `FireTicks`, `LavaHurts`/`BurnHurts`/`Extinguishes`, the fire block in `Step`.
- `FoundationSimulation.h/.cpp`: `bSwimming`, `bLowPose`, `GetPoseEyesCm`; `FFoundationEligibility::bSwimPose`. `FoundationProfile.h`: `SwimmingHeightCm`, `SwimmingEyesCm`.
- `FoundationPawn.cpp`: `foundation.Fire`, `foundation.SwimPose`; the lava fact in `StepVitals`.
- `FoundationInteractionView.cpp`: `PlayFireSounds`; the waterlogged non-model branch in `AddInstance`.
- `FoundationAuthority.cpp`: `RollBlockDrops` (shared with `BreakBlock`); `UseBucket`'s `TakesWater` (no double slab) and `IsReplaced` (washed away by fluids).
- `FoundationBlockRegistry.cpp` `GetFluid` (waterlogged property), `VoxelBlocks.cpp` (`HeldFluid` for any state), `VoxelMesher.cpp` (`EmitFluid`).

## 5. Known issues, with when

- 16a's list: `Docs/Records/2026-10-15-Use-SurvivalFixes-16a.md` section 3.
- Step 16's handoff section 5 (step 17, 10b, 14) still holds, except a pour's replaced plant now drops with item entities on.

## 6. Token figures

16a lead: see report B. The 16a Sonnet 5.5 pre-commit review peaked at about 93k for 1,044 diff lines.

## 7. Open questions (with options)

- **Push of `feature/survival-use`:** Jesus's call.
- **Slab water sides:** keep (vanilla-like look unverified) or inset the water's side quads inside a waterlogged slab's cell; recommend a Playground look check first.
