#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationContainer.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "HAL/IConsoleManager.h"
#include "Hud/HudContainer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags TableTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Survival step 7's lab: the project's rules and blocks, the full inventory, block use on, an empty inventory but
    // oak planks in hotbar slot 3, and the player's eye looking at the cell it aims at (a crafting table by default).
    struct FTableLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        FFoundationInteraction I;
        int32 Player = 0;
        int64 StepIndex = 0;
        bool bSneaking = false;
        FVector Eye = FVector(50, 50, 150);
        FIntVector Aimed = FIntVector(2, 0, 1);

        FTableLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bInfiniteSupply = false;
            A.Options.bFullInventory = true;
            A.Options.bBlockUse = true;
            Player = A.AddPlayer();
            A.Reset();
            I.PlayerId = Player;
            I.SetPreset(*UFoundationInteractionSettings::Get().FindPreset(TEXT("VanillaReference")), .05);
            I.Resync(A);
            for (int32 Slot = 0; Slot < FFoundationInventory::Slots; ++Slot)
                Inv().SetSlot(Slot, {});
            Inv().SetSlot(3, Inv().MakeStack(Item(TEXT("oak_planks")), 16));
            Inv().Select(3);
            Put(TEXT("crafting_table"));
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }
        FFoundationInventory& Inv() { return P().Inventory; }
        int32 Item(const TCHAR* Id) const { return A.Rules.FindItem(Id); }

        // The next reference step, aiming at the centre of Aimed.
        void NextStep()
        {
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = Eye - FVector(0, 0, 162);
            Facts.EyeCm = Eye;
            Facts.Aim = (FoundationGrid::CellBox(Aimed, A.World.OriginCm).GetCenter() - Eye).GetSafeNormal();
            Facts.bSecondaryUse = bSneaking;
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
            A.Step();
        }

        void Put(FName Id)
        {
            A.World.Set(Aimed, A.World.FindBlock(Id)->GetDefaultState());
            NextStep();
        }

        FFoundationCommand Make(EFoundationCommandType Type)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = I.NextSequence++;
            Command.Type = Type;
            return Command;
        }

        FFoundationResult Use(EFoundationHand Hand = EFoundationHand::Main)
        {
            FFoundationCommand Command = Make(EFoundationCommandType::UseOn);
            Command.Hand = Hand;
            Command.Item = Inv().GetHand(Hand).GetRef();
            Command.Target = A.Resolve(P().Facts);
            FIntVector Destination;
            EFoundationReason Reason;
            if (A.GetDestination(P(), Hand, Command.Target, Destination, Reason)) Command.Destination = Destination;
            return A.Execute(Player, Command, Vitals);
        }

        FFoundationResult Click(int32 Container, int32 Slot, int32 Button, EFoundationClickType Type)
        {
            FFoundationCommand Command = Make(EFoundationCommandType::ContainerClick);
            Command.ContainerId = Container;
            Command.Slot = Slot;
            Command.Button = Button;
            Command.ClickType = Type;
            return A.Execute(Player, Command, Vitals);
        }

        FFoundationResult Close() { return A.Execute(Player, Make(EFoundationCommandType::ContainerClose), Vitals); }

        // Opens the table at Aimed (not sneaking); the container id.
        int32 Open()
        {
            bSneaking = false;
            NextStep();
            return Use().OpenedContainer;
        }

        // Lays a pattern into the open table's grid (row-major, nullptr empty), one item each.
        void Lay(std::initializer_list<const TCHAR*> Cells)
        {
            int32 Index = 0;
            for (const TCHAR* Id : Cells)
            {
                P().OpenMenu.Grid[Index] = Id ? Inv().MakeStack(Item(Id), 1) : FFoundationItemInstance();
                ++Index;
            }
        }

        FFoundationItemInstance Menu(int32 Slot)
        {
            const TArray<FFoundationItemInstance> Slots = A.GetOpenMenu(P());
            return Slots.IsValidIndex(Slot) ? Slots[Slot] : FFoundationItemInstance();
        }

        bool Holds(const FFoundationItemInstance& Stack, const TCHAR* Id, int32 Count) const
        {
            return Count == 0 ? Stack.IsEmpty() : !Stack.IsEmpty() && Stack.Item == Item(Id) && Stack.Count == Count;
        }

        int32 CountEvents(EFoundationEventKind Kind)
        {
            int32 Count = 0;
            for (const FFoundationEvent& Event : A.GetEvents())
                Count += Event.Kind == Kind;
            return Count;
        }

        FName BlockAt(const FIntVector& Cell) { return A.World.GetBlock(A.World.Get(Cell).State).Id; }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationCraftingTableLayoutTest, "Unnamed.Foundation.CraftingTable.Layout", TableTestFlags)

bool FFoundationCraftingTableLayoutTest::RunTest(const FString&)
{
    // The crafting table's menu (value-swaps.md, "Crafting table"): 0 the result, 1-9 the 3x3, 10-36 the main grid,
    // 37-45 the hotbar; no armor or offhand.
    const FFoundationMenuLayout& Table = FFoundationMenuLayout::Get(EFoundationMenuKind::Crafting);
    TestEqual(TEXT("table slots"), Table.Slots, 46);
    TestTrue(TEXT("table grid 1-9"), Table.IsCraft(1) && Table.IsCraft(9) && !Table.IsCraft(0) && !Table.IsCraft(10));
    TestTrue(TEXT("table 3x3"), Table.CraftWidth == 3 && Table.CraftHeight == 3);
    TestTrue(TEXT("table main 10-36"), Table.IsMain(10) && Table.IsMain(36) && !Table.IsMain(9) && !Table.IsMain(37));
    TestTrue(TEXT("table hotbar 37-45"),
        Table.IsHotbar(37) && Table.IsHotbar(45) && !Table.IsHotbar(36) && !Table.IsHotbar(46));
    bool bNoArmor = true;
    for (int32 Slot = -1; Slot <= 46; ++Slot)
        bNoArmor &= !Table.IsArmor(Slot) && !Table.IsOffhand(Slot);
    TestTrue(TEXT("table has no armor or offhand"), bNoArmor);
    // Inventory numbers: hotbar 0-8, main 9-35; armor and offhand have no table slot.
    bool bRoundTrip = true;
    for (int32 Slot = 0; Slot < FFoundationInventory::FirstArmor; ++Slot)
        bRoundTrip &= Table.ToInventorySlot(Table.ToMenuSlot(Slot)) == Slot;
    TestTrue(TEXT("table round trip, hotbar and main"), bRoundTrip);
    TestEqual(TEXT("hotbar 0 is table 37"), Table.ToMenuSlot(0), 37);
    TestEqual(TEXT("main 9 is table 10"), Table.ToMenuSlot(FFoundationInventory::FirstMain), 10);
    TestEqual(TEXT("armor has no table slot"), Table.ToMenuSlot(FFoundationInventory::FirstArmor), int32(INDEX_NONE));
    TestEqual(
        TEXT("offhand has no table slot"), Table.ToMenuSlot(FFoundationInventory::OffhandSlot), int32(INDEX_NONE));
    TestEqual(TEXT("table grid is no inventory slot"), Table.ToInventorySlot(5), int32(INDEX_NONE));

    // The player's menu keeps step 5b's numbering: every menu slot maps as before.
    using Menu = FFoundationContainerMenu;
    const FFoundationMenuLayout& Player = FFoundationMenuLayout::Get(EFoundationMenuKind::Inventory);
    TestEqual(TEXT("player slots"), Player.Slots, Menu::PlayerMenuSlots);
    bool bSame = true;
    for (int32 Slot = -1; Slot <= Menu::PlayerMenuSlots; ++Slot)
    {
        const int32 Expected = Slot >= Menu::FirstArmor && Slot < Menu::FirstMain
            ? FFoundationInventory::FirstArmor + FFoundationInventory::ArmorSlots - 1 - (Slot - Menu::FirstArmor)
            : Slot >= Menu::FirstMain && Slot < Menu::FirstHotbar   ? Slot
            : Slot >= Menu::FirstHotbar && Slot < Menu::OffhandSlot ? Slot - Menu::FirstHotbar
            : Slot == Menu::OffhandSlot                             ? FFoundationInventory::OffhandSlot
                                                                    : int32(INDEX_NONE);
        bSame &= Menu::ToInventorySlot(Slot) == Expected;
        if (Expected != INDEX_NONE) bSame &= Menu::ToMenuSlot(Expected) == Slot;
    }
    TestTrue(TEXT("player menu numbering unchanged"), bSame);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationCraftingTableUseTest, "Unnamed.Foundation.CraftingTable.Use", TableTestFlags)

bool FFoundationCraftingTableUseTest::RunTest(const FString&)
{
    // Right-click with planks in hand: the table opens (vanilla), nothing is placed.
    {
        FTableLab L;
        const int32 Placements = L.A.Placements;
        const FFoundationResult Result = L.Use();
        TestTrue(TEXT("use committed"), Result.IsCommitted());
        TestEqual(TEXT("container 1 opened"), Result.OpenedContainer, 1);
        TestTrue(TEXT("crafting menu"), Result.OpenedMenu == EFoundationMenuKind::Crafting);
        TestFalse(TEXT("nothing placed"), Result.bPlaced);
        TestEqual(TEXT("placements"), L.A.Placements, Placements);
        TestTrue(TEXT("planks kept"), L.Holds(L.Inv().Hotbar[3], TEXT("oak_planks"), 16));
        TestEqual(TEXT("one opened event"), L.CountEvents(EFoundationEventKind::ContainerOpened), 1);
        TestTrue(TEXT("open menu"), L.P().OpenMenu.IsOpen() && L.P().OpenMenu.Kind == EFoundationMenuKind::Crafting);
        TestEqual(TEXT("open menu grid"), L.P().OpenMenu.Grid.Num(), 9);
        TestTrue(TEXT("open menu cell"), L.P().OpenMenu.Cell == L.Aimed);
        TestEqual(TEXT("open menu slots"), L.A.GetOpenMenu(L.P()).Num(), 46);
        // A second open is the next id; ids wrap after 100 (vanilla's containerId).
        TestEqual(TEXT("container 2"), L.Use().OpenedContainer, 2);
        L.P().LastContainerId = 100;
        TestEqual(TEXT("container 1 after 100"), L.Use().OpenedContainer, 1);
    }
    // Sneaking with an item places against the table instead (the sneak bypass).
    {
        FTableLab L;
        L.bSneaking = true;
        L.NextStep();
        const FFoundationResult Result = L.Use();
        TestTrue(TEXT("sneak places"), Result.bPlaced);
        TestEqual(TEXT("sneak opens nothing"), Result.OpenedContainer, 0);
        TestEqual(TEXT("placed planks"), L.BlockAt(Result.Cell), FName(TEXT("oak_planks")));
        TestFalse(TEXT("no open menu"), L.P().OpenMenu.IsOpen());
    }
    // Sneaking empty-handed still opens (the bypass needs something in hand).
    {
        FTableLab L;
        L.Inv().Select(0);
        L.bSneaking = true;
        L.NextStep();
        TestEqual(TEXT("empty-handed sneak opens"), L.Use().OpenedContainer, 1);
    }
    // foundation.Blocks.Use 0: right-click always places, as before step 7.
    {
        FTableLab L;
        L.A.Options.bBlockUse = false;
        const FFoundationResult Result = L.Use();
        TestTrue(TEXT("switch off places"), Result.bPlaced);
        TestEqual(TEXT("switch off opens nothing"), Result.OpenedContainer, 0);
    }
    // The off hand never uses the block: with the main hand empty and planks in the off hand, the off hand places.
    {
        FTableLab L;
        L.Inv().SetSlot(FFoundationInventory::OffhandSlot, L.Inv().MakeStack(L.Item(TEXT("oak_planks")), 4));
        L.Inv().Select(0);
        const FFoundationResult Result = L.Use(EFoundationHand::Off);
        TestEqual(TEXT("off hand opens nothing"), Result.OpenedContainer, 0);
        TestTrue(TEXT("off hand places"), Result.bPlaced);
    }
    // Another block has no use: right-click on stone places.
    {
        FTableLab L;
        L.Put(TEXT("stone"));
        const FFoundationResult Result = L.Use();
        TestTrue(TEXT("stone: placed"), Result.bPlaced);
        TestFalse(TEXT("stone has no use"), L.A.HasBlockUse(L.Aimed));
    }
    // The preview mirrors it: the table shows no placement; sneaking with an item shows one.
    {
        FTableLab L;
        TestTrue(TEXT("table has a use"), L.A.HasBlockUse(L.Aimed));
        L.I.UpdatePreview(L.A, L.P().Facts);
        TestTrue(TEXT("preview: interaction"), L.I.Preview.bInteraction && !L.I.Preview.bPlacement);
        L.bSneaking = true;
        L.NextStep();
        L.I.UpdatePreview(L.A, L.P().Facts);
        TestTrue(TEXT("preview sneaking: placement"), L.I.Preview.bPlacement && !L.I.Preview.bInteraction);
        L.bSneaking = false;
        L.A.Options.bBlockUse = false;
        L.NextStep();
        L.I.UpdatePreview(L.A, L.P().Facts);
        TestTrue(TEXT("preview switch off: placement"), L.I.Preview.bPlacement && !L.I.Preview.bInteraction);
    }
    // RegisterBlockUse (T5's entry point): a handler for a block id runs on the same terms; one that returns false
    // passes on to placing; a registered handler overrides the built-in one.
    {
        FTableLab L;
        int32 Calls = 0;
        L.A.RegisterBlockUse(TEXT("stone"),
            [&Calls](FFoundationAuthority&, FFoundationPlayer&, const FFoundationHit&, FFoundationResult& Result)
            {
                ++Calls;
                Result.Outcome = EFoundationOutcome::Committed;
                Result.bInteracted = true;
                return true;
            });
        L.Put(TEXT("stone"));
        TestTrue(TEXT("registered: has use"), L.A.HasBlockUse(L.Aimed));
        const FFoundationResult Used = L.Use();
        TestTrue(TEXT("registered use ran"), Calls == 1 && Used.bInteracted && !Used.bPlaced);
        L.bSneaking = true;
        L.NextStep();
        TestTrue(TEXT("registered: sneak places"), L.Use().bPlaced && Calls == 1);
    }
    {
        FTableLab L;
        L.A.RegisterBlockUse(TEXT("crafting_table"),
            [](FFoundationAuthority&, FFoundationPlayer&, const FFoundationHit&, FFoundationResult&) { return false; });
        const FFoundationResult Passed = L.Use();
        TestTrue(TEXT("a false handler passes to placing"), Passed.bPlaced && Passed.OpenedContainer == 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationCraftingTableMenuTest, "Unnamed.Foundation.CraftingTable.Menu", TableTestFlags)

bool FFoundationCraftingTableMenuTest::RunTest(const FString&)
{
    // 3x3 recipes (step 6's book): the wooden pickaxe (3 planks over 2 sticks), then the stone pickaxe.
    {
        FTableLab L;
        const int32 Id = L.Open();
        L.Lay({TEXT("oak_planks"), TEXT("oak_planks"), TEXT("oak_planks"), nullptr, TEXT("stick"), nullptr, nullptr,
            TEXT("stick"), nullptr});
        TestTrue(TEXT("result: wooden pickaxe"), L.Holds(L.Menu(0), TEXT("wooden_pickaxe"), 1));
        TestTrue(TEXT("take it"), L.Click(Id, 0, 0, EFoundationClickType::Pickup).IsCommitted());
        TestTrue(TEXT("cursor: wooden pickaxe"), L.Holds(L.Inv().Carried, TEXT("wooden_pickaxe"), 1));
        bool bUsed = true;
        for (const FFoundationItemInstance& Stack : L.P().OpenMenu.Grid)
            bUsed &= Stack.IsEmpty();
        TestTrue(TEXT("grid used up"), bUsed);
        // Into the hotbar (table slot 37 is hotbar 0).
        TestTrue(TEXT("place in hotbar"), L.Click(Id, 37, 0, EFoundationClickType::Pickup).IsCommitted());
        TestTrue(TEXT("hotbar 0: pickaxe"), L.Holds(L.Inv().Hotbar[0], TEXT("wooden_pickaxe"), 1));
        L.Lay({TEXT("cobblestone"), TEXT("cobblestone"), TEXT("cobblestone"), nullptr, TEXT("stick"), nullptr, nullptr,
            TEXT("stick"), nullptr});
        TestTrue(TEXT("result: stone pickaxe"), L.Holds(L.Menu(0), TEXT("stone_pickaxe"), 1));
        // A pattern only the 3x3 holds is not a 2x2 recipe: the same items in the player's grid make nothing.
        TestTrue(TEXT("player grid empty result"), L.A.GetPlayerMenu(L.P())[0].IsEmpty());
    }
    // Shift-clicks (provisional, minecraft.wiki "Crafting Table"): the player's slots fill the 3x3 first, the grid
    // goes back to the main grid and hotbar, the result crafts into them from the hotbar's last slot.
    {
        FTableLab L;
        const int32 Id = L.Open();
        L.Inv().SetSlot(FFoundationInventory::FirstMain, L.Inv().MakeStack(L.Item(TEXT("stick")), 5));
        TestTrue(TEXT("shift main"), L.Click(Id, 10, 0, EFoundationClickType::QuickMove).IsCommitted());
        TestTrue(TEXT("sticks into grid 1"), L.Holds(L.P().OpenMenu.Grid[0], TEXT("stick"), 5));
        TestTrue(TEXT("main emptied"), L.Inv().GetSlot(FFoundationInventory::FirstMain)->IsEmpty());
        TestTrue(TEXT("shift grid"), L.Click(Id, 1, 0, EFoundationClickType::QuickMove).IsCommitted());
        TestTrue(TEXT("grid emptied"), L.P().OpenMenu.Grid[0].IsEmpty());
        TestTrue(TEXT("sticks back in main 9"),
            L.Holds(*L.Inv().GetSlot(FFoundationInventory::FirstMain), TEXT("stick"), 5));
        // Hotbar planks (16 in hotbar 3, table slot 40): into the grid, merging into the first slot.
        TestTrue(TEXT("shift hotbar"), L.Click(Id, 40, 0, EFoundationClickType::QuickMove).IsCommitted());
        TestTrue(TEXT("planks into grid 1"), L.Holds(L.P().OpenMenu.Grid[0], TEXT("oak_planks"), 16));
        // One plank makes 4 sticks... no: 2 planks stacked make sticks. One plank alone is no recipe; lay 2 planks.
        L.P().OpenMenu.Grid[0] = L.Inv().MakeStack(L.Item(TEXT("oak_planks")), 2);
        L.P().OpenMenu.Grid[3] = L.Inv().MakeStack(L.Item(TEXT("oak_planks")), 2);
        TestTrue(TEXT("result: 4 sticks"), L.Holds(L.Menu(0), TEXT("stick"), 4));
        TestTrue(TEXT("shift result"), L.Click(Id, 0, 0, EFoundationClickType::QuickMove).IsCommitted());
        // Merging first: both crafts join the 5 sticks in main 9.
        TestTrue(TEXT("sticks crafted twice, merged into main 9"),
            L.Holds(*L.Inv().GetSlot(FFoundationInventory::FirstMain), TEXT("stick"), 13));
        TestTrue(TEXT("grid used up"), L.P().OpenMenu.Grid[0].IsEmpty() && L.P().OpenMenu.Grid[3].IsEmpty());
        // Into the hotbar's last slot when nothing merges (provisional, wiki "Crafting").
        L.Inv().SetSlot(FFoundationInventory::FirstMain, {});
        L.P().OpenMenu.Grid[0] = L.Inv().MakeStack(L.Item(TEXT("oak_planks")), 1);
        L.P().OpenMenu.Grid[3] = L.Inv().MakeStack(L.Item(TEXT("oak_planks")), 1);
        L.Click(Id, 0, 0, EFoundationClickType::QuickMove);
        TestTrue(TEXT("sticks into the hotbar's last slot"), L.Holds(L.Inv().Hotbar[8], TEXT("stick"), 4));
    }
    // Close: the cursor, then the 3x3, back into the inventory; the menu closes and its id goes stale.
    {
        FTableLab L;
        const int32 Id = L.Open();
        L.Lay({TEXT("stick"), nullptr, nullptr, nullptr, TEXT("cobblestone")});
        L.Inv().Carried = L.Inv().MakeStack(L.Item(TEXT("apple")), 2);
        TestTrue(TEXT("close"), L.Close().IsCommitted());
        TestFalse(TEXT("menu closed"), L.P().OpenMenu.IsOpen());
        TestTrue(TEXT("cursor emptied"), L.Inv().Carried.IsEmpty());
        // Return order: the cursor's apples first (hotbar 0), then the grid's stick and cobblestone.
        TestTrue(TEXT("apple returned first"), L.Holds(L.Inv().Hotbar[0], TEXT("apple"), 2));
        TestTrue(TEXT("stick returned"), L.Holds(L.Inv().Hotbar[1], TEXT("stick"), 1));
        TestTrue(TEXT("cobblestone returned"), L.Holds(L.Inv().Hotbar[2], TEXT("cobblestone"), 1));
        const FFoundationResult Stale = L.Click(Id, 37, 0, EFoundationClickType::Pickup);
        TestTrue(TEXT("stale id refused"), Stale.Reason == EFoundationReason::InvalidSlot);
        TestTrue(TEXT("player menu still works"),
            L.Click(FFoundationContainerMenu::PlayerContainer, 36, 0, EFoundationClickType::Pickup).IsCommitted());
    }
    // A close whose 3x3 does not fit throws the leftovers as Q does (items fixes), as for the 2x2: the stick and the
    // cursor's apple become item entities and the menu closes.
    {
        FTableLab L;
        L.A.Options.bItemEntities = true;
        L.Open();
        for (int32 Slot = 0; Slot < FFoundationInventory::FirstArmor; ++Slot)
            L.Inv().SetSlot(Slot, L.Inv().MakeStack(L.Item(TEXT("cobblestone")), 64));
        L.Lay({TEXT("stick")});
        L.Inv().Carried = L.Inv().MakeStack(L.Item(TEXT("apple")), 2);
        const int32 Before = L.A.GetItemEntities().Num();
        TestTrue(TEXT("full, entities on: close commits"), L.Close().IsCommitted());
        TestFalse(TEXT("full, entities on: menu closed"), L.P().OpenMenu.IsOpen());
        TestTrue(TEXT("full, entities on: cursor emptied"), L.Inv().Carried.IsEmpty());
        TestEqual(TEXT("full, entities on: two stacks thrown"), L.A.GetItemEntities().Num(), Before + 2);
        bool bStick = false, bApple = false;
        for (const FFoundationItemEntity& Entity : L.A.GetItemEntities())
        {
            bStick |= Entity.Item == L.Item(TEXT("stick")) && Entity.Count == 1;
            bApple |= Entity.Item == L.Item(TEXT("apple")) && Entity.Count == 2;
        }
        TestTrue(TEXT("full, entities on: the stick and the apples thrown"), bStick && bApple);
    }
    // With foundation.Items.Entities 0 such a close is refused; the grid stays with the open menu.
    {
        FTableLab L;
        L.Open();
        for (int32 Slot = 0; Slot < FFoundationInventory::FirstArmor; ++Slot)
            L.Inv().SetSlot(Slot, L.Inv().MakeStack(L.Item(TEXT("cobblestone")), 64));
        L.Lay({TEXT("stick")});
        const FFoundationResult Result = L.Close();
        TestTrue(TEXT("full: refused"), Result.Reason == EFoundationReason::DropDeferred);
        TestTrue(TEXT("full: grid kept"), L.P().OpenMenu.IsOpen() && L.Holds(L.P().OpenMenu.Grid[0], TEXT("stick"), 1));
        // Opened again, the kept grid carries over rather than vanish.
        L.Use();
        TestTrue(TEXT("reopen keeps the grid"), L.Holds(L.P().OpenMenu.Grid[0], TEXT("stick"), 1));
    }
    // A table broken while open: its clicks are refused (provisional stillValid); a wrong id too.
    {
        FTableLab L;
        const int32 Id = L.Open();
        TestTrue(TEXT("wrong id refused"),
            L.Click(Id + 1, 37, 0, EFoundationClickType::Pickup).Reason == EFoundationReason::InvalidSlot);
        L.Put(TEXT("air"));
        TestTrue(TEXT("broken table refused"),
            L.Click(Id, 37, 0, EFoundationClickType::Pickup).Reason == EFoundationReason::InvalidSlot);
        TestTrue(TEXT("close still returns"), L.Close().IsCommitted());
    }
    // Codex 5b finding 4: the client's container click copies foundation.Inventory.Full into the authority first.
    {
        FTableLab L;
        IConsoleVariable* Full = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Inventory.Full"));
        if (!TestNotNull(TEXT("foundation.Inventory.Full"), Full)) return false;
        const int32 Before = Full->GetInt();
        Full->Set(0, ECVF_SetByCode);
        L.A.Options.bFullInventory = true;
        L.I.ContainerClick(
            L.A, L.Vitals, FFoundationContainerMenu::PlayerContainer, 36, 0, EFoundationClickType::Pickup);
        TestFalse(TEXT("click copies the switch (0)"), L.A.Options.bFullInventory);
        TestFalse(TEXT("lab inventory applied"), L.Inv().bFull);
        Full->Set(1, ECVF_SetByCode);
        L.I.ContainerClose(L.A, L.Vitals);
        TestTrue(TEXT("close copies the switch (1)"), L.A.Options.bFullInventory);
        Full->Set(Before, ECVF_SetByCode);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHudContainerCraftingLayoutTest, "Unnamed.Hud.Container.CraftingLayout", TableTestFlags)

bool FHudContainerCraftingLayoutTest::RunTest(const FString&)
{
    // The crafting table's slot positions (value-swaps.md, "Crafting table"), each hit by its centre.
    FHudContainerModel Model;
    Model.Kind = EFoundationMenuKind::Crafting;
    struct FExpected
    {
        int32 Slot;
        FIntPoint At;
    };
    const FExpected Expected[] = {{0, {124, 35}}, {1, {30, 17}}, {3, {66, 17}}, {5, {48, 35}}, {9, {66, 53}},
        {10, {8, 84}}, {18, {152, 84}}, {36, {152, 120}}, {37, {8, 142}}, {45, {152, 142}}};
    for (const auto& [Slot, At] : Expected)
    {
        TestEqual(FString::Printf(TEXT("slot %d position"), Slot), Model.GetPosition(Slot), At);
        TestEqual(FString::Printf(TEXT("slot %d hit"), Slot), Model.HitTest(At + FIntPoint(8, 8)), Slot);
    }
    TestEqual(TEXT("slots"), Model.GetSlots(), 46);
    // The inventory's armor and offhand places are no slots on the table.
    TestEqual(TEXT("no armor"), Model.HitTest({16, 16}), int32(INDEX_NONE));
    TestEqual(TEXT("no offhand"), Model.HitTest({85, 70}), int32(INDEX_NONE));
    TestEqual(TEXT("outside"), Model.HitTest({-1, 10}), FFoundationContainerMenu::Outside);
    // The recipe-book button (provisional: 5,34).
    TestTrue(TEXT("recipe button"),
        FHudContainerModel::IsInRecipeButton(EFoundationMenuKind::Crafting, {5, 34}) &&
            FHudContainerModel::IsInRecipeButton(EFoundationMenuKind::Crafting, {24, 51}) &&
            !FHudContainerModel::IsInRecipeButton(EFoundationMenuKind::Crafting, {104, 61}));
    // Without the full inventory the main grid is no slot; the 3x3 and hotbar stay.
    Model.bFullInventory = false;
    TestEqual(TEXT("lab: main hidden"), Model.HitTest({16, 92}), int32(INDEX_NONE));
    TestEqual(TEXT("lab: grid kept"), Model.HitTest({38, 25}), 1);
    TestEqual(TEXT("lab: hotbar kept"), Model.HitTest({16, 150}), 37);
    Model.bFullInventory = true;

    // A drag over the 3x3 sends QUICK_CRAFT for the table's own slot numbers; the result slot never joins.
    FHudContainerView View;
    View.Slots.SetNum(46);
    View.Carried = {TEXT("oak_planks"), 3};
    Model.MouseDown(FIntPoint(30, 17) + FIntPoint(8, 8), 0, false, View, 0);
    Model.MouseMove(FIntPoint(124, 35) + FIntPoint(8, 8), View);
    Model.MouseMove(FIntPoint(48, 17) + FIntPoint(8, 8), View);
    Model.MouseMove(FIntPoint(66, 17) + FIntPoint(8, 8), View);
    const TArray<FHudContainerClick> Clicks = Model.MouseUp(FIntPoint(66, 17) + FIntPoint(8, 8), 0, View);
    TArray<int32> Slots;
    for (const FHudContainerClick& Click : Clicks)
        if (Click.Slot != FFoundationContainerMenu::Outside) Slots.Add(Click.Slot);
    TestEqual(TEXT("drag clicks"), Clicks.Num(), 5);
    TestTrue(TEXT("drag slots 1, 2, 3"), Slots == TArray<int32>{1, 2, 3});

    // The inventory's model is unchanged (step 5b's positions).
    FHudContainerModel Inventory;
    TestEqual(TEXT("inventory result"), Inventory.GetPosition(0), FIntPoint(154, 28));
    TestEqual(TEXT("inventory offhand"), Inventory.GetPosition(45), FIntPoint(77, 62));
    TestEqual(TEXT("inventory slots"), Inventory.GetSlots(), 46);
    return true;
}

#endif
