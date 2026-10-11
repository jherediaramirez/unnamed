# Voxel world: a new world's spawn on land, the way 26.3 picks it - October 11, 2026

Jesus's first play test (2026-10-11) started him in the ocean. A new world now spawns the player as Java 26.3 does: a climate search over the preset's `spawn_target`, then the first dry top block near its pick. This was scout P's option B (`C:\dev\sandbox\scout-spawn\facts.md`), built to Jesus's five picks: the climate search; no spread; standing on a treetop as Java does; the nearest-land search, then the water's surface, when nothing near the pick is dry; and captures and the flight keep their spawn. Branch `feature/voxel-spawn`, worktree `Unnamed-spawn`.

**Saved worlds:** nothing about the spawn is saved (`world.json` holds the seed and settings), so every existing saved world now starts at the new spawn the next time it opens. No saved data changes.

## 1. What changed

- **Reader** (`VoxelWorldgenReader.cpp` `ReadSettings`): `spawn_target` is read into `FVoxelTerrainPreset::SpawnTargets`. Each key resolves like a router entry, and each range is quantized as the biome table's (`FVoxelBiomeTable::Quantize`). All three presets' keys are router functions already. Reference/ is unchanged.
- **`FVoxelSpawnFinder`** (`Terrain/VoxelSpawnFinder.{h,cpp}`, new). The score of a point is the distance penalty `(x^2 + y^2)^2 / 390625`, rounded down, plus the smallest climate distance over the targets.
  - Stage 1 scores 66 points: the origin, then rings at r 512 to 2048 with ceil(2 pi r / 512) points each, from +X, rounded to the nearest block.
  - Stage 2 scores 862 points on rings r 32 to 512 around the winner, against the origin.
  - Ties go to the earlier candidate.
  - `Find` chooses the path, and `FindNearestDry` is the old `AVoxelWorld::GetSpawnCell` search, moved verbatim. `GetStandingZ` is the standing helper.
- **`IVoxelTerrain::FindSpawn(Cell, bDry)`**: false means the terrain has no climate search. Flat and the stand-in keep the nearest-land search from the origin, so Superflat is exactly as before.
  - The density terrain checks the 11 x 11 columns in square rings around the pick's column. It builds each column's density once (`GetSurfaceHeights`) and scans block columns from the minimum corner. The first top at or above sea level - 1 wins.
  - When none is dry: the nearest-land search from the pick with a fixed reach of 192, else the water's surface over the pick (feet at sea level).
- **`AVoxelWorld`**:
  - `-VoxelSpawnAt` or `-VoxelSpawnExact` takes the nearest-land search exactly as before: the ViewRadius reach, the sea-floor fallback, no lift. `CaptureVoxel.ps1` passes `-VoxelSpawnAt=0,0` unless it gets `-SpawnAt` or the new `-ClimateSpawn`, so shots, their variants and the flight keep their spawn.
  - On the climate search's dry land, the first placement (`StandOnSpawnBlocks`) stands the player one above the highest cell with a collision box in the spawn's block column, so a treetop counts and grass does not. It then lifts until the body fits, as the Playground does, and stores the cell for death and fall-out.
  - **A fix the brief missed:** the streaming rings follow the camera from the first tick, and the pawn waits at the map's player start (0,0,0). A spawn hundreds of blocks out would never stream in, so the player would never be placed. `Regenerate` now pins the rings on the spawn until the player stands there (shots keep the pin, as before).

## 2. Results

- **The dev seed:**
  - The nearest-land spawn is (129, 101, 71), measured with a default capture on the build before the change (ff075ef, `Tests/Evidence/2026-10-11/VoxelShots-20261011-113429-spawn-before/build.txt`). The 2026-10-08 evidence's (0, 0, 65) was stale; the terrain has changed since.
  - The climate spawn is (16, -32, 67), 36 blocks out (pick (20, -25)).
- **Twenty seeds plus the dev seed (overworld):** all on dry land, 7 of 21 picks at the origin, the farthest 334 blocks out (seed 4, origin top 35, spawn (169, 288, 63)). Several spawns stand on a block level with the water (top 62).
- **Timing** (`FindSpawn`, test runs, PC otherwise idle): 2 to 8 ms per preset for the dev seed and seed 4.
- **Known rare case:** in large_biomes seed 1, nothing in the 11 x 11 is dry (285 ms), and the nearest-land fallback (the PS estimate, 64 confirmations, reach 192) finds nothing either. That player starts on the water at (1071, -186), and the whole search takes 1.5 s. This is Jesus's pick 4 as specified. An exact fallback would be the fix if it matters.

## 3. Tests (`Unnamed.Voxel.Spawn.*`, `Source/Unnamed/Tests/VoxelSpawnTests.cpp`)

- **Reader:** 2 targets of 5 in overworld and large_biomes, with the quantized ranges and large_biomes' function keys. Amplified equals overworld.
- **Score:** 66 and 862 candidates; the penalty at 0, 512 and 2500; the climate terms.
- **Fallback:** a mock terrain.
  - A wet pick with land 100 blocks off is found at ViewRadius 4 and 12.
  - All sea gives the water's surface.
  - The nearest-land path and its sea floor.
- **Standing:** grass, logs, leaves and tall grass give the top leaf + 1.
- **Seeds:** dry, the feet on the top, within 2650 blocks. The one-pass heights equal `GetSurfaceHeight`. Timings are logged.
- **DevSeed:** the pinned climate cell, the same twice and at both view reaches, and the nearest-land cell (129, 101, 71).

**Mutations, all 8 caught:** floor for ceil, no penalty, unquantized ranges, only the first target, the pick without the 11 x 11 check, the density top instead of the final blocks, the reach tied to ViewRadius, and the nearest-land path changed (`Tests/Evidence/2026-10-11/Native-20261011-*-mut-<N>`).

## 4. Suites

`Scripts\RunChecks.ps1 -Profile final -Flight smoke`, GREEN in 26 min, after the review's two fixes (`Tests/Evidence/2026-10-11/Checks-final-20261011-115135/summary.txt`):
- native 266 of 266 tests;
- TestVoxel 173 suites;
- 9 of 9 expected hashes equal (the radius-8 hashes and `Mesher.Digest`, so no terrain moved);
- the Playground runtime: all 119, interaction 81 and tour 27 checks;
- create-world 32 of 32 checks;
- the flight smoke (126 s, no errors);
- npm 61 tests, source-check and the five reference verifiers.

Runtime captures after the last code change (`Tests/Evidence/2026-10-11/`):
- **Default `CaptureVoxel.ps1`** (`VoxelShots-20261011-123332-spawn-after`): spawn cell (129, 101, 71), as before the change, grounded, 0.0 cm from it. The old search took 404 ms.
- **`-ClimateSpawn`** (`VoxelShots-20261011-123407-spawn-climate`, `ground.png`): the search's (16, -32, 67) took 6.1 ms. The player stands at (16, -32, 74), grounded, on top of a bamboo stalk in a bamboo jungle. Bamboo collides, as it is motion-blocking in 26.3.
- **The far spawn** (`VoxelFlight-20261011-123438-spawn-flight-seed4`, `-Flight -ClimateSpawn -Seed 4`): spawn (169, 288, 63), 334 blocks out, with the sea at the origin. The player was placed, and the 20 s flight ran; it only starts once the player stands on the spawn, with the rings following the camera.

The Sonnet pre-commit review of the diff found no confirmed bugs. Two of its "plausible" notes were applied: `StandOnSpawnBlocks` reads the spawn cell before its flag, and the spawn log names the path.

## 5. Try this (Jesus, in play)

1. Create five worlds with an empty seed: each starts on land, not in water or a river.
2. Seeds 4, 9 and 16 have the sea at the origin; expect land 230 to 330 blocks out.
3. Leave and reopen a world: the same spot. Change the render distance and reopen: still the same spot.
4. Die (a fall or lava): back at the start spot, on land.
5. Start in a forest: on top of the tree or the ground, never inside a trunk or leaves. The dev seed (no world, or `Unnamed`) now starts on top of a bamboo stalk in a bamboo jungle.
6. No long freeze at world start.
7. Superflat: still in the middle, on top.
8. Known: Large Biomes with seed 1 starts on the water.

## 6. Tokens (session `3b06c310-8bb8-5f79-9a7e-36851e81d113`, `Scripts/MeasureAgentUsage.mjs`)

- **Lead** (Opus 5.5): 303k peak context before the runtime captures, against a planned ~300k and the manager's 400k stop. The first read of the brief, the facts and the code ranges took 176k on its own.
- **Subagents** (Sonnet 5.5):
  - pre-commit review: 76.8k peak;
  - the eight mutations: 50.4k peak.

## 7. Next

Jesus asked for a "Generating world" screen like Minecraft's, which would wait on `AVoxelWorld::IsReady()`, and behind it a spawn fallback that checks the real terrain, so Large Biomes seed 1 lands on ground. The brief is `C:\dev\sandbox\spawn-worker\next-loading-screen-prompt.md`.
