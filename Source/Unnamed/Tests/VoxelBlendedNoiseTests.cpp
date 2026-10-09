#include "Misc/AutomationTest.h"
#include "Noise/VoxelBlendedNoise.h"
#include "Random/VoxelRandom.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // The 26.3 jar's old_blended_noise with the overworld's numbers (xz scale 0.25, y scale 0.125, factors 80 and 160),
    // sampled through DensityFunction.CODEC by C:\dev\sandbox\phase4s3\OpProbe3.java: 4 seeds x 16,384 positions with x
    // and z in +-100,000 and y in [-64, 320). Means over the seeds.
    constexpr double BlendedVanillaStd = 0.1982;
    constexpr double BlendedVanillaP1 = -0.461;
    constexpr double BlendedVanillaP99 = 0.4635;
    // Where the variogram reaches half its sill (2 variance): about 34 blocks along x and 60 along y.
    constexpr double BlendedVanillaHalfLagX = 34;
    constexpr double BlendedVanillaHalfLagY = 60;
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelBlendedNoiseSpreadTest, "Unnamed.Voxel.Density.BlendedSpread",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelBlendedNoiseSpreadTest::RunTest(const FString& Parameters)
{
    // Tolerances fixed before calibrating OutputScale: std within 10%, p1 and p99 within 15%, and the half-sill lags
    // within a factor of 2 of vanilla's.
    constexpr int32 Seeds = 4, Positions = 16384, Lagged = 2048;
    TArray<double> Values;
    double VarioX[3] = {}, VarioY[3] = {};
    const double LagsX[3] = {BlendedVanillaHalfLagX / 2, BlendedVanillaHalfLagX, BlendedVanillaHalfLagX * 2};
    const double LagsY[3] = {BlendedVanillaHalfLagY / 2, BlendedVanillaHalfLagY, BlendedVanillaHalfLagY * 2};
    for (int32 Seed = 0; Seed < Seeds; ++Seed)
    {
        const FVoxelBlendedNoise Noise(1000 + Seed, .25, .125, 80, 160);
        FVoxelXoroshiro Stream(VoxelRandom::Hash64(77 + Seed, VoxelRandom::Salt("blended-spread-test"), 0, 0, 0));
        for (int32 Index = 0; Index < Positions; ++Index)
        {
            const double X = FMath::FloorToDouble(Stream.NextDouble() * 200000 - 100000);
            const double Y = FMath::FloorToDouble(Stream.NextDouble() * 384 - 64);
            const double Z = FMath::FloorToDouble(Stream.NextDouble() * 200000 - 100000);
            const float Value = Noise.Sample(X, Y, Z);
            Values.Add(Value);
            if (FMath::Abs(Value) > Noise.GetBound()) AddError(FString::Printf(TEXT("%f beyond the bound"), Value));
            if (Index < Lagged)
                for (int32 Lag = 0; Lag < 3; ++Lag)
                {
                    VarioX[Lag] += FMath::Square(Noise.Sample(X + LagsX[Lag], Y, Z) - Value);
                    VarioY[Lag] += FMath::Square(Noise.Sample(X, Y + LagsY[Lag], Z) - Value);
                }
        }
    }
    double Mean = 0, Variance = 0;
    for (const double Value : Values)
        Mean += Value / Values.Num();
    for (const double Value : Values)
        Variance += FMath::Square(Value - Mean) / Values.Num();
    const double Std = FMath::Sqrt(Variance);
    Values.Sort();
    const double P1 = Values[int32(.01 * (Values.Num() - 1))], P99 = Values[int32(.99 * (Values.Num() - 1))];
    // Normalized semivariance: 0.5 is the half-sill.
    double HalfX[3], HalfY[3];
    for (int32 Lag = 0; Lag < 3; ++Lag)
    {
        HalfX[Lag] = VarioX[Lag] / (Seeds * Lagged) / (2 * Variance);
        HalfY[Lag] = VarioY[Lag] / (Seeds * Lagged) / (2 * Variance);
    }
    AddInfo(FString::Printf(TEXT("blended: std %.4f (vanilla %.4f, ratio %.4f), p1 %.3f, p99 %.3f, mean %.4f; "
                                 "variogram/2var x %.3f %.3f %.3f, y %.3f %.3f %.3f"),
        Std, BlendedVanillaStd, Std / BlendedVanillaStd, P1, P99, Mean, HalfX[0], HalfX[1], HalfX[2], HalfY[0],
        HalfY[1], HalfY[2]));

    TestTrue(TEXT("std within 10% of vanilla"), FMath::Abs(Std / BlendedVanillaStd - 1) <= .10);
    TestTrue(TEXT("p1 within 15% of vanilla"), FMath::Abs(P1 / BlendedVanillaP1 - 1) <= .15);
    TestTrue(TEXT("p99 within 15% of vanilla"), FMath::Abs(P99 / BlendedVanillaP99 - 1) <= .15);
    TestTrue(TEXT("x correlation within a factor 2"), HalfX[0] < .5 && HalfX[2] > .5);
    TestTrue(TEXT("y correlation within a factor 2"), HalfY[0] < .5 && HalfY[2] > .5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelBlendedNoiseBatchTest, "Unnamed.Voxel.Density.BlendedBatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// The batch Sample equals Sample point by point, bit for bit: the overworld's numbers and another set, 4,096 positions
// each (block corners and fractional positions, x and z in +-100,000, y in [-64, 320)), in batches of uneven sizes. The
// selector's gain clamps about a third of the points to one limit or the other, so both skips are covered.
bool FVoxelBlendedNoiseBatchTest::RunTest(const FString& Parameters)
{
    constexpr int32 Positions = 4096;
    const int32 Batches[] = {1, 7, 64, 1000, Positions};
    const FVoxelBlendedNoise Noises[2] = {
        FVoxelBlendedNoise(263, .25, .125, 80, 160), FVoxelBlendedNoise(264, 1, 1.5, 40, 20)};
    FVoxelXoroshiro Stream(VoxelRandom::Hash64(263, VoxelRandom::Salt("blended-batch-test"), 0, 0, 0));
    TArray<double> X, Y, Z;
    for (int32 Index = 0; Index < Positions; ++Index)
    {
        const bool bCorner = Index % 2 == 0;
        const double PX = Stream.NextDouble() * 200000 - 100000, PY = Stream.NextDouble() * 384 - 64,
                     PZ = Stream.NextDouble() * 200000 - 100000;
        X.Add(bCorner ? FMath::FloorToDouble(PX) : PX);
        Y.Add(bCorner ? FMath::FloorToDouble(PY) : PY);
        Z.Add(bCorner ? FMath::FloorToDouble(PZ) : PZ);
    }
    FVoxelBlendedNoiseScratch Scratch;
    TArray<float> Batched;
    Batched.SetNumUninitialized(Positions);
    for (int32 Set = 0; Set < 2; ++Set)
    {
        // The scratch is reused across batches and sets, as an evaluator's is.
        int32 Start = 0, Next = 0;
        while (Start < Positions)
        {
            const int32 Count = FMath::Min(Batches[Next++ % UE_ARRAY_COUNT(Batches)], Positions - Start);
            Noises[Set].Sample(&X[Start], &Y[Start], &Z[Start], Count, &Batched[Start], Scratch);
            Start += Count;
        }
        // The last batch's alphas stay in the scratch: both clamps must have happened, so both skips ran.
        int32 Zeros = 0, Ones = 0;
        for (const float Alpha : Scratch.Selector)
        {
            Zeros += Alpha == 0;
            Ones += Alpha == 1;
        }
        TestTrue(FString::Printf(TEXT("set %d: selectors clamped to both limits (%d at 0, %d at 1)"), Set, Zeros, Ones),
            Zeros > 0 && Ones > 0);
        int32 Differing = 0;
        for (int32 Index = 0; Index < Positions; ++Index)
        {
            const float Single = Noises[Set].Sample(X[Index], Y[Index], Z[Index]);
            Differing += FMemory::Memcmp(&Single, &Batched[Index], sizeof(float)) != 0;
        }
        TestEqual(FString::Printf(TEXT("set %d: values that differ from Sample point by point"), Set), Differing, 0);
    }
    return true;
}

#endif
