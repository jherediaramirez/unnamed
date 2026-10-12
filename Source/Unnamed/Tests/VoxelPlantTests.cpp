#include "Biome/VoxelBiomeSource.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Decoration/Features/VoxelPatchFeatures.h"
#include "Decoration/VoxelBlockPredicates.h"
#include "Decoration/VoxelDecorationContext.h"
#include "Decoration/VoxelDecorator.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Decoration/VoxelStateProviders.h"
#include "Misc/AutomationTest.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // For manual runs: 0 none, 5 supports_vegetation left out of the survival table, 6 writes no longer raise the
    // live heightmaps (VoxelDecorationTests.cpp's numbering), 7 glow lichen's search walks 4 cells instead of 1
    // (Plants.Multiface). With 0, every test must pass.
    constexpr int32 PlantTestMutation = 0;

    bool PlantTestHasBlocks(FAutomationTestBase& Test, const FVoxelBlockTable& Table)
    {
        if (Table.FindState(TEXT("stone"))) return true;
        Test.AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }

    // The survival table from the reference's tags.
    bool MakeSurvival(FAutomationTestBase& Test, const FVoxelBlockTable& Table, FVoxelPlantSurvival& Out)
    {
        FVoxelFeatureGraph Graph;
        FString Error;
        if (!Test.TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error),
                VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error)))
            return false;
        FVoxelBlockTags Tags(Graph, Table);
        return Test.TestTrue(FString::Printf(TEXT("the survival table builds (%s)"), *Error),
            Out.Initialize(
                Table, Tags, &Error, PlantTestMutation == 5 ? TEXT("minecraft:supports_vegetation") : TEXT("")));
    }

    // A flat column: Ground up to TopZ (stone, then three of Under, then Top), then Fill up to FillZ, air above.
    TSharedRef<FVoxelColumn> MakeFlatColumn(const FVoxelBlockTable& Table, int32 TopZ, const TCHAR* Top,
        const TCHAR* Under, const TCHAR* Fill = TEXT("air"), int32 FillZ = 0)
    {
        TArray<uint16> Dense;
        Dense.SetNumZeroed(FVoxelColumn::DenseVolume);
        const uint16 Stone = Table.FindState(TEXT("stone")), TopState = Table.FindState(Top),
                     UnderState = Table.FindState(Under), FillState = Table.FindState(Fill);
        for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
        {
            const uint16 State = Z == TopZ ? TopState
                : Z < TopZ - 3             ? Stone
                : Z < TopZ                 ? UnderState
                : Z <= FillZ               ? FillState
                                           : uint16(0);
            for (int32 Cell = 0; Cell < FVoxelColumn::Area; ++Cell)
                Dense[FVoxelColumn::GetDenseIndex(0, 0, Z) + Cell] = State;
        }
        TSharedRef<FVoxelColumn> Column = MakeShared<FVoxelColumn>();
        Column->Build(Dense, Table);
        return Column;
    }

    // One patch tail from an origin, as 26.3's patches end: Tries tries, each offset by trapezoids (±XY across, ±Z
    // up), kept where the cell is Wanted (air, or water), then the state placed if it survives. Returns the distinct
    // cells of the placed state.
    int32 RunPatch(const FVoxelBlockTable& Table, const FVoxelPlantSurvival& Survival,
        const TMap<uint16, uint16>& UpperOf, const FVoxelColumn& Flat, FIntVector Origin, int32 Tries, int32 XY,
        int32 Z, uint16 State, bool bWater, uint64 Seed, int32* OutUppers = nullptr)
    {
        const FVoxelColumn* Around[9];
        for (const FVoxelColumn*& Column : Around)
            Column = &Flat;
        FVoxelDecorationContext Context(FIntPoint(0, 0), Around, &Table);
        FVoxelBlockPredicates Predicates;
        FVoxelStateProviders Providers;
        TSet<FIntVector> Lowers, Uppers;
        const uint16 Upper = UpperOf.FindRef(State);
        const auto Emit = [&](const FVoxelPlantWrite& Write)
        {
            Context.SetState(Write.Cell.X, Write.Cell.Y, Write.Cell.Z, Write.State);
            (Write.State == State ? Lowers : Uppers).Add(Write.Cell);
        };
        const FVoxelPlantEnvironment Env{Table, Survival, Predicates, Providers, Context, UpperOf, Emit};
        const FVoxelIntProvider Across = FVoxelIntProvider::MakeTrapezoid(-XY, XY, 0);
        const FVoxelIntProvider Up = FVoxelIntProvider::MakeTrapezoid(-Z, Z, 0);
        for (int32 Try = 0; Try < Tries; ++Try)
        {
            FVoxelXoroshiro Random(VoxelRandom::Hash64(Seed, VoxelRandom::Salt("plant test"), Try, 0));
            FIntVector Cell = Origin;
            Cell.X += Across.Sample(Random);
            Cell.Z += Up.Sample(Random);
            Cell.Y += Across.Sample(Random);
            const uint16 There = Context.GetState(Cell.X, Cell.Y, Cell.Z);
            if (bWater ? !(Table.Get(There).IsFluid() && Survival.IsWaterSource(There)) : There != 0) continue;
            VoxelPatchFeatures::PlacePlant(Env, Cell, State);
        }
        if (OutUppers) *OutUppers = Uppers.Num() + (Upper ? 0 : Lowers.Num());
        return Lowers.Num();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPlantPropertiesTest, "Unnamed.Voxel.Plants.Properties",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPlantPropertiesTest::RunTest(const FString& Parameters)
{
    // The core table's block properties (S8): plants with properties resolve to the registry's state ids.
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!PlantTestHasBlocks(*this, *Table)) return true;
    const int32 Lower = Table->MakeState(TEXT("minecraft:tall_grass"), {{TEXT("half"), TEXT("lower")}});
    const int32 Upper = Table->WithValue(uint16(Lower), TEXT("half"), TEXT("upper"));
    TestTrue(TEXT("tall_grass has both halves"), Lower != INDEX_NONE && Upper != INDEX_NONE && Lower != Upper);
    TestTrue(TEXT("the upper half reads upper"), Table->GetValue(uint16(Upper), TEXT("half")) == FName(TEXT("upper")));
    TestEqual(TEXT("both halves are tall grass"), int32(Table->Get(uint16(Upper)).Block),
        int32(Table->Get(uint16(Lower)).Block));
    const int32 Pickles =
        Table->MakeState(TEXT("sea_pickle"), {{TEXT("pickles"), TEXT("3")}, {TEXT("waterlogged"), TEXT("true")}});
    TestTrue(TEXT("sea_pickle pickles=3 waterlogged=true exists"), Pickles != INDEX_NONE);
    if (Pickles != INDEX_NONE)
    {
        TestTrue(TEXT("its pickles"), Table->GetValue(uint16(Pickles), TEXT("pickles")) == FName(TEXT("3")));
        TestTrue(
            TEXT("its waterlogging"), Table->GetValue(uint16(Pickles), TEXT("waterlogged")) == FName(TEXT("true")));
        TestTrue(TEXT("a waterlogged pickle holds water"), Table->Get(uint16(Pickles)).HeldFluid != 0);
    }
    TestEqual(TEXT("an unknown value gives none"), Table->WithValue(uint16(Lower), TEXT("half"), TEXT("middle")),
        int32(INDEX_NONE));
    TestEqual(TEXT("an unknown block gives none"), Table->MakeState(TEXT("no_such_block"), {}), int32(INDEX_NONE));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPlantSurvivalTest, "Unnamed.Voxel.Plants.Survival",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPlantSurvivalTest::RunTest(const FString& Parameters)
{
    // Each survival rule on hand-made neighbourhoods (26.3's support tags; the rules scout I's probes confirmed).
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    FVoxelPlantSurvival Survival;
    if (!PlantTestHasBlocks(*this, *Table) || !MakeSurvival(*this, *Table, Survival)) return false;
    const auto S = [&Table](const TCHAR* Id) { return Table->FindState(Id); };
    struct FCase
    {
        const TCHAR* Name;
        const TCHAR* Plant;
        const TCHAR* Below;
        const TCHAR* Cell;
        const TCHAR* Side;
        bool bSurvives;
    };
    const FCase Cases[] = {
        {TEXT("short grass on grass"), TEXT("short_grass"), TEXT("grass_block"), TEXT("air"), TEXT("air"), true},
        {TEXT("short grass on dirt"), TEXT("short_grass"), TEXT("dirt"), TEXT("air"), TEXT("air"), true},
        {TEXT("short grass on stone"), TEXT("short_grass"), TEXT("stone"), TEXT("air"), TEXT("air"), false},
        {TEXT("a poppy on grass"), TEXT("poppy"), TEXT("grass_block"), TEXT("air"), TEXT("air"), true},
        {TEXT("a poppy on sand"), TEXT("poppy"), TEXT("sand"), TEXT("air"), TEXT("air"), false},
        {TEXT("a dead bush on sand"), TEXT("dead_bush"), TEXT("sand"), TEXT("air"), TEXT("air"), true},
        {TEXT("a dead bush on stone"), TEXT("dead_bush"), TEXT("stone"), TEXT("air"), TEXT("air"), false},
        {TEXT("sugar cane on sand by water"), TEXT("sugar_cane"), TEXT("sand"), TEXT("air"), TEXT("water"), true},
        {TEXT("sugar cane on sand, dry"), TEXT("sugar_cane"), TEXT("sand"), TEXT("air"), TEXT("air"), false},
        {TEXT("cactus on sand, free sides"), TEXT("cactus"), TEXT("sand"), TEXT("air"), TEXT("air"), true},
        {TEXT("cactus beside stone"), TEXT("cactus"), TEXT("sand"), TEXT("air"), TEXT("stone"), false},
        {TEXT("a lily pad on water"), TEXT("lily_pad"), TEXT("water"), TEXT("air"), TEXT("air"), true},
        {TEXT("a lily pad on stone"), TEXT("lily_pad"), TEXT("stone"), TEXT("air"), TEXT("air"), false},
        {TEXT("seagrass in water on stone"), TEXT("seagrass"), TEXT("stone"), TEXT("water"), TEXT("water"), true},
        {TEXT("seagrass on magma"), TEXT("seagrass"), TEXT("magma_block"), TEXT("water"), TEXT("water"), false},
        {TEXT("seagrass in air"), TEXT("seagrass"), TEXT("stone"), TEXT("air"), TEXT("air"), false},
        {TEXT("kelp on kelp"), TEXT("kelp"), TEXT("kelp_plant"), TEXT("water"), TEXT("water"), true},
        {TEXT("a mushroom on stone"), TEXT("brown_mushroom"), TEXT("stone"), TEXT("air"), TEXT("air"), true},
        {TEXT("a mushroom on glass"), TEXT("brown_mushroom"), TEXT("glass"), TEXT("air"), TEXT("air"), false},
        {TEXT("a pumpkin anywhere"), TEXT("pumpkin"), TEXT("stone"), TEXT("air"), TEXT("air"), true},
    };
    for (const FCase& Case : Cases)
    {
        const uint16 Plant = S(Case.Plant), Below = S(Case.Below), Cell = S(Case.Cell), Side = S(Case.Side);
        if (!TestTrue(FString::Printf(TEXT("%s: the blocks exist"), Case.Name), Plant != 0 && Below != 0)) continue;
        const bool bSurvives = Survival.Survives(Plant, 0, 0, 0, [&](int32 X, int32 Y, int32 Z) -> uint16
            { return Z == -1 ? (X || Y ? Side : Below)
                  : Z == 0   ? (X || Y ? Side : Cell)
                             : 0; });
        TestTrue(
            FString::Printf(TEXT("%s: survives %d"), Case.Name, int32(Case.bSurvives)), bSurvives == Case.bSurvives);
    }
    // A two-cell plant's upper half needs its lower half.
    const int32 Lower = Table->MakeState(TEXT("tall_grass"), {{TEXT("half"), TEXT("lower")}});
    const int32 Upper = Table->MakeState(TEXT("tall_grass"), {{TEXT("half"), TEXT("upper")}});
    TestTrue(TEXT("an upper half over its lower half"), Survival.SurvivesOn(uint16(Upper), uint16(Lower), 0));
    TestFalse(TEXT("an upper half over grass"), Survival.SurvivesOn(uint16(Upper), S(TEXT("grass_block")), 0));
    TestEqual(TEXT("stone is sturdy on top"),
        int32(Survival.GetSturdyFaces(S(TEXT("stone")))) >> int32(EVoxelFace::Up) & 1, 1);
    TestEqual(TEXT("short grass has no sturdy face"), int32(Survival.GetSturdyFaces(S(TEXT("short_grass")))), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPlantLiveHeightmapsTest, "Unnamed.Voxel.Plants.LiveHeightmaps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPlantLiveHeightmapsTest::RunTest(const FString& Parameters)
{
    // A write raises the live heightmaps that count it, and never the _WG maps (the snapshot's).
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!PlantTestHasBlocks(*this, *Table)) return true;
    const TSharedRef<FVoxelColumn> Flat = MakeFlatColumn(*Table, 63, TEXT("grass_block"), TEXT("dirt"));
    const FVoxelColumn* Around[9];
    for (const FVoxelColumn*& Column : Around)
        Column = &*Flat;
    FVoxelDecorationContext Context(FIntPoint(0, 0), Around, &*Table);
    if (PlantTestMutation == 6) Context.FreezeHeightsForTest();
    using EMap = EVoxelDecorationHeightmap;
    TestEqual(TEXT("motion blocking starts at the grass"), Context.GetHeight(EMap::MotionBlocking, 5, 5), 63);
    Context.SetState(5, 5, 64, Table->FindState(TEXT("pumpkin")));
    Context.SetState(6, 6, 64, Table->FindState(TEXT("short_grass")));
    TestEqual(TEXT("a pumpkin raises motion blocking"), Context.GetHeight(EMap::MotionBlocking, 5, 5), 64);
    TestEqual(TEXT("a pumpkin raises the ocean floor"), Context.GetHeight(EMap::OceanFloor, 5, 5), 64);
    TestEqual(TEXT("the _WG maps stay the snapshot's"), Context.GetHeight(EMap::WorldSurfaceWG, 5, 5), 63);
    TestEqual(TEXT("grass raises the world surface"), Context.GetHeight(EMap::WorldSurface, 6, 6), 64);
    TestEqual(TEXT("grass leaves motion blocking"), Context.GetHeight(EMap::MotionBlocking, 6, 6), 63);
    TestEqual(
        TEXT("outside the window: none"), Context.GetHeight(EMap::MotionBlocking, 100, 5), VoxelConstants::MinZ - 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPlantPatchCountsTest, "Unnamed.Voxel.Plants.PatchCounts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPlantPatchCountsTest::RunTest(const FString& Parameters)
{
    // Scout I's oracles (facts 5.1, confirmed by its vanilla probes): the distinct cells a patch tail plants on flat
    // ground, from an origin on the first air (or water) cell, are sum over cells of 1 - (1 - p)^n. Only dy = 0 tries
    // survive on land. 2,000 origins each; the bound is about 4.5 standard errors of the mean.
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    FVoxelPlantSurvival Survival;
    if (!PlantTestHasBlocks(*this, *Table) || !MakeSurvival(*this, *Table, Survival)) return false;
    const TSharedRef<FVoxelColumn> Land = MakeFlatColumn(*Table, 63, TEXT("grass_block"), TEXT("dirt"));
    const TSharedRef<FVoxelColumn> Sea = MakeFlatColumn(*Table, 59, TEXT("stone"), TEXT("stone"), TEXT("water"), 75);
    const uint16 TallLower = uint16(Table->MakeState(TEXT("tall_grass"), {{TEXT("half"), TEXT("lower")}}));
    TMap<uint16, uint16> UpperOf;
    UpperOf.Add(TallLower, uint16(Table->WithValue(TallLower, TEXT("half"), TEXT("upper"))));
    struct FCase
    {
        const TCHAR* Name;
        const FVoxelColumn* Ground;
        int32 Tries, XY, Z;
        uint16 State;
        bool bWater;
        double Expected, Bound;
    };
    const FCase Cases[] = {
        {TEXT("short grass (32, ±7, ±3)"), &*Land, 32, 7, 3, Table->FindState(TEXT("short_grass")), false, 7.79, 0.25},
        {TEXT("tall grass (96, ±7, ±3)"), &*Land, 96, 7, 3, TallLower, false, 22.12, 0.40},
        {TEXT("flowers (96, ±6, ±2)"), &*Land, 96, 6, 2, Table->FindState(TEXT("poppy")), false, 27.83, 0.40},
        {TEXT("seagrass (48, ±7, 0)"), &*Sea, 48, 7, 0, Table->FindState(TEXT("seagrass")), true, 41.02, 0.40},
    };
    constexpr int32 Origins = 2000;
    for (const FCase& Case : Cases)
    {
        const FIntVector Origin(8, 8, Case.bWater ? 60 : 64);
        int64 Cells = 0, Uppers = 0;
        for (int32 Index = 0; Index < Origins; ++Index)
        {
            int32 UpperCount = 0;
            Cells += RunPatch(*Table, Survival, UpperOf, *Case.Ground, Origin, Case.Tries, Case.XY, Case.Z, Case.State,
                Case.bWater, VoxelRandom::Hash64(263, VoxelRandom::Salt("patch origin"), Index, 0), &UpperCount);
            Uppers += UpperCount;
        }
        const double Mean = double(Cells) / Origins;
        AddInfo(FString::Printf(TEXT("%s: %.3f distinct cells per origin (analytic %.2f, bound ±%.2f)"), Case.Name,
            Mean, Case.Expected, Case.Bound));
        TestTrue(FString::Printf(TEXT("%s: %.3f is within %.2f of %.2f"), Case.Name, Mean, Case.Bound, Case.Expected),
            FMath::Abs(Mean - Case.Expected) <= Case.Bound);
        if (Case.State == TallLower) TestEqual(TEXT("every tall grass has its upper half"), Uppers, Cells);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPlantColumnsTest, "Unnamed.Voxel.Plants.Columns",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPlantColumnsTest::RunTest(const FString& Parameters)
{
    // block_column's trim: prioritize_tip gives up the base layers first (kelp keeps its top), otherwise the top goes.
    TArray<int32> Kelp = {5, 1};
    VoxelPatchFeatures::TrimColumn(Kelp, 3, true);
    TestTrue(TEXT("kelp in a 3-cell run keeps its tip"), Kelp == TArray<int32>({2, 1}));
    TArray<int32> Cactus = {3, 1};
    VoxelPatchFeatures::TrimColumn(Cactus, 2, false);
    TestTrue(TEXT("a cactus in a 2-cell run loses its flower first"), Cactus == TArray<int32>({2, 0}));
    TArray<int32> Free = {2, 1};
    VoxelPatchFeatures::TrimColumn(Free, 9, false);
    TestTrue(TEXT("a long run trims nothing"), Free == TArray<int32>({2, 1}));
    // biased_to_bottom 2..4 against the analytic distribution (the jar's: 0.6114 / 0.2780 / 0.1107).
    FVoxelIntProvider Biased = FVoxelIntProvider::MakeUniform(2, 4);
    Biased.Type = FVoxelIntProvider::EType::BiasedToBottom;
    TArray<double> Distribution;
    Biased.GetDistribution(Distribution);
    TestTrue(TEXT("biased_to_bottom 2..4 is 11/18, 5/18, 2/18"),
        Distribution.Num() == 3 && FMath::IsNearlyEqual(Distribution[0], 11. / 18, 1e-9) &&
            FMath::IsNearlyEqual(Distribution[1], 5. / 18, 1e-9) &&
            FMath::IsNearlyEqual(Distribution[2], 2. / 18, 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPlantMultifaceTest, "Unnamed.Voxel.Plants.Multiface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPlantMultifaceTest::RunTest(const FString& Parameters)
{
    // Scout I's room probe (9 x 9 x 9 of air in stone, 50 origins each): from the centre, 4 cells from every face,
    // glow lichen never placed; from 1 cell short of the east wall it always placed on the cell next to the wall, on
    // its east face, and 31 of 50 times also on one face-adjacent cell of the same wall (never a diagonal, never more).
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    FVoxelPlantSurvival Survival;
    if (!PlantTestHasBlocks(*this, *Table) || !MakeSurvival(*this, *Table, Survival)) return false;
    const TSharedRef<FVoxelColumn> Rock = MakeFlatColumn(*Table, 100, TEXT("stone"), TEXT("stone"));
    const FVoxelColumn* Around[9];
    for (const FVoxelColumn*& Column : Around)
        Column = &*Rock;
    // glow_lichen as the data has it: walls and ceilings, on stone (the room's only block).
    FVoxelBlockPredicates Predicates;
    FVoxelStateProviders Providers;
    FVoxelBlockSet Stone;
    Stone.Init(Table->GetBlockLimit(), false);
    Stone.Add(Table->Get(Table->FindState(TEXT("stone"))).Block);
    FVoxelPlantFeature Lichen;
    Lichen.Kind = EVoxelPlantFeature::Multiface;
    Lichen.Faces = 0b1111 | 1 << int32(EVoxelFace::Up);
    Lichen.BaseSet = Predicates.AddSet(Stone);
    Lichen.SpreadChance = 0.5f;
    Lichen.SearchSteps = PlantTestMutation == 7 ? 4 : 1;
    const TCHAR* const FaceNames[6] = {
        TEXT("west"), TEXT("east"), TEXT("north"), TEXT("south"), TEXT("down"), TEXT("up")};
    for (int32 Face = 0; Face < 6; ++Face)
    {
        const int32 Dry = Table->MakeState(TEXT("glow_lichen"), {{FName(FaceNames[Face]), FName(TEXT("true"))}});
        if (!TestTrue(FString::Printf(TEXT("glow_lichen has a %s face"), FaceNames[Face]), Dry != INDEX_NONE))
            return false;
        Lichen.FaceStates[Face][0] = uint16(Dry);
        Lichen.FaceStates[Face][1] = uint16(Table->WithValue(uint16(Dry), TEXT("waterlogged"), TEXT("true")));
    }
    TestEqual(TEXT("an east-face lichen holds only its east face"),
        Table->GetValue(Lichen.FaceStates[int32(EVoxelFace::East)][0], TEXT("up")), FName(TEXT("false")));
    const TMap<uint16, uint16> UpperOf;
    // The room: X and Y 4..12, Z 26..34, filled with Fill; the centre is (8, 8, 30) and the east wall at X 13.
    const auto Run = [&](const TCHAR* Fill, FIntVector Origin, int32 Count, TArray<TArray<FVoxelPlantWrite>>& Out)
    {
        FVoxelDecorationContext Context(FIntPoint(0, 0), Around, &*Table);
        const uint16 FillState = Table->FindState(Fill);
        for (int32 Z = 26; Z <= 34; ++Z)
            for (int32 Y = 4; Y <= 12; ++Y)
                for (int32 X = 4; X <= 12; ++X)
                    Context.SetState(X, Y, Z, FillState);
        Out.SetNum(Count);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const auto Emit = [&Out, Index](const FVoxelPlantWrite& Write) { Out[Index].Add(Write); };
            const FVoxelPlantEnvironment Env{*Table, Survival, Predicates, Providers, Context, UpperOf, Emit};
            FVoxelXoroshiro Random(VoxelRandom::Hash64(263, VoxelRandom::Salt("lichen test"), Index, 0));
            VoxelPatchFeatures::PlaceMultiface(Lichen, Origin, Random, Env);
        }
    };
    TArray<TArray<FVoxelPlantWrite>> Centre, Wall, Ceiling, Floor, Wet;
    Run(TEXT("air"), FIntVector(8, 8, 30), 200, Centre);
    int32 Placed = 0;
    for (const TArray<FVoxelPlantWrite>& Writes : Centre)
        Placed += Writes.Num();
    TestEqual(TEXT("from the room's centre nothing places"), Placed, 0);

    constexpr int32 Origins = 2000;
    Run(TEXT("air"), FIntVector(11, 8, 30), Origins, Wall);
    const uint16 East = Lichen.FaceStates[int32(EVoxelFace::East)][0];
    int32 Seconds = 0, Wrong = 0;
    for (const TArray<FVoxelPlantWrite>& Writes : Wall)
    {
        if (Writes.Num() < 1 || Writes.Num() > 2 || Writes[0].Cell != FIntVector(12, 8, 30))
        {
            ++Wrong;
            continue;
        }
        for (const FVoxelPlantWrite& Write : Writes)
        {
            const FIntVector Offset = Write.Cell - FIntVector(12, 8, 30);
            if (Write.State != East || Write.Cell.X != 12 || FMath::Abs(Offset.Y) + FMath::Abs(Offset.Z) > 1 ||
                Write.Mode != EVoxelWriteMode::IfUnchanged || Write.Argument != 0)
                ++Wrong;
        }
        Seconds += Writes.Num() - 1;
    }
    TestEqual(TEXT("beside the wall: one block on the wall-side cell's east face, at most one beside it"), Wrong, 0);
    // The probe's 31 of 50 has a binomial 99% interval of 0.44 to 0.80; ours spreads with chance_of_spreading (0.5).
    const double Rate = double(Seconds) / Origins;
    AddInfo(FString::Printf(TEXT("second blocks: %.3f per origin (probe 0.62 of 50)"), Rate));
    TestTrue(FString::Printf(TEXT("the second-block rate %.3f lies in the probe's interval 0.44 to 0.80"), Rate),
        Rate >= 0.44 && Rate <= 0.80);

    Run(TEXT("air"), FIntVector(8, 8, 33), 100, Ceiling);
    Run(TEXT("air"), FIntVector(8, 8, 27), 100, Floor);
    Run(TEXT("water"), FIntVector(11, 8, 30), 100, Wet);
    int32 OnCeiling = 0, OnFloor = 0, WetOk = 0;
    for (int32 Index = 0; Index < 100; ++Index)
    {
        OnCeiling += !Ceiling[Index].IsEmpty() &&
            Ceiling[Index][0].State == Lichen.FaceStates[int32(EVoxelFace::Up)][0] &&
            Ceiling[Index][0].Cell == FIntVector(8, 8, 34);
        OnFloor += Floor[Index].Num();
        WetOk += !Wet[Index].IsEmpty() && Wet[Index][0].State == Lichen.FaceStates[int32(EVoxelFace::East)][1];
    }
    TestEqual(TEXT("under the ceiling: on the ceiling-side cell's up face"), OnCeiling, 100);
    TestEqual(TEXT("above the floor: nothing (no floor faces)"), OnFloor, 0);
    TestEqual(TEXT("in water: the waterlogged state"), WetOk, 100);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelPlantReplayTest, "Unnamed.Voxel.Plants.Replay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelPlantReplayTest::RunTest(const FString& Parameters)
{
    // Finalize re-checks what a plant write relied on (Nyx's condition 4): real writes from seed 263's overworld
    // replayed alone over their own snapshot, unchanged (they land) and changed under them (they do not): a plant's
    // support removed, its cell taken, a two-cell plant's upper cell taken (both halves refused), a disk's target
    // replaced by stone.
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!PlantTestHasBlocks(*this, *Table)) return true;
    FString Error;
    FVoxelTerrainPreset Preset;
    FVoxelBiomeTable Biomes;
    FVoxelSurfaceInputs Surface;
    FVoxelFeatureGraph Graph;
    if (!TestTrue(FString::Printf(TEXT("the inputs read (%s)"), *Error),
            VoxelTerrainFactory::ReadInputs(
                VoxelWorldgen::GetReferenceRoot(), TEXT("overworld"), Preset, Biomes, Surface, Error) &&
                VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error)))
        return false;
    const TSharedRef<FVoxelDensityTerrain> Terrain =
        MakeShared<FVoxelDensityTerrain>(Preset, 263, *Table, FVoxelDensityTerrainSettings(), &Biomes, &Surface);
    FVoxelDecorator Decorator;
    if (!TestTrue(FString::Printf(TEXT("the decorator compiles (%s)"), *Error),
            Terrain->IsValid() &&
                Decorator.Initialize(Graph, *Table, Terrain->GetBiomeSource()->GetTable(), Terrain->GetSeed(),
                    FVoxelDecorationOptions(), &Error)))
        return false;
    TMap<FIntPoint, TSharedRef<FVoxelColumn>> Snapshots;
    TArray<uint16> Dense;
    const auto Snapshot = [&](FIntPoint Column) -> const FVoxelColumn&
    {
        if (const TSharedRef<FVoxelColumn>* Found = Snapshots.Find(Column)) return **Found;
        TSharedRef<FVoxelColumn> Made = MakeShared<FVoxelColumn>(Column);
        Decorator.GenerateSnapshot(*Terrain, Column, *Made, Dense);
        return *Snapshots.Add(Column, Made);
    };
    // Each kind of write, found in a column's own bucket: (column, writes).
    TOptional<TPair<FIntPoint, TArray<FVoxelWrite>>> Plant, Pair, Disk;
    for (int32 Ring = 0; Ring <= 16 && !(Plant && Pair && Disk); ++Ring)
        for (int32 DY = -Ring; DY <= Ring; ++DY)
            for (int32 DX = -Ring; DX <= Ring; ++DX)
            {
                if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != Ring || (Plant && Pair && Disk)) continue;
                const FIntPoint Column(DX, DY);
                const FVoxelColumn* Around[9];
                for (int32 NY = -1; NY <= 1; ++NY)
                    for (int32 NX = -1; NX <= 1; ++NX)
                        Around[FVoxelWriteLog::GetBucket(NX, NY)] = &Snapshot(Column + FIntPoint(NX, NY));
                FVoxelWriteLog Log;
                Decorator.Decorate(Column, Around, Log);
                const TArray<FVoxelWrite>& Own = Log.Buckets[FVoxelWriteLog::GetBucket(0, 0)].Writes;
                const FVoxelColumn& Mine = Snapshot(Column);
                for (int32 Index = 0; Index < Own.Num(); ++Index)
                {
                    // Only writes that hold on the snapshot alone (not on an earlier write of the same Decorate).
                    const FVoxelWrite& Write = Own[Index];
                    const uint16 Cell = Mine.GetState(Write.GetX(), Write.GetY(), Write.GetZ());
                    const uint16 Below = Mine.GetState(Write.GetX(), Write.GetY(), Write.GetZ() - 1);
                    const bool bOnSnapshot = Write.GetMode() == EVoxelWriteMode::IfUnchanged
                        ? Cell == Write.Argument && Decorator.GetSurvival().SurvivesOn(Write.State, Below, Cell)
                        : Cell != Write.State && Cell != 0;
                    if (!bOnSnapshot) continue;
                    if (!Disk && Write.GetMode() == EVoxelWriteMode::IfReplaceable)
                        Disk.Emplace(Column, TArray<FVoxelWrite>{Write});
                    if (!Plant && Write.GetMode() == EVoxelWriteMode::IfUnchanged &&
                        Write.GetSupport() == EVoxelWriteSupport::Plant)
                        Plant.Emplace(Column, TArray<FVoxelWrite>{Write});
                    // A lower half and, next, its upper half in the cell above.
                    if (!Pair && Write.GetSupport() == EVoxelWriteSupport::PlantPair && Index + 1 < Own.Num() &&
                        Own[Index + 1].GetDenseIndex() == Write.GetDenseIndex() + FVoxelColumn::Area)
                        Pair.Emplace(Column, TArray<FVoxelWrite>{Write, Own[Index + 1]});
                }
            }
    const auto Replay = [&](const TPair<FIntPoint, TArray<FVoxelWrite>>& Found,
                            TFunctionRef<void(FVoxelColumn&)> Change, FVoxelColumn& Out, FVoxelFinalizeStats& Stats)
    {
        FVoxelColumn Changed = Snapshot(Found.Key);
        Change(Changed);
        FVoxelWriteBucket Bucket;
        for (const FVoxelWrite& Write : Found.Value)
            Bucket.Add(FVoxelWriteBucket::MakeKey(9, 0), Write);
        const FVoxelWriteBucket* Incoming[9] = {};
        Incoming[FVoxelWriteLog::GetBucket(0, 0)] = &Bucket;
        Decorator.Finalize(Changed, Incoming, Out, &Stats);
    };
    const uint16 Stone = Table->FindState(TEXT("stone"));
    const auto Check = [&](const TCHAR* Name, const TOptional<TPair<FIntPoint, TArray<FVoxelWrite>>>& Found,
                           FIntVector ChangeAt, uint16 ChangeTo)
    {
        if (!Found)
        {
            AddWarning(FString::Printf(TEXT("%s: no such write within 16 columns of the origin"), Name));
            return;
        }
        const FVoxelWrite& Write = Found->Value[0];
        FVoxelColumn Control, Changed;
        FVoxelFinalizeStats ControlStats, ChangedStats;
        Replay(*Found, [](FVoxelColumn&) {}, Control, ControlStats);
        Replay(
            *Found,
            [&](FVoxelColumn& Column)
            {
                Column.SetState(
                    Write.GetX() + ChangeAt.X, Write.GetY() + ChangeAt.Y, Write.GetZ() + ChangeAt.Z, ChangeTo, *Table);
            },
            Changed, ChangedStats);
        TestEqual(FString::Printf(TEXT("%s: unchanged, the write lands"), Name),
            int32(Control.GetState(Write.GetX(), Write.GetY(), Write.GetZ())), int32(Write.State));
        TestNotEqual(FString::Printf(TEXT("%s: changed, it does not"), Name),
            int32(Changed.GetState(Write.GetX(), Write.GetY(), Write.GetZ())), int32(Write.State));
        TestTrue(FString::Printf(TEXT("%s: the refusal is counted (%d)"), Name, ChangedStats.Refused),
            ChangedStats.Refused >= Found->Value.Num());
        AddInfo(FString::Printf(TEXT("%s: column (%d, %d), cell (%d, %d, %d), state %d"), Name, Found->Key.X,
            Found->Key.Y, Write.GetX(), Write.GetY(), Write.GetZ(), Write.State));
    };
    Check(TEXT("a plant's support removed"), Plant, FIntVector(0, 0, -1), 0);
    Check(TEXT("a plant's cell taken"), Plant, FIntVector(0, 0, 0), Stone);
    Check(TEXT("a two-cell plant's upper cell taken"), Pair, FIntVector(0, 0, 1), Stone);
    Check(TEXT("a disk's target replaced"), Disk, FIntVector(0, 0, 0), Stone);
    return true;
}

#endif
