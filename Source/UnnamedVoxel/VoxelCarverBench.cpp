#include "VoxelCarverBench.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "VoxelBench.h"

namespace
{
    struct FCarverBenchSpread
    {
        double Mean = 0, P50 = 0, P95 = 0;
    };

    FCarverBenchSpread CarverBenchSpread(TArray<double> Values)
    {
        FCarverBenchSpread Out;
        if (Values.IsEmpty()) return Out;
        Values.Sort();
        for (const double Value : Values)
            Out.Mean += Value;
        Out.Mean /= Values.Num();
        Out.P50 = Values[Values.Num() / 2];
        Out.P95 = Values[FMath::Min(Values.Num() - 1, Values.Num() * 95 / 100)];
        return Out;
    }

    FString CarverBenchDescribe(const FCarverBenchSpread& Spread)
    {
        return FString::Printf(TEXT("mean %.3f ms, p50 %.3f, p95 %.3f"), Spread.Mean, Spread.P50, Spread.P95);
    }

    uint64 CarverBenchCombine(uint64 Hash, uint64 Value)
    {
        return (Hash ^ Value) * 0x100000001b3ull;
    }
}

FString VoxelBench::RunCarvers(const FVoxelDensityTerrain& Terrain, int32 Radius)
{
    if (!Terrain.IsValid() || !Terrain.GetCarvers().IsInitialized())
        return TEXT("  no carvers: ") + (Terrain.IsValid() ? Terrain.GetCarverError() : Terrain.GetError());
    TArray<FIntPoint> Columns;
    for (int32 Y = -Radius; Y <= Radius; ++Y)
        for (int32 X = -Radius; X <= Radius; ++X)
            Columns.Emplace(X, Y);
    const int32 Count = Columns.Num();
    FVoxelDensityTerrainOverrides Off, On, OnNoCache;
    Off.bShareCorners = On.bShareCorners = OnNoCache.bShareCorners = false;
    Off.bCarvers = false;
    On.bCarvers = OnNoCache.bCarvers = true;
    OnNoCache.bCarveCache = false;
    Terrain.ResetCaches();

    TArray<double> OffMs, OnMs, Added, CarveMs, StartsMs, PathsMs, MarkMs, NoCacheMs;
    int64 Starts = 0, Built = 0, Found = 0, Steps = 0, Marked = 0, Candidates = 0, Air = 0, Water = 0, Lava = 0,
          Barriers = 0;
    TArray<TArray<uint16>> OnBlocks;
    OnBlocks.SetNum(Count);
    uint64 OffHash = 0xcbf29ce484222325ull, OnHash = 0xcbf29ce484222325ull;
    FVoxelColumn Column;
    TArray<uint16> Dense;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        double Times[2] = {0, 0};
        // Odd columns carve first, even ones second, so drift and warm caches fall on both sides alike.
        for (int32 Pass = 0; Pass < 2; ++Pass)
        {
            const bool bOn = (Pass == 0) == (Index % 2 == 1);
            FVoxelDensityTerrainTimings Timings;
            const double Start = FPlatformTime::Seconds();
            Terrain.Generate(Columns[Index], Column, Dense, &Timings, bOn ? On : Off);
            Times[bOn] = (FPlatformTime::Seconds() - Start) * 1000;
            if (!bOn)
            {
                OffHash = CarverBenchCombine(OffHash, VoxelBench::HashColumnBlocks(Column));
                continue;
            }
            OnHash = CarverBenchCombine(OnHash, VoxelBench::HashColumnBlocks(Column));
            OnBlocks[Index] = Dense;
            CarveMs.Add(Timings.CarversMs);
            StartsMs.Add(Timings.Carve.StartsMs);
            PathsMs.Add(Timings.Carve.PathsMs);
            MarkMs.Add(Timings.Carve.MarkMs);
            Starts += Timings.Carve.Starts;
            Built += Timings.Carve.PathsBuilt;
            Found += Timings.Carve.PathsFound;
            Steps += Timings.Carve.Steps;
            Marked += Timings.Carve.Marked;
            Candidates += Timings.CarveCandidates;
            Air += Timings.CarveAir;
            Water += Timings.CarveWater;
            Lava += Timings.CarveLava;
            Barriers += Timings.CarveBarriers;
        }
        OffMs.Add(Times[0]);
        OnMs.Add(Times[1]);
        Added.Add(Times[1] - Times[0]);
    }
    const int64 CacheBytes = Terrain.GetCarveCacheBytes();

    // The path cache off: every path each column needs is built in it; the blocks must not change.
    int64 Differing = 0;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        FVoxelDensityTerrainTimings Timings;
        Terrain.Generate(Columns[Index], Column, Dense, &Timings, OnNoCache);
        NoCacheMs.Add(Timings.CarversMs);
        const TArray<uint16>& Cached = OnBlocks[Index];
        for (int32 Block = 0; Block < Dense.Num(); ++Block)
            Differing += Dense[Block] != Cached[Block];
    }

    const double PerColumn = 1. / FMath::Max(1, Count);
    const int64 Carved = Air + Water + Lava;
    const double Share = 100. / FMath::Max<int64>(1, Carved);
    const FCarverBenchSpread Carve = CarverBenchSpread(CarveMs), StartsSpread = CarverBenchSpread(StartsMs),
                             PathsSpread = CarverBenchSpread(PathsMs), MarkSpread = CarverBenchSpread(MarkMs);
    FString Out = FString::Printf(
        TEXT("  %d columns (radius %d), one thread, corner sharing off on both sides\n"), Count, Radius);
    Out += TEXT("  Generate, carvers off: ") + CarverBenchDescribe(CarverBenchSpread(OffMs)) + TEXT("\n");
    Out += TEXT("  Generate, carvers on: ") + CarverBenchDescribe(CarverBenchSpread(OnMs)) + TEXT("\n");
    Out += TEXT("  Added by the carvers (on - off, per column): ") + CarverBenchDescribe(CarverBenchSpread(Added)) +
        TEXT("\n");
    Out += FString::Printf(TEXT("  Carve pass: %s; starts %.3f, paths %.3f, mark %.3f, aquifer and apply %.3f\n"),
        *CarverBenchDescribe(Carve), StartsSpread.Mean, PathsSpread.Mean, MarkSpread.Mean,
        Carve.Mean - StartsSpread.Mean - PathsSpread.Mean - MarkSpread.Mean);
    Out += FString::Printf(TEXT("  Per column: starts %.1f, paths built %.2f, found %.1f, steps %.0f, marked %.0f, "
                                "carvable %.0f, carved %.0f (air %.1f%%, water %.1f%%, lava %.1f%%), barriers %.1f\n"),
        Starts * PerColumn, Built * PerColumn, Found * PerColumn, Steps * PerColumn, Marked * PerColumn,
        Candidates * PerColumn, Carved * PerColumn, Air * Share, Water * Share, Lava * Share, Barriers * PerColumn);
    Out += FString::Printf(TEXT("  Path cache: %.2f MB after the pass\n"), CacheBytes / (1024. * 1024.));
    Out += FString::Printf(TEXT("  Path cache off: %lld of %lld blocks differ; carve pass %s\n"), Differing,
        int64(Count) * FVoxelColumn::DenseVolume, *CarverBenchDescribe(CarverBenchSpread(NoCacheMs)));
    Out += FString::Printf(TEXT("  Block hashes: carvers off 0x%016llx, on 0x%016llx"),
        static_cast<unsigned long long>(OffHash), static_cast<unsigned long long>(OnHash));

    // Where to look into a canyon (captures): the three canyon starts nearest the origin, each with its middle step
    // and the step ten further on, in world blocks (our axes, Z up).
    const FVoxelCarvers& Carvers = Terrain.GetCarvers();
    for (int32 Carver = 0; Carver < Carvers.GetConfigs().Num(); ++Carver)
    {
        if (Carvers.GetConfigs()[Carver].Kind != EVoxelCarverKind::Canyon) continue;
        int32 Canyons = 0;
        for (int32 Ring = 0; Ring <= 64 && Canyons < 3; ++Ring)
            for (int32 Y = -Ring; Y <= Ring && Canyons < 3; ++Y)
                for (int32 X = -Ring; X <= Ring && Canyons < 3; ++X)
                {
                    const FIntPoint Start(X, Y);
                    if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != Ring || !Carvers.IsStart(Carver, Start)) continue;
                    FVoxelCarvePath Path;
                    Carvers.BuildPath(Carver, Start, Path);
                    if (Path.Steps.Num() < 12) continue;
                    const int32 Middle = Path.Steps.Num() / 2, Next = FMath::Min(Path.Steps.Num() - 1, Middle + 10);
                    const auto World = [&](const FVoxelCarveStep& Step)
                    {
                        return FString::Printf(
                            TEXT("(%.0f, %.0f, %.0f)"), Start.X * 16. + Step.X, Start.Y * 16. + Step.Y, double(Step.Z));
                    };
                    Out += FString::Printf(TEXT("\n  Canyon at start column (%d, %d): middle %s, ten steps on %s, half "
                                                "height %.1f"),
                        X, Y, *World(Path.Steps[Middle]), *World(Path.Steps[Next]), Path.Steps[Middle].Vertical);
                    ++Canyons;
                }
    }
    return Out;
}
