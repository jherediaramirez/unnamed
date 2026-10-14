#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationContainer.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Hud/HudContainer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags ContainerScreenTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    using FScreenMenu = FFoundationContainerMenu;
    using FScreenClicks = TArray<FHudContainerClick>;

    // The middle of a menu slot, where a player clicks it.
    FIntPoint ScreenSlotPoint(int32 Slot)
    {
        return FHudContainerModel::GetSlotPosition(Slot) + FIntPoint(8, 8);
    }
    int32 ScreenMain(int32 Index)
    {
        return FScreenMenu::FirstMain + Index;
    }

    FHudContainerModel MakeScreenModel()
    {
        FHudContainerModel Model;
        Model.Items.Add(TEXT("stone")).MaxStack = 64;
        Model.Items.Add(TEXT("dirt")).MaxStack = 64;
        FHudCreativeItem& Helmet = Model.Items.Add(TEXT("iron_helmet"));
        Helmet.MaxStack = 1;
        Helmet.Equipment = EFoundationEquipmentSlot::Head;
        return Model;
    }

    FHudContainerView MakeScreenView(FHudCreativeStack Carried = {})
    {
        FHudContainerView View;
        View.Slots.SetNum(FScreenMenu::PlayerMenuSlots);
        View.Carried = Carried;
        return View;
    }

    FScreenClicks ScreenDragClicks(int32 Type, TArray<int32> Slots)
    {
        FScreenClicks Clicks = {
            {FScreenMenu::Outside, FScreenMenu::QuickCraftButton(0, Type), EFoundationClickType::QuickCraft}};
        for (const int32 Slot : Slots)
            Clicks.Add({Slot, FScreenMenu::QuickCraftButton(1, Type), EFoundationClickType::QuickCraft});
        Clicks.Add({FScreenMenu::Outside, FScreenMenu::QuickCraftButton(2, Type), EFoundationClickType::QuickCraft});
        return Clicks;
    }

    // The authority with the project's items and one player; the screen's clicks go through Execute as the client
    // sends them.
    struct FScreenLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        int32 Player = 0;
        uint32 Sequence = 1;
        FHudContainerModel Model;

        FScreenLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bFullInventory = true;
            A.Options.bInfiniteSupply = false;
            Player = A.AddPlayer();
            A.Reset();
            // Facts as the eating tests set them: eye at 162 cm, looking straight up at nothing.
            FFoundationPlayerFacts Facts;
            Facts.EyeCm = FVector(0, 0, 162);
            Facts.Aim = FVector::UpVector;
            Facts.Step = 1;
            A.SetFacts(Player, Facts);
            A.Step();
            Vitals.bNaturalRegeneration = false;
            for (const FFoundationItemDefinition& Item : A.Rules.Items)
            {
                FHudCreativeItem& Entry = Model.Items.Add(Item.Id);
                Entry.MaxStack = Item.MaxStack;
                Entry.Equipment = Item.Equipment;
            }
        }
        FFoundationInventory& Inv() { return A.FindPlayer(Player)->Inventory; }
        FFoundationCommand MakeCommand(EFoundationCommandType Type)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = Sequence++;
            Command.Type = Type;
            return Command;
        }
        FFoundationResult Send(const FHudContainerClick& Click)
        {
            FFoundationCommand Command = MakeCommand(EFoundationCommandType::ContainerClick);
            Command.ContainerId = FScreenMenu::PlayerContainer;
            Command.Slot = Click.Slot;
            Command.Button = Click.Button;
            Command.ClickType = Click.Type;
            return A.Execute(Player, Command, Vitals);
        }
        // The view the screen reads: what UFoundationPanels' GetView builds.
        FHudContainerView View()
        {
            FHudContainerView Result = MakeScreenView();
            auto Stack = [this](const FFoundationItemInstance* Item)
            {
                return !Item || Item->IsEmpty()
                    ? FHudCreativeStack()
                    : FHudCreativeStack{A.Rules.GetItem(Item->Item).Id, Item->Count, Item->Damage};
            };
            for (int32 Slot = 0; Slot < FScreenMenu::PlayerMenuSlots; ++Slot)
                if (const int32 Index = FScreenMenu::ToInventorySlot(Slot); Index != INDEX_NONE)
                    Result.Slots[Slot] = Stack(Inv().GetSlot(Index));
            Result.Carried = Stack(&Inv().Carried);
            return Result;
        }
        int32 SendAll(const FScreenClicks& Clicks)
        {
            int32 Committed = 0;
            for (const FHudContainerClick& Click : Clicks)
                Committed += Send(Click).IsCommitted() ? 1 : 0;
            return Committed;
        }
        void Down(int32 Slot, int32 Button, double Time, bool bShift = false)
        {
            SendAll(Model.MouseDown(ScreenSlotPoint(Slot), Button, bShift, View(), Time));
        }
        void Over(int32 Slot) { SendAll(Model.MouseMove(ScreenSlotPoint(Slot), View())); }
        void Up(int32 Slot, int32 Button) { SendAll(Model.MouseUp(ScreenSlotPoint(Slot), Button, View())); }
    };

    // Equal values, for the types TestEqual has no overload for (click lists, points).
    template <typename T>
    bool ExpectSame(FAutomationTestBase& Test, const TCHAR* What, const T& Actual, const T& Expected)
    {
        return Test.TestTrue(What, Actual == Expected);
    }

    bool ScreenHolds(const FFoundationItemInstance& Stack, int32 Item, int32 Count)
    {
        return Count == 0 ? Stack.IsEmpty() : !Stack.IsEmpty() && Stack.Item == Item && Stack.Count == Count;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudContainerLayoutTest, "Unnamed.Hud.Container.Layout", ContainerScreenTestFlags)

bool FHudContainerLayoutTest::RunTest(const FString&)
{
    // inventory.png's slot frames (the 16 x 16 item areas), measured from the texture.
    ExpectSame(*this, TEXT("result"), FHudContainerModel::GetSlotPosition(0), FIntPoint(154, 28));
    ExpectSame(*this, TEXT("crafting 4"), FHudContainerModel::GetSlotPosition(4), FIntPoint(116, 36));
    ExpectSame(*this, TEXT("head"), FHudContainerModel::GetSlotPosition(5), FIntPoint(8, 8));
    ExpectSame(*this, TEXT("feet"), FHudContainerModel::GetSlotPosition(8), FIntPoint(8, 62));
    ExpectSame(*this, TEXT("main 0"), FHudContainerModel::GetSlotPosition(9), FIntPoint(8, 84));
    ExpectSame(*this, TEXT("main 26"), FHudContainerModel::GetSlotPosition(35), FIntPoint(152, 120));
    ExpectSame(*this, TEXT("hotbar 0"), FHudContainerModel::GetSlotPosition(36), FIntPoint(8, 142));
    ExpectSame(*this, TEXT("hotbar 8"), FHudContainerModel::GetSlotPosition(44), FIntPoint(152, 142));
    ExpectSame(*this, TEXT("offhand"), FHudContainerModel::GetSlotPosition(45), FIntPoint(77, 62));
    FHudContainerModel Model = MakeScreenModel();
    bool bRoundTrip = true;
    for (int32 Slot = 0; Slot < FScreenMenu::PlayerMenuSlots; ++Slot)
    {
        const FIntPoint At = FHudContainerModel::GetSlotPosition(Slot);
        // The hover area is the item area and the pixel around it.
        bRoundTrip &= Model.HitTest(At) == Slot && Model.HitTest(At + FIntPoint(16, 16)) == Slot &&
            Model.HitTest(At - FIntPoint(1, 1)) == Slot;
    }
    TestTrue(TEXT("every slot hit-tests to itself"), bRoundTrip);
    ExpectSame(*this, TEXT("outside the window"), Model.HitTest({-1, 50}), FScreenMenu::Outside);
    ExpectSame(*this, TEXT("past the right edge"), Model.HitTest({176, 50}), FScreenMenu::Outside);
    ExpectSame(*this, TEXT("the player-model frame is no slot"), Model.HitTest({50, 40}), int32(INDEX_NONE));
    TestTrue(TEXT("the recipe-book button"),
        FHudContainerModel::IsInRecipeButton({104, 61}) && FHudContainerModel::IsInRecipeButton({123, 78}) &&
            !FHudContainerModel::IsInRecipeButton({124, 61}));
    Model.bFullInventory = false;
    TestTrue(TEXT("switch 0: the main and armor slots are not slots; the hotbar and offhand are"),
        Model.HitTest(ScreenSlotPoint(ScreenMain(0))) == INDEX_NONE &&
            Model.HitTest(ScreenSlotPoint(5)) == INDEX_NONE && Model.HitTest(ScreenSlotPoint(36)) == 36 &&
            Model.HitTest(ScreenSlotPoint(45)) == 45);
    IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Inventory.Screen"));
    TestTrue(TEXT("foundation.Inventory.Screen exists and ships at 1"), Variable && Variable->GetInt() == 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudContainerClicksTest, "Unnamed.Hud.Container.Clicks", ContainerScreenTestFlags)

bool FHudContainerClicksTest::RunTest(const FString&)
{
    FHudContainerModel Model = MakeScreenModel();
    const FHudContainerView Empty = MakeScreenView();
    const FHudContainerView Holding = MakeScreenView({TEXT("stone"), 5});
    const FIntPoint Main0 = ScreenSlotPoint(ScreenMain(0));
    // An empty cursor clicks at the press: left and right PICKUP, shift QUICK_MOVE.
    ExpectSame(*this, TEXT("left"), Model.MouseDown(Main0, 0, false, Empty, 0),
        FScreenClicks{{9, 0, EFoundationClickType::Pickup}});
    ExpectSame(*this, TEXT("its release sends nothing"), Model.MouseUp(Main0, 0, Empty), FScreenClicks());
    ExpectSame(*this, TEXT("right"), Model.MouseDown(Main0, 1, false, Empty, 10),
        FScreenClicks{{9, 1, EFoundationClickType::Pickup}});
    Model.MouseUp(Main0, 1, Empty);
    ExpectSame(*this, TEXT("shift"), Model.MouseDown(ScreenSlotPoint(40), 0, true, Empty, 20),
        FScreenClicks{{40, 0, EFoundationClickType::QuickMove}});
    Model.MouseUp(ScreenSlotPoint(40), 0, Empty);
    // Middle clicks clone only in creative; the crafting slots wait for step 6; the frame and the button do nothing.
    ExpectSame(*this, TEXT("middle"), Model.MouseDown(Main0, 2, false, Empty, 30), FScreenClicks());
    ExpectSame(*this, TEXT("crafting slot"), Model.MouseDown(ScreenSlotPoint(1), 0, false, Empty, 40), FScreenClicks());
    ExpectSame(*this, TEXT("result slot"), Model.MouseDown(ScreenSlotPoint(0), 0, false, Empty, 50), FScreenClicks());
    ExpectSame(*this, TEXT("recipe-book button"), Model.MouseDown({110, 65}, 0, false, Empty, 60), FScreenClicks());
    // Outside the window: the cursor stack would be thrown (left all, right one); nothing without one.
    ExpectSame(*this, TEXT("outside, empty cursor"), Model.MouseDown({-5, 10}, 0, false, Empty, 70), FScreenClicks());
    ExpectSame(*this, TEXT("outside with a stack"), Model.MouseDown({-5, 10}, 1, false, Holding, 80),
        FScreenClicks{{FScreenMenu::Outside, 1, EFoundationClickType::Pickup}});
    Model.MouseUp({-5, 10}, 1, Holding);
    // Number keys and F swap the hovered slot with a hotbar slot or the offhand, with an empty cursor only.
    ExpectSame(
        *this, TEXT("key 3"), Model.PressSwap(Main0, 2, Empty), FScreenClicks{{9, 2, EFoundationClickType::Swap}});
    ExpectSame(*this, TEXT("F"), Model.PressSwap(Main0, 40, Empty), FScreenClicks{{9, 40, EFoundationClickType::Swap}});
    ExpectSame(*this, TEXT("no swap with a stack on the cursor"), Model.PressSwap(Main0, 2, Holding), FScreenClicks());
    ExpectSame(*this, TEXT("no swap over nothing"), Model.PressSwap({50, 40}, 2, Empty), FScreenClicks());
    // Q throws one, control-Q the stack.
    ExpectSame(
        *this, TEXT("Q"), Model.PressDrop(Main0, false, Empty), FScreenClicks{{9, 0, EFoundationClickType::Throw}});
    ExpectSame(*this, TEXT("control-Q"), Model.PressDrop(Main0, true, Empty),
        FScreenClicks{{9, 1, EFoundationClickType::Throw}});
    ExpectSame(*this, TEXT("no Q with a stack on the cursor"), Model.PressDrop(Main0, false, Holding), FScreenClicks());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudContainerDragTest, "Unnamed.Hud.Container.Drag", ContainerScreenTestFlags)

bool FHudContainerDragTest::RunTest(const FString&)
{
    FHudContainerModel Model = MakeScreenModel();
    FHudContainerView View = MakeScreenView({TEXT("stone"), 10});
    View.Slots[ScreenMain(2)] = {TEXT("stone"), 5};
    View.Slots[ScreenMain(3)] = {TEXT("dirt"), 1};
    View.Slots[ScreenMain(4)] = {TEXT("stone"), 64};
    // A press with a stack starts a drag; slots join as the mouse crosses them, once each, when they can take it.
    ExpectSame(*this, TEXT("the press sends nothing"),
        Model.MouseDown(ScreenSlotPoint(ScreenMain(0)), 0, false, View, 0), FScreenClicks());
    for (const int32 Slot : {ScreenMain(1), ScreenMain(1), ScreenMain(3), ScreenMain(4), 5, 1, ScreenMain(2)})
        Model.MouseMove(ScreenSlotPoint(Slot), View);
    Model.MouseMove({-10, -10}, View);
    ExpectSame(*this, TEXT("dragged slots: not another item, a full stack, armor for stone, crafting or outside"),
        Model.GetDragSlots(), TArray<int32>{ScreenMain(0), ScreenMain(1), ScreenMain(2)});
    // The preview: 10 over three slots is 3 each, one left on the cursor.
    TMap<int32, FHudCreativeStack> Preview;
    FHudCreativeStack Cursor;
    Model.GetDragPreview(View, Preview, Cursor);
    TestTrue(TEXT("left preview"),
        Preview.FindRef(ScreenMain(0)).Count == 3 && Preview.FindRef(ScreenMain(1)).Count == 3 &&
            Preview.FindRef(ScreenMain(2)).Count == 8 && Cursor.Count == 1);
    ExpectSame(*this, TEXT("the release sends the drag"), Model.MouseUp({-10, -10}, 0, View),
        ScreenDragClicks(0, {ScreenMain(0), ScreenMain(1), ScreenMain(2)}));
    TestFalse(TEXT("the drag ended"), Model.IsDragging());
    // Right: no more slots than items.
    View.Carried.Count = 2;
    Model.MouseDown(ScreenSlotPoint(ScreenMain(5)), 1, false, View, 10);
    Model.MouseMove(ScreenSlotPoint(ScreenMain(6)), View);
    Model.MouseMove(ScreenSlotPoint(ScreenMain(7)), View);
    Model.GetDragPreview(View, Preview, Cursor);
    TestTrue(TEXT("right preview: one each, the cursor empty"),
        Preview.FindRef(ScreenMain(5)).Count == 1 && Preview.FindRef(ScreenMain(6)).Count == 1 && Cursor.IsEmpty());
    ExpectSame(*this, TEXT("right drag over two of three"), Model.MouseUp(ScreenSlotPoint(ScreenMain(7)), 1, View),
        ScreenDragClicks(1, {ScreenMain(5), ScreenMain(6)}));
    // One slot is a plain click on the pressed slot, also when that slot cannot take the stack (a swap).
    Model.MouseDown(ScreenSlotPoint(ScreenMain(8)), 0, false, View, 20);
    ExpectSame(*this, TEXT("one-slot drag"), Model.MouseUp(ScreenSlotPoint(ScreenMain(8)), 0, View),
        FScreenClicks{{ScreenMain(8), 0, EFoundationClickType::Pickup}});
    Model.MouseDown(ScreenSlotPoint(ScreenMain(3)), 0, false, View, 30);
    ExpectSame(*this, TEXT("press on another item"), Model.MouseUp(ScreenSlotPoint(ScreenMain(3)), 0, View),
        FScreenClicks{{ScreenMain(3), 0, EFoundationClickType::Pickup}});
    // The other button's release does not end the drag; a press during it does nothing.
    Model.MouseDown(ScreenSlotPoint(ScreenMain(9)), 0, false, View, 40);
    ExpectSame(
        *this, TEXT("right release mid-drag"), Model.MouseUp(ScreenSlotPoint(ScreenMain(9)), 1, View), FScreenClicks());
    ExpectSame(*this, TEXT("right press mid-drag"), Model.MouseDown(ScreenSlotPoint(ScreenMain(9)), 1, false, View, 41),
        FScreenClicks());
    TestTrue(TEXT("still dragging"), Model.IsDragging());
    Model.Reset();
    TestFalse(TEXT("Reset ends it"), Model.IsDragging());
    // The head slot takes a helmet.
    FHudContainerView Helmet = MakeScreenView({TEXT("iron_helmet"), 2});
    Model.MouseDown(ScreenSlotPoint(ScreenMain(0)), 1, false, Helmet, 50);
    Model.MouseMove(ScreenSlotPoint(5), Helmet);
    Model.MouseMove(ScreenSlotPoint(6), Helmet);
    ExpectSame(
        *this, TEXT("a helmet drags into the head slot only"), Model.GetDragSlots(), TArray<int32>{ScreenMain(0), 5});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHudContainerDoubleClickTest, "Unnamed.Hud.Container.DoubleClick", ContainerScreenTestFlags)

bool FHudContainerDoubleClickTest::RunTest(const FString&)
{
    FHudContainerModel Model = MakeScreenModel();
    const FIntPoint Main0 = ScreenSlotPoint(ScreenMain(0));
    FHudContainerView Picked = MakeScreenView({TEXT("stone"), 10});
    // The first press picks up; a second left press on the slot within 250 ms, with the stack on the cursor,
    // collects on its release.
    Model.MouseDown(Main0, 0, false, MakeScreenView(), 1.0);
    Model.MouseUp(Main0, 0, MakeScreenView());
    ExpectSame(*this, TEXT("second press"), Model.MouseDown(Main0, 0, false, Picked, 1.2), FScreenClicks());
    TestFalse(TEXT("a double-click is no drag"), Model.IsDragging());
    ExpectSame(*this, TEXT("release: PICKUP_ALL"), Model.MouseUp(Main0, 0, Picked),
        FScreenClicks{{ScreenMain(0), 0, EFoundationClickType::PickupAll}});
    // A third press is a new first click (a drag here, with the stack on the cursor).
    Model.MouseDown(Main0, 0, false, Picked, 1.3);
    TestTrue(TEXT("third press drags"), Model.IsDragging());
    Model.Reset();
    // Too slow, another slot, the right button or an empty cursor: no double-click.
    Model.MouseDown(Main0, 0, false, MakeScreenView(), 2.0);
    Model.MouseUp(Main0, 0, MakeScreenView());
    Model.MouseDown(Main0, 0, false, Picked, 2.3);
    ExpectSame(*this, TEXT("300 ms is two clicks"), Model.MouseUp(Main0, 0, Picked),
        FScreenClicks{{ScreenMain(0), 0, EFoundationClickType::Pickup}});
    Model.MouseDown(ScreenSlotPoint(ScreenMain(1)), 0, false, Picked, 2.4);
    TestTrue(TEXT("another slot drags"), Model.IsDragging());
    Model.Reset();
    Model.MouseDown(Main0, 1, false, MakeScreenView(), 3.0);
    Model.MouseUp(Main0, 1, MakeScreenView());
    Model.MouseDown(Main0, 1, false, Picked, 3.1);
    TestTrue(TEXT("right twice drags"), Model.IsDragging());
    Model.Reset();
    Model.MouseDown(Main0, 0, false, MakeScreenView(), 4.0);
    Model.MouseUp(Main0, 0, MakeScreenView());
    ExpectSame(*this, TEXT("an empty cursor clicks again"), Model.MouseDown(Main0, 0, false, MakeScreenView(), 4.1),
        FScreenClicks{{ScreenMain(0), 0, EFoundationClickType::Pickup}});
    Model.MouseUp(Main0, 0, MakeScreenView());
    // Shift double-click: every stack like the one just shift-moved moves too.
    Model.Reset();
    FHudContainerView Stones = MakeScreenView();
    Stones.Slots[ScreenMain(0)] = {TEXT("stone"), 3};
    Stones.Slots[ScreenMain(5)] = {TEXT("stone"), 7};
    Stones.Slots[40] = {TEXT("stone"), 1};
    Stones.Slots[ScreenMain(6)] = {TEXT("dirt"), 7};
    // Another damage is another stack: it stays.
    Stones.Slots[ScreenMain(7)] = {TEXT("stone"), 2, 5};
    ExpectSame(*this, TEXT("shift-click"), Model.MouseDown(Main0, 0, true, Stones, 5.0),
        FScreenClicks{{ScreenMain(0), 0, EFoundationClickType::QuickMove}});
    Model.MouseUp(Main0, 0, Stones);
    Stones.Slots[ScreenMain(0)] = {};
    ExpectSame(*this, TEXT("shift second press"), Model.MouseDown(Main0, 0, true, Stones, 5.1), FScreenClicks());
    ExpectSame(*this, TEXT("shift release: every stone"), Model.MouseUp(Main0, 0, Stones),
        FScreenClicks{{ScreenMain(5), 0, EFoundationClickType::QuickMove}, {40, 0, EFoundationClickType::QuickMove}});
    // A plain press in between forgets the shift-click.
    Model.Reset();
    Stones.Slots[ScreenMain(0)] = {TEXT("stone"), 3};
    Model.MouseDown(Main0, 0, true, Stones, 6.0);
    Model.MouseUp(Main0, 0, Stones);
    Model.MouseDown(ScreenSlotPoint(ScreenMain(8)), 0, false, MakeScreenView(), 6.05);
    Model.MouseUp(ScreenSlotPoint(ScreenMain(8)), 0, MakeScreenView());
    Model.MouseDown(ScreenSlotPoint(ScreenMain(8)), 0, true, Stones, 6.1);
    ExpectSame(*this, TEXT("a plain press forgets the shift-click"),
        Model.MouseUp(ScreenSlotPoint(ScreenMain(8)), 0, Stones), FScreenClicks());
    // Reset (a lost mouse capture) ends a double-click waiting for its release: the next press clicks.
    Model.Reset();
    Model.MouseDown(Main0, 0, false, MakeScreenView(), 7.0);
    Model.MouseUp(Main0, 0, MakeScreenView());
    Model.MouseDown(Main0, 0, false, Picked, 7.1);
    Model.Reset();
    ExpectSame(*this, TEXT("Reset ends a pending double-click"),
        Model.MouseDown(Main0, 0, false, MakeScreenView(), 9.0),
        FScreenClicks{{ScreenMain(0), 0, EFoundationClickType::Pickup}});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHudContainerAuthorityTest, "Unnamed.Hud.Container.Authority", ContainerScreenTestFlags)

bool FHudContainerAuthorityTest::RunTest(const FString&)
{
    FScreenLab L;
    const int32 Stone = L.A.Rules.FindItem(TEXT("stone"));
    const int32 Apple = L.A.Rules.FindItem(TEXT("apple"));
    if (!TestTrue(TEXT("stone and apple exist"), Stone != INDEX_NONE && Apple != INDEX_NONE)) return false;
    FFoundationInventory& I = L.Inv();
    I.Hotbar[0] = I.MakeStack(Stone, 10);
    // Pick up, then drag over three main slots: 3 each, one left on the cursor.
    L.Down(36, 0, 0);
    L.Up(36, 0);
    TestTrue(TEXT("picked up"), ScreenHolds(I.Carried, Stone, 10) && I.Hotbar[0].IsEmpty());
    L.Down(ScreenMain(0), 0, 1);
    L.Over(ScreenMain(1));
    L.Over(ScreenMain(2));
    L.Up(ScreenMain(2), 0);
    TestTrue(TEXT("a drag spreads"),
        ScreenHolds(I.Main[0], Stone, 3) && ScreenHolds(I.Main[1], Stone, 3) && ScreenHolds(I.Main[2], Stone, 3) &&
            ScreenHolds(I.Carried, Stone, 1));
    // Put the last one in an empty slot, pick that slot up again and double-click it: the cursor collects them.
    L.Down(ScreenMain(5), 0, 2);
    L.Up(ScreenMain(5), 0);
    L.Down(ScreenMain(5), 0, 3);
    L.Up(ScreenMain(5), 0);
    L.Down(ScreenMain(5), 0, 3.1);
    L.Up(ScreenMain(5), 0);
    TestTrue(TEXT("a double-click collects"),
        ScreenHolds(I.Carried, Stone, 10) && I.Main[0].IsEmpty() && I.Main[1].IsEmpty() && I.Main[2].IsEmpty());
    // Closing returns the cursor.
    TestTrue(TEXT("close returns the cursor"),
        L.A.Execute(L.Player, L.MakeCommand(EFoundationCommandType::ContainerClose), L.Vitals).IsCommitted() &&
            I.Carried.IsEmpty() && ScreenHolds(I.Hotbar[0], Stone, 10));

    // Codex 5a finding 1: a drag whose slot disappears (the full inventory switched off mid-drag) ends; no crash,
    // nothing moves.
    I.Carried = I.MakeStack(Stone, 4);
    L.Send({FScreenMenu::Outside, FScreenMenu::QuickCraftButton(0, 0), EFoundationClickType::QuickCraft});
    L.Send({ScreenMain(0), FScreenMenu::QuickCraftButton(1, 0), EFoundationClickType::QuickCraft});
    L.A.Options.bFullInventory = false;
    // Any command applies the switch: the drag ends there, before its end arrives.
    FFoundationCommand Select = L.MakeCommand(EFoundationCommandType::SelectSlot);
    Select.Slot = 0;
    L.A.Execute(L.Player, Select, L.Vitals);
    TestTrue(TEXT("the switch ends the drag"), I.QuickCraft.Status == 0 && I.QuickCraft.Slots.IsEmpty());
    L.Send({FScreenMenu::Outside, FScreenMenu::QuickCraftButton(2, 0), EFoundationClickType::QuickCraft});
    TestTrue(TEXT("switch off mid-drag: the drag ends, nothing moved"),
        I.QuickCraft.Status == 0 && ScreenHolds(I.Carried, Stone, 4) && I.Main[0].IsEmpty());
    // The menu alone (no authority to end the drag) refuses the vanished slot quietly.
    FFoundationContainerRules Rules;
    Rules.GetMaxStack = [](int32) { return 64; };
    FFoundationInventory Lab;
    Lab.bFull = true;
    Lab.Carried = Lab.MakeStack(Stone, 4);
    FFoundationContainerMenu Menu(Lab, Rules, false);
    Menu.Click(FScreenMenu::Outside, FScreenMenu::QuickCraftButton(0, 0), EFoundationClickType::QuickCraft);
    Menu.Click(ScreenMain(0), FScreenMenu::QuickCraftButton(1, 0), EFoundationClickType::QuickCraft);
    Lab.bFull = false;
    Menu.Click(FScreenMenu::Outside, FScreenMenu::QuickCraftButton(2, 0), EFoundationClickType::QuickCraft);
    TestTrue(TEXT("menu: a one-slot end on a missing slot does nothing"),
        ScreenHolds(Lab.Carried, Stone, 4) && Lab.QuickCraft.Status == 0);
    L.A.Options.bFullInventory = true;
    L.A.Execute(L.Player, L.MakeCommand(EFoundationCommandType::ContainerClose), L.Vitals);

    // Codex 5a finding 3: a container click or close ends an item use in progress, as a slot change does.
    I.Hotbar[1] = I.MakeStack(Apple, 5);
    L.Vitals.Food = 10;
    L.A.Options.bItemUse = true;
    auto StartEating = [&L, &I]
    {
        FFoundationCommand Select = L.MakeCommand(EFoundationCommandType::SelectSlot);
        Select.Slot = 1;
        L.A.Execute(L.Player, Select, L.Vitals);
        FFoundationCommand Start = L.MakeCommand(EFoundationCommandType::UseItemStart);
        Start.Hand = EFoundationHand::Main;
        Start.Item = I.GetHand(EFoundationHand::Main).GetRef();
        return L.A.Execute(L.Player, Start, L.Vitals).IsCommitted() && L.A.FindPlayer(L.Player)->Use.bActive;
    };
    if (TestTrue(TEXT("eating starts"), StartEating()))
    {
        L.Send({ScreenMain(10), 0, EFoundationClickType::Pickup});
        TestFalse(TEXT("a container click ends the use"), L.A.FindPlayer(L.Player)->Use.bActive);
    }
    if (TestTrue(TEXT("eating starts again"), StartEating()))
    {
        L.A.Execute(L.Player, L.MakeCommand(EFoundationCommandType::ContainerClose), L.Vitals);
        TestFalse(TEXT("a container close ends the use"), L.A.FindPlayer(L.Player)->Use.bActive);
    }
    return true;
}
#endif
