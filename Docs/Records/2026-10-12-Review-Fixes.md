# Review fixes F1-F8: the external review's eight findings (2026-10-12)

An external review (`C:\dev\sandbox\openai-review-20261012\code-review.md`) raised eight findings. Scouts Z and Z2 confirmed all eight (`C:\dev\sandbox\scout-aquifer-rng\review-verdicts.md`, F4 in `facts.md` there). This lane (Unnamed-qol, branch `feature/voxel-review-fixes` from `3acc72b`) fixed them as eight commits. Commits 1-7 move no world output, golden or hash. Commit 8 (F4) is a decided generator change that re-records the aquifer-dependent goldens and hashes.

## Jesus's answers (through the manager, under his go "implement the fix once it gets done and proposes a plan")

- Q1 F4: (a) now, as the last commit (a decided generator change).
- Q2 F4 ranges: (a) bug fix only; our ranges stay (x/z 2..13, y 1..10).
- Q3 F7: (a) missing assets fail the test (the 44 guards).
- Q4 F1: (a) atomic write plus a startup restore from `.bak`.
- Q5 F6: (a) fix the hash; note the stale checks.
- Q6: (a) one chat, all 8 commits.

## The fixes (one commit each, in this order)

| # | Finding (Z2's verdict) | Fix | Test |
|---|---|---|---|
| 1 | **F2** Crash dumps deleted with the temporary profile (real; worst in TestVoxel and radius-8, which lost every dump) | New `Scripts/CrashFolders.ps1`, `Close-Profile -UserDir -Destination`: copies `<UserDir>\Saved\Crashes\UECC-*` to `<Destination>\Crashes`, checks every file arrived (relative path and size), and deletes the profile only then. A failed copy keeps the profile, warns, and names it (`.Kept`); it never fails a green step. Used by RunChecks' native step (replaces `KeepCrashes`), RunChecks' radius-8 editor, and `TestVoxel.ps1`. | `Tests/run-checks.test.mjs`: a fake UECC folder (with a subfolder) is kept and the profile goes; no crashes keeps nothing; a blocked destination keeps the profile and names it; a copy that leaves short files (Copy-Item mocked) keeps the profile; text checks for all three call sites and no bare profile `Remove-Item`. |
| 2 | **F5** TestMenu/TestVideoSettings ignore the editor's exit code (real) | `$code=$LASTEXITCODE` right after the launch, a "missing report (exit N)" throw, and `$code -ne 0` in the failure condition, as TestVoxel does. | `run-checks.test.mjs`: every `Scripts/Test*.ps1` that launches the editor with `&` reads `$LASTEXITCODE` on the next line and fails on it (TestHud, TestInput, TestMenu, TestVideoSettings, TestVoxel). |
| 3 | **F3** A reused `--output` mixes old evidence in (real, only with a reused `--output`) | `freshEvidenceDir(dir)` in `Tests/evidence.mjs`: makes the parent, then the folder with a non-recursive mkdir (throws "Use a fresh output folder" when it exists). `create-world.mjs` and `foundation-menu.mjs` call it before anything launches. `Docs/Foundation/README.md`'s command gets a timestamped `--output`. | New `Tests/evidence.test.mjs` (in `npm test`): makes a new folder and its parent; refuses an existing one and leaves its files; both drivers call it before `spawn(` and make no output folder themselves. |
| 4 | **F6** input-settings.mjs hashes the working tree's page (real, impact overstated) | Hashes `fileURLToPath(page.url())` per profile and records `pages: {profile: {htmlPath, htmlSHA256}}`. | `evidence.test.mjs`: no working-tree page path; the hash reads the loaded page's path. |
| 5 | **F8** A right-click move repairs a worn tool (real; the Playground's finite mode today) | `HudCreative.cpp`: right-pick copies the stack then sets the count (`Carried = Stack; Carried.Count = Take`); right-place into an empty slot copies the carried stack with count 0 before adding one. Damage (and any later field) comes along, as vanilla's `split` / `copyWithCount` keep components. | `Unnamed.Hud.Creative.Clicks`: an `iron_pickaxe` with damage 37 keeps it through right-pick, right-place back, and left-pick then right-place into an empty slot; 64 dirt still splits 32 + 32. |
| 6 | **F1** The input save truncates the old file first (real; "Critical" overstated) | `UUnnamedInputSettings::SaveChecked` now calls `WriteSlot`: `SaveGameToMemory`, then `UnnamedFiles::SaveBytesAtomically` (`.tmp`, then the old file to `.bak`, then the move) to `GetSlotPath(slot)` = `<Saved>/SaveGames/<slot>.sav`, the generic save system's path. Same slot name (`UnnamedInputSettings`) and the same `.sav` bytes. New `UUnnamedInputRestore` (a `UGameInstanceSubsystem`) restores `<slot>.sav` from `.sav.bak` (through a `.tmp` and a move) at startup when the `.sav` is missing or empty, before the first local player exists (Enhanced Input reads the save in its LocalPlayer subsystem's `Initialize`). It reads the slot name from `UEnhancedInputDeveloperSettings::InputSettingsSaveSlotName`. Sessions that can't save settings (the editor, `-MenuSettingsSessionOnly`) don't restore. | `Unnamed.Project.AtomicSave`: bytes A then B gives B with A in `.bak`; a `.tmp` folder makes the save fail before the file is touched; `RestoreFromBackup` restores a missing file and never overwrites a present one. `Unnamed.Input.BindingsAndSerialization` (a throwaway slot): the slot name equals the one Enhanced Input saves the player to and is still `UnnamedInputSettings`; the restore subsystem exists in every game instance; the slot holds `SaveGameToMemory`'s bytes and `LoadGameFromSlot` reads them; a blocked `.tmp` fails the save and the slot still loads the last save; a missing or emptied slot restores from `.bak`. A headless `-game` smoke with only a `.sav.bak` in the profile logged the restore with "local players: 0" before "Game Engine Initialized", reached L_Menu and exited 0 (`C:\dev\sandbox\review-fixes\runs\restore-smoke-valid-20261012-035701`). |
| 7 | **F7** Missing-asset guards count as passes (real, and 44 guards in 24 files, not 2) | Every `DA_Blocks is missing` guard under `Source/Unnamed/Tests` is now `AddError` (text unchanged), and the 35 guards followed by `return true;` return `false`. The 9 helper guards already returned `false`; with `AddError` their callers fail too. | `Tests/source-check.mjs`: no `AddWarning` holds "DA_Blocks is missing", and no guard returns true after its error (44 guards counted). |
| 8 | **F4** Aquifer centres: y and z offsets always 0 (real, Scout Z) | `FVoxelAquifer::GetCenter(Seed, CX, CL, CZ)`, pure and static, which `Prepare` uses: one `FVoxelXoroshiro` seeded from the same `Hash64(seed, "aquifer/center", cell)`, one `Next()` per axis (x, then y, then z, in separate statements). Our ranges stay (Q2 (a)): x and z 2..13 blocks into the cell, y 1..10. x moves too, since it now comes from the stream's first draw. | New `Unnamed.Voxel.Aquifer.Centers` (no assets): over 24³ cells and two seeds, every centre lies in its range, each axis takes every offset, (x, z) pairs cover at least 137 of 144 and (x, y) pairs at least 114 of 120; a cell's centre is pure in (seed, cell). `Determinism`, `Aquifer.Fluids`, `Carvers.AquiferAgrees` and both load orders (0 columns differ) stay green. |

## The F7 proof run (correcting Z2's "PASS before")

`TestVoxel.ps1`'s editor command, by hand, with `-ini:Game:[/Script/Unnamed.UnnamedProjectSettings]:BlockRoot=/Game/NoSuchBlocks` (`BlockRoot` is `UPROPERTY(Config)`; the copy is `C:\dev\sandbox\review-fixes\TestVoxelNoBlocks.ps1`):

- **Before F7:** 173 of 182 "passed": 94 passed, 79 passed with the "DA_Blocks is missing" warning, and 9 failed on other missing assets. So the suite was already red as a whole for this override. Z2 expected PASS-with-warnings; that holds only for the 79 guarded tests, not the run. The 9 failures: `Fluid.States`, `OreLooks.Texture`, `Plants.Multiface`, `Plants.PatchCounts`, `Plants.Survival`, `Spawn.DevSeed`, `Spawn.Standing`, `Trees.FeatureSize`, `Trees.Shapes`. Folder: `C:\dev\sandbox\review-fixes\runs\F7-proof-before-20261012-040043`.
- **After F7:** 94 of 182 passed and 88 failed, each guarded test with "DA_Blocks is missing: run ImportBlocks.py". Folder: `C:\dev\sandbox\review-fixes\runs\F7-proof-after-20261012-035913`.

## Mutations (each applied, run, reverted; all caught)

| Fix | Mutation | Caught by |
|---|---|---|
| F2 | Close-Profile deletes the profile despite a failed copy | run-checks.test: "a failed copy keeps the profile, names it" |
| F2 | TestVoxel back to a bare `Remove-Item` | run-checks.test: "every runner ... closes it through Close-Profile" |
| F2 | radius-8 without the call | the same text test (the hashes step) |
| F2 (review) | the copy check removed | run-checks.test: "a copy that does not arrive whole keeps the profile" (Copy-Item mocked to leave short files) |
| F5 | TestMenu without `$code -ne 0` | run-checks.test: "every Test*.ps1 ... fails on its exit code" |
| F5 | TestVideoSettings without `$code -ne 0` | the same |
| F5 | TestMenu without the `$LASTEXITCODE` capture | the same |
| F3 | a recursive mkdir in the helper | evidence.test: "refuses a folder that exists" |
| F3 | foundation-menu doing its own mkdir | evidence.test: "both E2E drivers ..." |
| F3 | create-world doing its own mkdir | the same |
| F6 | back to the working-copy path | evidence.test: "input-settings.mjs hashes the page it loaded" |
| F8 | right-pick back to `{Stack.Item, Take}` | `Hud.Creative.Clicks`: "right-pick carries the tool with its damage", "right-place writes the damage back" |
| F8 | right-place back to `{Carried.Item, 0}` | `Hud.Creative.Clicks`: "right-place writes the damage back", "then right-place into an empty slot writes the damage" |
| F1 | `WriteSlot` back to `SaveGameToSlot` | `Input.BindingsAndSerialization`: "A save that cannot write its temporary file fails", "leaves the last save readable", "A missing slot is restored" |
| F1 | a wrong slot path (`SaveGame/`) | the same test: "LoadGameFromSlot reads what WriteSlot wrote" |
| F1 | no restore (`RestoreSlot` returns false) | the same test: "A missing slot is restored from its backup" |
| F7 | one guard back to `AddWarning` (VoxelEditTests:1302) | source-check |
| F7 | one guard fully reverted, `AddWarning` and `return true` (VoxelOreLookTests:230-231) | source-check |
| F7 (review) | a guard split over two lines back to `AddWarning` | source-check (now reads whole files) |
| F7 (review) | `return true` after a comment line | source-check |
| F4 | `>> 21` / `>> 42` put back (shifted copies of the x draw) | `Aquifer.Centers`: y and z take 1 value each; (x, z) pairs fail |
| F4 | z reusing x's draw | `Aquifer.Centers`: (x, z) pairs fail for both seeds |

Not caught by a unit test: `UUnnamedInputRestore::Initialize` without its `RestoreSlot` call (the editor can't save settings, so the subsystem's restore never runs in automation). The headless `-game` smoke covers it: the restore line with "local players: 0" before "Game Engine Initialized" (`runs\restore-smoke-final-20261012-045353`, on the final code).

Runners: `C:\dev\sandbox\review-fixes\mutate.py` (scripts; restores bytes) and `cppmut.py` (C++: build, run the filter, restore, rebuild); runs under `C:\dev\sandbox\review-fixes\runs\mut-*`.

## F4: the decided change and its numbers

Old values are main's (`3acc72b`, equal to `C:\dev\tools\shift-managers\expected-hashes-main.txt` and the test sources). New values come from the first full native run after the change (`C:\dev\sandbox\review-fixes\runs\Native-F4-first-20261012-043524`, which failed exactly these four tests) and the radius-8 bench (`runs\r8-new-20261012-044354`). They were confirmed by a green full native run after the re-record (`runs\Native-F4-20261012-043933`, 281 of 281) and by RunChecks final against the lane hash file.

| Golden or hash | Case | Old | New |
|---|---|---|---|
| `Aquifer.Golden` | veins on: overworld, large_biomes, amplified | `0x06587391fe15bb98`, `0x6bef4434e7dee57c`, `0x82816e657968c3f1` | `0xb44ddb192ba22210`, `0xd259904d82120a3c`, `0xc821a64852f297d0` |
| `Aquifer.Golden` | veins off | `0x22523b7400c2f38e`, `0x6b19b1927b73266a`, `0x863f9b8b01468a3f` | `0xaa680306b5fab08a`, `0xa5bba90d26fbd006`, `0x4a576591fbb516a4` |
| `Carvers.Golden` | carvers off (its copy of Aquifer.Golden) | as Aquifer.Golden veins on | as Aquifer.Golden veins on |
| `Carvers.Golden` | carvers on | `0xe30dd5332a1bcab6`, `0x884085f14f14e1d5`, `0xf4915ea9f7dfc5cf` | `0xff1aeb8212aee5de`, `0x67f9f23e4aa7f3f8`, `0x885e712fbf153855` |
| `Carvers.Golden` | canyon column (-16, 28) | `0x223e91dd098ee111`, `0xa8b562ac46bf299e`, `0xc91686bd6f0b108b` | `0x52cc2bc1c5b80f18`, `0xd83d32150cce5f0d`, amplified unchanged |
| `Decoration.Golden` | plants off: hash / writes | `0xa28113883ba6e7ca` / 761,145; `0xf87e27cd1455c61a` / 768,534; `0xfc7c6279d46e6b76` / 823,270 | `0x37d27b0852660ad1` / 761,246; `0x2877986febaa1be9` / 769,062; `0xf3b8f8fb85546aaa` / 823,114 |
| `Decoration.Golden` | plants on, trees off | `0xb77656a8d37bde4a` / 769,195; `0xa290112228773eef` / 774,817; `0xed3582d140b6c106` / 829,385 | `0x08c6c008411701aa` / 769,297; `0x855983745971691f` / 775,340; `0xe735ed7621c1f140` / 829,229 |
| `Decoration.Golden` | plants and trees on | `0x95645bbf53f73c12` / 822,150; `0x5e72b754b1f20fa6` / 793,454; `0x98ae73a412fa50ca` / 857,244 | `0x91fb1227a4a24fde` / 822,252; `0x373ea972a4e47724` / 793,966; `0x3387c175962c3a24` / 857,088 |
| `Mesher.Digest` | plants off (models on = off) | `0xa8ba8d0d0a39174f`, `0xe49601da349c7c0d`, `0x7f9491318204f4b2` | `0xc68ae45d4719463a`, `0x4e14f54cb77a8b07`, `0xaee5521250bc5f2b` |
| `Mesher.Digest` | plants on, trees off, models on / off | `0xd8f3b39ed6d66f5a` / `0x6b673e48bda8a0b7`; `0xd8c297a251e3c4b1` / `0x81cbb033a8f2c493`; `0x9719bc3f651df81b` / `0xa0fbd505d2c267f9` | `0x5f5fb0383c290f6f` / `0xfdf117d2df0ca2d7`; `0xf7b967a9a2651a02` / `0x89f631f2e707d599`; `0xb33de1dab0a58225` / `0xbfc96349acb68766` |
| `Mesher.Digest` (hash file) | plants and trees on, models on / off | `0xdc44febc46d1c73a` / `0xa0ea785555d107d2`; `0x95edc2ccd204faaa` / `0x8b29db68a9ad618d`; `0x61c8745ba36e2004` / `0x6fd8255fd190e1ca` | `0x32229dac0d9d6b0d` / `0xef9503a539dd809e`; `0x2e7717221a276877` / `0x77988bdd559f22ef`; `0xe9b3a94605bb242b` / `0x4e519d9bd4039bc8` |
| radius 8 (hash file) | overworld, large_biomes, amplified; both load orders, 0 columns differ | `0xa96b931bfbc4c39b`, `0xe2a8bc32dc58d73c`, `0x5cae8edc58ba77d5` | `0x83dcc06f7ac4f6d8`, `0x2eab0bc7783931a7`, `0x776ca36a56d6462e` |

- **Expected moves (Z2's list):** all of them moved. `OreLooks.Mesh` (to check) did not. `Density`, `Biome`, `Noise`, `Random` and `Surface` stayed, as expected. Nothing outside the list failed.
- **Decided-change comments:** every re-recorded constant carries one, naming F4 and pointing here. `Docs/VoxelWorld.md`'s aquifer section has the note and the new `Aquifer.Golden` values. Its `voxel.Bench Columns` line has the new radius-8 hashes.
- **Lane hash file:** `C:\dev\sandbox\review-fixes\expected-hashes-aquifer.txt` (the main file's format; 3 radius-8 and 6 digest values). It goes to `C:\dev\tools\shift-managers\expected-hashes-main.txt` after the merge.
- **Saved worlds:** `world.json` is unchanged. Existing worlds keep their seed and settings, and their underground fluids regenerate differently. Edits are session-only until phase 10.
- **Cost:** `voxel.Bench Columns 8 overworld`, old and new module pairs swapped in alternately, 3 rounds each, in one window. The machine had 12 python processes from another lane and no other editor. Aquifer stage mean (1 worker, 289 columns): old 0.290, 0.292, 0.296 ms; new 0.312, 0.312, 0.297 ms (+0.014 ms, about 5% of the stage). Generate-column mean: old 2.677, 2.688, 2.678 ms; new 2.711, 2.682, 2.667 ms (+0.006 ms, noise). Runs: `runs\ab-old-1..3`, `runs\ab-new-1..3`.
- **Cave captures (not a gate):** `CaptureVoxel.ps1 -Caves` on the dev seed, before (the commit-7 modules) and after. Share of pixels differing by more than 16/255: section 4.53%, cave-wall 1.40%, lumen-scene 0.35%, overview 0.26%, closeup 0.07%, cave 0.04%, cave-lava 0.01%, ground 0.00%. The vertical section shows the change; the spawn's cave view barely moves. Folders: `runs\caves-before-20261012-045013`, `runs\caves-after-20261012-045055`.

## Pre-commit review (two Sonnet reviewers, the diff split by patch)

- **Applied, each in its fix's commit:**
  - F2: nothing tested the copy check, so a test now mocks a Copy-Item that leaves short files.
  - F1: the restore copies to `.tmp` and moves, so a restore that stops never leaves a half-written slot.
  - F7: source-check reads whole files, so a guard split over two lines, or a `return true` after a comment, is still caught.
  - F7: two "False (with a warning)" comments in VoxelTreeTests now say "with an error".
  - The new mutations are in the table above. Input+Project 4/4, npm test and source-check pass after them.
- **Rejected:**
  - "GetCenter's `Seed` parameter hides the member (C4458), a compile error": the module built and all 281 native tests ran.
  - "A right-pick leaves a zero-count stack with its damage": the existing `if (Stack.Count <= 0) Stack = {};` clears it.
- **Noted, no change:**
  - The restore brings back the previous bindings if someone deletes only the `.sav` to reset them. The in-game reset saves defaults, so this only matters for a manual reset.
  - The copy check skips empty folders.
  - `input-settings.mjs`'s results.json key changed from `htmlSHA256` to `pages`; nothing reads it.
  - The F5 text check would accept `$code -ne 0` inside a comment, and new launcher scripts must be added to its list.

## Where crash dumps go now

- RunChecks native step: `<Native folder>\Crashes` (unchanged place; now only deleted after a complete copy).
- RunChecks voxel step and direct `TestVoxel.ps1`: `<VoxelAutomation folder>\Crashes` (new; they were deleted before).
- RunChecks radius-8: `<Radius8 folder>\Crashes` (new).
- When a copy fails: the profile stays where it was (a `UnnamedChecksProfile-<guid>` or `UnnamedVoxelProfile-<guid>` folder under `%TEMP%`), and the CRASH line, the radius-8 line or TestVoxel's throw names it.
- `2026-10-11-Voxel-CrashHunt.md` stays as it was.

## Runs

All on 2026-10-12, worktree `C:\dev\projects\Unnamed-qol`. Machine state is logged in each summary. Another lane's editor ran at the start of the commit-7 runs; the benches and captures ran beside 12 python processes from another lane.

- **Commit-7 code, main hashes:** `RunChecks.ps1 -Profile final` GREEN in 18 min (`C:\dev\projects\Unnamed-qol\Tests\Evidence\2026-10-12\Checks-final-20261012-041646\summary.txt`).
  - native 280/280; voxel PASS; 9 of 9 hashes equal to `expected-hashes-main.txt`;
  - Playground runtime 121, 83 and 27 checks, 0 failures; create-world 38/38; npm 68/68; source-check and verifiers PASS.
  - The first attempt (`Checks-final-20261012-040405`) went red at the voxel step: F4's sources were applied while it ran, and TestVoxel lists the declared tests from source at its start (183 declared, 182 ran). The re-run kept the sources still.
- **Final code, lane hashes, run 1 with `-Flight smoke`:** GREEN in 21 min (`C:\dev\projects\Unnamed-qol\Tests\Evidence\2026-10-12\Checks-final-20261012-045441\summary.txt`).
  - native 281/281 on attempt 2. Attempt 1 hit the known allocator crash: exit 777004 in `Unnamed.Voxel.Mesher.Digest`, EXCEPTION_ACCESS_VIOLATION reading 0xffffffffffffffff. F2 kept its dump (`C:\dev\projects\Unnamed-qol\Tests\Evidence\2026-10-12\Native-20261012-045442\Crashes\UECC-Windows-3FD23EDC43FA185AC1C3BF9D372378CE_0000`: UEMinidump.dmp, CrashContext.runtime-xml, the log). That is the first kept dump of this crash, for the crash hunt.
  - 9 of 9 lane hashes equal; runtime 121, 83 and 27 checks, 0 failures; create-world 38/38; flight PASS (124 s, 0 error lines); npm 69/69.
- **Final code, lane hashes, run 2:** GREEN in 18 min (`C:\dev\projects\Unnamed-qol\Tests\Evidence\2026-10-12\Checks-final-20261012-051540\summary.txt`). native 281/281 on attempt 1; 9 of 9 lane hashes equal; every other step PASS.
- **Direct runners on the commit-7 code:**
  - `TestMenu.ps1` PASS (5 tests), `TestVideoSettings.ps1` PASS, `TestInput.ps1` PASS (3).
  - `Unnamed.Hud` 13/13 (`C:\dev\sandbox\review-fixes\runs\Hud-F8-20261012-034831`). `TestHud.ps1` itself fails on its pinned count of 7 (open item).
- **Other runs:**
  - F1's startup smokes and F7's proof runs (above).
  - The full native run after F4, before the re-record (`runs\Native-F4-first-20261012-043524`) and after it (`runs\Native-F4-20261012-043933`).
  - The mutation runs (`runs\mut-*`).
  - Every sandbox run folder is under `C:\dev\sandbox\review-fixes\runs`; builds are logged in `C:\dev\sandbox\review-fixes\logs`.

## Open items

- **For the crash hunt: a kept dump of the allocator crash.** It's the first one, kept by F2: `C:\dev\projects\Unnamed-qol\Tests\Evidence\2026-10-12\Native-20261012-045442\Crashes\UECC-Windows-3FD23EDC43FA185AC1C3BF9D372378CE_0000`, with UEMinidump.dmp, CrashContext.runtime-xml and the log. Exit 777004 in `Unnamed.Voxel.Mesher.Digest`, EXCEPTION_ACCESS_VIOLATION reading 0xffffffffffffffff, on the final code's first lane RunChecks run. The folder is under the ignored `Tests/Evidence/`; a copy is in `C:\dev\sandbox\review-fixes\allocator-crash-dump\` in case this worktree goes. When: the next crash-hunt session; the callstack is `2026-10-11-Voxel-CrashHunt.md`'s "cheapest lead".
- **Done (runner follow-up, `feature/voxel-runner-fu`):** ~~`Scripts/CaptureVoxel.ps1` still deletes its temporary profile (`:60`, `:100`) with any crash dump in it.~~ Both exits now go through one `Close-Profile` call: a finished editor, and a timed-out one it stops by PID and waits for (the timeout used to leave the profile behind). A crash note goes into every failure and PASS line. `run-checks.test.mjs`'s "every runner" test covers it (mutations caught: the old deletion line back; the timeout throwing before the close). Runs: a plain capture, PASS with 5 shots in 36 s and no profile left (`C:\dev\sandbox\runner-fu\runs\Capture-20261012-123233`); `-TimeoutSeconds 20`, which threw "Timed out after 20 s" with no profile and no editor left (`C:\dev\sandbox\runner-fu\runs\Capture-timeout-20261012-123321`; again on the final code, after `Stop-Process` got `-ErrorAction SilentlyContinue` for an editor that exits right at the deadline: `...\Capture-timeout-20261012-123523`).
- **`Close-Profile` doesn't report a profile its final delete left behind** (`Scripts/CrashFolders.ps1:27`, `Remove-Item -ErrorAction SilentlyContinue`): if a process still holds the profile (say, an editor alive after CaptureVoxel's 30 s wait), the profile stays with `Kept` empty and no note. This is rare and no worse than before; it applies to every runner. From the runner follow-up's pre-commit review. Fix direction: set `Kept` when the folder still exists after the delete. When: the next chat that touches the runners.
- **Left-click on a tool of the same item with different damage does nothing** (`HudCreative.cpp`, `Move = 0`); vanilla swaps them. When: before the creative or survival inventory work reuses `HudCreative`; confirm with a black-box check first.
- **`Tests/input-settings.mjs`'s checks are stale:** it expects 7 native rows (current code has 35) and defaults to the 2026-10-07 package. Only its hash was fixed (Q5 (a)). When: if Jesus picks Q5 (b) or (c) later.
- **S11 measures aquifer flooding after F4.** Scout K's vanilla diff (94% / 0.6% / 5%) predates it. When: S11.
- **Done (runner follow-up, `feature/voxel-runner-fu`):** ~~`Scripts/TestHud.ps1` pins 7 HUD tests; 13 exist.~~ It expects the declared `Unnamed.Hud` tests (`ListAutomationTests.ps1 -Prefix Unnamed.Hud`), fails on any declared test that didn't run or any extra one that did, and counts passes with warnings, as TestVoxel does. A new `run-checks.test.mjs` test keeps TestHud and TestVoxel off pinned counts (mutation caught: the old TestHud back). The direct run passed 13 of 13 in 34 s (`C:\dev\sandbox\runner-fu\runs\Hud-20261012-123150`).
- **The engine's own `UEnhancedInputUserSettings::SaveSettings` / `AsyncSaveSettings` still use `SaveGameToSlot`.** Our code never calls them (every save goes through `SaveChecked`). When: only if a future change calls them.
- **A non-empty corrupt `UnnamedInputSettings.sav` crashes the game at startup** (an FName length assertion inside Enhanced Input's load, seen in the first F1 smoke, which planted arbitrary bytes as the backup). F1 removes the main way to get a partial file and restores only missing or empty slots. When: a later robustness pass, if it matters (validate the header, or load into memory first and fall back).
- **TestVoxel reads the declared tests from source when it starts:** a source edit during a RunChecks run turns the voxel step red (this chat's first commit-7 run: 183 declared, 182 ran). Don't edit sources while a run is going.

## Tokens

- Lead: about 392k at the wrap-up (MeasureAgentUsage), against the 300k checkpoint. The manager accepted the case to finish here, with report A by about 650k.
- Subagents: two Sonnet 5.5 pre-commit reviewers. They peaked at 74.9k (scripts and F7, 1,100 diff lines) and 76.6k (F8, F1 and F4, 610 lines).
- Cost drivers: reading the brief, the plan and facts (about 90k before any work); the first commit-7 RunChecks, lost to a source edit mid-run; 19 build-and-run mutation cycles, kept small by the runner scripts.

## Follow-up (short prompt)

> Unnamed, a small runner follow-up after the brightness pass commits. Read `Docs/Records/2026-10-12-Review-Fixes.md` (open items). (1) `Scripts/CaptureVoxel.ps1`: close its temporary profile through `Scripts/CrashFolders.ps1` `Close-Profile` at both deletion sites, as RunChecks and TestVoxel do, and extend `Tests/run-checks.test.mjs`'s "every runner" check to it. (2) `Scripts/TestHud.ps1`: count the declared `Unnamed.Hud` tests (`ListAutomationTests.ps1 -Prefix Unnamed.Hud`) instead of the pinned 7. Guards: npm test, a direct TestHud run, one CaptureVoxel run. No world output moves.
