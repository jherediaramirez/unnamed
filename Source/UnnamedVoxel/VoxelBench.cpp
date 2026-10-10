#include "VoxelBench.h"
#include "Biome/VoxelBiomeStorage.h"
#include "Biome/VoxelBiomeTints.h"
#include "Decoration/VoxelDecorator.h"
#include "Meshing/VoxelMesher.h"
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

uint64 VoxelBench::HashColumnBlocks(const FVoxelColumn& Column)
{
    TArray<uint16> Dense;
    Dense.SetNumUninitialized(FVoxelColumn::DenseVolume);
    for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
        Column.GetSection(Section).Expand(
            TArrayView<uint16>(Dense.GetData() + Section * FVoxelSection::Volume, FVoxelSection::Volume));
    return CityHash64(reinterpret_cast<const char*>(Dense.GetData()), Dense.Num() * sizeof(uint16));
}

VoxelBench::FLoadOrderHashes VoxelBench::HashLoadOrders(
    const IVoxelTerrain& Terrain, int32 Radius, const FVoxelDecorator* Decorator)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelBench_HashLoadOrders);
    const int32 Side = 2 * Radius + 1, Columns = Side * Side;
    const auto Combine = [Columns](const TArray<uint64>& Hashes)
    { return CityHash64(reinterpret_cast<const char*>(Hashes.GetData()), Columns * sizeof(uint64)); };
    if (Decorator)
    {
        TArray<uint64> Hashes[2], Biomes[2];
        for (int32 Order = 0; Order < 2; ++Order)
        {
            Terrain.ResetCaches();
            FVoxelDecoratedArea Area;
            FVoxelDecoratedArea::Build(*Decorator, Terrain, FIntPoint::ZeroValue, Radius, Order == 1, Order == 1, Area);
            for (const TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>& Column : Area.Columns)
            {
                Hashes[Order].Add(HashColumnBlocks(*Column));
                const TConstArrayView<uint8> Quarts = Column->GetBiomes().GetQuarts();
                Biomes[Order].Add(CityHash64(reinterpret_cast<const char*>(Quarts.GetData()), Quarts.Num()));
            }
        }
        FLoadOrderHashes Result;
        Result.Columns = Columns;
        for (int32 Index = 0; Index < Columns; ++Index)
        {
            Result.Mismatches += Hashes[0][Index] != Hashes[1][Index];
            Result.BiomeMismatches += Biomes[0][Index] != Biomes[1][Index];
        }
        Result.Forward = Combine(Hashes[0]);
        Result.Backward = Combine(Hashes[1]);
        Result.BiomeForward = Combine(Biomes[0]);
        Result.BiomeBackward = Combine(Biomes[1]);
        return Result;
    }
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
    Result.Forward = Combine(Forward);
    Result.Backward = Combine(Backward);
    Result.BiomeForward = Combine(BiomeForward);
    Result.BiomeBackward = Combine(BiomeBackward);
    return Result;
}

namespace
{
    // One mesh pass over a built area on one thread, with Options and the terrain's tints.
    struct FMeshPass
    {
        int64 Quads = 0;
        int64 LayerQuads[int32(EVoxelLayer::Count)] = {};
        double Ms = 0;
    };

    void GetAreaNeighborhood(const FVoxelArea& Area, int32 ColumnIndex, FVoxelNeighborhood& Out)
    {
        const FIntPoint Position = Area.Columns[ColumnIndex]->GetPosition();
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
            {
                const int32 Neighbor = Area.GetColumnIndex(Position + FIntPoint(DX, DY));
                Out.Set(DX, DY, Neighbor == INDEX_NONE ? nullptr : Area.Columns[Neighbor].Get());
            }
    }

    FMeshPass MeshArea(const FVoxelBlockTable& Table, const FVoxelArea& Area, const FVoxelTintTable* Tints,
        const FVoxelMeshOptions& Options)
    {
        FMeshPass Pass;
        TUniquePtr<FVoxelPaddedSection> Padded = MakeUnique<FVoxelPaddedSection>();
        FVoxelMeshData Data;
        for (int32 ColumnIndex = 0; ColumnIndex < Area.Columns.Num(); ++ColumnIndex)
        {
            FVoxelNeighborhood Neighborhood;
            GetAreaNeighborhood(Area, ColumnIndex, Neighborhood);
            for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
            {
                VoxelMesher::GatherPadded(Neighborhood, Section, *Padded);
                const double Start = FPlatformTime::Seconds();
                VoxelMesher::MeshSection(Table, *Padded, Section, Options, Data, Tints, &Neighborhood);
                Pass.Ms += (FPlatformTime::Seconds() - Start) * 1000.;
                Pass.Quads += Data.GetQuadCount();
                for (int32 Layer = 0; Layer < int32(EVoxelLayer::Count); ++Layer)
                    Pass.LayerQuads[Layer] += Data.Layers[Layer].GetQuadCount();
            }
        }
        return Pass;
    }

    // Vertices whose tint differs between two option sets that must give the same mesh otherwise; INDEX_NONE when
    // the meshes themselves differ.
    int64 CountTintDifferences(const FVoxelBlockTable& Table, const FVoxelArea& Area, const FVoxelTintTable* Tints,
        const FVoxelMeshOptions& A, const FVoxelMeshOptions& B, int64& OutVertices)
    {
        int64 Differences = 0;
        OutVertices = 0;
        TUniquePtr<FVoxelPaddedSection> Padded = MakeUnique<FVoxelPaddedSection>();
        FVoxelMeshData DataA, DataB;
        for (int32 ColumnIndex = 0; ColumnIndex < Area.Columns.Num(); ++ColumnIndex)
        {
            FVoxelNeighborhood Neighborhood;
            GetAreaNeighborhood(Area, ColumnIndex, Neighborhood);
            for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
            {
                VoxelMesher::GatherPadded(Neighborhood, Section, *Padded);
                VoxelMesher::MeshSection(Table, *Padded, Section, A, DataA, Tints, &Neighborhood);
                VoxelMesher::MeshSection(Table, *Padded, Section, B, DataB, Tints, &Neighborhood);
                for (int32 Layer = 0; Layer < int32(EVoxelLayer::Count); ++Layer)
                {
                    const FVoxelMeshLayer &LayerA = DataA.Layers[Layer], &LayerB = DataB.Layers[Layer];
                    if (LayerA.Positions != LayerB.Positions || LayerA.TintFaces.Num() != LayerB.TintFaces.Num())
                        return INDEX_NONE;
                    OutVertices += LayerA.TintFaces.Num();
                    for (int32 Vertex = 0; Vertex < LayerA.TintFaces.Num(); ++Vertex)
                        Differences += LayerA.TintFaces[Vertex] != LayerB.TintFaces[Vertex];
                }
            }
        }
        return Differences;
    }
}

FString VoxelBench::RunColumns(const FVoxelBlockTable& Table, const IVoxelTerrain& Terrain,
    const FVoxelDensityTerrain* Density, const FVoxelMeshOptions& Options, int32 Radius,
    const FVoxelDecorator* Decorator)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelBench_RunColumns);
    FString Report;
    FVoxelArea Area;
    for (const bool bParallel : {false, true})
    {
        Terrain.ResetCaches();
        FVoxelArea::Build(Table, Terrain, FIntPoint::ZeroValue, Radius, Options, bParallel, Area, Decorator);
        Report += Area.Describe() + TEXT("\n");
    }

    // Tints: the same columns meshed again on one thread with biome tints off, blend 0 and blend 2, then the uniform-
    // biome shortcut's gate.
    if (const FVoxelTintTable* Tints = Terrain.GetTints())
    {
        const int32 AreaColumns = Area.Columns.Num();
        FMeshPass Passes[3];
        const int32 Blends[3] = {-1, 0, 2};
        for (int32 Index = 0; Index < 3; ++Index)
        {
            FVoxelMeshOptions Pass = Options;
            Pass.TintBlend = Blends[Index];
            Passes[Index] = MeshArea(Table, Area, Tints, Pass);
        }
        Report += FString::Printf(
            TEXT("Tints, %d columns meshed on one thread (quads per column, mesh ms per column):"), AreaColumns);
        const TCHAR* Labels[3] = {TEXT("off"), TEXT("blend 0"), TEXT("blend 2")};
        for (int32 Index = 0; Index < 3; ++Index)
            Report += FString::Printf(TEXT("\n  %-8s %8.1f quads (%+.1f%%), %.3f ms (%+.1f%%)"), Labels[Index],
                double(Passes[Index].Quads) / AreaColumns,
                100. * (double(Passes[Index].Quads) / FMath::Max<int64>(1, Passes[0].Quads) - 1.),
                Passes[Index].Ms / AreaColumns, 100. * (Passes[Index].Ms / FMath::Max(1e-9, Passes[0].Ms) - 1.));
        for (const int32 Blend : {0, 2})
        {
            FVoxelMeshOptions On = Options, Off = Options;
            On.TintBlend = Off.TintBlend = Blend;
            Off.bTintShortcut = false;
            int64 Vertices = 0;
            const int64 Differences = CountTintDifferences(Table, Area, Tints, On, Off, Vertices);
            Report += Differences == INDEX_NONE
                ? FString::Printf(TEXT("\nTint shortcut, blend %d: the meshes differ"), Blend)
                : FString::Printf(
                      TEXT("\nVertex tints that differ with the tint shortcut on and off, blend %d: %lld of %lld"),
                      Blend, Differences, Vertices);
        }
        Report += TEXT("\n");

        // Merging by look against merging by state, layer by layer (leaves are most of the cutout layer).
        FMeshPass Merges[2];
        for (int32 Index = 0; Index < 2; ++Index)
        {
            FVoxelMeshOptions Pass = Options;
            Pass.bMergeLooks = Index == 1;
            Merges[Index] = MeshArea(Table, Area, Tints, Pass);
        }
        Report += FString::Printf(TEXT("Merging, %d columns meshed on one thread (quads per column: all, then opaque, "
                                       "cutout, translucent, water, model; mesh ms per column):"),
            AreaColumns);
        const TCHAR* MergeLabels[2] = {TEXT("by state"), TEXT("by look")};
        for (int32 Index = 0; Index < 2; ++Index)
        {
            Report += FString::Printf(TEXT("\n  %-8s %8.1f quads (%+.1f%%):"), MergeLabels[Index],
                double(Merges[Index].Quads) / AreaColumns,
                100. * (double(Merges[Index].Quads) / FMath::Max<int64>(1, Merges[0].Quads) - 1.));
            for (const int64 LayerQuads : Merges[Index].LayerQuads)
                Report += FString::Printf(TEXT(" %.1f"), double(LayerQuads) / AreaColumns);
            Report += FString::Printf(TEXT("; %.3f ms (%+.1f%%)"), Merges[Index].Ms / AreaColumns,
                100. * (Merges[Index].Ms / FMath::Max(1e-9, Merges[0].Ms) - 1.));
        }
        Report += TEXT("\n");
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

    const FLoadOrderHashes Hashes = HashLoadOrders(Terrain, Radius, Decorator);
    if (Decorator) Report += TEXT("Decorated (voxel.Decoration 1). ");
    Report += FString::Printf(TEXT("Block hashes, %d columns: row by row on one thread 0x%016llx, backwards across "
                                   "workers 0x%016llx, %d columns differ"),
        Hashes.Columns, Hashes.Forward, Hashes.Backward, Hashes.Mismatches);
    if (Density && Density->GetBiomeSource())
        Report += FString::Printf(TEXT("\nBiome hashes: row by row 0x%016llx, backwards 0x%016llx, %d columns differ"),
            Hashes.BiomeForward, Hashes.BiomeBackward, Hashes.BiomeMismatches);
    return Report;
}

FString VoxelBench::RunVeinProbe(
    TConstArrayView<const FVoxelOreVeins*> VeinSets, TConstArrayView<int64> Seeds, int32 Toggle, int32 Mask)
{
    check(VeinSets.Num() == Seeds.Num());
    FString Report;
    for (const FVoxelOreVeins* Set : VeinSets)
        if (Set->Num() < 2 || !Set->GetEvaluator().IsCompiledNode(Toggle) || !Set->GetEvaluator().IsCompiledNode(Mask))
            return TEXT("The probe needs two veins whose densities read toggle and mask.");
    TArray<double> Dense[2], Placed[2];
    TArray<float> Toggles, Masks, GapNoise;
    for (int32 Index = 0; Index < Seeds.Num(); ++Index)
    {
        const FVoxelOreVeins& Veins = *VeinSets[Index];
        const FVoxelDensityEvaluator& Evaluator = Veins.GetEvaluator();
        const uint64 Seed = uint64(Seeds[Index]);
        FVoxelDensityScratch Scratch;
        const auto ToggleAt = [&](double X, double Y, double Z)
        {
            Evaluator.Evaluate(&X, &Y, &Z, 1, Scratch);
            return Evaluator.GetValues(Scratch, Toggle)[0];
        };
        FIntPoint Chunks[2] = {FIntPoint(-1), FIntPoint(-1)};
        for (int32 I = 0; I < 32 * 32 && (Chunks[0].X < 0 || Chunks[1].X < 0); ++I)
        {
            const int32 CX = I % 32, CZ = I / 32;
            if (Chunks[0].X < 0 && ToggleAt(CX * 16 + 8, 25, CZ * 16 + 8) >= .5f)
                Chunks[0] = {CX, CZ};
            else if (Chunks[1].X < 0 && ToggleAt(CX * 16 + 8, -34, CZ * 16 + 8) <= -.5f)
                Chunks[1] = {CX, CZ};
        }
        if (Chunks[0].X < 0 || Chunks[1].X < 0)
        {
            Report += FString::Printf(TEXT("seed %lld: no vein chunks found\n"), Seeds[Index]);
            continue;
        }
        int32 Agreement[2][3] = {};
        TArray<double> X, Y, Z;
        for (const FIntPoint Chunk : Chunks)
            for (int32 Height = -64; Height <= 56; ++Height)
            {
                X.Reset();
                Y.Reset();
                Z.Reset();
                for (int32 LZ = 0; LZ < 16; ++LZ)
                    for (int32 LX = 0; LX < 16; ++LX)
                    {
                        X.Add(Chunk.X * 16 + LX);
                        Y.Add(Height);
                        Z.Add(Chunk.Y * 16 + LZ);
                    }
                Evaluator.Evaluate(X.GetData(), Y.GetData(), Z.GetData(), X.Num(), Scratch);
                for (int32 K = 0; K < X.Num(); ++K)
                {
                    bool bVein = false;
                    for (int32 Vein = 0; Vein < FMath::Min(2, Veins.Num()); ++Vein)
                    {
                        const FVoxelOreVein& Data = Veins.GetVein(Vein);
                        const float Density = Evaluator.GetValues(Scratch, Data.Density)[K];
                        const FVoxelOreVeins::EOutcome Outcome =
                            FVoxelOreVeins::ApplyGap(FVoxelOreVeins::Draw(Data, Seed, int32(X[K]), int32(Z[K]), Height,
                                                         Density, Evaluator.GetValues(Scratch, Data.Richness)[K]),
                                Evaluator.GetValues(Scratch, Data.Gap)[K]);
                        const bool bDense = Density > 0, bPlaced = Outcome != FVoxelOreVeins::EOutcome::Nothing;
                        if (bDense || bPlaced) ++Agreement[Vein][bPlaced ? (bDense ? 0 : 2) : 1];
                        bVein |= bPlaced;
                    }
                    // The probe's uniform sample: one position in 64 of those without a vein block.
                    if (bVein ||
                        VoxelRandom::ToIndex(VoxelRandom::Hash64(Seed, VoxelRandom::Salt("vein_probe"), int32(X[K]),
                                                 int32(Z[K]), Height),
                            64) != 0)
                        continue;
                    const float Value = Evaluator.GetValues(Scratch, Toggle)[K];
                    Toggles.Add(Value);
                    // The gap's noise: gap = -0.3 - noise in the data.
                    GapNoise.Add(-.3f - Evaluator.GetValues(Scratch, Veins.GetVein(0).Gap)[K]);
                    if (FMath::Abs(Value) >= .4f) Masks.Add(Evaluator.GetValues(Scratch, Mask)[K]);
                }
            }
        for (int32 Vein = 0; Vein < 2; ++Vein)
        {
            Dense[Vein].Add(Agreement[Vein][0] + Agreement[Vein][1]);
            Placed[Vein].Add(Agreement[Vein][0]);
        }
        Report += FString::Printf(TEXT("seed %lld: chunks [%d,%d] [%d,%d], copper agreement [%d,%d,%d], iron agreement "
                                       "[%d,%d,%d]\n"),
            Seeds[Index], Chunks[0].X, Chunks[0].Y, Chunks[1].X, Chunks[1].Y, Agreement[0][0], Agreement[0][1],
            Agreement[0][2], Agreement[1][0], Agreement[1][1], Agreement[1][2]);
    }
    const auto Summary = [](TArray<double> Values)
    {
        if (Values.IsEmpty()) return FString(TEXT("none"));
        Values.Sort();
        double Sum = 0, Squares = 0;
        for (const double Value : Values)
            Sum += Value, Squares += Value * Value;
        const double Mean = Sum / Values.Num();
        return FString::Printf(TEXT("mean %.0f, sd %.0f, min %.0f, median %.0f, max %.0f over %d seeds"), Mean,
            FMath::Sqrt(FMath::Max(0., Squares / Values.Num() - Mean * Mean)), Values[0], Values[Values.Num() / 2],
            Values.Last(), Values.Num());
    };
    const auto Deciles = [](TArray<float> Values)
    {
        if (Values.IsEmpty()) return FString(TEXT("none"));
        Values.Sort();
        FString Out;
        for (int32 Decile = 1; Decile < 10; ++Decile)
            Out += FString::Printf(TEXT(" %.3f"), Values[Values.Num() * Decile / 10]);
        return FString::Printf(TEXT("%d values:%s"), Values.Num(), *Out);
    };
    Report += FString::Printf(TEXT("Positions with density > 0 per seed (both chunks): copper %s; iron %s\n"),
        *Summary(Dense[0]), *Summary(Dense[1]));
    Report +=
        FString::Printf(TEXT("Vein blocks per seed: copper %s; iron %s\n"), *Summary(Placed[0]), *Summary(Placed[1]));
    int32 MaskPositive = 0;
    for (const float Value : Masks)
        MaskPositive += Value >= 0;
    Report += FString::Printf(TEXT("Uniform sample (no vein block): toggle deciles, %s; |toggle| >= 0.4 in %.3f\n"),
        *Deciles(Toggles), double(Masks.Num()) / FMath::Max(1, Toggles.Num()));
    double GapMean = 0, GapSquares = 0;
    for (const float Value : GapNoise)
        GapMean += Value, GapSquares += double(Value) * Value;
    GapMean /= FMath::Max(1, GapNoise.Num());
    Report +=
        FString::Printf(TEXT("Uniform sample: -0.3 - gap (ore_gap's noise) deciles, %s; sd %.4f\n"), *Deciles(GapNoise),
            FMath::Sqrt(FMath::Max(0., GapSquares / FMath::Max(1, GapNoise.Num()) - GapMean * GapMean)));
    Report += FString::Printf(TEXT("Uniform sample where |toggle| >= 0.4: mask deciles, %s; mask >= 0 in %.3f\n"),
        *Deciles(Masks), double(MaskPositive) / FMath::Max(1, Masks.Num()));
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
    // With veins: their shortcut off (every solid block evaluated directly) must give the default's blocks; veins off
    // is timed, and the blocks the veins change are counted.
    const bool bHasVeins = bHasRules && Terrain.GetSurfaceRules().GetOreVeins().Num() > 0;
    int32 VeinsOffPass = INDEX_NONE;
    if (bHasVeins)
    {
        FVoxelDensityTerrainOverrides VeinShortcut;
        VeinShortcut.bOreVeinShortcut = false;
        Passes.Add({TEXT("vein shortcut off"), VeinShortcut, 0, TEXT("the vein shortcut on and off")});
        FVoxelDensityTerrainOverrides VeinsOff;
        VeinsOff.bOreVeins = false;
        VeinsOffPass = Passes.Add({TEXT("ore veins off"), VeinsOff, INDEX_NONE, nullptr});
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
    int64 RulesDifferences = 0, VeinDifferences = 0;
    for (int32 PassIndex = 0; PassIndex < Passes.Num(); ++PassIndex)
    {
        const FPass& Pass = Passes[PassIndex];
        TArray<double> Total, Density, Aquifer, Biomes, Blocks, Surface, Preliminary, Freeze, Flat, Bounds, Corners,
            Refine, Cells, Veins;
        int64 SolidCells = 0, EmptyCells = 0, RefinedCells = 0, AllCells = 0, CornerCount = 0, SharedCount = 0;
        int64 DeepBlocks = 0, InteriorBlocks = 0, FullBlocks = 0, Steps = 0, InteriorRuns = 0;
        int64 VeinCells = 0, VeinCorners = 0, VeinBlocks = 0, VeinGaps = 0, VeinGiven = 0;
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
            Veins.Add(Timings.VeinMs);
            VeinCells += Timings.VeinCells;
            VeinCorners += Timings.VeinCorners;
            VeinBlocks += Timings.VeinBlocks;
            VeinGaps += Timings.VeinGaps;
            VeinGiven += Timings.VeinGiven;
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
            int64& Count = Pass.Compare != INDEX_NONE ? Differences[PassIndex]
                : PassIndex == VeinsOffPass           ? VeinDifferences
                                                      : RulesDifferences;
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
        if (bHasVeins)
        {
            Report += DensityBenchStage(TEXT("      ore veins"), Veins) + TEXT("\n");
            Report +=
                FString::Printf(TEXT("  ore veins per column: %.1f cells open, %.0f corners, %.0f blocks evaluated, "
                                     "%.1f gaps, %.1f blocks given\n"),
                    double(VeinCells) / Columns, double(VeinCorners) / Columns, double(VeinBlocks) / Columns,
                    double(VeinGaps) / Columns, double(VeinGiven) / Columns);
        }
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
    if (bHasVeins)
        Report += FString::Printf(
            TEXT("Blocks the ore veins change (veins on against off): %lld of %lld\n"), VeinDifferences, AllBlocks);
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

FString VoxelBench::RunDecorate(
    const FVoxelDecorator& Decorator, const FVoxelDecorator& NoEarlyOut, const IVoxelTerrain& Terrain, int32 Radius)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelBench_RunDecorate);
    FString Report = Decorator.Describe() + TEXT("\n");
    Report += FString::Printf(TEXT("Freeze pass: %s\n"),
        Decorator.IsFreezeLast() ? TEXT("at Finalize, after the merge (FreezeLast 1)")
                                 : TEXT("inside the snapshot, S2's order (FreezeLast 0)"));
    uint64 Hashes[2] = {};
    // The exhaustive pass first: it warms caches and allocators, so the gate figures come from the second.
    for (int32 Pass = 1; Pass >= 0; --Pass)
    {
        Terrain.ResetCaches();
        FVoxelDecoratedArea Area;
        FVoxelDecoratedArea::Build(
            Pass ? NoEarlyOut : Decorator, Terrain, FIntPoint::ZeroValue, Radius, false, false, Area);
        TArray<uint64> Columns;
        for (const TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>& Column : Area.Columns)
            Columns.Add(HashColumnBlocks(*Column));
        Hashes[Pass] = CityHash64(reinterpret_cast<const char*>(Columns.GetData()), Columns.Num() * sizeof(uint64));
        const auto Mean = [](TConstArrayView<double> Values)
        {
            double Sum = 0;
            for (const double Value : Values)
                Sum += Value;
            return Values.Num() ? Sum / Values.Num() : 0.;
        };
        const double Whole = Mean(Area.GenerateMs) + Mean(Area.DecorateMs) + Mean(Area.FinalizeMs);
        Report += FString::Printf(TEXT("%s: %d final columns, one thread (early-out %s):\n"),
            Pass ? TEXT("Early-out off") : TEXT("Decoration stages"), Area.Columns.Num(),
            Pass ? TEXT("off") : TEXT("on"));
        Report += DensityBenchStage(TEXT("Generate column"), Area.GenerateMs) + TEXT("\n");
        Report += DensityBenchStage(TEXT("Decorate column"), Area.DecorateMs) + TEXT("\n");
        Report += DensityBenchStage(TEXT("Finalize column"), Area.FinalizeMs) + TEXT("\n");
        Report += DensityBenchStage(TEXT("  of which the freeze"), Area.FinishMs) + TEXT("\n");
        Report += FString::Printf(TEXT("  Whole column (means): %.3f ms\n"), Whole);
        if (Pass) continue;

        // What the stages did, per column.
        double Features = 0, Tries = 0, Skipped = 0, Placed = 0, Writes = 0, LogBytes = 0, Working = 0;
        int32 MaxWrites = 0, Dropped = 0;
        SIZE_T MaxLog = 0, MaxWorking = 0;
        for (const FVoxelDecorateStats& Stats : Area.Decorates)
        {
            Features += Stats.Features;
            Tries += Stats.Tries;
            Skipped += Stats.Skipped;
            Placed += Stats.Placed;
            Writes += Stats.Writes;
            LogBytes += Stats.LogBytes;
            Working += Stats.WorkingBytes;
            MaxWrites = FMath::Max(MaxWrites, Stats.Writes);
            MaxWorking = FMath::Max(MaxWorking, Stats.WorkingBytes);
            MaxLog = FMath::Max(MaxLog, Stats.LogBytes);
            Dropped += Stats.Dropped;
        }
        const double Count = FMath::Max(1, Area.Decorates.Num());
        double Replayed = 0, Applied = 0, Sections = 0, HeightCells = 0;
        for (const FVoxelFinalizeStats& Stats : Area.Finalizes)
        {
            Replayed += Stats.Writes;
            Applied += Stats.Applied;
            Sections += Stats.Sections;
            HeightCells += Stats.HeightCells;
        }
        const double Finals = FMath::Max(1, Area.Finalizes.Num());
        Report += FString::Printf(
            TEXT("  Per Decorate: %.1f placed features, %.1f ore tries (%.1f skipped by the early-out), "
                 "%.0f blocks placed, %.0f writes (max %d), log %.1f KB (max %.1f KB), %d cells "
                 "dropped outside the 3x3\n"),
            Features / Count, Tries / Count, Skipped / Count, Placed / Count, Writes / Count, MaxWrites,
            LogBytes / Count / 1024., double(MaxLog) / 1024., Dropped);
        Report += FString::Printf(TEXT("  Working copy (the written cells): %.0f KB per Decorate (max %.0f KB)\n"),
            Working / Count / 1024., double(MaxWorking) / 1024.);
        Report += FString::Printf(TEXT("  Per Finalize: %.0f writes replayed, %.0f changed a block, %.1f sections "
                                       "repacked, %.1f heightmap cells updated\n"),
            Replayed / Finals, Applied / Finals, Sections / Finals, HeightCells / Finals);
    }
    Report += FString::Printf(TEXT("Final column hashes: early-out on 0x%016llx, off 0x%016llx (%s)"), Hashes[0],
        Hashes[1], Hashes[0] == Hashes[1] ? TEXT("equal: the early-out is exact") : TEXT("DIFFER"));
    return Report;
}

FString VoxelBench::RunFreezeDiff(const FVoxelDecorator& Last, const FVoxelDecorator& First,
    const IVoxelTerrain& Terrain, int32 Radius, TOptional<FIntPoint> Center)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelBench_RunFreezeDiff);
    const FVoxelBlockTable& Table = Last.GetTable();
    const uint16 Snow = Table.FindState(TEXT("snow"));
    FString Report = FString::Printf(TEXT("Freeze pass at Finalize (FreezeLast 1) against inside the snapshot "
                                          "(FreezeLast 0), radius %d:\n"),
        Radius);
    FIntPoint At = Center.Get(FIntPoint::ZeroValue);
    if (!Center)
    {
        // The snowiest column top of a coarse grid near the origin, nearest first on ties.
        constexpr int32 Step = 8, Reach = 16, Side = 2 * Reach + 1;
        TArray<int32> Snowy;
        Snowy.SetNumZeroed(Side * Side);
        ParallelFor(Side * Side,
            [&](int32 Index)
            {
                FVoxelColumn Column;
                TArray<uint16> Dense;
                Terrain.Generate(
                    FIntPoint((Index % Side - Reach) * Step, (Index / Side - Reach) * Step), Column, Dense);
                for (int32 Y = 0; Y < VoxelConstants::ColumnSize; ++Y)
                    for (int32 X = 0; X < VoxelConstants::ColumnSize; ++X)
                        Snowy[Index] +=
                            Column.GetState(X, Y, Column.GetHeight(EVoxelHeightmap::WorldSurface, X, Y)) == Snow;
            });
        int32 Best = 0;
        const auto Distance = [](int32 Index)
        { return FMath::Square(Index % Side - Reach) + FMath::Square(Index / Side - Reach); };
        for (int32 Index = 1; Index < Side * Side; ++Index)
            if (Snowy[Index] > Snowy[Best] || (Snowy[Index] == Snowy[Best] && Distance(Index) < Distance(Best)))
                Best = Index;
        At = FIntPoint((Best % Side - Reach) * Step, (Best / Side - Reach) * Step);
        Report +=
            FString::Printf(TEXT("  Snowiest of %d columns every %d columns within %d of the origin: (%d, %d), %d "
                                 "snow layers on its top\n"),
                Side * Side, Step, Reach * Step, At.X, At.Y, Snowy[Best]);
    }
    FVoxelDecoratedArea LastArea, FirstArea;
    FVoxelDecoratedArea::Build(Last, Terrain, At, Radius, true, false, LastArea);
    FVoxelDecoratedArea::Build(First, Terrain, At, Radius, true, false, FirstArea);
    const auto HashArea = [](const FVoxelDecoratedArea& Area)
    {
        TArray<uint64> Columns;
        for (const TSharedPtr<const FVoxelColumn, ESPMode::ThreadSafe>& Column : Area.Columns)
            Columns.Add(HashColumnBlocks(*Column));
        return CityHash64(reinterpret_cast<const char*>(Columns.GetData()), Columns.Num() * sizeof(uint64));
    };
    TSet<uint16> OreStates;
    for (const TPair<uint16, FString>& Ore : Last.GetOreStates())
        OreStates.Add(Ore.Key);
    const int32 Side = 2 * Radius + 1;
    // A world cell's state in First's columns, or MAX_uint16 outside the area.
    const auto FirstAt = [&](int64 X, int64 Y, int32 Z) -> uint16
    {
        const int64 CX = FMath::FloorToInt64(double(X) / VoxelConstants::ColumnSize) - At.X + Radius,
                    CY = FMath::FloorToInt64(double(Y) / VoxelConstants::ColumnSize) - At.Y + Radius;
        if (CX < 0 || CY < 0 || CX >= Side || CY >= Side) return MAX_uint16;
        return FirstArea.Columns[CY * Side + CX]->GetState(int32(X & 15), int32(Y & 15), Z);
    };
    int64 Differing = 0, Explained = 0, SnowLayers = 0, OnlyLast = 0;
    TArray<FString> Unexplained;
    struct FDiffCell
    {
        FIntVector Position;
        uint16 Last = 0, First = 0;
        bool bSnowBeside = false;
    };
    TArray<FDiffCell> Cells;
    TArray<uint16> LastStates, FirstStates;
    LastStates.SetNumUninitialized(FVoxelSection::Volume);
    FirstStates.SetNumUninitialized(FVoxelSection::Volume);
    for (int32 Index = 0; Index < Side * Side; ++Index)
    {
        const FVoxelColumn& LastColumn = *LastArea.Columns[Index];
        const FVoxelColumn& FirstColumn = *FirstArea.Columns[Index];
        const FIntPoint Position = FirstColumn.GetPosition();
        for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
        {
            LastColumn.GetSection(Section).Expand(LastStates);
            FirstColumn.GetSection(Section).Expand(FirstStates);
            for (int32 Cell = 0; Cell < FVoxelSection::Volume; ++Cell)
            {
                SnowLayers += FirstStates[Cell] == Snow;
                if (LastStates[Cell] == FirstStates[Cell]) continue;
                ++Differing;
                const int32 X = Cell & 15, Y = Cell >> 4 & 15, Z = VoxelConstants::MinZ + Section * 16 + (Cell >> 8);
                const int64 WX = int64(Position.X) * 16 + X, WY = int64(Position.Y) * 16 + Y;
                bool bSnowBeside = false;
                const int32 Faces[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                for (const int32* Face : Faces)
                    bSnowBeside |= FirstAt(WX + Face[0], WY + Face[1], Z + Face[2]) == Snow;
                OnlyLast += OreStates.Contains(LastStates[Cell]) && !OreStates.Contains(FirstStates[Cell]);
                Cells.Add({FIntVector(int32(WX), int32(WY), Z), LastStates[Cell], FirstStates[Cell], bSnowBeside});
            }
        }
    }
    // Beside a snow layer: Decorate saw air there with the freeze moved after it. In the same blob as such a cell (the
    // same ore state within 16 blocks): an exposed candidate draws a random number for the discard, so the blob's
    // later candidates draw differently.
    const auto IsOre = [&](const FDiffCell& Cell)
    { return OreStates.Contains(Cell.Last) || OreStates.Contains(Cell.First); };
    const auto SharesOre = [&](const FDiffCell& A, const FDiffCell& B)
    {
        for (const uint16 State : {A.Last, A.First})
            if (OreStates.Contains(State) && (State == B.Last || State == B.First)) return true;
        return false;
    };
    int64 SameBlob = 0;
    for (const FDiffCell& Cell : Cells)
    {
        if (IsOre(Cell) && Cell.bSnowBeside)
        {
            ++Explained;
            continue;
        }
        bool bNear = false;
        for (const FDiffCell& Other : Cells)
            bNear |= Other.bSnowBeside && IsOre(Cell) && SharesOre(Cell, Other) &&
                FMath::Abs(Other.Position.X - Cell.Position.X) <= 16 &&
                FMath::Abs(Other.Position.Y - Cell.Position.Y) <= 16 &&
                FMath::Abs(Other.Position.Z - Cell.Position.Z) <= 16;
        if (bNear)
            ++SameBlob;
        else if (Unexplained.Num() < 8)
            Unexplained.Add(FString::Printf(TEXT("(%d, %d, %d) states %u / %u"), Cell.Position.X, Cell.Position.Y,
                Cell.Position.Z, uint32(Cell.Last), uint32(Cell.First)));
    }
    Report += FString::Printf(
        TEXT("  Center (%d, %d): %d columns; %lld snow layers (FreezeLast 0)\n"), At.X, At.Y, Side * Side, SnowLayers);
    Report += FString::Printf(
        TEXT("  Hashes: FreezeLast 1 0x%016llx, FreezeLast 0 0x%016llx\n"), HashArea(LastArea), HashArea(FirstArea));
    Report +=
        FString::Printf(TEXT("  Cells that differ: %lld; an ore or its host beside a snow layer: %lld, in the same "
                             "blob as one: %lld (ore only with FreezeLast 1: %lld); unexplained: %lld%s%s"),
            Differing, Explained, SameBlob, OnlyLast, Differing - Explained - SameBlob,
            Unexplained.IsEmpty() ? TEXT("") : TEXT(": "), *FString::Join(Unexplained, TEXT(", ")));
    return Report;
}
