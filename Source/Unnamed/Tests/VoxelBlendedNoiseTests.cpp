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

#endif
