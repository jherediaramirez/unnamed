# Sound reference: the sounds beyond breaking

2026-10-13, lane `feature/sound-reference` (worktree `Unnamed-water`, from 9fd07b1). This file is the record and the handoff.

The scout plan's open question 2 asked for the vanilla 26.3 sounds that the block reference lacks. Jesus approved it on 2026-10-13 ("yes, now"). The manager approved option (b), a reference of its own, plus the fall events. The place fallbacks wait for a later step, and attack and thorns sounds stay out.

## What changed

- **`Reference/minecraft-sounds-26.3/`** is a new hash-checked reference with its own `tools/build_reference.py`, `verify_reference.py`, README and SHA256SUMS. It holds:
  - 193 events: 21 named, then 86 step and 86 fall events;
  - 111 `.ogg` files (1,238,583 bytes) from asset index 34;
  - 366 more files that are referenced, not copied: the block reference already has them, mostly because vanilla's hit events play the step files.
  - `sounds.json` is not duplicated.

  The build refuses an accepted folder. A rebuild from scratch (the accepted folder moved away, then `tools/` and the README restored) reproduced all 116 files byte for byte. RunChecks finds its verifier by itself.
- **Why it is separate.** `BreakImportData.py` requires the block reference's `sounds/` to hold exactly the break, hit and place files. Adding files there would invalidate the committed `DA_BreakAssets`. The block reference and `/Game/Breaking` are byte-identical: the 771 files under `Content/Breaking` were hashed before and after.
- **Footsteps** were partly in the block set already: 345 of the 390 step files are there. The 45 missing ones belong to 10 newer sound types: amethyst block and cluster, cinnabar, creaking heart, potent sulfur, resin, resin bricks, shelf mushroom, sulfur and sulfur spike.
- **Import** (`Scripts/Editor/SoundImportData.py`, `ImportSounds.py`) builds `/Game/Sounds`:
  - `Sounds/<dir>/<name>`, the 111 waves;
  - `SA_Sound16` and `SA_Sound12`;
  - `DA_Sounds` (`UFoundationSoundAssets`).

  The import is additive and locked to its digest, and reruns only verify. It loads the `/Game/Breaking` waves and `DA_BreakAssets` read-only. The new setting `UUnnamedProjectSettings::SoundRoot` has its Python twin `ProjectPaths.sound_asset`, and `/Game/Sounds` is always cooked (`Config/DefaultGame.ini`).
- **Play:** `FFoundationBreakEffects::PlayEvent(Event, Location, Volume, Pitch)` goes through the block sounds' path: the sound log, the trace, `SpawnSoundAtLocation`, and the event's attenuation. It draws from a random stream of its own. The switch is `foundation.Sound.Events` (1; 0 is silent). It is the only runtime hook, and nothing calls it yet.
- **Docs:** `Docs/Breaking.md` gained the switch row, the "Other sound events" section and the checks.

## Checks

- `verify_reference.py` passes for the new reference (477 file entries compared with the asset index). The block reference's verifier passes unchanged.
- The `ImportSounds.py` runs:
  - First run: `SOUND_IMPORT PASS: DA_Sounds built (193 events, 1065 files, 618 blocks; 111 waves imported, 366 block-set waves reused)`.
  - Second run: `... matches the sound data (...); not rebuilt`.
- `Unnamed.Foundation.Sound.Pick` and `.Assets` pass, and the 14 `Unnamed.Foundation.Break.*` tests pass unchanged.
- `RunChecks -Profile final -Flight smoke` came back GREEN after 27 minutes. Summary: `Tests/Evidence/2026-10-13/Checks-final-20261013-193309/summary.txt`.
  - Native: 315 of 315. Attempt 1 crashed in `Unnamed.Voxel.Mesher.Digest`, in the worldgen JSON parse on a ParallelFor worker. That is the known intermittent crash, and nothing in it touches sound code. A separate full native run passed 315 of 315 with no crash (`Checks-final-20261013-200036`).
  - Voxel: 207 suites. Hashes: 9 of 9 equal.
  - Runtime checks: all 128, interaction 83 and tour 27, with 0 failures.
  - Create-world 38 checks, flight smoke, npm 85 tests, source-check and the six verifiers all passed.

## For the steps that use it

Each caller passes vanilla's call-site volume and pitch:

| Step | Event | Volume, pitch | Called from |
|---|---|---|---|
| 3-4, item entities | `entity.item.pickup` | 0.2, ((r1 - r2) x 0.7 + 1) x 2 (facts.md section 2) | The pickup handler, through `View.GetBreakEffects()->PlayEvent(...)` |
| 10, eating | `entity.generic.eat`, then `entity.player.burp` at the end | The step records its own call-site values | |
| 11 and 16, damage | `entity.player.hurt*`, `.death`, `.small_fall`/`.big_fall` | The step records its own call-site values | Fall damage also plays the landing block's fall event (`DA_Sounds.Blocks[i].FallEvent`, in `DA_BreakAssets` order) |
| Footsteps, swim | The block's `StepEvent`; `entity.player.swim`, `.splash` | The step records its own call-site values | |
| 8-9, containers | Furnace crackle, barrel and chest open and close | The step records its own call-site values | |
| 14, axe stripping | `item.axe.strip` | The step records its own call-site values | |

**Still open:**
- The 66 place fallbacks: their 110 place files would join this reference as a later step of its own.
- CLAUDE.md needs a rules line for the new reference, which is Jesus's to add.
