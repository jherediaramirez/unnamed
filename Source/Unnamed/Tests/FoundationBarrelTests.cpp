#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationContainer.h"
#include "Foundation/FoundationContainers.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags BarrelTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // T1 step 9's lab: the project's rules and blocks, the full inventory, block use and barrels on, two players with
    // empty inventories, both eyes looking at a barrel.
    struct FBarrelLab
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
        int64 LastSerial = 0;

        explicit FBarrelLab(const TCHAR* Facing = nullptr)
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bInfiniteSupply = false;
            A.Options.bFullInventory = true;
            A.Options.bBlockUse = true;
            A.Options.bBarrel = true;
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
            FFoundationBlockState State = A.World.FindBlock(TEXT("barrel"))->GetDefaultState();
            if (Facing) State = A.World.GetRegistry().WithValue(State, TEXT("facing"), Facing);
            A.World.Set(Aimed, State);
            Steps(1);
            LastSerial = A.GetEventSerial();
        }

        FFoundationPlayer& P(int32 Who = 0) { return *A.FindPlayer(Players[Who]); }
        FFoundationInventory& Inv(int32 Who = 0) { return P(Who).Inventory; }
        int32 Item(const TCHAR* Id) const { return A.Rules.FindItem(Id); }
        FFoundationItemInstance Stack(const TCHAR* Id, int32 Count) { return Inv().MakeStack(Item(Id), Count); }
        bool Holds(const FFoundationItemInstance& Slot, const TCHAR* Id, int32 Count) const
        {
            return Count == 0 ? Slot.IsEmpty() : !Slot.IsEmpty() && Slot.Item == Item(Id) && Slot.Count == Count;
        }

        // Count reference steps, both players aiming at the barrel.
        void Steps(int32 Count)
        {
            for (int32 Index = 0; Index < Count; ++Index)
            {
                ++StepIndex;
                for (const int32 Id : Players)
                    if (A.FindPlayer(Id))
                    {
                        FFoundationPlayerFacts Facts;
                        Facts.FeetCm = Eye - FVector(0, 0, 162);
                        Facts.EyeCm = Eye;
                        Facts.Aim =
                            (FoundationGrid::CellBox(Aimed, A.World.OriginCm).GetCenter() - Eye).GetSafeNormal();
                        Facts.Step = StepIndex;
                        A.SetFacts(Id, Facts);
                    }
                A.Step();
            }
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
                Command.Destination = Destination;
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

        FEntity& B() { return A.BlockEntities.FindOrAdd(Aimed, EFoundationBlockEntityKind::Barrel, TEXT("barrel")); }
        bool IsOpenState() const
        {
            return A.World.GetRegistry().GetValue(A.World.Get(Aimed).State, TEXT("open")) == FName(TEXT("true"));
        }

        // The BlockUsed events since the last call.
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
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBarrelMenuTest, "Unnamed.Foundation.Barrel.Menu", BarrelTestFlags)

bool FFoundationBarrelMenuTest::RunTest(const FString&)
{
    // The barrel's menu (ACTION-PLAN.md Line 1 #4, vanilla's ChestMenu with 3 rows): 0-26 its slots, 27-53 the main
    // grid, 54-62 the hotbar.
    const FFoundationMenuLayout& Layout = FFoundationMenuLayout::Get(EFoundationMenuKind::Barrel);
    TestEqual(TEXT("63 slots"), Layout.Slots, 63);
    TestTrue(
        TEXT("0 and 26 are the barrel's"), Layout.IsContainer(0) && Layout.IsContainer(26) && !Layout.IsContainer(27));
    TestEqual(TEXT("container slot: no inventory slot"), Layout.ToInventorySlot(0), int32(INDEX_NONE));
    TestEqual(TEXT("menu 27 is inventory 9"), Layout.ToInventorySlot(27), FFoundationInventory::FirstMain);
    TestEqual(TEXT("menu 53 is inventory 35"), Layout.ToInventorySlot(53), FFoundationInventory::FirstMain + 26);
    TestEqual(TEXT("menu 54 is hotbar 0"), Layout.ToInventorySlot(54), 0);
    TestEqual(TEXT("menu 62 is hotbar 8"), Layout.ToInventorySlot(62), 8);

    // The use opens the barrel's menu over its new 27-slot entity; what goes in stays in it across a close.
    {
        FBarrelLab L;
        L.A.BlockEntities.Clear();
        L.Inv().SetSlot(0, L.Stack(TEXT("dirt"), 10));
        TestTrue(TEXT("its use foreseen"), L.A.WouldUseBlock(L.P(), L.A.Resolve(L.P().Facts)));
        const FFoundationResult Opened = L.Use();
        TestTrue(TEXT("the barrel's menu"),
            Opened.IsCommitted() && Opened.OpenedMenu == EFoundationMenuKind::Barrel &&
                L.P().OpenMenu.Block == FName(TEXT("barrel")));
        const FFoundationBlockEntity* Entity = L.A.BlockEntities.Find(L.Aimed);
        TestTrue(TEXT("a barrel entity with 27 slots"),
            Entity && Entity->Kind == EFoundationBlockEntityKind::Barrel && Entity->Items.Num() == 27);
        TestEqual(TEXT("the open menu shows 63 slots"), L.A.GetOpenMenu(L.P()).Num(), 63);
        const int32 Id = Opened.OpenedContainer;
        L.Click(Id, 54, 0, EFoundationClickType::Pickup);
        TestTrue(TEXT("placed in barrel slot 5"), L.Click(Id, 5, 0, EFoundationClickType::Pickup).IsCommitted());
        TestTrue(TEXT("closed"), L.Close().IsCommitted());
        TestTrue(TEXT("kept in the entity"), L.Holds(L.B().Items[5], TEXT("dirt"), 10));
        const int32 Again = L.Open();
        TestTrue(TEXT("reopened: slot 5 shows it"), L.Holds(L.A.GetOpenMenu(L.P())[5], TEXT("dirt"), 10) && Again > 0);
    }

    // Shift-click from the barrel (ChestMenu.quickMoveStack): to the hotbar from its last slot, then the main grid from
    // its last.
    {
        FBarrelLab L;
        L.B().Items[0] = L.Stack(TEXT("dirt"), 10);
        const int32 Id = L.Open();
        TestTrue(TEXT("barrel to player"), L.Click(Id, 0, 0, EFoundationClickType::QuickMove).IsCommitted());
        TestTrue(TEXT("lands in hotbar 8"),
            L.Holds(L.Inv().GetSlot(8) ? *L.Inv().GetSlot(8) : FFoundationItemInstance(), TEXT("dirt"), 10));
        TestTrue(TEXT("the barrel slot empties"), L.B().Items[0].IsEmpty());
        for (int32 Slot = 0; Slot < FFoundationInventory::HotbarSlots; ++Slot)
            L.Inv().SetSlot(Slot, L.Stack(TEXT("stone"), 64));
        L.B().Items[1] = L.Stack(TEXT("dirt"), 7);
        L.Click(Id, 1, 0, EFoundationClickType::QuickMove);
        TestTrue(TEXT("with the hotbar full: main 35 (menu 53)"),
            L.Holds(*L.Inv().GetSlot(FFoundationInventory::FirstMain + 26), TEXT("dirt"), 7));
    }
    // Shift-click from the player: into the barrel from its first slot, merging first; with no room it stays (no main
    // and hotbar trade, unlike the furnace).
    {
        FBarrelLab L;
        L.B().Items[0] = L.Stack(TEXT("dirt"), 10);
        L.B().Items[2] = L.Stack(TEXT("dirt"), 60);
        L.Inv().SetSlot(0, L.Stack(TEXT("dirt"), 64));
        const int32 Id = L.Open();
        L.Click(Id, 54, 0, EFoundationClickType::QuickMove);
        TestTrue(TEXT("merged into slot 0"), L.Holds(L.B().Items[0], TEXT("dirt"), 64));
        TestTrue(TEXT("merged into slot 2"), L.Holds(L.B().Items[2], TEXT("dirt"), 64));
        TestTrue(TEXT("the rest to slot 1"), L.Holds(L.B().Items[1], TEXT("dirt"), 6));
        TestTrue(TEXT("the hotbar slot empties"), L.Inv().GetSlot(0)->IsEmpty());
        for (FFoundationItemInstance& Slot : L.B().Items)
            Slot = L.Stack(TEXT("stone"), 64);
        L.Inv().SetSlot(1, L.Stack(TEXT("dirt"), 5));
        L.Click(Id, 55, 0, EFoundationClickType::QuickMove);
        TestTrue(TEXT("full barrel: stays in hotbar 1"), L.Holds(*L.Inv().GetSlot(1), TEXT("dirt"), 5));
        TestTrue(
            TEXT("full barrel: main 35 stays empty"), L.Inv().GetSlot(FFoundationInventory::FirstMain + 26)->IsEmpty());
    }

    // Broken, the barrel drops its 27 slots (8a1's path) and its entity goes.
    {
        FBarrelLab L;
        L.A.Options.bItemEntities = true;
        for (FFoundationItemInstance& Slot : L.B().Items)
            Slot = L.Stack(TEXT("stick"), 1);
        TestTrue(TEXT("broken"), L.Break());
        L.Steps(1);
        TestNull(TEXT("entity gone"), L.A.BlockEntities.Find(L.Aimed));
        int32 Sticks = 0;
        for (const FFoundationItemEntity& Entity : L.A.GetItemEntities())
            if (Entity.Item == L.Item(TEXT("stick"))) Sticks += Entity.Count;
        TestEqual(TEXT("27 sticks dropped"), Sticks, 27);
    }

    // Replaced without a break (a command), its entity goes, so a barrel placed there later starts empty.
    {
        FBarrelLab L;
        L.B().Items[0] = L.Stack(TEXT("dirt"), 1);
        L.A.World.Set(L.Aimed, L.A.World.FindBlock(TEXT("stone"))->GetDefaultState());
        L.Steps(1);
        TestNull(TEXT("replaced: entity gone"), L.A.BlockEntities.Find(L.Aimed));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBarrelSwitchTest, "Unnamed.Foundation.Barrel.Switch0", BarrelTestFlags)

bool FFoundationBarrelSwitchTest::RunTest(const FString&)
{
    // foundation.Barrel 0 is as before step 9: no use, so planks in hand place against it and nothing is made.
    FBarrelLab L;
    L.A.BlockEntities.Clear();
    L.A.Options.bBarrel = false;
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
    TestFalse(TEXT("off: stays closed"), L.IsOpenState());
    // QC RA1-2: switched off with its menu open, a click on it is refused at once and the menu closes at the next step.
    {
        FBarrelLab Open;
        Open.B().Items[0] = Open.Stack(TEXT("dirt"), 3);
        const int32 Id = Open.Open();
        TestTrue(TEXT("open"), Id > 0 && Open.P().OpenMenu.IsOpen());
        Open.A.Options.bBarrel = false;
        TestFalse(TEXT("off: a click on it is refused before the step"),
            Open.Click(Id, 0, 0, EFoundationClickType::Pickup).IsCommitted());
        TestTrue(TEXT("off: the barrel keeps its slot"), Open.Holds(Open.B().Items[0], TEXT("dirt"), 3));
        Open.Steps(1);
        TestFalse(TEXT("off: the open menu closes"), Open.P().OpenMenu.IsOpen());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBarrelOpenStateTest, "Unnamed.Foundation.Barrel.OpenState", BarrelTestFlags)

bool FFoundationBarrelOpenStateTest::RunTest(const FString&)
{
    // SURVIVAL-RUN2-FINDINGS R16 (survival2.r16.animation_and_viewers): the first viewer opens the barrel and plays
    // block.barrel.open; the last one leaving closes it and plays block.barrel.close; the state switches on these
    // transitions. Volume 0.5 (value-swaps.md "Chest / barrel sounds"), from the facing face's centre (R16's barrel
    // rows).
    {
        FBarrelLab L;
        const FVector Centre = FoundationGrid::CellBox(L.Aimed, L.A.World.OriginCm).GetCenter();
        TestFalse(TEXT("placed closed"), L.IsOpenState());
        L.Open(0);
        TestTrue(TEXT("first viewer: open"), L.IsOpenState());
        TArray<FFoundationEvent> Used = L.TakeUsed();
        TestEqual(TEXT("first viewer: one sound"), Used.Num(), 1);
        if (Used.Num() == 1)
        {
            TestEqual(TEXT("the open sound"), Used[0].Interaction, FName(TEXT("block.barrel.open")));
            TestEqual(TEXT("volume 0.5"), Used[0].Amount, .5f);
            // Facing north (the default): half a block toward -Y.
            TestTrue(TEXT("from the north face's centre"), Used[0].LocationCm.Equals(Centre + FVector(0, -50, 0), .01));
            TestEqual(TEXT("its cell"), Used[0].Cell, L.Aimed);
        }
        L.Open(1);
        TestEqual(TEXT("second viewer: no sound"), L.TakeUsed().Num(), 0);
        TestTrue(TEXT("second viewer: open"), L.IsOpenState());
        L.Close(0);
        TestEqual(TEXT("first leaves: no sound"), L.TakeUsed().Num(), 0);
        TestTrue(TEXT("first leaves: still open"), L.IsOpenState());
        L.Close(1);
        TestFalse(TEXT("last leaves: closed"), L.IsOpenState());
        Used = L.TakeUsed();
        TestTrue(TEXT("last leaves: the close sound"),
            Used.Num() == 1 && Used[0].Interaction == FName(TEXT("block.barrel.close")) && Used[0].Amount == .5f);
        L.Steps(3);
        TestEqual(TEXT("nothing more"), L.TakeUsed().Num(), 0);
    }
    // A viewer that leaves the game closes it too.
    {
        FBarrelLab L;
        L.Open(1);
        L.TakeUsed();
        L.A.RemovePlayer(L.Players[1]);
        TestFalse(TEXT("viewer removed: closed"), L.IsOpenState());
        TestEqual(TEXT("viewer removed: the close sound"), L.TakeUsed().Num(), 1);
    }
    // Facing up, the sound comes from the top face.
    {
        FBarrelLab L(TEXT("up"));
        const FVector Centre = FoundationGrid::CellBox(L.Aimed, L.A.World.OriginCm).GetCenter();
        L.Open(0);
        const TArray<FFoundationEvent> Used = L.TakeUsed();
        TestTrue(TEXT("from the top face's centre"),
            Used.Num() == 1 && Used[0].LocationCm.Equals(Centre + FVector(0, 0, 50), .01));
    }
    // Another menu opened over it (a crafting table) leaves the barrel: it closes with its sound.
    {
        FBarrelLab L;
        L.Open(0);
        L.TakeUsed();
        FFoundationResult Table;
        L.A.OpenContainer(L.P(), EFoundationMenuKind::Crafting, FIntVector(3, 3, 1), TEXT("crafting_table"), Table);
        TestTrue(TEXT("another menu: opened"), Table.IsCommitted());
        TestFalse(TEXT("another menu: the barrel closes"), L.IsOpenState());
        const TArray<FFoundationEvent> Used = L.TakeUsed();
        TestTrue(TEXT("another menu: the close sound"),
            Used.Num() == 1 && Used[0].Interaction == FName(TEXT("block.barrel.close")));
    }
    // Walking out of reach closes the menu (StepOpenMenus) and the barrel, with its sound.
    {
        FBarrelLab L;
        L.Open(0);
        L.TakeUsed();
        L.Eye = FVector(1800, 1800, 150);
        L.Steps(1);
        TestFalse(TEXT("out of reach: menu closed"), L.P().OpenMenu.IsOpen());
        TestFalse(TEXT("out of reach: the barrel closes"), L.IsOpenState());
        const TArray<FFoundationEvent> Used = L.TakeUsed();
        TestTrue(TEXT("out of reach: the close sound"),
            Used.Num() == 1 && Used[0].Interaction == FName(TEXT("block.barrel.close")));
    }
    // Broken while viewed: the menu closes, with no close sound (vanilla's removed block entity plays none).
    {
        FBarrelLab L;
        L.Open(0);
        L.TakeUsed();
        TestTrue(TEXT("broken"), L.Break());
        L.Steps(1);
        TestFalse(TEXT("broken: menu closed"), L.P().OpenMenu.IsOpen());
        TestEqual(TEXT("broken: no close sound"), L.TakeUsed().Num(), 0);
    }
    // B3a's doors keep their sound unchanged: their BlockUsed (CommitBlockUse, a trapdoor opened) has no place and,
    // since B3b, Amount the play volume 1 (CommitBlockUse's default), so the view plays it at the cell's centre at
    // volume 1.
    {
        FBarrelLab L;
        const FIntVector Cell(3, 3, 1);
        const FFoundationBlockState Trapdoor = L.A.World.FindBlock(TEXT("oak_trapdoor"))->GetDefaultState();
        L.A.World.Set(Cell, Trapdoor);
        L.TakeUsed();
        const TPair<FIntVector, FFoundationBlockState> Write{
            Cell, L.A.World.GetRegistry().WithValue(Trapdoor, TEXT("open"), TEXT("true"))};
        FFoundationResult Result;
        L.A.CommitBlockUse(L.P(), Cell, MakeArrayView(&Write, 1), TEXT("block.wooden_trapdoor.open"), Result);
        const TArray<FFoundationEvent> Used = L.TakeUsed();
        TestTrue(TEXT("trapdoor: one BlockUsed at volume 1 with no place"),
            Used.Num() == 1 && Used[0].Amount == 1 && Used[0].LocationCm.IsZero());
    }
    return true;
}
#endif
