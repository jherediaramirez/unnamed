#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/PlatformTLS.h"
#include "HAL/Thread.h"
#include "Misc/AutomationTest.h"
#include "Noise/VoxelNoise.h"
#include "Random/VoxelRandom.h"
#include "Spline/VoxelSpline.h"

namespace
{
    constexpr uint64 WorldSeed = 0x0123456789ABCDEFull;
    constexpr int32 ThreadCount = 8;
    constexpr int32 JobCount = 24000;

    enum class EJobKind : uint8
    {
        Hash,
        Stream,
        Gradient2,
        Gradient3,
        Fbm2,
        Fbm3,
        Ridged2,
        Damped3,
        Warped2,
        Warped3,
        Spline,
        Count
    };

    struct FJob
    {
        EJobKind Kind;
        double X, Y, Z;
    };

    // Everything a job may sample. Built from (WorldSeed, salts) only, so a context built on any thread is identical.
    struct FContext
    {
        FVoxelGradientNoise Gradient;
        FVoxelNoise Fbm, Ridged, Damped, Warped;
        FVoxelSpline Spline;

        FContext()
        {
            constexpr uint64 Salt = VoxelRandom::Salt("determinism");
            Gradient = FVoxelGradientNoise(VoxelRandom::Hash64(WorldSeed, Salt, 0, 0, 0));
            FVoxelNoiseSettings Settings;
            Settings.Frequency = 1. / 128;
            Settings.Octaves = 5;
            Fbm = FVoxelNoise(WorldSeed, Salt, Settings);
            Settings.Fractal = EVoxelFractal::Ridged;
            Ridged = FVoxelNoise(WorldSeed, Salt + 1, Settings);
            Settings.Fractal = EVoxelFractal::Damped;
            Damped = FVoxelNoise(WorldSeed, Salt + 2, Settings);
            Settings.Fractal = EVoxelFractal::Fbm;
            Settings.WarpAmplitude = 30;
            Warped = FVoxelNoise(WorldSeed, Salt + 3, Settings);

            FVoxelSplineBuilder Builder;
            const int32 Root = Builder.AddNode(0);
            for (int32 Index = 0; Index < 4; ++Index)
            {
                const int32 Child = Builder.AddNode(1);
                Builder.AddPoint(Child, -1, -.5f + Index * .1f, .3f);
                Builder.AddPoint(Child, .2f, .4f, -1.2f);
                Builder.AddPoint(Child, 1, Index * .25f, 0);
                Builder.AddChildPoint(Root, -1 + Index * .6f, Child, .1f);
            }
            verify(Builder.Build(Root, 2, Spline));
        }

        uint64 Evaluate(const FJob& Job) const
        {
            auto Bits = [](float Value)
            {
                uint32 Result;
                FMemory::Memcpy(&Result, &Value, sizeof(Result));
                return uint64(Result);
            };
            const int32 CellX = FMath::FloorToInt32(Job.X), CellY = FMath::FloorToInt32(Job.Y),
                        CellZ = FMath::FloorToInt32(Job.Z);
            switch (Job.Kind)
            {
            case EJobKind::Hash:
                return VoxelRandom::Hash64(WorldSeed, VoxelRandom::Salt("ores"), CellX, CellY, CellZ);
            case EJobKind::Stream:
            {
                // A per-position stream, as a feature would use: three draws from one positional seed.
                FVoxelXoroshiro Stream(VoxelRandom::Hash64(WorldSeed, VoxelRandom::Salt("trees"), CellX, CellY));
                return Stream.Next() ^ uint64(Stream.NextInt(1000)) << 7 ^ Bits(Stream.NextFloat());
            }
            case EJobKind::Gradient2:
                return Bits(Gradient.Sample(Job.X / 13, Job.Y / 13));
            case EJobKind::Gradient3:
                return Bits(Gradient.Sample(Job.X / 13, Job.Y / 13, Job.Z / 13));
            case EJobKind::Fbm2:
                return Bits(Fbm.Sample(Job.X, Job.Y));
            case EJobKind::Fbm3:
                return Bits(Fbm.Sample(Job.X, Job.Y, Job.Z));
            case EJobKind::Ridged2:
                return Bits(Ridged.Sample(Job.X, Job.Y));
            case EJobKind::Damped3:
                return Bits(Damped.Sample(Job.X, Job.Y, Job.Z));
            case EJobKind::Warped2:
                return Bits(Warped.Sample(Job.X, Job.Y));
            case EJobKind::Warped3:
                return Bits(Warped.Sample(Job.X, Job.Y, Job.Z));
            case EJobKind::Spline:
            {
                const float Coordinates[2] = {Fbm.Sample(Job.X, Job.Y), Ridged.Sample(Job.X, Job.Y)};
                return Bits(Spline.Evaluate(Coordinates));
            }
            default:
                return 0;
            }
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelThreadDeterminismTest, "Unnamed.Voxel.Determinism.Threads",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelThreadDeterminismTest::RunTest(const FString& Parameters)
{
    // Jobs spread over a wide area, every kind interleaved.
    TArray<FJob> Jobs;
    FVoxelXoroshiro Placement(uint64(2026));
    for (int32 Index = 0; Index < JobCount; ++Index)
        Jobs.Add({EJobKind(Index % int32(EJobKind::Count)), (Placement.NextDouble() * 2 - 1) * 2e6,
            (Placement.NextDouble() * 2 - 1) * 2e6, Placement.NextDouble() * 384 - 64});

    // The reference: this thread, in job order.
    const FContext Shared;
    TArray<uint64> Reference;
    Reference.SetNumUninitialized(JobCount);
    for (int32 Index = 0; Index < JobCount; ++Index)
        Reference[Index] = Shared.Evaluate(Jobs[Index]);

    // Eight system threads, each in its own shuffled order. Pass 0 shares one context; pass 1 has every thread build
    // its own, so construction is checked too.
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        TArray<TArray<uint64>> Results;
        TArray<uint32> ThreadIds;
        Results.SetNum(ThreadCount);
        ThreadIds.SetNumZeroed(ThreadCount);
        {
            TArray<FThread> Threads;
            for (int32 Thread = 0; Thread < ThreadCount; ++Thread)
                Threads.Emplace(*FString::Printf(TEXT("VoxelDeterminism%d"), Thread),
                    [&, Thread, Pass]
                    {
                        ThreadIds[Thread] = FPlatformTLS::GetCurrentThreadId();
                        TArray<int32> Order;
                        for (int32 Index = 0; Index < JobCount; ++Index)
                            Order.Add(Index);
                        FVoxelXoroshiro Shuffle(uint64(Thread * 7919 + Pass));
                        for (int32 Index = JobCount - 1; Index > 0; --Index)
                            Order.Swap(Index, Shuffle.NextInt(Index + 1));
                        const TUniquePtr<FContext> Own = Pass == 1 ? MakeUnique<FContext>() : nullptr;
                        const FContext& Context = Own ? *Own : Shared;
                        TArray<uint64>& Out = Results[Thread];
                        Out.SetNumZeroed(JobCount);
                        for (const int32 Index : Order)
                            Out[Index] = Context.Evaluate(Jobs[Index]);
                    });
            for (FThread& Thread : Threads)
                Thread.Join();
        }

        TSet<uint32> Distinct(ThreadIds);
        TestEqual(FString::Printf(TEXT("Pass %d ran on 8 distinct threads"), Pass), Distinct.Num(), ThreadCount);
        for (int32 Thread = 0; Thread < ThreadCount; ++Thread)
        {
            int32 Mismatches = 0, First = INDEX_NONE;
            for (int32 Index = 0; Index < JobCount; ++Index)
                if (Results[Thread][Index] != Reference[Index])
                {
                    ++Mismatches;
                    if (First == INDEX_NONE) First = Index;
                }
            TestEqual(FString::Printf(TEXT("Pass %d, thread %d: every job matches the reference%s"), Pass, Thread,
                          First == INDEX_NONE ? TEXT("")
                                              : *FString::Printf(TEXT(" (first mismatch: job %d, kind %d)"), First,
                                                    int32(Jobs[First].Kind))),
                Mismatches, 0);
        }
    }
    AddInfo(FString::Printf(
        TEXT("%d jobs of %d kinds, 8 threads x 2 passes, shuffled order"), JobCount, int32(EJobKind::Count)));
    return true;
}
#endif
