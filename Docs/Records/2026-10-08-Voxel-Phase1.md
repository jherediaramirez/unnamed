# Voxel world phase 1 - October 8, 2026

The deterministic core: seeds, positional hashing, a stream generator, gradient and fractal noise, and nested splines, all in the Core-only `UnnamedVoxel` module ([Voxel world](../VoxelWorld.md#deterministic-core)). Phase 0 was committed first as `b67b801`.

## What changed

- **`Random/VoxelRandom.h`** (header-only, constexpr):
  - SplitMix64 and its `Mix64` finalizer;
  - FNV-1a 64;
  - compile-time `Salt("name")`;
  - `Hash64(seed, salt, x, y, z)`;
  - `ToUnitFloat`, `ToUnitDouble`, `ToIndex`, `ToRange`;
  - `FVoxelXoroshiro` (Xoroshiro128++ with exact `NextInt`).
- **`Random/VoxelSeed.{h,cpp}`:** `FVoxelSeed::Parse`, `HashText`, `MakeRandom`, `Describe`.
- **`Noise/VoxelGradientNoise.{h,cpp}`:** one octave of 2D/3D gradient noise with analytic gradients.
- **`Noise/VoxelNoise.{h,cpp}`:** `FVoxelNoiseSettings` and `FVoxelNoise` (fBm, ridged, damped, domain warp, grid sampling).
- **`Spline/VoxelSpline.{h,cpp}`:** `FVoxelSpline` and `FVoxelSplineBuilder`.
- **`VoxelBench.{h,cpp}`:** timings shared by the console and a test.
- **Console:** `voxel.Seed [text]`, `voxel.Bench Noise [millions]`; `voxel.Info` now shows the parsed dev seed.
- **Tests:** `Source/Unnamed/Tests/VoxelRandomTests.cpp`, `VoxelNoiseTests.cpp`, `VoxelSplineTests.cpp` and `VoxelDeterminismTests.cpp`, ten new tests in all. `Scripts/TestVoxel.ps1` expects 13, records the console output in `voxel-console.txt`, and keeps the editor's cache folder (~120 MB) out of the evidence.

## Decisions

- **Hashed lattice, not permutation tables.** Each lattice point's gradient comes from an integer hash of (octave seed, cell). Nothing repeats within the int32 lattice, and nothing is shared between noises. Each octave gets its own seed and a random lattice offset, so octaves never share the origin's zero.
- **No runtime trigonometry or pow.** The 2D gradient directions are a literal table generated once; octave frequencies and amplitudes use repeated multiplication. `cos` and `pow` can round differently between C runtimes.
- **Double for position, float for the cell.** The lattice cell and fraction split in double precision, and the fraction is evaluated in float, as 26.3 evaluates density. Thirty million blocks out, the noise keeps its spread (0.176 against 0.195 at the origin) and its smoothness (the same largest step for 0.01 block).
- **Text seeds use our own pinned hash.** We use FNV-1a 64 plus a mixer, not the engine's `FXxHash64`, so an engine upgrade cannot change existing worlds.
- **Integer helpers without rejection.** `ToIndex` uses multiply-high, so a positional value stays a pure function of its hash; streams use Lemire's exact method.
- **Golden values are recorded, then frozen.** RNG vectors come from an independent Python implementation, cross-checked against the published FNV-1a and SplitMix64 values (`0xE220A8397B1DCDAF` for seed 0). Noise checksums were recorded on the first run and matched in later processes and builds.

## Verification

- **Editor build:** succeeded; no warnings in project code. The `static_assert`s on salts, FNV-1a and `Hash64` hold, so the build itself checks them against the Python values.
- **`Scripts\TestVoxel.ps1`:** 13/13. Evidence: `Tests/Evidence/2026-10-08/VoxelAutomation-20261008-144031/`.
- **Every native suite:** 59/59 in 22 s.
- **Threads:** 24 000 jobs of 11 kinds on 8 distinct system threads, each in its own shuffled order. Every result matched the single-threaded reference bit for bit, both with shared noise objects and with per-thread construction.
- **Noise properties:**
  - Gradient noise peaks: 2D 0.9935, 3D 0.7747.
  - Means 0.0006 and 0.0002; deviations 0.30 and 0.22.
  - Largest gradient error against finite differences: 0.0011.
  - Largest gradient jump across a lattice plane: 0.000009.
  - Correlation with another salt 0.003, with the next seed -0.005.
- **Timings, one thread, Development editor:**
  - `Hash64` 5.6 ns; gradient noise 2D 15.0 ns, 3D 34.7 ns.
  - fBm 2D, 6 octaves: 84.6 ns. fBm 3D, 4 octaves: 130.5 ns.
  - Ridged 97.5 ns, damped 159.5 ns, warped 155.0 ns (all 2D, 6 octaves).
  - 3-level spline: 61.3 ns.
  - Across all 30 workers: 117 million 3D fBm samples per second.
  - One 4-octave 3D noise at a column's 1 225 density corners: about 160 µs.
- **First run:** found one wrong test. The spline slope check used a 1e-3 central difference across a point where the second derivative jumps by about 14, an error of about H / 4 times the jump (0.0035). It now uses H = 1e-4.

## Known issues

- 3D gradient noise is the costliest primitive (35 ns). A terrain with several 3D noises at every corner spends a millisecond or more per column. `SampleGrid` is where a SIMD path can go if Phase 4 timings need it.
- 3D noise uses about ±0.78 of its range. Terrain tuning in Phase 4 scales for that.

## Next

Phase 2, data and meshing on a fixed area:

- a flat per-state table compiled from the block registry;
- paletted sections, and columns with heightmaps;
- a simple seeded 2D heightmap terrain;
- a binary greedy mesher with neighbour borders and ambient occlusion;
- chunk materials from `CreateBlockMaterials.py`;
- the generated map with a 9 x 9-column test area.
