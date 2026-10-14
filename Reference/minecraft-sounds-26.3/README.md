# Java 26.3 sound reference

The sound events the game plays beyond block break, hit and place: 21 named player, item and container events, the 86 footstep events and the 86 fall events of the block sound types, with their `sounds.json` entries verbatim and the `.ogg` files they play. This folder is generated, hash-listed and never edited by hand.

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
| `assets/minecraft/sounds/` | The 111 `.ogg` files (1,238,583 bytes) homed here, byte for byte from the asset index. |
| `tools/build_reference.py`, `tools/verify_reference.py` | Build from the asset index and block reference; verify and re-derive. |
| `SHA256SUMS.txt` | SHA-256 of every other file. |

`sounds.json` is not copied: the build reads it from the asset index (pin check) and requires it to equal the block reference's copy byte for byte. The build fails on any `sounds` entry with keys outside `name`, `volume`, `pitch`, `attenuation_distance` (so on `type: event`), and on an event whose entries disagree on `attenuation_distance`.

## Events

Fixed order; 193 events.

- `named` (21): `entity.item.pickup`, `entity.item.break`, `entity.generic.eat`, `entity.player.burp`, `entity.player.hurt`, `entity.player.hurt_drown`, `entity.player.hurt_freeze`, `entity.player.hurt_on_fire`, `entity.player.hurt_sweet_berry_bush`, `entity.player.death`, `entity.player.small_fall`, `entity.player.big_fall`, `item.axe.strip`, `block.furnace.fire_crackle`, `block.barrel.open`, `block.barrel.close`, `block.chest.open`, `block.chest.close`, `entity.player.splash`, `entity.player.splash.high_speed`, `entity.player.swim`.
- `step` (86): the sorted unique `step` values of `block_sounds.csv`, without `intentionally_empty`.
- `fall` (86): the same for the `fall` column.

## Home rule

`files` lists every `.ogg` the events play, in order of first appearance. A file the block reference already has under `assets/minecraft/sounds/` is homed there (`"home": "minecraft-blocks-26.3"`, referenced by SHA-1 and size, not duplicated); any other is homed here (`"home": "minecraft-sounds-26.3"`) and stored in this folder.

| Group | Files first played | Homed here | Homed in the block reference |
|---|---|---|---|
| named | 58 | 58 | 0 |
| step | 390 | 45 | 345 (the block hit sounds' files) |
| fall | 29 | 8 | 21 |

Total: 477 files, 111 here and 366 in the block reference.

## Appending

New events and files go last in `events` and `files`; no existing index may shift. Add them by extending `NAMED` in `tools/build_reference.py` (or by a new group appended after `fall`), then rebuild into a fresh folder and move the old one away.
