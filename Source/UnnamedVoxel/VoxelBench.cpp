#include "VoxelBench.h"
#include "Async/Fundamental/Scheduler.h"
#include "Async/ParallelFor.h"
#include "Generation/VoxelArea.h"
#include "Hash/CityHash.h"
#include "HAL/PlatformTime.h"
#include "Noise/VoxelNoise.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Random/VoxelRandom.h"
#include "Spline/VoxelSpline.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelClimate.h"
#include "Terrain/VoxelDensityTerrain.h"

namespace
{
    constexpr uint64 BenchSalt = VoxelRandom::Salt("bench");

    // Positions on a 1024-wide grid of 1.37-block steps: never lattice-aligned, and wide enough to keep hashing.
    FORCEINLINE double PositionX(int32 Index)
    {
        return (Index & 1023) * 1.37;
    }

    FORCEINLINE double PositionY(int32 Index)
    {
        return (Index >> 10) * 1.37;
    }

    template <typename FunctionType>
    FVoxelBenchResult TimePrimitive(const TCHAR* Name, int32 Samples, FunctionType&& Function)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(VoxelBench_Primitive);
        FVoxelBenchResult Result;
        Result.Name = Name;
        Result.Samples = Samples;
        const double Start = FPlatformTime::Seconds();
        double Checksum = 0;
        for (int32 Index = 0; Index < Samples; ++Index)
            Checksum += Function(Index);
        Result.Seconds = FPlatformTime::Seconds() - Start;
        Result.Checksum = Checksum;
        return Result;
    }

    // A three-level spline like the terrain offset spline: continentalness, then erosion, then peaks and valleys.
    FVoxelSpline MakeBenchSpline()
    {
        FVoxelSplineBuilder Builder;
        const int32 Root = Builder.AddNode(0);
        for (int32 Index = 0; Index < 6; ++Index)
        {
            const int32 Middle = Builder.AddNode(1);
            for (int32 Inner = 0; Inner < 5; ++Inner)
            {
                const int32 Leaf = Builder.AddNode(2);
                for (int32 Point = 0; Point < 4; ++Point)
                    Builder.AddPoint(Leaf, -1 + Point * .6f, .1f * (Index + Inner + Point), .5f);
                Builder.AddChildPoint(Middle, -1 + Inner * .45f, Leaf, .2f);
            }
            Builder.AddChildPoint(Root, -1.2f + Index * .4f, Middle);
        }
        FVoxelSpline Spline;
        verify(Builder.Build(Root, 3, Spline));
        return Spline;
    }
}

TArray<FVoxelBenchResult> VoxelBench::RunNoise(int32 Samples, uint64 Seed)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelBench_RunNoise);
    Samples = FMath::Max(Samples, 1024);
    const FVoxelGradientNoise Gradient(VoxelRandom::Hash64(Seed, BenchSalt, 0, 0, 0));
    auto Make = [Seed](EVoxelFractal Fractal, int32 Octaves, double WarpAmplitude)
    {
        FVoxelNoiseSettings Settings;
        Settings.Fractal = Fractal;
        Settings.Frequency = 1. / 64;
        Settings.Octaves = Octaves;
        Settings.WarpAmplitude = WarpAmplitude;
        return FVoxelNoise(Seed, BenchSalt, Settings);
    };
    const FVoxelNoise Fbm6 = Make(EVoxelFractal::Fbm, 6, 0), Fbm4 = Make(EVoxelFractal::Fbm, 4, 0);
    const FVoxelNoise Ridged6 = Make(EVoxelFractal::Ridged, 6, 0), Damped6 = Make(EVoxelFractal::Damped, 6, 0);
    const FVoxelNoise Warped6 = Make(EVoxelFractal::Fbm, 6, 32);
    const FVoxelSpline Spline = MakeBenchSpline();

    TArray<FVoxelBenchResult> Results;
    Results.Add(TimePrimitive(TEXT("Hash64 (x, y, z)"), Samples,
        [Seed](int32 I) { return double(VoxelRandom::Hash64(Seed, BenchSalt, I & 1023, I >> 10, I & 7) >> 40); }));
    FVoxelXoroshiro Stream(Seed);
    Results.Add(
        TimePrimitive(TEXT("Xoroshiro128++ next"), Samples, [&Stream](int32) { return double(Stream.Next() >> 40); }));
    Results.Add(TimePrimitive(TEXT("Gradient noise 2D, 1 octave"), Samples,
        [&Gradient](int32 I) { return Gradient.Sample(PositionX(I) / 16, PositionY(I) / 16); }));
    Results.Add(TimePrimitive(TEXT("Gradient noise 3D, 1 octave"), Samples,
        [&Gradient](int32 I) { return Gradient.Sample(PositionX(I) / 16, PositionY(I) / 16, (I & 63) * .37); }));
    Results.Add(TimePrimitive(
        TEXT("fBm 2D, 6 octaves"), Samples, [&Fbm6](int32 I) { return Fbm6.Sample(PositionX(I), PositionY(I)); }));
    Results.Add(TimePrimitive(TEXT("fBm 3D, 4 octaves"), Samples,
        [&Fbm4](int32 I) { return Fbm4.Sample(PositionX(I), PositionY(I), (I & 63) * 2.1); }));
    Results.Add(TimePrimitive(TEXT("Ridged 2D, 6 octaves"), Samples,
        [&Ridged6](int32 I) { return Ridged6.Sample(PositionX(I), PositionY(I)); }));
    Results.Add(TimePrimitive(TEXT("Damped 2D, 6 octaves"), Samples,
        [&Damped6](int32 I) { return Damped6.Sample(PositionX(I), PositionY(I)); }));
    Results.Add(TimePrimitive(TEXT("Warped fBm 2D, 6 octaves"), Samples,
        [&Warped6](int32 I) { return Warped6.Sample(PositionX(I), PositionY(I)); }));
    Results.Add(TimePrimitive(TEXT("Spline, 3 nested levels"), Samples,
        [&Spline](int32 I)
        {
            const float Coordinates[3] = {
                (I & 255) / 128.f - 1, ((I >> 8) & 255) / 128.f - 1, ((I >> 16) & 255) / 128.f - 1};
            return Spline.Evaluate(Coordinates);
        }));

    // A column's density corners at the reference's 4 x 8 x 4 cells: 5 x 5 x 49 points of 4-octave 3D fBm.
    {
        constexpr int32 CountXY = 5, CountZ = 49, PerColumn = CountXY * CountXY * CountZ;
        const int32 Columns = FMath::Max(1, Samples / PerColumn);
        TArray<float> Corners;
        Corners.SetNumUninitialized(PerColumn);
        FVoxelBenchResult Result;
        Result.Name = TEXT("Column corners, 3D fBm 4 octaves (5x5x49)");
        Result.Samples = int64(Columns) * PerColumn;
        const double Start = FPlatformTime::Seconds();
        for (int32 Column = 0; Column < Columns; ++Column)
        {
            Fbm4.SampleGrid((Column & 63) * 16., (Column >> 6) * 16., -64, 4, 8, CountXY, CountXY, CountZ, Corners);
            Result.Checksum += Corners[Column % PerColumn];
        }
        Result.Seconds = FPlatformTime::Seconds() - Start;
        Results.Add(Result);
    }

    // The same 3D fBm across every worker: what the generator's job graph can draw on.
    {
        const int32 Workers = FMath::Max(1, int32(LowLevelTasks::FScheduler::Get().GetNumWorkers()));
        const int32 Batches = Workers * 4, PerBatch = FMath::Max(1, Samples / 4);
        TArray<double> Sums;
        Sums.SetNumZeroed(Batches);
        FVoxelBenchResult Result;
        Result.Name = FString::Printf(TEXT("fBm 3D, 4 octaves, %d workers"), Workers);
        Result.Samples = int64(Batches) * PerBatch;
        Result.bParallel = true;
        const double Start = FPlatformTime::Seconds();
        ParallelFor(Batches,
            [&](int32 Batch)
            {
                double Sum = 0;
                for (int32 Index = 0; Index < PerBatch; ++Index)
                    Sum += Fbm4.Sample(PositionX(Index) + Batch * 1400., PositionY(Index), (Index & 63) * 2.1);
                Sums[Batch] = Sum;
            });
        Result.Seconds = FPlatformTime::Seconds() - Start;
        for (const double Sum : Sums)
            Result.Checksum += Sum;
        Results.Add(Result);
    }
    return Results;
}

FString VoxelBench::Describe(TConstArrayView<FVoxelBenchResult> Results)
{
    FString Out;
    for (const FVoxelBenchResult& Result : Results)
        Out +=
            FString::Printf(TEXT("%-46s %9.1f ns/sample %9.2f M/s%s\n"), *Result.Name, Result.GetNanosecondsPerSample(),
                Result.GetMillionsPerSecond(), Result.bParallel ? TEXT(" (all workers)") : TEXT(""));
    return Out;
}

TArray<FVoxelBenchResult> VoxelBench::RunClimate(const FVoxelClimateSampler& Sampler, int32 Radius)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelBench_RunClimate);
    const int32 Side = 2 * Radius + 1, Columns = Side * Side;
    TArray<FVoxelBenchResult> Results;

    // Each result's Samples counts columns, so nanoseconds per sample read as the time per column.
    FVoxelBenchResult Single;
    Single.Name = TEXT("Climate column, 5x5 corners, one thread");
    Single.Samples = Columns;
    {
        FVoxelDensityScratch Scratch;
        FVoxelColumnClimate Climate;
        // One untimed column sizes the scratch, as a worker's would be after its first column.
        Sampler.SampleColumn(0, 0, Scratch, Climate);
        const double Start = FPlatformTime::Seconds();
        for (int32 Index = 0; Index < Columns; ++Index)
        {
            Sampler.SampleColumn(Index % Side - Radius, Index / Side - Radius, Scratch, Climate);
            Single.Checksum += Climate.Samples[Index % 25].Values[Index % int32(EVoxelClimate::Count)];
        }
        Single.Seconds = FPlatformTime::Seconds() - Start;
    }
    Results.Add(Single);

    const int32 Workers = FMath::Max(1, int32(LowLevelTasks::FScheduler::Get().GetNumWorkers()));
    FVoxelBenchResult Parallel;
    Parallel.Name = FString::Printf(TEXT("Climate column, %d workers"), Workers);
    Parallel.Samples = Columns;
    Parallel.bParallel = true;
    TArray<double> Sums;
    Sums.SetNumZeroed(Columns);
    const double Start = FPlatformTime::Seconds();
    // One scratch per worker context, as the generator's workers would keep theirs.
    TArray<FVoxelDensityScratch> Scratches;
    ParallelForWithTaskContext(Scratches, Columns,
        [&](FVoxelDensityScratch& Scratch, int32 Index)
        {
            FVoxelColumnClimate Climate;
            Sampler.SampleColumn(Index % Side - Radius, Index / Side - Radius, Scratch, Climate);
            Sums[Index] = Climate.Samples[Index % 25].Values[Index % int32(EVoxelClimate::Count)];
        });
    Parallel.Seconds = FPlatformTime::Seconds() - Start;
    for (const double Sum : Sums)
        Parallel.Checksum += Sum;
    Results.Add(Parallel);
    return Results;
}

namespace
{
    FString DensityBenchStage(const TCHAR* Name, TArray<double>& Values)
    {
        Values.Sort();
        double Sum = 0;
        for (const double Value : Values)
            Sum += Value;
        const auto At = [&Values](double Fraction) { return Values[FMath::FloorToInt(Fraction * (Values.Num() - 1))]; };
        return FString::Printf(TEXT("  %-22s p50 %.3f  p95 %.3f  mean %.3f  max %.3f ms"), Name, At(.5), At(.95),
            Sum / Values.Num(), Values.Last());
    }
} // namespace

VoxelBench::FLoadOrderHashes VoxelBench::HashLoadOrders(const IVoxelTerrain& Terrain, int32 Radius)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelBench_HashLoadOrders);
    const int32 Side = 2 * Radius + 1, Columns = Side * Side;
    const auto HashColumn = [&Terrain, Side, Radius](int32 Index, TArray<uint16>& Dense)
    {
        FVoxelColumn Column;
        Terrain.Generate(FIntPoint(Index % Side - Radius, Index / Side - Radius), Column, Dense);
        return CityHash64(reinterpret_cast<const char*>(Dense.GetData()), Dense.Num() * sizeof(uint16));
    };
    TArray<uint64> Forward, Backward;
    Forward.SetNumZeroed(Columns);
    Backward.SetNumZeroed(Columns);
    TArray<uint16> Dense;
    for (int32 Index = 0; Index < Columns; ++Index)
        Forward[Index] = HashColumn(Index, Dense);
    ParallelFor(Columns,
        [&](int32 Step)
        {
            TArray<uint16> WorkerDense;
            const int32 Index = Columns - 1 - Step;
            Backward[Index] = HashColumn(Index, WorkerDense);
        });

    FLoadOrderHashes Result;
    Result.Columns = Columns;
    for (int32 Index = 0; Index < Columns; ++Index)
        Result.Mismatches += Forward[Index] != Backward[Index];
    Result.Forward = CityHash64(reinterpret_cast<const char*>(Forward.GetData()), Columns * sizeof(uint64));
    Result.Backward = CityHash64(reinterpret_cast<const char*>(Backward.GetData()), Columns * sizeof(uint64));
    return Result;
}

FString VoxelBench::RunColumns(const FVoxelBlockTable& Table, const IVoxelTerrain& Terrain,
    const FVoxelDensityTerrain* Density, const FVoxelMeshOptions& Options, int32 Radius)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelBench_RunColumns);
    FString Report;
    FVoxelArea Area;
    for (const bool bParallel : {false, true})
    {
        FVoxelArea::Build(Table, Terrain, FIntPoint::ZeroValue, Radius, Options, bParallel, Area);
        Report += Area.Describe() + TEXT("\n");
    }

    const int32 Side = 2 * Radius + 1, Columns = Side * Side;
    if (Density)
    {
        TArray<double> Total, DensityMs, Aquifer, Blocks;
        TArray<uint16> Dense;
        FVoxelColumn Column;
        for (int32 Index = 0; Index < Columns; ++Index)
        {
            FVoxelDensityTerrainTimings Timings;
            const double Start = FPlatformTime::Seconds();
            Density->Generate(FIntPoint(Index % Side - Radius, Index / Side - Radius), Column, Dense, &Timings);
            Total.Add((FPlatformTime::Seconds() - Start) * 1000);
            DensityMs.Add(Timings.DensityMs);
            Aquifer.Add(Timings.AquiferMs);
            Blocks.Add(Timings.BlocksMs);
        }
        Report += FString::Printf(TEXT("Generation stages, %d columns, one thread:\n"), Columns);
        Report += DensityBenchStage(TEXT("Generate column"), Total) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  density"), DensityMs) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  aquifer"), Aquifer) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  blocks"), Blocks) + TEXT("\n");
    }

    const FLoadOrderHashes Hashes = HashLoadOrders(Terrain, Radius);
    Report += FString::Printf(TEXT("Block hashes, %d columns: row by row on one thread 0x%016llx, backwards across "
                                   "workers 0x%016llx, %d columns differ"),
        Hashes.Columns, Hashes.Forward, Hashes.Backward, Hashes.Mismatches);
    return Report;
}

FString VoxelBench::RunDensity(const FVoxelDensityTerrain& Terrain, int32 Radius)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelBench_RunDensity);
    const int32 Side = 2 * Radius + 1, Columns = Side * Side;
    FString Report;
    TArray<uint16> Dense, Skipped;
    FVoxelColumn Column;
    // One untimed column sizes the scratch, as a worker's would be after its first column.
    Terrain.Generate(FIntPoint::ZeroValue, Column, Dense);

    int64 Differences = 0;
    for (const bool bSkip : {true, false})
    {
        TArray<double> Total, Density, Aquifer, Blocks, Flat, Bounds, Corners, Refine, Cells;
        int64 SolidCells = 0, EmptyCells = 0, RefinedCells = 0, AllCells = 0, CornerCount = 0;
        for (int32 Index = 0; Index < Columns; ++Index)
        {
            const FIntPoint Position(Index % Side - Radius, Index / Side - Radius);
            FVoxelDensityTerrainTimings Timings;
            const double Start = FPlatformTime::Seconds();
            Terrain.Generate(Position, Column, Dense, &Timings, &bSkip);
            Total.Add((FPlatformTime::Seconds() - Start) * 1000);
            Density.Add(Timings.DensityMs);
            Aquifer.Add(Timings.AquiferMs);
            Blocks.Add(Timings.BlocksMs);
            Flat.Add(Timings.Density.FlatMs);
            Bounds.Add(Timings.Density.BoundsMs);
            Corners.Add(Timings.Density.CornersMs);
            Refine.Add(Timings.Density.RefineMs);
            Cells.Add(Timings.Density.BlocksMs);
            SolidCells += Timings.Density.SolidCells;
            EmptyCells += Timings.Density.EmptyCells;
            RefinedCells += Timings.Density.RefinedCells;
            AllCells += Timings.Density.Cells;
            CornerCount += Timings.Density.CornersEvaluated;
            if (bSkip)
                Skipped.Append(Dense);
            else
                for (int32 Cell = 0; Cell < Dense.Num(); ++Cell)
                    Differences += Dense[Cell] != Skipped[Index * Dense.Num() + Cell];
        }
        Report += FString::Printf(TEXT("%d columns, one thread, skipping %s: %.1f%% of cells skipped (%.1f%% solid, "
                                       "%.1f%% empty) from bounds, %.1f%% more from corners, %.0f corners "
                                       "evaluated per column\n"),
            Columns, bSkip ? TEXT("on") : TEXT("off"), 100. * (SolidCells + EmptyCells) / AllCells,
            100. * SolidCells / AllCells, 100. * EmptyCells / AllCells, 100. * RefinedCells / AllCells,
            double(CornerCount) / Columns);
        Report += DensityBenchStage(TEXT("Generate column"), Total) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  density"), Density) + TEXT("\n");
        Report += DensityBenchStage(TEXT("    flat (climate)"), Flat) + TEXT("\n");
        Report += DensityBenchStage(TEXT("    bounds"), Bounds) + TEXT("\n");
        Report += DensityBenchStage(TEXT("    corners"), Corners) + TEXT("\n");
        Report += DensityBenchStage(TEXT("    cells from corners"), Refine) + TEXT("\n");
        Report += DensityBenchStage(TEXT("    cells to blocks"), Cells) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  aquifer"), Aquifer) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  blocks"), Blocks) + TEXT("\n");
    }
    Report += FString::Printf(TEXT("Blocks that differ with skipping on and off: %lld of %lld\n"), Differences,
        int64(Columns) * FVoxelColumn::DenseVolume);

    const int32 Workers = FMath::Max(1, int32(LowLevelTasks::FScheduler::Get().GetNumWorkers()));
    const double Start = FPlatformTime::Seconds();
    ParallelFor(Columns,
        [&](int32 Index)
        {
            TArray<uint16> WorkerDense;
            FVoxelColumn WorkerColumn;
            Terrain.Generate(FIntPoint(Index % Side - Radius, Index / Side - Radius), WorkerColumn, WorkerDense);
        });
    const double Seconds = FPlatformTime::Seconds() - Start;
    Report += FString::Printf(TEXT("%d columns across %d workers: %.1f ms wall, %.3f ms per column"), Columns, Workers,
        Seconds * 1000, Seconds * 1000 / Columns);
    return Report;
}
