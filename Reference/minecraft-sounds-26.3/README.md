# Java 26.3 sound reference

The sound events the game plays beyond block break, hit and place: 21 named player, item and container events, the 86 footstep events and the 86 fall events of the block sound types, then 133 events appended for the survival program (B0, 2026-10-14) and the step and fall events of the 3 block sound types survival B1 added (2026-10-14), with their `sounds.json` entries verbatim and the `.ogg` files they play. This folder is generated, hash-listed and never edited by hand.

It is separate from [`minecraft-blocks-26.3`](../minecraft-blocks-26.3/README.md) because the block import requires that reference's `assets/minecraft/sounds/` to equal exactly its break, hit and place files; adding files there would invalidate the committed break assets.

| Source | SHA-1 |
|---|---|
| Asset index 34 (the 26.3 client's `assetIndex`; `sounds.json` and the `.ogg` objects) | `abfaa525f923f807df8b4e4d29c1b5e3a104adbe` (official). The local Modrinth copy `%APPDATA%\ModrinthApp\meta\assets\indexes\34.json` differs from the official one only in `minecraft/lang/` entries, so the tools pin the SHA-1 of the other entries (canonical JSON, sorted keys, compact separators): `9ba09e4fc9e2245b7395eee4732096038d9f6e2d`. |
| `Reference/minecraft-blocks-26.3` (read-only input) | `assets/minecraft/sounds.json` (byte-identical to the index's `minecraft/sounds.json`), `facts/block_sounds.csv` (its SHA-256 is in `sound_events.json`) and its `assets/minecraft/sounds/` files |

```powershell
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-sounds-26.3/tools/verify_reference.py
uv run --no-project --python 3.13 python -I Reference/minecraft-sounds-26.3/tools/build_reference.py   # into an empty folder (tools/ and README.md only); move an accepted folder away first
```

The verifier checks every file against `SHA256SUMS.txt`, re-derives `sound_events.json` from the block reference's `sounds.json` and `block_sounds.csv` plus the files on disk, checks each file against its entry and home, and, when the asset index is present and its non-lang pin holds, compares `sounds.json` and every file entry's SHA-1 and size with the index objects.

## Files

| Path | What it is |
|---|---|
| `sound_events.json` | `source` (index pins, `sounds.json` SHA-1, `block_sounds.csv` SHA-256), `events` and `files`. Each event has `name`, `group` (`named`, `step` or `fall`), `subtitle` (or null) and `sounds`, the event's `sounds.json` list verbatim (strings or objects with `name`, `volume`, `pitch`, `attenuation_distance`). Each file has `name` (path under `sounds/`, no extension), `sha1`, `size` and `home`. |
| `assets/minecraft/sounds/` | The 394 `.ogg` files (3,924,037 bytes) homed here, byte for byte from the asset index. |
| `tools/build_reference.py`, `tools/verify_reference.py` | Build from the asset index and block reference; verify and re-derive. |
| `tools/own_files.txt` | The files homed here, in `files` order: an input from B0 on (see Home rule). |
| `SHA256SUMS.txt` | SHA-256 of every other file. |

`sounds.json` is not copied: the build reads it from the asset index (pin check) and requires it to equal the block reference's copy byte for byte. The build fails on any `sounds` entry with keys outside `name`, `volume`, `pitch`, `attenuation_distance` (so on `type: event`), and on an event whose entries disagree on `attenuation_distance`.

## Events

Fixed order; 332 events.

- `named` (21): `entity.item.pickup`, `entity.item.break`, `entity.generic.eat`, `entity.player.burp`, `entity.player.hurt`, `entity.player.hurt_drown`, `entity.player.hurt_freeze`, `entity.player.hurt_on_fire`, `entity.player.hurt_sweet_berry_bush`, `entity.player.death`, `entity.player.small_fall`, `entity.player.big_fall`, `item.axe.strip`, `block.furnace.fire_crackle`, `block.barrel.open`, `block.barrel.close`, `block.chest.open`, `block.chest.close`, `entity.player.splash`, `entity.player.splash.high_speed`, `entity.player.swim`.
- `step` (86): the sorted unique `step` values of `block_sounds.csv`, without `intentionally_empty`.
- `fall` (86): the same for the `fall` column.
- `named`, appended (133; `APPENDED_NAMED`): B0's batch, the events the survival steps play.
  - T5's 58 (`APPENDED_T5`): the 54 `sounds.json` events `T5_PATTERN` matches (door, trapdoor, fence gate, button and pressure plate open, close and clicks), sorted, then `block.lever.click`, `item.hoe.till`, `item.shovel.flatten`, `item.crop.plant`.
  - The program's batch, 75 (`APPENDED_BATCH`, in its file's order): T1 `block.smoker.smoke`; T2 experience and level up, the 8 armor equips, shield, arrows; T3 buckets, drink, fire and lava; T6 the cow, pig, sheep, chicken, zombie, spider, creeper and skeleton events, player attacks, the generic hurt, death, falls, explode, splash and swim. `block.blast_furnace.fire_crackle` was asked for but has no `sounds.json` entry in 26.3 (silent in vanilla), so it is not here.

The step and fall events of the first build are frozen (`SOUND_TYPE_BASE`, 86 sound types): sound types that `block_sounds.csv` gains later get their step and fall events after every earlier event (step, then fall, each sorted), never sorted in among the old ones. Survival B1 added 3 (`block.cherry_sapling`, `block.ladder`, `block.weeping_vines`; its crop and hard crop types reuse `block.grass` and `block.wood`): events 326-328 are their step events, 329-331 their fall events. The verifier pins the first build's 193 events and 477 file entries by SHA-256.

## Home rule

`files` lists every `.ogg` the events play, in order of first appearance. A file the block reference already has under `assets/minecraft/sounds/` is homed there (`"home": "minecraft-blocks-26.3"`, referenced by SHA-1 and size, not duplicated); any other is homed here (`"home": "minecraft-sounds-26.3"`) and stored in this folder.

| Group | Files first played | Homed here | Homed in the block reference |
|---|---|---|---|
| named | 58 | 58 | 0 |
| step | 390 | 45 | 345 (the block hit sounds' files) |
| fall | 29 | 8 | 21 |
| named, appended | 278 | 278 | 0 |
| step, appended (B1) | 15 | 5 (`block/roots/step1`-`5`, first played by `block.weeping_vines.step`) | 10 |
| fall, appended (B1) | 6 | 0 | 6 |

Total: 776 files, 394 here and 382 in the block reference.

From B0 on the rule is frozen for the files already homed here: `tools/own_files.txt` lists them (in `files` order), and a listed file stays homed here even if the block reference later gains a copy. Later files follow the rule above. The build writes the list when it is absent; otherwise it reads it, and the verifier checks it is a prefix of the files homed here.

## Appending

New events and files go last in `events` and `files`; no existing index may shift (`DA_Sounds`' blocks point at their step and fall events by index). Add named events at the end of `APPENDED_NAMED` in `tools/build_reference.py`; new block sound types append by themselves. Either way, update the verifier's group counts and the count pins in `Source/Unnamed/Tests/FoundationSoundTests.cpp`. Then move the accepted folder away (keep `tools/`), rebuild, and prove the old files and entries unchanged. `Scripts/Editor/ImportSounds.py` then appends the new events to `DA_Sounds`.
