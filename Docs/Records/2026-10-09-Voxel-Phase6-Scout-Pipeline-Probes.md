# Voxel world phase 6: scout C's probes P2 and P4 (October 9, 2026)

[Scout C's pipeline research](2026-10-09-Voxel-Phase6-Scout-Pipeline.md), section 2.6, proposed five vanilla probes. S1b ran P2 and P4 in a 30-minute timebox through one-off scripts (`C:\dev\sandbox\s1-feature-probe\p2p4\`), on normal 26.3 worlds with seed 263, black-box. These are their numbers; P1, P3, P5 and a same-order repeat of P4 are follow-ups through the probe harness, and none blocks S2 or S3.

## P2: rings per status

One chunk forced far from spawn (chunk 200, 200), then a 3x3 forced around it, each in a fresh world. Status of every chunk by Chebyshev ring around the forced area's centre:

| Ring | One chunk forced (A) | 3x3 forced (B) |
|---|---|---|
| 0 to 2 | `full` (1, 8, 16 chunks) | `full` |
| 3 | `initialize_light` (24) | `full` (24) |
| 4 | `terrain` (32) | `initialize_light` (32) |
| 5 | `biomes` (40) | `terrain` (40) |
| 6 | `structure_starts` (48) | `biomes` (48) |
| 7 to 12 | `structure_starts` (56 to 96) | `structure_starts` (56 to 96) |

Every chunk within 12 rings was saved in both worlds. So vanilla brings 2 rings around a forced area to `full`, and generates partial statuses out to 10 rings beyond those. S3 compares this with our R+1, R+2 and R+3.

The 26.3 server rejects `spawn_chunk_radius` and `spawnChunkRadius` as gamerule names ("Incorrect argument for command"); spawn chunks stayed at the default, far from the probed rings.

## P4: snow on leaves by generation order

A snowy_taiga chunk X at (-40, 4) (found with `locate` at -640, 71, 64), generated with X's 3x3 and the column east of it, in two orders (A and B), each in a fresh world.

| | Order A | Order B |
|---|---|---|
| Columns with leaves | 128 | 128 |
| Top leaves with air above (bare) | 28 | 23 |
| Columns with snow under leaves | 4 | 0 |
| Snow blocks | 214 | 215 |

730 blocks of X differ between the orders: leaves, logs, snow, grass, ores and dripstone. With one chunk per order and no same-order repeat, order and run-to-run noise are not separated yet: repeat the same order before reading this as MC-119599.
