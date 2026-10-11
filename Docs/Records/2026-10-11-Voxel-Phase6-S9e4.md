# Voxel world phase 6, S9e part 4: the chi-square's sparse bins merged; the mangroves parked off - October 11, 2026

S9e part 4 ([its handoff](2026-10-11-Voxel-Handoff-Phase6-S9e4.md)) was to refit the mangroves' root walk for the moss, merge sparse bins in the chi-square and switch the mangroves on. The merge landed. The moss fit was stopped unfinished by a PC shutdown, and then Jesus said "Commit it as with mangroves off". `IsMangroveBuilt()` stays false, the world is unchanged and the phase 6 chain is paused for player fixes.

## 1. The sparse-bin merge (the manager's comp6 call (a))

- **The rule** (`VoxelTreeTests.cpp` `ChiSquareHomogeneity`, every caller): bins are taken in key order and join a group until both histograms' expected counts reach 5 (Cochran's rule). A short last group joins the one before. With fewer than two groups there is no test. The callers are the top-log histogram, the 6-connected parts, log directions (shown per probe, and the pooled fallen test through `OutChi2`), the mushroom caps and the forest rock's cells.
- **Forest rock** keeps its local bins of three cells (F3b's, inside `CompareBlob`); the shared rule merges further on top of them, which is why its p moved (below).
- **The lines** print chi-square p to 3 places, and the parts line shows mean parts ours/probe (`GetHistogramMean`).
- **Mutations:**
  - 16, the unmerged test, gives the before runs and S9e part 3's verdicts;
  - 17 sets upwards_branching's `place_branch_per_log_probability` to 0.8 times, a fifth fewer mangrove branches.

## 2. Before and after (mutation 16 against the merged run, the same code)

Tables: `C:\dev\sandbox\phase6s9\s9e4\comp6_table_off.md` and `comp6_table_on_f6a.md` (`comp6_table.py BEFORE AFTER`).

| State | p values | Changed | Verdict flips | Runs (`C:\dev\sandbox\phase6s9\runs\`) |
|---|---|---|---|---|
| Mangroves off (committed) | 94 | 17 | 0 | `s9e4-mut16-20261011-074105` against `s9e4-merge-20261011-073957`, both 8/8 |
| Mangroves on (S9e part 3's walk, temporarily) | 108 | 31 | 1, the target | `s9e4-mut16-20261011-074317` against `s9e4-on-merge-20261011-074237` |

- **The target:** the accepted mangrove@mudwater's 6-connected parts went from 0.010 (failing) to 0.560.
- **Nearest the bar:** fancy_oak_bees_0002_leaf_litter's parts went from 0.224 to 0.015, though the means are 3.33 against 3.34; it still passes.
- **Other changed lines, all passing:**

  | Probe | Line | Before | After |
  |---|---|---|---|
  | forest_rock | cells | 0.521 | 0.898 |
  | mega_jungle_tree | parts | 0.283 | 0.124 |
  | mega_jungle_tree | top | 0.367 | 0.760 |
  | fancy_oak | parts | 0.184 | 0.424 |
  | huge_brown_mushroom | cap top | 0.522 | 0.302 |

  The rest are in the tables.
- **Mutation 17,** with the mangroves on: the tall pool's mean parts fall from 6.35 to 5.34 (probe 6.48, -16%).
  - Merged comp6 fails tall_mangrove pooled at p 0.007; the mud pool gives 0.037 and tall@mud 0.028.
  - The same mutation unmerged gives 0.013, 0.100 and 0.088, so the merge gained power on the pools.
  - Logs and leaves fail first on those pools.
  - Runs: `s9e4-mut17-20261011-074427`, `s9e4-mut17-unmerged-20261011-074526`.

## 3. The mangroves, parked off

- **The moss** is still not fitted. With the mangroves on, only the two moss lines fail now: mangrove@mudwater pooled -13.5% and tall_mangrove pooled +7.5%. The parts line passes under the merge.
- **F7** (Sonnet 5.5, peak 98.9k) worked in `C:\dev\sandbox\phase6s9\s9e4\fit\`, a copy of F6a's folder plus F6b's `scan2.py` and `search.py`.
  - It ran random searches (`s_a.log` to `s_d.log`) over F6b's descent-slope family, adding a ground slope (`g_slope`), `share_pow`, `ring_cut` and `n_slope`. Its edited `roots_f.py` holds `g_slope`.
  - Best loss was about 50, never checked against the eight lines. It wrote no report, and its last search was killed at the shutdown.
- **To resume:**
  - a fresh fit agent, starting from F7's folder and logs and F6b's report (`s9e3\fit2\b\moss2-report.md`);
  - then the S9e part 4 handoff's section 4 from step 2;
  - the pinned 46 goes to 54 at the switch-on;
  - re-run mutations 16 and 17 on the final walk.

## 4. Suites

- **Tree tests**, mangroves off, after restoring `VoxelTrunkPlacers.cpp` (equal to HEAD) and a `-DisableAdaptiveUnity` build:
  - 8/8 (`C:\dev\sandbox\phase6s9\runs\s9e4-off-20261011-103054`);
  - the same 94 p values as the merged run.
- **The review** (Sonnet 5.5, the 252-line diff, peak 56.0k) found nothing: `C:\dev\sandbox\phase6s9\s9e4\review\review-findings.md`. It checked the merge's edge cases, mutation 16's equivalence to the old test, the `OutChi2` contract, the TMap use and the formats.
- **RunChecks final** against `C:\dev\tools\shift-managers\expected-hashes-main.txt`, with the flight smoke: GREEN in 19 minutes (`Tests/Evidence/2026-10-11/Checks-final-20261011-103228/summary.txt`).
  - Native 260 of 260 (1 with warnings), 0 crashes; TestVoxel 167 suites.
  - **The hashes: 9 of 9 equal.** The world is unchanged.
  - Playground: 119, 81 and 27 checks, 0 failures each.
  - create-world 32/32.
  - The flight smoke passed (`Tests/Evidence/2026-10-11/VoxelFlight-20261011-104839`, 125 s, no errors besides LogPython).
  - npm 61/61, source-check, all the reference verifiers.
  - Machine: Jesus's Minecraft (javaw, from 10:32:59) and two java processes ran during the voxel, hashes and Playground steps. Every step renders offscreen, so the Playground's fps figures (39 to 43) are not findings.

## 5. The manager's calls

- **Manager 6:**
  - the plan approved as written;
  - the table must cover every caller and flag any flip other than the target;
  - the comp6 mutation must name its size;
  - fancy_oak_bees_0002_leaf_litter is the line to watch;
  - the PC-shutdown stop.
- **Manager 7, carrying Jesus's words "Commit it as with mangroves off":**
  - the moss work stops (no new fit agent);
  - restore `IsMangroveBuilt()` to false and keep the merge and mutations 16 and 17;
  - RunChecks final against the main hash file (unchanged); no re-records, window, photos or gate9, the RunChecks flight smoke being enough;
  - one short Sonnet review;
  - a short record and a one-page handoff into S11. The phase 6 chain is paused; player fixes come next.

## 6. Commits

One commit holds the code (`VoxelTreeTests.cpp`), this record, the S11 handoff, `VoxelWorld.md` and the records index. Nothing was pushed (the manager pushes).

## 7. Known issues

| Issue | When |
|---|---|
| The mangroves are off: the moss's root walk is unfitted (section 3) | When the phase 6 chain resumes, before or with S11 (Jesus's call) |
| fancy_oak_bees_0002_leaf_litter's 6-connected parts sit at p 0.015 under the merge (means 3.33/3.34); this is the line to watch if a later change tips it | Watch |
| The S9e part 4 handoff's section 7 items (the end-below attachment, the re-probe spread, the S9 gate, part 2's list) | As listed there |

## 8. Tokens (session `7332b3fb-da2a-5a0a-bd3f-1272b26232dc`, measured)

| Agent | Peak context | Notes |
|---|---|---|
| Lead (Opus 5.5) | 204.5k at report A | 88.1k after the reading list, 144.3k after the comp6 work, 164.8k at the shutdown stop, 188.3k after the review |
| F7, moss fit (Sonnet 5.5) | 98.9k | Briefed for about 90k; stopped at the shutdown, no report |
| Review (Sonnet 5.5) | 56.0k | 252 diff lines, no findings |
