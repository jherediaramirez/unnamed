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
    Menu.Click(MainMenuSlot(1), 0, EFoundationClickType::Pickup);
    TestTrue(
        TEXT("left on another item swaps"), HoldsStack(I.Main[1], InvDirt, 3) && HoldsStack(I.Carried, InvStone, 32));
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
    TestFalse(TEXT("menu slot 0 (the crafting result) waits for step 6"),
        Menu.Click(0, 0, EFoundationClickType::Pickup).bValid);
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
    // Middle: creative only, a full stack in each, the cursor kept.
    I.Carried = I.MakeStack(InvStone, 1);
    Drag(2, {MainMenuSlot(8), MainMenuSlot(9)});
    TestTrue(TEXT("no middle drag in survival"), I.Main[8].IsEmpty() && HoldsStack(I.Carried, InvStone, 1));
    FFoundationContainerMenu Creative(I, Rules, true);
    Creative.Click(FFoundationContainerMenu::Outside, FFoundationContainerMenu::QuickCraftButton(0, 2),
        EFoundationClickType::QuickCraft);
    Creative.Click(MainMenuSlot(8), FFoundationContainerMenu::QuickCraftButton(1, 2), EFoundationClickType::QuickCraft);
    Creative.Click(MainMenuSlot(9), FFoundationContainerMenu::QuickCraftButton(1, 2), EFoundationClickType::QuickCraft);
    Creative.Click(FFoundationContainerMenu::Outside, FFoundationContainerMenu::QuickCraftButton(2, 2),
        EFoundationClickType::QuickCraft);
    TestTrue(TEXT("creative middle drag: full stacks"),
        HoldsStack(I.Main[8], InvStone, 64) && HoldsStack(I.Main[9], InvStone, 64) &&
            HoldsStack(I.Carried, InvStone, 1));
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
    // A throw waits for survival step 4: refused, nothing changes.
    const FFoundationResult Thrown = L.Click(FFoundationContainerMenu::Outside, 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("throw deferred, the cursor kept"),
        Thrown.Reason == EFoundationReason::DropDeferred && HoldsStack(I.Carried, StoneItem, 9));
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
    // A cursor that does not fit would be thrown: refused, the cursor kept.
    for (int32 Slot = 0; Slot < 36; ++Slot)
        I.SetSlot(Slot, I.MakeStack(Pickaxe, 1));
    I.Carried = I.MakeStack(StoneItem, 3);
    TestEqual(TEXT("close with no room is deferred"), L.Close().Reason, EFoundationReason::DropDeferred);
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
#endif
