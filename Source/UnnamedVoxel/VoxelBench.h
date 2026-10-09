#pragma once

#include "CoreMinimal.h"

class FVoxelClimateSampler;
class FVoxelDensityTerrain;

// One timed run: how long Samples evaluations took, and a checksum so the work cannot be optimized away.
struct FVoxelBenchResult
{
    FString Name;
    int64 Samples = 0;
    double Seconds = 0;
    double Checksum = 0;
    bool bParallel = false;

    double GetNanosecondsPerSample() const { return Samples > 0 ? Seconds * 1e9 / double(Samples) : 0; }
    double GetMillionsPerSecond() const { return Seconds > 0 ? double(Samples) / Seconds / 1e6 : 0; }
};

// Timings for the deterministic core, shared by voxel.Bench and the throughput test.
namespace VoxelBench
{
    // Times each primitive on the calling thread over Samples evaluations (positions spread over a wide area, so the
    // lattice hashes see realistic inputs), then 3D fBm across every task worker.
    UNNAMEDVOXEL_API TArray<FVoxelBenchResult> RunNoise(int32 Samples, uint64 Seed);

    // Times the climate pass of (2 Radius + 1)^2 columns around the origin, on the calling thread and then across every
    // task worker. Each result counts columns, so its nanoseconds per sample are the time per column.
    UNNAMEDVOXEL_API TArray<FVoxelBenchResult> RunClimate(const FVoxelClimateSampler& Sampler, int32 Radius);

    // Generates (2 Radius + 1)^2 columns of a density terrain around the origin on the calling thread, with
    // height-bound skipping on and off, and reports each stage's p50, p95 and mean per column, how many cells skipping
    // filled, and how many blocks differ between the two (always 0 unless skipping is wrong); then the same columns
    // across every task worker.
    UNNAMEDVOXEL_API FString RunDensity(const FVoxelDensityTerrain& Terrain, int32 Radius);

    // One line per result: name, nanoseconds per sample, millions per second.
    UNNAMEDVOXEL_API FString Describe(TConstArrayView<FVoxelBenchResult> Results);
}
