# Voxel world, phase 6 S9d (part 1): the crash hunt and the trees' cost - October 10, 2026

S9d's first part hunted the intermittent allocator crash (not caught; a real use-after-free in a test was, and is fixed), then measured where the trees' Decorate time goes and cut it without changing any draw or output. Fallen trees and huge mushrooms (step 3) move to the next session (the manager's budget call at 300k): [the handoff](2026-10-10-Voxel-Handoff-Phase6-S9d2.md).

## 1. The crash hunt (step 1)

- **The crash:** about 1 in 4 or 5 full native runs, `EXCEPTION_ACCESS_VIOLATION reading address 0xffffffffffffffff` in a ParallelFor worker. The four dumps (S10's merge; items S3 twice; items S4) all fault **inside the allocator** (Core frames under `operator new` or a `TArray` realloc), at three call sites: `ReadFeatures`' preload (`LoadFileToString`, then `ParseWorldgenJson`) and once `FVoxelBlendedNoise::Sample` under `FVoxelDecoratedArea::Build`. A free-list pointer overwritten with -1 fits. `ReadFeatures` is the victim: `Mesher.Digest` (three of the four) builds its areas single-threaded, so the preload's workers are its only parallel allocations.
- **Runs** (`C:\dev\sandbox\phase6s9\runs\`):

| Run | Allocator | Tests | Result |
|---|---|---|---|
| `stomp1-full-20261010-202743` | Stomp (`-stompmalloc`) | every native suite | Faulted in `Pipeline.ReturnTrip`: a use-after-free read (below) |
| `stomp2-full-20261010-203642` | Stomp | every native suite, the fix in | 227/227, no fault |
| `band1-*`, `band2-*`, `bandb1-*` to `bandb6-*` | default (binned) | Features, Freeze, Mesher, Noise, OreLooks (23) | 8 of 8 clean |
| `ansi-full1-*` to `ansi-full4-*` | ANSI (`-ansimalloc`) | every native suite | 4 of 4 clean |

- **Found and fixed (commit `e214663`):** `Unnamed.Voxel.Pipeline.ReturnTrip` kept `FindColumn(F)` across `Pipeline.SetCell`, which copies the column on write and frees the old one; the test then read the freed column. It now looks F up for each write; the cells it edits are unchanged. It is not the crash: it only reads, and it runs after the crash band.
- **Ruled out:** thread-unsafe function statics (the module builds without `/Zc:threadSafeInit-`); threads created by our code (none); the decorated area's writes (per index); the density terrain's scratch pool (locked).
- **Open:** never seen under stomp (2 runs) or ANSI (4), nor in the band alone (8); about 1 in 4 or 5 full runs under the default allocator. Next tools: full runs under the default allocator with halves of the tests before the band removed, then the needed test under stomp in a loop. Notes: `C:\dev\sandbox\phase6s9\s9d\crash-hunt.md`.
- **A runner trap:** `Start-Process -Wait` hangs when the editor starts the shared zen or trace server (PowerShell 7 waits for descendants). `s9d\runtests-extra.ps1` waits with `WaitForExit()`; `runtests-long.ps1` still has the trap (passed to the tools chat).

## 2. Where the trees' time goes (step 2)

`voxel.Bench Decorate` prints two new lines after "Per Finalize": the trees' time per Decorate by stage (feature size, trunk, foliage, leaves' distance, decorators, emission; tries and placed), and each decorator's time with its calls and microseconds per call (`FVoxelTreeTimes`, filled through `VoxelTreeFeature::Place` and `VoxelTreeDecorators::Place` into `FVoxelDecorateStats::TreeTimes`).

- **Before the cuts** (overworld's bench area, jungle-heavy; 0 other processes, CPU 9%): trees 0.582 ms per Decorate in 4.4 tries (3.19 placed): the shape (height, feature size, trunk, foliage) 0.208, the leaves' distance 0.143, the decorators 0.118 (trunk_vine 0.049, leave_vine 0.069, cocoa 0.000), emission 0.112. The decorators were a fifth; the tree core's lookups were the bulk.

## 3. The cuts (every draw and output kept)

| Cut | Where | Why it is exact |
|---|---|---|
| The builder's cell index: `TMap<FIntVector, int32>` replaced by `FVoxelTreeCellIndex` (open addressing, linear probing, at most half full, each axis's low 21 bits packed) | `VoxelTreeFeature.h` | The map is private and never iterated; a removed cell keeps its key with no index (as `TMap::Remove`), and adding it again reuses the slot |
| `SetLog`, `SetLeaf`, `SetDecoration` look the cell up once (`Append`, `IsFreeBelow`) | `VoxelTreeFeature.cpp` | The same conditions (`SetLog`: the tree's log, or its leaf inside the world, or a free cell; `SetLeaf`: the tree's leaf, else a free cell), the provider's draw on the same path |
| The leaves' distance: a dense grid of leaf indices over the logs' and leaves' box | `GetLeafDistances` | The same BFS order and steps |
| `GetCells`: a stable counting sort by Z | `FVoxelTreeBuilder::GetCells` | Stable, so the same order as `StableSortBy` on Z |
| The feature size: a footprint that fills its box (1 or 2 x 2 columns) checks the widened box once per layer | `GetFreeHeight` | The union of the columns' squares is that box; `IsFree` has no side effects |
| Emission: the last foliage state's variants cached; the pre-tree state read only where used | `VoxelTreeFeature::Place` | Pure reads |

- **The guard after the last cut:** `Trees.*`, `Decoration.Golden` and `Mesher.Digest` 9/9; every one of the 65 tree probe lines byte-identical **in full** (no field stripped; `C:\dev\sandbox\phase6s9\s9d\cmpfull.py`) against S9c's `fix-1-20261010-185440`; radius 8 with trees on `0xf427b0a0ec0e6eaa`, `0xcac6968a2e00c046`, `0x0a9cb42b08a0f10a` (S9c's), both load orders equal.
- **Not cut:** the decorators' own work (their draws are per leaf or log side) and emission's writes (the log and the working copy are already open addressing).

## 4. The gate and the flight

- **The S9 bench gate** (quiet window 22:09 to 22:17; `C:\dev\sandbox\phase6s9\s9b\gate9.ps1 -Round N`, one bench per process, trees off and on alternating; `C:\dev\sandbox\phase6s9\s9d\gate9-r1.txt` to `-r3.txt`, summarized by `s9b\gatesum9.py`; 0 java or editor processes before each launch, CPU 0 to 35%). Medians of 3, ms per column, final code:

| Preset | Decorate, trees on - off (rounds) | Finalize with trees (off) | Whole column with trees (off) |
|---|---|---|---|
| overworld | **+0.332** (+0.870, +0.332, +0.256): met (S9c +0.687) | 0.354 (0.316); S9c 0.453 | 4.440 (4.229); S9c 4.745 |
| large_biomes | +0.038 (-0.031, +0.038, +0.327) | 0.261 (0.338) | 4.133 (4.571) |
| amplified | +0.136 (+0.136, +0.030, +0.142); S9c +0.250 | 0.294 (0.257) | 4.708 (4.796); S9c 5.294 |

  Round 1's overworld trees-on bench ran slow on an idle machine (Decorate 1.662 against 1.22 to 1.25 in rounds 2 and 3); the median absorbs it. `gate9.ps1` runs only round 3 when called without `-Round` (PowerShell names ignore case, so `$rounds` is the `[int]$Rounds` parameter); run it once per round.
- **The flight** (in the window, final code; `Tests/Evidence/2026-10-10/VoxelFlight-20261010-221726`): holes in 212 frames, at most 16 columns (circles 87, 56, 69), against S9c's 228 (106, 64, 58); still about 5x S9b's 40 and 42, which is apply throughput (S9b's levers, a streaming session). Quads 2,182,747, S9c's (the output is unchanged). Chunk work over 2 ms in 2 of 9,446 frames (worst 4.69 ms, once, in east 20). Memory flat over laps 2 and 3 (process 4,932 -> 4,950 MB, columns 18.93 -> 19.61 MB).
- **Indicative breakdowns** (outside the window, `C:\dev\sandbox\phase6s9\s9d\brk.ps1`; the machine was loaded, so only the split is comparable): in two runs under the same load, the trees went from 0.662 to 0.535 ms per Decorate across the BFS grid and the footprint union. The feature size dropped from 0.121 to 0.047 and the leaves' distance from 0.135 to 0.070. Emission (about 0.18 under that load) and the two vines (about 0.11) are now the largest parts.

## 5. The manager's calls

- Scope (the handoff's open question 1 (a)): the crash fix, the cuts, then fallen trees and huge mushrooms; mangroves, azalea, bamboo, vines and forest rock to S9e.
- Step 1's timebox: two stomp runs and one bisect round, then four ANSI runs; none faulting, the hunt stops with the crash open. Commit 1 is the test fix alone, proven by one full native run and TestVoxel (a test-only change moves no world output).
- Step 2: exact or nothing; the 300k case accepted in advance; step 3 to a fresh chat by the handoff.

## 6. Commits and suites

- **`e214663`:** the ReturnTrip fix. Full native 227/227 (`runs\c1-native-full-20261010-213757`), TestVoxel 147/147 (`Tests/Evidence/2026-10-10/VoxelAutomation-20261010-214034`); a Sonnet diff review (one medium, checked moot: `FindColumn` returns the record's data, which `SetCell` never clears).
- **`c459ab7`** (the breakdown and the cuts; after W2b's merge `535fd5d`: TestVoxel 156/156 in `Tests/Evidence/2026-10-10/VoxelAutomation-20261010-225456`, every native suite 236/236 in `runs\c2b-native-full-20261010-225659`). Before that merge: TestVoxel 147/147 (`VoxelAutomation-20261010-220531`); every native suite 227/227 (`runs\c2-native-full-20261010-220718`); the gate and flight above; create-world PASS (`Tests/Evidence/2026-10-10/Worlds/Create-1791685193071`); the Playground probe 0 failures, 43.94 FPS (`Playground-all/Runtime-20261010-222650`). A Sonnet diff review: no high or medium; three comment wordings fixed. No golden, count or hash moved, and no test was added (TestVoxel stays 147).

## 7. Known issues

| Issue | Due |
|---|---|
| The intermittent allocator crash in full native runs (section 1) | A test session: halve the pre-band tests under the default allocator |
| `runtests-long.ps1`'s `Start-Process -Wait` hangs when the editor starts the zen or trace server | The tools chat's shared runner |
| `gate9.ps1` ignores `-Rounds`: `$rounds` and the `[int]$Rounds` parameter are one variable (PowerShell names ignore case), so a bare call runs round 3 only; run `-Round 1`, `2` and `3` | The tools chat, or whoever touches the gate script |
| Fallen trees, huge mushrooms (with `attached_to_logs`) | S9d part 2 (the handoff) |
| Mangroves with `attached_to_leaves`, azalea, bamboo, vines, forest rock | S9e |
| The flight's holes (S9c: 228 frames, at most 16 columns) | See section 4; a streaming session (S9b's levers) |
| The earlier handoffs' issues (the S9d handoff's section 9) | As listed there |
