#include "VoxelBench.h"
#include "Biome/VoxelBiomeStorage.h"
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

    // Fills (2 Radius + 1)^2 columns' biomes with the pruned search and by brute force, across every worker (brute
    // force takes a fraction of a second per column), and counts the quarts that differ: always 0 unless the pruning is
    // wrong.
    FString CompareBiomeFills(const FVoxelBiomeSource& Source, int32 Radius)
    {
        const int32 Side = 2 * Radius + 1, Columns = Side * Side;
        TArray<int64> Differ;
        Differ.SetNumZeroed(Columns);
        TArray<double> BruteMs;
        BruteMs.SetNumZeroed(Columns);
        ParallelFor(Columns,
            [&](int32 Index)
            {
                const FIntPoint Position(Index % Side - Radius, Index / Side - Radius);
                FVoxelDensityScratch Scratch;
                FVoxelBiomeStorage Pruned, Brute;
                Source.FillColumn(Position, Scratch, Pruned, EVoxelBiomeFill::Pruned);
                const double Start = FPlatformTime::Seconds();
                Source.FillColumn(Position, Scratch, Brute, EVoxelBiomeFill::BruteForce);
                BruteMs[Index] = (FPlatformTime::Seconds() - Start) * 1000;
                for (int32 Quart = 0; Quart < FVoxelBiomeStorage::Volume; ++Quart)
                    Differ[Index] += Pruned.GetQuarts()[Quart] != Brute.GetQuarts()[Quart];
            });
        int64 Total = 0;
        double Ms = 0;
        for (int32 Index = 0; Index < Columns; ++Index)
        {
            Total += Differ[Index];
            Ms += BruteMs[Index];
        }
        return FString::Printf(TEXT("Biome quarts that differ, pruned against brute force: %lld of %lld (%d columns; "
                                    "brute force %.0f ms per column on a worker)"),
            Total, int64(Columns) * FVoxelBiomeStorage::Volume, Columns, Ms / Columns);
    }
} // namespace

VoxelBench::FLoadOrderHashes VoxelBench::HashLoadOrders(const IVoxelTerrain& Terrain, int32 Radius)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelBench_HashLoadOrders);
    const int32 Side = 2 * Radius + 1, Columns = Side * Side;
    // A column's block hash and its biome hash.
    const auto HashColumn = [&Terrain, Side, Radius](int32 Index, TArray<uint16>& Dense, uint64& OutBiomes)
    {
        FVoxelColumn Column;
        Terrain.Generate(FIntPoint(Index % Side - Radius, Index / Side - Radius), Column, Dense);
        const TConstArrayView<uint8> Quarts = Column.GetBiomes().GetQuarts();
        OutBiomes = CityHash64(reinterpret_cast<const char*>(Quarts.GetData()), Quarts.Num());
        return CityHash64(reinterpret_cast<const char*>(Dense.GetData()), Dense.Num() * sizeof(uint16));
    };
    TArray<uint64> Forward, Backward, BiomeForward, BiomeBackward;
    Forward.SetNumZeroed(Columns);
    Backward.SetNumZeroed(Columns);
    BiomeForward.SetNumZeroed(Columns);
    BiomeBackward.SetNumZeroed(Columns);
    TArray<uint16> Dense;
    // Each order starts cold, so no column reads work the other order shared.
    Terrain.ResetCaches();
    for (int32 Index = 0; Index < Columns; ++Index)
        Forward[Index] = HashColumn(Index, Dense, BiomeForward[Index]);
    Terrain.ResetCaches();
    ParallelFor(Columns,
        [&](int32 Step)
        {
            TArray<uint16> WorkerDense;
            const int32 Index = Columns - 1 - Step;
            Backward[Index] = HashColumn(Index, WorkerDense, BiomeBackward[Index]);
        });

    FLoadOrderHashes Result;
    Result.Columns = Columns;
    for (int32 Index = 0; Index < Columns; ++Index)
    {
        Result.Mismatches += Forward[Index] != Backward[Index];
        Result.BiomeMismatches += BiomeForward[Index] != BiomeBackward[Index];
    }
    const auto Combine = [Columns](const TArray<uint64>& Hashes)
    { return CityHash64(reinterpret_cast<const char*>(Hashes.GetData()), Columns * sizeof(uint64)); };
    Result.Forward = Combine(Forward);
    Result.Backward = Combine(Backward);
    Result.BiomeForward = Combine(BiomeForward);
    Result.BiomeBackward = Combine(BiomeBackward);
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
        Terrain.ResetCaches();
        FVoxelArea::Build(Table, Terrain, FIntPoint::ZeroValue, Radius, Options, bParallel, Area);
        Report += Area.Describe() + TEXT("\n");
    }

    const int32 Side = 2 * Radius + 1, Columns = Side * Side;
    if (Density)
    {
        TArray<double> Total, DensityMs, Aquifer, Biomes, Blocks, Surface;
        TArray<uint16> Dense;
        FVoxelColumn Column;
        Terrain.ResetCaches();
        for (int32 Index = 0; Index < Columns; ++Index)
        {
            FVoxelDensityTerrainTimings Timings;
            const double Start = FPlatformTime::Seconds();
            Density->Generate(FIntPoint(Index % Side - Radius, Index / Side - Radius), Column, Dense, &Timings);
            Total.Add((FPlatformTime::Seconds() - Start) * 1000);
            DensityMs.Add(Timings.DensityMs);
            Aquifer.Add(Timings.AquiferMs);
            Biomes.Add(Timings.BiomesMs);
            Blocks.Add(Timings.BlocksMs);
            Surface.Add(Timings.SurfaceMs);
        }
        Report += FString::Printf(TEXT("Generation stages, %d columns, one thread:\n"), Columns);
        Report += DensityBenchStage(TEXT("Generate column"), Total) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  density"), DensityMs) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  aquifer"), Aquifer) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  biomes"), Biomes) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  blocks"), Blocks) + TEXT("\n");
        Report += DensityBenchStage(TEXT("    surface"), Surface) + TEXT("\n");
    }

    const FLoadOrderHashes Hashes = HashLoadOrders(Terrain, Radius);
    Report += FString::Printf(TEXT("Block hashes, %d columns: row by row on one thread 0x%016llx, backwards across "
                                   "workers 0x%016llx, %d columns differ"),
        Hashes.Columns, Hashes.Forward, Hashes.Backward, Hashes.Mismatches);
    if (Density && Density->GetBiomeSource())
        Report += FString::Printf(TEXT("\nBiome hashes: row by row 0x%016llx, backwards 0x%016llx, %d columns differ"),
            Hashes.BiomeForward, Hashes.BiomeBackward, Hashes.BiomeMismatches);
    return Report;
}

FString VoxelBench::RunDensity(const FVoxelDensityTerrain& Terrain, int32 Radius)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelBench_RunDensity);
    const int32 Side = 2 * Radius + 1, Columns = Side * Side;
    FString Report;
    TArray<uint16> Dense;
    FVoxelColumn Column;
    // One untimed column sizes the scratch, as a worker's would be after its first column.
    Terrain.Generate(FIntPoint::ZeroValue, Column, Dense);

    // Passes, each starting cold: the default (skipping, corner sharing, biomes, surface rules with the shortcut), then
    // one setting changed at a time. Row by row, so with sharing each column finds the corners its west and north
    // neighbours evaluated: the best case. Each pass but rules off must give the blocks of the pass it names.
    struct FPass
    {
        const TCHAR* Label;
        FVoxelDensityTerrainOverrides Overrides;
        // The pass whose blocks this one must equal; INDEX_NONE: timed only.
        int32 Compare;
        const TCHAR* Gate;
    };
    const bool bHasBiomes = Terrain.GetBiomeSource() != nullptr;
    const bool bHasRules = bHasBiomes && Terrain.GetSurfaceRules().IsCompiled();
    TArray<FPass> Passes;
    Passes.Add({TEXT("default"), {}, INDEX_NONE, nullptr});
    if (bHasRules)
    {
        FVoxelDensityTerrainOverrides Shortcut;
        Shortcut.bSurfaceShortcut = false;
        Passes.Add({TEXT("surface shortcut off"), Shortcut, 0, TEXT("the surface shortcut on and off")});
        FVoxelDensityTerrainOverrides Rules;
        Rules.bSurfaceRules = false;
        Passes.Add({TEXT("surface rules off"), Rules, INDEX_NONE, nullptr});
    }
    if (bHasBiomes)
    {
        // Without rules no block reads its biome, so biomes off must give the rules-off blocks.
        FVoxelDensityTerrainOverrides BiomesOff;
        BiomesOff.bSurfaceRules = false;
        BiomesOff.BiomeFill = EVoxelBiomeFill::Off;
        Passes.Add({TEXT("biomes off, rules off"), BiomesOff, bHasRules ? Passes.Num() - 1 : 0,
            TEXT("biomes on and off (rules off)")});
    }
    FVoxelDensityTerrainOverrides ShareOff;
    ShareOff.bShareCorners = false;
    Passes.Add({TEXT("sharing off"), ShareOff, 0, TEXT("sharing on and off")});
    FVoxelDensityTerrainOverrides SkipOff;
    SkipOff.bSkip = false;
    SkipOff.bShareCorners = false;
    Passes.Add({TEXT("skipping and sharing off"), SkipOff, 0, TEXT("skipping on and off")});

    TArray<TArray<uint16>> Kept;
    Kept.SetNum(Passes.Num());
    TArray<int64> Differences;
    Differences.Init(0, Passes.Num());
    int64 RulesDifferences = 0;
    for (int32 PassIndex = 0; PassIndex < Passes.Num(); ++PassIndex)
    {
        const FPass& Pass = Passes[PassIndex];
        TArray<double> Total, Density, Aquifer, Biomes, Blocks, Surface, Preliminary, Freeze, Flat, Bounds, Corners,
            Refine, Cells;
        int64 SolidCells = 0, EmptyCells = 0, RefinedCells = 0, AllCells = 0, CornerCount = 0, SharedCount = 0;
        int64 DeepBlocks = 0, InteriorBlocks = 0, FullBlocks = 0, Steps = 0, InteriorRuns = 0;
        // Keep the blocks of every pass another names, and of the default for the rules-off count.
        const bool bKeep = PassIndex == 0 ||
            Passes.ContainsByPredicate([PassIndex](const FPass& Other) { return Other.Compare == PassIndex; });
        Terrain.ResetCaches();
        for (int32 Index = 0; Index < Columns; ++Index)
        {
            const FIntPoint Position(Index % Side - Radius, Index / Side - Radius);
            FVoxelDensityTerrainTimings Timings;
            const double Start = FPlatformTime::Seconds();
            Terrain.Generate(Position, Column, Dense, &Timings, Pass.Overrides);
            Total.Add((FPlatformTime::Seconds() - Start) * 1000);
            Density.Add(Timings.DensityMs);
            Aquifer.Add(Timings.AquiferMs);
            Biomes.Add(Timings.BiomesMs);
            Blocks.Add(Timings.BlocksMs);
            Surface.Add(Timings.SurfaceMs);
            Preliminary.Add(Timings.PreliminaryMs);
            Freeze.Add(Timings.FreezeMs);
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
            SharedCount += Timings.Density.CornersShared;
            DeepBlocks += Timings.SurfaceDeep;
            InteriorBlocks += Timings.SurfaceInterior;
            FullBlocks += Timings.SurfaceFull;
            Steps += Timings.SurfaceSteps;
            InteriorRuns += Timings.SurfaceInteriorRuns;
            if (bKeep) Kept[PassIndex].Append(Dense);
            const int32 Against = Pass.Compare != INDEX_NONE ? Pass.Compare : (PassIndex > 0 ? 0 : INDEX_NONE);
            if (Against == INDEX_NONE) continue;
            int64& Count = Pass.Compare != INDEX_NONE ? Differences[PassIndex] : RulesDifferences;
            for (int32 Cell = 0; Cell < Dense.Num(); ++Cell)
                Count += Dense[Cell] != Kept[Against][Index * Dense.Num() + Cell];
        }
        Report += FString::Printf(TEXT("%d columns, one thread, %s: %.1f%% of cells skipped (%.1f%% solid, %.1f%% "
                                       "empty) from bounds, %.1f%% more from corners, %.0f corners evaluated and %.0f "
                                       "shared per column\n"),
            Columns, Pass.Label, 100. * (SolidCells + EmptyCells) / AllCells, 100. * SolidCells / AllCells,
            100. * EmptyCells / AllCells, 100. * RefinedCells / AllCells, double(CornerCount) / Columns,
            double(SharedCount) / Columns);
        Report += DensityBenchStage(TEXT("Generate column"), Total) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  density"), Density) + TEXT("\n");
        Report += DensityBenchStage(TEXT("    flat (climate)"), Flat) + TEXT("\n");
        Report += DensityBenchStage(TEXT("    bounds"), Bounds) + TEXT("\n");
        Report += DensityBenchStage(TEXT("    corners"), Corners) + TEXT("\n");
        Report += DensityBenchStage(TEXT("    cells from corners"), Refine) + TEXT("\n");
        Report += DensityBenchStage(TEXT("    cells to blocks"), Cells) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  aquifer"), Aquifer) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  biomes"), Biomes) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  blocks"), Blocks) + TEXT("\n");
        Report += DensityBenchStage(TEXT("    surface"), Surface) + TEXT("\n");
        Report += DensityBenchStage(TEXT("      preliminary"), Preliminary) + TEXT("\n");
        Report += DensityBenchStage(TEXT("      freeze"), Freeze) + TEXT("\n");
        if (FullBlocks > 0)
            Report += FString::Printf(TEXT("  surface blocks per column: %.0f deep, %.0f interior (%.0f heights "
                                           "built), %.0f through the tree (%.1f of %d instructions each)\n"),
                double(DeepBlocks) / Columns, double(InteriorBlocks) / Columns, double(InteriorRuns) / Columns,
                double(FullBlocks) / Columns, double(Steps) / FullBlocks, Terrain.GetSurfaceRules().GetProgramSize());
    }
    const int64 AllBlocks = int64(Columns) * FVoxelColumn::DenseVolume;
    for (int32 PassIndex = 0; PassIndex < Passes.Num(); ++PassIndex)
        if (Passes[PassIndex].Gate)
            Report += FString::Printf(TEXT("Blocks that differ with %s: %lld of %lld\n"), Passes[PassIndex].Gate,
                Differences[PassIndex], AllBlocks);
    if (bHasRules)
        Report += FString::Printf(TEXT("Blocks the surface rules change (rules on against off): %lld of %lld\n"),
            RulesDifferences, AllBlocks);
    if (bHasBiomes) Report += CompareBiomeFills(*Terrain.GetBiomeSource(), FMath::Min(Radius, 1)) + TEXT("\n");
    Report += FString::Printf(TEXT("Corner cache %.1f MB\n"), Terrain.GetCornerCacheBytes() / 1048576.);

    const int32 Workers = FMath::Max(1, int32(LowLevelTasks::FScheduler::Get().GetNumWorkers()));
    Terrain.ResetCaches();
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
