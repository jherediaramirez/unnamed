#include "Misc/AutomationTest.h"
#include "Blocks/VoxelBlockTable.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationBlockPlacement.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationPlantRules.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationTargeting.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Voxel/VoxelBlocks.h"

#if WITH_DEV_AUTOMATION_TESTS
// Plants and the other model blocks (phase 6, S7): their moved boxes, placement states and replaceable flags.
namespace FoundationPlantTests
{
    constexpr EAutomationTestFlags PlantTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr double PlantBlockCm = UFoundationProfile::BlockCm;

    // A world on the project registry with a few cells set by block-state text.
    struct FPlantWorld
    {
        FFoundationBlockWorld World;

        FPlantWorld()
        {
            World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            World.BoundsMin = FIntVector(-20);
            World.BoundsMax = FIntVector(19);
        }

        const FFoundationBlockRegistry& R() const { return World.GetRegistry(); }

        FFoundationBlockState State(const TCHAR* Text) const
        {
            FFoundationBlockState State;
            ensureMsgf(R().Parse(Text, State), TEXT("parse %s"), Text);
            return State;
        }

        void Put(FIntVector Cell, const TCHAR* Text) { World.Set(Cell, State(Text)); }

        // getStateForPlacement for a click on Cell's Face, the hit at the middle of the face.
        FFoundationBlockState Place(
            const TCHAR* Block, FIntVector Clicked, EFoundationFace Face, double Yaw = 0, double Pitch = 0)
        {
            FFoundationPlacementContext Context;
            Context.ClickedCell = Clicked;
            Context.ClickedFace = Face;
            Context.Yaw = Yaw;
            Context.Pitch = Pitch;
            Context.ClickPointCm = FoundationGrid::CellBox(Clicked, World.OriginCm).GetCenter() +
                FoundationGrid::FaceNormal(Face) * (PlantBlockCm / 2);
            const FFoundationBlockType& Type = *World.FindBlock(Block);
            FoundationBlockPlacement::ResolveDestination(World, Type, true, Context);
            Destination = Context.Destination;
            return FoundationBlockPlacement::GetStateForPlacement(World, Type, Context);
        }

        FString Text(FFoundationBlockState State) const { return R().ToString(State); }

        FIntVector Destination = FIntVector::ZeroValue;
    };

    bool SamePlantBox(const FBox& A, const FBox& B)
    {
        return A.Min.Equals(B.Min, 1e-4) && A.Max.Equals(B.Max, 1e-4);
    }
}

// Every model state's outline and collision boxes in a cell: the facts' boxes moved by the one offset mapping (the
// voxel table's, which the mesher draws with) where the facts move them, inside their cell; the single boxes are
// their bounds; collision gathers every box; a ray misses a flower's empty corner and hits its moved middle.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationPlantShapesTest, "Unnamed.Foundation.Blocks.PlantShapes", FoundationPlantTests::PlantTestFlags)

bool FFoundationPlantShapesTest::RunTest(const FString& Parameters)
{
    using namespace FoundationPlantTests;
    FPlantWorld W;
    const FFoundationBlockRegistry& Registry = W.R();
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::MakeTable(Registry);
    const FIntVector Cells[] = {{3, -7, 2}, {-11, 5, 0}, {0, 0, 1}, {17, 12, -4}};
    int32 ModelStates = 0, Mismatches = 0, MovedBoxes = 0, Outside = 0, OffsetBlocks = 0, LeavesColumn = 0;
    TMap<FName, int32> OutsideBlocks;
    TSet<FName> OutsideFamilies;
    auto Fail = [&](const FString& Message)
    {
        if (++Mismatches <= 20) AddError(Message);
    };
    for (int32 Index = 1; Index < Registry.GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Block = Registry.GetBlockAt(Index);
        OffsetBlocks += Block.OffsetKind != EFoundationOffsetKind::None;
        for (int32 Local = 0; Local < Block.StateCount; ++Local)
        {
            const FFoundationBlockState State{uint16(Block.MinStateId + Local)};
            if (Registry.GetShape(State) != EFoundationBlockShape::Model) continue;
            ++ModelStates;
            for (const FIntVector& Cell : Cells)
            {
                const FVector Offset = FoundationBlockOffsets::GetOffset(Block, Cell);
                if (!Offset.Equals(FVector(VoxelModels::GetOffset(Table->Get(State.Id), Cell.X, Cell.Y)), 1e-6))
                    Fail(Registry.ToString(State) + TEXT(" offset differs from the voxel table's"));
                const FBox CellBox = FoundationGrid::CellBox(Cell, W.World.OriginCm).ExpandBy(1e-3);
                for (const bool bCollision : {false, true})
                {
                    TArray<FBox> Units, Boxes;
                    if (bCollision)
                        Registry.GetCollisionBoxes(State, Units);
                    else
                        Registry.GetOutlineBoxes(State, Units);
                    const bool bAny = bCollision ? W.World.GetCollisionBoxes(Cell, State, Boxes)
                                                 : W.World.GetOutlineBoxes(Cell, State, Boxes);
                    const bool bMoves = bCollision ? Block.bCollisionMovesWithOffset : Block.bOutlineMovesWithOffset;
                    const FVector Min = FoundationGrid::CellMin(Cell, W.World.OriginCm) +
                        (bMoves ? Offset : FVector::ZeroVector) * PlantBlockCm;
                    const FString Label =
                        Registry.ToString(State) + (bCollision ? TEXT(" collision") : TEXT(" outline"));
                    if (bAny == Units.IsEmpty() || Boxes.Num() != Units.Num())
                    {
                        Fail(Label + TEXT(" box count differs"));
                        continue;
                    }
                    FBox Bounds(ForceInit);
                    for (int32 Box = 0; Box < Boxes.Num(); ++Box)
                    {
                        if (!SamePlantBox(Boxes[Box],
                                FBox(Min + Units[Box].Min * PlantBlockCm, Min + Units[Box].Max * PlantBlockCm)))
                            Fail(Label + TEXT(" box differs"));
                        // Collision stays in its cell; a thick bamboo outline reaches 1/16 into the next one at
                        // the largest offsets.
                        const bool bOutside =
                            !(bCollision ? CellBox : CellBox.ExpandBy(PlantBlockCm / 16)).IsInside(Boxes[Box]);
                        Outside += bOutside;
                        // The gatherers' outer layers (foundation.Blocks.Shapes) read only the cells below and above:
                        // every box stays inside its cell's column.
                        const FBox Column(
                            FVector(CellBox.Min.X, CellBox.Min.Y, -1e9), FVector(CellBox.Max.X, CellBox.Max.Y, 1e9));
                        LeavesColumn += bCollision && !Column.IsInside(Boxes[Box]);
                        if (bOutside) ++OutsideBlocks.FindOrAdd(Block.Id);
                        if (bOutside && Block.bAppendedFamily) OutsideFamilies.Add(Block.Id);
                        MovedBoxes += bMoves && !Offset.IsNearlyZero();
                        Bounds += Boxes[Box];
                    }
                    FBox Single;
                    const bool bSingle = bCollision ? W.World.GetCollisionBox(Cell, State, Single)
                                                    : W.World.GetSelectionBox(Cell, State, Single);
                    if (bSingle != bAny || (bAny && !SamePlantBox(Single, Bounds)))
                        Fail(Label + TEXT(" single box is not the bounds"));
                }
            }
        }
    }
    TestEqual(TEXT("model states (block_states.json's 25,082, B1's families included)"), ModelStates, 25082);
    TestEqual(TEXT("moved boxes and their bounds that differ"), Mismatches, 0);
    // B1's fences, walls and fence gates collide 1.5 blocks tall (vanilla), and the pitcher crop's lower half
    // reaches 1/16 below its cell: those leave the cell, vertically only (B2a's gather reads the cells below and above
    // a region for them: Blocks.ShapesGather). Every other block stays inside.
    TestEqual(TEXT("collision boxes leaving their cell's column"), LeavesColumn, 0);
    for (const TPair<FName, int32>& Pair : OutsideBlocks)
    {
        const FString Id = Pair.Key.ToString();
        if (!OutsideFamilies.Contains(Pair.Key) ||
            (!Id.EndsWith(TEXT("_fence")) && !Id.EndsWith(TEXT("_wall")) && !Id.EndsWith(TEXT("_fence_gate")) &&
                Id != TEXT("pitcher_crop")))
            AddError(FString::Printf(TEXT("%s: %d boxes leave their cell"), *Id, Pair.Value));
    }
    AddInfo(FString::Printf(TEXT("%d blocks with boxes outside their cell"), OutsideBlocks.Num()));
    TestEqual(TEXT("collision boxes outside their cell, outline boxes beyond 1/16 of it"), Outside, 127524);
    TestTrue(TEXT("boxes moved by an offset"), MovedBoxes > 0);
    // Bamboo at 256 cells: its outline leaves the cell by at most 1/16, its moved collision never.
    int32 BambooOutside = 0;
    const FFoundationBlockType& BambooBlock = *W.World.FindBlock(TEXT("bamboo"));
    for (int32 Y = 0; Y < 16; ++Y)
        for (int32 X = 0; X < 16; ++X)
            for (int32 Local = 0; Local < BambooBlock.StateCount; ++Local)
            {
                const FIntVector Cell(X, Y, 1);
                const FFoundationBlockState State{uint16(BambooBlock.MinStateId + Local)};
                const FBox CellBox = FoundationGrid::CellBox(Cell, W.World.OriginCm).ExpandBy(1e-3);
                TArray<FBox> Outline, Collision;
                W.World.GetOutlineBoxes(Cell, State, Outline);
                W.World.GetCollisionBoxes(Cell, State, Collision);
                for (const FBox& Box : Outline)
                    BambooOutside += !CellBox.ExpandBy(PlantBlockCm / 16).IsInside(Box);
                for (const FBox& Box : Collision)
                    BambooOutside += !CellBox.IsInside(Box);
            }
    TestEqual(TEXT("bamboo boxes beyond those bounds at 256 cells"), BambooOutside, 0);
    TestTrue(TEXT("blocks with an offset"), OffsetBlocks > 0);

    // Collision gathers every box of every cell: an azalea's five, a bamboo's one moved by its offset, a cube's one.
    FPlantWorld G;
    const FIntVector Azalea(1, 1, 1), Bamboo(4, 2, 1), Stone(6, 2, 1);
    G.Put(Azalea, TEXT("azalea"));
    G.Put(Bamboo, TEXT("bamboo"));
    G.Put(Stone, TEXT("stone"));
    TArray<FBox> Gathered, Expected;
    G.World.GatherCollision(FBox(FVector(-100), FVector(1000)), Gathered);
    for (const FIntVector& Cell : {Azalea, Bamboo, Stone})
        G.World.GetCollisionBoxes(Cell, G.World.Get(Cell).State, Expected);
    TestEqual(TEXT("gathered boxes (5 azalea, 1 bamboo, 1 stone)"), Gathered.Num(), 7);
    bool bSame = Gathered.Num() == Expected.Num();
    for (int32 Box = 0; bSame && Box < Gathered.Num(); ++Box)
        bSame = Expected.ContainsByPredicate([&](const FBox& Want) { return SamePlantBox(Want, Gathered[Box]); });
    TestTrue(TEXT("gathered boxes are the cells' boxes"), bSame);
    const FVector BambooOffset = FoundationBlockOffsets::GetOffset(*G.World.FindBlock(TEXT("bamboo")), Bamboo);
    TestFalse(TEXT("the bamboo cell has an offset"), BambooOffset.IsNearlyZero());
    const FVector BambooMin = FoundationGrid::CellMin(Bamboo, G.World.OriginCm) + BambooOffset * PlantBlockCm;
    TestTrue(TEXT("bamboo collides where it is drawn (facts 0.40625-0.59375, moved)"),
        Gathered.ContainsByPredicate(
            [&](const FBox& Box)
            {
                return SamePlantBox(Box,
                    FBox(BambooMin + FVector(.40625, .40625, 0) * PlantBlockCm,
                        BambooMin + FVector(.59375, .59375, 1) * PlantBlockCm));
            }));

    // A ray through a dandelion's empty corner hits the stone behind it; through its moved middle, the dandelion.
    FPlantWorld T;
    const FIntVector Flower(0, 0, 1);
    T.Put(Flower, TEXT("dandelion"));
    T.Put(Flower + FIntVector(0, 1, 0), TEXT("stone"));
    const FVector FlowerOffset = FoundationBlockOffsets::GetOffset(*T.World.FindBlock(TEXT("dandelion")), Flower);
    TestFalse(TEXT("the dandelion cell has an offset"), FlowerOffset.IsNearlyZero());
    const FVector CellMin = FoundationGrid::CellMin(Flower, T.World.OriginCm);
    FFoundationRay Ray;
    Ray.Direction = FVector(0, 1, 0);
    Ray.Eye = CellMin + FVector(3, -150, 30);
    FFoundationHit Hit = FoundationTargeting::Resolve(Ray, T.World, {}, {});
    TestTrue(TEXT("the empty corner passes to the stone"),
        Hit.Kind == EFoundationHitKind::Block && Hit.Cell == Flower + FIntVector(0, 1, 0));
    Ray.Eye = CellMin + FVector(50 + FlowerOffset.X * PlantBlockCm, -150, 30);
    Hit = FoundationTargeting::Resolve(Ray, T.World, {}, {});
    TestTrue(TEXT("the moved middle hits the dandelion"), Hit.Kind == EFoundationHitKind::Block && Hit.Cell == Flower);
    return true;
}

// getStateForPlacement for the model blocks, and the halves of double plants.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationPlantPlacementTest, "Unnamed.Foundation.Blocks.PlantPlacement", FoundationPlantTests::PlantTestFlags)

bool FFoundationPlantPlacementTest::RunTest(const FString& Parameters)
{
    using namespace FoundationPlantTests;
    FPlantWorld W;
    const FFoundationBlockRegistry& R = W.R();
    W.Put({0, 0, 0}, TEXT("grass_block"));
    W.Put({5, 0, 0}, TEXT("stone"));
    W.Put({8, 0, 0}, TEXT("jungle_log"));
    auto Expect = [&](const TCHAR* What, FFoundationBlockState Actual, const TCHAR* Want)
    { TestEqual(What, W.Text(Actual), W.Text(W.State(Want))); };
    Expect(TEXT("a flower places its default state"), W.Place(TEXT("dandelion"), {0, 0, 0}, EFoundationFace::Up),
        TEXT("dandelion"));
    TestEqual(TEXT("on top of the clicked block"), W.Destination, FIntVector(0, 0, 1));
    const FFoundationBlockState Lower = W.Place(TEXT("tall_grass"), {0, 0, 0}, EFoundationFace::Up);
    Expect(TEXT("a double plant places its lower half"), Lower, TEXT("tall_grass[half=lower]"));

    // The two halves.
    FIntVector Other;
    FFoundationBlockState Half;
    TestTrue(TEXT("the lower half's other half"),
        FoundationPlantRules::GetOtherHalf(W.World, {0, 0, 1}, Lower, Other, Half) && Other == FIntVector(0, 0, 2) &&
            Half == W.State(TEXT("tall_grass[half=upper]")));
    TestTrue(TEXT("the upper half's other half"),
        FoundationPlantRules::GetOtherHalf(W.World, {0, 0, 2}, Half, Other, Half) && Other == FIntVector(0, 0, 1) &&
            Half == Lower);
    TestFalse(TEXT("a flower has no other half"),
        FoundationPlantRules::GetOtherHalf(W.World, {0, 0, 1}, W.State(TEXT("dandelion")), Other, Half));
    TestTrue(TEXT("lower and upper are halves"),
        FoundationPlantRules::IsOtherHalf(R, Lower, W.State(TEXT("tall_grass[half=upper]"))));
    TestFalse(TEXT("two lower halves are not"), FoundationPlantRules::IsOtherHalf(R, Lower, Lower));
    TestFalse(TEXT("another double plant is not"),
        FoundationPlantRules::IsOtherHalf(R, Lower, W.State(TEXT("large_fern[half=upper]"))));
    W.Put({12, 0, 1}, TEXT("water"));
    TestTrue(TEXT("the default water state is a source"), R.GetFluid(W.World.Get({12, 0, 1}).State).bSource);
    TestTrue(TEXT("a small dripleaf's upper half takes waterlogged from its cell"),
        FoundationPlantRules::GetOtherHalf(
            W.World, {12, 0, 0}, W.State(TEXT("small_dripleaf[half=lower,waterlogged=false]")), Other, Half) &&
            W.Text(Half) == W.Text(W.State(TEXT("small_dripleaf[half=upper,waterlogged=true]"))));

    // Facing: amethyst points away from the clicked block on every face; cocoa faces the log it hangs on; the
    // others face the player (yaw 0 looks south, 90 west).
    for (int32 Face = 0; Face < 6; ++Face)
    {
        const EFoundationFace Clicked = EFoundationFace(Face);
        TestEqual(FString::Printf(TEXT("amethyst on the %s face"), FoundationGrid::FaceName(Clicked)),
            R.GetValue(W.Place(TEXT("amethyst_cluster"), {5, 0, 0}, Clicked), TEXT("facing")),
            FName(FoundationGrid::FaceName(Clicked)));
    }
    TestEqual(TEXT("cocoa on a log's south side faces north, toward the log"),
        R.GetValue(W.Place(TEXT("cocoa"), {8, 0, 0}, EFoundationFace::South), TEXT("facing")), FName(TEXT("north")));
    Expect(TEXT("small dripleaf faces the player"), W.Place(TEXT("small_dripleaf"), {0, 0, 0}, EFoundationFace::Up),
        TEXT("small_dripleaf[facing=north,half=lower]"));
    TestEqual(TEXT("pink petals face the player looking west"),
        R.GetValue(W.Place(TEXT("pink_petals"), {0, 0, 0}, EFoundationFace::Up, 90), TEXT("facing")),
        FName(TEXT("east")));

    // Multiface: the one face toward the clicked block; a vine has no floor face, and under a block it holds its up
    // face (Jesus's answer to Q8).
    Expect(TEXT("vine on a south side"), W.Place(TEXT("vine"), {5, 0, 0}, EFoundationFace::South),
        TEXT("vine[north=true]"));
    Expect(
        TEXT("vine on an east side"), W.Place(TEXT("vine"), {5, 0, 0}, EFoundationFace::East), TEXT("vine[west=true]"));
    TestTrue(TEXT("vine on a top face holds no face"),
        FoundationPlantRules::HasNoFace(R, W.Place(TEXT("vine"), {5, 0, 0}, EFoundationFace::Up)));
    Expect(
        TEXT("vine on an underside"), W.Place(TEXT("vine"), {5, 0, 0}, EFoundationFace::Down), TEXT("vine[up=true]"));
    Expect(TEXT("glow lichen on a top face"), W.Place(TEXT("glow_lichen"), {5, 0, 0}, EFoundationFace::Up),
        TEXT("glow_lichen[down=true]"));
    Expect(TEXT("glow lichen on an underside"), W.Place(TEXT("glow_lichen"), {5, 0, 0}, EFoundationFace::Down),
        TEXT("glow_lichen[up=true]"));
    Expect(TEXT("sculk vein on a west side"), W.Place(TEXT("sculk_vein"), {5, 0, 0}, EFoundationFace::West),
        TEXT("sculk_vein[east=true]"));
    TestFalse(TEXT("a vine with a face has one"), FoundationPlantRules::HasNoFace(R, W.State(TEXT("vine[up=true]"))));
    TestFalse(TEXT("other blocks are not multiface"), FoundationPlantRules::HasNoFace(R, W.State(TEXT("dandelion"))));
    TestFalse(TEXT("pale moss carpet is not multiface"),
        FoundationPlantRules::HasNoFace(R, W.State(TEXT("pale_moss_carpet"))));

    // Pointed dripstone: up on a top face, down under a block, and on a side against the vertical look.
    auto Direction = [&](EFoundationFace Face, double Pitch)
    { return R.GetValue(W.Place(TEXT("pointed_dripstone"), {5, 0, 0}, Face, 0, Pitch), TEXT("vertical_direction")); };
    TestEqual(TEXT("dripstone on a top face"), Direction(EFoundationFace::Up, -30), FName(TEXT("up")));
    TestEqual(TEXT("dripstone under a block"), Direction(EFoundationFace::Down, 30), FName(TEXT("down")));
    TestEqual(TEXT("dripstone on a side looking down"), Direction(EFoundationFace::North, 30), FName(TEXT("up")));
    TestEqual(TEXT("dripstone on a side looking up"), Direction(EFoundationFace::North, -30), FName(TEXT("down")));

    // Waterlogged from a water-source destination.
    W.Put({12, 0, 0}, TEXT("stone"));
    TestEqual(TEXT("sea pickle into water is waterlogged"),
        R.GetValue(W.Place(TEXT("sea_pickle"), {12, 0, 0}, EFoundationFace::Up), TEXT("waterlogged")),
        FName(TEXT("true")));
    TestEqual(TEXT("sea pickle in air is not"),
        R.GetValue(W.Place(TEXT("sea_pickle"), {5, 0, 0}, EFoundationFace::Up), TEXT("waterlogged")),
        FName(TEXT("false")));
    TestEqual(TEXT("glow lichen into water is waterlogged"),
        R.GetValue(W.Place(TEXT("glow_lichen"), {12, 0, 0}, EFoundationFace::Up), TEXT("waterlogged")),
        FName(TEXT("true")));

    // The standing-and-wall pairs are the reference's (facts/block_support.json, "items"), and no other: the torch's
    // from DA_Blocks, the coral fans' from FoundationPlantRules (Jesus's answer to Q7).
    FString SupportJson;
    TSharedPtr<FJsonObject> Support;
    if (FFileHelper::LoadFileToString(
            SupportJson, *(FPaths::ProjectDir() / TEXT("Reference/minecraft-blocks-26.3/facts/block_support.json"))))
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SupportJson), Support);
    if (!TestValid(TEXT("block_support.json"), Support)) return false;
    int32 Matched = 0, WithWall = 0;
    for (const TSharedPtr<FJsonValue>& Value : Support->GetArrayField(TEXT("items")))
    {
        const FFoundationBlockType* Standing = W.World.FindBlock(*Value->AsObject()->GetStringField(TEXT("standing")));
        Matched += Standing && Standing->ItemWallBlock == FName(*Value->AsObject()->GetStringField(TEXT("wall")));
    }
    for (int32 Index = 0; Index < R.GetBlockCount(); ++Index)
        WithWall += !R.GetBlockAt(Index).ItemWallBlock.IsNone();
    TestEqual(TEXT("the reference's standing-and-wall pairs"), Matched, Support->GetArrayField(TEXT("items")).Num());
    TestEqual(TEXT("blocks with a wall block"), WithWall, Matched);
    return true;
}

// Every block takes the facts' replaceable flag (Jesus's answer 4), except snow: one layer only (Jesus's answer to
// scout H's Q5).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationPlantReplaceableTest, "Unnamed.Foundation.Blocks.Replaceable", FoundationPlantTests::PlantTestFlags)

bool FFoundationPlantReplaceableTest::RunTest(const FString& Parameters)
{
    using namespace FoundationPlantTests;
    FPlantWorld W;
    FString Text;
    TSharedPtr<FJsonObject> Facts;
    const FString Path = FPaths::ProjectDir() / TEXT("Reference/minecraft-blocks-26.3/facts/block_state_facts.json");
    if (FFileHelper::LoadFileToString(Text, *Path))
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Facts);
    if (!TestValid(TEXT("block_state_facts.json"), Facts)) return false;
    int32 Blocks = 0, Replaceable = 0, Mismatches = 0;
    for (const TSharedPtr<FJsonValue>& Value : Facts->GetArrayField(TEXT("blocks")))
    {
        const TSharedPtr<FJsonObject> Entry = Value->AsObject();
        const FString Id = Entry->GetStringField(TEXT("id"));
        const FFoundationBlockType* Block = W.World.FindBlock(*Id);
        if (!Block)
        {
            AddError(Id + TEXT(" is not in the registry"));
            continue;
        }
        ++Blocks;
        // The flag is constant per block (Blocks.Shapes checks every model state).
        const bool bFact = Entry->GetArrayField(TEXT("states"))[0]->AsObject()->GetBoolField(TEXT("replaceable"));
        Replaceable += bFact;
        if (Id == TEXT("snow"))
        {
            TestTrue(TEXT("snow is replaceable in the facts"), bFact);
            TestFalse(TEXT("snow's own flag stays false"), Block->bReplaceable);
            for (int32 Layers = 1; Layers <= 8; ++Layers)
                TestEqual(FString::Printf(TEXT("snow[layers=%d] is replaceable"), Layers),
                    FoundationBlockPlacement::IsReplaceable(
                        W.World, W.State(*FString::Printf(TEXT("snow[layers=%d]"), Layers))),
                    Layers == 1);
            continue;
        }
        if (Block->bReplaceable != bFact && ++Mismatches <= 20)
            AddError(Id + TEXT(" replaceable differs from the facts"));
    }
    TestEqual(TEXT("facts blocks (block_state_facts.json, B1's families included)"), Blocks, 928);
    TestEqual(TEXT("replaceable in the facts (water, lava, snow and 17 model blocks with B1's fire and soul fire)"),
        Replaceable, 20);
    TestEqual(TEXT("blocks whose flag differs from the facts"), Mismatches, 0);
    TestTrue(TEXT("air is replaceable"), W.World.GetBlock(FFoundationBlockState{}).bReplaceable);
    // A placed block replaces one snow layer, but snow does not (stacking layers is not built), and two layers stay.
    const FFoundationPlacementContext Context;
    const FFoundationBlockType& Stone = *W.World.FindBlock(TEXT("stone"));
    const FFoundationBlockType& Snow = *W.World.FindBlock(TEXT("snow"));
    using FoundationBlockPlacement::CanBeReplaced;
    TestTrue(TEXT("stone replaces one layer"),
        CanBeReplaced(W.World, W.State(TEXT("snow[layers=1]")), Stone, Context, false));
    TestFalse(TEXT("snow does not"), CanBeReplaced(W.World, W.State(TEXT("snow[layers=1]")), Snow, Context, false));
    TestFalse(TEXT("stone does not replace two layers"),
        CanBeReplaced(W.World, W.State(TEXT("snow[layers=2]")), Stone, Context, false));
    return true;
}

// foundation.Blocks.Shapes (survival B2a): the block world's gather also reads the cells just below and above a region
// and keeps the boxes that leave them into it: a fence's 1.5-tall post above its cell, the pitcher crop's lower half
// 1/16 below its cell. With the switch at 0 the gather is B1's: exactly the boxes of the region's own cells, in order.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationShapesGatherTest, "Unnamed.Foundation.Blocks.ShapesGather", FoundationPlantTests::PlantTestFlags)

bool FFoundationShapesGatherTest::RunTest(const FString& Parameters)
{
    using namespace FoundationPlantTests;
    IConsoleVariable* Shapes = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Blocks.Shapes"));
    if (!TestNotNull(TEXT("foundation.Blocks.Shapes exists"), Shapes)) return false;
    const int32 Saved = Shapes->GetInt();
    FPlantWorld W;
    const FIntVector Fence(2, 2, 1), Pitcher(6, 2, 2), Wall(9, 2, 1), Gate(12, 2, 1);
    W.Put(Fence, TEXT("oak_fence"));
    W.Put(Pitcher, TEXT("pitcher_crop[age=0,half=lower]"));
    W.Put(Wall, TEXT("cobblestone_wall"));
    W.Put(Gate, TEXT("oak_fence_gate[facing=north,in_wall=false,open=false,powered=false]"));
    W.Put(FIntVector(12, 5, 1), TEXT("oak_fence_gate[facing=north,in_wall=false,open=true,powered=false]"));
    // Floors and neighbours of every kind under and around them.
    for (int32 X = 0; X < 16; ++X)
        for (int32 Y = 0; Y < 8; ++Y)
            W.Put(FIntVector(X, Y, 0), (X + Y) % 3 ? TEXT("stone") : TEXT("oak_slab[type=bottom,waterlogged=false]"));
    W.Put(FIntVector(4, 4, 1), TEXT("bamboo"));
    W.Put(FIntVector(4, 5, 2), TEXT("stone"));
    W.Put(FIntVector(6, 2, 1), TEXT("farmland[moisture=0]"));
    const double Cm = PlantBlockCm;
    auto Gather = [&W, Shapes](int32 Value, const FBox& Region)
    {
        Shapes->Set(Value, ECVF_SetByCode);
        TArray<FBox> Out;
        W.World.GatherCollision(Region, Out);
        return Out;
    };
    // B1's gather: every solid cell the region's cells cover, in storage order.
    auto Cells = [&W](const FBox& Region)
    {
        TArray<FBox> Out;
        const FIntVector Low = FoundationGrid::CellAt(Region.Min, W.World.OriginCm),
                         High = FoundationGrid::CellAt(Region.Max, W.World.OriginCm);
        W.World.GetStorage().ForEachSolid(Low, High, [&W, &Out](const FIntVector& Cell, const FFoundationCell& Value)
            { W.World.GetCollisionBoxes(Cell, Value.State, Out); });
        return Out;
    };
    auto Same = [](const TArray<FBox>& A, const TArray<FBox>& B)
    {
        if (A.Num() != B.Num()) return false;
        for (int32 Index = 0; Index < A.Num(); ++Index)
            if (!SamePlantBox(A[Index], B[Index])) return false;
        return true;
    };
    auto Above = [&W, Cm](const FIntVector& Cell, double Bottom, double Top)
    {
        const FVector Min = FoundationGrid::CellMin(Cell, W.World.OriginCm);
        return FBox(Min + FVector(1, 1, Bottom * Cm), Min + FVector(Cm - 1, Cm - 1, Top * Cm));
    };
    // The regions a body sweeps over: just above each block, inside the cells, across all of them.
    const FBox Regions[] = {Above(Fence + FIntVector(0, 0, 1), 0, .4), Above(Fence + FIntVector(0, 0, 1), .5, .9),
        Above(Pitcher - FIntVector(0, 0, 1), .95, .99), Above(Pitcher - FIntVector(0, 0, 1), .5, .9),
        Above(Wall + FIntVector(0, 0, 1), 0, .4), Above(Gate + FIntVector(0, 0, 1), 0, .4),
        Above(FIntVector(12, 5, 2), 0, .4), FBox(FVector(-50, -50, 20), FVector(1500, 700, 260)),
        FBox(FVector(150, 150, 100), FVector(1000, 300, 230))};
    const int32 Expected[] = {1, 0, 1, 0, 1, 1, 0, -1, -1};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Regions); ++Index)
    {
        const FBox& Region = Regions[Index];
        const TArray<FBox> Off = Gather(0, Region), On = Gather(1, Region), Old = Cells(Region);
        TestTrue(FString::Printf(TEXT("region %d, switch 0: B1's boxes exactly (%d, %d)"), Index, Off.Num(), Old.Num()),
            Same(Off, Old));
        // Switch 1 adds only boxes outside the region's cells that overlap it.
        int32 Added = 0;
        bool bSuperset = true;
        for (const FBox& Box : Off)
            bSuperset &= On.ContainsByPredicate([&Box](const FBox& Other) { return SamePlantBox(Box, Other); });
        for (const FBox& Box : On)
            if (!Off.ContainsByPredicate([&Box](const FBox& Other) { return SamePlantBox(Box, Other); }))
            {
                ++Added;
                // It overlaps the region with volume, and it crosses a cell boundary vertically (it leaves its cell).
                const bool bVolume = Box.Min.X < Region.Max.X && Box.Max.X > Region.Min.X && Box.Min.Y < Region.Max.Y &&
                    Box.Max.Y > Region.Min.Y && Box.Min.Z < Region.Max.Z && Box.Max.Z > Region.Min.Z;
                const bool bCrosses = FMath::FloorToInt((Box.Min.Z - W.World.OriginCm.Z) / Cm + 1e-6) !=
                    FMath::FloorToInt((Box.Max.Z - W.World.OriginCm.Z) / Cm - 1e-6);
                bSuperset &= bVolume && bCrosses;
            }
        TestTrue(FString::Printf(TEXT("region %d, switch 1: B1's boxes and overlapping ones"), Index), bSuperset);
        if (Expected[Index] >= 0)
            TestEqual(FString::Printf(TEXT("region %d, switch 1: boxes added from the cells below and above"), Index),
                Added, Expected[Index]);
    }
    // The fence's post reaches y 1.5: a body standing on the fence's own cell top is held 50 cm higher.
    const TArray<FBox> Post = Gather(1, Regions[0]);
    TestTrue(TEXT("the fence's gathered post tops out at 1.5 blocks"),
        Post.Num() == 1 &&
            FMath::IsNearlyEqual(Post[0].Max.Z, FoundationGrid::CellMin(Fence, W.World.OriginCm).Z + 1.5 * Cm, 1e-3));
    // A region whose bottom touches the post's top only gathers nothing (no volume in common).
    TestEqual(TEXT("touching the post's top gathers nothing"), Gather(1, Regions[1]).Num(), 0);
    Shapes->Set(Saved, ECVF_SetByCode);
    return true;
}
#endif
