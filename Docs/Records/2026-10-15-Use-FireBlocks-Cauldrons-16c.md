# Use step 16c: fire-block contact damage and cauldrons (record and handoff)

Lane `C:\dev\projects\Unnamed-use`, branch `feature/survival-use`, step commit `39452653` (on the merges `b0fce3d4` of feature/survival db4e78cd and of 8762cf89, T5 B4b). Line 3's last step (FINAL-SCOPE.md): **there is no next step on this lane**; what is left goes to the QC list. Scout: `C:\dev\sandbox\scout-16c\facts.md`. Settled by SM-Player 2 (vanilla first): Q1 lava-cauldron contents hurt like lava, yes; Q2 a full bucket on a cauldron holding something else replaces the contents, full (provisional); Q3 a water cauldron putting out a burning player, deferred.

## 1. What was built

**(a) Fire blocks** (`foundation.FireBlocks`, 1; needs `foundation.Fire 1`; 0 = step 16a).
- `FoundationFireContact::Get(World, Body)` (new `FoundationFireContact.h/.cpp`): the body box shrunk 0.1 cm against the authority world's cells. Fire 1, soul fire 2 (the hotter wins), and a lava cauldron's lava box (x/y 2/16-14/16, z 4/16-15/16 of the cell). It works in the Playground and the voxel world (both have an authority); a bare lab has none.
- `FFoundationVitalsStep::FireBlockDamage` / `bLavaContact`, filled by `AFoundationPawn::StepVitals`.
- `FFoundationVitals::StepHazards`: new `EFoundationDamage::InFire` (armor applies, exhaustion 0.1, `death.attack.inFire` "went up in flames", #is_fire armor wear). In fire, every tick: the first 20 contact ticks spend `FireGraceTicks` (vanilla's -20..0 fire ticks), the 20th ignites at 160, then fire ticks grow net 0 or +1 (a fixed hash of Age, measured 50/50) with no cap; `Hurt(InFire, 1 or 2)`, which the 20-tick cooldown makes a hit every 10 ticks [survival2.r14.player_fire]. Water restores the grace (the water case's -20); lava (and lava-cauldron contact) ignites at 300 and zeroes it. `FireTicks` keeps its meaning (0 = not burning), so 16a's tests, the HUD and mobs don't move. `FireBlockHurts` counts the hits; the view plays `entity.player.hurt_on_fire` for them (`PlayFireSounds`).

**(b) Cauldrons** (`foundation.Cauldrons`, 1, with `foundation.Items.Use`).
- `FFoundationAuthority::UseCauldron`, `UseOn`'s branch 1c (after a block's own use, before stripping; either hand; skipped when crouching with an item; Adventure passes). Water bucket: `water_cauldron[level=3]`. Lava bucket: `lava_cauldron`. Empty bucket: a level-3 water cauldron or a lava cauldron back to `cauldron`; levels 1-2 pass. A full bucket on a filled cauldron replaces the contents (Q2).
- `SwapBucket`, extracted from `UseBucket`'s old `Swap` lambda (behaviour-identical, reviewer-checked), carries the survival, stack, full-inventory and Creative rules for both.
- Events `Emptied` (the poured bucket) and `Filled` (the filled bucket, at the cauldron's centre), so the step-17 sounds play with no view change.
- Docs: `Docs/Items.md` (Buckets: the cauldron bullet), `Docs/Blocks.md` (fire line). 8 rows in `C:\dev\tools\shift-managers\survival\provisional-values.md` (T3 16c).

**(c) Unity-build fixes** (my new files reshuffled the unity blobs): `FoundationChests.cpp`'s helpers clashed with `FoundationBlockFamilies.cpp`'s unnamed-namespace `GetClockWise` / `IsSide` / `GetFacing` (moved to `FoundationChestsDetail` + using-declarations); `FoundationItemMesh.h`'s local `Main` hid a global `Main` in `FoundationInteractionTests.cpp` (renamed `Pivot`). **At the landing merge:** T2 19c fixed the same chest clash by renaming the helpers (`ChestClockWise`, `IsChestSide`, `GetChestFacing`): take feature/survival's `FoundationChests.cpp` as it comes and drop the `FoundationChestsDetail` version (SM-Player 2). Keep both sides in `FoundationItemMesh.h` (19c edited the held pose too).

## 2. Checks

- RunChecks final -Flight smoke, survival hashes, after the last code change: GREEN, `Tests\Evidence\2026-10-15\Checks-final-20261015-204240\summary.txt`. Native 519/519, voxel 211 suites, hashes 9 of 9, runtime-all 41.3 fps, interaction 42.4, tour 39.3, create-world, flight, npm, source check, verifiers. Machine: other lanes' editors and a javaw running (no timed gate).
- New tests: `Unnamed.Foundation.Vitals.FireBlock` (scout F1-F12), `Vitals.FireContact` (C1-C6 and a moved origin), `Items.Cauldron` (K1-K11, Q2, Adventure); `Death.Messages` gains inFire.
- Mutations, 15, all caught: `C:\dev\sandbox\use-16c\mutations\summary.txt` (B1-B8). The ignite-300 mutation slipped at first (the test compared against the constant); the test now pins 160 and 300 as literals.
- Pre-commit review: Sonnet 5.5 on the diff only (`C:\dev\sandbox\use-16c\review.diff`), no bugs; one comment added.
- Playground: no route moved; the tour frames the gallery from cameras 2 m and more above it, and the probe areas sit clear of it.

## 3. Known issues and QC notes (for QC-KNOWN-BUGS.md)

- Mobs ignore fire blocks and lava cauldrons (the mob path reads only fluids).
- Fire doesn't spread, burn out or get put out by rain; campfires stay data only (B6).
- A water cauldron doesn't put out a burning player (Q3, deferred).
- The 16a `hurt_on_fire` pitch is random 1 ± 0.2, while run 2 measured 1.0 (`survival2.r23.fire_sounds`).
- Provisional, waiting on value-holder: the grace not refilling after a burn-out without water; the hash-based growth coin; the whole-cell fire contact box; the lava-cauldron box; Q2's replace rule; Creative keeping the bucket on cauldrons; the fill sound played at the cauldron.
- Merge order: T5 B5's `TillOn` lives in `UseOn` branch 2, just after my branch 1c.

## 4. Working notes

- Python and PowerShell writes must keep the repository's LF endings (`.gitattributes` is `* -text`): `pathlib.write_text` on Windows writes CRLF.
- Unity blobs shift when files are added: names in unnamed namespaces and globals collide across `.cpp` files. Build after adding a file even if nothing else changed.
- Token figures: lead peak 245.2k (MeasureAgentUsage); reviewer subagent peak 97.9k.
- Open questions for Jesus: none for this step. Pushes: none; the track stays local.
