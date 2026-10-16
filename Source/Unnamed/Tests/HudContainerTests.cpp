#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationContainer.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Hud/HudContainer.h"
#include "Hud/HudContainerScreen.h"

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
            const TArray<FFoundationItemInstance> Menu = A.GetPlayerMenu(*A.FindPlayer(Player));
            for (int32 Slot = 0; Slot < FScreenMenu::PlayerMenuSlots; ++Slot)
                Result.Slots[Slot] = Stack(&Menu[Slot]);
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
    // Middle clicks clone only in creative; the crafting grid and its result click like any slot (survival step 6);
    // the frame and the button do nothing.
    ExpectSame(*this, TEXT("middle"), Model.MouseDown(Main0, 2, false, Empty, 30), FScreenClicks());
    ExpectSame(*this, TEXT("crafting slot"), Model.MouseDown(ScreenSlotPoint(1), 0, false, Empty, 40),
        FScreenClicks{{1, 0, EFoundationClickType::Pickup}});
    Model.MouseUp(ScreenSlotPoint(1), 0, Empty);
    ExpectSame(*this, TEXT("result slot"), Model.MouseDown(ScreenSlotPoint(0), 0, false, Empty, 50),
        FScreenClicks{{0, 0, EFoundationClickType::Pickup}});
    Model.MouseUp(ScreenSlotPoint(0), 0, Empty);
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
    {
        // The crafting grid takes a drag; its result never does.
        FHudContainerModel Grid = MakeScreenModel();
        Grid.MouseDown(ScreenSlotPoint(1), 0, false, View, 0);
        for (const int32 Slot : {0, 2, 4})
            Grid.MouseMove(ScreenSlotPoint(Slot), View);
        ExpectSame(*this, TEXT("a drag over the grid skips the result"), Grid.GetDragSlots(), TArray<int32>{1, 2, 4});
    }
    for (const int32 Slot : {ScreenMain(1), ScreenMain(1), ScreenMain(3), ScreenMain(4), 5, 0, ScreenMain(2)})
        Model.MouseMove(ScreenSlotPoint(Slot), View);
    Model.MouseMove({-10, -10}, View);
    ExpectSame(*this, TEXT("dragged slots: not another item, a full stack, armor for stone, the result or outside"),
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
    // The other button's release does not end the drag; its press cancels it (QC RA1-3, minecraft.wiki "Controls"), so
    // the drag button's release then sends nothing.
    Model.MouseDown(ScreenSlotPoint(ScreenMain(9)), 0, false, View, 40);
    ExpectSame(
        *this, TEXT("right release mid-drag"), Model.MouseUp(ScreenSlotPoint(ScreenMain(9)), 1, View), FScreenClicks());
    TestTrue(TEXT("still dragging after the other release"), Model.IsDragging());
    ExpectSame(*this, TEXT("right press mid-drag"), Model.MouseDown(ScreenSlotPoint(ScreenMain(9)), 1, false, View, 41),
        FScreenClicks());
    TestFalse(TEXT("the other press cancels the drag"), Model.IsDragging());
    ExpectSame(*this, TEXT("the release after a cancel sends nothing"),
        Model.MouseUp(ScreenSlotPoint(ScreenMain(9)), 0, View), FScreenClicks());
    Model.MouseDown(ScreenSlotPoint(ScreenMain(9)), 0, false, View, 50);
    TestTrue(TEXT("dragging again"), Model.IsDragging());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudContainerInputTest, "Unnamed.Hud.Container.Input", ContainerScreenTestFlags)

bool FHudContainerInputTest::RunTest(const FString&)
{
    // The screen's own input path (Codex 5b findings 1-3): Slate's mouse and key events on the real widget, at GUI
    // scale 2 on a 1280 x 720 frame (the menu at 232,97), its clicks sent to the authority.
    FScreenLab L;
    FScreenClicks Sent;
    bool bClosed = false;
    TMap<FName, FKey> Bindings = {{TEXT("key.inventory"), EKeys::I}, {TEXT("key.hotbar.1"), EKeys::Z},
        {TEXT("key.swapOffhand"), EKeys::G}, {TEXT("key.drop"), EKeys::X}};
    // clang-format off
    const TSharedRef<SHudContainerScreen> Screen = SNew(SHudContainerScreen)
        .Registry(L.A.World.GetRegistryRef())
        .Items(L.Model.Items)
        .RequestedGuiScale(2)
        .GetView([&L] { return L.View(); })
        .OnClick([&L, &Sent](const FHudContainerClick& Click) { Sent.Add(Click); L.Send(Click); })
        .OnClose([&bClosed] { bClosed = true; })
        .GetKey([&Bindings](FName Id) { return Bindings.FindRef(Id); });
    // clang-format on
    // Window-local GUI pixels to Slate's screen space, under a root geometry placed at Corner.
    FVector2D Corner(0, 0);
    auto Geometry = [&Corner] { return FGeometry::MakeRoot(FVector2D(1280, 720), FSlateLayoutTransform(Corner)); };
    auto At = [&Corner](int32 Slot) { return Corner + FVector2D((FIntPoint(232, 97) + ScreenSlotPoint(Slot)) * 2); };
    auto Mouse = [](FVector2D Position, FKey Button, bool bDown)
    {
        TSet<FKey> Pressed;
        if (bDown) Pressed.Add(Button);
        return FPointerEvent(0, Position, Position, Pressed, Button, 0, FModifierKeysState());
    };
    // A press and its release; a release that lets the capture go gets Slate's capture loss, as the application sends.
    auto Click = [&](int32 Slot, FKey Button)
    {
        Screen->OnMouseButtonDown(Geometry(), Mouse(At(Slot), Button, true));
        const FReply Reply = Screen->OnMouseButtonUp(Geometry(), Mouse(At(Slot), Button, false));
        if (Reply.ShouldReleaseMouse()) Screen->OnMouseCaptureLost(FCaptureLostEvent(0, 0));
    };
    FFoundationInventory& I = L.Inv();
    // The full inventory, as the authority sets it on the next command.
    I.bFull = true;
    const int32 Stone = L.A.Rules.FindItem(TEXT("stone")), Dirt = L.A.Rules.FindItem(TEXT("dirt"));
    for (int32 Slot = 0; Slot < FFoundationInventory::OffhandSlot; ++Slot)
        I.SetSlot(Slot, {});

    // Finding 1: a double-click survives the capture the first release lets go. Take 5 stone, then double-click the
    // same slot: the release collects the 3 elsewhere.
    I.SetSlot(9, I.MakeStack(Stone, 5));
    I.SetSlot(10, I.MakeStack(Stone, 3));
    Click(ScreenMain(0), EKeys::LeftMouseButton);
    Click(ScreenMain(0), EKeys::LeftMouseButton);
    TestTrue(TEXT("a double-click through Slate collects"),
        Sent.ContainsByPredicate(
            [](const FHudContainerClick& C) { return C.Type == EFoundationClickType::PickupAll; }) &&
            I.Carried.Count == 8 && I.Main[1].IsEmpty());

    // Finding 2: carrying stone, press on dirt, drag to an empty slot: the one collected slot is clicked.
    I.SetSlot(11, I.MakeStack(Dirt, 1));
    Sent.Reset();
    Screen->OnMouseButtonDown(Geometry(), Mouse(At(ScreenMain(2)), EKeys::LeftMouseButton, true));
    Screen->OnMouseMove(Geometry(), Mouse(At(ScreenMain(3)), EKeys::Invalid, false));
    if (Screen->OnMouseButtonUp(Geometry(), Mouse(At(ScreenMain(3)), EKeys::LeftMouseButton, false))
            .ShouldReleaseMouse())
        Screen->OnMouseCaptureLost(FCaptureLostEvent(0, 0));
    TestTrue(TEXT("a one-slot drag places in the slot it covered, not the pressed dirt"),
        I.Main[3].Item == Stone && I.Main[3].Count == 8 && I.Main[2].Item == Dirt && I.Carried.IsEmpty());

    // Finding 3: keys are the player's bindings. Keys hit-test the live cursor, so the root geometry is placed to put
    // the cursor over the slot (the real pointer is never moved).
    if (!FSlateApplication::IsInitialized())
    {
        // The editor always has one; without it finding 3 would go unchecked.
        AddError(TEXT("no Slate application: the key cases cannot run"));
        return false;
    }
    auto KeyOver = [&](int32 Slot, FKey Key)
    {
        Corner = FSlateApplication::Get().GetCursorPos() - FVector2D((FIntPoint(232, 97) + ScreenSlotPoint(Slot)) * 2);
        Sent.Reset();
        Screen->OnKeyDown(Geometry(), FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
    };
    KeyOver(ScreenMain(3), EKeys::One);
    TestTrue(TEXT("1 is not the hotbar key when Z is"), Sent.IsEmpty());
    KeyOver(ScreenMain(3), EKeys::Z);
    TestTrue(TEXT("Z swaps with hotbar slot 1"),
        Sent.Num() == 1 && Sent[0].Type == EFoundationClickType::Swap && Sent[0].Button == 0 && I.Hotbar[0].Count == 8);
    KeyOver(FScreenMenu::FirstHotbar, EKeys::G);
    TestTrue(TEXT("G swaps with the offhand"), Sent.Num() == 1 && Sent[0].Button == FFoundationInventory::OffhandSlot);
    KeyOver(FScreenMenu::OffhandSlot, EKeys::Q);
    TestTrue(TEXT("Q is not the drop key when X is"), Sent.IsEmpty());
    KeyOver(FScreenMenu::OffhandSlot, EKeys::X);
    TestTrue(TEXT("X throws"), Sent.Num() == 1 && Sent[0].Type == EFoundationClickType::Throw);
    KeyOver(FScreenMenu::OffhandSlot, EKeys::E);
    TestFalse(TEXT("E does not close when the inventory key is I"), bClosed);
    KeyOver(FScreenMenu::OffhandSlot, EKeys::I);
    TestTrue(TEXT("I closes"), bClosed);
    return true;
}
namespace
{
    // The real widget over a FScreenLab, at GUI scale 2 on a 1280 x 720 frame (the menu at 232,97), as the input test
    // builds it; Corner places the root geometry.
    struct FScreenWidget
    {
        FScreenLab& L;
        FScreenClicks Sent;
        bool bClosed = false;
        TMap<FName, FKey> Bindings;
        FVector2D Corner = FVector2D(0, 0);
        TSharedPtr<SHudContainerScreen> Screen;
        // A fixed press clock (Codex review 12, finding 7); unset, the platform's.
        TOptional<double> Now;

        explicit FScreenWidget(FScreenLab& InLab) : L(InLab)
        {
            // clang-format off
            Screen = SNew(SHudContainerScreen)
                .Registry(L.A.World.GetRegistryRef())
                .Items(L.Model.Items)
                .RequestedGuiScale(2)
                .GetView([this] { return L.View(); })
                .OnClick([this](const FHudContainerClick& Click) { Sent.Add(Click); L.Send(Click); })
                .OnClose([this] { bClosed = true; })
                .GetKey([this](FName Id) { return Bindings.FindRef(Id); })
                .GetTime([this] { return Now.Get(FPlatformTime::Seconds()); });
            // clang-format on
        }
        FGeometry Geometry() const { return FGeometry::MakeRoot(FVector2D(1280, 720), FSlateLayoutTransform(Corner)); }
        FVector2D At(int32 Slot) const { return Corner + FVector2D((FIntPoint(232, 97) + ScreenSlotPoint(Slot)) * 2); }
        static FPointerEvent Mouse(FVector2D Position, FKey Button, bool bDown)
        {
            TSet<FKey> Pressed;
            if (bDown) Pressed.Add(Button);
            return FPointerEvent(0, Position, Position, Pressed, Button, 0, FModifierKeysState());
        }
        // A press and its release; a release that lets the capture go gets Slate's capture loss.
        void Click(int32 Slot, FKey Button)
        {
            Screen->OnMouseButtonDown(Geometry(), Mouse(At(Slot), Button, true));
            const FReply Reply = Screen->OnMouseButtonUp(Geometry(), Mouse(At(Slot), Button, false));
            if (Reply.ShouldReleaseMouse()) Screen->OnMouseCaptureLost(FCaptureLostEvent(0, 0));
        }
        // A mouse button pressed with the live cursor over Slot (the root geometry moves; the pointer never does).
        FReply ButtonOver(int32 Slot, FKey Button)
        {
            Corner =
                FSlateApplication::Get().GetCursorPos() - FVector2D((FIntPoint(232, 97) + ScreenSlotPoint(Slot)) * 2);
            Sent.Reset();
            const FReply Reply = Screen->OnMouseButtonDown(Geometry(), Mouse(At(Slot), Button, true));
            Screen->OnMouseButtonUp(Geometry(), Mouse(At(Slot), Button, false));
            return Reply;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHudContainerResultClicksTest, "Unnamed.Hud.Container.ResultClicks", ContainerScreenTestFlags)

bool FHudContainerResultClicksTest::RunTest(const FString&)
{
    // Codex review 10, finding 4: two quick left clicks on the result slot through the real widget craft twice (no
    // double-click there; provisional, vanilla play). Two logs in the 2x2 end as 8 planks on the cursor. Both presses
    // at the same instant (review 12, finding 7): a double-click would have to be refused, not merely too slow.
    FScreenLab L;
    FScreenWidget W(L);
    W.Now = 100.;
    FFoundationInventory& I = L.Inv();
    I.bFull = true;
    for (int32 Slot = 0; Slot < FFoundationInventory::OffhandSlot; ++Slot)
        I.SetSlot(Slot, {});
    I.Craft[0] = I.MakeStack(L.A.Rules.FindItem(TEXT("oak_log")), 2);
    W.Click(FScreenMenu::ResultSlot, EKeys::LeftMouseButton);
    W.Click(FScreenMenu::ResultSlot, EKeys::LeftMouseButton);
    const int32 Planks = L.A.Rules.FindItem(TEXT("oak_planks"));
    TestTrue(TEXT("8 planks on the cursor"), I.Carried.Item == Planks && I.Carried.Count == 8);
    TestTrue(TEXT("both logs used"), I.Craft[0].IsEmpty());
    TestFalse(TEXT("no pick-all sent"),
        W.Sent.ContainsByPredicate(
            [](const FHudContainerClick& C) { return C.Type == EFoundationClickType::PickupAll; }));
    // Elsewhere the double-click still collects (step 5b): Unnamed.Hud.Container.Input, finding 1.
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHudContainerMouseBindingsTest, "Unnamed.Hud.Container.MouseBindings", ContainerScreenTestFlags)

bool FHudContainerMouseBindingsTest::RunTest(const FString&)
{
    // Codex review 10, finding 2: a thumb button bound to a screen action acts as a key would.
    if (!FSlateApplication::IsInitialized())
    {
        AddError(TEXT("no Slate application: the cursor-placed cases cannot run"));
        return false;
    }
    FScreenLab L;
    FScreenWidget W(L);
    W.Bindings = {{TEXT("key.inventory"), EKeys::E}, {TEXT("key.hotbar.1"), EKeys::ThumbMouseButton},
        {TEXT("key.swapOffhand"), EKeys::F}, {TEXT("key.drop"), EKeys::ThumbMouseButton2}};
    FFoundationInventory& I = L.Inv();
    I.bFull = true;
    for (int32 Slot = 0; Slot < FFoundationInventory::OffhandSlot; ++Slot)
        I.SetSlot(Slot, {});
    const int32 Stone = L.A.Rules.FindItem(TEXT("stone"));
    I.SetSlot(9, I.MakeStack(Stone, 5));
    const FReply Swap = W.ButtonOver(ScreenMain(0), EKeys::ThumbMouseButton);
    TestTrue(TEXT("thumb 1 swaps with hotbar slot 1"),
        Swap.IsEventHandled() && W.Sent.Num() == 1 && W.Sent[0].Type == EFoundationClickType::Swap &&
            W.Sent[0].Button == 0 && I.Hotbar[0].Count == 5);
    W.ButtonOver(FScreenMenu::FirstHotbar, EKeys::ThumbMouseButton2);
    TestTrue(TEXT("thumb 2 throws"), W.Sent.Num() == 1 && W.Sent[0].Type == EFoundationClickType::Throw);
    // An unbound thumb button does nothing and is not handled; the inventory key bound to it closes.
    W.Bindings.Add(TEXT("key.drop"), EKeys::Q);
    const FReply Unbound = W.ButtonOver(FScreenMenu::FirstHotbar, EKeys::ThumbMouseButton2);
    TestTrue(TEXT("unbound thumb: nothing"), !Unbound.IsEventHandled() && W.Sent.IsEmpty());
    W.Bindings.Add(TEXT("key.inventory"), EKeys::ThumbMouseButton2);
    W.ButtonOver(FScreenMenu::FirstHotbar, EKeys::ThumbMouseButton2);
    TestTrue(TEXT("thumb bound to the inventory closes"), W.bClosed);
    // Left, right and middle stay clicks.
    W.Bindings.Add(TEXT("key.hotbar.1"), EKeys::LeftMouseButton);
    W.ButtonOver(ScreenMain(3), EKeys::LeftMouseButton);
    TestTrue(TEXT("left is a click, never a binding"),
        !W.Sent.ContainsByPredicate([](const FHudContainerClick& C) { return C.Type == EFoundationClickType::Swap; }) &&
            W.Sent.ContainsByPredicate([](const FHudContainerClick& C)
                { return C.Type == EFoundationClickType::Pickup && C.Slot == ScreenMain(3); }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudContainerFurnaceTest, "Unnamed.Hud.Container.Furnace", ContainerScreenTestFlags)

bool FHudContainerFurnaceTest::RunTest(const FString&)
{
    // T1 step 8a2: the furnace's screen (value-swaps.md "FurnaceScreen"; input 0, fuel 1, result 2, main 3-29, hotbar
    // 30-38).
    constexpr EFoundationMenuKind Furnace = EFoundationMenuKind::Furnace;
    auto At = [Furnace](int32 Slot) { return FHudContainerModel::GetSlotPosition(Furnace, Slot); };
    TestEqual(TEXT("input at 56,17"), At(0), FIntPoint(56, 17));
    TestEqual(TEXT("fuel at 56,53"), At(1), FIntPoint(56, 53));
    TestEqual(TEXT("result at 116,35"), At(2), FIntPoint(116, 35));
    TestEqual(TEXT("first main slot at 8,84"), At(3), FIntPoint(8, 84));
    TestEqual(TEXT("last main slot at 152,120"), At(29), FIntPoint(152, 120));
    TestEqual(TEXT("first hotbar slot at 8,142"), At(30), FIntPoint(8, 142));
    TestEqual(TEXT("recipe-book button at 20,34"), FHudContainerModel::GetRecipeButton(Furnace), FIntPoint(20, 34));
    FHudContainerModel Model = MakeScreenModel();
    Model.Kind = Furnace;
    Model.Items.Add(TEXT("coal")).MaxStack = 64;
    Model.FuelItems.Add(TEXT("coal"));
    TestEqual(TEXT("39 slots"), Model.GetSlots(), 39);
    for (const int32 Slot : {0, 1, 2, 3, 29, 30, 38})
        TestEqual(
            FString::Printf(TEXT("hit test finds slot %d"), Slot), Model.HitTest(At(Slot) + FIntPoint(8, 8)), Slot);

    // A drag skips the result, and the fuel slot unless the stack is fuel.
    FHudContainerView View;
    View.Slots.SetNum(39);
    for (const FName Item : {FName(TEXT("stone")), FName(TEXT("coal"))})
    {
        View.Carried = {Item, 10};
        FHudContainerModel Drag = Model;
        Drag.MouseDown(At(3) + FIntPoint(8, 8), 0, false, View, 0);
        for (const int32 Slot : {0, 1, 2, 4})
            Drag.MouseMove(At(Slot) + FIntPoint(8, 8), View);
        const TArray<int32> Expected = Item == TEXT("coal") ? TArray<int32>{3, 0, 1, 4} : TArray<int32>{3, 0, 4};
        ExpectSame(*this, *FString::Printf(TEXT("a drag of %s"), *Item.ToString()), Drag.GetDragSlots(), Expected);
    }

    // A double-click collects on the input and on the output, which does not craft (Codex review 22, finding 2), in
    // all three kinds.
    View.Carried = {TEXT("coal"), 1};
    for (const EFoundationMenuKind Kind :
        {EFoundationMenuKind::Furnace, EFoundationMenuKind::Smoker, EFoundationMenuKind::BlastFurnace})
        for (const int32 Slot : {0, 2})
        {
            FHudContainerModel Clicks = Model;
            Clicks.Kind = Kind;
            const FIntPoint Point = At(Slot) + FIntPoint(8, 8);
            Clicks.MouseDown(Point, 0, false, View, 0);
            Clicks.MouseUp(Point, 0, View);
            Clicks.MouseDown(Point, 0, false, View, .1);
            const bool bPickAll = Clicks.MouseUp(Point, 0, View)
                                      .ContainsByPredicate([](const FHudContainerClick& C)
                                          { return C.Type == EFoundationClickType::PickupAll; });
            TestTrue(FString::Printf(TEXT("kind %d: a double-click on slot %d collects"), int32(Kind), Slot), bPickAll);
        }
    // A right press then a left press on the output within 250 ms is two clicks, not a collect (Codex review 29,
    // finding 1: vanilla's double-click needs the same button, survival2.r23.inventory_label).
    {
        FHudContainerModel Clicks = Model;
        const FIntPoint Point = At(2) + FIntPoint(8, 8);
        Clicks.MouseDown(Point, 1, false, View, 0);
        Clicks.MouseUp(Point, 1, View);
        Clicks.MouseDown(Point, 0, false, View, .1);
        TestFalse(TEXT("right then left: no collect"),
            Clicks.MouseUp(Point, 0, View)
                .ContainsByPredicate(
                    [](const FHudContainerClick& C) { return C.Type == EFoundationClickType::PickupAll; }));
    }

    // The crafting table's result still never starts a collect: each click there crafts.
    {
        FHudContainerModel Table = MakeScreenModel();
        Table.Kind = EFoundationMenuKind::Crafting;
        FHudContainerView Grid;
        Grid.Slots.SetNum(Table.GetSlots());
        Grid.Carried = {TEXT("coal"), 1};
        const FIntPoint Point = FHudContainerModel::GetSlotPosition(EFoundationMenuKind::Crafting, 0) + FIntPoint(8, 8);
        Table.MouseDown(Point, 0, false, Grid, 0);
        Table.MouseUp(Point, 0, Grid);
        Table.MouseDown(Point, 0, false, Grid, .1);
        TestFalse(TEXT("crafting result: a double-click does not collect"),
            Table.MouseUp(Point, 0, Grid)
                .ContainsByPredicate(
                    [](const FHudContainerClick& C) { return C.Type == EFoundationClickType::PickupAll; }));
    }

    // The drag preview holds the fuel slot to one empty bucket, as the authority does (Codex review 22, finding 3): 8
    // buckets over the fuel slot and an empty inventory slot show 1 and 4, with 3 left on the cursor; a fuel slot
    // already holding a bucket takes no drag.
    for (const EFoundationMenuKind Kind :
        {EFoundationMenuKind::Furnace, EFoundationMenuKind::Smoker, EFoundationMenuKind::BlastFurnace})
    {
        FHudContainerModel Buckets = Model;
        Buckets.Kind = Kind;
        Buckets.Items.Add(TEXT("bucket")).MaxStack = 16;
        Buckets.FuelItems.Add(TEXT("bucket"));
        FHudContainerView Empty;
        Empty.Slots.SetNum(39);
        Empty.Carried = {TEXT("bucket"), 8};
        FHudContainerModel Drag = Buckets;
        Drag.MouseDown(At(1) + FIntPoint(8, 8), 0, false, Empty, 0);
        Drag.MouseMove(At(3) + FIntPoint(8, 8), Empty);
        TMap<int32, FHudCreativeStack> Preview;
        FHudCreativeStack Cursor;
        Drag.GetDragPreview(Empty, Preview, Cursor);
        TestTrue(FString::Printf(TEXT("kind %d: the preview shows 1 / 4 and 3 on the cursor"), int32(Kind)),
            Preview.Contains(1) && Preview[1].Count == 1 && Preview.Contains(3) && Preview[3].Count == 4 &&
                Cursor.Count == 3);
        Empty.Slots[1] = {TEXT("bucket"), 1};
        FHudContainerModel Held = Buckets;
        Held.MouseDown(At(3) + FIntPoint(8, 8), 0, false, Empty, 0);
        Held.MouseMove(At(1) + FIntPoint(8, 8), Empty);
        ExpectSame(*this, *FString::Printf(TEXT("kind %d: a held bucket takes no drag"), int32(Kind)),
            Held.GetDragSlots(), TArray<int32>{3});
    }

    // T1 step 8b: the smoker and blast furnace screens place every slot and the button as the furnace's
    // (value-swaps.md "SmokerScreen", "BlastFurnaceScreen").
    for (const EFoundationMenuKind Kind : {EFoundationMenuKind::Smoker, EFoundationMenuKind::BlastFurnace})
    {
        bool bSame = FHudContainerModel::GetRecipeButton(Kind) == FHudContainerModel::GetRecipeButton(Furnace);
        for (int32 Slot = 0; Slot < 39; ++Slot)
            bSame &= FHudContainerModel::GetSlotPosition(Kind, Slot) == At(Slot);
        TestTrue(FString::Printf(TEXT("menu kind %d: the furnace's positions"), int32(Kind)), bSame);
    }

    // A shift-double-click moves like stacks from the clicked slot's side only (Codex review 22, finding 1; vanilla
    // "from that inventory"): logs in the input and in the inventory, the input's shift-clicked out, and the release
    // sends none of the inventory's back in. From the inventory side, the input's logs stay.
    for (const EFoundationMenuKind Kind :
        {EFoundationMenuKind::Furnace, EFoundationMenuKind::Smoker, EFoundationMenuKind::BlastFurnace})
    {
        FHudContainerModel Logs = Model;
        Logs.Kind = Kind;
        FHudContainerView Both;
        Both.Slots.SetNum(39);
        Both.Slots[0] = {TEXT("oak_log"), 8};
        Both.Slots[3] = {TEXT("oak_log"), 8};
        const FIntPoint Input = At(0) + FIntPoint(8, 8);
        ExpectSame(*this, *FString::Printf(TEXT("kind %d: shift-click the input"), int32(Kind)),
            Logs.MouseDown(Input, 0, true, Both, 0), FScreenClicks{{0, 0, EFoundationClickType::QuickMove}});
        Logs.MouseUp(Input, 0, Both);
        Both.Slots[0] = {};
        Both.Slots[3] = {TEXT("oak_log"), 16};
        Logs.MouseDown(Input, 0, true, Both, .1);
        ExpectSame(*this, *FString::Printf(TEXT("kind %d: the release leaves the inventory's logs"), int32(Kind)),
            Logs.MouseUp(Input, 0, Both), FScreenClicks());

        FHudContainerModel Player = Model;
        Player.Kind = Kind;
        Both.Slots[0] = {TEXT("oak_log"), 8};
        Both.Slots[3] = {TEXT("oak_log"), 8};
        Both.Slots[4] = {TEXT("oak_log"), 8};
        const FIntPoint Main = At(3) + FIntPoint(8, 8);
        Player.MouseDown(Main, 0, true, Both, 1);
        Player.MouseUp(Main, 0, Both);
        Both.Slots[3] = {};
        Player.MouseDown(Main, 0, true, Both, 1.1);
        ExpectSame(*this, *FString::Printf(TEXT("kind %d: from the inventory, its logs only"), int32(Kind)),
            Player.MouseUp(Main, 0, Both), FScreenClicks{{4, 0, EFoundationClickType::QuickMove}});
    }

    // The flame and arrow sizes (survival.r3.sprites): 0/25/50/75/100 % -> flame 0 (hidden)/5/8/11/14, arrow
    // 0/6/12/18/24.
    TestEqual(TEXT("unlit: no flame"), FHudContainerModel::GetFlameHeight(false, 1), 0);
    const float Fractions[] = {0, .25f, .5f, .75f, 1};
    const int32 Flames[] = {0, 5, 8, 11, 14}, Arrows[] = {0, 6, 12, 18, 24};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Fractions); ++Index)
    {
        TestEqual(FString::Printf(TEXT("flame at %.2f"), Fractions[Index]),
            FHudContainerModel::GetFlameHeight(true, Fractions[Index]), Flames[Index]);
        TestEqual(FString::Printf(TEXT("arrow at %.2f"), Fractions[Index]),
            FHudContainerModel::GetArrowWidth(Fractions[Index]), Arrows[Index]);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudContainerBarrelTest, "Unnamed.Hud.Container.Barrel", ContainerScreenTestFlags)

bool FHudContainerBarrelTest::RunTest(const FString&)
{
    // T1 step 9: the barrel's screen (value-swaps.md "Barrel", inventory.layouts: 176 x 168, container 0-26 at 8 + 18c,
    // y 18/36/54; main 27-53 at y 85/103/121; hotbar 54-62 at y 143).
    constexpr EFoundationMenuKind Barrel = EFoundationMenuKind::Barrel;
    auto At = [Barrel](int32 Slot) { return FHudContainerModel::GetSlotPosition(Barrel, Slot); };
    TestEqual(TEXT("container 0 at 8,18"), At(0), FIntPoint(8, 18));
    TestEqual(TEXT("container 8 at 152,18"), At(8), FIntPoint(152, 18));
    TestEqual(TEXT("container 26 at 152,54"), At(26), FIntPoint(152, 54));
    TestEqual(TEXT("first main slot at 8,85"), At(27), FIntPoint(8, 85));
    TestEqual(TEXT("last main slot at 152,121"), At(53), FIntPoint(152, 121));
    TestEqual(TEXT("first hotbar slot at 8,143"), At(54), FIntPoint(8, 143));
    TestEqual(TEXT("last hotbar slot at 152,143"), At(62), FIntPoint(152, 143));
    TestEqual(TEXT("168 high"), FHudContainerModel::GetHeight(Barrel), 168);
    TestEqual(TEXT("the furnace stays 166 high"), FHudContainerModel::GetHeight(EFoundationMenuKind::Furnace), 166);
    FHudContainerModel Model = MakeScreenModel();
    Model.Kind = Barrel;
    TestEqual(TEXT("63 slots"), Model.GetSlots(), 63);
    for (const int32 Slot : {0, 13, 26, 27, 53, 54, 62})
        TestEqual(
            FString::Printf(TEXT("hit test finds slot %d"), Slot), Model.HitTest(At(Slot) + FIntPoint(8, 8)), Slot);
    // The window's last row (y 167) is inside it; y 168 is outside.
    TestEqual(TEXT("y 167 is in the window"), Model.HitTest({2, 167}), int32(INDEX_NONE));
    TestEqual(TEXT("y 168 is outside"), Model.HitTest({2, 168}), int32(FScreenMenu::Outside));
    // No recipe-book button (vanilla's ContainerScreen): the inventory's button area is plain window.
    const FIntPoint Button = FHudContainerModel::GetRecipeButton(EFoundationMenuKind::Inventory) + FIntPoint(2, 2);
    TestFalse(TEXT("no recipe-book button"), FHudContainerModel::IsInRecipeButton(Barrel, Button));
    TestTrue(TEXT("the inventory keeps its button"),
        FHudContainerModel::IsInRecipeButton(EFoundationMenuKind::Inventory, Button));
    // Every container slot takes a drag (no result or fuel slot).
    FHudContainerView View;
    View.Slots.SetNum(Model.GetSlots());
    View.Carried = {TEXT("dirt"), 64};
    FHudContainerModel Drag = Model;
    Drag.MouseDown(At(0) + FIntPoint(8, 8), 0, false, View, 0);
    for (const int32 Slot : {1, 26, 27})
        Drag.MouseMove(At(Slot) + FIntPoint(8, 8), View);
    ExpectSame(
        *this, TEXT("a drag over the barrel and the main grid"), Drag.GetDragSlots(), TArray<int32>{0, 1, 26, 27});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudContainerChestTest, "Unnamed.Hud.Container.Chest", ContainerScreenTestFlags)

bool FHudContainerChestTest::RunTest(const FString&)
{
    // T1 step 18-chest: a single chest's screen is the barrel's (value-swaps.md l.47: 176 x 168; 8,18 / 8,85 / 8,143).
    constexpr EFoundationMenuKind Chest = EFoundationMenuKind::Chest;
    for (const int32 Slot : {0, 26, 27, 53, 54, 62})
        TestEqual(FString::Printf(TEXT("slot %d as the barrel's"), Slot),
            FHudContainerModel::GetSlotPosition(Chest, Slot),
            FHudContainerModel::GetSlotPosition(EFoundationMenuKind::Barrel, Slot));
    TestEqual(TEXT("168 high"), FHudContainerModel::GetHeight(Chest), 168);
    TestFalse(TEXT("no recipe-book button"), FHudContainerModel::HasRecipeButton(Chest));
    FHudContainerModel Model = MakeScreenModel();
    Model.Kind = Chest;
    TestEqual(TEXT("63 slots"), Model.GetSlots(), 63);
    TestEqual(TEXT("y 167 is in the window"), Model.HitTest({2, 167}), int32(INDEX_NONE));
    TestEqual(TEXT("y 168 is outside"), Model.HitTest({2, 168}), int32(FScreenMenu::Outside));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHudContainerLargeChestTest, "Unnamed.Hud.Container.LargeChest", ContainerScreenTestFlags)

bool FHudContainerLargeChestTest::RunTest(const FString&)
{
    // T1 step 18-chest: a double chest's screen (generic_54 with 6 rows; value-swaps.md l.48, inventory.layouts: 176 x
    // 222, container 0-53 at 8 + 18c, y 18..108; main 54-80 at y 139/157/175; hotbar 81-89 at y 197).
    constexpr EFoundationMenuKind Large = EFoundationMenuKind::LargeChest;
    auto At = [Large](int32 Slot) { return FHudContainerModel::GetSlotPosition(Large, Slot); };
    TestEqual(TEXT("container 0 at 8,18"), At(0), FIntPoint(8, 18));
    TestEqual(TEXT("container 27 at 8,72"), At(27), FIntPoint(8, 72));
    TestEqual(TEXT("container 53 at 152,108"), At(53), FIntPoint(152, 108));
    TestEqual(TEXT("first main slot at 8,139"), At(54), FIntPoint(8, 139));
    TestEqual(TEXT("last main slot at 152,175"), At(80), FIntPoint(152, 175));
    TestEqual(TEXT("first hotbar slot at 8,197"), At(81), FIntPoint(8, 197));
    TestEqual(TEXT("last hotbar slot at 152,197"), At(89), FIntPoint(152, 197));
    TestEqual(TEXT("222 high"), FHudContainerModel::GetHeight(Large), 222);
    TestEqual(TEXT("Inventory label at y 128 (imageHeight - 94)"), FHudContainerModel::LargeChestInventoryLabelY,
        FHudContainerModel::GetHeight(Large) - 94);
    TestFalse(TEXT("no recipe-book button"), FHudContainerModel::HasRecipeButton(Large));
    FHudContainerModel Model = MakeScreenModel();
    Model.Kind = Large;
    TestEqual(TEXT("90 slots"), Model.GetSlots(), 90);
    for (const int32 Slot : {0, 26, 27, 53, 54, 80, 81, 89})
        TestEqual(
            FString::Printf(TEXT("hit test finds slot %d"), Slot), Model.HitTest(At(Slot) + FIntPoint(8, 8)), Slot);
    TestEqual(TEXT("y 221 is in the window"), Model.HitTest({2, 221}), int32(INDEX_NONE));
    TestEqual(TEXT("y 222 is outside"), Model.HitTest({2, 222}), int32(FScreenMenu::Outside));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHudContainerDragCancelTest, "Unnamed.Hud.Container.DragCancel", ContainerScreenTestFlags)

bool FHudContainerDragCancelTest::RunTest(const FString&)
{
    // QC RA1-3 (minecraft.wiki "Controls"): carrying 8 stone, a left drag over two empty slots, a tap of the right
    // button, then the left release: the drag is cancelled, nothing moves and the cursor keeps all 8.
    FScreenLab L;
    const int32 Stone = L.A.Rules.FindItem(TEXT("stone"));
    FFoundationInventory& I = L.Inv();
    I.Carried = I.MakeStack(Stone, 8);
    L.Down(ScreenMain(0), 0, 0);
    L.Over(ScreenMain(1));
    L.Down(ScreenMain(1), 1, 0.5);
    L.Up(ScreenMain(1), 1);
    L.Up(ScreenMain(1), 0);
    TestTrue(TEXT("cancelled: nothing placed, the cursor keeps 8"),
        I.Main[0].IsEmpty() && I.Main[1].IsEmpty() && ScreenHolds(I.Carried, Stone, 8));
    TestFalse(TEXT("no drag left"), L.Model.IsDragging());
    // A right drag is cancelled by the left button the same way.
    L.Down(ScreenMain(0), 1, 2);
    L.Over(ScreenMain(1));
    L.Down(ScreenMain(1), 0, 2.5);
    L.Up(ScreenMain(1), 0);
    L.Up(ScreenMain(1), 1);
    TestTrue(TEXT("right drag cancelled by the left button"),
        I.Main[0].IsEmpty() && I.Main[1].IsEmpty() && ScreenHolds(I.Carried, Stone, 8));
    return true;
}
#endif
