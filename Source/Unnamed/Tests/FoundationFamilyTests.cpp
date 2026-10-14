#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockAssets.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationItemAssets.h"
#include "Foundation/FoundationPlayground.h"
#include "Foundation/FoundationProfile.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Voxel/VoxelBlocks.h"

// Survival B1 (Docs/Records/2026-10-14-Blocks-Plan.md, section 2): the block families appended to DA_Blocks behind
// foundation.Blocks.Families. They fail until the extended blocks reference is imported.
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags FamilyTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // The 302 family blocks plus the PM's 8 (cauldrons, fires, campfires).
    constexpr int32 B1Blocks = 310;

    // The ids the blocks reference marks added: "survival-B1", in manifest order.
    TArray<FName> ReadFamilyIds()
    {
        FString Text;
        TSharedPtr<FJsonObject> Json;
        const FString Path = FPaths::ProjectDir() / TEXT("Reference/minecraft-blocks-26.3/block_manifest.json");
        TArray<FName> Ids;
        if (!FFileHelper::LoadFileToString(Text, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
            return Ids;
        for (const TSharedPtr<FJsonValue>& Value : Json->GetArrayField(TEXT("blocks")))
        {
            const TSharedPtr<FJsonObject> Block = Value->AsObject();
            FString Added;
            if (Block->TryGetStringField(TEXT("added"), Added) && Added == TEXT("survival-B1"))
                Ids.Add(FName(Block->GetStringField(TEXT("id"))));
        }
        return Ids;
    }

    // Sets foundation.Blocks.Families for a test and restores it after.
    struct FFamiliesSwitch
    {
        IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Blocks.Families"));
        int32 Saved = Variable ? Variable->GetInt() : 1;

        void Set(int32 Value) const
        {
            if (Variable) Variable->Set(Value, ECVF_SetByCode);
        }
        ~FFamiliesSwitch() { Set(Saved); }
    };

    // An authority with the project's blocks and rules and one player far from the cells it places in.
    struct FFamilyLab
    {
        FFoundationAuthority A;
        int32 Player = 0;

        FFamilyLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.PlantSurvival = VoxelBlocks::GetPlantSurvival(A.World.GetRegistry());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bInfiniteSupply = false;
            Player = A.AddPlayer();
            A.Reset();
            A.FindPlayer(Player)->Inventory.Select(3);
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = FVector(-1500, -1500, 0);
            Facts.HeightCm = 180;
            Facts.EyeCm = Facts.FeetCm + FVector(0, 0, 162);
            Facts.Aim = FVector(1, 0, 0);
            Facts.bGrounded = true;
            Facts.Step = 1;
            A.SetFacts(Player, Facts);
        }

        // Holding the item that places Block, a click on the top of a grass block; false when no item places it.
        bool Place(const FFoundationBlockType& Block, EFoundationReason& OutReason, FFoundationBlockState& OutPlaced)
        {
            const FFoundationItemDefinition* Item = A.Rules.Items.FindByPredicate(
                [&Block](const FFoundationItemDefinition& Candidate) { return Candidate.PlacesBlock == Block.Id; });
            if (!Item) return false;
            FFoundationPlayer& P = *A.FindPlayer(Player);
            P.Inventory.SetSlot(3, P.Inventory.MakeStack(A.Rules.FindItem(Item->Id), 1));
            const FIntVector Support(0, 0, 1);
            FFoundationBlockState Grass;
            A.World.GetRegistry().Parse(TEXT("grass_block[snowy=false]"), Grass);
            A.World.Set(Support, Grass);
            A.World.Set(Support + FIntVector(0, 0, 1), FFoundationBlockState());
            FFoundationHit Hit;
            Hit.Kind = EFoundationHitKind::Block;
            Hit.Face = EFoundationFace::Up;
            Hit.Cell = Support;
            Hit.Point = FoundationGrid::CellBox(Support, A.World.OriginCm).GetCenter() +
                FVector(0, 0, UFoundationProfile::BlockCm * .5);
            Hit.bInRange = true;
            FIntVector Destination;
            OutPlaced = {};
            OutReason = A.CheckPlacement(P, EFoundationHand::Main, Hit, Destination, &OutPlaced);
            return true;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationFamiliesPresentTest, "Unnamed.Foundation.Blocks.Families.Present", FamilyTestFlags)
bool FFoundationFamiliesPresentTest::RunTest(const FString& Parameters)
{
    const TArray<FName> Ids = ReadFamilyIds();
    TestEqual(TEXT("the blocks reference marks B1Blocks blocks added: survival-B1"), Ids.Num(), B1Blocks);
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    const UFoundationBlockAssets* Assets = Registry->GetAssets();
    if (!TestNotNull(TEXT("DA_Blocks"), Assets)) return false;
    const TSet<FName> Family(Ids);
    int32 Flagged = 0;
    for (const FFoundationBlockAsset& Block : Assets->Blocks)
    {
        Flagged += Block.bAppendedFamily;
        if (Block.bAppendedFamily != Family.Contains(Block.Id))
            AddError(FString::Printf(TEXT("%s: bAppendedFamily %d, the reference says %d"), *Block.Id.ToString(),
                Block.bAppendedFamily, Family.Contains(Block.Id)));
    }
    TestEqual(TEXT("DA_Blocks' flagged blocks"), Flagged, B1Blocks);
    for (const FName Id : Ids)
    {
        const FFoundationBlockType* Block = Registry->FindBlock(Id);
        if (!Block || !Block->bAppendedFamily)
            AddError(FString::Printf(TEXT("%s is not a family block of the registry"), *Id.ToString()));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationFamiliesPlaceTest, "Unnamed.Foundation.Blocks.Families.Place", FamilyTestFlags)
bool FFoundationFamiliesPlaceTest::RunTest(const FString& Parameters)
{
    FFamiliesSwitch Switch;
    FFamilyLab L;
    const FFoundationBlockRegistry& Registry = L.A.World.GetRegistry();
    int32 Families = 0, Placed = 0, Refused = 0, NoItem = 0;
    for (int32 Index = 1; Index < Registry.GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Block = Registry.GetBlockAt(Index);
        if (!Block.bAppendedFamily) continue;
        ++Families;
        EFoundationReason Reason;
        FFoundationBlockState State;
        Switch.Set(1);
        if (!L.Place(Block, Reason, State))
        {
            ++NoItem;
            continue;
        }
        if (Reason == EFoundationReason::None && State == Block.GetDefaultState())
            ++Placed;
        else
            AddError(FString::Printf(TEXT("%s with the switch on: reason %d, placed %s"), *Block.Id.ToString(),
                int32(Reason), *Registry.ToString(State)));
        Switch.Set(0);
        L.Place(Block, Reason, State);
        if (Reason == EFoundationReason::UnknownBlock)
            ++Refused;
        else
            AddError(FString::Printf(TEXT("%s with the switch off: reason %d"), *Block.Id.ToString(), int32(Reason)));
    }
    AddInfo(FString::Printf(TEXT("%d family blocks: %d placed their default state with the switch on, %d refused "
                                 "with it off, %d have no item"),
        Families, Placed, Refused, NoItem));
    TestEqual(TEXT("family blocks in the registry"), Families, B1Blocks);
    TestTrue(TEXT("every family block with an item placed"), Placed == Families - NoItem && Refused == Placed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationFamiliesChestTest, "Unnamed.Foundation.Blocks.Families.ChestStandIn", FamilyTestFlags)
bool FFoundationFamiliesChestTest::RunTest(const FString& Parameters)
{
    // The chests' stand-in: one 14/16 box element, [1, 0, 1] to [15, 14, 15] in model units (Unreal: Z up).
    const FBox Expected(FVector(1, 1, 0) / 16, FVector(15, 15, 14) / 16);
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    for (const TCHAR* Id : {TEXT("chest"), TEXT("trapped_chest"), TEXT("ender_chest")})
    {
        const FFoundationBlockType* Block = Registry->FindBlock(Id);
        TArray<TConstArrayView<int32>> Parts;
        if (!Block || !Registry->GetModelParts(Block->GetDefaultState(), Parts))
        {
            AddError(FString::Printf(TEXT("%s: no model block"), Id));
            continue;
        }
        FBox Bounds(ForceInit);
        int32 Quads = 0;
        for (const TConstArrayView<int32>& Part : Parts)
            for (const int32 Model : Part)
                for (const FFoundationModelQuad& Quad : Registry->GetModelQuads(Model))
                {
                    ++Quads;
                    for (const FVector3f& Position : Quad.Positions)
                        Bounds += FVector(Position);
                }
        TestTrue(FString::Printf(TEXT("%s: one model of one box (%d parts, %d quads)"), Id, Parts.Num(), Quads),
            Parts.Num() == 1 && Parts[0].Num() == 1 && Quads >= 1 && Quads <= 6);
        TestTrue(FString::Printf(TEXT("%s: the box is %s"), Id, *Expected.ToString()),
            Bounds.IsValid && Bounds.Min.Equals(Expected.Min, 1e-4) && Bounds.Max.Equals(Expected.Max, 1e-4));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationFamiliesGalleryTest, "Unnamed.Foundation.Blocks.Families.GalleryTails", FamilyTestFlags)
bool FFoundationFamiliesGalleryTest::RunTest(const FString& Parameters)
{
    using FCell = AFoundationPlayground::FGalleryCell;
    FFamiliesSwitch Switch;
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    TArray<FFoundationBlockState> Bases, Families;
    int32 BaseStates = 0;
    for (int32 Index = 1; Index < Registry->GetBlockCount(); ++Index)
    {
        const FFoundationBlockType& Block = Registry->GetBlockAt(Index);
        (Block.bAppendedFamily ? Families : Bases).Add(Block.GetDefaultState());
        BaseStates += Block.bAppendedFamily ? 0 : Block.StateCount;
    }
    TestEqual(TEXT("family blocks in the registry"), Families.Num(), B1Blocks);

    TArray<FCell> On[2], Off[2];
    Switch.Set(1);
    AFoundationPlayground::GetGalleryCells(*Registry, On[0], On[1]);
    Switch.Set(0);
    AFoundationPlayground::GetGalleryCells(*Registry, Off[0], Off[1]);
    const auto Tail = [](const TArray<FCell>& Cells)
    { return Cells.FilterByPredicate([](const FCell& C) { return C.bTail; }); };
    const auto Base = [](const TArray<FCell>& Cells)
    { return Cells.FilterByPredicate([](const FCell& C) { return !C.bTail; }); };
    const FIntVector Origins[2] = {
        AFoundationPlayground::RegistryGalleryOrigin, AFoundationPlayground::StateGalleryOrigin};
    const int32 Columns[2] = {
        AFoundationPlayground::RegistryGalleryColumns, AFoundationPlayground::StateGalleryColumns};
    const int32 BaseCounts[2] = {Bases.Num(), BaseStates};
    for (int32 Gallery = 0; Gallery < 2; ++Gallery)
    {
        const TCHAR* Name = Gallery == 0 ? TEXT("registry gallery") : TEXT("state gallery");
        const TArray<FCell> BaseOn = Base(On[Gallery]), BaseOff = Base(Off[Gallery]), TailOn = Tail(On[Gallery]);
        TestEqual(FString::Printf(TEXT("%s: tail with the switch on"), Name), TailOn.Num(), Families.Num());
        TestEqual(FString::Printf(TEXT("%s: tail with the switch off"), Name), Tail(Off[Gallery]).Num(), 0);
        TestEqual(FString::Printf(TEXT("%s: cells before B1"), Name), BaseOn.Num(), BaseCounts[Gallery]);
        // The cells before B1 sit in order from the origin, whatever the switch; the registry gallery's hold each
        // block's default state, the state gallery's every state of those blocks.
        bool bSame = BaseOn.Num() == BaseOff.Num(), bInPlace = true;
        for (int32 Index = 0; Index < BaseOn.Num(); ++Index)
        {
            bSame = bSame && BaseOn[Index].Cell == BaseOff[Index].Cell && BaseOn[Index].State == BaseOff[Index].State;
            bInPlace = bInPlace &&
                BaseOn[Index].Cell ==
                    Origins[Gallery] + FIntVector(Index % Columns[Gallery] * 2, Index / Columns[Gallery] * 2, 0) &&
                (Gallery == 1 || BaseOn[Index].State == Bases[Index]);
        }
        TestTrue(FString::Printf(TEXT("%s: cells before B1 unchanged by the switch"), Name), bSame);
        TestTrue(FString::Printf(TEXT("%s: cells before B1 in place"), Name), bInPlace);
        // The tail: the family blocks' default states in registry order, from the row after the last cell before B1.
        const int32 First = FMath::DivideAndRoundUp(BaseOn.Num(), Columns[Gallery]) * Columns[Gallery];
        bool bTail = TailOn.Num() == Families.Num();
        for (int32 Index = 0; bTail && Index < TailOn.Num(); ++Index)
            bTail = TailOn[Index].State == Families[Index] &&
                TailOn[Index].Cell ==
                    Origins[Gallery] +
                        FIntVector((First + Index) % Columns[Gallery] * 2, (First + Index) / Columns[Gallery] * 2, 0);
        TestTrue(FString::Printf(TEXT("%s: the tail's cells"), Name), bTail);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationFamiliesItemIdsTest, "Unnamed.Foundation.Blocks.Families.ItemIds", FamilyTestFlags)
bool FFoundationFamiliesItemIdsTest::RunTest(const FString& Parameters)
{
    // An item id is DA_Items' or a DA_Blocks block's, never both (the saplings stay DA_Items' and place their block).
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    const UFoundationItemAssets* Items = UFoundationItemAssets::LoadDefault();
    if (!TestNotNull(TEXT("DA_Blocks"), Registry->GetAssets()) || !TestNotNull(TEXT("DA_Items"), Items)) return false;
    TSet<FName> BlockItems;
    for (const FFoundationBlockAsset& Block : Registry->GetAssets()->Blocks)
        if (!Block.Item.IsNone()) BlockItems.Add(Block.Item);
    int32 Both = 0, Saplings = 0;
    for (const FFoundationItemAsset& Item : Items->Items)
    {
        if (BlockItems.Contains(Item.Id))
        {
            ++Both;
            AddError(FString::Printf(TEXT("%s is in DA_Items and DA_Blocks"), *Item.Id.ToString()));
        }
        if (Item.Id.ToString().EndsWith(TEXT("_sapling")))
        {
            Saplings += Item.PlacesBlock == Item.Id && Registry->FindBlock(Item.Id);
            if (Item.PlacesBlock != Item.Id)
                AddError(FString::Printf(TEXT("%s places %s"), *Item.Id.ToString(), *Item.PlacesBlock.ToString()));
        }
    }
    TestEqual(TEXT("item ids in both"), Both, 0);
    TestEqual(TEXT("sapling items placing their block"), Saplings, 9);
    return true;
}
#endif
