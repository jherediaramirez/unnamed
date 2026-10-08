#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/PlatformTLS.h"
#include "HAL/Thread.h"
#include "Misc/AutomationTest.h"
#include "Noise/VoxelNoise.h"
#include "Random/VoxelRandom.h"
#include "Spline/VoxelSpline.h"

namespace
{
    constexpr uint64 DeterminismSeed = 0x0123456789ABCDEFull;
    constexpr int32 DeterminismThreads = 8;
    constexpr int32 DeterminismJobs = 24000;

    enum class EDeterminismJob : uint8
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

    struct FDeterminismJob
    {
        EDeterminismJob Kind;
        double X, Y, Z;
    };

    // Everything a job may sample. Built from (DeterminismSeed, salts) only, so a context built on any thread is
    // identical.
    struct FDeterminismContext
    {
        FVoxelGradientNoise Gradient;
        FVoxelNoise Fbm, Ridged, Damped, Warped;
        FVoxelSpline Spline;

        FDeterminismContext()
        {
            constexpr uint64 Salt = VoxelRandom::Salt("determinism");
            Gradient = FVoxelGradientNoise(VoxelRandom::Hash64(DeterminismSeed, Salt, 0, 0, 0));
            FVoxelNoiseSettings Settings;
            Settings.Frequency = 1. / 128;
            Settings.Octaves = 5;
            Fbm = FVoxelNoise(DeterminismSeed, Salt, Settings);
            Settings.Fractal = EVoxelFractal::Ridged;
            Ridged = FVoxelNoise(DeterminismSeed, Salt + 1, Settings);
            Settings.Fractal = EVoxelFractal::Damped;
            Damped = FVoxelNoise(DeterminismSeed, Salt + 2, Settings);
            Settings.Fractal = EVoxelFractal::Fbm;
            Settings.WarpAmplitude = 30;
            Warped = FVoxelNoise(DeterminismSeed, Salt + 3, Settings);

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

        uint64 Evaluate(const FDeterminismJob& Job) const
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
            case EDeterminismJob::Hash:
                return VoxelRandom::Hash64(DeterminismSeed, VoxelRandom::Salt("ores"), CellX, CellY, CellZ);
            case EDeterminismJob::Stream:
            {
                // A per-position stream, as a feature would use: three draws from one positional seed.
                FVoxelXoroshiro Stream(VoxelRandom::Hash64(DeterminismSeed, VoxelRandom::Salt("trees"), CellX, CellY));
                return Stream.Next() ^ uint64(Stream.NextInt(1000)) << 7 ^ Bits(Stream.NextFloat());
            }
            case EDeterminismJob::Gradient2:
                return Bits(Gradient.Sample(Job.X / 13, Job.Y / 13));
            case EDeterminismJob::Gradient3:
                return Bits(Gradient.Sample(Job.X / 13, Job.Y / 13, Job.Z / 13));
            case EDeterminismJob::Fbm2:
                return Bits(Fbm.Sample(Job.X, Job.Y));
            case EDeterminismJob::Fbm3:
                return Bits(Fbm.Sample(Job.X, Job.Y, Job.Z));
            case EDeterminismJob::Ridged2:
                return Bits(Ridged.Sample(Job.X, Job.Y));
            case EDeterminismJob::Damped3:
                return Bits(Damped.Sample(Job.X, Job.Y, Job.Z));
            case EDeterminismJob::Warped2:
                return Bits(Warped.Sample(Job.X, Job.Y));
            case EDeterminismJob::Warped3:
                return Bits(Warped.Sample(Job.X, Job.Y, Job.Z));
            case EDeterminismJob::Spline:
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
    TArray<FDeterminismJob> Jobs;
    FVoxelXoroshiro Placement(uint64(2026));
    for (int32 Index = 0; Index < DeterminismJobs; ++Index)
        Jobs.Add({EDeterminismJob(Index % int32(EDeterminismJob::Count)), (Placement.NextDouble() * 2 - 1) * 2e6,
            (Placement.NextDouble() * 2 - 1) * 2e6, Placement.NextDouble() * 384 - 64});

    // The reference: this thread, in job order.
    const FDeterminismContext Shared;
    TArray<uint64> Reference;
    Reference.SetNumUninitialized(DeterminismJobs);
    for (int32 Index = 0; Index < DeterminismJobs; ++Index)
        Reference[Index] = Shared.Evaluate(Jobs[Index]);

    // Eight system threads, each in its own shuffled order. Pass 0 shares one context; pass 1 has every thread build
    // its own, so construction is checked too.
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        TArray<TArray<uint64>> Results;
        TArray<uint32> ThreadIds;
        Results.SetNum(DeterminismThreads);
        ThreadIds.SetNumZeroed(DeterminismThreads);
        {
            TArray<FThread> Threads;
            for (int32 Thread = 0; Thread < DeterminismThreads; ++Thread)
                Threads.Emplace(*FString::Printf(TEXT("VoxelDeterminism%d"), Thread),
                    [&, Thread, Pass]
                    {
                        ThreadIds[Thread] = FPlatformTLS::GetCurrentThreadId();
                        TArray<int32> Order;
                        for (int32 Index = 0; Index < DeterminismJobs; ++Index)
                            Order.Add(Index);
                        FVoxelXoroshiro Shuffle(uint64(Thread * 7919 + Pass));
                        for (int32 Index = DeterminismJobs - 1; Index > 0; --Index)
                            Order.Swap(Index, Shuffle.NextInt(Index + 1));
                        const TUniquePtr<FDeterminismContext> Own =
                            Pass == 1 ? MakeUnique<FDeterminismContext>() : nullptr;
                        const FDeterminismContext& Context = Own ? *Own : Shared;
                        TArray<uint64>& Out = Results[Thread];
                        Out.SetNumZeroed(DeterminismJobs);
                        for (const int32 Index : Order)
                            Out[Index] = Context.Evaluate(Jobs[Index]);
                    });
            for (FThread& Thread : Threads)
                Thread.Join();
        }

        TSet<uint32> Distinct(ThreadIds);
        TestEqual(FString::Printf(TEXT("Pass %d ran on 8 distinct threads"), Pass), Distinct.Num(), DeterminismThreads);
        for (int32 Thread = 0; Thread < DeterminismThreads; ++Thread)
        {
            int32 Mismatches = 0, First = INDEX_NONE;
            for (int32 Index = 0; Index < DeterminismJobs; ++Index)
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
    AddInfo(FString::Printf(TEXT("%d jobs of %d kinds, 8 threads x 2 passes, shuffled order"), DeterminismJobs,
        int32(EDeterminismJob::Count)));
    return true;
}
#endif
