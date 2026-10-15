# Blocks B0: the program's sound batch (record)

Lane `feature/survival-blocks` (worktree `Unnamed-blocks`), from `fd356e1` plus `feature/survival` at `8cfbb86` (merge `ff0131b`). Survival track T5, session B0 of [the blocks plan](2026-10-14-Blocks-Plan.md) (section 2A). Next on this lane: B1, the reference extension (plan section 2).

## 1. What was built

- **`Reference/minecraft-sounds-26.3`** (through its own tool, rebuilt into a fresh folder; the accepted one is kept at `C:\dev\sandbox\b0-sounds-old\minecraft-sounds-26.3`):
  - 133 named events appended after the first build's 193 (326 now), with their 278 new `.ogg` files (2,648,466 bytes; 389 files homed here, 755 entries). Every new event is in 26.3's `sounds.json`; none has a `type: event` entry; all fall off over 16 blocks.
  - `tools/build_reference.py`: `APPENDED_NAMED` (`APPENDED_T5` + `APPENDED_BATCH`); `SOUND_TYPE_BASE` freezes the 86 sound types, so step and fall events of types B1 adds go after every earlier event; `T5_PATTERN` (the build stops if `APPENDED_T5` is not exactly what it matches); `tools/own_files.txt` freezes the home of the 389 files homed here (written by the build when absent, then an input).
  - `tools/verify_reference.py`: the new counts (named 154, step 86, fall 86), the appended order, the pattern check, the first build's 193 events and 477 file entries pinned by SHA-256, and `own_files.txt` a prefix of the files homed here.
- **`DA_Sounds`:** `Scripts/Editor/ImportSounds.py` gained an append path: a `DA_Sounds` whose events are an exact prefix of the new data and whose blocks equal it gets the new waves imported and the new events appended; anything else is still refused. 278 waves under `/Game/Sounds/Sounds`; `SA_Sound16` reused. `SoundImportData.py` is unchanged (blocks point at their step and fall events by index, which appending keeps).
- **Tests:** `FoundationSoundTests.cpp` pins 133 appended events, the first (`block.bamboo_wood_button.click_off`) and the last (`entity.generic.swim`).
- **Docs:** the reference's README (events, home rule, appending), `Docs/Breaking.md` ("Other sound events").

No switch: data only. Nothing plays the new events until a step calls `PlayEvent`.

## 2. The events (133)

- **T5, 58:** the 54 events `T5_PATTERN` matches (doors and trapdoors of wood, iron, copper, cherry, bamboo and nether wood; four fence gates; five buttons; six pressure plates), then `block.lever.click`, `item.hoe.till`, `item.shovel.flatten`, `item.crop.plant`.
- **The batch, 75** (`C:\dev\tools\shift-managers\survival\sounds-batch.md`, unchanged since 18:50 UTC; read at 18:54 and again at the 19:20 UTC deadline):
  - T1: `block.smoker.smoke`.
  - T2, 15: experience pickup, level up, 8 armor equips, shield block and break, arrow shoot, hit, hit player.
  - T3, 11: the four bucket events, `entity.generic.drink`, `entity.generic.extinguish_fire`, `block.fire.extinguish`, `block.lava.pop`, `block.lava.ambient`, `entity.generic.burn`, `block.lava.extinguish`.
  - T6, 48: the 46 listed (cow, pig, sheep, chicken, zombie, spider, creeper, skeleton, player attacks, generic hurt, death, falls, explode, shear, milk) and the optional `entity.generic.splash` and `entity.generic.swim` (burn and drink came with T3).
- **Left out:** `block.blast_furnace.fire_crackle` (T1 step 8b), a misspelled name: 26.3's event is `block.blastfurnace.fire_crackle` (files `block/blastfurnace/blastfurnace1`-`5`), so the blast furnace is not silent in vanilla. *Correction (survival B1, 2026-10-14, review 7-T5-B0 finding 1):* B1 appended it as event 332.
- Lines added to the batch file after the deadline go into a small second batch at the end of the program.

## 3. Checks

- Sound verifier: REFERENCE OK, 394 files hashed, 326 events, 755 files, asset index compared.
- Append proof (`C:\dev\sandbox\b0-sounds\append_proof.py`, against `git archive HEAD`): the 111 old `.ogg` byte-identical; `sound_events.json` minus the appended entries equals the old bytes; SHA256SUMS keeps every old line but the four changed files'.
- Regeneration proof: the tool rerun into `C:\dev\sandbox\b0-sounds\regen`, 395 of 395 files byte-identical.
- `Reference/minecraft-blocks-26.3`: no change.
- `ImportSounds.py`: run 1 appended (193 kept, 133 added, 278 waves); run 2 "matches, not rebuilt", nothing written. The T3D export before and after: events 0-192 and the 618 block entries identical; only `SourceSha256` changed.
- The native suites and `RunChecks -Profile final -Flight smoke`: in report A.

## 4. Known issues

- None found. Event subtitles are carried in the reference but not used by the game.
