#include "Blocks/VoxelBlockTable.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Decoration/VoxelBlockPredicates.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Decoration/VoxelRuleTests.h"
#include "Dom/JsonObject.h"
#include "Fluids/VoxelFluidRules.h"
#include "Fluids/VoxelFluidScheduler.h"
#include "Fluids/VoxelFluidTestWorld.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

// Scout M's fluid oracles (Docs/Records/2026-10-10-WaterSky-Scout-M-Fluids.md, section 5), replayed in the core's test
// world. Source/Unnamed/Tests/VoxelFluidOracles.json comes from Scripts/MakeFluidOracles.py. Each scenario is rebuilt
// from its commands, stepped tick by tick, and saved at the probe's own checkpoints, so a cell's "last change" is the
// first checkpoint where it differs from the one before, as make_oracles.py counts it. Pinned: the settled cells and
// the removed cells with those ticks. The five full series are compared and their transient differences reported, not
// pinned (plan Q9 (a)). Probe coordinates (x, y up, z) map to ours as (x, z, y).
namespace
{
    struct FFluidOracle
    {
        FString Name;
        TArray<int32> Checkpoints;
        FIntVector BoxMin, BoxMax;
        TArray<TSharedPtr<FJsonValue>> Setup, Fluids;
        TArray<TPair<int32, TArray<TSharedPtr<FJsonValue>>>> Actions;
        int32 ObservedTo = 0;
        int32 SettledBy = 0;
        // The last checkpoint's cells (non-air, non-stone) with the checkpoint of their last change, and the cells that
        // changed but ended air or stone.
        TMap<FIntVector, TPair<FString, int32>> Cells;
        TMap<FIntVector, int32> Removed;
        // The full series as changes between checkpoints ("" = air or stone again).
        bool bSeries = false;
        TArray<TPair<int32, TMap<FIntVector, FString>>> Series;
    };

    FIntVector FluidTestCell(const TArray<TSharedPtr<FJsonValue>>& Values, int32 First)
    {
        return FIntVector(int32(Values[First]->AsNumber()), int32(Values[First + 2]->AsNumber()),
            int32(Values[First + 1]->AsNumber()));
    }

    const TArray<FFluidOracle>& GetFluidOracles(FString& OutError)
    {
        static TArray<FFluidOracle> Oracles;
        static FString Error;
        static bool bRead = false;
        if (!bRead)
        {
            bRead = true;
            FString Text;
            const FString Path = FPaths::ProjectDir() / TEXT("Source/Unnamed/Tests/VoxelFluidOracles.json");
            TSharedPtr<FJsonObject> Root;
            if (!FFileHelper::LoadFileToString(Text, *Path) ||
                !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
                Error = FString::Printf(TEXT("cannot read %s"), *Path);
            else if (int32(Root->GetNumberField(TEXT("floor_top"))) != 63)
                Error = TEXT("the probe's floor is not at y 63, the test world's");
            else
            {
                const TSharedPtr<FJsonObject> Checkpoints = Root->GetObjectField(TEXT("checkpoints"));
                for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("scenarios")))
                {
                    const TSharedPtr<FJsonObject> Object = Value->AsObject();
                    FFluidOracle& Oracle = Oracles.AddDefaulted_GetRef();
                    Oracle.Name = Object->GetStringField(TEXT("name"));
                    Oracle.ObservedTo = int32(Object->GetNumberField(TEXT("observed_to")));
                    Oracle.SettledBy = int32(Object->GetNumberField(TEXT("settled_by")));
                    for (const TSharedPtr<FJsonValue>& Tick :
                        Checkpoints->GetArrayField(Object->GetStringField(TEXT("run"))))
                        if (int32(Tick->AsNumber()) <= Oracle.ObservedTo)
                            Oracle.Checkpoints.Add(int32(Tick->AsNumber()));
                    if (Oracle.Checkpoints.IsEmpty() || Oracle.Checkpoints.Last() != Oracle.ObservedTo)
                        Error = FString::Printf(
                            TEXT("%s: the checkpoints stop before t=%d"), *Oracle.Name, Oracle.ObservedTo);
                    const TArray<TSharedPtr<FJsonValue>>& Box = Object->GetArrayField(TEXT("box"));
                    const auto Range = [&Box](int32 Axis, int32 End)
                    { return int32(Box[Axis]->AsArray()[End]->AsNumber()); };
                    Oracle.BoxMin = FIntVector(Range(0, 0), Range(2, 0), Range(1, 0));
                    Oracle.BoxMax = FIntVector(Range(0, 1), Range(2, 1), Range(1, 1));
                    Oracle.Setup = Object->GetArrayField(TEXT("setup"));
                    Oracle.Fluids = Object->GetArrayField(TEXT("fluids"));
                    for (const TSharedPtr<FJsonValue>& Action : Object->GetArrayField(TEXT("actions")))
                        Oracle.Actions.Emplace(
                            int32(Action->AsArray()[0]->AsNumber()), Action->AsArray()[1]->AsArray());
                    for (const TSharedPtr<FJsonValue>& Cell : Object->GetArrayField(TEXT("cells")))
                    {
                        const TArray<TSharedPtr<FJsonValue>>& Fields = Cell->AsArray();
                        Oracle.Cells.Add(
                            FluidTestCell(Fields, 0), {Fields[3]->AsString(), int32(Fields[4]->AsNumber())});
                    }
                    for (const TSharedPtr<FJsonValue>& Cell : Object->GetArrayField(TEXT("removed")))
                        Oracle.Removed.Add(FluidTestCell(Cell->AsArray(), 0), int32(Cell->AsArray()[3]->AsNumber()));
                    const TArray<TSharedPtr<FJsonValue>>* Series;
                    if (Object->TryGetArrayField(TEXT("series"), Series))
                    {
                        Oracle.bSeries = true;
                        for (const TSharedPtr<FJsonValue>& Step : *Series)
                        {
                            TMap<FIntVector, FString> Changes;
                            for (const TSharedPtr<FJsonValue>& Change : Step->AsArray()[1]->AsArray())
                                Changes.Add(FluidTestCell(Change->AsArray(), 0), Change->AsArray()[3]->AsString());
                            Oracle.Series.Emplace(int32(Step->AsArray()[0]->AsNumber()), MoveTemp(Changes));
                        }
                    }
                }
            }
        }
        OutError = Error;
        return Oracles;
    }

    const FFluidOracle* FindFluidOracle(const TArray<FFluidOracle>& Oracles, FStringView Name)
    {
        return Oracles.FindByPredicate([Name](const FFluidOracle& Oracle) { return Oracle.Name == Name; });
    }

    constexpr uint8 FluidTestSlabBottom = 1 << int32(EVoxelFace::Down);
    constexpr uint8 FluidTestSlabTop = 1 << int32(EVoxelFace::Up);

    // The probe's blocks as the rules see them.
    bool MakeFluidTestCell(const FString& Name, FVoxelFluidCell& Out)
    {
        Out = FVoxelFluidCell();
        if (Name == TEXT("air")) return true;
        if (Name == TEXT("stone"))
            Out = FVoxelFluidCell::MakeSolid();
        else if (Name == TEXT("soul_soil"))
            Out = FVoxelFluidCell::MakeSolid(EVoxelFluidBlock::SoulSoil);
        else if (Name == TEXT("blue_ice"))
            Out = FVoxelFluidCell::MakeSolid(EVoxelFluidBlock::BlueIce);
        else if (Name == TEXT("water"))
            Out = FVoxelFluidCell::MakeFluid(EVoxelFluid::Water, 0);
        else if (Name == TEXT("lava"))
            Out = FVoxelFluidCell::MakeFluid(EVoxelFluid::Lava, 0);
        else if (Name == TEXT("poppy") || Name == TEXT("short_grass") || Name == TEXT("torch") ||
            Name == TEXT("oak_sapling"))
            Out.bWashedAway = true;
        else if (Name.StartsWith(TEXT("stone_slab[")))
        {
            Out.bSolid = Out.bWaterloggable = true;
            Out.Walls = Name.Contains(TEXT("type=top")) ? FluidTestSlabTop : FluidTestSlabBottom;
            if (Name.Contains(TEXT("waterlogged=true"))) Out.Fluid = EVoxelFluid::Water;
        }
        else
            return false;
        return true;
    }

    // A cell as the probe saved it (default properties left out); "" for air and stone, which it skipped.
    FString GetFluidTestName(const FVoxelFluidCell& Cell, const FString* Plant)
    {
        if (Cell.bWaterloggable)
        {
            const bool bTop = (Cell.Walls & FluidTestSlabTop) != 0, bWet = Cell.Fluid == EVoxelFluid::Water;
            if (!bTop && !bWet) return TEXT("stone_slab");
            return FString::Printf(TEXT("stone_slab[type=%s,waterlogged=%s]"), bTop ? TEXT("top") : TEXT("bottom"),
                bWet ? TEXT("true") : TEXT("false"));
        }
        if (Cell.Fluid != EVoxelFluid::None)
        {
            const TCHAR* Fluid = Cell.Fluid == EVoxelFluid::Water ? TEXT("water") : TEXT("lava");
            return Cell.Level == 0 ? FString(Fluid) : FString::Printf(TEXT("%s[level=%d]"), Fluid, Cell.Level);
        }
        if (Cell.bWashedAway) return Plant ? *Plant : FString(TEXT("plant"));
        switch (Cell.Block)
        {
        case EVoxelFluidBlock::SoulSoil:
            return TEXT("soul_soil");
        case EVoxelFluidBlock::BlueIce:
            return TEXT("blue_ice");
        case EVoxelFluidBlock::Obsidian:
            return TEXT("obsidian");
        case EVoxelFluidBlock::Cobblestone:
            return TEXT("cobblestone");
        case EVoxelFluidBlock::Basalt:
            return TEXT("basalt");
        default:
            return FString();
        }
    }

    // A replay: the box's cells at each checkpoint.
    struct FFluidReplay
    {
        TArray<TMap<FIntVector, FString>> Snapshots;
        int64 Acts = 0;
        FString Error;
    };

    FFluidReplay ReplayFluidOracle(const FFluidOracle& Oracle, const FVoxelFluidOptions& Options)
    {
        FFluidReplay Replay;
        FVoxelFluidTestWorld World(63, Options);
        TMap<FIntVector, FString> Plants;
        const auto Run = [&](const TArray<TSharedPtr<FJsonValue>>& Commands)
        {
            for (const TSharedPtr<FJsonValue>& Command : Commands)
            {
                const TArray<TSharedPtr<FJsonValue>>& Fields = Command->AsArray();
                const bool bFill = Fields[0]->AsString() == TEXT("fill");
                const FString Name = Fields.Last()->AsString();
                FVoxelFluidCell Cell;
                if (!MakeFluidTestCell(Name, Cell))
                {
                    Replay.Error = FString::Printf(TEXT("unknown block %s"), *Name);
                    continue;
                }
                const FIntVector From = FluidTestCell(Fields, 1), To = bFill ? FluidTestCell(Fields, 4) : From;
                // /fill's order: x fastest, then height, then z.
                for (int32 Y = FMath::Min(From.Y, To.Y); Y <= FMath::Max(From.Y, To.Y); ++Y)
                    for (int32 Z = FMath::Min(From.Z, To.Z); Z <= FMath::Max(From.Z, To.Z); ++Z)
                        for (int32 X = FMath::Min(From.X, To.X); X <= FMath::Max(From.X, To.X); ++X)
                        {
                            if (Cell.bWashedAway) Plants.Add(FIntVector(X, Y, Z), Name);
                            World.Place(FIntVector(X, Y, Z), Cell);
                        }
            }
        };
        Run(Oracle.Setup);
        Run(Oracle.Fluids);
        for (const int32 Checkpoint : Oracle.Checkpoints)
        {
            while (World.GetTick() < uint64(Checkpoint))
                World.Step();
            TMap<FIntVector, FString>& Snapshot = Replay.Snapshots.AddDefaulted_GetRef();
            for (int32 Z = Oracle.BoxMin.Z; Z <= Oracle.BoxMax.Z; ++Z)
                for (int32 Y = Oracle.BoxMin.Y; Y <= Oracle.BoxMax.Y; ++Y)
                    for (int32 X = Oracle.BoxMin.X; X <= Oracle.BoxMax.X; ++X)
                    {
                        const FIntVector At(X, Y, Z);
                        FString Name = GetFluidTestName(World.Get(At), Plants.Find(At));
                        if (!Name.IsEmpty()) Snapshot.Add(At, MoveTemp(Name));
                    }
            for (const TPair<int32, TArray<TSharedPtr<FJsonValue>>>& Action : Oracle.Actions)
                if (Action.Key == Checkpoint) Run(Action.Value);
        }
        Replay.Acts = World.GetActCount();
        return Replay;
    }

    FString FormatFluidCell(const FIntVector& At)
    {
        return FString::Printf(TEXT("(%d, %d, %d)"), At.X, At.Z, At.Y);
    }

    FString JoinFluidDifferences(TArray<FString>& Differences)
    {
        Differences.Sort();
        const int32 Count = Differences.Num();
        if (Count > 6) Differences.SetNum(6);
        return Count ? FString::Printf(TEXT("%d differences: %s"), Count, *FString::Join(Differences, TEXT("; ")))
                     : FString();
    }

    // Compares a replay's settled and removed cells, with their ticks, and the tick it settled by with the oracle's, as
    // make_oracles.py counts them. Empty when they match; else the first differences. In a drain (a source removed),
    // the tick each cell empties at follows the order of acts within a tick, a transient: differences of up to two
    // water waves (10 ticks) go to OutTransients, not the result (plan Q9 (a)).
    FString CompareFluidOracle(const FFluidOracle& Oracle, const FFluidReplay& Replay, FString* OutTransients = nullptr)
    {
        if (!Replay.Error.IsEmpty()) return Replay.Error;
        TMap<FIntVector, int32> Last;
        for (int32 Index = 1; Index < Replay.Snapshots.Num(); ++Index)
        {
            const TMap<FIntVector, FString>&Before = Replay.Snapshots[Index - 1], &Now = Replay.Snapshots[Index];
            TSet<FIntVector> Keys;
            Before.GetKeys(Keys);
            for (const TPair<FIntVector, FString>& Cell : Now)
                Keys.Add(Cell.Key);
            for (const FIntVector& Key : Keys)
            {
                const FString* A = Before.Find(Key);
                const FString* B = Now.Find(Key);
                if ((A == nullptr) != (B == nullptr) || (A && *A != *B)) Last.Add(Key, Oracle.Checkpoints[Index]);
            }
        }
        TArray<FString> Differences, Transients;
        const bool bDrain = Oracle.Name.Contains(TEXT("drain"));
        int32 SettledBy = 0;
        for (const TPair<FIntVector, int32>& Cell : Last)
            SettledBy = FMath::Max(SettledBy, Cell.Value);
        if (SettledBy != Oracle.SettledBy)
            Differences.Add(FString::Printf(TEXT("settled by t=%d, the probe by t=%d"), SettledBy, Oracle.SettledBy));
        const TMap<FIntVector, FString>& Final = Replay.Snapshots.Last();
        for (const TPair<FIntVector, FString>& Cell : Final)
        {
            const TPair<FString, int32>* Expected = Oracle.Cells.Find(Cell.Key);
            const int32 Tick = Last.FindRef(Cell.Key);
            if (!Expected || Expected->Key != Cell.Value || Expected->Value != Tick)
                Differences.Add(FString::Printf(TEXT("%s is %s at t=%d, the probe has %s"), *FormatFluidCell(Cell.Key),
                    *Cell.Value, Tick,
                    Expected ? *FString::Printf(TEXT("%s at t=%d"), *Expected->Key, Expected->Value)
                             : TEXT("nothing")));
        }
        for (const TPair<FIntVector, TPair<FString, int32>>& Cell : Oracle.Cells)
            if (!Final.Contains(Cell.Key))
                Differences.Add(FString::Printf(TEXT("%s is empty, the probe has %s at t=%d"),
                    *FormatFluidCell(Cell.Key), *Cell.Value.Key, Cell.Value.Value));
        for (const TPair<FIntVector, int32>& Cell : Last)
        {
            const int32 Expected = Oracle.Removed.FindRef(Cell.Key, -1);
            if (Final.Contains(Cell.Key) || Expected == Cell.Value) continue;
            (bDrain && Expected >= 0 && FMath::Abs(Expected - Cell.Value) <= 10 ? Transients : Differences)
                .Add(FString::Printf(
                    TEXT("%s emptied at t=%d, the probe at t=%d"), *FormatFluidCell(Cell.Key), Cell.Value, Expected));
        }
        for (const TPair<FIntVector, int32>& Cell : Oracle.Removed)
            if (!Last.Contains(Cell.Key) || Final.Contains(Cell.Key))
                Differences.Add(FString::Printf(
                    TEXT("%s never emptied, the probe emptied it at t=%d"), *FormatFluidCell(Cell.Key), Cell.Value));
        if (OutTransients) *OutTransients = JoinFluidDifferences(Transients);
        return JoinFluidDifferences(Differences);
    }

    bool RunFluidOracleGroup(FAutomationTestBase& Test, const TCHAR* Prefix, int32 ExpectedCount)
    {
        FString Error;
        const TArray<FFluidOracle>& Oracles = GetFluidOracles(Error);
        if (!Test.TestTrue(FString::Printf(TEXT("the oracle data reads (%s)"), *Error), Error.IsEmpty())) return false;
        int32 Count = 0, Passed = 0;
        for (const FFluidOracle& Oracle : Oracles)
        {
            if (!Oracle.Name.StartsWith(Prefix)) continue;
            ++Count;
            const FFluidReplay Replay = ReplayFluidOracle(Oracle, FVoxelFluidOptions());
            FString Transients;
            const FString Differences = CompareFluidOracle(Oracle, Replay, &Transients);
            Passed += Differences.IsEmpty();
            Test.TestTrue(
                FString::Printf(TEXT("%s: %s"), *Oracle.Name, Differences.IsEmpty() ? TEXT("matches") : *Differences),
                Differences.IsEmpty());
            if (!Transients.IsEmpty())
                Test.AddInfo(
                    FString::Printf(TEXT("%s, transients (reported, not pinned): %s"), *Oracle.Name, *Transients));
        }
        Test.AddInfo(FString::Printf(TEXT("%s*: %d of %d oracles match"), Prefix, Passed, Count));
        return Test.TestEqual(FString::Printf(TEXT("%s* oracles in the data"), Prefix), Count, ExpectedCount);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidWaterTest, "Unnamed.Voxel.Fluid.Water",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidWaterTest::RunTest(const FString& Parameters)
{
    // Spread, reach, drops, new sources, falling, merging, draining, slabs and washed-away plants (scout M 4.1-4.4).
    return RunFluidOracleGroup(*this, TEXT("w_"), 25);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidLavaTest, "Unnamed.Voxel.Fluid.Lava",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidLavaTest::RunTest(const FString& Parameters)
{
    // Overworld lava: every 30 ticks, reach 3, drops 3 away, no randomness (four copies of each case).
    return RunFluidOracleGroup(*this, TEXT("l_"), 16);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidLavaWaterTest, "Unnamed.Voxel.Fluid.LavaWater",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidLavaWaterTest::RunTest(const FString& Parameters)
{
    // Lava meeting water: obsidian, cobblestone, stone, basalt, and lava over water left alone (scout M 4.5).
    return RunFluidOracleGroup(*this, TEXT("lw_"), 10);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidSeriesTest, "Unnamed.Voxel.Fluid.Series",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidSeriesTest::RunTest(const FString& Parameters)
{
    // The five full series, checkpoint by checkpoint. Differences are transients (the drain's order within a tick) and
    // are reported, not pinned (plan Q9 (a)); the settled state is pinned by the group tests. Pinned here: a
    // waterlogged slab spreads exactly as a source (w_slab equals w_plane outside the centre at every checkpoint).
    FString Error;
    const TArray<FFluidOracle>& Oracles = GetFluidOracles(Error);
    if (!TestTrue(FString::Printf(TEXT("the oracle data reads (%s)"), *Error), Error.IsEmpty())) return false;
    TMap<FString, FFluidReplay> Replays;
    int32 Series = 0;
    for (const FFluidOracle& Oracle : Oracles)
    {
        if (!Oracle.bSeries) continue;
        ++Series;
        const FFluidReplay& Replay = Replays.Add(Oracle.Name, ReplayFluidOracle(Oracle, FVoxelFluidOptions()));
        TMap<FIntVector, FString> Expected;
        int32 Change = 0, Differing = 0;
        FString First;
        for (int32 Index = 0; Index < Oracle.Checkpoints.Num(); ++Index)
        {
            for (; Change < Oracle.Series.Num() && Oracle.Series[Change].Key <= Oracle.Checkpoints[Index]; ++Change)
                for (const TPair<FIntVector, FString>& Cell : Oracle.Series[Change].Value)
                    if (Cell.Value.IsEmpty())
                        Expected.Remove(Cell.Key);
                    else
                        Expected.Add(Cell.Key, Cell.Value);
            const TMap<FIntVector, FString>& Got = Replay.Snapshots[Index];
            int32 Cells = 0;
            for (const TPair<FIntVector, FString>& Cell : Expected)
                Cells += Got.FindRef(Cell.Key) != Cell.Value;
            for (const TPair<FIntVector, FString>& Cell : Got)
                Cells += !Expected.Contains(Cell.Key);
            if (!Cells) continue;
            ++Differing;
            if (First.IsEmpty())
                First = FString::Printf(TEXT("first at t=%d (%d cells)"), Oracle.Checkpoints[Index], Cells);
        }
        AddInfo(FString::Printf(TEXT("%s: %d of %d checkpoints equal the probe%s%s"), *Oracle.Name,
            Oracle.Checkpoints.Num() - Differing, Oracle.Checkpoints.Num(), First.IsEmpty() ? TEXT("") : TEXT("; "),
            *First));
    }
    TestEqual(TEXT("series in the data"), Series, 5);
    const FFluidReplay* Plane = Replays.Find(TEXT("w_plane"));
    const FFluidReplay* Slab = Replays.Find(TEXT("w_slab"));
    if (!TestTrue(TEXT("w_plane and w_slab replayed"), Plane && Slab)) return false;
    bool bSame = Plane->Snapshots.Num() == Slab->Snapshots.Num();
    for (int32 Index = 0; bSame && Index < Plane->Snapshots.Num(); ++Index)
    {
        TMap<FIntVector, FString> A = Plane->Snapshots[Index], B = Slab->Snapshots[Index];
        A.Remove(FIntVector(0, 0, 64));
        B.Remove(FIntVector(0, 0, 64));
        bSame = A.OrderIndependentCompareEqual(B);
    }
    return TestTrue(TEXT("w_slab equals w_plane outside the centre at every checkpoint"), bSame);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidDeterminismTest, "Unnamed.Voxel.Fluid.Determinism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidDeterminismTest::RunTest(const FString& Parameters)
{
    // The same edits at the same ticks give the same world: every scenario twice, every checkpoint equal.
    FString Error;
    const TArray<FFluidOracle>& Oracles = GetFluidOracles(Error);
    if (!TestTrue(FString::Printf(TEXT("the oracle data reads (%s)"), *Error), Error.IsEmpty())) return false;
    for (const FFluidOracle& Oracle : Oracles)
    {
        const FFluidReplay A = ReplayFluidOracle(Oracle, FVoxelFluidOptions());
        const FFluidReplay B = ReplayFluidOracle(Oracle, FVoxelFluidOptions());
        bool bSame = A.Acts == B.Acts && A.Snapshots.Num() == B.Snapshots.Num();
        for (int32 Index = 0; bSame && Index < A.Snapshots.Num(); ++Index)
            bSame = A.Snapshots[Index].OrderIndependentCompareEqual(B.Snapshots[Index]);
        TestTrue(FString::Printf(TEXT("%s replays identically (%lld acts)"), *Oracle.Name, A.Acts), bSame);
    }
    // Water, Lava and LavaWater compare 25, 16 and 10 of them: nothing else may sit in the data.
    return TestEqual(TEXT("scenarios in the data"), Oracles.Num(), 51);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidMutationsTest, "Unnamed.Voxel.Fluid.Mutations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidMutationsTest::RunTest(const FString& Parameters)
{
    // Scout M's mutations that must fail (section 5): each one breaks the oracle named beside it. FIFO against LIFO is
    // not among them: same-tick order only moves transients, which are not pinned (plan Q9 (a)).
    FString Error;
    const TArray<FFluidOracle>& Oracles = GetFluidOracles(Error);
    if (!TestTrue(FString::Printf(TEXT("the oracle data reads (%s)"), *Error), Error.IsEmpty())) return false;
    struct FCase
    {
        EVoxelFluidMutation Mutation;
        const TCHAR* Name;
        const TCHAR* Oracle;
    };
    const FCase Cases[] = {
        {EVoxelFluidMutation::WaterDelay4, TEXT("spread delay 4"), TEXT("w_plane")},
        {EVoxelFluidMutation::WaterDelay6, TEXT("spread delay 6"), TEXT("w_plane")},
        {EVoxelFluidMutation::WaterDropOff2, TEXT("water drop-off 2"), TEXT("w_plane")},
        {EVoxelFluidMutation::WaterDropOff2, TEXT("water drop-off 2"), TEXT("w_channel")},
        {EVoxelFluidMutation::WaterSearch4, TEXT("water drop search 4"), TEXT("w_hole5")},
        {EVoxelFluidMutation::WaterSearch6, TEXT("water drop search 6"), TEXT("w_hole6")},
        {EVoxelFluidMutation::LavaSearch4, TEXT("lava drop search 4"), TEXT("l_hole4")},
        {EVoxelFluidMutation::LavaDropOff1, TEXT("lava drop-off 1"), TEXT("l_plane0")},
        {EVoxelFluidMutation::NoFloorCheck, TEXT("new sources need no floor"), TEXT("w_infinite_air")},
        {EVoxelFluidMutation::SourceAboveCounts, TEXT("a source above counts with one beside"),
            TEXT("w_above_beside2")},
        {EVoxelFluidMutation::SlabNotFloor, TEXT("a bottom slab is no floor"), TEXT("w_infinite_slab")},
        {EVoxelFluidMutation::TargetFromSpreader, TEXT("targets take my amount less the drop-off"), TEXT("w_infinite")},
        {EVoxelFluidMutation::FallingSpreads, TEXT("flowing water over a drop spreads sideways"), TEXT("w_cliff")},
        {EVoxelFluidMutation::ReactBelow, TEXT("lava meets water below it"), TEXT("lw_lava_over_water")},
        {EVoxelFluidMutation::FlowingWaterlogs, TEXT("flowing water waterlogs slabs"), TEXT("w_slab_wall")},
        {EVoxelFluidMutation::NoPlacedReaction, TEXT("placed lava doesn't meet water at once"),
            TEXT("lw_place_beside")},
    };
    TSet<EVoxelFluidMutation> Covered;
    for (const FCase& Case : Cases)
    {
        const FFluidOracle* Oracle = FindFluidOracle(Oracles, Case.Oracle);
        if (!TestNotNull(FString::Printf(TEXT("oracle %s"), Case.Oracle), Oracle)) continue;
        FVoxelFluidOptions Options;
        Options.Mutation = Case.Mutation;
        const FFluidReplay Replay = ReplayFluidOracle(*Oracle, Options);
        if (!TestTrue(FString::Printf(TEXT("%s replays (%s)"), Case.Oracle, *Replay.Error), Replay.Error.IsEmpty()))
            continue;
        const FString Differences = CompareFluidOracle(*Oracle, Replay);
        Covered.Add(Case.Mutation);
        if (TestFalse(FString::Printf(TEXT("%s fails %s"), Case.Name, Case.Oracle), Differences.IsEmpty()))
            AddInfo(FString::Printf(TEXT("%s fails %s: %s"), Case.Name, Case.Oracle, *Differences));
    }
    return TestEqual(TEXT("every mutation is checked"), Covered.Num(), int32(EVoxelFluidMutation::Count) - 1);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidSchedulerTest, "Unnamed.Voxel.Fluid.Scheduler",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidSchedulerTest::RunTest(const FString& Parameters)
{
    // Acts come out by due tick, then in the order they were scheduled; one pending act per cell and fluid.
    FVoxelFluidScheduler Scheduler;
    const FIntVector A(1, 2, 3), B(4, 5, 6), C(-7, 8, -9);
    TestTrue(TEXT("A water at 10"), Scheduler.Schedule(A, EVoxelFluid::Water, 10));
    TestTrue(TEXT("B water at 5"), Scheduler.Schedule(B, EVoxelFluid::Water, 5));
    TestTrue(TEXT("C water at 10"), Scheduler.Schedule(C, EVoxelFluid::Water, 10));
    TestFalse(TEXT("A water again at 7 is dropped"), Scheduler.Schedule(A, EVoxelFluid::Water, 7));
    TestTrue(TEXT("A lava at 10 is its own act"), Scheduler.Schedule(A, EVoxelFluid::Lava, 10));
    TestFalse(TEXT("nothing for no fluid"), Scheduler.Schedule(A, EVoxelFluid::None, 10));
    TestEqual(TEXT("four pending"), Scheduler.Num(), 4);
    FVoxelFluidScheduler::FAct Act;
    TestFalse(TEXT("nothing due at 4"), Scheduler.PopDue(4, Act));
    TestTrue(TEXT("B due at 5"), Scheduler.PopDue(5, Act) && Act.Cell == B);
    TestFalse(TEXT("nothing more at 5"), Scheduler.PopDue(5, Act));
    TArray<FIntVector> Order;
    TArray<EVoxelFluid> Fluids;
    while (Scheduler.PopDue(12, Act))
    {
        Order.Add(Act.Cell);
        Fluids.Add(Act.Fluid);
    }
    TestTrue(TEXT("same tick in the order scheduled: A water, C water, A lava"),
        Order == TArray<FIntVector>{A, C, A} &&
            Fluids == TArray<EVoxelFluid>{EVoxelFluid::Water, EVoxelFluid::Water, EVoxelFluid::Lava});
    TestFalse(TEXT("A no longer pending"), Scheduler.IsPending(A, EVoxelFluid::Water));
    TestTrue(TEXT("A can be scheduled again"), Scheduler.Schedule(A, EVoxelFluid::Water, 20));
    Scheduler.Reset();
    return TestEqual(TEXT("empty after Reset"), Scheduler.Num(), 0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidParkingTest, "Unnamed.Voxel.Fluid.Parking",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidParkingTest::RunTest(const FString& Parameters)
{
    // Streaming's parking (W2a): a parked act keeps the delay it had left and stays pending for its cell; Unpark
    // re-queues it at the unpark tick plus that delay, first scheduled first; outside the ring only the acts waiting
    // for a column to be known leave; ParkAsleep takes exactly the queued acts of the columns it names; Reset forgets
    // them.
    using FAct = FVoxelFluidScheduler::FAct;
    const FIntPoint Home(0, 0), East(1, 0), West(-1, 0);
    FAct Act;
    uint64 Due = 0;
    {
        // 6 left of an act due at 10 parked at 4; none of one due at 5 parked at 8.
        FVoxelFluidScheduler Scheduler;
        const FIntVector Early(1, 1, 64), Late(2, 1, 64);
        Scheduler.Park(Home, FAct{10, 0, Early, EVoxelFluid::Water}, 4, false);
        Scheduler.Park(East, FAct{5, 1, Late, EVoxelFluid::Water}, 8, false);
        TestTrue(TEXT("two parked, none queued"), Scheduler.GetParkedCount() == 2 && Scheduler.Num() == 0);
        TestFalse(TEXT("a parked cell stays pending: a second act is dropped"),
            Scheduler.Schedule(Early, EVoxelFluid::Water, 50));
        TestTrue(TEXT("IsPending for a parked cell"), Scheduler.IsPending(Early, EVoxelFluid::Water));
        TestEqual(TEXT("Unpark re-queues the act parked on its column"), Scheduler.Unpark(Home, 20, true), 1);
        TestTrue(TEXT("due at 20 plus the 6 left"), Scheduler.PeekDue(Due) && Due == 26);
        TestFalse(TEXT("not due at 25"), Scheduler.PopDue(25, Act));
        TestTrue(TEXT("due at 26"), Scheduler.PopDue(26, Act) && Act.Cell == Early);
        TestEqual(TEXT("the overdue act leaves its column"), Scheduler.Unpark(East, 30, true), 1);
        TestTrue(TEXT("an overdue act is due at the unpark tick"), Scheduler.PeekDue(Due) && Due == 30);
        TestEqual(TEXT("nothing parked on a column with none"), Scheduler.Unpark(West, 30, true), 0);
        TestEqual(TEXT("nothing left parked"), Scheduler.GetParkedCount(), 0);
    }
    {
        // First in, first out among the acts unparked to one due tick, whatever order they were parked in.
        FVoxelFluidScheduler Scheduler;
        const FIntVector X(1, 1, 64), Y(2, 1, 64), Z(3, 1, 64);
        for (const FIntVector& Cell : {X, Y, Z})
            Scheduler.Schedule(Cell, EVoxelFluid::Water, 10);
        FAct Popped[3];
        for (FAct& Each : Popped)
            Scheduler.PopDue(10, Each);
        Scheduler.Park(Home, Popped[2], 10, false);
        Scheduler.Park(Home, Popped[0], 10, false);
        Scheduler.Park(Home, Popped[1], 10, false);
        Scheduler.Unpark(Home, 20, true);
        TArray<FIntVector> Order;
        while (Scheduler.PopDue(20, Act))
            Order.Add(Act.Cell);
        TestTrue(TEXT("re-queued in the order first scheduled: X, Y, Z"), Order == TArray<FIntVector>{X, Y, Z});
        TestFalse(TEXT("X is no longer pending once its act ran"), Scheduler.IsPending(X, EVoxelFluid::Water));
    }
    {
        // The same across columns when they are unparked together: the acts interleave as first scheduled.
        FVoxelFluidScheduler Scheduler;
        const FIntVector A(1, 1, 64), B(17, 1, 64), C(2, 1, 64), D(18, 1, 64);
        for (const FIntVector& Cell : {A, B, C, D})
            Scheduler.Schedule(Cell, EVoxelFluid::Water, 10);
        FAct Popped[4];
        for (FAct& Each : Popped)
            Scheduler.PopDue(10, Each);
        for (const int32 Index : {3, 1, 2, 0})
            Scheduler.Park(FVoxelFluidScheduler::GetColumn(Popped[Index].Cell), Popped[Index], 10, true);
        TestEqual(TEXT("both columns unpark together"), Scheduler.Unpark(20, [](FIntPoint) { return 2; }), 4);
        TArray<FIntVector> Order;
        while (Scheduler.PopDue(20, Act))
            Order.Add(Act.Cell);
        TestTrue(TEXT("re-queued across columns in the order first scheduled: A, B, C, D"),
            Order == TArray<FIntVector>{A, B, C, D});
    }
    {
        // Out of the ring only the act waiting for the column to be known leaves; the asleep one waits for the ring.
        FVoxelFluidScheduler Scheduler;
        const FIntVector Sleeper(1, 1, 64), Waiter(2, 1, 64);
        Scheduler.Park(Home, FAct{10, 0, Sleeper, EVoxelFluid::Water}, 10, true);
        Scheduler.Park(Home, FAct{12, 1, Waiter, EVoxelFluid::Lava}, 10, false);
        TestEqual(TEXT("out of the ring, one act leaves"), Scheduler.Unpark(Home, 15, false), 1);
        TestFalse(TEXT("not due at 16"), Scheduler.PopDue(16, Act));
        TestTrue(TEXT("the waiting lava act, due at 17"),
            Scheduler.PopDue(17, Act) && Act.Cell == Waiter && Act.Fluid == EVoxelFluid::Lava);
        TestEqual(TEXT("the asleep act stays parked"), Scheduler.GetParkedCount(), 1);
        TestTrue(TEXT("and pending"), Scheduler.IsPending(Sleeper, EVoxelFluid::Water));
        TestEqual(TEXT("back in the ring it leaves too"), Scheduler.Unpark(Home, 20, true), 1);
        TestTrue(TEXT("due at 20, with no delay left"), Scheduler.PopDue(20, Act) && Act.Cell == Sleeper);
        TArray<FIntPoint> Columns;
        Scheduler.GetParkedColumns(Columns);
        TestEqual(TEXT("no column holds parked acts"), Columns.Num(), 0);
    }
    {
        // ParkAsleep: exactly the queued acts of the columns the predicate names, with the delays they had left.
        FVoxelFluidScheduler Scheduler;
        const FIntVector AtHome(5, 5, 64), AtEast(20, 5, 64), AtWest(-1, 5, 64), AtWestToo(-16, 15, 70),
            AtNorth(5, 16, 64);
        Scheduler.Schedule(AtHome, EVoxelFluid::Water, 12);
        Scheduler.Schedule(AtEast, EVoxelFluid::Water, 15);
        Scheduler.Schedule(AtWest, EVoxelFluid::Lava, 40);
        Scheduler.Schedule(AtWestToo, EVoxelFluid::Water, 11);
        Scheduler.Schedule(AtNorth, EVoxelFluid::Water, 13);
        const int32 Moved =
            Scheduler.ParkAsleep(10, [&](FIntPoint Column) { return Column == East || Column == West; });
        TestEqual(TEXT("the three acts of the east and west columns park"), Moved, 3);
        TestTrue(TEXT("two queued, three parked"), Scheduler.Num() == 2 && Scheduler.GetParkedCount() == 3);
        TArray<FIntPoint> Columns;
        Scheduler.GetParkedColumns(Columns);
        TestTrue(TEXT("parked on the east and west columns"),
            Columns.Num() == 2 && Columns.Contains(East) && Columns.Contains(West));
        TestFalse(TEXT("a parked cell stays pending"), Scheduler.Schedule(AtWest, EVoxelFluid::Lava, 99));
        TArray<FIntVector> Queued;
        while (Scheduler.PopDue(100, Act))
            Queued.Add(Act.Cell);
        TestTrue(TEXT("the others stay queued"), Queued == TArray<FIntVector>{AtHome, AtNorth});
        TestEqual(TEXT("asleep acts stay out of the ring"), Scheduler.Unpark(West, 100, false), 0);
        TestEqual(TEXT("the east act leaves"), Scheduler.Unpark(East, 50, true), 1);
        TestTrue(TEXT("due at 50 plus the 5 left"), Scheduler.PeekDue(Due) && Due == 55);
        TestTrue(TEXT("popped at 55"), Scheduler.PopDue(55, Act) && Act.Cell == AtEast);
        TestEqual(TEXT("the west acts leave"), Scheduler.Unpark(West, 100, true), 2);
        TestTrue(TEXT("due at 101 (1 left) and 130 (30 left)"),
            Scheduler.PopDue(101, Act) && Act.Cell == AtWestToo && !Scheduler.PopDue(129, Act) &&
                Scheduler.PopDue(130, Act) && Act.Cell == AtWest);
    }
    {
        // Reset forgets the parked acts and their cells' pending marks.
        FVoxelFluidScheduler Scheduler;
        const FIntVector Cell(1, 1, 64);
        Scheduler.Park(Home, FAct{10, 0, Cell, EVoxelFluid::Water}, 4, true);
        Scheduler.Reset();
        TArray<FIntPoint> Columns;
        Scheduler.GetParkedColumns(Columns);
        TestTrue(TEXT("nothing parked after Reset"), Scheduler.GetParkedCount() == 0 && Columns.IsEmpty());
        TestEqual(TEXT("and Unpark finds nothing"), Scheduler.Unpark(Home, 20, true), 0);
        TestTrue(TEXT("the cell can be scheduled again"), Scheduler.Schedule(Cell, EVoxelFluid::Water, 30));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidRulesTest, "Unnamed.Voxel.Fluid.Rules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidRulesTest::RunTest(const FString& Parameters)
{
    // The parts no oracle isolates: acts wait for unknown cells, and lava meets water above and beside, never below.
    FVoxelFluidTestWorld World;
    const FIntVector Cell(0, 0, 64);
    World.Set(Cell, FVoxelFluidCell::MakeFluid(EVoxelFluid::Water, 3));
    FVoxelFluidCell Unknown;
    Unknown.bUnknown = true;
    World.Set(Cell + FIntVector(1, 0, 0), Unknown);
    TArray<FVoxelFluidWrite> Writes;
    TestFalse(TEXT("an act reading an unknown cell waits"),
        VoxelFluidRules::Act(World, Cell, EVoxelFluid::Water, FVoxelFluidOptions(), Writes));
    TestEqual(TEXT("and writes nothing"), Writes.Num(), 0);
    TestTrue(TEXT("an act for a fluid the cell lacks does nothing"),
        VoxelFluidRules::Act(World, Cell, EVoxelFluid::Lava, FVoxelFluidOptions(), Writes) && Writes.IsEmpty());

    const auto Reacts = [](uint8 Level, FIntVector Water, EVoxelFluidBlock& OutMade)
    {
        FVoxelFluidTestWorld Here;
        const FIntVector Lava(0, 0, 70);
        Here.Set(Lava, FVoxelFluidCell::MakeFluid(EVoxelFluid::Lava, Level));
        Here.Set(Lava + Water, FVoxelFluidCell::MakeFluid(EVoxelFluid::Water, 1));
        FVoxelFluidWrite Write;
        const bool bMade = VoxelFluidRules::React(Here, Lava, FVoxelFluidOptions(), Write);
        OutMade = bMade ? Write.After.Block : EVoxelFluidBlock::None;
        return bMade;
    };
    EVoxelFluidBlock Made;
    TestTrue(TEXT("a lava source with water beside becomes obsidian"),
        Reacts(0, FIntVector(1, 0, 0), Made) && Made == EVoxelFluidBlock::Obsidian);
    TestTrue(TEXT("flowing lava with water above becomes cobblestone"),
        Reacts(2, FIntVector(0, 0, 1), Made) && Made == EVoxelFluidBlock::Cobblestone);
    TestFalse(TEXT("lava with water below stays"), Reacts(0, FIntVector(0, 0, -1), Made));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidStatesTest, "Unnamed.Voxel.Fluid.States",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidStatesTest::RunTest(const FString& Parameters)
{
    // The block table's fluid fields from the registry, and the three readers that took FluidAmount == 8 as a source:
    // falling fluid has amount 8 too, and is not one.
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    const auto State = [&Table](const TCHAR* Block, const TCHAR* Property, const TCHAR* Value)
    {
        if (!Property) return Table->MakeState(Block, {});
        const TPair<FName, FName> Pair(Property, Value);
        return Table->MakeState(Block, MakeArrayView(&Pair, 1));
    };
    for (const TCHAR* Fluid : {TEXT("water"), TEXT("lava")})
        for (int32 Level = 0; Level < 16; ++Level)
        {
            const int32 Id = State(Fluid, TEXT("level"), *FString::FromInt(Level));
            if (!TestTrue(FString::Printf(TEXT("%s[level=%d] exists"), Fluid, Level), Id != INDEX_NONE)) continue;
            const FVoxelStateInfo& Info = Table->Get(uint16(Id));
            TestTrue(FString::Printf(TEXT("%s[level=%d]: fluid, level and source"), Fluid, Level),
                Info.Fluid == (Fluid == FStringView(TEXT("water")) ? EVoxelFluid::Water : EVoxelFluid::Lava) &&
                    Info.FluidLevel == Level && Info.IsFluidSource() == (Level == 0) &&
                    Info.FluidAmount == (Level == 0 || Level >= 8 ? 8 : 8 - Level));
        }
    const auto Holds = [&](const TCHAR* Name, int32 Id, EVoxelFluid Fluid, bool bSource)
    {
        if (!TestTrue(FString::Printf(TEXT("%s exists"), Name), Id != INDEX_NONE)) return;
        const FVoxelStateInfo& Info = Table->Get(uint16(Id));
        TestTrue(FString::Printf(TEXT("%s holds its fluid"), Name),
            Info.Fluid == Fluid && Info.IsFluidSource() == bSource && Info.FluidLevel == 0);
    };
    // Waterlogged blocks other than models (slabs) read as dry: the block import fills a state's water for model
    // blocks only (Scripts/Editor/BlockImportData.py). W2b's waterlogging adds them.
    Holds(TEXT("a dry slab"), State(TEXT("stone_slab"), nullptr, nullptr), EVoxelFluid::None, false);
    Holds(TEXT("kelp"), State(TEXT("kelp"), nullptr, nullptr), EVoxelFluid::Water, true);
    Holds(TEXT("stone"), State(TEXT("stone"), nullptr, nullptr), EVoxelFluid::None, false);

    // The readers on falling water and lava.
    const uint16 Falling = uint16(State(TEXT("water"), TEXT("level"), TEXT("8")));
    const uint16 Source = uint16(State(TEXT("water"), TEXT("level"), TEXT("0")));
    const uint16 FallingLava = uint16(State(TEXT("lava"), TEXT("level"), TEXT("8")));
    FVoxelPlantSurvival Survival;
    FVoxelBlockPredicates Predicates;
    Predicates.Initialize(*Table, Survival);
    TestEqual(TEXT("GetFluidBits: falling water is flowing"), Predicates.GetFluidBits(Falling),
        uint8(FVoxelBlockPredicates::WaterFlowing));
    TestEqual(TEXT("GetFluidBits: a water source"), Predicates.GetFluidBits(Source),
        uint8(FVoxelBlockPredicates::WaterSource));
    TestEqual(TEXT("GetFluidBits: falling lava is flowing"), Predicates.GetFluidBits(FallingLava),
        uint8(FVoxelBlockPredicates::LavaFlowing));
    FVoxelFeatureGraph Graph;
    FString Error;
    const bool bRead = VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error);
    if (!TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error), bRead)) return false;
    FVoxelBlockTags Tags(Graph, *Table);
    const bool bBuilt = Survival.Initialize(*Table, Tags, &Error);
    if (!TestTrue(FString::Printf(TEXT("the survival table builds (%s)"), *Error), bBuilt)) return false;
    TestFalse(TEXT("IsWaterSource: falling water is not a source"), Survival.IsWaterSource(Falling));
    return TestTrue(TEXT("IsWaterSource: a water source"), Survival.IsWaterSource(Source));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidBenchTest, "Unnamed.Voxel.Fluid.Bench",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidBenchTest::RunTest(const FString& Parameters)
{
    // Acts per millisecond in the test world (logged, not pinned: timings take a quiet machine). Scout M estimated 1 to
    // 4 us per act in the game; the test world reads cells through a map, so it is slower.
    struct FCase
    {
        const TCHAR* Name;
        int32 Spacing;
        int32 Count;
    };
    // One source on a plane (w_plane's 113 cells), and a flood: a 10 x 10 grid of sources 6 apart.
    for (const FCase Case : {FCase{TEXT("plane"), 0, 1}, FCase{TEXT("flood"), 6, 10}})
    {
        FVoxelFluidTestWorld World;
        const double Start = FPlatformTime::Seconds();
        for (int32 X = 0; X < Case.Count; ++X)
            for (int32 Y = 0; Y < Case.Count; ++Y)
                World.Place(FIntVector(X * Case.Spacing, Y * Case.Spacing, 64),
                    FVoxelFluidCell::MakeFluid(EVoxelFluid::Water, 0));
        while (World.GetScheduler().Num() > 0)
            World.Step();
        const double Seconds = FPlatformTime::Seconds() - Start;
        const int64 Acts = FMath::Max<int64>(World.GetActCount(), 1);
        AddInfo(
            FString::Printf(TEXT("%s: %lld acts, %lld writes, %llu ticks, %.3f ms, %.2f us per act (%.0f acts per ms)"),
                Case.Name, World.GetActCount(), World.GetWriteCount(), World.GetTick(), Seconds * 1000.0,
                Seconds * 1e6 / double(Acts), double(Acts) / (Seconds * 1000.0)));
        TestTrue(FString::Printf(TEXT("%s settles"), Case.Name), World.GetActCount() > 0);
    }
    return true;
}

#endif
