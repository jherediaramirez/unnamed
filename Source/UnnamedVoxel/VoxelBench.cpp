#include "VoxelBench.h"
#include "Async/Fundamental/Scheduler.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"
#include "Noise/VoxelNoise.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Random/VoxelRandom.h"
#include "Spline/VoxelSpline.h"

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

    template <typename FunctionType> FVoxelBenchResult Time(const TCHAR* Name, int32 Samples, FunctionType&& Function)
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
    Results.Add(Time(TEXT("Hash64 (x, y, z)"), Samples,
        [Seed](int32 I) { return double(VoxelRandom::Hash64(Seed, BenchSalt, I & 1023, I >> 10, I & 7) >> 40); }));
    FVoxelXoroshiro Stream(Seed);
    Results.Add(Time(TEXT("Xoroshiro128++ next"), Samples, [&Stream](int32) { return double(Stream.Next() >> 40); }));
    Results.Add(Time(TEXT("Gradient noise 2D, 1 octave"), Samples,
        [&Gradient](int32 I) { return Gradient.Sample(PositionX(I) / 16, PositionY(I) / 16); }));
    Results.Add(Time(TEXT("Gradient noise 3D, 1 octave"), Samples,
        [&Gradient](int32 I) { return Gradient.Sample(PositionX(I) / 16, PositionY(I) / 16, (I & 63) * .37); }));
    Results.Add(
        Time(TEXT("fBm 2D, 6 octaves"), Samples, [&Fbm6](int32 I) { return Fbm6.Sample(PositionX(I), PositionY(I)); }));
    Results.Add(Time(TEXT("fBm 3D, 4 octaves"), Samples,
        [&Fbm4](int32 I) { return Fbm4.Sample(PositionX(I), PositionY(I), (I & 63) * 2.1); }));
    Results.Add(Time(TEXT("Ridged 2D, 6 octaves"), Samples,
        [&Ridged6](int32 I) { return Ridged6.Sample(PositionX(I), PositionY(I)); }));
    Results.Add(Time(TEXT("Damped 2D, 6 octaves"), Samples,
        [&Damped6](int32 I) { return Damped6.Sample(PositionX(I), PositionY(I)); }));
    Results.Add(Time(TEXT("Warped fBm 2D, 6 octaves"), Samples,
        [&Warped6](int32 I) { return Warped6.Sample(PositionX(I), PositionY(I)); }));
    Results.Add(Time(TEXT("Spline, 3 nested levels"), Samples,
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
