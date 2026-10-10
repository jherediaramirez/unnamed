#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockPlacement.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags TorchTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    TSharedPtr<FJsonObject> ReadTorchReference(const TCHAR* Name)
    {
        FString Text;
        TSharedPtr<FJsonObject> Json;
        const FString Path = FPaths::ProjectDir() / TEXT("Reference/minecraft-blocks-26.3") / Name;
        if (FFileHelper::LoadFileToString(Text, *Path))
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
        return Json;
    }

    struct FTorchTestWorld
    {
        FFoundationBlockWorld World;

        FTorchTestWorld()
        {
            World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            World.BoundsMin = FIntVector(-8);
            World.BoundsMax = FIntVector(7);
        }

        FFoundationBlockState State(const TCHAR* Text) const
        {
            FFoundationBlockState State;
            ensureMsgf(World.GetRegistry().Parse(Text, State), TEXT("parse %s"), Text);
            return State;
        }

        // The torch item's block for a click on Face of the cell below or beside Destination, placed and checked.
        bool Survives(const TCHAR* Support, EFoundationFace Face)
        {
            const FIntVector Destination = FoundationGrid::FaceOffset(Face);
            World.Set(FIntVector::ZeroValue, State(Support));
            World.Set(Destination, FFoundationBlockState());
            FFoundationPlacementContext Context;
            Context.ClickedFace = Face;
            Context.Destination = Destination;
            const FName Id = FoundationBlockPlacement::GetItemBlock(TEXT("torch"), TEXT("wall_torch"), Face);
            const FFoundationBlockState Placed =
                FoundationBlockPlacement::GetStateForPlacement(World, *World.FindBlock(Id), Context);
            return FoundationBlockPlacement::HasTorchSupport(World, Placed, Destination, Face);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBlocksSupportTest, "Unnamed.Foundation.Blocks.Support", TorchTestFlags)

bool FFoundationBlocksSupportTest::RunTest(const FString&)
{
    // Every imported state holds a torch on exactly the faces facts/block_support.json measured.
    const TSharedPtr<FJsonObject> Support = ReadTorchReference(TEXT("facts/block_support.json"));
    if (!TestTrue(TEXT("block_support.json"), Support.IsValid())) return false;
    const FFoundationBlockRegistry& Registry = *FFoundationBlockRegistry::GetDefault();
    int32 States = 0, Different = 0;
    for (const TSharedPtr<FJsonValue>& Value : Support->GetArrayField(TEXT("blocks")))
    {
        const TSharedPtr<FJsonObject> Block = Value->AsObject();
        const int32 First = int32(Block->GetNumberField(TEXT("first_state")));
        const TArray<TSharedPtr<FJsonValue>>& Masks = Block->GetArrayField(TEXT("support"));
        for (int32 Index = 0; Index < Masks.Num(); ++Index, ++States)
        {
            FFoundationBlockState State;
            State.Id = uint16(First + Index);
            if (Registry.GetTorchSupport(State) != uint8(Masks[Index]->AsNumber()) && ++Different <= 5)
                AddError(FString::Printf(TEXT("%s state %d holds a torch on mask %d, not %d"),
                    *Block->GetStringField(TEXT("id")), State.Id, Registry.GetTorchSupport(State),
                    int32(Masks[Index]->AsNumber())));
        }
    }
    TestEqual(TEXT("every imported state"), States, 4424);
    TestEqual(TEXT("states whose support differs"), Different, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBlocksTorchTest, "Unnamed.Foundation.Blocks.Torch", TorchTestFlags)

bool FFoundationBlocksTorchTest::RunTest(const FString&)
{
    using namespace FoundationBlockPlacement;
    // One torch item (the wall torch has none); it places the wall torch against a side face.
    FFoundationRules Rules;
    UFoundationInteractionSettings::Get().BuildRules(Rules);
    const FFoundationItemDefinition* Torch =
        Rules.Items.FindByPredicate([](const FFoundationItemDefinition& Item) { return Item.Id == TEXT("torch"); });
    if (!TestNotNull(TEXT("torch item"), Torch)) return false;
    TestTrue(TEXT("torch item: places torch, wall_torch on a side, stacks to 64"),
        Torch->PlacesBlock == TEXT("torch") && Torch->PlacesWallBlock == TEXT("wall_torch") && Torch->MaxStack == 64);
    TestFalse(TEXT("no wall_torch item"),
        Rules.Items.ContainsByPredicate([](const FFoundationItemDefinition& Item)
            { return Item.Id == TEXT("wall_torch") || Item.PlacesBlock == TEXT("wall_torch"); }));
    TestEqual(TEXT("items with a wall block"),
        Rules.Items
            .FilterByPredicate([](const FFoundationItemDefinition& Item) { return !Item.PlacesWallBlock.IsNone(); })
            .Num(),
        1);

    // The block by clicked face: the floor torch on top (and on the underside, which the support check refuses), the
    // wall torch on a side; an item without a wall block always places its own.
    TestTrue(TEXT("top face"), GetItemBlock(TEXT("torch"), TEXT("wall_torch"), EFoundationFace::Up) == TEXT("torch"));
    TestTrue(
        TEXT("underside"), GetItemBlock(TEXT("torch"), TEXT("wall_torch"), EFoundationFace::Down) == TEXT("torch"));
    for (const EFoundationFace Face :
        {EFoundationFace::West, EFoundationFace::East, EFoundationFace::North, EFoundationFace::South})
        TestTrue(TEXT("side face"), GetItemBlock(TEXT("torch"), TEXT("wall_torch"), Face) == TEXT("wall_torch"));
    TestTrue(TEXT("no wall block"), GetItemBlock(TEXT("stone"), NAME_None, EFoundationFace::East) == TEXT("stone"));

    // The wall torch faces the clicked side, away from its wall.
    FTorchTestWorld T;
    const FFoundationBlockType& Wall = *T.World.FindBlock(TEXT("wall_torch"));
    TestTrue(TEXT("wall torch rule"), Wall.Placement == EFoundationPlacementRule::WallAttached);
    for (const EFoundationFace Face :
        {EFoundationFace::West, EFoundationFace::East, EFoundationFace::North, EFoundationFace::South})
    {
        FFoundationPlacementContext Context;
        Context.ClickedFace = Face;
        Context.Destination = FoundationGrid::FaceOffset(Face);
        TestTrue(FString::Printf(TEXT("wall torch on the %s face"), FoundationGrid::FaceName(Face)),
            T.World.GetRegistry().GetValue(GetStateForPlacement(T.World, Wall, Context), TEXT("facing")) ==
                FName(FoundationGrid::FaceName(Face)));
    }

    // Survival from the support table (minecraft.wiki "Torch": leaves are no support; upside-down slabs hold a torch
    // on top). Columns: top, east side, underside.
    struct FCase
    {
        const TCHAR* Support;
        bool bTop, bSide;
    };
    for (const FCase& Case : {FCase{TEXT("stone"), true, true}, FCase{TEXT("glass"), true, true},
             FCase{TEXT("oak_leaves[persistent=true]"), false, false},
             FCase{TEXT("oak_slab[type=bottom]"), false, false}, FCase{TEXT("oak_slab[type=top]"), true, false},
             FCase{TEXT("oak_slab[type=double]"), true, true}, FCase{TEXT("air"), false, false},
             FCase{TEXT("torch"), false, false}})
    {
        TestEqual(FString::Printf(TEXT("torch on top of %s"), Case.Support),
            T.Survives(Case.Support, EFoundationFace::Up), Case.bTop);
        TestEqual(FString::Printf(TEXT("wall torch on the east of %s"), Case.Support),
            T.Survives(Case.Support, EFoundationFace::East), Case.bSide);
        TestFalse(FString::Printf(TEXT("underside of %s refused until T3"), Case.Support),
            T.Survives(Case.Support, EFoundationFace::Down));
    }
    return true;
}
#endif
