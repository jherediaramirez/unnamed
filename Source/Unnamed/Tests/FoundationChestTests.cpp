#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockFamilies.h"
#include "Foundation/FoundationBlockPlacement.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationChests.h"
#include "Foundation/FoundationContainer.h"
#include "Foundation/FoundationContainers.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags ChestTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    using EChest = FoundationChests::EType;

    // T1 step 18-chest's lab (step 9's FBarrelLab): the project's rules and blocks, the full inventory, block use and
    // chests on, two players with empty inventories looking east at a chest facing west (its front toward them). With
    // bDouble, a type=right half north of it (the clockwise side of west), so the aimed one is type=left.
    struct FChestLab
    {
        using FEntity = FFoundationBlockEntity;
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        FFoundationInteraction I;
        int32 Players[2] = {0, 0};
        int32 Sequences[2] = {1, 1};
        int64 StepIndex = 0;
        FVector Eye = FVector(50, 50, 150);
        FIntVector Aimed = FIntVector(2, 0, 1);
        FIntVector Partner = FIntVector(2, -1, 1);
        // Where each player looks (the aimed chest's centre by default) and whether it sneaks.
        TOptional<FVector> AimPoint[2];
        bool bSneak = false;
        int64 LastSerial = 0;

        explicit FChestLab(bool bDouble = false, const TCHAR* Block = TEXT("chest"))
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bInfiniteSupply = false;
            A.Options.bFullInventory = true;
            A.Options.bBlockUse = true;
            A.Options.bChest = true;
            Players[0] = A.AddPlayer();
            Players[1] = A.AddPlayer();
            A.Reset();
            I.PlayerId = Players[0];
            I.SetPreset(*UFoundationInteractionSettings::Get().FindPreset(TEXT("VanillaReference")), .05);
            I.Resync(A);
            for (const int32 Id : Players)
            {
                FFoundationInventory& Inventory = A.FindPlayer(Id)->Inventory;
                Inventory.bFull = true;
                for (int32 Slot = 0; Slot < FFoundationInventory::Slots; ++Slot)
                    Inventory.SetSlot(Slot, {});
                Inventory.Select(0);
            }
            A.World.Set(Aimed, ChestState(Block, TEXT("west"), bDouble ? TEXT("left") : TEXT("single")));
            if (bDouble) A.World.Set(Partner, ChestState(Block, TEXT("west"), TEXT("right")));
            Steps(1);
            LastSerial = A.GetEventSerial();
        }

        FFoundationBlockState ChestState(const TCHAR* Block, const TCHAR* Facing, const TCHAR* Type) const
        {
            const FFoundationBlockRegistry& R = A.World.GetRegistry();
            FFoundationBlockState State = A.World.FindBlock(Block)->GetDefaultState();
            State = R.WithValue(State, TEXT("facing"), Facing);
            return R.WithValue(State, TEXT("type"), Type);
        }
        void SetBlock(const FIntVector& Cell, const TCHAR* Block)
        {
            A.World.Set(Cell, A.World.FindBlock(Block)->GetDefaultState());
        }
        EChest TypeAt(const FIntVector& Cell) const
        {
            return FoundationChests::GetType(A.World.GetRegistry(), A.World.Get(Cell).State);
        }
        FName FacingAt(const FIntVector& Cell) const
        {
            return A.World.GetRegistry().GetValue(A.World.Get(Cell).State, TEXT("facing"));
        }

        FFoundationPlayer& P(int32 Who = 0) { return *A.FindPlayer(Players[Who]); }
        FFoundationInventory& Inv(int32 Who = 0) { return P(Who).Inventory; }
        int32 Item(const TCHAR* Id) const { return A.Rules.FindItem(Id); }
        FFoundationItemInstance Stack(const TCHAR* Id, int32 Count) { return Inv().MakeStack(Item(Id), Count); }
        bool Holds(const FFoundationItemInstance& Slot, const TCHAR* Id, int32 Count) const
        {
            return Count == 0 ? Slot.IsEmpty() : !Slot.IsEmpty() && Slot.Item == Item(Id) && Slot.Count == Count;
        }

        void Steps(int32 Count)
        {
            for (int32 Index = 0; Index < Count; ++Index)
            {
                ++StepIndex;
                for (int32 Who = 0; Who < 2; ++Who)
                    if (A.FindPlayer(Players[Who]))
                    {
                        FFoundationPlayerFacts Facts;
                        Facts.FeetCm = Eye - FVector(0, 0, 162);
                        Facts.EyeCm = Eye;
                        const FVector Point = AimPoint[Who].IsSet()
                            ? *AimPoint[Who]
                            : FoundationGrid::CellBox(Aimed, A.World.OriginCm).GetCenter();
                        Facts.Aim = (Point - Eye).GetSafeNormal();
                        Facts.bSecondaryUse = bSneak;
                        Facts.Step = StepIndex;
                        A.SetFacts(Players[Who], Facts);
                    }
                A.Step();
            }
        }

        // Aims Who at a point and takes one step so the facts hold it.
        void AimAt(int32 Who, const FVector& Point)
        {
            AimPoint[Who] = Point;
            Steps(1);
        }

        FFoundationCommand Make(EFoundationCommandType Type, int32 Who = 0)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = Sequences[Who]++;
            Command.Type = Type;
            Command.Hand = EFoundationHand::Main;
            Command.Item = Inv(Who).GetHand(EFoundationHand::Main).GetRef();
            Command.Target = A.Resolve(P(Who).Facts);
            return Command;
        }

        FFoundationResult Use(int32 Who = 0)
        {
            FFoundationCommand Command = Make(EFoundationCommandType::UseOn, Who);
            FIntVector Destination;
            EFoundationReason Reason;
            if (A.GetDestination(P(Who), EFoundationHand::Main, Command.Target, Destination, Reason))
            {
                Command.Destination = Destination;
                Command.DestinationRevision = A.World.IsKnown(Destination) ? A.World.Get(Destination).Revision : 0;
            }
            return A.Execute(Players[Who], Command, Vitals);
        }

        int32 Open(int32 Who = 0) { return Use(Who).OpenedContainer; }

        FFoundationResult Click(int32 Container, int32 Slot, int32 Button, EFoundationClickType Type)
        {
            FFoundationCommand Command = Make(EFoundationCommandType::ContainerClick);
            Command.ContainerId = Container;
            Command.Slot = Slot;
            Command.Button = Button;
            Command.ClickType = Type;
            return A.Execute(Players[0], Command, Vitals);
        }

        FFoundationResult Close(int32 Who = 0)
        {
            return A.Execute(Players[Who], Make(EFoundationCommandType::ContainerClose, Who), Vitals);
        }

        FEntity& E(const FIntVector& Cell)
        {
            return A.BlockEntities.FindOrAdd(
                Cell, EFoundationBlockEntityKind::Chest, A.World.GetBlock(A.World.Get(Cell).State).Id);
        }

        TArray<FFoundationEvent> TakeUsed()
        {
            TArray<FFoundationEvent> Used;
            for (const FFoundationEvent& Event : A.GetEvents())
                if (Event.Serial > LastSerial && Event.Kind == EFoundationEventKind::BlockUsed) Used.Add(Event);
            LastSerial = A.GetEventSerial();
            return Used;
        }

        bool Break()
        {
            FFoundationResult R = A.Execute(Players[0], Make(EFoundationCommandType::MineStart), Vitals);
            for (int32 Step = 0; Step < 4000 && !R.bBroken && R.IsCommitted(); ++Step)
            {
                Steps(1);
                R = A.Execute(Players[0], Make(EFoundationCommandType::MineContinue), Vitals);
            }
            return R.bBroken;
        }

        FVector Centre(const FIntVector& Cell) const
        {
            return FoundationGrid::CellBox(Cell, A.World.OriginCm).GetCenter();
        }
    };

    // Minecraft yaw for a horizontal look (0 south, 90 west, 180 north, 270 east).
    double YawFor(EFoundationFace Look)
    {
        return Look == EFoundationFace::South ? 0
            : Look == EFoundationFace::West   ? 90
            : Look == EFoundationFace::North  ? 180
                                              : 270;
    }

    EFoundationFace ClockWise(EFoundationFace Face)
    {
        return Face == EFoundationFace::North ? EFoundationFace::East
            : Face == EFoundationFace::East   ? EFoundationFace::South
            : Face == EFoundationFace::South  ? EFoundationFace::West
                                              : EFoundationFace::North;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationChestMenuTest, "Unnamed.Foundation.Chest.Menu", ChestTestFlags)

bool FFoundationChestMenuTest::RunTest(const FString&)
{
    // A single chest's menu is the barrel's (vanilla's ChestMenu with 3 rows): 0-26 its slots, 27-53 main, 54-62
    // hotbar.
    const FFoundationMenuLayout& Layout = FFoundationMenuLayout::Get(EFoundationMenuKind::Chest);
    TestEqual(TEXT("63 slots"), Layout.Slots, 63);
    TestTrue(TEXT("the barrel's layout"), &Layout == &FFoundationMenuLayout::Get(EFoundationMenuKind::Barrel));
    for (const TCHAR* Block : {TEXT("chest"), TEXT("trapped_chest")})
    {
        FChestLab L(false, Block);
        L.A.BlockEntities.Clear();
        L.Inv().SetSlot(0, L.Stack(TEXT("dirt"), 10));
        TestTrue(TEXT("its use foreseen"), L.A.WouldUseBlock(L.P(), L.A.Resolve(L.P().Facts)));
        const FFoundationResult Opened = L.Use();
        TestTrue(FString::Printf(TEXT("%s: the chest's menu"), Block),
            Opened.IsCommitted() && Opened.OpenedMenu == EFoundationMenuKind::Chest &&
                L.P().OpenMenu.Block == FName(Block));
        const FFoundationBlockEntity* Entity = L.A.BlockEntities.Find(L.Aimed);
        TestTrue(TEXT("a chest entity with 27 slots"),
            Entity && Entity->Kind == EFoundationBlockEntityKind::Chest && Entity->Items.Num() == 27);
        const int32 Id = Opened.OpenedContainer;
        L.Click(Id, 54, 0, EFoundationClickType::Pickup);
        L.Click(Id, 5, 0, EFoundationClickType::Pickup);
        TestTrue(TEXT("closed"), L.Close().IsCommitted());
        TestTrue(TEXT("kept in the entity"), L.Holds(L.E(L.Aimed).Items[5], TEXT("dirt"), 10));
    }
    // Shift-click both ways (ChestMenu.quickMoveStack): the chest's to the hotbar from its last slot; the player's into
    // the chest from its first.
    {
        FChestLab L;
        L.E(L.Aimed).Items[0] = L.Stack(TEXT("dirt"), 10);
        L.Inv().SetSlot(0, L.Stack(TEXT("stone"), 3));
        const int32 Id = L.Open();
        L.Click(Id, 0, 0, EFoundationClickType::QuickMove);
        TestTrue(TEXT("chest to hotbar 8"), L.Holds(*L.Inv().GetSlot(8), TEXT("dirt"), 10));
        L.Click(Id, 54, 0, EFoundationClickType::QuickMove);
        TestTrue(TEXT("hotbar 0 to chest slot 0"), L.Holds(L.E(L.Aimed).Items[0], TEXT("stone"), 3));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationChestLargeMenuTest, "Unnamed.Foundation.Chest.LargeMenu", ChestTestFlags)

bool FFoundationChestLargeMenuTest::RunTest(const FString&)
{
    // A double chest's menu (ChestMenu with 6 rows): 0-53 its slots, 54-80 main, 81-89 hotbar. Slots 0-26 are the
    // type=right half's and 27-53 the type=left half's (C:\dev\sandbox\inv18\chest_probe.txt), whichever half is used.
    const FFoundationMenuLayout& Layout = FFoundationMenuLayout::Get(EFoundationMenuKind::LargeChest);
    TestEqual(TEXT("90 slots"), Layout.Slots, 90);
    TestTrue(TEXT("0-53 the chest's"), Layout.IsContainer(0) && Layout.IsContainer(53) && !Layout.IsContainer(54));
    TestEqual(TEXT("menu 54 is inventory 9"), Layout.ToInventorySlot(54), FFoundationInventory::FirstMain);
    TestEqual(TEXT("menu 80 is inventory 35"), Layout.ToInventorySlot(80), FFoundationInventory::FirstMain + 26);
    TestEqual(TEXT("menu 81 is hotbar 0"), Layout.ToInventorySlot(81), 0);
    TestEqual(TEXT("menu 89 is hotbar 8"), Layout.ToInventorySlot(89), 8);
    for (const int32 Case : {0, 1, 2})
    {
        // From the left half, from the right half, and a trapped double chest (it behaves as the chest).
        const bool bFromRight = Case == 1;
        FChestLab L(true, Case == 2 ? TEXT("trapped_chest") : TEXT("chest"));
        if (bFromRight) L.Aimed = L.Partner, L.Steps(1);
        const FIntVector Right(2, -1, 1), Left(2, 0, 1);
        L.E(Right).Items[0] = L.Stack(TEXT("dirt"), 1);
        L.E(Left).Items[0] = L.Stack(TEXT("stone"), 2);
        const FFoundationResult Opened = L.Use();
        const TCHAR* From = Case == 2 ? TEXT("trapped")
            : bFromRight              ? TEXT("from the right half")
                                      : TEXT("from the left half");
        TestTrue(FString::Printf(TEXT("%s: the large menu"), From),
            Opened.IsCommitted() && Opened.OpenedMenu == EFoundationMenuKind::LargeChest);
        const TArray<FFoundationItemInstance> Shown = L.A.GetOpenMenu(L.P());
        TestEqual(TEXT("90 shown"), Shown.Num(), 90);
        if (Shown.Num() != 90) continue;
        TestTrue(FString::Printf(TEXT("%s: slot 0 is the right half's 0"), From), L.Holds(Shown[0], TEXT("dirt"), 1));
        TestTrue(FString::Printf(TEXT("%s: slot 27 is the left half's 0"), From), L.Holds(Shown[27], TEXT("stone"), 2));
        // A commit writes both halves.
        const int32 Id = Opened.OpenedContainer;
        L.Click(Id, 0, 0, EFoundationClickType::Pickup);
        L.Click(Id, 30, 0, EFoundationClickType::Pickup);
        TestTrue(TEXT("taken from the right half"), L.E(Right).Items[0].IsEmpty());
        TestTrue(TEXT("put in the left half's slot 3"), L.Holds(L.E(Left).Items[3], TEXT("dirt"), 1));
        // Shift-click: the player's into 0-53 from the first free; the chest's to the hotbar from its last.
        L.Inv().SetSlot(0, L.Stack(TEXT("oak_planks"), 4));
        L.Click(Id, 81, 0, EFoundationClickType::QuickMove);
        TestTrue(TEXT("hotbar 0 to the right half's slot 0"), L.Holds(L.E(Right).Items[0], TEXT("oak_planks"), 4));
        L.Click(Id, 27, 0, EFoundationClickType::QuickMove);
        TestTrue(TEXT("left half's slot 0 to hotbar 8"), L.Holds(*L.Inv().GetSlot(8), TEXT("stone"), 2));
        TestTrue(TEXT("left half's slot 0 empties"), L.E(Left).Items[0].IsEmpty());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationChestPairingTest, "Unnamed.Foundation.Chest.Pairing", ChestTestFlags)

bool FFoundationChestPairingTest::RunTest(const FString&)
{
    // getStateForPlacement (VALUES "Double chests", blocks.chest_neighbor_pairing; chest_probe.txt): facing opposite
    // the look; a single same-id chest of the same facing on the clockwise side makes type=left, on the anticlockwise
    // side type=right.
    FChestLab L;
    const FFoundationBlockRegistry& R = L.A.World.GetRegistry();
    const FFoundationBlockType& Chest = *L.A.World.FindBlock(TEXT("chest"));
    const FIntVector Destination(0, 5, 1);
    for (const EFoundationFace Facing :
        {EFoundationFace::North, EFoundationFace::East, EFoundationFace::South, EFoundationFace::West})
    {
        const TCHAR* Name = FoundationGrid::FaceName(Facing);
        FFoundationPlacementContext Context;
        Context.ClickedCell = Destination - FIntVector(0, 0, 1);
        Context.ClickedFace = EFoundationFace::Up;
        Context.Destination = Destination;
        Context.Yaw = YawFor(FoundationBlockPlacement::GetOpposite(Facing));
        auto Place = [&] { return FoundationChests::GetStateForPlacement(L.A.World, Chest, Context); };
        const FIntVector CW = Destination + FoundationGrid::FaceOffset(ClockWise(Facing));
        const FIntVector CCW = Destination - FoundationGrid::FaceOffset(ClockWise(Facing));
        TestEqual(
            FString::Printf(TEXT("%s: alone single"), Name), FoundationChests::GetType(R, Place()), EChest::Single);
        TestEqual(FString::Printf(TEXT("%s: faces away from the look"), Name), R.GetValue(Place(), TEXT("facing")),
            FName(Name));
        L.A.World.Set(CW, L.ChestState(TEXT("chest"), Name, TEXT("single")));
        TestEqual(FString::Printf(TEXT("%s: clockwise neighbour: left"), Name), FoundationChests::GetType(R, Place()),
            EChest::Left);
        Context.bSecondaryUse = true;
        TestEqual(
            FString::Printf(TEXT("%s: sneaking: single"), Name), FoundationChests::GetType(R, Place()), EChest::Single);
        Context.bSecondaryUse = false;
        L.SetBlock(CW, TEXT("air"));
        L.A.World.Set(CCW, L.ChestState(TEXT("chest"), Name, TEXT("single")));
        TestEqual(FString::Printf(TEXT("%s: anticlockwise neighbour: right"), Name),
            FoundationChests::GetType(R, Place()), EChest::Right);
        L.A.World.Set(CCW, L.ChestState(TEXT("trapped_chest"), Name, TEXT("single")));
        TestEqual(FString::Printf(TEXT("%s: a trapped chest stays apart"), Name), FoundationChests::GetType(R, Place()),
            EChest::Single);
        L.A.World.Set(CCW,
            L.ChestState(TEXT("chest"), FoundationGrid::FaceName(FoundationBlockPlacement::GetOpposite(Facing)),
                TEXT("single")));
        TestEqual(FString::Printf(TEXT("%s: another facing stays apart"), Name), FoundationChests::GetType(R, Place()),
            EChest::Single);
        L.A.World.Set(CCW, L.ChestState(TEXT("chest"), Name, TEXT("left")));
        TestEqual(FString::Printf(TEXT("%s: a half already paired stays apart"), Name),
            FoundationChests::GetType(R, Place()), EChest::Single);
        L.SetBlock(CCW, TEXT("air"));
    }
    // Sneak-clicking a single chest's side joins it, taking its facing: a chest facing north, its east face clicked,
    // the new half east of it is type=right (its partner on the west, anticlockwise of north); the front face does not.
    {
        const FIntVector Clicked(5, 5, 1);
        L.A.World.Set(Clicked, L.ChestState(TEXT("chest"), TEXT("north"), TEXT("single")));
        FFoundationPlacementContext Context;
        Context.ClickedCell = Clicked;
        Context.ClickedFace = EFoundationFace::East;
        Context.Destination = Clicked + FoundationGrid::FaceOffset(EFoundationFace::East);
        Context.Yaw = YawFor(EFoundationFace::West);
        Context.bSecondaryUse = true;
        const FFoundationBlockState Joined = FoundationChests::GetStateForPlacement(L.A.World, Chest, Context);
        TestEqual(TEXT("side click: right"), FoundationChests::GetType(R, Joined), EChest::Right);
        TestEqual(TEXT("side click: the clicked chest's facing"), R.GetValue(Joined, TEXT("facing")), FName("north"));
        Context.ClickedFace = EFoundationFace::West;
        Context.Destination = Clicked + FoundationGrid::FaceOffset(EFoundationFace::West);
        TestEqual(TEXT("west side click: left"),
            FoundationChests::GetType(R, FoundationChests::GetStateForPlacement(L.A.World, Chest, Context)),
            EChest::Left);
        Context.ClickedFace = EFoundationFace::North;
        Context.Destination = Clicked + FoundationGrid::FaceOffset(EFoundationFace::North);
        const FFoundationBlockState Front = FoundationChests::GetStateForPlacement(L.A.World, Chest, Context);
        TestEqual(TEXT("front click: single"), FoundationChests::GetType(R, Front), EChest::Single);
        TestEqual(TEXT("front click: faces away from the look"), R.GetValue(Front, TEXT("facing")), FName("east"));
        // A replaceable block beside the chest, its east face sneak-clicked: it is replaced, and the chest beside the
        // destination is still joined.
        const FIntVector Grass = Clicked + FoundationGrid::FaceOffset(EFoundationFace::East);
        L.SetBlock(Grass, TEXT("short_grass"));
        Context.ClickedCell = Grass;
        Context.ClickedFace = EFoundationFace::East;
        Context.bReplaceClicked = true;
        Context.Destination = Grass;
        TestEqual(TEXT("replacing beside the chest: right"),
            FoundationChests::GetType(R, FoundationChests::GetStateForPlacement(L.A.World, Chest, Context)),
            EChest::Right);
        L.SetBlock(Grass, TEXT("air"));
    }
    // End to end through the place command: a chest placed on the floor south of a single chest (looking east, so
    // facing west, whose clockwise side is north) is type=left and its neighbour turns type=right; sneaking, both stay
    // single.
    for (const bool bSneak : {false, true})
    {
        FChestLab P;
        P.SetBlock(P.Aimed, TEXT("air"));
        const FIntVector Floor(2, 0, 0), Placed(2, 0, 1), North(2, -1, 1);
        P.SetBlock(Floor, TEXT("stone"));
        P.A.World.Set(North, P.ChestState(TEXT("chest"), TEXT("west"), TEXT("single")));
        P.Inv().SetSlot(0, P.Stack(TEXT("chest"), 1));
        P.bSneak = bSneak;
        P.AimAt(0, P.Centre(Floor) + FVector(0, 0, 50));
        const FFoundationResult Result = P.Use();
        const TCHAR* Case = bSneak ? TEXT("sneaking") : TEXT("standing");
        TestTrue(FString::Printf(TEXT("%s: placed"), Case),
            Result.IsCommitted() && P.A.World.GetBlock(P.A.World.Get(Placed).State).Id == FName(TEXT("chest")));
        TestEqual(FString::Printf(TEXT("%s: facing west"), Case), P.FacingAt(Placed), FName("west"));
        TestEqual(FString::Printf(TEXT("%s: the new chest's type"), Case), P.TypeAt(Placed),
            bSneak ? EChest::Single : EChest::Left);
        TestEqual(FString::Printf(TEXT("%s: the neighbour's type"), Case), P.TypeAt(North),
            bSneak ? EChest::Single : EChest::Right);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationChestBreakHalfTest, "Unnamed.Foundation.Chest.BreakHalf", ChestTestFlags)

bool FFoundationChestBreakHalfTest::RunTest(const FString&)
{
    // Breaking a half turns the other single (updateShape) with its slots kept; the broken half drops its own; a
    // viewer's large menu closes (it is no longer valid).
    FChestLab L(true);
    L.A.Options.bItemEntities = true;
    for (FFoundationItemInstance& Slot : L.E(L.Aimed).Items)
        Slot = L.Stack(TEXT("stick"), 1);
    L.E(L.Partner).Items[4] = L.Stack(TEXT("dirt"), 9);
    TestEqual(TEXT("viewer: the large menu"), L.Use(1).OpenedMenu, EFoundationMenuKind::LargeChest);
    L.TakeUsed();
    TestTrue(TEXT("broken"), L.Break());
    L.Steps(1);
    TestEqual(TEXT("the other half turns single"), L.TypeAt(L.Partner), EChest::Single);
    TestTrue(TEXT("its slots stay"), L.Holds(L.E(L.Partner).Items[4], TEXT("dirt"), 9));
    TestNull(TEXT("the broken half's entity goes"), L.A.BlockEntities.Find(L.Aimed));
    TestFalse(TEXT("the viewer's menu closes"), L.P(1).OpenMenu.IsOpen());
    // The half left behind counts its own viewers: none now, so it plays the close, and its next open plays again.
    const TArray<FFoundationEvent> Closed = L.TakeUsed();
    TestTrue(TEXT("the other half plays the close"),
        Closed.Num() == 1 && Closed[0].Interaction == FName(TEXT("block.chest.close")) &&
            Closed[0].LocationCm.Equals(L.Centre(L.Partner), .01));
    L.AimAt(1, L.Centre(L.Partner));
    TestEqual(TEXT("the other half opens single"), L.Use(1).OpenedMenu, EFoundationMenuKind::Chest);
    const TArray<FFoundationEvent> Reopened = L.TakeUsed();
    TestTrue(
        TEXT("and plays the open"), Reopened.Num() == 1 && Reopened[0].Interaction == FName(TEXT("block.chest.open")));
    int32 Sticks = 0, Dirt = 0;
    for (const FFoundationItemEntity& Entity : L.A.GetItemEntities())
    {
        if (Entity.Item == L.Item(TEXT("stick"))) Sticks += Entity.Count;
        if (Entity.Item == L.Item(TEXT("dirt"))) Dirt += Entity.Count;
    }
    TestEqual(TEXT("27 sticks dropped"), Sticks, 27);
    TestEqual(TEXT("the other half's dirt stays in"), Dirt, 0);
    // Replaced by another block (no break), the half left behind turns single too.
    {
        FChestLab R(true);
        R.SetBlock(R.Aimed, TEXT("stone"));
        FoundationBlockFamilies::UpdateNeighbourShapes(R.A.World, R.Aimed);
        TestEqual(TEXT("replaced: the other half turns single"), R.TypeAt(R.Partner), EChest::Single);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationChestOpenStateTest, "Unnamed.Foundation.Chest.OpenState", ChestTestFlags)

bool FFoundationChestOpenStateTest::RunTest(const FString&)
{
    // R16 (survival2.r16.animation_and_viewers): block.chest.open on the first viewer, .close when the last leaves,
    // volume 0.5 (value-swaps.md l.261), from the block's centre; a double chest plays once, from between its halves
    // (provisional).
    {
        FChestLab L;
        L.Open(0);
        TArray<FFoundationEvent> Used = L.TakeUsed();
        TestEqual(TEXT("first viewer: one sound"), Used.Num(), 1);
        if (Used.Num() == 1)
        {
            TestEqual(TEXT("the open sound"), Used[0].Interaction, FName(TEXT("block.chest.open")));
            TestEqual(TEXT("volume 0.5"), Used[0].Amount, .5f);
            TestTrue(TEXT("from the centre"), Used[0].LocationCm.Equals(L.Centre(L.Aimed), .01));
        }
        L.Open(1);
        TestEqual(TEXT("second viewer: no sound"), L.TakeUsed().Num(), 0);
        L.Close(0);
        TestEqual(TEXT("first leaves: no sound"), L.TakeUsed().Num(), 0);
        L.Close(1);
        Used = L.TakeUsed();
        TestTrue(TEXT("last leaves: the close sound"),
            Used.Num() == 1 && Used[0].Interaction == FName(TEXT("block.chest.close")) && Used[0].Amount == .5f);
        TestTrue(TEXT("the state is unchanged"), L.TypeAt(L.Aimed) == EChest::Single);
    }
    {
        FChestLab L(true);
        const FVector Middle = (L.Centre(L.Aimed) + L.Centre(L.Partner)) / 2;
        L.Open(0);
        TArray<FFoundationEvent> Used = L.TakeUsed();
        TestTrue(TEXT("double: one open sound between the halves"),
            Used.Num() == 1 && Used[0].Interaction == FName(TEXT("block.chest.open")) &&
                Used[0].LocationCm.Equals(Middle, .01));
        // The other player opens it from the other half: one viewer more, no sound.
        L.AimAt(1, L.Centre(L.Partner));
        TestEqual(TEXT("double: the other half's viewer"), L.Use(1).OpenedMenu, EFoundationMenuKind::LargeChest);
        TestEqual(TEXT("double: second viewer, no sound"), L.TakeUsed().Num(), 0);
        L.Close(0);
        TestEqual(TEXT("double: first leaves, no sound"), L.TakeUsed().Num(), 0);
        L.Close(1);
        Used = L.TakeUsed();
        TestTrue(TEXT("double: one close sound"),
            Used.Num() == 1 && Used[0].Interaction == FName(TEXT("block.chest.close")));
        // A viewer moving from one half's menu to the other's plays nothing.
        L.Open(0);
        L.TakeUsed();
        L.AimAt(0, L.Centre(L.Partner));
        TestTrue(TEXT("moved: the other half's menu"), L.Use(0).IsCommitted());
        TestEqual(TEXT("moved: no sound"), L.TakeUsed().Num(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationChestBlockedTest, "Unnamed.Foundation.Chest.Blocked", ChestTestFlags)

bool FFoundationChestBlockedTest::RunTest(const FString&)
{
    // A chest does not open under a conductive block (minecraft.wiki "Chest"; a full opaque cube, provisional): the
    // click is used (a held block does not place) and nothing opens. A slab or a torch does not block.
    for (const bool bDouble : {false, true})
    {
        const TCHAR* Case = bDouble ? TEXT("double, stone over the other half") : TEXT("single, stone above");
        FChestLab L(bDouble);
        L.A.BlockEntities.Clear();
        const FIntVector Over = (bDouble ? L.Partner : L.Aimed) + FIntVector(0, 0, 1);
        L.SetBlock(Over, TEXT("stone"));
        L.Inv().SetSlot(0, L.Stack(TEXT("dirt"), 4));
        const FFoundationResult Result = L.Use();
        TestTrue(FString::Printf(TEXT("%s: the click is used"), Case), Result.IsCommitted());
        TestFalse(FString::Printf(TEXT("%s: no menu"), Case), L.P().OpenMenu.IsOpen());
        TestEqual(FString::Printf(TEXT("%s: nothing placed"), Case), L.Inv().GetSlot(0)->Count, 4);
        TestNull(FString::Printf(TEXT("%s: no entity"), Case), L.A.BlockEntities.Find(L.Aimed));
        TestNull(FString::Printf(TEXT("%s: none for the other half"), Case), L.A.BlockEntities.Find(L.Partner));
        TestEqual(FString::Printf(TEXT("%s: no sound"), Case), L.TakeUsed().Num(), 0);
    }
    for (const TCHAR* Above : {TEXT("stone_slab"), TEXT("torch"), TEXT("glass")})
    {
        FChestLab L;
        L.SetBlock(L.Aimed + FIntVector(0, 0, 1), Above);
        TestEqual(FString::Printf(TEXT("%s above: opens"), Above), L.Use().OpenedMenu, EFoundationMenuKind::Chest);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationChestSwitchTest, "Unnamed.Foundation.Chest.Switch0", ChestTestFlags)

bool FFoundationChestSwitchTest::RunTest(const FString&)
{
    // foundation.Chest 0 is as before: no use, so planks in hand place against it and nothing is made.
    FChestLab L;
    L.A.BlockEntities.Clear();
    L.A.Options.bChest = false;
    L.Inv().SetSlot(0, L.Stack(TEXT("oak_planks"), 4));
    TestFalse(TEXT("off: no use foreseen"), L.A.WouldUseBlock(L.P(), L.A.Resolve(L.P().Facts)));
    const FFoundationResult Result = L.Use();
    const FName Placed =
        L.A.World.IsKnown(Result.Cell) ? L.A.World.GetBlock(L.A.World.Get(Result.Cell).State).Id : NAME_None;
    TestTrue(TEXT("off: the planks place against it"),
        Result.IsCommitted() && Result.OpenedMenu == EFoundationMenuKind::Inventory && Result.Cell != L.Aimed &&
            Placed == TEXT("oak_planks"));
    TestFalse(TEXT("off: no menu"), L.P().OpenMenu.IsOpen());
    TestNull(TEXT("off: no entity"), L.A.BlockEntities.Find(L.Aimed));
    TestEqual(TEXT("off: no sound"), L.TakeUsed().Num(), 0);
    return true;
}
#endif
