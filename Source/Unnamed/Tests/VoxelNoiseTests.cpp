#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Noise/VoxelNoise.h"
#include "Random/VoxelRandom.h"
#include "VoxelBench.h"

namespace
{
    constexpr uint64 TestSeed = 0x5EED5EED5EED5EEDull;
    constexpr uint64 TestSalt = VoxelRandom::Salt("noise-test");

    uint32 FloatBits(float Value)
    {
        uint32 Bits;
        FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }

    // Order-sensitive checksum of exact float bit patterns.
    uint64 Checksum(TConstArrayView<float> Values)
    {
        uint64 Hash = 0;
        for (const float Value : Values)
            Hash = VoxelRandom::Mix64(Hash ^ FloatBits(Value));
        return Hash;
    }

    FVoxelNoise MakeNoise(EVoxelFractal Fractal, int32 Octaves, double WarpAmplitude = 0, uint64 Salt = TestSalt)
    {
        FVoxelNoiseSettings Settings;
        Settings.Fractal = Fractal;
        Settings.Frequency = 1. / 64;
        Settings.Octaves = Octaves;
        Settings.WarpAmplitude = WarpAmplitude;
        return FVoxelNoise(TestSeed, Salt, Settings);
    }

    struct FGolden
    {
        const TCHAR* Name;
        uint64 Expected;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelNoiseGoldenTest, "Unnamed.Voxel.Noise.Golden",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelNoiseGoldenTest::RunTest(const FString& Parameters)
{
    // Checksums of fixed grids, recorded from this implementation. A change here changes every world: only update one
    // on purpose, together with a note in Docs/VoxelWorld.md.
    const FGolden Golden[] = {
        {TEXT("gradient 2D"), 0xE475AA5F53E9F163ull},
        {TEXT("gradient 3D"), 0xECE883ECD6090FD1ull},
        {TEXT("fBm 2D, 6 octaves"), 0xA93CB3FE1AC72DE3ull},
        {TEXT("fBm 3D, 4 octaves"), 0x649900DFE955359Dull},
        {TEXT("ridged 2D, 6 octaves"), 0x10FEE47C1DE29CF0ull},
        {TEXT("ridged 3D, 4 octaves"), 0xC8563B17209FDA48ull},
        {TEXT("damped 2D, 6 octaves"), 0x00E0EDB22CB2F83Bull},
        {TEXT("damped 3D, 4 octaves"), 0x4D950DCFDCC8210Aull},
        {TEXT("warped fBm 2D"), 0x99B0145B979DF24Aull},
        {TEXT("warped fBm 3D"), 0x66790E9C689E05D5ull},
        {TEXT("fBm 2D, 30 million blocks out"), 0x39B3A5467F3977B3ull},
    };

    const FVoxelGradientNoise Gradient(VoxelRandom::Hash64(TestSeed, TestSalt, 0, 0, 0));
    TArray<float> Values;
    auto Grid2 = [&Values](auto&& Function, double X0, double Y0)
    {
        Values.Reset();
        for (int32 IY = 0; IY < 64; ++IY)
            for (int32 IX = 0; IX < 64; ++IX)
                Values.Add(Function(X0 + IX * 2.37, Y0 + IY * 2.37));
        return Checksum(Values);
    };
    auto Grid3 = [&Values](auto&& Function)
    {
        Values.Reset();
        for (int32 IZ = 0; IZ < 16; ++IZ)
            for (int32 IY = 0; IY < 16; ++IY)
                for (int32 IX = 0; IX < 16; ++IX)
                    Values.Add(Function(-500.3 + IX * 3.1, 812.9 + IY * 3.1, -64 + IZ * 5.3));
        return Checksum(Values);
    };
    const FVoxelNoise Fbm6 = MakeNoise(EVoxelFractal::Fbm, 6), Fbm4 = MakeNoise(EVoxelFractal::Fbm, 4);
    const FVoxelNoise Ridged6 = MakeNoise(EVoxelFractal::Ridged, 6), Ridged4 = MakeNoise(EVoxelFractal::Ridged, 4);
    const FVoxelNoise Damped6 = MakeNoise(EVoxelFractal::Damped, 6), Damped4 = MakeNoise(EVoxelFractal::Damped, 4);
    const FVoxelNoise Warped = MakeNoise(EVoxelFractal::Fbm, 4, 24);
    const uint64 Actual[] = {
        Grid2([&](double X, double Y) { return Gradient.Sample(X / 16, Y / 16); }, -1000.3, 777.7),
        Grid3([&](double X, double Y, double Z) { return Gradient.Sample(X / 16, Y / 16, Z / 16); }),
        Grid2([&](double X, double Y) { return Fbm6.Sample(X, Y); }, -1000.3, 777.7),
        Grid3([&](double X, double Y, double Z) { return Fbm4.Sample(X, Y, Z); }),
        Grid2([&](double X, double Y) { return Ridged6.Sample(X, Y); }, -1000.3, 777.7),
        Grid3([&](double X, double Y, double Z) { return Ridged4.Sample(X, Y, Z); }),
        Grid2([&](double X, double Y) { return Damped6.Sample(X, Y); }, -1000.3, 777.7),
        Grid3([&](double X, double Y, double Z) { return Damped4.Sample(X, Y, Z); }),
        Grid2([&](double X, double Y) { return Warped.Sample(X, Y); }, -1000.3, 777.7),
        Grid3([&](double X, double Y, double Z) { return Warped.Sample(X, Y, Z); }),
        Grid2([&](double X, double Y) { return Fbm6.Sample(X, Y); }, 30000000.5, -29999999.5),
    };
    static_assert(UE_ARRAY_COUNT(Golden) == UE_ARRAY_COUNT(Actual));
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Golden); ++Index)
        if (Actual[Index] != Golden[Index].Expected)
            AddError(FString::Printf(TEXT("%s checksum is 0x%016llXull, expected 0x%016llXull"), Golden[Index].Name,
                Actual[Index], Golden[Index].Expected));

    // Two noises built from the same seed, salt and settings agree bit for bit.
    TestEqual(TEXT("A rebuilt noise matches"), Grid2([&](double X, double Y) { return Fbm6.Sample(X, Y); }, 5, 5),
        Grid2([Copy = MakeNoise(EVoxelFractal::Fbm, 6)](double X, double Y) { return Copy.Sample(X, Y); }, 5, 5));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelNoisePropertiesTest, "Unnamed.Voxel.Noise.Properties",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelNoisePropertiesTest::RunTest(const FString& Parameters)
{
    const FVoxelGradientNoise Gradient(VoxelRandom::Hash64(TestSeed, TestSalt, 1, 0, 0));
    FVoxelXoroshiro Stream(uint64(99));
    auto Uniform = [&Stream](double Range) { return (Stream.NextDouble() * 2 - 1) * Range; };

    // Zero on every lattice point, inside [-1, 1] everywhere, centred and spread out.
    bool bZeroOnLattice = true;
    for (int32 Index = 0; Index < 1000; ++Index)
    {
        const double X = FMath::RoundToDouble(Uniform(5000)), Y = FMath::RoundToDouble(Uniform(5000));
        bZeroOnLattice &= Gradient.Sample(X, Y) == 0 && Gradient.Sample(X, Y, -X) == 0;
    }
    TestTrue(TEXT("Zero on lattice points"), bZeroOnLattice);
    for (const int32 Dimensions : {2, 3})
    {
        double Sum = 0, SumSquares = 0, Peak = 0;
        constexpr int32 Count = 200000;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const double X = Uniform(1000), Y = Uniform(1000), Z = Uniform(1000);
            const float Value = Dimensions == 2 ? Gradient.Sample(X, Y) : Gradient.Sample(X, Y, Z);
            Sum += Value;
            SumSquares += Value * Value;
            Peak = FMath::Max(Peak, double(FMath::Abs(Value)));
        }
        const double Mean = Sum / Count, Deviation = FMath::Sqrt(SumSquares / Count - Mean * Mean);
        AddInfo(FString::Printf(TEXT("Gradient noise %dD over %d points: peak %.4f, mean %.4f, deviation %.4f"),
            Dimensions, Count, Peak, Mean, Deviation));
        TestTrue(FString::Printf(TEXT("%dD stays in [-1, 1] (peak %.4f)"), Dimensions, Peak), Peak <= 1.0001);
        TestTrue(FString::Printf(TEXT("%dD mean near 0 (%.4f)"), Dimensions, Mean), FMath::Abs(Mean) < .01);
        TestTrue(FString::Printf(TEXT("%dD spread (deviation %.3f)"), Dimensions, Deviation),
            Deviation > .15 && Deviation < .6);
    }

    // Analytic gradients match central differences, and are continuous across lattice planes.
    double WorstGradient = 0, WorstSeam = 0;
    for (int32 Index = 0; Index < 2000; ++Index)
    {
        const double X = Uniform(300), Y = Uniform(300), Z = Uniform(300), H = 1e-3;
        const FVoxelNoiseSample2 Sample2 = Gradient.SampleWithGradient(X, Y);
        const FVoxelNoiseSample3 Sample3 = Gradient.SampleWithGradient(X, Y, Z);
        const FVector2f Numeric2((Gradient.Sample(X + H, Y) - Gradient.Sample(X - H, Y)) / (2 * H),
            (Gradient.Sample(X, Y + H) - Gradient.Sample(X, Y - H)) / (2 * H));
        const FVector3f Numeric3((Gradient.Sample(X + H, Y, Z) - Gradient.Sample(X - H, Y, Z)) / (2 * H),
            (Gradient.Sample(X, Y + H, Z) - Gradient.Sample(X, Y - H, Z)) / (2 * H),
            (Gradient.Sample(X, Y, Z + H) - Gradient.Sample(X, Y, Z - H)) / (2 * H));
        WorstGradient = FMath::Max(WorstGradient,
            double(FMath::Max((Sample2.Gradient - Numeric2).GetAbsMax(), (Sample3.Gradient - Numeric3).GetAbsMax())));
        TestEqual(TEXT("SampleWithGradient returns the same value"), Sample3.Value, Gradient.Sample(X, Y, Z));

        const double Plane = FMath::RoundToDouble(X), Epsilon = 1e-6;
        const FVoxelNoiseSample3 Below = Gradient.SampleWithGradient(Plane - Epsilon, Y, Z);
        const FVoxelNoiseSample3 Above = Gradient.SampleWithGradient(Plane + Epsilon, Y, Z);
        WorstSeam = FMath::Max(WorstSeam, double((Above.Gradient - Below.Gradient).GetAbsMax()));
    }
    AddInfo(FString::Printf(TEXT("Gradient error against finite differences %.5f; largest jump across a lattice "
                                 "plane %.6f"),
        WorstGradient, WorstSeam));
    TestTrue(
        FString::Printf(TEXT("Gradients match finite differences (worst %.5f)"), WorstGradient), WorstGradient < .01);
    TestTrue(
        FString::Printf(TEXT("No seams at lattice planes (worst gradient jump %.6f)"), WorstSeam), WorstSeam < .001);

    // Thirty million blocks out the noise keeps its spread and stays smooth (no precision collapse).
    const FVoxelNoise Fbm = MakeNoise(EVoxelFractal::Fbm, 4);
    auto Spread = [&Fbm](double OriginX, double OriginY, double& OutRoughest)
    {
        double Sum = 0, SumSquares = 0;
        OutRoughest = 0;
        for (int32 Index = 0; Index < 4096; ++Index)
        {
            const double X = OriginX + (Index & 63) * 7.3, Y = OriginY + (Index >> 6) * 7.3;
            const float Value = Fbm.Sample(X, Y);
            Sum += Value;
            SumSquares += Value * Value;
            OutRoughest = FMath::Max(OutRoughest, double(FMath::Abs(Fbm.Sample(X + .01, Y) - Value)));
        }
        return FMath::Sqrt(SumSquares / 4096 - FMath::Square(Sum / 4096));
    };
    double NearRough, FarRough;
    const double Near = Spread(0, 0, NearRough), Far = Spread(30000000, -30000000, FarRough);
    AddInfo(FString::Printf(TEXT("fBm deviation near the origin %.4f, 30 million blocks out %.4f; largest 0.01-block "
                                 "step near %.5f, far %.5f"),
        Near, Far, NearRough, FarRough));
    TestTrue(FString::Printf(TEXT("Far spread %.3f vs near %.3f"), Far, Near), Far > Near * .7 && Far < Near * 1.4);
    TestTrue(FString::Printf(TEXT("Far noise is smooth (largest 0.01-block step %.5f)"), FarRough), FarRough < .01);

    // Different salts or seeds are uncorrelated.
    const FVoxelNoise Other = MakeNoise(EVoxelFractal::Fbm, 4, 0, VoxelRandom::Salt("other"));
    FVoxelNoiseSettings Settings = Fbm.GetSettings();
    const FVoxelNoise OtherSeed(TestSeed + 1, TestSalt, Settings);
    for (const FVoxelNoise* Pair : {&Other, &OtherSeed})
    {
        double SumA = 0, SumB = 0, SumAA = 0, SumBB = 0, SumAB = 0;
        constexpr int32 Count = 20000;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const double X = Uniform(20000), Y = Uniform(20000);
            const double A = Fbm.Sample(X, Y), B = Pair->Sample(X, Y);
            SumA += A, SumB += B, SumAA += A * A, SumBB += B * B, SumAB += A * B;
        }
        const double Correlation = (SumAB / Count - SumA / Count * SumB / Count) /
            FMath::Sqrt((SumAA / Count - FMath::Square(SumA / Count)) * (SumBB / Count - FMath::Square(SumB / Count)));
        AddInfo(FString::Printf(TEXT("Correlation with %s: %.4f"),
            Pair == &Other ? TEXT("another salt") : TEXT("the next seed"), Correlation));
        TestTrue(FString::Printf(TEXT("Independent noises are uncorrelated (r = %.4f)"), Correlation),
            FMath::Abs(Correlation) < .05);
    }

    // Every fractal kind, 2D and 3D, warped or not, stays in [-1, 1].
    for (const EVoxelFractal Fractal : {EVoxelFractal::Fbm, EVoxelFractal::Ridged, EVoxelFractal::Damped})
        for (const double Warp : {0., 40.})
        {
            const FVoxelNoise Noise = MakeNoise(Fractal, 6, Warp);
            double Peak = 0;
            for (int32 Index = 0; Index < 20000; ++Index)
            {
                const double X = Uniform(5000), Y = Uniform(5000), Z = Uniform(300);
                Peak = FMath::Max(
                    Peak, double(FMath::Max(FMath::Abs(Noise.Sample(X, Y)), FMath::Abs(Noise.Sample(X, Y, Z)))));
            }
            TestTrue(FString::Printf(TEXT("Fractal %d, warp %.0f in [-1, 1] (peak %.3f)"), int32(Fractal), Warp, Peak),
                Peak <= 1.0001);
        }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelNoiseGridTest, "Unnamed.Voxel.Noise.Grid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelNoiseGridTest::RunTest(const FString& Parameters)
{
    const FVoxelNoise Noise = MakeNoise(EVoxelFractal::Damped, 5, 16);
    TArray<float> Grid2, Grid3;
    Grid2.SetNumUninitialized(7 * 5);
    Grid3.SetNumUninitialized(5 * 5 * 49);
    Noise.SampleGrid(-31.5, 12.25, 4, 7, 5, Grid2);
    Noise.SampleGrid(160, -32, -64, 4, 8, 5, 5, 49, Grid3);
    bool bMatches2 = true, bMatches3 = true;
    for (int32 IY = 0; IY < 5; ++IY)
        for (int32 IX = 0; IX < 7; ++IX)
            bMatches2 &= FloatBits(Grid2[IY * 7 + IX]) == FloatBits(Noise.Sample(-31.5 + IX * 4, 12.25 + IY * 4));
    for (int32 IZ = 0; IZ < 49; ++IZ)
        for (int32 IY = 0; IY < 5; ++IY)
            for (int32 IX = 0; IX < 5; ++IX)
                bMatches3 &= FloatBits(Grid3[(IZ * 5 + IY) * 5 + IX]) ==
                    FloatBits(Noise.Sample(160. + IX * 4, -32. + IY * 4, -64. + IZ * 8));
    TestTrue(TEXT("2D grid equals point samples, X fastest"), bMatches2);
    TestTrue(TEXT("3D column corners (5 x 5 x 49, 4 x 8 cells) equal point samples"), bMatches3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelNoiseThroughputTest, "Unnamed.Voxel.Noise.Throughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelNoiseThroughputTest::RunTest(const FString& Parameters)
{
    // Logs timings only: thresholds would depend on the machine. voxel.Bench Noise runs the same code at full size.
    const TArray<FVoxelBenchResult> Results = VoxelBench::RunNoise(200000, TestSeed);
    TArray<FString> Lines;
    VoxelBench::Describe(Results).ParseIntoArrayLines(Lines);
    for (const FString& Line : Lines)
        AddInfo(Line);
    for (const FVoxelBenchResult& Result : Results)
        TestTrue(Result.Name + TEXT(" ran"), Result.Seconds > 0 && FMath::IsFinite(Result.Checksum));
    return true;
}
#endif
