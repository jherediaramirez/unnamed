#include "Algo/Find.h"
#include "Density/VoxelDensityGraph.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Noise/VoxelNormalNoise.h"
#include "Math/RandomStream.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // Set before calibrating FVoxelNormalNoise::SpreadScale, and never loosened to pass: our noise's standard deviation
    // and its 1st and 99th percentiles against vanilla's, as ratios.
    constexpr double NoiseSpreadStdTolerance = .10;
    constexpr double NoiseSpreadTailTolerance = .15;

    // The noises the climate reads in the three presets: these must spread like vanilla's. The others are logged, for
    // the 3D density in session 3.
    const TCHAR* const NoiseSpreadGated[] = {TEXT("minecraft:continentalness"), TEXT("minecraft:continentalness_large"),
        TEXT("minecraft:erosion"), TEXT("minecraft:erosion_large"), TEXT("minecraft:ridge"),
        TEXT("minecraft:temperature"), TEXT("minecraft:temperature_large"), TEXT("minecraft:vegetation"),
        TEXT("minecraft:vegetation_large"), TEXT("minecraft:offset")};

    struct FNoiseSpread
    {
        double Std = 0;
        double Low = 0; // 1st percentile
        double High = 0; // 99th percentile
    };

    // Matches tools/WorldgenFacts.java's sampling: 4 seeds x 16384 seeded positions over 64 periods of the lowest
    // octave.
    void MeasureNoiseSpread(const FVoxelDensityNoise& Description, bool bVolume, FNoiseSpread& Out)
    {
        const uint64 Seeds[] = {0, 263, uint64(-4000000007ll), 1234567890123ull};
        constexpr int32 PerSeed = 16384;
        double Extent = 64;
        for (int32 Index = 0; Index < -Description.BaseOctave; ++Index)
            Extent *= 2;
        TArray<double> Values;
        Values.Reserve(UE_ARRAY_COUNT(Seeds) * PerSeed);
        double Sum = 0, Squares = 0;
        for (int32 SeedIndex = 0; SeedIndex < UE_ARRAY_COUNT(Seeds); ++SeedIndex)
        {
            const FVoxelNormalNoise Noise(Seeds[SeedIndex], Description);
            FRandomStream Random(26300 + SeedIndex);
            for (int32 Index = 0; Index < PerSeed; ++Index)
            {
                const double X = (Random.FRand() * 2 - 1) * Extent;
                const double Y = (Random.FRand() * 2 - 1) * Extent;
                const double Z = (Random.FRand() * 2 - 1) * Extent;
                const double Value = Noise.Sample(X, bVolume ? Y : 0, Z);
                Values.Add(Value);
                Sum += Value;
                Squares += Value * Value;
            }
        }
        const double Mean = Sum / Values.Num();
        Out.Std = FMath::Sqrt(Squares / Values.Num() - Mean * Mean);
        Values.Sort();
        Out.Low = Values[FMath::FloorToInt(.01 * (Values.Num() - 1))];
        Out.High = Values[FMath::FloorToInt(.99 * (Values.Num() - 1))];
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelNoiseSpreadTest, "Unnamed.Voxel.Worldgen.NoiseSpread",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelNoiseSpreadTest::RunTest(const FString& Parameters)
{
    const FString Root = VoxelWorldgen::GetReferenceRoot();
    FString Text;
    TSharedPtr<FJsonObject> Facts;
    if (!TestTrue(TEXT("noise facts load"),
            FFileHelper::LoadFileToString(Text, *(Root / TEXT("facts/noise_samples.json"))) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Facts) && Facts.IsValid()))
    {
        return false;
    }

    // Percentile indices in the facts' "values" arrays.
    constexpr int32 LowIndex = 1, HighIndex = 9;
    TArray<FString> Names;
    for (const TSharedPtr<FJsonValue>& Entry : Facts->GetArrayField(TEXT("noises")))
        Names.Add(Entry->AsObject()->GetStringField(TEXT("noise")));
    FVoxelDensityGraph Graph;
    FString Error;
    if (!TestTrue(TEXT("noises read: ") + Error, VoxelWorldgen::ReadReferences(Root, {}, Names, Graph, Error)))
        return false;

    int32 Gated = 0;
    double LogRatioSum = 0;
    int32 Logged = 0;
    for (const TSharedPtr<FJsonValue>& Entry : Facts->GetArrayField(TEXT("noises")))
    {
        const TSharedPtr<FJsonObject> Fact = Entry->AsObject();
        const FString Name = Fact->GetStringField(TEXT("noise"));
        const FVoxelDensityNoise& Description = Graph.Noises[Graph.FindNoise(Name)];
        const bool bGated = Algo::Find(NoiseSpreadGated, Name) != nullptr;
        for (const TCHAR* Plane : {TEXT("xz"), TEXT("xyz")})
        {
            const TSharedPtr<FJsonObject> Vanilla = Fact->GetObjectField(Plane);
            const TArray<TSharedPtr<FJsonValue>>& Percentiles = Vanilla->GetArrayField(TEXT("values"));
            FNoiseSpread Ours;
            MeasureNoiseSpread(Description, FCString::Strcmp(Plane, TEXT("xyz")) == 0, Ours);
            const double StdRatio = Ours.Std / Vanilla->GetNumberField(TEXT("std"));
            const double LowRatio = Ours.Low / Percentiles[LowIndex]->AsNumber();
            const double HighRatio = Ours.High / Percentiles[HighIndex]->AsNumber();
            AddInfo(FString::Printf(TEXT("%s %s: std %.4f (x%.3f), p1 x%.3f, p99 x%.3f%s"), *Name, Plane, Ours.Std,
                StdRatio, LowRatio, HighRatio, bGated ? TEXT(" [gated]") : TEXT("")));
            LogRatioSum += FMath::Loge(StdRatio);
            ++Logged;
            if (bGated)
            {
                ++Gated;
                TestTrue(FString::Printf(TEXT("%s %s std within %.0f%%"), *Name, Plane, NoiseSpreadStdTolerance * 100),
                    FMath::Abs(StdRatio - 1) <= NoiseSpreadStdTolerance);
                TestTrue(
                    FString::Printf(TEXT("%s %s tails within %.0f%%"), *Name, Plane, NoiseSpreadTailTolerance * 100),
                    FMath::Abs(LowRatio - 1) <= NoiseSpreadTailTolerance &&
                        FMath::Abs(HighRatio - 1) <= NoiseSpreadTailTolerance);
            }
        }
    }
    AddInfo(FString::Printf(
        TEXT("Geometric mean std ratio over %d planes: %.4f"), Logged, FMath::Exp(LogRatioSum / Logged)));
    TestEqual(TEXT("gated noises found (2 planes each)"), Gated, 2 * int32(UE_ARRAY_COUNT(NoiseSpreadGated)));
    return true;
}

#endif
