# Voxel world phase 6, S7b: Jesus's plant answers, the survival matrix, and the handoff into S7c (sturdy faces) - October 10, 2026

The S7b record (parallel lane `feature/voxel-s7b`, worktree `C:\dev\projects\Unnamed-s5`, run for Jesus by the day manager "Shift manager 3" under his away rule), and the handoff into the next session of this lane. [The S7b handoff](2026-10-10-Voxel-Handoff-Phase6-S7b.md) set the scope. S7b's commit is `01a4a66` on top of `a468a8b` (= `feature/voxel-world` then). Facts were true at that commit; check `git status`, `git log -5` and newer records first.

## Short prompt for the next session

> Voxel world phase 6, S7c (parallel lane): sturdy faces from the game (scout H's Q2), after S7b. Read `C:\dev\projects\Unnamed-s5\Docs\Records\2026-10-10-Voxel-Phase6-S7b.md` first; its section 6 is the whole reading list, so keep to it. First check whether S7b's merge into `feature/voxel-world` happened (section 5); if not, it comes first, on the manager's "merge now". Then send the manager a plan for Q2 (section 8 has the options and what each moves) before building. Report as section 7 says, and measure your context after every build, test run and subagent. End with a handoff into the next session.

## 1. What was built

Jesus's answers to scout H's questions, all (a), each as [the S7b handoff](2026-10-10-Voxel-Handoff-Phase6-S7b.md), section 4, maps them:
- **Q1, stacks:** after the one-level pass, `FFoundationAuthority::SettlePlants` follows each changed segment of a stack up and down the column while the cells there change (`FoundationPlantRules::GetStack`: sugar cane, cactus with its flower, bamboo, kelp, cave vines, pointed dripstone, sulfur spike, pale hanging moss, vine, big dripleaf with its stem). One `Settle` step trims a multiface block's unheld faces first, then pops a state that no longer survives (with its other half).
- **Q5, snow:** `FoundationBlockPlacement::IsReplaceable`: a block's flag, or one snow layer (`EFoundationBlockShape::Layers1`). `CanBeReplaced` and the authority's `GetDestination` and upper-half check use it. Snow on a snow layer is not a replacement (stacking layers is not built).
- **Q7, coral wall fans:** `FoundationPlantRules::GetWallBlock` gives the five coral fans their wall fans (the reference's standing-and-wall items, `facts/block_support.json`). The registry sets `ItemWallBlock` for them without the torch rule; a wall fan drops its fan's item. `GetTorchState` became `GetStandingOrWallState` with a support test: the torch's support table, or for a coral fan `CheckPlantPlacement` (the survival table). Block items skip every wall block.
- **Q8, the vine's up face:** a vine under a block takes `up=true`; `VineUpFace` is gone and the up face is a checked face whose support is the upper face of the block above (full, or sturdy on Up): a top slab holds it, a bottom slab does not.
- **The manager's (c):** a vine survives while any face holds (T1's measurement); glow lichen and sculk vein need every face. Unheld faces still trim.
- **Q3, the oracle:** `FactsDump.dumpSurvival` (the blocks reference's own dumper, a `java.lang.reflect.Proxy` of the public `LevelReader`) writes `facts/plant_survival.json`: for every imported state (4,424 subjects) and eight probes (each of six neighbours; below with water sources around the support; below at light 0), the set of candidate states (air plus every imported state, 4,425) that keep it, as 75 shared signatures. `Blocks.Survival` checks the table against all 156,432,600 answers and counts each difference by its cause (section 2).
- **Reference and asset:** `build_reference.py` copies the new file, `verify_reference.py` checks it (structure plus spot rows), README has a "Plant survival" section. `DA_Blocks` is restamped (`SourceSha256` only). Docs/Blocks.md: Placement, known issues, Tests.

## 2. Verified

| What | Result |
|---|---|
| `Unnamed.Foundation` | 53 of 53 (after the review's test hardening) |
| All native tests (`Unnamed`) | 226 of 226 (225 + 1 with the known LogHttp generate_204 warning) |
| `TestVoxel.ps1` | PASS 147 |
| Radius-8 hashes | unchanged: ow `0x194a623a86b97dd6`, lb `0x1b665f4703a79406`, amp `0xbde96a3deaa9d130`, 0 columns differ |
| `Mesher.Digest` | unchanged: ow `0x2d204b0b4cf1abd8` / `0x6ee399181d5fccd7`, lb `0xca80f4d03a73a6f9` / `0x762f1bf60ca6a7e5`, amp `0x49802047907506c6` / `0x627b48a897480211` |
| The survival matrix | S7's kinds: 0 differences. Counted and pinned (the manager's calls): sturdy faces (Q2's follow-up) 20,931; the mushrooms' light 10,360; snow layers (no rule in the table) 205,680; S8's rules: pale moss carpet 1,055,349, lily pad 1,521, upper halves 213 (small dripleaf 192, six tall plants and tall seagrass 3 each), mushrooms in the dark 4 |
| Blocks reference | verifier OK (2,278 files hashed). Diff proof: one new file, five expected files changed, the other 2,273 byte-identical; SHA256SUMS +1 line, 4 changed. Regeneration from the repo's tools: 2,279 files byte-identical. Verifier mutation caught. FactsDump: the other facts byte-identical (creative_tabs differs only in Java identity hashes, kept), two runs identical, scout H's probe agrees on 7,592 of 7,592 rows |
| `DA_Blocks` | `SourceSha256` `3d9371d9...` to `9d1779d2...` (SHA-256 of the new SHA256SUMS); T3D before/after differ in that line only; `ImportBlocks.py`: PASS, nothing rebuilt |
| Mutations, each caught | any-face off; the up-face quirk off; the stack pass off; one snow layer not replaceable; no coral pairs (`C:\dev\sandbox\s7b\mutations.txt`) |
| create-world `--editor` (CEF lock) | all PASS, 393 s |
| Playground probe, cap 60 | 44.18 / 44.21 FPS (15 Python processes and another lane's editor running); interaction p50 0.014 ms, p95 0.039 / 0.038 ms per update (S7: 0.0151 / 0.0147 and 0.0455 / 0.0373); plants group exact (cells 5,126, instances 6,457). One failure in both runs: the known combat flake (strength 0.520; it passes at 0.440; main hit it at 09:47, S10 at 16:30). The manager accepted it (fixed in main `b00bfee`, so the merge run must pass it) |
| Flight | holes in 302 frames (at most 33 columns), chunk work worst 2.69 ms, 10 of 9,094 frames over budget, memory flat. Same code with trees on gave 77-134 frames (at most 10-15) in S10's runs, under less load. Accepted by the manager; the merge run flies again |
| Pre-commit review | Sonnet 5.5, diff only (1,246 lines): nothing confirmed; the plausible items were checked or hardened (pairs checked against `block_support.json`, a literal drop check, snow placement through `CheckPlacement`, a bounds check in the matrix parser) |

## 3. Known issues

| Issue | Due |
|---|---|
| Sturdy faces are S8's `GetSturdyFaces`: leaves, mud, soul sand, snow, powder snow and the model blocks' faces differ from the game (20,931 matrix answers) | S7c, Q2 (section 8) |
| S8's rule gaps: pale moss carpet `bottom=false` takes any floor (needs a `bottom=true` carpet below); a lily pad pops on a water source held in a block (waterlogged slabs, leaves); an upper half stands on its own upper half; a mushroom stands on powder snow or a test instance block in the dark | Main lane, small fixes (they move generation) |
| Snow layers have no survival rule (205,680 answers) | Main lane, with snow (generation places snow layers) |
| The mushrooms ignore light (10,360 answers at light 15; the dark probe agrees) | With light in survival |
| Breaking or popping a water-holding plant leaves air | The water track (W2a) |
| Snow placed on a snow layer goes into the cell above (stacking layers is not built) | Later, with snow stacking |
| `C:\dev\sandbox\mc-26.3-blocks` was regenerated on 10/9 (531 blocks, not the accepted 616), so `build_reference.py` cannot use it as `--research`. S7b built through `C:\dev\sandbox\s7b\ref\research-view\` (the accepted reference's own `research/block_manifest.research.json`, `tools/*.py` and textures, plus junctions to the jars) | The next blocks-reference rebuild: reuse the view, or teach `build_reference.py` to read its research inputs from the reference's own copies |
| `FactsDump.java` names the two extra probes "Phase A2" and "A3" in a comment; the JSON calls them `down_water`, `down_dark` | Whenever FactsDump changes next (cosmetic; changing it means a reference rebuild) |
| Carried from S7: project blocks hold no plant; a thick bamboo outline's 1/16 sliver; replacing a tall grass half pops the old upper half first; S7's interaction fast paths and a 2,000-round `Blocks.GatherBench`; the S7 handoff's rows (12 model blocks without an item, plant shadows, the firefly bush overlay, Playground water z-fighting, ISM component cost, UV1 model codes near 2^21, the 30-frame jitter test) | As in [the S7b handoff](2026-10-10-Voxel-Handoff-Phase6-S7b.md), section 6 |

## 4. Worked and failed

- **Worked:**
  - porting scout H's probe into the official dumper with every imported state as a subject, so the dumper reads no build output;
  - classifying matrix differences by cause (a rule that reads sturdy faces against a support Q2 will measure; the light probes; snow; otherwise the rule's kind) and pinning each count;
  - the T2-style diff and regeneration proofs;
  - the mutation runner (`C:\dev\sandbox\s7b\mutate.py`, `mutrun.ps1`);
  - a Monitor-free wait for the CEF slot (`slotfree.ps1` in a background `until` loop).
- **Failed first:**
  - a vine stack at z 0 in the lab: its cell below is outside the bounds, unknown, and an unknown cell keeps a plant;
  - `FLab::Put` takes a block id, not a state string (parse and `World.Set` instead);
  - UE 5.8's `FJsonObject::Values` keys are `UE::FSharedString` (iterate with `auto`, `*Key` gives `TCHAR*`);
  - a local named like an outer one (`Rows`) is an error;
  - the first classifier filed model-block candidates under Q2 even for rules that never read sturdy faces;
  - a PowerShell command line holding `Remove-Item` and a quoted `C:\Program Files` path is blocked: use `$env:ProgramFiles` in a script file.

## 5. If the merge has not happened

The manager sends "merge now" after S9c (main lane) commits. Then, in the worktree: commit when green, `git merge feature/voxel-world` once (never rebase or force), rebuild with touched sources or `-DisableAdaptiveUnity`, and run every suite once:
- `Unnamed.Foundation` and all of `Unnamed`;
- `TestVoxel.ps1` (its expected count is main's);
- the radius-8 hashes (`C:\dev\sandbox\s7b\r8.ps1 -Name merge`) and `Mesher.Digest`, which must equal S9c's new values;
- the blocks verifier;
- the end-to-end checks: create-world under the CEF lock, the probe (the combat check must pass now), the flight.

`Blocks.Survival`'s matrix counts depend only on the survival table and the reference. If S9c changed `VoxelPlantSurvival`, re-derive them from its documented change and say so. If `DA_Blocks` moved on main, redo the restamp in the merge (`C:\dev\sandbox\s7b\restamp.ps1`). Conflicts in `Docs/Records/README.md` keep both sides. Send one combined report; stop on red.

## 6. Reading list for S7c (about 60k)

1. Rules: `C:\Users\jherediaramirez\.claude\CLAUDE.md`, `C:\dev\CLAUDE.md`, the project's CLAUDE.md (its handoff pointer may be stale; this file wins).
2. This file. Then scout H's prep, [section 4.1](2026-10-10-Voxel-Phase6-Scout-H-Plant-Interaction.md) (T1) and its "Derived predicates" just below the table (sturdy FULL and CENTER against the facts).
3. `Reference/minecraft-blocks-26.3/README.md`: "Torch support", "Plant survival", "Rebuild"; `tools/FactsDump.java`: `dumpSupport` and `dumpSurvival` (the proxy level); `tools/verify_reference.py`: the plant survival block.
4. `Source/UnnamedVoxel/Blocks/VoxelPlantSurvival.h/.cpp`: `GetSturdyFaces`, `HoldsFace`, the sturdy switch at the end of `Initialize`, `SurvivesOn`, `HoldsAt`.
5. `Source/Unnamed/Tests/FoundationInteractionTests.cpp`: `Blocks.Survival`, the matrix part (`Cause` and the pins).
6. [Docs/Blocks.md](../Blocks.md): Placement (plants) and "Budgets and known issues".

## 7. Working agreement

- The plan goes to the manager first, with the files to change against the other lanes' lists, and whether anything moves hashes, goldens or saved worlds.
- A Sonnet 5.5 pre-commit review, diff only (split over about 1,500 lines).
- Look calls come with numbers and a control pair.
- Context is measured after every build, test run and subagent (`C:\dev\sandbox\s7b\measure.ps1`, a copy of the transcript into a temp `CLAUDE_CONFIG_DIR`).
- Quiet windows are respected; only timed gates need one.
- A parallel lane never re-records goldens or world hashes.
- The blocks reference changes only through its tools, with the diff, regeneration and mutation proofs, and a `DA_Blocks` restamp.
- Never bare `git stash`; never push.

## 8. Open questions for S7c (recommendation first)

**Q2, how to bring the game's sturdy faces in.** FactsDump can measure `isFaceSturdy` (FULL, CENTER, RIGID) per state and face through the same proxy level; scout H's `candidates.tsv` shows the bits exist.
- **(a) recommended:** a new facts file (for example `facts/block_sturdy.json`: per state, a FULL and a CENTER mask over `face_order`) through `FactsDump`, then `build_reference.py`, the verifier, the proofs and a restamp. Getting the bits to the voxel table is a design choice: a new `DA_Blocks` field (a reimport, binary) or the table built from the facts at load. `GetSturdyFaces` then reads them, CENTER where a rule needs it (spore blossom, cactus flower). The matrix's Q2 count should fall to about 0; any remainder is a finding.
- (b) derive the bits from the facts' collision boxes with the measured exceptions (leaves, mud, soul sand, snow 8). It needs no reference change, but it is not measured.

**Either way it likely moves generation:** kelp, seagrass, sea pickles and leaf litter read sturdy faces (not checked which of them generation places today, or on which supports). Wherever generation puts one on a support whose bits change (mud, leaves, snow), the radius-8 hashes and the plant goldens move. A parallel lane may not re-record them. So the options are:
- **(a) recommended:** S7c builds and proves everything with the switch off or the hashes compared, and the manager schedules the re-record as a main-lane change together with S8's rule gaps (section 3);
- (b) S7c runs on the main lane.

## 9. Evidence (`C:\dev\sandbox\s7b\`, local)

- `runs\foundation4-*` (the final Foundation run), `runs\all1-*` (all native), `runs\r8-*` (hashes), `runs\foundation3-*\infos.txt` (every matrix difference group);
- `ref\` (diff-proof-repo.txt, regen-proof.txt, mutation.txt, REPORT.md);
- `facts-run\` (the dumper run, compare.py);
- `restamp\` (T3D before/after, logs);
- `mutations.txt`;
- repo evidence: `Tests\Evidence\2026-10-10\VoxelAutomation-20261010-182717`, `Playground-all\Runtime-20261010-184657` and `-184910`, `VoxelFlight-20261010-185146`, `Worlds\Create-1791672018565`.

## 10. Token use

The lead peaked at about 430k before the handoff; most of it was reading (about 180k before any work) and the test and runtime loops. Subagents (Sonnet 5.5):
- the FactsDump port: 111k;
- the reference rebuild and its proofs: 100k;
- the pre-commit review: 86k.

The two worker subagents passed their 100k flag slightly; each brief was one deliverable.
