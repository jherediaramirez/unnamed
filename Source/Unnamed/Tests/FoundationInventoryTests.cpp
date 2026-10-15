#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationContainer.h"
#include "Foundation/FoundationInteractionSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags InvTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Items by index for the model tests: stone and dirt stack to 64, the others to 1.
    enum : int32
    {
        InvStone,
        InvDirt,
        InvPick,
        InvHelmet,
        InvShield
    };

    FFoundationContainerRules MakeInvRules()
    {
        FFoundationContainerRules Rules;
        Rules.GetMaxStack = [](int32 Item) { return Item == InvStone || Item == InvDirt ? 64 : 1; };
        Rules.GetEquipment = [](int32 Item)
        {
            return Item == InvHelmet ? EFoundationEquipmentSlot::Head
                : Item == InvShield  ? EFoundationEquipmentSlot::Offhand
                                     : EFoundationEquipmentSlot::None;
        };
        return Rules;
    }

    FFoundationInventory MakeInvInventory()
    {
        FFoundationInventory Inventory;
        Inventory.bFull = true;
        return Inventory;
    }

    // Menu slots of the player's container.
    int32 HotbarMenuSlot(int32 Index)
    {
        return FFoundationContainerMenu::FirstHotbar + Index;
    }
    int32 MainMenuSlot(int32 Index)
    {
        return FFoundationContainerMenu::FirstMain + Index;
    }
    constexpr int32 HeadMenuSlot = FFoundationContainerMenu::FirstArmor;
    constexpr int32 OffhandMenuSlot = FFoundationContainerMenu::OffhandSlot;

    bool HoldsStack(const FFoundationItemInstance& Stack, int32 Item, int32 Count)
    {
        return Count == 0 ? Stack.IsEmpty() : !Stack.IsEmpty() && Stack.Item == Item && Stack.Count == Count;
    }

    // An authority with the project's items and one player, for the command tests.
    struct FInventoryLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        int32 Player = 0;
        uint32 Sequence = 1;

        FInventoryLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.Options.bFullInventory = true;
            Player = A.AddPlayer();
            A.Reset();
        }
        FFoundationInventory& Inv() { return A.FindPlayer(Player)->Inventory; }
        FFoundationResult Click(int32 Slot, int32 Button, EFoundationClickType Type, int32 Container = 0)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = Sequence++;
            Command.Type = EFoundationCommandType::ContainerClick;
            Command.ContainerId = Container;
            Command.Slot = Slot;
            Command.Button = Button;
            Command.ClickType = Type;
            return A.Execute(Player, Command, Vitals);
        }
        FFoundationResult Use(EFoundationHand Hand = EFoundationHand::Main)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = Sequence++;
            Command.Type = EFoundationCommandType::UseItemStart;
            Command.Hand = Hand;
            Command.Item = Inv().GetHand(Hand).GetRef();
            return A.Execute(Player, Command, Vitals);
        }
        int32 CountEquipped(int32 Item) const
        {
            return A.GetEvents()
                .FilterByPredicate([Item](const FFoundationEvent& Event)
                    { return Event.Kind == EFoundationEventKind::Equipped && Event.Item == Item; })
                .Num();
        }
        FFoundationResult Close()
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = Sequence++;
            Command.Type = EFoundationCommandType::ContainerClose;
            return A.Execute(Player, Command, Vitals);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationInventoryFillOrderTest, "Unnamed.Foundation.Inventory.FillOrder", InvTestFlags)

bool FFoundationInventoryFillOrderTest::RunTest(const FString&)
{
    // Merges first: the selected stack, the offhand, then the hotbar in order.
    FFoundationInventory I = MakeInvInventory();
    I.Select(2);
    I.SetSlot(0, I.MakeStack(InvStone, 10));
    I.SetSlot(2, I.MakeStack(InvStone, 60));
    I.SetSlot(FFoundationInventory::OffhandSlot, I.MakeStack(InvStone, 62));
    I.SetSlot(9, I.MakeStack(InvStone, 1));
    TestEqual(TEXT("all ten fit"), I.Add(InvStone, 10, 64), 10);
    TestTrue(TEXT("selected, then offhand, then hotbar 0; the main stack untouched"),
        HoldsStack(I.Hotbar[2], InvStone, 64) && HoldsStack(I.Offhand, InvStone, 64) &&
            HoldsStack(I.Hotbar[0], InvStone, 14) && HoldsStack(I.Main[0], InvStone, 1));
    // A main-grid stack merges before an empty hotbar slot takes a new one.
    I = MakeInvInventory();
    I.SetSlot(13, I.MakeStack(InvDirt, 10));
    I.Add(InvDirt, 5, 64);
    TestTrue(
        TEXT("merge into main before a free hotbar slot"), HoldsStack(I.Main[4], InvDirt, 15) && I.Hotbar[0].IsEmpty());
    // Free slots: the hotbar first, then the main grid.
    I = MakeInvInventory();
    for (int32 Slot = 0; Slot < 9; ++Slot)
        if (Slot != 7) I.SetSlot(Slot, I.MakeStack(InvPick, 1));
    I.Add(InvDirt, 3, 64);
    TestTrue(TEXT("the free hotbar slot first"), HoldsStack(I.Hotbar[7], InvDirt, 3) && I.Main[0].IsEmpty());
    I.SetSlot(7, I.MakeStack(InvPick, 1));
    TestEqual(TEXT("space counts the main grid"), I.GetSpaceFor(InvDirt, 64), 27 * 64);
    TestEqual(TEXT("then the main grid, spilling over"), I.Add(InvDirt, 100, 64), 100);
    TestTrue(
        TEXT("main 0 full, main 1 the rest"), HoldsStack(I.Main[0], InvDirt, 64) && HoldsStack(I.Main[1], InvDirt, 36));
    // The lab inventory (the switch at 0): the hotbar and offhand only, as before step 5a.
    I.bFull = false;
    TestEqual(TEXT("lab: no space beyond the hotbar"), I.GetSpaceFor(InvDirt, 64), 0);
    TestEqual(TEXT("lab: nothing added"), I.Add(InvDirt, 5, 64), 0);
    TestNull(TEXT("lab: no main slot"), I.GetSlot(9));
    TestNull(TEXT("lab: no armor slot"), I.GetSlot(36));
    TestNotNull(TEXT("lab: the offhand is slot 40"), I.GetSlot(FFoundationInventory::OffhandSlot));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationInventoryQuickMoveTest, "Unnamed.Foundation.Inventory.QuickMove", InvTestFlags)

bool FFoundationInventoryQuickMoveTest::RunTest(const FString&)
{
    const FFoundationContainerRules Rules = MakeInvRules();
    FFoundationInventory I = MakeInvInventory();
    FFoundationContainerMenu Menu(I, Rules, false);
    // The hotbar goes to the main grid, merging first.
    I.SetSlot(0, I.MakeStack(InvStone, 40));
    I.SetSlot(12, I.MakeStack(InvStone, 50));
    const int32 Id = I.Hotbar[0].InstanceId;
    Menu.Click(HotbarMenuSlot(0), 0, EFoundationClickType::QuickMove);
    TestTrue(TEXT("hotbar to main: merge 14, then the first empty main slot"),
        HoldsStack(I.Main[3], InvStone, 64) && HoldsStack(I.Main[0], InvStone, 26) && I.Hotbar[0].IsEmpty());
    TestEqual(TEXT("the rest keeps its identity"), I.Main[0].InstanceId, Id);
    // The main grid goes to the hotbar.
    Menu.Click(MainMenuSlot(0), 0, EFoundationClickType::QuickMove);
    TestTrue(TEXT("main to the hotbar"), HoldsStack(I.Hotbar[0], InvStone, 26) && I.Main[0].IsEmpty());
    // A wearable item goes to its empty armor slot; from there back to the main grid.
    I.SetSlot(3, I.MakeStack(InvHelmet, 1));
    Menu.Click(HotbarMenuSlot(3), 0, EFoundationClickType::QuickMove);
    TestTrue(
        TEXT("helmet to the head slot (inventory 39)"), HoldsStack(I.Armor[3], InvHelmet, 1) && I.Hotbar[3].IsEmpty());
    Menu.Click(HeadMenuSlot, 0, EFoundationClickType::QuickMove);
    TestTrue(
        TEXT("armor back to the first empty main slot"), HoldsStack(I.Main[0], InvHelmet, 1) && I.Armor[3].IsEmpty());
    // With the head slot taken, a second helmet trades between main and hotbar instead.
    I.SetSlot(39, I.MakeStack(InvHelmet, 1));
    Menu.Click(MainMenuSlot(0), 0, EFoundationClickType::QuickMove);
    TestTrue(
        TEXT("armor slot taken: main to the hotbar"), HoldsStack(I.Hotbar[1], InvHelmet, 1) && I.Main[0].IsEmpty());
    // An offhand item goes to an empty offhand; the offhand goes back to the main grid.
    I.SetSlot(5, I.MakeStack(InvShield, 1));
    Menu.Click(HotbarMenuSlot(5), 0, EFoundationClickType::QuickMove);
    TestTrue(TEXT("shield to the offhand"), HoldsStack(I.Offhand, InvShield, 1));
    Menu.Click(OffhandMenuSlot, 0, EFoundationClickType::QuickMove);
    TestTrue(TEXT("offhand to main"), HoldsStack(I.Main[0], InvShield, 1) && I.Offhand.IsEmpty());
    // The offhand is routed by its item like any slot: a helmet there goes to an empty head slot.
    I.SetSlot(39, {});
    I.SetSlot(FFoundationInventory::OffhandSlot, I.MakeStack(InvHelmet, 1));
    Menu.Click(OffhandMenuSlot, 0, EFoundationClickType::QuickMove);
    TestTrue(
        TEXT("a helmet in the offhand to the head slot"), HoldsStack(I.Armor[3], InvHelmet, 1) && I.Offhand.IsEmpty());
    // Nothing fits: nothing moves.
    FFoundationInventory Full = MakeInvInventory();
    FFoundationContainerMenu FullMenu(Full, Rules, false);
    for (int32 Slot = 9; Slot < 36; ++Slot)
        Full.SetSlot(Slot, Full.MakeStack(InvPick, 1));
    Full.SetSlot(0, Full.MakeStack(InvDirt, 5));
    TestFalse(TEXT("a full main grid takes nothing"),
        FullMenu.Click(HotbarMenuSlot(0), 0, EFoundationClickType::QuickMove).bChanged);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInventoryPickupTest, "Unnamed.Foundation.Inventory.Pickup", InvTestFlags)

bool FFoundationInventoryPickupTest::RunTest(const FString&)
{
    const FFoundationContainerRules Rules = MakeInvRules();
    FFoundationInventory I = MakeInvInventory();
    FFoundationContainerMenu Menu(I, Rules, false);
    I.SetSlot(9, I.MakeStack(InvStone, 33));
    const int32 Revision = I.Revision;
    Menu.Click(MainMenuSlot(0), 1, EFoundationClickType::Pickup);
    TestTrue(TEXT("right takes the larger half"),
        HoldsStack(I.Carried, InvStone, 17) && HoldsStack(I.Main[0], InvStone, 16));
    TestTrue(TEXT("a click bumps the inventory revision"), I.Revision > Revision);
    Menu.Click(MainMenuSlot(1), 1, EFoundationClickType::Pickup);
    TestTrue(TEXT("right places one"), HoldsStack(I.Main[1], InvStone, 1) && HoldsStack(I.Carried, InvStone, 16));
    Menu.Click(MainMenuSlot(0), 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("left merges"), HoldsStack(I.Main[0], InvStone, 32) && I.Carried.IsEmpty());
    Menu.Click(MainMenuSlot(0), 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("left takes all"), HoldsStack(I.Carried, InvStone, 32) && I.Main[0].IsEmpty());
    I.SetSlot(10, I.MakeStack(InvDirt, 3));
    Menu.Click(MainMenuSlot(1), 0, EFoundationClickType::Pickup);
    // Checked after the first click, so a no-op cannot pass (Codex 5a finding 5).
    TestTrue(
        TEXT("left on another item swaps"), HoldsStack(I.Main[1], InvStone, 32) && HoldsStack(I.Carried, InvDirt, 3));
    Menu.Click(MainMenuSlot(1), 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("and swaps back"), HoldsStack(I.Main[1], InvDirt, 3) && HoldsStack(I.Carried, InvStone, 32));
    // Armor slots take only their own slot's items, one at a time.
    FFoundationClickResult R = Menu.Click(HeadMenuSlot, 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("stone refused by the head slot"), !R.bChanged && I.Armor[3].IsEmpty());
    I.Carried = I.MakeStack(InvHelmet, 1);
    Menu.Click(HeadMenuSlot, 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("a helmet goes on"), HoldsStack(I.Armor[3], InvHelmet, 1) && I.Carried.IsEmpty());
    // Outside the window: the cursor stack is thrown (left all, right one).
    I.Carried = I.MakeStack(InvDirt, 5);
    R = Menu.Click(FFoundationContainerMenu::Outside, 1, EFoundationClickType::Pickup);
    TestTrue(TEXT("right outside throws one"), HoldsStack(R.Thrown, InvDirt, 1) && HoldsStack(I.Carried, InvDirt, 4));
    R = Menu.Click(FFoundationContainerMenu::Outside, 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("left outside throws the stack"), HoldsStack(R.Thrown, InvDirt, 4) && I.Carried.IsEmpty());
    // Menu slot 0 is the crafting result (survival step 6): a slot, empty without a recipe, which never takes.
    R = Menu.Click(FFoundationContainerMenu::ResultSlot, 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("menu slot 0 (the crafting result) is a slot"), R.bValid && !R.bChanged);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationInventorySwapCloneThrowTest, "Unnamed.Foundation.Inventory.SwapCloneThrow", InvTestFlags)

bool FFoundationInventorySwapCloneThrowTest::RunTest(const FString&)
{
    const FFoundationContainerRules Rules = MakeInvRules();
    FFoundationInventory I = MakeInvInventory();
    FFoundationContainerMenu Menu(I, Rules, false);
    I.SetSlot(9, I.MakeStack(InvStone, 20));
    I.SetSlot(4, I.MakeStack(InvDirt, 2));
    Menu.Click(MainMenuSlot(0), 4, EFoundationClickType::Swap);
    TestTrue(TEXT("number key 5 swaps with hotbar 4"),
        HoldsStack(I.Main[0], InvDirt, 2) && HoldsStack(I.Hotbar[4], InvStone, 20));
    Menu.Click(MainMenuSlot(0), FFoundationInventory::OffhandSlot, EFoundationClickType::Swap);
    TestTrue(TEXT("F swaps with the offhand"), HoldsStack(I.Offhand, InvDirt, 2) && I.Main[0].IsEmpty());
    Menu.Click(HeadMenuSlot, 4, EFoundationClickType::Swap);
    TestTrue(
        TEXT("a swap into the head slot refuses stone"), I.Armor[3].IsEmpty() && HoldsStack(I.Hotbar[4], InvStone, 20));
    TestFalse(TEXT("button 9 is no key"), Menu.Click(MainMenuSlot(0), 9, EFoundationClickType::Swap).bValid);
    // CLONE: creative only, a full stack on an empty cursor.
    Menu.Click(HotbarMenuSlot(4), 2, EFoundationClickType::Clone);
    TestTrue(TEXT("no clone in survival"), I.Carried.IsEmpty());
    FFoundationContainerMenu Creative(I, Rules, true);
    Creative.Click(HotbarMenuSlot(4), 2, EFoundationClickType::Clone);
    TestTrue(TEXT("creative clone: a full stack"),
        HoldsStack(I.Carried, InvStone, 64) && HoldsStack(I.Hotbar[4], InvStone, 20));
    // THROW: one (Q) or the stack (Ctrl+Q), only with an empty cursor.
    FFoundationClickResult R = Menu.Click(HotbarMenuSlot(4), 0, EFoundationClickType::Throw);
    TestTrue(TEXT("no throw while carrying"), R.Thrown.IsEmpty());
    I.Carried = {};
    R = Menu.Click(HotbarMenuSlot(4), 0, EFoundationClickType::Throw);
    TestTrue(TEXT("Q throws one"), HoldsStack(R.Thrown, InvStone, 1) && HoldsStack(I.Hotbar[4], InvStone, 19));
    R = Menu.Click(HotbarMenuSlot(4), 1, EFoundationClickType::Throw);
    TestTrue(TEXT("Ctrl+Q throws the stack"), HoldsStack(R.Thrown, InvStone, 19) && I.Hotbar[4].IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationInventoryQuickCraftTest, "Unnamed.Foundation.Inventory.QuickCraft", InvTestFlags)

bool FFoundationInventoryQuickCraftTest::RunTest(const FString&)
{
    const FFoundationContainerRules Rules = MakeInvRules();
    FFoundationInventory I = MakeInvInventory();
    FFoundationContainerMenu Menu(I, Rules, false);
    auto Drag = [&Menu](int32 Type, TArray<int32> Slots)
    {
        Menu.Click(FFoundationContainerMenu::Outside, FFoundationContainerMenu::QuickCraftButton(0, Type),
            EFoundationClickType::QuickCraft);
        for (const int32 Slot : Slots)
            Menu.Click(Slot, FFoundationContainerMenu::QuickCraftButton(1, Type), EFoundationClickType::QuickCraft);
        Menu.Click(FFoundationContainerMenu::Outside, FFoundationContainerMenu::QuickCraftButton(2, Type),
            EFoundationClickType::QuickCraft);
    };
    // Left: an even split, the remainder stays on the cursor; an existing stack adds its share.
    I.SetSlot(11, I.MakeStack(InvStone, 5));
    I.Carried = I.MakeStack(InvStone, 10);
    Drag(0, {MainMenuSlot(0), MainMenuSlot(1), MainMenuSlot(2)});
    TestTrue(TEXT("left drag: 3 each, 1 left"),
        HoldsStack(I.Main[0], InvStone, 3) && HoldsStack(I.Main[1], InvStone, 3) &&
            HoldsStack(I.Main[2], InvStone, 8) && HoldsStack(I.Carried, InvStone, 1));
    // Right: one each.
    I.Carried.Count = 10;
    Drag(1, {MainMenuSlot(3), MainMenuSlot(4)});
    TestTrue(TEXT("right drag: one each"),
        HoldsStack(I.Main[3], InvStone, 1) && HoldsStack(I.Main[4], InvStone, 1) && HoldsStack(I.Carried, InvStone, 8));
    // A slot of another item is skipped; no more slots than items.
    I.SetSlot(14, I.MakeStack(InvDirt, 1));
    I.Carried.Count = 2;
    Drag(1, {MainMenuSlot(5), MainMenuSlot(6), MainMenuSlot(7)});
    TestTrue(TEXT("no more slots than items"),
        HoldsStack(I.Main[5], InvDirt, 1) && HoldsStack(I.Main[6], InvStone, 1) && HoldsStack(I.Main[7], InvStone, 1) &&
            I.Carried.IsEmpty());
    // Middle: creative only, a full stack in each, and the cursor ends empty (value report, survival.r7.creative: a
    // cursor of 64 stone over 3 empty slots; survival changes nothing and keeps the 64).
    I.Carried = I.MakeStack(InvStone, 64);
    Drag(2, {MainMenuSlot(8), MainMenuSlot(9)});
    TestTrue(TEXT("no middle drag in survival"),
        I.Main[8].IsEmpty() && I.Main[9].IsEmpty() && HoldsStack(I.Carried, InvStone, 64));
    I.Carried = I.MakeStack(InvStone, 1);
    FFoundationContainerMenu Creative(I, Rules, true);
    Creative.Click(FFoundationContainerMenu::Outside, FFoundationContainerMenu::QuickCraftButton(0, 2),
        EFoundationClickType::QuickCraft);
    Creative.Click(MainMenuSlot(8), FFoundationContainerMenu::QuickCraftButton(1, 2), EFoundationClickType::QuickCraft);
    Creative.Click(MainMenuSlot(9), FFoundationContainerMenu::QuickCraftButton(1, 2), EFoundationClickType::QuickCraft);
    Creative.Click(FFoundationContainerMenu::Outside, FFoundationContainerMenu::QuickCraftButton(2, 2),
        EFoundationClickType::QuickCraft);
    TestTrue(TEXT("creative middle drag: full stacks, the cursor empty"),
        HoldsStack(I.Main[8], InvStone, 64) && HoldsStack(I.Main[9], InvStone, 64) && I.Carried.IsEmpty());
    I.Carried = I.MakeStack(InvStone, 1);
    // One slot is a plain click; another click type ends a drag and does nothing else.
    I.Carried.Count = 6;
    Drag(0, {MainMenuSlot(10)});
    TestTrue(TEXT("a one-slot left drag places all"), HoldsStack(I.Main[10], InvStone, 6) && I.Carried.IsEmpty());
    I.Carried = I.MakeStack(InvStone, 4);
    Menu.Click(FFoundationContainerMenu::Outside, FFoundationContainerMenu::QuickCraftButton(0, 0),
        EFoundationClickType::QuickCraft);
    Menu.Click(MainMenuSlot(11), FFoundationContainerMenu::QuickCraftButton(1, 0), EFoundationClickType::QuickCraft);
    Menu.Click(MainMenuSlot(12), 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("a click mid-drag cancels it and places nothing"),
        I.QuickCraft.Status == 0 && I.Main[11].IsEmpty() && I.Main[12].IsEmpty() && HoldsStack(I.Carried, InvStone, 4));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationInventoryPickupAllTest, "Unnamed.Foundation.Inventory.PickupAll", InvTestFlags)

bool FFoundationInventoryPickupAllTest::RunTest(const FString&)
{
    const FFoundationContainerRules Rules = MakeInvRules();
    FFoundationInventory I = MakeInvInventory();
    FFoundationContainerMenu Menu(I, Rules, false);
    // Non-full stacks first (main 0 is full and comes first in slot order), then full ones, up to a full stack.
    I.SetSlot(9, I.MakeStack(InvStone, 64));
    I.SetSlot(10, I.MakeStack(InvStone, 20));
    I.SetSlot(2, I.MakeStack(InvStone, 30));
    I.SetSlot(11, I.MakeStack(InvDirt, 30));
    I.Carried = I.MakeStack(InvStone, 4);
    Menu.Click(MainMenuSlot(5), 0, EFoundationClickType::PickupAll);
    TestTrue(TEXT("double-click takes the partial stacks first"), I.Main[1].IsEmpty() && I.Hotbar[2].IsEmpty());
    TestTrue(TEXT("then from the full stack, only up to 64"),
        HoldsStack(I.Carried, InvStone, 64) && HoldsStack(I.Main[0], InvStone, 54));
    TestTrue(TEXT("other items stay"), HoldsStack(I.Main[2], InvDirt, 30));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationInventoryAuthorityTest, "Unnamed.Foundation.Inventory.Authority", InvTestFlags)

bool FFoundationInventoryAuthorityTest::RunTest(const FString&)
{
    FInventoryLab L;
    const int32 StoneItem = L.A.Rules.FindItem(TEXT("stone"));
    if (!TestTrue(TEXT("stone exists"), StoneItem != INDEX_NONE)) return false;
    FFoundationInventory& I = L.Inv();
    I.Hotbar[0] = I.MakeStack(StoneItem, 10);
    // A click commits through the authority with the cursor kept between commands.
    TestTrue(TEXT("pickup commits"),
        L.Click(HotbarMenuSlot(0), 0, EFoundationClickType::Pickup).IsCommitted() &&
            HoldsStack(I.Carried, StoneItem, 10));
    TestTrue(TEXT("place into main"),
        L.Click(MainMenuSlot(4), 1, EFoundationClickType::Pickup).IsCommitted() && HoldsStack(I.Main[4], StoneItem, 1));
    TestEqual(TEXT("only the player's container exists"),
        L.Click(MainMenuSlot(4), 0, EFoundationClickType::Pickup, 1).Reason, EFoundationReason::InvalidSlot);
    // With foundation.Items.Entities 0 a throw is refused and nothing changes.
    L.A.Options.bItemEntities = false;
    const FFoundationResult Thrown = L.Click(FFoundationContainerMenu::Outside, 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("throw deferred, the cursor kept"),
        Thrown.Reason == EFoundationReason::DropDeferred && HoldsStack(I.Carried, StoneItem, 9));
    // At 1 (items fixes) a right click outside throws one as Q's throw does: delay 40, the cursor keeps the rest.
    L.A.Options.bItemEntities = true;
    TestTrue(TEXT("switch 1: a right click outside throws one"),
        L.Click(FFoundationContainerMenu::Outside, 1, EFoundationClickType::Pickup).IsCommitted() &&
            HoldsStack(I.Carried, StoneItem, 8) && L.A.GetItemEntities().Num() == 1 &&
            L.A.GetItemEntities()[0].Item == StoneItem && L.A.GetItemEntities()[0].Count == 1 &&
            L.A.GetItemEntities()[0].PickupDelay == 40 && L.A.ItemsThrown == 1);
    // Back to 0 for the cases below, which were written for it.
    I.Carried.Count = 9;
    L.A.Options.bItemEntities = false;
    // Closing puts the cursor stack back.
    TestTrue(TEXT("close returns the cursor, merging as a pickup does"),
        L.Close().IsCommitted() && I.Carried.IsEmpty() && HoldsStack(I.Main[4], StoneItem, 10) &&
            I.Hotbar[0].IsEmpty());
    // A worn tool on the cursor goes back whole: its damage and identity kept.
    const int32 Pickaxe = L.A.Rules.FindItem(TEXT("iron_pickaxe"));
    I.Carried = I.MakeStack(Pickaxe, 1);
    I.Carried.Damage = 100;
    const int32 PickaxeId = I.Carried.InstanceId;
    TestTrue(TEXT("close keeps a worn tool's damage and identity"),
        L.Close().IsCommitted() && I.Carried.IsEmpty() && I.Hotbar[0].Item == Pickaxe && I.Hotbar[0].Damage == 100 &&
            I.Hotbar[0].InstanceId == PickaxeId);
    // A cursor that does not fit is thrown; with foundation.Items.Entities 0 the close is refused, the cursor kept.
    for (int32 Slot = 0; Slot < 36; ++Slot)
        I.SetSlot(Slot, I.MakeStack(Pickaxe, 1));
    I.Carried = I.MakeStack(StoneItem, 3);
    TestEqual(TEXT("close with no room is deferred"), L.Close().Reason, EFoundationReason::DropDeferred);
    TestTrue(TEXT("switch 0: the cursor kept, nothing thrown"),
        HoldsStack(I.Carried, StoneItem, 3) && L.A.GetItemEntities().Num() == 1);
    L.A.Options.bItemEntities = true;
    // A dead player throws nothing from the screen either (Codex review 11, finding 2): a click outside and a close
    // with leftovers are refused, the cursor kept.
    L.A.FindPlayer(L.Player)->Facts.bAlive = false;
    TestEqual(TEXT("dead: a click outside is refused"),
        L.Click(FFoundationContainerMenu::Outside, 1, EFoundationClickType::Pickup).Reason, EFoundationReason::Dead);
    TestEqual(TEXT("dead: a close with leftovers is refused"), L.Close().Reason, EFoundationReason::Dead);
    TestTrue(TEXT("dead: the cursor kept, nothing thrown"),
        HoldsStack(I.Carried, StoneItem, 3) && L.A.GetItemEntities().Num() == 1 && L.A.ItemsThrown == 1);
    L.A.FindPlayer(L.Player)->Facts.bAlive = true;
    TestTrue(TEXT("switch 1: close with no room throws the cursor stack"),
        L.Close().IsCommitted() && I.Carried.IsEmpty() && L.A.GetItemEntities().Num() == 2 &&
            L.A.GetItemEntities()[1].Item == StoneItem && L.A.GetItemEntities()[1].Count == 3 &&
            L.A.GetItemEntities()[1].PickupDelay == 40 && L.A.ItemsThrown == 2);
    L.A.Options.bItemEntities = false;
    // The switch at 0: the main and armor slots are not there; the hotbar and offhand still click.
    L.A.Options.bFullInventory = false;
    I.Carried = {};
    TestEqual(TEXT("switch 0: main refused"), L.Click(MainMenuSlot(0), 0, EFoundationClickType::Pickup).Reason,
        EFoundationReason::InvalidSlot);
    TestEqual(TEXT("switch 0: armor refused"), L.Click(HeadMenuSlot, 0, EFoundationClickType::Pickup).Reason,
        EFoundationReason::InvalidSlot);
    TestTrue(
        TEXT("switch 0: the hotbar clicks"), L.Click(HotbarMenuSlot(1), 0, EFoundationClickType::Pickup).IsCommitted());
    IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Inventory.Full"));
    TestTrue(TEXT("foundation.Inventory.Full exists and ships at 1"), Variable && Variable->GetInt() == 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationInventoryArmorTest, "Unnamed.Foundation.Inventory.Armor", InvTestFlags)

bool FFoundationInventoryArmorTest::RunTest(const FString&)
{
    // Survival step 19a. DA_Items has no armor pieces until 19a2, so the test adds a chestplate and boots with
    // diamond_chestplate's and iron_boots' 26.3 data (armor 8 and 2, toughness 2 and 0, durability 528 and 195).
    FInventoryLab L;
    const auto AddPiece = [&L](const TCHAR* Id, EFoundationEquipmentSlot Slot, int32 Armor, float Toughness,
                              int32 Durability, const TCHAR* Sound)
    {
        FFoundationItemDefinition Piece;
        Piece.Id = Id;
        Piece.MaxStack = 1;
        Piece.Equipment = Slot;
        Piece.Armor = Armor;
        Piece.ArmorToughness = Toughness;
        Piece.MaxDurability = Durability;
        Piece.EquipSound = Sound;
        L.A.Rules.Items.Add(Piece);
        return L.A.Rules.Items.Num() - 1;
    };
    const int32 Chest =
        AddPiece(TEXT("lab_chestplate"), EFoundationEquipmentSlot::Chest, 8, 2, 528, TEXT("item.armor.equip_diamond"));
    const int32 Boots =
        AddPiece(TEXT("lab_boots"), EFoundationEquipmentSlot::Feet, 2, 0, 195, TEXT("item.armor.equip_iron"));
    L.A.Options.bItemUse = true;
    FFoundationInventory& I = L.Inv();
    const auto Worn = [&L]
    {
        int32 Armor = 0;
        float Toughness = 0;
        L.A.GetWornArmor(L.Player, Armor, Toughness);
        return TPair<int32, float>(Armor, Toughness);
    };
    TestTrue(TEXT("nothing worn: 0, 0"), Worn() == TPair<int32, float>(0, 0.f));

    // carved_pumpkin wears on the head (its 26.3 equippable, by id until DA_Blocks has equippables), not swappable,
    // with the default equip sound.
    const int32 Pumpkin = L.A.Rules.FindItem(TEXT("carved_pumpkin"));
    if (TestTrue(TEXT("carved_pumpkin exists"), Pumpkin != INDEX_NONE))
    {
        const FFoundationItemDefinition& Item = L.A.Rules.GetItem(Pumpkin);
        TestTrue(TEXT("carved_pumpkin: head, not swappable, equip_generic"),
            Item.Equipment == EFoundationEquipmentSlot::Head && !Item.bSwappable &&
                Item.EquipSound == TEXT("item.armor.equip_generic") && Item.Armor == 0);
        I.Hotbar[1] = I.MakeStack(Pumpkin, 1);
        TestTrue(TEXT("carved_pumpkin: shift-click puts it on the head"),
            L.Click(HotbarMenuSlot(1), 0, EFoundationClickType::QuickMove).IsCommitted() &&
                HoldsStack(I.Armor[3], Pumpkin, 1) && L.CountEquipped(Pumpkin) == 1);
        TestTrue(TEXT("carved_pumpkin: no armor points"), Worn() == TPair<int32, float>(0, 0.f));
        I.Armor[3] = {};
        I.Hotbar[0] = I.MakeStack(Pumpkin, 1);
        I.Select(0);
        TestEqual(TEXT("carved_pumpkin: a right click does not wear it"), L.Use().Outcome, EFoundationOutcome::Pass);
        TestTrue(TEXT("carved_pumpkin: still in hand"), I.Armor[3].IsEmpty() && HoldsStack(I.Hotbar[0], Pumpkin, 1));
    }

    // Review 19a1: a spectator, the item-use switch at 0 and a stack of two wear nothing.
    I.Hotbar[0] = I.MakeStack(Chest, 1);
    I.Hotbar[0].Count = 2;
    I.Select(0);
    TestEqual(TEXT("a stack of two: passes"), L.Use().Outcome, EFoundationOutcome::Pass);
    I.Hotbar[0] = I.MakeStack(Chest, 1);
    L.Vitals.Mode = EFoundationPlayerMode::Spectator;
    TestEqual(TEXT("spectator: passes"), L.Use().Outcome, EFoundationOutcome::Pass);
    L.Vitals.Mode = EFoundationPlayerMode::Survival;
    L.A.Options.bItemUse = false;
    TestEqual(TEXT("item use off: passes"), L.Use().Outcome, EFoundationOutcome::Pass);
    L.A.Options.bItemUse = true;
    TestTrue(TEXT("none of those wore it or made an event"), I.Armor[2].IsEmpty() && L.CountEquipped(Chest) == 0);
    // A right click wears the chestplate (the hand empties) with one Equipped event; the worn totals count it.
    I.Hotbar[0] = I.MakeStack(Chest, 1);
    I.Select(0);
    TestTrue(TEXT("use: worn"), L.Use().IsCommitted() && HoldsStack(I.Armor[2], Chest, 1) && I.Hotbar[0].IsEmpty());
    TestEqual(TEXT("use: one Equipped event"), L.CountEquipped(Chest), 1);
    TestTrue(TEXT("chestplate: armor 8, toughness 2"), Worn() == TPair<int32, float>(8, 2.f));
    // The boots by shift-click; the totals add.
    I.Hotbar[2] = I.MakeStack(Boots, 1);
    TestTrue(TEXT("boots: shift-click to the feet"),
        L.Click(HotbarMenuSlot(2), 0, EFoundationClickType::QuickMove).IsCommitted() &&
            HoldsStack(I.Armor[0], Boots, 1));
    TestTrue(TEXT("chestplate and boots: 10, 2"), Worn() == TPair<int32, float>(10, 2.f));
    // A second chestplate swaps with the worn one; the worn one comes back to the hand, damage kept.
    I.Armor[2].Damage = 40;
    const int32 OldId = I.Armor[2].InstanceId;
    I.Hotbar[0] = I.MakeStack(Chest, 1);
    TestTrue(TEXT("swap: committed, the old piece in hand with its damage"),
        L.Use().IsCommitted() && I.Hotbar[0].InstanceId == OldId && I.Hotbar[0].Damage == 40 && I.Armor[2].Damage == 0);
    TestEqual(TEXT("swap: a second Equipped event"), L.CountEquipped(Chest), 2);
    // A piece outside its own slot counts nothing.
    I.Armor[3] = I.MakeStack(Boots, 1);
    TestTrue(TEXT("boots on the head: still 10, 2"), Worn() == TPair<int32, float>(10, 2.f));
    I.Armor[3] = {};

    // Wear (lab tool wear on, survival): every worn piece takes the amount; a piece worn out breaks.
    L.A.Options.bToolWear = true;
    TestEqual(TEXT("wear: none broke"), L.A.WearArmor(L.Player, 3, EFoundationPlayerMode::Survival), 0);
    TestTrue(TEXT("wear: both pieces took 3"), I.Armor[2].Damage == 3 && I.Armor[0].Damage == 3);
    TestEqual(TEXT("wear: creative wears nothing"), L.A.WearArmor(L.Player, 3, EFoundationPlayerMode::Creative), 0);
    TestEqual(TEXT("wear: the boots break at 195"), L.A.WearArmor(L.Player, 192, EFoundationPlayerMode::Survival), 1);
    TestTrue(TEXT("wear: the boots gone, the chestplate at 195"), I.Armor[0].IsEmpty() && I.Armor[2].Damage == 195);
    L.A.Options.bToolWear = false;
    TestEqual(
        TEXT("wear: lab tool wear off wears nothing"), L.A.WearArmor(L.Player, 3, EFoundationPlayerMode::Survival), 0);
    TestEqual(TEXT("wear off: unchanged"), I.Armor[2].Damage, 195);

    // R19 (survival2.r19.equip): in creative an empty slot gets a copy and the hand keeps the piece; an occupied one
    // swaps as in survival.
    L.Vitals.Mode = EFoundationPlayerMode::Creative;
    I.Armor[2] = {};
    I.Hotbar[0] = I.MakeStack(Chest, 1);
    const int32 HeldId = I.Hotbar[0].InstanceId;
    TestTrue(TEXT("creative, empty slot: a copy worn, the hand keeps its piece"),
        L.Use().IsCommitted() && HoldsStack(I.Armor[2], Chest, 1) && I.Armor[2].InstanceId != HeldId &&
            HoldsStack(I.Hotbar[0], Chest, 1) && I.Hotbar[0].InstanceId == HeldId);
    const int32 CopyId = I.Armor[2].InstanceId;
    TestTrue(TEXT("creative, occupied: swapped"),
        L.Use().IsCommitted() && I.Armor[2].InstanceId == HeldId && I.Hotbar[0].InstanceId == CopyId);
    L.Vitals.Mode = EFoundationPlayerMode::Survival;
    // foundation.Inventory.Full 0 has no armor slots: nothing worn, and a right click does not wear.
    L.A.Options.bFullInventory = false;
    TestTrue(TEXT("switch 0: nothing worn"), Worn() == TPair<int32, float>(0, 0.f));
    I.Hotbar[0] = I.MakeStack(Chest, 1);
    TestEqual(TEXT("switch 0: a right click passes"), L.Use().Outcome, EFoundationOutcome::Pass);
    return true;
}
#endif
