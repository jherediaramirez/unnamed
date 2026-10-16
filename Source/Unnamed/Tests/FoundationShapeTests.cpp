#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockFamilies.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationProfile.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Voxel/VoxelBlocks.h"

// Survival B2b (track T5): placement shapes and the neighbour pass against the oracle, every row of
// Reference/minecraft-blocks-26.3/facts/shape_updates.json (BlockState.updateShape answers measured in 26.3).
#if WITH_DEV_AUTOMATION_TESTS
namespace FoundationShapeTests
{
    constexpr EAutomationTestFlags ShapeTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    TSharedPtr<FJsonObject> LoadOracle()
    {
        static TSharedPtr<FJsonObject> Json;
        if (!Json.IsValid())
        {
            FString Text;
            const FString Path =
                FPaths::ProjectDir() / TEXT("Reference/minecraft-blocks-26.3/facts/shape_updates.json");
            if (FFileHelper::LoadFileToString(Text, *Path))
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
        }
        return Json;
    }

    EFoundationFace ParseFace(const FString& Name)
    {
        for (int32 Face = 0; Face < 6; ++Face)
            if (Name == FoundationGrid::FaceName(EFoundationFace(Face))) return EFoundationFace(Face);
        return EFoundationFace::None;
    }

    // Sets foundation.Blocks.Shapes for a test and restores it after.
    struct FShapesSwitch
    {
        IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Blocks.Shapes"));
        int32 Saved = Variable ? Variable->GetInt() : 1;

        void Set(int32 Value) const
        {
            if (Variable) Variable->Set(Value, ECVF_SetByCode);
        }
        ~FShapesSwitch() { Set(Saved); }
    };

    // Compares results and keeps the first mismatches, grouped by subject, direction and neighbour block.
    struct FTally
    {
        const FFoundationBlockRegistry& Registry;
        int32 Rows = 0, Misses = 0;
        TMap<FString, int32> Groups;
        TArray<FString> Order;

        void Check(FFoundationBlockState Subject, const FString& Where, FFoundationBlockState Neighbour,
            FFoundationBlockState Got, int32 Want)
        {
            ++Rows;
            if (Got.Id == Want) return;
            ++Misses;
            const FString Key = FString::Printf(TEXT("%s %s %s: got %s, want %s"), *Registry.ToString(Subject), *Where,
                *Registry.GetBlock(Neighbour).Id.ToString(), *Registry.ToString(Got),
                *Registry.ToString({uint16(Want)}));
            if (int32* Count = Groups.Find(Key))
                ++*Count;
            else
            {
                Groups.Add(Key, 1);
                Order.Add(Key);
            }
        }

        void Report(FAutomationTestBase& Test, const TCHAR* What, int32 MinRows) const
        {
            for (int32 Index = 0; Index < FMath::Min(Order.Num(), 400); ++Index)
                Test.AddError(FString::Printf(TEXT("%s: %s (x%d)"), What, *Order[Index], Groups[Order[Index]]));
            Test.TestEqual(FString::Printf(TEXT("%s: mismatches in %d rows"), What, Rows), Misses, 0);
            Test.TestTrue(FString::Printf(TEXT("%s: at least %d rows (%d)"), What, MinRows, Rows), Rows >= MinRows);
        }
    };

    // A case: the subject at the origin, the given neighbours, air elsewhere.
    struct FCase
    {
        TMap<FIntVector, FFoundationBlockState> Cells;

        FFoundationBlockState Get(const FIntVector& Cell) const
        {
            const FFoundationBlockState* State = Cells.Find(Cell);
            return State ? *State : FFoundationBlockState();
        }

        FFoundationBlockState Settle(const FFoundationBlockRegistry& Registry, FFoundationBlockState Subject) const
        {
            return FoundationBlockFamilies::SettleShape(
                Registry, Subject, FIntVector::ZeroValue, [this](const FIntVector& Cell) { return Get(Cell); });
        }
    };

    // Walks a compact group {"r": {"<result>": [[first, last], ...]}}: every neighbour id with its result.
    void ForEachRow(const FJsonObject& Group, TFunctionRef<void(int32 Neighbour, int32 Result)> Visit)
    {
        const TSharedPtr<FJsonObject>* Results;
        if (!Group.TryGetObjectField(TEXT("r"), Results)) return;
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Result : (*Results)->Values)
            for (const TSharedPtr<FJsonValue>& Run : Result.Value->AsArray())
            {
                const TArray<TSharedPtr<FJsonValue>>& Range = Run->AsArray();
                for (int32 Id = int32(Range[0]->AsNumber()); Id <= int32(Range[1]->AsNumber()); ++Id)
                    Visit(Id, FCString::Atoi(*Result.Key));
            }
    }

    // A subject's groups per direction, each neighbour alone toward that direction.
    void CheckDirections(const FFoundationBlockRegistry& Registry, const FJsonObject& Subject, FTally& Tally)
    {
        const FFoundationBlockState State{uint16(Subject.GetNumberField(TEXT("state")))};
        for (int32 Face = 0; Face < 6; ++Face)
        {
            const TSharedPtr<FJsonObject>* Group;
            if (!Subject.TryGetObjectField(FoundationGrid::FaceName(EFoundationFace(Face)), Group)) continue;
            ForEachRow(**Group,
                [&](int32 Neighbour, int32 Result)
                {
                    FCase Case;
                    Case.Cells.Add(FoundationGrid::FaceOffset(EFoundationFace(Face)), {uint16(Neighbour)});
                    Tally.Check(State, FoundationGrid::FaceName(EFoundationFace(Face)), {uint16(Neighbour)},
                        Case.Settle(Registry, State), Result);
                });
        }
    }

    // An authority with the project's blocks and rules, a stone floor at z 0 and one player.
    struct FShapeLab
    {
        FFoundationAuthority A;
        int32 Player = 0;
        uint32 Sequence = 1;
        FFoundationVitals Vitals;

        FShapeLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.PlantSurvival = VoxelBlocks::GetPlantSurvival(A.World.GetRegistry());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            Player = A.AddPlayer();
            A.Reset();
            for (int32 X = -6; X <= 6; ++X)
                for (int32 Y = -6; Y <= 6; ++Y)
                    A.World.Set({X, Y, 0}, State(TEXT("stone")));
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }

        FFoundationBlockState State(const TCHAR* Text) const
        {
            FFoundationBlockState State;
            ensureMsgf(A.World.GetRegistry().Parse(Text, State), TEXT("parse %s"), Text);
            return State;
        }

        FString At(const FIntVector& Cell) const { return A.World.GetRegistry().ToString(A.World.Get(Cell).State); }

        // Looks at the middle of Cell's Face from 2.5 blocks east and 1 above, holding Item.
        void Aim(const FIntVector& Cell, EFoundationFace Face, FName Item)
        {
            const FVector Point = FoundationGrid::CellBox(Cell, A.World.OriginCm).GetCenter() +
                FoundationGrid::FaceNormal(Face) * (UFoundationProfile::BlockCm / 2);
            FFoundationPlayerFacts Facts = P().Facts;
            Facts.EyeCm = Point + FVector(250, 30, 100);
            Facts.FeetCm = Facts.EyeCm - FVector(0, 0, 162);
            Facts.HeightCm = 180;
            Facts.Aim = (Point - Facts.EyeCm).GetSafeNormal();
            Facts.bGrounded = true;
            Facts.Step = P().Facts.Step + 1;
            A.SetFacts(Player, Facts);
            P().Inventory.SetSlot(0, P().Inventory.MakeStack(A.Rules.FindItem(Item), 1));
            P().Inventory.Select(0);
        }

        FFoundationResult Run(EFoundationCommandType Type, bool bPlace)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = Sequence++;
            Command.Type = Type;
            Command.Hand = EFoundationHand::Main;
            Command.Item = P().Inventory.GetHand(EFoundationHand::Main).GetRef();
            Command.Target = A.Resolve(P().Facts);
            if (bPlace)
            {
                FIntVector Destination;
                A.CheckPlacement(P(), EFoundationHand::Main, Command.Target, Destination);
                Command.Destination = Destination;
                Command.DestinationRevision = A.World.Get(Destination).Revision;
            }
            return A.Execute(Player, Command, Vitals);
        }

        bool Place(const FIntVector& Cell, EFoundationFace Face, FName Item)
        {
            Aim(Cell, Face, Item);
            return Run(EFoundationCommandType::UseOn, true).bPlaced;
        }

        bool Break(const FIntVector& Cell)
        {
            Aim(Cell, EFoundationFace::Up, TEXT("stone"));
            P().Inventory.SetSlot(0, {});
            FFoundationResult R = Run(EFoundationCommandType::MineStart, false);
            for (int32 Step = 0; Step < 2000 && !R.bBroken && R.IsCommitted(); ++Step)
            {
                FFoundationPlayerFacts Facts = P().Facts;
                ++Facts.Step;
                A.SetFacts(Player, Facts);
                R = Run(EFoundationCommandType::MineContinue, false);
            }
            return R.bBroken;
        }
    };
}

using namespace FoundationShapeTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationShapesFamiliesTest, "Unnamed.Foundation.Shapes.Families", ShapeTestFlags)
bool FFoundationShapesFamiliesTest::RunTest(const FString& Parameters)
{
    // The family lookup (tags and the pane/bars property set) names exactly the oracle's subjects, by their class.
    const TSharedPtr<FJsonObject> Oracle = LoadOracle();
    if (!TestTrue(TEXT("shape_updates.json loads"), Oracle.IsValid())) return false;
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    TMap<FName, EFoundationBlockFamily> Want;
    const TPair<const TCHAR*, EFoundationBlockFamily> Classes[] = {{TEXT("stairs"), EFoundationBlockFamily::Stairs},
        {TEXT("fence"), EFoundationBlockFamily::Fence}, {TEXT("wall"), EFoundationBlockFamily::Wall},
        {TEXT("fence_gate"), EFoundationBlockFamily::FenceGate}, {TEXT("bars"), EFoundationBlockFamily::Pane}};
    for (const TPair<const TCHAR*, EFoundationBlockFamily>& Class : Classes)
        for (const TSharedPtr<FJsonValue>& Id : Oracle->GetObjectField(TEXT("subjects"))->GetArrayField(Class.Key))
            Want.Add(FName(Id->AsString()), Class.Value);
    TestEqual(TEXT("182 subjects"), Want.Num(), 182);
    int32 Wrong = 0;
    for (int32 Index = 0; Index < Registry->GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Block = Registry->GetBlockAt(Index);
        const EFoundationBlockFamily* Expected = Want.Find(Block.Id);
        EFoundationBlockFamily Got = FoundationBlockFamilies::GetFamily(Block);
        // B3a's doors and trapdoors, B3b's buttons and lever and B4b's ladder are not shape-oracle subjects
        // (Unnamed.Foundation.Interact.Families checks them).
        if (Got == EFoundationBlockFamily::Door || Got == EFoundationBlockFamily::Trapdoor ||
            Got == EFoundationBlockFamily::Button || Got == EFoundationBlockFamily::Lever ||
            Got == EFoundationBlockFamily::Ladder)
            Got = EFoundationBlockFamily::None;
        if (Got != (Expected ? *Expected : EFoundationBlockFamily::None))
        {
            ++Wrong;
            AddError(FString::Printf(
                TEXT("%s: family %d, want %d"), *Block.Id.ToString(), int32(Got), Expected ? int32(*Expected) : 0));
        }
    }
    TestEqual(TEXT("blocks with the wrong family"), Wrong, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationShapesSingleTest, "Unnamed.Foundation.Shapes.SingleNeighbour", ShapeTestFlags)
bool FFoundationShapesSingleTest::RunTest(const FString& Parameters)
{
    // 7 representatives, one neighbour (air or any imported state) toward each of the 6 directions.
    const TSharedPtr<FJsonObject> Oracle = LoadOracle();
    if (!TestTrue(TEXT("shape_updates.json loads"), Oracle.IsValid())) return false;
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    FTally Tally{*Registry};
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Subject :
        Oracle->GetObjectField(TEXT("single_neighbour"))->Values)
        CheckDirections(*Registry, *Subject.Value->AsObject(), Tally);
    Tally.Report(*this, TEXT("single_neighbour"), 7 * 6 * 28000);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationShapesCrossTest, "Unnamed.Foundation.Shapes.CrossFamily", ShapeTestFlags)
bool FFoundationShapesCrossTest::RunTest(const FString& Parameters)
{
    // Every subject's default state, one of 195 sample neighbours toward each horizontal direction.
    const TSharedPtr<FJsonObject> Oracle = LoadOracle();
    if (!TestTrue(TEXT("shape_updates.json loads"), Oracle.IsValid())) return false;
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    FTally Tally{*Registry};
    const TSharedPtr<FJsonObject> Cross = Oracle->GetObjectField(TEXT("cross_family"));
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Subject : Cross->GetObjectField(TEXT("subjects"))->Values)
        CheckDirections(*Registry, *Subject.Value->AsObject(), Tally);
    Tally.Report(*this, TEXT("cross_family"), 182 * 195 * 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationShapesWallUpTest, "Unnamed.Foundation.Shapes.WallUp", ShapeTestFlags)
bool FFoundationShapesWallUpTest::RunTest(const FString& Parameters)
{
    // cobblestone_wall with stone at the listed sides and a state above: low and tall sides, the post.
    const TSharedPtr<FJsonObject> Oracle = LoadOracle();
    if (!TestTrue(TEXT("shape_updates.json loads"), Oracle.IsValid())) return false;
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    const FFoundationBlockState Wall = Registry->FindBlock(TEXT("cobblestone_wall"))->GetDefaultState();
    const FFoundationBlockState Stone = Registry->FindBlock(TEXT("stone"))->GetDefaultState();
    FTally Tally{*Registry};
    for (const TSharedPtr<FJsonValue>& Value : Oracle->GetArrayField(TEXT("wall_up")))
    {
        const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
        FCase Case;
        FString Sides;
        for (const TSharedPtr<FJsonValue>& Side : Row[0]->AsArray())
        {
            Case.Cells.Add(FoundationGrid::FaceOffset(ParseFace(Side->AsString())), Stone);
            Sides += Side->AsString() + TEXT(",");
        }
        const FFoundationBlockState Above{uint16(Row[1]->AsNumber())};
        Case.Cells.Add(FIntVector(0, 0, 1), Above);
        Tally.Check(Wall, FString::Printf(TEXT("stone at [%s], above"), *Sides), Above, Case.Settle(*Registry, Wall),
            int32(Row[2]->AsNumber()));
    }
    Tally.Report(*this, TEXT("wall_up"), 32);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationShapesStairsTest, "Unnamed.Foundation.Shapes.Stairs", ShapeTestFlags)
bool FFoundationShapesStairsTest::RunTest(const FString& Parameters)
{
    // The straight dry oak stairs with one stair neighbour per side, and the 512 front-or-back plus side pairs.
    const TSharedPtr<FJsonObject> Oracle = LoadOracle();
    if (!TestTrue(TEXT("shape_updates.json loads"), Oracle.IsValid())) return false;
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    const TSharedPtr<FJsonObject> Stairs = Oracle->GetObjectField(TEXT("stairs"));
    FTally Tally{*Registry};
    int32 Outer = 0, Inner = 0;
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Subject : Stairs->GetObjectField(TEXT("single"))->Values)
    {
        const FFoundationBlockState State{uint16(FCString::Atoi(*Subject.Key))};
        const bool bBottom = Registry->GetValue(State, TEXT("half")) == TEXT("bottom");
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Direction : Subject.Value->AsObject()->Values)
        {
            const EFoundationFace Face = ParseFace(Direction.Key);
            ForEachRow(*Direction.Value->AsObject(),
                [&](int32 Neighbour, int32 Result)
                {
                    FCase Case;
                    Case.Cells.Add(FoundationGrid::FaceOffset(Face), {uint16(Neighbour)});
                    Tally.Check(State, Direction.Key, {uint16(Neighbour)}, Case.Settle(*Registry, State), Result);
                    // VALUES "Stair corners": 32 bottom-stair cases with one straight dry bottom stair neighbour.
                    const FFoundationBlockState Other{uint16(Neighbour)};
                    if (bBottom && Registry->GetValue(Other, TEXT("half")) == TEXT("bottom") &&
                        Registry->GetValue(Other, TEXT("shape")) == TEXT("straight") &&
                        Registry->GetValue(Other, TEXT("waterlogged")) == TEXT("false"))
                    {
                        const FString Shape = Registry->GetValue({uint16(Result)}, TEXT("shape")).ToString();
                        Outer += Shape.StartsWith(TEXT("outer"));
                        Inner += Shape.StartsWith(TEXT("inner"));
                    }
                });
        }
    }
    TestEqual(TEXT("bottom outer corners (VALUES blocks.stair_corners)"), Outer, 16);
    TestEqual(TEXT("bottom inner corners (VALUES blocks.stair_corners)"), Inner, 16);
    for (const TSharedPtr<FJsonValue>& Value : Stairs->GetArrayField(TEXT("pairs")))
    {
        // [subject, main_dir, main, side_dir, side, result, one_pass]
        const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
        const FFoundationBlockState State{uint16(Row[0]->AsNumber())};
        FCase Case;
        Case.Cells.Add(FoundationGrid::FaceOffset(ParseFace(Row[1]->AsString())), {uint16(Row[2]->AsNumber())});
        Case.Cells.Add(FoundationGrid::FaceOffset(ParseFace(Row[3]->AsString())), {uint16(Row[4]->AsNumber())});
        Tally.Check(State,
            FString::Printf(TEXT("pair %s %s + %s"), *Row[1]->AsString(),
                *Registry->ToString({uint16(Row[2]->AsNumber())}), *Row[3]->AsString()),
            {uint16(Row[4]->AsNumber())}, Case.Settle(*Registry, State), int32(Row[5]->AsNumber()));
    }
    Tally.Report(*this, TEXT("stairs"), 512 + 8 * 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationShapesGatesTest, "Unnamed.Foundation.Shapes.GatesInWall", ShapeTestFlags)
bool FFoundationShapesGatesTest::RunTest(const FString& Parameters)
{
    // Closed oak gates with walls, stone or a fence at the listed sides: in_wall (VALUES "Fence gates").
    const TSharedPtr<FJsonObject> Oracle = LoadOracle();
    if (!TestTrue(TEXT("shape_updates.json loads"), Oracle.IsValid())) return false;
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    FTally Tally{*Registry};
    for (const TSharedPtr<FJsonValue>& Value : Oracle->GetArrayField(TEXT("gates_in_wall")))
    {
        // [subject, sides, neighbour, result, one_pass]
        const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
        const FFoundationBlockState State{uint16(Row[0]->AsNumber())};
        const FFoundationBlockState Neighbour{uint16(Row[2]->AsNumber())};
        FCase Case;
        FString Sides;
        for (const TSharedPtr<FJsonValue>& Side : Row[1]->AsArray())
        {
            Case.Cells.Add(FoundationGrid::FaceOffset(ParseFace(Side->AsString())), Neighbour);
            Sides += Side->AsString() + TEXT(",");
        }
        Tally.Check(State, FString::Printf(TEXT("at [%s]"), *Sides), Neighbour, Case.Settle(*Registry, State),
            int32(Row[3]->AsNumber()));
    }
    Tally.Report(*this, TEXT("gates_in_wall"), 72);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationShapesPlacementTest, "Unnamed.Foundation.Shapes.Placement", ShapeTestFlags)
bool FFoundationShapesPlacementTest::RunTest(const FString& Parameters)
{
    // getStateForPlacement: stairs face the look with the slab rule's half, gates face the look (VALUES "Fence
    // gates", blocks.fence_gate_placement), connections and shapes from the neighbours as the oracle settles them.
    FShapesSwitch Switch;
    if (!TestNotNull(TEXT("foundation.Blocks.Shapes exists"), Switch.Variable)) return false;
    Switch.Set(1);
    FShapeLab L;
    FFoundationBlockWorld& World = L.A.World;
    const FFoundationBlockRegistry& R = World.GetRegistry();
    auto Place = [&](FName Block, const FIntVector& Clicked, EFoundationFace Face, double Yaw, double Height = .5)
    {
        FFoundationPlacementContext Context;
        Context.ClickedCell = Clicked;
        Context.ClickedFace = Face;
        Context.Yaw = Yaw;
        Context.ClickPointCm = FoundationGrid::CellMin(Clicked, World.OriginCm) +
            FVector(.5, .5, Height) * UFoundationProfile::BlockCm +
            FoundationGrid::FaceNormal(Face) * (UFoundationProfile::BlockCm / 2) *
                FVector(1, 1, Face == EFoundationFace::Up || Face == EFoundationFace::Down ? 1 : 0);
        const FFoundationBlockType& Type = *World.FindBlock(Block);
        FoundationBlockPlacement::ResolveDestination(World, Type, true, Context);
        return R.ToString(FoundationBlockPlacement::GetStateForPlacement(World, Type, Context));
    };
    // Yaw 180 looks north, 0 south, 90 west, -90 east.
    TestEqual(TEXT("a stair on a floor, looking north"), Place(TEXT("oak_stairs"), {0, 0, 0}, EFoundationFace::Up, 180),
        FString(TEXT("oak_stairs[facing=north,half=bottom,shape=straight,waterlogged=false]")));
    World.Set({0, 0, 3}, L.State(TEXT("stone")));
    World.Set({0, 0, 1}, L.State(TEXT("stone")));
    TestEqual(TEXT("a stair under a ceiling, looking east"),
        Place(TEXT("oak_stairs"), {0, 0, 3}, EFoundationFace::Down, -90),
        FString(TEXT("oak_stairs[facing=east,half=top,shape=straight,waterlogged=false]")));
    TestEqual(TEXT("a stair on the upper half of a side, looking west"),
        Place(TEXT("oak_stairs"), {0, 0, 1}, EFoundationFace::East, 90, .75),
        FString(TEXT("oak_stairs[facing=west,half=top,shape=straight,waterlogged=false]")));
    TestEqual(TEXT("a stair on the lower half of a side, looking west"),
        Place(TEXT("oak_stairs"), {0, 0, 1}, EFoundationFace::East, 90, .25),
        FString(TEXT("oak_stairs[facing=west,half=bottom,shape=straight,waterlogged=false]")));
    // A corner: a north-facing stair in front (north) of a stair placed facing east turns it outer (oracle pairs).
    FCase Corner;
    Corner.Cells.Add({1, 0, 0}, L.State(TEXT("oak_stairs[facing=north,half=bottom,shape=straight,waterlogged=false]")));
    World.Set({4, 0, 1}, L.State(TEXT("oak_stairs[facing=north,half=bottom,shape=straight,waterlogged=false]")));
    const FString Settled = R.ToString(
        Corner.Settle(R, L.State(TEXT("oak_stairs[facing=east,half=bottom,shape=straight,waterlogged=false]"))));
    // The front stair faces east's counter-clockwise side: the left outer corner (oracle stairs.single).
    TestEqual(TEXT("a stair placed facing east with a north-facing stair in front"),
        Place(TEXT("oak_stairs"), {3, 0, 0}, EFoundationFace::Up, -90),
        FString(TEXT("oak_stairs[facing=east,half=bottom,shape=outer_left,waterlogged=false]")));
    TestEqual(TEXT("...the oracle-checked settle agrees"), Settled,
        FString(TEXT("oak_stairs[facing=east,half=bottom,shape=outer_left,waterlogged=false]")));
    // Connections: a fence beside stone and an oak fence, a pane beside glass, a wall under stone with a wall beside.
    World.Set({-3, 0, 1}, L.State(TEXT("stone")));
    World.Set({-4, 1, 1}, L.State(TEXT("oak_fence")));
    TestEqual(TEXT("a fence between stone (east) and a fence (south)"),
        Place(TEXT("oak_fence"), {-4, 0, 0}, EFoundationFace::Up, 0),
        FString(TEXT("oak_fence[east=true,north=false,south=true,waterlogged=false,west=false]")));
    World.Set({-3, 3, 1}, L.State(TEXT("glass")));
    TestEqual(TEXT("a pane beside glass (east)"), Place(TEXT("glass_pane"), {-4, 3, 0}, EFoundationFace::Up, 0),
        FString(TEXT("glass_pane[east=true,north=false,south=false,waterlogged=false,west=false]")));
    World.Set({2, 4, 1}, L.State(TEXT("cobblestone_wall")));
    World.Set({3, 4, 2}, L.State(TEXT("stone")));
    TestEqual(TEXT("a wall beside a wall (west) under stone: a tall side and, at its end, the post"),
        Place(TEXT("cobblestone_wall"), {3, 4, 0}, EFoundationFace::Up, 0),
        FString(TEXT("cobblestone_wall[east=none,north=none,south=none,up=true,waterlogged=false,west=tall]")));
    // A gate looking north between walls east and west sits in the wall.
    World.Set({5, -4, 1}, L.State(TEXT("cobblestone_wall")));
    World.Set({3, -4, 1}, L.State(TEXT("cobblestone_wall")));
    TestEqual(TEXT("a gate placed looking north between walls"),
        Place(TEXT("oak_fence_gate"), {4, -4, 0}, EFoundationFace::Up, 180),
        FString(TEXT("oak_fence_gate[facing=north,in_wall=true,open=false,powered=false]")));
    TestEqual(TEXT("a gate placed looking east between those walls"),
        Place(TEXT("oak_fence_gate"), {4, -4, 0}, EFoundationFace::Up, -90),
        FString(TEXT("oak_fence_gate[facing=east,in_wall=false,open=false,powered=false]")));
    // Switch 0: B1's placement (the default state, plant facing).
    Switch.Set(0);
    TestEqual(TEXT("switch 0: a fence between stone and a fence places unconnected"),
        Place(TEXT("oak_fence"), {-4, 0, 0}, EFoundationFace::Up, 0),
        FString(TEXT("oak_fence[east=false,north=false,south=false,waterlogged=false,west=false]")));
    TestEqual(TEXT("switch 0: a stair looking north faces the player, as B1's plant rule"),
        Place(TEXT("oak_stairs"), {5, 5, 0}, EFoundationFace::Up, 180),
        FString(TEXT("oak_stairs[facing=south,half=bottom,shape=straight,waterlogged=false]")));
    TestEqual(TEXT("switch 0: a gate between walls stays out of the wall"),
        Place(TEXT("oak_fence_gate"), {4, -4, 0}, EFoundationFace::Up, 180),
        FString(TEXT("oak_fence_gate[facing=south,in_wall=false,open=false,powered=false]")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationShapesNeighbourTest, "Unnamed.Foundation.Shapes.Neighbours", ShapeTestFlags)
bool FFoundationShapesNeighbourTest::RunTest(const FString& Parameters)
{
    // The neighbour pass through the authority's commands: placing and mining beside family blocks updates them, one
    // ring only; with the switch at 0 nothing changes.
    FShapesSwitch Switch;
    if (!TestNotNull(TEXT("foundation.Blocks.Shapes exists"), Switch.Variable)) return false;
    for (const int32 Value : {1, 0})
    {
        Switch.Set(Value);
        const bool bOn = Value == 1;
        FShapeLab L;
        const FString Tag = FString::Printf(TEXT("switch %d: "), Value);
        // A fence north of the cell (0, 0, 1), a wall west of it and a fence two cells away.
        const FIntVector Cell(0, 0, 1);
        L.A.World.Set({0, -1, 1}, L.State(TEXT("oak_fence")));
        L.A.World.Set({-1, 0, 1}, L.State(TEXT("cobblestone_wall")));
        L.A.World.Set({0, -2, 1}, L.State(TEXT("oak_fence")));
        const FString Fence = L.At({0, -1, 1}), Wall = L.At({-1, 0, 1}), Far = L.At({0, -2, 1});
        if (!TestTrue(Tag + TEXT("stone placed at the cell"), L.Place({0, 0, 0}, EFoundationFace::Up, TEXT("stone"))))
            continue;
        TestEqual(Tag + TEXT("the fence north of it"), L.At({0, -1, 1}),
            bOn ? FString(TEXT("oak_fence[east=false,north=false,south=true,waterlogged=false,west=false]")) : Fence);
        TestEqual(Tag + TEXT("the wall west of it"), L.At({-1, 0, 1}),
            bOn ? FString(TEXT("cobblestone_wall[east=low,north=none,south=none,up=true,waterlogged=false,west=none]"))
                : Wall);
        TestEqual(Tag + TEXT("the fence beyond is not in the ring"), L.At({0, -2, 1}), Far);
        // Mining the stone: the neighbours let go.
        TestTrue(Tag + TEXT("the stone at the cell is mined"), L.Break(Cell) && L.A.World.Get(Cell).State.IsAir());
        TestEqual(Tag + TEXT("the fence after mining"), L.At({0, -1, 1}), Fence);
        TestEqual(Tag + TEXT("the wall after mining"), L.At({-1, 0, 1}), Wall);
    }
    // The wall below: a wall at (2, 2, 1) beside stone east; placing stone on top of it makes that side tall.
    for (const int32 Value : {1, 0})
    {
        Switch.Set(Value);
        FShapeLab L;
        const TCHAR* Low = TEXT("cobblestone_wall[east=low,north=none,south=none,up=true,waterlogged=false,west=none]");
        L.A.World.Set({3, 2, 1}, L.State(TEXT("stone")));
        L.A.World.Set({2, 2, 1}, L.State(Low));
        if (TestTrue(TEXT("stone placed on the wall"), L.Place({2, 2, 1}, EFoundationFace::Up, TEXT("stone"))))
            TestEqual(FString::Printf(TEXT("switch %d: the wall below the new stone"), Value), L.At({2, 2, 1}),
                Value ? FString(TEXT(
                            "cobblestone_wall[east=tall,north=none,south=none,up=true,waterlogged=false,west=none]"))
                      : FString(Low));
    }
    return true;
}
#endif
