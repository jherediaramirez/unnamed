#pragma once

#include "CoreMinimal.h"

class FVoxelBlockTable;
class FVoxelClimateSampler;
class FVoxelDensityTerrain;
class IVoxelTerrain;
struct FVoxelMeshOptions;

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

    // Block hashes of (2 Radius + 1)^2 columns around the origin generated in two load orders: row by row on the
    // calling thread, then from the last column back across every task worker. A column is a pure function of its
    // position, so the two agree column for column (Mismatches 0) unless generation depends on what ran before it.
    struct FLoadOrderHashes
    {
        // Each order's hash of every column's blocks, combined in position order.
        uint64 Forward = 0, Backward = 0;
        int32 Columns = 0, Mismatches = 0;
    };
    UNNAMEDVOXEL_API FLoadOrderHashes HashLoadOrders(const IVoxelTerrain& Terrain, int32 Radius);

    // voxel.Bench Columns: the area's stages (generate, gather neighbours, mesh) on the calling thread and across every
    // worker; for a density terrain (Density, the same object as Terrain, or null) each generation stage's p50, p95
    // and mean per column; then the load-order hashes.
    UNNAMEDVOXEL_API FString RunColumns(const FVoxelBlockTable& Table, const IVoxelTerrain& Terrain,
        const FVoxelDensityTerrain* Density, const FVoxelMeshOptions& Options, int32 Radius);

    // One line per result: name, nanoseconds per sample, millions per second.
    UNNAMEDVOXEL_API FString Describe(TConstArrayView<FVoxelBenchResult> Results);
}
