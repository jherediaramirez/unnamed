# Voxel world phase 6, S9e part 3: the mangroves re-probed into the reference and refitted, still off - October 11, 2026

The main lane's S9e part 3 ([its handoff](2026-10-11-Voxel-Handoff-Phase6-S9e3.md)) set out to switch the mangroves on. The re-probes are now in the worldgen reference through its own tool, and the test pools them. Two fit rounds (agents F5 and F6a) fixed every log, leaf, occupancy and propagule line. The moss did not fit (agent F6b), so `IsMangroveBuilt()` stays false and the world is unchanged. At the 300k checkpoint the manager chose to commit this and leave the rest to a part 4 ([its handoff](2026-10-11-Voxel-Handoff-Phase6-S9e4.md)): the root walk for the moss, a sparse-bin rule for the chi-square, then the switch-on.

## 1. The re-probes into the reference

- **The tool.** `tools/tree_probe.py` gained `--x-start`, `--level-seed` and `--run-tag r<N>`. The tag is required when either value differs from the accepted 1024 / 263, and refused when neither does.
  - A tagged run writes `tree_<probe>_r<N>.json`. Its "probe" field is `<probe>_r<N>`, and its `world` records `x_start` and `level_seed`.
  - Untagged output keeps the accepted format.
  - `read` checks every placement's x against the given start.
- **The verifier.** `tools/verify_reference.py`'s `check_tree_probe` accepts a `_r<N>` suffix only when the file's world records a pair other than the accepted one, and fails an untagged probe that records either. The accepted files take the same checks as before, plus that last condition (the manager's Q3).
- **The runs.** Three servers, one per run: r1 at x 9216 and seed 263, r2 at 25600 and 7001, r3 at 41997 and 90210; four probes each; 60, 61 and 63 s. Each ran behind `QuietWindow.ps1`, outside any window, on the tool's own sandbox server, with no java, javaw or UnrealEditor running (`C:\dev\sandbox\phase6s9\s9e3\probe\runs.log`).
  - Placed: mangrove 100, 100, 100; tall_mangrove 97, 90, 93; mangrove@mudwater 86, 89, 94; tall_mangrove@mudwater 33, 28, 33.
- **Into the reference.** `build_reference.py --probes C:\dev\sandbox\phase6s9\s9e3\probe\staging` copied the 12 files in, and `verify_reference.py` passes with 118 feature probes, 94 of them tree probes. The proof (`C:\dev\sandbox\phase6s9\s9e3\refproof.py`, output `refproof_out.txt`) shows:
  - git status over `Reference/` as a whole: 4 files changed (README, SHA256SUMS, the two tools) and 12 new;
  - 866 accepted files byte-identical with HEAD;
  - SHA256SUMS: new minus the 12 additions and the three edited files' lines equals old minus those lines; every kept line identical and in the same order;
  - `manifest.json` byte-identical (its counts cover `data/` only).
  - No index moved: the tool's probe order, which fixes each probe's strip, is unchanged.
- **The verifier's cases** (`vercases.py`, `vercases_out.txt`, on sandbox copies with SHA256SUMS rewritten as the build does):
  - well-formed: OK;
  - an `_r1` file without the run: FAIL "a re-probe whose world does not record another grid start or level seed";
  - an `_r1` file recording 1024 / 263: the same FAIL;
  - the r1 file under the accepted name: FAIL "a re-probe's world under a probe's own name".
  - The tool itself refuses a re-run without a tag, and a tag without a re-run.
- **Against the s9e2 sandbox runs** (`compare_sandbox_out.txt`): the placement positions are the same (the same probe order), but the draws are not. `/place feature` draws from the server's random, not the seed or the position, so the counts differ (tall_mangrove 97, 90, 93 against 92, 91, 91).
  - Statistically they agree, except the grass mangrove. The new runs match the accepted probe there (logs 6.60 against 6.58), not the sandbox's 7.20 (z -2.3).
  - The finding stands: tall@mud trees with h <= 6 are 18 of 94 new, 26 of 86 in the sandbox, 0 of 28 accepted.

## 2. The test

- `RunTreeProbes` reads 94 files. `GetProbeRun` and `PoolTreeProbes` pool each probe's three runs into one 300-tree probe:
  - tries, placed trees, lists and histograms added;
  - means per tree weighted by placed trees;
  - leaf occupancy weighted by profiled trees;
  - extremes as min or max.
- Pinned: 94 files, 4 pools, 300 tries each. Mutation 15 (a pool of its first run alone) fails all four pools' tries (`C:\dev\sandbox\phase6s9\runs\s9e3-mut15-20261011-062025`).
- The accepted tall_mangrove@mudwater: placed rate only (`FTreeProbe::bPlacedRateOnly`, citing the re-probe report). The other three accepted mangroves keep their full tests (the manager's Q4).
- Probes run: 46 while the mangroves are off, 54 once on.

## 3. The fit

| Round | Agent | What it did | Result in C++ (N 2000, the test's seed) |
|---|---|---|---|
| Baseline | | Today's model against the pools | Fails: moss -14% and -17% on mangrove and mud; tall pools leaves -13% and -15%, propagules -16% and -18%; the accepted mangrove's dy -5 occupancy and mud's 6-connected parts (`runs\s9e3-base-20261011-061730`) |
| 1 | F5 (Sonnet, peak 157.8k) | Branch chance 0.425 to 0.45; propagule y exclusion back to the data's 0; root outward chance 0.36 to 0.38, and 0.45 once an arm has landed, its outward attempts not counted | 7 of 8 tree tests pass. Shapes still fails mud moss, tall moss, tall@mud leaves and mud occupancy (`runs\s9e3-f5-20261011-063348`) |
| 2a | F6a (Sonnet, 143.0k) | A branch with logs takes an attachment below its last log, chance 0.5: far leaves at dy -1 and below ran 18 to 54% short | Every log, leaf, occupancy and propagule line passes (`runs\s9e3-f6a-20261011-065215`) |
| 2b | F6b (Sonnet, 137.2k) | Moss: nothing adopted. The moss per root is right; the roots sit in the wrong places (mud arms walk out higher, tall piles roots at the ground). Its candidate fixes moss but breaks the root totals | - |

- **Still failing** with the mangroves on:
  - mangrove@mudwater pooled moss -13.5% (z -4.8);
  - tall_mangrove pooled moss +7.5% (z +3.1);
  - the accepted mangrove@mudwater's 6-connected parts at p 0.01. Its rate at 5 or more parts matches the pool (7 of 89 against 19 of 269), but 4 trees sit in bin 6 against 2 of 269 pooled; the chi-square does not merge sparse bins (`C:\dev\sandbox\phase6s9\s9e3\comp6_out.txt`).
- Reports:
  - `C:\dev\sandbox\phase6s9\s9e3\fit\fit-report.md` (F5);
  - `fit2\a\crown2-report.md` (F6a);
  - `fit2\b\moss2-report.md` (F6b).
- F5's and F6a's rules are ported (`VoxelTrunkPlacers.cpp`, `VoxelTreeDecorators.cpp`), so part 4 starts from them. With `IsMangroveBuilt()` false they place nothing.

## 4. The suites

- **The review** (Sonnet 5.5, 711 diff lines, peak 82.7k) made one medium finding, rejected with evidence: no code changed, no suite re-run.
  - The claim: the two end attachments are in the opposite order from the model.
  - The fitted model runs `ub_att_every=1, ub_end_dy=1` (`fit2\a\joint.py`), so its order is end + 1, then below the end: the C++ order.
  - Findings: `C:\dev\sandbox\phase6s9\s9e3\review\review-findings.md`.
- **RunChecks final** (`Tests/Evidence/2026-10-11/Checks-final-20261011-070847/summary.txt`), GREEN in 22 minutes, with no java, javaw or UnrealEditor running besides its own:
  - native 260/260 (2 with warnings), 0 crashes; voxel 167 suites;
  - **the expected hashes, 9 of 9 equal** (`C:\dev\tools\shift-managers\expected-hashes-main.txt`): the world is unchanged;
  - Playground all 119, interaction 81, tour 27, 0 failures each;
  - create-world 32/32;
  - the flight smoke passed (`Tests/Evidence/2026-10-11/VoxelFlight-20261011-072727`, 150 s, no errors besides LogPython);
  - npm 61/61, source-check, all five reference verifiers.
- **Tree tests** on the committed state: 8/8 (`C:\dev\sandbox\phase6s9\runs\s9e3-off-20261011-070651`); the mangroves skip at their root placer.
- **No re-records, photos or window** (the manager's call): with `IsMangroveBuilt()` false no hash moves.

## 5. The manager's calls

- The plan; Q3: the verifier extension is part of extending the reference by its own tools, proven by negative cases; Q4: the three valid accepted probes keep their full tests ("report it and don't loosen").
- At the checkpoint: (A), commit with the mangroves off. No re-records, photos or window, since the world is unchanged. Part 4 gets the root-walk refit, the comp6 change and the switch-on.
- comp6 (a), approved as a shared-kit change (audit A3), for part 4:
  - merge bins with expected count under 5 in `ChiSquareHomogeneity`, for every probe;
  - a before/after p table for every affected line, flagging any flip other than the target;
  - a mutation proving comp6 still catches a real change;
  - forest rock's local pooling either kept, or folded into the shared rule with its numbers unchanged.

## 6. Commits

- `3ba9929` Re-probe the mangroves into the worldgen reference and refit them, still off (phase 6, S9e part 3): the reference's re-probe mode, the 12 re-probes, the verifier's extension, the pooling, the refit, `Docs/VoxelWorld.md`.
- The commit after it: this record, [the S9e part 4 handoff](2026-10-11-Voxel-Handoff-Phase6-S9e4.md) and the records index.
- Tokens (session `79e20346-9150-5fa1-8bc1-e15c8aed9b89`): the lead 318.8k at commit 1; F5 157.8k, F6a 143.0k, F6b 137.2k (all past their briefs), the review 82.7k.

## 7. Known issues

| Issue | Due |
|---|---|
| The mangroves are off: moss on mud -13.5%, on tall +7.5%; the accepted mud probe's 6-connected parts at p 0.01 | Part 4 |
| Leaves per log: after F6a the pools are within 4%, but the end-below attachment is a fit; why the real far ends sit lower is unexplained | Part 4, watch |
| The re-probes' grass mangrove ran lower than the s9e2 sandbox runs (logs z -2.3): run-to-run spread may exceed the binomial | Part 4, watch |
| The S9 gate as S9e part 2 measured it | Jesus, at the phase 6 record |
| Every other item in the S9e part 2 record's section 11 | As listed there |
