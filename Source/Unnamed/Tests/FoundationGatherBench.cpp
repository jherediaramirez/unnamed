#include "Misc/AutomationTest.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationProfile.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS
// Collision gathering over a plant field, timed (phase 6, S7's gate: at most +10% per tick). It uses only the block
// world's public queries, so the same file times builds from before and after S7. The field: a 32 x 32 grass floor,
// and above it a repeating mix of short grass, both halves of tall grass, dandelions, azaleas (five boxes each),
// bamboo (a moved box) and moss carpet; the pawn's step region (a body and its reach) gathers at 64 places.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationGatherBenchTest, "Unnamed.Foundation.Blocks.GatherBench",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationGatherBenchTest::RunTest(const FString& Parameters)
{
    FFoundationBlockWorld World;
    World.SetRegistry(FFoundationBlockRegistry::GetDefault());
    World.BoundsMin = FIntVector(-1, -1, 0);
    World.BoundsMax = FIntVector(33, 33, 8);
    const FFoundationBlockRegistry& Registry = World.GetRegistry();
    auto State = [&Registry](const TCHAR* Text)
    {
        FFoundationBlockState Parsed;
        Registry.Parse(Text, Parsed);
        return Parsed;
    };
    const FFoundationBlockState Grass = State(TEXT("grass_block")), Lower = State(TEXT("tall_grass[half=lower]")),
                                Upper = State(TEXT("tall_grass[half=upper]"));
    const FFoundationBlockState Mix[] = {State(TEXT("short_grass")), State(TEXT("dandelion")), State(TEXT("azalea")),
        State(TEXT("bamboo")), State(TEXT("moss_carpet")), Lower};
    for (int32 Y = 0; Y < 32; ++Y)
        for (int32 X = 0; X < 32; ++X)
        {
            World.Set({X, Y, 0}, Grass);
            const FFoundationBlockState Plant = Mix[(X * 7 + Y * 3) % UE_ARRAY_COUNT(Mix)];
            World.Set({X, Y, 1}, Plant);
            if (Plant == Lower) World.Set({X, Y, 2}, Upper);
        }
    // A standing body (60 x 60 x 180 cm) and one block around it, as a step gathers.
    const double Cm = UFoundationProfile::BlockCm;
    TArray<FBox> Boxes;
    int64 Gathered = 0;
    constexpr int32 Rounds = 200;
    const double Start = FPlatformTime::Seconds();
    for (int32 Round = 0; Round < Rounds; ++Round)
        for (int32 Place = 0; Place < 64; ++Place)
        {
            const FVector Feet((2 + Place % 8 * 3.7) * Cm, (2 + Place / 8 * 3.7) * Cm, Cm);
            Boxes.Reset();
            World.GatherCollision(FBox(Feet - FVector(30, 30, 0), Feet + FVector(30, 30, 180)).ExpandBy(Cm), Boxes);
            Gathered += Boxes.Num();
        }
    const double Us = (FPlatformTime::Seconds() - Start) * 1e6 / (Rounds * 64);
    AddInfo(FString::Printf(TEXT("GATHER_BENCH us_per_gather=%.3f boxes_per_gather=%.2f gathers=%d"), Us,
        double(Gathered) / (Rounds * 64), Rounds * 64));
    UE_LOG(LogTemp, Display, TEXT("GATHER_BENCH us_per_gather=%.3f boxes_per_gather=%.2f"), Us,
        double(Gathered) / (Rounds * 64));
    TestTrue(TEXT("boxes gathered"), Gathered > 0);
    return true;
}
#endif
