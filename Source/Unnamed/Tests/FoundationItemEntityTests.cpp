#include "Misc/AutomationTest.h"
#include "Blocks/VoxelPlantSurvival.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationExperienceOrbs.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationItemEntities.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationSimulation.h"
#include "Foundation/FoundationTags.h"
#include "HAL/IConsoleManager.h"
#include "Voxel/VoxelBlocks.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace ItemEntityTestsLocal
{
    constexpr EFoundationHand MainHand = EFoundationHand::Main;

    FFoundationBlockDefinition MakeLabBlock()
    {
        FFoundationBlockDefinition Block;
        Block.Id = TEXT("lab_block");
        Block.DisplayName = TEXT("Lab Block");
        Block.Hardness = 1.5f;
        Block.Tags = FGameplayTagContainer(FoundationTags::BlockMineablePickaxe);
        Block.DropItem = TEXT("lab_block");
        return Block;
    }

    TSharedRef<const FFoundationBlockRegistry> MakeLabRegistry()
    {
        TArray<FFoundationBlockDefinition> Extra;
        Extra.Add(MakeLabBlock());
        return FFoundationBlockRegistry::Build(FFoundationBlockRegistry::GetDefault()->GetAssets(), Extra);
    }

    // The interaction tests' lab: the project's rules plus the stand-in block, a floor at z 0, one player.
    struct FEntityLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        int32 Player = 0;
        uint32 Sequence = 1;
        int64 StepIndex = 0;

        explicit FEntityLab(bool bInfinite)
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            FFoundationItemDefinition Item;
            Item.Id = TEXT("lab_block");
            Item.DisplayName = TEXT("Lab Block");
            Item.Tags = FGameplayTagContainer(FoundationTags::ItemBlock);
            Item.MaxStack = 64;
            Item.PlacesBlock = TEXT("lab_block");
            Item.Icon = TEXT("block");
            A.Rules.Items.Add(Item);
            A.Rules.Loadout[0] = TEXT("lab_block");
            A.World.SetRegistry(MakeLabRegistry());
            A.PlantSurvival = VoxelBlocks::GetPlantSurvival(A.World.GetRegistry());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            FFoundationStatic Floor;
            Floor.Box = FBox(FVector(-2000, -2000, -100), FVector(2000, 2000, 0));
            Floor.Label = TEXT("floor");
            Floor.bAnchor = true;
            A.Statics.Add(Floor);
            A.Options.bInfiniteSupply = bInfinite;
            A.Options.bToolWear = !bInfinite;
            Player = A.AddPlayer();
            A.Reset();
            Vitals.bNaturalRegeneration = false;
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }

        void Stand(FVector Feet, FVector Aim = FVector(1, 0, 0))
        {
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = Feet;
            Facts.HeightCm = 180;
            Facts.EyeCm = Feet + FVector(0, 0, 162);
            Facts.Aim = Aim.GetSafeNormal();
            Facts.bGrounded = true;
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
        }

        void Facts(TFunctionRef<void(FFoundationPlayerFacts&)> Edit)
        {
            FFoundationPlayerFacts Copy = P().Facts;
            Edit(Copy);
            A.SetFacts(Player, Copy);
        }

        void NextStep()
        {
            Facts([this](FFoundationPlayerFacts& F) { F.Step = ++StepIndex; });
            A.Step();
        }

        void Steps(int32 Count)
        {
            for (int32 Index = 0; Index < Count; ++Index)
                NextStep();
        }

        FFoundationCommand Make(EFoundationCommandType Type)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Type = Type;
            Command.Hand = MainHand;
            Command.Item = P().Inventory.GetHand(MainHand).GetRef();
            Command.Target = A.Resolve(P().Facts);
            FIntVector Destination;
            EFoundationReason Ignored;
            if (A.GetDestination(Command.Target, Destination, Ignored) && A.World.IsKnown(Destination))
            {
                Command.Destination = Destination;
                Command.DestinationRevision = A.World.Get(Destination).Revision;
            }
            return Command;
        }

        FFoundationResult Run(FFoundationCommand Command)
        {
            if (Command.Sequence == 0) Command.Sequence = Sequence++;
            return A.Execute(Player, Command, Vitals);
        }

        void Put(FIntVector Cell) { A.World.Set(Cell, A.World.FindBlock(TEXT("lab_block"))->GetDefaultState()); }
        bool Solid(FIntVector Cell) const { return !A.World.Get(Cell).State.IsAir(); }
        int32 LabItem() const { return A.Rules.FindItem(TEXT("lab_block")); }

        // The first entity with this id, or null.
        const FFoundationItemEntity* Find(int32 Id) const
        {
            return A.GetItemEntities().FindByPredicate([Id](const FFoundationItemEntity& E) { return E.Id == Id; });
        }

        // Fills every hotbar slot but 0 and the offhand with other items (a full inventory for lab_block).
        void FillOthers()
        {
            for (int32 Slot = 1; Slot < FFoundationInventory::HotbarSlots; ++Slot)
                P().Inventory.SetSlot(Slot, P().Inventory.MakeStack(1, 1));
            P().Inventory.Offhand = P().Inventory.MakeStack(2, 1);
        }
    };

    // Mines the targeted block through the command path; returns the last result and sets Steps (negative: refused).
    FFoundationResult MineResult(FEntityLab& L, int32& OutSteps, int32 Limit = 2000)
    {
        FFoundationResult R = L.Run(L.Make(EFoundationCommandType::MineStart));
        OutSteps = 0;
        if (R.bBroken) return R;
        for (int32 Step = 1; Step <= Limit; ++Step)
        {
            R = L.Run(L.Make(EFoundationCommandType::MineContinue));
            if (R.bBroken)
            {
                OutSteps = Step;
                return R;
            }
            if (!R.IsCommitted())
            {
                OutSteps = -Step;
                return R;
            }
            L.NextStep();
        }
        OutSteps = -1;
        return R;
    }

    // Every hotbar slot and the offhand as (item, count).
    TArray<TPair<int32, int32>> Snapshot(const FEntityLab& L)
    {
        TArray<TPair<int32, int32>> Out;
        const FFoundationInventory& Inventory = L.A.FindPlayer(L.Player)->Inventory;
        for (const FFoundationItemInstance& Slot : Inventory.Hotbar)
            Out.Emplace(Slot.IsEmpty() ? INDEX_NONE : Slot.Item, Slot.IsEmpty() ? 0 : Slot.Count);
        Out.Emplace(Inventory.Offhand.IsEmpty() ? INDEX_NONE : Inventory.Offhand.Item,
            Inventory.Offhand.IsEmpty() ? 0 : Inventory.Offhand.Count);
        return Out;
    }

    int32 CountItem(const FEntityLab& L, int32 Item)
    {
        int32 Total = 0;
        for (const TPair<int32, int32>& Slot : Snapshot(L))
            if (Slot.Key == Item) Total += Slot.Value;
        return Total;
    }

    // A lab with a full inventory (as the interaction tests build it), holding the pickaxe.
    void MakeFull(FEntityLab& L)
    {
        L.P().Inventory.Select(1);
        for (int32 Slot = 3; Slot < 9; ++Slot)
            L.P().Inventory.SetSlot(Slot, L.P().Inventory.MakeStack(1, 1));
        L.P().Inventory.Hotbar[0].Count = 64;
        L.P().Inventory.Offhand = L.P().Inventory.MakeStack(2, 1);
    }

    // Block storage with columns that can be marked unloaded (streamed worlds).
    class FColumnStorage final : public IFoundationBlockStorage
    {
    public:
        TSet<FIntPoint> Unloaded;

        virtual FFoundationCell Get(const FIntVector& Cell) const override { return Inner.Get(Cell); }
        virtual void Set(const FIntVector& Cell, const FFoundationCell& Value) override { Inner.Set(Cell, Value); }
        virtual void Empty() override { Inner.Empty(); }
        virtual int32 GetSolidCount() const override { return Inner.GetSolidCount(); }
        virtual void ForEachSolid(const FIntVector& Min, const FIntVector& Max,
            TFunctionRef<void(const FIntVector&, const FFoundationCell&)> Visit) const override
        {
            Inner.ForEachSolid(Min, Max, Visit);
        }
        virtual SIZE_T GetAllocatedSize() const override { return Inner.GetAllocatedSize(); }
        virtual bool IsLoaded(const FIntVector& Cell) const override
        {
            return !Unloaded.Contains(
                FIntPoint(FoundationGrid::FloorDivide(Cell.X, 16), FoundationGrid::FloorDivide(Cell.Y, 16)));
        }

    private:
        FFoundationSectionStorage Inner;
    };

    // Whether an item resting at Pos is picked up by a player standing at Feet (delay 0), optionally on a shelf whose
    // top is ShelfTopZ.
    bool PickedAt(const FVector& Pos, const FVector& Feet, const double* ShelfTopZ = nullptr)
    {
        FEntityLab L(false);
        if (ShelfTopZ)
        {
            FFoundationStatic Shelf;
            Shelf.Box = FBox(FVector(Pos.X - 50, Pos.Y - 50, -10), FVector(Pos.X + 50, Pos.Y + 50, *ShelfTopZ));
            Shelf.Label = TEXT("shelf");
            L.A.Statics.Add(Shelf);
        }
        L.Stand(Feet);
        L.A.SpawnItem(L.LabItem(), 1, Pos, FVector::ZeroVector, 0);
        L.NextStep();
        return L.A.GetItemEntities().IsEmpty();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemEntitySpawnTest, "Unnamed.Foundation.Items.Entities.Spawn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationItemEntitySpawnTest::RunTest(const FString&)
{
    using namespace ItemEntityTestsLocal;
    auto Break = [](FEntityLab& L, FFoundationResult& R, int32& Steps)
    {
        L.A.Options.bItemEntities = true;
        L.Put({0, 0, 0});
        L.Stand({-150, 50, 0}, FVector(150, 0, -112));
        R = MineResult(L, Steps);
    };
    FEntityLab L(false);
    FFoundationResult R;
    int32 Steps = 0;
    L.A.Options.bItemEntities = true;
    L.Put({0, 0, 0});
    L.Stand({-150, 50, 0}, FVector(150, 0, -112));
    const TArray<TPair<int32, int32>> Before = Snapshot(L);
    R = MineResult(L, Steps);
    TestTrue(TEXT("committed"), R.IsCommitted());
    TestTrue(TEXT("broken"), R.bBroken);
    TestTrue(TEXT("one lab_block drop"), R.Drops.Num() == 1 && R.Drops[0].Item == L.LabItem() && R.Drops[0].Count == 1);
    TestFalse(TEXT("not returned to the inventory"), R.bItemReturned);
    TestTrue(TEXT("hotbar unchanged"), Snapshot(L) == Before);
    TestEqual(TEXT("one entity"), L.A.GetItemEntities().Num(), 1);
    TestEqual(TEXT("ItemsSpawned"), L.A.ItemsSpawned, 1);
    if (L.A.GetItemEntities().Num() != 1) return false;
    const FFoundationItemEntity E = L.A.GetItemEntities()[0];
    TestEqual(TEXT("item"), E.Item, L.LabItem());
    TestEqual(TEXT("count"), E.Count, 1);
    TestEqual(TEXT("pickup delay"), E.PickupDelay, 10);
    TestEqual(TEXT("age"), E.Age, 0);
    TestTrue(TEXT("X in the cell's spread"), E.LocalCm.X >= 25 && E.LocalCm.X <= 75);
    TestTrue(TEXT("Y in the cell's spread"), E.LocalCm.Y >= 25 && E.LocalCm.Y <= 75);
    TestTrue(TEXT("Z in the cell's spread"), E.LocalCm.Z >= 12.5 && E.LocalCm.Z <= 62.5);
    TestEqual(TEXT("upward speed"), E.VelocityCm.Z, 20.);
    TestTrue(TEXT("sideways speed"), FMath::Abs(E.VelocityCm.X) <= 10 && FMath::Abs(E.VelocityCm.Y) <= 10);

    FEntityLab L2(false);
    FFoundationResult R2;
    int32 Steps2 = 0;
    Break(L2, R2, Steps2);
    if (!TestEqual(TEXT("second lab: one entity"), L2.A.GetItemEntities().Num(), 1)) return false;
    TestTrue(TEXT("deterministic position"), L2.A.GetItemEntities()[0].LocalCm == E.LocalCm);
    TestTrue(TEXT("deterministic velocity"), L2.A.GetItemEntities()[0].VelocityCm == E.VelocityCm);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemEntityFallTest, "Unnamed.Foundation.Items.Entities.Fall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationItemEntityFallTest::RunTest(const FString&)
{
    using namespace ItemEntityTestsLocal;
    const FVector Far(-1500, -1500, 0);
    auto Make = [&Far](FEntityLab& L)
    {
        L.A.Options.bItemEntities = true;
        L.Stand(Far);
    };
    {
        // (a) Free fall against an independent reference, then rest on the floor.
        FEntityLab L(false);
        Make(L);
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(50, 50, 300), FVector::ZeroVector, 10);
        double Z = 300, Vz = 0;
        bool bRefGround = false;
        int32 RefLanding = -1, Landing = -1;
        double MaxError = 0;
        for (int32 Step = 1; Step <= 120; ++Step)
        {
            Vz -= 4;
            if (Z + Vz <= 0)
            {
                Z = 0;
                Vz = 0;
                if (!bRefGround) RefLanding = Step;
                bRefGround = true;
            }
            else
            {
                Z += Vz;
                bRefGround = false;
            }
            Vz *= .98;
            L.NextStep();
            const FFoundationItemEntity* E = L.Find(Id);
            if (!E) return TestTrue(TEXT("fall: entity stays"), false);
            MaxError = FMath::Max(MaxError, FMath::Abs(E->LocalCm.Z - Z));
            if (Landing < 0 && E->bOnGround) Landing = Step;
        }
        TestTrue(TEXT("fall matches the reference every step"), MaxError < 1e-9);
        TestEqual(TEXT("same landing step"), Landing, RefLanding);
        TestTrue(TEXT("it landed"), Landing > 0);
        const FFoundationItemEntity* E = L.Find(Id);
        if (!E) return false;
        TestEqual(TEXT("rests at Z 0"), E->LocalCm.Z, 0.);
        TestTrue(TEXT("rests with zero velocity"), E->VelocityCm == FVector::ZeroVector);
        TestTrue(TEXT("on the ground"), E->bOnGround);
        L.Steps(40);
        E = L.Find(Id);
        TestTrue(TEXT("still resting after 40 steps"),
            E && E->LocalCm.Z == 0 && E->VelocityCm == FVector::ZeroVector && E->bOnGround);
    }
    {
        // (b) Rests on a block, falls again when it goes.
        FEntityLab L(false);
        Make(L);
        L.Put({2, 0, 0});
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(250, 50, 300), FVector::ZeroVector, 10);
        L.Steps(80);
        const FFoundationItemEntity* E = L.Find(Id);
        TestTrue(TEXT("rests on the block at Z 100"), E && E->LocalCm.Z == 100.);
        L.A.World.Set({2, 0, 0}, FFoundationBlockState{});
        L.Steps(80);
        E = L.Find(Id);
        TestTrue(TEXT("falls to the floor when the block goes"), E && E->LocalCm.Z == 0.);
    }
    {
        // (c) Ground friction.
        FEntityLab L(false);
        Make(L);
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(50, 50, 0), FVector(10, 0, 0), 10);
        L.NextStep();
        const FFoundationItemEntity* E = L.Find(Id);
        if (!E) return false;
        TestTrue(TEXT("moved by the velocity"), FMath::IsNearlyEqual(E->LocalCm.X, 60., 1e-9));
        TestTrue(TEXT("friction and drag"), FMath::IsNearlyEqual(E->VelocityCm.X, 10 * .6 * .98, 1e-9));
    }
    {
        // (d) A wall stops it.
        FEntityLab L(false);
        Make(L);
        L.Put({3, 0, 0});
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(250, 50, 0), FVector(60, 0, 0), 10);
        L.NextStep();
        const FFoundationItemEntity* E = L.Find(Id);
        if (!E) return false;
        TestTrue(TEXT("stops at the wall face"), FMath::IsNearlyEqual(E->LocalCm.X, 287.5, 1e-9));
        TestEqual(TEXT("blocked axis velocity is 0"), E->VelocityCm.X, 0.);
    }
    {
        // (e) Terminal velocity.
        FEntityLab L(false);
        Make(L);
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(50, 50, 100000), FVector::ZeroVector, 10);
        L.Steps(400);
        const FFoundationItemEntity* E = L.Find(Id);
        TestTrue(TEXT("terminal velocity about -196"), E && FMath::Abs(E->VelocityCm.Z + 196) < 1);
    }
    {
        // (f) The void.
        FEntityLab L(false);
        Make(L);
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(5000, 0, 0), FVector::ZeroVector, 10);
        double LastZ = 0;
        bool bGone = false;
        for (int32 Step = 0; Step < 300 && !bGone; ++Step)
        {
            L.NextStep();
            if (const FFoundationItemEntity* E = L.Find(Id))
                LastZ = E->LocalCm.Z;
            else
                bGone = true;
        }
        TestTrue(TEXT("vanishes below the world"), bGone);
        TestTrue(TEXT("last seen no lower than 64 cells below the bounds"), LastZ >= -6400);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemEntityPickupTest, "Unnamed.Foundation.Items.Entities.Pickup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationItemEntityPickupTest::RunTest(const FString&)
{
    using namespace ItemEntityTestsLocal;
    {
        // (a) The delay.
        FEntityLab L(false);
        L.Stand({0, 0, 0});
        const int32 Item = L.LabItem();
        const int32 Id = L.A.SpawnItem(Item, 1, FVector::ZeroVector, FVector::ZeroVector, 10);
        const TArray<TPair<int32, int32>> Before = Snapshot(L);
        const int32 Had = CountItem(L, Item);
        bool bHeld = true;
        for (int32 Step = 1; Step <= 9; ++Step)
        {
            L.NextStep();
            bHeld &= L.Find(Id) != nullptr && Snapshot(L) == Before;
        }
        TestTrue(TEXT("held for 9 steps"), bHeld);
        L.NextStep();
        TestNull(TEXT("picked on the 10th step"), L.Find(Id));
        TestEqual(TEXT("inventory +1"), CountItem(L, Item), Had + 1);
        TestEqual(TEXT("ItemsPickedUp"), L.A.ItemsPickedUp, 1);
        const FFoundationEvent& Event = L.A.GetEvents().Last();
        TestTrue(TEXT("PickedUp event"),
            Event.Kind == EFoundationEventKind::PickedUp && Event.Player == L.Player && Event.EntityId == Id &&
                Event.Item == Item && Event.Count == 1 && Event.bWholeStack);
    }
    {
        // (b) The box edges.
        const FVector Zero = FVector::ZeroVector;
        TestTrue(TEXT("X 142.4 picked"), PickedAt({142.4, 0, 0}, Zero));
        TestFalse(TEXT("X 142.6 not"), PickedAt({142.6, 0, 0}, Zero));
        TestFalse(TEXT("X 142.5 touching not"), PickedAt({142.5, 0, 0}, Zero));
        TestTrue(TEXT("Y 142.4 picked"), PickedAt({0, 142.4, 0}, Zero));
        TestFalse(TEXT("Y 142.6 not"), PickedAt({0, 142.6, 0}, Zero));
        TestFalse(TEXT("Y 142.5 touching not"), PickedAt({0, 142.5, 0}, Zero));
        double Top = 229.9;
        TestTrue(TEXT("top: Z 229.9 picked"), PickedAt({0, 0, Top}, Zero, &Top));
        Top = 230.1;
        TestFalse(TEXT("top: Z 230.1 not"), PickedAt({0, 0, Top}, Zero, &Top));
        const FVector High(0, 0, 200);
        Top = 125.1;
        TestTrue(TEXT("bottom: Z 125.1 picked"), PickedAt({0, 0, Top}, High, &Top));
        Top = 124.9;
        TestFalse(TEXT("bottom: Z 124.9 not"), PickedAt({0, 0, Top}, High, &Top));
    }
    {
        // (c) A partial pickup.
        FEntityLab L(false);
        const int32 Item = L.LabItem();
        L.FillOthers();
        L.P().Inventory.SetSlot(0, L.P().Inventory.MakeStack(Item, 59));
        L.Stand({0, 0, 0});
        const int32 Id = L.A.SpawnItem(Item, 12, FVector::ZeroVector, FVector::ZeroVector, 0);
        L.NextStep();
        TestEqual(TEXT("slot filled to 64"), L.P().Inventory.Hotbar[0].Count, 64);
        const FFoundationItemEntity* E = L.Find(Id);
        TestTrue(TEXT("entity keeps 7"), E && E->Count == 7);
        const FFoundationEvent First = L.A.GetEvents().Last();
        TestTrue(TEXT("event says 5, not whole"),
            First.Kind == EFoundationEventKind::PickedUp && First.Count == 5 && !First.bWholeStack);
        const int32 Events = L.A.GetEvents().Num(), Picked = L.A.ItemsPickedUp;
        L.NextStep();
        E = L.Find(Id);
        TestTrue(TEXT("another step changes nothing"),
            E && E->Count == 7 && L.A.GetEvents().Num() == Events && L.A.ItemsPickedUp == Picked &&
                L.P().Inventory.Hotbar[0].Count == 64);
        L.P().Inventory.SetSlot(1, FFoundationItemInstance{});
        L.NextStep();
        TestNull(TEXT("rest picked up once there is room"), L.Find(Id));
        const FFoundationEvent& Last = L.A.GetEvents().Last();
        TestTrue(TEXT("event says 7, whole"),
            Last.Kind == EFoundationEventKind::PickedUp && Last.Count == 7 && Last.bWholeStack);
    }
    {
        // (d) A full inventory leaves the break's drop on the ground.
        FEntityLab L(false);
        L.A.Options.bItemEntities = true;
        MakeFull(L);
        L.Put({0, 0, 0});
        L.Stand({-150, 50, 0}, FVector(150, 0, -112));
        int32 Steps = 0;
        const FFoundationResult R = MineResult(L, Steps);
        TestTrue(TEXT("committed and broken"), R.IsCommitted() && R.bBroken);
        TestFalse(TEXT("the block is gone"), L.Solid({0, 0, 0}));
        if (!TestEqual(TEXT("one entity"), L.A.GetItemEntities().Num(), 1)) return false;
        const int32 Id = L.A.GetItemEntities()[0].Id;
        L.Stand(L.A.GetItemEntities()[0].LocalCm);
        L.Steps(30);
        const FFoundationItemEntity* E = L.Find(Id);
        TestTrue(TEXT("still there with a full inventory"), E && E->Count == 1);
        L.P().Inventory.SetSlot(3, FFoundationItemInstance{});
        L.NextStep();
        TestNull(TEXT("picked up once there is room"), L.Find(Id));
    }
    {
        // (e) The dead pick up nothing.
        FEntityLab L(false);
        L.Stand({0, 0, 0});
        L.Facts([](FFoundationPlayerFacts& F) { F.bAlive = false; });
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector::ZeroVector, FVector::ZeroVector, 0);
        L.Steps(5);
        TestNotNull(TEXT("a dead player does not pick up"), L.Find(Id));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemEntityOriginTest, "Unnamed.Foundation.Items.Entities.Origin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationItemEntityOriginTest::RunTest(const FString&)
{
    using namespace ItemEntityTestsLocal;
    // A rebased world: positions stay relative to the origin, collision, pickups and events are in world cm.
    FEntityLab L(false);
    const FVector Origin(12345, -6789, 250);
    L.A.World.OriginCm = Origin;
    L.A.Statics[0].Box = L.A.Statics[0].Box.ShiftBy(Origin);
    TArray<FBox> Regions;
    L.A.GatherCollision = [&L, &Regions](const FBox& Region, TArray<FBox>& Out)
    {
        Regions.Add(Region);
        for (const FFoundationStatic& Static : L.A.Statics)
            if (Static.Box.Intersect(Region)) Out.Add(Static.Box);
    };
    L.Stand(Origin + FVector(-1500, -1500, 0));
    const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(50, 50, 300), FVector::ZeroVector, 10);
    L.Steps(60);
    const FFoundationItemEntity* Entity = L.Find(Id);
    TestTrue(TEXT("rests on the moved floor in local cm"),
        Entity && FMath::Abs(Entity->LocalCm.Z) < 1e-6 && Entity->bOnGround && Entity->LocalCm.X == 50);
    TestTrue(TEXT("the hook gets world regions"),
        !Regions.IsEmpty() && Regions[0].IsInside(Origin + FVector(50, 50, 300) + FVector(0, 0, 1)));
    L.Stand(Origin + FVector(50, 50, 0));
    L.NextStep();
    const FFoundationEvent& Last = L.A.GetEvents().Last();
    TestTrue(TEXT("picked up where it lies in world cm"),
        !L.Find(Id) && Last.Kind == EFoundationEventKind::PickedUp &&
            Last.LocationCm.Equals(Origin + FVector(50, 50, 0), 1e-6));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemEntityHeldStackTest, "Unnamed.Foundation.Items.Entities.HeldStack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationItemEntityHeldStackTest::RunTest(const FString&)
{
    using namespace ItemEntityTestsLocal;
    // Breaking by hand while holding lab_block, a lab_block picked up into the held stack mid-break: the break goes on
    // and takes as many steps as without the pickup.
    auto Mine = [this](bool bPickup, int32& OutPicked)
    {
        FEntityLab L(false);
        L.A.Options.bItemEntities = true;
        L.P().Inventory.Hotbar[0].Count = 10;
        L.Put({0, 0, 0});
        L.Stand({-150, 50, 0}, FVector(150, 0, -112));
        FFoundationResult R = L.Run(L.Make(EFoundationCommandType::MineStart));
        int32 Steps = 0;
        while (!R.bBroken && R.IsCommitted() && Steps < 400)
        {
            if (bPickup && Steps == 5) L.A.SpawnItem(L.LabItem(), 1, FVector(-150, 50, 0), FVector::ZeroVector, 0);
            L.NextStep();
            R = L.Run(L.Make(EFoundationCommandType::MineContinue));
            ++Steps;
        }
        OutPicked = L.P().Inventory.Hotbar[0].Count;
        return R.bBroken ? Steps : -Steps;
    };
    int32 Held = 0, HeldControl = 0;
    const int32 Control = Mine(false, HeldControl);
    const int32 WithPickup = Mine(true, Held);
    TestTrue(TEXT("the control breaks"), Control > 5);
    TestEqual(TEXT("a pickup into the held stack keeps the break going"), WithPickup, Control);
    TestEqual(TEXT("and the stack grew"), Held, HeldControl + 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemEntityStreamingTest, "Unnamed.Foundation.Items.Entities.Streaming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationItemEntityStreamingTest::RunTest(const FString&)
{
    using namespace ItemEntityTestsLocal;
    const FVector Far(-1500, -1500, 0);
    {
        // (a) Frozen in an unloaded column.
        FEntityLab L(false);
        L.A.Options.bItemEntities = true;
        L.Stand(Far);
        TUniquePtr<FColumnStorage> Storage = MakeUnique<FColumnStorage>();
        FColumnStorage* Columns = Storage.Get();
        L.A.World.SetStorage(MoveTemp(Storage));
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(50, 50, 300), FVector::ZeroVector, 10);
        L.Steps(3);
        Columns->Unloaded.Add(FIntPoint(0, 0));
        const FFoundationItemEntity Moving = *L.Find(Id);
        L.Steps(5);
        const FFoundationItemEntity* E = L.Find(Id);
        if (!E) return false;
        TestTrue(TEXT("frozen: position and velocity unchanged"),
            E->LocalCm == Moving.LocalCm && E->VelocityCm == Moving.VelocityCm);
        TestTrue(TEXT("frozen: age and delay unchanged"), E->Age == Moving.Age && E->PickupDelay == Moving.PickupDelay);
        TestTrue(TEXT("frozen flag and previous position"), E->bFrozen && E->PreviousLocalCm == E->LocalCm);
        Columns->Unloaded.Reset();
        L.NextStep();
        E = L.Find(Id);
        if (!E) return false;
        TestTrue(TEXT("thawed: it moves"), E->LocalCm != Moving.LocalCm && !E->bFrozen);
        TestEqual(TEXT("thawed: age increments"), E->Age, Moving.Age + 1);
    }
    {
        // A frozen item is not picked up.
        FEntityLab L(false);
        TUniquePtr<FColumnStorage> Storage = MakeUnique<FColumnStorage>();
        FColumnStorage* Columns = Storage.Get();
        L.A.World.SetStorage(MoveTemp(Storage));
        L.Stand({0, 0, 0});
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(0, 0, 0), FVector::ZeroVector, 0);
        Columns->Unloaded.Add(FIntPoint(0, 0));
        L.Steps(3);
        TestNotNull(TEXT("a frozen item is not picked up"), L.Find(Id));
    }
    {
        // (b) Stops at an unloaded column's face through the hook.
        FEntityLab L(false);
        L.A.Options.bItemEntities = true;
        L.Stand(Far);
        TUniquePtr<FColumnStorage> Storage = MakeUnique<FColumnStorage>();
        FColumnStorage* Columns = Storage.Get();
        L.A.World.SetStorage(MoveTemp(Storage));
        FEntityLab* Lab = &L;
        L.A.GatherCollision = [Lab, Columns](const FBox& Region, TArray<FBox>& Out)
        {
            Lab->A.World.GatherCollision(Region, Out);
            for (const FFoundationStatic& Static : Lab->A.Statics)
                if (Static.Box.Intersect(Region)) Out.Add(Static.Box);
            const double Cm = UFoundationProfile::BlockCm * 16;
            const double MinZ = Lab->A.World.BoundsMin.Z * UFoundationProfile::BlockCm;
            const double MaxZ = (Lab->A.World.BoundsMax.Z + 1) * UFoundationProfile::BlockCm;
            for (const FIntPoint& Column : Columns->Unloaded)
            {
                const FBox Box(FVector(Column.X * Cm, Column.Y * Cm, MinZ),
                    FVector((Column.X + 1) * Cm, (Column.Y + 1) * Cm, MaxZ));
                if (Box.Intersect(Region)) Out.Add(Box);
            }
        };
        Columns->Unloaded.Add(FIntPoint(1, 0));
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(1560, 50, 0), FVector(60, 0, 0), 10);
        L.NextStep();
        const FFoundationItemEntity* E = L.Find(Id);
        if (!E) return false;
        TestTrue(TEXT("stops at the column face"), FMath::IsNearlyEqual(E->LocalCm.X, 1587.5, 1e-9));
        TestEqual(TEXT("blocked velocity is 0"), E->VelocityCm.X, 0.);
        TestFalse(TEXT("not frozen"), E->bFrozen);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemEntitySwitchTest, "Unnamed.Foundation.Items.Entities.Switch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationItemEntitySwitchTest::RunTest(const FString&)
{
    using namespace ItemEntityTestsLocal;
    {
        // (a) Off: the break returns straight to the inventory.
        FEntityLab L(false);
        L.Put({0, 0, 0});
        L.Stand({-150, 50, 0}, FVector(150, 0, -112));
        const int32 Had = CountItem(L, L.LabItem());
        int32 Steps = 0;
        const FFoundationResult R = MineResult(L, Steps);
        TestTrue(TEXT("off: broken and returned"), R.bBroken && R.bItemReturned);
        TestEqual(TEXT("off: count +1"), CountItem(L, L.LabItem()), Had + 1);
        L.Steps(20);
        TestTrue(TEXT("off: no entities, none spawned"), L.A.GetItemEntities().IsEmpty() && L.A.ItemsSpawned == 0);
    }
    {
        // (b) Off with a full inventory: refused.
        FEntityLab L(false);
        MakeFull(L);
        L.Put({0, 0, 0});
        L.Stand({-150, 50, 0}, FVector(150, 0, -112));
        int32 Steps = 0;
        const FFoundationResult R = MineResult(L, Steps);
        TestEqual(TEXT("full: reason"), R.Reason, EFoundationReason::InventoryFull);
        TestTrue(TEXT("full: block stays, no entities"), L.Solid({0, 0, 0}) && L.A.GetItemEntities().IsEmpty());
    }
    {
        // (c) Infinite supply drops nothing.
        FEntityLab L(true);
        L.A.Options.bItemEntities = true;
        L.Put({0, 0, 0});
        L.Stand({-150, 50, 0}, FVector(150, 0, -112));
        int32 Steps = 0;
        const FFoundationResult R = MineResult(L, Steps);
        TestTrue(TEXT("infinite: broken"), R.bBroken);
        TestTrue(TEXT("infinite: nothing spawned"), L.A.GetItemEntities().IsEmpty() && L.A.ItemsSpawned == 0);
    }
    {
        // (d) Reset clears the entities.
        FEntityLab L(false);
        L.A.SpawnItem(L.LabItem(), 1, FVector(50, 50, 100), FVector::ZeroVector, 10);
        L.A.SpawnItem(L.LabItem(), 1, FVector(150, 50, 100), FVector::ZeroVector, 10);
        TestEqual(TEXT("two entities"), L.A.GetItemEntities().Num(), 2);
        L.A.Reset();
        TestTrue(TEXT("reset clears them"), L.A.GetItemEntities().IsEmpty());
    }
    {
        // (e) The console variable.
        IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Items.Entities"));
        TestNotNull(TEXT("foundation.Items.Entities exists"), Variable);
        TestEqual(TEXT("reads 1 by default"), Variable ? Variable->GetInt() : -1, 1);
        TestTrue(TEXT("IsOn"), FoundationItemEntities::IsOn());
    }
    return true;
}

// Items S8b: Q throws one item, Ctrl+Q the stack (40-tick pickup delay); stacks merge every 40 ticks in the merge box;
// items despawn at 6,000 ticks of age (paused while frozen); 1 to 5 copies are drawn.
namespace ItemEntityTestsLocal
{
    // A lab with the switch on, the player at Feet looking along +X, slot 0 holding Count lab blocks.
    void MakeThrowLab(FEntityLab& L, int32 Count, FVector Feet = FVector::ZeroVector)
    {
        L.A.Options.bItemEntities = true;
        L.P().Inventory.SetSlot(0, L.P().Inventory.MakeStack(L.LabItem(), Count));
        L.P().Inventory.Select(0);
        L.Stand(Feet);
    }

    FFoundationResult Throw(FEntityLab& L, bool bWholeStack)
    {
        FFoundationCommand Command = L.Make(EFoundationCommandType::Drop);
        Command.bWholeStack = bWholeStack;
        return L.Run(Command);
    }

    // Steps until the entity is picked up, standing on it each step; the step it went, or -1 within Limit.
    int32 StepsToPickup(FEntityLab& L, int32 Id, int32 Limit = 200)
    {
        for (int32 Step = 1; Step <= Limit; ++Step)
        {
            const FFoundationItemEntity* E = L.Find(Id);
            if (!E) return -1;
            L.Stand(FVector(E->LocalCm.X, E->LocalCm.Y, 0));
            L.A.Step();
            if (!L.Find(Id)) return Step;
        }
        return -1;
    }

    // Two resting stacks of the lab block (no velocity), the player far away; their ids.
    TPair<int32, int32> SpawnPair(FEntityLab& L, FVector APos, int32 ACount, FVector BPos, int32 BCount)
    {
        L.A.Options.bItemEntities = true;
        L.Stand(FVector(-1500, -1500, 0));
        const int32 A = L.A.SpawnItem(L.LabItem(), ACount, APos, FVector::ZeroVector, 0);
        const int32 B = L.A.SpawnItem(L.LabItem(), BCount, BPos, FVector::ZeroVector, 0);
        return {A, B};
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemEntityThrowTest, "Unnamed.Foundation.Items.Entities.Throw",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationItemEntityThrowTest::RunTest(const FString&)
{
    using namespace ItemEntityTestsLocal;
    using namespace FoundationItemEntities;
    {
        // Off: refused as before items S8b.
        FEntityLab L(false);
        MakeThrowLab(L, 10);
        L.A.Options.bItemEntities = false;
        const FFoundationResult R = Throw(L, false);
        TestEqual(TEXT("off: refused"), R.Reason, EFoundationReason::DropDeferred);
        TestTrue(TEXT("off: nothing thrown"),
            L.P().Inventory.Hotbar[0].Count == 10 && L.A.GetItemEntities().IsEmpty() && L.A.ItemsThrown == 0);
    }
    {
        // On: one item from the eye along the look, delay 40.
        FEntityLab L(false);
        MakeThrowLab(L, 10);
        const FFoundationResult R = Throw(L, false);
        TestTrue(TEXT("committed"), R.IsCommitted());
        TestEqual(TEXT("one taken"), L.P().Inventory.Hotbar[0].Count, 9);
        if (L.A.GetItemEntities().Num() != 1) return false;
        const FFoundationItemEntity E = L.A.GetItemEntities()[0];
        TestTrue(TEXT("one lab block, delay 40"), E.Item == L.LabItem() && E.Count == 1 && E.PickupDelay == 40);
        TestTrue(TEXT("from 0.3 below the eye"), E.LocalCm.Equals(FVector(0, 0, 162 - 30), 1e-9));
        TestTrue(TEXT("0.3 along the look, 0.1 up, small spread"),
            FMath::Abs(E.VelocityCm.X - 30) <= 2 + 1e-9 && FMath::Abs(E.VelocityCm.Y) <= 2 + 1e-9 &&
                FMath::Abs(E.VelocityCm.Z - 10) <= 10 + 1e-9);
        TestEqual(TEXT("ItemsThrown"), L.A.ItemsThrown, 1);
        // Picked up on the 40th step, not before.
        TestEqual(TEXT("picked up on step 40"), StepsToPickup(L, E.Id), 40);
        TestEqual(TEXT("back in the stack"), L.P().Inventory.Hotbar[0].Count, 10);
    }
    {
        // Looking up: thrown upward; the last item empties the slot.
        FEntityLab L(false);
        MakeThrowLab(L, 1);
        L.Stand(FVector::ZeroVector, FVector(0, 0, 1));
        Throw(L, false);
        if (L.A.GetItemEntities().Num() != 1) return false;
        TestTrue(TEXT("looking up: 0.4 up"), FMath::Abs(L.A.GetItemEntities()[0].VelocityCm.Z - 40) <= 10 + 1e-9);
        TestTrue(TEXT("last item: the slot empties"), L.P().Inventory.Hotbar[0].IsEmpty());
    }
    {
        // An empty hand throws nothing.
        FEntityLab L(false);
        MakeThrowLab(L, 1);
        L.P().Inventory.SetSlot(0, {});
        TestEqual(TEXT("empty hand: refused"), Throw(L, false).Reason, EFoundationReason::NoSupply);
        TestTrue(TEXT("empty hand: nothing thrown"), L.A.GetItemEntities().IsEmpty());
    }
    {
        // Creative (infinite supply) throws from the stack too, as vanilla's Q does.
        FEntityLab L(true);
        MakeThrowLab(L, 10);
        TestTrue(TEXT("creative: committed"), Throw(L, false).IsCommitted());
        TestTrue(TEXT("creative: one taken"), L.P().Inventory.Hotbar[0].Count == 9 && L.A.GetItemEntities().Num() == 1);
    }
    {
        // A dead player's Q is refused as every hand command is (Codex review 4, #5), and so is a stale command.
        FEntityLab L(false);
        MakeThrowLab(L, 10);
        L.Facts([](FFoundationPlayerFacts& F) { F.bAlive = false; });
        TestEqual(TEXT("dead: refused"), Throw(L, false).Reason, EFoundationReason::Dead);
        L.Facts([](FFoundationPlayerFacts& F) { F.bAlive = true; });
        FFoundationCommand Stale = L.Make(EFoundationCommandType::Drop);
        L.P().Inventory.SetSlot(0, L.P().Inventory.MakeStack(L.LabItem(), 10));
        TestEqual(TEXT("changed hand: refused"), L.Run(Stale).Reason, EFoundationReason::ItemChanged);
        TestTrue(TEXT("dead, changed: nothing thrown"),
            L.P().Inventory.Hotbar[0].Count == 10 && L.A.GetItemEntities().IsEmpty() && L.A.ItemsThrown == 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemEntityThrowStackTest, "Unnamed.Foundation.Items.Entities.ThrowStack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationItemEntityThrowStackTest::RunTest(const FString&)
{
    using namespace ItemEntityTestsLocal;
    FEntityLab L(false);
    MakeThrowLab(L, 37);
    const FFoundationResult R = Throw(L, true);
    TestTrue(TEXT("committed"), R.IsCommitted());
    TestTrue(TEXT("the slot empties"), L.P().Inventory.Hotbar[0].IsEmpty());
    if (L.A.GetItemEntities().Num() != 1) return false;
    const FFoundationItemEntity E = L.A.GetItemEntities()[0];
    TestTrue(TEXT("one entity of 37, delay 40"), E.Count == 37 && E.PickupDelay == 40);
    TestEqual(TEXT("picked up on step 40"), StepsToPickup(L, E.Id), 40);
    TestEqual(TEXT("all 37 back"), CountItem(L, L.LabItem()), 37);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemEntityThrowToolTest, "Unnamed.Foundation.Items.Entities.ThrowTool",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationItemEntityThrowToolTest::RunTest(const FString&)
{
    using namespace ItemEntityTestsLocal;
    FEntityLab L(false);
    L.A.Options.bItemEntities = true;
    const int32 Pickaxe = L.A.Rules.FindItem(TEXT("iron_pickaxe"));
    if (!TestTrue(TEXT("iron_pickaxe wears"), Pickaxe != INDEX_NONE && L.A.Rules.Items[Pickaxe].MaxDurability > 0))
        return false;
    for (int32 Slot = 0; Slot < FFoundationInventory::HotbarSlots; ++Slot)
        L.P().Inventory.SetSlot(Slot, {});
    FFoundationItemInstance Tool = L.P().Inventory.MakeStack(Pickaxe, 1);
    Tool.Damage = 57;
    L.P().Inventory.SetSlot(4, Tool);
    L.P().Inventory.Select(4);
    L.Stand(FVector::ZeroVector);
    Throw(L, false);
    if (L.A.GetItemEntities().Num() != 1) return false;
    const FFoundationItemEntity E = L.A.GetItemEntities()[0];
    TestTrue(TEXT("the entity keeps the damage"), E.Item == Pickaxe && E.Damage == 57);
    TestTrue(TEXT("the slot empties"), L.P().Inventory.Hotbar[4].IsEmpty());
    TestTrue(TEXT("picked up"), StepsToPickup(L, E.Id) > 0);
    const FFoundationItemInstance* Back = L.P().Inventory.Hotbar.FindByPredicate(
        [Pickaxe](const FFoundationItemInstance& S) { return !S.IsEmpty() && S.Item == Pickaxe; });
    TestTrue(TEXT("back with damage 57"), Back && Back->Damage == 57 && Back->Count == 1);
    // Inventory::Add merges only into stacks of the same damage (and its default, 0, merges as before).
    FFoundationInventory Inventory;
    for (int32 Slot = 0; Slot < FFoundationInventory::HotbarSlots; ++Slot)
        Inventory.SetSlot(Slot, {});
    Inventory.Select(0);
    Inventory.SetSlot(0, Inventory.MakeStack(L.LabItem(), 5));
    TestEqual(TEXT("default damage merges"), Inventory.Add(L.LabItem(), 3, 64), 3);
    TestEqual(TEXT("merged into slot 0"), Inventory.Hotbar[0].Count, 8);
    TestEqual(TEXT("a damaged add"), Inventory.Add(L.LabItem(), 2, 64, true, 7), 2);
    TestTrue(TEXT("goes to a new stack with its damage"),
        Inventory.Hotbar[0].Count == 8 && Inventory.Hotbar[1].Count == 2 && Inventory.Hotbar[1].Damage == 7);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemEntityMergeTest, "Unnamed.Foundation.Items.Entities.Merge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationItemEntityMergeTest::RunTest(const FString&)
{
    using namespace ItemEntityTestsLocal;
    using namespace FoundationItemEntities;
    {
        // Inside the box: merged on the 40th step of age, into the larger stack, not before.
        FEntityLab L(false);
        const TPair<int32, int32> Ids = SpawnPair(L, FVector(0, 0, 0), 3, FVector(60, 0, 0), 5);
        L.Steps(39);
        TestEqual(TEXT("not merged before step 40"), L.A.GetItemEntities().Num(), 2);
        L.NextStep();
        TestTrue(TEXT("merged on step 40 into the larger"),
            L.A.GetItemEntities().Num() == 1 && L.Find(Ids.Value) && L.Find(Ids.Value)->Count == 8 && !L.Find(Ids.Key));
        TestEqual(TEXT("ItemsMerged"), L.A.ItemsMerged, 1);
    }
    // The box's edge (an item's box grown 0.5 sideways): 74 cm apart merge, 75 (touching) do not.
    for (const TTuple<double, bool>& Case : TArray<TTuple<double, bool>>{{74., true}, {75., false}})
    {
        FEntityLab L(false);
        SpawnPair(L, FVector(0, 0, 0), 3, FVector(Case.Get<0>(), 0, 0), 5);
        L.Steps(80);
        TestEqual(*FString::Printf(TEXT("sideways %.0f cm"), Case.Get<0>()), L.A.GetItemEntities().Num(),
            Case.Get<1>() ? 1 : 2);
    }
    // Not grown up or down [items.merge_boundary_stationary]: an item on a shelf 24 cm up (its box still overlaps)
    // merges, 25 cm (touching) does not.
    for (const TTuple<double, bool>& Case : TArray<TTuple<double, bool>>{{24., true}, {25., false}})
    {
        FEntityLab L(false);
        FFoundationStatic Shelf;
        Shelf.Box = FBox(FVector(20, -50, -10), FVector(80, 50, Case.Get<0>()));
        Shelf.Label = TEXT("shelf");
        L.A.Statics.Add(Shelf);
        SpawnPair(L, FVector(0, 0, 0), 3, FVector(40, 0, Case.Get<0>()), 5);
        L.Steps(80);
        TestEqual(
            *FString::Printf(TEXT("up %.0f cm"), Case.Get<0>()), L.A.GetItemEntities().Num(), Case.Get<1>() ? 1 : 2);
    }
    {
        // [items.physics] an item whose integer block cell changed this step seeks every 2 steps of its age: A falls
        // from 105 cm (101 after step 1, 93.08 after step 2, a new cell on its 2nd step) beside B resting on a shelf
        // 90 cm up, and merges into it on step 2, not step 1.
        FEntityLab L(false);
        FFoundationStatic Shelf;
        Shelf.Box = FBox(FVector(20, -50, -10), FVector(80, 50, 90));
        Shelf.Label = TEXT("shelf");
        L.A.Statics.Add(Shelf);
        const TPair<int32, int32> Ids = SpawnPair(L, FVector(0, 0, 105), 3, FVector(40, 0, 90), 5);
        L.NextStep();
        TestEqual(TEXT("moving: not merged on step 1"), L.A.GetItemEntities().Num(), 2);
        L.NextStep();
        TestTrue(TEXT("moving: merged on step 2"),
            L.A.GetItemEntities().Num() == 1 && L.Find(Ids.Value) && L.Find(Ids.Value)->Count == 8);
    }
    {
        // Only when the sum fits the max stack (minecraft.wiki "Item (entity)", Merging): 40 and 40 stay apart.
        FEntityLab L(false);
        SpawnPair(L, FVector(0, 0, 0), 40, FVector(30, 0, 0), 40);
        L.Steps(120);
        const TArray<FFoundationItemEntity>& Es = L.A.GetItemEntities();
        TestTrue(
            TEXT("40 and 40 stay"), Es.Num() == 2 && Es[0].Count == 40 && Es[1].Count == 40 && L.A.ItemsMerged == 0);
    }
    {
        // 32 and 32 make exactly 64: merged.
        FEntityLab L(false);
        SpawnPair(L, FVector(0, 0, 0), 32, FVector(30, 0, 0), 32);
        L.Steps(40);
        TestTrue(
            TEXT("32 and 32 make one 64"), L.A.GetItemEntities().Num() == 1 && L.A.GetItemEntities()[0].Count == 64);
    }
    {
        // Different damage never merges.
        FEntityLab L(false);
        L.A.Options.bItemEntities = true;
        L.Stand(FVector(-1500, -1500, 0));
        const int32 Pickaxe = L.A.Rules.FindItem(TEXT("iron_pickaxe"));
        L.A.Rules.Items[Pickaxe].MaxStack = 64;
        L.A.SpawnItem(Pickaxe, 1, FVector(0, 0, 0), FVector::ZeroVector, 0, 4);
        L.A.SpawnItem(Pickaxe, 1, FVector(30, 0, 0), FVector::ZeroVector, 0, 5);
        L.Steps(80);
        TestEqual(TEXT("different damage: not merged"), L.A.GetItemEntities().Num(), 2);
    }
    {
        // Off: nothing merges.
        FEntityLab L(false);
        SpawnPair(L, FVector(0, 0, 0), 3, FVector(30, 0, 0), 5);
        L.A.Options.bItemEntities = false;
        L.Steps(80);
        TestEqual(TEXT("off: not merged"), L.A.GetItemEntities().Num(), 2);
    }
    {
        // A frozen stack neither merges nor is merged into.
        FEntityLab L(false);
        TUniquePtr<FColumnStorage> Storage = MakeUnique<FColumnStorage>();
        FColumnStorage* Columns = Storage.Get();
        L.A.World.SetStorage(MoveTemp(Storage));
        SpawnPair(L, FVector(-10, 50, 0), 3, FVector(10, 50, 0), 5);
        Columns->Unloaded.Add(FIntPoint(-1, 0));
        L.Steps(80);
        TestEqual(TEXT("frozen: not merged"), L.A.GetItemEntities().Num(), 2);
    }
    {
        // On a tie the seeker (the first to reach a multiple of 40) moves into the other (minecraft.wiki), which keeps
        // the longer delay and the younger age: A (delay 0) seeks on its 40th step; B, spawned 10 steps later with
        // delay 50, has age 30 and delay 20, and stays where it lies.
        FEntityLab L(false);
        L.A.Options.bItemEntities = true;
        L.Stand(FVector(-1500, -1500, 0));
        const int32 A = L.A.SpawnItem(L.LabItem(), 3, FVector(0, 0, 0), FVector::ZeroVector, 0);
        L.Steps(10);
        const int32 B = L.A.SpawnItem(L.LabItem(), 3, FVector(30, 0, 0), FVector::ZeroVector, 50);
        L.Steps(30);
        const FFoundationItemEntity* Into = L.Find(B);
        TestTrue(TEXT("tie: the seeker moves into the other"), Into && !L.Find(A) && Into->Count == 6);
        TestTrue(TEXT("tie: at the other's place"), Into && Into->LocalCm.Equals(FVector(30, 0, 0), 1e-6));
        TestTrue(TEXT("tie: the longer delay, the younger age"), Into && Into->PickupDelay == 20 && Into->Age == 30);
    }
    {
        // A chain: three single items 30 cm apart, all within the first's merge box, end as one stack of 3.
        FEntityLab L(false);
        L.A.Options.bItemEntities = true;
        L.Stand(FVector(-1500, -1500, 0));
        for (const double X : {0., 30., 60.})
            L.A.SpawnItem(L.LabItem(), 1, FVector(X, 0, 0), FVector::ZeroVector, 0);
        L.Steps(40);
        TestTrue(TEXT("chain: one stack of 3"),
            L.A.GetItemEntities().Num() == 1 && L.A.GetItemEntities()[0].Count == 3 && L.A.ItemsMerged == 2);
    }
    // The copies drawn: 1 for 1, then 2, 3, 4 and 5 from 2, 17, 33 and 49 (minecraft.wiki).
    for (const TTuple<int32, int32>& Case :
        TArray<TTuple<int32, int32>>{{1, 1}, {2, 2}, {16, 2}, {17, 3}, {32, 3}, {33, 4}, {48, 4}, {49, 5}, {64, 5}})
        TestEqual(*FString::Printf(TEXT("copies for %d"), Case.Get<0>()), GetCopies(Case.Get<0>()), Case.Get<1>());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemEntityDespawnTest, "Unnamed.Foundation.Items.Entities.Despawn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationItemEntityDespawnTest::RunTest(const FString&)
{
    using namespace ItemEntityTestsLocal;
    using namespace FoundationItemEntities;
    {
        // Gone on step 6,000 of age, not before.
        FEntityLab L(false);
        L.A.Options.bItemEntities = true;
        L.Stand(FVector(-1500, -1500, 0));
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(0, 0, 0), FVector::ZeroVector, 0);
        L.Steps(5999);
        TestNotNull(TEXT("present at age 5,999"), L.Find(Id));
        L.NextStep();
        TestNull(TEXT("gone at age 6,000"), L.Find(Id));
        TestEqual(TEXT("ItemsDespawned"), L.A.ItemsDespawned, 1);
    }
    {
        // The age pauses while frozen: 100 frozen steps put the despawn 100 steps later.
        FEntityLab L(false);
        L.A.Options.bItemEntities = true;
        TUniquePtr<FColumnStorage> Storage = MakeUnique<FColumnStorage>();
        FColumnStorage* Columns = Storage.Get();
        L.A.World.SetStorage(MoveTemp(Storage));
        L.Stand(FVector(-1500, -1500, 0));
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(50, 50, 0), FVector::ZeroVector, 0);
        L.Steps(1000);
        Columns->Unloaded.Add(FIntPoint(0, 0));
        L.Steps(100);
        Columns->Unloaded.Reset();
        L.Steps(4999);
        TestNotNull(TEXT("frozen: present at 6,099 steps"), L.Find(Id));
        L.NextStep();
        TestNull(TEXT("frozen: gone at 6,100 steps"), L.Find(Id));
    }
    {
        // Off: no despawn.
        FEntityLab L(false);
        L.Stand(FVector(-1500, -1500, 0));
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(0, 0, 0), FVector::ZeroVector, 0);
        L.Steps(6010);
        TestNotNull(TEXT("off: still present"), L.Find(Id));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationItemEntityPushOutTest, "Unnamed.Foundation.Items.Entities.PushOut",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationItemEntityPushOutTest::RunTest(const FString&)
{
    using namespace ItemEntityTestsLocal;
    // Items fixes (S8a-b; minecraft.wiki "Item (entity)", Behavior): a block placed over a resting drop pushes it out
    // of one of the unobstructed sides, or out of the top when every side is blocked. The drop lies at (130, 40) in
    // cell (1, 0, 0): its -X face is 30 cm away, -Y 40, +Y 60, +X 70.
    struct FCase
    {
        const TCHAR* Name;
        TArray<FIntVector> Walls;
        // Where it must end: its box wholly in this cell.
        FIntVector Out;
    };
    const TArray<FCase> Cases = {
        {TEXT("nearest side (-X)"), {}, {0, 0, 0}},
        {TEXT("-X walled: the next nearest (-Y)"), {{0, 0, 0}}, {1, -1, 0}},
        {TEXT("every side walled: out of the top"), {{0, 0, 0}, {2, 0, 0}, {1, -1, 0}, {1, 1, 0}}, {1, 0, 1}},
    };
    // The lab's origin is 0, so cells are 100 cm from the world's zero.
    const auto Check = [this](const TCHAR* Name, const FBox& Box, const FIntVector& Out)
    {
        const FBox Cell(FVector(Out) * 100 - FVector(.001), FVector(Out + FIntVector(1)) * 100 + FVector(.001));
        TestTrue(*FString::Printf(TEXT("%s: out, in cell %s (box %s)"), Name, *Out.ToString(), *Box.ToString()),
            Cell.IsInside(Box));
    };
    for (const FCase& Case : Cases)
    {
        FEntityLab L(false);
        L.A.Options.bItemEntities = true;
        L.Stand(FVector(-1500, -1500, 0));
        for (const FIntVector& Wall : Case.Walls)
            L.Put(Wall);
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(130, 40, 0), FVector::ZeroVector, 0);
        L.Steps(5);
        TestTrue(*FString::Printf(TEXT("%s: resting first"), Case.Name),
            L.Find(Id) && L.Find(Id)->bOnGround && L.Find(Id)->LocalCm.Equals(FVector(130, 40, 0), 1e-6));
        L.Put({1, 0, 0});
        L.Steps(30);
        const FFoundationItemEntity* E = L.Find(Id);
        if (!TestNotNull(*FString::Printf(TEXT("%s: still there"), Case.Name), E)) continue;
        const FBox Box = FoundationItemEntities::GetBox(L.A.World.OriginCm + E->LocalCm);
        TestTrue(*FString::Printf(TEXT("%s: lab origin 0"), Case.Name), L.A.World.OriginCm.IsZero());
        Check(Case.Name, Box, Case.Out);
        TestTrue(*FString::Printf(TEXT("%s: moving normally again"), Case.Name), E->bOnGround);
    }
    {
        // A block placed over the drop's edge only (its centre stays clear, 2.5 cm in): out the short way (-X).
        FEntityLab L(false);
        L.A.Options.bItemEntities = true;
        L.Stand(FVector(-1500, -1500, 0));
        const int32 Id = L.A.SpawnItem(L.LabItem(), 1, FVector(190, 40, 0), FVector::ZeroVector, 0);
        L.Steps(5);
        L.Put({2, 0, 0});
        L.Steps(30);
        const FFoundationItemEntity* E = L.Find(Id);
        if (TestNotNull(TEXT("edge: still there"), E))
            Check(TEXT("edge: the short way (-X)"), FoundationItemEntities::GetBox(E->LocalCm), {1, 0, 0});
    }
    {
        // The orbs share the motion: an orb at rest in cell (1, 0, 0) leaves by its nearest side too.
        FEntityLab L(false);
        L.Stand(FVector(-1500, -1500, 0));
        if (!TestEqual(TEXT("orb: one spawned"), L.A.SpawnExperience(FVector(130, 40, 0), 1, 1), 1)) return false;
        FFoundationExperienceOrb& Orb = const_cast<FFoundationExperienceOrb&>(L.A.GetExperienceOrbs()[0]);
        Orb.LocalCm = Orb.PreviousLocalCm = FVector(130, 40, 0);
        Orb.VelocityCm = FVector::ZeroVector;
        L.Steps(5);
        L.Put({1, 0, 0});
        L.Steps(30);
        if (!TestEqual(TEXT("orb: still there"), L.A.GetExperienceOrbs().Num(), 1)) return false;
        const FFoundationExperienceOrb& After = L.A.GetExperienceOrbs()[0];
        Check(TEXT("orb, nearest side (-X)"), FoundationExperienceOrbs::GetBox(L.A.World.OriginCm + After.LocalCm),
            {0, 0, 0});
    }
    return true;
}
#endif
