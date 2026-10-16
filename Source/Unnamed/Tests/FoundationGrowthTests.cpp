#include "Misc/AutomationTest.h"
#include "Blocks/VoxelBlockTable.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationBlockFamilies.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationGrowth.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationItems.h"
#include "HAL/IConsoleManager.h"
#include "Random/VoxelRandom.h"
#include "Voxel/VoxelBlocks.h"

// Survival B6a (track T5): random ticks, crops, stems, farmland moisture and the stem tint. The oracles are written
// here from the sources, apart from FoundationGrowth: VALUES "Crop growth" and "Watering farmland" (the f table, 1/13
// and 1/7, the water box), R22 (stems), R23 (the stem colours, survival2.r23.stem_color_rule), the wiki ("Crop
// farming": night growth under the sky; "Beetroot Seeds": the 1/3 skip).
#if WITH_DEV_AUTOMATION_TESTS
namespace FoundationGrowthTests
{
    using namespace FoundationGrowth;
    constexpr EAutomationTestFlags GrowthTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    using FGrowthWrites = TArray<TPair<FIntVector, FFoundationBlockState>, TInlineAllocator<2>>;

    // A block world on the project's registry, open sky over Z 0..31, and the growth table for it.
    struct FGrowthLab
    {
        FFoundationBlockWorld W;
        TUniquePtr<FGrowthTable> T;

        FGrowthLab()
        {
            W.SetRegistry(FFoundationBlockRegistry::GetDefault());
            W.BoundsMin = FIntVector(-16, -16, 0);
            W.BoundsMax = FIntVector(15, 15, 31);
            T = MakeUnique<FGrowthTable>(W.GetRegistry());
        }
        const FFoundationBlockRegistry& R() const { return W.GetRegistry(); }
        FFoundationBlockState S(const TCHAR* Id, const TCHAR* Property = nullptr, const TCHAR* Value = nullptr,
            const TCHAR* Property2 = nullptr, const TCHAR* Value2 = nullptr) const
        {
            const FFoundationBlockType* Block = R().FindBlock(Id);
            if (!Block) return {};
            FFoundationBlockState State = Block->GetDefaultState();
            if (Property) State = R().WithValue(State, Property, Value);
            if (Property2) State = R().WithValue(State, Property2, Value2);
            return State;
        }
        FName Id(const FIntVector& Cell) const { return R().GetBlock(W.Get(Cell).State).Id; }
        FName Value(const FIntVector& Cell, const TCHAR* Property) const
        {
            return R().GetValue(W.Get(Cell).State, Property);
        }
        // The test's own sky: no non-air cell above.
        bool SeesSky(const FIntVector& Cell) const
        {
            for (FIntVector At = Cell + FIntVector(0, 0, 1); At.Z <= W.BoundsMax.Z; ++At.Z)
                if (!W.Get(At).State.IsAir()) return false;
            return true;
        }
        // One random tick with roll stream Seed; its writes applied.
        int32 Tick(const FIntVector& Cell, uint64 Seed)
        {
            FGrowthRoll Roll{Seed};
            FGrowthWrites Writes;
            RandomTick(W, *T, Cell, Roll, [this](const FIntVector& At) { return SeesSky(At); }, Writes);
            for (const TPair<FIntVector, FFoundationBlockState>& Write : Writes)
                W.Set(Write.Key, Write.Value);
            return Writes.Num();
        }
        // A 3x3 of farmland under Centre (Wet: moisture 7, else 0).
        void Field(const FIntVector& Centre, bool bWet, int32 Reach = 1)
        {
            for (int32 DY = -Reach; DY <= Reach; ++DY)
                for (int32 DX = -Reach; DX <= Reach; ++DX)
                    W.Set(Centre + FIntVector(DX, DY, -1),
                        S(TEXT("farmland"), TEXT("moisture"), bWet ? TEXT("7") : TEXT("0")));
        }
    };

    // The hand-computed f of VALUES "Crop growth" for the cases below.
    struct FGrowthPointCase
    {
        const TCHAR* Name;
        // Centre farmland wet; the 8 around: none, dry or wet; crops of the same kind at these offsets.
        bool bCentreWet;
        int32 Around; // 0 none, 1 dry, 2 wet
        TArray<FIntPoint> Same;
        float Expected;
        int32 Denominator;
    };
} // namespace FoundationGrowthTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationGrowthPicksTest, "Unnamed.Foundation.Growth.Picks", FoundationGrowthTests::GrowthTestFlags)
bool FFoundationGrowthPicksTest::RunTest(const FString& Parameters)
{
    using namespace FoundationGrowth;
    // Pure: the same (seed, section, tick, pick) gives the same cell and rolls; inside the section; uniform over it.
    const FIntVector Section(-2, 3, 1);
    int32 Differ = 0;
    TArray<int32> PerAxis[3];
    for (int32 Axis = 0; Axis < 3; ++Axis)
        PerAxis[Axis].Init(0, SectionCells);
    constexpr int32 Samples = 64 * 1024;
    for (int32 Tick = 0; Tick < Samples / 4; ++Tick)
        for (int32 Index = 0; Index < 4; ++Index)
        {
            FGrowthRoll A, B, C;
            const FIntVector Cell = GetRandomTickCell(77, Section, Tick, Index, A);
            const FIntVector Again = GetRandomTickCell(77, Section, Tick, Index, B);
            if (Cell != Again || A.Next(1000) != B.Next(1000))
                return TestTrue(TEXT("a pick is a pure function"), false);
            if (GetRandomTickCell(78, Section, Tick, Index, C) != Cell) ++Differ;
            const FIntVector Local = Cell - Section * SectionCells;
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                if (Local[Axis] < 0 || Local[Axis] >= SectionCells)
                    return TestTrue(TEXT("a pick stays in its section"), false);
                ++PerAxis[Axis][Local[Axis]];
            }
        }
    TestTrue(TEXT("another seed picks other cells"), Differ > Samples * 99 / 100);
    // Many picks per step (a raised RandomTickSpeed): pick i of tick t is neither pick i - 1 nor pick i - 4 of tick
    // t + 1 (the first hash, tick * 4 + pick, repeated the latter).
    int32 Repeats = 0;
    for (int32 Tick = 0; Tick < 64; ++Tick)
        for (int32 Index = 4; Index < 64; ++Index)
            for (const int32 Back : {1, 4})
            {
                FGrowthRoll A, B;
                Repeats += GetRandomTickCell(77, Section, Tick, Index, A) ==
                        GetRandomTickCell(77, Section, Tick + 1, Index - Back, B) &&
                    A.Next(1 << 30) == B.Next(1 << 30);
            }
    TestEqual(TEXT("no stream repeats across ticks"), Repeats, 0);
    // Chi-square per axis, 15 degrees of freedom: 37.7 is p = 0.001.
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        double Chi = 0;
        const double Expected = double(Samples) / SectionCells;
        for (const int32 Count : PerAxis[Axis])
            Chi += FMath::Square(Count - Expected) / Expected;
        TestTrue(FString::Printf(TEXT("axis %d uniform (chi-square %.1f)"), Axis, Chi), Chi < 37.7);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationGrowthPointsTest, "Unnamed.Foundation.Growth.Points", FoundationGrowthTests::GrowthTestFlags)
bool FFoundationGrowthPointsTest::RunTest(const FString& Parameters)
{
    using namespace FoundationGrowthTests;
    // f: 1 + centre (1 dry, 3 wet) + a quarter of each of the 8 around; halved for the same crop on both axes or a
    // diagonal (VALUES "Crop growth"; wiki "Crop farming"). Chance 1 / (floor(25 / f) + 1).
    const TArray<FGrowthPointCase> Cases = {
        {TEXT("isolated dry (VALUES 1/13)"), false, 0, {}, 2.f, 13},
        {TEXT("isolated wet (VALUES 1/7)"), true, 0, {}, 4.f, 7},
        {TEXT("dry 3x3"), false, 1, {}, 4.f, 7},
        {TEXT("wet 3x3 (wiki: surroundings add 6)"), true, 2, {}, 10.f, 3},
        {TEXT("wet centre, dry around"), true, 1, {}, 6.f, 5},
        {TEXT("row along X"), true, 2, {FIntPoint(-1, 0), FIntPoint(1, 0)}, 10.f, 3},
        {TEXT("row along Y"), true, 2, {FIntPoint(0, -1), FIntPoint(0, 1)}, 10.f, 3},
        {TEXT("both axes"), true, 2, {FIntPoint(1, 0), FIntPoint(0, 1)}, 5.f, 6},
        {TEXT("a diagonal"), true, 2, {FIntPoint(1, 1)}, 5.f, 6},
        {TEXT("both axes and a diagonal: halved once"), true, 2, {FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, -1)},
            5.f, 6},
        {TEXT("isolated dry with a diagonal"), false, 0, {FIntPoint(1, 1)}, 1.f, 26},
    };
    for (const FGrowthPointCase& Case : Cases)
    {
        FGrowthLab L;
        const FIntVector Crop(0, 0, 5);
        if (Case.Around) L.Field(Crop, Case.Around == 2);
        L.W.Set(Crop + FIntVector(0, 0, -1),
            L.S(TEXT("farmland"), TEXT("moisture"), Case.bCentreWet ? TEXT("7") : TEXT("0")));
        for (const FIntPoint& Same : Case.Same)
            L.W.Set(Crop + FIntVector(Same.X, Same.Y, 0), L.S(TEXT("wheat")));
        L.W.Set(Crop, L.S(TEXT("wheat")));
        const float Points = GetGrowthPoints(L.W, *L.T, Crop);
        TestEqual(FString::Printf(TEXT("%s: f"), Case.Name), Points, Case.Expected);
        TestEqual(FString::Printf(TEXT("%s: 1/n"), Case.Name), GetChanceDenominator(Points), Case.Denominator);
    }
    // Another crop on the diagonal does not crowd; moisture 1 is wet.
    {
        FGrowthLab L;
        const FIntVector Crop(0, 0, 5);
        L.Field(Crop, true);
        L.W.Set(Crop + FIntVector(1, 1, 0), L.S(TEXT("carrots")));
        L.W.Set(Crop, L.S(TEXT("wheat")));
        TestEqual(TEXT("another crop on a diagonal"), GetGrowthPoints(L.W, *L.T, Crop), 10.f);
        L.Field(Crop, false);
        L.W.Set(Crop + FIntVector(0, 0, -1), L.S(TEXT("farmland"), TEXT("moisture"), TEXT("1")));
        TestEqual(TEXT("moisture 1 counts as wet"), GetGrowthPoints(L.W, *L.T, Crop), 6.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationGrowthRatesTest, "Unnamed.Foundation.Growth.Rates", FoundationGrowthTests::GrowthTestFlags)
bool FFoundationGrowthRatesTest::RunTest(const FString& Parameters)
{
    using namespace FoundationGrowthTests;
    // Seeded runs against the expected rates within 4 sigma: wheat 1/13 dry and 1/7 wet (VALUES), beetroots 2/3 of
    // 1/7 (the wiki's 1/3 skip), torchflower and pitcher at 1/7 (provisional: no source names their rate), a stem
    // 1/7.
    struct FRateCase
    {
        const TCHAR* Crop;
        bool bWet;
        double Rate;
    };
    const FRateCase Cases[] = {{TEXT("wheat"), false, 1. / 13}, {TEXT("wheat"), true, 1. / 7},
        {TEXT("carrots"), true, 1. / 7}, {TEXT("potatoes"), false, 1. / 13}, {TEXT("beetroots"), true, 2. / 21},
        {TEXT("torchflower_crop"), true, 1. / 7}, {TEXT("pitcher_crop"), true, 1. / 7},
        {TEXT("pumpkin_stem"), true, 1. / 7}, {TEXT("melon_stem"), false, 1. / 13}};
    constexpr int32 Trials = 100000;
    for (const FRateCase& Case : Cases)
    {
        FGrowthLab L;
        const FIntVector Crop(0, 0, 5);
        L.W.Set(
            Crop + FIntVector(0, 0, -1), L.S(TEXT("farmland"), TEXT("moisture"), Case.bWet ? TEXT("7") : TEXT("0")));
        const bool bPitcher = FCString::Strcmp(Case.Crop, TEXT("pitcher_crop")) == 0;
        const FFoundationBlockState Start = bPitcher ? L.S(Case.Crop, TEXT("half"), TEXT("lower")) : L.S(Case.Crop);
        int32 Grown = 0;
        for (int32 Trial = 0; Trial < Trials; ++Trial)
        {
            L.W.Set(Crop, Start);
            if (bPitcher) L.W.Set(Crop + FIntVector(0, 0, 1), L.R().WithValue(Start, TEXT("half"), TEXT("upper")));
            L.Tick(Crop, VoxelRandom::Mix64(uint64(Trial) + 1));
            Grown += L.Value(Crop, TEXT("age")) == TEXT("1");
        }
        const double Sigma = FMath::Sqrt(Case.Rate * (1 - Case.Rate) / Trials);
        const double Rate = double(Grown) / Trials;
        TestTrue(FString::Printf(
                     TEXT("%s %s: %.5f vs %.5f"), Case.Crop, Case.bWet ? TEXT("wet") : TEXT("dry"), Rate, Case.Rate),
            FMath::Abs(Rate - Case.Rate) < 4 * Sigma);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationGrowthCropsTest, "Unnamed.Foundation.Growth.Crops", FoundationGrowthTests::GrowthTestFlags)
bool FFoundationGrowthCropsTest::RunTest(const FString& Parameters)
{
    using namespace FoundationGrowthTests;
    const FIntVector Crop(0, 0, 5);
    // Max ages from the state tables (block_states.json): a crop at its last age never changes.
    const TPair<const TCHAR*, int32> MaxAges[] = {{TEXT("wheat"), 7}, {TEXT("carrots"), 7}, {TEXT("potatoes"), 7},
        {TEXT("beetroots"), 3}, {TEXT("torchflower_crop"), 1}, {TEXT("pitcher_crop"), 4}, {TEXT("pumpkin_stem"), 7},
        {TEXT("melon_stem"), 7}};
    for (const TPair<const TCHAR*, int32>& Max : MaxAges)
    {
        FGrowthLab L;
        const FFoundationBlockState State = L.S(Max.Key, TEXT("age"), *FString::FromInt(Max.Value));
        const FGrowthState& Growth = L.T->Get(FCString::Strcmp(Max.Key, TEXT("pitcher_crop")) == 0
                ? L.R().WithValue(State, TEXT("half"), TEXT("lower"))
                : State);
        TestEqual(FString::Printf(TEXT("%s max age"), Max.Key), int32(Growth.MaxAge), Max.Value);
        TestTrue(FString::Printf(TEXT("%s ticks"), Max.Key), Growth.Kind != EGrowthKind::None);
        TestTrue(FString::Printf(TEXT("%s maintains farmland"), Max.Key), Growth.bMaintainsFarmland);
    }
    // A grown crop and the torchflower crop at 1 (no torchflower block yet; B6b changes this pin) stay over 2000 ticks;
    // age goes up one at a time from 0 to 7.
    {
        FGrowthLab L;
        L.Field(Crop, true);
        for (const TCHAR* Id : {TEXT("wheat"), TEXT("torchflower_crop")})
        {
            const FFoundationBlockState Grown =
                L.S(Id, TEXT("age"), Id == FString(TEXT("wheat")) ? TEXT("7") : TEXT("1"));
            L.W.Set(Crop, Grown);
            for (int32 Trial = 0; Trial < 2000; ++Trial)
                L.Tick(Crop, Trial);
            TestEqual(FString::Printf(TEXT("%s stays grown"), Id), L.W.Get(Crop).State, Grown);
        }
        L.W.Set(Crop, L.S(TEXT("wheat")));
        int32 Age = 0;
        for (int32 Trial = 0; Trial < 2000 && Age < 7; ++Trial)
        {
            L.Tick(Crop, VoxelRandom::Mix64(Trial));
            const int32 Now = FCString::Atoi(*L.Value(Crop, TEXT("age")).ToString());
            if (Now != Age && Now != Age + 1) return TestTrue(TEXT("wheat grows one age at a time"), false);
            Age = Now;
        }
        TestEqual(TEXT("wheat reaches 7"), Age, 7);
    }
    // Light: a stone roof stops growth; a torch 4 cells away lights it (14), 5 away does not; a redstone torch (7)
    // never does. The open sky is 15 at any time of day (wiki "Crop farming").
    {
        FGrowthLab L;
        L.Field(Crop, true);
        L.W.Set(Crop, L.S(TEXT("wheat")));
        auto Sky = [&L](const FIntVector& At) { return L.SeesSky(At); };
        TestEqual(TEXT("open sky"), GetLight(L.W, *L.T, Crop, Sky), SkyLight);
        L.W.Set(Crop + FIntVector(0, 0, 3), L.S(TEXT("stone")));
        TestEqual(TEXT("under a roof"), GetLight(L.W, *L.T, Crop, Sky), 0);
        int32 Grown = 0;
        for (int32 Trial = 0; Trial < 2000; ++Trial)
            Grown += L.Tick(Crop, Trial);
        TestEqual(TEXT("no growth in the dark"), Grown, 0);
        const FFoundationBlockState Torch = L.S(TEXT("torch"));
        TestTrue(TEXT("a torch emits 9 or more"), L.R().GetLightEmission(Torch) >= GrowthLight);
        L.W.Set(Crop + FIntVector(5, 0, 0), Torch);
        TestEqual(TEXT("a torch 5 cells away"), GetLight(L.W, *L.T, Crop, Sky), 0);
        L.W.Set(Crop + FIntVector(4, -4, 4), Torch);
        TestEqual(
            TEXT("a torch 4 cells away (each axis)"), GetLight(L.W, *L.T, Crop, Sky), L.R().GetLightEmission(Torch));
        L.W.Set(Crop + FIntVector(4, -4, 4), {});
        // The brightest emitter below 9 the registry has (7 or 8: a redstone torch, a glow lichen face...).
        FFoundationBlockState Dim;
        int32 DimLight = 0;
        for (int32 Id = 1; Id < L.R().GetStateLimit(); ++Id)
        {
            const FFoundationBlockState State{uint16(Id)};
            const int32 Emission = L.R().IsKnown(State) ? L.R().GetLightEmission(State) : 0;
            if (Emission < GrowthLight && Emission > DimLight)
            {
                Dim = State;
                DimLight = Emission;
            }
        }
        TestEqual(TEXT("a dim emitter of 8 exists"), DimLight, GrowthLight - 1);
        L.W.Set(Crop + FIntVector(1, 0, 0), Dim);
        TestEqual(TEXT("an emitter below 9 is too dim"), GetLight(L.W, *L.T, Crop, Sky), 0);
        // An emitter of exactly 9 (lit redstone ore) is enough.
        FFoundationBlockState Nine;
        for (int32 Id = 1; Id < L.R().GetStateLimit() && Nine.IsAir(); ++Id)
        {
            const FFoundationBlockState State{uint16(Id)};
            if (L.R().IsKnown(State) && L.R().GetLightEmission(State) == GrowthLight) Nine = State;
        }
        if (TestFalse(TEXT("an emitter of 9 exists"), Nine.IsAir()))
        {
            L.W.Set(Crop + FIntVector(0, 2, 0), Nine);
            TestEqual(TEXT("an emitter of 9 lights it"), GetLight(L.W, *L.T, Crop, Sky), GrowthLight);
        }
    }
    // The pitcher crop grows both halves; from age 2 it needs its upper half or air above.
    {
        FGrowthLab L;
        L.Field(Crop, true, 2);
        const FFoundationBlockState Lower =
            L.S(TEXT("pitcher_crop"), TEXT("age"), TEXT("2"), TEXT("half"), TEXT("lower"));
        const FIntVector Above = Crop + FIntVector(0, 0, 1);
        int32 Grew = 0, Blocked = 0;
        for (int32 Trial = 0; Trial < 400; ++Trial)
        {
            L.W.Set(Crop, Lower);
            L.W.Set(Above, L.R().WithValue(Lower, TEXT("half"), TEXT("upper")));
            if (L.Tick(Crop, VoxelRandom::Mix64(Trial)))
            {
                ++Grew;
                if (L.Value(Crop, TEXT("age")) != TEXT("3") || L.Value(Above, TEXT("age")) != TEXT("3") ||
                    L.Value(Above, TEXT("half")) != TEXT("upper"))
                    return TestTrue(TEXT("the pitcher crop's halves grow together"), false);
            }
            // Lit by a torch, so only the stone above stops it.
            L.W.Set(Crop + FIntVector(2, 0, 0), L.S(TEXT("torch")));
            L.W.Set(Crop, Lower);
            L.W.Set(Above, L.S(TEXT("stone")));
            Blocked += L.Tick(Crop, VoxelRandom::Mix64(Trial));
        }
        TestTrue(TEXT("the pitcher crop grows"), Grew > 0);
        TestEqual(TEXT("not under stone"), Blocked, 0);
        // Control: the same torch-lit crop under a roof higher up, with air above it, grows and writes its upper half.
        L.W.Set(Above + FIntVector(0, 0, 2), L.S(TEXT("stone")));
        int32 Roofed = 0;
        for (int32 Trial = 0; Trial < 400 && !Roofed; ++Trial)
        {
            L.W.Set(Crop, Lower);
            L.W.Set(Above, {});
            Roofed += L.Tick(Crop, VoxelRandom::Mix64(Trial));
        }
        TestTrue(TEXT("torch-lit, air above: grows"), Roofed > 0);
        TestEqual(TEXT("its upper half is written"), L.Value(Above, TEXT("half")), FName(TEXT("upper")));
        TestEqual(TEXT("the upper half does not tick"),
            int32(L.T->Get(L.R().WithValue(Lower, TEXT("half"), TEXT("upper"))).Kind), int32(EGrowthKind::None));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationGrowthStemsTest, "Unnamed.Foundation.Growth.Stems", FoundationGrowthTests::GrowthTestFlags)
bool FFoundationGrowthStemsTest::RunTest(const FString& Parameters)
{
    using namespace FoundationGrowthTests;
    // R22: at 7, a success picks one of the 4 sides uniformly; the fruit needs air there and its soil below; a blocked
    // pick is not retried. The stem stays at 7 (no attached stem block yet: B6b).
    const FIntVector Stem(0, 0, 5);
    const FIntVector Sides[4] = {FIntVector(0, -1, 0), FIntVector(0, 1, 0), FIntVector(-1, 0, 0), FIntVector(1, 0, 0)};
    for (const TCHAR* Id : {TEXT("pumpkin_stem"), TEXT("melon_stem")})
    {
        const bool bPumpkin = Id == FString(TEXT("pumpkin_stem"));
        const FName Fruit = bPumpkin ? TEXT("pumpkin") : TEXT("melon");
        FGrowthLab L;
        L.W.Set(Stem + FIntVector(0, 0, -1), L.S(TEXT("farmland"), TEXT("moisture"), TEXT("7")));
        for (const FIntVector& Side : Sides)
            L.W.Set(Stem + Side + FIntVector(0, 0, -1), L.S(TEXT("dirt")));
        const FFoundationBlockState Grown = L.S(Id, TEXT("age"), TEXT("7"));
        constexpr int32 Trials = 28000;
        int32 Counts[4] = {}, Successes = 0;
        for (int32 Trial = 0; Trial < Trials; ++Trial)
        {
            for (const FIntVector& Side : Sides)
                L.W.Set(Stem + Side, {});
            L.W.Set(Stem, Grown);
            L.Tick(Stem, VoxelRandom::Mix64(uint64(Trial) * 3 + 1));
            if (L.W.Get(Stem).State != Grown) return TestTrue(TEXT("the stem stays at 7"), false);
            for (int32 Side = 0; Side < 4; ++Side)
                if (L.Id(Stem + Sides[Side]) == Fruit)
                {
                    ++Counts[Side];
                    ++Successes;
                }
        }
        // Isolated wet farmland: f 4, 1/7.
        const double Rate = double(Successes) / Trials, Sigma = FMath::Sqrt((1. / 7) * (6. / 7) / Trials);
        TestTrue(FString::Printf(TEXT("%s fruit rate %.4f vs 1/7"), Id, Rate), FMath::Abs(Rate - 1. / 7) < 4 * Sigma);
        double Chi = 0;
        for (const int32 Count : Counts)
            Chi += FMath::Square(Count - Successes / 4.) / (Successes / 4.);
        // 3 degrees of freedom: 16.3 is p = 0.001.
        TestTrue(FString::Printf(TEXT("%s sides uniform (chi-square %.1f)"), Id, Chi), Chi < 16.3);
        // Three sides blocked by stone: a quarter of the successes land, never elsewhere (no retry).
        int32 Landed = 0;
        for (int32 Trial = 0; Trial < Trials; ++Trial)
        {
            for (int32 Side = 0; Side < 3; ++Side)
                L.W.Set(Stem + Sides[Side], L.S(TEXT("stone")));
            L.W.Set(Stem + Sides[3], {});
            L.Tick(Stem, VoxelRandom::Mix64(uint64(Trial) * 3 + 1));
            Landed += L.Id(Stem + Sides[3]) == Fruit;
            for (int32 Side = 0; Side < 3; ++Side)
                if (L.Id(Stem + Sides[Side]) != TEXT("stone"))
                    return TestTrue(TEXT("a fruit never replaces a block"), false);
        }
        TestEqual(FString::Printf(TEXT("%s: no retry (the open side's own picks)"), Id), Landed, Counts[3]);
        // Stone soil under every side: no fruit.
        int32 Unsupported = 0;
        for (const FIntVector& Side : Sides)
        {
            L.W.Set(Stem + Side, {});
            L.W.Set(Stem + Side + FIntVector(0, 0, -1), L.S(TEXT("stone")));
        }
        for (int32 Trial = 0; Trial < 2000; ++Trial)
            Unsupported += L.Tick(Stem, VoxelRandom::Mix64(Trial));
        TestEqual(FString::Printf(TEXT("%s: no fruit without its soil"), Id), Unsupported, 0);
        // Soil from #minecraft:supports_*_stem_fruit (block_manifest.json): grass and farmland yes, sand no.
        const FGrowthState& Grass = L.T->Get(L.S(TEXT("grass_block")));
        const FGrowthState& Sand = L.T->Get(L.S(TEXT("sand")));
        const FGrowthState& Farmland = L.T->Get(L.S(TEXT("farmland")));
        TestTrue(TEXT("farmland supports the fruit"), bPumpkin ? Farmland.bPumpkinSoil : Farmland.bMelonSoil);
        TestTrue(TEXT("grass supports the fruit"), bPumpkin ? Grass.bPumpkinSoil : Grass.bMelonSoil);
        TestFalse(TEXT("sand does not"), bPumpkin ? Sand.bPumpkinSoil : Sand.bMelonSoil);
        // Below 7 the stem ages one step.
        L.W.Set(Stem, L.S(Id, TEXT("age"), TEXT("3")));
        int32 Trial = 0;
        while (L.Value(Stem, TEXT("age")) == TEXT("3") && Trial < 1000)
            L.Tick(Stem, VoxelRandom::Mix64(Trial++));
        TestEqual(FString::Printf(TEXT("%s ages 3 -> 4"), Id), L.Value(Stem, TEXT("age")), FName(TEXT("4")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationGrowthFarmlandTest, "Unnamed.Foundation.Growth.Farmland", FoundationGrowthTests::GrowthTestFlags)
bool FFoundationGrowthFarmlandTest::RunTest(const FString& Parameters)
{
    using namespace FoundationGrowthTests;
    // VALUES "Watering farmland": water within X/Z -4..4 and Y 0..+1 (here Z up) sets 7 (native: X 4,Z 1 wet, X 5,Z 1
    // not); otherwise -1 per tick; dry at 0 turns to dirt unless the block above maintains it.
    const FIntVector Farm(0, 0, 5);
    struct FWaterCase
    {
        FIntVector Offset;
        bool bWet;
    };
    const FWaterCase Cases[] = {{FIntVector(4, 1, 0), true}, {FIntVector(5, 1, 0), false}, {FIntVector(1, 4, 0), true},
        {FIntVector(1, 5, 0), false}, {FIntVector(-4, -4, 0), true}, {FIntVector(-4, -4, 1), true},
        {FIntVector(0, 1, 2), false}, {FIntVector(1, 0, -1), false}};
    for (const FWaterCase& Case : Cases)
    {
        FGrowthLab L;
        L.W.Set(Farm, L.S(TEXT("farmland"), TEXT("moisture"), TEXT("3")));
        L.W.Set(Farm + Case.Offset, L.S(TEXT("water")));
        L.Tick(Farm, 1);
        TestEqual(FString::Printf(TEXT("water at %s"), *Case.Offset.ToString()), L.Value(Farm, TEXT("moisture")),
            FName(Case.bWet ? TEXT("7") : TEXT("2")));
    }
    {
        // A waterlogged block's water counts (a fluid state holding water).
        FGrowthLab L;
        FFoundationBlockState Logged;
        for (int32 Id = 1; Id < L.R().GetStateLimit() && Logged.IsAir(); ++Id)
        {
            const FFoundationBlockState State{uint16(Id)};
            if (L.R().IsKnown(State) && !L.R().GetBlock(State).IsWater() && L.R().GetFluid(State).bWater)
                Logged = State;
        }
        TestFalse(TEXT("a water-holding state exists"), Logged.IsAir());
        L.W.Set(Farm, L.S(TEXT("farmland")));
        L.W.Set(Farm + FIntVector(2, 0, 1), Logged);
        L.Tick(Farm, 1);
        TestEqual(TEXT("waterlogged water hydrates"), L.Value(Farm, TEXT("moisture")), FName(TEXT("7")));
    }
    {
        FGrowthLab L;
        L.W.Set(Farm, L.S(TEXT("farmland"), TEXT("moisture"), TEXT("2")));
        L.Tick(Farm, 1);
        L.Tick(Farm, 2);
        TestEqual(TEXT("dries one step per tick"), L.Value(Farm, TEXT("moisture")), FName(TEXT("0")));
        L.W.Set(Farm + FIntVector(0, 0, 1), L.S(TEXT("wheat")));
        L.Tick(Farm, 3);
        TestEqual(TEXT("a crop keeps dry farmland"), L.Id(Farm), FName(TEXT("farmland")));
        L.W.Set(Farm + FIntVector(0, 0, 1), L.S(TEXT("oak_fence_gate")));
        L.Tick(Farm, 4);
        TestEqual(TEXT("a fence gate keeps it (maintains_farmland)"), L.Id(Farm), FName(TEXT("farmland")));
        L.W.Set(Farm + FIntVector(0, 0, 1), L.S(TEXT("dandelion")));
        L.Tick(Farm, 5);
        TestEqual(TEXT("a flower does not"), L.Id(Farm), FName(TEXT("dirt")));
        L.W.Set(Farm, L.S(TEXT("farmland"), TEXT("moisture"), TEXT("7")));
        L.W.Set(Farm + FIntVector(0, 0, 1), {});
        L.Tick(Farm, 6);
        TestEqual(TEXT("wet farmland with nothing near dries to 6"), L.Value(Farm, TEXT("moisture")), FName(TEXT("6")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationGrowthAuthorityTest, "Unnamed.Foundation.Growth.Authority", FoundationGrowthTests::GrowthTestFlags)
bool FFoundationGrowthAuthorityTest::RunTest(const FString& Parameters)
{
    // The authority's step: 3 picks per section near the player; wheat by water grows to 7; bare dry farmland turns
    // to dirt; the switches stop everything.
    struct FGrowthAuthorityLab
    {
        FFoundationAuthority A;
        int32 Player = 0;
        FGrowthAuthorityLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bGrowth = true;
            Player = A.AddPlayer();
            A.Reset();
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = A.World.OriginCm + FVector(50, 250, 600);
            Facts.EyeCm = Facts.FeetCm + FVector(0, 0, 162);
            Facts.Aim = FVector(1, 0, 0);
            A.SetFacts(Player, Facts);
        }
        FFoundationBlockState S(const TCHAR* Id, const TCHAR* Property = nullptr, const TCHAR* Value = nullptr)
        {
            FFoundationBlockState State = A.World.FindBlock(Id)->GetDefaultState();
            return Property ? A.World.GetRegistry().WithValue(State, Property, Value) : State;
        }
        FName Id(const FIntVector& Cell) { return A.World.GetBlock(A.World.Get(Cell).State).Id; }
    };
    const FIntVector Crop(0, 0, 4), Water(1, 0, 3), Bare(-6, 3, 3);
    auto Build = [&](FGrowthAuthorityLab& L)
    {
        L.A.World.Set(Crop + FIntVector(0, 0, -1), L.S(TEXT("farmland")));
        L.A.World.Set(Crop, L.S(TEXT("wheat")));
        L.A.World.Set(Water, L.S(TEXT("water")));
        L.A.World.Set(Bare, L.S(TEXT("farmland")));
    };
    {
        FGrowthAuthorityLab L;
        Build(L);
        L.A.Step();
        // Radius 4: 9 x 9 section columns (picks count in sections outside the bounds too), Z 0..15 one section, 3
        // picks each.
        TestEqual(TEXT("picks per step"), L.A.RandomTicks, int64(9 * 9 * 3));
        // Accelerated (300 picks per section) so the test runs in a few thousand steps.
        L.A.Rules.RandomTickSpeed = 300;
        for (int32 Step = 0;
            Step < 4000 && L.A.World.GetRegistry().GetValue(L.A.World.Get(Crop).State, TEXT("age")) != TEXT("7");
            ++Step)
            L.A.Step();
        TestEqual(TEXT("wheat grows to 7"), L.A.World.GetRegistry().GetValue(L.A.World.Get(Crop).State, TEXT("age")),
            FName(TEXT("7")));
        TestEqual(TEXT("its farmland is wet"),
            L.A.World.GetRegistry().GetValue(L.A.World.Get(Crop + FIntVector(0, 0, -1)).State, TEXT("moisture")),
            FName(TEXT("7")));
        TestEqual(TEXT("bare dry farmland turned to dirt"), L.Id(Bare), FName(TEXT("dirt")));
        TestEqual(TEXT("no pops"), L.A.Pops, 0);
    }
    for (int32 Switch = 0; Switch < 2; ++Switch)
    {
        FGrowthAuthorityLab L;
        Build(L);
        if (Switch == 0)
            L.A.Options.bGrowth = false;
        else
            L.A.Rules.RandomTickSpeed = 0;
        const int64 Revision = L.A.World.Revision;
        for (int32 Step = 0; Step < 10000; ++Step)
            L.A.Step();
        TestEqual(Switch == 0 ? TEXT("bGrowth off: nothing changes") : TEXT("speed 0: nothing changes"),
            L.A.World.Revision, Revision);
        TestEqual(TEXT("no picks"), L.A.RandomTicks, int64(0));
    }
    // The pawn's switch.
    TestTrue(TEXT("foundation.Blocks.Growth defaults to 1"), FoundationGrowth::IsOn());
    {
        // Cost figure (not a gate): a step's picks around one player, 300 steps.
        FGrowthAuthorityLab L;
        Build(L);
        const double Start = FPlatformTime::Seconds();
        for (int32 Step = 0; Step < 300; ++Step)
            L.A.Step();
        AddInfo(FString::Printf(TEXT("GROWTH_COST %.4f ms per step (81 sections, 243 picks)"),
            (FPlatformTime::Seconds() - Start) * 1000 / 300));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationGrowthStemTintTest, "Unnamed.Foundation.Growth.StemTint", FoundationGrowthTests::GrowthTestFlags)
bool FFoundationGrowthStemTintTest::RunTest(const FString& Parameters)
{
    // R23 (survival2.r23.stem_tint_sources, stem_color_rule): RGB by age, written here from the findings table.
    const uint32 ByAge[8] = {0x00FF00, 0x20F704, 0x40EF08, 0x60E70C, 0x80DF10, 0xA0D714, 0xC0CF18, 0xE0C71C};
    const FFoundationBlockRegistry& Registry = *FFoundationBlockRegistry::GetDefault();
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::MakeTable(Registry);
    for (const TCHAR* Id : {TEXT("pumpkin_stem"), TEXT("melon_stem")})
    {
        const FFoundationBlockType* Block = Registry.FindBlock(Id);
        if (!TestNotNull(Id, Block)) continue;
        for (int32 Age = 0; Age < 8; ++Age)
        {
            const FFoundationBlockState State =
                Registry.WithValue(Block->GetDefaultState(), TEXT("age"), FName(*FString::FromInt(Age)));
            const FVoxelStateInfo& Info = Table->Get(State.Id);
            TestEqual(FString::Printf(TEXT("%s age %d tint"), Id, Age), Info.Tint, ByAge[Age]);
            TestEqual(FString::Printf(TEXT("%s age %d fixed tint"), Id, Age), int32(Info.TintSource),
                int32(EVoxelTintSource::Constant));
            TestTrue(FString::Printf(TEXT("%s age %d registry"), Id, Age),
                Registry.GetTint(State) == FColor(ByAge[Age] >> 16, ByAge[Age] >> 8 & 255, ByAge[Age] & 255, 255));
        }
    }
    // Other blocks keep their one tint: the grass block's plains grass colour.
    const FFoundationBlockType* Grass = Registry.FindBlock(TEXT("grass_block"));
    if (TestNotNull(TEXT("grass_block"), Grass))
        TestTrue(TEXT("grass keeps its tint"), Registry.GetTint(Grass->GetDefaultState()) == Grass->Tint);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationGrowthPlantTest, "Unnamed.Foundation.Growth.Plant", FoundationGrowthTests::GrowthTestFlags)
bool FFoundationGrowthPlantTest::RunTest(const FString& Parameters)
{
    // wheat_seeds plants wheat on farmland, from the top face, and nowhere else (the wheat block's survival). The
    // other seeds come with B6b (QC-KNOWN-BUGS).
    struct FGrowthPlantLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        FFoundationInteraction I;
        int32 Player = 0;
        int64 StepIndex = 0;
        FGrowthPlantLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bInfiniteSupply = false;
            // The game's plant survival rules (the voxel world and the Playground set them).
            A.PlantSurvival = VoxelBlocks::GetPlantSurvival(A.World.GetRegistry());
            Player = A.AddPlayer();
            A.Reset();
            I.PlayerId = Player;
            I.SetPreset(*UFoundationInteractionSettings::Get().FindPreset(TEXT("VanillaReference")), .05);
            I.Resync(A);
        }
        FFoundationPlayer& P() { return *A.FindPlayer(Player); }
        void Aim(const FIntVector& Target)
        {
            FFoundationPlayerFacts Facts;
            const FVector Centre = FoundationGrid::CellBox(Target, A.World.OriginCm).GetCenter();
            Facts.EyeCm = Centre + FVector(0, 0, 220);
            Facts.FeetCm = Facts.EyeCm - FVector(0, 0, 162);
            Facts.Aim = FVector(0, 0, -1);
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
            A.Step();
        }
        FFoundationResult Exec(EFoundationCommandType Type, int32 Slot = 0, FName Item = NAME_None, int32 Count = 0)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = I.NextSequence++;
            Command.Type = Type;
            Command.Hand = EFoundationHand::Main;
            Command.Item = P().Inventory.GetHand(EFoundationHand::Main).GetRef();
            Command.Slot = Slot;
            Command.CreativeItem = Item;
            Command.CreativeCount = Count;
            if (Type == EFoundationCommandType::UseOn)
            {
                Command.Target = A.Resolve(P().Facts);
                if (Command.Target.IsBlockLike())
                {
                    Command.Destination = Command.Target.Cell + FoundationGrid::FaceOffset(Command.Target.Face);
                    Command.DestinationRevision = A.World.Get(Command.Destination).Revision;
                }
            }
            return A.Execute(Player, Command, Vitals);
        }
        FName At(const FIntVector& Cell) { return A.World.GetBlock(A.World.Get(Cell).State).Id; }
    };
    for (const TCHAR* Ground : {TEXT("farmland"), TEXT("dirt"), TEXT("grass_block")})
    {
        FGrowthPlantLab L;
        const FIntVector Soil(0, 0, 4);
        L.A.World.Set(Soil, L.A.World.FindBlock(Ground)->GetDefaultState());
        L.Aim(Soil);
        L.Exec(EFoundationCommandType::SetCreativeSlot, 0, TEXT("wheat_seeds"), 4);
        L.Exec(EFoundationCommandType::SelectSlot, 0);
        L.Exec(EFoundationCommandType::UseOn);
        const bool bFarmland = FCString::Strcmp(Ground, TEXT("farmland")) == 0;
        TestEqual(FString::Printf(TEXT("wheat_seeds on %s"), Ground), L.At(Soil + FIntVector(0, 0, 1)),
            bFarmland ? FName(TEXT("wheat")) : FName(TEXT("air")));
        if (bFarmland) TestEqual(TEXT("one seed used"), L.P().Inventory.Hotbar[0].Count, 3);
    }
    // The soil rule (block_manifest.json: #supports_crops, #supports_pumpkin_stem, #supports_melon_stem: farmland).
    {
        FoundationGrowthTests::FGrowthLab L;
        const FIntVector Plant(0, 0, 5), Below(0, 0, 4);
        auto Survival = [&](const TCHAR* Soil, FFoundationBlockState State)
        {
            L.W.Set(Below, L.S(Soil));
            L.W.Set(Plant, State);
            return FoundationBlockFamilies::GetSurvival(L.W, Plant, State);
        };
        for (const TCHAR* Id : {TEXT("wheat"), TEXT("carrots"), TEXT("potatoes"), TEXT("beetroots"),
                 TEXT("torchflower_crop"), TEXT("pumpkin_stem"), TEXT("melon_stem")})
        {
            TestEqual(FString::Printf(TEXT("%s on farmland"), Id), int32(Survival(TEXT("farmland"), L.S(Id))),
                int32(EFoundationFamilySurvival::Survives));
            TestEqual(FString::Printf(TEXT("%s on grass"), Id), int32(Survival(TEXT("grass_block"), L.S(Id))),
                int32(EFoundationFamilySurvival::Fails));
        }
        const FFoundationBlockState Lower = L.S(TEXT("pitcher_crop"), TEXT("half"), TEXT("lower"));
        TestEqual(TEXT("pitcher lower half on dirt"), int32(Survival(TEXT("dirt"), Lower)),
            int32(EFoundationFamilySurvival::Fails));
        TestEqual(TEXT("pitcher upper half: the plant rules pair it"),
            int32(Survival(TEXT("dirt"), L.R().WithValue(Lower, TEXT("half"), TEXT("upper")))),
            int32(EFoundationFamilySurvival::NoRule));
        IConsoleVariable* Growth = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Blocks.Growth"));
        if (TestNotNull(TEXT("foundation.Blocks.Growth"), Growth))
        {
            const int32 Old = Growth->GetInt();
            Growth->Set(0, ECVF_SetByCode);
            TestEqual(TEXT("switch 0: no soil rule"), int32(Survival(TEXT("grass_block"), L.S(TEXT("wheat")))),
                int32(EFoundationFamilySurvival::NoRule));
            Growth->Set(Old, ECVF_SetByCode);
        }
    }
    return true;
}
#endif
